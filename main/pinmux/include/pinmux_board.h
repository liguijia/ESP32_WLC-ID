#pragma once

#include <stdbool.h>
#include "driver/gpio.h"

const char *pinmux_board_name(void);
const char *pinmux_board_version(void);

bool pinmux_board_has_twai(void);
bool pinmux_board_has_ir_uart(void);
bool pinmux_board_has_display(void);
bool pinmux_board_has_ws2812(void);

gpio_num_t pinmux_board_key_gpio(void);
bool pinmux_board_key_active_low(void);
bool pinmux_board_key_falling_edge_triggered(void);
