#pragma once

#include <stdbool.h>
#include "driver/gpio.h"

typedef struct {
    gpio_num_t gpio;
    bool active_low;
    bool external_pullup;
    gpio_int_type_t intr_type;
} pinmux_key_config_t;

const pinmux_key_config_t *pinmux_key_get_config(void);
