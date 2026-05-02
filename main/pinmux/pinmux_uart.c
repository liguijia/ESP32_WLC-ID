#include "pinmux_uart.h"
#include "project_config.h"

static const pinmux_uart_config_t s_uart0_config = {
    .port = UART_NUM_0,
    .tx_gpio = GPIO_NUM_NC,
    .rx_gpio = GPIO_NUM_NC,
    .baud_rate = WIRELESSID_DEFAULT_UART0_BAUD_RATE,
};

const pinmux_uart_config_t *pinmux_uart0_get_config(void)
{
    return &s_uart0_config;
}
