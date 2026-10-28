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
//
// El tick lo genera un temporizador de hardware (timer1 en el ESP8266, un
// repeating timer en la Pico). Su ISR solo cuenta ticks; el driver toma el
// tiempo de ese contador y loop() duerme entre interrupciones, sin busy waiting.

#include <Arduino.h>

#if defined(ARDUINO_ARCH_RP2040)
#include <hardware/sync.h>
#include <pico/time.h>
#endif

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

#if defined(ARDUINO_ARCH_ESP8266)
// timer1 cuenta a 80 MHz / 256 = 312.5 kHz: 3125 cuentas son 10 ms.
static const uint32_t TIMER1_COUNTS_PER_10_MS = 3125u;
#endif

namespace
{
node_driver_t driver;

// La ISR solo cuenta pulsaciones; el calendarizador las atiende en su tick.
volatile uint8_t button_events = 0;
volatile uint32_t last_press_us = 0;
bool sync_state = false;

// Ticks generados por la ISR del temporizador. board_now_us los convierte a
// microsegundos, asi el driver corre un tick del calendarizador por interrupcion.
volatile uint32_t generated_ticks = 0;
uint32_t tick_period_us = 0;

#if defined(ARDUINO_ARCH_ESP8266)
void IRAM_ATTR on_tick()
{
    generated_ticks++;
    esp_schedule(); // reanuda loop() si esta en esp_suspend()
}
#elif defined(ARDUINO_ARCH_RP2040)
repeating_timer_t tick_timer;
bool tick_timer_active = false;

bool on_tick(repeating_timer_t *)
{
    generated_ticks++;
    return true;
}
#endif

// Arma el temporizador con un tick de tick_ms y reinicia el contador. Solo
// ocurre antes de 0x03 (el driver rechaza 0x00 ya iniciado), asi el reloj del
// driver no cambia de escala con la ejecucion en marcha. Devuelve false, sin
// tocar el temporizador, si la duracion no se admite.
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
    timer1_write((uint32_t)tick_ms * TIMER1_COUNTS_PER_10_MS / 10u);
    return true;
#elif defined(ARDUINO_ARCH_RP2040)
    if (tick_timer_active)
        cancel_repeating_timer(&tick_timer);
    generated_ticks = 0;
    tick_period_us = (uint32_t)tick_ms * 1000u;
    // Retardo negativo: el periodo se cuenta desde el inicio del callback
    // anterior, asi el tick no acumula la duracion de la ISR.
    tick_timer_active = add_repeating_timer_ms(-(int32_t)tick_ms, on_tick, NULL, &tick_timer);
    return tick_timer_active;
#else
#error "Placa sin temporizador de tick"
#endif
}

// Duerme hasta la siguiente interrupcion si desde seen_ticks no llego ningun
// tick y no hay bytes del host por leer. Puede volver por otra interrupcion:
// loop() vuelve a revisar.
void wait_for_interrupt(uint32_t seen_ticks)
{
    if (Serial.available() > 0)
        return;
#if defined(ARDUINO_ARCH_ESP8266)
    // Si la ISR llega entre la comparacion y esp_suspend(), su esp_schedule()
    // ya dejo el aviso y esp_suspend() vuelve enseguida. El UART no reanuda
    // loop(): los bytes que lleguen mientras duerme esperan en el buffer de
    // recepcion hasta el siguiente tick.
    if (generated_ticks == seen_ticks)
        esp_suspend();
#elif defined(ARDUINO_ARCH_RP2040)
    // Con las interrupciones deshabilitadas, una que llegue entre la
    // comparacion y __wfi() queda pendiente y despierta al nucleo. La del USB
    // tambien, asi que un byte del host no espera al siguiente tick.
    uint32_t state = save_and_disable_interrupts();
    if (generated_ticks == seen_ticks)
        __wfi();
    restore_interrupts(state);
#endif
}

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

    // Hasta que llegue 0x00 el tick por defecto solo despierta el lazo.
    if (node_driver_init(&driver, &io) != 0 || !configure_tick(TICK_MS_DEFECTO))
        fatal_blink();
}

void loop()
{
    uint32_t seen_ticks = generated_ticks;
    drain_button_events();
    if (node_driver_poll(&driver) != 0)
        fatal_blink();
    wait_for_interrupt(seen_ticks);
}