#include "app_devtest.h"

#include <inttypes.h>
#include <string.h>

#include "app_ir_master.h"
#include "app_ir_slave.h"
#include "app_twai.h"
#include "bsp_display.h"
#include "bsp_ir_hw.h"
#include "bsp_ws2812.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_config.h"

static const char *TAG = "devtest";

#define DEVTEST_MASTER_ID  0x01
#define DEVTEST_SLAVE_ID   0x02
#define DEVTEST_CMD_INTERVAL_MS 1000
#define IR_RX_BUF_SIZE 256

static volatile uint32_t s_heartbeat;

#if WIRELESSID_IR_TEST_TX_ENABLE
static ir_master_t s_master;
static volatile uint32_t s_cmd_sent;
static volatile uint32_t s_cmd_ok;
static volatile uint32_t s_cmd_fail;
#endif

#if WIRELESSID_IR_TEST_RX_ENABLE
static ir_slave_t s_slave;
static volatile uint32_t s_cmd_received;
#endif

static void ir_rx_task(void *arg) {
  (void)arg;
  uint8_t buf[IR_RX_BUF_SIZE];

  while (1) {
    int n = bsp_ir_hw_read(buf, sizeof(buf), pdMS_TO_TICKS(100));
    if (n <= 0) {
      continue;
    }

#if WIRELESSID_IR_TEST_TX_ENABLE
    ir_master_process_rx(&s_master, buf, (size_t)n);
#endif

#if WIRELESSID_IR_TEST_RX_ENABLE
    ir_slave_process_rx(&s_slave, buf, (size_t)n);
#endif
  }
}

#if WIRELESSID_IR_TEST_TX_ENABLE
static void master_task(void *arg) {
  (void)arg;
  uint8_t cmd[8];
  uint8_t rsp[32];
  size_t rsp_len;

  while (1) {
    cmd[0] = 0x01;
    cmd[1] = (uint8_t)(s_cmd_sent & 0xFF);

    esp_err_t ret = ir_master_send_cmd_req_default(&s_master, DEVTEST_SLAVE_ID,
                                                   cmd, 2, rsp, sizeof(rsp), &rsp_len);
    if (ret == ESP_OK) {
      s_cmd_ok++;
      ESP_LOGI(TAG, "CMD ok, rsp_len=%d rsp=[%02x %02x %02x %02x %02x]",
               (int)rsp_len,
               rsp_len > 0 ? rsp[0] : 0xff,
               rsp_len > 1 ? rsp[1] : 0xff,
               rsp_len > 2 ? rsp[2] : 0xff,
               rsp_len > 3 ? rsp[3] : 0xff,
               rsp_len > 4 ? rsp[4] : 0xff);
    } else {
      s_cmd_fail++;
      ESP_LOGW(TAG, "CMD fail: %d", ret);
    }

    s_cmd_sent++;
    vTaskDelay(pdMS_TO_TICKS(DEVTEST_CMD_INTERVAL_MS));
  }
}
#endif

#if WIRELESSID_IR_TEST_RX_ENABLE
static void slave_cmd_handler(ir_slave_t *self, uint8_t master_id,
                              const uint8_t *cmd, size_t cmd_len,
                              uint8_t *rsp, size_t *rsp_len) {
  (void)self;
  s_cmd_received++;

  ESP_LOGI(TAG, "CMD from 0x%02x [%d]: %02x %02x",
           master_id, (int)cmd_len,
           cmd_len > 0 ? cmd[0] : 0, cmd_len > 1 ? cmd[1] : 0);

  rsp[0] = 0xAA;
  rsp[1] = 0xBB;
  rsp[2] = (uint8_t)(s_cmd_received & 0xFF);
  *rsp_len = 3;
}

static void slave_data_handler(ir_slave_t *self, uint8_t src_id,
                               const uint8_t *data, size_t len) {
  (void)self;
  ESP_LOGI(TAG, "DATA from 0x%02x [%d]", src_id, (int)len);
}
#endif

static void heartbeat_task(void *arg) {
  (void)arg;

  while (1) {
    s_heartbeat++;
    ESP_LOGI(TAG, "hb=%" PRIu32, s_heartbeat);

#if WIRELESSID_IR_TEST_TX_ENABLE
    ir_proto_stats_t master_stats;
    ir_master_get_stats(&s_master, &master_stats);
    bsp_display_printf(2, 0, "M CMD:%" PRIu32 "/%" PRIu32, s_cmd_ok, s_cmd_fail);
    bsp_display_printf(3, 0, "TX:%" PRIu32 " RX:%" PRIu32,
                       master_stats.tx_frames, master_stats.rx_frames);
#endif

#if WIRELESSID_IR_TEST_RX_ENABLE
    ir_proto_stats_t slave_stats;
    ir_slave_get_stats(&s_slave, &slave_stats);
    bsp_display_printf(2, 0, "S CMD:%" PRIu32, s_cmd_received);
    bsp_display_printf(3, 0, "TX:%" PRIu32 " RX:%" PRIu32,
                       slave_stats.tx_frames, slave_stats.rx_frames);
#endif

    bsp_display_printf(4, 0, "hb:%" PRIu32, s_heartbeat);
    bsp_display_refresh();

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void app_devtest_start(void) {
  bsp_display_clear();
  bsp_display_printf(0, 0, "WirelessID");

#if WIRELESSID_IR_TEST_TX_ENABLE
  bsp_display_printf(1, 0, "IR MASTER 0x%02x", DEVTEST_MASTER_ID);
  ir_master_init(&s_master, DEVTEST_MASTER_ID);
  xTaskCreate(master_task, "ir_master", 4096, NULL, 4, NULL);
#elif WIRELESSID_IR_TEST_RX_ENABLE
  bsp_display_printf(1, 0, "IR SLAVE 0x%02x", DEVTEST_SLAVE_ID);
  ir_slave_init(&s_slave, DEVTEST_SLAVE_ID);
  ir_slave_set_cmd_cb(&s_slave, slave_cmd_handler);
  ir_slave_set_data_cb(&s_slave, slave_data_handler);
#endif

  xTaskCreate(ir_rx_task, "ir_rx", 4096, NULL, 5, NULL);
  bsp_ws2812_play(BSP_WS2812_EFFECT_RAINBOW, 0, 0, 0, 128, 4000);
  xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 3, NULL);
}
