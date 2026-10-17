/*
 * Puerto del ESP8266 (NodeMCU v3).
 *
 * El core de Arduino del ESP8266 no usa un RTOS: loop() corre en un contexto
 * cooperativo del SDK. Para esperar el tick, el lazo se suspende con
 * esp_suspend() y la ISR de timer1 lo reanuda con esp_schedule(), el mismo
 * mecanismo con el que el core implementa delay(), pero despertado por el
 * temporizador de hardware. El orden de ejecucion lo decide el calendarizador.
 *
 * Toda ISR va en IRAM (IRAM_ATTR): el codigo en flash no se puede ejecutar
 * desde una interrupcion mientras se lee la flash.
 */

#if defined(ARDUINO_ARCH_ESP8266)

#include <Arduino.h>

#include "board_esp8266.h"
#include "lazo.h"
#include "port.h"

static volatile uint32_t ticks_generados;
static volatile uint32_t pulsaciones;
static lazo_rebote_t rebote;

static void IRAM_ATTR isr_tick(void)
{
    ticks_generados++;
    esp_schedule();
}

static void IRAM_ATTR isr_boton(void)
{
    if (lazo_rebote_aceptar(&rebote, millis()))
        pulsaciones++;
}

void port_iniciar(void)
{
    for (uint8_t pin : PINES_LEDS_TAREA)
        pinMode(pin, OUTPUT);
    pinMode(PIN_LED_PLAZO, OUTPUT);
    port_mostrar(0, false);

    pinMode(PIN_BOTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_BOTON), isr_boton, FALLING);

    timer1_attachInterrupt(isr_tick);
}

bool port_configurar_tick(uint16_t tick_ms)
{
    if (tick_ms == 0 || tick_ms > TICK_MS_MAX)
        return false;

    timer1_disable();
    timer1_enable(TIM_DIV256, TIM_EDGE, TIM_LOOP);
    timer1_write((uint32_t)tick_ms * TIMER1_CUENTAS_POR_10_MS / 10u);
    return true;
}

uint32_t port_ticks_generados(void)
{
    return ticks_generados;
}

uint32_t port_pulsaciones(void)
{
    return pulsaciones;
}

void port_esperar_tick(uint32_t procesados)
{
    /* Si la ISR llega entre la comparacion y esp_suspend(), su esp_schedule()
     * ya dejo el aviso en la cola del SDK y esp_suspend() vuelve enseguida:
     * no se pierde el tick. */
    if (ticks_generados == procesados)
        esp_suspend();
}

void port_mostrar(uint8_t leds_tarea, bool led_plazo)
{
    for (uint8_t i = 0; i < sizeof PINES_LEDS_TAREA; i++)
        digitalWrite(PINES_LEDS_TAREA[i], (leds_tarea >> i) & 1u ? HIGH : LOW);
    digitalWrite(PIN_LED_PLAZO, led_plazo ? HIGH : LOW);
}

#endif
