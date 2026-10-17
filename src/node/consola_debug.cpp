#include "consola_debug.h"

#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linea de comando mas larga: "t 255 1 65535 65535 65535" con holgura. */
#define LINEA_MAX 40u
/* Linea de salida mas larga: "ev 4294967295 4 255 65535" con holgura. */
#define SALIDA_MAX 48u

static char linea[LINEA_MAX];
static uint8_t largo;
static bool desbordada;     /* la linea en curso no cupo; se descarta entera */

/* Escribe una linea solo si cabe completa en el bufer de transmision, para no
 * bloquear el lazo. Devuelve false si no cupo. */
static bool escribir(const char *texto)
{
    size_t n = strlen(texto);
    if ((size_t)Serial.availableForWrite() < n + 2)
        return false;
    Serial.print(texto);
    Serial.print("\r\n");
    return true;
}

/* Lee el siguiente numero decimal de *p, que debe ser como mucho max. */
static bool leer_numero(char **p, uint32_t max, uint32_t *out)
{
    char *fin;
    while (**p == ' ')
        (*p)++;
    if (**p < '0' || **p > '9')
        return false;
    unsigned long v = strtoul(*p, &fin, 10);
    if (v > max)
        return false;
    *p = fin;
    *out = (uint32_t)v;
    return true;
}

static bool fin_de_linea(char *p)
{
    while (*p == ' ')
        p++;
    return *p == '\0';
}

static void responder_ack(ack_status_t status)
{
    char salida[SALIDA_MAX];
    snprintf(salida, sizeof salida, "ack %u", (unsigned)status);
    escribir(salida);
}

static void ejecutar(nodo_t *n, char *p)
{
    char salida[SALIDA_MAX];
    char comando = *p++;
    uint32_t a, b, c, d, e;

    switch (comando) {
    case 'c':
        if (!leer_numero(&p, UINT16_MAX, &a) || !fin_de_linea(p))
            break;
        snprintf(salida, sizeof salida, "s_n %lu",
                 (unsigned long)nodo_calibrar(n, (uint16_t)a));
        escribir(salida);
        return;
    case 't':
        if (!leer_numero(&p, UINT8_MAX, &a) || !leer_numero(&p, UINT8_MAX, &b) ||
            !leer_numero(&p, UINT16_MAX, &c) || !leer_numero(&p, UINT16_MAX, &d) ||
            !leer_numero(&p, UINT16_MAX, &e) || !fin_de_linea(p))
            break;
        {
            tarea_def_t def = {(uint8_t)a, (uint8_t)b, (uint16_t)c, (uint16_t)d, (uint16_t)e};
            responder_ack(sched_agregar_tarea(&n->sched, &def));
        }
        return;
    case 'i':
        if (!leer_numero(&p, UINT8_MAX, &a) || !leer_numero(&p, UINT16_MAX, &b) ||
            !fin_de_linea(p))
            break;
        responder_ack(sched_iniciar(&n->sched, (uint8_t)a, (uint16_t)b));
        return;
    case 'd':
        if (!fin_de_linea(p))
            break;
        sched_registrar_disparo(&n->sched);
        return;
    case 's':
        if (!fin_de_linea(p))
            break;
        sched_detener(&n->sched);
        responder_ack(ACK_OK);
        return;
    case '?':
        if (!fin_de_linea(p))
            break;
        snprintf(salida, sizeof salida, "tick %lu s_n %lu sob %lu desc %lu ign %lu",
                 (unsigned long)n->sched.tick, (unsigned long)n->s_n,
                 (unsigned long)n->ticks.sobrecargas,
                 (unsigned long)n->sched.eventos.descartados,
                 (unsigned long)n->sched.esporadicas_ignoradas);
        escribir(salida);
        return;
    default:
        break;
    }
    escribir("err");
}

static void leer_comandos(nodo_t *n)
{
    while (Serial.available() > 0) {
        char ch = (char)Serial.read();
        if (ch == '\r')
            continue;
        if (ch != '\n') {
            if (largo < LINEA_MAX - 1)
                linea[largo++] = ch;
            else
                desbordada = true;
            continue;
        }
        linea[largo] = '\0';
        if (desbordada)
            escribir("err");
        else if (largo > 0)
            ejecutar(n, linea);
        largo = 0;
        desbordada = false;
    }
}

/* Escribe los eventos de la cola mientras quepan; los demas esperan al
 * siguiente tick en la cola, que descarta y cuenta si se llena. */
static void escribir_eventos(nodo_t *n)
{
    char salida[SALIDA_MAX];
    evento_t e;
    while ((size_t)Serial.availableForWrite() >= SALIDA_MAX && evq_pop(&n->sched.eventos, &e)) {
        snprintf(salida, sizeof salida, "ev %lu %u %u %u", (unsigned long)e.tick,
                 (unsigned)e.tipo, (unsigned)e.id_tarea, (unsigned)e.aux);
        escribir(salida);
    }
}

void consola_atender(nodo_t *n, bool reporte_tick)
{
    leer_comandos(n);
    escribir_eventos(n);
    if (reporte_tick) {
        char salida[SALIDA_MAX];
        /* El tick reportado es el que se acaba de procesar. */
        snprintf(salida, sizeof salida, "rep %lu", (unsigned long)(n->sched.tick - 1u));
        escribir(salida);
    }
}
