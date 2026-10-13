#include "medicion.h"

#include <Arduino.h>

#include "calibracion.h"

/* ut.h es del curso y no se modifica; no trae guardas para C++. */
extern "C" {
#include "ut.h"
}

uint32_t calibrar_ut_por_tick(uint16_t tick_ms)
{
    const uint32_t ventana_us = CAL_VENTANA_MS * 1000u;
    uint32_t ut = 0;
    uint32_t transcurrido;
    uint32_t inicio = micros();
    do {
        for (uint32_t i = 0; i < CAL_UT_POR_BLOQUE; i++)
            unidad_trabajo();
        ut += CAL_UT_POR_BLOQUE;
        transcurrido = micros() - inicio;
    } while (transcurrido < ventana_us);

    return calib_s_n(ut, transcurrido, (uint32_t)tick_ms * 1000u);
}
