#include <unity.h>

#include "evq.h"

static evq_t q;

void setUp(void)
{
    evq_init(&q);
}

void tearDown(void) {}

static evento_t evento(uint32_t tick)
{
    evento_t e = {.tick = tick, .tipo = EV_LIBERACION, .id_tarea = 1, .aux = 0};
    return e;
}

static void test_cola_vacia_no_entrega_eventos(void)
{
    evento_t e;
    TEST_ASSERT_FALSE(evq_pop(&q, &e));
}

static void test_entrega_en_orden_fifo(void)
{
    evento_t a = evento(1), b = evento(2), e;
    TEST_ASSERT_TRUE(evq_push(&q, &a));
    TEST_ASSERT_TRUE(evq_push(&q, &b));

    TEST_ASSERT_TRUE(evq_pop(&q, &e));
    TEST_ASSERT_EQUAL_UINT32(1, e.tick);
    TEST_ASSERT_TRUE(evq_pop(&q, &e));
    TEST_ASSERT_EQUAL_UINT32(2, e.tick);
    TEST_ASSERT_FALSE(evq_pop(&q, &e));
}

static void test_conserva_todos_los_campos(void)
{
    evento_t in = {.tick = 0xA1B2C3D4u, .tipo = EV_COMPLETADO, .id_tarea = 7, .aux = 0xBEEF};
    evento_t out;
    evq_push(&q, &in);
    evq_pop(&q, &out);

    TEST_ASSERT_EQUAL_UINT32(in.tick, out.tick);
    TEST_ASSERT_EQUAL_UINT8(in.tipo, out.tipo);
    TEST_ASSERT_EQUAL_UINT8(in.id_tarea, out.id_tarea);
    TEST_ASSERT_EQUAL_UINT16(in.aux, out.aux);
}

static void test_llena_descarta_y_cuenta_sin_perder_los_previos(void)
{
    for (uint32_t i = 0; i < EVQ_CAPACIDAD; i++) {
        evento_t e = evento(i);
        TEST_ASSERT_TRUE(evq_push(&q, &e));
    }

    evento_t extra = evento(999);
    TEST_ASSERT_FALSE(evq_push(&q, &extra));
    TEST_ASSERT_FALSE(evq_push(&q, &extra));
    TEST_ASSERT_EQUAL_UINT32(2, q.descartados);

    evento_t e;
    for (uint32_t i = 0; i < EVQ_CAPACIDAD; i++) {
        TEST_ASSERT_TRUE(evq_pop(&q, &e));
        TEST_ASSERT_EQUAL_UINT32(i, e.tick);
    }
    TEST_ASSERT_FALSE(evq_pop(&q, &e));
}

static void test_funciona_tras_muchas_vueltas_del_bufer(void)
{
    /* Mas vueltas que el rango de los indices de 16 bits. */
    evento_t e;
    for (uint32_t i = 0; i < 70000u; i++) {
        evento_t in = evento(i);
        TEST_ASSERT_TRUE(evq_push(&q, &in));
        TEST_ASSERT_TRUE(evq_pop(&q, &e));
        TEST_ASSERT_EQUAL_UINT32(i, e.tick);
    }
    TEST_ASSERT_EQUAL_UINT32(0, q.descartados);
}

static void test_se_puede_volver_a_llenar_tras_vaciar(void)
{
    evento_t e;
    for (int vuelta = 0; vuelta < 3; vuelta++) {
        for (uint32_t i = 0; i < EVQ_CAPACIDAD; i++) {
            evento_t in = evento(i);
            TEST_ASSERT_TRUE(evq_push(&q, &in));
        }
        for (uint32_t i = 0; i < EVQ_CAPACIDAD; i++)
            TEST_ASSERT_TRUE(evq_pop(&q, &e));
    }
    TEST_ASSERT_EQUAL_UINT32(0, q.descartados);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cola_vacia_no_entrega_eventos);
    RUN_TEST(test_entrega_en_orden_fifo);
    RUN_TEST(test_conserva_todos_los_campos);
    RUN_TEST(test_llena_descarta_y_cuenta_sin_perder_los_previos);
    RUN_TEST(test_funciona_tras_muchas_vueltas_del_bufer);
    RUN_TEST(test_se_puede_volver_a_llenar_tras_vaciar);
    return UNITY_END();
}
