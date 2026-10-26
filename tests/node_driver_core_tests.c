#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "node/node_driver.h"
#include "test_harness.h"

#define TEST_BUFFER_SIZE 4096

typedef struct
{
    uint8_t rx[TEST_BUFFER_SIZE];
    size_t rx_length;
    size_t rx_offset;
    uint8_t tx[TEST_BUFFER_SIZE];
    size_t tx_length;
    uint32_t now_us;
    uint32_t ut_per_tick;
    uint32_t calibration_tick_ms;
    uint32_t units_executed;
} fake_board_t;

static fake_board_t board;
static node_driver_t driver;

static int fake_read_byte(void *context, uint8_t *byte)
{
    fake_board_t *fake = context;
    if (fake->rx_offset == fake->rx_length)
        return 0;

    *byte = fake->rx[fake->rx_offset++];
    return 1;
}

static int fake_write_bytes(void *context, const uint8_t *bytes, size_t length)
{
    fake_board_t *fake = context;
    if (fake->tx_length + length > sizeof(fake->tx))
        return -1;

    memcpy(fake->tx + fake->tx_length, bytes, length);
    fake->tx_length += length;
    return 0;
}

static uint32_t fake_now_us(void *context)
{
    return ((fake_board_t *)context)->now_us;
}

static uint32_t fake_calibrate(void *context, uint16_t tick_ms)
{
    fake_board_t *fake = context;
    fake->calibration_tick_ms = tick_ms;
    return fake->ut_per_tick;
}

static void fake_run_unit(void *context)
{
    ((fake_board_t *)context)->units_executed++;
}

static void setup_driver(void)
{
    memset(&board, 0, sizeof(board));
    board.now_us = 1000;
    board.ut_per_tick = 3;
    node_driver_io_t io = {
        .context = &board,
        .read_byte = fake_read_byte,
        .write_bytes = fake_write_bytes,
        .now_us = fake_now_us,
        .calibrate = fake_calibrate,
        .run_unit = fake_run_unit,
        .apply_decision = NULL,
    };
    assert(node_driver_init(&driver, &io) == 0);
}

static void queue_host_message(uint8_t type, const uint8_t *load, size_t load_length)
{
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE];
    int frame_length = protocol_frame_build(frame,
                                            sizeof(frame),
                                            type,
                                            load,
                                            load_length);
    assert(frame_length > 0);
    assert(board.rx_length + (size_t)frame_length <= sizeof(board.rx));
    memcpy(board.rx + board.rx_length, frame, (size_t)frame_length);
    board.rx_length += (size_t)frame_length;
}

static size_t collect_node_messages(protocol_frame_t *frames, size_t capacity)
{
    protocol_parser_t parser;
    protocol_parser_init(&parser);

    size_t count = 0;
    for (size_t i = 0; i < board.tx_length; i++)
    {
        protocol_frame_t frame;
        int result = protocol_parser_feed(&parser, board.tx[i], &frame);
        if (result == PROTOCOL_FRAME_READY)
        {
            assert(count < capacity);
            frames[count++] = frame;
        }
    }
    return count;
}

static void test_calibration_and_task_deployment(void)
{
    setup_driver();

    msg_calib_req_t calibration = {.tick_ms = 10};
    uint8_t load[LOAD_SIZE_TASK_DEPLOY];
    int length = msg_calib_req_pack(&calibration, load, sizeof(load));
    assert(length == LOAD_SIZE_CALIB_REQ);
    queue_host_message(MSG_CALIB_REQ, load, (size_t)length);
    assert(node_driver_poll(&driver) == 0);

    protocol_frame_t output[8];
    size_t count = collect_node_messages(output, 8);
    assert(count == 1);
    assert(output[0].type == MSG_CALIB_RESP);

    msg_calib_resp_t calibration_response;
    assert(msg_calib_resp_unpack(output[0].load,
                                 output[0].load_len,
                                 &calibration_response) == PROTOCOL_OK);
    assert(calibration_response.ut_per_tick == 3);
    assert(board.calibration_tick_ms == 10);

    board.rx_length = 0;
    board.rx_offset = 0;
    msg_task_deploy_t task = {
        .task_id = 4,
        .kind = TASK_KIND_PERIODIC,
        .cost_ticks = 1,
        .period = 2,
        .deadline = 2,
    };
    length = msg_task_deploy_pack(&task, load, sizeof(load));
    assert(length == LOAD_SIZE_TASK_DEPLOY);
    queue_host_message(MSG_TASK_DEPLOY, load, (size_t)length);
    assert(node_driver_poll(&driver) == 0);

    count = collect_node_messages(output, 8);
    assert(count == 2);
    assert(output[1].type == MSG_ACK);
    msg_ack_t ack;
    assert(msg_ack_unpack(output[1].load, output[1].load_len, &ack) == PROTOCOL_OK);
    assert(ack.acked_type == MSG_TASK_DEPLOY);
    assert(ack.status == MSG_ACK_OK);
}

static void test_start_executes_and_reports_scheduler_events(void)
{
    setup_driver();
    driver.calibrated = 1;
    driver.tick_ms = 10;
    driver.ut_per_tick = 2;

    tarea_def_t task = {
        .id = 9,
        .tipo = TAREA_PERIODICA,
        .costo = 1,
        .periodo = 2,
        .plazo = 2,
    };
    assert(sched_agregar_tarea(&driver.scheduler, &task) == ACK_OK);

    msg_start_t start = {
        .algorithm = ALGORITHM_EDF,
        .report_period = 1,
    };
    uint8_t load[LOAD_SIZE_START];
    int length = msg_start_pack(&start, load, sizeof(load));
    assert(length == LOAD_SIZE_START);
    queue_host_message(MSG_START, load, (size_t)length);

    assert(node_driver_poll(&driver) == 0);
    assert(board.units_executed == 2);

    protocol_frame_t output[8];
    size_t count = collect_node_messages(output, 8);
    assert(count == 5);

    msg_ack_t ack;
    assert(output[0].type == MSG_ACK);
    assert(msg_ack_unpack(output[0].load, output[0].load_len, &ack) == PROTOCOL_OK);
    assert(ack.acked_type == MSG_START && ack.status == MSG_ACK_OK);

    msg_event_t event;
    assert(output[1].type == MSG_EVENT);
    assert(msg_event_unpack(output[1].load, output[1].load_len, &event) == PROTOCOL_OK);
    assert(event.event_type == EVENT_RELEASE && event.task_id == 9);
    assert(output[2].type == MSG_EVENT);
    assert(msg_event_unpack(output[2].load, output[2].load_len, &event) == PROTOCOL_OK);
    assert(event.event_type == EVENT_SWITCH && event.task_id == 9);
    assert(output[3].type == MSG_EVENT);
    assert(msg_event_unpack(output[3].load, output[3].load_len, &event) == PROTOCOL_OK);
    assert(event.event_type == EVENT_COMPLETE && event.task_id == 9);
    assert(output[4].type == MSG_TICK_REPORT);

    msg_tick_report_t report;
    assert(msg_tick_report_unpack(output[4].load, output[4].load_len, &report) == PROTOCOL_OK);
    assert(report.tick == 1);
    assert(report.bad_frames == 0);

    board.rx_length = 0;
    board.rx_offset = 0;
    queue_host_message(MSG_STOP, NULL, 0);
    assert(node_driver_poll(&driver) == 0);
    assert(driver.stopped);
    count = collect_node_messages(output, 8);
    assert(count == 6);
    assert(output[5].type == MSG_ACK);
    assert(msg_ack_unpack(output[5].load, output[5].load_len, &ack) == PROTOCOL_OK);
    assert(ack.acked_type == MSG_STOP && ack.status == MSG_ACK_OK);

    size_t tx_length_after_stop = board.tx_length;
    board.now_us += 10000;
    assert(node_driver_poll(&driver) == 0);
    assert(board.tx_length == tx_length_after_stop);
    assert(board.units_executed == 2);
}

static void test_invalid_task_is_acknowledged_as_invalid(void)
{
    setup_driver();
    driver.calibrated = 1;
    driver.tick_ms = 10;

    msg_task_deploy_t invalid = {
        .task_id = TASK_NONE,
        .kind = TASK_KIND_PERIODIC,
        .cost_ticks = 1,
        .period = 2,
        .deadline = 2,
    };
    uint8_t load[LOAD_SIZE_TASK_DEPLOY];
    int length = msg_task_deploy_pack(&invalid, load, sizeof(load));
    assert(length == LOAD_SIZE_TASK_DEPLOY);
    queue_host_message(MSG_TASK_DEPLOY, load, (size_t)length);

    assert(node_driver_poll(&driver) == 0);

    protocol_frame_t output[2];
    size_t count = collect_node_messages(output, 2);
    assert(count == 1);
    assert(output[0].type == MSG_ACK);
    msg_ack_t ack;
    assert(msg_ack_unpack(output[0].load, output[0].load_len, &ack) == PROTOCOL_OK);
    assert(ack.acked_type == MSG_TASK_DEPLOY);
    assert(ack.status == MSG_ACK_INVALID);
}

int main(void)
{
    puts("Node Driver Core");
    RUN_TEST(test_calibration_and_task_deployment);
    RUN_TEST(test_start_executes_and_reports_scheduler_events);
    RUN_TEST(test_invalid_task_is_acknowledged_as_invalid);
    puts("OK: All node driver core tests passed!");
    return 0;
}
