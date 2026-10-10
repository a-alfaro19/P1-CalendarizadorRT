#include <stdio.h>

#include "task_parser.h"

/*
 * Lee las tareas una por una usando el formato:
 * id,tipo,W,T,D,criticidad
 *
 */
int load_tasks(const char *filename, Task tasks[], int max_tasks)
{
    FILE *file = fopen(filename, "r");

    if (file == NULL)
    {
        perror("Error al abrir el archivo de tareas");
        return -1;
    }

    char line[256];
    int count = 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        if (count >= max_tasks)
        {
            fprintf(stderr,
                    "Se alcanzo el maximo de %d tareas\n",
                    max_tasks);
            break;
        }

        int id;
        char type;
        int work;
        int period;
        int deadline;
        int criticality;

        int fields = sscanf(
            line,
            "%d,%c,%d,%d,%d,%d",
            &id,
            &type,
            &work,
            &period,
            &deadline,
            &criticality);

        /* Si no se leen los seis campos la linea no es valida */
        if (fields != 6)
        {
            fprintf(stderr, "Línea de tarea inválida: %s", line);
            continue;
        }

        /* solamente admite P y S como tipo de tarea. */
        if (type != 'P' && type != 'S')
        {
            fprintf(stderr, "Tipo de tarea inválido: %c\n", type);
            continue;
        }

        /*
         * Se validan los valores mas importantes para evitar cargar
         * tareas con param sin sentido
         */
        if (id < 0 ||
            work <= 0 ||
            period <= 0 ||
            deadline <= 0 ||
            criticality < 0)
        {
            fprintf(stderr, "Valores invalidos en la tarea: %s", line);
            continue;
        }

        tasks[count].id = id;
        tasks[count].type =
            (type == 'P') ? TASK_PERIODIC : TASK_SPORADIC;
        tasks[count].work = work;
        tasks[count].period = period;
        tasks[count].deadline = deadline;
        tasks[count].criticality = criticality;

        count++;
    }

    fclose(file);
    return count;
}
