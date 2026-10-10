#include <stdio.h>

#include "config.h"
#include "task.h"
#include "task_parser.h"

/* Muestra en consola la config */
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
    printf("Periodo de reporte: %d ticks\n",
           config->tick_report_period);
    printf("Reportes perdidos permitidos: %d\n",
           config->max_missed_reports);
    printf("Archivo de tareas: %s\n\n",
           config->task_file);
}

/* Muestra las tareas cargadas */
static void print_tasks(const Task tasks[], int task_count)
{
    printf("=== Tareas cargadas (%d) ===\n", task_count);

    for (int i = 0; i < task_count; i++)
    {
        printf(
            "Tarea %d | %c | W=%d | T=%d | D=%d | Crit=%d\n",
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

    if (argc != 2)
    {
        fprintf(stderr,
                "Uso: %s <archivo_configuracion>\n",
                argv[0]);
        return 1;
    }

    Config config;

    if (load_config(argv[1], &config) != 0)
    {
        return 1;
    }

    print_config(&config);

    Task tasks[MAX_TASKS];

    int task_count = load_tasks(
        config.task_file,
        tasks,
        MAX_TASKS);

    if (task_count < 0)
    {
        return 1;
    }

    print_tasks(tasks, task_count);

    return 0;
}
