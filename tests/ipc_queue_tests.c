#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include "host/shared_memory.h"
#include "test_harness.h"

static shared_memory_t memory;
static char name[128];

static void test_capacity_fifo_and_timeouts(void)
{
    ipc_message_t m = {.type = IPC_EVENT, .node_id = 1}, out = {.request_id = 999};
    assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &out, 0) == -1);
    assert(errno == EAGAIN && out.request_id == 999);
    assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &out, 20) == -1);
    assert(errno == ETIMEDOUT && out.request_id == 999);
    for (int cycle = 0; cycle < 3; cycle++) {
        for (unsigned i = 0; i < IPC_QUEUE_CAPACITY; i++) {
            m.request_id = i;
            assert(shared_memory_send(&memory, SHARED_QUEUE_EVENT, 0, &m, 0) == 0);
        }
        assert(shared_memory_send(&memory, SHARED_QUEUE_EVENT, 0, &m, 0) == -1);
        assert(errno == EAGAIN);
        assert(shared_memory_send(&memory, SHARED_QUEUE_EVENT, 0, &m, 20) == -1);
        assert(errno == ETIMEDOUT);
        for (unsigned i = 0; i < IPC_QUEUE_CAPACITY; i++) {
            assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &out, 0) == 0);
            assert(out.request_id == i);
        }
    }
    /* Failing to acquire mutex must restore the reserved slot/item. */
    assert(sem_wait(&memory.data->events.mutex) == 0);
    assert(shared_memory_send(&memory, SHARED_QUEUE_EVENT, 0, &m, 10) == -1);
    assert(errno == ETIMEDOUT);
    assert(sem_post(&memory.data->events.mutex) == 0);
    assert(shared_memory_send(&memory, SHARED_QUEUE_EVENT, 0, &m, 0) == 0);
    assert(sem_wait(&memory.data->events.mutex) == 0);
    assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &out, 10) == -1);
    assert(errno == ETIMEDOUT);
    assert(sem_post(&memory.data->events.mutex) == 0);
    assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &out, 0) == 0);
}

static void test_validation_and_channel_isolation(void)
{
    ipc_message_t m = {.type = IPC_CALIBRATE, .node_id = 1}, out;
    assert(shared_memory_send(&memory, SHARED_QUEUE_COMMAND, 2, &m, 0) == -1);
    assert(errno == EINVAL);
    assert(shared_memory_send(&memory, SHARED_QUEUE_COMMAND, 0, &m, 0) == -1);
    assert(shared_memory_send(&memory, SHARED_QUEUE_RESPONSE, 1, &m, 0) == -1);
    assert(shared_memory_send(&memory, SHARED_QUEUE_EVENT, 0, NULL, 0) == -1);
    assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &out, -2) == -1);
    assert(shared_memory_send(&memory, SHARED_QUEUE_COMMAND, 1, &m, 0) == 0);
    assert(shared_memory_receive(&memory, SHARED_QUEUE_COMMAND, 2, &out, 0) == -1);
    assert(errno == EAGAIN);
    assert(shared_memory_receive(&memory, SHARED_QUEUE_RESPONSE, 0, &out, 0) == -1);
    assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &out, 0) == -1);
    assert(shared_memory_receive(&memory, SHARED_QUEUE_COMMAND, 1, &out, 0) == 0);
}

static void test_process_roundtrip_and_multiple_producers(void)
{
    pid_t children[SHARED_NUM_NODES];
    for (int node = 1; node <= SHARED_NUM_NODES; node++) {
        children[node - 1] = fork();
        assert(children[node - 1] >= 0);
        if (children[node - 1] == 0) {
            shared_memory_t peer;
            assert(shared_memory_open(&peer, name) == 0);
            ipc_message_t command;
            assert(shared_memory_receive(&peer, SHARED_QUEUE_COMMAND, node, &command, -1) == 0);
            assert(command.type == IPC_CALIBRATE && command.payload.calibrate.tick_ms == 10);
            ipc_message_t reply = {.type = IPC_CALIBRATED, .node_id = node,
                                   .request_id = command.request_id};
            reply.payload.calibrated.ut_per_tick = 20 + node;
            assert(shared_memory_send(&peer, SHARED_QUEUE_RESPONSE, 0, &reply, -1) == 0);
            for (unsigned i = 0; i < 512; i++) {
                ipc_message_t event = {.type = IPC_EVENT, .node_id = node, .request_id = i};
                assert(shared_memory_send(&peer, SHARED_QUEUE_EVENT, 0, &event, -1) == 0);
            }
            assert(shared_memory_close(&peer) == 0);
            _exit(0);
        }
    }
    for (int node = 1; node <= SHARED_NUM_NODES; node++) {
        ipc_message_t command = {.type = IPC_CALIBRATE, .node_id = node, .request_id = (unsigned)node};
        command.payload.calibrate.tick_ms = 10;
        assert(shared_memory_send(&memory, SHARED_QUEUE_COMMAND, node, &command, -1) == 0);
    }
    unsigned replies = 0;
    for (int i = 0; i < SHARED_NUM_NODES; i++) {
        ipc_message_t reply;
        assert(shared_memory_receive(&memory, SHARED_QUEUE_RESPONSE, 0, &reply, -1) == 0);
        assert(reply.request_id == (unsigned)reply.node_id);
        assert(reply.payload.calibrated.ut_per_tick == (unsigned)(20 + reply.node_id));
        replies |= 1u << (reply.node_id - 1);
    }
    assert(replies == (1u << SHARED_NUM_NODES) - 1);
    unsigned counts[SHARED_NUM_NODES] = {0};
    for (int i = 0; i < 512 * SHARED_NUM_NODES; i++) {
        ipc_message_t event;
        assert(shared_memory_receive(&memory, SHARED_QUEUE_EVENT, 0, &event, -1) == 0);
        assert(event.node_id >= 1 && event.node_id <= SHARED_NUM_NODES);
        assert(event.request_id == counts[event.node_id - 1]++);
    }
    for (int i = 0; i < SHARED_NUM_NODES; i++) {
        int status;
        assert(counts[i] == 512);
        assert(waitpid(children[i], &status, 0) == children[i]);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}

int main(void)
{
    alarm(15); /* Fail boundedly if an IPC regression deadlocks. */
    snprintf(name, sizeof(name), "/ipc_queue_tests_%ld", (long)getpid());
    assert(shared_memory_create(&memory, name) == 0);
    RUN_TEST(test_capacity_fifo_and_timeouts);
    RUN_TEST(test_validation_and_channel_isolation);
    RUN_TEST(test_process_roundtrip_and_multiple_producers);
    assert(shared_memory_destroy(&memory) == 0);
    assert(shared_memory_open(&memory, name) == -1 && errno == ENOENT);
    alarm(0);
    return 0;
}
