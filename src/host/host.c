#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <host/node_driver.h>
#include <host/process_manager.h>
#include <host/ring_buffer.h>
#include <host/scheduler/scheduler.h>
#include <host/shared_memory.h>

#define SHM_NAME "/shm_system_config"
#define HOST_NODE_COUNT 2
#define SCHEDULER_REPORT_INTERVAL_SECONDS 5

typedef struct
{
    int node_id;
    const char *tty_device;
    ring_buffer_t *tx_rb;
    const char *shm_name;
    node_driver_config_t driver_config;
} node_driver_args_t;

typedef struct
{
    const PlannerState *planner;
    shared_memory_t *shared_memory;
} scheduler_process_args_t;

static process_manager_t process_manager;
static int configured_node_count = HOST_NODE_COUNT;

static int parse_node_speed(const char *text, int *speed)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' || value <= 0 || value > INT_MAX)
    {
        fprintf(stderr, "[Host] Velocidad de nodo inválida: %s\n", text);
        return -1;
    }

    *speed = (int)value;
    return 0;
}

static void print_assignment(const TaskAssignment *assignment)
{
    if (assignment->accepted)
    {
        printf("Tarea %d aceptada -> Nodo %d | C=%d | U=%.4f | %s\n",
               assignment->task_id,
               assignment->node_id,
               assignment->cost_ticks,
               assignment->utilization,
               assignment->reason);
    }
    else
    {
        printf("Tarea %d rechazada -> %s\n",
               assignment->task_id,
               assignment->reason);
    }
}

static int initialize_planner(int argc,
                              char *argv[],
                              Config *config,
                              PlannerState *planner,
                              TaskAssignment assignments[],
                              int *task_count)
{
    if (load_config(argv[1], config) != 0)
        return -1;

    configured_node_count = config->node2_port[0] == '\0' ? 1 : HOST_NODE_COUNT;
    int expected_argc = configured_node_count + 2;
    bool has_failure_option = argc == expected_argc + 2;
    if (argc != expected_argc && !has_failure_option)
    {
        fprintf(stderr,
                "Uso: %s <config> <vel_nodo1> [vel_nodo2] [--fail nodo]\n",
                argv[0]);
        return -1;
    }

    int failure_option_index = expected_argc;
    if (has_failure_option && strcmp(argv[failure_option_index], "--fail") != 0)
    {
        fprintf(stderr, "[Host] Opción inválida: %s\n", argv[failure_option_index]);
        return -1;
    }

    int node_speeds[HOST_NODE_COUNT] = {0};
    if (parse_node_speed(argv[2], &node_speeds[0]) != 0)
        return -1;
    if (configured_node_count == HOST_NODE_COUNT &&
        parse_node_speed(argv[3], &node_speeds[1]) != 0)
        return -1;

    NodeInfo nodes[HOST_NODE_COUNT] = {0};
    for (int i = 0; i < configured_node_count; i++)
        nodes[i] = (NodeInfo){i + 1, node_speeds[i], 1};

    if (planner_init(planner, nodes, configured_node_count, config) != 0)
    {
        fprintf(stderr, "[Host] No se pudo inicializar el planificador\n");
        return -1;
    }

    Task tasks[MAX_TASKS];
    *task_count = load_tasks(config->task_file, tasks, MAX_TASKS);
    if (*task_count < 0)
        return -1;

    int accepted = planner_load_initial_tasks(planner,
                                              tasks,
                                              *task_count,
                                              assignments);
    if (accepted < 0)
    {
        fprintf(stderr, "[Host] No se pudieron planificar las tareas iniciales\n");
        return -1;
    }

    printf("=== Planificación inicial cargada ===\n");
    for (int i = 0; i < *task_count; i++)
        print_assignment(&assignments[i]);
    printf("Aceptadas inicialmente: %d de %d\n\n", accepted, *task_count);

    return 0;
}

static int copy_planner_to_shared_memory(const PlannerState *planner,
                                         shared_system_config_t *config)
{
    const int shared_task_capacity =
        (int)(sizeof config->nodes[0].tasks / sizeof config->nodes[0].tasks[0]);
    if (planner->node_count < 0 || planner->node_count > SHARED_NUM_NODES)
    {
        fprintf(stderr, "[Host] Cantidad de nodos fuera de capacidad de memoria compartida\n");
        return -1;
    }

    for (int i = 0; i < SHARED_NUM_NODES; i++)
    {
        config->nodes[i].node_id = -1;
        config->nodes[i].task_count = 0;
    }

    for (int node_index = 0; node_index < planner->node_count; node_index++)
    {
        const NodePlan *plan = &planner->nodes[node_index];
        if (plan->task_count < 0 || plan->task_count > shared_task_capacity)
        {
            fprintf(stderr,
                    "[Host] Nodo %d tiene %d tareas, pero la memoria compartida permite %d\n",
                    plan->info.id,
                    plan->task_count,
                    shared_task_capacity);
            return -1;
        }

        node_task_list_t *node_list = &config->nodes[node_index];
        node_list->node_id = plan->info.id;
        node_list->task_count = plan->task_count;

        for (int task_index = 0; task_index < plan->task_count; task_index++)
        {
            const Task *task = &plan->tasks[task_index];
            const int cost_ticks = plan->costs[task_index];

            if (task->id < 0 || task->id >= UINT8_MAX ||
                cost_ticks < 0 || cost_ticks > UINT16_MAX ||
                task->period <= 0 || task->period > UINT16_MAX ||
                task->deadline <= 0 || task->deadline > UINT16_MAX)
            {
                fprintf(stderr,
                        "[Host] Parámetros fuera de rango para la tarea %d del nodo %d\n",
                        task->id,
                        plan->info.id);
                return -1;
            }

            task_config_t *shared_task = &node_list->tasks[task_index];
            shared_task->task_id = (uint8_t)task->id;
            shared_task->kind = (uint8_t)task->type;
            shared_task->cost_ticks = (uint16_t)cost_ticks;
            shared_task->period = (uint16_t)task->period;
            shared_task->deadline = (uint16_t)task->deadline;
            shared_task->assigned = false;
        }
    }

    return 0;
}

static int create_shared_state(const PlannerState *planner,
                               shared_memory_t *shared_memory)
{
    if (shared_memory_create(shared_memory, SHM_NAME) != 0)
    {
        perror("[Host] No se pudo crear memoria compartida");
        return -1;
    }

    shared_system_config_t config = {0};
    if (copy_planner_to_shared_memory(planner, &config) != 0)
    {
        shared_memory_destroy(shared_memory);
        return -1;
    }
    if (shared_memory_write_config(shared_memory, &config) != 0)
    {
        perror("[Host] No se pudo inicializar la configuración compartida");
        shared_memory_destroy(shared_memory);
        return -1;
    }

    return 0;
}

static void scheduler_process(void *arg)
{
    scheduler_process_args_t *args = arg;

    printf("[Scheduler] Planificador iniciado (PID: %ld)\n", (long)getpid());
    planner_print_state(args->planner);

    while (!process_manager_shutdown_requested(&process_manager))
    {
        sleep(SCHEDULER_REPORT_INTERVAL_SECONDS);
        if (process_manager_shutdown_requested(&process_manager))
            break;
        printf("[Scheduler] Proceso activo; esperando lógica de planificación dinámica.\n");
    }
}

static void node_driver_process(void *arg)
{
    node_driver_args_t *args = arg;
    printf("[Host] Iniciando driver del nodo %d en %s (PID: %ld)\n",
           args->node_id,
           args->tty_device,
           (long)getpid());
    node_driver_run(args->node_id,
                    args->tty_device,
                    args->tx_rb,
                    args->shm_name,
                    &args->driver_config);
}

// Temp (Dummy)
static void report_process(void *arg)
{
    shared_memory_t *shared_memory = arg;
    printf("[Report] Proceso de reporte iniciado (PID: %ld); reporte detallado pendiente.\n",
           (long)getpid());

    while (!process_manager_shutdown_requested(&process_manager))
    {
        sleep(SCHEDULER_REPORT_INTERVAL_SECONDS);
        if (process_manager_shutdown_requested(&process_manager))
            break;

        for (int node_id = 1; node_id <= configured_node_count; node_id++)
        {
            shared_node_status_t status;
            if (shared_memory_read_node_status(shared_memory, node_id, &status) != 0)
            {
                perror("[Report] No se pudo leer el estado del nodo");
                _exit(EXIT_FAILURE);
            }

            printf("[Report] Nodo %d: %s, tick=%u, tramas inválidas=%u, "
                   "reportes perdidos=%u, "
                   "tareas completadas=%llu, plazos perdidos=%llu, "
                   "disparos esporádicos ignorados=%llu.\n",
                   node_id,
                   status.active ? "activo" : "inactivo",
                   status.last_tick,
                   status.bad_frames,
                   status.missed_reports,
                   (unsigned long long)status.completed_tasks,
                   (unsigned long long)status.missed_deadlines,
                   (unsigned long long)status.ignored_sporadic_triggers);
            if (status.has_last_event)
            {
                printf("[Report] Nodo %d último evento: tipo=%u, tarea=%u, tick=%u, aux=%u.\n",
                       node_id,
                       status.last_event.event_type,
                       status.last_event.task_id,
                       status.last_event.tick,
                       status.last_event.aux);
            }
        }
    }
}

static int launch_processes(const Config *config,
                            const PlannerState *planner,
                            shared_memory_t *shared_memory)
{
    if (config->tick_ms > UINT16_MAX ||
        config->tick_report_period > UINT16_MAX)
    {
        fprintf(stderr, "[Host] tick_ms and tick_report_period must fit in uint16.\n");
        return -1;
    }

    scheduler_process_args_t scheduler_args = {
        .planner = planner};

    if (process_manager_create(&process_manager, scheduler_process, &scheduler_args) < 0)
        return -1;

    const char *configured_ports[HOST_NODE_COUNT] = {config->node1_port,
                                                     config->node2_port};
    for (int i = 0; i < configured_node_count; i++)
    {
        node_driver_args_t driver_args = {
            .node_id = i + 1,
            .tty_device = configured_ports[i],
            .tx_rb = shared_memory_tx_ring_buffer(shared_memory, i + 1),
            .shm_name = SHM_NAME,
            .driver_config = {
                .tick_ms = (uint16_t)config->tick_ms,
                .algorithm = config->algorithm == SCHED_EDF
                                 ? ALGORITHM_EDF
                                 : ALGORITHM_RMS,
                .report_period = (uint16_t)config->tick_report_period}};
        if (driver_args.tx_rb == NULL)
        {
            perror("[Host] No se pudo obtener el buffer del driver");
            return -1;
        }

        if (process_manager_create(&process_manager, node_driver_process, &driver_args) < 0)
            return -1;
    }

    if (process_manager_create(&process_manager, report_process, shared_memory) < 0)
        return -1;

    return 0;
}

static void destroy_shared_state(shared_memory_t *shared_memory)
{
    if (shared_memory != NULL && shared_memory->data != NULL)
    {
        if (shared_memory_destroy(shared_memory) < 0)
            perror("[Host] Error destruyendo la memoria compartida");
    }
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);

    Config config;
    PlannerState planner;
    TaskAssignment assignments[MAX_TASKS];
    int task_count = 0;

    if (process_manager_init(&process_manager) != 0)
    {
        perror("[Host] No se pudo inicializar el administrador de procesos");
        return EXIT_FAILURE;
    }

    if (initialize_planner(argc,
                           argv,
                           &config,
                           &planner,
                           assignments,
                           &task_count) != 0)
        return EXIT_FAILURE;

    (void)task_count;

    shared_memory_t shared_memory;
    if (create_shared_state(&planner, &shared_memory) != 0)
        return EXIT_FAILURE;

    if (process_manager_install_signal_handlers(&process_manager) != 0)
    {
        perror("[Host] No se pudieron instalar los manejadores de señales");
        destroy_shared_state(&shared_memory);
        return EXIT_FAILURE;
    }

    printf("[Host] Orquestador iniciado (PID: %ld).\n", (long)getpid());

    if (launch_processes(&config, &planner, &shared_memory) != 0)
    {
        process_manager_stop(&process_manager);
        process_manager_wait(&process_manager);
        destroy_shared_state(&shared_memory);
        return EXIT_FAILURE;
    }

    printf("[Host] Scheduler, %d driver(s) y proceso de reporte iniciados.\n",
           configured_node_count);
    int exit_status = process_manager_wait(&process_manager);
    destroy_shared_state(&shared_memory);
    printf("[Host] Apagado completo.\n");

    return exit_status;
}
