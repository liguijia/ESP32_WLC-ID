#include "app_devtest.h"
#include "app_system.h"
#include "app_twai.h"

#include "bsp_display.h"
#include "project_config.h"

void app_main(void) {
  bsp_display_set_rotation(DISP_ROT_180);
  app_system_init_with_uart0_mode(APP_UART0_MODE_DEBUG);
  app_system_start();

  app_twai_start();
  app_devtest_start();

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
