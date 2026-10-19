#include <stdio.h>
#include <string.h>

#include "host/scheduler/admission.h"
#include "host/scheduler/planner.h"

#define DOUBLE_EPSILON 1e-12

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
 * Prueba una tarea contra la carga actual de un nodo sin modificarla
 * Si pasa admision, luego se agrega de forma definitiva
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

/* First-Fit: revisa primero los nodos de mayor velocidad. */
static int choose_first_fit(const PlannerState *state,
                            const Task *task,
                            int *selected_cost,
                            double *selected_util)
{
    int order[MAX_NODES];

    for (int i = 0; i < state->node_count; i++)
        order[i] = i;

    for (int i = 0; i < state->node_count - 1; i++)
    {
        for (int j = i + 1; j < state->node_count; j++)
        {
            if (state->nodes[order[j]].info.speed_ut_per_tick >
                state->nodes[order[i]].info.speed_ut_per_tick)
            {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    for (int i = 0; i < state->node_count; i++)
    {
        int idx = order[i];
        int cost;
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

/* Worst-Fit: escoge el nodo admisible que quede con mas capacidad libre. */
static int choose_worst_fit(const PlannerState *state,
                            const Task *task,
                            int *selected_cost,
                            double *selected_util)
{
    int selected = -1;
    int best_cost = -1;
    double best_util = 0.0;
    double best_free = -1.0;

    for (int i = 0; i < state->node_count; i++)
    {
        int cost;
        double util;

        if (!node_accepts_task(state, i, task, &cost, &util))
            continue;

        double free_capacity =
            1.0 - (planner_node_utilization(state, i) + util);

        if (selected == -1 ||
            free_capacity > best_free + DOUBLE_EPSILON ||
            (free_capacity >= best_free - DOUBLE_EPSILON &&
             free_capacity <= best_free + DOUBLE_EPSILON &&
             state->nodes[i].info.speed_ut_per_tick >
                 state->nodes[selected].info.speed_ut_per_tick))
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

/*
 * Funcion interna para asignar una tarea
 * check_duplicate se desactiva durante la recuperacion porque la tarea ya
 * pertenecia al sistema antes de la caida
 */
static int assign_task(PlannerState *state,
                       const Task *task,
                       TaskAssignment *assignment,
                       int check_duplicate)
{
    if (state == NULL || task == NULL || assignment == NULL)
        return -1;

    assignment->task_id = task->id;
    assignment->accepted = 0;
    assignment->node_id = -1;
    assignment->cost_ticks = -1;
    assignment->utilization = 0.0;

    if (check_duplicate && planner_find_task(state, task->id) >= 0)
    {
        snprintf(assignment->reason,
                 REJECTION_REASON_LENGTH,
                 "Ya existe una tarea con ese id");
        return 0;
    }

    int cost = -1;
    int selected = -1;
    double util = 0.0;

    if (state->heuristic == HEURISTIC_FIRST_FIT)
        selected = choose_first_fit(state, task, &cost, &util);
    else
        selected = choose_worst_fit(state, task, &cost, &util);

    if (selected < 0)
    {
        snprintf(assignment->reason,
                 REJECTION_REASON_LENGTH,
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
    return assign_task(state, task, assignment, 1);
}

static int fastest_active_node(const PlannerState *state)
{
    int fastest = -1;

    for (int i = 0; i < state->node_count; i++)
    {
        if (!state->nodes[i].info.active)
            continue;

        if (fastest == -1 ||
            state->nodes[i].info.speed_ut_per_tick >
                state->nodes[fastest].info.speed_ut_per_tick)
            fastest = i;
    }

    return fastest;
}

/*
 * Orden inicial: mayor utilizacion en el nodo mas rapido.
 * Desempate elegido: mayor criticidad y luego menor id.
 */
static void build_initial_task_order(const PlannerState *state,
                                     const Task tasks[],
                                     int task_count,
                                     int order[])
{
    int fastest = fastest_active_node(state);
    int speed = state->nodes[fastest].info.speed_ut_per_tick;

    for (int i = 0; i < task_count; i++)
        order[i] = i;

    for (int i = 0; i < task_count - 1; i++)
    {
        int best = i;

        for (int j = i + 1; j < task_count; j++)
        {
            int a = order[best];
            int b = order[j];

            int cost_a = task_cost_ticks(&tasks[a], speed);
            int cost_b = task_cost_ticks(&tasks[b], speed);
            double util_a = task_utilization(&tasks[a], cost_a);
            double util_b = task_utilization(&tasks[b], cost_b);

            int b_first = 0;

            if (util_b > util_a + DOUBLE_EPSILON)
                b_first = 1;
            else if (util_b >= util_a - DOUBLE_EPSILON &&
                     util_b <= util_a + DOUBLE_EPSILON)
            {
                if (tasks[b].criticality > tasks[a].criticality)
                    b_first = 1;
                else if (tasks[b].criticality == tasks[a].criticality &&
                         tasks[b].id < tasks[a].id)
                    b_first = 1;
            }

            if (b_first)
                best = j;
        }

        if (best != i)
        {
            int tmp = order[i];
            order[i] = order[best];
            order[best] = tmp;
        }
    }
}

int planner_load_initial_tasks(PlannerState *state,
                               const Task tasks[],
                               int task_count,
                               TaskAssignment assignments[])
{
    if (state == NULL || tasks == NULL || assignments == NULL ||
        task_count < 0 || task_count > MAX_TASKS_PER_NODE)
        return -1;

    if (fastest_active_node(state) < 0)
        return -1;

    int order[MAX_TASKS_PER_NODE];
    build_initial_task_order(state, tasks, task_count, order);

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

    int accepted = 0;

    for (int pos = 0; pos < task_count; pos++)
    {
        int original_index = order[pos];
        int result = planner_add_task(state,
                                      &tasks[original_index],
                                      &assignments[original_index]);
        if (result < 0)
            return -1;
        if (result == 1)
            accepted++;
    }

    return accepted;
}

/*
 * Durante una caida se intentan primero las tareas mas criticas.
 * Asi, si falta capacidad, las de menor criticidad son las que quedan fuera.
 */
static void sort_failed_tasks_by_criticality(Task tasks[], int task_count)
{
    for (int i = 0; i < task_count - 1; i++)
    {
        int best = i;

        for (int j = i + 1; j < task_count; j++)
        {
            if (tasks[j].criticality > tasks[best].criticality ||
                (tasks[j].criticality == tasks[best].criticality &&
                 tasks[j].id < tasks[best].id))
                best = j;
        }

        if (best != i)
        {
            Task tmp = tasks[i];
            tasks[i] = tasks[best];
            tasks[best] = tmp;
        }
    }
}

int planner_handle_node_failure(PlannerState *state,
                                int failed_node_id,
                                TaskAssignment results[],
                                int *result_count)
{
    if (state == NULL || results == NULL || result_count == NULL)
        return -1;

    int failed_index = -1;

    for (int i = 0; i < state->node_count; i++)
    {
        if (state->nodes[i].info.id == failed_node_id)
        {
            failed_index = i;
            break;
        }
    }

    if (failed_index < 0)
        return -1;

    NodePlan *failed = &state->nodes[failed_index];

    if (!failed->info.active)
        return -1;

    Task failed_tasks[MAX_TASKS_PER_NODE];
    int failed_task_count = failed->task_count;

    for (int i = 0; i < failed_task_count; i++)
        failed_tasks[i] = failed->tasks[i];

    /*
     * El nodo caido deja de participar inmediatamente.
     * Sus tareas se eliminan del estado local antes de intentar reubicarlas.
     */
    failed->info.active = 0;
    failed->task_count = 0;

    Task periodic_tasks[MAX_TASKS_PER_NODE];
    int periodic_count = 0;
    int output = 0;

    for (int i = 0; i < failed_task_count; i++)
    {
        if (failed_tasks[i].type == TASK_SPORADIC)
        {
            TaskAssignment *assignment = &results[output++];
            assignment->task_id = failed_tasks[i].id;
            assignment->accepted = 0;
            assignment->node_id = -1;
            assignment->cost_ticks = -1;
            assignment->utilization = 0.0;
            snprintf(assignment->reason,
                     REJECTION_REASON_LENGTH,
                     "Tarea esporadica no se reubica tras caida de nodo");
        }
        else
        {
            periodic_tasks[periodic_count++] = failed_tasks[i];
        }
    }

    sort_failed_tasks_by_criticality(periodic_tasks, periodic_count);

    int relocated = 0;

    for (int i = 0; i < periodic_count; i++)
    {
        TaskAssignment *assignment = &results[output++];
        int result = assign_task(state,
                                 &periodic_tasks[i],
                                 assignment,
                                 0);

        if (result < 0)
            return -1;

        if (result == 1)
        {
            relocated++;
            snprintf(assignment->reason,
                     REJECTION_REASON_LENGTH,
                     "Reubicada despues de caida del nodo %d",
                     failed_node_id);
        }
        else
        {
            snprintf(assignment->reason,
                     REJECTION_REASON_LENGTH,
                     "Rechazada tras caida: capacidad insuficiente");
        }
    }

    *result_count = output;
    return relocated;
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
        {
            printf("  Tarea %d | %c | Crit=%d | C=%d | T=%d | U=%.4f\n",
                   node->tasks[j].id,
                   node->tasks[j].type == TASK_PERIODIC ? 'P' : 'S',
                   node->tasks[j].criticality,
                   node->costs[j],
                   node->tasks[j].period,
                   task_utilization(&node->tasks[j], node->costs[j]));
        }
    }
}
