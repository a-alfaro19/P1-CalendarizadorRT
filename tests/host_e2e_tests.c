#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "protocol/protocol.h"
#include "test_harness.h"

/*
 * Prueba de extremo a extremo: ejecuta bin/host_app (requiere `make host`) con dos
 * nodos simulados por pseudoterminales, pide el cierre con "q" y verifica que el
 * host termine con código 0, escriba el reporte y libere la memoria compartida.
 */

#define NODES 2

typedef struct
{
    int master, keeper;
    char path[128];
    protocol_parser_t parser;
    bool started, stopped, corrupt_sent;
    int64_t started_ms;
    uint32_t reported_ticks;
} fake_node_t;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void write_frame(int fd, int type, const uint8_t *load, size_t length, bool corrupt)
{
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE];
    int len = protocol_frame_build(frame, sizeof(frame), type, load, length);
    assert(len > 0);
    if (corrupt)
        frame[len - 1] ^= 0xff;
    assert(write(fd, frame, (size_t)len) == len);
}

static void ack(int fd, int type)
{
    msg_ack_t message = {type, MSG_ACK_OK};
    uint8_t load[LOAD_SIZE_ACK];
    assert(msg_ack_pack(&message, load, sizeof(load)) == sizeof(load));
    write_frame(fd, MSG_ACK, load, sizeof(load), false);
}

static void send_event(int fd, uint32_t tick, uint8_t type, uint8_t task, uint16_t aux)
{
    msg_event_t event = {tick, type, task, aux};
    uint8_t load[LOAD_SIZE_EVENT];
    assert(msg_event_pack(&event, load, sizeof(load)) == sizeof(load));
    write_frame(fd, MSG_EVENT, load, sizeof(load), false);
}

static void send_tick_report(int fd, uint32_t tick)
{
    msg_tick_report_t report = {tick, 0};
    uint8_t load[LOAD_SIZE_TICK_REPORT];
    assert(msg_tick_report_pack(&report, load, sizeof(load)) == sizeof(load));
    write_frame(fd, MSG_TICK_REPORT, load, sizeof(load), false);
}

static void open_pty(fake_node_t *node)
{
    node->master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    assert(node->master >= 0 && grantpt(node->master) == 0 && unlockpt(node->master) == 0);
    assert(ptsname_r(node->master, node->path, sizeof(node->path)) == 0);
    /* Mantener el esclavo abierto evita EIO en el maestro mientras el host lo abre */
    node->keeper = open(node->path, O_RDWR | O_NOCTTY);
    assert(node->keeper >= 0);
    protocol_parser_init(&node->parser);
}

static void handle_frame(fake_node_t *node, const protocol_frame_t *frame)
{
    switch (frame->type)
    {
    case MSG_CALIB_REQ:
    {
        msg_calib_resp_t response = {50};
        uint8_t load[LOAD_SIZE_CALIB_RESP];
        assert(msg_calib_resp_pack(&response, load, sizeof(load)) == sizeof(load));
        write_frame(node->master, MSG_CALIB_RESP, load, sizeof(load), false);
        break;
    }
    case MSG_TASK_DEPLOY:
        ack(node->master, MSG_TASK_DEPLOY);
        break;
    case MSG_START:
        ack(node->master, MSG_START);
        node->started = true;
        node->started_ms = now_ms();
        /* Un tramo ejecutándose, un plazo perdido y una trama corrupta entre medio */
        send_event(node->master, 0, EVENT_RELEASE, 1, 0);
        send_event(node->master, 0, EVENT_SWITCH, 1, 0);
        write_frame(node->master, MSG_EVENT, (const uint8_t[LOAD_SIZE_EVENT]){0}, LOAD_SIZE_EVENT, true);
        send_event(node->master, 2, EVENT_COMPLETE, 1, 2);
        send_event(node->master, 2, EVENT_SWITCH, TASK_NONE, 0);
        send_event(node->master, 6, EVENT_DEADLINE_MISS, 1, 0);
        break;
    case MSG_STOP:
        ack(node->master, MSG_STOP);
        node->stopped = true;
        break;
    default:
        break;
    }
}

/* Atiende a los nodos; sin reportes de tick periódicos el host los declararía caídos */
static void pump(fake_node_t nodes[], int timeout_ms)
{
    struct pollfd fds[NODES];
    for (int i = 0; i < NODES; i++)
        fds[i] = (struct pollfd){.fd = nodes[i].master, .events = POLLIN};
    poll(fds, NODES, timeout_ms);
    for (int i = 0; i < NODES; i++)
    {
        uint8_t bytes[128];
        ssize_t n = (fds[i].revents & POLLIN) ? read(nodes[i].master, bytes, sizeof(bytes)) : 0;
        for (ssize_t b = 0; b < n; b++)
        {
            protocol_frame_t frame;
            if (protocol_parser_feed(&nodes[i].parser, bytes[b], &frame) == PROTOCOL_FRAME_READY)
                handle_frame(&nodes[i], &frame);
        }
        if (nodes[i].started && !nodes[i].stopped)
        {
            uint32_t due = (uint32_t)((now_ms() - nodes[i].started_ms) / 100);
            while (nodes[i].reported_ticks < due)
            {
                nodes[i].reported_ticks++;
                send_tick_report(nodes[i].master, nodes[i].reported_ticks * 10);
            }
        }
    }
}

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f && fputs(text, f) >= 0 && fclose(f) == 0);
}

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *text = calloc((size_t)size + 1, 1);
    assert(text && fread(text, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    return text;
}

/* Primer archivo del directorio cuyo nombre empieza con prefix; copia la ruta completa */
static bool find_file(const char *dir, const char *prefix, char *path, size_t capacity)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    struct dirent *entry;
    bool found = false;
    while (!found && (entry = readdir(d)))
        if (!strncmp(entry->d_name, prefix, strlen(prefix)))
        {
            snprintf(path, capacity, "%s/%s", dir, entry->d_name);
            found = true;
        }
    closedir(d);
    return found;
}

static bool shm_exists(pid_t host)
{
    char path[64];
    snprintf(path, sizeof(path), "/dev/shm/calendarizador_%ld", (long)host);
    return access(path, F_OK) == 0;
}

/* El cierre puede pedirse con `q` o con Ctrl+C, que una terminal envía a todo el grupo */
static void run_scenario(bool use_sigint)
{
    char host_path[PATH_MAX];
    assert(realpath("bin/host_app", host_path) && "ejecute `make host` antes de esta prueba");

    char dir[] = "/tmp/host_e2e_XXXXXX";
    assert(mkdtemp(dir));
    fake_node_t nodes[NODES] = {0};
    for (int i = 0; i < NODES; i++)
        open_pty(&nodes[i]);

    char path[PATH_MAX], text[2 * PATH_MAX];
    snprintf(path, sizeof(path), "%s/tasks.txt", dir);
    write_text(path, "1,P,100,20,20,2\n2,P,50,10,10,1\n3,P,100,20,20,3\n");
    snprintf(text, sizeof(text),
             "algorithm=EDF\nheuristic=WORST_FIT\ntick_ms=10\nnode1_port=%s\nnode2_port=%s\n"
             "tick_report_period=10\nmax_missed_reports=3\ntask_file=%s\n",
             nodes[0].path, nodes[1].path, path);
    char config[PATH_MAX], log[PATH_MAX];
    snprintf(config, sizeof(config), "%s/config.txt", dir);
    snprintf(log, sizeof(log), "%s/host.log", dir);
    write_text(config, text);

    int input[2];
    assert(pipe(input) == 0);
    pid_t host = fork();
    assert(host >= 0);
    if (!host)
    {
        assert(chdir(dir) == 0);
        setpgid(0, 0);
        dup2(input[0], STDIN_FILENO);
        int out = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        close(input[0]);
        close(input[1]);
        close(out);
        execl(host_path, host_path, config, (char *)NULL);
        _exit(127);
    }
    close(input[0]);

    int64_t deadline = now_ms() + 30000;
    int64_t quit_at = 0;
    bool quit_sent = false;
    int status = 0;
    for (;;)
    {
        pump(nodes, 20);
        int64_t now = now_ms();
        assert(now < deadline && "el host no terminó a tiempo");
        if (nodes[0].started && nodes[1].started && !quit_at)
            quit_at = now + 1500;
        if (quit_at && now >= quit_at && !quit_sent)
        {
            if (use_sigint)
                assert(kill(-host, SIGINT) == 0);
            else
                assert(write(input[1], "q\n", 2) == 2);
            quit_sent = true;
        }
        if (waitpid(host, &status, WNOHANG) == host)
            break;
    }
    close(input[1]);
    assert(quit_sent && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(nodes[0].stopped && nodes[1].stopped);
    assert(!shm_exists(host));

    char reports[PATH_MAX], report[PATH_MAX], csv[PATH_MAX];
    snprintf(reports, sizeof(reports), "%s/reports", dir);
    assert(find_file(reports, "reporte_", report, sizeof(report)));
    assert(find_file(reports, "gantt_nodo", csv, sizeof(csv)));
    char *body = read_text(report);
    assert(body);
    assert(strstr(body, "1. DIAGRAMA DE GANTT") && strstr(body, "6. TAREAS ACEPTADAS Y RECHAZADAS"));
    assert(strstr(body, "PERDIÓ su plazo"));
    assert(strstr(body, "trama corrupta descartada"));
    assert(strstr(body, "ACEPTADA"));
    free(body);
    char *output = read_text(log);
    assert(output && strstr(output, "Reporte guardado en"));
    free(output);

    for (int i = 0; i < NODES; i++)
    {
        close(nodes[i].master);
        close(nodes[i].keeper);
    }
    /* Limpieza del directorio temporal creado por la prueba */
    char command[PATH_MAX + 16];
    snprintf(command, sizeof(command), "rm -rf '%s'", dir);
    assert(system(command) == 0);
}

static void test_quit_writes_report_and_cleans_up(void) { run_scenario(false); }
static void test_ctrl_c_writes_report_and_cleans_up(void) { run_scenario(true); }

int main(void)
{
    puts("Host extremo a extremo");
    RUN_TEST(test_quit_writes_report_and_cleans_up);
    RUN_TEST(test_ctrl_c_writes_report_and_cleans_up);
    puts("OK: All host end-to-end tests passed!");
    return 0;
}
