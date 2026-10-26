#include "host/console.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void skip_space(const char **p)
{
    while (isspace((unsigned char)**p)) (*p)++;
}

static int number(const char **p, int *value)
{
    skip_space(p);
    char *end;
    errno = 0;
    long n = strtol(*p, &end, 10);
    if (errno || end == *p || n < 0 || n > INT_MAX) return -1;
    *p = end;
    *value = (int)n;
    skip_space(p);
    return 0;
}

static int comma(const char **p)
{
    skip_space(p);
    if (**p != ',') return -1;
    (*p)++;
    return 0;
}

int host_parse_command(const char *line, ipc_message_t *command)
{
    if (!line || !command) return -1;
    const char *p = line;
    skip_space(&p);
    const char *start = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    size_t len = (size_t)(p - start);
    skip_space(&p);
    ipc_message_t parsed = {0};
    if (!len || (len == 4 && !strncmp(start, "help", len) && !*p)) return 1;
    if (len == 3 && !strncmp(start, "add", len)) {
        parsed.type = IPC_ADD_TASK;
        Task *task = &parsed.payload.task;
        if (number(&p, &task->id) < 0 || comma(&p) < 0) return -1;
        skip_space(&p);
        if (*p != 'P' && *p != 'S') return -1;
        task->type = *p++ == 'P' ? TASK_PERIODIC : TASK_SPORADIC;
        if (comma(&p) < 0 || number(&p, &task->work) < 0 ||
            comma(&p) < 0 || number(&p, &task->period) < 0 ||
            comma(&p) < 0 || number(&p, &task->deadline) < 0 ||
            comma(&p) < 0 || number(&p, &task->criticality) < 0 || *p ||
            task->id >= UINT8_MAX || !task->work || !task->period ||
            task->period > UINT16_MAX || !task->deadline || task->deadline > task->period)
            return -1;
    } else if (len == 4 && !strncmp(start, "fail", len)) {
        parsed.type = IPC_FAIL_NODE;
        if (number(&p, &parsed.node_id) < 0 || *p ||
            parsed.node_id < 1 || parsed.node_id > HOST_NODE_COUNT) return -1;
    } else if (len == 6 && !strncmp(start, "status", len) && !*p) {
        parsed.type = IPC_STATUS;
    } else if (len == 1 && *start == 'q' && !*p) {
        parsed.type = IPC_SHUTDOWN;
    } else return -1;
    *command = parsed;
    return 0;
}

void host_print_help(void)
{
    puts("Comandos (Enter para enviar):\n"
         "  add id,P|S,W,T,D,criticidad  Agregar una tarea sin detener los nodos\n"
         "  fail nodo                  Simular desconexión del nodo 1 o 2\n"
         "  status                     Mostrar asignaciones y nodos activos\n"
         "  q                          Solicitar detención y cerrar el host\n"
         "  help                       Mostrar esta ayuda");
}
