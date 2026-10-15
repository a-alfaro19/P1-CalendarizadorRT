#include <stdio.h>
#include <string.h>

#include "admission.h"
#include "planner.h"

int planner_find_task(const PlannerState *state, int task_id)
{
    if (state == NULL)
        return -1;

    for (int i = 0; i < state->node_count; i++)
        for (int j = 0; j < state->nodes[i].task_count; j++)
            if (state->nodes[i].tasks[j].id == task_id)
                return i;

    return -1;
}

double planner_node_utilization(const PlannerState *state, int node_index)
{
    if (state == NULL || node_index < 0 || node_index >= state->node_count)
        return -1.0;

    const NodePlan *node = &state->nodes[node_index];
    return total_utilization(node->tasks, node->costs, node->task_count);
}

/*
 * Prueba una tarea contra la carga actual de un nodo sin cambiarla
 * Si la prueba pasa, planner_add_task() la agrega
 */
static int node_accepts_task(const PlannerState *state,
                             int node_index,
                             const Task *task,
                             int *cost_out,
                             double *util_out)
{
    const NodePlan *node = &state->nodes[node_index];

    if (!node->info.active || node->task_count >= MAX_TASKS_PER_NODE)
        return 0;

    int cost = task_cost_ticks(task, node->info.speed_ut_per_tick);
    if (cost <= 0)
        return 0;

    Task temp_tasks[MAX_TASKS_PER_NODE];
    int temp_costs[MAX_TASKS_PER_NODE];

    for (int i = 0; i < node->task_count; i++)
    {
        temp_tasks[i] = node->tasks[i];
        temp_costs[i] = node->costs[i];
    }

    temp_tasks[node->task_count] = *task;
    temp_costs[node->task_count] = cost;

    if (!is_admissible(state->algorithm,
                       temp_tasks,
                       temp_costs,
                       node->task_count + 1))
        return 0;

    if (cost_out != NULL)
        *cost_out = cost;
    if (util_out != NULL)
        *util_out = task_utilization(task, cost);
    return 1;
}

/* First-Fit: revisa primero los nodos de mayor velocidad */
static int choose_first_fit(const PlannerState *state,
                            const Task *task,
                            int *selected_cost,
                            double *selected_util)
{
    int order[MAX_NODES];
    for (int i = 0; i < state->node_count; i++)
        order[i] = i;

    for (int i = 0; i < state->node_count - 1; i++)
        for (int j = i + 1; j < state->node_count; j++)
            if (state->nodes[order[j]].info.speed_ut_per_tick >
                state->nodes[order[i]].info.speed_ut_per_tick)
            {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }

    for (int i = 0; i < state->node_count; i++)
    {
        int idx = order[i], cost;
        double util;
        if (node_accepts_task(state, idx, task, &cost, &util))
        {
            *selected_cost = cost;
            *selected_util = util;
            return idx;
        }
    }
    return -1;
}

/* Worst-Fit: escoge el nodo admisible que quede con mas capacidad libre */
static int choose_worst_fit(const PlannerState *state,
                            const Task *task,
                            int *selected_cost,
                            double *selected_util)
{
    int selected = -1, best_cost = -1;
    double best_util = 0.0, best_free = -1.0;

    for (int i = 0; i < state->node_count; i++)
    {
        int cost;
        double util;
        if (!node_accepts_task(state, i, task, &cost, &util))
            continue;

        double free_capacity = 1.0 - (planner_node_utilization(state, i) + util);

        if (selected == -1 || free_capacity > best_free)
        {
            selected = i;
            best_cost = cost;
            best_util = util;
            best_free = free_capacity;
        }
    }

    if (selected >= 0)
    {
        *selected_cost = best_cost;
        *selected_util = best_util;
    }
    return selected;
}

int planner_init(PlannerState *state,
                 const NodeInfo nodes[],
                 int node_count,
                 const Config *config)
{
    if (state == NULL || nodes == NULL || config == NULL ||
        node_count <= 0 || node_count > MAX_NODES)
        return -1;

    memset(state, 0, sizeof(*state));
    state->node_count = node_count;
    state->algorithm = config->algorithm;
    state->heuristic = config->heuristic;

    for (int i = 0; i < node_count; i++)
    {
        if (nodes[i].speed_ut_per_tick <= 0)
            return -1;
        state->nodes[i].info = nodes[i];
    }
    return 0;
}

int planner_add_task(PlannerState *state,
                     const Task *task,
                     TaskAssignment *assignment)
{
    if (state == NULL || task == NULL || assignment == NULL)
        return -1;

    assignment->task_id = task->id;
    assignment->accepted = 0;
    assignment->node_id = -1;
    assignment->cost_ticks = -1;
    assignment->utilization = 0.0;

    /* No se permiten dos tareas con el mismo id. */
    if (planner_find_task(state, task->id) >= 0)
    {
        snprintf(assignment->reason, REJECTION_REASON_LENGTH,
                 "Ya existe una tarea con ese id");
        return 0;
    }

    int cost = -1, selected = -1;
    double util = 0.0;

    if (state->heuristic == HEURISTIC_FIRST_FIT)
        selected = choose_first_fit(state, task, &cost, &util);
    else
        selected = choose_worst_fit(state, task, &cost, &util);

    if (selected < 0)
    {
        snprintf(assignment->reason, REJECTION_REASON_LENGTH,
                 "No pasa admision en ningun nodo activo");
        return 0;
    }

    NodePlan *node = &state->nodes[selected];
    node->tasks[node->task_count] = *task;
    node->costs[node->task_count] = cost;
    node->task_count++;

    assignment->accepted = 1;
    assignment->node_id = node->info.id;
    assignment->cost_ticks = cost;
    assignment->utilization = util;
    snprintf(assignment->reason, REJECTION_REASON_LENGTH, "Aceptada");

    return 1;
}

int planner_load_initial_tasks(PlannerState *state,
                               const Task tasks[],
                               int task_count,
                               TaskAssignment assignments[])
{
    if (state == NULL || tasks == NULL || assignments == NULL || task_count < 0)
        return -1;

    int accepted = 0;
    for (int i = 0; i < task_count; i++)
    {
        int result = planner_add_task(state, &tasks[i], &assignments[i]);
        if (result < 0)
            return -1;
        if (result == 1)
            accepted++;
    }
    return accepted;
}

void planner_print_state(const PlannerState *state)
{
    if (state == NULL)
        return;

    printf("\n=== Estado actual del planificador ===\n");

    for (int i = 0; i < state->node_count; i++)
    {
        const NodePlan *node = &state->nodes[i];
        printf("\nNodo %d | %s | %d UT/tick | U=%.4f\n",
               node->info.id,
               node->info.active ? "ACTIVO" : "INACTIVO",
               node->info.speed_ut_per_tick,
               planner_node_utilization(state, i));

        if (node->task_count == 0)
        {
            printf("  Sin tareas asignadas\n");
            continue;
        }

        for (int j = 0; j < node->task_count; j++)
            printf("  Tarea %d | C=%d | T=%d | U=%.4f\n",
                   node->tasks[j].id,
                   node->costs[j],
                   node->tasks[j].period,
                   task_utilization(&node->tasks[j], node->costs[j]));
    }
}
