#include "evq.h"

_Static_assert((EVQ_CAPACIDAD & (EVQ_CAPACIDAD - 1u)) == 0u,
               "EVQ_CAPACIDAD debe ser potencia de dos");
_Static_assert(EVQ_CAPACIDAD <= 32768u,
               "EVQ_CAPACIDAD debe caber en los indices de 16 bits");

#define POSICION(i) ((i) & (EVQ_CAPACIDAD - 1u))

void evq_init(evq_t *q)
{
    q->cabeza = 0;
    q->cola = 0;
    q->descartados = 0;
}

bool evq_push(evq_t *q, const evento_t *e)
{
    uint16_t cabeza = q->cabeza;
    /* La resta en 16 bits da la ocupacion aunque los indices hayan dado la
     * vuelta, porque la capacidad divide a 2^16. */
    if ((uint16_t)(cabeza - q->cola) >= EVQ_CAPACIDAD) {
        q->descartados++;
        return false;
    }
    q->buf[POSICION(cabeza)] = *e;
    q->cabeza = (uint16_t)(cabeza + 1u);
    return true;
}

bool evq_pop(evq_t *q, evento_t *out)
{
    uint16_t cola = q->cola;
    if (cola == q->cabeza)
        return false;
    *out = q->buf[POSICION(cola)];
    q->cola = (uint16_t)(cola + 1u);
    return true;
}
