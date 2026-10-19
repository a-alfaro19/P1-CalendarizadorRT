#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>

#include <protocol/protocol.h>
#include <host/scheduler/scheduler.h>
#include <host/node_driver.h>
#include <host/shared_memory.h>

#define SHM_NAME "/shm_system_config"

typedef struct
{
    int node_id;
    const char *tty_device;
    ring_buffer_t *tx_rb;
    const char *shm_name;
} node_driver_args_t;

void run_node_driver_wrapper(void *arg)
{
    node_driver_args_t *args = (node_driver_args_t *)arg;
    node_driver_run(args->node_id, args->tty_device, args->tx_rb, args->shm_name);
}
typedef void (*thread_func_t)(void *arg);

#define NUM_NODES 2

// Children's PID
pid_t node_pids[NUM_NODES];
ring_buffer_t tx_ring_buffers[NUM_NODES];
const char *tty_devices[NUM_NODES] = {
    "/dev/pts/3", // O tu puerto serial real ej. "/dev/ttyUSB0"
    "/dev/pts/4",
    "/dev/pts/5",
    "/dev/pts/6"};

// Manejador de señales para apagado limpio (Ctrl+C)
void handle_sigint(int sig)
{
    (void)sig;
    printf("\n[Host] Señal de interrupción recibida. Terminando controladores de nodo...\n");
    for (int i = 0; i < NUM_NODES; i++)
    {
        if (node_pids[i] > 0)
        {
            kill(node_pids[i], SIGTERM);
        }
    }
    // Esperar a que todos los hijos mueran
    while (wait(NULL) > 0)
        ;
    printf("[Host] Apagado completo.\n");
    exit(0);
}

pid_t create_proccess(thread_func_t func, void *arg)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("[Error] Something went wrong creating process.");
        handle_sigint(SIGTERM);
    }
    else if (pid == 0)
    {
        // --- CHILD PROCCESS ---
        func(arg);
        exit(EXIT_SUCCESS);
    }

    return pid;
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

int main(int argc, char *argv[])
{
    /*
     * Uso esperado:
     *   ./bin/host_app config/config.txt 20 12
     *   ./bin/host_app config/config.txt 20 12 --fail 1
     */
    if (argc != 4 && argc != 6)
    {
        fprintf(stderr,
                "Uso: %s <config> <vel_nodo1> <vel_nodo2> [--fail nodo]\n",
                argv[0]);
        return 1;
    }

    // 1. Cargar configuración y planificador
    Config config;
    if (load_config(argv[1], &config) != 0)
        return 1;

    int node1_speed = atoi(argv[2]);
    int node2_speed = atoi(argv[3]);

    if (node1_speed <= 0 || node2_speed <= 0)
    {
        fprintf(stderr, "Las velocidades deben ser mayores que 0\n");
        return 1;
    }

    NodeInfo nodes[2] = {
        {1, node1_speed, 1},
        {2, node2_speed, 1}};

    PlannerState planner;
    if (planner_init(&planner, nodes, 2, &config) != 0)
    {
        fprintf(stderr, "No se pudo inicializar el planificador\n");
        return 1;
    }

    Task tasks[MAX_TASKS];
    int task_count = load_tasks(config.task_file, tasks, MAX_TASKS);
    if (task_count < 0)
        return 1;

    TaskAssignment assignments[MAX_TASKS];
    int accepted = planner_load_initial_tasks(&planner, tasks, task_count, assignments);
    if (accepted < 0)
        return 1;

    printf("=== Planificación inicial cargada ===\n");
    for (int i = 0; i < task_count; i++)
        print_assignment(&assignments[i]);
    printf("Aceptadas inicialmente: %d de %d\n\n", accepted, task_count);

    // --- CONFIGURACIÓN DE MEMORIA COMPARTIDA ---
    // 1. Limpiar por si quedó un segmento abierto anterior
    shm_unlink(SHM_NAME);

    // 2. Crear el objeto de memoria compartida
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0)
    {
        perror("[Error Host] No se pudo crear la memoria compartida");
        return 1;
    }

    // 3. Ajustar el tamaño al de nuestra estructura global del sistema
    if (ftruncate(shm_fd, sizeof(shared_system_config_t)) < 0)
    {
        perror("[Error Host] Error al redimensionar la memoria compartida");
        close(shm_fd);
        return 1;
    }

    // 4. Mapear en el espacio de direcciones del proceso padre
    void *shm_base = mmap(NULL, sizeof(shared_system_config_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd); // El descriptor ya no es necesario una vez mapeado

    if (shm_base == MAP_FAILED)
    {
        perror("[Error Host] Falló el mapeo de memoria compartida");
        return 1;
    }

    // 5. Poblar la memoria compartida con la configuración y tareas del planificador
    shared_system_config_t *sys_config = (shared_system_config_t *)shm_base;
    // (Aquí copias la información de tus nodos y tareas estructuradas desde tu objeto 'planner' hacia sys_config->nodes)
    // Ejemplo básico para estructurar los nodos:
    for (int i = 0; i < NUM_NODES; i++)
    {
        sys_config->nodes[i].node_id = i;
        // Asignas el conteo y copias las tareas correspondientes a cada nodo según el planificador...
    }

    // Registrar manejador para Ctrl+C
    signal(SIGINT, handle_sigint);

    printf("[Host] (PID: %d)\n", getpid());

    // Create Node Controller Proccess
    for (int i = 0; i < NUM_NODES; i++)
    {
        // 1. Initialize Ring Buffer
        ring_buffer_init(&tx_ring_buffers[i]);

        // 2. Set driver args
        node_driver_args_t node_driver_args = {
            .node_id = i,
            .tty_device = tty_devices[i],
            .tx_rb = &tx_ring_buffers[i]};

        // 3. Create process with driver & driver args
        node_pids[i] = create_proccess(run_node_driver_wrapper, &node_driver_args);
    }

    // --- Lógica del Proceso Planificador (Host Padre) ---
    printf("[Planificador] Todos los drivers de nodo han sido desplegados. Ejecutando lógica de planificación...\n");

    while (1)
    {
        // El planificador gestionará aquí las tareas, tiempos y sincronización por memoria compartida
        sleep(5);
        printf("[Planificador] Ciclo de planificación activo...\n");
    }

    return 0;
}
