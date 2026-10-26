#include <Arduino.h>

#include "node/measure/medicion.h"
#include "node/node_driver.h"

#if !defined(LED_BUILTIN) && defined(ARDUINO_ARCH_ESP32)
#define LED_BUILTIN 2
#endif

extern "C" {
#include "node/ut/ut.h"
}

namespace
{
node_driver_t driver;

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

void board_apply_decision(void *, const decision_t *decision)
{
    digitalWrite(LED_BUILTIN, decision->slot == SLOT_IDLE ? LOW : HIGH);
}
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

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
