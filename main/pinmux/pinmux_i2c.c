#include "pinmux_i2c.h"
#include "project_config.h"

static const pinmux_i2c_config_t s_i2c_config = {
    .sda_gpio = GPIO_NUM_5,
    .scl_gpio = GPIO_NUM_4,
    .clk_speed_hz = WIRELESSID_DEFAULT_I2C_FREQ_HZ,
    .display_addr = WIRELESSID_DEFAULT_DISPLAY_I2C_ADDR,
};

const pinmux_i2c_config_t *pinmux_i2c_get_config(void)
{
    return &s_i2c_config;
}
