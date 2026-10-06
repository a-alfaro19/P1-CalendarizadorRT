#include "message.h"
#include "protocol.h"
#include "byte_utils.h"

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

/* ---------- 0x03 Start ----------
 * Positions: [0] algorithm  [1..2] report_period */
int msg_start_pack(const msg_start_t *message, uint8_t *load, size_t capacity)
{
    if (message == NULL || load == NULL || capacity < LOAD_SIZE_START)
        return PROTOCOL_ERR_ARG;

    load[0] = message->algorithm;
    put16(load + 1, message->report_period);

    return LOAD_SIZE_START;
}

int msg_start_unpack(const uint8_t *load, size_t len, msg_start_t *message)
{
    if (load == NULL || message == NULL)
        return PROTOCOL_ERR_ARG;
    if (len != LOAD_SIZE_START)
        return PROTOCOL_ERR_LEN;

    message->algorithm = load[0];
    message->report_period = get16(load + 1);

    return PROTOCOL_OK;
}

/* ---------- 0x04 Event ----------
 * Positions: [0..3] tick  [4] event_type  [5] task_id  [6..7] aux */
int msg_event_pack(const msg_event_t *message, uint8_t *load, size_t capacity)
{
    if (message == NULL || load == NULL || capacity < LOAD_SIZE_EVENT)
        return PROTOCOL_ERR_ARG;

    put32(load, message->tick);
    load[4] = message->event_type;
    load[5] = message->task_id;
    put16(load + 6, message->aux);

    return LOAD_SIZE_EVENT;
}

int msg_event_unpack(const uint8_t *load, size_t len, msg_event_t *message)
{
    if (load == NULL || message == NULL)
        return PROTOCOL_ERR_ARG;
    if (len != LOAD_SIZE_EVENT)
        return PROTOCOL_ERR_LEN;

    message->tick = get32(load);
    message->event_type = load[4];
    message->task_id = load[5];
    message->aux = get16(load + 6);

    return PROTOCOL_OK;
}

/* ---------- 0x05 Tick report ----------
 * Positions: [0..3] tick */
int msg_tick_report_pack(const msg_tick_report_t *message, uint8_t *load, size_t capacity)
{
    if (message == NULL || load == NULL || capacity < LOAD_SIZE_TICK_REPORT)
        return PROTOCOL_ERR_ARG;

    put32(load, message->tick);

    return LOAD_SIZE_TICK_REPORT;
}

int msg_tick_report_unpack(const uint8_t *load, size_t len, msg_tick_report_t *message)
{
    if (load == NULL || message == NULL)
        return PROTOCOL_ERR_ARG;
    if (len != LOAD_SIZE_TICK_REPORT)
        return PROTOCOL_ERR_LEN;

    message->tick = get32(load);

    return PROTOCOL_OK;
}

/* ---------- 0x06 Sporadic task trigger ----------
 * Positions: [0] task_id */
int msg_sporadic_pack(const msg_sporadic_t *message, uint8_t *load, size_t capacity)
{
    if (message == NULL || load == NULL || capacity < LOAD_SIZE_SPORADIC)
        return PROTOCOL_ERR_ARG;

    load[0] = message->task_id;

    return LOAD_SIZE_SPORADIC;
}

int msg_sporadic_unpack(const uint8_t *load, size_t len, msg_sporadic_t *message)
{
    if (load == NULL || message == NULL)
        return PROTOCOL_ERR_ARG;
    if (len != LOAD_SIZE_SPORADIC)
        return PROTOCOL_ERR_LEN;

    message->task_id = load[0];

    return PROTOCOL_OK;
}

/* ---------- 0x08 Acknowledgement ----------
 * Positions: [0] acked_type  [1] status */
int msg_ack_pack(const msg_ack_t *message, uint8_t *load, size_t capacity)
{
    if (message == NULL || load == NULL || capacity < LOAD_SIZE_ACK)
        return PROTOCOL_ERR_ARG;

    load[0] = message->acked_type;
    load[1] = message->status;

    return LOAD_SIZE_ACK;
}

int msg_ack_unpack(const uint8_t *load, size_t len, msg_ack_t *message)
{
    if (load == NULL || message == NULL)
        return PROTOCOL_ERR_ARG;
    if (len != LOAD_SIZE_ACK)
        return PROTOCOL_ERR_LEN;

    message->acked_type = load[0];
    message->status = load[1];

    return PROTOCOL_OK;
}
