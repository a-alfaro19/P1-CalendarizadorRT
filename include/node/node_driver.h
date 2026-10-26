#ifndef NODE_DRIVER_H
#define NODE_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "node/scheduler/calendarizador.h"
#include "protocol/protocol.h"

/**
 * @brief Hardware operations required by the portable node driver.
 *
 * The read callback returns 1 with a byte, 0 when no byte is available, or -1
 * on error. The write callback transmits the complete buffer and returns 0,
 * or returns -1 on error. Time is measured in wrapping microseconds.
 */
typedef struct
{
    void *context;
    int (*read_byte)(void *context, uint8_t *byte);
    int (*write_bytes)(void *context, const uint8_t *bytes, size_t length);
    uint32_t (*now_us)(void *context);
    uint32_t (*calibrate)(void *context, uint16_t tick_ms);
    void (*run_unit)(void *context);
    void (*apply_decision)(void *context, const decision_t *decision);
} node_driver_io_t;

/** Runtime state for the node protocol and scheduler. */
typedef struct
{
    node_driver_io_t io;
    protocol_parser_t parser;
    sched_t scheduler;
    uint16_t tick_ms;
    uint16_t bad_frames;
    uint32_t ut_per_tick;
    uint32_t tick_period_us;
    uint32_t next_tick_us;
    bool calibrated;
    bool started;
    bool stopped;
} node_driver_t;

/**
 * @brief Initializes the protocol parser and local scheduler.
 * @param driver Driver state to initialize.
 * @param io Board-specific I/O callbacks.
 * @return 0 on success, or -1 if required arguments or callbacks are invalid.
 */
int node_driver_init(node_driver_t *driver, const node_driver_io_t *io);

/**
 * @brief Processes received frames and runs any scheduler ticks that are due.
 *
 * Call frequently from the board's main loop.
 *
 * @param driver Initialized node driver.
 * @return 0 on success, or -1 if a hardware I/O callback fails.
 */
int node_driver_poll(node_driver_t *driver);

#ifdef __cplusplus
}
#endif

#endif