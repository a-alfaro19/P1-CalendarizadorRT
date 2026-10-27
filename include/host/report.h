#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "host/shared_memory.h"
#include "host/scheduler/config.h"

/* Las tareas se identifican con 8 bits (TASK_NONE = 0xFF queda reservado) */
#define REPORT_MAX_TASKS 256
/* Max de tramos de Gantt por nodo; las estadisticas siguen exactas si se llena */
#define REPORT_MAX_SEGMENTS 8192
/* Max de la linea de tiempo y de decisiones de admision, se conservan las primeras */
#define REPORT_MAX_LOG 4096
#define REPORT_MAX_DECISIONS 256
/* Ventana del Gantt en texto, el CSV contiene todos los tramos almacenados */
#define REPORT_GANTT_TICKS 400
#define REPORT_GANTT_WIDTH 100

typedef struct
{
    uint32_t start; /* tick de inicio (inclusive) */
    uint32_t end;   /* tick de fin (exclusive) */
    uint8_t task;   /* TASK_NONE = nodo inactivo */
} report_segment_t;

typedef struct
{
    bool used;
    int node; /* 0 si la tarea no esta en ningun nodo */
    int kind;
    int cost, period, deadline, criticality;
    bool accepted, confirmed;
    uint32_t releases, completions, misses, ignored;
    uint64_t response_sum; /* suma de tiempos de respuesta, en ticks */
    uint32_t response_min, response_max;
} report_task_t;

typedef struct
{
    uint32_t speed_ut_per_tick;
    uint32_t last_tick;
    uint32_t switches;
    uint32_t busy_ticks;
    uint32_t bad_frames_host; /* tramas descartadas por el controlador del host */
    uint32_t bad_frames_node; /* ultimo contador informado por el nodo */
    double theoretical_util;  /* se congela cuando el nodo cae */
    bool down;
    uint32_t down_tick;
    char down_reason[IPC_REASON_LENGTH];
    bool open; /* hay un tramo en curso */
    uint8_t current_task;
    uint32_t current_start;
    size_t segment_count;
    uint32_t segments_dropped;
    report_segment_t segments[REPORT_MAX_SEGMENTS];
} report_node_t;

typedef enum
{
    REPORT_LOG_RELEASE,
    REPORT_LOG_COMPLETE,
    REPORT_LOG_MISS,
    REPORT_LOG_SPORADIC_IGNORED,
    REPORT_LOG_NODE_DOWN,
    REPORT_LOG_BAD_FRAME
} report_log_kind_t;

typedef struct
{
    uint32_t tick;
    uint16_t aux;
    uint8_t node;
    uint8_t kind; /* report_log_kind_t */
    uint8_t task;
} report_log_t;

typedef struct
{
    int task_id;
    int node;
    bool accepted, confirmed;
    char reason[IPC_REASON_LENGTH];
} report_decision_t;

typedef struct
{
    int tick_ms;
    int report_period;
    SchedulingAlgorithm algorithm;
    AllocationHeuristic heuristic;
    bool have_startup;
    startup_status_t startup;
    report_node_t nodes[HOST_NODE_COUNT];
    report_task_t tasks[REPORT_MAX_TASKS];
    size_t log_count;
    uint32_t log_dropped;
    report_log_t log[REPORT_MAX_LOG];
    size_t decision_count;
    uint32_t decisions_dropped;
    report_decision_t decisions[REPORT_MAX_DECISIONS];
} report_state_t;

void report_init(report_state_t *state, const HostConfig *config);

/* Acumula un mensaje de la cola de eventos. No imprime ni usa memoria dinamica. */
void report_consume(report_state_t *state, const ipc_message_t *message);

/* Cierra los tramos abiertos, llamar una vez, antes de escribir el reporte. */
void report_finalize(report_state_t *state);

/* Escribe Gantt, linea de tiempo, estadisticas,
 * utilizacion, cambios de tarea y admision. Devuelve 0 o -1. */
int report_write(const report_state_t *state, FILE *out);
int report_write_gantt_csv(const report_state_t *state, int node_index, FILE *out);

/* Linea de consola en vivo para un mensaje de la cola de eventos */
void report_print_live(const ipc_message_t *message);

/* Proceso de reporte: consume eventos hasta IPC_SHUTDOWN y guarda los archivos
 * en el directorio dado. Devuelve 0 si el reporte quedo escrito. */
int report_run(shared_memory_t *memory, const HostConfig *config, const char *directory);
