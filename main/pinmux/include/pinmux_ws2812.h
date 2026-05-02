#pragma once

#include <stdint.h>
#include "driver/gpio.h"

typedef struct {
    gpio_num_t data_gpio;
    uint8_t led_count;
} pinmux_ws2812_config_t;

const pinmux_ws2812_config_t *pinmux_ws2812_get_config(void);
