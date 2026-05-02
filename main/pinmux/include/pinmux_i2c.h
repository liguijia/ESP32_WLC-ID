#pragma once

#include <stdint.h>
#include "driver/gpio.h"

typedef struct {
    gpio_num_t sda_gpio;
    gpio_num_t scl_gpio;
    uint32_t clk_speed_hz;
    uint8_t display_addr;
} pinmux_i2c_config_t;

const pinmux_i2c_config_t *pinmux_i2c_get_config(void);
