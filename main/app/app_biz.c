#include "app_biz.h"

#include <inttypes.h>
#include <string.h>

#include "app_ir.h"
#include "app_ir_master.h"
#include "app_ir_slave.h"
#include "app_twai.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_config.h"

static const char *TAG = "biz";

static biz_ctx_t *s_base_ctx = NULL;
static biz_ctx_t *s_device_ctx = NULL;
static ir_master_t s_master;
static ir_slave_t s_slave;

#define BIZ_BROADCAST_INTERVAL_MS  50
#define BIZ_CMD_TIMEOUT_MS         10

static void can_buf_init(can_ring_buf_t *buf) {
    memset(buf, 0, sizeof(*buf));
    buf->mutex = xSemaphoreCreateMutex();
}

static void can_buf_push(can_ring_buf_t *buf, const bsp_twai_msg_t *msg) {
    xSemaphoreTake(buf->mutex, portMAX_DELAY);
    buf->frames[buf->head] = *msg;
    buf->head = (buf->head + 1) % BIZ_CAN_BUF_SIZE;
    if (buf->count < BIZ_CAN_BUF_SIZE) buf->count++;
    xSemaphoreGive(buf->mutex);
}

static bool can_buf_get_latest(can_ring_buf_t *buf, bsp_twai_msg_t *msg) {
    xSemaphoreTake(buf->mutex, portMAX_DELAY);
    if (buf->count == 0) {
        xSemaphoreGive(buf->mutex);
        return false;
    }
    uint32_t latest = (buf->head + BIZ_CAN_BUF_SIZE - 1) % BIZ_CAN_BUF_SIZE;
    *msg = buf->frames[latest];
    xSemaphoreGive(buf->mutex);
    return true;
}

void biz_can_rx_push(biz_ctx_t *ctx, const bsp_twai_msg_t *msg) {
    if (ctx == NULL || msg == NULL) return;
    can_buf_push(&ctx->can_rx_buf, msg);
}

bool biz_can_get_latest(biz_ctx_t *ctx, bsp_twai_msg_t *msg) {
    if (ctx == NULL || msg == NULL) return false;
    return can_buf_get_latest(&ctx->can_rx_buf, msg);
}

void biz_get_stats(biz_ctx_t *ctx, uint32_t *tx, uint32_t *rx,
                   uint32_t *tx_err, uint32_t *rx_err) {
    if (ctx == NULL) return;
    if (tx) *tx = ctx->tx_frames;
    if (rx) *rx = ctx->rx_frames;
    if (tx_err) *tx_err = ctx->tx_errors;
    if (rx_err) *rx_err = ctx->rx_errors;
}

static void ir_rx_callback(const uint8_t *data, size_t len) {
    if (s_base_ctx != NULL) {
        ir_master_process_rx(&s_master, data, len);
    }

    if (s_device_ctx != NULL) {
        ir_slave_process_rx(&s_slave, data, len);
    }
}

static void device_ir_cmd_cb(ir_slave_t *self, uint8_t master_id,
                              const uint8_t *cmd, size_t cmd_len,
                              uint8_t *rsp, size_t *rsp_len) {
    (void)self;

    biz_ctx_t *ctx = s_device_ctx;
    if (ctx == NULL) {
        *rsp_len = 0;
        return;
    }

    ctx->rx_frames++;

    if (cmd_len < 13) {
        *rsp_len = 0;
        return;
    }

    bsp_twai_msg_t rx_msg;
    rx_msg.id = ((uint32_t)cmd[0] << 24) | ((uint32_t)cmd[1] << 16) |
                ((uint32_t)cmd[2] << 8) | (uint32_t)cmd[3];
    rx_msg.dlc = cmd[4];
    memcpy(rx_msg.data, &cmd[5], 8);
    rx_msg.extd = 0;
    rx_msg.rtr = 0;

    biz_can_rx_push(ctx, &rx_msg);

    bsp_twai_msg_t tx_msg;
    tx_msg.id = rx_msg.id + 1;
    tx_msg.dlc = rx_msg.dlc;
    tx_msg.extd = 0;
    tx_msg.rtr = 0;
    for (int i = 0; i < 8; i++) {
        tx_msg.data[i] = rx_msg.data[i] + 1;
    }

    esp_err_t twai_ret = app_twai_transmit(&tx_msg, pdMS_TO_TICKS(5));
    if (twai_ret == ESP_OK) {
        ctx->tx_frames++;
    } else {
        ctx->tx_errors++;
    }

    rsp[0] = (uint8_t)((tx_msg.id >> 24) & 0xFF);
    rsp[1] = (uint8_t)((tx_msg.id >> 16) & 0xFF);
    rsp[2] = (uint8_t)((tx_msg.id >> 8) & 0xFF);
    rsp[3] = (uint8_t)(tx_msg.id & 0xFF);
    rsp[4] = tx_msg.dlc;
    memcpy(&rsp[5], tx_msg.data, 8);
    *rsp_len = 13;

    ctx->rx_frames++;

    ESP_LOGI(TAG, "DEV RX CAN 0x%03lx -> TX CAN 0x%03lx", rx_msg.id, tx_msg.id);
    bsp_display_printf(3, 0, "CAN 0x%03lx->0x%03lx", rx_msg.id, tx_msg.id);
}

static void base_broadcast_task(void *arg) {
    biz_ctx_t *ctx = (biz_ctx_t *)arg;

    ESP_LOGI(TAG, "base broadcast started, interval=%dms", BIZ_BROADCAST_INTERVAL_MS);
    bsp_display_printf(2, 0, "BASE BROADCASTING");

    while (1) {
        bsp_twai_msg_t latest;
        if (can_buf_get_latest(&ctx->can_rx_buf, &latest)) {
            uint8_t can_data[13];
            can_data[0] = (uint8_t)((latest.id >> 24) & 0xFF);
            can_data[1] = (uint8_t)((latest.id >> 16) & 0xFF);
            can_data[2] = (uint8_t)((latest.id >> 8) & 0xFF);
            can_data[3] = (uint8_t)(latest.id & 0xFF);
            can_data[4] = latest.dlc;
            memcpy(&can_data[5], latest.data, 8);

            esp_err_t ret = ir_master_send_cmd(&s_master, IR_PROTO_BROADCAST,
                                               can_data, 13);
            if (ret == ESP_OK) {
                ctx->tx_frames++;
                ESP_LOGD(TAG, "BC CAN 0x%03lx", latest.id);
            } else {
                ctx->tx_errors++;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BIZ_BROADCAST_INTERVAL_MS));
    }
}

static void device_task(void *arg) {
    biz_ctx_t *ctx = (biz_ctx_t *)arg;

    ir_slave_init(&s_slave, ctx->self_id);
    ir_slave_set_cmd_cb(&s_slave, device_ir_cmd_cb);

    ESP_LOGI(TAG, "device started, id=0x%02x", ctx->self_id);
    bsp_display_printf(2, 0, "DEV 0x%02x READY", ctx->self_id);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t biz_init(biz_ctx_t *ctx, biz_role_t role, uint8_t self_id) {
    if (ctx == NULL) return ESP_ERR_INVALID_ARG;

    memset(ctx, 0, sizeof(*ctx));
    ctx->role = role;
    ctx->self_id = self_id;
    can_buf_init(&ctx->can_rx_buf);

    if (role == BIZ_ROLE_BASE) {
        s_base_ctx = ctx;
    } else {
        s_device_ctx = ctx;
    }

    ESP_LOGI(TAG, "biz init role=%s id=0x%02x",
             role == BIZ_ROLE_BASE ? "BASE" : "DEVICE", self_id);
    return ESP_OK;
}

esp_err_t biz_start(biz_ctx_t *ctx) {
    if (ctx == NULL) return ESP_ERR_INVALID_STATE;

    app_ir_init();
    app_ir_set_rx_cb(ir_rx_callback);
    app_ir_start();

    ESP_LOGI(TAG, "IR started");

    if (ctx->role == BIZ_ROLE_BASE) {
        ir_master_init(&s_master, ctx->self_id);
        xTaskCreate(base_broadcast_task, "biz_bcast", 4096, ctx, 5, NULL);
        ESP_LOGI(TAG, "base broadcast task created");
        bsp_display_printf(2, 0, "BASE 0x%02x", ctx->self_id);
    } else {
        xTaskCreate(device_task, "biz_dev", 4096, ctx, 5, NULL);
        ESP_LOGI(TAG, "device task created");
    }

    return ESP_OK;
}
