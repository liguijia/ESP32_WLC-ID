#include "bsp_key.h"

#include <stdbool.h>
#include "driver/gpio.h"
#include "pinmux_key.h"

static const char *TAG = "bsp_key";
static bool s_isr_service_installed;

esp_err_t bsp_key_init(void)
{
    const pinmux_key_config_t *config = pinmux_key_get_config();

    if (config->gpio == GPIO_NUM_NC) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << config->gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->external_pullup ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = config->intr_type,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "key gpio config failed");

    if (!s_isr_service_installed) {
        esp_err_t ret = gpio_install_isr_service(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
        s_isr_service_installed = true;
    }

    ESP_RETURN_ON_ERROR(gpio_intr_disable(config->gpio), TAG, "key intr disable failed");
    return ESP_OK;
}

bool bsp_key_is_pressed(void)
{
    const pinmux_key_config_t *config = pinmux_key_get_config();
    int level = gpio_get_level(config->gpio);

    return config->active_low ? (level == 0) : (level != 0);
}
