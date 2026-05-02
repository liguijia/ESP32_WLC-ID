#include "bsp_ir_uart.h"

#include "driver/uart.h"
#include "pinmux_ir.h"

#define BSP_IR_UART_RX_BUFFER_SIZE 1024
#define BSP_IR_UART_TX_BUFFER_SIZE 0
#define BSP_IR_UART_EVENT_QUEUE_SIZE 0

static const char *TAG = "bsp_ir_uart";
static bool s_ir_uart_initialized;

esp_err_t bsp_ir_uart_init(void)
{
    const pinmux_ir_uart_config_t *config = pinmux_ir_uart_get_config();

    if (config->tx_gpio == GPIO_NUM_NC || config->rx_gpio == GPIO_NUM_NC) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_delete(config->uart_port);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ESP_RETURN_ON_ERROR(uart_param_config(config->uart_port, &uart_config), TAG, "ir uart param config failed");
    ESP_RETURN_ON_ERROR(
        uart_set_pin(config->uart_port, config->tx_gpio, config->rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
        TAG,
        "ir uart pin config failed");
    ESP_RETURN_ON_ERROR(
        uart_driver_install(
            config->uart_port,
            BSP_IR_UART_RX_BUFFER_SIZE,
            BSP_IR_UART_TX_BUFFER_SIZE,
            BSP_IR_UART_EVENT_QUEUE_SIZE,
            NULL,
            0),
        TAG,
        "ir uart driver install failed");

    s_ir_uart_initialized = true;
    return ESP_OK;
}

esp_err_t bsp_ir_uart_wait_tx_done(TickType_t ticks_to_wait)
{
    const pinmux_ir_uart_config_t *config = pinmux_ir_uart_get_config();

    if (!s_ir_uart_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return uart_wait_tx_done(config->uart_port, ticks_to_wait);
}

int bsp_ir_uart_write(const void *data, size_t len)
{
    const pinmux_ir_uart_config_t *config = pinmux_ir_uart_get_config();

    if (!s_ir_uart_initialized || data == NULL) {
        return -1;
    }

    return uart_write_bytes(config->uart_port, data, len);
}

int bsp_ir_uart_read(uint8_t *buf, size_t len, TickType_t ticks_to_wait)
{
    const pinmux_ir_uart_config_t *config = pinmux_ir_uart_get_config();

    if (!s_ir_uart_initialized || buf == NULL) {
        return -1;
    }

    return uart_read_bytes(config->uart_port, buf, len, ticks_to_wait);
}
