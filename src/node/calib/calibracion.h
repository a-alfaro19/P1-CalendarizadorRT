#ifndef CALIBRACION_H
#define CALIBRACION_H

/*
 * Calculo de s_n, las unidades de trabajo (UT) que el nodo puede ejecutar en
 * un tick. La medicion la hace el puerto de cada placa; aqui solo esta la
 * cuenta, para poder probarla en PC.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Duracion de cada medicion. El ritmo de UT no depende del tick, asi que se
 * mide una ventana fija y se escala a la duracion pedida. */
#define CAL_VENTANA_MS 1000u

/* UT seguidas entre dos lecturas del reloj, para que leerlo no pese en la
 * medicion. */
#define CAL_UT_POR_BLOQUE 16u

/* Porcentaje del tick que se ofrece a las tareas. El resto queda para el
 * calendarizador y el envio de eventos. */
#define CAL_MARGEN_PCT 80u

/*
 * s_n a partir de una medicion: ut_contadas UT en us_medidos microsegundos,
 * escalado a un tick de tick_us microsegundos y con el margen aplicado.
 * Redondea hacia abajo y nunca devuelve 0 para una medicion valida. Devuelve
 * 0 si tick_us o us_medidos es 0; el host rechaza ese valor.
 */
uint32_t calib_s_n(uint32_t ut_contadas, uint32_t us_medidos, uint32_t tick_us);

#ifdef __cplusplus
}
#endif

#endif
