#pragma once

#include <stddef.h>
#include <stdint.h>
#include "bsp_common.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"

esp_err_t bsp_i2c_init(void);
esp_err_t bsp_i2c_probe(uint8_t addr);
esp_err_t bsp_i2c_write(uint8_t addr, const uint8_t *data, size_t len);
esp_err_t bsp_i2c_read(uint8_t addr, uint8_t *data, size_t len);
esp_err_t bsp_i2c_transmit_receive(uint8_t addr,
                                   const uint8_t *tx, size_t tx_len,
                                   uint8_t *rx, size_t rx_len);

/* Return the master bus handle for devices that need direct access */
void *bsp_i2c_get_bus_handle(void);

/* Advanced API: cached device handle by (addr, speed) */
esp_err_t bsp_i2c_get_device(uint8_t addr, uint32_t scl_speed_hz, i2c_master_dev_handle_t *out_dev);
esp_err_t bsp_i2c_device_transmit(i2c_master_dev_handle_t dev,
                                  const uint8_t *data,
                                  size_t data_len,
                                  int timeout_ms);
