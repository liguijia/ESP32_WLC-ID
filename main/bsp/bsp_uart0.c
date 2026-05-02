#include "bsp_uart0.h"

#include "driver/uart.h"
#include "pinmux_uart.h"

#define BSP_UART0_RX_BUFFER_SIZE 1024
#define BSP_UART0_TX_BUFFER_SIZE 0
#define BSP_UART0_EVENT_QUEUE_SIZE 0

static const char *TAG = "bsp_uart0";
static bool s_uart0_initialized;

esp_err_t bsp_uart0_init(void)
{
    const pinmux_uart_config_t *config = pinmux_uart0_get_config();

    const uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_delete(config->port);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ESP_RETURN_ON_ERROR(uart_param_config(config->port, &uart_config), TAG, "uart0 param config failed");
    ESP_RETURN_ON_ERROR(
        uart_set_pin(
            config->port,
            (config->tx_gpio == GPIO_NUM_NC) ? UART_PIN_NO_CHANGE : config->tx_gpio,
            (config->rx_gpio == GPIO_NUM_NC) ? UART_PIN_NO_CHANGE : config->rx_gpio,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE),
        TAG,
        "uart0 pin config failed");
    ESP_RETURN_ON_ERROR(
        uart_driver_install(
            config->port,
            BSP_UART0_RX_BUFFER_SIZE,
            BSP_UART0_TX_BUFFER_SIZE,
            BSP_UART0_EVENT_QUEUE_SIZE,
            NULL,
            0),
        TAG,
        "uart0 driver install failed");

    s_uart0_initialized = true;
    return ESP_OK;
}

int bsp_uart0_write(const void *data, size_t len)
{
    const pinmux_uart_config_t *config = pinmux_uart0_get_config();

    if (!s_uart0_initialized || data == NULL) {
        return -1;
    }

    return uart_write_bytes(config->port, data, len);
}

int bsp_uart0_read(uint8_t *buf, size_t len, TickType_t ticks_to_wait)
{
    const pinmux_uart_config_t *config = pinmux_uart0_get_config();

    if (!s_uart0_initialized || buf == NULL) {
        return -1;
    }

    return uart_read_bytes(config->port, buf, len, ticks_to_wait);
}
