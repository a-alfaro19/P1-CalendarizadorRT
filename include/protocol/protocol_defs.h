#pragma once

/**
 * @file protocol_defs.h
 * @brief Return codes shared by every layer of the serial protocol.
 */

/* ---------- Return codes ---------- */
#define PROTOCOL_OK 0          /**< Success, or "more bytes are needed" for the parser. */
#define PROTOCOL_FRAME_READY 1 /**< The parser completed a valid frame. */
#define PROTOCOL_ERR_ARG -1    /**< NULL pointer or buffer too small. */
#define PROTOCOL_ERR_LEN -2    /**< Invalid LOAD length. */
#define PROTOCOL_ERR_CRC -3    /**< CRC doesn't match. */
