#pragma once

#include <stddef.h>
#include <stdint.h>
#include "bsp_common.h"

esp_err_t bsp_i2c_init(void);
esp_err_t bsp_i2c_probe(uint8_t addr);
esp_err_t bsp_i2c_write(uint8_t addr, const uint8_t *data, size_t len);
esp_err_t bsp_i2c_read(uint8_t addr, uint8_t *data, size_t len);

/* Return the master bus handle for devices that need direct access */
void *bsp_i2c_get_bus_handle(void);
