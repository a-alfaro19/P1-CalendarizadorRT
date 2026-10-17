#include <stdio.h>
#include <unity.h>

#include "calendarizador.h"

/*
 * Corre 8 tareas durante 10 000 ticks y revisa en cada tick que el estado y
 * los eventos sean coherentes. Los ids van de 1 a 8 y se registran en orden,
 * asi que el slot de la tarea con id i es i - 1.
 */

#define TICKS 10000

static sched_t s;

void setUp(void)
{
    sched_init(&s);
}

void tearDown(void) {}

typedef struct {
    bool activo[MAX_TAREAS];
    uint32_t liberaciones, completados, perdidas;
    uint16_t peor_respuesta[MAX_TAREAS];
} resumen_t;

static void agregar(uint8_t id, uint8_t tipo, uint16_t costo, uint16_t periodo, uint16_t plazo)
{
    tarea_def_t d = {.id = id, .tipo = tipo, .costo = costo, .periodo = periodo, .plazo = plazo};
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_agregar_tarea(&s, &d));
}

static void fallar(uint32_t t, const char *que)
{
    char msg[64];
    snprintf(msg, sizeof msg, "tick %lu: %s", (unsigned long)t, que);
    TEST_FAIL_MESSAGE(msg);
}

/* Revisa los eventos que emitio el tick t y actualiza el resumen. */
static void revisar_eventos(uint32_t t, decision_t d, resumen_t *r)
{
    evento_t e;
    while (evq_pop(&s.eventos, &e)) {
        uint8_t slot = (uint8_t)(e.id_tarea - 1u);
        switch (e.tipo) {
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

static void revisar_estado(uint32_t t, decision_t d, const resumen_t *r)
{
    for (uint8_t i = 0; i < s.n_tareas; i++) {
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

/* disparar_cada: cada cuantos ticks se pulsa el boton; 0 = nunca. */
static resumen_t correr(uint8_t alg, uint32_t disparar_cada)
{
    resumen_t r = {0};
    TEST_ASSERT_EQUAL_INT(ACK_OK, sched_iniciar(&s, alg, 100));
    for (uint32_t t = 0; t < TICKS; t++) {
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

/* U = 0.65, por debajo de la cota de Liu y Layland para 8 tareas (0.724). */
static void agregar_conjunto_factible(void)
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

/* U > 1 con plazos menores que el periodo: hay perdidas con los dos algoritmos. */
static void agregar_conjunto_sobrecargado(void)
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

static void assert_sin_perdidas_y_respuesta_dentro_del_plazo(const resumen_t *r)
{
    TEST_ASSERT_EQUAL_UINT32(0, r->perdidas);
    TEST_ASSERT_GREATER_THAN_UINT32(0, r->completados);
    /* La esporadica mide desde el tick del disparo, uno antes de liberarse. */
    for (uint8_t i = 0; i < s.n_tareas; i++) {
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rms_factible_no_pierde_plazos);
    RUN_TEST(test_edf_factible_no_pierde_plazos);
    RUN_TEST(test_rms_sobrecargado_mantiene_las_invariantes);
    RUN_TEST(test_edf_sobrecargado_mantiene_las_invariantes);
    return UNITY_END();
}
