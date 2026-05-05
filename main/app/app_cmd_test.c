#include "app_cmd_test.h"

#include <inttypes.h>
#include <string.h>

#include "app_cmd.h"
#include "app_webui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cmd_test";

static esp_err_t test_handler(cmd_context_t *ctx) {
    ESP_LOGI(TAG, "test_handler called: cmd=0x%02x, src=0x%02x, len=%d",
             ctx->cmd_type, ctx->src_id, (int)ctx->payload_len);

    if (ctx->payload_len > 0) {
        ESP_LOGI(TAG, "  payload: %02X %02X %02X %02X",
                 ctx->payload[0],
                 ctx->payload_len > 1 ? ctx->payload[1] : 0,
                 ctx->payload_len > 2 ? ctx->payload[2] : 0,
                 ctx->payload_len > 3 ? ctx->payload[3] : 0);
    }

    if (ctx->rsp_buf != NULL && ctx->rsp_max_len >= 4) {
        ctx->rsp_buf[0] = 0xAA;
        ctx->rsp_buf[1] = 0xBB;
        ctx->rsp_buf[2] = 0xCC;
        ctx->rsp_buf[3] = 0xDD;
        ctx->rsp_len = 4;
        ctx->rsp_status = ESP_OK;
        ESP_LOGI(TAG, "  response: AA BB CC DD");
    }

    return ESP_OK;
}

static void cmd_test_task(void *arg) {
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "=== CMD Framework Test Start ===");

    app_cmd_print_handlers();

    ESP_LOGI(TAG, "--- Test 1: Local QUERY_STATUS ---");
    {
        uint8_t rsp[32] = {0};
        size_t rsp_len = 0;
        esp_err_t ret = app_cmd_send_local(0, CMD_TYPE_QUERY_STATUS, NULL, 0,
                                           rsp, sizeof(rsp), &rsp_len, 500);
        ESP_LOGI(TAG, "  result: %d, rsp_len=%d", ret, (int)rsp_len);
        if (rsp_len > 0) {
            ESP_LOGI(TAG, "  rsp: %02X %02X %02X %02X %02X %02X",
                     rsp[0], rsp[1], rsp[2], rsp[3], rsp[4], rsp[5]);
        }
        app_webui_log_espnow("TEST: QUERY_STATUS OK");
    }

    ESP_LOGI(TAG, "--- Test 2: Local QUERY_VERSION ---");
    {
        uint8_t rsp[32] = {0};
        size_t rsp_len = 0;
        esp_err_t ret = app_cmd_send_local(0, CMD_TYPE_QUERY_VERSION, NULL, 0,
                                           rsp, sizeof(rsp), &rsp_len, 500);
        ESP_LOGI(TAG, "  result: %d, rsp_len=%d", ret, (int)rsp_len);
        if (rsp_len > 3) {
            ESP_LOGI(TAG, "  version: %02X.%02X.%02X name=%s",
                     rsp[0], rsp[1], rsp[2], &rsp[3]);
        }
        app_webui_log_espnow("TEST: QUERY_VERSION OK");
    }

    ESP_LOGI(TAG, "--- Test 3: Local QUERY_STATS ---");
    {
        uint8_t rsp[32] = {0};
        size_t rsp_len = 0;
        esp_err_t ret = app_cmd_send_local(0, CMD_TYPE_QUERY_STATS, NULL, 0,
                                           rsp, sizeof(rsp), &rsp_len, 500);
        ESP_LOGI(TAG, "  result: %d, rsp_len=%d", ret, (int)rsp_len);
        app_webui_log_espnow("TEST: QUERY_STATS OK");
    }

    ESP_LOGI(TAG, "--- Test 4: Local LED_CTRL ---");
    {
        uint8_t cmd[3] = {0xFF, 0x00, 0x00};
        uint8_t rsp[8] = {0};
        size_t rsp_len = 0;
        esp_err_t ret = app_cmd_send_local(0, CMD_TYPE_LED_CTRL, cmd, 3,
                                           rsp, sizeof(rsp), &rsp_len, 500);
        ESP_LOGI(TAG, "  result: %d, rsp_len=%d", ret, (int)rsp_len);
        app_webui_log_espnow("TEST: LED_CTRL OK");
    }

    ESP_LOGI(TAG, "--- Test 5: Local LED_EFFECT ---");
    {
        uint8_t cmd[5] = {0x02, 0x00, 0xFF, 0x00, 0x80};
        uint8_t rsp[8] = {0};
        size_t rsp_len = 0;
        esp_err_t ret = app_cmd_send_local(0, CMD_TYPE_LED_EFFECT, cmd, 5,
                                           rsp, sizeof(rsp), &rsp_len, 500);
        ESP_LOGI(TAG, "  result: %d, rsp_len=%d", ret, (int)rsp_len);
        app_webui_log_espnow("TEST: LED_EFFECT OK");
    }

    ESP_LOGI(TAG, "--- Test 6: Local OLED_TEXT ---");
    {
        uint8_t cmd[16] = {0x00, 0x00};
        memcpy(&cmd[2], "Hello CMD!", 10);
        uint8_t rsp[8] = {0};
        size_t rsp_len = 0;
        esp_err_t ret = app_cmd_send_local(0, CMD_TYPE_OLED_TEXT, cmd, 12,
                                           rsp, sizeof(rsp), &rsp_len, 500);
        ESP_LOGI(TAG, "  result: %d, rsp_len=%d", ret, (int)rsp_len);
        app_webui_log_espnow("TEST: OLED_TEXT OK");
    }

    ESP_LOGI(TAG, "--- Test 7: Local OLED_REFRESH ---");
    {
        uint8_t rsp[8] = {0};
        size_t rsp_len = 0;
        esp_err_t ret = app_cmd_send_local(0, CMD_TYPE_OLED_REFRESH, NULL, 0,
                                           rsp, sizeof(rsp), &rsp_len, 500);
        ESP_LOGI(TAG, "  result: %d, rsp_len=%d", ret, (int)rsp_len);
        app_webui_log_espnow("TEST: OLED_REFRESH OK");
    }

    ESP_LOGI(TAG, "--- Test 8: Register custom handler ---");
    {
        esp_err_t ret = app_cmd_register(CMD_TYPE_USER_MIN, test_handler, "test_cmd");
        ESP_LOGI(TAG, "  register result: %d", ret);

        uint8_t cmd[4] = {0x11, 0x22, 0x33, 0x44};
        uint8_t rsp[8] = {0};
        size_t rsp_len = 0;
        ret = app_cmd_send_local(0, CMD_TYPE_USER_MIN, cmd, 4,
                                 rsp, sizeof(rsp), &rsp_len, 500);
        ESP_LOGI(TAG, "  test_cmd result: %d, rsp_len=%d", ret, (int)rsp_len);
        app_webui_log_espnow("TEST: CUSTOM CMD OK");
    }

    ESP_LOGI(TAG, "--- Test 9: UART command format ---");
    ESP_LOGI(TAG, "  Send via UART: [cmd_type] [payload...]");
    ESP_LOGI(TAG, "  Example: 01 (QUERY_STATUS)");
    ESP_LOGI(TAG, "  Example: 02 (REBOOT)");
    ESP_LOGI(TAG, "  Example: 10 FF 00 00 (LED_CTRL R=255 G=0 B=0)");

    ESP_LOGI(TAG, "--- Test 10: IR command format ---");
    ESP_LOGI(TAG, "  IR payload: [cmd_type] [payload...]");
    ESP_LOGI(TAG, "  Use app_cmd_from_ir(src_id, cmd_type, payload, len, rsp, &rsp_len)");

    ESP_LOGI(TAG, "--- Test 11: ESP-NOW command format ---");
    ESP_LOGI(TAG, "  ESP-NOW payload: [cmd_type] [payload...]");
    ESP_LOGI(TAG, "  Use app_cmd_from_espnow(src_id, cmd_type, payload, len, rsp, &rsp_len)");

    ESP_LOGI(TAG, "=== CMD Framework Test Complete ===");

    app_cmd_stats_t stats;
    app_cmd_get_stats(&stats);
    ESP_LOGI(TAG, "Stats: dispatched=%" PRIu32 " success=%" PRIu32 " fail=%" PRIu32,
             stats.total_dispatched, stats.total_success, stats.total_fail);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        app_cmd_get_stats(&stats);
        ESP_LOGI(TAG, "Stats: dispatched=%" PRIu32 " success=%" PRIu32
                      " no_handler=%" PRIu32 " ir=%" PRIu32 " espnow=%" PRIu32,
                 stats.total_dispatched, stats.total_success,
                 stats.total_no_handler, stats.ir_received, stats.espnow_received);
    }
}

esp_err_t app_cmd_test_start(void) {
    ESP_LOGI(TAG, "starting cmd test task");

    BaseType_t ret = xTaskCreate(cmd_test_task, "cmd_test", 4096, NULL, 3, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create test task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
