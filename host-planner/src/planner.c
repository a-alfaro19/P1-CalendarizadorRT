#include <stdio.h>
#include <string.h>

#include "admission.h"
#include "planner.h"
#include "task_parser.h"

/* Carga temporal de un nodo durante la planificacion */
typedef struct
{
    Task tasks[MAX_TASKS];
    int costs[MAX_TASKS];
    int count;
} NodeLoad;

/* Busca el nodo con mayor velocidad de calibracion */
static int find_fastest_node(const NodeInfo nodes[], int node_count)
{
    int fastest = 0;

    for (int i = 1; i < node_count; i++)
    {
        if (nodes[i].speed_ut_per_tick > nodes[fastest].speed_ut_per_tick)
        {
            fastest = i;
        }
    }

    return fastest;
}

/*
 * Ordena indices de tareas de mayor a menor utilizacion medida en el nodo
 * mas rapido
 */
static void build_task_order(const Task tasks[],
                             int task_count,
                             int fastest_speed,
                             int order[])
{
    for (int i = 0; i < task_count; i++)
    {
        order[i] = i;
    }

    /* Se usa selection sort porque es simple de leer y suficiente para MAX_TASKS=100. */
    for (int i = 0; i < task_count - 1; i++)
    {
        int best = i;

        for (int j = i + 1; j < task_count; j++)
        {
            int cost_best = task_cost_ticks(&tasks[order[best]], fastest_speed);
            int cost_candidate = task_cost_ticks(&tasks[order[j]], fastest_speed);

            double util_best = task_utilization(&tasks[order[best]], cost_best);
            double util_candidate = task_utilization(&tasks[order[j]], cost_candidate);

            /*
             * Para mejor criterio fijo se escoge el de
             * mayor criticidad primero y luego menor id
             */
            if (util_candidate > util_best ||
                (util_candidate == util_best &&
                 tasks[order[j]].criticality > tasks[order[best]].criticality) ||
                (util_candidate == util_best &&
                 tasks[order[j]].criticality == tasks[order[best]].criticality &&
                 tasks[order[j]].id < tasks[order[best]].id))
            {
                best = j;
            }
        }

        if (best != i)
        {
            int temp = order[i];
            order[i] = order[best];
            order[best] = temp;
        }
    }
}

/* Prueba una tarea candidata */
static int candidate_is_admissible(const NodeLoad *load,
                                   const Task *candidate,
                                   int candidate_cost,
                                   SchedulingAlgorithm algorithm)
{
    Task temp_tasks[MAX_TASKS];
    int temp_costs[MAX_TASKS];

    if (load->count >= MAX_TASKS)
    {
        return 0;
    }

    for (int i = 0; i < load->count; i++)
    {
        temp_tasks[i] = load->tasks[i];
        temp_costs[i] = load->costs[i];
    }

    temp_tasks[load->count] = *candidate;
    temp_costs[load->count] = candidate_cost;

    return is_admissible(algorithm,
                         temp_tasks,
                         temp_costs,
                         load->count + 1);
}

static double node_utilization(const NodeLoad *load)
{
    return total_utilization(load->tasks, load->costs, load->count);
}

/* First-Fit: nodos de mayor a menor velocidad y toma el primero que admite */
static int choose_first_fit(const Task *task,
                            const NodeInfo nodes[],
                            int node_count,
                            const NodeLoad loads[],
                            SchedulingAlgorithm algorithm)
{
    int node_order[MAX_NODES];

    for (int i = 0; i < node_count; i++)
    {
        node_order[i] = i;
    }

    for (int i = 0; i < node_count - 1; i++)
    {
        for (int j = i + 1; j < node_count; j++)
        {
            if (nodes[node_order[j]].speed_ut_per_tick >
                nodes[node_order[i]].speed_ut_per_tick)
            {
                int temp = node_order[i];
                node_order[i] = node_order[j];
                node_order[j] = temp;
            }
        }
    }

    for (int i = 0; i < node_count; i++)
    {
        int node_index = node_order[i];
        int cost = task_cost_ticks(task, nodes[node_index].speed_ut_per_tick);

        if (candidate_is_admissible(&loads[node_index],
                                    task,
                                    cost,
                                    algorithm))
        {
            return node_index;
        }
    }

    return -1;
}

/*
 * Worst-Fit: entre los nodos admisibles escoge el que quede con mayor
 * capacidad libre despues de agregar la tarea
 */
static int choose_worst_fit(const Task *task,
                            const NodeInfo nodes[],
                            int node_count,
                            const NodeLoad loads[],
                            SchedulingAlgorithm algorithm)
{
    int selected = -1;
    double best_free_capacity = -1.0;

    for (int i = 0; i < node_count; i++)
    {
        int cost = task_cost_ticks(task, nodes[i].speed_ut_per_tick);

        if (!candidate_is_admissible(&loads[i], task, cost, algorithm))
        {
            continue;
        }

        double current_utilization = node_utilization(&loads[i]);
        double task_util = task_utilization(task, cost);
        double free_capacity = 1.0 - (current_utilization + task_util);

        /* Desempate: se prefiere el nodo mas rapido */
        if (selected == -1 ||
            free_capacity > best_free_capacity ||
            (free_capacity == best_free_capacity &&
             nodes[i].speed_ut_per_tick > nodes[selected].speed_ut_per_tick))
        {
            selected = i;
            best_free_capacity = free_capacity;
        }
    }

    return selected;
}

static void add_to_node_load(NodeLoad *load, const Task *task, int cost)
{
    load->tasks[load->count] = *task;
    load->costs[load->count] = cost;
    load->count++;
}

int plan_tasks(const Task tasks[],
               int task_count,
               const NodeInfo nodes[],
               int node_count,
               const Config *config,
               TaskAssignment assignments[])
{
    if (tasks == NULL || nodes == NULL || config == NULL ||
        assignments == NULL || task_count < 0 || task_count > MAX_TASKS ||
        node_count <= 0 || node_count > MAX_NODES)
    {
        return -1;
    }

    for (int i = 0; i < node_count; i++)
    {
        if (nodes[i].speed_ut_per_tick <= 0)
        {
            return -1;
        }
    }

    NodeLoad loads[MAX_NODES] = {0};
    int order[MAX_TASKS];

    int fastest_node = find_fastest_node(nodes, node_count);
    build_task_order(tasks,
                     task_count,
                     nodes[fastest_node].speed_ut_per_tick,
                     order);

    for (int i = 0; i < task_count; i++)
    {
        assignments[i].task_id = tasks[i].id;
        assignments[i].accepted = 0;
        assignments[i].node_id = -1;
        assignments[i].cost_ticks = -1;
        assignments[i].utilization = 0.0;
        snprintf(assignments[i].reason,
                 REJECTION_REASON_LENGTH,
                 "Aun no procesada");
    }

    int accepted_count = 0;

    for (int position = 0; position < task_count; position++)
    {
        int task_index = order[position];
        const Task *task = &tasks[task_index];
        int selected_node = -1;

        if (config->heuristic == HEURISTIC_FIRST_FIT)
        {
            selected_node = choose_first_fit(task,
                                             nodes,
                                             node_count,
                                             loads,
                                             config->algorithm);
        }
        else
        {
            selected_node = choose_worst_fit(task,
                                             nodes,
                                             node_count,
                                             loads,
                                             config->algorithm);
        }

        if (selected_node < 0)
        {
            snprintf(assignments[task_index].reason,
                     REJECTION_REASON_LENGTH,
                     "No pasa la prueba de admision en ningun nodo");
            continue;
        }

        int cost = task_cost_ticks(task,
                                   nodes[selected_node].speed_ut_per_tick);

        add_to_node_load(&loads[selected_node], task, cost);

        assignments[task_index].accepted = 1;
        assignments[task_index].node_id = nodes[selected_node].id;
        assignments[task_index].cost_ticks = cost;
        assignments[task_index].utilization = task_utilization(task, cost);

        snprintf(assignments[task_index].reason,
                 REJECTION_REASON_LENGTH,
                 "Aceptada");

        accepted_count++;
    }

    return accepted_count;
}

void print_plan_summary(const Task tasks[],
                        int task_count,
                        const NodeInfo nodes[],
                        int node_count,
                        const TaskAssignment assignments[])
{
    printf("\n=== Resultado de planificacion ===\n");

    for (int i = 0; i < task_count; i++)
    {
        if (assignments[i].accepted)
        {
            printf("Tarea %d -> Nodo %d | C=%d ticks | U=%.4f\n",
                   tasks[i].id,
                   assignments[i].node_id,
                   assignments[i].cost_ticks,
                   assignments[i].utilization);
        }
        else
        {
            printf("Tarea %d -> RECHAZADA | %s\n",
                   tasks[i].id,
                   assignments[i].reason);
        }
    }

    printf("\n=== Nodos usados para la prueba ===\n");

    for (int i = 0; i < node_count; i++)
    {
        printf("Nodo %d | velocidad = %d UT/tick\n",
               nodes[i].id,
               nodes[i].speed_ut_per_tick);
    }
}
