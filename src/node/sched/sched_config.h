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

/* Duracion del tick hasta que el host manda la suya en 0x00, en ms. */
#define TICK_MS_DEFECTO 10u

/* Ventana de debounce del boton de la esporadica: tras una pulsacion
 * aceptada, los flancos que llegan antes de este tiempo son rebotes. */
#define DEBOUNCE_MS 50u

/* Velocidad del puerto serial con el host (docs/PROTOCOL.md, 8N1). */
#define SERIAL_BAUDIOS 115200u

#endif
