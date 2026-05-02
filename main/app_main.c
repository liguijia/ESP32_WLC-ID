#include "app_system.h"

#include <stdint.h>

#include "app_twai.h"
#include "bsp_display.h"
#include "bsp_ws2812.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_config.h"

static const char *TAG = WIRELESSID_LOG_TAG;
static bsp_twai_msg_t s_last_rx;

static void twai_on_rx(const bsp_twai_msg_t *msg)
{
    ESP_LOGI(TAG, "RX id=0x%03lx dlc=%d [%02x %02x %02x %02x %02x %02x %02x %02x]",
             msg->id, msg->dlc,
             msg->data[0], msg->data[1], msg->data[2], msg->data[3],
             msg->data[4], msg->data[5], msg->data[6], msg->data[7]);
    s_last_rx = *msg;
}

static void twai_on_err(uint32_t alerts, uint32_t tx_err, uint32_t rx_err)
{
    ESP_LOGW(TAG, "CAN err=0x%lx tx=%lu rx=%lu", alerts, tx_err, rx_err);
}

void app_main(void)
{
    uint32_t heartbeat_count = 0;

    bsp_display_set_rotation(DISP_ROT_180);
    app_system_init();
    app_system_start();

    bsp_ws2812_play(BSP_WS2812_EFFECT_RAINBOW, 0, 0, 0, 128, 4000);

    app_twai_set_rx_cb(twai_on_rx);
    app_twai_set_err_cb(twai_on_err);
    app_twai_start();

    bsp_display_clear();
    bsp_display_printf(0, 0, "WirelessID");
    bsp_display_printf(2, 0, "TWAI 1M");

    while (1) {
        ESP_LOGI(TAG, "hb=%lu", heartbeat_count);

        bsp_twai_msg_t tx = {
            .id   = 0x114,
            .dlc  = 8,
            .data = {0, 1, 2, 3, 4, 5, 6, 7},
        };
        tx.data[0] = (uint8_t)(heartbeat_count & 0xFF);
        bsp_twai_transmit(&tx, pdMS_TO_TICKS(10));

        heartbeat_count++;
        bsp_display_printf(4, 0, "beat: %lu", heartbeat_count);
        if (s_last_rx.dlc) {
            bsp_display_printf(5, 0, "RX:%03lx L%d", s_last_rx.id, s_last_rx.dlc);
        }
        bsp_display_refresh();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
