#ifndef BOARD_ESP8266_H
#define BOARD_ESP8266_H

/*
 * Pines del NodeMCU v3 (ESP8266MOD). RX/TX quedan para el serial con el host.
 * D3 (GPIO0), D4 (GPIO2) y D8 (GPIO15) deciden el modo de arranque: los LEDs
 * no van ahi. D3 ya tiene el boton FLASH de la placa, con pull-up.
 */

#include <Arduino.h>

/* Tarea en ejecucion en binario: bit 0 primero. */
static const uint8_t PINES_LEDS_TAREA[] = {D1, D2, D5, D6};
#define PIN_LED_PLAZO D7
#define PIN_BOTON D3

/* timer1 cuenta a 80 MHz / 256 = 312.5 kHz: 3125 cuentas son 10 ms. */
#define TIMER1_CUENTAS_POR_10_MS 3125u

/* Tick mas largo admitido. Un tramo ocupa casi todo el tick sin ceder el
 * control, y el watchdog del ESP8266 reinicia la placa a los ~3 s. */
#define TICK_MS_MAX 2000u

#endif
