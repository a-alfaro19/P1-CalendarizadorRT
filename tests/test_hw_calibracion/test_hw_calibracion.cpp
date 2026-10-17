/*
 * Prueba en la placa: mide s_n con unidad_trabajo() real y lo reporta.
 * Se corre con la placa conectada:  pio test -e esp32   o   pio test -e pico
 */

#include <Arduino.h>
#include <stdio.h>
#include <unity.h>

#include "calibracion.h"
#include "medicion.h"

/* Duraciones de tick para las que se reporta s_n, en ms. */
static const uint16_t TICKS_MS[] = {1, 5, 10, 20, 50, 100};

/* Tick por defecto del proyecto; con el se revisa la estabilidad. */
#define TICK_REFERENCIA_MS 10u
#define REPETICIONES 10u
#define DISPERSION_MAX_PCT 2u

/* Espera antes de empezar, para que el monitor serial alcance a conectarse
 * (el Pico reinicia su USB al cargar el programa). */
#define ESPERA_INICIAL_MS 2000u

#if defined(ARDUINO_ARCH_ESP32)
#define NOMBRE_PLACA "ESP32"
#elif defined(ARDUINO_ARCH_RP2040)
#define NOMBRE_PLACA "RP2040 (Pico)"
#else
#define NOMBRE_PLACA "desconocida"
#endif

void setUp(void) {}

void tearDown(void) {}

static void test_informa_la_placa(void)
{
    char linea[96];
    snprintf(linea, sizeof linea, "placa: %s, CPU nominal: %lu MHz, margen: %u %%, ventana: %u ms",
             NOMBRE_PLACA, (unsigned long)(F_CPU / 1000000UL), CAL_MARGEN_PCT, CAL_VENTANA_MS);
    TEST_MESSAGE(linea);
}

static void test_s_n_es_estable_entre_mediciones(void)
{
    uint32_t minimo = UINT32_MAX, maximo = 0;
    char linea[96];

    for (uint32_t i = 0; i < REPETICIONES; i++) {
        uint32_t s_n = calibrar_ut_por_tick(TICK_REFERENCIA_MS);
        snprintf(linea, sizeof linea, "medicion %lu: s_n = %lu (tick %u ms)",
                 (unsigned long)(i + 1), (unsigned long)s_n, TICK_REFERENCIA_MS);
        TEST_MESSAGE(linea);
        if (s_n < minimo)
            minimo = s_n;
        if (s_n > maximo)
            maximo = s_n;
    }

    TEST_ASSERT_GREATER_THAN_UINT32(0, minimo);
    uint32_t dispersion_x100 = (maximo - minimo) * 10000u / minimo;
    snprintf(linea, sizeof linea, "dispersion: %lu.%02lu %% (min %lu, max %lu)",
             (unsigned long)(dispersion_x100 / 100), (unsigned long)(dispersion_x100 % 100),
             (unsigned long)minimo, (unsigned long)maximo);
    TEST_MESSAGE(linea);
    TEST_ASSERT_TRUE_MESSAGE((maximo - minimo) * 100u <= DISPERSION_MAX_PCT * minimo,
                             "la dispersion supera el 2 %");
}

static void test_s_n_por_duracion_de_tick(void)
{
    char linea[96];
    for (size_t i = 0; i < sizeof TICKS_MS / sizeof TICKS_MS[0]; i++) {
        uint32_t s_n = calibrar_ut_por_tick(TICKS_MS[i]);
        snprintf(linea, sizeof linea, "RESULTADO %s tick_ms=%u s_n=%lu",
                 NOMBRE_PLACA, TICKS_MS[i], (unsigned long)s_n);
        TEST_MESSAGE(linea);
        TEST_ASSERT_GREATER_THAN_UINT32(0, s_n);
    }
}

static void test_tick_cero_devuelve_cero(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, calibrar_ut_por_tick(0));
}

void setup()
{
    delay(ESPERA_INICIAL_MS);
    UNITY_BEGIN();
    RUN_TEST(test_informa_la_placa);
    RUN_TEST(test_s_n_es_estable_entre_mediciones);
    RUN_TEST(test_s_n_por_duracion_de_tick);
    RUN_TEST(test_tick_cero_devuelve_cero);
    UNITY_END();
}

void loop() {}
