#pragma once

/**
 * @file crc16.h
 * @brief CRC-16/CCITT-FALSE (polynomial 0x1021, initial value 0xFFFF).
 */

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Updates a running CRC-16/CCITT-FALSE with one more byte.
 *
 * Polynomial 0x1021. The first call must start from the initial value 0xFFFF.
 * Used by the parser, which receives the frame one byte at a time.
 *
 * @param crc   Current CRC value.
 * @param byte  Next byte to include in the computation.
 *
 * @return The updated CRC value.
 */
uint16_t protocol_crc16_update(uint16_t crc, uint8_t byte);

/**
 * @brief Computes the CRC-16/CCITT-FALSE of a whole buffer.
 *
 * @param data  Bytes to process.
 * @param len   Number of bytes in @p data.
 *
 * @return The CRC value.
 */
uint16_t protocol_crc16(const uint8_t *data, size_t len);
