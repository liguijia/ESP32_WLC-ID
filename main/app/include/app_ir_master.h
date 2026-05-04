#pragma once

#include "ir_proto_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct ir_master ir_master_t;

typedef void (*ir_master_rsp_cb_t)(ir_master_t *self, uint8_t slave_id,
                                   const uint8_t *data, size_t len);

struct ir_master {
    uint8_t id;
    uint8_t seq;
    uint32_t last_tx_tick;
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t rsp_sem;
    uint8_t rsp_buf[IR_PROTO_MAX_PAYLOAD];
    size_t rsp_len;
    uint8_t rsp_slave_id;
    ir_master_rsp_cb_t on_rsp;
    ir_proto_stats_t stats;
};

esp_err_t ir_master_init(ir_master_t *self, uint8_t id);
void ir_master_deinit(ir_master_t *self);

void ir_master_set_rsp_cb(ir_master_t *self, ir_master_rsp_cb_t cb);

esp_err_t ir_master_send_cmd(ir_master_t *self, uint8_t slave_id,
                             const void *data, size_t len);

esp_err_t ir_master_send_cmd_req(ir_master_t *self, uint8_t slave_id,
                                 const void *cmd, size_t cmd_len,
                                 void *rsp, size_t rsp_max, size_t *rsp_len,
                                 uint32_t timeout_ms);

esp_err_t ir_master_send_cmd_req_default(ir_master_t *self, uint8_t slave_id,
                                         const void *cmd, size_t cmd_len,
                                         void *rsp, size_t rsp_max, size_t *rsp_len);

esp_err_t ir_master_broadcast(ir_master_t *self, const void *data, size_t len);

void ir_master_get_stats(ir_master_t *self, ir_proto_stats_t *stats);

void ir_master_process_rx(ir_master_t *self, const uint8_t *frame, size_t len);
