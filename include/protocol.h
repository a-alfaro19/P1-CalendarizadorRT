#pragma once

/**
 * @file protocol.h
 * @brief Serial protocol between the host and the nodes.
 *
 * Include only this header from the rest of the project. The protocol is
 * split in four modules:
 *
 *   crc16.h     CRC-16/CCITT-FALSE
 *   frame.h     frame format and frame building
 *   parser.h    receiving frames byte by byte, with reception counters
 *   message.h   messages: what each one means and how its load is encoded
 *
 * protocol_defs.h holds the return codes shared by all of them.
 */

#include "protocol_defs.h"
#include "frame.h"
#include "message.h"
#include "parser.h"
#include "parser.h"
