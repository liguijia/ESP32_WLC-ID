#include "app_cmd.h"
#include "app_cmd_handlers.h"
#include "app_cmd_test.h"
#include "app_devtest.h"
#include "app_system.h"
#include "app_twai.h"

#include "bsp_display.h"
#include "esp_log.h"
#include "project_config.h"

static const char *TAG = "main";

void app_main(void) {
  bsp_display_set_rotation(DISP_ROT_180);
  app_system_init_with_uart0_mode(APP_UART0_MODE_DEBUG);
  app_system_start();

  esp_err_t ret = app_cmd_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "cmd init failed: %d", ret);
    return;
  }

  ret = app_cmd_handlers_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "cmd handlers init failed: %d", ret);
    return;
  }

  app_cmd_print_handlers();

  app_twai_start();
  app_devtest_start();

#if WIRELESSID_CMD_TEST_ENABLE
  app_cmd_test_start();
#endif

  ESP_LOGI(TAG, "system started");

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
