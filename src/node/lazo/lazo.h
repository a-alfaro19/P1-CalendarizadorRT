#ifndef LAZO_H
#define LAZO_H

/*
 * Logica del lazo principal del nodo que no depende de la placa: cuantos
 * ticks del temporizador faltan por procesar, las sobrecargas y el debounce
 * del boton. Los puertos solo leen el reloj y los contadores de sus ISR.
 */

#include <stdbool.h>
#include <stdint.h>

/* Ruta relativa: el Makefile del host compila todos los .c de src/ solo con
 * -Iinclude, y debe encontrar este header igual que PlatformIO. */
#include "../sched/sched_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ticks del temporizador. La ISR solo incrementa su contador de ticks
 * generados; el lazo lleva aqui los procesados. Cada contador tiene un solo
 * escritor, asi que no hace falta deshabilitar interrupciones. La diferencia
 * se calcula con aritmetica sin signo y tolera que los contadores den la
 * vuelta.
 */
typedef struct {
    uint32_t procesados;
    uint32_t sobrecargas;   /* tramos que terminaron con el siguiente tick ya generado */
} lazo_ticks_t;

/* Descarta los ticks pendientes y pone sobrecargas en 0. Se usa al arrancar y
 * al cambiar la duracion del tick. */
void lazo_ticks_reiniciar(lazo_ticks_t *l, uint32_t generados);

/* Si hay un tick generado sin procesar, lo marca como procesado y devuelve
 * true. Los ticks atrasados se toman de uno en uno, en orden. */
bool lazo_tomar_tick(lazo_ticks_t *l, uint32_t generados);

/* Se llama al terminar el tramo del tick recien tomado. Si para entonces el
 * temporizador ya genero el siguiente tick, el tramo se paso de su tick y se
 * cuenta una sobrecarga. */
void lazo_fin_tramo(lazo_ticks_t *l, uint32_t generados);

/* Debounce del boton, por tiempo desde la ultima pulsacion aceptada. */
typedef struct {
    uint32_t ultima_ms;
    bool hubo;      /* ya se acepto al menos una pulsacion */
} lazo_rebote_t;

/*
 * Devuelve true si un flanco en ahora_ms es una pulsacion nueva y no un rebote
 * de la anterior. Va en el header y siempre en linea porque la llama la ISR
 * del boton: asi queda dentro de la ISR, que en el ESP8266 debe vivir en IRAM.
 * Con solo static inline el compilador podria dejar una copia en flash.
 */
static inline __attribute__((always_inline))
bool lazo_rebote_aceptar(lazo_rebote_t *r, uint32_t ahora_ms)
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
