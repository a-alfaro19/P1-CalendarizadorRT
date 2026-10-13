#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <protocol/protocol.h>

#define NUM_NODES 2

// Children's PID
pid_t node_pids[NUM_NODES];

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

// Función que ejecuta el proceso Controlador de Nodo
void run_node_driver(int node_id)
{
    // Aquí inicializarías la comunicación serial específica para el nodo (ej. /dev/ttyUSB0, /dev/ttyUSB1)
    printf("[Driver Nodo %d] Inició proceso (PID: %d)\n", node_id, getpid());

    // Bucle principal del driver de nodo (recibir tramas, parsear, etc.)
    while (1)
    {
        // Simulación de trabajo del driver
        sleep(2);
        printf("[Driver Nodo %d] Escuchando puerto serial...\n", node_id);
    }

    exit(EXIT_SUCCESS);
}

int main(void)
{
    // Registrar manejador para Ctrl+C
    signal(SIGINT, handle_sigint);

    printf("[Host Principal] Iniciando Calendarizador en Tiempo Real (PID: %d)\n", getpid());

    // Create proccess (Node controllers)
    for (int i = 0; i < NUM_NODES; i++)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            perror("[Error] Something went wrong creating process.");
            handle_sigint(SIGTERM);
        }
        else if (pid == 0)
        {
            // Proceso Hijo: Controlador de Nodo
            run_node_driver(i + 1);
        }
        else
        {
            // Proceso Padre: Guarda el PID del hijo
            node_pids[i] = pid;
        }
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
