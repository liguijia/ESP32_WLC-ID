#pragma once

#include "bsp_twai.h"

typedef void (*app_twai_rx_cb_t)(const bsp_twai_msg_t *msg);
typedef void (*app_twai_tx_done_cb_t)(const bsp_twai_msg_t *msg);
typedef void (*app_twai_err_cb_t)(uint32_t alerts, uint32_t tx_err, uint32_t rx_err);

void app_twai_start(void);
void app_twai_set_rx_cb(app_twai_rx_cb_t cb);
void app_twai_set_tx_done_cb(app_twai_tx_done_cb_t cb);
void app_twai_set_err_cb(app_twai_err_cb_t cb);
