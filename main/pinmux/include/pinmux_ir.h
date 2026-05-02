#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/uart.h"

typedef struct {
    bool use_uart_mode;
    uart_port_t uart_port;
    gpio_num_t tx_gpio;
    gpio_num_t rx_gpio;
    gpio_num_t carrier_gpio;
    int baud_rate;
    uint32_t carrier_hz;
    uint8_t carrier_duty_percent;
    bool tx_inverted_before_gate;
    bool rx_is_demodulated_digital;
} pinmux_ir_uart_config_t;

const pinmux_ir_uart_config_t *pinmux_ir_uart_get_config(void);
