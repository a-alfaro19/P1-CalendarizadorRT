#pragma once
#include "host/shared_memory.h"
#include "host/scheduler/planner.h"

/* Recibe un plan cuyo despliegue inicial ya fue confirmado. */
int host_runtime_loop(shared_memory_t *memory, const HostConfig *config, PlannerState *planner);
