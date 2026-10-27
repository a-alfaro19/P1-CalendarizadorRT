#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host/report.h"
#include "test_harness.h"

/* El estado ocupa cientos de KB: estático para no depender del tamaño de la pila */
static report_state_t state;

static void fresh(void)
{
    HostConfig config = {.algorithm = SCHED_EDF, .heuristic = HEURISTIC_FIRST_FIT,
                         .tick_ms = 10, .tick_report_period = 10};
    report_init(&state, &config);
}

static void assignment(int task, int node, int accepted, int confirmed, int cost, Task def, const char *reason)
{
    ipc_message_t m = {.type = IPC_ASSIGNMENT, .node_id = accepted ? node : 0};
    m.payload.assignment.task_id = task;
    m.payload.assignment.accepted = accepted;
    m.payload.assignment.confirmed = confirmed;
    m.payload.assignment.cost_ticks = cost;
    ipc_assignment_set_task(&m, &def);
    snprintf(m.payload.assignment.reason, IPC_REASON_LENGTH, "%s", reason);
    report_consume(&state, &m);
}

static void event(int node, uint32_t tick, uint8_t type, uint8_t task, uint16_t aux)
{
    ipc_message_t m = {.type = IPC_EVENT, .node_id = node};
    m.payload.event = (msg_event_t){tick, type, task, aux};
    report_consume(&state, &m);
}

static void tick_report(int node, uint32_t tick, uint16_t bad)
{
    ipc_message_t m = {.type = IPC_TICK, .node_id = node};
    m.payload.tick = (msg_tick_report_t){tick, bad};
    report_consume(&state, &m);
}

static char *render(void)
{
    FILE *f = tmpfile();
    assert(f);
    assert(report_write(&state, f) == 0);
    long size = ftell(f);
    rewind(f);
    char *text = calloc((size_t)size + 1, 1);
    assert(text && fread(text, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    return text;
}

static void test_accumulates_gantt_stats_and_utilization(void)
{
    fresh();
    assignment(1, 1, 1, 1, 2, (Task){1, TASK_PERIODIC, 20, 10, 10, 2}, "ok");
    event(1, 0, EVENT_RELEASE, 1, 0);
    event(1, 0, EVENT_SWITCH, 1, 0);
    event(1, 2, EVENT_COMPLETE, 1, 2);
    event(1, 2, EVENT_SWITCH, TASK_NONE, 0);
    event(1, 10, EVENT_RELEASE, 1, 0);
    event(1, 10, EVENT_SWITCH, 1, 0);
    event(1, 12, EVENT_COMPLETE, 1, 2);
    event(1, 12, EVENT_SWITCH, TASK_NONE, 0);
    tick_report(1, 20, 0);
    report_finalize(&state);

    const report_node_t *node = &state.nodes[0];
    assert(node->switches == 4 && node->busy_ticks == 4 && node->last_tick == 20);
    assert(node->segment_count == 4);
    assert(node->segments[0].task == 1 && node->segments[0].start == 0 && node->segments[0].end == 2);
    assert(node->segments[3].task == TASK_NONE && node->segments[3].end == 20);
    const report_task_t *task = &state.tasks[1];
    assert(task->releases == 2 && task->completions == 2 && task->misses == 0);
    assert(task->response_min == 2 && task->response_max == 2 && task->response_sum == 4);
    assert(fabs(node->theoretical_util - 0.2) < 1e-9);

    char *text = render();
    assert(strstr(text, "1. DIAGRAMA DE GANTT") && strstr(text, "6. TAREAS ACEPTADAS Y RECHAZADAS"));
    assert(strstr(text, "T1   |##"));
    assert(strstr(text, "teórica 20.0% | medida 20.0%"));
    assert(strstr(text, "4 cambios de tarea"));
    free(text);

    FILE *csv = tmpfile();
    assert(csv && report_write_gantt_csv(&state, 0, csv) == 0);
    rewind(csv);
    char line[64];
    assert(fgets(line, sizeof(line), csv) && !strncmp(line, "tarea,", 6));
    assert(fgets(line, sizeof(line), csv) && !strcmp(line, "1,0,2\n"));
    fclose(csv);
}

static void test_deadline_miss_and_sporadic_are_reported(void)
{
    fresh();
    assignment(3, 1, 1, 1, 1, (Task){3, TASK_SPORADIC, 10, 50, 20, 3}, "ok");
    event(1, 5, EVENT_DEADLINE_MISS, 3, 0);
    event(1, 6, EVENT_SPORADIC_IGNORED, 3, 0);
    event(1, 30, EVENT_COMPLETE, 3, 4);
    report_finalize(&state);
    assert(state.tasks[3].misses == 1 && state.tasks[3].ignored == 1 && state.tasks[3].kind == TASK_SPORADIC);
    char *text = render();
    assert(strstr(text, "PERDIÓ su plazo") && strstr(text, "disparo esporádico ignorado"));
    assert(strstr(text, "latencia"));
    free(text);
}

static void test_node_failure_freezes_utilization_and_rejections_are_listed(void)
{
    fresh();
    assignment(1, 1, 1, 1, 5, (Task){1, TASK_PERIODIC, 50, 10, 10, 1}, "ok");
    assignment(2, 2, 1, 1, 2, (Task){2, TASK_PERIODIC, 20, 10, 10, 1}, "ok");
    tick_report(2, 100, 0);
    ipc_message_t down = {.type = IPC_NODE_DOWN, .node_id = 2};
    snprintf(down.payload.error.reason, IPC_REASON_LENGTH, "Timeout de reportes de tick");
    report_consume(&state, &down);
    report_consume(&state, &down); /* una segunda notificación no debe duplicar la falla */
    /* Reubicación al nodo 1 y rechazo de otra tarea tras la caída */
    assignment(2, 1, 1, 0, 4, (Task){2, TASK_PERIODIC, 20, 10, 10, 1}, "Reubicación admitida");
    assignment(9, 1, 0, 0, 0, (Task){9, TASK_PERIODIC, 90, 10, 10, 1}, "No pasa admisión");
    report_finalize(&state);

    assert(state.nodes[1].down && state.nodes[1].down_tick == 100);
    assert(fabs(state.nodes[1].theoretical_util - 0.2) < 1e-9); /* congelada en lo previo a la caída */
    assert(fabs(state.nodes[0].theoretical_util - 0.9) < 1e-9); /* 5/10 + 4/10 */
    assert(state.tasks[2].node == 1 && state.tasks[9].accepted == 0);
    char *text = render();
    assert(strstr(text, "FALLA: nodo declarado caído (Timeout de reportes de tick)"));
    assert(strstr(text, "Tarea 9: RECHAZADA") && strstr(text, "No pasa admisión"));
    assert(strstr(text, "nodo caído"));
    free(text);
    int failures = 0;
    for (size_t i = 0; i < state.log_count; i++)
        failures += state.log[i].kind == REPORT_LOG_NODE_DOWN;
    assert(failures == 1);
}

static void test_duplicate_id_rejection_keeps_running_task(void)
{
    fresh();
    assignment(1, 1, 1, 1, 2, (Task){1, TASK_PERIODIC, 20, 10, 10, 1}, "ok");
    assignment(1, 0, 0, 0, 0, (Task){1, TASK_PERIODIC, 20, 10, 10, 1}, "Ya existe una tarea con ese id");
    assert(state.tasks[1].accepted && state.tasks[1].node == 1);
    assert(state.decision_count == 2); /* la decisión sí queda registrada */
}

static void test_bounded_storage_and_invalid_input(void)
{
    fresh();
    for (uint32_t t = 0; t < 3 * REPORT_MAX_SEGMENTS; t++)
        event(1, t, EVENT_SWITCH, (uint8_t)(t % 2), 0);
    for (uint32_t t = 0; t < 2 * REPORT_MAX_LOG; t++)
        event(2, t, EVENT_RELEASE, 4, 0);
    report_finalize(&state);
    assert(state.nodes[0].segment_count == REPORT_MAX_SEGMENTS && state.nodes[0].segments_dropped > 0);
    /* Las estadísticas no dependen del tope de almacenamiento */
    assert(state.nodes[0].busy_ticks > REPORT_MAX_SEGMENTS);
    assert(state.log_count == REPORT_MAX_LOG && state.log_dropped == REPORT_MAX_LOG);
    assert(state.tasks[4].releases == 2 * REPORT_MAX_LOG);

    /* Nodo y tarea fuera de rango, eventos desconocidos y asignaciones absurdas */
    event(0, 1, EVENT_SWITCH, 1, 0);
    event(HOST_NODE_COUNT + 1, 1, EVENT_RELEASE, 1, 0);
    event(1, 1, 200, 255, 0);
    assignment(-5, 1, 1, 1, 1, (Task){0}, "x");
    assignment(300, 1, 1, 1, 1, (Task){0}, "x");
    ipc_message_t junk = {.type = IPC_BAD_FRAME, .node_id = 77};
    report_consume(&state, &junk);
    char *text = render();
    assert(strstr(text, "se omitieron"));
    free(text);
}

int main(void)
{
    puts("Report");
    RUN_TEST(test_accumulates_gantt_stats_and_utilization);
    RUN_TEST(test_deadline_miss_and_sporadic_are_reported);
    RUN_TEST(test_node_failure_freezes_utilization_and_rejections_are_listed);
    RUN_TEST(test_duplicate_id_rejection_keeps_running_task);
    RUN_TEST(test_bounded_storage_and_invalid_input);
    puts("OK: All report tests passed!");
    return 0;
}
