#include "bsp_i2c.h"

#include <inttypes.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pinmux_i2c.h"

#define BSP_I2C_TIMEOUT_MS 100
#define BSP_I2C_MAX_DEVICES 8
#define BSP_I2C_LOG_THROTTLE_MS 2000

static const char *TAG = "bsp_i2c";
static bool s_initialized;
static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t s_i2c_mutex;

typedef struct {
    bool used;
    uint8_t addr;
    uint32_t scl_speed_hz;
    i2c_master_dev_handle_t dev;
} bsp_i2c_dev_slot_t;

static bsp_i2c_dev_slot_t s_devs[BSP_I2C_MAX_DEVICES];
static uint32_t s_last_err_log_ms;

static void i2c_log_throttled(const char *op, uint8_t addr, esp_err_t err)
{
    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if ((now_ms - s_last_err_log_ms) >= BSP_I2C_LOG_THROTTLE_MS) {
        ESP_LOGW(TAG, "%s failed addr=0x%02x err=%d", op, addr, err);
        s_last_err_log_ms = now_ms;
    }
}

static esp_err_t i2c_lock(TickType_t timeout)
{
    if (!s_i2c_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    return xSemaphoreTake(s_i2c_mutex, timeout) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void i2c_unlock(void)
{
    if (s_i2c_mutex) {
        xSemaphoreGive(s_i2c_mutex);
    }
}

static esp_err_t i2c_get_or_create_device_locked(uint8_t addr, uint32_t scl_speed_hz, i2c_master_dev_handle_t *out_dev)
{
    if (!out_dev || scl_speed_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < BSP_I2C_MAX_DEVICES; i++) {
        if (s_devs[i].used && s_devs[i].addr == addr && s_devs[i].scl_speed_hz == scl_speed_hz) {
            *out_dev = s_devs[i].dev;
            return ESP_OK;
        }
    }

    size_t free_idx = BSP_I2C_MAX_DEVICES;
    for (size_t i = 0; i < BSP_I2C_MAX_DEVICES; i++) {
        if (!s_devs[i].used) {
            free_idx = i;
            break;
        }
    }

    if (free_idx == BSP_I2C_MAX_DEVICES) {
        return ESP_ERR_NO_MEM;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = scl_speed_hz,
    };

    i2c_master_dev_handle_t dev = NULL;
    esp_err_t ret = i2c_master_bus_add_device(s_bus, &dev_cfg, &dev);
    if (ret != ESP_OK) {
        return ret;
    }

    s_devs[free_idx].used = true;
    s_devs[free_idx].addr = addr;
    s_devs[free_idx].scl_speed_hz = scl_speed_hz;
    s_devs[free_idx].dev = dev;
    *out_dev = dev;

    ESP_LOGI(TAG, "add cached dev addr=0x%02x speed=%" PRIu32, addr, scl_speed_hz);
    return ESP_OK;
}

esp_err_t bsp_i2c_init(void)
{
    const pinmux_i2c_config_t *cfg = pinmux_i2c_get_config();

    if (cfg->sda_gpio == GPIO_NUM_NC || cfg->scl_gpio == GPIO_NUM_NC) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    /*
     * 对于未加外部上拉的场景，显式打开内部上拉。
     * 这里同时保留 i2c_new_master_bus 的 enable_internal_pullup，双重保证。
     */
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(cfg->sda_gpio, GPIO_PULLUP_ONLY), TAG, "sda pullup");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(cfg->scl_gpio, GPIO_PULLUP_ONLY), TAG, "scl pullup");

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = cfg->sda_gpio,
        .scl_io_num = cfg->scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "new bus");

    s_i2c_mutex = xSemaphoreCreateMutex();
    if (!s_i2c_mutex) {
        return ESP_ERR_NO_MEM;
    }

    memset(s_devs, 0, sizeof(s_devs));
    s_last_err_log_ms = 0;

    s_initialized = true;
    return ESP_OK;
}

void *bsp_i2c_get_bus_handle(void)
{
    return s_initialized ? (void *)s_bus : NULL;
}

esp_err_t bsp_i2c_get_device(uint8_t addr, uint32_t scl_speed_hz, i2c_master_dev_handle_t *out_dev)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = i2c_lock(pdMS_TO_TICKS(BSP_I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_get_or_create_device_locked(addr, scl_speed_hz, out_dev);
    i2c_unlock();
    return ret;
}

esp_err_t bsp_i2c_device_transmit(i2c_master_dev_handle_t dev,
                                  const uint8_t *data,
                                  size_t data_len,
                                  int timeout_ms)
{
    if (!s_initialized || !dev || !data || data_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (timeout_ms < 0) {
        timeout_ms = -1;
    }

    TickType_t lock_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    esp_err_t ret = i2c_lock(lock_ticks);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_master_transmit(dev, data, data_len, timeout_ms);
    if (ret == ESP_ERR_TIMEOUT) {
        (void)i2c_master_bus_reset(s_bus);
    }
    i2c_unlock();
    return ret;
}

static esp_err_t do_transfer(uint8_t addr, const uint8_t *tx, size_t tx_len,
                              uint8_t *rx, size_t rx_len)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    const pinmux_i2c_config_t *cfg = pinmux_i2c_get_config();
    uint32_t speed = cfg->clk_speed_hz ? cfg->clk_speed_hz : 400000;

    esp_err_t ret = i2c_lock(pdMS_TO_TICKS(BSP_I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        i2c_log_throttled("lock", addr, ret);
        return ret;
    }

    i2c_master_dev_handle_t dev = NULL;
    ret = i2c_get_or_create_device_locked(addr, speed, &dev);
    if (ret != ESP_OK) {
        i2c_unlock();
        i2c_log_throttled("get_dev", addr, ret);
        return ret;
    }

    if (rx_len > 0 && tx_len > 0)
        ret = i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, BSP_I2C_TIMEOUT_MS);
    else if (tx_len > 0)
        ret = i2c_master_transmit(dev, tx, tx_len, BSP_I2C_TIMEOUT_MS);
    else if (rx_len > 0)
        ret = i2c_master_receive(dev, rx, rx_len, BSP_I2C_TIMEOUT_MS);
    else
        ret = ESP_ERR_INVALID_ARG;

    i2c_unlock();

    if (ret == ESP_ERR_TIMEOUT) {
        (void)i2c_master_bus_reset(s_bus);
    }

    if (ret != ESP_OK) {
        i2c_log_throttled("xfer", addr, ret);
    }

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
