#define _POSIX_C_SOURCE 200809L
#include "host/scheduler_runtime.h"
#include "host/runtime.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int64_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int publish_startup(shared_memory_t *memory, startup_status_t *startup,
                           startup_phase_t phase, int timeout_ms)
{
    startup->phase = phase;
    if (shared_memory_write_startup(memory, startup) < 0)
        return -1;
    ipc_message_t event = {.type = IPC_STARTUP};
    event.payload.startup = *startup;
    return shared_memory_send(memory, SHARED_QUEUE_EVENT, 0, &event, timeout_ms);
}

static int send_command(shared_memory_t *memory, ipc_message_t *command,
                        uint32_t *sequence, int timeout_ms)
{
    command->request_id = ++*sequence;
    return shared_memory_send(memory, SHARED_QUEUE_COMMAND, command->node_id,
                              command, timeout_ms);
}

static int await_responses(shared_memory_t *memory, const ipc_message_t *commands,
                           int count, ipc_message_t *responses, int timeout_ms,
                           startup_status_t *startup)
{
    bool received[SHARED_NUM_NODES] = {0};
    int64_t now = now_ms();
    if (now < 0)
        return -1;
    int64_t deadline = now + timeout_ms;
    int remaining = count;
    while (remaining)
    {
        for (int i = 0; i < count; i++)
        {
            if (!received[i])
            {
                startup->failed_node = commands[i].node_id;
                startup->failed_request = commands[i].request_id;
                break;
            }
        }
        now = now_ms();
        if (now < 0)
            return -1;
        if (now >= deadline)
        {
            errno = ETIMEDOUT;
            return -1;
        }
        ipc_message_t message;
        if (shared_memory_receive(memory, SHARED_QUEUE_RESPONSE, 0, &message,
                                  (int)(deadline - now)) < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (message.type == IPC_NODE_DOWN || message.type == IPC_ERROR)
        {
            fprintf(stderr, "[Planificador] Error del nodo %d: %.128s\n",
                    message.node_id, message.payload.error.reason);
            startup->failed_node = message.node_id;
            startup->failed_request = message.request_id;
            errno = EIO;
            return -1;
        }
        for (int i = 0; i < count; i++)
        {
            if (received[i] || message.node_id != commands[i].node_id ||
                message.request_id != commands[i].request_id)
                continue;
            startup->failed_node = message.node_id;
            startup->failed_request = message.request_id;
            if (commands[i].type == IPC_CALIBRATE)
            {
                if (message.type != IPC_CALIBRATED)
                    continue;
                if (!message.payload.calibrated.ut_per_tick ||
                    message.payload.calibrated.ut_per_tick > INT_MAX)
                {
                    errno = ERANGE;
                    return -1;
                }
            }
            else
            {
                uint8_t expected = commands[i].type == IPC_DEPLOY ? MSG_TASK_DEPLOY : MSG_START;
                if (message.type != IPC_ACK || message.payload.ack.acked_type != expected)
                    continue;
                if (message.payload.ack.status != MSG_ACK_OK)
                {
                    fprintf(stderr, "[Planificador] Nodo %d rechazó la orden %u (estado %u)\n",
                            message.node_id, message.request_id, message.payload.ack.status);
                    errno = EPROTO;
                    return -1;
                }
            }
            if (commands[i].type == IPC_START)
            {
                startup_timing_t t = message.timing;
                if (!t.tx_begin_ns || t.tx_begin_ns < commands[i].not_before_ns || t.tx_end_ns < t.tx_begin_ns || t.ack_ns < t.tx_end_ns ||
                    t.ack_ns > INT64_MAX)
                {
                    errno = EPROTO;
                    return -1;
                }
                startup->nodes[message.node_id - 1].start_confirmed = true;
                startup->nodes[message.node_id - 1].start = t;
                if (shared_memory_write_startup(memory, startup) < 0)
                    return -1;
            }
            responses[i] = message;
            received[i] = true;
            remaining--;
            break;
        }
    }
    startup->failed_node = 0;
    startup->failed_request = 0;
    return 0;
}

static int report_assignment(shared_memory_t *memory, const TaskAssignment *assignment,
                             const Task *task, int timeout_ms)
{
    ipc_message_t event = {.type = IPC_ASSIGNMENT,
                           .node_id = assignment->accepted ? assignment->node_id : 0};
    event.payload.assignment.task_id = assignment->task_id;
    event.payload.assignment.accepted = assignment->accepted;
    event.payload.assignment.cost_ticks = assignment->cost_ticks;
    ipc_assignment_set_task(&event, task);
    snprintf(event.payload.assignment.reason, IPC_REASON_LENGTH, "%s", assignment->reason);
    return shared_memory_send(memory, SHARED_QUEUE_EVENT, 0, &event, timeout_ms);
}

static int bootstrap_steps(shared_memory_t *memory, const HostConfig *config,
                           const Task *tasks, int task_count, int node_count,
                           int timeout_ms, PlannerState *planner, startup_status_t *startup)
{
    if (!memory || !config || !tasks || !planner || task_count < 0 ||
        task_count > MAX_TASKS || node_count < 1 || node_count > SHARED_NUM_NODES ||
        timeout_ms <= 0 || config->tick_ms <= 0 || config->tick_ms > UINT16_MAX ||
        config->tick_report_period <= 0 || config->tick_report_period > UINT16_MAX)
    {
        errno = EINVAL;
        return -1;
    }
    uint32_t sequence = 0;
    ipc_message_t commands[SHARED_NUM_NODES] = {0}, responses[SHARED_NUM_NODES];
    NodeInfo nodes[SHARED_NUM_NODES];
    for (int i = 0; i < node_count; i++)
    {
        commands[i].type = IPC_CALIBRATE;
        commands[i].node_id = i + 1;
        commands[i].payload.calibrate.tick_ms = (uint16_t)config->tick_ms;
        if (send_command(memory, &commands[i], &sequence, timeout_ms) < 0)
            return -1;
    }
    if (await_responses(memory, commands, node_count, responses, timeout_ms, startup) < 0)
        return -1;
    for (int i = 0; i < node_count; i++)
    {
        startup->nodes[i].speed_ut_per_tick = responses[i].payload.calibrated.ut_per_tick;
        nodes[i] = (NodeInfo){i + 1, (int)responses[i].payload.calibrated.ut_per_tick, 1};
        printf("[Planificador] Nodo %d: %d UT/tick medidas\n", i + 1, nodes[i].speed_ut_per_tick);
    }
    if (publish_startup(memory, startup, STARTUP_PLANNING, timeout_ms) < 0)
        return -1;
    if (planner_init(planner, nodes, node_count, config) < 0)
    {
        errno = EINVAL;
        return -1;
    }
    TaskAssignment assignments[MAX_TASKS];
    if (planner_load_initial_tasks(planner, tasks, task_count, assignments) < 0)
    {
        errno = EINVAL;
        return -1;
    }
    shared_system_config_t shared = {0};
    for (int i = 0; i < SHARED_NUM_NODES; i++)
        shared.nodes[i].node_id = -1;
    for (int i = 0; i < node_count; i++)
    {
        const NodePlan *node = &planner->nodes[i];
        shared.nodes[i].node_id = node->info.id;
        shared.nodes[i].task_count = node->task_count;
        startup->nodes[i].planned_tasks = (unsigned)node->task_count;
        if (node->task_count > SHARED_MAX_TASKS_PER_NODE)
        {
            errno = EOVERFLOW;
            return -1;
        }
        for (int j = 0; j < node->task_count; j++)
        {
            const Task *task = &node->tasks[j];
            shared.nodes[i].tasks[j] = (task_config_t){
                .task_id = (uint8_t)task->id, .kind = (uint8_t)task->type, .cost_ticks = (uint16_t)node->costs[j], .period = (uint16_t)task->period, .deadline = (uint16_t)task->deadline, .assigned = false};
        }
    }
    if (shared_memory_write_config(memory, &shared) < 0)
        return -1;
    if (publish_startup(memory, startup, STARTUP_DEPLOYING, timeout_ms) < 0)
        return -1;
    /* La admision y la confirmacion son estados distintos: assigned cambia tras el ACK */
    for (int i = 0; i < task_count; i++)
    {
        /* El planificador puede reordenar: la tarea se busca por id, no por posición */
        const Task *task = NULL;
        for (int j = 0; j < task_count; j++)
            if (tasks[j].id == assignments[i].task_id)
                task = &tasks[j];
        if (report_assignment(memory, &assignments[i], task, timeout_ms) < 0)
            return -1;
    }
    for (int i = 0; i < node_count; i++)
    {
        for (int j = 0; j < shared.nodes[i].task_count; j++)
        {
            task_config_t *task = &shared.nodes[i].tasks[j];
            ipc_message_t command = {.type = IPC_DEPLOY, .node_id = i + 1}, response;
            command.payload.deploy = (msg_task_deploy_t){task->task_id, task->kind,
                                                         task->cost_ticks, task->period, task->deadline};
            if (send_command(memory, &command, &sequence, timeout_ms) < 0 ||
                await_responses(memory, &command, 1, &response, timeout_ms, startup) < 0)
                return -1;
            if (shared_memory_mark_task_assigned(memory, i + 1, task->task_id) < 0)
                return -1;
            startup->nodes[i].confirmed_tasks++;
            if (shared_memory_write_startup(memory, startup) < 0)
                return -1;
        }
    }
    if (publish_startup(memory, startup, STARTUP_STARTING, timeout_ms) < 0)
        return -1;
    int64_t start_now = now_ms();
    if (start_now < 0)
        return -1;
    /* Dejar margen para que ambos controladores reciban el objetivo comun */
    startup->target_start_ns = (uint64_t)(start_now + 100) * 1000000u;
    if (shared_memory_write_startup(memory, startup) < 0)
        return -1;
    /* Ningun nodo recibe START antes de confirmar todo el despliegue */
    for (int i = 0; i < node_count; i++)
    {
        commands[i] = (ipc_message_t){.type = IPC_START, .node_id = i + 1, .not_before_ns = startup->target_start_ns};
        commands[i].payload.start = (msg_start_t){
            config->algorithm == SCHED_EDF ? ALGORITHM_EDF : ALGORITHM_RMS,
            (uint16_t)config->tick_report_period};
        if (send_command(memory, &commands[i], &sequence, timeout_ms) < 0)
            return -1;
    }
    if (await_responses(memory, commands, node_count, responses, timeout_ms, startup) < 0)
        return -1;
    startup->timing_valid = true;
    if (node_count == 2)
    {
        startup_timing_t a = startup->nodes[0].start, b = startup->nodes[1].start;
        startup->tx_skew_ns = a.tx_begin_ns > b.tx_begin_ns ? a.tx_begin_ns - b.tx_begin_ns : b.tx_begin_ns - a.tx_begin_ns;
        startup->zero_delta_min_ns = (int64_t)b.tx_begin_ns - (int64_t)a.ack_ns;
        startup->zero_delta_max_ns = (int64_t)b.ack_ns - (int64_t)a.tx_begin_ns;
    }
    if (publish_startup(memory, startup, STARTUP_RUNNING, timeout_ms) < 0)
        return -1;
    planner_print_state(planner);
    printf("[Planificador] Despliegue confirmado e inicio completado.\n");
    return 0;
}

static void abort_startup(shared_memory_t *memory, startup_status_t *startup, int timeout_ms)
{
    int budget = timeout_ms < 1000 ? timeout_ms : 1000;
    publish_startup(memory, startup, STARTUP_ABORTING, 0);
    bool finished[SHARED_NUM_NODES] = {0};
    uint32_t requests[SHARED_NUM_NODES];
    int remaining = 0;
    for (int i = 0; i < startup->node_count; i++)
    {
        requests[i] = UINT32_MAX - (uint32_t)i;
        ipc_message_t stop = {.type = IPC_STOP, .node_id = i + 1, .request_id = requests[i]};
        if (shared_memory_send(memory, SHARED_QUEUE_COMMAND, i + 1, &stop, 0) == 0)
            remaining++;
        else
            finished[i] = true;
    }
    int64_t now = now_ms(), deadline = now + budget;
    while (remaining && now >= 0 && now < deadline)
    {
        ipc_message_t response;
        if (shared_memory_receive(memory, SHARED_QUEUE_RESPONSE, 0, &response,
                                  (int)(deadline - now)) < 0)
        {
            if (errno != EINTR)
                break;
        }
        else
        {
            int i = response.node_id - 1;
            if (i >= 0 && i < startup->node_count && !finished[i] &&
                response.request_id == requests[i])
            {
                if (response.type == IPC_ACK && response.payload.ack.acked_type == MSG_STOP)
                {
                    startup->nodes[i].stop_confirmed = response.payload.ack.status == MSG_ACK_OK;
                    finished[i] = true;
                    remaining--;
                }
                else if (response.type == IPC_ERROR || response.type == IPC_NODE_DOWN)
                {
                    finished[i] = true;
                    remaining--;
                }
            }
        }
        now = now_ms();
    }
    for (int i = 0; i < startup->node_count; i++)
    {
        fprintf(stderr, "[Arranque] STOP nodo %d: %s.\n", i + 1,
                startup->nodes[i].stop_confirmed ? "confirmado" : "sin confirmar");
        if (startup->nodes[i].stop_confirmed)
            shared_memory_set_node_active(memory, i + 1, false);
    }
    publish_startup(memory, startup, STARTUP_FAILED, 0);
}

int scheduler_bootstrap(shared_memory_t *memory, const HostConfig *config,
                        const Task *tasks, int task_count, int node_count,
                        int timeout_ms, PlannerState *planner)
{
    if (!memory || !config || !tasks || !planner || node_count < 1 ||
        node_count > SHARED_NUM_NODES || timeout_ms <= 0 || task_count < 0 ||
        task_count > MAX_TASKS || config->tick_ms <= 0 || config->tick_ms > UINT16_MAX ||
        config->tick_report_period <= 0 || config->tick_report_period > UINT16_MAX ||
        (config->algorithm != SCHED_EDF && config->algorithm != SCHED_RMS) ||
        (config->heuristic != HEURISTIC_FIRST_FIT && config->heuristic != HEURISTIC_WORST_FIT))
    {
        errno = EINVAL;
        return -1;
    }
    startup_status_t startup = {.node_count = node_count};
    if (publish_startup(memory, &startup, STARTUP_CALIBRATING, timeout_ms) < 0)
        return -1;
    if (bootstrap_steps(memory, config, tasks, task_count, node_count, timeout_ms, planner, &startup) == 0)
        return 0;
    int original_error = errno;
    startup.error_code = original_error;
    startup.failed_phase = startup.phase;
    fprintf(stderr, "[Arranque] Falló fase %d, nodo %d, solicitud %u (errno=%d).\n",
            startup.phase, startup.failed_node, startup.failed_request, original_error);
    abort_startup(memory, &startup, timeout_ms);
    errno = original_error;
    return -1;
}

int scheduler_run(shared_memory_t *memory, const HostConfig *config,
                  const Task *tasks, int task_count, int node_count)
{
    PlannerState planner;
    if (scheduler_bootstrap(memory, config, tasks, task_count, node_count, 30000, &planner) < 0)
        return -1;
    return host_runtime_loop(memory, config, &planner);
}
