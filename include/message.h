#pragma once

/**
 * @file message.h
 * @brief Message layer: what each message means and how its load is encoded.
 *
 * Every message type has a struct with the decoded fields and a pair of
 * functions: msg_*_pack() writes the struct into the load bytes and
 * msg_*_unpack() reads them back.
 */

#include <stdint.h>
#include <stddef.h>

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

/* ---------- Load size of each message type, in bytes ---------- */
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

/** Task kind (msg_task_deploy_t.kind). */
enum
{
    TASK_PERIODIC = 0,
    TASK_SPORADIC = 1
};

/** Scheduling algorithm (msg_start_t.algorithm). */
enum
{
    ALGORITHM_RMS = 0,
    ALGORITHM_EDF = 1
};

/** Event type (msg_event_t.event_type). */
enum
{
    EVENT_RELEASE = 0,         /**< A job was released. */
    EVENT_SWITCH = 1,          /**< Task switch: task_id is the task that now runs. */
    EVENT_COMPLETE = 2,        /**< A job finished: aux is its response time in ticks. */
    EVENT_DEADLINE_MISS = 3,   /**< A job missed its deadline and was aborted. */
    EVENT_SPORADIC_IGNORED = 4 /**< Sporadic event arrived before the minimum interval. */
};

/** Acknowledgement status (msg_ack_t.status). */
enum
{
    ACK_OK = 0,      /**< Message accepted. */
    ACK_INVALID = 1, /**< Invalid data. */
    ACK_NO_SPACE = 2 /**< The node has no room for another task. */
};

#define TASK_NONE 0xFF /* task_id meaning "no task" (idle node) */

/* ---------- Decoded messages ---------- */

/** 0x00 Calibration request (host -> node). */
typedef struct
{
    uint16_t tick_ms; /**< Tick duration in milliseconds. */
} msg_calib_req_t;

/** 0x01 Calibration response (node -> host). */
typedef struct
{
    uint32_t ut_per_tick; /**< Work units the node runs per tick (s_n). */
} msg_calib_resp_t;

/** 0x02 Task deployment (host -> node). */
typedef struct
{
    uint8_t task_id;     /**< Task identifier. */
    uint8_t kind;        /**< TASK_PERIODIC or TASK_SPORADIC. */
    uint16_t cost_ticks; /**< C(i, n): cost in ticks, computed by the host. */
    uint16_t period;     /**< T in ticks. */
    uint16_t deadline;   /**< D, relative deadline in ticks. */
} msg_task_deploy_t;

/** 0x03 Start (host -> node). */
typedef struct
{
    uint8_t algorithm;      /**< ALGORITHM_RMS or ALGORITHM_EDF. */
    uint16_t report_period; /**< Ticks between tick reports. */
} msg_start_t;

/** 0x04 Event (node -> host). */
typedef struct
{
    uint32_t tick;      /**< Ticks elapsed since the start message. */
    uint8_t event_type; /**< EVENT_* value. */
    uint8_t task_id;    /**< Task involved, or TASK_NONE. */
    uint16_t aux;       /**< Extra data, depends on event_type. */
} msg_event_t;

/** 0x05 Tick report (node -> host). */
typedef struct
{
    uint32_t tick; /**< Current tick of the node. */
} msg_tick_report_t;

/** 0x06 Sporadic task trigger (host -> node). */
typedef struct
{
    uint8_t task_id; /**< Sporadic task to release. */
} msg_sporadic_t;

/* 0x07 MSG_STOP has no load, so it has no struct and no pack/unpack. */

/** 0x08 Acknowledgement (node -> host). */
typedef struct
{
    uint8_t acked_type; /**< TYPE of the message being confirmed. */
    uint8_t status;     /**< ACK_* value. */
} msg_ack_t;

/* ---------- Serialization ----------
 * Every pack function returns the number of bytes written, or
 * PROTOCOL_ERR_ARG if a pointer is NULL or the buffer is too small.
 *
 * Every unpack function returns PROTOCOL_OK,
 * PROTOCOL_ERR_LEN if len is not the exact size of the message, or
 * PROTOCOL_ERR_ARG if a pointer is NULL.
 * On error the output message is left untouched.
 */

/**
 * @brief Serializes a calibration request (0x00) into its load bytes.
 *
 * Layout: [0..1] tick_ms.
 *
 * @param message   Message to serialize.
 * @param load      Buffer where the load is written.
 * @param capacity  Capacity of @p load in bytes (at least LOAD_SIZE_CALIB_REQ).
 *
 * @return LOAD_SIZE_CALIB_REQ, or PROTOCOL_ERR_ARG.
 */
int msg_calib_req_pack(const msg_calib_req_t *message, uint8_t *load, size_t capacity);

/**
 * @brief Deserializes the load of a calibration request (0x00).
 *
 * @param load     Received load bytes.
 * @param len      Length of @p load. It must be exactly LOAD_SIZE_CALIB_REQ.
 * @param message  Output message.
 *
 * @return PROTOCOL_OK, PROTOCOL_ERR_LEN or PROTOCOL_ERR_ARG.
 */
int msg_calib_req_unpack(const uint8_t *load, size_t len, msg_calib_req_t *message);

/**
 * @brief Serializes a calibration response (0x01) into its load bytes.
 *
 * Layout: [0..3] ut_per_tick.
 *
 * @param message   Message to serialize.
 * @param load      Buffer where the load is written.
 * @param capacity  Capacity of @p load in bytes (at least LOAD_SIZE_CALIB_RESP).
 *
 * @return LOAD_SIZE_CALIB_RESP, or PROTOCOL_ERR_ARG.
 */
int msg_calib_resp_pack(const msg_calib_resp_t *message, uint8_t *load, size_t capacity);

/**
 * @brief Deserializes the load of a calibration response (0x01).
 *
 * @param load     Received load bytes.
 * @param len      Length of @p load. It must be exactly LOAD_SIZE_CALIB_RESP.
 * @param message  Output message.
 *
 * @return PROTOCOL_OK, PROTOCOL_ERR_LEN or PROTOCOL_ERR_ARG.
 */
int msg_calib_resp_unpack(const uint8_t *load, size_t len, msg_calib_resp_t *message);

/**
 * @brief Serializes a task deployment (0x02) into its load bytes.
 *
 * Layout: [0] task_id, [1] kind, [2..3] cost_ticks, [4..5] period,
 *         [6..7] deadline.
 *
 * @param message   Message to serialize.
 * @param load      Buffer where the load is written.
 * @param capacity  Capacity of @p load in bytes (at least LOAD_SIZE_TASK_DEPLOY).
 *
 * @return LOAD_SIZE_TASK_DEPLOY, or PROTOCOL_ERR_ARG.
 */
int msg_task_deploy_pack(const msg_task_deploy_t *message, uint8_t *load, size_t capacity);

/**
 * @brief Deserializes the load of a task deployment (0x02).
 *
 * @param load     Received load bytes.
 * @param len      Length of @p load. It must be exactly LOAD_SIZE_TASK_DEPLOY.
 * @param message  Output message.
 *
 * @return PROTOCOL_OK, PROTOCOL_ERR_LEN or PROTOCOL_ERR_ARG.
 */
int msg_task_deploy_unpack(const uint8_t *load, size_t len, msg_task_deploy_t *message);

/**
 * @brief Serializes a start message (0x03) into its load bytes.
 *
 * Layout: [0] algorithm, [1..2] report_period.
 *
 * @param message   Message to serialize.
 * @param load      Buffer where the load is written.
 * @param capacity  Capacity of @p load in bytes (at least LOAD_SIZE_START).
 *
 * @return LOAD_SIZE_START, or PROTOCOL_ERR_ARG.
 */
int msg_start_pack(const msg_start_t *message, uint8_t *load, size_t capacity);

/**
 * @brief Deserializes the load of a start message (0x03).
 *
 * @param load     Received load bytes.
 * @param len      Length of @p load. It must be exactly LOAD_SIZE_START.
 * @param message  Output message.
 *
 * @return PROTOCOL_OK, PROTOCOL_ERR_LEN or PROTOCOL_ERR_ARG.
 */
int msg_start_unpack(const uint8_t *load, size_t len, msg_start_t *message);

/**
 * @brief Serializes an event (0x04) into its load bytes.
 *
 * Layout: [0..3] tick, [4] event_type, [5] task_id, [6..7] aux.
 *
 * @param message   Message to serialize.
 * @param load      Buffer where the load is written.
 * @param capacity  Capacity of @p load in bytes (at least LOAD_SIZE_EVENT).
 *
 * @return LOAD_SIZE_EVENT, or PROTOCOL_ERR_ARG.
 */
int msg_event_pack(const msg_event_t *message, uint8_t *load, size_t capacity);

/**
 * @brief Deserializes the load of an event (0x04).
 *
 * @param load     Received load bytes.
 * @param len      Length of @p load. It must be exactly LOAD_SIZE_EVENT.
 * @param message  Output message.
 *
 * @return PROTOCOL_OK, PROTOCOL_ERR_LEN or PROTOCOL_ERR_ARG.
 */
int msg_event_unpack(const uint8_t *load, size_t len, msg_event_t *message);

/**
 * @brief Serializes a tick report (0x05) into its load bytes.
 *
 * Layout: [0..3] tick.
 *
 * @param message   Message to serialize.
 * @param load      Buffer where the load is written.
 * @param capacity  Capacity of @p load in bytes (at least LOAD_SIZE_TICK_REPORT).
 *
 * @return LOAD_SIZE_TICK_REPORT, or PROTOCOL_ERR_ARG.
 */
int msg_tick_report_pack(const msg_tick_report_t *message, uint8_t *load, size_t capacity);

/**
 * @brief Deserializes the load of a tick report (0x05).
 *
 * @param load     Received load bytes.
 * @param len      Length of @p load. It must be exactly LOAD_SIZE_TICK_REPORT.
 * @param message  Output message.
 *
 * @return PROTOCOL_OK, PROTOCOL_ERR_LEN or PROTOCOL_ERR_ARG.
 */
int msg_tick_report_unpack(const uint8_t *load, size_t len, msg_tick_report_t *message);

/**
 * @brief Serializes a sporadic task trigger (0x06) into its load bytes.
 *
 * Layout: [0] task_id.
 *
 * @param message   Message to serialize.
 * @param load      Buffer where the load is written.
 * @param capacity  Capacity of @p load in bytes (at least LOAD_SIZE_SPORADIC).
 *
 * @return LOAD_SIZE_SPORADIC, or PROTOCOL_ERR_ARG.
 */
int msg_sporadic_pack(const msg_sporadic_t *message, uint8_t *load, size_t capacity);

/**
 * @brief Deserializes the load of a sporadic task trigger (0x06).
 *
 * @param load     Received load bytes.
 * @param len      Length of @p load. It must be exactly LOAD_SIZE_SPORADIC.
 * @param message  Output message.
 *
 * @return PROTOCOL_OK, PROTOCOL_ERR_LEN or PROTOCOL_ERR_ARG.
 */
int msg_sporadic_unpack(const uint8_t *load, size_t len, msg_sporadic_t *message);

/**
 * @brief Serializes an acknowledgement (0x08) into its load bytes.
 *
 * Layout: [0] acked_type, [1] status.
 *
 * @param message   Message to serialize.
 * @param load      Buffer where the load is written.
 * @param capacity  Capacity of @p load in bytes (at least LOAD_SIZE_ACK).
 *
 * @return LOAD_SIZE_ACK, or PROTOCOL_ERR_ARG.
 */
int msg_ack_pack(const msg_ack_t *message, uint8_t *load, size_t capacity);

/**
 * @brief Deserializes the load of an acknowledgement (0x08).
 *
 * @param load     Received load bytes.
 * @param len      Length of @p load. It must be exactly LOAD_SIZE_ACK.
 * @param message  Output message.
 *
 * @return PROTOCOL_OK, PROTOCOL_ERR_LEN or PROTOCOL_ERR_ARG.
 */
int msg_ack_unpack(const uint8_t *load, size_t len, msg_ack_t *message);
