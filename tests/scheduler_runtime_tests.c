#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include "host/scheduler_runtime.h"
#include "host/scheduler/admission.h"
#include "test_harness.h"

static shared_memory_t memory;
static HostConfig config = {.algorithm = SCHED_EDF, .heuristic = HEURISTIC_FIRST_FIT,
                             .tick_ms = 10, .tick_report_period = 10};
static Task tasks[] = {{1, TASK_PERIODIC, 100, 10, 10, 1},
                       {2, TASK_PERIODIC, 100, 10, 10, 1},
                       {3, TASK_PERIODIC, 100, 10, 10, 1}};
static void create_memory(void)
{
    char name[128];
    snprintf(name, sizeof(name), "/scheduler_test_%ld", (long)getpid());
    assert(shared_memory_create(&memory, name) == 0);
}
static ipc_message_t command(int node)
{
    ipc_message_t message;
    assert(shared_memory_receive(&memory, SHARED_QUEUE_COMMAND, node, &message, 2000) == 0);
    return message;
}
static void respond(ipc_message_t message)
{
    assert(shared_memory_send(&memory, SHARED_QUEUE_RESPONSE, 0, &message, 1000) == 0);
}
static void ack(ipc_message_t request, int status)
{
    request.payload.ack = (msg_ack_t){request.type == IPC_DEPLOY ? MSG_TASK_DEPLOY : MSG_START, status};
    request.type = IPC_ACK;
    respond(request);
}
static void joined(pid_t pid)
{
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void test_measured_speeds_and_ack_barrier(void)
{
    create_memory();
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
        PlannerState planner;
        assert(scheduler_bootstrap(&memory, &config, tasks, 3, 2, 2000, &planner) == 0);
        assert(planner.nodes[0].info.speed_ut_per_tick == 10);
        assert(planner.nodes[1].info.speed_ut_per_tick == 20);
        assert(planner.nodes[0].task_count == 1 && planner.nodes[1].task_count == 2);
        _exit(0);
    }
    ipc_message_t first = command(1), second = command(2), extra;
    assert(first.type == IPC_CALIBRATE && second.type == IPC_CALIBRATE);
    assert(first.payload.calibrate.tick_ms == 10);
    second.type = IPC_CALIBRATED;
    second.payload.calibrated.ut_per_tick = 20;
    respond(second);
    assert(shared_memory_receive(&memory, SHARED_QUEUE_COMMAND, 2, &extra, 50) == -1);
    assert(errno == ETIMEDOUT);
    first.type = IPC_CALIBRATED;
    first.payload.calibrated.ut_per_tick = 10;
    respond(first);
    int nodes[] = {1, 2, 2};
    int ids[] = {3, 1, 2};
    for (int i = 0; i < 3; i++) {
        ipc_message_t deploy = command(nodes[i]);
        assert(deploy.type == IPC_DEPLOY && deploy.payload.deploy.task_id == ids[i]);
        assert(deploy.payload.deploy.cost_ticks == (nodes[i] == 1 ? 10 : 5));
        shared_system_config_t stored;
        assert(shared_memory_read_config(&memory, &stored) == 0);
        int index = i == 2 ? 1 : 0;
        assert(!stored.nodes[nodes[i] - 1].tasks[index].assigned);
        ipc_message_t stale = deploy;
        stale.request_id += 100;
        ack(stale, MSG_ACK_OK);
        assert(shared_memory_receive(&memory, SHARED_QUEUE_COMMAND, nodes[i], &extra, 30) == -1);
        ack(deploy, MSG_ACK_OK);
    }
    first = command(1);
    second = command(2);
    assert(first.type == IPC_START && second.type == IPC_START);
    shared_system_config_t stored;
    assert(shared_memory_read_config(&memory, &stored) == 0);
    for (int n = 0; n < 2; n++)
        for (int t = 0; t < stored.nodes[n].task_count; t++) assert(stored.nodes[n].tasks[t].assigned);
    ack(second, MSG_ACK_OK);
    ack(first, MSG_ACK_OK);
    joined(pid);
    for (int i = 0; i < 3; i++) {
        assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &extra, 0) == 0);
        assert(extra.type == IPC_ASSIGNMENT && extra.payload.assignment.accepted);
    }
    assert(shared_memory_destroy(&memory) == 0);
}

static void failure_case(int mode)
{
    create_memory();
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
        PlannerState planner;
        assert(scheduler_bootstrap(&memory, &config, tasks, 1, 1, 200, &planner) == -1);
        assert(errno == (mode == 0 ? ETIMEDOUT : mode == 1 ? ERANGE : EPROTO));
        _exit(0);
    }
    ipc_message_t request = command(1);
    if (mode) {
        request.type = IPC_CALIBRATED;
        request.payload.calibrated.ut_per_tick = mode == 1 ? 0 : 20;
        respond(request);
        if (mode == 2) {
            request = command(1);
            assert(request.type == IPC_DEPLOY);
            ack(request, MSG_ACK_NO_SPACE);
        }
    }
    joined(pid);
    if (mode == 2) {
        shared_system_config_t stored;
        assert(shared_memory_read_config(&memory, &stored) == 0);
        assert(!stored.nodes[0].tasks[0].assigned);
    }
    assert(shared_memory_receive(&memory, SHARED_QUEUE_COMMAND, 1, &request, 0) == -1);
    assert(errno == EAGAIN);
    assert(shared_memory_destroy(&memory) == 0);
}
static void test_missing_calibration(void) { failure_case(0); }
static void test_invalid_calibration(void) { failure_case(1); }
static void test_rejected_deployment(void) { failure_case(2); }

static void test_hardware_admission_limits(void)
{
    PlannerState planner;
    NodeInfo node = {1, INT_MAX, 1};
    assert(planner_init(&planner, &node, 1, &config) == 0);
    Task task = {0, TASK_PERIODIC, INT_MAX, 100, 100, 0};
    assert(task_cost_ticks(&task, INT_MAX) == 1);
    TaskAssignment assignment;
    task.id = 255;
    assert(planner_add_task(&planner, &task, &assignment) == 0);
    for (int i = 0; i < 8; i++) {
        task.id = i;
        assert(planner_add_task(&planner, &task, &assignment) == 1);
    }
    task.id = 8;
    assert(planner_add_task(&planner, &task, &assignment) == 0);
    assert(planner_init(&planner, &node, 1, &config) == 0);
    task.type = TASK_SPORADIC;
    assert(planner_add_task(&planner, &task, &assignment) == 1);
    task.id++;
    assert(planner_add_task(&planner, &task, &assignment) == 0);
}
int main(void)
{
    alarm(15);
    RUN_TEST(test_measured_speeds_and_ack_barrier);
    RUN_TEST(test_missing_calibration);
    RUN_TEST(test_invalid_calibration);
    RUN_TEST(test_rejected_deployment);
    RUN_TEST(test_hardware_admission_limits);
    return 0;
}
