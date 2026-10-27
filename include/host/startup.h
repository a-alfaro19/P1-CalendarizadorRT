#pragma once
#include <stdbool.h>
#include <stdint.h>

#define HOST_NODE_COUNT 2

typedef enum
{
    STARTUP_IDLE,
    STARTUP_CALIBRATING,
    STARTUP_PLANNING,
    STARTUP_DEPLOYING,
    STARTUP_STARTING,
    STARTUP_RUNNING,
    STARTUP_ABORTING,
    STARTUP_FAILED
} startup_phase_t;

/* Todas las marcas pertenecen a CLOCK_MONOTONIC del host */
typedef struct
{
    uint64_t tx_begin_ns;
    uint64_t tx_end_ns;
    uint64_t ack_ns;
} startup_timing_t;

typedef struct
{
    uint32_t speed_ut_per_tick;
    unsigned planned_tasks;
    unsigned confirmed_tasks;
    bool start_confirmed;
    bool stop_confirmed;
    startup_timing_t start;
} startup_node_t;

typedef struct
{
    startup_phase_t phase;
    int node_count;
    startup_phase_t failed_phase;
    int error_code;
    int failed_node;
    uint32_t failed_request;
    uint64_t target_start_ns;
    startup_node_t nodes[HOST_NODE_COUNT];
    bool timing_valid;
    uint64_t tx_skew_ns;
    /* Intervalo para cero_nodo2 - cero_nodo1, acotado por envio y recepcion del ACK. */
    int64_t zero_delta_min_ns;
    int64_t zero_delta_max_ns;
} startup_status_t;
