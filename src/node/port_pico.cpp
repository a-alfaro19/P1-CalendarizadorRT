/*
 * Puerto de la Raspberry Pi Pico (RP2040).
 *
 * El tick lo genera un repeating timer del Pico SDK, que corre sobre el
 * temporizador de hardware. Para esperarlo, el lazo duerme el nucleo con
 * __wfi() hasta la siguiente interrupcion.
 */

#if defined(ARDUINO_ARCH_RP2040)

#include <Arduino.h>
#include <hardware/sync.h>
#include <pico/time.h>

#include "board_pico.h"
#include "lazo.h"
#include "port.h"

static volatile uint32_t ticks_generados;
static volatile uint32_t pulsaciones;
static lazo_rebote_t rebote;
static repeating_timer_t temporizador;
static bool temporizador_activo;

static bool isr_tick(repeating_timer_t *t)
{
    (void)t;
    ticks_generados++;
    return true;    /* sigue repitiendo */
}

static void isr_boton(void)
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
}

bool port_configurar_tick(uint16_t tick_ms)
{
    if (tick_ms == 0 || tick_ms > TICK_MS_MAX)
        return false;

    if (temporizador_activo)
        cancel_repeating_timer(&temporizador);
    /* Retardo negativo: el periodo se cuenta desde el inicio del callback
     * anterior, asi el tick no acumula la duracion de la ISR. */
    temporizador_activo = add_repeating_timer_ms(-(int32_t)tick_ms, isr_tick, NULL, &temporizador);
    return temporizador_activo;
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
    /* Con las interrupciones deshabilitadas, una que llegue entre la
     * comparacion y __wfi() queda pendiente y despierta al nucleo: no se
     * pierde el tick. La ISR corre al rehabilitarlas. */
    uint32_t estado = save_and_disable_interrupts();
    if (ticks_generados == procesados)
        __wfi();
    restore_interrupts(estado);
}

void port_mostrar(uint8_t leds_tarea, bool led_plazo)
{
    for (uint8_t i = 0; i < sizeof PINES_LEDS_TAREA; i++)
        digitalWrite(PINES_LEDS_TAREA[i], (leds_tarea >> i) & 1u ? HIGH : LOW);
    digitalWrite(PIN_LED_PLAZO, led_plazo ? HIGH : LOW);
}

#endif
