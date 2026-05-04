#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define IR_PROTO_HEADER       0xAA55
#define IR_PROTO_MAX_PAYLOAD  248
#define IR_PROTO_BROADCAST    0xFF

#define IR_CTRL_CMD           0x10
#define IR_CTRL_CMD_REQ       0x18
#define IR_CTRL_RSP           0x20
#define IR_CTRL_DATA          0x30
#define IR_CTRL_BCAST         0x40

#define IR_CTRL_TYPE_MASK     0xF0
#define IR_CTRL_REQ_MASK      0x08

typedef struct __attribute__((packed)) {
    uint16_t header;
    uint8_t  ctrl;
    uint8_t  master_id;
    uint8_t  slave_id;
    uint8_t  seq;
    uint8_t  data[];
} ir_proto_hdr_t;

typedef struct {
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t rx_filtered;
    uint32_t rx_crc_errors;
    uint32_t rx_len_errors;
    uint32_t tx_retries;
    uint32_t tx_timeouts;
} ir_proto_stats_t;

static inline bool ir_ctrl_is_req(uint8_t ctrl) {
    return (ctrl & IR_CTRL_REQ_MASK) != 0;
}

static inline uint8_t ir_ctrl_type(uint8_t ctrl) {
    return ctrl & IR_CTRL_TYPE_MASK;
}
