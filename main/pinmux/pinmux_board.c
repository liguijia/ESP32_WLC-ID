#include "pinmux_board.h"

const char *pinmux_board_name(void)
{
    return "WirelessID-ESP32C3";
}

const char *pinmux_board_version(void)
{
    return "revA-unknown";
}

bool pinmux_board_has_twai(void)
{
    return true;
}

bool pinmux_board_has_ir_uart(void)
{
    return true;
}

bool pinmux_board_has_display(void)
{
    return true;
}

bool pinmux_board_has_ws2812(void)
{
    return true;
}

gpio_num_t pinmux_board_key_gpio(void)
{
    return GPIO_NUM_10;
}

bool pinmux_board_key_active_low(void)
{
    return true;
}

bool pinmux_board_key_falling_edge_triggered(void)
{
    return true;
}
