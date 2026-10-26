#include <Arduino.h>

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
#elif defined(ARDUINO_ARCH_RP2040)
/* GP0 y GP1 son el UART con el host. */
const uint8_t task_led_pins[] = {2, 3, 4, 5};
const uint8_t deadline_led_pin = 6;
const uint8_t button_pin = 15;
#else
#error "Placa sin pines de LEDs definidos"
#endif

node_driver_t driver;

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
    return micros();
}

uint32_t board_calibrate(void *, uint16_t tick_ms)
{
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

    if (node_driver_init(&driver, &io) != 0)
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
    forward_button_presses();
    if (node_driver_poll(&driver) != 0)
    {
#if !defined(ARDUINO_ARCH_ESP8266)
        Serial.println("Node driver I/O error");
#endif
        while (true)
            delay(1000);
    }
    delayMicroseconds(100);
}
