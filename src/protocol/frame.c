#include "protocol/frame.h"
#include "protocol/protocol.h"
#include "protocol/byte_utils.h"

int protocol_frame_build(uint8_t *out, size_t capacity, uint8_t type,
                         const uint8_t *load, size_t load_len)
{
    // Validation
    if (out == NULL || (load_len > 0 && load == NULL))
        return PROTOCOL_ERR_ARG;
    if (load_len > PROTOCOL_MAX_LOAD_SIZE)
        return PROTOCOL_ERR_LEN;

    size_t total = PROTOCOL_HEADER_SIZE + load_len + PROTOCOL_CRC_SIZE;
    if (capacity < total)
        return PROTOCOL_ERR_ARG;

    // Writting
    out[0] = PROTOCOL_START_MARK;
    out[1] = type;
    out[2] = (uint8_t)load_len;
    if (load_len > 0)
        memcpy(out + PROTOCOL_HEADER_SIZE, load, load_len);

    /* CRC uses TYPE + LOAD_LEN + LOAD: pointer starts from out[1] */
    uint16_t crc = protocol_crc16(out + 1, 2 + load_len);
    put16(out + PROTOCOL_HEADER_SIZE + load_len, crc);

    return (int)total;
}
