#pragma once

#include <stdbool.h>
#include "driver/gpio.h"

typedef struct {
    gpio_num_t tx_gpio;
    gpio_num_t rx_gpio;
    gpio_num_t standby_gpio;
    bool has_standby_gpio;
    bool standby_active_high;
} pinmux_twai_config_t;

const pinmux_twai_config_t *pinmux_twai_get_config(void);
