#pragma once
#include "host/shared_memory.h"
#include "host/scheduler/scheduler.h"

/* El proceso planificador es dueño de PlannerState. El timeout se
 * aplica a cada intercambio; un fallo cancela el arranque con STOP acotado. */
int scheduler_bootstrap(shared_memory_t *memory, const HostConfig *config,
                        const Task *tasks, int task_count, int node_count,
                        int timeout_ms, PlannerState *planner);
int scheduler_run(shared_memory_t *memory, const HostConfig *config,
                  const Task *tasks, int task_count, int node_count);
