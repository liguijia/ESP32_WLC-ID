#include "app_system.h"

#include "app_status.h"
#include "bsp_display.h"
#include "bsp_espnow.h"
#include "bsp_i2c.h"
#include "bsp_ir_hw.h"
#include "bsp_key.h"
#include "bsp_twai.h"
#include "bsp_uart0.h"
#include "bsp_ws2812.h"
#include "esp_log.h"
#include "pinmux_board.h"
#include "project_config.h"

static const char *TAG = WIRELESSID_LOG_TAG;

esp_err_t app_system_init(void) {
  esp_err_t ret;

  app_status_init();

  ESP_LOGI(TAG, "%s", WIRELESSID_STARTUP_BANNER);
  ESP_LOGI(TAG, "board=%s version=%s", pinmux_board_name(),
           pinmux_board_version());

  ret = bsp_ws2812_init();
  app_status_set_ws2812_ready(ret == ESP_OK);

  ret = bsp_key_init();
  app_status_set_key_ready(ret == ESP_OK);

  ret = bsp_uart0_init();
  app_status_set_uart0_ready(ret == ESP_OK);

  ret = bsp_i2c_init();
  app_status_set_display_ready(false);

  if (ret == ESP_OK) {
    ret = bsp_display_init();
    app_status_set_display_ready(ret == ESP_OK);
  }

  ret = bsp_twai_init_no_ack(WIRELESSID_DEFAULT_TWAI_BAUD_RATE);
  app_status_set_twai_ready(ret == ESP_OK);

  ret = bsp_espnow_init();
  app_status_set_espnow_ready(ret == ESP_OK);

  ret = bsp_ir_hw_init();
  app_status_set_ir_ready(ret == ESP_OK);

  return ESP_OK;
}

void app_system_start(void) {
  ESP_LOGI(TAG, "Application skeleton is ready for feature implementation");
}
