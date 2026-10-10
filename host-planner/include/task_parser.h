#ifndef TASK_PARSER_H
#define TASK_PARSER_H

#include "task.h"

/* Cantidad max de tareas que se pueden leer del archivo */
#define MAX_TASKS 100

/*
 * Lee las tareas desde un archivo de texto
 *
 * Retorna:
 *   >= 0 : cant tareas cargadas correctamente
 *   -1   : error al abrir el archivo
 */
int load_tasks(const char *filename, Task tasks[], int max_tasks);

#endif
