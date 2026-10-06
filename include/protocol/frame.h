#pragma once

/**
 * @file frame.h
 * @brief Frame layer: building frames.
 *
 * Frame format (multi-byte fields are little-endian):
 *
 *   | START_MARK | TYPE   | LOAD_LEN | LOAD        | CRC     |
 *   | 1 byte     | 1 byte | 1 byte   | 0..32 bytes | 2 bytes |
 *
 * The CRC (CRC-16/CCITT-FALSE) covers TYPE + LOAD_LEN + LOAD. START_MARK and
 * the CRC itself are not included.
 */

#include <stdint.h>
#include <stddef.h>

/* ---------- Frame constants ---------- */
#define PROTOCOL_START_MARK 0x7E
#define PROTOCOL_MAX_LOAD_SIZE 32
#define PROTOCOL_HEADER_SIZE 3 /* START_MARK + TYPE + LOAD_LEN */
#define PROTOCOL_CRC_SIZE 2
#define PROTOCOL_MAX_FRAME_SIZE (PROTOCOL_HEADER_SIZE + PROTOCOL_MAX_LOAD_SIZE + PROTOCOL_CRC_SIZE)

/** A received frame whose CRC has already been verified. */
typedef struct
{
    uint8_t type;
    uint8_t load_len;
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
} protocol_frame_t;

/**
 * @brief Builds a complete frame START_MARK | TYPE | LOAD_LEN | LOAD | CRC.
 *
 * @param out      Buffer where the frame is written.
 * @param capacity Capacity of @p out in bytes. It must be at least
 *                 PROTOCOL_HEADER_SIZE + load_len + PROTOCOL_CRC_SIZE
 *
 * @return The total frame length in bytes on success, or a negative code:
 *         - PROTOCOL_ERR_ARG: NULL pointer or @p cap too small.
 *         - PROTOCOL_ERR_LEN: @p load_len greater than PROTOCOL_MAX_LOAD.
 */
int protocol_frame_build(uint8_t *out, size_t capacity, uint8_t type,
                         const uint8_t *load, size_t load_len);
