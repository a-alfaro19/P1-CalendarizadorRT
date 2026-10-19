#pragma once

#include "config.h"
#include "task.h"

#define MAX_NODES 8
#define MAX_TASKS_PER_NODE 100
#define REJECTION_REASON_LENGTH 128

typedef struct
{
    int id;
    int speed_ut_per_tick;
    int active;
} NodeInfo;

typedef struct
{
    int task_id;
    int accepted;
    int node_id;
    int cost_ticks;
    double utilization;
    char reason[REJECTION_REASON_LENGTH];
} TaskAssignment;

typedef struct
{
    NodeInfo info;
    Task tasks[MAX_TASKS_PER_NODE];
    int costs[MAX_TASKS_PER_NODE];
    int task_count;
} NodePlan;

typedef struct
{
    NodePlan nodes[MAX_NODES];
    int node_count;
    SchedulingAlgorithm algorithm;
    AllocationHeuristic heuristic;
} PlannerState;

int planner_init(PlannerState *state,
                 const NodeInfo nodes[],
                 int node_count,
                 const Config *config);

int planner_add_task(PlannerState *state,
                     const Task *task,
                     TaskAssignment *assignment);

/*
 * Carga la planificacion inicial ordenando primero las tareas de mayor a menor
 * utilizacion en el nodo activo mas rapido
 */
int planner_load_initial_tasks(PlannerState *state,
                               const Task tasks[],
                               int task_count,
                               TaskAssignment assignments[]);

int planner_find_task(const PlannerState *state, int task_id);

double planner_node_utilization(const PlannerState *state,
                                int node_index);

/*
 * Se encarga de la caida de un nodo
 * Solo reubica tareas periodicas, las esporadicas se rechazan
 * Las tareas de los nodos sobrevivientes se mantienen
 */
int planner_handle_node_failure(PlannerState *state,
                                int failed_node_id,
                                TaskAssignment results[],
                                int *result_count);

void planner_print_state(const PlannerState *state);
