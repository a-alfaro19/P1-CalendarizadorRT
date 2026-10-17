#ifndef BOARD_PICO_H
#define BOARD_PICO_H

/* Pines de la Raspberry Pi Pico (numeracion GPn). */

#include <Arduino.h>

/* Tarea en ejecucion en binario: bit 0 primero. */
static const uint8_t PINES_LEDS_TAREA[] = {2, 3, 4, 5};
#define PIN_LED_PLAZO 6u
/* Boton a GND; usa el pull-up interno. */
#define PIN_BOTON 15u

/* Tick mas largo admitido: el mismo limite que en el ESP8266, para que las
 * dos placas acepten la misma configuracion. */
#define TICK_MS_MAX 2000u

#endif
