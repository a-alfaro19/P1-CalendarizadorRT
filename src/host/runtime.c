#define _POSIX_C_SOURCE 200809L
#include "host/runtime.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct
{
    bool waiting;
    uint32_t request_id;
    int task_id;
    int64_t deadline_ms;
} pending_deploy_t;

typedef struct
{
    shared_memory_t *memory;
    const HostConfig *config;
    PlannerState *planner;
    uint32_t sequence;
    int command_timeout_ms;
    int64_t started_ms;
    bool confirmed[SHARED_NUM_NODES][UINT8_MAX];
    bool relocated[SHARED_NUM_NODES][UINT8_MAX];
    pending_deploy_t pending[SHARED_NUM_NODES];
} runtime_t;

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const Task *find_task(const Task *tasks, int count, int id)
{
    for (int i = 0; i < count; i++)
        if (tasks[i].id == id)
            return &tasks[i];
    return NULL;
}

static int emit_assignment(runtime_t *runtime, const TaskAssignment *assignment,
                           const Task *task, bool confirmed)
{
    ipc_message_t event = {.type = IPC_ASSIGNMENT,
                           .node_id = assignment->accepted ? assignment->node_id : 0};
    event.payload.assignment.task_id = assignment->task_id;
    event.payload.assignment.accepted = assignment->accepted;
    event.payload.assignment.confirmed = confirmed;
    event.payload.assignment.cost_ticks = assignment->cost_ticks;
    ipc_assignment_set_task(&event, task);
    snprintf(event.payload.assignment.reason, IPC_REASON_LENGTH, "%s", assignment->reason);
    return shared_memory_send(runtime->memory, SHARED_QUEUE_EVENT, 0, &event, 1000);
}

static int sync_plan(runtime_t *runtime)
{
    shared_system_config_t shared = {0};
    for (int i = 0; i < SHARED_NUM_NODES; i++)
        shared.nodes[i].node_id = -1;
    for (int i = 0; i < runtime->planner->node_count; i++)
    {
        NodePlan *node = &runtime->planner->nodes[i];
        shared.nodes[i].node_id = node->info.id;
        shared.nodes[i].task_count = node->task_count;
        if (node->task_count > SHARED_MAX_TASKS_PER_NODE)
        {
            errno = EOVERFLOW;
            return -1;
        }
        for (int j = 0; j < node->task_count; j++)
        {
            Task *task = &node->tasks[j];
            shared.nodes[i].tasks[j] = (task_config_t){
                .task_id = (uint8_t)task->id, .kind = (uint8_t)task->type, .cost_ticks = (uint16_t)node->costs[j], .period = (uint16_t)task->period, .deadline = (uint16_t)task->deadline, .assigned = runtime->confirmed[i][task->id]};
        }
    }
    return shared_memory_write_config(runtime->memory, &shared);
}

static int node_index(runtime_t *runtime, int id)
{
    for (int i = 0; i < runtime->planner->node_count; i++)
        if (runtime->planner->nodes[i].info.id == id)
            return i;
    return -1;
}

static int fail_node(runtime_t *runtime, int node_id, const char *reason)
{
    int index = node_index(runtime, node_id);
    if (index < 0 || !runtime->planner->nodes[index].info.active)
        return 0;
    printf("[Runtime] Nodo %d caído: %s\n", node_id, reason);
    ipc_message_t event = {.type = IPC_NODE_DOWN, .node_id = node_id};
    snprintf(event.payload.error.reason, IPC_REASON_LENGTH, "%s", reason);
    if (shared_memory_send(runtime->memory, SHARED_QUEUE_EVENT, 0, &event, 1000) < 0)
        return -1;
    if (shared_memory_set_node_active(runtime->memory, node_id, false) < 0)
        return -1;
    /* Aislar impide que reportes o ACK tardios vuelvan a incorporar un nodo caido
     * Equivale a desconectar el cable, la placa puede seguir sola */
    ipc_message_t isolate = {.type = IPC_ISOLATE, .node_id = node_id};
    if (shared_memory_send(runtime->memory, SHARED_QUEUE_COMMAND, node_id, &isolate, 0) < 0)
        fprintf(stderr, "[Runtime] No se pudo entregar aislamiento al controlador %d.\n", node_id);
    runtime->pending[index].waiting = false;
    memset(runtime->confirmed[index], 0, sizeof(runtime->confirmed[index]));
    memset(runtime->relocated[index], 0, sizeof(runtime->relocated[index]));
    /* El planificador vacia el nodo caido: se conserva una copia para describir
     * cada resultado (reubicada o rechazada) en el reporte. */
    Task failed_tasks[MAX_TASKS_PER_NODE];
    int failed_count = runtime->planner->nodes[index].task_count;
    memcpy(failed_tasks, runtime->planner->nodes[index].tasks, sizeof(Task) * (size_t)failed_count);
    TaskAssignment results[MAX_TASKS_PER_NODE];
    int count = 0;
    if (planner_handle_node_failure(runtime->planner, node_id, results, &count) < 0)
        return -1;
    for (int i = 0; i < count; i++)
    {
        if (results[i].accepted)
        {
            int target = node_index(runtime, results[i].node_id);
            runtime->confirmed[target][results[i].task_id] = false;
            runtime->relocated[target][results[i].task_id] = true;
            snprintf(results[i].reason, REJECTION_REASON_LENGTH,
                     "Reubicación admitida tras falla del nodo %d; pendiente de ACK", node_id);
        }
        if (emit_assignment(runtime, &results[i],
                            find_task(failed_tasks, failed_count, results[i].task_id), false) < 0)
            return -1;
    }
    return sync_plan(runtime);
}

static int process_response(runtime_t *runtime, const ipc_message_t *response)
{
    int index = node_index(runtime, response->node_id);
    if (index < 0 || !runtime->planner->nodes[index].info.active)
        return 0;
    pending_deploy_t *pending = &runtime->pending[index];
    if (response->type == IPC_NODE_DOWN)
        return fail_node(runtime, response->node_id, "Pérdida de conexión o salida del controlador");
    if (response->type == IPC_ERROR)
    {
        if (!response->request_id || (pending->waiting && response->request_id == pending->request_id))
            return fail_node(runtime, response->node_id, "Error del controlador durante una orden");
        return 0;
    }
    if (response->type != IPC_ACK || !pending->waiting ||
        response->request_id != pending->request_id || response->payload.ack.acked_type != MSG_TASK_DEPLOY)
        return 0;
    NodePlan *node = &runtime->planner->nodes[index];
    int slot = -1;
    for (int j = 0; j < node->task_count; j++)
        if (node->tasks[j].id == pending->task_id)
            slot = j;
    if (slot < 0)
    {
        errno = EINVAL;
        return -1;
    }
    TaskAssignment result = {.task_id = pending->task_id, .node_id = response->node_id, .cost_ticks = node->costs[slot]};
    Task deployed = node->tasks[slot]; /* copia: el rechazo la elimina del plan */
    pending->waiting = false;
    if (response->payload.ack.status == MSG_ACK_OK)
    {
        result.accepted = 1;
        runtime->confirmed[index][result.task_id] = true;
        snprintf(result.reason, REJECTION_REASON_LENGTH, "%s",
                 runtime->relocated[index][result.task_id] ? "Reubicada y confirmada; reinicia en el nodo sobreviviente" : "Tarea manual confirmada por el nodo");
    }
    else
    {
        snprintf(result.reason, REJECTION_REASON_LENGTH,
                 "Nodo rechazo el despliegue (ACK=%u); reserva liberada", response->payload.ack.status);
        /* Un rechazo explicito permite liberar capacidad, un timeout no demuestra
         * que la placa haya rechazado la tarea y se trata como falla del nodo. */
        for (int j = slot; j + 1 < node->task_count; j++)
        {
            node->tasks[j] = node->tasks[j + 1];
            node->costs[j] = node->costs[j + 1];
        }
        node->task_count--;
        runtime->confirmed[index][result.task_id] = false;
        runtime->relocated[index][result.task_id] = false;
    }
    if (sync_plan(runtime) < 0)
        return -1;
    return emit_assignment(runtime, &result, &deployed, result.accepted != 0);
}

static int dispatch_deployments(runtime_t *runtime, int64_t now)
{
    for (int i = 0; i < runtime->planner->node_count; i++)
    {
        NodePlan *node = &runtime->planner->nodes[i];
        if (!node->info.active || runtime->pending[i].waiting)
            continue;
        for (int j = 0; j < node->task_count; j++)
        {
            Task *task = &node->tasks[j];
            if (runtime->confirmed[i][task->id])
                continue;
            ipc_message_t command = {.type = IPC_DEPLOY, .node_id = node->info.id, .request_id = ++runtime->sequence};
            command.payload.deploy = (msg_task_deploy_t){(uint8_t)task->id, (uint8_t)task->type,
                                                         (uint16_t)node->costs[j], (uint16_t)task->period, (uint16_t)task->deadline};
            if (shared_memory_send(runtime->memory, SHARED_QUEUE_COMMAND, node->info.id, &command, 0) < 0)
            {
                if (fail_node(runtime, node->info.id, "Cola del controlador no disponible") < 0)
                    return -1;
                break;
            }
            runtime->pending[i] = (pending_deploy_t){true, command.request_id, task->id,
                                                     now + runtime->command_timeout_ms};
            break;
        }
    }
    return 0;
}

static int check_timeouts(runtime_t *runtime, int64_t now)
{
    uint64_t period_ms = (uint64_t)runtime->config->tick_ms * runtime->config->tick_report_period;
    for (int i = 0; i < runtime->planner->node_count; i++)
    {
        NodePlan *node = &runtime->planner->nodes[i];
        if (!node->info.active)
            continue;
        shared_node_status_t status;
        if (shared_memory_read_node_status(runtime->memory, node->info.id, &status) < 0)
            return -1;
        int64_t last = status.last_report_ns ? (int64_t)(status.last_report_ns / 1000000u) : runtime->started_ms;
        uint64_t missed = now > last ? (uint64_t)(now - last) / period_ms : 0;
        if (shared_memory_set_missed_reports(runtime->memory, node->info.id,
                                             missed > UINT32_MAX ? UINT32_MAX : (uint32_t)missed) < 0)
            return -1;
        if (missed >= (uint64_t)runtime->config->max_missed_reports)
        {
            if (fail_node(runtime, node->info.id, "Timeout de reportes de tick") < 0)
                return -1;
        }
        else if (runtime->pending[i].waiting && now >= runtime->pending[i].deadline_ms)
        {
            if (fail_node(runtime, node->info.id, "Timeout de ACK; despliegue de estado incierto") < 0)
                return -1;
        }
    }
    return 0;
}

/* Disparo manual de la tarea esporadica del nodo (pulsar boton)
 * Solo se libera si el nodo la tiene confirmada, el nodo aplica el intervalo min */
static int release_sporadic(runtime_t *runtime, int node_id)
{
    int index = node_index(runtime, node_id);
    if (index < 0 || !runtime->planner->nodes[index].info.active)
    {
        printf("[Runtime] Nodo %d inexistente o inactivo.\n", node_id);
        return 0;
    }
    NodePlan *node = &runtime->planner->nodes[index];
    for (int j = 0; j < node->task_count; j++)
    {
        const Task *task = &node->tasks[j];
        if (task->type != TASK_SPORADIC || !runtime->confirmed[index][task->id])
            continue;
        ipc_message_t command = {.type = IPC_SPORADIC, .node_id = node_id, .request_id = ++runtime->sequence};
        command.payload.sporadic.task_id = (uint8_t)task->id;
        if (shared_memory_send(runtime->memory, SHARED_QUEUE_COMMAND, node_id, &command, 0) < 0)
            printf("[Runtime] No se pudo encolar el disparo para el nodo %d.\n", node_id);
        return 0;
    }
    printf("[Runtime] El nodo %d no tiene una tarea esporádica confirmada.\n", node_id);
    return 0;
}

static int stop_nodes(runtime_t *runtime)
{
    bool waiting[SHARED_NUM_NODES] = {0};
    uint32_t requests[SHARED_NUM_NODES] = {0};
    int left = 0;
    for (int i = 0; i < runtime->planner->node_count; i++)
    {
        NodePlan *node = &runtime->planner->nodes[i];
        if (!node->info.active)
            continue;
        ipc_message_t stop = {.type = IPC_STOP, .node_id = node->info.id, .request_id = ++runtime->sequence};
        if (shared_memory_send(runtime->memory, SHARED_QUEUE_COMMAND, node->info.id, &stop, 0) == 0)
        {
            requests[i] = stop.request_id;
            waiting[i] = true;
            left++;
        }
    }
    int64_t now = monotonic_ms(), deadline = now + runtime->command_timeout_ms;
    while (left && now >= 0 && now < deadline)
    {
        ipc_message_t reply;
        if (shared_memory_receive(runtime->memory, SHARED_QUEUE_RESPONSE, 0, &reply,
                                  (int)(deadline - now)) < 0)
        {
            if (errno != EINTR)
                break;
        }
        else
        {
            int i = node_index(runtime, reply.node_id);
            if (i >= 0 && waiting[i] && reply.request_id == requests[i] &&
                reply.type == IPC_ACK && reply.payload.ack.acked_type == MSG_STOP &&
                reply.payload.ack.status == MSG_ACK_OK)
            {
                waiting[i] = false;
                left--;
                shared_memory_set_node_active(runtime->memory, reply.node_id, false);
                printf("[Runtime] STOP nodo %d confirmado.\n", reply.node_id);
            }
        }
        now = monotonic_ms();
    }
    for (int i = 0; i < runtime->planner->node_count; i++)
        if (waiting[i])
            fprintf(stderr, "[Runtime] STOP nodo %d sin confirmar.\n", runtime->planner->nodes[i].info.id);
    return 0;
}

int host_runtime_loop(shared_memory_t *memory, const HostConfig *config, PlannerState *planner)
{
    if (!memory || !config || !planner || config->tick_ms <= 0 || config->tick_report_period <= 0 ||
        config->max_missed_reports <= 0 || planner->node_count < 1 || planner->node_count > SHARED_NUM_NODES)
    {
        errno = EINVAL;
        return -1;
    }
    runtime_t runtime = {.memory = memory, .config = config, .planner = planner, .sequence = 1000000, .command_timeout_ms = config->command_timeout_ms > 0 ? config->command_timeout_ms : 1000};
    runtime.started_ms = monotonic_ms();
    if (runtime.started_ms < 0)
        return -1;
    for (int i = 0; i < planner->node_count; i++)
    {
        for (int j = 0; j < planner->nodes[i].task_count; j++)
            runtime.confirmed[i][planner->nodes[i].tasks[j].id] = true;
    }
    if (sync_plan(&runtime) < 0)
        return -1;
    puts("[Runtime] Listo: add, fail, status, q, help.");
    for (;;)
    {
        ipc_message_t message;
        /* Atender respuestas antes de vencer plazos, sin permitir que un flujo
         * continuo de mensajes impida comprobar los relojes y la consola. */
        for (int n = 0; n < 8; n++)
        {
            if (shared_memory_receive(memory, SHARED_QUEUE_RESPONSE, 0, &message, 0) < 0)
            {
                if (errno == EAGAIN || errno == EINTR)
                    break;
                return -1;
            }
            if (process_response(&runtime, &message) < 0)
                return -1;
        }
        if (shared_memory_receive(memory, SHARED_QUEUE_CONTROL, 0, &message, 0) == 0)
        {
            if (message.type == IPC_SHUTDOWN)
                return stop_nodes(&runtime);
            if (message.type == IPC_STATUS)
                planner_print_state(planner);
            else if (message.type == IPC_FAIL_NODE)
            {
                int i = node_index(&runtime, message.node_id);
                if (i < 0 || !planner->nodes[i].info.active)
                    printf("[Runtime] Nodo %d inexistente o ya inactivo.\n", message.node_id);
                else if (fail_node(&runtime, message.node_id, "Falla simulada desde la consola") < 0)
                    return -1;
            }
            else if (message.type == IPC_SPORADIC)
            {
                if (release_sporadic(&runtime, message.node_id) < 0)
                    return -1;
            }
            else if (message.type == IPC_ADD_TASK)
            {
                TaskAssignment result;
                int accepted = planner_add_task(planner, &message.payload.task, &result);
                if (accepted < 0)
                    return -1;
                if (accepted)
                {
                    int i = node_index(&runtime, result.node_id);
                    runtime.confirmed[i][result.task_id] = false;
                    runtime.relocated[i][result.task_id] = false;
                    snprintf(result.reason, REJECTION_REASON_LENGTH, "Admitida; pendiente de confirmación del nodo");
                }
                if (sync_plan(&runtime) < 0 ||
                    emit_assignment(&runtime, &result, &message.payload.task, false) < 0)
                    return -1;
            }
        }
        else if (errno != EAGAIN && errno != EINTR)
            return -1;
        int64_t now = monotonic_ms();
        if (now < 0 || check_timeouts(&runtime, now) < 0 || dispatch_deployments(&runtime, now) < 0)
            return -1;
        if (shared_memory_receive(memory, SHARED_QUEUE_RESPONSE, 0, &message, 20) == 0)
        {
            if (process_response(&runtime, &message) < 0)
                return -1;
        }
        else if (errno != ETIMEDOUT && errno != EINTR)
            return -1;
    }
}
