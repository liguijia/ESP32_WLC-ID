#include "bsp_i2c.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "pinmux_i2c.h"

#define BSP_I2C_TIMEOUT_MS 100

static const char *TAG = "bsp_i2c";
static bool s_initialized;
static i2c_master_bus_handle_t s_bus;

esp_err_t bsp_i2c_init(void)
{
    const pinmux_i2c_config_t *cfg = pinmux_i2c_get_config();

    if (cfg->sda_gpio == GPIO_NUM_NC || cfg->scl_gpio == GPIO_NUM_NC) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = cfg->sda_gpio,
        .scl_io_num = cfg->scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "new bus");

    s_initialized = true;
    return ESP_OK;
}

void *bsp_i2c_get_bus_handle(void)
{
    return s_initialized ? (void *)s_bus : NULL;
}

static esp_err_t do_transfer(uint8_t addr, const uint8_t *tx, size_t tx_len,
                              uint8_t *rx, size_t rx_len)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };

    i2c_master_dev_handle_t dev;
    esp_err_t ret = i2c_master_bus_add_device(s_bus, &dev_cfg, &dev);
    if (ret != ESP_OK) return ret;

    if (rx_len > 0 && tx_len > 0)
        ret = i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, pdMS_TO_TICKS(BSP_I2C_TIMEOUT_MS));
    else if (tx_len > 0)
        ret = i2c_master_transmit(dev, tx, tx_len, pdMS_TO_TICKS(BSP_I2C_TIMEOUT_MS));
    else if (rx_len > 0)
        ret = i2c_master_receive(dev, rx, rx_len, pdMS_TO_TICKS(BSP_I2C_TIMEOUT_MS));
    else
        ret = ESP_ERR_INVALID_ARG;

    i2c_master_bus_rm_device(dev);
    return ret;
}

esp_err_t bsp_i2c_probe(uint8_t addr)
{
    uint8_t dummy = 0;
    return do_transfer(addr, &dummy, 1, NULL, 0);
}

esp_err_t bsp_i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    if (!data || !len) return ESP_ERR_INVALID_ARG;
    return do_transfer(addr, data, len, NULL, 0);
}

esp_err_t bsp_i2c_read(uint8_t addr, uint8_t *data, size_t len)
{
    if (!data || !len) return ESP_ERR_INVALID_ARG;
    return do_transfer(addr, NULL, 0, data, len);
}
