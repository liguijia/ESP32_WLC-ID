#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bsp_common.h"
#include "driver/twai.h"

typedef struct {
    uint32_t id;
    bool     extd;
    bool     rtr;
    uint8_t  dlc;
    uint8_t  data[8];
} bsp_twai_msg_t;

typedef struct {
    uint32_t id;
    uint32_t mask;
    bool     extd;
} bsp_twai_filter_t;

esp_err_t bsp_twai_init(uint32_t baud_rate);
esp_err_t bsp_twai_init_no_ack(uint32_t baud_rate);
esp_err_t bsp_twai_config_filter(const bsp_twai_filter_t *filters, size_t count);
esp_err_t bsp_twai_start(void);
esp_err_t bsp_twai_stop(void);

esp_err_t bsp_twai_transmit(const bsp_twai_msg_t *msg, TickType_t timeout);
esp_err_t bsp_twai_receive(bsp_twai_msg_t *msg, TickType_t timeout);
esp_err_t bsp_twai_read_alerts(uint32_t *alerts, TickType_t timeout);

esp_err_t bsp_twai_get_status(twai_status_info_t *status);
bool bsp_twai_is_started(void);
