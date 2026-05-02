#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bsp_common.h"
#include "freertos/FreeRTOS.h"

esp_err_t bsp_ir_hw_init(void);
esp_err_t bsp_ir_hw_enable_tx_carrier(bool enabled);
esp_err_t bsp_ir_hw_set_tx_carrier_duty(uint8_t duty_percent);
int bsp_ir_hw_write(const void *data, size_t len);
int bsp_ir_hw_read(uint8_t *buf, size_t len, TickType_t ticks_to_wait);
