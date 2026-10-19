#pragma once

#include <stdint.h>
#include <stdbool.h>

// Constantes globales del sistema y del planificador
#define TICKS_MS 10
#define ALGORITHM_EDF 1 // Asegúrate de que esté alineado con tus enums de protocolo
#define REPORT_PERIOD 1000

#define MAX_TASKS_PER_NODE 10
#define NUM_NODES 4 // O el número máximo de nodos concurrentes en tu arquitectura

typedef struct
{
    uint8_t task_id;
    uint8_t kind; // TASK_PERIODIC / TASK_KIND_SPORADIC
    uint16_t cost_ticks;
    uint16_t period;
    uint16_t deadline;
    bool assigned; // Bandera para saber si el planificador ya la despachó
} task_config_t;

typedef struct
{
    int node_id;
    int task_count;
    task_config_t tasks[MAX_TASKS_PER_NODE];
} node_task_list_t;

// El bloque total de memoria compartida para todos los nodos
typedef struct
{
    node_task_list_t nodes[NUM_NODES];
} shared_system_config_t;

task_config_t *get_next_unassigned_task_for_node(void *shm_base, int node_id);
