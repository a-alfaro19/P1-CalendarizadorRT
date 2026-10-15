#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "planner.h"
#include "task.h"
#include "task_parser.h"

static void print_assignment(const TaskAssignment *assignment)
{
    if (assignment->accepted)
        printf("Tarea %d aceptada -> Nodo %d | C=%d | U=%.4f\n",
               assignment->task_id,
               assignment->node_id,
               assignment->cost_ticks,
               assignment->utilization);
    else
        printf("Tarea %d rechazada -> %s\n",
               assignment->task_id,
               assignment->reason);
}

int main(int argc, char *argv[])
{
    /*
     * Las velocidades siguen simuladas por argumentos hasta integrar
     * la calibracion real enviada por los microcontroladores
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
        return 1;

    int node1_speed = atoi(argv[2]);
    int node2_speed = atoi(argv[3]);

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
    int accepted = planner_load_initial_tasks(&planner,
                                              tasks,
                                              task_count,
                                              assignments);
    if (accepted < 0)
        return 1;

    printf("=== Planificacion inicial ===\n");
    for (int i = 0; i < task_count; i++)
        print_assignment(&assignments[i]);

    planner_print_state(&planner);

    /*
     * Simula el modo manual: el sistema ya tiene tareas corriendo y
     * se agrega una nueva sin borrar las anteriores.
     */
    printf("\n=== Prueba de tarea agregada durante la ejecucion ===\n");

    Task manual_task = {99, TASK_PERIODIC, 25, 30, 30, 2};
    TaskAssignment manual_assignment;

    if (planner_add_task(&planner, &manual_task, &manual_assignment) < 0)
        return 1;

    print_assignment(&manual_assignment);
    planner_print_state(&planner);

    return 0;
}
