#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "host/node_driver.h"
#include "host/process_manager.h"
#include "host/scheduler_runtime.h"

static process_manager_t process_manager;

typedef struct
{
    const HostConfig *config;
    const Task *tasks;
    int task_count;
    int node_count;
    shared_memory_t *memory;
} scheduler_args_t;

typedef struct
{
    int node_id;
    const char *port;
    shared_memory_t *memory;
    node_driver_config_t config;
} driver_args_t;

static void scheduler_process(void *arg)
{
    scheduler_args_t *args = arg;
    printf("[Planificador] Iniciado (PID %ld).\n", (long)getpid());
    if (scheduler_run(args->memory, args->config, args->tasks,
                      args->task_count, args->node_count) < 0)
    {
        perror("[Planificador] No se pudo completar la operación");
        _exit(EXIT_FAILURE);
    }
}

static void driver_process(void *arg)
{
    driver_args_t *args = arg;
    node_driver_run(args->node_id, args->port,
                    shared_memory_tx_ring_buffer(args->memory, args->node_id),
                    args->memory->name, &args->config);
}

static const char *startup_phase_name(startup_phase_t phase)
{
    switch (phase)
    {
    case STARTUP_IDLE:
        return "pendiente";
    case STARTUP_CALIBRATING:
        return "calibrando";
    case STARTUP_PLANNING:
        return "planificando";
    case STARTUP_DEPLOYING:
        return "desplegando";
    case STARTUP_STARTING:
        return "iniciando";
    case STARTUP_RUNNING:
        return "en ejecución";
    case STARTUP_ABORTING:
        return "cancelando";
    case STARTUP_FAILED:
        return "fallido";
    }
    return "desconocido";
}

static void report_startup(const startup_status_t *status)
{
    printf("[Arranque] Estado: %s.\n", startup_phase_name(status->phase));
    if (status->phase == STARTUP_RUNNING && status->timing_valid)
    {
        for (int i = 0; i < status->node_count; i++)
        {
            startup_timing_t t = status->nodes[i].start;
            printf("[Arranque] Nodo %d: %u UT/tick, tareas=%u/%u; "
                   "START host tx_inicio=%llu tx_fin=%llu ack=%llu ns; "
                   "retraso respecto al objetivo=%.3f ms.\n",
                   i + 1, status->nodes[i].speed_ut_per_tick,
                   status->nodes[i].confirmed_tasks, status->nodes[i].planned_tasks,
                   (unsigned long long)t.tx_begin_ns, (unsigned long long)t.tx_end_ns,
                   (unsigned long long)t.ack_ns,
                   (double)(t.tx_begin_ns - status->target_start_ns) / 1e6);
        }
        if (status->node_count == 2)
        {
            printf("[Arranque] Desfase de envío observado en host: %.3f ms; "
                   "intervalo estimado cero2-cero1: [%.3f, %.3f] ms.\n",
                   (double)status->tx_skew_ns / 1e6,
                   (double)status->zero_delta_min_ns / 1e6,
                   (double)status->zero_delta_max_ns / 1e6);
        }
        else
            printf("[Arranque] Un nodo: no aplica desfase entre nodos.\n");
    }
    else if (status->phase == STARTUP_FAILED)
    {
        fprintf(stderr, "[Arranque] Fallo en %s: nodo=%d solicitud=%u errno=%d.\n",
                startup_phase_name(status->failed_phase), status->failed_node,
                status->failed_request, status->error_code);
    }
}

static void report_process(void *arg)
{
    shared_memory_t *memory = arg;
    /* Consumir desde el arranque evita llenar la cola con las admisiones.
     * El reporte persistente y el Gantt se incorporan en el paso de reportes. */
    for (;;)
    {
        ipc_message_t event;
        if (shared_memory_receive(memory, SHARED_QUEUE_EVENT, 0, &event, -1) < 0)
        {
            if (errno == EINTR)
                continue;
            perror("[Reporte] Error leyendo eventos");
            _exit(EXIT_FAILURE);
        }
        if (event.type == IPC_SHUTDOWN)
            return;
        if (event.type == IPC_STARTUP)
        {
            report_startup(&event.payload.startup);
        }
        else if (event.type == IPC_ASSIGNMENT)
        {
            printf("[Reporte] Tarea %d: %s, nodo=%d, C=%d; %.128s\n",
                   event.payload.assignment.task_id,
                   event.payload.assignment.accepted ? "admitida" : "rechazada",
                   event.node_id, event.payload.assignment.cost_ticks,
                   event.payload.assignment.reason);
        }
        else if (event.type == IPC_EVENT)
        {
            printf("[Reporte] Nodo %d: evento=%u tarea=%u tick=%u aux=%u\n",
                   event.node_id, event.payload.event.event_type,
                   event.payload.event.task_id, event.payload.event.tick, event.payload.event.aux);
        }
        else if (event.type == IPC_TICK)
        {
            printf("[Reporte] Nodo %d: tick=%u tramas inválidas=%u\n",
                   event.node_id, event.payload.tick.tick, event.payload.tick.bad_frames);
        }
        else if (event.type == IPC_ERROR || event.type == IPC_NODE_DOWN)
        {
            fprintf(stderr, "[Reporte] Nodo %d: %.128s\n", event.node_id, event.payload.error.reason);
        }
    }
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <config> (las velocidades se miden en los nodos)\n", argv[0]);
        return EXIT_FAILURE;
    }
    HostConfig config;
    if (load_config(argv[1], &config) != 0)
        return EXIT_FAILURE;
    Task tasks[MAX_TASKS];
    int task_count = load_tasks(config.task_file, tasks, MAX_TASKS);
    if (task_count < 0)
        return EXIT_FAILURE;
    int node_count = config.node2_port[0] ? 2 : 1;
    if (process_manager_init(&process_manager) < 0)
        return EXIT_FAILURE;

    /* Cada ejecucion tiene su propio objeto, no elimina memoria de otro host. */
    char name[SHARED_MEMORY_NAME_MAX];
    snprintf(name, sizeof(name), "/calendarizador_%ld", (long)getpid());
    shared_memory_t memory;
    if (shared_memory_create(&memory, name) < 0)
    {
        perror("[Host] Memoria compartida");
        return EXIT_FAILURE;
    }
    shared_system_config_t shared = {0};
    for (int i = 0; i < SHARED_NUM_NODES; i++)
        shared.nodes[i].node_id = i < node_count ? i + 1 : -1;
    if (shared_memory_write_config(&memory, &shared) < 0 ||
        process_manager_install_signal_handlers(&process_manager) < 0)
    {
        shared_memory_destroy(&memory);
        return EXIT_FAILURE;
    }
    int result = EXIT_FAILURE;
    if (process_manager_create(&process_manager, report_process, &memory) < 0)
        goto stop;
    const char *ports[] = {config.node1_port, config.node2_port};
    for (int i = 0; i < node_count; i++)
    {
        driver_args_t args = {
            .node_id = i + 1, .port = ports[i], .memory = &memory, .config = {(uint16_t)config.tick_ms, config.algorithm == SCHED_EDF ? ALGORITHM_EDF : ALGORITHM_RMS, (uint16_t)config.tick_report_period}};
        if (process_manager_create(&process_manager, driver_process, &args) < 0)
            goto stop;
    }
    scheduler_args_t args = {&config, tasks, task_count, node_count, &memory};
    if (process_manager_create(&process_manager, scheduler_process, &args) < 0)
        goto stop;
    result = process_manager_wait(&process_manager);
    goto cleanup;
stop:
    perror("[Host] No se pudieron crear los procesos");
    process_manager_stop(&process_manager);
    process_manager_wait(&process_manager);
cleanup:
    if (shared_memory_destroy(&memory) < 0)
    {
        perror("[Host] Error liberando memoria compartida");
        result = EXIT_FAILURE;
    }
    return result;
}
