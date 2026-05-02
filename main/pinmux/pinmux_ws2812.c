#include "pinmux_ws2812.h"

static const pinmux_ws2812_config_t s_ws2812_config = {
    .data_gpio = GPIO_NUM_3,
    .led_count = 1,
};

const pinmux_ws2812_config_t *pinmux_ws2812_get_config(void)
{
    return &s_ws2812_config;
}
