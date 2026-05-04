#include "app_twai.h"

#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define APP_TWAI_TASK_STACK 6144
#define APP_TWAI_TASK_PRIO 3
#define APP_TWAI_POLL_MS 50
#define APP_TWAI_ERR_LOG_MS 5000

static const char *TAG = "app_twai";

static app_twai_rx_cb_t s_rx_cb;
static app_twai_tx_done_cb_t s_tx_done_cb;
static app_twai_err_cb_t s_err_cb;

static void app_twai_default_on_rx(const bsp_twai_msg_t *msg) {
  ESP_LOGI(
      TAG,
      "RX id=0x%03" PRIx32 " dlc=%d [%02x %02x %02x %02x %02x %02x %02x %02x]",
      msg->id, msg->dlc, msg->data[0], msg->data[1], msg->data[2], msg->data[3],
      msg->data[4], msg->data[5], msg->data[6], msg->data[7]);
}

static void app_twai_default_on_err(uint32_t alerts, uint32_t tx_err,
                                    uint32_t rx_err) {
  ESP_LOGW(TAG, "CAN err=0x%" PRIx32 " tx=%" PRIu32 " rx=%" PRIu32, alerts,
           tx_err, rx_err);
}

static inline void app_twai_dispatch_rx(const bsp_twai_msg_t *msg) {
  if (s_rx_cb) {
    s_rx_cb(msg);
  } else {
    app_twai_default_on_rx(msg);
  }
}

static inline void app_twai_dispatch_err(uint32_t alerts, uint32_t tx_err,
                                         uint32_t rx_err) {
  if (s_err_cb) {
    s_err_cb(alerts, tx_err, rx_err);
  } else {
    app_twai_default_on_err(alerts, tx_err, rx_err);
  }
}

static void app_twai_handle_rx_alert(void) {
  bsp_twai_msg_t msg;
  while (bsp_twai_receive(&msg, 0) == ESP_OK) {
    app_twai_dispatch_rx(&msg);
  }
}

static void app_twai_handle_tx_success_alert(void) {
  if (s_tx_done_cb) {
    bsp_twai_msg_t empty = {0};
    s_tx_done_cb(&empty);
  }
}

static void app_twai_handle_error_alert(uint32_t alerts,
                                         uint32_t *last_err_ms) {
  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
  if (now - *last_err_ms < APP_TWAI_ERR_LOG_MS) {
    return;
  }

  twai_status_info_t st;
  bsp_twai_get_status(&st);
  app_twai_dispatch_err(alerts, st.tx_error_counter, st.rx_error_counter);
  *last_err_ms = now;
}

static void twai_task(void *arg) {
  (void)arg;

  while (1) {
    if (!bsp_twai_is_started()) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    uint32_t alerts = 0;
    bsp_twai_read_alerts(&alerts, pdMS_TO_TICKS(APP_TWAI_POLL_MS));

    if (alerts & TWAI_ALERT_RX_DATA) {
      app_twai_handle_rx_alert();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void app_twai_start(void) {
  xTaskCreate(twai_task, "can_app", APP_TWAI_TASK_STACK, NULL,
              APP_TWAI_TASK_PRIO, NULL);
}

esp_err_t app_twai_transmit(const bsp_twai_msg_t *msg,
                            TickType_t ticks_to_wait) {
  if (msg == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!bsp_twai_is_started()) {
    return ESP_ERR_INVALID_STATE;
  }

  return bsp_twai_transmit(msg, ticks_to_wait);
}

void app_twai_set_rx_cb(app_twai_rx_cb_t cb) { s_rx_cb = cb; }
void app_twai_set_tx_done_cb(app_twai_tx_done_cb_t cb) { s_tx_done_cb = cb; }
void app_twai_set_err_cb(app_twai_err_cb_t cb) { s_err_cb = cb; }
