#pragma once
#include "host/ipc_queue.h"

/* Devuelve 0 para una orden válida, 1 para ayuda/línea vacía y -1 si es inválida. */
int host_parse_command(const char *line, ipc_message_t *command);
void host_print_help(void);
