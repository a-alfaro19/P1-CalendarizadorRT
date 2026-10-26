#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include <sys/wait.h>
#include "host/console.h"
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
                   event.payload.assignment.accepted ?
                       (event.payload.assignment.confirmed ? "confirmada" : "admitida, pendiente") : "rechazada",
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

static int64_t host_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int submit_line(shared_memory_t *memory, const char *line, bool *closing)
{
    ipc_message_t command;
    int parsed = host_parse_command(line, &command);
    if (parsed > 0) { host_print_help(); return 0; }
    if (parsed < 0) {
        fprintf(stderr, "[Consola] Orden inválida. Use help para ver el formato.\n");
        return 0;
    }
    if (shared_memory_send(memory, SHARED_QUEUE_CONTROL, 0, &command, 0) < 0) {
        fprintf(stderr, "[Consola] No se pudo encolar la orden; vuelva a intentarlo.\n");
        return 0;
    }
    if (command.type == IPC_SHUTDOWN) *closing = true;
    return 0;
}

static int supervise(shared_memory_t *memory, const HostConfig *config,
                     const pid_t drivers[], int node_count, pid_t scheduler, int automatic_failure)
{
    bool closing = false, input_open = true, discard_line = false;
    char line[512];
    size_t used = 0;
    int64_t close_deadline = 0;
    host_print_help();
    for (;;) {
        startup_status_t startup;
        if (shared_memory_read_startup(memory, &startup) < 0) return EXIT_FAILURE;
        if (automatic_failure && startup.phase == STARTUP_RUNNING) {
            ipc_message_t command = {.type = IPC_FAIL_NODE, .node_id = automatic_failure};
            if (shared_memory_send(memory, SHARED_QUEUE_CONTROL, 0, &command, 0) == 0)
                automatic_failure = 0;
        }
        if (process_manager_shutdown_requested(&process_manager) && !closing) {
            if (startup.phase != STARTUP_RUNNING) return EXIT_SUCCESS;
            ipc_message_t command = {.type = IPC_SHUTDOWN};
            if (shared_memory_send(memory, SHARED_QUEUE_CONTROL, 0, &command, 0) == 0) closing = true;
        }
        int64_t now = host_now_ms();
        if (now < 0) return EXIT_FAILURE;
        if (closing && !close_deadline)
            close_deadline = now + 30000 + config->command_timeout_ms;
        if (closing && now >= close_deadline) {
            fprintf(stderr, "[Host] Venció el plazo para cerrar el planificador.\n");
            return EXIT_FAILURE;
        }
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (size_t i = 0; i < process_manager.count; i++)
                if (process_manager.pids[i] == pid) process_manager.pids[i] = 0;
            if (pid == scheduler)
                return closing && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
            int node = 0;
            for (int i = 0; i < node_count; i++) if (drivers[i] == pid) node = i + 1;
            if (node && startup.phase == STARTUP_RUNNING && !closing) {
                ipc_message_t down = {.type = IPC_NODE_DOWN, .node_id = node};
                snprintf(down.payload.error.reason, IPC_REASON_LENGTH, "El proceso controlador terminó");
                if (shared_memory_send(memory, SHARED_QUEUE_RESPONSE, 0, &down, 100) < 0) return EXIT_FAILURE;
                continue;
            }
            if (!closing) {
                fprintf(stderr, "[Host] Proceso esencial %ld terminó inesperadamente.\n", (long)pid);
                return EXIT_FAILURE;
            }
        }
        if (pid < 0 && errno != ECHILD && errno != EINTR) return EXIT_FAILURE;
        struct pollfd input = {.fd = input_open && !closing ? STDIN_FILENO : -1, .events = POLLIN};
        int ready = poll(&input, 1, 50);
        if (ready < 0) { if (errno == EINTR) continue; return EXIT_FAILURE; }
        if (ready && (input.revents & (POLLIN | POLLHUP))) {
            char bytes[256];
            ssize_t count = read(STDIN_FILENO, bytes, sizeof(bytes));
            if (count < 0) { if (errno == EINTR) continue; input_open = false; }
            else if (count == 0) {
                input_open = false;
                if (used && !discard_line) {
                    line[used] = '\0';
                    submit_line(memory, line, &closing);
                }
                used = 0;
            } else for (ssize_t i = 0; i < count && !closing; i++) {
                if (bytes[i] == '\n') {
                    if (!discard_line) {
                        line[used] = '\0';
                        submit_line(memory, line, &closing);
                    }
                    used = 0;
                    discard_line = false;
                } else if (!discard_line) {
                    if (used + 1 >= sizeof(line) || bytes[i] == '\0') {
                        fprintf(stderr, "[Consola] Línea demasiado larga o inválida; descartada.\n");
                        discard_line = true;
                        used = 0;
                    } else line[used++] = bytes[i];
                }
            }
        } else if (ready && (input.revents & (POLLERR | POLLNVAL))) input_open = false;
    }
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 2 && argc != 4)
    {
        fprintf(stderr, "Uso: %s <config> [--fail 1|2] (las velocidades se miden en los nodos)\n", argv[0]);
        return EXIT_FAILURE;
    }
    int automatic_failure = 0;
    if (argc == 4) {
        if (strcmp(argv[2], "--fail") || (strcmp(argv[3], "1") && strcmp(argv[3], "2"))) {
            fprintf(stderr, "[Host] Use --fail 1 o --fail 2.\n");
            return EXIT_FAILURE;
        }
        automatic_failure = argv[3][0] - '0';
    }
    HostConfig config;
    if (load_config(argv[1], &config) != 0)
        return EXIT_FAILURE;
    Task tasks[MAX_TASKS];
    int task_count = load_tasks(config.task_file, tasks, MAX_TASKS);
    if (task_count < 0)
        return EXIT_FAILURE;
    int node_count = config.node2_port[0] ? 2 : 1;
    if (automatic_failure > node_count) {
        fprintf(stderr, "[Host] El nodo indicado para --fail no está configurado.\n");
        return EXIT_FAILURE;
    }
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
    pid_t drivers[SHARED_NUM_NODES] = {0};
    const char *ports[] = {config.node1_port, config.node2_port};
    for (int i = 0; i < node_count; i++)
    {
        driver_args_t args = {
            .node_id = i + 1, .port = ports[i], .memory = &memory, .config = {(uint16_t)config.tick_ms, config.algorithm == SCHED_EDF ? ALGORITHM_EDF : ALGORITHM_RMS, (uint16_t)config.tick_report_period}};
        drivers[i] = process_manager_create(&process_manager, driver_process, &args);
        if (drivers[i] < 0)
            goto stop;
    }
    scheduler_args_t args = {&config, tasks, task_count, node_count, &memory};
    pid_t scheduler = process_manager_create(&process_manager, scheduler_process, &args);
    if (scheduler < 0)
        goto stop;
    result = supervise(&memory, &config, drivers, node_count, scheduler, automatic_failure);
    process_manager_stop(&process_manager);
    process_manager_wait(&process_manager);
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
