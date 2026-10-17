#include <stdio.h>
#include <unity.h>

#include "calendarizador.h"

static sched_t s;

void setUp(void)
{
    sched_init(&s);
}

void tearDown(void) {}

static void agregar(uint8_t id, uint16_t costo, uint16_t periodo, uint16_t plazo)
{
    tarea_def_t d = {.id = id, .tipo = TAREA_PERIODICA, .costo = costo, .periodo = periodo, .plazo = plazo};
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &d));
}

/* id de la tarea que ejecuta la decision, o ID_TAREA_NINGUNA si es idle. */
static uint8_t id_de(decision_t d)
{
    return d.slot == SLOT_IDLE ? ID_TAREA_NINGUNA : s.tareas[d.slot].def.id;
}

/* Corre n ticks y compara la secuencia de ids ejecutados con la esperada. */
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

#define N ID_TAREA_NINGUNA

static void test_antes_de_iniciar_no_ejecuta_ni_emite(void)
{
    agregar(1, 1, 2, 2);
    for (int t = 0; t < 5; t++)
        TEST_ASSERT_EQUAL_UINT8(SLOT_IDLE, sched_tick(&s).slot);
    assert_sin_mas_eventos();
}

static void test_iniciar_rechaza_algoritmo_desconocido(void)
{
    TEST_ASSERT_EQUAL_INT(ACK_INVALIDO, sched_iniciar(&s, 2, 100));
    TEST_ASSERT_EQUAL_UINT8(SLOT_IDLE, sched_tick(&s).slot);
}

static void test_una_tarea_alterna_ejecucion_e_idle(void)
{
    agregar(1, 2, 5, 5);
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_iniciar(&s, ALG_RMS, 100));

    const uint8_t traza[] = {1, 1, N, N, N, 1, 1, N, N, N};
    assert_traza(traza, 10);
}

static void test_una_tarea_emite_liberacion_cambio_y_completado(void)
{
    agregar(1, 2, 5, 5);
    sched_iniciar(&s, ALG_RMS, 100);
    for (int t = 0; t < 6; t++)
        sched_tick(&s);

    assert_evento(0, EV_LIBERACION, 1, 0);
    assert_evento(0, EV_CAMBIO_TAREA, 1, 0);
    assert_evento(2, EV_COMPLETADO, 1, 2);      /* termina al final del tick 1 */
    assert_evento(2, EV_CAMBIO_TAREA, N, 0);
    assert_evento(5, EV_LIBERACION, 1, 0);
    assert_evento(5, EV_CAMBIO_TAREA, 1, 0);
    assert_sin_mas_eventos();
}

static void test_rms_apropia_en_el_limite_de_tick(void)
{
    /* A (id 2) tiene menor periodo que B (id 1), asi que manda en RMS. */
    agregar(1, 4, 10, 10);   /* B */
    agregar(2, 1, 4, 4);     /* A */
    sched_iniciar(&s, ALG_RMS, 100);

    /* En t=4 A se libera y desplaza a B, a la que le falta un tick. */
    const uint8_t traza[] = {2, 1, 1, 1, 2, 1, N, N, 2, N};
    assert_traza(traza, 10);
}

static void test_rms_tiempo_de_respuesta_incluye_la_apropiacion(void)
{
    agregar(1, 4, 10, 10);
    agregar(2, 1, 4, 4);
    sched_iniciar(&s, ALG_RMS, 100);
    for (int t = 0; t < 7; t++)
        sched_tick(&s);

    assert_evento(0, EV_LIBERACION, 1, 0);
    assert_evento(0, EV_LIBERACION, 2, 0);
    assert_evento(0, EV_CAMBIO_TAREA, 2, 0);
    assert_evento(1, EV_COMPLETADO, 2, 1);
    assert_evento(1, EV_CAMBIO_TAREA, 1, 0);
    assert_evento(4, EV_LIBERACION, 2, 0);
    assert_evento(4, EV_CAMBIO_TAREA, 2, 0);
    assert_evento(5, EV_COMPLETADO, 2, 1);
    assert_evento(5, EV_CAMBIO_TAREA, 1, 0);
    assert_evento(6, EV_COMPLETADO, 1, 6);
    assert_evento(6, EV_CAMBIO_TAREA, N, 0);
    assert_sin_mas_eventos();
}

static void test_leds_muestran_slot_mas_uno_e_idle_en_cero(void)
{
    agregar(7, 1, 2, 2);
    sched_iniciar(&s, ALG_RMS, 100);

    TEST_ASSERT_EQUAL_UINT8(1, sched_tick(&s).leds_tarea);   /* slot 0 */
    TEST_ASSERT_EQUAL_UINT8(0, sched_tick(&s).leds_tarea);   /* idle */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_antes_de_iniciar_no_ejecuta_ni_emite);
    RUN_TEST(test_iniciar_rechaza_algoritmo_desconocido);
    RUN_TEST(test_una_tarea_alterna_ejecucion_e_idle);
    RUN_TEST(test_una_tarea_emite_liberacion_cambio_y_completado);
    RUN_TEST(test_rms_apropia_en_el_limite_de_tick);
    RUN_TEST(test_rms_tiempo_de_respuesta_incluye_la_apropiacion);
    RUN_TEST(test_leds_muestran_slot_mas_uno_e_idle_en_cero);
    return UNITY_END();
}
