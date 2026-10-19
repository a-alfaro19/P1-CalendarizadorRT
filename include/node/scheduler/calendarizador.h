#ifndef CALENDARIZADOR_H
#define CALENDARIZADOR_H

/*
 * Nucleo portable del calendarizador local del nodo (RMS / EDF por tick).
 * No depende de hardware: el puerto de cada placa le entrega los ticks y
 * ejecuta las decisiones que devuelve.
 *
 * Salvo sched_registrar_disparo, ninguna funcion es reentrante: el puerto las
 * llama desde el mismo contexto que sched_tick, entre un tick y el siguiente.
 * Por eso un alta o una detencion surte efecto en el siguiente tick.
 *
 * El archivo no se llama sched.h para no chocar con el header POSIX del
 * sistema del mismo nombre.
 */

#include <stdbool.h>
#include <stdint.h>

#include "evq.h"
#include "sched_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Valores de status del ACK definidos por el protocolo. */
typedef enum {
    ACK_OK = 0,
    ACK_INVALIDO = 1,
    ACK_SIN_ESPACIO = 2
} ack_status_t;

/* Valores de kind del mensaje 0x02 Despliegue. */
typedef enum {
    TAREA_PERIODICA = 0,
    TAREA_ESPORADICA = 1
} tipo_tarea_t;

/* Definicion de una tarea tal como llega en 0x02. Los tiempos van en ticks. */
typedef struct {
    uint8_t id;
    uint8_t tipo;       /* tipo_tarea_t; se guarda crudo para validarlo */
    uint16_t costo;     /* C, calculado por el host con el s_n del nodo */
    uint16_t periodo;   /* T, o intervalo minimo entre llegadas si es esporadica */
    uint16_t plazo;     /* D relativo */
} tarea_def_t;

/* Valores de algorithm del mensaje 0x03 Inicio. */
typedef enum {
    ALG_RMS = 0,
    ALG_EDF = 1
} algoritmo_t;

/* Slot que indica que en este tick no corre ninguna tarea. */
#define SLOT_IDLE 0xFFu

typedef struct {
    tarea_def_t def;
    uint32_t t_alta;        /* tick en que se registro; base de sus liberaciones */
    uint32_t llegada;       /* desde aqui se mide el tiempo de respuesta: la
                               liberacion si es periodica, el disparo si es
                               esporadica */
    uint32_t plazo_abs;     /* liberacion + plazo del trabajo activo */
    uint16_t restante;      /* ticks de costo que le faltan; 0 = sin trabajo activo */
} tarea_t;

typedef struct {
    tarea_t tareas[MAX_TAREAS];
    uint8_t n_tareas;
    uint8_t alg;            /* algoritmo_t */
    uint16_t periodo_reporte;   /* report_period de 0x03, en ticks */
    bool en_marcha;
    bool plazo_perdido;     /* hubo al menos una perdida desde el inicio */
    uint8_t slot_actual;    /* slot elegido en el tick anterior, o SLOT_IDLE */
    uint32_t tick;          /* proximo tick a procesar, contado desde el inicio */
    evq_t eventos;          /* el modulo de protocolo consume de aqui */

    /* Disparos de la esporadica. Solo sched_registrar_disparo escribe
     * disparos y solo sched_tick escribe disparos_atendidos, asi que la ISR
     * no comparte ninguna variable de escritura con el calendarizador. */
    volatile uint32_t disparos;
    uint32_t disparos_atendidos;
    bool esporadica_liberada;       /* ya hubo al menos una liberacion aceptada */
    uint32_t ultima_esporadica;     /* tick de la ultima liberacion aceptada */
    uint32_t esporadicas_ignoradas; /* disparos antes del intervalo minimo */
} sched_t;

/* Lo que el puerto debe hacer durante el tick recien decidido. */
typedef struct {
    uint8_t slot;           /* tarea a ejecutar (s_n UT), o SLOT_IDLE */
    uint8_t leds_tarea;     /* valor para los LEDs en binario: slot + 1, 0 = idle */
    bool led_plazo;         /* encendido desde la primera perdida hasta la detencion */
    bool reporte_tick;      /* toca enviar 0x05 en este tick */
} decision_t;

void sched_init(sched_t *s);

/*
 * Registra una tarea (mensaje 0x02) y devuelve el status del ACK:
 *   ACK_OK          tarea registrada, o id ya registrado (idempotente: no se
 *                   duplica ni cambia sus parametros).
 *   ACK_INVALIDO    costo, periodo o plazo en 0; plazo > periodo; tipo
 *                   desconocido; id reservado ID_TAREA_NINGUNA; o segunda
 *                   esporadica.
 *   ACK_SIN_ESPACIO ya hay MAX_TAREAS tareas.
 * Un rechazo no modifica el estado.
 */
ack_status_t sched_agregar_tarea(sched_t *s, const tarea_def_t *def);

/*
 * Mensaje 0x03: arranca en el tick cero con el algoritmo dado y marca
 * reporte_tick cada periodo_reporte ticks. ACK_INVALIDO si el algoritmo es
 * desconocido o el periodo es 0. Si ya esta en marcha responde ACK_OK sin
 * cambiar nada (idempotente: no reinicia el tick).
 */
ack_status_t sched_iniciar(sched_t *s, uint8_t alg, uint16_t periodo_reporte);

/* Mensaje 0x07: deja de ejecutar. Desde aqui sched_tick devuelve la decision
 * apagada (idle, LEDs y LED de plazo en 0, sin reporte). Idempotente. El
 * protocolo no permite volver a iniciar despues de detener. */
void sched_detener(sched_t *s);

/*
 * Registra un disparo de la tarea esporadica (boton o mensaje 0x06). Es segura
 * de llamar desde una ISR: solo incrementa un contador. Todo lo demas lo hace
 * el siguiente sched_tick, que toma como tick del disparo el tick en curso.
 * Debe haber un solo productor a la vez: si el puerto la llama fuera de la
 * ISR, lo hace con la interrupcion del boton deshabilitada. Antes de
 * sched_iniciar el disparo se descarta.
 */
void sched_registrar_disparo(sched_t *s);

/*
 * Procesa el siguiente tick y devuelve que ejecutar durante el. Debe llamarse
 * una vez por tick del temporizador. Antes de sched_iniciar devuelve idle y
 * no avanza el tiempo.
 */
decision_t sched_tick(sched_t *s);

#ifdef __cplusplus
}
#endif

#endif
