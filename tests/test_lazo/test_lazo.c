#include <stdint.h>
#include <unity.h>

#include "lazo.h"

static lazo_ticks_t l;

void setUp(void)
{
    lazo_ticks_reiniciar(&l, 0);
}

void tearDown(void) {}

static void test_sin_ticks_generados_no_hay_tick_que_tomar(void)
{
    TEST_ASSERT_FALSE(lazo_tomar_tick(&l, 0));
}

static void test_cada_tick_generado_se_toma_una_sola_vez(void)
{
    TEST_ASSERT_TRUE(lazo_tomar_tick(&l, 1));
    TEST_ASSERT_FALSE(lazo_tomar_tick(&l, 1));
}

static void test_los_ticks_atrasados_se_toman_todos_sin_saltarse_ninguno(void)
{
    int tomados = 0;
    while (lazo_tomar_tick(&l, 3))
        tomados++;
    TEST_ASSERT_EQUAL_INT(3, tomados);
}

static void test_un_tramo_que_termina_antes_del_siguiente_tick_no_es_sobrecarga(void)
{
    lazo_tomar_tick(&l, 1);
    lazo_fin_tramo(&l, 1);
    TEST_ASSERT_EQUAL_UINT32(0, l.sobrecargas);
}

static void test_un_tramo_que_termina_con_el_siguiente_tick_ya_generado_es_sobrecarga(void)
{
    lazo_tomar_tick(&l, 1);
    lazo_fin_tramo(&l, 2);
    TEST_ASSERT_EQUAL_UINT32(1, l.sobrecargas);
}

static void test_reiniciar_descarta_los_ticks_pendientes_y_las_sobrecargas(void)
{
    lazo_tomar_tick(&l, 1);
    lazo_fin_tramo(&l, 2);
    lazo_ticks_reiniciar(&l, 120);
    TEST_ASSERT_FALSE(lazo_tomar_tick(&l, 120));
    TEST_ASSERT_EQUAL_UINT32(0, l.sobrecargas);
}

static void test_el_contador_de_ticks_puede_dar_la_vuelta(void)
{
    lazo_ticks_reiniciar(&l, UINT32_MAX);
    TEST_ASSERT_TRUE(lazo_tomar_tick(&l, 0));
    lazo_fin_tramo(&l, 0);
    TEST_ASSERT_EQUAL_UINT32(0, l.sobrecargas);
    TEST_ASSERT_FALSE(lazo_tomar_tick(&l, 0));
}

static void test_la_primera_pulsacion_se_acepta(void)
{
    lazo_rebote_t r = {0};
    TEST_ASSERT_TRUE(lazo_rebote_aceptar(&r, 3));
}

static void test_los_rebotes_dentro_de_la_ventana_se_ignoran(void)
{
    lazo_rebote_t r = {0};
    lazo_rebote_aceptar(&r, 1000);
    TEST_ASSERT_FALSE(lazo_rebote_aceptar(&r, 1001));
    TEST_ASSERT_FALSE(lazo_rebote_aceptar(&r, 1000 + DEBOUNCE_MS - 1));
}

static void test_una_pulsacion_despues_de_la_ventana_se_acepta(void)
{
    lazo_rebote_t r = {0};
    lazo_rebote_aceptar(&r, 1000);
    lazo_rebote_aceptar(&r, 1010);
    TEST_ASSERT_TRUE(lazo_rebote_aceptar(&r, 1000 + DEBOUNCE_MS));
}

static void test_el_rebote_funciona_cuando_millis_da_la_vuelta(void)
{
    lazo_rebote_t r = {0};
    lazo_rebote_aceptar(&r, UINT32_MAX - 10);
    TEST_ASSERT_FALSE(lazo_rebote_aceptar(&r, 5));
    TEST_ASSERT_TRUE(lazo_rebote_aceptar(&r, DEBOUNCE_MS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sin_ticks_generados_no_hay_tick_que_tomar);
    RUN_TEST(test_cada_tick_generado_se_toma_una_sola_vez);
    RUN_TEST(test_los_ticks_atrasados_se_toman_todos_sin_saltarse_ninguno);
    RUN_TEST(test_un_tramo_que_termina_antes_del_siguiente_tick_no_es_sobrecarga);
    RUN_TEST(test_un_tramo_que_termina_con_el_siguiente_tick_ya_generado_es_sobrecarga);
    RUN_TEST(test_reiniciar_descarta_los_ticks_pendientes_y_las_sobrecargas);
    RUN_TEST(test_el_contador_de_ticks_puede_dar_la_vuelta);
    RUN_TEST(test_la_primera_pulsacion_se_acepta);
    RUN_TEST(test_los_rebotes_dentro_de_la_ventana_se_ignoran);
    RUN_TEST(test_una_pulsacion_despues_de_la_ventana_se_acepta);
    RUN_TEST(test_el_rebote_funciona_cuando_millis_da_la_vuelta);
    return UNITY_END();
}
