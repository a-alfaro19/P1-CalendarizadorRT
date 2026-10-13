#ifndef MEDICION_H
#define MEDICION_H

/* Medicion de s_n en la placa. Depende de Arduino, por eso no entra en native. */

#include <stdint.h>

/*
 * Mensaje 0x00: ejecuta unidad_trabajo() durante CAL_VENTANA_MS, medido con
 * micros() (temporizador de hardware), y devuelve s_n para un tick de
 * tick_ms. Bloquea mientras mide. Devuelve 0 si tick_ms es 0.
 */
uint32_t calibrar_ut_por_tick(uint16_t tick_ms);

#endif
