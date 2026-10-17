#include <stdio.h>
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

static void assert_traza(const uint8_t *esperada, int n)
{
    for (int t = 0; t < n; t++) {
        uint8_t id = id_de(sched_tick(&s));
        char msg[32];
        snprintf(msg, sizeof msg, "tick %d", t);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(esperada[t], id, msg);
    }
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

static int contar_eventos(uint8_t tipo, uint8_t id)
{
    int n = 0;
    evento_t e;
    while (evq_pop(&s.eventos, &e))
        if (e.tipo == tipo && e.id_tarea == id)
            n++;
    return n;
}

static void test_aborta_justo_cuando_el_tick_alcanza_el_plazo(void)
{
    agregar(1, 3, 5, 2);    /* necesita 3 ticks y solo tiene 2 */
    sched_iniciar(&s, ALG_EDF, 100);

    const uint8_t traza[] = {1, 1, N, N, N, 1, 1, N};
    assert_traza(traza, 8);

    assert_evento(0, EV_LIBERACION, 1, 0);
    assert_evento(0, EV_CAMBIO_TAREA, 1, 0);
    assert_evento(2, EV_PLAZO_PERDIDO, 1, 0);
    assert_evento(2, EV_CAMBIO_TAREA, N, 0);
    assert_evento(5, EV_LIBERACION, 1, 0);
    assert_evento(5, EV_CAMBIO_TAREA, 1, 0);
    assert_evento(7, EV_PLAZO_PERDIDO, 1, 0);
    assert_evento(7, EV_CAMBIO_TAREA, N, 0);
    assert_sin_mas_eventos();
}

static void test_aborta_antes_de_liberar_el_siguiente_trabajo_de_la_misma_tarea(void)
{
    agregar(1, 3, 2, 2);    /* D = T: el plazo y la nueva liberacion coinciden */
    sched_iniciar(&s, ALG_RMS, 100);

    const uint8_t traza[] = {1, 1, 1, 1};
    assert_traza(traza, 4);

    assert_evento(0, EV_LIBERACION, 1, 0);
    assert_evento(0, EV_CAMBIO_TAREA, 1, 0);
    assert_evento(2, EV_PLAZO_PERDIDO, 1, 0);
    assert_evento(2, EV_LIBERACION, 1, 0);
}

static void test_el_trabajo_abortado_no_se_completa_y_las_demas_siguen(void)
{
    agregar(1, 1, 4, 4);
    agregar(2, 9, 10, 3);   /* imposible: siempre pierde */
    sched_iniciar(&s, ALG_EDF, 100);
    for (int t = 0; t < 40; t++)
        sched_tick(&s);

    sched_t copia = s;
    TEST_ASSERT_EQUAL_INT(10, contar_eventos(EV_COMPLETADO, 1));
    s = copia;
    TEST_ASSERT_EQUAL_INT(0, contar_eventos(EV_COMPLETADO, 2));
    s = copia;
    TEST_ASSERT_EQUAL_INT(4, contar_eventos(EV_PLAZO_PERDIDO, 2));
}

static void test_led_de_plazo_se_enciende_al_perder_y_queda_encendido(void)
{
    agregar(1, 3, 100, 2);  /* pierde una sola vez, en t = 2 */
    sched_iniciar(&s, ALG_EDF, 100);

    TEST_ASSERT_FALSE(sched_tick(&s).led_plazo);   /* t = 0 */
    TEST_ASSERT_FALSE(sched_tick(&s).led_plazo);   /* t = 1 */
    TEST_ASSERT_TRUE(sched_tick(&s).led_plazo);    /* t = 2: pierde */
    for (int t = 3; t < 100; t++)
        TEST_ASSERT_TRUE(sched_tick(&s).led_plazo);
}

/*
 * Mitad RMS de la traza dorada de test_edf: A = (C 2, T 5), B = (C 4, T 7).
 * Con RMS, B pierde su plazo en t = 7 y se aborta; EDF no pierde ninguno.
 * Secuencia del simulador de referencia independiente.
 */
static void test_traza_dorada_rms_pierde_el_plazo_de_b_en_el_tick_7(void)
{
    agregar(2, 4, 7, 7);
    agregar(1, 2, 5, 5);
    sched_iniciar(&s, ALG_RMS, 100);

    const uint8_t traza[35] = {
        1, 1, 2, 2, 2, 1, 1, 2, 2, 2, 1, 1, 2, N, 2, 1, 1, 2,
        2, 2, 1, 1, 2, 2, 2, 1, 1, 2, 2, 2, 1, 1, 2, 2, N,
    };
    assert_traza(traza, 35);

    int perdidas_b = 0, perdidas_a = 0;
    evento_t e;
    while (evq_pop(&s.eventos, &e)) {
        if (e.tipo != EV_PLAZO_PERDIDO)
            continue;
        if (e.id_tarea == 2) {
            perdidas_b++;
            TEST_ASSERT_EQUAL_UINT32(7, e.tick);
        } else {
            perdidas_a++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, perdidas_b);
    TEST_ASSERT_EQUAL_INT(0, perdidas_a);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_aborta_justo_cuando_el_tick_alcanza_el_plazo);
    RUN_TEST(test_aborta_antes_de_liberar_el_siguiente_trabajo_de_la_misma_tarea);
    RUN_TEST(test_el_trabajo_abortado_no_se_completa_y_las_demas_siguen);
    RUN_TEST(test_led_de_plazo_se_enciende_al_perder_y_queda_encendido);
    RUN_TEST(test_traza_dorada_rms_pierde_el_plazo_de_b_en_el_tick_7);
    return UNITY_END();
}
