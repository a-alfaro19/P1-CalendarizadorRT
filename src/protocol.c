#include "protocol.h"
#include <string.h>

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

/* ---------- CRC-16/CCITT-FALSE (polynomial 0x1021, init 0xFFFF) ---------- */
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

/* ---------- Frame layer ---------- */
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

/* ---------- 0x00 Calibration request ---------- */
int msg_calib_req_pack(const msg_calib_req_t *message, uint8_t *load, size_t capacity)
{
    if (message == NULL || load == NULL || capacity < LOAD_SIZE_CALIB_REQ)
        return PROTOCOL_ERR_ARG;

    put16(load, message->tick_ms);

    return LOAD_SIZE_CALIB_REQ;
}

int msg_calib_req_unpack(const uint8_t *load, size_t len, msg_calib_req_t *message)
{
    if (load == NULL || message == NULL)
        return PROTOCOL_ERR_ARG;
    if (len != LOAD_SIZE_CALIB_REQ)
        return PROTOCOL_ERR_LEN;

    message->tick_ms = get16(load);

    return PROTOCOL_OK;
}

/* ---------- 0x01 Calibration response ---------- */
int msg_calib_resp_pack(const msg_calib_resp_t *message, uint8_t *load, size_t capacity)
{
    if (message == NULL || load == NULL || capacity < LOAD_SIZE_CALIB_RESP)
        return PROTOCOL_ERR_ARG;

    put32(load, message->ut_per_tick);

    return LOAD_SIZE_CALIB_RESP;
}

int msg_calib_resp_unpack(const uint8_t *load, size_t len, msg_calib_resp_t *message)
{
    if (load == NULL || message == NULL)
        return PROTOCOL_ERR_ARG;
    if (len != LOAD_SIZE_CALIB_RESP)
        return PROTOCOL_ERR_LEN;

    message->ut_per_tick = get32(load);

    return PROTOCOL_OK;
}

/* ---------- 0x02 Task deploy ----------
 * Fields positions: [0] task_id  [1] kind  [2..3] cost_ticks
 *                   [4..5] period  [6..7] deadline                */
int msg_task_deploy_pack(const msg_task_deploy_t *message, uint8_t *load, size_t capacity)
{
    if (message == NULL || load == NULL || capacity < LOAD_SIZE_TASK_DEPLOY)
        return PROTOCOL_ERR_ARG;

    load[0] = message->task_id;
    load[1] = message->kind;
    put16(load + 2, message->cost_ticks);
    put16(load + 4, message->period);
    put16(load + 6, message->deadline);

    return LOAD_SIZE_TASK_DEPLOY;
}

int msg_task_deploy_unpack(const uint8_t *load, size_t len, msg_task_deploy_t *message)
{
    if (load == NULL || message == NULL)
        return PROTOCOL_ERR_ARG;
    if (len != LOAD_SIZE_TASK_DEPLOY)
        return PROTOCOL_ERR_LEN;

    message->task_id = load[0];
    message->kind = load[1];
    message->cost_ticks = get16(load + 2);
    message->period = get16(load + 4);
    message->deadline = get16(load + 6);

    return PROTOCOL_OK;
}
