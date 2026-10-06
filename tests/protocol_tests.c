#include <protocol/protocol.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ---------------------------------------------------------------------------
 * Test harness
 *
 * Every test is a function with no arguments. A failed assert() aborts the
 * program and reports the file and line, so the name printed by RUN_TEST is
 * the last one on screen when something breaks.
 * ------------------------------------------------------------------------- */
#define RUN_TEST(test)            \
    do                            \
    {                             \
        printf("  %-52s", #test); \
        fflush(stdout);           \
        test();                   \
        puts("OK");               \
    } while (0)

/** Feeds n bytes to the parser and returns the result of the last byte. */
static int feed_bytes(protocol_parser_t *parser, const uint8_t *bytes, size_t n,
                      protocol_frame_t *out)
{
    int result = PROTOCOL_OK;
    for (size_t i = 0; i < n; i++)
        result = protocol_parser_feed(parser, bytes[i], out);
    return result;
}

/* ===========================================================================
 * CRC
 * ========================================================================= */

/**
 * Case:     CRC-16/CCITT-FALSE of the ASCII string "123456789" (the standard
 *           check value) and of an empty buffer.
 * Expected: 0x29B1 for the string; 0xFFFF for the empty buffer, which is the
 *           initial value of the algorithm.
 */
static void test_crc16_check_value(void)
{
    assert(protocol_crc16((const uint8_t *)"123456789", 9) == 0x29B1);
    assert(protocol_crc16((const uint8_t *)"", 0) == 0xFFFF);
}

/**
 * Case:     CRC computed one byte at a time with protocol_crc16_update(),
 *           starting from 0xFFFF. This is how the parser computes it.
 * Expected: the same value as the whole-buffer version (0x29B1).
 */
static void test_crc16_update_matches_buffer(void)
{
    const uint8_t data[] = "123456789";
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < 9; i++)
        crc = protocol_crc16_update(crc, data[i]);

    assert(crc == 0x29B1);
    assert(crc == protocol_crc16(data, 9));
}

/* ===========================================================================
 * Frame layer: protocol_frame_build()
 * ========================================================================= */

/**
 * Case:     Build a calibration request with tick = 10 ms.
 * Expected: 7 bytes: 7E | 00 | 02 | 0A 00 | 6B 05
 *           (CRC 0x056B, sent low byte first).
 */
static void test_frame_build_calib_req(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE], frame[PROTOCOL_MAX_FRAME_SIZE];
    const uint8_t expected[] = {0x7E, 0x00, 0x02, 0x0A, 0x00, 0x6B, 0x05};
    msg_calib_req_t req = {.tick_ms = 10};

    int load_len = msg_calib_req_pack(&req, load, sizeof load);
    assert(load_len == 2);

    int frame_len = protocol_frame_build(frame, sizeof frame, MSG_CALIB_REQ, load, (size_t)load_len);
    assert(frame_len == 7);
    assert(memcmp(frame, expected, 7) == 0);
}

/**
 * Case:     Build a message that has no load (stop), passing load = NULL.
 * Expected: 5 bytes: START_MARK, TYPE = MSG_STOP, LOAD_LEN = 0 and the CRC of
 *           [TYPE, LOAD_LEN] in the last two bytes (low byte first).
 */
static void test_frame_build_without_load(void)
{
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE];

    int frame_len = protocol_frame_build(frame, sizeof frame, MSG_STOP, NULL, 0);
    assert(frame_len == 5);
    assert(frame[0] == PROTOCOL_START_MARK);
    assert(frame[1] == MSG_STOP);
    assert(frame[2] == 0x00);

    uint16_t crc = protocol_crc16(frame + 1, 2);
    assert(frame[3] == (crc & 0xFF) && frame[4] == (crc >> 8));
}

/**
 * Case:     Invalid arguments and size limits in protocol_frame_build().
 * Expected: PROTOCOL_ERR_ARG for a NULL output, a NULL load with load_len > 0
 *           or a buffer smaller than the frame; PROTOCOL_ERR_LEN for a load
 *           above PROTOCOL_MAX_LOAD_SIZE; success when the buffer is exactly
 *           the size of the frame (boundary).
 */
static void test_frame_build_invalid_arguments(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE + 1] = {0}, frame[PROTOCOL_MAX_FRAME_SIZE];

    assert(protocol_frame_build(NULL, sizeof frame, MSG_CALIB_REQ, load, 2) == PROTOCOL_ERR_ARG);
    assert(protocol_frame_build(frame, sizeof frame, MSG_CALIB_REQ, NULL, 2) == PROTOCOL_ERR_ARG);
    assert(protocol_frame_build(frame, 6, MSG_CALIB_REQ, load, 2) == PROTOCOL_ERR_ARG); /* needs 7 */
    assert(protocol_frame_build(frame, 7, MSG_CALIB_REQ, load, 2) == 7);                /* exact fit */
    assert(protocol_frame_build(frame, sizeof frame, MSG_CALIB_REQ, load,
                                PROTOCOL_MAX_LOAD_SIZE + 1) == PROTOCOL_ERR_LEN);
}

/* ===========================================================================
 * Message layer: pack / unpack round trips
 * Each test checks the bytes written by hand (position and little-endian
 * order) and that unpack recovers the original message.
 * ========================================================================= */

/**
 * Case:     calibration request: a single uint16 field (tick_ms = 10).
 * Expected: load = 0A 00 (2 bytes); unpack recovers tick_ms = 10.
 */
static void test_calib_req_roundtrip(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
    msg_calib_req_t message = {.tick_ms = 10}, recovered;

    int load_len = msg_calib_req_pack(&message, load, sizeof load);
    assert(load_len == 2 && load[0] == 0x0A && load[1] == 0x00);
    assert(msg_calib_req_unpack(load, (size_t)load_len, &recovered) == PROTOCOL_OK);
    assert(recovered.tick_ms == 10);
}

/**
 * Case:     calibration response: a single uint32 field (0x12345678).
 * Expected: load = 78 56 34 12 (little-endian, 4 bytes); unpack recovers the
 *           same value.
 */
static void test_calib_resp_roundtrip(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
    msg_calib_resp_t message = {.ut_per_tick = 0x12345678}, recovered;

    int load_len = msg_calib_resp_pack(&message, load, sizeof load);
    assert(load_len == 4);
    assert(load[0] == 0x78 && load[1] == 0x56 && load[2] == 0x34 && load[3] == 0x12);
    assert(msg_calib_resp_unpack(load, (size_t)load_len, &recovered) == PROTOCOL_OK);
    assert(recovered.ut_per_tick == message.ut_per_tick);
}

/**
 * Case:     task deployment: mixed field sizes (uint8 and uint16).
 *           task_id = 3, periodic, cost = 2, period = 20, deadline = 20.
 * Expected: load = 03 00 | 02 00 | 14 00 | 14 00 (8 bytes); every field is
 *           recovered in its own position.
 */
static void test_task_deploy_roundtrip(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
    msg_task_deploy_t message = {3, TASK_PERIODIC, 2, 20, 20}, recovered;
    const uint8_t expected[] = {0x03, 0x00, 0x02, 0x00, 0x14, 0x00, 0x14, 0x00};

    int load_len = msg_task_deploy_pack(&message, load, sizeof load);
    assert(load_len == 8);
    assert(memcmp(load, expected, sizeof expected) == 0);
    assert(msg_task_deploy_unpack(load, (size_t)load_len, &recovered) == PROTOCOL_OK);
    assert(recovered.task_id == 3 && recovered.kind == TASK_PERIODIC);
    assert(recovered.cost_ticks == 2 && recovered.period == 20 && recovered.deadline == 20);
}

/**
 * Case:     start message: uint8 + uint16 (EDF, report_period = 0x0102).
 * Expected: load = ALGORITHM_EDF | 02 01 (3 bytes); both fields recovered.
 */
static void test_start_roundtrip(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
    msg_start_t message = {ALGORITHM_EDF, 0x0102}, recovered;

    int load_len = msg_start_pack(&message, load, sizeof load);
    assert(load_len == 3);
    assert(load[0] == ALGORITHM_EDF && load[1] == 0x02 && load[2] == 0x01);
    assert(msg_start_unpack(load, (size_t)load_len, &recovered) == PROTOCOL_OK);
    assert(recovered.algorithm == ALGORITHM_EDF && recovered.report_period == 0x0102);
}

/**
 * Case:     event: uint32 + uint8 + uint8 + uint16
 *           (tick = 0x01020304, EVENT_COMPLETE, task 5, aux = 0xABCD).
 * Expected: load = 04 03 02 01 | EVENT_COMPLETE | 05 | CD AB (8 bytes);
 *           all four fields recovered.
 */
static void test_event_roundtrip(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
    msg_event_t message = {0x01020304, EVENT_COMPLETE, 5, 0xABCD}, recovered;

    int load_len = msg_event_pack(&message, load, sizeof load);
    assert(load_len == 8);
    assert(load[0] == 0x04 && load[1] == 0x03 && load[2] == 0x02 && load[3] == 0x01);
    assert(load[4] == EVENT_COMPLETE && load[5] == 5);
    assert(load[6] == 0xCD && load[7] == 0xAB);
    assert(msg_event_unpack(load, (size_t)load_len, &recovered) == PROTOCOL_OK);
    assert(recovered.tick == 0x01020304 && recovered.event_type == EVENT_COMPLETE);
    assert(recovered.task_id == 5 && recovered.aux == 0xABCD);
}

/**
 * Case:     tick report: a single uint32 (0xAABBCCDD).
 * Expected: load = DD CC BB AA (4 bytes); same value recovered.
 */
static void test_tick_report_roundtrip(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
    msg_tick_report_t message = {0xAABBCCDD}, recovered;

    int load_len = msg_tick_report_pack(&message, load, sizeof load);
    assert(load_len == 6);
    assert(load[0] == 0xDD && load[1] == 0xCC && load[2] == 0xBB && load[3] == 0xAA);
    assert(msg_tick_report_unpack(load, (size_t)load_len, &recovered) == PROTOCOL_OK);
    assert(recovered.tick == 0xAABBCCDD);
}

/**
 * Case:     sporadic task trigger: a single uint8 (task_id = 7).
 * Expected: load = 07 (1 byte); same task_id recovered.
 */
static void test_sporadic_roundtrip(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
    msg_sporadic_t message = {7}, recovered;

    int load_len = msg_sporadic_pack(&message, load, sizeof load);
    assert(load_len == 1 && load[0] == 7);
    assert(msg_sporadic_unpack(load, (size_t)load_len, &recovered) == PROTOCOL_OK);
    assert(recovered.task_id == 7);
}

/**
 * Case:     acknowledgement: two uint8 (MSG_TASK_DEPLOY confirmed, no space).
 * Expected: load = MSG_TASK_DEPLOY | ACK_NO_SPACE (2 bytes); both recovered.
 */
static void test_ack_roundtrip(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE];
    msg_ack_t message = {MSG_TASK_DEPLOY, ACK_NO_SPACE}, recovered;

    int load_len = msg_ack_pack(&message, load, sizeof load);
    assert(load_len == 2 && load[0] == MSG_TASK_DEPLOY && load[1] == ACK_NO_SPACE);
    assert(msg_ack_unpack(load, (size_t)load_len, &recovered) == PROTOCOL_OK);
    assert(recovered.acked_type == MSG_TASK_DEPLOY && recovered.status == ACK_NO_SPACE);
}

/**
 * Case:     unpack with a load length different from the size of the message
 *           (too small, too big, empty).
 * Expected: PROTOCOL_ERR_LEN in every case.
 */
static void test_unpack_invalid_length(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE] = {0};
    msg_calib_req_t calib_req;
    msg_calib_resp_t calib_resp;
    msg_task_deploy_t task_deploy;
    msg_start_t start;
    msg_event_t event;
    msg_tick_report_t tick_report;
    msg_sporadic_t sporadic;
    msg_ack_t ack;

    assert(msg_calib_req_unpack(load, 1, &calib_req) == PROTOCOL_ERR_LEN); /* needs 2 */
    assert(msg_calib_req_unpack(load, 3, &calib_req) == PROTOCOL_ERR_LEN);
    assert(msg_calib_resp_unpack(load, 3, &calib_resp) == PROTOCOL_ERR_LEN);   /* needs 4 */
    assert(msg_task_deploy_unpack(load, 7, &task_deploy) == PROTOCOL_ERR_LEN); /* needs 8 */
    assert(msg_start_unpack(load, 2, &start) == PROTOCOL_ERR_LEN);             /* needs 3 */
    assert(msg_event_unpack(load, 9, &event) == PROTOCOL_ERR_LEN);             /* needs 8 */
    assert(msg_tick_report_unpack(load, 0, &tick_report) == PROTOCOL_ERR_LEN); /* needs 4 */
    assert(msg_sporadic_unpack(load, 2, &sporadic) == PROTOCOL_ERR_LEN);       /* needs 1 */
    assert(msg_ack_unpack(load, 3, &ack) == PROTOCOL_ERR_LEN);                 /* needs 2 */
}

/**
 * Case:     pack/unpack with NULL pointers and with a buffer smaller than the
 *           load.
 * Expected: PROTOCOL_ERR_ARG in every case.
 */
static void test_message_invalid_arguments(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE] = {0};
    msg_calib_req_t calib_req = {.tick_ms = 10};
    msg_task_deploy_t task_deploy = {3, TASK_PERIODIC, 2, 20, 20};
    msg_start_t start = {ALGORITHM_RMS, 10};
    msg_event_t event = {0};
    msg_ack_t ack = {0};

    assert(msg_calib_req_unpack(NULL, 2, &calib_req) == PROTOCOL_ERR_ARG); /* NULL load      */
    assert(msg_calib_req_unpack(load, 2, NULL) == PROTOCOL_ERR_ARG);       /* NULL message   */
    assert(msg_ack_unpack(NULL, 2, &ack) == PROTOCOL_ERR_ARG);
    assert(msg_ack_pack(NULL, load, sizeof load) == PROTOCOL_ERR_ARG);       /* NULL message   */
    assert(msg_ack_pack(&ack, NULL, sizeof load) == PROTOCOL_ERR_ARG);       /* NULL load      */
    assert(msg_task_deploy_pack(&task_deploy, load, 4) == PROTOCOL_ERR_ARG); /* needs 8        */
    assert(msg_start_pack(&start, load, 2) == PROTOCOL_ERR_ARG);             /* needs 3        */
    assert(msg_event_pack(&event, load, 7) == PROTOCOL_ERR_ARG);             /* needs 8        */
    assert(msg_event_pack(&event, load, 8) == 8);                            /* exact fit      */
}

/**
 * Case:     unpack fails (wrong length) on an output message that already
 *           holds values.
 * Expected: PROTOCOL_ERR_LEN and the output message is left untouched.
 */
static void test_unpack_error_leaves_message_untouched(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE] = {0};
    msg_event_t event = {123, EVENT_RELEASE, 4, 9};
    msg_task_deploy_t task_deploy = {1, TASK_SPORADIC, 2, 3, 4};

    assert(msg_event_unpack(load, 3, &event) == PROTOCOL_ERR_LEN);
    assert(event.tick == 123 && event.event_type == EVENT_RELEASE);
    assert(event.task_id == 4 && event.aux == 9);

    assert(msg_task_deploy_unpack(load, 1, &task_deploy) == PROTOCOL_ERR_LEN);
    assert(task_deploy.task_id == 1 && task_deploy.kind == TASK_SPORADIC);
    assert(task_deploy.cost_ticks == 2 && task_deploy.period == 3 && task_deploy.deadline == 4);
}

/* ===========================================================================
 * Parser
 * ========================================================================= */

/**
 * Case:     Initialize a parser.
 * Expected: all counters at zero. A NULL parser is ignored without crashing.
 */
static void test_parser_init(void)
{
    protocol_parser_t parser;

    protocol_parser_init(&parser);
    assert(parser.stats.frames_ok == 0);
    assert(parser.stats.crc_errors == 0);
    assert(parser.stats.length_errors == 0);

    protocol_parser_init(NULL);
}

/**
 * Case:     Receive a valid calibration request one byte at a time.
 * Expected: PROTOCOL_OK for every byte except the last one, which returns
 *           PROTOCOL_FRAME_READY. The frame has type MSG_CALIB_REQ and a
 *           2-byte load that unpacks to tick_ms = 10. frames_ok = 1.
 */
static void test_parser_receives_valid_frame_byte_by_byte(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE], frame[PROTOCOL_MAX_FRAME_SIZE];
    protocol_parser_t parser;
    protocol_frame_t parsed;
    msg_calib_req_t message = {.tick_ms = 10}, recovered;

    protocol_parser_init(&parser);
    int load_len = msg_calib_req_pack(&message, load, sizeof load);
    int frame_len = protocol_frame_build(frame, sizeof frame, MSG_CALIB_REQ, load, (size_t)load_len);

    for (int i = 0; i < frame_len - 1; i++)
        assert(protocol_parser_feed(&parser, frame[i], &parsed) == PROTOCOL_OK);
    assert(protocol_parser_feed(&parser, frame[frame_len - 1], &parsed) == PROTOCOL_FRAME_READY);

    assert(parsed.type == MSG_CALIB_REQ && parsed.load_len == 2);
    assert(msg_calib_req_unpack(parsed.load, parsed.load_len, &recovered) == PROTOCOL_OK);
    assert(recovered.tick_ms == 10);
    assert(parser.stats.frames_ok == 1);
}

/**
 * Case:     Full path through every layer with an event message:
 *           pack -> frame_build -> parser -> unpack.
 * Expected: the message recovered at the end is identical to the original.
 */
static void test_parser_end_to_end_event(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE], frame[PROTOCOL_MAX_FRAME_SIZE];
    protocol_parser_t parser;
    protocol_frame_t parsed;
    msg_event_t message = {500, EVENT_DEADLINE_MISS, 2, 0}, recovered;

    protocol_parser_init(&parser);
    int load_len = msg_event_pack(&message, load, sizeof load);
    int frame_len = protocol_frame_build(frame, sizeof frame, MSG_EVENT, load, (size_t)load_len);

    assert(feed_bytes(&parser, frame, (size_t)frame_len, &parsed) == PROTOCOL_FRAME_READY);
    assert(parsed.type == MSG_EVENT);
    assert(msg_event_unpack(parsed.load, parsed.load_len, &recovered) == PROTOCOL_OK);
    assert(recovered.tick == 500 && recovered.event_type == EVENT_DEADLINE_MISS);
    assert(recovered.task_id == 2 && recovered.aux == 0);
}

/**
 * Case:     Receive a frame without load (stop). The parser must go from
 *           LOAD_LEN directly to the CRC.
 * Expected: PROTOCOL_FRAME_READY, type MSG_STOP, load_len = 0.
 */
static void test_parser_frame_without_load(void)
{
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE];
    protocol_parser_t parser;
    protocol_frame_t parsed;

    protocol_parser_init(&parser);
    int frame_len = protocol_frame_build(frame, sizeof frame, MSG_STOP, NULL, 0);

    assert(feed_bytes(&parser, frame, (size_t)frame_len, &parsed) == PROTOCOL_FRAME_READY);
    assert(parsed.type == MSG_STOP && parsed.load_len == 0);
    assert(parser.stats.frames_ok == 1);
}

/**
 * Case:     Boundary: a load of exactly PROTOCOL_MAX_LOAD_SIZE bytes.
 * Expected: PROTOCOL_FRAME_READY and all the load bytes recovered intact.
 */
static void test_parser_max_load(void)
{
    uint8_t big[PROTOCOL_MAX_LOAD_SIZE], frame[PROTOCOL_MAX_FRAME_SIZE];
    protocol_parser_t parser;
    protocol_frame_t parsed;

    protocol_parser_init(&parser);
    for (size_t i = 0; i < sizeof big; i++)
        big[i] = (uint8_t)i;
    int frame_len = protocol_frame_build(frame, sizeof frame, MSG_EVENT, big, sizeof big);
    assert(frame_len == PROTOCOL_MAX_FRAME_SIZE);

    assert(feed_bytes(&parser, frame, (size_t)frame_len, &parsed) == PROTOCOL_FRAME_READY);
    assert(parsed.load_len == sizeof big);
    assert(memcmp(parsed.load, big, sizeof big) == 0);
}

/**
 * Case:     Bytes that are not a START_MARK arrive before a valid frame.
 * Expected: the garbage returns PROTOCOL_OK and is skipped; the frame that
 *           follows is received normally. No error counter increases.
 */
static void test_parser_skips_garbage_before_frame(void)
{
    const uint8_t garbage[] = {0x00, 0x12, 0xFF, 0x80}; /* no 0x7E inside */
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE];
    protocol_parser_t parser;
    protocol_frame_t parsed;

    protocol_parser_init(&parser);
    assert(feed_bytes(&parser, garbage, sizeof garbage, &parsed) == PROTOCOL_OK);

    int frame_len = protocol_frame_build(frame, sizeof frame, MSG_STOP, NULL, 0);
    assert(feed_bytes(&parser, frame, (size_t)frame_len, &parsed) == PROTOCOL_FRAME_READY);

    assert(parser.stats.frames_ok == 1);
    assert(parser.stats.crc_errors == 0 && parser.stats.length_errors == 0);
}

/**
 * Case:     A frame arrives with a damaged load byte, followed by the same
 *           frame intact.
 * Expected: the damaged frame returns PROTOCOL_ERR_CRC, is counted in
 *           crc_errors and does not write the output frame. The intact one
 *           is then received normally (the parser resynchronized).
 */
static void test_parser_crc_error_is_counted_and_recovers(void)
{
    uint8_t load[PROTOCOL_MAX_LOAD_SIZE], frame[PROTOCOL_MAX_FRAME_SIZE];
    protocol_parser_t parser;
    protocol_frame_t parsed;
    msg_calib_req_t message = {.tick_ms = 10};

    protocol_parser_init(&parser);
    int load_len = msg_calib_req_pack(&message, load, sizeof load);
    int frame_len = protocol_frame_build(frame, sizeof frame, MSG_CALIB_REQ, load, (size_t)load_len);

    parsed.type = 0xEE; /* marker: must not change */
    frame[3] ^= 0xFF;   /* damage a load byte */
    assert(feed_bytes(&parser, frame, (size_t)frame_len, &parsed) == PROTOCOL_ERR_CRC);
    assert(parser.stats.crc_errors == 1 && parser.stats.frames_ok == 0);
    assert(parsed.type == 0xEE);

    frame[3] ^= 0xFF; /* restore it */
    assert(feed_bytes(&parser, frame, (size_t)frame_len, &parsed) == PROTOCOL_FRAME_READY);
    assert(parser.stats.frames_ok == 1 && parser.stats.crc_errors == 1);
}

/**
 * Case:     A frame announces LOAD_LEN = PROTOCOL_MAX_LOAD_SIZE + 1, followed
 *           by a valid frame.
 * Expected: the first two bytes return PROTOCOL_OK; the length byte returns
 *           PROTOCOL_ERR_LEN and is counted in length_errors. The valid
 *           frame that follows is received normally.
 */
static void test_parser_invalid_length_is_counted_and_recovers(void)
{
    const uint8_t bad_header[] = {PROTOCOL_START_MARK, MSG_EVENT, PROTOCOL_MAX_LOAD_SIZE + 1};
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE];
    protocol_parser_t parser;
    protocol_frame_t parsed;

    protocol_parser_init(&parser);
    assert(feed_bytes(&parser, bad_header, 2, &parsed) == PROTOCOL_OK);
    assert(protocol_parser_feed(&parser, bad_header[2], &parsed) == PROTOCOL_ERR_LEN);
    assert(parser.stats.length_errors == 1);

    int frame_len = protocol_frame_build(frame, sizeof frame, MSG_STOP, NULL, 0);
    assert(feed_bytes(&parser, frame, (size_t)frame_len, &parsed) == PROTOCOL_FRAME_READY);
    assert(parser.stats.frames_ok == 1 && parser.stats.crc_errors == 0);
}

/**
 * Case:     Two frames arrive back to back in the same byte stream.
 * Expected: each one completes with PROTOCOL_FRAME_READY, which shows the
 *           parser leaves a clean state after every frame. frames_ok = 2.
 */
static void test_parser_two_frames_back_to_back(void)
{
    uint8_t stream[2 * PROTOCOL_MAX_FRAME_SIZE];
    protocol_parser_t parser;
    protocol_frame_t parsed;

    protocol_parser_init(&parser);
    int n1 = protocol_frame_build(stream, sizeof stream, MSG_STOP, NULL, 0);
    int n2 = protocol_frame_build(stream + n1, sizeof stream - (size_t)n1, MSG_STOP, NULL, 0);

    assert(feed_bytes(&parser, stream, (size_t)n1, &parsed) == PROTOCOL_FRAME_READY);
    assert(feed_bytes(&parser, stream + n1, (size_t)n2, &parsed) == PROTOCOL_FRAME_READY);
    assert(parser.stats.frames_ok == 2);
}

/**
 * Case:     protocol_parser_feed() with a NULL parser and with a NULL output
 *           frame.
 * Expected: PROTOCOL_ERR_ARG in both cases, without crashing.
 */
static void test_parser_invalid_arguments(void)
{
    protocol_parser_t parser;
    protocol_frame_t parsed;

    protocol_parser_init(&parser);
    assert(protocol_parser_feed(NULL, 0x00, &parsed) == PROTOCOL_ERR_ARG);
    assert(protocol_parser_feed(&parser, 0x00, NULL) == PROTOCOL_ERR_ARG);
}

/* ===========================================================================
 * Main
 * ========================================================================= */
int main(void)
{
    puts("CRC");
    RUN_TEST(test_crc16_check_value);
    RUN_TEST(test_crc16_update_matches_buffer);

    puts("Frame layer");
    RUN_TEST(test_frame_build_calib_req);
    RUN_TEST(test_frame_build_without_load);
    RUN_TEST(test_frame_build_invalid_arguments);

    puts("Message layer");
    RUN_TEST(test_calib_req_roundtrip);
    RUN_TEST(test_calib_resp_roundtrip);
    RUN_TEST(test_task_deploy_roundtrip);
    RUN_TEST(test_start_roundtrip);
    RUN_TEST(test_event_roundtrip);
    RUN_TEST(test_tick_report_roundtrip);
    RUN_TEST(test_sporadic_roundtrip);
    RUN_TEST(test_ack_roundtrip);
    RUN_TEST(test_unpack_invalid_length);
    RUN_TEST(test_message_invalid_arguments);
    RUN_TEST(test_unpack_error_leaves_message_untouched);

    puts("Parser");
    RUN_TEST(test_parser_init);
    RUN_TEST(test_parser_receives_valid_frame_byte_by_byte);
    RUN_TEST(test_parser_end_to_end_event);
    RUN_TEST(test_parser_frame_without_load);
    RUN_TEST(test_parser_max_load);
    RUN_TEST(test_parser_skips_garbage_before_frame);
    RUN_TEST(test_parser_crc_error_is_counted_and_recovers);
    RUN_TEST(test_parser_invalid_length_is_counted_and_recovers);
    RUN_TEST(test_parser_two_frames_back_to_back);
    RUN_TEST(test_parser_invalid_arguments);

    puts("OK: All protocol tests passed!");
    return 0;
}
