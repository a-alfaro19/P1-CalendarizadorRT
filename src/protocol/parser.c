#include "protocol/parser.h"
#include "protocol/protocol.h"

void protocol_parser_init(protocol_parser_t *parser)
{
    if (parser == NULL)
        return;

    memset(parser, 0, sizeof *parser);
    parser->state = PARSER_WAIT_START;
}

int protocol_parser_feed(protocol_parser_t *parser, uint8_t byte, protocol_frame_t *frame)
{
    if (parser == NULL || frame == NULL)
        return PROTOCOL_ERR_ARG;

    switch (parser->state)
    {
    case PARSER_WAIT_START:
        /* Any byte that is not START_MARK is skipped */
        if (byte == PROTOCOL_START_MARK)
        {
            parser->crc = 0xFFFF; /* CRC initial value */
            parser->state = PARSER_TYPE;
        }
        break;

    case PARSER_TYPE:
        parser->frame.type = byte;
        parser->crc = protocol_crc16_update(parser->crc, byte);
        parser->state = PARSER_LOAD_LEN;
        break;

    case PARSER_LOAD_LEN:
        if (byte > PROTOCOL_MAX_LOAD_SIZE)
        {
            /* invalid length: discard */
            parser->state = PARSER_WAIT_START;
            parser->stats.length_errors++;
            return PROTOCOL_ERR_LEN;
        }
        parser->frame.load_len = byte;
        parser->received = 0;
        parser->crc = protocol_crc16_update(parser->crc, byte);
        /* A frame without load goes straight to the CRC */
        parser->state = (byte == 0) ? PARSER_CRC_LOW : PARSER_LOAD;
        break;

    case PARSER_LOAD:
        /* received < load_len <= PROTOCOL_MAX_LOAD_SIZE, so it never overflows */
        parser->frame.load[parser->received++] = byte;
        parser->crc = protocol_crc16_update(parser->crc, byte);
        if (parser->received == parser->frame.load_len)
            parser->state = PARSER_CRC_LOW;
        break;

    case PARSER_CRC_LOW:
        parser->crc_received = byte;
        parser->state = PARSER_CRC_HIGH;
        break;

    case PARSER_CRC_HIGH:
        parser->crc_received |= (uint16_t)((uint16_t)byte << 8);
        parser->state = PARSER_WAIT_START; /* the frame ends here either way */
        if (parser->crc_received == parser->crc)
        {
            parser->stats.frames_ok++;
            *frame = parser->frame;
            return PROTOCOL_FRAME_READY;
        }
        parser->stats.crc_errors++;
        return PROTOCOL_ERR_CRC;

    default: /* corrupted state: resynchronize */
        parser->state = PARSER_WAIT_START;
        break;
    }

    return PROTOCOL_OK;
}
