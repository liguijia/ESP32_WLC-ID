#include "pinmux_key.h"

static const pinmux_key_config_t s_key_config = {
    .gpio = GPIO_NUM_10,
    .active_low = true,
    .external_pullup = true,
    .intr_type = GPIO_INTR_NEGEDGE,
};

const pinmux_key_config_t *pinmux_key_get_config(void)
{
    return &s_key_config;
}
