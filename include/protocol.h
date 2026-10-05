#pragma once

#include <stdint.h>
#include <stddef.h>

/* ---------- Frame constants ---------- */
#define PROTOCOL_START_MARK 0x7E
#define PROTOCOL_MAX_LOAD_SIZE 32
#define PROTOCOL_HEADER_SIZE 3 /* START_MARK + TYPE + LOAD_LEN */
#define PROTOCOL_CRC_SIZE 2
#define PROTOCOL_MAX_FRAME_SIZE (PROTOCOL_HEADER_SIZE + PROTOCOL_MAX_LOAD_SIZE + PROTOCOL_CRC_SIZE)

/* ---------- Return codes ---------- */
#define PROTOCOL_OK 0
#define PROTOCOL_ERR_ARG -1 /* null pointer or very small buffer */
#define PROTOCOL_ERR_LEN -2 /* invalid LOAD len */
#define PROTOCOL_ERR_CRC -3 /* CRC doesn't match */

/* ---------- Message Types (field TYPE) ---------- */
typedef enum
{
    MSG_CALIB_REQ = 0x00,   /* host -> node */
    MSG_CALIB_RESP = 0x01,  /* node -> host */
    MSG_TASK_DEPLOY = 0x02, /* host -> node */
    MSG_START = 0x03,       /* host -> node */
    MSG_EVENT = 0x04,       /* node -> host */
    MSG_TICK_REPORT = 0x05, /* node -> host */
    MSG_SPORADIC = 0x06,    /* host -> node */
    MSG_STOP = 0x07,        /* host -> node */
    MSG_ACK = 0x08          /* node -> host */
} msg_type_t;

/* ---------- LOAD size on each message type ---------- */
#define LOAD_SIZE_CALIB_REQ 2
#define LOAD_SIZE_CALIB_RESP 4
#define LOAD_SIZE_TASK_DEPLOY 8
#define LOAD_SIZE_START 3
#define LOAD_SIZE_EVENT 8
#define LOAD_SIZE_TICK_REPORT 4
#define LOAD_SIZE_SPORADIC 1
#define LOAD_SIZE_STOP 0
#define LOAD_SIZE_ACK 2

/* ---------- Field values ---------- */
/* kind */
enum
{
    TASK_PERIODIC = 0,
    TASK_SPORADIC = 1
};

/* algorithm */
enum
{
    ALGORITHM_RMS = 0,
    ALGORITHM_EDF = 1
};

/* event_type  */
enum
{
    EVENT_RELEASE = 0,
    EVENT_SWITCH = 1,
    EVENT_COMPLETE = 2,
    EVENT_DEADLINE_MISS = 3,
    EVENT_SPORADIC_IGNORED = 4
};

/* status */
enum
{
    ACK_OK = 0,
    ACK_INVALID = 1,
    ACK_NO_SPACE = 2
};
#define TASK_NONE 0xFF /* task_id idle */

/* ---------- Decoded Messages ---------- */
typedef struct
{
    uint16_t tick_ms;
} msg_calib_req_t;

typedef struct
{
    uint32_t ut_per_tick;
} msg_calib_resp_t;

typedef struct
{
    uint8_t task_id;
    uint8_t kind;
    uint16_t cost_ticks;
    uint16_t period;
    uint16_t deadline;
} msg_task_deploy_t;

typedef struct
{
    uint8_t algorithm;
    uint16_t report_period;
} msg_start_t;

typedef struct
{
    uint32_t tick;
    uint8_t event_type;
    uint8_t task_id;
    uint16_t aux;
} msg_event_t;

typedef struct
{
    uint32_t tick;
} msg_tick_report_t;

typedef struct
{
    uint8_t task_id;
} msg_sporadic_t;

/* MSG_STOP has no LOAD, so it doesn't need a struct */

typedef struct
{
    uint8_t acked_type;
    uint8_t status;
} msg_ack_t;

/* ---------- CRC-16/CCITT-FALSE ---------- */
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

/* ---------- Frame layer ---------- */
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

/* ---------- Message layer ---------- */
/**
 * @brief Serializes a calibration request into its load bytes.
 *
 * Layout (little-endian): [0..1] tick_ms.
 *
 * @param message  Message to serialize.
 * @param load     Buffer where the load is written.
 * @param capacity Capacity of @p load in bytes (at least LOAD_SIZE_CALIB_REQ).
 *
 * @return The number of bytes written (LOAD_SIZE_CALIB_REQ), or
 *         PROTOCOL_ERR_ARG if a pointer is NULL or @p cap is too small.
 */
int msg_calib_req_pack(const msg_calib_req_t *message, uint8_t *load, size_t capacity);

/**
 * @brief Deserializes the load of a calibration request (0x00).
 *
 * @param load    Received load bytes.
 * @param len     Length of @p load. It must match LOAD_SIZE_CALIB_REQ exactly.
 * @param message Output message.
 *
 * @return PROTOCOL_OK on success, PROTOCOL_ERR_LEN if @p len is not the exact size
 *         of the message, or PROTOCOL_ERR_ARG if a pointer is NULL.
 *         On error @p message is left untouched.
 */
int msg_calib_req_unpack(const uint8_t *load, size_t len, msg_calib_req_t *message);

int msg_calib_resp_pack(const msg_calib_resp_t *message, uint8_t *load, size_t capacity);
int msg_calib_resp_unpack(const uint8_t *load, size_t len, msg_calib_resp_t *message);

int msg_task_deploy_pack(const msg_task_deploy_t *message, uint8_t *load, size_t capacity);
int msg_task_deploy_unpack(const uint8_t *load, size_t len, msg_task_deploy_t *message);

int msg_start_pack(const msg_start_t *message, uint8_t *load, size_t capacity);
int msg_start_unpack(const uint8_t *load, size_t len, msg_start_t *message);

int msg_event_pack(const msg_event_t *message, uint8_t *load, size_t capacity);
int msg_event_unpack(const uint8_t *load, size_t len, msg_event_t *message);

int msg_tick_report_pack(const msg_tick_report_t *message, uint8_t *load, size_t capacity);
int msg_tick_report_unpack(const uint8_t *load, size_t len, msg_tick_report_t *message);

int msg_sporadic_pack(const msg_sporadic_t *message, uint8_t *load, size_t capacity);
int msg_sporadic_unpack(const uint8_t *load, size_t len, msg_sporadic_t *message);

/* MSG_STOP has no LOAD, so it doesn't require a serialization/deserialization function */

int msg_ack_pack(const msg_ack_t *message, uint8_t *load, size_t capacity);
int msg_ack_unpack(const uint8_t *load, size_t len, msg_ack_t *message);
