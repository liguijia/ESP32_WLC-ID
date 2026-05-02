#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool twai_ready;
    bool espnow_ready;
    bool display_ready;
    bool ir_ready;
    bool key_ready;
    bool uart0_ready;
    bool ws2812_ready;
    uint32_t twai_rx_count;
    uint32_t twai_tx_count;
    uint32_t espnow_rx_count;
    uint32_t espnow_tx_count;
    uint32_t ir_rx_count;
    uint32_t ir_tx_count;
    uint32_t uart0_rx_count;
    uint32_t uart0_tx_count;
} app_status_t;

void app_status_init(void);
app_status_t app_status_get(void);

void app_status_set_twai_ready(bool ready);
void app_status_set_espnow_ready(bool ready);
void app_status_set_display_ready(bool ready);
void app_status_set_ir_ready(bool ready);
void app_status_set_key_ready(bool ready);
void app_status_set_uart0_ready(bool ready);
void app_status_set_ws2812_ready(bool ready);
