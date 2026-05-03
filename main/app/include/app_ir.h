#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bsp_twai.h"
#include "esp_err.h"

#define APP_IR_FRAME_HEADER 0xAA55
#define APP_IR_MAX_PAYLOAD  256

typedef void (*app_ir_rx_cb_t)(const uint8_t *data, size_t len);

typedef struct {
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t rx_crc_errors;
    uint32_t rx_len_errors;
    uint32_t rx_drops;
} app_ir_stats_t;

esp_err_t app_ir_init(void);
esp_err_t app_ir_start(void);
esp_err_t app_ir_send(const void *data, size_t len);
void app_ir_set_rx_cb(app_ir_rx_cb_t cb);
void app_ir_get_stats(app_ir_stats_t *stats);

esp_err_t app_ir_send_can(const bsp_twai_msg_t *msg);
esp_err_t app_ir_parse_can(const uint8_t *frame, size_t frame_len, bsp_twai_msg_t *msg);
