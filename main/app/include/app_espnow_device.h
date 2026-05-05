#pragma once

#include "ir_proto_common.h"

#define ESPNOW_NAME_MAX_LEN 16

typedef struct espnow_device espnow_device_t;

typedef void (*espnow_device_cmd_cb_t)(espnow_device_t *self, uint8_t base_id,
                                       const uint8_t *cmd, size_t cmd_len,
                                       uint8_t *rsp, size_t *rsp_len);

typedef void (*espnow_device_data_cb_t)(espnow_device_t *self, uint8_t src_id,
                                        const uint8_t *data, size_t len);

struct espnow_device {
    uint8_t id;
    char name[ESPNOW_NAME_MAX_LEN];
    uint8_t seq;
    bool registered;
    espnow_device_cmd_cb_t on_cmd;
    espnow_device_data_cb_t on_data;
};

esp_err_t espnow_device_init(espnow_device_t *self, uint8_t id, const char *name);
void espnow_device_deinit(espnow_device_t *self);

void espnow_device_set_cmd_cb(espnow_device_t *self, espnow_device_cmd_cb_t cb);
void espnow_device_set_data_cb(espnow_device_t *self, espnow_device_data_cb_t cb);

esp_err_t espnow_device_announce(espnow_device_t *self);
esp_err_t espnow_device_send_heartbeat(espnow_device_t *self);

void espnow_device_process_rx(espnow_device_t *self, const uint8_t *mac,
                               const uint8_t *frame, size_t len);
