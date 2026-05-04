#include "app_devtest.h"

#include <inttypes.h>
#include <string.h>

#include "app_ir.h"
#include "app_twai.h"
#include "bsp_display.h"
#include "bsp_ws2812.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_config.h"

static const char *TAG = "devtest";

static volatile uint32_t s_ir_tx_count;
static volatile uint32_t s_ir_tx_fail;
static volatile uint32_t s_heartbeat;

#if WIRELESSID_IR_TEST_RX_ENABLE
static void ir_rx_cb(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0) {
    return;
  }

  bsp_twai_msg_t can_msg;
  esp_err_t ret = app_ir_parse_can(data, len, &can_msg);
  if (ret == ESP_OK) {
    app_twai_transmit(&can_msg, pdMS_TO_TICKS(10));
  }
}
#endif

#if WIRELESSID_IR_TEST_TX_ENABLE
#define IR_TX_TASK_STACK 4096
#define IR_TX_TASK_PRIO  4
#define IR_TX_INTERVAL_MS 40

static void ir_tx_task(void *arg) {
  (void)arg;
  uint32_t count = 0;

  while (1) {
    bsp_twai_msg_t ir_can = {
        .id = 0x114,
        .dlc = 8,
        .data = {0, 1, 2, 3, 4, 5, 6, 7},
    };
    ir_can.data[0] = (uint8_t)(count & 0xFF);

    esp_err_t ret = app_ir_send_can(&ir_can);
    if (ret == ESP_OK) {
      s_ir_tx_count++;
    } else {
      s_ir_tx_fail++;
    }

    count++;
    vTaskDelay(pdMS_TO_TICKS(IR_TX_INTERVAL_MS));
  }
}
#endif

static void heartbeat_task(void *arg) {
  (void)arg;

  while (1) {
    s_heartbeat++;
    ESP_LOGI(TAG, "hb=%" PRIu32, s_heartbeat);

#if WIRELESSID_TWAI_TEST_TX_ENABLE
    bsp_twai_msg_t tx = {
        .id = 0x114,
        .dlc = 8,
        .data = {0, 1, 2, 3, 4, 5, 6, 7},
    };
    tx.data[0] = (uint8_t)(s_heartbeat & 0xFF);
    app_twai_transmit(&tx, pdMS_TO_TICKS(10));
#endif

    app_ir_stats_t ir_st;
    app_ir_get_stats(&ir_st);
    bsp_display_printf(3, 0, "IR TX:%" PRIu32 " F:%" PRIu32, s_ir_tx_count, s_ir_tx_fail);
    bsp_display_printf(4, 0, "IR RX:%" PRIu32 " E:%" PRIu32, ir_st.rx_frames, ir_st.rx_crc_errors);
    bsp_display_printf(2, 0, "beat: %" PRIu32, s_heartbeat);
    bsp_display_refresh();

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void app_devtest_start(void) {
  bsp_display_clear();
  bsp_display_printf(0, 0, "WirelessID");
  bsp_display_printf(1, 0, "TWAI 1M IR 4800");

  bsp_ws2812_play(BSP_WS2812_EFFECT_RAINBOW, 0, 0, 0, 128, 4000);

  app_ir_init();

#if WIRELESSID_IR_TEST_RX_ENABLE
  app_ir_set_rx_cb(ir_rx_cb);
  app_ir_start();
#endif

#if WIRELESSID_IR_TEST_TX_ENABLE
  xTaskCreate(ir_tx_task, "ir_tx", IR_TX_TASK_STACK, NULL, IR_TX_TASK_PRIO, NULL);
#endif

  xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 3, NULL);
}
