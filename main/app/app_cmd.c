#include "app_cmd.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "cmd";

typedef struct {
    uint8_t cmd_type;
    cmd_handler_fn_t handler;
    char name[CMD_NAME_MAX_LEN];
} cmd_handler_entry_t;

static cmd_handler_entry_t s_handlers[CMD_MAX_HANDLERS];
static size_t s_handler_count = 0;
static SemaphoreHandle_t s_mutex = NULL;
static app_cmd_stats_t s_stats = {0};

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static cmd_handler_entry_t *find_handler(uint8_t cmd_type) {
    for (size_t i = 0; i < s_handler_count; i++) {
        if (s_handlers[i].cmd_type == cmd_type) {
            return &s_handlers[i];
        }
    }
    return NULL;
}

esp_err_t app_cmd_init(void) {
    if (s_mutex != NULL) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memset(s_handlers, 0, sizeof(s_handlers));
    s_handler_count = 0;
    memset(&s_stats, 0, sizeof(s_stats));

    ESP_LOGI(TAG, "cmd framework initialized");
    return ESP_OK;
}

esp_err_t app_cmd_register(uint8_t cmd_type, cmd_handler_fn_t handler,
                           const char *name) {
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (find_handler(cmd_type) != NULL) {
        xSemaphoreGive(s_mutex);
        ESP_LOGW(TAG, "handler for 0x%02x already registered", cmd_type);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_handler_count >= CMD_MAX_HANDLERS) {
        xSemaphoreGive(s_mutex);
        ESP_LOGE(TAG, "handler table full");
        return ESP_ERR_NO_MEM;
    }

    cmd_handler_entry_t *entry = &s_handlers[s_handler_count++];
    entry->cmd_type = cmd_type;
    entry->handler = handler;
    if (name != NULL) {
        strncpy(entry->name, name, CMD_NAME_MAX_LEN - 1);
        entry->name[CMD_NAME_MAX_LEN - 1] = '\0';
    } else {
        snprintf(entry->name, CMD_NAME_MAX_LEN, "cmd_0x%02x", cmd_type);
    }

    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "registered handler for 0x%02x: %s", cmd_type, entry->name);
    return ESP_OK;
}

esp_err_t app_cmd_unregister(uint8_t cmd_type) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (size_t i = 0; i < s_handler_count; i++) {
        if (s_handlers[i].cmd_type == cmd_type) {
            if (i < s_handler_count - 1) {
                memmove(&s_handlers[i], &s_handlers[i + 1],
                        (s_handler_count - i - 1) * sizeof(cmd_handler_entry_t));
            }
            s_handler_count--;
            xSemaphoreGive(s_mutex);
            ESP_LOGI(TAG, "unregistered handler for 0x%02x", cmd_type);
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_mutex);
    ESP_LOGW(TAG, "no handler found for 0x%02x", cmd_type);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t app_cmd_dispatch(cmd_context_t *ctx) {
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_stats.total_dispatched++;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    cmd_handler_entry_t *entry = find_handler(ctx->cmd_type);
    xSemaphoreGive(s_mutex);

    if (entry == NULL) {
        s_stats.total_no_handler++;
        ESP_LOGW(TAG, "no handler for cmd 0x%02x from src 0x%02x",
                 ctx->cmd_type, ctx->src_id);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGD(TAG, "dispatch cmd 0x%02x to %s", ctx->cmd_type, entry->name);

    esp_err_t ret = entry->handler(ctx);

    if (ret == ESP_OK) {
        s_stats.total_success++;
    } else {
        s_stats.total_fail++;
        ESP_LOGW(TAG, "handler %s returned %d", entry->name, ret);
    }

    return ret;
}

esp_err_t app_cmd_from_ir(uint8_t src_id, uint8_t cmd_type,
                          const uint8_t *payload, size_t len,
                          uint8_t *rsp_buf, size_t *rsp_len) {
    s_stats.ir_received++;

    cmd_context_t ctx = {
        .source = CMD_SRC_IR,
        .src_id = src_id,
        .dst_id = 0,
        .cmd_type = cmd_type,
        .seq = 0,
        .timestamp = now_ms(),
        .payload = payload,
        .payload_len = len,
        .rsp_buf = rsp_buf,
        .rsp_max_len = rsp_buf ? CMD_MAX_RSP_LEN : 0,
        .rsp_len = 0,
        .rsp_status = ESP_OK,
        .rsp_pending = (rsp_buf != NULL),
        .transport_ctx = NULL,
    };

    esp_err_t ret = app_cmd_dispatch(&ctx);

    if (rsp_len != NULL) {
        *rsp_len = ctx.rsp_len;
    }

    return ret;
}

esp_err_t app_cmd_from_espnow(uint8_t src_id, uint8_t cmd_type,
                              const uint8_t *payload, size_t len,
                              uint8_t *rsp_buf, size_t *rsp_len) {
    s_stats.espnow_received++;

    cmd_context_t ctx = {
        .source = CMD_SRC_ESPNOW,
        .src_id = src_id,
        .dst_id = 0,
        .cmd_type = cmd_type,
        .seq = 0,
        .timestamp = now_ms(),
        .payload = payload,
        .payload_len = len,
        .rsp_buf = rsp_buf,
        .rsp_max_len = rsp_buf ? CMD_MAX_RSP_LEN : 0,
        .rsp_len = 0,
        .rsp_status = ESP_OK,
        .rsp_pending = (rsp_buf != NULL),
        .transport_ctx = NULL,
    };

    esp_err_t ret = app_cmd_dispatch(&ctx);

    if (rsp_len != NULL) {
        *rsp_len = ctx.rsp_len;
    }

    return ret;
}

esp_err_t app_cmd_from_can(uint32_t can_id, const uint8_t *payload, size_t len) {
    s_stats.can_received++;

    cmd_context_t ctx = {
        .source = CMD_SRC_CAN,
        .src_id = (uint8_t)(can_id & 0xFF),
        .dst_id = 0,
        .cmd_type = CMD_TYPE_USER_MIN,
        .seq = 0,
        .timestamp = now_ms(),
        .payload = payload,
        .payload_len = len,
        .rsp_buf = NULL,
        .rsp_max_len = 0,
        .rsp_len = 0,
        .rsp_status = ESP_OK,
        .rsp_pending = false,
        .transport_ctx = NULL,
    };

    return app_cmd_dispatch(&ctx);
}

esp_err_t app_cmd_from_uart(const uint8_t *payload, size_t len) {
    s_stats.uart_received++;

    if (len < 2) {
        return ESP_ERR_INVALID_SIZE;
    }

    cmd_context_t ctx = {
        .source = CMD_SRC_UART,
        .src_id = 0,
        .dst_id = 0,
        .cmd_type = payload[0],
        .seq = 0,
        .timestamp = now_ms(),
        .payload = &payload[1],
        .payload_len = len - 1,
        .rsp_buf = NULL,
        .rsp_max_len = 0,
        .rsp_len = 0,
        .rsp_status = ESP_OK,
        .rsp_pending = false,
        .transport_ctx = NULL,
    };

    return app_cmd_dispatch(&ctx);
}

esp_err_t app_cmd_send_local(uint8_t dst_id, uint8_t cmd_type,
                             const uint8_t *payload, size_t len,
                             uint8_t *rsp_buf, size_t rsp_max,
                             size_t *rsp_len, uint32_t timeout_ms) {
    cmd_context_t ctx = {
        .source = CMD_SRC_LOCAL,
        .src_id = 0,
        .dst_id = dst_id,
        .cmd_type = cmd_type,
        .seq = 0,
        .timestamp = now_ms(),
        .payload = payload,
        .payload_len = len,
        .rsp_buf = rsp_buf,
        .rsp_max_len = rsp_max,
        .rsp_len = 0,
        .rsp_status = ESP_OK,
        .rsp_pending = (rsp_buf != NULL),
        .transport_ctx = NULL,
    };

    esp_err_t ret = app_cmd_dispatch(&ctx);

    if (rsp_len != NULL) {
        *rsp_len = ctx.rsp_len;
    }

    return ret;
}

void app_cmd_get_stats(app_cmd_stats_t *stats) {
    if (stats != NULL) {
        *stats = s_stats;
    }
}

void app_cmd_print_handlers(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    ESP_LOGI(TAG, "registered handlers (%d):", (int)s_handler_count);
    for (size_t i = 0; i < s_handler_count; i++) {
        ESP_LOGI(TAG, "  [%d] type=0x%02x name=%s", (int)i,
                 s_handlers[i].cmd_type, s_handlers[i].name);
    }

    xSemaphoreGive(s_mutex);
}
