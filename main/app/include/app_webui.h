#pragma once

#include "bsp_common.h"

#define WEBUI_PEER_STR_MAX 128
#define WEBUI_LOG_LINES     12
#define WEBUI_LOG_LINE_MAX  48
#define WEBUI_MAX_DEVICES   4

typedef struct {
    uint8_t device_id;
    bool espnow_online;
    bool ir_online;
} webui_device_status_t;

typedef struct app_webui_status {
    uint32_t uptime_sec;
    uint8_t device_id;
    size_t peer_count;
    char peer_ids[WEBUI_PEER_STR_MAX];
    uint32_t twai_tx_frames;
    uint32_t twai_rx_frames;
    uint32_t twai_tx_drops;
    uint32_t ir_tx_frames;
    uint32_t ir_rx_frames;
    uint32_t ir_rx_crc_err;
    uint32_t espnow_tx_frames;
    uint32_t espnow_rx_frames;
    uint32_t espnow_announce_recv;
    webui_device_status_t devices[WEBUI_MAX_DEVICES];
    size_t device_count;
} app_webui_status_t;

typedef struct app_webui_log {
    char lines[WEBUI_LOG_LINES][WEBUI_LOG_LINE_MAX];
    uint8_t head;
    uint8_t count;
} app_webui_log_t;

esp_err_t app_webui_init(uint8_t device_id);
esp_err_t app_webui_start(void);
void app_webui_update_status(const app_webui_status_t *status);
void app_webui_log_twai(const char *msg);
void app_webui_log_ir(const char *msg);
void app_webui_log_espnow(const char *msg);
