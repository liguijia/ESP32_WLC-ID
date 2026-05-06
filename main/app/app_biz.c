#include "app_biz.h"

#include <inttypes.h>
#include <string.h>

#include "app_ir.h"
#include "app_twai.h"
#include "bsp_display.h"
#include "bsp_ir_hw.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ir_proto_common.h"
#include "project_config.h"

static const char *TAG = "biz";

static biz_ctx_t *s_base_ctx = NULL;
static biz_ctx_t *s_device_ctx = NULL;

#define BIZ_DEVICE_SEND_INTERVAL_MS  50

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void can_buf_init(can_ring_buf_t *buf) {
    memset(buf, 0, sizeof(*buf));
    buf->mutex = xSemaphoreCreateMutex();
}

static void can_buf_push(can_ring_buf_t *buf, const bsp_twai_msg_t *msg) {
    xSemaphoreTake(buf->mutex, portMAX_DELAY);
    buf->entries[buf->head].frame = *msg;
    buf->entries[buf->head].timestamp_ms = now_ms();
    buf->head = (buf->head + 1) % BIZ_CAN_BUF_SIZE;
    if (buf->count < BIZ_CAN_BUF_SIZE) buf->count++;
    buf->last_push_ms = now_ms();
    xSemaphoreGive(buf->mutex);
}

static bool can_buf_get_latest(can_ring_buf_t *buf, bsp_twai_msg_t *msg) {
    xSemaphoreTake(buf->mutex, portMAX_DELAY);
    if (buf->count == 0) {
        xSemaphoreGive(buf->mutex);
        return false;
    }
    uint32_t latest = (buf->head + BIZ_CAN_BUF_SIZE - 1) % BIZ_CAN_BUF_SIZE;
    *msg = buf->entries[latest].frame;
    xSemaphoreGive(buf->mutex);
    return true;
}

static bool can_buf_get_latest_if_fresh(can_ring_buf_t *buf, bsp_twai_msg_t *msg, uint32_t max_age_ms) {
    xSemaphoreTake(buf->mutex, portMAX_DELAY);
    if (buf->count == 0) {
        xSemaphoreGive(buf->mutex);
        return false;
    }
    uint32_t latest = (buf->head + BIZ_CAN_BUF_SIZE - 1) % BIZ_CAN_BUF_SIZE;
    uint32_t age = now_ms() - buf->entries[latest].timestamp_ms;
    if (age > max_age_ms) {
        xSemaphoreGive(buf->mutex);
        return false;
    }
    *msg = buf->entries[latest].frame;
    xSemaphoreGive(buf->mutex);
    return true;
}

static bool can_buf_has_fresh_data(can_ring_buf_t *buf, uint32_t max_age_ms) {
    xSemaphoreTake(buf->mutex, portMAX_DELAY);
    if (buf->count == 0) {
        xSemaphoreGive(buf->mutex);
        return false;
    }
    uint32_t age = now_ms() - buf->last_push_ms;
    xSemaphoreGive(buf->mutex);
    return (age <= max_age_ms);
}

void biz_can_rx_push(biz_ctx_t *ctx, const bsp_twai_msg_t *msg) {
    if (ctx == NULL || msg == NULL) return;
    can_buf_push(&ctx->can_rx_buf, msg);
}

bool biz_can_get_latest(biz_ctx_t *ctx, bsp_twai_msg_t *msg) {
    if (ctx == NULL || msg == NULL) return false;
    return can_buf_get_latest(&ctx->can_rx_buf, msg);
}

bool biz_can_get_latest_if_fresh(biz_ctx_t *ctx, bsp_twai_msg_t *msg, uint32_t max_age_ms) {
    if (ctx == NULL || msg == NULL) return false;
    return can_buf_get_latest_if_fresh(&ctx->can_rx_buf, msg, max_age_ms);
}

bool biz_can_has_fresh_data(biz_ctx_t *ctx, uint32_t max_age_ms) {
    if (ctx == NULL) return false;
    return can_buf_has_fresh_data(&ctx->can_rx_buf, max_age_ms);
}

void biz_get_stats(biz_ctx_t *ctx, uint32_t *tx, uint32_t *rx,
                   uint32_t *tx_err, uint32_t *rx_err) {
    if (ctx == NULL) return;
    if (tx) *tx = ctx->tx_frames;
    if (rx) *rx = ctx->rx_frames;
    if (tx_err) *tx_err = ctx->tx_errors;
    if (rx_err) *rx_err = ctx->rx_errors;
}

static uint16_t crc16_calc(const uint8_t *data, size_t len) {
    return esp_crc16_le(0, data, len);
}

static void ir_rx_callback(const uint8_t *data, size_t len) {
    if (s_base_ctx == NULL) return;

    if (len < sizeof(ir_proto_hdr_t) + 2) {
        return;
    }

    const ir_proto_hdr_t *hdr = (const ir_proto_hdr_t *)data;

    if (hdr->header != IR_PROTO_HEADER) {
        return;
    }

    uint8_t type = ir_ctrl_type(hdr->ctrl);
    if (type != IR_CTRL_RSP) {
        return;
    }

    size_t payload_len = len - sizeof(ir_proto_hdr_t) - 2;
    if (payload_len < 13) {
        return;
    }

    uint16_t received_crc = (uint16_t)data[len - 2] | ((uint16_t)data[len - 1] << 8);
    uint16_t calc_crc = crc16_calc(&data[2], len - 4);

    if (received_crc != calc_crc) {
        ESP_LOGW(TAG, "IR CRC error: rx=0x%04x calc=0x%04x", received_crc, calc_crc);
        s_base_ctx->rx_errors++;
        return;
    }

    s_base_ctx->rx_frames++;

    uint8_t device_id = hdr->slave_id;
    const uint8_t *can_data = hdr->data;

    bsp_twai_msg_t tx_msg;
    tx_msg.id = ((uint32_t)can_data[0] << 24) | ((uint32_t)can_data[1] << 16) |
                ((uint32_t)can_data[2] << 8) | (uint32_t)can_data[3];
    tx_msg.dlc = can_data[4];
    memcpy(tx_msg.data, &can_data[5], 8);
    tx_msg.extd = 0;
    tx_msg.rtr = 0;

    esp_err_t ret = app_twai_transmit(&tx_msg, pdMS_TO_TICKS(5));
    if (ret == ESP_OK) {
        s_base_ctx->tx_frames++;
        ESP_LOGI(TAG, "IR RSP from 0x%02x: CAN 0x%03lx DLC=%d [%02X %02X %02X %02X %02X %02X %02X %02X]",
                 device_id, tx_msg.id, tx_msg.dlc,
                 tx_msg.data[0], tx_msg.data[1], tx_msg.data[2], tx_msg.data[3],
                 tx_msg.data[4], tx_msg.data[5], tx_msg.data[6], tx_msg.data[7]);
        bsp_display_printf(2, 0, "IR:0x%02x CAN:0x%03lx", device_id, tx_msg.id);
        bsp_display_printf(3, 0, "D:%02X%02X%02X%02X%02X%02X%02X%02X",
                           tx_msg.data[0], tx_msg.data[1], tx_msg.data[2], tx_msg.data[3],
                           tx_msg.data[4], tx_msg.data[5], tx_msg.data[6], tx_msg.data[7]);
        bsp_display_printf(4, 0, "IR:%" PRIu32 " CAN:%" PRIu32,
                           s_base_ctx->rx_frames, s_base_ctx->tx_frames);
        bsp_display_refresh();
    } else {
        s_base_ctx->tx_errors++;
        ESP_LOGW(TAG, "CAN TX failed: %d", ret);
    }
}

static void device_send_task(void *arg) {
    biz_ctx_t *ctx = (biz_ctx_t *)arg;

    uint32_t seq = 0;

    bsp_display_clear();
    bsp_display_printf(0, 0, "WirelessID DEV");
    bsp_display_printf(1, 0, "ID:0x%02x SENDING", ctx->self_id);
    bsp_display_refresh();

    ESP_LOGI(TAG, "device send task started, id=0x%02x", ctx->self_id);

    while (1) {
        bsp_twai_msg_t latest;
        if (can_buf_get_latest_if_fresh(&ctx->can_rx_buf, &latest, BIZ_DATA_TIMEOUT_MS)) {
            uint8_t frame[32];
            ir_proto_hdr_t *hdr = (ir_proto_hdr_t *)frame;
            uint8_t *data = frame + sizeof(ir_proto_hdr_t);

            hdr->header = IR_PROTO_HEADER;
            hdr->ctrl = IR_CTRL_RSP;
            hdr->master_id = 0xA0;
            hdr->slave_id = ctx->self_id;
            hdr->seq = (uint8_t)(seq++ & 0xFF);
            hdr->data_len = 13;

            data[0] = (uint8_t)((latest.id >> 24) & 0xFF);
            data[1] = (uint8_t)((latest.id >> 16) & 0xFF);
            data[2] = (uint8_t)((latest.id >> 8) & 0xFF);
            data[3] = (uint8_t)(latest.id & 0xFF);
            data[4] = latest.dlc;
            memcpy(&data[5], latest.data, 8);

            size_t payload_len = sizeof(ir_proto_hdr_t) + 13;
            uint16_t crc = crc16_calc(&frame[2], payload_len - 2);
            frame[payload_len] = (uint8_t)(crc & 0xFF);
            frame[payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

            int written = bsp_ir_hw_write(frame, payload_len + 2);
            if (written > 0) {
                ctx->tx_frames++;
                ESP_LOGI(TAG, "IR TX: CAN 0x%03lx DLC=%d [%02X %02X %02X %02X %02X %02X %02X %02X]",
                         latest.id, latest.dlc,
                         latest.data[0], latest.data[1], latest.data[2], latest.data[3],
                         latest.data[4], latest.data[5], latest.data[6], latest.data[7]);
                bsp_display_printf(2, 0, "CAN:0x%03lx TX:%" PRIu32, latest.id, ctx->tx_frames);
                bsp_display_printf(3, 0, "D:%02X%02X%02X%02X%02X%02X%02X%02X",
                                   latest.data[0], latest.data[1], latest.data[2], latest.data[3],
                                   latest.data[4], latest.data[5], latest.data[6], latest.data[7]);
                bsp_display_refresh();
            } else {
                ctx->tx_errors++;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BIZ_DEVICE_SEND_INTERVAL_MS));
    }
}

static void base_receive_task(void *arg) {
    biz_ctx_t *ctx = (biz_ctx_t *)arg;

    app_ir_init();
    app_ir_set_rx_cb(ir_rx_callback);
    app_ir_start();

    bsp_display_clear();
    bsp_display_printf(0, 0, "WirelessID BASE");
    bsp_display_printf(1, 0, "ID:0x%02x READY", ctx->self_id);
    bsp_display_refresh();

    ESP_LOGI(TAG, "base receive task started");

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

    if (ctx->role == BIZ_ROLE_BASE) {
        xTaskCreate(base_receive_task, "biz_rx", 4096, ctx, 5, NULL);
        ESP_LOGI(TAG, "base receive task created");
    } else {
        xTaskCreate(device_send_task, "biz_tx", 4096, ctx, 5, NULL);
        ESP_LOGI(TAG, "device send task created");
    }

    return ESP_OK;
}
