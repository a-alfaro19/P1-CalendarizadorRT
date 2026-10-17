#include <unity.h>

#include "calendarizador.h"

static sched_t s;

void setUp(void)
{
    sched_init(&s);
}

void tearDown(void) {}

#define N ID_TAREA_NINGUNA

static void agregar(uint8_t id, uint16_t costo, uint16_t periodo, uint16_t plazo)
{
    tarea_def_t d = {.id = id, .tipo = TAREA_PERIODICA, .costo = costo, .periodo = periodo, .plazo = plazo};
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

static void vaciar_eventos(void)
{
    evento_t e;
    while (evq_pop(&s.eventos, &e)) {}
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

static void test_alta_en_caliente_libera_en_el_tick_en_que_se_agrega(void)
{
    agregar(1, 1, 10, 10);
    sched_iniciar(&s, ALG_EDF, 100);
    correr(5);                              /* t = 0..4 */
    vaciar_eventos();

    agregar(2, 1, 4, 4);                    /* entre el tick 4 y el 5 */
    TEST_ASSERT_EQUAL_UINT8(2, id_de(sched_tick(&s)));     /* t = 5 */
    assert_evento(5, EV_LIBERACION, 2, 0);

    /* Sus liberaciones siguientes se cuentan desde el alta: 9, 13, ... */
    correr(3);                              /* t = 6..8 */
    vaciar_eventos();
    TEST_ASSERT_EQUAL_UINT8(2, id_de(sched_tick(&s)));     /* t = 9 */
    assert_evento(9, EV_LIBERACION, 2, 0);
}

static void test_alta_en_caliente_no_altera_a_las_demas(void)
{
    agregar(1, 1, 10, 10);
    sched_iniciar(&s, ALG_EDF, 100);
    correr(5);
    agregar(2, 1, 4, 4);
    correr(5);                              /* t = 5..9 */
    vaciar_eventos();

    TEST_ASSERT_EQUAL_UINT8(1, id_de(sched_tick(&s)));     /* t = 10 */
    assert_evento(10, EV_LIBERACION, 1, 0);
}

static void test_iniciar_repetido_no_reinicia_el_tick_ni_cambia_el_algoritmo(void)
{
    agregar(1, 1, 10, 10);
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_iniciar(&s, ALG_RMS, 100));
    correr(7);

    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_iniciar(&s, ALG_EDF, 3));
    TEST_ASSERT_EQUAL_UINT32(7, s.tick);
    TEST_ASSERT_EQUAL_UINT8(ALG_RMS, s.alg);
    TEST_ASSERT_EQUAL_UINT16(100, s.periodo_reporte);
}

static void test_iniciar_rechaza_periodo_de_reporte_cero(void)
{
    TEST_ASSERT_EQUAL_INT(ACK_INVALIDO, sched_iniciar(&s, ALG_RMS, 0));
    TEST_ASSERT_FALSE(s.en_marcha);
}

static void test_reporte_tick_cada_report_period_ticks(void)
{
    sched_iniciar(&s, ALG_EDF, 3);
    for (int t = 0; t < 10; t++)
        TEST_ASSERT_EQUAL_MESSAGE(t % 3 == 0, sched_tick(&s).reporte_tick, "reporte_tick");
}

static void test_detener_deja_idle_y_apaga_los_leds(void)
{
    agregar(1, 3, 100, 2);                  /* pierde el plazo en t = 2 */
    sched_iniciar(&s, ALG_EDF, 1);
    correr(3);
    TEST_ASSERT_TRUE(s.plazo_perdido);

    sched_detener(&s);
    for (int i = 0; i < 5; i++) {
        decision_t d = sched_tick(&s);
        TEST_ASSERT_EQUAL_UINT8(SLOT_IDLE, d.slot);
        TEST_ASSERT_EQUAL_UINT8(0, d.leds_tarea);
        TEST_ASSERT_FALSE(d.led_plazo);
        TEST_ASSERT_FALSE(d.reporte_tick);
    }
}

static void test_detener_no_emite_eventos_y_es_idempotente(void)
{
    agregar(1, 1, 2, 2);
    sched_iniciar(&s, ALG_RMS, 100);
    correr(3);
    vaciar_eventos();

    sched_detener(&s);
    sched_detener(&s);
    correr(10);

    evento_t e;
    TEST_ASSERT_FALSE(evq_pop(&s.eventos, &e));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_alta_en_caliente_libera_en_el_tick_en_que_se_agrega);
    RUN_TEST(test_alta_en_caliente_no_altera_a_las_demas);
    RUN_TEST(test_iniciar_repetido_no_reinicia_el_tick_ni_cambia_el_algoritmo);
    RUN_TEST(test_iniciar_rechaza_periodo_de_reporte_cero);
    RUN_TEST(test_reporte_tick_cada_report_period_ticks);
    RUN_TEST(test_detener_deja_idle_y_apaga_los_leds);
    RUN_TEST(test_detener_no_emite_eventos_y_es_idempotente);
    return UNITY_END();
}
