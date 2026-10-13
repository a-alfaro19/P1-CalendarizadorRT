#pragma once

/**
 * @file ring_buffer.h
 * @brief Circular RAM buffer for asynchronous background transmission and decoupling.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RING_BUFFER_SIZE 256

typedef struct
{
    uint8_t buffer[RING_BUFFER_SIZE]; ///< Internal byte storage array.
    volatile int head;                ///< Write index (producer).
    volatile int tail;                ///< Read index (consumer).
    volatile int count;               ///< Current number of bytes stored in the buffer.
} ring_buffer_t;

/**
 * @brief Initializes an empty ring buffer.
 *
 * @param rb Pointer to the ring buffer structure to initialize.
 */
void ring_buffer_init(ring_buffer_t *rb);

/**
 * @brief Checks if the ring buffer is completely empty.
 *
 * @param rb Pointer to the ring buffer.
 *
 * @return true If the buffer contains no pending elements to read.
 * @return false If there is at least one byte available in the buffer.
 */
bool ring_buffer_is_empty(const ring_buffer_t *rb);

/**
 * @brief Gets the current number of bytes available to read in the ring buffer.
 *
 * @param rb Pointer to the ring buffer.
 * @return The number of bytes currently stored and ready to be read.
 */
size_t ring_buffer_available(const ring_buffer_t *rb);

/**
 * @brief Inserts a block of data (frame or bytes) into the ring buffer.
 *
 * @param rb Pointer to the ring buffer.
 * @param data Pointer to the byte array to be inserted.
 * @param len Number of bytes to insert.
 *
 * @return true If the buffer had enough space and the data was inserted successfully.
 * @return false If a buffer overflow occurs (not enough space available).
 */
bool ring_buffer_push(ring_buffer_t *rb, const uint8_t *data, size_t len);

/**
 * @brief Extracts a single byte from the ring buffer (consumer mode).
 *
 * @param rb Pointer to the ring buffer.
 * @param byte Pointer where the extracted byte will be stored if successful.
 *
 * @return true If a byte was successfully extracted.
 * @return false If the ring buffer is currently empty.
 */
bool ring_buffer_pop(ring_buffer_t *rb, uint8_t *byte);
