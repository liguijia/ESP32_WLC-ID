#pragma once

#include <stddef.h>
#include <stdint.h>
#include "bsp_common.h"
#include "esp_now.h"

typedef void (*bsp_espnow_recv_cb_t)(const uint8_t *mac, const uint8_t *data, int len);
typedef void (*bsp_espnow_send_cb_t)(const uint8_t *mac, esp_now_send_status_t status);

esp_err_t bsp_espnow_init(void);
esp_err_t bsp_espnow_register_recv_cb(bsp_espnow_recv_cb_t cb);
esp_err_t bsp_espnow_register_send_cb(bsp_espnow_send_cb_t cb);
esp_err_t bsp_espnow_send(const uint8_t *mac, const uint8_t *data, size_t len);
