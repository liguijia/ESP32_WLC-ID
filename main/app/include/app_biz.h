#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_espnow.h"
#include "app_espnow_device.h"
#include "app_webui.h"
#include "bsp_twai.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define BIZ_CAN_BUF_SIZE      64
#define BIZ_POLL_INTERVAL_MS  50
#define BIZ_CMD_TIMEOUT_MS    10
#define BIZ_MAX_DEVICES       4
#define BIZ_DATA_TIMEOUT_MS   100
#define BIZ_IR_HEARTBEAT_MS   200
#define BIZ_IR_TIMEOUT_MS     1000

typedef enum {
    BIZ_ROLE_BASE   = 0,
    BIZ_ROLE_DEVICE = 1,
} biz_role_t;

typedef struct {
    bsp_twai_msg_t frame;
    uint32_t timestamp_ms;
} can_frame_entry_t;

typedef struct {
    can_frame_entry_t entries[BIZ_CAN_BUF_SIZE];
    uint32_t head;
    uint32_t count;
    uint32_t last_push_ms;
    SemaphoreHandle_t mutex;
} can_ring_buf_t;

typedef struct {
    uint8_t device_id;
    bool espnow_online;
    bool ir_online;
    uint32_t last_ir_seen_ms;
    uint32_t last_espnow_seen_ms;
    uint32_t rx_count;
    uint32_t tx_count;
} biz_device_info_t;

typedef struct {
    biz_role_t role;
    uint8_t self_id;

    can_ring_buf_t can_rx_buf;

    biz_device_info_t devices[BIZ_MAX_DEVICES];
    uint32_t device_count;

    uint32_t poll_index;
    uint32_t poll_count;
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t tx_errors;
    uint32_t rx_errors;

    espnow_base_t espnow_base;
    espnow_device_t espnow_device;
} biz_ctx_t;

esp_err_t biz_init(biz_ctx_t *ctx, biz_role_t role, uint8_t self_id);
esp_err_t biz_start(biz_ctx_t *ctx);

void biz_can_rx_push(biz_ctx_t *ctx, const bsp_twai_msg_t *msg);
bool biz_can_get_latest(biz_ctx_t *ctx, bsp_twai_msg_t *msg);
bool biz_can_get_latest_if_fresh(biz_ctx_t *ctx, bsp_twai_msg_t *msg, uint32_t max_age_ms);
bool biz_can_has_fresh_data(biz_ctx_t *ctx, uint32_t max_age_ms);

void biz_get_stats(biz_ctx_t *ctx, uint32_t *tx, uint32_t *rx, uint32_t *tx_err, uint32_t *rx_err);
void biz_update_ir_online(biz_ctx_t *ctx, uint8_t device_id);
void biz_update_espnow_online(biz_ctx_t *ctx, uint8_t device_id);
void biz_check_timeouts(biz_ctx_t *ctx);
