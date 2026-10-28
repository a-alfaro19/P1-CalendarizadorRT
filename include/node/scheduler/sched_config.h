#ifndef SCHED_CONFIG_H
#define SCHED_CONFIG_H

/*
 * Constantes del calendarizador del nodo. Todo valor ajustable vive aqui
 * para no repetir numeros magicos en el nucleo ni en los puertos.
 */

/* Tareas simultaneas por nodo (minimo exigido por el enunciado). */
#define MAX_TAREAS 8u

/* Capacidad de la cola de eventos. Debe ser potencia de dos y caber en los
 * indices de 16 bits de evq_t. */
#define EVQ_CAPACIDAD 64u

/* Duracion del tick hasta que el host manda la suya en 0x00, en ms. Antes de
 * eso el temporizador solo sirve para despertar el lazo y atender el serial. */
#define TICK_MS_DEFECTO 10u

/* Tick mas largo admitido. Un tramo ocupa casi todo el tick sin ceder el
 * control, y el watchdog del ESP8266 reinicia la placa a los ~3 s. La Pico
 * usa el mismo limite para que las dos placas acepten la misma configuracion. */
#define TICK_MS_MAX 2000u

/* Ventana de debounce del boton de la esporadica: tras una pulsacion
 * aceptada, los flancos que llegan antes de este tiempo son rebotes. */
#define DEBOUNCE_MS 50u

#endif
