#include <stdint.h>

#include "test_harness.h"

#include "node/button/rebote.h"

static void test_la_primera_pulsacion_se_acepta(void)
{
    rebote_t r = {0};
    assert(rebote_aceptar(&r, 3));
}

static void test_los_rebotes_dentro_de_la_ventana_se_ignoran(void)
{
    rebote_t r = {0};
    rebote_aceptar(&r, 1000);
    assert(!rebote_aceptar(&r, 1001));
    assert(!rebote_aceptar(&r, 1000 + DEBOUNCE_MS - 1));
}

static void test_una_pulsacion_despues_de_la_ventana_se_acepta(void)
{
    rebote_t r = {0};
    rebote_aceptar(&r, 1000);
    rebote_aceptar(&r, 1010);
    assert(rebote_aceptar(&r, 1000 + DEBOUNCE_MS));
}

static void test_el_rebote_funciona_cuando_millis_da_la_vuelta(void)
{
    rebote_t r = {0};
    rebote_aceptar(&r, UINT32_MAX - 10);
    assert(!rebote_aceptar(&r, 5));
    assert(rebote_aceptar(&r, DEBOUNCE_MS));
}

int main(void)
{
    puts("Button debounce");
    RUN_TEST(test_la_primera_pulsacion_se_acepta);
    RUN_TEST(test_los_rebotes_dentro_de_la_ventana_se_ignoran);
    RUN_TEST(test_una_pulsacion_despues_de_la_ventana_se_acepta);
    RUN_TEST(test_el_rebote_funciona_cuando_millis_da_la_vuelta);
    puts("OK: All button debounce tests passed!");
    return 0;
}
