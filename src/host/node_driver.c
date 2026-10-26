#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "host/node_driver.h"
#include "host/shared_memory.h"
#include "protocol/protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

static int setup_tty_device(const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    struct termios tty;
    if (tcgetattr(fd, &tty) < 0) goto fail;
    cfmakeraw(&tty);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    if (cfsetispeed(&tty, B115200) < 0 || cfsetospeed(&tty, B115200) < 0 ||
        tcsetattr(fd, TCSANOW, &tty) < 0) goto fail;
    return fd;
fail:;
    int saved = errno;
    close(fd);
    errno = saved;
    return -1;
}

static int enqueue_command(ring_buffer_t *tx, const ipc_message_t *command, uint8_t *wire_type)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE], frame[PROTOCOL_MAX_FRAME_SIZE];
    int len;
    switch (command->type) {
    case IPC_CALIBRATE:
        *wire_type = MSG_CALIB_REQ;
        len = msg_calib_req_pack(&command->payload.calibrate, load, sizeof(load));
        break;
    case IPC_DEPLOY:
        *wire_type = MSG_TASK_DEPLOY;
        len = msg_task_deploy_pack(&command->payload.deploy, load, sizeof(load));
        break;
    case IPC_START:
        *wire_type = MSG_START;
        len = msg_start_pack(&command->payload.start, load, sizeof(load));
        break;
    case IPC_SPORADIC:
        *wire_type = MSG_SPORADIC;
        len = msg_sporadic_pack(&command->payload.sporadic, load, sizeof(load));
        break;
    case IPC_STOP:
        *wire_type = MSG_STOP;
        len = 0;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    if (len < 0) { errno = EINVAL; return -1; }
    len = protocol_frame_build(frame, sizeof(frame), *wire_type, load, (size_t)len);
    /* Insertar la trama completa evita dejar un prefijo truncado si falta espacio. */
    if (len < 0 || !ring_buffer_push(tx, frame, (size_t)len)) {
        errno = ENOBUFS;
        return -1;
    }
    return 0;
}

static int receive_frame(shared_memory_t *memory, int node_id, const protocol_frame_t *frame,
                         const ipc_message_t *pending, bool *waiting, uint8_t wire_type)
{
    ipc_message_t message = {.node_id = node_id};
    if (frame->type == MSG_EVENT) {
        message.type = IPC_EVENT;
        if (msg_event_unpack(frame->load, frame->load_len, &message.payload.event) != PROTOCOL_OK)
            return 0;
        if (shared_memory_record_event(memory, node_id, &message.payload.event) < 0) return -1;
        return shared_memory_send(memory, SHARED_QUEUE_EVENT, 0, &message, 1000);
    }
    if (frame->type == MSG_TICK_REPORT) {
        message.type = IPC_TICK;
        if (msg_tick_report_unpack(frame->load, frame->load_len, &message.payload.tick) != PROTOCOL_OK)
            return 0;
        if (shared_memory_record_tick_report(memory, node_id, &message.payload.tick) < 0) return -1;
        return shared_memory_send(memory, SHARED_QUEUE_EVENT, 0, &message, 1000);
    }
    if (!*waiting) return 0;
    message.request_id = pending->request_id;
    if (wire_type == MSG_CALIB_REQ && frame->type == MSG_CALIB_RESP) {
        message.type = IPC_CALIBRATED;
        if (msg_calib_resp_unpack(frame->load, frame->load_len,
                                 &message.payload.calibrated) != PROTOCOL_OK) return 0;
        if (shared_memory_set_node_active(memory, node_id, true) < 0) return -1;
    } else if (frame->type == MSG_ACK) {
        message.type = IPC_ACK;
        if (msg_ack_unpack(frame->load, frame->load_len, &message.payload.ack) != PROTOCOL_OK ||
            message.payload.ack.acked_type != wire_type) return 0;
        /* Una calibración rechazada llega como ACK; no tiene una velocidad utilizable. */
        if (wire_type == MSG_CALIB_REQ) {
            message.type = IPC_ERROR;
            message.payload.error.code = EPROTO;
            snprintf(message.payload.error.reason, IPC_REASON_LENGTH, "Calibración rechazada por el nodo");
        }
    } else return 0;
    if (shared_memory_send(memory, SHARED_QUEUE_RESPONSE, 0, &message, 1000) < 0) return -1;
    *waiting = false;
    return 0;
}

void node_driver_run(int node_id, const char *tty_device, ring_buffer_t *tx_rb,
                     const char *shm_name, const node_driver_config_t *config)
{
    if (!config || !tx_rb || node_id < 1 || node_id > SHARED_NUM_NODES) exit(EXIT_FAILURE);
    shared_memory_t memory;
    if (shared_memory_open(&memory, shm_name) < 0) exit(EXIT_FAILURE);
    int fd = setup_tty_device(tty_device);
    int result = EXIT_FAILURE;
    ipc_message_t pending = {0};
    if (fd < 0) goto done;
    protocol_parser_t parser;
    protocol_parser_init(&parser);
    ring_buffer_init(tx_rb);
    bool waiting = false, has_byte = false;
    uint8_t wire_type = 0, byte = 0;
    for (;;) {
        /* Solo una orden pendiente por nodo: el ACK serial identifica el tipo,
         * no la solicitud. La asociación request_id se conserva en este proceso. */
        if (!waiting && !has_byte && ring_buffer_is_empty(tx_rb)) {
            ipc_message_t command;
            int rc = shared_memory_receive(&memory, SHARED_QUEUE_COMMAND, node_id, &command, 0);
            if (rc == 0) {
                if (command.type == IPC_SHUTDOWN) { result = EXIT_SUCCESS; break; }
                pending = command;
                if (enqueue_command(tx_rb, &pending, &wire_type) < 0) goto done;
                waiting = true;
            } else if (errno != EAGAIN && errno != EINTR) goto done;
        }
        if (!has_byte) has_byte = ring_buffer_pop(tx_rb, &byte);
        fd_set reads, writes;
        FD_ZERO(&reads);
        FD_ZERO(&writes);
        FD_SET(fd, &reads);
        if (has_byte) FD_SET(fd, &writes);
        /* La espera permite revisar órdenes nuevas sin consumir CPU en un bucle vacío. */
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 20000};
        int ready = select(fd + 1, &reads, &writes, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            goto done;
        }
        if (FD_ISSET(fd, &writes)) {
            ssize_t n = write(fd, &byte, 1);
            if (n == 1) has_byte = false;
            else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) goto done;
            /* El byte permanece pendiente cuando write no lo pudo transmitir. */
        }
        if (FD_ISSET(fd, &reads)) {
            uint8_t bytes[128];
            ssize_t n = read(fd, bytes, sizeof(bytes));
            if (n == 0) { errno = EIO; goto done; }
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
                goto done;
            }
            for (ssize_t i = 0; i < n; i++) {
                protocol_frame_t frame;
                int rc = protocol_parser_feed(&parser, bytes[i], &frame);
                if (rc == PROTOCOL_FRAME_READY) {
                    if (receive_frame(&memory, node_id, &frame, &pending, &waiting, wire_type) < 0)
                        goto done;
                } else if (rc == PROTOCOL_ERR_CRC || rc == PROTOCOL_ERR_LEN) {
                    fprintf(stderr, "[Controlador %d] Trama corrupta descartada (%d).\n", node_id, rc);
                }
            }
        }
    }
done:;
    int saved = errno;
    if (result != EXIT_SUCCESS) {
        ipc_message_t error = {.type = IPC_NODE_DOWN, .node_id = node_id,
                               .request_id = pending.request_id};
        error.payload.error.code = saved;
        snprintf(error.payload.error.reason, IPC_REASON_LENGTH,
                 "Error de comunicación (errno=%d)", saved);
        if (shared_memory_send(&memory, SHARED_QUEUE_RESPONSE, 0, &error, 1000) < 0)
            perror("[Controlador] No se pudo informar la falla");
    }
    shared_memory_set_node_active(&memory, node_id, false);
    if (fd >= 0) close(fd);
    shared_memory_close(&memory);
    exit(result);
}
