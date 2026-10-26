#include "host/node_driver.h"
#include "protocol/protocol.h"
#include "host/shared_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <termios.h>

#define READ_BUF_SIZE 128 // Size of the buffer used for reading bytes from the TTY device.

/**
 * @brief Represents the protocol states for the host communication state machine.
 */
typedef enum
{
    DRIVER_STATE_IDLE = 0,            /**< Initial state: ready to request node calibration (0x00). */
    DRIVER_STATE_WAITING_CALIB,       /**< Waiting for the node's calibration response (0x01). */
    DRIVER_STATE_DEPLOYING_TASKS,     /**< State reserved for sequential task configurations (0x02). */
    DRIVER_STATE_START,               /**< Sending execution start command (0x03) to the node. */
    DRIVER_STATE_WAITING_TICK_REPORT, /**< Active runtime state: listening for heartbeats (0x05) and events (0x04). */
    DRIVER_STATE_SPORADIC_TASK,       /**< Injecting a sporadic task simulation trigger (0x06). */
    DRIVER_STATE_STOP,                /**< Ordering node termination or shutdown (0x07). */
    DRIVER_STATE_WAITING_TASK_ACK,    /**< Waiting for task deployment acknowledgment. */
    DRIVER_STATE_WAITING_START_ACK,   /**< Waiting for start acknowledgment. */
} driver_state_t;

/**
 * @brief Internal helper to build a protocol frame and push it into the transmission ring buffer.
 *
 * @param tx_rb Pointer to the transmission ring buffer where the frame bytes will be pushed.
 * @param msg_type The protocol message type identifier.
 * @param load Pointer to the payload buffer containing the message data to be packed.
 * @param load_len Size in bytes of the message payload.
 * @return true if the frame was successfully built and fully pushed to the ring buffer, false otherwise.
 */
static bool driver_send_frame_to_rb(ring_buffer_t *tx_rb, uint8_t msg_type, const uint8_t *load, size_t load_len)
{
    // Build frame
    uint8_t frame_buffer[PROTOCOL_MAX_FRAME_SIZE];
    int frame_len = protocol_frame_build(frame_buffer, sizeof(frame_buffer), msg_type, load, load_len);

    if (frame_len < 0)
    {
        fprintf(stderr, "[Driver Error] Failed to build frame type 0x%02X\n", msg_type);
        return false;
    }

    // Push bytes to Ring Buffer
    for (int i = 0; i < frame_len; i++)
    {
        if (!ring_buffer_push(tx_rb, &frame_buffer[i], 1))
        {
            fprintf(stderr, "[Driver Error] Ring buffer full while sending frame type 0x%02X\n", msg_type);
            return false;
        }
    }
    return true;
}

/**
 * @brief Host-side communication protocol state machine handler that manages node orchestration,
 *        message serialization, task deployment via shared memory, and state transitions.
 *
 * @param node_id Identifier of the current node driver process.
 * @param tx_rb Pointer to the transmission ring buffer where outgoing protocol frames are pushed.
 * @param state Pointer to the current driver state variable, which is updated during protocol execution.
 * @param parsed_frame Pointer to the incoming protocol frame received from the node (can be NULL for initial state triggers).
 * @param shared_memory Pointer to the mapped shared memory region used to query unassigned tasks.
 * @return None (void).
 */
static void handle_state_machine(int node_id,
                                 ring_buffer_t *tx_rb,
                                 driver_state_t *state,
                                 const protocol_frame_t *parsed_frame,
                                 shared_memory_t *shared_memory,
                                 const node_driver_config_t *config)
{
    switch (*state)
    {
    case DRIVER_STATE_IDLE:
        printf("[Host Driver %d] IDLE State: Sending calibration request to the node...\n", node_id);
        {
            // Build calibration request
            msg_calib_req_t req = {.tick_ms = config->tick_ms};
            uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
            int load_len = msg_calib_req_pack(&req, load, sizeof(load));

            // Send frame to the ring buffer
            if (driver_send_frame_to_rb(tx_rb, MSG_CALIB_REQ, load, (size_t)load_len))
            {
                *state = DRIVER_STATE_WAITING_CALIB;
            }
        }
        break;

    case DRIVER_STATE_WAITING_CALIB:
        // Wait for calibration response
        if (parsed_frame && parsed_frame->type == MSG_CALIB_RESP)
        {
            printf("[Host Driver %d] Calibration response received!\n", node_id);
            if (shared_memory_set_node_active(shared_memory, node_id, true) != 0)
                perror("[Driver Error] Could not update shared node state");
            *state = DRIVER_STATE_DEPLOYING_TASKS;
        }
        else if (parsed_frame)
        {
            fprintf(stderr,
                    "[Host Driver %d] Unexpected message while waiting for calibration: "
                    "expected 0x%02X, received 0x%02X (length=%u).\n",
                    node_id,
                    MSG_CALIB_RESP,
                    parsed_frame->type,
                    parsed_frame->load_len);
        }
        break;

    case DRIVER_STATE_DEPLOYING_TASKS:
        // Read unassigned tasks from shared memory
        task_config_t next_task;
        int task_result = shared_memory_read_next_task(shared_memory, node_id, &next_task);
        if (task_result < 0)
        {
            perror("[Driver Error] Could not read next task from shared memory");
            break;
        }

        if (task_result > 0)
        {
            printf("[Host Driver %d] Sending task to the node...\n", node_id);

            // Build task
            uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
            msg_task_deploy_t deploy_msg = {
                .task_id = next_task.task_id,
                .kind = next_task.kind,
                .cost_ticks = next_task.cost_ticks,
                .period = next_task.period,
                .deadline = next_task.deadline};
            int load_len = msg_task_deploy_pack(&deploy_msg, load, sizeof(load));

            // Send frame to ring buffer
            if (driver_send_frame_to_rb(tx_rb, MSG_TASK_DEPLOY, load, (size_t)load_len))
            {
                if (shared_memory_mark_task_assigned(shared_memory,
                                                     node_id,
                                                     next_task.task_id) != 0)
                {
                    perror("[Driver Error] Could not mark task as assigned");
                    break;
                }
                *state = DRIVER_STATE_WAITING_TASK_ACK;
            }
        }
        else
        {
            printf("[Host Driver %d] All tasks have been sent successfully to the node.\n", node_id);
            *state = DRIVER_STATE_START;
        }

        break;

    case DRIVER_STATE_WAITING_TASK_ACK:
        if (parsed_frame && parsed_frame->type == 0x08) // MSG_ACK
        {
            msg_ack_t ack;
            if (msg_ack_unpack(parsed_frame->load, parsed_frame->load_len, &ack) == PROTOCOL_OK)
            {
                if (ack.acked_type != MSG_TASK_DEPLOY)
                {
                    fprintf(stderr,
                            "[Host Driver %d] Ignoring ACK for unexpected message 0x%02X; "
                            "waiting for task deployment ACK.\n",
                            node_id,
                            ack.acked_type);
                }
                else if (ack.status == MSG_ACK_OK)
                {
                    printf("[Host Driver %d] Task deployment confirmed with ACK (0x08).\n", node_id);
                    *state = DRIVER_STATE_DEPLOYING_TASKS;
                }
                else
                {
                    fprintf(stderr,
                            "[Host Driver %d] Task deployment rejected with status %u.\n",
                            node_id,
                            ack.status);
                }
            }
        }
        break;

    case DRIVER_STATE_WAITING_START_ACK:
        if (parsed_frame && parsed_frame->type == MSG_ACK)
        {
            msg_ack_t ack;
            if (msg_ack_unpack(parsed_frame->load, parsed_frame->load_len, &ack) == PROTOCOL_OK)
            {
                if (ack.acked_type != MSG_START)
                {
                    fprintf(stderr,
                            "[Host Driver %d] Ignoring ACK for unexpected message 0x%02X; "
                            "waiting for start ACK.\n",
                            node_id,
                            ack.acked_type);
                }
                else if (ack.status == MSG_ACK_OK)
                {
                    printf("[Host Driver %d] Start message confirmed with ACK (0x08).\n", node_id);
                    if (shared_memory_set_node_active(shared_memory, node_id, true) != 0)
                        perror("[Driver Error] Could not update shared node state");
                    *state = DRIVER_STATE_WAITING_TICK_REPORT;
                }
                else
                {
                    fprintf(stderr,
                            "[Host Driver %d] Start message rejected with status %u.\n",
                            node_id,
                            ack.status);
                }
            }
        }
        break;

    case DRIVER_STATE_START:
    {
        printf("[Host Driver %d] Sending start message to the node...\n", node_id);

        // Build start message
        uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
        msg_start_t start_msg = {
            .algorithm = config->algorithm,
            .report_period = config->report_period};
        int load_len = msg_start_pack(&start_msg, load, sizeof(load));

        // Send frame to ring buffer
        if (driver_send_frame_to_rb(tx_rb, 0x03, load, (size_t)load_len))
        {
            *state = DRIVER_STATE_WAITING_START_ACK;
        }
    }
    break;

    case DRIVER_STATE_WAITING_TICK_REPORT:
        if (parsed_frame)
        {
            if (parsed_frame->type == MSG_TICK_REPORT)
            {
                msg_tick_report_t report;
                if (msg_tick_report_unpack(parsed_frame->load,
                                           parsed_frame->load_len,
                                           &report) == PROTOCOL_OK &&
                    shared_memory_record_tick_report(shared_memory, node_id, &report) != 0)
                    perror("[Driver Error] Could not save tick report");
            }
            else if (parsed_frame->type == MSG_EVENT)
            {
                msg_event_t event;
                if (msg_event_unpack(parsed_frame->load,
                                     parsed_frame->load_len,
                                     &event) == PROTOCOL_OK)
                {
                    if (shared_memory_record_event(shared_memory, node_id, &event) != 0)
                        perror("[Driver Error] Could not save node event");

                    printf("[Host Driver %d] Event received: type=%u task=%u tick=%u aux=%u\n",
                           node_id,
                           event.event_type,
                           event.task_id,
                           event.tick,
                           event.aux);
                }
            }
        }
        break;

    case DRIVER_STATE_SPORADIC_TASK:
        // State or function to send a sporadic task trigger (0x06) simulating a button press
        {
            uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
            msg_sporadic_t sporadic_msg = {.task_id = 7};
            int load_len = msg_sporadic_pack(&sporadic_msg, load, sizeof(load));

            if (driver_send_frame_to_rb(tx_rb, 0x06, load, (size_t)load_len))
            {
                *state = DRIVER_STATE_WAITING_TICK_REPORT; // Return to monitoring
            }
        }
        break;

    case DRIVER_STATE_STOP:
        printf("[Host Driver %d] Ordering node shutdown (0x07)...\n", node_id);
        driver_send_frame_to_rb(tx_rb, 0x07, NULL, 0);
        break;
    }
}

/**
 * @brief Dispatches state machine actions in a loop until the state changes or no further actions are required.
 *
 * @param node_id Identifier of the current node driver process.
 * @param tx_rb Pointer to the transmission ring buffer where outgoing protocol frames are pushed.
 * @param state Pointer to the current driver state variable, which is updated during protocol execution.
 * @param shared_memory Pointer to the shared memory object for data exchange.
 * @param config Pointer to the node driver configuration structure.
 */
static void dispatch_state_actions(int node_id,
                                   ring_buffer_t *tx_rb,
                                   driver_state_t *state,
                                   shared_memory_t *shared_memory,
                                   const node_driver_config_t *config)
{
    for (int action = 0; action < 2; action++)
    {
        if (*state != DRIVER_STATE_DEPLOYING_TASKS &&
            *state != DRIVER_STATE_START)
            return;

        driver_state_t previous_state = *state;
        handle_state_machine(node_id,
                             tx_rb,
                             state,
                             NULL,
                             shared_memory,
                             config);
        if (*state == previous_state)
            return;
    }
}

/**
 * @brief Opens and maps the system shared memory object into the process's address space.
 *
 * @param shm_name The name string of the shared memory object.
 * @param out_size Pointer where the mapped memory size will be stored.
 * @return void* Pointer to the base address of the mapped shared memory region, or terminates execution on failure.
 */
static int setup_shared_memory(shared_memory_t *shared_memory, const char *shm_name)
{
    if (shared_memory_open(shared_memory, shm_name) != 0)
    {
        perror("[Error Driver] Could not open shared memory object");
        return -1;
    }

    return 0;
}

/**
 * @brief Opens and configures the TTY serial device in non-blocking read/write mode.
 *
 * @param tty_device Path string to the TTY device.
 * @return int File descriptor for the opened TTY device, or terminates execution on failure.
 */
static int setup_tty_device(const char *tty_device)
{
    int tty_fd = open(tty_device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (tty_fd < 0)
    {
        perror("[Error Driver] Could not open the TTY device");
        exit(EXIT_FAILURE);
    }

    struct termios tty;
    if (tcgetattr(tty_fd, &tty) != 0)
    {
        perror("[Error Driver] Could not read TTY settings");
        close(tty_fd);
        exit(EXIT_FAILURE);
    }

    cfmakeraw(&tty);
    if (cfsetispeed(&tty, B115200) != 0 ||
        cfsetospeed(&tty, B115200) != 0)
    {
        perror("[Error Driver] Could not set TTY baud rate");
        close(tty_fd);
        exit(EXIT_FAILURE);
    }
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    if (tcsetattr(tty_fd, TCSANOW, &tty) != 0)
    {
        perror("[Error Driver] Could not apply TTY settings");
        close(tty_fd);
        exit(EXIT_FAILURE);
    }

    return tty_fd;
}

/**
 * @brief Processes the RX channel by reading raw bytes from the TTY device, feeding the protocol parser,
 *        and advancing the state machine upon successfully decoding a valid frame.
 *
 * @param tty_fd File descriptor of the TTY serial device to read from.
 * @param node_id Identifier of the current node driver process.
 * @param tx_rb Pointer to the transmission ring buffer.
 * @param parser Pointer to the protocol parser instance.
 * @param current_state Pointer to the current driver state variable.
 * @param shared_memory Pointer to the mapped shared memory region.
 * @return true if the RX operation completed normally (or handled non-blocking states),
 *         false if a fatal read error occurred or the connection was closed by the node.
 */
static bool process_rx_channel(int tty_fd,
                               int node_id,
                               ring_buffer_t *tx_rb,
                               protocol_parser_t *parser,
                               driver_state_t *current_state,
                               shared_memory_t *shared_memory,
                               const node_driver_config_t *config)
{
    uint8_t read_buf[READ_BUF_SIZE];
    ssize_t bytes_read = read(tty_fd, read_buf, sizeof(read_buf));

    if (bytes_read < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            perror("[Driver Error] Read error on TTY");
            return false;
        }
    }
    else if (bytes_read == 0)
    {
        printf("[Node Driver %d] Connection closed by node.\n", node_id);
        return false;
    }
    else
    {
        protocol_frame_t parsed_frame;
        for (ssize_t i = 0; i < bytes_read; i++)
        {
            int result = protocol_parser_feed(parser, read_buf[i], &parsed_frame);

            if (result == PROTOCOL_FRAME_READY)
            {
                handle_state_machine(node_id,
                                     tx_rb,
                                     current_state,
                                     &parsed_frame,
                                     shared_memory,
                                     config);
                dispatch_state_actions(node_id,
                                       tx_rb,
                                       current_state,
                                       shared_memory,
                                       config);
            }
            else if (result == PROTOCOL_ERR_LEN || result == PROTOCOL_ERR_CRC)
            {
                fprintf(stderr, "[Node Driver %d] Warning: Corrupted frame detected.\n", node_id);
            }
        }
    }
    return true;
}

/**
 * @brief Processes the TX channel by popping a byte from the transmission ring buffer and writing it to the TTY device.
 *
 * @param tty_fd File descriptor of the TTY serial device to write to.
 * @param tx_rb Pointer to the transmission ring buffer.
 * @return true if the TX operation completed successfully (or handled non-blocking states),
 *         false if a fatal write error occurred.
 */
static bool process_tx_channel(int tty_fd, ring_buffer_t *tx_rb)
{
    uint8_t byte_to_send;
    if (ring_buffer_pop(tx_rb, &byte_to_send))
    {
        ssize_t written = write(tty_fd, &byte_to_send, 1);
        if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            perror("[Driver Error] Write error on TTY");
            return false;
        }
    }
    return true;
}

void node_driver_run(int node_id,
                     const char *tty_device,
                     ring_buffer_t *tx_rb,
                     const char *shm_name,
                     const node_driver_config_t *config)
{
    if (config == NULL || config->tick_ms == 0 || config->report_period == 0)
    {
        fprintf(stderr, "[Driver Error] Invalid node driver configuration.\n");
        exit(EXIT_FAILURE);
    }

    shared_memory_t shared_memory;
    if (setup_shared_memory(&shared_memory, shm_name) != 0)
        exit(EXIT_FAILURE);

    // Setup tty device
    int tty_fd = setup_tty_device(tty_device);

    // Initialize parser
    protocol_parser_t parser;
    protocol_parser_init(&parser);

    // Initialize state machine
    driver_state_t current_state = DRIVER_STATE_IDLE;
    handle_state_machine(node_id,
                         tx_rb,
                         &current_state,
                         NULL,
                         &shared_memory,
                         config);

    fd_set read_fds, write_fds;

    while (1)
    {
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        // Check if there is data to read from the TTY (RX)
        FD_SET(tty_fd, &read_fds);
        if (!ring_buffer_is_empty(tx_rb))
        {
            FD_SET(tty_fd, &write_fds);
        }

        // Block until the file descriptors are ready for reading or writing
        int activity = select(tty_fd + 1, &read_fds, &write_fds, NULL, NULL);
        if (activity < 0)
        {
            if (errno == EINTR) // Interrupted by a signal, retry
                continue;
            perror("[Error Driver] Error on select()");
            break;
        }

        // RECEPTION CHANNEL (RX): TTY -> Parser
        if (FD_ISSET(tty_fd, &read_fds))
        {
            if (!process_rx_channel(tty_fd,
                                    node_id,
                                    tx_rb,
                                    &parser,
                                    &current_state,
                                    &shared_memory,
                                    config))
            {
                break;
            }
        }

        // TRANSMISSION CHANNEL (TX): Ring Buffer -> TTY
        if (FD_ISSET(tty_fd, &write_fds))
        {
            if (!process_tx_channel(tty_fd, tx_rb))
            {
                break;
            }
        }
    }

    if (shared_memory_set_node_active(&shared_memory, node_id, false) != 0)
        perror("[Driver Error] Could not update shared node state");
    if (shared_memory_close(&shared_memory) != 0)
        perror("[Driver Error] Could not close shared memory");
    close(tty_fd);
    exit(EXIT_SUCCESS);
}
