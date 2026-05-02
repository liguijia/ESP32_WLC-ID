#pragma once

#include <stddef.h>
#include <stdint.h>
#include "bsp_common.h"
#include "freertos/FreeRTOS.h"

esp_err_t bsp_uart0_init(void);
int bsp_uart0_write(const void *data, size_t len);
int bsp_uart0_read(uint8_t *buf, size_t len, TickType_t ticks_to_wait);
