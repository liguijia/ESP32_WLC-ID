#pragma once

#include <stddef.h>
#include <stdint.h>
#include "bsp_common.h"
#include "freertos/FreeRTOS.h"

esp_err_t bsp_ir_uart_init(void);
esp_err_t bsp_ir_uart_wait_tx_done(TickType_t ticks_to_wait);
int bsp_ir_uart_write(const void *data, size_t len);
int bsp_ir_uart_read(uint8_t *buf, size_t len, TickType_t ticks_to_wait);
