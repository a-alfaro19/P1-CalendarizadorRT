#include <unity.h>

#include "calendarizador.h"

static sched_t s;

void setUp(void)
{
    sched_init(&s);
}

void tearDown(void) {}

static tarea_def_t periodica(uint8_t id)
{
    tarea_def_t d = {.id = id, .tipo = TAREA_PERIODICA, .costo = 2, .periodo = 10, .plazo = 10};
    return d;
}

static tarea_def_t esporadica(uint8_t id)
{
    tarea_def_t d = periodica(id);
    d.tipo = TAREA_ESPORADICA;
    return d;
}

/* Comprueba que un despliegue invalido no deja rastro en el calendarizador. */
static void assert_rechazo_sin_efectos(tarea_def_t d)
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
    for (uint8_t i = 0; i < MAX_TAREAS; i++) {
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
    for (uint8_t i = 0; i < MAX_TAREAS; i++) {
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
    for (uint8_t i = 0; i < MAX_TAREAS; i++) {
        tarea_def_t d = periodica(i);
        sched_agregar_tarea(&s, &d);
    }
    tarea_def_t d = periodica(100);
    d.costo = 0;
    assert_rechazo_sin_efectos(d);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_registra_tarea_valida);
    RUN_TEST(test_acepta_plazo_igual_al_periodo_y_menor);
    RUN_TEST(test_acepta_hasta_max_tareas_y_rechaza_la_siguiente_por_espacio);
    RUN_TEST(test_id_duplicado_es_idempotente_y_conserva_los_parametros);
    RUN_TEST(test_id_duplicado_responde_ok_aunque_el_nodo_este_lleno);
    RUN_TEST(test_rechaza_costo_cero);
    RUN_TEST(test_rechaza_periodo_cero);
    RUN_TEST(test_rechaza_plazo_cero);
    RUN_TEST(test_rechaza_plazo_mayor_que_el_periodo);
    RUN_TEST(test_rechaza_tipo_desconocido);
    RUN_TEST(test_rechaza_id_reservado_para_idle);
    RUN_TEST(test_rechaza_segunda_esporadica);
    RUN_TEST(test_invalida_tiene_prioridad_sobre_sin_espacio);
    return UNITY_END();
}
