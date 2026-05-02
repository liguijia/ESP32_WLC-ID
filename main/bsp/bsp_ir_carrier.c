#include "bsp_ir_carrier.h"

#include <stdbool.h>
#include "driver/ledc.h"
#include "pinmux_ir.h"

#define BSP_IR_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BSP_IR_LEDC_TIMER LEDC_TIMER_0
#define BSP_IR_LEDC_CHANNEL LEDC_CHANNEL_0
#define BSP_IR_LEDC_RESOLUTION LEDC_TIMER_10_BIT

static const char *TAG = "bsp_ir_carrier";
static bool s_enabled;
static uint8_t s_duty_percent;
static bool s_ir_carrier_initialized;

static uint32_t duty_percent_to_raw(uint8_t duty_percent)
{
    const uint32_t max_duty = (1U << BSP_IR_LEDC_RESOLUTION) - 1U;
    return (max_duty * duty_percent) / 100U;
}

esp_err_t bsp_ir_carrier_init(void)
{
    const pinmux_ir_uart_config_t *config = pinmux_ir_uart_get_config();

    if (config->carrier_gpio == GPIO_NUM_NC || config->carrier_hz == 0U) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = BSP_IR_LEDC_MODE,
        .duty_resolution = BSP_IR_LEDC_RESOLUTION,
        .timer_num = BSP_IR_LEDC_TIMER,
        .freq_hz = config->carrier_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    const ledc_channel_config_t channel_config = {
        .gpio_num = config->carrier_gpio,
        .speed_mode = BSP_IR_LEDC_MODE,
        .channel = BSP_IR_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BSP_IR_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };

    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "ir carrier timer config failed");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "ir carrier channel config failed");

    s_enabled = false;
    s_duty_percent = config->carrier_duty_percent;
    s_ir_carrier_initialized = true;
    return ESP_OK;
}

esp_err_t bsp_ir_carrier_set_enabled(bool enabled)
{
    if (!s_ir_carrier_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_enabled = enabled;

    const uint32_t duty = s_enabled ? duty_percent_to_raw(s_duty_percent) : 0U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(BSP_IR_LEDC_MODE, BSP_IR_LEDC_CHANNEL, duty), TAG, "ir carrier set duty failed");
    return ledc_update_duty(BSP_IR_LEDC_MODE, BSP_IR_LEDC_CHANNEL);
}

esp_err_t bsp_ir_carrier_set_duty(uint8_t duty_percent)
{
    if (duty_percent > 100U) {
        return ESP_ERR_INVALID_ARG;
    }

    s_duty_percent = duty_percent;
    return bsp_ir_carrier_set_enabled(s_enabled);
}
