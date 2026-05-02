#include "app_twai.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define APP_TWAI_TASK_STACK 6144
#define APP_TWAI_TASK_PRIO  3
#define APP_TWAI_POLL_MS    50
#define APP_TWAI_ERR_LOG_MS 5000

static const char *TAG = "app_twai";

static app_twai_rx_cb_t      s_rx_cb;
static app_twai_tx_done_cb_t s_tx_done_cb;
static app_twai_err_cb_t     s_err_cb;

static void twai_task(void *arg)
{
    (void)arg;
    uint32_t last_err_ms = 0;

    while (1) {
        if (!bsp_twai_is_started()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        uint32_t alerts = 0;
        bsp_twai_read_alerts(&alerts, pdMS_TO_TICKS(APP_TWAI_POLL_MS));

        if (alerts & TWAI_ALERT_RX_DATA) {
            bsp_twai_msg_t msg;
            while (bsp_twai_receive(&msg, 0) == ESP_OK) {
                if (s_rx_cb) s_rx_cb(&msg);
            }
        }

        if (alerts & TWAI_ALERT_TX_SUCCESS) {
            if (s_tx_done_cb) {
                bsp_twai_msg_t empty = {0};
                s_tx_done_cb(&empty);
            }
        }

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (alerts & (TWAI_ALERT_BUS_ERROR | TWAI_ALERT_ABOVE_ERR_WARN |
                      TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF)) {
            if (now - last_err_ms >= APP_TWAI_ERR_LOG_MS) {
                twai_status_info_t st;
                bsp_twai_get_status(&st);
                if (s_err_cb) s_err_cb(alerts, st.tx_error_counter, st.rx_error_counter);
                last_err_ms = now;
            }
        }
    }
}

void app_twai_start(void)
{
    xTaskCreate(twai_task, "can_app", APP_TWAI_TASK_STACK,
                NULL, APP_TWAI_TASK_PRIO, NULL);
}

void app_twai_set_rx_cb(app_twai_rx_cb_t cb)           { s_rx_cb = cb; }
void app_twai_set_tx_done_cb(app_twai_tx_done_cb_t cb) { s_tx_done_cb = cb; }
void app_twai_set_err_cb(app_twai_err_cb_t cb)         { s_err_cb = cb; }
