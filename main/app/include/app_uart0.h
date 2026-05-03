#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    APP_UART0_MODE_DEBUG = 0,
    APP_UART0_MODE_NORMAL,
} app_uart0_mode_t;

typedef void (*app_uart0_rx_cb_t)(const uint8_t *data, size_t len);

typedef struct {
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t rx_drops;
    uint32_t tx_errors;
} app_uart0_stats_t;

esp_err_t app_uart0_init(app_uart0_mode_t mode);
esp_err_t app_uart0_start(void);
esp_err_t app_uart0_send(const void *data, size_t len);
void app_uart0_set_rx_cb(app_uart0_rx_cb_t cb);
void app_uart0_get_stats(app_uart0_stats_t *stats);
