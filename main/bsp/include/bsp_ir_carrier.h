#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bsp_common.h"

esp_err_t bsp_ir_carrier_init(void);
esp_err_t bsp_ir_carrier_set_enabled(bool enabled);
esp_err_t bsp_ir_carrier_set_duty(uint8_t duty_percent);
