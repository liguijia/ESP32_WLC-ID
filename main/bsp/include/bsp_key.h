#pragma once

#include <stdbool.h>
#include "bsp_common.h"

esp_err_t bsp_key_init(void);
bool bsp_key_is_pressed(void);
