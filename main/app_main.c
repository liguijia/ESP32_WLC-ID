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

static volatile uint32_t s_ir_tx_count;
static volatile uint32_t s_ir_rx_count;

#if WIRELESSID_IR_TEST_RX_ENABLE
#define IR_RX_TASK_STACK 3072
#define IR_RX_TASK_PRIO  3

static void ir_rx_task(void *arg) {
  (void)arg;
  uint8_t buf[64];

  while (1) {
    int n = bsp_ir_hw_read(buf, sizeof(buf) - 1, pdMS_TO_TICKS(100));
    if (n > 0) {
      buf[n] = '\0';
      s_ir_rx_count++;
      ESP_LOGI(TAG, "IR RX[%d]: %s", n, buf);
      bsp_display_printf(3, 0, "IR 9600 RX:%" PRIu32, s_ir_rx_count);
      bsp_display_refresh();
    }
  }
}
#endif

#if WIRELESSID_UART0_LOOPBACK_TEST_ENABLE
static void uart0_loopback_on_rx(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0) {
    return;
  }

  bsp_display_printf(5, 0, "RX:%u", (unsigned)len);
  bsp_display_refresh();

  app_uart0_send(data, len);
}
#endif

void app_main(void) {
  uint32_t heartbeat_count = 0;

  bsp_display_set_rotation(DISP_ROT_180);
  app_system_init_with_uart0_mode(APP_UART0_MODE_DEBUG);
  app_system_start();

#if WIRELESSID_UART0_LOOPBACK_TEST_ENABLE
  app_uart0_set_rx_cb(uart0_loopback_on_rx);
#endif

  bsp_ws2812_play(BSP_WS2812_EFFECT_RAINBOW, 0, 0, 0, 128, 4000);
  app_twai_start();

#if WIRELESSID_IR_TEST_RX_ENABLE
  xTaskCreate(ir_rx_task, "ir_rx", IR_RX_TASK_STACK, NULL, IR_RX_TASK_PRIO, NULL);
#endif

  bsp_display_clear();
  bsp_display_printf(0, 0, "WirelessID");
  bsp_display_printf(2, 0, "TWAI 1M");
  bsp_display_printf(3, 0, "IR 9600 RX:0");

  while (1) {
#if WIRELESSID_UART0_ALIVE_PROBE_ENABLE
    const char *probe = "U0_TX_ALIVE\r\n";
    app_uart0_send(probe, strlen(probe));
#endif

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
      int ir_len = snprintf(ir_payload, sizeof(ir_payload), "IR_HB:%" PRIu32 "\r\n", heartbeat_count);
      if (ir_len > 0) {
        int written = bsp_ir_hw_write(ir_payload, (size_t)ir_len);
        if (written > 0) {
          s_ir_tx_count++;
          ESP_LOGI(TAG, "IR TX[%d]: %s", written, ir_payload);
        } else {
          ESP_LOGW(TAG, "IR TX failed: %d", written);
        }
        bsp_display_printf(4, 0, "IR TX:%" PRIu32, s_ir_tx_count);
      }
    }
#endif

    app_uart0_stats_t st;
    app_uart0_get_stats(&st);
    bsp_display_printf(6, 0, "U0 RX:%" PRIu32 " TX:%" PRIu32, st.rx_frames,
                       st.tx_frames);

    heartbeat_count++;
    bsp_display_printf(4, 0, "beat: %" PRIu32, heartbeat_count);
    bsp_display_refresh();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
