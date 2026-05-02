#include "bsp_ir_hw.h"

#include "bsp_ir_carrier.h"
#include "bsp_ir_uart.h"

esp_err_t bsp_ir_hw_init(void)
{
    esp_err_t uart_ret = bsp_ir_uart_init();
    esp_err_t carrier_ret = bsp_ir_carrier_init();

    if (uart_ret != ESP_OK) {
        return uart_ret;
    }

    if (carrier_ret != ESP_OK) {
        return carrier_ret;
    }

    return bsp_ir_carrier_set_enabled(false);
}

esp_err_t bsp_ir_hw_enable_tx_carrier(bool enabled)
{
    return bsp_ir_carrier_set_enabled(enabled);
}

esp_err_t bsp_ir_hw_set_tx_carrier_duty(uint8_t duty_percent)
{
    return bsp_ir_carrier_set_duty(duty_percent);
}

int bsp_ir_hw_write(const void *data, size_t len)
{
    if (data == NULL || len == 0U) {
        return -1;
    }

    if (bsp_ir_carrier_set_enabled(true) != ESP_OK) {
        return -1;
    }

    int written = bsp_ir_uart_write(data, len);
    if (written > 0) {
        (void)bsp_ir_uart_wait_tx_done(pdMS_TO_TICKS(1000));
    }

    (void)bsp_ir_carrier_set_enabled(false);
    return written;
}

int bsp_ir_hw_read(uint8_t *buf, size_t len, TickType_t ticks_to_wait)
{
    return bsp_ir_uart_read(buf, len, ticks_to_wait);
}
