#ifndef PLANNER_H
#define PLANNER_H

#include "config.h"
#include "task.h"

#define MAX_NODES 8
#define REJECTION_REASON_LENGTH 128

/*
 * Informacion minima que el planificador necesita de un nodo
 * speed_ut_per_tick debe venir de la calibracion real del nodo
 */
typedef struct
{
    int id;
    int speed_ut_per_tick;
} NodeInfo;

/* Resultado de la planificacion de una task */
typedef struct
{
    int task_id;
    int accepted;
    int node_id;
    int cost_ticks;
    double utilization;
    char reason[REJECTION_REASON_LENGTH];
} TaskAssignment;

/*
 * Planifica todas las task usando el algoritmo y la heuristica
 * Retorna la cantidad aceptada o -1 si hay parametros invalidos
 */
int plan_tasks(const Task tasks[],
               int task_count,
               const NodeInfo nodes[],
               int node_count,
               const Config *config,
               TaskAssignment assignments[]);

/* Muestra resumen para pruebas */
void print_plan_summary(const Task tasks[],
                        int task_count,
                        const NodeInfo nodes[],
                        int node_count,
                        const TaskAssignment assignments[]);

#endif
