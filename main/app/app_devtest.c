#include "app_devtest.h"

#include <inttypes.h>
#include <string.h>

#include <stdio.h>

#include "app_espnow.h"
#include "app_espnow_device.h"
#include "app_twai.h"
#include "app_webui.h"
#include "bsp_display.h"
#include "bsp_espnow.h"
#include "bsp_ws2812.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_config.h"

static const char *TAG = "devtest";

#define ESPNOW_BROADCAST_INTERVAL_MS 100
#define ESPNOW_HEARTBEAT_INTERVAL_MS 2000
#define ESPNOW_CMD_INTERVAL_MS 1000
#define ESPNOW_CMD_TIMEOUT_MS 500
#define ESPNOW_RX_BUF_SIZE 256

#define TWAI_TEST_TX_INTERVAL_MS 1000

static volatile uint32_t s_heartbeat;
static volatile uint32_t s_twai_tx_count;

#if WIRELESSID_ESPNOW_BASE_ENABLE
static espnow_base_t s_base;
static volatile uint32_t s_bcast_sent;
static volatile uint32_t s_cmd_ok;
static volatile uint32_t s_cmd_fail;
#endif

#if WIRELESSID_ESPNOW_DEVICE_ENABLE
static espnow_device_t s_device;
static volatile uint32_t s_cmd_received;
static volatile uint32_t s_data_received;
#endif

static void twai_rx_cb(const bsp_twai_msg_t *msg) {
  char log_buf[48];
  snprintf(log_buf, sizeof(log_buf), "RX 0x%03X [%02X %02X %02X %02X %02X %02X %02X %02X]",
           (unsigned)msg->id,
           msg->dlc > 0 ? msg->data[0] : 0, msg->dlc > 1 ? msg->data[1] : 0,
           msg->dlc > 2 ? msg->data[2] : 0, msg->dlc > 3 ? msg->data[3] : 0,
           msg->dlc > 4 ? msg->data[4] : 0, msg->dlc > 5 ? msg->data[5] : 0,
           msg->dlc > 6 ? msg->data[6] : 0, msg->dlc > 7 ? msg->data[7] : 0);
  app_webui_log_twai(log_buf);
}

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
      char log_buf[48];
      snprintf(log_buf, sizeof(log_buf), "TX BCAST [%02X %02X %02X]",
               payload[0], payload[1], payload[2]);
      app_webui_log_espnow(log_buf);
    }

    vTaskDelay(pdMS_TO_TICKS(ESPNOW_BROADCAST_INTERVAL_MS));
  }
}

static void base_cmd_task(void *arg) {
  (void)arg;
  uint8_t cmd[8];
  uint8_t rsp[32];
  size_t rsp_len;

  ESP_LOGI(TAG, "cmd_task started");

  while (1) {
    espnow_peer_t peers[ESPNOW_MAX_PEERS];
    size_t peer_count = 0;
    espnow_base_get_peers(&s_base, peers, ESPNOW_MAX_PEERS, &peer_count);

    if (peer_count == 0) {
      ESP_LOGD(TAG, "no peers, skip cmd");
      vTaskDelay(pdMS_TO_TICKS(ESPNOW_CMD_INTERVAL_MS));
      continue;
    }

    ESP_LOGI(TAG, "cmd_task: %d peers online", (int)peer_count);

    for (size_t i = 0; i < peer_count; i++) {
      cmd[0] = 0x01;
      cmd[1] = (uint8_t)(s_cmd_ok & 0xFF);

      ESP_LOGI(TAG, "CMD_REQ to 0x%02x...", peers[i].device_id);

      esp_err_t ret = espnow_base_send_cmd_req(
          &s_base, peers[i].device_id, cmd, 2, rsp, sizeof(rsp), &rsp_len,
          ESPNOW_CMD_TIMEOUT_MS);

      if (ret == ESP_OK) {
        s_cmd_ok++;
        char log_buf[48];
        snprintf(log_buf, sizeof(log_buf), "TX CMD 0x%02X [%02X %02X]",
                 peers[i].device_id, cmd[0], cmd[1]);
        app_webui_log_espnow(log_buf);
        snprintf(log_buf, sizeof(log_buf), "RX RSP 0x%02X [%02X %02X %02X]",
                 peers[i].device_id, rsp_len > 0 ? rsp[0] : 0,
                 rsp_len > 1 ? rsp[1] : 0, rsp_len > 2 ? rsp[2] : 0);
        app_webui_log_espnow(log_buf);
        ESP_LOGI(TAG, "CMD ok, id=0x%02x rsp=[%02x %02x %02x]",
                 peers[i].device_id, rsp_len > 0 ? rsp[0] : 0,
                 rsp_len > 1 ? rsp[1] : 0, rsp_len > 2 ? rsp[2] : 0);
      } else {
        s_cmd_fail++;
        char log_buf[48];
        snprintf(log_buf, sizeof(log_buf), "!CMD fail 0x%02X err=%d",
                 peers[i].device_id, ret);
        app_webui_log_espnow(log_buf);
        ESP_LOGW(TAG, "CMD fail id=0x%02x: %d", peers[i].device_id, ret);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(ESPNOW_CMD_INTERVAL_MS));
  }
}
#endif

#if WIRELESSID_ESPNOW_DEVICE_ENABLE
static void device_cmd_handler(espnow_device_t *self, uint8_t base_id,
                               const uint8_t *cmd, size_t cmd_len, uint8_t *rsp,
                               size_t *rsp_len) {
  (void)self;
  s_cmd_received++;

  char log_buf[48];
  snprintf(log_buf, sizeof(log_buf), "RX CMD 0x%02X [%02X %02X]",
           base_id, cmd_len > 0 ? cmd[0] : 0, cmd_len > 1 ? cmd[1] : 0);
  app_webui_log_espnow(log_buf);

  ESP_LOGI(TAG, "CMD from 0x%02x [%d]: %02x %02x", base_id, (int)cmd_len,
           cmd_len > 0 ? cmd[0] : 0, cmd_len > 1 ? cmd[1] : 0);

  rsp[0] = 0xCC;
  rsp[1] = 0xDD;
  rsp[2] = (uint8_t)(s_cmd_received & 0xFF);
  *rsp_len = 3;

  snprintf(log_buf, sizeof(log_buf), "TX RSP 0x%02X [%02X %02X %02X]",
           base_id, rsp[0], rsp[1], rsp[2]);
  app_webui_log_espnow(log_buf);
}

static void device_data_handler(espnow_device_t *self, uint8_t src_id,
                                const uint8_t *data, size_t len) {
  (void)self;
  s_data_received++;
  char log_buf[48];
  snprintf(log_buf, sizeof(log_buf), "RX DATA 0x%02X [%d]", src_id, (int)len);
  app_webui_log_espnow(log_buf);
  ESP_LOGI(TAG, "DATA #%d from 0x%02x [%d]", (int)s_data_received, src_id,
           (int)len);
}

static void device_heartbeat_task(void *arg) {
  (void)arg;

  while (1) {
    espnow_device_send_heartbeat(&s_device);
    app_webui_log_espnow("TX HB");
    vTaskDelay(pdMS_TO_TICKS(ESPNOW_HEARTBEAT_INTERVAL_MS));
  }
}
#endif

#if WIRELESSID_TWAI_TEST_TX_ENABLE
static void twai_test_tx_task(void *arg) {
  (void)arg;

  ESP_LOGI(TAG, "twai_test_tx_task started, interval=%dms", TWAI_TEST_TX_INTERVAL_MS);

  while (1) {
    bsp_twai_msg_t msg = {
        .id = 0x123,
        .extd = false,
        .rtr = false,
        .dlc = 8,
        .data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
    };
    msg.data[0] = (uint8_t)(s_twai_tx_count & 0xFF);
    msg.data[1] = (uint8_t)((s_twai_tx_count >> 8) & 0xFF);

    esp_err_t ret = app_twai_transmit(&msg, pdMS_TO_TICKS(100));
    if (ret == ESP_OK) {
      s_twai_tx_count++;
      if (s_twai_tx_count % 100 == 0) {
        ESP_LOGI(TAG, "TWAI TX #%" PRIu32 " OK", s_twai_tx_count);
      }
    } else {
      ESP_LOGW(TAG, "TWAI TX fail: %d (0x%x)", ret, ret);
      twai_status_info_t st;
      if (bsp_twai_get_status(&st) == ESP_OK) {
        ESP_LOGW(TAG, "TWAI state=%d tx_err=%" PRIu32 " rx_err=%" PRIu32,
                 st.state, st.tx_error_counter, st.rx_error_counter);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(TWAI_TEST_TX_INTERVAL_MS));
  }
}
#endif

static void heartbeat_task(void *arg) {
  (void)arg;

  while (1) {
    s_heartbeat++;
    ESP_LOGI(TAG, "hb=%" PRIu32, s_heartbeat);

#if WIRELESSID_ESPNOW_BASE_ENABLE
    espnow_base_check_peers(&s_base, ESPNOW_PEER_TIMEOUT_MS);

    espnow_base_stats_t st;
    espnow_base_get_stats(&s_base, &st);

    size_t online = espnow_base_online_count(&s_base);
    bsp_display_printf(1, 0, "BASE online:%d", (int)online);

    size_t peer_count = 0;
    espnow_peer_t peers[ESPNOW_MAX_PEERS];
    espnow_base_get_peers(&s_base, peers, ESPNOW_MAX_PEERS, &peer_count);

    if (peer_count > 0) {
      bsp_display_printf(2, 0, "ID:%02x", peers[0].device_id);
    } else {
      bsp_display_printf(2, 0, "ID:--");
    }

    bsp_display_printf(3, 0, "CMD:%" PRIu32 "/%" PRIu32, s_cmd_ok, s_cmd_fail);
    bsp_display_printf(4, 0, "TX:%" PRIu32 " RX:%" PRIu32, st.tx_frames,
                       st.rx_frames);
#endif

#if WIRELESSID_ESPNOW_DEVICE_ENABLE
    bsp_display_printf(1, 0, "DEV 0x%02x", WIRELESSID_DEVICE_ID);
    bsp_display_printf(2, 0, "DATA:%" PRIu32 " CMD:%" PRIu32, s_data_received,
                       s_cmd_received);
#endif

    bsp_display_printf(4, 0, "hb:%" PRIu32, s_heartbeat);
    bsp_display_refresh();

#if WIRELESSID_TWAI_TEST_TX_ENABLE
    bsp_display_printf(5, 0, "TWAI TX:%" PRIu32, s_twai_tx_count);
#endif

#if WIRELESSID_ESPNOW_BASE_ENABLE
    {
      char peer_ids[WEBUI_PEER_STR_MAX] = "";
      size_t pos = 0;
      for (size_t i = 0; i < peer_count && pos < WEBUI_PEER_STR_MAX - 4; i++) {
        int n = snprintf(peer_ids + pos, WEBUI_PEER_STR_MAX - pos,
                         "%s%02X", i > 0 ? " " : "", peers[i].device_id);
        if (n > 0) pos += (size_t)n;
      }

      uint32_t twai_tx = 0, twai_rx = 0;
      bsp_twai_get_frame_counts(&twai_tx, &twai_rx);

      app_webui_status_t wst = {
          .uptime_sec = s_heartbeat,
          .device_id = WIRELESSID_DEVICE_ID,
          .peer_count = online,
          .twai_tx_frames = twai_tx,
          .twai_rx_frames = twai_rx,
          .twai_tx_drops = 0,
          .ir_tx_frames = 0,
          .ir_rx_frames = 0,
          .ir_rx_crc_err = 0,
          .espnow_tx_frames = st.tx_frames,
          .espnow_rx_frames = st.rx_frames,
          .espnow_announce_recv = st.announce_recv,
      };
      strncpy(wst.peer_ids, peer_ids, WEBUI_PEER_STR_MAX - 1);
      app_webui_update_status(&wst);
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void app_devtest_start(void) {
  bsp_display_clear();
  bsp_display_printf(0, 0, "WirelessID");

  app_webui_init(WIRELESSID_DEVICE_ID);

  app_twai_set_rx_cb(twai_rx_cb);

#if WIRELESSID_TWAI_TEST_TX_ENABLE
  xTaskCreate(twai_test_tx_task, "twai_tx", 4096, NULL, 4, NULL);
#endif

  bsp_espnow_register_recv_cb(espnow_rx_cb);

#if WIRELESSID_ESPNOW_BASE_ENABLE
  espnow_base_init(&s_base, WIRELESSID_DEVICE_ID, "Base");
  espnow_base_discover(&s_base);
  xTaskCreate(base_bcast_task, "en_bcast", 4096, NULL, 4, NULL);
  xTaskCreate(base_cmd_task, "en_cmd", 4096, NULL, 4, NULL);
#endif

#if WIRELESSID_ESPNOW_DEVICE_ENABLE
  espnow_device_init(&s_device, WIRELESSID_DEVICE_ID, "Device");
  espnow_device_set_cmd_cb(&s_device, device_cmd_handler);
  espnow_device_set_data_cb(&s_device, device_data_handler);
  espnow_device_announce(&s_device);
  xTaskCreate(device_heartbeat_task, "en_hb", 2048, NULL, 3, NULL);
#endif

  bsp_ws2812_play(BSP_WS2812_EFFECT_RAINBOW, 0, 0, 0, 128, 4000);
  xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 3, NULL);

  app_webui_start();
}
