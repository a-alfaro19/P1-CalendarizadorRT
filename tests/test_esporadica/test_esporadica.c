#include <unity.h>

#include "calendarizador.h"

static sched_t s;

void setUp(void)
{
    sched_init(&s);
}

void tearDown(void) {}

#define N ID_TAREA_NINGUNA

static void agregar(uint8_t id, uint8_t tipo, uint16_t costo, uint16_t periodo, uint16_t plazo)
{
    tarea_def_t d = {.id = id, .tipo = tipo, .costo = costo, .periodo = periodo, .plazo = plazo};
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &d));
}

static uint8_t id_de(decision_t d)
{
    return d.slot == SLOT_IDLE ? ID_TAREA_NINGUNA : s.tareas[d.slot].def.id;
}

static void correr(int ticks)
{
    for (int i = 0; i < ticks; i++)
        sched_tick(&s);
}

static void assert_evento(uint32_t tick, uint8_t tipo, uint8_t id, uint16_t aux)
{
    evento_t e;
    TEST_ASSERT_TRUE_MESSAGE(evq_pop(&s.eventos, &e), "faltan eventos");
    TEST_ASSERT_EQUAL_UINT32(tick, e.tick);
    TEST_ASSERT_EQUAL_UINT8(tipo, e.tipo);
    TEST_ASSERT_EQUAL_UINT8(id, e.id_tarea);
    TEST_ASSERT_EQUAL_UINT16(aux, e.aux);
}

static void assert_sin_mas_eventos(void)
{
    evento_t e;
    TEST_ASSERT_FALSE_MESSAGE(evq_pop(&s.eventos, &e), "sobran eventos");
}

static void vaciar_eventos(void)
{
    evento_t e;
    while (evq_pop(&s.eventos, &e)) {}
}

static void test_disparo_valido_libera_un_trabajo_con_plazo_t_mas_d(void)
{
    agregar(3, TAREA_ESPORADICA, 2, 10, 4);
    sched_iniciar(&s, ALG_EDF, 100);
    correr(3);                              /* t = 0..2 en idle */

    sched_registrar_disparo(&s);
    TEST_ASSERT_EQUAL_UINT8(3, id_de(sched_tick(&s)));     /* t = 3 */
    TEST_ASSERT_EQUAL_UINT32(7, s.tareas[0].plazo_abs);

    assert_evento(3, EV_LIBERACION, 3, 0);
    assert_evento(3, EV_CAMBIO_TAREA, 3, 0);
    assert_sin_mas_eventos();
}

static void test_completado_lleva_la_latencia_desde_el_tick_del_disparo(void)
{
    agregar(3, TAREA_ESPORADICA, 2, 10, 4);
    sched_iniciar(&s, ALG_EDF, 100);
    correr(3);

    /* El disparo ocurre durante el tick 2; se libera en 3 y termina en 5. */
    sched_registrar_disparo(&s);
    correr(3);

    assert_evento(3, EV_LIBERACION, 3, 0);
    assert_evento(3, EV_CAMBIO_TAREA, 3, 0);
    assert_evento(5, EV_COMPLETADO, 3, 3);
    assert_evento(5, EV_CAMBIO_TAREA, N, 0);
    assert_sin_mas_eventos();
}

static void test_disparo_antes_del_intervalo_minimo_se_ignora_y_se_cuenta(void)
{
    agregar(3, TAREA_ESPORADICA, 1, 10, 10);
    sched_iniciar(&s, ALG_EDF, 100);
    sched_registrar_disparo(&s);
    correr(9);                              /* libera en t = 0 */
    vaciar_eventos();

    sched_registrar_disparo(&s);
    TEST_ASSERT_EQUAL_UINT8(SLOT_IDLE, sched_tick(&s).slot);   /* t = 9: 9 < T */
    TEST_ASSERT_EQUAL_UINT32(1, s.esporadicas_ignoradas);
    assert_evento(9, EV_ESPORADICA_IGNORADA, 3, 0);
    assert_sin_mas_eventos();

    sched_registrar_disparo(&s);
    TEST_ASSERT_EQUAL_UINT8(3, id_de(sched_tick(&s)));         /* t = 10: ya paso T */
    TEST_ASSERT_EQUAL_UINT32(1, s.esporadicas_ignoradas);
    assert_evento(10, EV_LIBERACION, 3, 0);
}

static void test_varios_disparos_en_el_mismo_tick_liberan_uno_e_ignoran_el_resto(void)
{
    agregar(3, TAREA_ESPORADICA, 1, 5, 5);
    sched_iniciar(&s, ALG_EDF, 100);
    correr(2);

    sched_registrar_disparo(&s);
    sched_registrar_disparo(&s);
    sched_registrar_disparo(&s);
    sched_tick(&s);                         /* t = 2 */

    TEST_ASSERT_EQUAL_UINT32(2, s.esporadicas_ignoradas);
    assert_evento(2, EV_LIBERACION, 3, 0);
    assert_evento(2, EV_ESPORADICA_IGNORADA, 3, 0);
    assert_evento(2, EV_ESPORADICA_IGNORADA, 3, 0);
    assert_evento(2, EV_CAMBIO_TAREA, 3, 0);
    assert_evento(3, EV_COMPLETADO, 3, 2);
    assert_sin_mas_eventos();
}

static void test_trabajo_vencido_se_aborta_antes_de_liberar_el_nuevo(void)
{
    /* D = T: el disparo valido llega justo cuando vence el trabajo anterior. */
    agregar(3, TAREA_ESPORADICA, 3, 2, 2);
    sched_iniciar(&s, ALG_EDF, 100);
    sched_registrar_disparo(&s);
    correr(2);                              /* libera en 0, corre 0 y 1 */
    vaciar_eventos();

    sched_registrar_disparo(&s);
    sched_tick(&s);                         /* t = 2 */

    assert_evento(2, EV_PLAZO_PERDIDO, 3, 0);
    assert_evento(2, EV_LIBERACION, 3, 0);
    assert_sin_mas_eventos();
    TEST_ASSERT_EQUAL_UINT16(2, s.tareas[0].restante);
}

static void test_disparo_antes_de_iniciar_se_descarta(void)
{
    agregar(3, TAREA_ESPORADICA, 1, 5, 5);
    sched_registrar_disparo(&s);
    sched_iniciar(&s, ALG_EDF, 100);

    TEST_ASSERT_EQUAL_UINT8(SLOT_IDLE, sched_tick(&s).slot);
    assert_sin_mas_eventos();
}

static void test_disparo_sin_tarea_esporadica_no_hace_nada(void)
{
    agregar(1, TAREA_PERIODICA, 1, 4, 4);
    sched_iniciar(&s, ALG_EDF, 100);
    correr(2);
    vaciar_eventos();

    sched_registrar_disparo(&s);
    TEST_ASSERT_EQUAL_UINT8(SLOT_IDLE, sched_tick(&s).slot);   /* t = 2 */
    TEST_ASSERT_EQUAL_UINT32(0, s.esporadicas_ignoradas);
    assert_sin_mas_eventos();
}

static void test_rms_la_esporadica_compite_con_su_intervalo_minimo_como_periodo(void)
{
    agregar(1, TAREA_PERIODICA, 3, 10, 10);
    agregar(2, TAREA_ESPORADICA, 1, 5, 5);
    sched_iniciar(&s, ALG_RMS, 100);
    sched_tick(&s);                         /* t = 0: corre la 1 */

    sched_registrar_disparo(&s);
    TEST_ASSERT_EQUAL_UINT8(2, id_de(sched_tick(&s)));     /* t = 1: se apropia */
    TEST_ASSERT_EQUAL_UINT8(1, id_de(sched_tick(&s)));     /* t = 2 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_disparo_valido_libera_un_trabajo_con_plazo_t_mas_d);
    RUN_TEST(test_completado_lleva_la_latencia_desde_el_tick_del_disparo);
    RUN_TEST(test_disparo_antes_del_intervalo_minimo_se_ignora_y_se_cuenta);
    RUN_TEST(test_varios_disparos_en_el_mismo_tick_liberan_uno_e_ignoran_el_resto);
    RUN_TEST(test_trabajo_vencido_se_aborta_antes_de_liberar_el_nuevo);
    RUN_TEST(test_disparo_antes_de_iniciar_se_descarta);
    RUN_TEST(test_disparo_sin_tarea_esporadica_no_hace_nada);
    RUN_TEST(test_rms_la_esporadica_compite_con_su_intervalo_minimo_como_periodo);
    return UNITY_END();
}
