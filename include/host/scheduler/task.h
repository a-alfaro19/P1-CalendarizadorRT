#pragma once

/*
 * Tipos de tarea:
 * P = periodica
 * S = esporadica
 */
typedef enum
{
    TASK_PERIODIC,
    TASK_SPORADIC
} TaskType;

/*
 * Estructura base de una tarea:
 *
 * work        -> cantidad de unidades de trabajo (W)
 * period      -> periodo T o intervalo mínimo si es s
 * deadline    -> plazo relativo D
 * criticality -> nivel de criticidad de la tarea
 */
typedef struct
{
    int id;
    TaskType type;
    int work;
    int period;
    int deadline;
    int criticality;
} Task;
