#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE], frame[PROTOCOL_MAX_FRAME_SIZE];

    /* 1. CRC test: standard check value for CRC-16/CCITT-FALSE.
     *    The ASCII string "123456789" must give 0x29B1. */
    assert(protocol_crc16((const uint8_t *)"123456789", 9) == 0x29B1);

    /* 2. Frame build test: calibration with tick = 10 ms. */
    msg_calib_req_t req = {.tick_ms = 10};
    int load_len = msg_calib_req_pack(&req, load, sizeof load);
    assert(load_len == 2 && load[0] == 0x0A && load[1] == 0x00);

    int frame_len = protocol_frame_build(frame, sizeof frame, MSG_CALIB_REQ, load, (size_t)load_len);
    const uint8_t expected[] = {0x7E, 0x00, 0x02, 0x0A, 0x00, 0x6B, 0x05};
    assert(frame_len == 7 && memcmp(frame, expected, 7) == 0);

    /* 3. Frame build test: message without LOAD (stop) -> 7E 07 00 CRC CRC. */
    frame_len = protocol_frame_build(frame, sizeof frame, MSG_STOP, NULL, 0);
    assert(frame_len == 5 && frame[2] == 0x00);

    /* 4. Round-trip tests: struct -> pack -> bytes -> unpack -> struct.
     *    The recovered message must be identical to the original. */

    /* 4a. calib_resp: a single uint32 field.
     *     Also checks the little-endian byte order. */
    msg_calib_resp_t r = {.ut_per_tick = 0x12345678}, r2;
    load_len = msg_calib_resp_pack(&r, load, sizeof load);
    assert(load[0] == 0x78 && load[3] == 0x12);
    assert(msg_calib_resp_unpack(load, (size_t)load_len, &r2) == PROTOCOL_OK);
    assert(r2.ut_per_tick == r.ut_per_tick);

    /* 4b. task_deploy: mixed field sizes (uint8 and uint16).
     *     Checks that each field lands in its own position and is recovered. */
    msg_task_deploy_t t = {3, TASK_PERIODIC, 2, 20, 20}, t2;
    load_len = msg_task_deploy_pack(&t, load, sizeof load);
    assert(load_len == 8);
    assert(msg_task_deploy_unpack(load, (size_t)load_len, &t2) == PROTOCOL_OK);
    assert(t2.task_id == 3 && t2.cost_ticks == 2 && t2.period == 20 && t2.deadline == 20);

    /* 5. Invalid entries tests */
    assert(msg_calib_req_unpack(load, 3, &req) == PROTOCOL_ERR_LEN);                                /* len bigger than LOAD_SIZE_CALIB_REQ */
    assert(msg_calib_req_unpack(load, 1, &req) == PROTOCOL_ERR_LEN);                                /* len smaller than LOAD_SIZE_CALIB_REQ */
    assert(msg_calib_req_unpack(NULL, 2, &req) == PROTOCOL_ERR_ARG);                                /* NULL load pointer */
    assert(msg_task_deploy_pack(&t, load, 4) == PROTOCOL_ERR_ARG);                                  /* Capacity smaller than LOAD_SIZE_TASK_DEPLOY */
    assert(protocol_frame_build(frame, 4, MSG_CALIB_REQ, load, 2) == PROTOCOL_ERR_ARG);             /* Capacity smaller than total frame size */
    assert(protocol_frame_build(frame, sizeof frame, MSG_CALIB_REQ, load, 33) == PROTOCOL_ERR_LEN); /* load_len bigger than PROTOCOL_MAX_LOAD_SIZE */

    puts("OK: All protocol tests passed!");
    return 0;
}
