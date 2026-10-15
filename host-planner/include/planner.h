#ifndef PLANNER_H
#define PLANNER_H

#include "config.h"
#include "task.h"

#define MAX_NODES 8
#define MAX_TASKS_PER_NODE 100
#define REJECTION_REASON_LENGTH 128

/* Informacion minima de cada nodo para planificar */
typedef struct
{
    int id;
    int speed_ut_per_tick;
    int active;
} NodeInfo;

/* Resultado de intentar asignar una tarea */
typedef struct
{
    int task_id;
    int accepted;
    int node_id;
    int cost_ticks;
    double utilization;
    char reason[REJECTION_REASON_LENGTH];
} TaskAssignment;

/* Estado continuo del nodo */
typedef struct
{
    NodeInfo info;
    Task tasks[MAX_TASKS_PER_NODE];
    int costs[MAX_TASKS_PER_NODE];
    int task_count;
} NodePlan;

/* Estado completo del planificador del host */
typedef struct
{
    NodePlan nodes[MAX_NODES];
    int node_count;
    SchedulingAlgorithm algorithm;
    AllocationHeuristic heuristic;
} PlannerState;

/* Inicializa el estado usando los resultados de calibracion */
int planner_init(PlannerState *state,
                 const NodeInfo nodes[],
                 int node_count,
                 const Config *config);

/* Agrega una tarea nueva sin borrar las asignaciones anteriores,permitiendo el modo manual */
int planner_add_task(PlannerState *state,
                     const Task *task,
                     TaskAssignment *assignment);

/* Carga el conjunto inicial de tareas sobre un estado vacio */
int planner_load_initial_tasks(PlannerState *state,
                               const Task tasks[],
                               int task_count,
                               TaskAssignment assignments[]);

/* Busca una tarea por id. Retorna indice de nodo o -1. */
int planner_find_task(const PlannerState *state, int task_id);

/* Consulta la utilizacion actual de un nodo. */
double planner_node_utilization(const PlannerState *state,
                                int node_index);

void planner_print_state(const PlannerState *state);

#endif
