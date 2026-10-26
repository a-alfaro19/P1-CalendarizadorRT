#include <fcntl.h>
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

#define SHM_NAME "/shm_system_config"       //< Shared memory object name for the system configuration.
#define HOST_MAX_NODE_COUNT 2               //< Maximum number of nodes supported by the host application.
#define SCHEDULER_REPORT_INTERVAL_SECONDS 5 //< Interval in seconds for the scheduler report process to print status.

/**
 * @brief Arguments for the host application.
 */
typedef struct
{
    const char *config_path;                          ///< Path to the configuration file.
    const char *node_speed_args[HOST_MAX_NODE_COUNT]; ///< Array of node speed arguments.
    int node_speed_count;                             ///< Number of node speed arguments.
} host_arguments_t;

// ===================================
// Child process arguments structures
// ===================================

/**
 * @brief Arguments for the scheduler process.
 */
typedef struct
{
    const PlannerState *planner;    //< Pointer to the planner state structure.
    shared_memory_t *shared_memory; ///< Pointer to the shared memory handle for the system configuration.
} scheduler_process_args_t;         // TODO: Use shared memory in the scheduler process instead of passing the planner state directly.

/**
 * @brief Arguments for the node driver process.
 */
typedef struct
{
    int node_id;                        ///< Node identifier.
    const char *tty_device;             ///< TTY device path for the node driver.
    ring_buffer_t *tx_rb;               ///< Pointer to the transmit ring buffer for the node driver.
    const char *shm_name;               ///< Shared memory object name for the system configuration.
    node_driver_config_t driver_config; ///< Configuration parameters for the node driver.
} node_driver_args_t;

typedef struct // TODO: Update when report process is implemented.
{
    shared_memory_t *shared_memory;
    int node_count;
} report_process_args_t;

// ===================================
// Global process manager instance
// ===================================

static process_manager_t process_manager; //< Process manager for managing child processes (scheduler, node drivers, and report process).

// =================================
// Helper functions
// =================================

/**
 * @brief Prints the assignment of a task to a node.
 *
 * @param assignment Pointer to the task assignment structure.
 */
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

/**
 * @brief Parses command-line arguments for the host application.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @param arguments Pointer to the host_arguments_t structure to populate.
 * @return 0 on success, -1 on failure.
 */
static int parse_arguments(int argc, char *argv[], host_arguments_t *arguments)
{
    // Validate input parameters
    if (argv == NULL || arguments == NULL || argc < 3)
    {
        fprintf(stderr,
                "Uso: %s <config> <vel_nodo1> [vel_nodo2] [--fail nodo]\n",
                argv != NULL && argc > 0 ? argv[0] : "host_app");
        return -1;
    }

    // Check for the presence of the "--fail" option
    int fail_option_index = -1;
    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--fail") == 0)
        {
            fail_option_index = i;
            break;
        }
    }
    bool has_fail_option = fail_option_index >= 0;

    // Validate the number of node speed arguments
    int speed_count = fail_option_index >= 0
                          ? fail_option_index - 2
                          : argc - 2;
    if (speed_count < 1 || speed_count > HOST_MAX_NODE_COUNT ||
        (has_fail_option &&
         (fail_option_index != argc - 2 || argv[argc - 1][0] == '\0')))
    {
        fprintf(stderr,
                "Uso: %s <config> <vel_nodo1> [vel_nodo2] [--fail nodo]\n",
                argv[0]);
        return -1;
    }

    // Populate the arguments structure
    arguments->config_path = argv[1];
    arguments->node_speed_count = speed_count;
    for (int i = 0; i < speed_count; i++)
        arguments->node_speed_args[i] = argv[i + 2];

    return 0;
}

// =================================
// Child process functions
// =================================

/**
 * @brief Function executed by the scheduler process
 *
 * @param arg Pointer to scheduler_process_args_t structure.
 */
static void scheduler_process(void *arg)
{
    scheduler_process_args_t *args = arg;

    printf("[Scheduler] Planificador iniciado (PID: %ld)\n", (long)getpid());
    planner_print_state(args->planner);

    while (!process_manager_shutdown_requested(&process_manager))
    {
        if (process_manager_shutdown_requested(&process_manager))
            break;
    }
}

/**
 * @brief Function executed by the node driver process
 *
 * @param arg Pointer to node_driver_args_t structure.
 */
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
    report_process_args_t *args = arg;
    printf("[Report] Proceso de reporte iniciado (PID: %ld); reporte detallado pendiente.\n",
           (long)getpid());

    while (!process_manager_shutdown_requested(&process_manager))
    {
        sleep(SCHEDULER_REPORT_INTERVAL_SECONDS);
        if (process_manager_shutdown_requested(&process_manager))
            break;

        for (int node_id = 1; node_id <= args->node_count; node_id++)
        {
            shared_node_status_t status;
            if (shared_memory_read_node_status(args->shared_memory, node_id, &status) != 0)
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

/**
 * @brief Launches the scheduler, node driver, and report processes.
 *
 * @param config Pointer to the HostConfig structure.
 * @param planner Pointer to the PlannerState structure.
 * @param shared_memory Pointer to the shared_memory_t structure.
 * @return 0 on success, -1 on failure.
 */
static int launch_processes(const HostConfig *config,
                            const PlannerState *planner,
                            shared_memory_t *shared_memory)
{
    // Create scheduler process
    scheduler_process_args_t scheduler_args = {
        .planner = planner};
    if (process_manager_create(&process_manager, scheduler_process, &scheduler_args) < 0)
        return -1;

    // Create node driver processes
    const char *configured_ports[HOST_MAX_NODE_COUNT] = {config->node1_port,
                                                         config->node2_port};
    for (int i = 0; i < planner->node_count; i++)
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

    // Create report process
    report_process_args_t report_args = {
        .shared_memory = shared_memory,
        .node_count = planner->node_count};
    if (process_manager_create(&process_manager, report_process, &report_args) < 0)
        return -1;

    return 0;
}

// ===================================
// Shared memory management functions
// ===================================

/**
 * @brief Copies the planner state to the shared memory configuration structure.
 *
 * @param planner Pointer to the planner state structure.
 * @param config Pointer to the shared memory configuration structure.
 * @return 0 on success, -1 on failure.
 */
static int copy_planner_state_to_shared_memory_config(const PlannerState *planner,
                                                      shared_system_config_t *config)
{
    // Validate node count against shared memory capacity
    if (planner->node_count < 0 || planner->node_count > SHARED_NUM_NODES)
    {
        fprintf(stderr, "[Host] Cantidad de nodos fuera de capacidad de memoria compartida\n");
        return -1;
    }

    // Clean nodes state on shared memory config
    for (int i = 0; i < SHARED_NUM_NODES; i++)
    {
        config->nodes[i].node_id = -1;
        config->nodes[i].task_count = 0;
    }

    for (int node_index = 0; node_index < planner->node_count; node_index++) // for each node
    {
        // Read the node plan from planner state
        const NodePlan *plan = &planner->nodes[node_index];

        // Validate node task count against shared memory capacity
        if (plan->task_count < 0 ||
            plan->task_count > SHARED_MAX_TASKS_PER_NODE)
        {
            fprintf(stderr,
                    "[Host] Nodo %d tiene %d tareas, pero la memoria compartida permite %d\n",
                    plan->info.id,
                    plan->task_count,
                    SHARED_MAX_TASKS_PER_NODE);
            return -1;
        }

        // Copy node information to shared memory configuration
        node_task_list_t *node_list = &config->nodes[node_index];
        node_list->node_id = plan->info.id;
        node_list->task_count = plan->task_count;

        for (int task_index = 0; task_index < plan->task_count; task_index++) // for each task
        {
            // Read task from the node plan
            const Task *task = &plan->tasks[task_index];
            const int cost_ticks = plan->costs[task_index];

            // Validate task parameters
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

            // Copy task configuration to shared memory
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

/**
 * @brief Creates and initializes the shared memory state based on the planner.
 *
 * @param planner Planner state to copy into shared memory.
 * @param shared_memory Shared memory handle to initialize.
 * @return 0 on success, or -1 on error.
 */
static int create_shared_state(const PlannerState *planner,
                               shared_memory_t *shared_memory)
{
    // Initialize shared memory
    if (shared_memory_create(shared_memory, SHM_NAME) != 0)
    {
        perror("[Host] No se pudo crear memoria compartida");
        return -1;
    }

    // Copy initial planner state to config struct
    shared_system_config_t config = {0};
    if (copy_planner_state_to_shared_memory_config(planner, &config) != 0)
    {
        shared_memory_destroy(shared_memory);
        return -1;
    }

    // Write the initial configuration
    if (shared_memory_write_config(shared_memory, &config) != 0)
    {
        perror("[Host] No se pudo inicializar la configuración compartida");
        shared_memory_destroy(shared_memory);
        return -1;
    }

    return 0;
}

/**
 * @brief Destroys the shared memory state.
 *
 * @param shared_memory Shared memory handle to destroy.
 */
static void destroy_shared_state(shared_memory_t *shared_memory)
{
    if (shared_memory_destroy(shared_memory) < 0)
        perror("[Host] Error destruyendo la memoria compartida");
}

// =================================
// Main function
// =================================

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);

    HostConfig config;
    PlannerState planner;
    TaskAssignment assignments[MAX_TASKS];

    // Parse command-line arguments
    host_arguments_t arguments = {0};
    if (parse_arguments(argc, argv, &arguments) != 0)
        return EXIT_FAILURE;

    // Initialize Process Manager
    if (process_manager_init(&process_manager) != 0)
    {
        perror("[Host] No se pudo inicializar el administrador de procesos");
        return EXIT_FAILURE;
    }

    // Initialize Scheduler
    int task_count = 0;
    int accepted_count = 0;
    if (planner_initialize(&planner,
                           &config,
                           arguments.config_path,
                           arguments.node_speed_args,
                           arguments.node_speed_count,
                           assignments,
                           &task_count,
                           &accepted_count) != 0)
        return EXIT_FAILURE;

    printf("=== Planificación inicial cargada ===\n");
    for (int i = 0; i < task_count; i++)
        print_assignment(&assignments[i]);
    printf("Aceptadas inicialmente: %d de %d\n\n", accepted_count, task_count);

    // Initialize shared memory
    shared_memory_t shared_memory;
    if (create_shared_state(&planner, &shared_memory) != 0)
        return EXIT_FAILURE;

    // Install signal handlers for a complete shutdown
    if (process_manager_install_signal_handlers(&process_manager) != 0)
    {
        perror("[Host] No se pudieron instalar los manejadores de señales");
        destroy_shared_state(&shared_memory);
        return EXIT_FAILURE;
    }

    printf("[Host] Host iniciado (PID: %ld).\n", (long)getpid());

    // Launch host processes (scheduler, node drivers, and report process)
    if (launch_processes(&config, &planner, &shared_memory) != 0)
    {
        process_manager_stop(&process_manager);
        process_manager_wait(&process_manager);
        destroy_shared_state(&shared_memory);
        return EXIT_FAILURE;
    }

    // Wait for all child processes to finish
    int exit_status = process_manager_wait(&process_manager);

    // Cleanup resources
    destroy_shared_state(&shared_memory);

    printf("[Host] Apagado por completo.\n");
    return exit_status;
}
