#include "app_ir.h"
#include "app_system.h"
#include "app_twai.h"
#include "app_uart0.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "bsp_display.h"
#include "bsp_ir_hw.h"
#include "bsp_ws2812.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_config.h"

static const char *TAG = WIRELESSID_LOG_TAG;

#if WIRELESSID_IR_TEST_RX_ENABLE
static void ir_app_rx_cb(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0) {
    return;
  }

  ESP_LOGI(TAG, "IR RX[%d]: %.*s", (int)len, (int)len, data);
}
#endif

void app_main(void) {
  uint32_t heartbeat_count = 0;

  bsp_display_set_rotation(DISP_ROT_180);
  app_system_init_with_uart0_mode(APP_UART0_MODE_DEBUG);
  app_system_start();

  bsp_ws2812_play(BSP_WS2812_EFFECT_RAINBOW, 0, 0, 0, 128, 4000);
  app_twai_start();

  app_ir_init();

#if WIRELESSID_IR_TEST_RX_ENABLE
  app_ir_set_rx_cb(ir_app_rx_cb);
  app_ir_start();
#endif

  bsp_display_clear();
  bsp_display_printf(0, 0, "WirelessID");
  bsp_display_printf(1, 0, "TWAI 1M IR 4800");

  while (1) {
    ESP_LOGI(TAG, "hb=%" PRIu32, heartbeat_count);

#if WIRELESSID_TWAI_TEST_TX_ENABLE
    bsp_twai_msg_t tx = {
        .id = 0x114,
        .dlc = 8,
        .data = {0, 1, 2, 3, 4, 5, 6, 7},
    };
    tx.data[0] = (uint8_t)(heartbeat_count & 0xFF);
    app_twai_transmit(&tx, pdMS_TO_TICKS(10));
#endif

#if WIRELESSID_IR_TEST_TX_ENABLE
    {
      char ir_payload[32];
      int ir_len = snprintf(ir_payload, sizeof(ir_payload), "IR_HB:%" PRIu32,
                            heartbeat_count);
      if (ir_len > 0) {
        esp_err_t ret = app_ir_send(ir_payload, (size_t)ir_len);
        if (ret == ESP_OK) {
          app_ir_stats_t ir_st;
          app_ir_get_stats(&ir_st);
          ESP_LOGI(TAG, "IR TX[%d]: %s", ir_len, ir_payload);
          bsp_display_printf(3, 0, "IR TX:%" PRIu32, ir_st.tx_frames);
        } else {
          ESP_LOGW(TAG, "IR TX failed: %d", ret);
        }
      }
    }
#endif

    app_ir_stats_t ir_st;
    app_ir_get_stats(&ir_st);
    bsp_display_printf(4, 0, "IR RX:%" PRIu32 " E:%" PRIu32, ir_st.rx_frames,
                       ir_st.rx_crc_errors);

    heartbeat_count++;
    bsp_display_printf(2, 0, "beat: %" PRIu32, heartbeat_count);
    bsp_display_refresh();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
