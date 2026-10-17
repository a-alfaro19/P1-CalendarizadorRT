#ifndef NODO_H
#define NODO_H

/*
 * Estado del programa del nodo. Lo crea node.cpp; la consola de depuracion
 * (y despues el modulo de protocolo) lo recibe para atender los mensajes del
 * host.
 */

#include <stdint.h>

#include "calendarizador.h"
#include "lazo.h"

typedef struct {
    sched_t sched;
    lazo_ticks_t ticks;
    uint32_t s_n;       /* UT por tick medidas en la ultima calibracion; 0 = sin calibrar */
} nodo_t;

/*
 * Mensaje 0x00: cambia el tick a tick_ms, mide s_n y lo devuelve para 0x01.
 * Bloquea mientras mide (CAL_VENTANA_MS). Devuelve 0, sin cambiar nada, si el
 * calendarizador ya esta en marcha o la placa no admite esa duracion de tick.
 */
uint32_t nodo_calibrar(nodo_t *n, uint16_t tick_ms);

#endif
