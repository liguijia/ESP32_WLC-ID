#include "bsp_twai.h"

#include <inttypes.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pinmux_twai.h"

static const char *TAG = "bsp_twai";

static bool              s_initialized;
static bool              s_started;
static uint32_t          s_baud;
static twai_mode_t       s_mode;
static SemaphoreHandle_t s_tx_mutex;
static volatile uint32_t s_tx_frames;
static volatile uint32_t s_rx_frames;

static const twai_timing_config_t *get_timing(uint32_t baud)
{
    switch (baud) {
    case 1000000: { static const twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();    return &t; }
    case 800000:  { static const twai_timing_config_t t = TWAI_TIMING_CONFIG_800KBITS();  return &t; }
    case 500000:  { static const twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();  return &t; }
    case 250000:  { static const twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();  return &t; }
    case 125000:  { static const twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS();  return &t; }
    case 100000:  { static const twai_timing_config_t t = TWAI_TIMING_CONFIG_100KBITS();  return &t; }
    default:      { static const twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();  return &t; }
    }
}

static void msg_from_twai(bsp_twai_msg_t *dst, const twai_message_t *src)
{
    dst->id   = src->identifier;
    dst->extd = src->extd;
    dst->rtr  = src->rtr;
    dst->dlc  = src->data_length_code;
    memcpy(dst->data, src->data, src->data_length_code > 8 ? 8 : src->data_length_code);
}

static void msg_to_twai(twai_message_t *dst, const bsp_twai_msg_t *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->identifier       = src->id;
    dst->extd             = src->extd;
    dst->rtr              = src->rtr;
    dst->data_length_code = src->dlc > 8 ? 8 : src->dlc;
    memcpy(dst->data, src->data, dst->data_length_code);
}

static esp_err_t do_init_internal(uint32_t baud_rate, twai_mode_t mode)
{
    const pinmux_twai_config_t *cfg = pinmux_twai_get_config();

    if (cfg->tx_gpio == GPIO_NUM_NC || cfg->rx_gpio == GPIO_NUM_NC) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (cfg->has_standby_gpio && cfg->standby_gpio != GPIO_NUM_NC) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->standby_gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "stb gpio");
        ESP_RETURN_ON_ERROR(gpio_set_level(cfg->standby_gpio, cfg->standby_active_high ? 0 : 1),
                            TAG, "stb level");
    }

    if (s_started)      { twai_stop(); s_started = false; }
    if (s_initialized)  { twai_driver_uninstall(); s_initialized = false; }

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(cfg->tx_gpio, cfg->rx_gpio, mode);
    g.rx_queue_len  = 64;
    g.tx_queue_len  = 32;
    g.alerts_enabled = TWAI_ALERT_RX_DATA;

    const twai_timing_config_t *t = get_timing(baud_rate);
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_RETURN_ON_ERROR(twai_driver_install(&g, t, &f), TAG, "install");
    ESP_RETURN_ON_ERROR(twai_start(), TAG, "start");

    if (!s_tx_mutex) {
        s_tx_mutex = xSemaphoreCreateMutex();
    }

    s_initialized = true;
    s_started = true;
    s_baud = baud_rate;
    s_mode = mode;

    ESP_LOGI(TAG, "init baud=%" PRIu32 " mode=%d", baud_rate, mode);
    return ESP_OK;
}

esp_err_t bsp_twai_init(uint32_t baud_rate)         { return do_init_internal(baud_rate, TWAI_MODE_NORMAL); }
esp_err_t bsp_twai_init_no_ack(uint32_t baud_rate)  { return do_init_internal(baud_rate, TWAI_MODE_NO_ACK); }

esp_err_t bsp_twai_config_filter(const bsp_twai_filter_t *filters, size_t count)
{
    if (!s_initialized || !filters || count == 0) return ESP_ERR_INVALID_ARG;
    if (s_started) { twai_stop(); s_started = false; }
    twai_driver_uninstall();

    const pinmux_twai_config_t *cfg = pinmux_twai_get_config();
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(cfg->tx_gpio, cfg->rx_gpio, TWAI_MODE_NORMAL);
    g.rx_queue_len  = 32;
    g.tx_queue_len  = 16;
    g.alerts_enabled = TWAI_ALERT_ALL;

    const twai_timing_config_t *t = get_timing(s_baud);
    twai_filter_config_t f = { .acceptance_code = 0, .acceptance_mask = 0xFFFFFFFF, .single_filter = false };

    size_t n = count > 4 ? 4 : count;
    for (size_t i = 0; i < n; i++) {
        f.acceptance_code |= (filters[i].id & 0x7FF) << (i * 8);
        f.acceptance_mask |= (filters[i].mask & 0x7FF) << (i * 8);
    }

    ESP_RETURN_ON_ERROR(twai_driver_install(&g, t, &f), TAG, "reinstall");
    ESP_RETURN_ON_ERROR(twai_start(), TAG, "restart");
    s_started = true;
    return ESP_OK;
}

esp_err_t bsp_twai_start(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_started)      return ESP_OK;
    ESP_RETURN_ON_ERROR(twai_start(), TAG, "start");
    s_started = true;
    return ESP_OK;
}

esp_err_t bsp_twai_stop(void)
{
    if (!s_started) return ESP_OK;
    ESP_RETURN_ON_ERROR(twai_stop(), TAG, "stop");
    s_started = false;
    return ESP_OK;
}

esp_err_t bsp_twai_transmit(const bsp_twai_msg_t *msg, TickType_t timeout)
{
    if (!msg) return ESP_ERR_INVALID_ARG;

    if (!s_started) {
        esp_err_t ret = do_init_internal(s_baud, s_mode);
        if (ret != ESP_OK) return ret;
    }

    twai_status_info_t st;
    if (twai_get_status_info(&st) == ESP_OK) {
        if (st.state == TWAI_STATE_BUS_OFF || st.state == TWAI_STATE_STOPPED) {
            do_init_internal(s_baud, s_mode);
        }
    }

    twai_message_t tx;
    msg_to_twai(&tx, msg);
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    esp_err_t ret = twai_transmit(&tx, timeout);
    xSemaphoreGive(s_tx_mutex);
    if (ret == ESP_OK) {
        s_tx_frames++;
    }
    return ret;
}

esp_err_t bsp_twai_receive(bsp_twai_msg_t *msg, TickType_t timeout)
{
    if (!s_started || !msg) return ESP_ERR_INVALID_STATE;
    twai_message_t rx;
    esp_err_t ret = twai_receive(&rx, timeout);
    if (ret == ESP_OK) {
        msg_from_twai(msg, &rx);
        s_rx_frames++;
    }
    return ret;
}

esp_err_t bsp_twai_read_alerts(uint32_t *alerts, TickType_t timeout)
{
    if (!alerts) return ESP_ERR_INVALID_ARG;
    return twai_read_alerts(alerts, timeout);
}

esp_err_t bsp_twai_get_status(twai_status_info_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    return twai_get_status_info(status);
}

bool bsp_twai_is_started(void) { return s_started; }

void bsp_twai_get_frame_counts(uint32_t *tx_frames, uint32_t *rx_frames)
{
    if (tx_frames) *tx_frames = s_tx_frames;
    if (rx_frames) *rx_frames = s_rx_frames;
}
