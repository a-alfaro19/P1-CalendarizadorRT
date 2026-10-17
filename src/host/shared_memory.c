#include "host/shared_memory.h"
#include <stddef.h>

/**
 * @brief Searches and returns a pointer to the next unassigned task for a specific node
 *        within the shared memory system configuration.
 *
 * @param shm_base Pointer to the base address of the mapped shared memory region.
 * @param node_id Numeric identifier of the node to query.
 * @return task_config_t* Pointer to the unassigned task configuration, or NULL if all tasks are assigned or the node is not found.
 */
task_config_t *get_next_unassigned_task_for_node(void *shm_base, int node_id)
{
    if (shm_base == NULL)
    {
        return NULL;
    }

    // Casteamos el puntero base al tipo de la estructura global compartida
    shared_system_config_t *system_config = (shared_system_config_t *)shm_base;

    // Buscamos el nodo que coincide con el ID
    for (int i = 0; i < NUM_NODES; i++)
    {
        if (system_config->nodes[i].node_id == node_id)
        {
            node_task_list_t *node_list = &system_config->nodes[i];

            // Buscamos la primera tarea que no haya sido asignada aún
            for (int j = 0; j < node_list->task_count; j++)
            {
                if (!node_list->tasks[j].assigned)
                {
                    return &node_list->tasks[j]; // Retornamos el puntero directo a la memoria compartida
                }
            }
            break; // Nodo encontrado, pero ya no quedan tareas libres
        }
    }

    return NULL; // No se encontró el nodo o no hay tareas pendientes
}
