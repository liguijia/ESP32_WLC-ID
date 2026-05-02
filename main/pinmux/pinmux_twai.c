#include "pinmux_twai.h"

static const pinmux_twai_config_t s_twai_config = {
    .tx_gpio = GPIO_NUM_7,
    .rx_gpio = GPIO_NUM_6,
    .standby_gpio = GPIO_NUM_NC,
    .has_standby_gpio = false,
    .standby_active_high = true,
};

const pinmux_twai_config_t *pinmux_twai_get_config(void)
{
    return &s_twai_config;
}
