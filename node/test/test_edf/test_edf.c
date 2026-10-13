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

/* Cuenta los eventos de un tipo para una tarea y vacia la cola. */
static int contar_eventos(uint8_t tipo, uint8_t id)
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
    /* Mismo plazo absoluto (4) con periodos distintos: RMS elegiria a la 4. */
    agregar(4, 1, 4, 4);
    agregar(2, 1, 8, 4);
    sched_iniciar(&s, ALG_EDF, 100);

    const uint8_t traza[] = {2, 4, N, N};
    assert_traza(traza, 4);
}

static void test_edf_elige_el_plazo_absoluto_mas_cercano_y_no_el_periodo(void)
{
    /* La 1 tiene periodo menor pero plazo mas lejano que la 2. */
    agregar(1, 1, 5, 5);
    agregar(2, 1, 10, 2);
    sched_iniciar(&s, ALG_EDF, 100);

    const uint8_t traza[] = {2, 1, N, N, N};
    assert_traza(traza, 5);
}

/*
 * Traza dorada: A = (C 2, T 5), B = (C 4, T 7), U = 34/35. Es factible con
 * EDF pero no con RMS (B pierde su plazo en t = 7). La secuencia esperada se
 * obtuvo con un simulador de referencia independiente y se reviso a mano.
 * En t = 30 los dos trabajos vencen en 35 y gana A por id menor.
 */
static void test_traza_dorada_edf_cumple_todos_los_plazos_en_una_hiperperiodo(void)
{
    /* B se registra primero para que el desempate dependa del id y no del slot. */
    agregar(2, 4, 7, 7);
    agregar(1, 2, 5, 5);
    sched_iniciar(&s, ALG_EDF, 100);

    const uint8_t traza[35] = {
        1, 1, 2, 2, 2, 2, 1, 1, 2, 2, 2, 2, 1, 1, 2, 1, 1, 2,
        2, 2, 1, 1, 2, 2, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, N,
    };
    assert_traza(traza, 35);

    TEST_ASSERT_EQUAL_INT(7, contar_eventos(EV_COMPLETADO, 1));
}

static void test_traza_dorada_edf_completa_todos_los_trabajos_de_b(void)
{
    /* B se registra primero para que el desempate dependa del id y no del slot. */
    agregar(2, 4, 7, 7);
    agregar(1, 2, 5, 5);
    sched_iniciar(&s, ALG_EDF, 100);
    for (int t = 0; t < 35; t++)
        sched_tick(&s);

    TEST_ASSERT_EQUAL_INT(5, contar_eventos(EV_COMPLETADO, 2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rms_desempata_por_id_menor_sin_importar_el_orden_de_registro);
    RUN_TEST(test_edf_desempata_por_id_menor_ante_el_mismo_plazo_absoluto);
    RUN_TEST(test_edf_elige_el_plazo_absoluto_mas_cercano_y_no_el_periodo);
    RUN_TEST(test_traza_dorada_edf_cumple_todos_los_plazos_en_una_hiperperiodo);
    RUN_TEST(test_traza_dorada_edf_completa_todos_los_trabajos_de_b);
    return UNITY_END();
}
