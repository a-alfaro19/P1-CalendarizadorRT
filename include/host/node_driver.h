#pragma once

/**
 * @file node_driver.h
 * @brief Node driver module for the host (handles TTY I/O, TX ring buffer flushing, and RX parsing).
 */

#include <stdint.h>
#include "host/ring_buffer.h"

/**
 * @brief Runtime settings for the host-side node driver.
 */
typedef struct
{
    uint16_t tick_ms; /**< Tick duration in milliseconds. */
    uint8_t algorithm; /**< Scheduling algorithm identifier. */
    uint16_t report_period; /**< Tick reports interval. */
} node_driver_config_t;

/**
 * @brief Main loop for the node driver process.
 *
 * This function runs inside the child process created by fork(). It manages
 * asynchronous transmission from the ring buffer to the TTY and processes
 * incoming bytes from the TTY using the protocol parser.
 *
 * @param node_id Numeric identifier of the node.
 * @param tty_device Path to the serial device file.
 * @param tx_rb Pointer to the ring buffer used for outbound traffic to this node.
 * @param shm_name Path or identifier string for the shared memory object.
 * @param config Driver settings loaded by the host.
 */
void node_driver_run(int node_id,
                     const char *tty_device,
                     ring_buffer_t *tx_rb,
                     const char *shm_name,
                     const node_driver_config_t *config);
