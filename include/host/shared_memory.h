#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <semaphore.h>

#include "host/ring_buffer.h"
#include "host/ipc_queue.h"
#include "protocol/message.h"

/** Maximum number of task definitions stored for each node. */
#define SHARED_MAX_TASKS_PER_NODE 10

/** Maximum number of nodes represented in shared memory. */
#define SHARED_NUM_NODES HOST_NODE_COUNT

/** Maximum length of a shared memory object name, including its terminator. */
#define SHARED_MEMORY_NAME_MAX 128

/**
 * @brief Task configuration assigned to a node.
 */
typedef struct
{
    uint8_t task_id;     /**< Task identifier. */
    uint8_t kind;        /**< Task execution kind. */
    uint16_t cost_ticks; /**< Execution cost in ticks. */
    uint16_t period;     /**< Task period or minimum sporadic interval. */
    uint16_t deadline;   /**< Relative deadline in ticks. */
    bool assigned;       /**< Whether the task has been assigned to its node. */
} task_config_t;

/**
 * @brief Task configuration for one node.
 */
typedef struct
{
    int node_id;                                    /**< Node identifier. */
    int task_count;                                 /**< Number of configured tasks. */
    task_config_t tasks[SHARED_MAX_TASKS_PER_NODE]; /**< Configured tasks. */
} node_task_list_t;

/**
 * @brief Initial task configuration for all nodes.
 */
typedef struct
{
    node_task_list_t nodes[SHARED_NUM_NODES]; /**< Per-node task lists. */
} shared_system_config_t;

/**
 * @brief Runtime status and statistics for one node.
 */
typedef struct
{
    uint64_t last_report_ns; /* Último reporte válido, medido con CLOCK_MONOTONIC. */
    bool active;                        /**< Whether the node is active. */
    uint32_t last_tick;                 /**< Most recent tick reported by the node. */
    uint16_t bad_frames;                /**< Invalid frames reported by the node. */
    uint32_t missed_reports;            /**< Consecutive reports missed by the node. */
    uint64_t completed_tasks;           /**< Completed task count. */
    uint64_t missed_deadlines;          /**< Missed deadline count. */
    uint64_t ignored_sporadic_triggers; /**< Ignored sporadic trigger count. */
    bool has_last_event;                /**< Whether a node event has been recorded. */
    msg_event_t last_event;             /**< Most recently recorded node event. */
} shared_node_status_t;

/**
 * @brief Shared data available to host processes.
 */
typedef struct
{
    ipc_queue_t commands[SHARED_NUM_NODES];          /* Planner -> one driver per queue. */
    ipc_queue_t controls; /* Consola -> planificador, separada de las respuestas seriales. */
    ipc_queue_t responses;                           /* Drivers -> planner; multiple producers. */
    ipc_queue_t events;                              /* Drivers and planner -> report; multiple producers. */
    startup_status_t startup;
    uint32_t magic;                                  /**< Shared memory format marker. */
    shared_system_config_t config;                   /**< Task configuration. */
    shared_node_status_t nodes[SHARED_NUM_NODES];    /**< Per-node runtime status. */
    ring_buffer_t tx_ring_buffers[SHARED_NUM_NODES]; /**< Per-node transmit buffers. */
    sem_t mutex;                                     /**< Shared access semaphore. */
} shared_memory_data_t;

/**
 * @brief Process-local handle for a shared memory object.
 */
typedef struct
{
    int fd;                            /**< Object file descriptor. */
    size_t size;                       /**< Mapped object size in bytes. */
    bool owner;                        /**< Whether this process created the object. */
    char name[SHARED_MEMORY_NAME_MAX]; /**< Shared memory object name. */
    shared_memory_data_t *data;        /**< Mapped shared data. */
} shared_memory_t;

/**
 * @brief Creates and initializes a named shared memory object.
 * @param memory Handle to initialize.
 * @param name Shared memory object name.
 * @return 0 on success, or -1 on error.
 */
int shared_memory_create(shared_memory_t *memory, const char *name);

/**
 * @brief Opens an existing shared memory object.
 * @param memory Handle to initialize.
 * @param name Shared memory object name.
 * @return 0 on success, or -1 on error.
 */
int shared_memory_open(shared_memory_t *memory, const char *name);

/**
 * @brief Closes a process-local mapping without removing the shared object.
 * @param memory Handle to close.
 * @return 0 on success, or -1 on error.
 */
int shared_memory_close(shared_memory_t *memory);

/**
 * @brief Removes a shared memory object owned by this process.
 * @param memory Handle to destroy.
 * @return 0 on success, or -1 on error.
 */
int shared_memory_destroy(shared_memory_t *memory);

/**
 * @brief Replaces the shared task configuration.
 * @param memory Shared memory handle.
 * @param config Task configuration to store.
 * @return 0 on success, or -1 on error.
 */
int shared_memory_write_config(shared_memory_t *memory,
                               const shared_system_config_t *config);

/**
 * @brief Reads the shared task configuration.
 * @param memory Shared memory handle.
 * @param config Destination for the task configuration.
 * @return 0 on success, or -1 on error.
 */
int shared_memory_read_config(shared_memory_t *memory,
                              shared_system_config_t *config);

/**
 * @brief Reads the next unassigned task for a node.
 * @param memory Shared memory handle.
 * @param node_id Node identifier.
 * @param task Destination for the task configuration.
 * @return 1 if a task is returned, 0 if none remain, or -1 on error.
 */
int shared_memory_read_next_task(shared_memory_t *memory,
                                 int node_id,
                                 task_config_t *task);

/**
 * @brief Marks a node's task as assigned.
 * @param memory Shared memory handle.
 * @param node_id Node identifier.
 * @param task_id Task identifier.
 * @return 0 on success, or -1 on error.
 */
int shared_memory_mark_task_assigned(shared_memory_t *memory,
                                     int node_id,
                                     uint8_t task_id);

/**
 * @brief Updates a node's active state.
 * @param memory Shared memory handle.
 * @param node_id Node identifier.
 * @param active New active state.
 * @return 0 on success, or -1 on error.
 */
int shared_memory_set_node_active(shared_memory_t *memory,
                                  int node_id,
                                  bool active);

/**
 * @brief Records a tick report for a node.
 * @param memory Shared memory handle.
 * @param node_id Node identifier.
 * @param report Tick report to record.
 * @return 0 on success, or -1 on error.
 */
int shared_memory_record_tick_report(shared_memory_t *memory,
                                     int node_id,
                                     const msg_tick_report_t *report);

/**
 * @brief Records a missed report for a node.
 * @param memory Shared memory handle.
 * @param node_id Node identifier.
 * @return 0 on success, or -1 on error.
 */
int shared_memory_record_missed_report(shared_memory_t *memory, int node_id);

/**
 * @brief Records a node event and updates its statistics.
 * @param memory Shared memory handle.
 * @param node_id Node identifier.
 * @param event Event to record.
 * @return 0 on success, or -1 on error.
 */
int shared_memory_record_event(shared_memory_t *memory,
                               int node_id,
                               const msg_event_t *event);

/**
 * @brief Reads the runtime status for a node.
 * @param memory Shared memory handle.
 * @param node_id Node identifier.
 * @param status Destination for the node status.
 * @return 0 on success, or -1 on error.
 */
int shared_memory_read_node_status(shared_memory_t *memory,
                                   int node_id,
                                   shared_node_status_t *status);

/**
 * @brief Gets the transmit buffer for a node.
 * @param memory Shared memory handle.
 * @param node_id One-based node identifier.
 * @return The node's transmit buffer, or NULL on error.
 */
ring_buffer_t *shared_memory_tx_ring_buffer(shared_memory_t *memory,
                                            int node_id);

typedef enum
{
    SHARED_QUEUE_COMMAND,
    SHARED_QUEUE_RESPONSE,
    SHARED_QUEUE_EVENT,
    SHARED_QUEUE_CONTROL
} shared_queue_channel_t;
int shared_memory_send(shared_memory_t *memory, shared_queue_channel_t channel,
                       int node_id, const ipc_message_t *message, int timeout_ms);
int shared_memory_receive(shared_memory_t *memory, shared_queue_channel_t channel,
                          int node_id, ipc_message_t *message, int timeout_ms);

/* El planificador publica el estado; el supervisor y las pruebas lo consultan. */
int shared_memory_write_startup(shared_memory_t *memory, const startup_status_t *status);
int shared_memory_read_startup(shared_memory_t *memory, startup_status_t *status);

int shared_memory_set_missed_reports(shared_memory_t *memory, int node_id, uint32_t missed);
