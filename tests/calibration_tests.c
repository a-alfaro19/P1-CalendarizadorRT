#include <stdint.h>

#include "test_harness.h"

#include "calibracion.h"

static void test_escala_la_medicion_al_tick_y_aplica_el_margen(void)
{
    /* 1000 UT en 1 s = 10 UT por tick de 10 ms; con el margen del 80 % quedan 8. */
    assert(calib_s_n(1000, 1000000, 10000) == 8);
}

static void test_redondea_hacia_abajo(void)
{
    /* 1999 UT en 1 s = 19.99 UT por tick; 80 % = 15.99, se reporta 15. */
    assert(calib_s_n(1999, 1000000, 10000) == 15);
}

static void test_una_medicion_valida_nunca_da_cero(void)
{
    /* 1 UT en 1 s con tick de 1 ms: cabe una fraccion de UT por tick. */
    assert(calib_s_n(1, 1000000, 1000) == 1);
}

static void test_tick_cero_devuelve_cero(void)
{
    /* El host rechaza s_n = 0, asi que 0 marca la solicitud invalida. */
    assert(calib_s_n(1000, 1000000, 0) == 0);
}

static void test_tiempo_medido_cero_devuelve_cero(void)
{
    assert(calib_s_n(1000, 0, 10000) == 0);
}

static void test_no_desborda_con_valores_extremos(void)
{
    /* UT por tick satura en UINT32_MAX antes de aplicar el margen. */
    assert(calib_s_n(UINT32_MAX, 1, 65535000u) == 3435973836u);
}

int main(void)
{
    puts("Calibration");
    RUN_TEST(test_escala_la_medicion_al_tick_y_aplica_el_margen);
    RUN_TEST(test_redondea_hacia_abajo);
    RUN_TEST(test_una_medicion_valida_nunca_da_cero);
    RUN_TEST(test_tick_cero_devuelve_cero);
    RUN_TEST(test_tiempo_medido_cero_devuelve_cero);
    RUN_TEST(test_no_desborda_con_valores_extremos);
    puts("OK: All calibration tests passed!");
    return 0;
}
