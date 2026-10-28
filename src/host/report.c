#define _POSIX_C_SOURCE 200809L
#include "host/report.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ---------- Acumulación ---------- */

void report_init(report_state_t *state, const HostConfig *config)
{
    memset(state, 0, sizeof(*state));
    state->tick_ms = config->tick_ms;
    state->report_period = config->tick_report_period;
    state->algorithm = config->algorithm;
    state->heuristic = config->heuristic;
}

static report_node_t *node_of(report_state_t *state, int node_id)
{
    if (node_id < 1 || node_id > HOST_NODE_COUNT)
        return NULL;
    return &state->nodes[node_id - 1];
}

/* Los últimos REPORT_LOG_RESERVE lugares de la línea de tiempo se guardan para
 * eventos raros pero críticos (plazos perdidos, fallas, esporádicas ignoradas):
 * en una corrida larga las liberaciones llenan el arreglo y esos eventos no
 * pueden perderse. */
#define REPORT_LOG_RESERVE 256

static bool log_is_critical(report_log_kind_t kind)
{
    return kind == REPORT_LOG_MISS || kind == REPORT_LOG_NODE_DOWN ||
           kind == REPORT_LOG_SPORADIC_IGNORED || kind == REPORT_LOG_BAD_FRAME;
}

static void log_add(report_state_t *state, uint32_t tick, int node_id, report_log_kind_t kind,
                    uint8_t task, uint16_t aux)
{
    size_t capacity = log_is_critical(kind) ? REPORT_MAX_LOG : REPORT_MAX_LOG - REPORT_LOG_RESERVE;
    if (state->log_count >= capacity)
    {
        state->log_dropped++;
        return;
    }
    state->log[state->log_count++] = (report_log_t){tick, aux, (uint8_t)node_id, (uint8_t)kind, task};
}

/* Cierra el tramo en curso. El tiempo ocupado se cuenta aunque el tramo no
 * quepa en el arreglo, así la utilización medida no depende del tope. */
static void close_segment(report_node_t *node, uint32_t end)
{
    if (!node->open)
        return;
    node->open = false;
    if (end < node->current_start)
        end = node->current_start;
    uint32_t length = end - node->current_start;
    if (length == 0)
        return;
    if (node->current_task != TASK_NONE)
        node->busy_ticks += length;
    if (node->segment_count >= REPORT_MAX_SEGMENTS)
    {
        node->segments_dropped++;
        return;
    }
    node->segments[node->segment_count++] =
        (report_segment_t){node->current_start, end, node->current_task};
}

static void note_tick(report_node_t *node, uint32_t tick)
{
    if (tick > node->last_tick)
        node->last_tick = tick;
}

static void recompute_utilization(report_state_t *state)
{
    double sum[HOST_NODE_COUNT] = {0};
    for (int i = 0; i < REPORT_MAX_TASKS; i++)
    {
        const report_task_t *task = &state->tasks[i];
        if (task->used && task->accepted && task->period > 0 &&
            task->node >= 1 && task->node <= HOST_NODE_COUNT)
            sum[task->node - 1] += (double)task->cost / task->period;
    }
    for (int i = 0; i < HOST_NODE_COUNT; i++)
        if (!state->nodes[i].down)
            state->nodes[i].theoretical_util = sum[i];
}

static void consume_assignment(report_state_t *state, const ipc_message_t *message)
{
    const int id = message->payload.assignment.task_id;
    if (id < 0 || id >= REPORT_MAX_TASKS - 1)
        return;
    const bool accepted = message->payload.assignment.accepted != 0;
    const bool confirmed = message->payload.assignment.confirmed != 0;
    const char *reason = message->payload.assignment.reason;

    if (state->decision_count < REPORT_MAX_DECISIONS)
    {
        report_decision_t *d = &state->decisions[state->decision_count++];
        d->task_id = id;
        d->node = accepted ? message->node_id : 0;
        d->accepted = accepted;
        d->confirmed = confirmed;
        snprintf(d->reason, sizeof(d->reason), "%.*s", IPC_REASON_LENGTH - 1, reason);

        /* Si la tarea ya estaba en otro nodo, la decisión es una reubicación (o un
         * rechazo tras la caída de su nodo) y el reporte debe decirlo. */
        const report_task_t *before = &state->tasks[id];
        if (before->used && before->accepted && before->node >= 1)
        {
            const report_node_t *old_node = node_of(state, before->node);
            size_t len = strlen(d->reason);
            if (accepted && before->node != message->node_id && !strstr(d->reason, "eubic"))
                snprintf(d->reason + len, sizeof(d->reason) - len,
                         " (reubicada desde el nodo %d)", before->node);
            else if (!accepted && old_node && old_node->down)
                snprintf(d->reason + len, sizeof(d->reason) - len,
                         " (estaba en el nodo %d, caído)", before->node);
        }
    }
    else
        state->decisions_dropped++;

    report_task_t *task = &state->tasks[id];
    /* Un rechazo de una tarea ya confirmada en un nodo vivo es un id repetido
     * enviado por el usuario: no debe borrar la tarea que sigue ejecutándose. */
    const report_node_t *current = task->used ? node_of(state, task->node) : NULL;
    if (!accepted && task->used && task->accepted && task->confirmed && current && !current->down)
        return;
    task->used = true;
    task->accepted = accepted;
    task->confirmed = confirmed;
    task->node = accepted ? message->node_id : 0;
    if (message->payload.assignment.period > 0)
    {
        task->kind = message->payload.assignment.kind;
        task->period = message->payload.assignment.period;
        task->deadline = message->payload.assignment.deadline;
        task->criticality = message->payload.assignment.criticality;
    }
    if (message->payload.assignment.cost_ticks > 0)
        task->cost = message->payload.assignment.cost_ticks;
    recompute_utilization(state);
}

/* El nodo confirma sus tareas al desplegarlas, pero el planificador solo publica
 * esa confirmación para las tareas manuales. Un evento real de la tarea en su
 * nodo demuestra que está desplegada y en ejecución. */
static void confirm_from_event(report_state_t *state, report_task_t *task, int task_id, int node_id)
{
    if (!task->accepted || task->confirmed || task->node != node_id)
        return;
    task->confirmed = true;
    for (size_t k = state->decision_count; k-- > 0;)
    {
        report_decision_t *d = &state->decisions[k];
        if (d->task_id != task_id || !d->accepted)
            continue;
        if (d->node == node_id)
            d->confirmed = true;
        return;
    }
}

/* Cuando el arranque termina, un nodo con todas sus tareas confirmadas (p. ej. 5/5)
 * tiene desplegadas incluso las que aún no han producido eventos, como la esporádica. */
static void confirm_deployed(report_state_t *state)
{
    for (int n = 0; n < HOST_NODE_COUNT && n < state->startup.node_count; n++)
    {
        if (!state->startup.nodes[n].planned_tasks ||
            state->startup.nodes[n].confirmed_tasks != state->startup.nodes[n].planned_tasks)
            continue;
        for (size_t k = 0; k < state->decision_count; k++)
        {
            report_decision_t *d = &state->decisions[k];
            if (!d->accepted || d->confirmed || d->node != n + 1)
                continue;
            d->confirmed = true;
            if (d->task_id >= 0 && d->task_id < REPORT_MAX_TASKS - 1 &&
                state->tasks[d->task_id].node == n + 1)
                state->tasks[d->task_id].confirmed = true;
        }
    }
}

static void consume_event(report_state_t *state, const ipc_message_t *message)
{
    report_node_t *node = node_of(state, message->node_id);
    if (!node)
        return;
    const msg_event_t *event = &message->payload.event;
    note_tick(node, event->tick);

    report_task_t *task = NULL;
    if (event->task_id != TASK_NONE)
    {
        task = &state->tasks[event->task_id];
        if (!task->used)
        {
            /* Evento de una tarea que el reporte no vio asignar: se conserva el conteo */
            task->used = true;
            task->node = message->node_id;
        }
        confirm_from_event(state, task, event->task_id, message->node_id);
    }
    switch (event->event_type)
    {
    case EVENT_SWITCH:
        close_segment(node, event->tick);
        node->open = true;
        node->current_task = event->task_id;
        node->current_start = event->tick;
        node->switches++;
        break;
    case EVENT_RELEASE:
        if (task)
            task->releases++;
        log_add(state, event->tick, message->node_id, REPORT_LOG_RELEASE, event->task_id, 0);
        break;
    case EVENT_COMPLETE:
        if (task)
        {
            task->completions++;
            task->response_sum += event->aux;
            if (task->completions == 1 || event->aux < task->response_min)
                task->response_min = event->aux;
            if (event->aux > task->response_max)
                task->response_max = event->aux;
        }
        log_add(state, event->tick, message->node_id, REPORT_LOG_COMPLETE, event->task_id, event->aux);
        break;
    case EVENT_DEADLINE_MISS:
        if (task)
            task->misses++;
        log_add(state, event->tick, message->node_id, REPORT_LOG_MISS, event->task_id, 0);
        break;
    case EVENT_SPORADIC_IGNORED:
        if (task)
            task->ignored++;
        log_add(state, event->tick, message->node_id, REPORT_LOG_SPORADIC_IGNORED, event->task_id, 0);
        break;
    default:
        break;
    }
}

void report_consume(report_state_t *state, const ipc_message_t *message)
{
    report_node_t *node = node_of(state, message->node_id);
    switch (message->type)
    {
    case IPC_STARTUP:
        state->startup = message->payload.startup;
        state->have_startup = true;
        for (int i = 0; i < HOST_NODE_COUNT; i++)
            state->nodes[i].speed_ut_per_tick = state->startup.nodes[i].speed_ut_per_tick;
        if (state->startup.phase == STARTUP_RUNNING)
            confirm_deployed(state);
        break;
    case IPC_ASSIGNMENT:
        consume_assignment(state, message);
        break;
    case IPC_EVENT:
        consume_event(state, message);
        break;
    case IPC_TICK:
        if (node)
        {
            note_tick(node, message->payload.tick.tick);
            node->bad_frames_node = message->payload.tick.bad_frames;
        }
        break;
    case IPC_NODE_DOWN:
        if (node && !node->down)
        {
            /* La utilización teórica de un nodo caído es la que tenía al caer */
            recompute_utilization(state);
            close_segment(node, node->last_tick);
            node->down = true;
            node->down_tick = node->last_tick;
            snprintf(node->down_reason, sizeof(node->down_reason), "%.*s",
                     IPC_REASON_LENGTH - 1, message->payload.error.reason);
            log_add(state, node->last_tick, message->node_id, REPORT_LOG_NODE_DOWN, TASK_NONE, 0);
        }
        break;
    case IPC_BAD_FRAME:
        if (node)
        {
            node->bad_frames_host++;
            log_add(state, node->last_tick, message->node_id, REPORT_LOG_BAD_FRAME, TASK_NONE,
                    (uint16_t)(message->payload.error.code < 0 ? -message->payload.error.code : 0));
        }
        break;
    default:
        break;
    }
}

void report_finalize(report_state_t *state)
{
    for (int i = 0; i < HOST_NODE_COUNT; i++)
        close_segment(&state->nodes[i], state->nodes[i].last_tick);
}

/* ---------- Escritura del reporte ---------- */

static const char *algorithm_name(SchedulingAlgorithm algorithm)
{
    return algorithm == SCHED_EDF ? "EDF" : "RMS";
}

static const char *heuristic_name(AllocationHeuristic heuristic)
{
    return heuristic == HEURISTIC_FIRST_FIT ? "First-Fit" : "Worst-Fit";
}

static void write_header(const report_state_t *state, FILE *out)
{
    fprintf(out, "REPORTE DE EJECUCIÓN\n====================\n");
    fprintf(out, "Algoritmo: %s | Heurística: %s | Tick: %d ms | Reporte de tick cada %d ticks\n",
            algorithm_name(state->algorithm), heuristic_name(state->heuristic),
            state->tick_ms, state->report_period);
    if (!state->have_startup)
    {
        fprintf(out, "El arranque no llegó a publicar su estado.\n\n");
        return;
    }
    for (int i = 0; i < state->startup.node_count && i < HOST_NODE_COUNT; i++)
        fprintf(out, "Nodo %d: %u UT/tick (calibración de esta ejecución)\n", i + 1,
                state->nodes[i].speed_ut_per_tick);
    if (state->startup.phase == STARTUP_FAILED)
        fprintf(out, "El arranque FALLÓ (fase %d, nodo %d, errno=%d).\n",
                state->startup.failed_phase, state->startup.failed_node, state->startup.error_code);
    else if (state->startup.timing_valid && state->startup.node_count == 2)
        fprintf(out, "Desfase de arranque entre nodos: envío %.3f ms; tick cero2-tick cero1 en [%.3f, %.3f] ms\n",
                (double)state->startup.tx_skew_ns / 1e6,
                (double)state->startup.zero_delta_min_ns / 1e6,
                (double)state->startup.zero_delta_max_ns / 1e6);
    fputc('\n', out);
}

/* Dibuja los ticks [from, limit) del nodo. Símbolos: # ejecuta, ^ liberación,
 * @ liberación en un tick en que la tarea ya ejecuta, X plazo perdido. */
static void write_gantt_range(const report_state_t *state, int index, uint32_t from, uint32_t limit,
                              FILE *out)
{
    const report_node_t *node = &state->nodes[index];
    bool row[REPORT_MAX_TASKS] = {0};
    for (int i = 0; i < REPORT_MAX_TASKS - 1; i++)
        if (state->tasks[i].used && state->tasks[i].accepted && state->tasks[i].node == index + 1)
            row[i] = true;
    for (size_t s = 0; s < node->segment_count; s++)
        if (node->segments[s].task != TASK_NONE && node->segments[s].end > from &&
            node->segments[s].start < limit)
            row[node->segments[s].task] = true;

    for (uint32_t base = from; base < limit; base += REPORT_GANTT_WIDTH)
    {
        uint32_t stop = base + REPORT_GANTT_WIDTH < limit ? base + REPORT_GANTT_WIDTH : limit;
        fprintf(out, "  ticks %u-%u   (# ejecuta, ^ liberación, @ liberación y ejecución, X plazo perdido)\n",
                base, stop - 1);
        for (int id = 0; id < REPORT_MAX_TASKS - 1; id++)
        {
            if (!row[id])
                continue;
            char cells[REPORT_GANTT_WIDTH + 1];
            memset(cells, '.', sizeof(cells));
            cells[stop - base] = '\0';
            for (size_t s = 0; s < node->segment_count; s++)
            {
                const report_segment_t *seg = &node->segments[s];
                if (seg->task != id || seg->end <= base || seg->start >= stop)
                    continue;
                uint32_t seg_from = seg->start > base ? seg->start : base;
                uint32_t seg_to = seg->end < stop ? seg->end : stop;
                for (uint32_t t = seg_from; t < seg_to; t++)
                    cells[t - base] = '#';
            }
            for (size_t l = 0; l < state->log_count; l++)
            {
                const report_log_t *entry = &state->log[l];
                if (entry->node != index + 1 || entry->task != id ||
                    entry->tick < base || entry->tick >= stop)
                    continue;
                char *cell = &cells[entry->tick - base];
                if (entry->kind == REPORT_LOG_MISS)
                    *cell = 'X';
                else if (entry->kind == REPORT_LOG_RELEASE)
                {
                    if (*cell == '.')
                        *cell = '^';
                    else if (*cell == '#')
                        *cell = '@';
                }
            }
            fprintf(out, "  T%-3d |%s|\n", id, cells);
        }
    }
}

static void write_gantt_node(const report_state_t *state, int index, FILE *out)
{
    const report_node_t *node = &state->nodes[index];
    fprintf(out, "Nodo %d (%u ticks observados)\n", index + 1, node->last_tick);
    if (node->last_tick == 0)
    {
        fprintf(out, "  (sin actividad registrada)\n\n");
        return;
    }
    uint32_t head = node->last_tick < REPORT_GANTT_TICKS ? node->last_tick : REPORT_GANTT_TICKS;
    if (node->last_tick > head)
        fprintf(out, "  [texto: primeros %u y últimos ticks; el SVG y el CSV tienen toda la ejecución]\n", head);
    write_gantt_range(state, index, 0, head, out);
    if (node->last_tick > head)
    {
        uint32_t tail = 2 * REPORT_GANTT_WIDTH;
        uint32_t from = node->last_tick > tail ? node->last_tick - tail : 0;
        from = (from / REPORT_GANTT_WIDTH) * REPORT_GANTT_WIDTH;
        if (from < head)
            from = head;
        if (from > head)
            fprintf(out, "  ... ticks %u-%u omitidos en este texto ...\n", head, from - 1);
        fprintf(out, "  [últimos ticks observados]\n");
        write_gantt_range(state, index, from, node->last_tick, out);
    }
    if (node->down)
        fprintf(out, "  >>> FALLA: nodo %d caído en el tick %u (%s)\n", index + 1, node->down_tick,
                node->down_reason);
    fputc('\n', out);
}

static void write_timeline(const report_state_t *state, FILE *out)
{
    for (size_t i = 0; i < state->log_count; i++)
    {
        const report_log_t *e = &state->log[i];
        fprintf(out, "  [nodo %u, tick %u] ", e->node, e->tick);
        switch (e->kind)
        {
        case REPORT_LOG_RELEASE:
            fprintf(out, "liberación de la tarea %u\n", e->task);
            break;
        case REPORT_LOG_COMPLETE:
            fprintf(out, "la tarea %u terminó (respuesta %u ticks)\n", e->task, e->aux);
            break;
        case REPORT_LOG_MISS:
            fprintf(out, "la tarea %u PERDIÓ su plazo; trabajo abortado\n", e->task);
            break;
        case REPORT_LOG_SPORADIC_IGNORED:
            fprintf(out, "disparo esporádico ignorado (tarea %u, intervalo mínimo no cumplido)\n", e->task);
            break;
        case REPORT_LOG_NODE_DOWN:
            fprintf(out, "FALLA: nodo declarado caído (%s)\n", state->nodes[e->node - 1].down_reason);
            break;
        case REPORT_LOG_BAD_FRAME:
            fprintf(out, "trama corrupta descartada\n");
            break;
        }
    }
    if (state->log_count == 0)
        fprintf(out, "  (sin eventos)\n");
    if (state->log_dropped)
        fprintf(out, "  ... se omitieron %u eventos posteriores (tope de la línea de tiempo)\n",
                state->log_dropped);
    fputc('\n', out);
}

#define REPORT_EXEC_LINES 150

/* Tramos de ejecución por nodo, para que la sección 2 incluya también la ejecución. */
static void write_execution(const report_state_t *state, FILE *out)
{
    fprintf(out, "Ejecución (tramos por nodo):\n");
    bool any = false;
    for (int i = 0; i < HOST_NODE_COUNT; i++)
    {
        const report_node_t *node = &state->nodes[i];
        unsigned total = 0, printed = 0;
        for (size_t s = 0; s < node->segment_count; s++)
            if (node->segments[s].task != TASK_NONE)
                total++;
        if (!total)
            continue;
        any = true;
        fprintf(out, "  Nodo %d:\n", i + 1);
        for (size_t s = 0; s < node->segment_count && printed < REPORT_EXEC_LINES; s++)
        {
            const report_segment_t *seg = &node->segments[s];
            if (seg->task == TASK_NONE)
                continue;
            fprintf(out, "    ticks %u-%u  ejecuta la tarea %u\n", seg->start, seg->end - 1, seg->task);
            printed++;
        }
        if (total > printed)
            fprintf(out, "    ... %u tramos más (ver el SVG o el CSV de Gantt)\n", total - printed);
    }
    if (!any)
        fprintf(out, "  (sin ejecución registrada)\n");
    fputc('\n', out);
}

/* Una tarea se reubicó si fue aceptada en más de un nodo. */
static bool task_relocated(const report_state_t *state, int id)
{
    int first = 0;
    for (size_t k = 0; k < state->decision_count; k++)
    {
        const report_decision_t *d = &state->decisions[k];
        if (d->task_id != id || !d->accepted || d->node <= 0)
            continue;
        if (!first)
            first = d->node;
        else if (d->node != first)
            return true;
    }
    return false;
}

static void write_task_stats(const report_state_t *state, FILE *out)
{
    fprintf(out, "  id tipo nodo   C    T    D crit  lib compl perd ign  resp(min/prom/máx ticks)  prom ms\n");
    bool any = false, any_relocated = false;
    for (int i = 0; i < REPORT_MAX_TASKS - 1; i++)
    {
        const report_task_t *t = &state->tasks[i];
        if (!t->used)
            continue;
        any = true;
        bool moved = task_relocated(state, i);
        any_relocated = any_relocated || moved;
        double average = t->completions ? (double)t->response_sum / t->completions : 0.0;
        fprintf(out, "  %2d  %c   %3d%c %3d %4d %4d %4d %4u %5u %4u %3u  %5u/%6.1f/%-5u  %10.1f\n",
                i, t->kind == TASK_SPORADIC ? 'S' : 'P', t->node, moved ? '*' : ' ', t->cost, t->period, t->deadline,
                t->criticality, t->releases, t->completions, t->misses, t->ignored,
                t->response_min, average, t->response_max, average * state->tick_ms);
    }
    if (!any)
        fprintf(out, "  (sin tareas)\n");
    fprintf(out, "  En las tareas esporádicas (S), la respuesta es la latencia desde la liberación hasta terminar.\n");
    if (any_relocated)
        fprintf(out, "  * Tarea reubicada tras la caída de un nodo: sus estadísticas acumulan ambos nodos y\n"
                     "    la columna nodo muestra el último. C es el costo en el nodo actual.\n");
    fputc('\n', out);
}

static void write_utilization(const report_state_t *state, FILE *out)
{
    for (int i = 0; i < HOST_NODE_COUNT; i++)
    {
        const report_node_t *n = &state->nodes[i];
        if (!n->last_tick && !n->theoretical_util && !n->speed_ut_per_tick)
            continue;
        double measured = n->last_tick ? (double)n->busy_ticks / n->last_tick * 100.0 : 0.0;
        const char *note = "";
        if (n->down)
            note = n->last_tick < 100
                       ? " [nodo caído: muestra demasiado corta, no representativa]"
                       : " [nodo caído: valores hasta la falla]";
        fprintf(out, "  Nodo %d: teórica %.1f%% | medida %.1f%% (%u de %u ticks ocupados)%s\n",
                i + 1, n->theoretical_util * 100.0, measured, n->busy_ticks, n->last_tick, note);
    }
    fprintf(out, "  La medida cubre hasta el último tick observado de cada nodo.\n\n");
}

static void write_switches(const report_state_t *state, FILE *out)
{
    for (int i = 0; i < HOST_NODE_COUNT; i++)
    {
        const report_node_t *n = &state->nodes[i];
        if (!n->last_tick && !n->switches)
            continue;
        fprintf(out, "  Nodo %d: %u cambios de tarea (incluye el paso a inactivo)", i + 1, n->switches);
        if (n->bad_frames_host || n->bad_frames_node)
            fprintf(out, " | tramas inválidas: %u en el host, %u vistas por el nodo",
                    n->bad_frames_host, n->bad_frames_node);
        if (n->segments_dropped)
            fprintf(out, " | %u tramos de Gantt omitidos por el tope", n->segments_dropped);
        fputc('\n', out);
    }
    fputc('\n', out);
}

static void write_decisions(const report_state_t *state, FILE *out)
{
    unsigned accepted = 0, rejected = 0;
    for (size_t i = 0; i < state->decision_count; i++)
    {
        const report_decision_t *d = &state->decisions[i];
        if (d->accepted)
            accepted++;
        else
            rejected++;
        fprintf(out, "  Tarea %d: %s", d->task_id,
                d->accepted ? (d->confirmed ? "ACEPTADA y confirmada" : "ACEPTADA (pendiente de confirmar)")
                            : "RECHAZADA");
        if (d->accepted)
            fprintf(out, " en el nodo %d", d->node);
        fprintf(out, " - %s\n", d->reason);
    }
    if (!state->decision_count)
        fprintf(out, "  (sin decisiones de admisión)\n");
    if (state->decisions_dropped)
        fprintf(out, "  ... se omitieron %u decisiones posteriores\n", state->decisions_dropped);
    fprintf(out, "  Total de decisiones: %u aceptadas, %u rechazadas.\n", accepted, rejected);
}

int report_write(const report_state_t *state, FILE *out)
{
    write_header(state, out);
    fprintf(out, "1. DIAGRAMA DE GANTT\n--------------------\n");
    for (int i = 0; i < HOST_NODE_COUNT; i++)
        if (state->nodes[i].last_tick || state->nodes[i].segment_count)
            write_gantt_node(state, i, out);
    fprintf(out, "2. LIBERACIONES, EJECUCIÓN, PLAZOS Y FALLAS\n-------------------------------------------\n");
    write_timeline(state, out);
    write_execution(state, out);
    fprintf(out, "3. ESTADÍSTICAS POR TAREA\n-------------------------\n");
    write_task_stats(state, out);
    fprintf(out, "4. UTILIZACIÓN POR NODO\n-----------------------\n");
    write_utilization(state, out);
    fprintf(out, "5. CAMBIOS DE TAREA\n-------------------\n");
    write_switches(state, out);
    fprintf(out, "6. TAREAS ACEPTADAS Y RECHAZADAS\n--------------------------------\n");
    write_decisions(state, out);
    return ferror(out) ? -1 : 0;
}

int report_write_gantt_csv(const report_state_t *state, int node_index, FILE *out)
{
    if (node_index < 0 || node_index >= HOST_NODE_COUNT)
        return -1;
    const report_node_t *node = &state->nodes[node_index];
    fprintf(out, "tarea,tick_inicio,tick_fin\n");
    for (size_t i = 0; i < node->segment_count; i++)
    {
        const report_segment_t *s = &node->segments[i];
        if (s->task == TASK_NONE)
            fprintf(out, "idle,%u,%u\n", s->start, s->end);
        else
            fprintf(out, "%u,%u,%u\n", s->task, s->start, s->end);
    }
    return ferror(out) ? -1 : 0;
}

/* Diagrama de Gantt completo de un nodo en SVG (se abre en cualquier navegador).
 * Cada tarea tiene una fila; los tramos de ejecución son rectángulos. Marcas:
 * triángulo = liberación, X roja = plazo perdido, círculo naranja = esporádica
 * ignorada, línea roja discontinua = nodo caído. */
static int report_write_gantt_svg(const report_state_t *state, int node_index, FILE *out)
{
    if (node_index < 0 || node_index >= HOST_NODE_COUNT)
        return -1;
    const report_node_t *node = &state->nodes[node_index];
    const int node_id = node_index + 1;
    const int left = 70, top = 46, row_h = 24;

    int row_of[REPORT_MAX_TASKS];
    int rows = 0;
    for (int i = 0; i < REPORT_MAX_TASKS; i++)
        row_of[i] = -1;
    for (int i = 0; i < REPORT_MAX_TASKS - 1; i++)
        if (state->tasks[i].used && state->tasks[i].accepted && state->tasks[i].node == node_id)
            row_of[i] = 0;
    for (size_t s = 0; s < node->segment_count; s++)
        if (node->segments[s].task != TASK_NONE)
            row_of[node->segments[s].task] = 0;
    for (size_t l = 0; l < state->log_count; l++)
        if (state->log[l].node == node_id && state->log[l].task != TASK_NONE)
            row_of[state->log[l].task] = 0;
    for (int i = 0; i < REPORT_MAX_TASKS; i++)
        if (row_of[i] == 0)
            row_of[i] = rows++;

    uint32_t total = node->last_tick ? node->last_tick : 1;
    double px = total <= 1000 ? 4.0 : (total <= 4000 ? 2.0 : 1.0);
    int width = left + (int)(total * px) + 30;
    int height = top + rows * row_h + 60;
    uint32_t step = (uint32_t)(50.0 / px / 10.0 + 0.999) * 10;
    if (step < 10)
        step = 10;

    fprintf(out, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" "
                 "viewBox=\"0 0 %d %d\" font-family=\"sans-serif\" font-size=\"11\">\n",
            width, height, width, height);
    fprintf(out, "<rect width=\"%d\" height=\"%d\" fill=\"#ffffff\"/>\n", width, height);
    fprintf(out, "<text x=\"%d\" y=\"18\" font-size=\"14\" font-weight=\"bold\">Nodo %d - %s, %s, "
                 "%u ticks observados (tick = %d ms)</text>\n",
            left, node_id, algorithm_name(state->algorithm), heuristic_name(state->heuristic),
            node->last_tick, state->tick_ms);

    for (uint32_t t = 0; t <= total; t += step)
    {
        double x = left + t * px;
        fprintf(out, "<line x1=\"%.1f\" y1=\"%d\" x2=\"%.1f\" y2=\"%d\" stroke=\"#dddddd\"/>\n", x,
                top - 6, x, top + rows * row_h);
        fprintf(out, "<text x=\"%.1f\" y=\"%d\" text-anchor=\"middle\" fill=\"#555555\">%u</text>\n", x,
                top - 10, t);
    }
    for (int id = 0; id < REPORT_MAX_TASKS; id++)
    {
        if (row_of[id] < 0)
            continue;
        int y = top + row_of[id] * row_h;
        fprintf(out, "<line x1=\"%d\" y1=\"%d\" x2=\"%.1f\" y2=\"%d\" stroke=\"#eeeeee\"/>\n", left,
                y + row_h, left + total * px, y + row_h);
        fprintf(out, "<text x=\"%d\" y=\"%d\" text-anchor=\"end\">T%d%s</text>\n", left - 8,
                y + row_h / 2 + 4, id,
                state->tasks[id].kind == TASK_SPORADIC && state->tasks[id].used ? " (S)" : "");
    }
    for (size_t s = 0; s < node->segment_count; s++)
    {
        const report_segment_t *seg = &node->segments[s];
        if (seg->task == TASK_NONE || row_of[seg->task] < 0)
            continue;
        fprintf(out, "<rect x=\"%.1f\" y=\"%d\" width=\"%.1f\" height=\"%d\" fill=\"hsl(%d,65%%,52%%)\">"
                     "<title>T%u: ticks %u-%u</title></rect>\n",
                left + seg->start * px, top + row_of[seg->task] * row_h + 4,
                (seg->end - seg->start) * px, row_h - 8, (seg->task * 47) % 360, seg->task,
                seg->start, seg->end - 1);
    }
    for (size_t l = 0; l < state->log_count; l++)
    {
        const report_log_t *e = &state->log[l];
        if (e->node != node_id || e->task == TASK_NONE || row_of[e->task] < 0)
            continue;
        double x = left + e->tick * px;
        int y = top + row_of[e->task] * row_h;
        if (e->kind == REPORT_LOG_RELEASE)
            fprintf(out, "<polygon points=\"%.1f,%d %.1f,%d %.1f,%d\" fill=\"#222222\"/>\n", x, y + 9,
                    x - 3, y + 1, x + 3, y + 1);
        else if (e->kind == REPORT_LOG_MISS)
            fprintf(out, "<path d=\"M%.1f %dL%.1f %dM%.1f %dL%.1f %d\" stroke=\"#d00000\" "
                         "stroke-width=\"2\"/>\n",
                    x - 4, y + 6, x + 4, y + row_h - 6, x + 4, y + 6, x - 4, y + row_h - 6);
        else if (e->kind == REPORT_LOG_SPORADIC_IGNORED)
            fprintf(out, "<circle cx=\"%.1f\" cy=\"%d\" r=\"4\" fill=\"#ff9800\"/>\n", x,
                    y + row_h / 2);
    }
    if (node->down)
    {
        double x = left + node->down_tick * px;
        fprintf(out, "<line x1=\"%.1f\" y1=\"%d\" x2=\"%.1f\" y2=\"%d\" stroke=\"#d00000\" "
                     "stroke-width=\"2\" stroke-dasharray=\"5,3\"/>\n",
                x, top - 6, x, top + rows * row_h);
        fprintf(out, "<text x=\"%.1f\" y=\"%d\" fill=\"#d00000\" font-weight=\"bold\">nodo caído "
                     "(tick %u)</text>\n",
                x + 4, top + rows * row_h + 14, node->down_tick);
    }
    fprintf(out, "<text x=\"%d\" y=\"%d\" fill=\"#333333\">Leyenda: rectángulo = ejecución | triángulo "
                 "negro = liberación | X roja = plazo perdido | círculo naranja = esporádica "
                 "ignorada | línea roja = nodo caído</text>\n",
            left, height - 10);
    fprintf(out, "</svg>\n");
    return ferror(out) ? -1 : 0;
}

/* ---------- Consola en vivo ---------- */

static const char *startup_phase_name(startup_phase_t phase)
{
    switch (phase)
    {
    case STARTUP_IDLE:
        return "pendiente";
    case STARTUP_CALIBRATING:
        return "calibrando";
    case STARTUP_PLANNING:
        return "planificando";
    case STARTUP_DEPLOYING:
        return "desplegando";
    case STARTUP_STARTING:
        return "iniciando";
    case STARTUP_RUNNING:
        return "en ejecución";
    case STARTUP_ABORTING:
        return "cancelando";
    case STARTUP_FAILED:
        return "fallido";
    }
    return "desconocido";
}

static const char *event_name(uint8_t type)
{
    switch (type)
    {
    case EVENT_RELEASE:
        return "liberación";
    case EVENT_SWITCH:
        return "cambio";
    case EVENT_COMPLETE:
        return "completada";
    case EVENT_DEADLINE_MISS:
        return "PLAZO PERDIDO";
    case EVENT_SPORADIC_IGNORED:
        return "esporádica ignorada";
    }
    return "desconocido";
}

static void print_startup(const startup_status_t *status)
{
    printf("[Arranque] Estado: %s.\n", startup_phase_name(status->phase));
    if (status->phase == STARTUP_RUNNING && status->timing_valid)
    {
        for (int i = 0; i < status->node_count; i++)
        {
            startup_timing_t t = status->nodes[i].start;
            printf("[Arranque] Nodo %d: %u UT/tick, tareas=%u/%u; "
                   "START host tx_inicio=%llu tx_fin=%llu ack=%llu ns; "
                   "retraso respecto al objetivo=%.3f ms.\n",
                   i + 1, status->nodes[i].speed_ut_per_tick,
                   status->nodes[i].confirmed_tasks, status->nodes[i].planned_tasks,
                   (unsigned long long)t.tx_begin_ns, (unsigned long long)t.tx_end_ns,
                   (unsigned long long)t.ack_ns,
                   (double)(t.tx_begin_ns - status->target_start_ns) / 1e6);
        }
        if (status->node_count == 2)
            printf("[Arranque] Desfase de envío observado en host: %.3f ms; "
                   "intervalo estimado cero2-cero1: [%.3f, %.3f] ms.\n",
                   (double)status->tx_skew_ns / 1e6,
                   (double)status->zero_delta_min_ns / 1e6,
                   (double)status->zero_delta_max_ns / 1e6);
        else
            printf("[Arranque] Un nodo: no aplica desfase entre nodos.\n");
    }
    else if (status->phase == STARTUP_FAILED)
        fprintf(stderr, "[Arranque] Fallo en %s: nodo=%d solicitud=%u errno=%d.\n",
                startup_phase_name(status->failed_phase), status->failed_node,
                status->failed_request, status->error_code);
}

void report_print_live(const ipc_message_t *event)
{
    switch (event->type)
    {
    case IPC_STARTUP:
        print_startup(&event->payload.startup);
        break;
    case IPC_ASSIGNMENT:
        printf("[Reporte] Tarea %d: %s, nodo=%d, C=%d; %.128s\n",
               event->payload.assignment.task_id,
               event->payload.assignment.accepted
                   ? (event->payload.assignment.confirmed ? "confirmada" : "admitida, pendiente")
                   : "rechazada",
               event->node_id, event->payload.assignment.cost_ticks,
               event->payload.assignment.reason);
        break;
    case IPC_EVENT:
        printf("[Reporte] Nodo %d: %s tarea=%u tick=%u aux=%u\n", event->node_id,
               event_name(event->payload.event.event_type), event->payload.event.task_id,
               event->payload.event.tick, event->payload.event.aux);
        break;
    case IPC_TICK:
        printf("[Reporte] Nodo %d: tick=%u tramas inválidas=%u\n", event->node_id,
               event->payload.tick.tick, event->payload.tick.bad_frames);
        break;
    case IPC_BAD_FRAME:
        fprintf(stderr, "[Reporte] Nodo %d: trama corrupta descartada (%.128s)\n", event->node_id,
                event->payload.error.reason);
        break;
    case IPC_ERROR:
    case IPC_NODE_DOWN:
        fprintf(stderr, "[Reporte] Nodo %d: %.128s\n", event->node_id, event->payload.error.reason);
        break;
    default:
        break;
    }
}

/* ---------- Proceso de reporte ---------- */

static volatile sig_atomic_t terminate_requested;

static void handle_terminate(int signal_number)
{
    (void)signal_number;
    terminate_requested = 1;
}

static int save_report(const report_state_t *state, const char *directory)
{
    if (mkdir(directory, 0755) < 0 && errno != EEXIST)
    {
        perror("[Reporte] No se pudo crear el directorio del reporte");
        return -1;
    }
    char stamp[32], path[512];
    time_t now = time(NULL);
    struct tm local;
    if (!localtime_r(&now, &local) || !strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local))
        snprintf(stamp, sizeof(stamp), "%ld", (long)now);

    snprintf(path, sizeof(path), "%s/reporte_%s.txt", directory, stamp);
    FILE *out = fopen(path, "w");
    if (!out)
    {
        perror("[Reporte] No se pudo abrir el archivo del reporte");
        return -1;
    }
    int result = report_write(state, out);
    if (fclose(out) != 0)
        result = -1;
    if (result < 0)
    {
        fprintf(stderr, "[Reporte] Error escribiendo %s\n", path);
        return -1;
    }
    printf("[Reporte] Reporte guardado en %s\n", path);

    for (int i = 0; i < HOST_NODE_COUNT; i++)
    {
        if (!state->nodes[i].segment_count)
            continue;
        snprintf(path, sizeof(path), "%s/gantt_nodo%d_%s.csv", directory, i + 1, stamp);
        out = fopen(path, "w");
        if (!out)
        {
            perror("[Reporte] No se pudo abrir el CSV de Gantt");
            result = -1;
            continue;
        }
        int csv = report_write_gantt_csv(state, i, out);
        if (fclose(out) != 0 || csv < 0)
            result = -1;

        snprintf(path, sizeof(path), "%s/gantt_nodo%d_%s.svg", directory, i + 1, stamp);
        out = fopen(path, "w");
        if (!out)
        {
            perror("[Reporte] No se pudo abrir el SVG de Gantt");
            result = -1;
            continue;
        }
        int svg = report_write_gantt_svg(state, i, out);
        if (fclose(out) != 0 || svg < 0)
            result = -1;
    }
    for (int i = 0; i < HOST_NODE_COUNT; i++)
    {
        const report_node_t *n = &state->nodes[i];
        if (!n->last_tick)
            continue;
        const char *note = "";
        if (n->down)
            note = n->last_tick < 100 ? " (nodo caído: muestra corta)" : " (nodo caído)";
        printf("[Reporte] Nodo %d: utilización teórica %.1f%%, medida %.1f%%, %u cambios de tarea%s\n",
               i + 1, n->theoretical_util * 100.0, (double)n->busy_ticks / n->last_tick * 100.0,
               n->switches, note);
    }
    return result;
}

int report_run(shared_memory_t *memory, const HostConfig *config, const char *directory)
{
    /* Estático: el estado ocupa cientos de KB y no debe vivir en la pila */
    static report_state_t state;
    report_init(&state, config);

    /* Sin SA_RESTART para que la señal interrumpa la espera; aun así se usa un
     * plazo corto por si llega entre la comprobación de la bandera y la espera. */
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_terminate;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);

    int result = 0;
    while (!terminate_requested)
    {
        ipc_message_t message;
        if (shared_memory_receive(memory, SHARED_QUEUE_EVENT, 0, &message, 500) < 0)
        {
            if (errno == EINTR || errno == ETIMEDOUT)
                continue;
            perror("[Reporte] Error leyendo eventos");
            result = -1;
            break;
        }
        if (message.type == IPC_SHUTDOWN)
            break;
        report_print_live(&message);
        report_consume(&state, &message);
    }
    /* Incluso tras un error o una terminación forzada se guarda lo acumulado */
    report_finalize(&state);
    if (save_report(&state, directory) < 0)
        result = -1;
    return result;
}