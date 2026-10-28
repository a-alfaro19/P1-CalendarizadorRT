#include "node/scheduler/calendarizador.h"

#include <string.h>

void sched_init(sched_t *s)
{
    memset(s, 0, sizeof *s);
    s->slot_actual = SLOT_IDLE;
    evq_init(&s->eventos);
}

static bool id_registrado(const sched_t *s, uint8_t id)
{
    for (uint8_t i = 0; i < s->n_tareas; i++)
        if (s->tareas[i].def.id == id)
            return true;
    return false;
}

static bool hay_esporadica(const sched_t *s)
{
    for (uint8_t i = 0; i < s->n_tareas; i++)
        if (s->tareas[i].def.tipo == TAREA_ESPORADICA)
            return true;
    return false;
}

static bool definicion_valida(const sched_t *s, const tarea_def_t *d)
{
    if (d->id == ID_TAREA_NINGUNA)
        return false;
    if (d->tipo != TAREA_PERIODICA && d->tipo != TAREA_ESPORADICA)
        return false;
    if (d->costo == 0 || d->periodo == 0 || d->plazo == 0)
        return false;
    if (d->plazo > d->periodo)
        return false;
    if (d->tipo == TAREA_ESPORADICA && hay_esporadica(s))
        return false;
    return true;
}

ack_status_t sched_agregar_tarea(sched_t *s, const tarea_def_t *def)
{
    /* Primero la idempotencia: el host reenvia cuando se pierde un ACK. */
    if (id_registrado(s, def->id))
        return ACK_OK;
    if (!definicion_valida(s, def))
        return ACK_INVALIDO;
    if (s->n_tareas >= MAX_TAREAS)
        return ACK_SIN_ESPACIO;

    tarea_t nueva = {.def = *def, .t_alta = s->tick};
    s->tareas[s->n_tareas] = nueva;
    s->n_tareas++;
    return ACK_OK;
}

ack_status_t sched_iniciar(sched_t *s, uint8_t alg, uint16_t periodo_reporte)
{
    if (s->en_marcha)
        return ACK_OK;
    if ((alg != ALG_RMS && alg != ALG_EDF) || periodo_reporte == 0)
        return ACK_INVALIDO;
    s->alg = alg;
    s->periodo_reporte = periodo_reporte;
    s->en_marcha = true;
    return ACK_OK;
}

void sched_detener(sched_t *s)
{
    s->en_marcha = false;
}

void sched_registrar_disparo(sched_t *s)
{
    if (s->en_marcha)
        s->disparos++;
}

static void emitir(sched_t *s, uint32_t tick, tipo_evento_t tipo, uint8_t id, uint16_t aux)
{
    evento_t e = {.tick = tick, .tipo = (uint8_t)tipo, .id_tarea = id, .aux = aux};
    evq_push(&s->eventos, &e);
}

static uint8_t id_del_slot(const sched_t *s, uint8_t slot)
{
    return slot == SLOT_IDLE ? ID_TAREA_NINGUNA : s->tareas[slot].def.id;
}

static uint16_t saturar_u16(uint32_t v)
{
    return v > UINT16_MAX ? UINT16_MAX : (uint16_t)v;
}

/* Paso 2: aborta los trabajos que llegaron a su plazo sin terminar. Va antes
 * de liberar para que, con D = T, el trabajo viejo se aborte antes de que el
 * nuevo lo reemplace. */
static void abortar_vencidos(sched_t *s, uint32_t t)
{
    for (uint8_t i = 0; i < s->n_tareas; i++) {
        tarea_t *ta = &s->tareas[i];
        if (ta->restante == 0 || t < ta->plazo_abs)
            continue;
        ta->restante = 0;
        s->plazo_perdido = true;
        emitir(s, t, EV_PLAZO_PERDIDO, ta->def.id, 0);
    }
}

static tarea_t *buscar_esporadica(sched_t *s)
{
    for (uint8_t i = 0; i < s->n_tareas; i++)
        if (s->tareas[i].def.tipo == TAREA_ESPORADICA)
            return &s->tareas[i];
    return NULL;
}

/* Paso 3: atiende los disparos registrados desde el tick anterior. Va despues
 * de abortar para que, con D = T, el trabajo vencido emita su evento 3 antes
 * de que el nuevo lo reemplace. */
static void atender_disparos(sched_t *s, uint32_t t)
{
    uint32_t pendientes = s->disparos - s->disparos_atendidos;
    s->disparos_atendidos += pendientes;

    tarea_t *ta = buscar_esporadica(s);
    if (ta == NULL)
        return;

    /* El disparo ocurrio durante el tick anterior, que es el que se mide. */
    uint32_t tick_disparo = (t > 0) ? t - 1 : 0;
    for (; pendientes > 0; pendientes--) {
        if (s->esporadica_liberada && t - s->ultima_esporadica < ta->def.periodo) {
            s->esporadicas_ignoradas++;
            emitir(s, t, EV_ESPORADICA_IGNORADA, ta->def.id, 0);
            continue;
        }
        s->esporadica_liberada = true;
        s->ultima_esporadica = t;
        ta->llegada = tick_disparo;
        ta->plazo_abs = t + ta->def.plazo;
        ta->restante = ta->def.costo;
        emitir(s, t, EV_LIBERACION, ta->def.id, 0);
    }
}

/* Paso 4: libera los trabajos periodicos que tocan en el tick t. */
static void liberar_periodicas(sched_t *s, uint32_t t)
{
    for (uint8_t i = 0; i < s->n_tareas; i++) {
        tarea_t *ta = &s->tareas[i];
        if (ta->def.tipo != TAREA_PERIODICA)
            continue;
        if ((t - ta->t_alta) % ta->def.periodo != 0)
            continue;
        ta->llegada = t;
        ta->plazo_abs = t + ta->def.plazo;
        ta->restante = ta->def.costo;
        emitir(s, t, EV_LIBERACION, ta->def.id, 0);
    }
}

/* Devuelve true si a tiene mayor prioridad que b segun el algoritmo activo. */
static bool tiene_prioridad(uint8_t alg, const tarea_t *a, const tarea_t *b)
{
    /* RMS: prioridad fija por periodo. EDF: plazo absoluto mas cercano. */
    uint32_t clave_a = (alg == ALG_RMS) ? a->def.periodo : a->plazo_abs;
    uint32_t clave_b = (alg == ALG_RMS) ? b->def.periodo : b->plazo_abs;

    if (clave_a != clave_b)
        return clave_a < clave_b;
    return a->def.id < b->def.id;   /* desempate: id menor */
}

/* Paso 5: slot listo de mayor prioridad, o SLOT_IDLE si no hay ninguno. */
static uint8_t elegir(const sched_t *s)
{
    uint8_t mejor = SLOT_IDLE;
    for (uint8_t i = 0; i < s->n_tareas; i++) {
        if (s->tareas[i].restante == 0)
            continue;
        if (mejor == SLOT_IDLE || tiene_prioridad(s->alg, &s->tareas[i], &s->tareas[mejor]))
            mejor = i;
    }
    return mejor;
}

decision_t sched_tick(sched_t *s)
{
    decision_t d = {.slot = SLOT_IDLE, .leds_tarea = 0, .led_plazo = false, .reporte_tick = false};
    if (!s->en_marcha)
        return d;

    uint32_t t = s->tick;
    abortar_vencidos(s, t);
    atender_disparos(s, t);
    liberar_periodicas(s, t);

    d.slot = elegir(s);
    if (d.slot != s->slot_actual) {
        emitir(s, t, EV_CAMBIO_TAREA, id_del_slot(s, d.slot), 0);
        s->slot_actual = d.slot;
    }

    /* Paso 6: la tarea elegida avanza un tick de costo. */
    if (d.slot != SLOT_IDLE) {
        tarea_t *ta = &s->tareas[d.slot];
        ta->restante--;
        if (ta->restante == 0)
            emitir(s, t + 1, EV_COMPLETADO, ta->def.id, saturar_u16(t + 1 - ta->llegada));
        d.leds_tarea = (uint8_t)(d.slot + 1u);
    }

    d.led_plazo = s->plazo_perdido;
    d.reporte_tick = (t % s->periodo_reporte == 0);
    s->tick = t + 1;
    return d;
}
