#include "app_ws2812_status.h"

#include "app_status.h"
#include "bsp_ws2812.h"
#include "esp_log.h"

static const char *TAG = "app_ws2812_status";

void app_ws2812_status_update(void)
{
    app_status_t status = app_status_get();
    bsp_rgb_t color;

    if (!status.ws2812_ready) {
        return;
    }

    bool all_ok = status.twai_ready && status.espnow_ready &&
                  status.display_ready && status.ir_ready &&
                  status.key_ready && status.uart0_ready;

    color.r = 0;
    color.g = 64;
    color.b = 0;

    if (all_ok) {
        color.g = 128;
    }

    if (!status.twai_ready || !status.espnow_ready ||
        !status.display_ready || !status.ir_ready ||
        !status.key_ready || !status.uart0_ready) {
        color.r = 128;
        color.g = 0;
    }

    ESP_LOGD(TAG, "status LED r=%d g=%d b=%d all_ok=%d",
             color.r, color.g, color.b, all_ok);

    bsp_ws2812_play(BSP_WS2812_EFFECT_SOLID, color.r, color.g, color.b, 0, 0);
}
