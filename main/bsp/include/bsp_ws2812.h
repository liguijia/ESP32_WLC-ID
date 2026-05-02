#pragma once

#include <stdint.h>
#include "bsp_common.h"

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} bsp_rgb_t;

typedef enum {
    BSP_WS2812_EFFECT_NONE = 0,
    BSP_WS2812_EFFECT_SOLID,
    BSP_WS2812_EFFECT_BREATH,
    BSP_WS2812_EFFECT_RAINBOW,
} bsp_ws2812_effect_t;

esp_err_t bsp_ws2812_init(void);

esp_err_t bsp_ws2812_play(bsp_ws2812_effect_t effect,
                          uint8_t r, uint8_t g, uint8_t b,
                          uint8_t max_brightness, uint32_t period_ms);
