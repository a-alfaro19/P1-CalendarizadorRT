#ifndef EVQ_H
#define EVQ_H

/*
 * Cola de eventos del nodo: bufer circular de capacidad fija con un solo
 * productor (el calendarizador) y un solo consumidor (el modulo de protocolo,
 * que transmite en segundo plano).
 *
 * evento_t replica el LOAD del mensaje 0x04 Evento del protocolo serial.
 */

#include <stdbool.h>
#include <stdint.h>

#include "sched_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Valores de event_type definidos por el protocolo. */
typedef enum {
    EV_LIBERACION = 0,
    EV_CAMBIO_TAREA = 1,       /* id_tarea = la que pasa a ejecutar */
    EV_COMPLETADO = 2,         /* aux = tiempo de respuesta en ticks */
    EV_PLAZO_PERDIDO = 3,
    EV_ESPORADICA_IGNORADA = 4
} tipo_evento_t;

/* id_tarea reservado por el protocolo para "ninguna tarea / nodo inactivo". */
#define ID_TAREA_NINGUNA 0xFFu

typedef struct {
    uint32_t tick;      /* ticks desde el mensaje de inicio */
    uint8_t tipo;       /* tipo_evento_t */
    uint8_t id_tarea;
    uint16_t aux;
} evento_t;

typedef struct {
    evento_t buf[EVQ_CAPACIDAD];
    /* Indices libres que solo crecen; la posicion real es indice % capacidad.
     * cabeza la escribe solo el productor y cola solo el consumidor. */
    volatile uint16_t cabeza;
    volatile uint16_t cola;
    uint32_t descartados;   /* eventos perdidos por cola llena */
} evq_t;

void evq_init(evq_t *q);

/* Encola sin bloquear. Si la cola esta llena descarta el evento, incrementa
 * descartados y devuelve false. */
bool evq_push(evq_t *q, const evento_t *e);

/* Desencola sin bloquear. Devuelve false si la cola esta vacia. */
bool evq_pop(evq_t *q, evento_t *out);

#ifdef __cplusplus
}
#endif

#endif
