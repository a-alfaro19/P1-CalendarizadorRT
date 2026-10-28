#include <Arduino.h>

#if defined(ARDUINO_ARCH_RP2040)
#include <hardware/sync.h>
#include <pico/time.h>
#endif

#include "node/button/rebote.h"
#include "node/measure/medicion.h"
#include "node/node_driver.h"

extern "C" {
#include "node/ut/ut.h"
}

/* Solo el ESP8266 necesita las ISR en IRAM. */
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

namespace
{
/* Tarea en ejecucion en binario (bit 0 primero, 0 = idle), LED de plazo y
 * boton de la esporadica (a GND, con pull-up). */
#if defined(ARDUINO_ARCH_ESP8266)
/* D3, D4 y D8 deciden el modo de arranque; RX/TX son el serial con el host.
 * D3 ya tiene el boton FLASH de la placa, con pull-up. */
const uint8_t task_led_pins[] = {D1, D2, D5, D6};
const uint8_t deadline_led_pin = D7;
const uint8_t button_pin = D3;
/* timer1 cuenta a 80 MHz / 256 = 312.5 kHz: 3125 cuentas son 10 ms. */
const uint32_t timer1_counts_per_10_ms = 3125u;
#elif defined(ARDUINO_ARCH_RP2040)
/* GP0 y GP1 son el UART con el host. */
const uint8_t task_led_pins[] = {2, 3, 4, 5};
const uint8_t deadline_led_pin = 6;
const uint8_t button_pin = 15;
#else
#error "Placa sin pines de LEDs definidos"
#endif

node_driver_t driver;

/* La ISR del temporizador solo cuenta ticks (y en el ESP8266 reanuda loop()).
 * El driver toma el tiempo de ese contador en board_now_us, asi que ejecuta un
 * tick del calendarizador por cada interrupcion, sin sondear micros(). */
volatile uint32_t generated_ticks;
uint32_t tick_period_us;

#if defined(ARDUINO_ARCH_ESP8266)
void IRAM_ATTR on_tick()
{
    generated_ticks++;
    esp_schedule();
}
#elif defined(ARDUINO_ARCH_RP2040)
repeating_timer_t tick_timer;
bool tick_timer_active;

bool on_tick(repeating_timer_t *)
{
    generated_ticks++;
    return true;
}
#endif

/* Arma el temporizador con un tick de tick_ms y reinicia el contador. Solo
 * ocurre antes de 0x03 (el driver rechaza 0x00 ya iniciado), asi que el reloj
 * del driver no cambia de escala con la ejecucion en marcha. Devuelve false,
 * sin tocar el temporizador, si la duracion no se admite. */
bool configure_tick(uint16_t tick_ms)
{
    if (tick_ms == 0 || tick_ms > TICK_MS_MAX)
        return false;

#if defined(ARDUINO_ARCH_ESP8266)
    timer1_disable();
    generated_ticks = 0;
    tick_period_us = (uint32_t)tick_ms * 1000u;
    timer1_attachInterrupt(on_tick);
    timer1_enable(TIM_DIV256, TIM_EDGE, TIM_LOOP);
    timer1_write((uint32_t)tick_ms * timer1_counts_per_10_ms / 10u);
    return true;
#elif defined(ARDUINO_ARCH_RP2040)
    if (tick_timer_active)
        cancel_repeating_timer(&tick_timer);
    generated_ticks = 0;
    tick_period_us = (uint32_t)tick_ms * 1000u;
    /* Retardo negativo: el periodo se cuenta desde el inicio del callback
     * anterior, asi el tick no acumula la duracion de la ISR. */
    tick_timer_active = add_repeating_timer_ms(-(int32_t)tick_ms, on_tick, NULL, &tick_timer);
    return tick_timer_active;
#endif
}

/* Duerme hasta la siguiente interrupcion si desde seen_ticks no llego ningun
 * tick y no hay bytes del host por leer. Puede volver por otra interrupcion:
 * loop() vuelve a revisar. */
void wait_for_interrupt(uint32_t seen_ticks)
{
#if defined(ARDUINO_ARCH_ESP8266)
    /* Si la ISR llega entre la comparacion y esp_suspend(), su esp_schedule()
     * ya dejo el aviso y esp_suspend() vuelve enseguida. El UART no reanuda
     * loop(): los bytes que lleguen mientras duerme esperan en el bufer de
     * recepcion hasta el siguiente tick. */
    if (generated_ticks == seen_ticks && Serial.available() <= 0)
        esp_suspend();
#elif defined(ARDUINO_ARCH_RP2040)
    if (Serial1.available() > 0)
        return;
    /* Con las interrupciones deshabilitadas, una que llegue entre la
     * comparacion y __wfi() queda pendiente y despierta al nucleo. El byte
     * del host que llegue despues de available() tambien. */
    uint32_t state = save_and_disable_interrupts();
    if (generated_ticks == seen_ticks)
        __wfi();
    restore_interrupts(state);
#endif
}

/* La ISR solo cuenta pulsaciones; loop() las pasa al calendarizador. Asi el
 * boton y el mensaje 0x06 registran disparos desde un solo contexto, como
 * pide sched_registrar_disparo. */
volatile uint32_t button_presses;
uint32_t forwarded_presses;
rebote_t button_debounce;

void IRAM_ATTR on_button_press()
{
    if (rebote_aceptar(&button_debounce, millis()))
        button_presses++;
}

void forward_button_presses()
{
    uint32_t presses = button_presses;
    for (; forwarded_presses != presses; forwarded_presses++)
        sched_registrar_disparo(&driver.scheduler);
}

int read_serial_byte(void *, uint8_t *byte)
{
#if defined(ARDUINO_ARCH_ESP8266)
    if (Serial.available() <= 0)
#else
    if (Serial1.available() <= 0)
#endif
        return 0;

#if defined(ARDUINO_ARCH_ESP8266)
    int received = Serial.read();
#else
    int received = Serial1.read();
#endif
    if (received < 0)
        return -1;

    *byte = static_cast<uint8_t>(received);
    return 1;
}

int write_serial_bytes(void *, const uint8_t *bytes, size_t length)
{
#if defined(ARDUINO_ARCH_ESP8266)
    return Serial.write(bytes, length) == length ? 0 : -1;
#else
    return Serial1.write(bytes, length) == length ? 0 : -1;
#endif
}

uint32_t board_now_us(void *)
{
    return generated_ticks * tick_period_us;
}

uint32_t board_calibrate(void *, uint16_t tick_ms)
{
    if (!configure_tick(tick_ms))
        return 0;
    return calibrar_ut_por_tick(tick_ms);
}

void board_run_unit(void *)
{
    unidad_trabajo();
}

void show_leds(uint8_t task_leds, bool deadline_missed)
{
    for (size_t i = 0; i < sizeof task_led_pins; i++)
        digitalWrite(task_led_pins[i], (task_leds >> i) & 1u ? HIGH : LOW);
    digitalWrite(deadline_led_pin, deadline_missed ? HIGH : LOW);
}

void board_apply_decision(void *, const decision_t *decision)
{
    show_leds(decision->leds_tarea, decision->led_plazo);
}
}

void setup()
{
    Serial.begin(115200);
    for (uint8_t pin : task_led_pins)
        pinMode(pin, OUTPUT);
    pinMode(deadline_led_pin, OUTPUT);
    show_leds(0, false);
    pinMode(button_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(button_pin), on_button_press, FALLING);

#if defined(ARDUINO_ARCH_ESP32)
    Serial1.begin(115200, SERIAL_8N1, 16, 17);
#elif defined(ARDUINO_ARCH_ESP8266)
    Serial.begin(115200);
#elif defined(ARDUINO_ARCH_RP2040)
    Serial1.setRX(1);
    Serial1.setTX(0);
    Serial1.begin(115200);
#else
    Serial1.begin(115200);
#endif

    node_driver_io_t io = {};
    io.read_byte = read_serial_byte;
    io.write_bytes = write_serial_bytes;
    io.now_us = board_now_us;
    io.calibrate = board_calibrate;
    io.run_unit = board_run_unit;
    io.apply_decision = board_apply_decision;

    if (node_driver_init(&driver, &io) != 0 || !configure_tick(TICK_MS_DEFECTO))
    {
#if !defined(ARDUINO_ARCH_ESP8266)
        Serial.println("Node driver initialization failed");
#endif
        while (true)
            delay(1000);
    }
}

void loop()
{
    uint32_t seen_ticks = generated_ticks;
    forward_button_presses();
    if (node_driver_poll(&driver) != 0)
    {
#if !defined(ARDUINO_ARCH_ESP8266)
        Serial.println("Node driver I/O error");
#endif
        while (true)
            delay(1000);
    }
    wait_for_interrupt(seen_ticks);
}
