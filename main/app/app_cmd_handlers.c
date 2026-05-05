#include "app_cmd_handlers.h"

#include <inttypes.h>
#include <string.h>

#include "app_cmd.h"
#include "bsp_display.h"
#include "bsp_ws2812.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_config.h"

static const char *TAG = "cmd_h";

static esp_err_t handle_query_status(cmd_context_t *ctx) {
    if (ctx->rsp_buf == NULL || ctx->rsp_max_len < 6) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000);

    ctx->rsp_buf[0] = WIRELESSID_DEVICE_ID;
    ctx->rsp_buf[1] = 0x01;
    ctx->rsp_buf[2] = (uint8_t)(uptime_sec & 0xFF);
    ctx->rsp_buf[3] = (uint8_t)((uptime_sec >> 8) & 0xFF);
    ctx->rsp_buf[4] = (uint8_t)((uptime_sec >> 16) & 0xFF);
    ctx->rsp_buf[5] = (uint8_t)((uptime_sec >> 24) & 0xFF);
    ctx->rsp_len = 6;
    ctx->rsp_status = ESP_OK;

    ESP_LOGI(TAG, "QUERY_STATUS from 0x%02x, uptime=%" PRIu32, ctx->src_id, uptime_sec);
    return ESP_OK;
}

static esp_err_t handle_query_version(cmd_context_t *ctx) {
    if (ctx->rsp_buf == NULL || ctx->rsp_max_len < 8) {
        return ESP_ERR_INVALID_SIZE;
    }

    const char *name = WIRELESSID_APP_NAME;
    size_t name_len = strlen(name);
    if (name_len > ctx->rsp_max_len - 4) {
        name_len = ctx->rsp_max_len - 4;
    }

    ctx->rsp_buf[0] = 0x01;
    ctx->rsp_buf[1] = 0x00;
    ctx->rsp_buf[2] = 0x01;
    memcpy(&ctx->rsp_buf[3], name, name_len);
    ctx->rsp_len = 3 + name_len;
    ctx->rsp_status = ESP_OK;

    ESP_LOGI(TAG, "QUERY_VERSION from 0x%02x", ctx->src_id);
    return ESP_OK;
}

static esp_err_t handle_query_stats(cmd_context_t *ctx) {
    if (ctx->rsp_buf == NULL || ctx->rsp_max_len < 8) {
        return ESP_ERR_INVALID_SIZE;
    }

    app_cmd_stats_t cmd_stats;
    app_cmd_get_stats(&cmd_stats);

    ctx->rsp_buf[0] = (uint8_t)(cmd_stats.total_dispatched & 0xFF);
    ctx->rsp_buf[1] = (uint8_t)((cmd_stats.total_dispatched >> 8) & 0xFF);
    ctx->rsp_buf[2] = (uint8_t)(cmd_stats.total_success & 0xFF);
    ctx->rsp_buf[3] = (uint8_t)((cmd_stats.total_success >> 8) & 0xFF);
    ctx->rsp_buf[4] = (uint8_t)(cmd_stats.total_fail & 0xFF);
    ctx->rsp_buf[5] = (uint8_t)((cmd_stats.total_fail >> 8) & 0xFF);
    ctx->rsp_buf[6] = (uint8_t)(cmd_stats.total_no_handler & 0xFF);
    ctx->rsp_buf[7] = (uint8_t)((cmd_stats.total_no_handler >> 8) & 0xFF);
    ctx->rsp_len = 8;
    ctx->rsp_status = ESP_OK;

    ESP_LOGI(TAG, "QUERY_STATS from 0x%02x", ctx->src_id);
    return ESP_OK;
}

static esp_err_t handle_reboot(cmd_context_t *ctx) {
    ESP_LOGW(TAG, "REBOOT command from 0x%02x", ctx->src_id);

    if (ctx->rsp_buf != NULL && ctx->rsp_max_len >= 2) {
        ctx->rsp_buf[0] = 0x01;
        ctx->rsp_buf[1] = 0x00;
        ctx->rsp_len = 2;
        ctx->rsp_status = ESP_OK;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();

    return ESP_OK;
}

static esp_err_t handle_led_ctrl(cmd_context_t *ctx) {
    if (ctx->payload_len < 3) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t r = ctx->payload[0];
    uint8_t g = ctx->payload[1];
    uint8_t b = ctx->payload[2];

    ESP_LOGI(TAG, "LED_CTRL from 0x%02x: R=%d G=%d B=%d", ctx->src_id, r, g, b);

    esp_err_t ret = bsp_ws2812_play(BSP_WS2812_EFFECT_SOLID, r, g, b, 128, 0);

    if (ctx->rsp_buf != NULL && ctx->rsp_max_len >= 2) {
        ctx->rsp_buf[0] = (ret == ESP_OK) ? 0x00 : 0x01;
        ctx->rsp_buf[1] = 0x00;
        ctx->rsp_len = 2;
        ctx->rsp_status = ret;
    }

    return ret;
}

static esp_err_t handle_led_effect(cmd_context_t *ctx) {
    if (ctx->payload_len < 4) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t effect = ctx->payload[0];
    uint8_t r = ctx->payload[1];
    uint8_t g = ctx->payload[2];
    uint8_t b = ctx->payload[3];
    uint8_t brightness = (ctx->payload_len > 4) ? ctx->payload[4] : 128;
    uint32_t period_ms = 0;

    if (ctx->payload_len > 5) {
        period_ms = ctx->payload[5] | (ctx->payload[6] << 8);
    }

    ESP_LOGI(TAG, "LED_EFFECT from 0x%02x: effect=%d R=%d G=%d B=%d",
             ctx->src_id, effect, r, g, b);

    esp_err_t ret = bsp_ws2812_play((bsp_ws2812_effect_t)effect, r, g, b,
                                    brightness, period_ms);

    if (ctx->rsp_buf != NULL && ctx->rsp_max_len >= 2) {
        ctx->rsp_buf[0] = (ret == ESP_OK) ? 0x00 : 0x01;
        ctx->rsp_buf[1] = effect;
        ctx->rsp_len = 2;
        ctx->rsp_status = ret;
    }

    return ret;
}

static esp_err_t handle_led_brightness(cmd_context_t *ctx) {
    if (ctx->payload_len < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t brightness = ctx->payload[0];

    ESP_LOGI(TAG, "LED_BRIGHTNESS from 0x%02x: %d", ctx->src_id, brightness);

    if (ctx->rsp_buf != NULL && ctx->rsp_max_len >= 2) {
        ctx->rsp_buf[0] = 0x00;
        ctx->rsp_buf[1] = brightness;
        ctx->rsp_len = 2;
        ctx->rsp_status = ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t handle_oled_clear(cmd_context_t *ctx) {
    ESP_LOGI(TAG, "OLED_CLEAR from 0x%02x", ctx->src_id);

    bsp_display_clear();
    bsp_display_refresh();

    if (ctx->rsp_buf != NULL && ctx->rsp_max_len >= 1) {
        ctx->rsp_buf[0] = 0x00;
        ctx->rsp_len = 1;
        ctx->rsp_status = ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t handle_oled_text(cmd_context_t *ctx) {
    if (ctx->payload_len < 3) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t row = ctx->payload[0];
    uint8_t col = ctx->payload[1];
    const char *text = (const char *)&ctx->payload[2];
    size_t text_len = ctx->payload_len - 2;

    char buf[32] = {0};
    if (text_len >= sizeof(buf)) {
        text_len = sizeof(buf) - 1;
    }
    memcpy(buf, text, text_len);

    ESP_LOGI(TAG, "OLED_TEXT from 0x%02x: row=%d col=%d text=%s", ctx->src_id, row, col, buf);

    bsp_display_printf(row, col, "%s", buf);

    if (ctx->rsp_buf != NULL && ctx->rsp_max_len >= 2) {
        ctx->rsp_buf[0] = 0x00;
        ctx->rsp_buf[1] = row;
        ctx->rsp_len = 2;
        ctx->rsp_status = ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t handle_oled_refresh(cmd_context_t *ctx) {
    ESP_LOGI(TAG, "OLED_REFRESH from 0x%02x", ctx->src_id);

    bsp_display_refresh();

    if (ctx->rsp_buf != NULL && ctx->rsp_max_len >= 1) {
        ctx->rsp_buf[0] = 0x00;
        ctx->rsp_len = 1;
        ctx->rsp_status = ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t handle_key_query(cmd_context_t *ctx) {
    ESP_LOGI(TAG, "KEY_QUERY from 0x%02x", ctx->src_id);

    if (ctx->rsp_buf != NULL && ctx->rsp_max_len >= 2) {
        ctx->rsp_buf[0] = 0x00;
        ctx->rsp_buf[1] = 0x00;
        ctx->rsp_len = 2;
        ctx->rsp_status = ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t handle_bridge_ir_esp(cmd_context_t *ctx) {
    if (ctx->payload_len < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t target_id = ctx->payload[0];
    size_t data_len = ctx->payload_len > 1 ? ctx->payload_len - 1 : 0;

    ESP_LOGI(TAG, "BRIDGE_IR_ESP from 0x%02x to 0x%02x, len=%d",
             ctx->src_id, target_id, (int)data_len);

    return ESP_OK;
}

esp_err_t app_cmd_handlers_init(void) {
    esp_err_t ret;

    ret = app_cmd_register(CMD_TYPE_QUERY_STATUS, handle_query_status, "query_status");
    if (ret != ESP_OK) return ret;

    ret = app_cmd_register(CMD_TYPE_QUERY_VERSION, handle_query_version, "query_version");
    if (ret != ESP_OK) return ret;

    ret = app_cmd_register(CMD_TYPE_QUERY_STATS, handle_query_stats, "query_stats");
    if (ret != ESP_OK) return ret;

    ret = app_cmd_register(CMD_TYPE_REBOOT, handle_reboot, "reboot");
    if (ret != ESP_OK) return ret;

    ret = app_cmd_register(CMD_TYPE_LED_CTRL, handle_led_ctrl, "led_ctrl");
    if (ret != ESP_OK) return ret;

    ret = app_cmd_register(CMD_TYPE_LED_EFFECT, handle_led_effect, "led_effect");
    if (ret != ESP_OK) return ret;

    ret = app_cmd_register(CMD_TYPE_LED_BRIGHTNESS, handle_led_brightness, "led_brightness");
    if (ret != ESP_OK) return ret;

    ret = app_cmd_register(CMD_TYPE_OLED_CLEAR, handle_oled_clear, "oled_clear");
    if (ret != ESP_OK) return ret;

    ret = app_cmd_register(CMD_TYPE_OLED_TEXT, handle_oled_text, "oled_text");
    if (ret != ESP_OK) return ret;

    ret = app_cmd_register(CMD_TYPE_OLED_REFRESH, handle_oled_refresh, "oled_refresh");
    if (ret != ESP_OK) return ret;

    ret = app_cmd_register(CMD_TYPE_KEY_QUERY, handle_key_query, "key_query");
    if (ret != ESP_OK) return ret;

    ret = app_cmd_register(CMD_TYPE_BRIDGE_IR_ESP, handle_bridge_ir_esp, "bridge_ir_esp");
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "builtin command handlers registered");
    return ESP_OK;
}
