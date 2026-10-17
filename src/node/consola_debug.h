#ifndef CONSOLA_DEBUG_H
#define CONSOLA_DEBUG_H

/*
 * Consola de depuracion PROVISIONAL: permite probar el nodo en la placa con
 * un monitor serial, sin el host. Se reemplaza por el protocolo binario de
 * docs/PROTOCOL.md (T12).
 *
 * Comandos, uno por linea (los numeros en decimal):
 *   c <tick_ms>                 calibrar (0x00); responde "s_n <valor>"
 *   t <id> <kind> <C> <T> <D>   desplegar una tarea (0x02); responde "ack <status>"
 *   i <alg> <report_period>     iniciar (0x03), alg 0 = RMS, 1 = EDF
 *   d                           disparo de la esporadica (0x06)
 *   s                           detener (0x07)
 *   ?                           estado: tick, s_n y contadores
 *
 * Salida: "ev <tick> <tipo> <id> <aux>" por cada evento (0x04) y
 * "rep <tick>" en cada reporte de tick (0x05).
 */

#include <stdbool.h>

#include "nodo.h"

/* Atiende los comandos recibidos y escribe los eventos pendientes, sin
 * bloquear: solo escribe si cabe en el bufer de transmision. */
void consola_atender(nodo_t *n, bool reporte_tick);

#endif
