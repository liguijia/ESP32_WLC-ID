#include "app_system.h"
#include "app_twai.h"

#include <inttypes.h>
#include <stdint.h>

#include "bsp_display.h"
#include "bsp_ws2812.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_config.h"

static const char *TAG = WIRELESSID_LOG_TAG;

void app_main(void) {
  uint32_t heartbeat_count = 0;

  bsp_display_set_rotation(DISP_ROT_180);
  app_system_init();
  app_system_start();

  bsp_ws2812_play(BSP_WS2812_EFFECT_RAINBOW, 0, 0, 0, 128, 4000);
  app_twai_start();

  bsp_display_clear();
  bsp_display_printf(0, 0, "WirelessID");
  bsp_display_printf(2, 0, "TWAI 1M");

  while (1) {
    ESP_LOGI(TAG, "hb=%" PRIu32, heartbeat_count);

    bsp_twai_msg_t tx = {
        .id = 0x114,
        .dlc = 8,
        .data = {0, 1, 2, 3, 4, 5, 6, 7},
    };
    tx.data[0] = (uint8_t)(heartbeat_count & 0xFF);
    app_twai_transmit(&tx, pdMS_TO_TICKS(10));

    heartbeat_count++;
    bsp_display_printf(4, 0, "beat: %" PRIu32, heartbeat_count);
    bsp_display_refresh();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
