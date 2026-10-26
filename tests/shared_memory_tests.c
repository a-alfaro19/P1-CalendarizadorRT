#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "host/shared_memory.h"
#include "test_harness.h"

static shared_memory_t owner;
static shared_memory_t peer;
static char shared_memory_name[SHARED_MEMORY_NAME_MAX];

static void test_shared_memory_task_access(void)
{
    shared_system_config_t config = {0};
    config.nodes[0].node_id = 1;
    config.nodes[0].task_count = 1;
    config.nodes[0].tasks[0] = (task_config_t){
        .task_id = 7,
        .kind = 1,
        .cost_ticks = 3,
        .period = 20,
        .deadline = 15,
        .assigned = false,
    };

    assert(shared_memory_write_config(&owner, &config) == 0);
    assert(shared_memory_open(&peer, shared_memory_name) == 0);

    task_config_t task;
    assert(shared_memory_read_next_task(&peer, 1, &task) == 1);
    assert(task.task_id == 7);
    assert(task.cost_ticks == 3);
    assert(shared_memory_mark_task_assigned(&peer, 1, task.task_id) == 0);
    assert(shared_memory_read_next_task(&owner, 1, &task) == 0);

    assert(shared_memory_close(&peer) == 0);
}

static void test_shared_memory_runtime_status(void)
{
    assert(shared_memory_set_node_active(&owner, 1, true) == 0);
    assert(shared_memory_record_missed_report(&owner, 1) == 0);

    msg_tick_report_t report = {.tick = 123, .bad_frames = 4};
    assert(shared_memory_record_tick_report(&owner, 1, &report) == 0);

    msg_event_t event = {
        .tick = 124,
        .event_type = EVENT_COMPLETE,
        .task_id = 7,
        .aux = 5,
    };
    assert(shared_memory_record_event(&owner, 1, &event) == 0);
    event.event_type = EVENT_DEADLINE_MISS;
    assert(shared_memory_record_event(&owner, 1, &event) == 0);
    event.event_type = EVENT_SPORADIC_IGNORED;
    assert(shared_memory_record_event(&owner, 1, &event) == 0);

    shared_node_status_t status;
    assert(shared_memory_read_node_status(&owner, 1, &status) == 0);
    assert(status.active);
    assert(status.last_tick == 123);
    assert(status.bad_frames == 4);
    assert(status.missed_reports == 0);
    assert(status.completed_tasks == 1);
    assert(status.missed_deadlines == 1);
    assert(status.ignored_sporadic_triggers == 1);
    assert(status.has_last_event);
    assert(status.last_event.event_type == EVENT_SPORADIC_IGNORED);
}

static void test_shared_memory_rejects_invalid_task_count(void)
{
    shared_system_config_t invalid_config = {0};
    invalid_config.nodes[0].node_id = 1;
    invalid_config.nodes[0].task_count = SHARED_MAX_TASKS_PER_NODE + 1;

    errno = 0;
    assert(shared_memory_write_config(&owner, &invalid_config) == -1);
    assert(errno == EINVAL);
}

int main(void)
{
    snprintf(shared_memory_name,
             sizeof(shared_memory_name),
             "/shared_memory_tests_%ld",
             (long)getpid());
    assert(shared_memory_create(&owner, shared_memory_name) == 0);

    puts("Shared Memory");
    RUN_TEST(test_shared_memory_task_access);
    RUN_TEST(test_shared_memory_runtime_status);
    RUN_TEST(test_shared_memory_rejects_invalid_task_count);

    assert(shared_memory_destroy(&owner) == 0);
    puts("OK: All shared memory tests passed!");
    return 0;
}
