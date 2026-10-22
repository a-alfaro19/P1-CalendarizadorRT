#pragma once

#define MAX_PATH_LENGTH 256
#define MAX_PORT_LENGTH 128

/* Algoritmo de calendarizacion seleccionado */
typedef enum
{
    SCHED_RMS,
    SCHED_EDF
} SchedulingAlgorithm;

/* Heuristica usada por el host para asignar tareas a nodos */
typedef enum
{
    HEURISTIC_FIRST_FIT,
    HEURISTIC_WORST_FIT
} AllocationHeuristic;

/*
 * Config general del host
 * Estos valores se cargan desde config.txt.
 */
typedef struct
{
    SchedulingAlgorithm algorithm;
    AllocationHeuristic heuristic;

    int tick_ms;

    char node1_port[MAX_PORT_LENGTH];
    char node2_port[MAX_PORT_LENGTH];

    int tick_report_period;
    int max_missed_reports;

    char task_file[MAX_PATH_LENGTH];
} HostConfig;

/*
 * Carga la config desde un archivo
 *
 * Retorna:
 *   0  : configuración cargada correctamente
 *   -1 : error de lectura o config invalida
 */
int load_config(const char *filename, Config *config);
