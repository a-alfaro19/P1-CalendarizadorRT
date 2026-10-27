#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/scheduler/config.h"

/* Elimina el salto de linea que deja fgets(). */
static void remove_newline(char *text)
{
    text[strcspn(text, "\r\n")] = '\0';
}

/*
 * Copia una cadena asegurando que siempre termine en '\0'
 * Esto evita problemas si el texto recibido es mas largo
 * que el espacio disponible
 */
static void copy_string(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0)
    {
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static int parse_tick_value(const char *text, const char *key, int *value)
{
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed <= 0 || parsed > UINT16_MAX)
    {
        fprintf(stderr, "%s debe estar entre 1 y %u\n",
                key,
                (unsigned)UINT16_MAX);
        return -1;
    }

    *value = (int)parsed;
    return 0;
}

/* Valores iniciales */
static void set_default_config(HostConfig *config)
{
    config->algorithm = SCHED_EDF;
    config->heuristic = HEURISTIC_FIRST_FIT;

    config->tick_ms = 10;

    config->node1_port[0] = '\0';
    config->node2_port[0] = '\0';

    config->tick_report_period = 10;
    config->max_missed_reports = 3;
    config->command_timeout_ms = 1000;

    config->task_file[0] = '\0';
}

/*
 * Procesa una linea key=value del archivo de config
 * Retorna 0 si la clave es valida y -1 si no se reconoce
 */
static int parse_config_entry(HostConfig *config,
                              const char *key,
                              const char *value)
{
    if (strcmp(key, "algorithm") == 0)
    {
        if (strcmp(value, "RMS") == 0)
        {
            config->algorithm = SCHED_RMS;
        }
        else if (strcmp(value, "EDF") == 0)
        {
            config->algorithm = SCHED_EDF;
        }
        else
        {
            fprintf(stderr, "Algoritmo invalido: %s\n", value);
            return -1;
        }
    }

    else if (strcmp(key, "heuristic") == 0)
    {
        if (strcmp(value, "FIRST_FIT") == 0)
        {
            config->heuristic = HEURISTIC_FIRST_FIT;
        }
        else if (strcmp(value, "WORST_FIT") == 0)
        {
            config->heuristic = HEURISTIC_WORST_FIT;
        }
        else
        {
            fprintf(stderr, "Heuristica invalida: %s\n", value);
            return -1;
        }
    }

    else if (strcmp(key, "tick_ms") == 0)
    {
        return parse_tick_value(value, key, &config->tick_ms);
    }

    else if (strcmp(key, "node1_port") == 0)
    {
        copy_string(config->node1_port,
                    sizeof(config->node1_port),
                    value);
    }

    else if (strcmp(key, "node2_port") == 0)
    {
        if (strcmp(value, "none") == 0)
            config->node2_port[0] = '\0';
        else
            copy_string(config->node2_port,
                        sizeof(config->node2_port),
                        value);
    }

    else if (strcmp(key, "tick_report_period") == 0)
    {
        return parse_tick_value(value, key, &config->tick_report_period);
    }

    else if (strcmp(key, "max_missed_reports") == 0)
    {
        return parse_tick_value(value, key, &config->max_missed_reports);
    }

    else if (strcmp(key, "command_timeout_ms") == 0)
    {
        return parse_tick_value(value, key, &config->command_timeout_ms);
    }

    else if (strcmp(key, "task_file") == 0)
    {
        copy_string(config->task_file,
                    sizeof(config->task_file),
                    value);
    }

    else
    {
        fprintf(stderr, "Clave de configuracion desconocida: %s\n", key);
        return -1;
    }

    return 0;
}

/* Comprueba que los valores obligatorios tengan sentido */
static int validate_config(const HostConfig *config)
{
    if (config->max_missed_reports <= 0)
    {
        fprintf(stderr, "max_missed_reports debe ser mayor que 0\n");
        return -1;
    }

    if (config->node1_port[0] == '\0')
    {
        fprintf(stderr, "Falta indicar node1_port\n");
        return -1;
    }

    if (config->task_file[0] == '\0')
    {
        fprintf(stderr, "Falta indicar el archivo de tareas\n");
        return -1;
    }

    return 0;
}

int load_config(const char *filename, HostConfig *config)
{
    if (config == NULL)
    {
        return -1;
    }

    set_default_config(config);

    FILE *file = fopen(filename, "r");

    if (file == NULL)
    {
        perror("Error al abrir el archivo de configuración");
        return -1;
    }

    char line[256];

    while (fgets(line, sizeof(line), file) != NULL)
    {
        remove_newline(line);

        /* Se ignoran lineas vacias y comentarios */
        if (line[0] == '\0' || line[0] == '#')
        {
            continue;
        }

        char key[128];
        char value[128];

        if (sscanf(line, "%127[^=]=%127s", key, value) != 2)
        {
            fprintf(stderr,
                    "Linea de configuracion invalida: %s\n",
                    line);
            continue;
        }

        if (parse_config_entry(config, key, value) != 0)
        {
            fclose(file);
            return -1;
        }
    }

    fclose(file);

    return validate_config(config);
}
