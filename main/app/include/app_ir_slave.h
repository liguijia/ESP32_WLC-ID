#pragma once

#include "ir_proto_common.h"

typedef struct ir_slave ir_slave_t;

typedef void (*ir_slave_cmd_cb_t)(ir_slave_t *self, uint8_t master_id,
                                  const uint8_t *cmd, size_t cmd_len,
                                  uint8_t *rsp, size_t *rsp_len);

typedef void (*ir_slave_data_cb_t)(ir_slave_t *self, uint8_t src_id,
                                   const uint8_t *data, size_t len);

struct ir_slave {
    uint8_t id;
    uint8_t seq;
    uint32_t last_tx_tick;
    ir_slave_cmd_cb_t on_cmd;
    ir_slave_data_cb_t on_data;
    ir_proto_stats_t stats;
};

esp_err_t ir_slave_init(ir_slave_t *self, uint8_t id);
void ir_slave_deinit(ir_slave_t *self);

void ir_slave_set_cmd_cb(ir_slave_t *self, ir_slave_cmd_cb_t cb);
void ir_slave_set_data_cb(ir_slave_t *self, ir_slave_data_cb_t cb);

void ir_slave_get_stats(ir_slave_t *self, ir_proto_stats_t *stats);

void ir_slave_process_rx(ir_slave_t *self, const uint8_t *frame, size_t len);
