#pragma once

/**
 * @file node_driver.h
 * @brief Node driver module for the host (handles TTY I/O, TX ring buffer flushing, and RX parsing).
 */

#include <stdint.h>
#include "host/ring_buffer.h"

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
 */
void node_driver_run(int node_id, const char *tty_device, ring_buffer_t *tx_rb);
