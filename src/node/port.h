#ifndef PORT_H
#define PORT_H

/*
 * Capa de hardware de cada placa: temporizador del tick, boton, LEDs y la
 * espera sin busy waiting. La implementa port_esp8266.cpp o port_pico.cpp,
 * segun la placa. El lazo de node.cpp solo usa esta interfaz.
 *
 * Las ISR solo incrementan contadores (ticks generados y pulsaciones del
 * boton); el lazo los lee y hace todo lo demas.
 */

#include <stdbool.h>
#include <stdint.h>

/* Configura los LEDs y la interrupcion del boton. */
void port_iniciar(void);

/* Arma (o rearma) el temporizador de hardware con un tick de tick_ms. Devuelve
 * false, sin tocar el temporizador, si la placa no admite esa duracion. */
bool port_configurar_tick(uint16_t tick_ms);

/* Ticks generados por la ISR del temporizador desde el arranque. */
uint32_t port_ticks_generados(void);

/* Pulsaciones del boton ya filtradas por el debounce, desde el arranque. */
uint32_t port_pulsaciones(void);

/* Duerme hasta la siguiente interrupcion si el temporizador no genero ningun
 * tick despues de procesados. Puede volver antes por otra interrupcion: quien
 * llama vuelve a revisar. */
void port_esperar_tick(uint32_t procesados);

/* Muestra la tarea en ejecucion en binario (0 = idle) y el LED de plazo. */
void port_mostrar(uint8_t leds_tarea, bool led_plazo);

#endif
