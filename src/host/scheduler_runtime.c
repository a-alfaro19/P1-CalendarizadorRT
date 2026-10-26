#define _POSIX_C_SOURCE 200809L
#include "host/scheduler_runtime.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int64_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int send_command(shared_memory_t *memory, ipc_message_t *command,
                        uint32_t *sequence, int timeout_ms)
{
    command->request_id = ++*sequence;
    return shared_memory_send(memory, SHARED_QUEUE_COMMAND, command->node_id,
                              command, timeout_ms);
}

static int await_responses(shared_memory_t *memory, const ipc_message_t *commands,
                           int count, ipc_message_t *responses, int timeout_ms)
{
    bool received[SHARED_NUM_NODES] = {0};
    int64_t now = now_ms();
    if (now < 0) return -1;
    int64_t deadline = now + timeout_ms;
    int remaining = count;
    while (remaining) {
        now = now_ms();
        if (now < 0) return -1;
        if (now >= deadline) { errno = ETIMEDOUT; return -1; }
        ipc_message_t message;
        if (shared_memory_receive(memory, SHARED_QUEUE_RESPONSE, 0, &message,
                                  (int)(deadline - now)) < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (message.type == IPC_NODE_DOWN || message.type == IPC_ERROR) {
            fprintf(stderr, "[Planificador] Error del nodo %d: %.128s\n",
                    message.node_id, message.payload.error.reason);
            errno = EIO;
            return -1;
        }
        for (int i = 0; i < count; i++) {
            if (received[i] || message.node_id != commands[i].node_id ||
                message.request_id != commands[i].request_id) continue;
            if (commands[i].type == IPC_CALIBRATE) {
                if (message.type != IPC_CALIBRATED) continue;
                if (!message.payload.calibrated.ut_per_tick ||
                    message.payload.calibrated.ut_per_tick > INT_MAX) {
                    errno = ERANGE;
                    return -1;
                }
            } else {
                uint8_t expected = commands[i].type == IPC_DEPLOY ? MSG_TASK_DEPLOY : MSG_START;
                if (message.type != IPC_ACK || message.payload.ack.acked_type != expected) continue;
                if (message.payload.ack.status != MSG_ACK_OK) {
                    fprintf(stderr, "[Planificador] Nodo %d rechazó la orden %u (estado %u)\n",
                            message.node_id, message.request_id, message.payload.ack.status);
                    errno = EPROTO;
                    return -1;
                }
            }
            responses[i] = message;
            received[i] = true;
            remaining--;
            break;
        }
    }
    return 0;
}

static int report_assignment(shared_memory_t *memory, const TaskAssignment *assignment,
                             int timeout_ms)
{
    ipc_message_t event = {.type = IPC_ASSIGNMENT,
                          .node_id = assignment->accepted ? assignment->node_id : 0};
    event.payload.assignment.task_id = assignment->task_id;
    event.payload.assignment.accepted = assignment->accepted;
    event.payload.assignment.cost_ticks = assignment->cost_ticks;
    snprintf(event.payload.assignment.reason, IPC_REASON_LENGTH, "%s", assignment->reason);
    return shared_memory_send(memory, SHARED_QUEUE_EVENT, 0, &event, timeout_ms);
}

int scheduler_bootstrap(shared_memory_t *memory, const HostConfig *config,
                        const Task *tasks, int task_count, int node_count,
                        int timeout_ms, PlannerState *planner)
{
    if (!memory || !config || !tasks || !planner || task_count < 0 ||
        task_count > MAX_TASKS || node_count < 1 || node_count > SHARED_NUM_NODES ||
        timeout_ms <= 0 || config->tick_ms <= 0 || config->tick_ms > UINT16_MAX ||
        config->tick_report_period <= 0 || config->tick_report_period > UINT16_MAX) {
        errno = EINVAL;
        return -1;
    }
    uint32_t sequence = 0;
    ipc_message_t commands[SHARED_NUM_NODES] = {0}, responses[SHARED_NUM_NODES];
    NodeInfo nodes[SHARED_NUM_NODES];
    for (int i = 0; i < node_count; i++) {
        commands[i].type = IPC_CALIBRATE;
        commands[i].node_id = i + 1;
        commands[i].payload.calibrate.tick_ms = (uint16_t)config->tick_ms;
        if (send_command(memory, &commands[i], &sequence, timeout_ms) < 0) return -1;
    }
    if (await_responses(memory, commands, node_count, responses, timeout_ms) < 0) return -1;
    for (int i = 0; i < node_count; i++) {
        nodes[i] = (NodeInfo){i + 1, (int)responses[i].payload.calibrated.ut_per_tick, 1};
        printf("[Planificador] Nodo %d: %d UT/tick medidas\n", i + 1, nodes[i].speed_ut_per_tick);
    }
    if (planner_init(planner, nodes, node_count, config) < 0) { errno = EINVAL; return -1; }
    TaskAssignment assignments[MAX_TASKS];
    if (planner_load_initial_tasks(planner, tasks, task_count, assignments) < 0) {
        errno = EINVAL;
        return -1;
    }
    shared_system_config_t shared = {0};
    for (int i = 0; i < SHARED_NUM_NODES; i++) shared.nodes[i].node_id = -1;
    for (int i = 0; i < node_count; i++) {
        const NodePlan *node = &planner->nodes[i];
        shared.nodes[i].node_id = node->info.id;
        shared.nodes[i].task_count = node->task_count;
        if (node->task_count > SHARED_MAX_TASKS_PER_NODE) { errno = EOVERFLOW; return -1; }
        for (int j = 0; j < node->task_count; j++) {
            const Task *task = &node->tasks[j];
            shared.nodes[i].tasks[j] = (task_config_t){
                .task_id = (uint8_t)task->id, .kind = (uint8_t)task->type,
                .cost_ticks = (uint16_t)node->costs[j], .period = (uint16_t)task->period,
                .deadline = (uint16_t)task->deadline, .assigned = false};
        }
    }
    if (shared_memory_write_config(memory, &shared) < 0) return -1;
    /* La admisión y la confirmación son estados distintos: assigned cambia tras el ACK. */
    for (int i = 0; i < task_count; i++)
        if (report_assignment(memory, &assignments[i], timeout_ms) < 0) return -1;
    for (int i = 0; i < node_count; i++) {
        for (int j = 0; j < shared.nodes[i].task_count; j++) {
            task_config_t *task = &shared.nodes[i].tasks[j];
            ipc_message_t command = {.type = IPC_DEPLOY, .node_id = i + 1}, response;
            command.payload.deploy = (msg_task_deploy_t){task->task_id, task->kind,
                task->cost_ticks, task->period, task->deadline};
            if (send_command(memory, &command, &sequence, timeout_ms) < 0 ||
                await_responses(memory, &command, 1, &response, timeout_ms) < 0) return -1;
            if (shared_memory_mark_task_assigned(memory, i + 1, task->task_id) < 0) return -1;
        }
    }
    /* Ningún nodo recibe START antes de confirmar todo el despliegue. */
    for (int i = 0; i < node_count; i++) {
        commands[i] = (ipc_message_t){.type = IPC_START, .node_id = i + 1};
        commands[i].payload.start = (msg_start_t){
            config->algorithm == SCHED_EDF ? ALGORITHM_EDF : ALGORITHM_RMS,
            (uint16_t)config->tick_report_period};
        if (send_command(memory, &commands[i], &sequence, timeout_ms) < 0) return -1;
    }
    if (await_responses(memory, commands, node_count, responses, timeout_ms) < 0) return -1;
    planner_print_state(planner);
    printf("[Planificador] Despliegue confirmado e inicio completado.\n");
    return 0;
}

int scheduler_run(shared_memory_t *memory, const HostConfig *config,
                  const Task *tasks, int task_count, int node_count)
{
    PlannerState planner;
    if (scheduler_bootstrap(memory, config, tasks, task_count, node_count, 30000, &planner) < 0)
        return -1;
    for (;;) {
        ipc_message_t response;
        if (shared_memory_receive(memory, SHARED_QUEUE_RESPONSE, 0, &response, -1) < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (response.type == IPC_SHUTDOWN) return 0;
        if (response.type == IPC_NODE_DOWN || response.type == IPC_ERROR) {
            fprintf(stderr, "[Planificador] Nodo %d: %.128s; recuperación pendiente.\n",
                    response.node_id, response.payload.error.reason);
            if (shared_memory_send(memory, SHARED_QUEUE_EVENT, 0, &response, 1000) < 0)
                return -1;
            /* La reubicación automática se incorpora con la gestión de fallas. */
            errno = EIO;
            return -1;
        }
    }
}
