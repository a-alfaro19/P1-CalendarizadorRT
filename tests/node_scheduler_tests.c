#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <node/scheduler/calendarizador.h>
#include "test_harness.h"

#define TEST_ASSERT_EQUAL_INT(expected, actual) assert((expected) == (actual))
#define TEST_ASSERT_EQUAL_UINT8(expected, actual) assert((expected) == (actual))
#define TEST_ASSERT_EQUAL_UINT16(expected, actual) assert((expected) == (actual))
#define TEST_ASSERT_EQUAL_UINT32(expected, actual) assert((expected) == (actual))
#define TEST_ASSERT_EQUAL(expected, actual) assert((expected) == (actual))
#define TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected, actual, message) assert((expected) == (actual))
#define TEST_ASSERT_EQUAL_MESSAGE(expected, actual, message) assert((expected) == (actual))
#define TEST_ASSERT_EQUAL_MEMORY(expected, actual, size) \
    assert(memcmp((expected), (actual), (size)) == 0)
#define TEST_ASSERT_TRUE(condition) assert(condition)
#define TEST_ASSERT_FALSE(condition) assert(!(condition))
#define TEST_ASSERT_TRUE_MESSAGE(condition, message) assert(condition)
#define TEST_ASSERT_FALSE_MESSAGE(condition, message) assert(!(condition))
#define TEST_ASSERT_GREATER_THAN_UINT32(threshold, actual) assert((actual) > (threshold))
#define TEST_ASSERT_LESS_OR_EQUAL_UINT16(threshold, actual) assert((actual) <= (threshold))
#define TEST_FAIL_MESSAGE(message)                     \
    do                                                 \
    {                                                  \
        fprintf(stderr, "Test failed: %s\n", message); \
        abort();                                       \
    } while (0)

#define RUN_SCHED_TEST(test, state) \
    do                              \
    {                               \
        sched_init(&(state));       \
        RUN_TEST(test);             \
    } while (0)

#define RUN_EVQ_TEST(test, queue) \
    do                            \
    {                             \
        evq_init(&(queue));       \
        RUN_TEST(test);           \
    } while (0)

#define N ID_TAREA_NINGUNA

/* Scheduler lifecycle and in-flight task registration tests. */
#define s lifecycle_state
#define agregar lifecycle_agregar
#define id_de lifecycle_id_de
#define correr lifecycle_correr
#define vaciar_eventos lifecycle_vaciar_eventos
#define assert_evento lifecycle_assert_evento

static sched_t lifecycle_state;

static void lifecycle_agregar(uint8_t id, uint16_t costo, uint16_t periodo, uint16_t plazo)
{
    tarea_def_t d = {.id = id, .tipo = TAREA_PERIODICA, .costo = costo, .periodo = periodo, .plazo = plazo};
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &d));
}

static uint8_t lifecycle_id_de(decision_t d)
{
    return d.slot == SLOT_IDLE ? ID_TAREA_NINGUNA : s.tareas[d.slot].def.id;
}

static void lifecycle_correr(int ticks)
{
    for (int i = 0; i < ticks; i++)
        sched_tick(&s);
}

static void lifecycle_vaciar_eventos(void)
{
    evento_t e;
    while (evq_pop(&s.eventos, &e))
    {
    }
}

static void lifecycle_assert_evento(uint32_t tick, uint8_t tipo, uint8_t id, uint16_t aux)
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
    correr(5);
    vaciar_eventos();

    agregar(2, 1, 4, 4);
    TEST_ASSERT_EQUAL_UINT8(2, id_de(sched_tick(&s)));
    assert_evento(5, EV_LIBERACION, 2, 0);

    correr(3);
    vaciar_eventos();
    TEST_ASSERT_EQUAL_UINT8(2, id_de(sched_tick(&s)));
    assert_evento(9, EV_LIBERACION, 2, 0);
}

static void test_alta_en_caliente_no_altera_a_las_demas(void)
{
    agregar(1, 1, 10, 10);
    sched_iniciar(&s, ALG_EDF, 100);
    correr(5);
    agregar(2, 1, 4, 4);
    correr(5);
    vaciar_eventos();

    TEST_ASSERT_EQUAL_UINT8(1, id_de(sched_tick(&s)));
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
        TEST_ASSERT_EQUAL(t % 3 == 0, sched_tick(&s).reporte_tick);
}

static void test_detener_deja_idle_y_apaga_los_leds(void)
{
    agregar(1, 3, 100, 2);
    sched_iniciar(&s, ALG_EDF, 1);
    correr(3);
    TEST_ASSERT_TRUE(s.plazo_perdido);

    sched_detener(&s);
    for (int i = 0; i < 5; i++)
    {
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

#undef s
#undef agregar
#undef id_de
#undef correr
#undef vaciar_eventos
#undef assert_evento

/* RMS/EDF ordering and golden-trace tests. */
#define s ordering_state
#define agregar ordering_agregar
#define id_de ordering_id_de
#define assert_traza ordering_assert_traza
#define contar_eventos ordering_contar_eventos

static sched_t ordering_state;

static void ordering_agregar(uint8_t id, uint16_t costo, uint16_t periodo, uint16_t plazo)
{
    tarea_def_t d = {.id = id, .tipo = TAREA_PERIODICA, .costo = costo, .periodo = periodo, .plazo = plazo};
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &d));
}

static uint8_t ordering_id_de(decision_t d)
{
    return d.slot == SLOT_IDLE ? ID_TAREA_NINGUNA : s.tareas[d.slot].def.id;
}

static void ordering_assert_traza(const uint8_t *esperada, int n)
{
    for (int t = 0; t < n; t++)
    {
        uint8_t id = id_de(sched_tick(&s));
        char msg[32];
        snprintf(msg, sizeof msg, "tick %d", t);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(esperada[t], id, msg);
    }
}

static int ordering_contar_eventos(uint8_t tipo, uint8_t id)
{
    int n = 0;
    evento_t e;
    while (evq_pop(&s.eventos, &e))
        if (e.tipo == tipo && e.id_tarea == id)
            n++;
    return n;
}

static void test_rms_desempata_por_id_menor_sin_importar_el_orden_de_registro(void)
{
    agregar(5, 1, 4, 4);
    agregar(3, 1, 4, 4);
    sched_iniciar(&s, ALG_RMS, 100);

    const uint8_t traza[] = {3, 5, N, N};
    assert_traza(traza, 4);
}

static void test_edf_desempata_por_id_menor_ante_el_mismo_plazo_absoluto(void)
{
    agregar(4, 1, 4, 4);
    agregar(2, 1, 8, 4);
    sched_iniciar(&s, ALG_EDF, 100);

    const uint8_t traza[] = {2, 4, N, N};
    assert_traza(traza, 4);
}

static void test_edf_elige_el_plazo_absoluto_mas_cercano_y_no_el_periodo(void)
{
    agregar(1, 1, 5, 5);
    agregar(2, 1, 10, 2);
    sched_iniciar(&s, ALG_EDF, 100);

    const uint8_t traza[] = {2, 1, N, N, N};
    assert_traza(traza, 5);
}

static void test_traza_dorada_edf_cumple_todos_los_plazos_en_una_hiperperiodo(void)
{
    agregar(2, 4, 7, 7);
    agregar(1, 2, 5, 5);
    sched_iniciar(&s, ALG_EDF, 100);

    const uint8_t traza[35] = {
        1,
        1,
        2,
        2,
        2,
        2,
        1,
        1,
        2,
        2,
        2,
        2,
        1,
        1,
        2,
        1,
        1,
        2,
        2,
        2,
        1,
        1,
        2,
        2,
        2,
        2,
        1,
        1,
        2,
        2,
        1,
        1,
        2,
        2,
        N,
    };
    assert_traza(traza, 35);

    TEST_ASSERT_EQUAL_INT(7, contar_eventos(EV_COMPLETADO, 1));
}

static void test_traza_dorada_edf_completa_todos_los_trabajos_de_b(void)
{
    agregar(2, 4, 7, 7);
    agregar(1, 2, 5, 5);
    sched_iniciar(&s, ALG_EDF, 100);
    for (int t = 0; t < 35; t++)
        sched_tick(&s);

    TEST_ASSERT_EQUAL_INT(5, contar_eventos(EV_COMPLETADO, 2));
}

#undef s
#undef agregar
#undef id_de
#undef assert_traza
#undef contar_eventos

/* Sporadic task dispatch and minimum-interarrival tests. */
#define s sporadic_state
#define agregar sporadic_agregar
#define id_de sporadic_id_de
#define correr sporadic_correr
#define assert_evento sporadic_assert_evento
#define assert_sin_mas_eventos sporadic_assert_sin_mas_eventos
#define vaciar_eventos sporadic_vaciar_eventos

static sched_t sporadic_state;

static void sporadic_agregar(uint8_t id, uint8_t tipo, uint16_t costo, uint16_t periodo, uint16_t plazo)
{
    tarea_def_t d = {.id = id, .tipo = tipo, .costo = costo, .periodo = periodo, .plazo = plazo};
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &d));
}

static uint8_t sporadic_id_de(decision_t d)
{
    return d.slot == SLOT_IDLE ? ID_TAREA_NINGUNA : s.tareas[d.slot].def.id;
}

static void sporadic_correr(int ticks)
{
    for (int i = 0; i < ticks; i++)
        sched_tick(&s);
}

static void sporadic_assert_evento(uint32_t tick, uint8_t tipo, uint8_t id, uint16_t aux)
{
    evento_t e;
    TEST_ASSERT_TRUE_MESSAGE(evq_pop(&s.eventos, &e), "faltan eventos");
    TEST_ASSERT_EQUAL_UINT32(tick, e.tick);
    TEST_ASSERT_EQUAL_UINT8(tipo, e.tipo);
    TEST_ASSERT_EQUAL_UINT8(id, e.id_tarea);
    TEST_ASSERT_EQUAL_UINT16(aux, e.aux);
}

static void sporadic_assert_sin_mas_eventos(void)
{
    evento_t e;
    TEST_ASSERT_FALSE_MESSAGE(evq_pop(&s.eventos, &e), "sobran eventos");
}

static void sporadic_vaciar_eventos(void)
{
    evento_t e;
    while (evq_pop(&s.eventos, &e))
    {
    }
}

static void test_disparo_valido_libera_un_trabajo_con_plazo_t_mas_d(void)
{
    agregar(3, TAREA_ESPORADICA, 2, 10, 4);
    sched_iniciar(&s, ALG_EDF, 100);
    correr(3);

    sched_registrar_disparo(&s);
    TEST_ASSERT_EQUAL_UINT8(3, id_de(sched_tick(&s)));
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
    correr(9);
    vaciar_eventos();

    sched_registrar_disparo(&s);
    TEST_ASSERT_EQUAL_UINT8(SLOT_IDLE, sched_tick(&s).slot);
    TEST_ASSERT_EQUAL_UINT32(1, s.esporadicas_ignoradas);
    assert_evento(9, EV_ESPORADICA_IGNORADA, 3, 0);
    assert_sin_mas_eventos();

    sched_registrar_disparo(&s);
    TEST_ASSERT_EQUAL_UINT8(3, id_de(sched_tick(&s)));
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
    sched_tick(&s);

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
    agregar(3, TAREA_ESPORADICA, 3, 2, 2);
    sched_iniciar(&s, ALG_EDF, 100);
    sched_registrar_disparo(&s);
    correr(2);
    vaciar_eventos();

    sched_registrar_disparo(&s);
    sched_tick(&s);

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
    TEST_ASSERT_EQUAL_UINT8(SLOT_IDLE, sched_tick(&s).slot);
    TEST_ASSERT_EQUAL_UINT32(0, s.esporadicas_ignoradas);
    assert_sin_mas_eventos();
}

static void test_rms_la_esporadica_compite_con_su_intervalo_minimo_como_periodo(void)
{
    agregar(1, TAREA_PERIODICA, 3, 10, 10);
    agregar(2, TAREA_ESPORADICA, 1, 5, 5);
    sched_iniciar(&s, ALG_RMS, 100);
    sched_tick(&s);

    sched_registrar_disparo(&s);
    TEST_ASSERT_EQUAL_UINT8(2, id_de(sched_tick(&s)));
    TEST_ASSERT_EQUAL_UINT8(1, id_de(sched_tick(&s)));
}

#undef s
#undef agregar
#undef id_de
#undef correr
#undef assert_evento
#undef assert_sin_mas_eventos
#undef vaciar_eventos

/* Long-running invariant and overload tests. */
#define TICKS 10000
#define s stress_state
#define agregar stress_agregar
#define fallar stress_fallar
#define revisar_eventos stress_revisar_eventos
#define revisar_estado stress_revisar_estado
#define correr stress_correr
#define agregar_conjunto_factible stress_agregar_conjunto_factible
#define agregar_conjunto_sobrecargado stress_agregar_conjunto_sobrecargado
#define assert_sin_perdidas_y_respuesta_dentro_del_plazo stress_assert_sin_perdidas_y_respuesta

static sched_t stress_state;

typedef struct
{
    bool activo[MAX_TAREAS];
    uint32_t liberaciones, completados, perdidas;
    uint16_t peor_respuesta[MAX_TAREAS];
} resumen_t;

static void stress_agregar(uint8_t id, uint8_t tipo, uint16_t costo, uint16_t periodo, uint16_t plazo)
{
    tarea_def_t d = {.id = id, .tipo = tipo, .costo = costo, .periodo = periodo, .plazo = plazo};
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &d));
}

static void stress_fallar(uint32_t t, const char *que)
{
    char msg[64];
    snprintf(msg, sizeof msg, "tick %lu: %s", (unsigned long)t, que);
    TEST_FAIL_MESSAGE(msg);
}

static void stress_revisar_eventos(uint32_t t, decision_t d, resumen_t *r)
{
    evento_t e;
    while (evq_pop(&s.eventos, &e))
    {
        uint8_t slot = (uint8_t)(e.id_tarea - 1u);
        switch (e.tipo)
        {
        case EV_LIBERACION:
            if (e.tick != t || r->activo[slot])
                fallar(t, "liberacion con un trabajo activo");
            r->activo[slot] = true;
            r->liberaciones++;
            break;
        case EV_CAMBIO_TAREA:
            if (e.tick != t || e.id_tarea != (d.slot == SLOT_IDLE ? ID_TAREA_NINGUNA : d.slot + 1u))
                fallar(t, "cambio de tarea distinto de la decision");
            break;
        case EV_COMPLETADO:
            if (e.tick != t + 1 || !r->activo[slot])
                fallar(t, "completado sin trabajo activo");
            r->activo[slot] = false;
            r->completados++;
            if (e.aux > r->peor_respuesta[slot])
                r->peor_respuesta[slot] = e.aux;
            break;
        case EV_PLAZO_PERDIDO:
            if (e.tick != t || !r->activo[slot])
                fallar(t, "plazo perdido sin trabajo activo");
            r->activo[slot] = false;
            r->perdidas++;
            break;
        case EV_ESPORADICA_IGNORADA:
            break;
        default:
            fallar(t, "tipo de evento desconocido");
        }
    }
}

static void stress_revisar_estado(uint32_t t, decision_t d, const resumen_t *r)
{
    for (uint8_t i = 0; i < s.n_tareas; i++)
    {
        const tarea_t *ta = &s.tareas[i];
        if (ta->restante > ta->def.costo)
            fallar(t, "restante mayor que el costo");
        if (ta->restante > 0 && !r->activo[i])
            fallar(t, "restante sin trabajo activo");
    }
    if (d.slot != SLOT_IDLE && d.slot >= s.n_tareas)
        fallar(t, "decision fuera de rango");
    if (d.leds_tarea != (d.slot == SLOT_IDLE ? 0 : d.slot + 1u))
        fallar(t, "leds distintos del slot");
}

static resumen_t stress_correr(uint8_t alg, uint32_t disparar_cada)
{
    resumen_t r = {0};
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_iniciar(&s, alg, 100));
    for (uint32_t t = 0; t < TICKS; t++)
    {
        if (disparar_cada != 0 && t % disparar_cada == 0)
            sched_registrar_disparo(&s);
        decision_t d = sched_tick(&s);
        revisar_eventos(t, d, &r);
        revisar_estado(t, d, &r);
    }
    TEST_ASSERT_EQUAL_UINT32(0, s.eventos.descartados);

    uint32_t activos = 0;
    for (uint8_t i = 0; i < s.n_tareas; i++)
        activos += r.activo[i];
    TEST_ASSERT_EQUAL_UINT32(r.liberaciones, r.completados + r.perdidas + activos);
    return r;
}

static void stress_agregar_conjunto_factible(void)
{
    agregar(1, TAREA_PERIODICA, 1, 10, 10);
    agregar(2, TAREA_PERIODICA, 1, 12, 12);
    agregar(3, TAREA_PERIODICA, 2, 15, 15);
    agregar(4, TAREA_PERIODICA, 1, 20, 20);
    agregar(5, TAREA_PERIODICA, 2, 24, 24);
    agregar(6, TAREA_PERIODICA, 3, 30, 30);
    agregar(7, TAREA_PERIODICA, 2, 40, 40);
    agregar(8, TAREA_ESPORADICA, 3, 60, 60);
}

static void stress_agregar_conjunto_sobrecargado(void)
{
    agregar(1, TAREA_PERIODICA, 2, 7, 5);
    agregar(2, TAREA_PERIODICA, 3, 11, 9);
    agregar(3, TAREA_PERIODICA, 4, 13, 13);
    agregar(4, TAREA_PERIODICA, 2, 17, 6);
    agregar(5, TAREA_PERIODICA, 5, 19, 19);
    agregar(6, TAREA_PERIODICA, 3, 23, 10);
    agregar(7, TAREA_PERIODICA, 6, 29, 29);
    agregar(8, TAREA_ESPORADICA, 4, 31, 12);
}

static void stress_assert_sin_perdidas_y_respuesta(const resumen_t *r)
{
    TEST_ASSERT_EQUAL_UINT32(0, r->perdidas);
    TEST_ASSERT_GREATER_THAN_UINT32(0, r->completados);
    for (uint8_t i = 0; i < s.n_tareas; i++)
    {
        const tarea_def_t *def = &s.tareas[i].def;
        uint16_t cota = def->plazo + (def->tipo == TAREA_ESPORADICA ? 1u : 0u);
        TEST_ASSERT_LESS_OR_EQUAL_UINT16(cota, r->peor_respuesta[i]);
    }
}

static void test_rms_factible_no_pierde_plazos(void)
{
    agregar_conjunto_factible();
    resumen_t r = correr(ALG_RMS, 45);
    assert_sin_perdidas_y_respuesta_dentro_del_plazo(&r);
}

static void test_edf_factible_no_pierde_plazos(void)
{
    agregar_conjunto_factible();
    resumen_t r = correr(ALG_EDF, 45);
    assert_sin_perdidas_y_respuesta_dentro_del_plazo(&r);
}

static void test_rms_sobrecargado_mantiene_las_invariantes(void)
{
    agregar_conjunto_sobrecargado();
    resumen_t r = correr(ALG_RMS, 7);
    TEST_ASSERT_GREATER_THAN_UINT32(0, r.perdidas);
    TEST_ASSERT_GREATER_THAN_UINT32(0, s.esporadicas_ignoradas);
}

static void test_edf_sobrecargado_mantiene_las_invariantes(void)
{
    agregar_conjunto_sobrecargado();
    resumen_t r = correr(ALG_EDF, 7);
    TEST_ASSERT_GREATER_THAN_UINT32(0, r.perdidas);
    TEST_ASSERT_GREATER_THAN_UINT32(0, s.esporadicas_ignoradas);
}

#undef TICKS
#undef s
#undef agregar
#undef fallar
#undef revisar_eventos
#undef revisar_estado
#undef correr
#undef agregar_conjunto_factible
#undef agregar_conjunto_sobrecargado
#undef assert_sin_perdidas_y_respuesta_dentro_del_plazo

/* Deadline expiration, abort, and completion accounting tests. */
#define s deadline_state
#define agregar deadline_agregar
#define id_de deadline_id_de
#define assert_traza deadline_assert_traza
#define assert_evento deadline_assert_evento
#define assert_sin_mas_eventos deadline_assert_sin_mas_eventos
#define contar_eventos deadline_contar_eventos

static sched_t deadline_state;

static void deadline_agregar(uint8_t id, uint16_t costo, uint16_t periodo, uint16_t plazo)
{
    tarea_def_t d = {.id = id, .tipo = TAREA_PERIODICA, .costo = costo, .periodo = periodo, .plazo = plazo};
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &d));
}

static uint8_t deadline_id_de(decision_t d)
{
    return d.slot == SLOT_IDLE ? ID_TAREA_NINGUNA : s.tareas[d.slot].def.id;
}

static void deadline_assert_traza(const uint8_t *esperada, int n)
{
    for (int t = 0; t < n; t++)
    {
        uint8_t id = id_de(sched_tick(&s));
        char msg[32];
        snprintf(msg, sizeof msg, "tick %d", t);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(esperada[t], id, msg);
    }
}

static void deadline_assert_evento(uint32_t tick, uint8_t tipo, uint8_t id, uint16_t aux)
{
    evento_t e;
    TEST_ASSERT_TRUE_MESSAGE(evq_pop(&s.eventos, &e), "faltan eventos");
    TEST_ASSERT_EQUAL_UINT32(tick, e.tick);
    TEST_ASSERT_EQUAL_UINT8(tipo, e.tipo);
    TEST_ASSERT_EQUAL_UINT8(id, e.id_tarea);
    TEST_ASSERT_EQUAL_UINT16(aux, e.aux);
}

static void deadline_assert_sin_mas_eventos(void)
{
    evento_t e;
    TEST_ASSERT_FALSE_MESSAGE(evq_pop(&s.eventos, &e), "sobran eventos");
}

static int deadline_contar_eventos(uint8_t tipo, uint8_t id)
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
    agregar(1, 3, 5, 2);
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
    agregar(1, 3, 2, 2);
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
    agregar(2, 9, 10, 3);
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
    agregar(1, 3, 100, 2);
    sched_iniciar(&s, ALG_EDF, 100);

    TEST_ASSERT_FALSE(sched_tick(&s).led_plazo);
    TEST_ASSERT_FALSE(sched_tick(&s).led_plazo);
    TEST_ASSERT_TRUE(sched_tick(&s).led_plazo);
    for (int t = 3; t < 100; t++)
        TEST_ASSERT_TRUE(sched_tick(&s).led_plazo);
}

static void test_traza_dorada_rms_pierde_el_plazo_de_b_en_el_tick_7(void)
{
    agregar(2, 4, 7, 7);
    agregar(1, 2, 5, 5);
    sched_iniciar(&s, ALG_RMS, 100);

    const uint8_t traza[35] = {
        1,
        1,
        2,
        2,
        2,
        1,
        1,
        2,
        2,
        2,
        1,
        1,
        2,
        N,
        2,
        1,
        1,
        2,
        2,
        2,
        1,
        1,
        2,
        2,
        2,
        1,
        1,
        2,
        2,
        2,
        1,
        1,
        2,
        2,
        N,
    };
    assert_traza(traza, 35);

    int perdidas_b = 0, perdidas_a = 0;
    evento_t e;
    while (evq_pop(&s.eventos, &e))
    {
        if (e.tipo != EV_PLAZO_PERDIDO)
            continue;
        if (e.id_tarea == 2)
        {
            perdidas_b++;
            TEST_ASSERT_EQUAL_UINT32(7, e.tick);
        }
        else
        {
            perdidas_a++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, perdidas_b);
    TEST_ASSERT_EQUAL_INT(0, perdidas_a);
}

#undef s
#undef agregar
#undef id_de
#undef assert_traza
#undef assert_evento
#undef assert_sin_mas_eventos
#undef contar_eventos

/* Task registration validation and idempotency tests. */
#define s registration_state
#define periodica registration_periodica
#define esporadica registration_esporadica
#define assert_rechazo_sin_efectos registration_assert_rechazo_sin_efectos

static sched_t registration_state;

static tarea_def_t registration_periodica(uint8_t id)
{
    tarea_def_t d = {.id = id, .tipo = TAREA_PERIODICA, .costo = 2, .periodo = 10, .plazo = 10};
    return d;
}

static tarea_def_t registration_esporadica(uint8_t id)
{
    tarea_def_t d = periodica(id);
    d.tipo = TAREA_ESPORADICA;
    return d;
}

static void registration_assert_rechazo_sin_efectos(tarea_def_t d)
{
    sched_t antes = s;
    TEST_ASSERT_EQUAL_INT(ACK_INVALIDO, sched_agregar_tarea(&s, &d));
    TEST_ASSERT_EQUAL_MEMORY(&antes, &s, sizeof s);
}

static void test_registra_tarea_valida(void)
{
    tarea_def_t d = periodica(3);
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &d));
    TEST_ASSERT_EQUAL_UINT8(1, s.n_tareas);
    TEST_ASSERT_EQUAL_MEMORY(&d, &s.tareas[0].def, sizeof d);
}

static void test_acepta_plazo_igual_al_periodo_y_menor(void)
{
    tarea_def_t a = periodica(1), b = periodica(2);
    b.plazo = 1;
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &a));
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &b));
}

static void test_acepta_hasta_max_tareas_y_rechaza_la_siguiente_por_espacio(void)
{
    for (uint8_t i = 0; i < MAX_TAREAS; i++)
    {
        tarea_def_t d = periodica(i);
        TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &d));
    }
    tarea_def_t extra = periodica(100);
    sched_t antes = s;
    TEST_ASSERT_EQUAL_INT(ACK_SIN_ESPACIO, sched_agregar_tarea(&s, &extra));
    TEST_ASSERT_EQUAL_MEMORY(&antes, &s, sizeof s);
}

static void test_id_duplicado_es_idempotente_y_conserva_los_parametros(void)
{
    tarea_def_t original = periodica(5);
    tarea_def_t reenvio = periodica(5);
    reenvio.costo = 7;
    sched_agregar_tarea(&s, &original);

    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &reenvio));
    TEST_ASSERT_EQUAL_UINT8(1, s.n_tareas);
    TEST_ASSERT_EQUAL_UINT16(2, s.tareas[0].def.costo);
}

static void test_id_duplicado_responde_ok_aunque_el_nodo_este_lleno(void)
{
    for (uint8_t i = 0; i < MAX_TAREAS; i++)
    {
        tarea_def_t d = periodica(i);
        sched_agregar_tarea(&s, &d);
    }
    tarea_def_t reenvio = periodica(0);
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &reenvio));
}

static void test_rechaza_costo_cero(void)
{
    tarea_def_t d = periodica(1);
    d.costo = 0;
    assert_rechazo_sin_efectos(d);
}

static void test_rechaza_periodo_cero(void)
{
    tarea_def_t d = periodica(1);
    d.periodo = 0;
    d.plazo = 0;
    assert_rechazo_sin_efectos(d);
}

static void test_rechaza_plazo_cero(void)
{
    tarea_def_t d = periodica(1);
    d.plazo = 0;
    assert_rechazo_sin_efectos(d);
}

static void test_rechaza_plazo_mayor_que_el_periodo(void)
{
    tarea_def_t d = periodica(1);
    d.plazo = d.periodo + 1;
    assert_rechazo_sin_efectos(d);
}

static void test_rechaza_tipo_desconocido(void)
{
    tarea_def_t d = periodica(1);
    d.tipo = 2;
    assert_rechazo_sin_efectos(d);
}

static void test_rechaza_id_reservado_para_idle(void)
{
    tarea_def_t d = periodica(ID_TAREA_NINGUNA);
    assert_rechazo_sin_efectos(d);
}

static void test_rechaza_segunda_esporadica(void)
{
    tarea_def_t primera = esporadica(1);
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &primera));
    assert_rechazo_sin_efectos(esporadica(2));
}

static void test_invalida_tiene_prioridad_sobre_sin_espacio(void)
{
    for (uint8_t i = 0; i < MAX_TAREAS; i++)
    {
        tarea_def_t d = periodica(i);
        sched_agregar_tarea(&s, &d);
    }
    tarea_def_t d = periodica(100);
    d.costo = 0;
    assert_rechazo_sin_efectos(d);
}

#undef s
#undef periodica
#undef esporadica
#undef assert_rechazo_sin_efectos

/* RMS dispatch and tick-state behavior tests. */
#define s rms_state
#define agregar rms_agregar
#define id_de rms_id_de
#define assert_traza rms_assert_traza
#define assert_evento rms_assert_evento
#define assert_sin_mas_eventos rms_assert_sin_mas_eventos

static sched_t rms_state;

static void rms_agregar(uint8_t id, uint16_t costo, uint16_t periodo, uint16_t plazo)
{
    tarea_def_t d = {.id = id, .tipo = TAREA_PERIODICA, .costo = costo, .periodo = periodo, .plazo = plazo};
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &d));
}

static uint8_t rms_id_de(decision_t d)
{
    return d.slot == SLOT_IDLE ? ID_TAREA_NINGUNA : s.tareas[d.slot].def.id;
}

static void rms_assert_traza(const uint8_t *esperada, int n)
{
    for (int t = 0; t < n; t++)
    {
        uint8_t id = id_de(sched_tick(&s));
        char msg[32];
        snprintf(msg, sizeof msg, "tick %d", t);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(esperada[t], id, msg);
    }
}

static void rms_assert_evento(uint32_t tick, uint8_t tipo, uint8_t id, uint16_t aux)
{
    evento_t e;
    TEST_ASSERT_TRUE_MESSAGE(evq_pop(&s.eventos, &e), "faltan eventos");
    TEST_ASSERT_EQUAL_UINT32(tick, e.tick);
    TEST_ASSERT_EQUAL_UINT8(tipo, e.tipo);
    TEST_ASSERT_EQUAL_UINT8(id, e.id_tarea);
    TEST_ASSERT_EQUAL_UINT16(aux, e.aux);
}

static void rms_assert_sin_mas_eventos(void)
{
    evento_t e;
    TEST_ASSERT_FALSE_MESSAGE(evq_pop(&s.eventos, &e), "sobran eventos");
}

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
    assert_evento(2, EV_COMPLETADO, 1, 2);
    assert_evento(2, EV_CAMBIO_TAREA, N, 0);
    assert_evento(5, EV_LIBERACION, 1, 0);
    assert_evento(5, EV_CAMBIO_TAREA, 1, 0);
    assert_sin_mas_eventos();
}

static void test_rms_apropia_en_el_limite_de_tick(void)
{
    agregar(1, 4, 10, 10);
    agregar(2, 1, 4, 4);
    sched_iniciar(&s, ALG_RMS, 100);

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

    TEST_ASSERT_EQUAL_UINT8(1, sched_tick(&s).leds_tarea);
    TEST_ASSERT_EQUAL_UINT8(0, sched_tick(&s).leds_tarea);
}

#undef s
#undef agregar
#undef id_de
#undef assert_traza
#undef assert_evento
#undef assert_sin_mas_eventos

/* Event queue behavior tests. */
static evq_t event_queue;

static evento_t event_queue_evento(uint32_t tick)
{
    evento_t e = {.tick = tick, .tipo = EV_LIBERACION, .id_tarea = 1, .aux = 0};
    return e;
}

static void test_cola_vacia_no_entrega_eventos(void)
{
    evento_t e;
    TEST_ASSERT_FALSE(evq_pop(&event_queue, &e));
}

static void test_entrega_en_orden_fifo(void)
{
    evento_t a = event_queue_evento(1), b = event_queue_evento(2), e;
    TEST_ASSERT_TRUE(evq_push(&event_queue, &a));
    TEST_ASSERT_TRUE(evq_push(&event_queue, &b));

    TEST_ASSERT_TRUE(evq_pop(&event_queue, &e));
    TEST_ASSERT_EQUAL_UINT32(1, e.tick);
    TEST_ASSERT_TRUE(evq_pop(&event_queue, &e));
    TEST_ASSERT_EQUAL_UINT32(2, e.tick);
    TEST_ASSERT_FALSE(evq_pop(&event_queue, &e));
}

static void test_conserva_todos_los_campos(void)
{
    evento_t in = {.tick = 0xA1B2C3D4u, .tipo = EV_COMPLETADO, .id_tarea = 7, .aux = 0xBEEF};
    evento_t out;
    evq_push(&event_queue, &in);
    evq_pop(&event_queue, &out);

    TEST_ASSERT_EQUAL_UINT32(in.tick, out.tick);
    TEST_ASSERT_EQUAL_UINT8(in.tipo, out.tipo);
    TEST_ASSERT_EQUAL_UINT8(in.id_tarea, out.id_tarea);
    TEST_ASSERT_EQUAL_UINT16(in.aux, out.aux);
}

static void test_llena_descarta_y_cuenta_sin_perder_los_previos(void)
{
    for (uint32_t i = 0; i < EVQ_CAPACIDAD; i++)
    {
        evento_t e = event_queue_evento(i);
        TEST_ASSERT_TRUE(evq_push(&event_queue, &e));
    }

    evento_t extra = event_queue_evento(999);
    TEST_ASSERT_FALSE(evq_push(&event_queue, &extra));
    TEST_ASSERT_FALSE(evq_push(&event_queue, &extra));
    TEST_ASSERT_EQUAL_UINT32(2, event_queue.descartados);

    evento_t e;
    for (uint32_t i = 0; i < EVQ_CAPACIDAD; i++)
    {
        TEST_ASSERT_TRUE(evq_pop(&event_queue, &e));
        TEST_ASSERT_EQUAL_UINT32(i, e.tick);
    }
    TEST_ASSERT_FALSE(evq_pop(&event_queue, &e));
}

static void test_funciona_tras_muchas_vueltas_del_bufer(void)
{
    evento_t e;
    for (uint32_t i = 0; i < 70000u; i++)
    {
        evento_t in = event_queue_evento(i);
        TEST_ASSERT_TRUE(evq_push(&event_queue, &in));
        TEST_ASSERT_TRUE(evq_pop(&event_queue, &e));
        TEST_ASSERT_EQUAL_UINT32(i, e.tick);
    }
    TEST_ASSERT_EQUAL_UINT32(0, event_queue.descartados);
}

static void test_se_puede_volver_a_llenar_tras_vaciar(void)
{
    evento_t e;
    for (int vuelta = 0; vuelta < 3; vuelta++)
    {
        for (uint32_t i = 0; i < EVQ_CAPACIDAD; i++)
        {
            evento_t in = event_queue_evento(i);
            TEST_ASSERT_TRUE(evq_push(&event_queue, &in));
        }
        for (uint32_t i = 0; i < EVQ_CAPACIDAD; i++)
            TEST_ASSERT_TRUE(evq_pop(&event_queue, &e));
    }
    TEST_ASSERT_EQUAL_UINT32(0, event_queue.descartados);
}

int main(void)
{
    puts("Scheduler");
    RUN_SCHED_TEST(test_alta_en_caliente_libera_en_el_tick_en_que_se_agrega, lifecycle_state);
    RUN_SCHED_TEST(test_alta_en_caliente_no_altera_a_las_demas, lifecycle_state);
    RUN_SCHED_TEST(test_iniciar_repetido_no_reinicia_el_tick_ni_cambia_el_algoritmo, lifecycle_state);
    RUN_SCHED_TEST(test_iniciar_rechaza_periodo_de_reporte_cero, lifecycle_state);
    RUN_SCHED_TEST(test_reporte_tick_cada_report_period_ticks, lifecycle_state);
    RUN_SCHED_TEST(test_detener_deja_idle_y_apaga_los_leds, lifecycle_state);
    RUN_SCHED_TEST(test_detener_no_emite_eventos_y_es_idempotente, lifecycle_state);

    RUN_SCHED_TEST(test_rms_desempata_por_id_menor_sin_importar_el_orden_de_registro, ordering_state);
    RUN_SCHED_TEST(test_edf_desempata_por_id_menor_ante_el_mismo_plazo_absoluto, ordering_state);
    RUN_SCHED_TEST(test_edf_elige_el_plazo_absoluto_mas_cercano_y_no_el_periodo, ordering_state);
    RUN_SCHED_TEST(test_traza_dorada_edf_cumple_todos_los_plazos_en_una_hiperperiodo, ordering_state);
    RUN_SCHED_TEST(test_traza_dorada_edf_completa_todos_los_trabajos_de_b, ordering_state);

    RUN_SCHED_TEST(test_disparo_valido_libera_un_trabajo_con_plazo_t_mas_d, sporadic_state);
    RUN_SCHED_TEST(test_completado_lleva_la_latencia_desde_el_tick_del_disparo, sporadic_state);
    RUN_SCHED_TEST(test_disparo_antes_del_intervalo_minimo_se_ignora_y_se_cuenta, sporadic_state);
    RUN_SCHED_TEST(test_varios_disparos_en_el_mismo_tick_liberan_uno_e_ignoran_el_resto, sporadic_state);
    RUN_SCHED_TEST(test_trabajo_vencido_se_aborta_antes_de_liberar_el_nuevo, sporadic_state);
    RUN_SCHED_TEST(test_disparo_antes_de_iniciar_se_descarta, sporadic_state);
    RUN_SCHED_TEST(test_disparo_sin_tarea_esporadica_no_hace_nada, sporadic_state);
    RUN_SCHED_TEST(test_rms_la_esporadica_compite_con_su_intervalo_minimo_como_periodo, sporadic_state);

    RUN_SCHED_TEST(test_rms_factible_no_pierde_plazos, stress_state);
    RUN_SCHED_TEST(test_edf_factible_no_pierde_plazos, stress_state);
    RUN_SCHED_TEST(test_rms_sobrecargado_mantiene_las_invariantes, stress_state);
    RUN_SCHED_TEST(test_edf_sobrecargado_mantiene_las_invariantes, stress_state);

    RUN_SCHED_TEST(test_aborta_justo_cuando_el_tick_alcanza_el_plazo, deadline_state);
    RUN_SCHED_TEST(test_aborta_antes_de_liberar_el_siguiente_trabajo_de_la_misma_tarea, deadline_state);
    RUN_SCHED_TEST(test_el_trabajo_abortado_no_se_completa_y_las_demas_siguen, deadline_state);
    RUN_SCHED_TEST(test_led_de_plazo_se_enciende_al_perder_y_queda_encendido, deadline_state);
    RUN_SCHED_TEST(test_traza_dorada_rms_pierde_el_plazo_de_b_en_el_tick_7, deadline_state);

    RUN_SCHED_TEST(test_registra_tarea_valida, registration_state);
    RUN_SCHED_TEST(test_acepta_plazo_igual_al_periodo_y_menor, registration_state);
    RUN_SCHED_TEST(test_acepta_hasta_max_tareas_y_rechaza_la_siguiente_por_espacio, registration_state);
    RUN_SCHED_TEST(test_id_duplicado_es_idempotente_y_conserva_los_parametros, registration_state);
    RUN_SCHED_TEST(test_id_duplicado_responde_ok_aunque_el_nodo_este_lleno, registration_state);
    RUN_SCHED_TEST(test_rechaza_costo_cero, registration_state);
    RUN_SCHED_TEST(test_rechaza_periodo_cero, registration_state);
    RUN_SCHED_TEST(test_rechaza_plazo_cero, registration_state);
    RUN_SCHED_TEST(test_rechaza_plazo_mayor_que_el_periodo, registration_state);
    RUN_SCHED_TEST(test_rechaza_tipo_desconocido, registration_state);
    RUN_SCHED_TEST(test_rechaza_id_reservado_para_idle, registration_state);
    RUN_SCHED_TEST(test_rechaza_segunda_esporadica, registration_state);
    RUN_SCHED_TEST(test_invalida_tiene_prioridad_sobre_sin_espacio, registration_state);

    RUN_SCHED_TEST(test_antes_de_iniciar_no_ejecuta_ni_emite, rms_state);
    RUN_SCHED_TEST(test_iniciar_rechaza_algoritmo_desconocido, rms_state);
    RUN_SCHED_TEST(test_una_tarea_alterna_ejecucion_e_idle, rms_state);
    RUN_SCHED_TEST(test_una_tarea_emite_liberacion_cambio_y_completado, rms_state);
    RUN_SCHED_TEST(test_rms_apropia_en_el_limite_de_tick, rms_state);
    RUN_SCHED_TEST(test_rms_tiempo_de_respuesta_incluye_la_apropiacion, rms_state);
    RUN_SCHED_TEST(test_leds_muestran_slot_mas_uno_e_idle_en_cero, rms_state);

    puts("Event queue");
    RUN_EVQ_TEST(test_cola_vacia_no_entrega_eventos, event_queue);
    RUN_EVQ_TEST(test_entrega_en_orden_fifo, event_queue);
    RUN_EVQ_TEST(test_conserva_todos_los_campos, event_queue);
    RUN_EVQ_TEST(test_llena_descarta_y_cuenta_sin_perder_los_previos, event_queue);
    RUN_EVQ_TEST(test_funciona_tras_muchas_vueltas_del_bufer, event_queue);
    RUN_EVQ_TEST(test_se_puede_volver_a_llenar_tras_vaciar, event_queue);

    puts("OK: All scheduler tests passed!");
    return 0;
}
