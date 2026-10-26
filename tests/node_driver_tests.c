#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "host/node_driver.h"
#include "host/scheduler_runtime.h"
#include "protocol/protocol.h"
#include "test_harness.h"

static shared_memory_t memory;
static char name[128];
static const node_driver_config_t settings = {10, ALGORITHM_EDF, 10};
typedef struct { int master; int keeper; pid_t pid; } driver_t;

static void setup(void)
{
    snprintf(name, sizeof(name), "/driver_tests_%ld", (long)getpid());
    assert(shared_memory_create(&memory, name) == 0);
    shared_system_config_t config = {0};
    config.nodes[0].node_id = 1;
    config.nodes[1].node_id = 2;
    assert(shared_memory_write_config(&memory, &config) == 0);
}
static driver_t launch_driver(int node)
{
    driver_t driver;
    driver.master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    assert(driver.master >= 0);
    assert(grantpt(driver.master) == 0 && unlockpt(driver.master) == 0);
    char path[128];
    assert(ptsname_r(driver.master, path, sizeof(path)) == 0);
    /* Mantener el esclavo abierto evita EIO en el maestro durante el arranque. */
    driver.keeper = open(path, O_RDWR | O_NOCTTY);
    assert(driver.keeper >= 0);
    driver.pid = fork();
    assert(driver.pid >= 0);
    if (!driver.pid) {
        close(driver.master);
        close(driver.keeper);
        node_driver_run(node, path, shared_memory_tx_ring_buffer(&memory, node), name, &settings);
        _exit(1);
    }
    return driver;
}
static void join_success(pid_t pid)
{
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static void stop_driver(driver_t driver, int node)
{
    ipc_message_t shutdown = {.type = IPC_SHUTDOWN, .node_id = node};
    assert(shared_memory_send(&memory, SHARED_QUEUE_COMMAND, node, &shutdown, 1000) == 0);
    join_success(driver.pid);
    close(driver.master);
    close(driver.keeper);
}
static protocol_frame_t read_frame(int fd)
{
    protocol_parser_t parser;
    protocol_parser_init(&parser);
    protocol_frame_t frame;
    for (;;) {
        struct pollfd p = {.fd = fd, .events = POLLIN};
        assert(poll(&p, 1, 2000) == 1 && (p.revents & POLLIN));
        uint8_t byte;
        assert(read(fd, &byte, 1) == 1);
        int rc = protocol_parser_feed(&parser, byte, &frame);
        assert(rc >= 0);
        if (rc == PROTOCOL_FRAME_READY) return frame;
    }
}
static void write_frame(int fd, int type, const uint8_t *load, size_t length, int corrupt)
{
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE];
    int len = protocol_frame_build(frame, sizeof(frame), type, load, length);
    assert(len > 0);
    if (corrupt) frame[len - 1] ^= 0xff;
    assert(write(fd, frame, (size_t)len) == len);
}
static void calibration(int fd, uint32_t speed, int corrupt)
{
    msg_calib_resp_t response = {speed};
    uint8_t load[LOAD_SIZE_CALIB_RESP];
    assert(msg_calib_resp_pack(&response, load, sizeof(load)) == sizeof(load));
    write_frame(fd, MSG_CALIB_RESP, load, sizeof(load), corrupt);
}
static void ack_frame(int fd, int type, int status)
{
    msg_ack_t ack = {type, status};
    uint8_t load[LOAD_SIZE_ACK];
    assert(msg_ack_pack(&ack, load, sizeof(load)) == sizeof(load));
    write_frame(fd, MSG_ACK, load, sizeof(load), 0);
}
static ipc_message_t response(void)
{
    ipc_message_t message;
    assert(shared_memory_receive(&memory, SHARED_QUEUE_RESPONSE, 0, &message, 2000) == 0);
    return message;
}
static void send_command(ipc_message_t message)
{
    assert(shared_memory_send(&memory, SHARED_QUEUE_COMMAND, message.node_id, &message, 1000) == 0);
}

static void test_driver_ipc_protocol_and_corruption(void)
{
    setup();
    driver_t driver = launch_driver(1);
    struct pollfd p = {.fd = driver.master, .events = POLLIN};
    assert(poll(&p, 1, 50) == 0);
    ipc_message_t request = {.type = IPC_CALIBRATE, .node_id = 1, .request_id = 42};
    request.payload.calibrate.tick_ms = 10;
    send_command(request);
    protocol_frame_t frame = read_frame(driver.master);
    assert(frame.type == MSG_CALIB_REQ);
    calibration(driver.master, 99, 1);
    ack_frame(driver.master, MSG_START, MSG_ACK_OK);
    ipc_message_t reply;
    assert(shared_memory_receive(&memory, SHARED_QUEUE_RESPONSE, 0, &reply, 50) == -1);
    assert(errno == ETIMEDOUT);
    calibration(driver.master, 23, 0);
    reply = response();
    assert(reply.type == IPC_CALIBRATED && reply.request_id == 42);
    assert(reply.payload.calibrated.ut_per_tick == 23);
    assert(poll(&p, 1, 50) == 0);
    request = (ipc_message_t){.type = IPC_DEPLOY, .node_id = 1, .request_id = 43};
    request.payload.deploy = (msg_task_deploy_t){7, TASK_KIND_PERIODIC, 2, 10, 10};
    send_command(request);
    frame = read_frame(driver.master);
    assert(frame.type == MSG_TASK_DEPLOY);
    ack_frame(driver.master, MSG_TASK_DEPLOY, MSG_ACK_NO_SPACE);
    reply = response();
    assert(reply.type == IPC_ACK && reply.request_id == 43 && reply.payload.ack.status == MSG_ACK_NO_SPACE);
    request = (ipc_message_t){.type = IPC_START, .node_id = 1, .request_id = 44};
    request.payload.start = (msg_start_t){ALGORITHM_EDF, 10};
    send_command(request);
    frame = read_frame(driver.master);
    assert(frame.type == MSG_START);
    ack_frame(driver.master, MSG_START, MSG_ACK_OK);
    reply = response();
    assert(reply.request_id == 44);
    msg_event_t event = {.tick = 4, .event_type = EVENT_COMPLETE, .task_id = 7, .aux = 2};
    uint8_t load[LOAD_SIZE_EVENT];
    assert(msg_event_pack(&event, load, sizeof(load)) == sizeof(load));
    write_frame(driver.master, MSG_EVENT, load, sizeof(load), 0);
    assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &reply, 2000) == 0);
    assert(reply.type == IPC_EVENT && reply.payload.event.tick == 4);
    stop_driver(driver, 1);
    assert(shared_memory_destroy(&memory) == 0);
}

static void run_two_serial_drivers(int failure_mode)
{
    setup();
    driver_t drivers[2] = {launch_driver(1), launch_driver(2)};
    pid_t scheduler = fork();
    assert(scheduler >= 0);
    if (!scheduler) {
        for (int i = 0; i < 2; i++) { close(drivers[i].master); close(drivers[i].keeper); }
        HostConfig config = {.algorithm = SCHED_EDF, .heuristic = HEURISTIC_FIRST_FIT,
                             .tick_ms = 10, .tick_report_period = 10};
        Task tasks[] = {{1, TASK_PERIODIC, 100, 10, 10, 1},
                        {2, TASK_PERIODIC, 100, 10, 10, 1},
                        {3, TASK_PERIODIC, 100, 10, 10, 1}};
        PlannerState planner;
        int rc = scheduler_bootstrap(&memory, &config, tasks, 3, 2, 500, &planner);
        if (failure_mode) {
            assert(rc == -1);
            assert(errno == (failure_mode == 1 ? EPROTO : ETIMEDOUT));
        } else assert(rc == 0);
        _exit(0);
    }
    for (int i = 0; i < 2; i++) {
        protocol_frame_t frame = read_frame(drivers[i].master);
        assert(frame.type == MSG_CALIB_REQ);
        calibration(drivers[i].master, i ? 20 : 10, 0);
    }
    int node_order[] = {0, 1, 1};
    for (int i = 0; i < 3; i++) {
        int n = node_order[i];
        protocol_frame_t frame = read_frame(drivers[n].master);
        assert(frame.type == MSG_TASK_DEPLOY);
        msg_task_deploy_t deploy;
        assert(msg_task_deploy_unpack(frame.load, frame.load_len, &deploy) == PROTOCOL_OK);
        assert(deploy.cost_ticks == (n ? 5 : 10));
        ack_frame(drivers[n].master, MSG_TASK_DEPLOY, MSG_ACK_OK);
    }
    for (int i = 0; i < 2; i++) {
        protocol_frame_t frame = read_frame(drivers[i].master);
        assert(frame.type == MSG_START);
        if (!i || !failure_mode) ack_frame(drivers[i].master, MSG_START, MSG_ACK_OK);
        else if (failure_mode == 1) ack_frame(drivers[i].master, MSG_START, MSG_ACK_INVALID);
    }
    if (failure_mode) {
        for (int i = 0; i < 2; i++) {
            protocol_frame_t frame = read_frame(drivers[i].master);
            assert(frame.type == MSG_STOP);
            /* Un START tardío no debe confundirse con la respuesta de STOP. */
            ack_frame(drivers[i].master, MSG_START, MSG_ACK_OK);
            ack_frame(drivers[i].master, MSG_STOP, MSG_ACK_OK);
        }
    }
    join_success(scheduler);
    startup_status_t startup;
    assert(shared_memory_read_startup(&memory, &startup) == 0);
    if (failure_mode) {
        assert(startup.phase == STARTUP_FAILED && startup.failed_phase == STARTUP_STARTING);
        assert(!startup.timing_valid);
        for (int i = 0; i < 2; i++) assert(startup.nodes[i].stop_confirmed);
    } else {
        assert(startup.phase == STARTUP_RUNNING && startup.timing_valid);
        for (int i = 0; i < 2; i++) {
            startup_timing_t timing = startup.nodes[i].start;
            assert(timing.tx_begin_ns >= startup.target_start_ns);
            assert(timing.tx_end_ns >= timing.tx_begin_ns && timing.ack_ns >= timing.tx_end_ns);
        }
        assert(startup.zero_delta_min_ns <= startup.zero_delta_max_ns);
    }
    for (int i = 0; i < 2; i++) stop_driver(drivers[i], i + 1);
    assert(shared_memory_destroy(&memory) == 0);
}
static void test_planner_with_two_serial_drivers(void) { run_two_serial_drivers(0); }
static void test_partial_start_rejected_stops_both_nodes(void) { run_two_serial_drivers(1); }
static void test_missing_start_ack_stops_both_nodes(void) { run_two_serial_drivers(2); }

static void test_stop_interrupts_calibration_wait(void)
{
    setup();
    driver_t driver = launch_driver(1);
    ipc_message_t request = {.type = IPC_CALIBRATE, .node_id = 1, .request_id = 1};
    request.payload.calibrate.tick_ms = 10;
    send_command(request);
    protocol_frame_t frame = read_frame(driver.master);
    assert(frame.type == MSG_CALIB_REQ);
    request = (ipc_message_t){.type = IPC_STOP, .node_id = 1, .request_id = 2};
    send_command(request);
    frame = read_frame(driver.master);
    assert(frame.type == MSG_STOP);
    calibration(driver.master, 30, 0);
    ack_frame(driver.master, MSG_STOP, MSG_ACK_OK);
    ipc_message_t reply = response();
    assert(reply.type == IPC_ACK && reply.request_id == 2 && reply.payload.ack.acked_type == MSG_STOP);
    stop_driver(driver, 1);
    assert(shared_memory_destroy(&memory) == 0);
}

static void test_unavailable_port_allows_abort_coordination(void)
{
    setup();
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
        node_driver_run(1, "/dev/null", shared_memory_tx_ring_buffer(&memory, 1), name, &settings);
        _exit(0);
    }
    ipc_message_t reply = response();
    assert(reply.type == IPC_NODE_DOWN && reply.node_id == 1);
    assert(kill(pid, 0) == 0);
    ipc_message_t stop = {.type = IPC_STOP, .node_id = 1, .request_id = 10};
    send_command(stop);
    reply = response();
    assert(reply.type == IPC_ERROR && reply.request_id == 10);
    stop.type = IPC_SHUTDOWN;
    send_command(stop);
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE);
    assert(shared_memory_destroy(&memory) == 0);
}

int main(void)
{
    alarm(20);
    RUN_TEST(test_driver_ipc_protocol_and_corruption);
    RUN_TEST(test_planner_with_two_serial_drivers);
    RUN_TEST(test_partial_start_rejected_stops_both_nodes);
    RUN_TEST(test_missing_start_ack_stops_both_nodes);
    RUN_TEST(test_stop_interrupts_calibration_wait);
    RUN_TEST(test_unavailable_port_allows_abort_coordination);
    return 0;
}
