// node.cpp - Capa de placa del nodo para ESP8266 (NodeMCU) y Raspberry Pi Pico W.
//
// El protocolo viaja por el puerto USB de la placa (objeto `Serial`), por lo que
// NO debe imprimirse nada de depuracion por Serial: contaminaria el canal binario.
//
// Hardware que refleja el nodo:
//   - 4 LEDs (bit0..bit3) con decision.leds_tarea (0 = ocioso, 1..15 = tarea).
//   - 1 LED de plazo perdido (decision.led_plazo).
//   - 1 boton (a GND, pull-up interno) que libera la tarea esporadica.
//   - 1 pin de sincronizacion que conmuta en cada reporte de tick (incluido el
//     tick 0): sirve para medir con analizador logico el desfase entre nodos.

#include <Arduino.h>

#include "node/measure/medicion.h"
#include "node/node_driver.h"

extern "C" {
#include "node/ut/ut.h"
}

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// ---------------------------------------------------------------------------
// Pines
// ---------------------------------------------------------------------------
#if defined(ARDUINO_ARCH_ESP8266)
// NodeMCU: D1, D2, D5, D6 / D7 / D3 (GPIO0) / D0 (GPIO16)
static const uint8_t PIN_TASK_LEDS[4] = {5, 4, 14, 12};
static const uint8_t PIN_MISS_LED = 13;
static const uint8_t PIN_BUTTON = 0;
static const uint8_t PIN_SYNC = 16;
#elif defined(ARDUINO_ARCH_RP2040)
// Pico W: GP2..GP5 / GP6 / GP14 / GP15
static const uint8_t PIN_TASK_LEDS[4] = {2, 3, 4, 5};
static const uint8_t PIN_MISS_LED = 6;
static const uint8_t PIN_BUTTON = 14;
static const uint8_t PIN_SYNC = 15;
#else
// Respaldo para el entorno esp32 del platformio.ini
static const uint8_t PIN_TASK_LEDS[4] = {25, 26, 27, 14};
static const uint8_t PIN_MISS_LED = 13;
static const uint8_t PIN_BUTTON = 32;
static const uint8_t PIN_SYNC = 33;
#endif

static const uint32_t DEBOUNCE_US = 50000u; // antirrebote por software (50 ms)

namespace
{
node_driver_t driver;

// La ISR solo cuenta pulsaciones; el calendarizador las atiende en su tick.
volatile uint8_t button_events = 0;
volatile uint32_t last_press_us = 0;
bool sync_state = false;

void IRAM_ATTR on_button_falling()
{
    uint32_t now = micros();
    if ((uint32_t)(now - last_press_us) < DEBOUNCE_US)
        return;
    last_press_us = now;
    if (button_events < 255)
        button_events++;
}

int read_serial_byte(void *, uint8_t *byte)
{
    if (Serial.available() <= 0)
        return 0;
    int received = Serial.read();
    if (received < 0)
        return -1;
    *byte = static_cast<uint8_t>(received);
    return 1;
}

// Mejor esfuerzo: si no hay host (o el buffer de salida esta lleno) la trama se
// descarta y el nodo sigue ejecutando sus tareas. Nunca bloquea dentro del tick.
int write_serial_bytes(void *, const uint8_t *bytes, size_t length)
{
#if !defined(ARDUINO_ARCH_RP2040)
    if (static_cast<size_t>(Serial.availableForWrite()) < length)
        return 0;
#endif
    Serial.write(bytes, length);
    return 0;
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

void board_apply_decision(void *, const decision_t *decision)
{
    uint8_t value = decision->leds_tarea;
    for (uint8_t bit = 0; bit < 4; bit++)
        digitalWrite(PIN_TASK_LEDS[bit], (value >> bit) & 1u ? HIGH : LOW);
    digitalWrite(PIN_MISS_LED, decision->led_plazo ? HIGH : LOW);

    if (decision->reporte_tick)
    {
        sync_state = !sync_state;
        digitalWrite(PIN_SYNC, sync_state ? HIGH : LOW);
    }
}

// Parpadeo del LED de plazo perdido para indicar un fallo irrecuperable de arranque.
[[noreturn]] void fatal_blink()
{
    for (;;)
    {
        digitalWrite(PIN_MISS_LED, HIGH);
        delay(150);
        digitalWrite(PIN_MISS_LED, LOW);
        delay(150);
    }
}

void drain_button_events()
{
    noInterrupts();
    uint8_t pending = button_events;
    button_events = 0;
    interrupts();
    while (pending--)
        sched_registrar_disparo(&driver.scheduler);
}
} // namespace

void setup()
{
    for (uint8_t bit = 0; bit < 4; bit++)
    {
        pinMode(PIN_TASK_LEDS[bit], OUTPUT);
        digitalWrite(PIN_TASK_LEDS[bit], LOW);
    }
    pinMode(PIN_MISS_LED, OUTPUT);
    digitalWrite(PIN_MISS_LED, LOW);
    pinMode(PIN_SYNC, OUTPUT);
    digitalWrite(PIN_SYNC, LOW);

    pinMode(PIN_BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), on_button_falling, FALLING);

    Serial.begin(115200);
#if defined(ARDUINO_ARCH_RP2040)
    // USB CDC: no esperar a que el host asevere DTR para poder transmitir.
    Serial.ignoreFlowControl(true);
#endif

    node_driver_io_t io = {};
    io.read_byte = read_serial_byte;
    io.write_bytes = write_serial_bytes;
    io.now_us = board_now_us;
    io.calibrate = board_calibrate;
    io.run_unit = board_run_unit;
    io.apply_decision = board_apply_decision;

    if (node_driver_init(&driver, &io) != 0)
        fatal_blink();
}

void loop()
{
    drain_button_events();
    if (node_driver_poll(&driver) != 0)
        fatal_blink();
}