/*
 * Programa del nodo: espera el tick del temporizador de hardware, le pide la
 * decision al calendarizador, la muestra en los LEDs, ejecuta el tramo de la
 * tarea elegida y despues atiende el serial. El codigo de cada placa esta
 * detras de port.h.
 */

#include <Arduino.h>

#include "consola_debug.h"
#include "medicion.h"
#include "nodo.h"
#include "port.h"

/* ut.h es del curso y no se modifica; no trae guardas para C++. */
extern "C" {
#include "ut.h"
}

static nodo_t nodo;
static uint32_t pulsaciones_vistas;

uint32_t nodo_calibrar(nodo_t *n, uint16_t tick_ms)
{
    if (n->sched.en_marcha || !port_configurar_tick(tick_ms))
        return 0;
    n->s_n = calibrar_ut_por_tick(tick_ms);
    /* Los ticks que el temporizador genero mientras se media no se procesan. */
    lazo_ticks_reiniciar(&n->ticks, port_ticks_generados());
    return n->s_n;
}

/* Las pulsaciones del boton llegan como contador desde la ISR. Se pasan al
 * calendarizador desde el lazo, antes del tick, asi el disparo tiene un solo
 * productor (el lazo), igual que el mensaje 0x06. */
static void traspasar_pulsaciones(void)
{
    uint32_t pulsaciones = port_pulsaciones();
    while (pulsaciones_vistas != pulsaciones) {
        sched_registrar_disparo(&nodo.sched);
        pulsaciones_vistas++;
    }
}

static void ejecutar_tramo(uint8_t slot)
{
    if (slot == SLOT_IDLE)
        return;
    for (uint32_t i = 0; i < nodo.s_n; i++)
        unidad_trabajo();
}

void setup()
{
    Serial.begin(SERIAL_BAUDIOS);
    sched_init(&nodo.sched);
    port_iniciar();
    port_configurar_tick(TICK_MS_DEFECTO);
    lazo_ticks_reiniciar(&nodo.ticks, port_ticks_generados());
    pulsaciones_vistas = port_pulsaciones();
}

void loop()
{
    if (!lazo_tomar_tick(&nodo.ticks, port_ticks_generados())) {
        port_esperar_tick(nodo.ticks.procesados);
        return;
    }

    traspasar_pulsaciones();
    decision_t d = sched_tick(&nodo.sched);
    port_mostrar(d.leds_tarea, d.led_plazo);
    ejecutar_tramo(d.slot);
    lazo_fin_tramo(&nodo.ticks, port_ticks_generados());

    consola_atender(&nodo, d.reporte_tick);
}
