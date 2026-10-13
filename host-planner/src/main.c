#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "planner.h"
#include "task.h"
#include "task_parser.h"

static void print_config(const Config *config)
{
    printf("=== Configuracion del host ===\n");

    printf("Algoritmo: %s\n",
           config->algorithm == SCHED_RMS ? "RMS" : "EDF");

    printf("Heuristica: %s\n",
           config->heuristic == HEURISTIC_FIRST_FIT
               ? "FIRST_FIT"
               : "WORST_FIT");

    printf("Tick: %d ms\n", config->tick_ms);
    printf("Puerto nodo 1: %s\n", config->node1_port);
    printf("Puerto nodo 2: %s\n", config->node2_port);
    printf("Periodo de reporte: %d ticks\n", config->tick_report_period);
    printf("Reportes perdidos permitidos: %d\n", config->max_missed_reports);
    printf("Archivo de tareas: %s\n\n", config->task_file);
}

static void print_tasks(const Task tasks[], int task_count)
{
    printf("=== Tareas cargadas (%d) ===\n", task_count);

    for (int i = 0; i < task_count; i++)
    {
        printf("Tarea %d | %c | W=%d | T=%d | D=%d | Crit=%d\n",
               tasks[i].id,
               tasks[i].type == TASK_PERIODIC ? 'P' : 'S',
               tasks[i].work,
               tasks[i].period,
               tasks[i].deadline,
               tasks[i].criticality);
    }
}

int main(int argc, char *argv[])
{
    /*
     * En la version final s(n) vendra de la calibracion real enviada por
     * cada microcontrolador. Para poder probar e integrar desde ya, este
     * commit recibe temporalmente esas velocidades como argumentos.
     *
     * Ejemplo: ./host config/config.txt 20 12
     */
    if (argc != 4)
    {
        fprintf(stderr,
                "Uso: %s <archivo_config> <vel_nodo1> <vel_nodo2>\n",
                argv[0]);
        return 1;
    }

    Config config;

    if (load_config(argv[1], &config) != 0)
    {
        return 1;
    }

    int node1_speed = atoi(argv[2]);
    int node2_speed = atoi(argv[3]);

    if (node1_speed <= 0 || node2_speed <= 0)
    {
        fprintf(stderr,
                "Las velocidades de calibracion deben ser mayores que 0\n");
        return 1;
    }

    print_config(&config);

    Task tasks[MAX_TASKS];
    int task_count = load_tasks(config.task_file, tasks, MAX_TASKS);

    if (task_count < 0)
    {
        return 1;
    }

    print_tasks(tasks, task_count);

    /*
     * @Companeros deben colocar aqui las velocidades reales
     * reportadas por los nodos durante la calibracion
     */
    NodeInfo nodes[2] = {
        {1, node1_speed},
        {2, node2_speed}};

    TaskAssignment assignments[MAX_TASKS];

    int accepted = plan_tasks(tasks,
                              task_count,
                              nodes,
                              2,
                              &config,
                              assignments);

    if (accepted < 0)
    {
        fprintf(stderr, "Error durante la planificacion\n");
        return 1;
    }

    print_plan_summary(tasks,
                       task_count,
                       nodes,
                       2,
                       assignments);

    printf("\nAceptadas: %d de %d tareas\n", accepted, task_count);

    return 0;
}
