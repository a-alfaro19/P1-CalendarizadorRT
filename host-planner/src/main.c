#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "planner.h"
#include "task.h"
#include "task_parser.h"

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
     * Uso normal:
     *   ./host config/config.txt 20 12
     *
     * Simular una caida:
     *   ./host config/config.txt 20 12 --fail 1
     *
     * Las velocidades siguen entrando por argumentos solamente para
     * prueba.
     */
    if (argc != 4 && argc != 6)
    {
        fprintf(stderr,
                "Uso: %s <config> <vel_nodo1> <vel_nodo2> [--fail nodo]\n",
                argv[0]);
        return 1;
    }

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
    int accepted = planner_load_initial_tasks(&planner,
                                              tasks,
                                              task_count,
                                              assignments);
    if (accepted < 0)
        return 1;

    printf("=== Planificacion inicial ===\n");
    for (int i = 0; i < task_count; i++)
        print_assignment(&assignments[i]);

    printf("\nAceptadas inicialmente: %d de %d\n", accepted, task_count);
    planner_print_state(&planner);

    printf("\n=== Prueba de tarea agregada durante la ejecucion ===\n");

    Task manual_task = {99, TASK_PERIODIC, 25, 30, 30, 2};
    TaskAssignment manual_assignment;

    if (planner_add_task(&planner, &manual_task, &manual_assignment) < 0)
        return 1;

    print_assignment(&manual_assignment);
    planner_print_state(&planner);

    if (argc == 6)
    {
        if (strcmp(argv[4], "--fail") != 0)
        {
            fprintf(stderr, "Opcion desconocida: %s\n", argv[4]);
            return 1;
        }

        int failed_node = atoi(argv[5]);
        printf("\n=== Simulacion de caida del nodo %d ===\n", failed_node);

        TaskAssignment recovery_results[MAX_TASKS_PER_NODE];
        int recovery_count = 0;

        int relocated = planner_handle_node_failure(&planner,
                                                    failed_node,
                                                    recovery_results,
                                                    &recovery_count);
        if (relocated < 0)
        {
            fprintf(stderr, "No se pudo procesar la caida del nodo %d\n",
                    failed_node);
            return 1;
        }

        for (int i = 0; i < recovery_count; i++)
            print_assignment(&recovery_results[i]);

        printf("\nReubicadas: %d de %d tareas afectadas\n",
               relocated,
               recovery_count);

        planner_print_state(&planner);
    }

    return 0;
}
