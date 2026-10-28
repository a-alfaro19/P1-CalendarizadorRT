#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "host/runtime.h"
#include "test_harness.h"

static void queue_contention(bool persistent)
{
    shared_memory_t memory;
    char name[128];
    snprintf(name, sizeof(name), "/runtime_retry_%ld", (long)getpid());
    assert(shared_memory_create(&memory, name) == 0);
    HostConfig config = {.algorithm = SCHED_EDF, .heuristic = HEURISTIC_FIRST_FIT,
        .tick_ms = 10, .tick_report_period = 100, .max_missed_reports = 10,
        .command_timeout_ms = 300};
    NodeInfo node = {.id = 1, .speed_ut_per_tick = 100, .active = 1};
    PlannerState planner;
    assert(planner_init(&planner, &node, 1, &config) == 0);
    shared_system_config_t shared = {0};
    shared.nodes[0].node_id = 1;
    shared.nodes[1].node_id = -1;
    assert(shared_memory_write_config(&memory, &shared) == 0);
    assert(shared_memory_set_node_active(&memory, 1, true) == 0);
    /* Simula un consumidor que mantiene el semáforo mientras llega un despliegue. */
    assert(sem_wait(&memory.data->commands[0].mutex) == 0);
    ipc_message_t add = {.type = IPC_ADD_TASK};
    add.payload.task = (Task){1, TASK_PERIODIC, 100, 10, 10, 1};
    assert(shared_memory_send(&memory, SHARED_QUEUE_CONTROL, 0, &add, 1000) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child)
        _exit(host_runtime_loop(&memory, &config, &planner) == 0 ? 0 : 1);
    ipc_message_t message;
    assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &message, 2000) == 0);
    assert(message.type == IPC_ASSIGNMENT && message.payload.assignment.accepted);
    if (persistent)
    {
        assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &message, 2000) == 0);
        assert(message.type == IPC_NODE_DOWN);
    }
    else
    {
        struct timespec pause = {.tv_nsec = 80000000};
        nanosleep(&pause, NULL);
        shared_node_status_t status;
        assert(shared_memory_read_node_status(&memory, 1, &status) == 0 && status.active);
    }
    assert(sem_post(&memory.data->commands[0].mutex) == 0);
    if (!persistent)
    {
        assert(shared_memory_receive(&memory, SHARED_QUEUE_COMMAND, 1, &message, 2000) == 0);
        assert(message.type == IPC_DEPLOY);
        message.type = IPC_ACK;
        message.payload.ack = (msg_ack_t){MSG_TASK_DEPLOY, MSG_ACK_OK};
        assert(shared_memory_send(&memory, SHARED_QUEUE_RESPONSE, 0, &message, 1000) == 0);
        assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &message, 2000) == 0);
        assert(message.type == IPC_ASSIGNMENT && message.payload.assignment.confirmed);
    }
    message = (ipc_message_t){.type = IPC_SHUTDOWN};
    assert(shared_memory_send(&memory, SHARED_QUEUE_CONTROL, 0, &message, 1000) == 0);
    if (!persistent)
    {
        assert(shared_memory_receive(&memory, SHARED_QUEUE_COMMAND, 1, &message, 2000) == 0);
        assert(message.type == IPC_STOP);
        message.type = IPC_ACK;
        message.payload.ack = (msg_ack_t){MSG_STOP, MSG_ACK_OK};
        assert(shared_memory_send(&memory, SHARED_QUEUE_RESPONSE, 0, &message, 1000) == 0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(shared_memory_destroy(&memory) == 0);
}
static void test_transient_contention_retries(void) { queue_contention(false); }
static void test_persistent_contention_is_bounded(void) { queue_contention(true); }
int main(void)
{
    RUN_TEST(test_transient_contention_retries);
    RUN_TEST(test_persistent_contention_is_bounded);
    return 0;
}
