#include "lazo.h"

void lazo_ticks_reiniciar(lazo_ticks_t *l, uint32_t generados)
{
    l->procesados = generados;
    l->sobrecargas = 0;
}

bool lazo_tomar_tick(lazo_ticks_t *l, uint32_t generados)
{
    if (generados == l->procesados)
        return false;
    l->procesados++;
    return true;
}

void lazo_fin_tramo(lazo_ticks_t *l, uint32_t generados)
{
    if (generados != l->procesados)
        l->sobrecargas++;
}
