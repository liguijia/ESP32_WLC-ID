#include "app_devtest.h"

#include <inttypes.h>
#include <string.h>

#include "app_espnow.h"
#include "app_espnow_device.h"
#include "bsp_display.h"
#include "bsp_espnow.h"
#include "bsp_ws2812.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_config.h"

static const char *TAG = "devtest";

#define ESPNOW_BROADCAST_INTERVAL_MS 10
#define ESPNOW_RX_BUF_SIZE 256

static volatile uint32_t s_heartbeat;

#if WIRELESSID_ESPNOW_BASE_ENABLE
static espnow_base_t s_base;
static volatile uint32_t s_bcast_sent;
static volatile uint32_t s_rsp_ok;
static volatile uint32_t s_rsp_fail;
#endif

#if WIRELESSID_ESPNOW_DEVICE_ENABLE
static espnow_device_t s_device;
static volatile uint32_t s_cmd_received;
static volatile uint32_t s_data_received;
#endif

static void espnow_rx_cb(const uint8_t *mac, const uint8_t *data, int len) {
  if (data == NULL || len <= 0) {
    return;
  }

#if WIRELESSID_ESPNOW_BASE_ENABLE
  espnow_base_process_rx(&s_base, mac, data, (size_t)len);
#endif

#if WIRELESSID_ESPNOW_DEVICE_ENABLE
  espnow_device_process_rx(&s_device, mac, data, (size_t)len);
#endif
}

#if WIRELESSID_ESPNOW_BASE_ENABLE
static void base_bcast_task(void *arg) {
  (void)arg;
  uint8_t payload[8];

  while (1) {
    payload[0] = 0xAA;
    payload[1] = 0xBB;
    payload[2] = (uint8_t)(s_bcast_sent & 0xFF);

    esp_err_t ret = espnow_base_broadcast(&s_base, payload, 3);
    if (ret == ESP_OK) {
      s_bcast_sent++;
      ESP_LOGI(TAG, "BCAST sent #%" PRIu32, s_bcast_sent);
    } else {
      ESP_LOGW(TAG, "BCAST fail: %d", ret);
    }

    vTaskDelay(pdMS_TO_TICKS(ESPNOW_BROADCAST_INTERVAL_MS));
  }
}
#endif

#if WIRELESSID_ESPNOW_DEVICE_ENABLE
static void device_cmd_handler(espnow_device_t *self, uint8_t base_id,
                               const uint8_t *cmd, size_t cmd_len, uint8_t *rsp,
                               size_t *rsp_len) {
  (void)self;
  s_cmd_received++;

  ESP_LOGI(TAG, "CMD from 0x%02x [%d]: %02x %02x", base_id, (int)cmd_len,
           cmd_len > 0 ? cmd[0] : 0, cmd_len > 1 ? cmd[1] : 0);

  rsp[0] = 0xCC;
  rsp[1] = 0xDD;
  rsp[2] = (uint8_t)(s_cmd_received & 0xFF);
  *rsp_len = 3;
}

static void device_data_handler(espnow_device_t *self, uint8_t src_id,
                                const uint8_t *data, size_t len) {
  (void)self;
  s_data_received++;
  ESP_LOGI(TAG, "DATA #%d from 0x%02x [%d]: %02x %02x %02x",
           (int)s_data_received, src_id, (int)len, len > 0 ? data[0] : 0,
           len > 1 ? data[1] : 0, len > 2 ? data[2] : 0);
}
#endif

static void heartbeat_task(void *arg) {
  (void)arg;

  while (1) {
    s_heartbeat++;
    ESP_LOGI(TAG, "hb=%" PRIu32, s_heartbeat);

#if WIRELESSID_ESPNOW_BASE_ENABLE
    espnow_base_stats_t st;
    espnow_base_get_stats(&s_base, &st);

    size_t peer_count = 0;
    espnow_peer_t peers[ESPNOW_MAX_PEERS];
    espnow_base_get_peers(&s_base, peers, ESPNOW_MAX_PEERS, &peer_count);

    bsp_display_printf(1, 0, "BASE peers:%d", (int)peer_count);
    bsp_display_printf(2, 0, "BC:%" PRIu32 " OK:%" PRIu32, s_bcast_sent,
                       st.rx_frames);
    bsp_display_printf(3, 0, "TX:%" PRIu32 " RX:%" PRIu32, st.tx_frames,
                       st.rx_frames);
#endif

#if WIRELESSID_ESPNOW_DEVICE_ENABLE
    bsp_display_printf(1, 0, "DEV 0x%02x", WIRELESSID_DEVICE_ID);
    bsp_display_printf(2, 0, "DATA:%" PRIu32 " CMD:%" PRIu32, s_data_received,
                       s_cmd_received);
#endif

    bsp_display_printf(4, 0, "hb:%" PRIu32, s_heartbeat);
    bsp_display_refresh();

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void app_devtest_start(void) {
  bsp_display_clear();
  bsp_display_printf(0, 0, "WirelessID");

  bsp_espnow_register_recv_cb(espnow_rx_cb);

#if WIRELESSID_ESPNOW_BASE_ENABLE
  espnow_base_init(&s_base, WIRELESSID_DEVICE_ID, "Base");
  xTaskCreate(base_bcast_task, "en_bcast", 4096, NULL, 4, NULL);
#endif

#if WIRELESSID_ESPNOW_DEVICE_ENABLE
  espnow_device_init(&s_device, WIRELESSID_DEVICE_ID, "Device");
  espnow_device_set_cmd_cb(&s_device, device_cmd_handler);
  espnow_device_set_data_cb(&s_device, device_data_handler);
  espnow_device_announce(&s_device);
#endif

  bsp_ws2812_play(BSP_WS2812_EFFECT_RAINBOW, 0, 0, 0, 128, 4000);
  xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 3, NULL);
}
