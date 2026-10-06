#include "crc16.h"
#include "byte_utils.h"

uint16_t protocol_crc16_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)byte << 8;
    for (int i = 0; i < 8; i++)
    {
        if (crc & 0x8000)
            crc = (uint16_t)((crc << 1) ^ 0x1021);
        else
            crc = (uint16_t)(crc << 1);
    }
    return crc;
}

uint16_t protocol_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
        crc = protocol_crc16_update(crc, data[i]);
    return crc;
}
