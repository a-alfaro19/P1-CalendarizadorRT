#include <stdint.h>
#include <stddef.h>

/* ---------- Helpers little-endian ---------- */
/** @brief Writes a 16-bit value in little-endian order (low byte first). */
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF); /* byte bajo primero */
    p[1] = (uint8_t)(v >> 8);
}

/** @brief Writes a 32-bit value in little-endian order (low byte first). */
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/** @brief Reads a 16-bit little-endian value from a byte buffer. */
static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/** @brief Reads a 32-bit little-endian value from a byte buffer. */
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
