#pragma once

/**
 * @file parser.h
 * @brief Receives frames from a byte stream, one byte at a time.
 *
 * Implemented as a state machine that advances one byte per call:
 *
 *   WAIT_START -> TYPE -> LOAD_LEN -> LOAD -> CRC_LOW -> CRC_HIGH
 *
 * A frame with an invalid LOAD_LEN or a wrong CRC is discarded and counted
 * in the statistics. The parser only checks that the frame arrived intact;
 * the meaning of the load is checked by the message layer (message.h).
 */

#include <stdint.h>
#include "frame.h"

/** Reception counters. They only grow; the owner can read them at any time. */
typedef struct
{
    uint32_t frames_ok;     /**< Valid frames received. */
    uint32_t crc_errors;    /**< Frames discarded because of a CRC mismatch. */
    uint32_t length_errors; /**< Frames discarded because LOAD_LEN > maximum. */
} protocol_stats_t;

/** Position of the parser inside the frame being received. */
typedef enum
{
    PARSER_WAIT_START, /**< Discarding bytes until START_MARK appears. */
    PARSER_TYPE,       /**< Next byte is TYPE. */
    PARSER_LOAD_LEN,   /**< Next byte is LOAD_LEN. */
    PARSER_LOAD,       /**< Next bytes are LOAD. */
    PARSER_CRC_LOW,    /**< Next byte is the low byte of the CRC. */
    PARSER_CRC_HIGH    /**< Next byte is the high byte of the CRC. */
} parser_state_t;

/** Parser state. Do not modify its fields directly, except reading stats. */
typedef struct
{
    parser_state_t state;
    uint8_t received;       /**< LOAD bytes received so far. */
    uint16_t crc;           /**< CRC computed over the bytes received. */
    uint16_t crc_received;  /**< CRC read from the wire. */
    protocol_frame_t frame; /**< Frame under construction. */
    protocol_stats_t stats; /**< Reception counters. */
} protocol_parser_t;

/**
 * @brief Initializes a parser: waiting for START_MARK, counters at zero.
 *
 * @param parser Parser to initialize. Nothing happens if it is NULL.
 */
void protocol_parser_init(protocol_parser_t *parser);

/**
 * @brief Feeds one received byte to the parser.
 *
 * @param parser  Parser state.
 * @param byte    Next received byte.
 * @param frame   Output. Written only when PROTOCOL_FRAME_READY is returned.
 *
 * @return PROTOCOL_FRAME_READY if a valid frame was completed and copied to @p frame;
 *         PROTOCOL_OK if more bytes are needed;
 *         PROTOCOL_ERR_LEN if LOAD_LEN exceeds PROTOCOL_MAX_LOAD_SIZE (frame discarded);
 *         PROTOCOL_ERR_CRC if the CRC does not match (frame discarded); or
 *         PROTOCOL_ERR_ARG if a pointer is NULL.
 */
int protocol_parser_feed(protocol_parser_t *parser, uint8_t byte, protocol_frame_t *frame);
