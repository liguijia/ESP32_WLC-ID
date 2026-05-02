#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"

typedef struct {
    uart_port_t port;
    gpio_num_t tx_gpio;
    gpio_num_t rx_gpio;
    int baud_rate;
} pinmux_uart_config_t;

const pinmux_uart_config_t *pinmux_uart0_get_config(void);
