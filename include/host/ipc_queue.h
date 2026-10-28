#pragma once

#include <semaphore.h>
#include <stdint.h>
#include "protocol/message.h"
#include "host/startup.h"
#include "host/scheduler/task.h"

#define IPC_QUEUE_CAPACITY 64
#define IPC_REASON_LENGTH 128

typedef enum
{
    IPC_CALIBRATE,
    IPC_DEPLOY,
    IPC_START,
    IPC_SPORADIC,
    IPC_STOP,
    IPC_CALIBRATED,
    IPC_ACK,
    IPC_NODE_DOWN,
    IPC_ERROR,
    IPC_EVENT,
    IPC_TICK,
    IPC_ASSIGNMENT,
    IPC_STARTUP,
    IPC_ADD_TASK,
    IPC_FAIL_NODE,
    IPC_ISOLATE,
    IPC_STATUS,
    IPC_BAD_FRAME, /* Controlador -> reporte: trama serial descartada */
    IPC_SHUTDOWN
} ipc_message_type_t;

typedef struct
{
    ipc_message_type_t type;
    int node_id;
    uint32_t request_id;
    uint64_t not_before_ns;
    startup_timing_t timing;
    union
    {
        Task task;
        startup_status_t startup;
        msg_calib_req_t calibrate;
        msg_task_deploy_t deploy;
        msg_start_t start;
        msg_sporadic_t sporadic;
        msg_calib_resp_t calibrated;
        msg_ack_t ack;
        msg_event_t event;
        msg_tick_report_t tick;
        struct
        {
            int task_id;
            int accepted;
            int confirmed;
            int cost_ticks;
            int kind;
            int period;
            int deadline;
            int criticality;
            char reason[IPC_REASON_LENGTH];
        } assignment;
        struct
        {
            int code;
            char reason[IPC_REASON_LENGTH];
        } error;
    } payload;
} ipc_message_t;

/* Copia los datos de la tarea al evento de asignacion (task puede ser NULL) */
static inline void ipc_assignment_set_task(ipc_message_t *event, const Task *task)
{
    if (!task)
        return;
    event->payload.assignment.kind = (int)task->type;
    event->payload.assignment.period = task->period;
    event->payload.assignment.deadline = task->deadline;
    event->payload.assignment.criticality = task->criticality;
}

typedef struct
{
    sem_t mutex;
    sem_t items;
    sem_t spaces;
    unsigned head;
    unsigned tail;
    ipc_message_t messages[IPC_QUEUE_CAPACITY];
} ipc_queue_t;

int ipc_queue_init(ipc_queue_t *queue);
int ipc_queue_destroy(ipc_queue_t *queue);

int ipc_queue_send(ipc_queue_t *queue, const ipc_message_t *message, int timeout_ms);
int ipc_queue_receive(ipc_queue_t *queue, ipc_message_t *message, int timeout_ms);
