#include "node/node_driver.h"

#include <string.h>

static void count_bad_frame(node_driver_t *driver)
{
    if (driver->bad_frames < UINT16_MAX)
        driver->bad_frames++;
}

static bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return (uint32_t)(now - deadline) < UINT32_C(0x80000000);
}

static int send_message(node_driver_t *driver,
                        uint8_t type,
                        const uint8_t *load,
                        size_t load_length)
{
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE];
    int frame_length = protocol_frame_build(frame,
                                            sizeof(frame),
                                            type,
                                            load,
                                            load_length);
    if (frame_length < 0)
        return -1;

    return driver->io.write_bytes(driver->io.context,
                                  frame,
                                  (size_t)frame_length);
}

static int send_ack(node_driver_t *driver, uint8_t type, uint8_t status)
{
    msg_ack_t ack = {.acked_type = type, .status = status};
    uint8_t load[LOAD_SIZE_ACK];
    int load_length = msg_ack_pack(&ack, load, sizeof(load));
    if (load_length < 0)
        return -1;
    return send_message(driver, MSG_ACK, load, (size_t)load_length);
}

static uint8_t scheduler_status_to_protocol(ack_status_t status)
{
    switch (status)
    {
    case ACK_OK:
        return MSG_ACK_OK;
    case ACK_INVALIDO:
        return MSG_ACK_INVALID;
    case ACK_SIN_ESPACIO:
        return MSG_ACK_NO_SPACE;
    default:
        return MSG_ACK_INVALID;
    }
}

static int handle_calibration_request(node_driver_t *driver,
                                      const protocol_frame_t *frame)
{
    msg_calib_req_t request;
    if (msg_calib_req_unpack(frame->load, frame->load_len, &request) != PROTOCOL_OK ||
        request.tick_ms == 0 || driver->started || driver->stopped)
    {
        count_bad_frame(driver);
        return 0;
    }

    driver->tick_ms = request.tick_ms;
    driver->ut_per_tick = driver->io.calibrate(driver->io.context, request.tick_ms);
    driver->calibrated = driver->ut_per_tick > 0;

    msg_calib_resp_t response = {.ut_per_tick = driver->ut_per_tick};
    uint8_t load[LOAD_SIZE_CALIB_RESP];
    int load_length = msg_calib_resp_pack(&response, load, sizeof(load));
    if (load_length < 0)
        return -1;

    return send_message(driver, MSG_CALIB_RESP, load, (size_t)load_length);
}

static int handle_task_deploy(node_driver_t *driver,
                              const protocol_frame_t *frame)
{
    msg_task_deploy_t message;
    uint8_t status = MSG_ACK_INVALID;

    if (msg_task_deploy_unpack(frame->load, frame->load_len, &message) != PROTOCOL_OK)
    {
        count_bad_frame(driver);
    }
    else if (!driver->calibrated || driver->stopped)
    {
        count_bad_frame(driver);
    }
    else
    {
        tarea_def_t definition = {
            .id = message.task_id,
            .tipo = message.kind,
            .costo = message.cost_ticks,
            .periodo = message.period,
            .plazo = message.deadline,
        };
        status = scheduler_status_to_protocol(
            sched_agregar_tarea(&driver->scheduler, &definition));
    }

    return send_ack(driver, MSG_TASK_DEPLOY, status);
}

static int handle_start(node_driver_t *driver,
                        const protocol_frame_t *frame)
{
    msg_start_t message;
    uint8_t status = MSG_ACK_INVALID;

    if (msg_start_unpack(frame->load, frame->load_len, &message) != PROTOCOL_OK)
    {
        count_bad_frame(driver);
    }
    else if (!driver->calibrated || driver->stopped)
    {
        count_bad_frame(driver);
    }
    else
    {
        ack_status_t start_status =
            sched_iniciar(&driver->scheduler, message.algorithm, message.report_period);
        status = scheduler_status_to_protocol(start_status);
        if (start_status == ACK_OK && !driver->started)
        {
            driver->tick_period_us = (uint32_t)driver->tick_ms * 1000u;
            driver->next_tick_us = driver->io.now_us(driver->io.context);
            driver->started = 1;
        }
    }

    return send_ack(driver, MSG_START, status);
}

static bool has_sporadic_task(const sched_t *scheduler, uint8_t task_id)
{
    for (uint8_t i = 0; i < scheduler->n_tareas; i++)
    {
        if (scheduler->tareas[i].def.id == task_id &&
            scheduler->tareas[i].def.tipo == TAREA_ESPORADICA)
            return true;
    }
    return false;
}

static int handle_sporadic_trigger(node_driver_t *driver,
                                   const protocol_frame_t *frame)
{
    msg_sporadic_t message;
    if (msg_sporadic_unpack(frame->load, frame->load_len, &message) != PROTOCOL_OK ||
        !driver->started || driver->stopped ||
        !has_sporadic_task(&driver->scheduler, message.task_id))
    {
        count_bad_frame(driver);
        return 0;
    }

    sched_registrar_disparo(&driver->scheduler);
    return 0;
}

static int handle_stop(node_driver_t *driver,
                       const protocol_frame_t *frame)
{
    if (frame->load_len != LOAD_SIZE_STOP)
    {
        count_bad_frame(driver);
        return send_ack(driver, MSG_STOP, MSG_ACK_INVALID);
    }

    sched_detener(&driver->scheduler);
    driver->stopped = 1;
    return send_ack(driver, MSG_STOP, MSG_ACK_OK);
}

static int handle_frame(node_driver_t *driver, const protocol_frame_t *frame)
{
    switch (frame->type)
    {
    case MSG_CALIB_REQ:
        return handle_calibration_request(driver, frame);
    case MSG_TASK_DEPLOY:
        return handle_task_deploy(driver, frame);
    case MSG_START:
        return handle_start(driver, frame);
    case MSG_SPORADIC:
        return handle_sporadic_trigger(driver, frame);
    case MSG_STOP:
        return handle_stop(driver, frame);
    default:
        count_bad_frame(driver);
        return 0;
    }
}

static int send_pending_events(node_driver_t *driver)
{
    evento_t event;
    while (evq_pop(&driver->scheduler.eventos, &event))
    {
        msg_event_t message = {
            .tick = event.tick,
            .event_type = event.tipo,
            .task_id = event.id_tarea,
            .aux = event.aux,
        };
        uint8_t load[LOAD_SIZE_EVENT];
        int load_length = msg_event_pack(&message, load, sizeof(load));
        if (load_length < 0 ||
            send_message(driver, MSG_EVENT, load, (size_t)load_length) != 0)
            return -1;
    }
    return 0;
}

static int send_tick_report(node_driver_t *driver, uint32_t tick)
{
    msg_tick_report_t report = {
        .tick = tick,
        .bad_frames = driver->bad_frames,
    };
    uint8_t load[LOAD_SIZE_TICK_REPORT];
    int load_length = msg_tick_report_pack(&report, load, sizeof(load));
    if (load_length < 0)
        return -1;
    return send_message(driver, MSG_TICK_REPORT, load, (size_t)load_length);
}

static int run_due_ticks(node_driver_t *driver)
{
    if (!driver->started || driver->stopped || driver->tick_period_us == 0)
        return 0;

    uint32_t now = driver->io.now_us(driver->io.context);
    while (deadline_reached(now, driver->next_tick_us))
    {
        decision_t decision = sched_tick(&driver->scheduler);
        if (decision.slot != SLOT_IDLE)
        {
            for (uint32_t i = 0; i < driver->ut_per_tick; i++)
                driver->io.run_unit(driver->io.context);
        }
        if (driver->io.apply_decision != NULL)
            driver->io.apply_decision(driver->io.context, &decision);
        if (send_pending_events(driver) != 0)
            return -1;
        if (decision.reporte_tick &&
            send_tick_report(driver, driver->scheduler.tick) != 0)
            return -1;

        driver->next_tick_us += driver->tick_period_us;
        now = driver->io.now_us(driver->io.context);
    }

    return 0;
}

int node_driver_init(node_driver_t *driver, const node_driver_io_t *io)
{
    if (driver == NULL || io == NULL ||
        io->read_byte == NULL || io->write_bytes == NULL ||
        io->now_us == NULL || io->calibrate == NULL || io->run_unit == NULL)
        return -1;

    memset(driver, 0, sizeof(*driver));
    driver->io = *io;
    protocol_parser_init(&driver->parser);
    sched_init(&driver->scheduler);
    return 0;
}

int node_driver_poll(node_driver_t *driver)
{
    if (driver == NULL)
        return -1;

    uint8_t byte;
    int read_result;
    while ((read_result = driver->io.read_byte(driver->io.context, &byte)) > 0)
    {
        protocol_frame_t frame;
        int parse_result = protocol_parser_feed(&driver->parser, byte, &frame);
        if (parse_result < 0)
        {
            count_bad_frame(driver);
        }
        else if (parse_result == PROTOCOL_FRAME_READY &&
                 handle_frame(driver, &frame) != 0)
        {
            return -1;
        }
    }

    if (read_result < 0)
        return -1;

    return run_due_ticks(driver);
}
