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

#if WIRELESSID_WIRELESS_ENABLE
#include "app_webui.h"
#include "bsp_espnow.h"
#endif

static const char *TAG = "biz";

static biz_ctx_t *s_base_ctx = NULL;
static biz_ctx_t *s_device_ctx = NULL;

#define BIZ_DEVICE_SEND_INTERVAL_MS  50
#define BIZ_HEARTBEAT_INTERVAL_MS    2000
#define BIZ_WEBUI_UPDATE_INTERVAL_MS 1000
#define BIZ_WEBUI_LOG_INTERVAL       10

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void can_buf_init(can_ring_buf_t *buf) {
    memset(buf, 0, sizeof(*buf));
    buf->mutex = xSemaphoreCreateMutex();
}

static void can_buf_push(can_ring_buf_t *buf, const bsp_twai_msg_t *msg) {
    if (buf->mutex == NULL) return;
    xSemaphoreTake(buf->mutex, portMAX_DELAY);
    buf->entries[buf->head].frame = *msg;
    buf->entries[buf->head].timestamp_ms = now_ms();
    buf->head = (buf->head + 1) % BIZ_CAN_BUF_SIZE;
    if (buf->count < BIZ_CAN_BUF_SIZE) buf->count++;
    buf->last_push_ms = now_ms();
    xSemaphoreGive(buf->mutex);
}

static bool can_buf_get_latest(can_ring_buf_t *buf, bsp_twai_msg_t *msg) {
    if (buf->mutex == NULL) return false;
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
    if (buf->mutex == NULL) return false;
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
    if (buf->mutex == NULL) return false;
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

void biz_update_ir_online(biz_ctx_t *ctx, uint8_t device_id) {
    if (ctx == NULL) return;
    for (int i = 0; i < BIZ_MAX_DEVICES; i++) {
        if (ctx->devices[i].device_id == device_id) {
            ctx->devices[i].ir_online = true;
            ctx->devices[i].last_ir_seen_ms = now_ms();
            return;
        }
    }
    for (int i = 0; i < BIZ_MAX_DEVICES; i++) {
        if (ctx->devices[i].device_id == 0) {
            ctx->devices[i].device_id = device_id;
            ctx->devices[i].ir_online = true;
            ctx->devices[i].last_ir_seen_ms = now_ms();
            ctx->device_count++;
            return;
        }
    }
}

void biz_update_espnow_online(biz_ctx_t *ctx, uint8_t device_id) {
    if (ctx == NULL) return;
    for (int i = 0; i < BIZ_MAX_DEVICES; i++) {
        if (ctx->devices[i].device_id == device_id) {
            ctx->devices[i].espnow_online = true;
            ctx->devices[i].last_espnow_seen_ms = now_ms();
            return;
        }
    }
    for (int i = 0; i < BIZ_MAX_DEVICES; i++) {
        if (ctx->devices[i].device_id == 0) {
            ctx->devices[i].device_id = device_id;
            ctx->devices[i].espnow_online = true;
            ctx->devices[i].last_espnow_seen_ms = now_ms();
            ctx->device_count++;
            return;
        }
    }
}

void biz_check_timeouts(biz_ctx_t *ctx) {
    if (ctx == NULL) return;
    uint32_t now = now_ms();
    for (int i = 0; i < BIZ_MAX_DEVICES; i++) {
        if (ctx->devices[i].device_id == 0) continue;
        if (ctx->devices[i].ir_online && (now - ctx->devices[i].last_ir_seen_ms > BIZ_IR_TIMEOUT_MS)) {
            ctx->devices[i].ir_online = false;
            ESP_LOGI(TAG, "IR timeout: 0x%02x", ctx->devices[i].device_id);
        }
        if (ctx->devices[i].espnow_online && (now - ctx->devices[i].last_espnow_seen_ms > ESPNOW_PEER_TIMEOUT_MS)) {
            ctx->devices[i].espnow_online = false;
            ESP_LOGI(TAG, "ESP-NOW timeout: 0x%02x", ctx->devices[i].device_id);
        }
        if (!ctx->devices[i].ir_online && !ctx->devices[i].espnow_online) {
            ESP_LOGI(TAG, "device offline: 0x%02x", ctx->devices[i].device_id);
            ctx->devices[i].device_id = 0;
            if (ctx->device_count > 0) ctx->device_count--;
        }
    }
}

static uint16_t crc16_calc(const uint8_t *data, size_t len) {
    return esp_crc16_le(0, data, len);
}

#if WIRELESSID_WIRELESS_ENABLE
static bool biz_is_device_dual_online(biz_ctx_t *ctx, uint8_t device_id) {
    if (ctx == NULL) return false;
    for (int i = 0; i < BIZ_MAX_DEVICES; i++) {
        if (ctx->devices[i].device_id == device_id) {
            return ctx->devices[i].ir_online && ctx->devices[i].espnow_online;
        }
    }
    return false;
}
#endif

void biz_forward_can_to_devices(biz_ctx_t *ctx, const bsp_twai_msg_t *msg) {
    if (ctx == NULL || msg == NULL) return;
#if WIRELESSID_WIRELESS_ENABLE
    uint8_t frame[32];
    ir_proto_hdr_t *hdr = (ir_proto_hdr_t *)frame;
    uint8_t *data = frame + sizeof(ir_proto_hdr_t);

    hdr->header = IR_PROTO_HEADER;
    hdr->ctrl = IR_CTRL_RSP;
    hdr->master_id = ctx->self_id;
    hdr->slave_id = 0xFF;
    hdr->seq = 0;
    hdr->data_len = 13;

    data[0] = (uint8_t)((msg->id >> 24) & 0xFF);
    data[1] = (uint8_t)((msg->id >> 16) & 0xFF);
    data[2] = (uint8_t)((msg->id >> 8) & 0xFF);
    data[3] = (uint8_t)(msg->id & 0xFF);
    data[4] = msg->dlc;
    memcpy(&data[5], msg->data, 8);

    size_t payload_len = sizeof(ir_proto_hdr_t) + 13;
    uint16_t crc = crc16_calc(&frame[2], payload_len - 2);
    frame[payload_len] = (uint8_t)(crc & 0xFF);
    frame[payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

    for (int i = 0; i < BIZ_MAX_DEVICES; i++) {
        if (ctx->devices[i].device_id != 0 && ctx->devices[i].espnow_online) {
            espnow_base_send_cmd(&ctx->espnow_base, ctx->devices[i].device_id, frame, payload_len + 2);
            ESP_LOGD(TAG, "Forward CAN 0x%03lx to 0x%02x", msg->id, ctx->devices[i].device_id);

            char espnow_log_buf[48];
            snprintf(espnow_log_buf, sizeof(espnow_log_buf), "TX 0x%02X CAN 0x%03lX",
                     ctx->devices[i].device_id, msg->id);
            app_webui_log_espnow(espnow_log_buf);
        }
    }
#endif
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

    if (type == IR_CTRL_PING) {
        uint16_t received_crc = (uint16_t)data[len - 2] | ((uint16_t)data[len - 1] << 8);
        uint16_t calc_crc = crc16_calc(&data[2], len - 4);
        if (received_crc == calc_crc) {
            biz_update_ir_online(s_base_ctx, hdr->slave_id);
            ESP_LOGD(TAG, "IR PING from 0x%02x", hdr->slave_id);
        }
        return;
    }

    if (type != IR_CTRL_RSP) {
        return;
    }

    biz_update_ir_online(s_base_ctx, hdr->slave_id);

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
    } else {
        s_base_ctx->tx_errors++;
        ESP_LOGW(TAG, "CAN TX failed: %d", ret);
    }

    static uint32_t s_log_counter = 0;
    if (++s_log_counter >= BIZ_WEBUI_LOG_INTERVAL) {
        s_log_counter = 0;

#if WIRELESSID_WIRELESS_ENABLE
        char ir_log_buf[48];
        snprintf(ir_log_buf, sizeof(ir_log_buf), "RX 0x%02X CAN 0x%03lX",
                 device_id, tx_msg.id);
        app_webui_log_ir(ir_log_buf);

        char twai_log_buf[64];
        if (ret == ESP_OK) {
            snprintf(twai_log_buf, sizeof(twai_log_buf),
                     "TX 0x%03lX [%02X %02X %02X %02X %02X %02X %02X %02X]",
                     tx_msg.id, tx_msg.data[0], tx_msg.data[1], tx_msg.data[2],
                     tx_msg.data[3], tx_msg.data[4], tx_msg.data[5],
                     tx_msg.data[6], tx_msg.data[7]);
        } else {
            snprintf(twai_log_buf, sizeof(twai_log_buf),
                     "!TX 0x%03lX err=%d", tx_msg.id, ret);
        }
        app_webui_log_twai(twai_log_buf);
#endif

        bsp_display_printf(2, 0, "IR:0x%02x CAN:0x%03lx", device_id, tx_msg.id);
        bsp_display_printf(3, 0, "D:%02X%02X%02X%02X%02X%02X%02X%02X",
                           tx_msg.data[0], tx_msg.data[1], tx_msg.data[2], tx_msg.data[3],
                           tx_msg.data[4], tx_msg.data[5], tx_msg.data[6], tx_msg.data[7]);
        bsp_display_printf(4, 0, "IR:%" PRIu32 " CAN:%" PRIu32,
                           s_base_ctx->rx_frames, s_base_ctx->tx_frames);
        bsp_display_refresh();
    }
}

#if WIRELESSID_WIRELESS_ENABLE
static void espnow_rx_cb(const uint8_t *mac, const uint8_t *data, int len) {
    if (data == NULL || len <= 0) return;

    if (s_base_ctx != NULL) {
        espnow_base_process_rx(&s_base_ctx->espnow_base, mac, data, (size_t)len);

        if (len >= sizeof(ir_proto_hdr_t) + 2) {
            const ir_proto_hdr_t *hdr = (const ir_proto_hdr_t *)data;
            uint8_t type = ir_ctrl_type(hdr->ctrl);

            char espnow_log_buf[48];
            snprintf(espnow_log_buf, sizeof(espnow_log_buf), "RX 0x%02X type=0x%02X len=%d",
                     hdr->slave_id, hdr->ctrl, len);
            app_webui_log_espnow(espnow_log_buf);

            if (hdr->header == IR_PROTO_HEADER && type == IR_CTRL_RSP) {
                uint8_t device_id = hdr->slave_id;

                if (!biz_is_device_dual_online(s_base_ctx, device_id)) {
                    ESP_LOGD(TAG, "ESPNOW RSP filtered: 0x%02x not dual online", device_id);
                    return;
                }

                size_t payload_len = len - sizeof(ir_proto_hdr_t) - 2;
                if (payload_len >= 13) {
                    uint16_t received_crc = (uint16_t)data[len - 2] | ((uint16_t)data[len - 1] << 8);
                    uint16_t calc_crc = crc16_calc(&data[2], len - 4);
                    if (received_crc == calc_crc) {
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
                            ESP_LOGI(TAG, "ESPNOW RSP from 0x%02x: CAN 0x%03lx", device_id, tx_msg.id);

                            char twai_log_buf[64];
                            snprintf(twai_log_buf, sizeof(twai_log_buf),
                                     "TX 0x%03lX [%02X %02X %02X %02X %02X %02X %02X %02X]",
                                     tx_msg.id, tx_msg.data[0], tx_msg.data[1], tx_msg.data[2],
                                     tx_msg.data[3], tx_msg.data[4], tx_msg.data[5],
                                     tx_msg.data[6], tx_msg.data[7]);
                            app_webui_log_twai(twai_log_buf);
                        } else {
                            s_base_ctx->tx_errors++;
                        }
                    }
                }
            }
        }
    }
    if (s_device_ctx != NULL) {
        espnow_device_process_rx(&s_device_ctx->espnow_device, mac, data, (size_t)len);
    }
}

static void base_webui_update_task(void *arg) {
    biz_ctx_t *ctx = (biz_ctx_t *)arg;

    while (1) {
        biz_check_timeouts(ctx);

        espnow_base_check_peers(&ctx->espnow_base, ESPNOW_PEER_TIMEOUT_MS);

        espnow_base_stats_t espnow_st;
        espnow_base_get_stats(&ctx->espnow_base, &espnow_st);

        espnow_peer_t peers[ESPNOW_MAX_PEERS];
        size_t peer_count = 0;
        espnow_base_get_peers(&ctx->espnow_base, peers, ESPNOW_MAX_PEERS, &peer_count);

        for (size_t i = 0; i < peer_count; i++) {
            biz_update_espnow_online(ctx, peers[i].device_id);
        }

        uint32_t twai_tx = 0, twai_rx = 0;
        bsp_twai_get_frame_counts(&twai_tx, &twai_rx);

        app_webui_status_t wst = {
            .uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000),
            .device_id = ctx->self_id,
            .peer_count = ctx->device_count,
            .twai_tx_frames = twai_tx,
            .twai_rx_frames = twai_rx,
            .twai_tx_drops = ctx->tx_errors,
            .ir_tx_frames = 0,
            .ir_rx_frames = ctx->rx_frames,
            .ir_rx_crc_err = ctx->rx_errors,
            .espnow_tx_frames = espnow_st.tx_frames,
            .espnow_rx_frames = espnow_st.rx_frames,
            .espnow_announce_recv = espnow_st.announce_recv,
            .device_count = 0,
        };

        for (int i = 0; i < BIZ_MAX_DEVICES && wst.device_count < WEBUI_MAX_DEVICES; i++) {
            if (ctx->devices[i].device_id != 0) {
                wst.devices[wst.device_count].device_id = ctx->devices[i].device_id;
                wst.devices[wst.device_count].espnow_online = ctx->devices[i].espnow_online;
                wst.devices[wst.device_count].ir_online = ctx->devices[i].ir_online;
                wst.device_count++;
            }
        }

        size_t online_count = 0;
        for (int i = 0; i < BIZ_MAX_DEVICES; i++) {
            if (ctx->devices[i].device_id != 0 &&
                (ctx->devices[i].ir_online || ctx->devices[i].espnow_online)) {
                online_count++;
            }
        }
        wst.peer_count = online_count;

        app_webui_update_status(&wst);

        bsp_display_printf(5, 0, "DEV:%d", (int)online_count);
        bsp_display_refresh();

        vTaskDelay(pdMS_TO_TICKS(BIZ_WEBUI_UPDATE_INTERVAL_MS));
    }
}
#endif

static void base_receive_task(void *arg) {
    biz_ctx_t *ctx = (biz_ctx_t *)arg;

#if WIRELESSID_WIRELESS_ENABLE
    app_webui_init(ctx->self_id);

    bsp_espnow_register_recv_cb(espnow_rx_cb);
    espnow_base_init(&ctx->espnow_base, ctx->self_id, "Base");
    espnow_base_discover(&ctx->espnow_base);
#endif

    app_ir_init();
    app_ir_set_rx_cb(ir_rx_callback);
    app_ir_start();

    bsp_display_clear();
    bsp_display_printf(0, 0, "WirelessID BASE");
    bsp_display_printf(1, 0, "ID:0x%02x READY", ctx->self_id);
    bsp_display_refresh();

    ESP_LOGI(TAG, "base receive task started");

#if WIRELESSID_WIRELESS_ENABLE
    xTaskCreate(base_webui_update_task, "biz_webui", 4096, ctx, 3, NULL);
    app_webui_start();
#endif

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#if WIRELESSID_WIRELESS_ENABLE
static void device_heartbeat_task(void *arg) {
    biz_ctx_t *ctx = (biz_ctx_t *)arg;

    while (1) {
        espnow_device_send_heartbeat(&ctx->espnow_device);
        vTaskDelay(pdMS_TO_TICKS(BIZ_HEARTBEAT_INTERVAL_MS));
    }
}
#endif

static void device_send_task(void *arg) {
    biz_ctx_t *ctx = (biz_ctx_t *)arg;

    uint32_t seq = 0;
    bool espnow_connected = false;

#if WIRELESSID_WIRELESS_ENABLE
    bsp_espnow_register_recv_cb(espnow_rx_cb);
    espnow_device_init(&ctx->espnow_device, ctx->self_id, "Device");
    espnow_device_announce(&ctx->espnow_device);
#endif

    bsp_display_clear();
    bsp_display_printf(0, 0, "WirelessID DEV");
    bsp_display_printf(1, 0, "ID:0x%02x SENDING", ctx->self_id);
    bsp_display_refresh();

    ESP_LOGI(TAG, "device send task started, id=0x%02x", ctx->self_id);

#if WIRELESSID_WIRELESS_ENABLE
    xTaskCreate(device_heartbeat_task, "biz_hb", 2048, ctx, 3, NULL);
#endif

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
                ESP_LOGD(TAG, "IR TX: CAN 0x%03lx", latest.id);
            } else {
                ctx->tx_errors++;
            }

#if WIRELESSID_WIRELESS_ENABLE
            if (espnow_connected) {
                uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                esp_err_t ret = bsp_espnow_send(broadcast_mac, frame, payload_len + 2);
                if (ret == ESP_OK) {
                    ESP_LOGD(TAG, "ESPNOW TX: CAN 0x%03lx", latest.id);
                } else {
                    ESP_LOGW(TAG, "ESPNOW TX failed: %d", ret);
                }
            }
#endif

            bsp_display_printf(2, 0, "CAN:0x%03lx TX:%" PRIu32, latest.id, ctx->tx_frames);
            bsp_display_printf(3, 0, "D:%02X%02X%02X%02X%02X%02X%02X%02X",
                               latest.data[0], latest.data[1], latest.data[2], latest.data[3],
                               latest.data[4], latest.data[5], latest.data[6], latest.data[7]);
            bsp_display_refresh();
        } else {
            static uint32_t s_ping_counter = 0;
            if (++s_ping_counter >= (BIZ_IR_HEARTBEAT_MS / BIZ_DEVICE_SEND_INTERVAL_MS)) {
                s_ping_counter = 0;

                uint8_t frame[16];
                ir_proto_hdr_t *hdr = (ir_proto_hdr_t *)frame;

                hdr->header = IR_PROTO_HEADER;
                hdr->ctrl = IR_CTRL_PING;
                hdr->master_id = 0xA0;
                hdr->slave_id = ctx->self_id;
                hdr->seq = (uint8_t)(seq++ & 0xFF);
                hdr->data_len = 0;

                size_t payload_len = sizeof(ir_proto_hdr_t);
                uint16_t crc = crc16_calc(&frame[2], payload_len - 2);
                frame[payload_len] = (uint8_t)(crc & 0xFF);
                frame[payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

                bsp_ir_hw_write(frame, payload_len + 2);
                ESP_LOGD(TAG, "IR PING TX");
            }
        }

#if WIRELESSID_WIRELESS_ENABLE
        espnow_connected = ctx->espnow_device.registered;
#endif

        vTaskDelay(pdMS_TO_TICKS(BIZ_DEVICE_SEND_INTERVAL_MS));
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
        xTaskCreate(base_receive_task, "biz_rx", 8192, ctx, 5, NULL);
        ESP_LOGI(TAG, "base receive task created");
    } else {
        xTaskCreate(device_send_task, "biz_tx", 4096, ctx, 5, NULL);
        ESP_LOGI(TAG, "device send task created");
    }

    return ESP_OK;
}
