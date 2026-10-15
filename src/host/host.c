#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include <protocol/protocol.h>
#include "node_driver.h"

typedef struct
{
    int node_id;
    const char *tty_device;
    ring_buffer_t *tx_rb;
} node_driver_args_t;

void run_node_driver_wrapper(void *arg)
{
    node_driver_args_t *args = (node_driver_args_t *)arg;
    node_driver_run(args->node_id, args->tty_device, args->tx_rb);
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

int main(void)
{
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
