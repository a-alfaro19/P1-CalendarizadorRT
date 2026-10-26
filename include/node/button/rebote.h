#ifndef REBOTE_H
#define REBOTE_H

/*
 * Debounce del boton de la tarea esporadica, por tiempo desde la ultima
 * pulsacion aceptada. No depende de la placa: la ISR le pasa millis().
 */

#include <stdbool.h>
#include <stdint.h>

#include "node/scheduler/sched_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t ultima_ms;
    bool hubo;      /* ya se acepto al menos una pulsacion */
} rebote_t;

/*
 * Devuelve true si un flanco en ahora_ms es una pulsacion nueva y no un rebote
 * de la anterior. Va en el header y siempre en linea porque la llama la ISR
 * del boton: asi queda dentro de la ISR, que en el ESP8266 debe vivir en IRAM.
 * Con solo static inline el compilador podria dejar una copia en flash.
 */
static inline __attribute__((always_inline))
bool rebote_aceptar(rebote_t *r, uint32_t ahora_ms)
{
    if (r->hubo && (uint32_t)(ahora_ms - r->ultima_ms) < DEBOUNCE_MS)
        return false;
    r->hubo = true;
    r->ultima_ms = ahora_ms;
    return true;
}

#ifdef __cplusplus
}
#endif

#endif
