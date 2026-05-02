#include "pinmux_ir.h"
#include "project_config.h"

static const pinmux_ir_uart_config_t s_ir_uart_config = {
    .use_uart_mode = true,
    .uart_port = UART_NUM_1,
    .tx_gpio = GPIO_NUM_1,
    .rx_gpio = GPIO_NUM_0,
    .carrier_gpio = GPIO_NUM_2,
    .baud_rate = WIRELESSID_DEFAULT_IR_UART_BAUD_RATE,
    .carrier_hz = WIRELESSID_DEFAULT_IR_CARRIER_HZ,
    .carrier_duty_percent = WIRELESSID_DEFAULT_IR_CARRIER_DUTY_PERCENT,
    .tx_inverted_before_gate = true,
    .rx_is_demodulated_digital = true,
};

const pinmux_ir_uart_config_t *pinmux_ir_uart_get_config(void)
{
    return &s_ir_uart_config;
}
