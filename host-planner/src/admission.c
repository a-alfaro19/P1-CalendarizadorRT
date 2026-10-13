#include <math.h>
#include <stddef.h>

#include "admission.h"

#define EPSILON 1e-9

int task_cost_ticks(const Task *task, int speed_ut_per_tick)
{
    if (task == NULL || speed_ut_per_tick <= 0 || task->work <= 0)
    {
        return -1;
    }

    /*
     * Calcula ceil(W/s) usando enteros
     * Ejemplo: W=41 y s=20 -> (41 + 19) / 20 = 3 ticks
     */
    return (task->work + speed_ut_per_tick - 1) / speed_ut_per_tick;
}

double task_utilization(const Task *task, int cost_ticks)
{
    if (task == NULL || task->period <= 0 || cost_ticks < 0)
    {
        return -1.0;
    }

    /* Para una tarea esporadica, period es el intervalo min */
    return (double)cost_ticks / (double)task->period;
}

double total_utilization(const Task tasks[], const int costs[], int task_count)
{
    if (tasks == NULL || costs == NULL || task_count < 0)
    {
        return -1.0;
    }

    double utilization = 0.0;

    for (int i = 0; i < task_count; i++)
    {
        double current = task_utilization(&tasks[i], costs[i]);

        if (current < 0.0)
        {
            return -1.0;
        }

        utilization += current;
    }

    return utilization;
}

int edf_is_admissible(const Task tasks[], const int costs[], int task_count)
{
    if (task_count <= 0)
    {
        return 1;
    }

    double utilization = total_utilization(tasks, costs, task_count);

    if (utilization < 0.0)
    {
        return 0;
    }

    /* Nota: EPSILON evita rechazos por pequeños errores de punto flotante */
    return utilization <= (1.0 + EPSILON);
}

double rms_utilization_bound(int task_count)
{
    if (task_count <= 0)
    {
        return 1.0;
    }

    return task_count * (pow(2.0, 1.0 / (double)task_count) - 1.0);
}

int rms_is_admissible(const Task tasks[], const int costs[], int task_count)
{
    if (task_count <= 0)
    {
        return 1;
    }

    double utilization = total_utilization(tasks, costs, task_count);

    if (utilization < 0.0)
    {
        return 0;
    }

    return utilization <= (rms_utilization_bound(task_count) + EPSILON);
}

int is_admissible(SchedulingAlgorithm algorithm,
                  const Task tasks[],
                  const int costs[],
                  int task_count)
{
    if (algorithm == SCHED_EDF)
    {
        return edf_is_admissible(tasks, costs, task_count);
    }

    if (algorithm == SCHED_RMS)
    {
        return rms_is_admissible(tasks, costs, task_count);
    }

    return 0;
}
