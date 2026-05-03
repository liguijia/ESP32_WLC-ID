#pragma once

#include "esp_err.h"
#include "app_uart0.h"

esp_err_t app_system_init(void);
esp_err_t app_system_init_with_uart0_mode(app_uart0_mode_t uart0_mode);
void app_system_start(void);
