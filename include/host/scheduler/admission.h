#pragma once

#include "config.h"
#include "task.h"

/*
 * Calcula cuantos ticks necesita una tarea en un nodo
 * Formula: C(i,n) = ceil(W(i) / s(n))
 */
int task_cost_ticks(const Task *task, int speed_ut_per_tick);

/* Calcula la utilizacion de una tarea en un nodo: U = C/T */
double task_utilization(const Task *task, int cost_ticks);

/* Suma la utilizacion de todas las tareas ya asignadas a un nodo */
double total_utilization(const Task tasks[], const int costs[], int task_count);

/* Prueba de admision EDF: sum(C/T) <= 1 */
int edf_is_admissible(const Task tasks[], const int costs[], int task_count);

/* Limite suficiente de utilizacion para RMS */
double rms_utilization_bound(int task_count);

/* Prueba de admision RMS */
int rms_is_admissible(const Task tasks[], const int costs[], int task_count);

/* Selecciona automaticamente la prueba */
int is_admissible(SchedulingAlgorithm algorithm,
                  const Task tasks[],
                  const int costs[],
                  int task_count);
