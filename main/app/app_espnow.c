#include "app_espnow.h"

#include <string.h>

#include "bsp_espnow.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "espnow";

#define ESPNOW_FRAME_OVERHEAD (sizeof(ir_proto_hdr_t) + 2)
#define ESPNOW_TIMEOUT_MS     500
#define ESPNOW_DISCOVER_DELAY_MS 50

static uint16_t crc16_calc(const uint8_t *data, size_t len) {
    return esp_crc16_le(0, data, len);
}

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static esp_err_t build_and_send(espnow_base_t *self, uint8_t ctrl,
                                uint8_t dst_id, const void *data, size_t len) {
    if (len > IR_PROTO_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t frame[ESPNOW_FRAME_OVERHEAD + IR_PROTO_MAX_PAYLOAD];
    ir_proto_hdr_t *hdr = (ir_proto_hdr_t *)frame;

    hdr->header = IR_PROTO_HEADER;
    hdr->ctrl = ctrl;
    hdr->master_id = self->id;
    hdr->slave_id = dst_id;
    hdr->seq = self->seq++;

    if (data && len > 0) {
        memcpy(hdr->data, data, len);
    }

    size_t payload_len = offsetof(ir_proto_hdr_t, data) + len;
    uint16_t crc = crc16_calc(&frame[2], payload_len - 2);
    frame[payload_len] = (uint8_t)(crc & 0xFF);
    frame[payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

    const uint8_t *mac = NULL;
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    if (dst_id == IR_PROTO_BROADCAST) {
        mac = broadcast_mac;
    } else {
        xSemaphoreTake(self->peer_mutex, portMAX_DELAY);
        for (size_t i = 0; i < self->peer_count; i++) {
            if (self->peers[i].device_id == dst_id && self->peers[i].online) {
                mac = self->peers[i].mac;
                break;
            }
        }
        xSemaphoreGive(self->peer_mutex);

        if (mac == NULL) {
            ESP_LOGW(TAG, "peer 0x%02x not found, sending broadcast", dst_id);
            mac = broadcast_mac;
        }
    }

    esp_err_t ret = bsp_espnow_send(mac, frame, payload_len + 2);
    if (ret == ESP_OK) {
        self->stats.tx_frames++;
    }
    return ret;
}

static void update_peer(espnow_base_t *self, uint8_t device_id,
                        const uint8_t *mac, const char *name, int8_t rssi) {
    xSemaphoreTake(self->peer_mutex, portMAX_DELAY);

    espnow_peer_t *existing = NULL;
    for (size_t i = 0; i < self->peer_count; i++) {
        if (self->peers[i].device_id == device_id) {
            existing = &self->peers[i];
            break;
        }
    }

    if (existing == NULL && self->peer_count < ESPNOW_MAX_PEERS) {
        existing = &self->peers[self->peer_count++];
    }

    if (existing != NULL) {
        existing->device_id = device_id;
        memcpy(existing->mac, mac, 6);
        if (name != NULL) {
            strncpy(existing->name, name, ESPNOW_NAME_MAX_LEN - 1);
            existing->name[ESPNOW_NAME_MAX_LEN - 1] = '\0';
        }
        existing->rssi = rssi;
        existing->last_seen_ms = now_ms();
        existing->online = true;
    }

    xSemaphoreGive(self->peer_mutex);
}

esp_err_t espnow_base_init(espnow_base_t *self, uint8_t id, const char *name) {
    if (!self || id == 0 || id == IR_PROTO_BROADCAST) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(self, 0, sizeof(*self));
    self->id = id;
    if (name != NULL) {
        strncpy(self->name, name, ESPNOW_NAME_MAX_LEN - 1);
    }

    self->peer_mutex = xSemaphoreCreateMutex();
    self->tx_mutex = xSemaphoreCreateMutex();
    self->rsp_sem = xSemaphoreCreateBinary();

    if (!self->peer_mutex || !self->tx_mutex || !self->rsp_sem) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "base init id=0x%02x name=%s", id, self->name);
    return ESP_OK;
}

void espnow_base_deinit(espnow_base_t *self) {
    if (!self) return;
    if (self->peer_mutex) { vSemaphoreDelete(self->peer_mutex); }
    if (self->tx_mutex) { vSemaphoreDelete(self->tx_mutex); }
    if (self->rsp_sem) { vSemaphoreDelete(self->rsp_sem); }
    memset(self, 0, sizeof(*self));
}

void espnow_base_set_rsp_cb(espnow_base_t *self, espnow_base_rsp_cb_t cb) {
    if (self) { self->on_rsp = cb; }
}

esp_err_t espnow_base_discover(espnow_base_t *self) {
    if (!self) return ESP_ERR_INVALID_STATE;

    uint8_t discover_data[1 + ESPNOW_NAME_MAX_LEN];
    discover_data[0] = self->id;
    memcpy(&discover_data[1], self->name, ESPNOW_NAME_MAX_LEN);

    esp_err_t ret = build_and_send(self, 0x50, IR_PROTO_BROADCAST,
                                   discover_data, 1 + ESPNOW_NAME_MAX_LEN);
    if (ret == ESP_OK) {
        self->stats.discover_sent++;
    }
    return ret;
}

esp_err_t espnow_base_get_peers(espnow_base_t *self, espnow_peer_t *peers,
                                 size_t max, size_t *count) {
    if (!self || !peers || !count) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(self->peer_mutex, portMAX_DELAY);

    size_t n = 0;
    for (size_t i = 0; i < self->peer_count && n < max; i++) {
        if (self->peers[i].online) {
            peers[n++] = self->peers[i];
        }
    }
    *count = n;

    xSemaphoreGive(self->peer_mutex);
    return ESP_OK;
}

bool espnow_base_is_device_online(espnow_base_t *self, uint8_t device_id) {
    if (!self) return false;

    xSemaphoreTake(self->peer_mutex, portMAX_DELAY);
    for (size_t i = 0; i < self->peer_count; i++) {
        if (self->peers[i].device_id == device_id && self->peers[i].online) {
            xSemaphoreGive(self->peer_mutex);
            return true;
        }
    }
    xSemaphoreGive(self->peer_mutex);
    return false;
}

esp_err_t espnow_base_send_cmd(espnow_base_t *self, uint8_t device_id,
                                const void *data, size_t len) {
    if (!self) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(self->tx_mutex, portMAX_DELAY);
    esp_err_t ret = build_and_send(self, IR_CTRL_CMD, device_id, data, len);
    xSemaphoreGive(self->tx_mutex);
    return ret;
}

esp_err_t espnow_base_send_cmd_req(espnow_base_t *self, uint8_t device_id,
                                    const void *cmd, size_t cmd_len,
                                    void *rsp, size_t rsp_max, size_t *rsp_len,
                                    uint32_t timeout_ms) {
    if (!self || !rsp || !rsp_len) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(self->tx_mutex, portMAX_DELAY);
    self->rsp_device_id = device_id;
    self->rsp_len = 0;

    esp_err_t ret = build_and_send(self, IR_CTRL_CMD_REQ, device_id, cmd, cmd_len);
    xSemaphoreGive(self->tx_mutex);

    if (ret != ESP_OK) return ret;

    if (xSemaphoreTake(self->rsp_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        self->stats.tx_timeouts++;
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(self->tx_mutex, portMAX_DELAY);
    size_t copy_len = self->rsp_len < rsp_max ? self->rsp_len : rsp_max;
    memcpy(rsp, self->rsp_buf, copy_len);
    *rsp_len = copy_len;
    xSemaphoreGive(self->tx_mutex);

    return ESP_OK;
}

esp_err_t espnow_base_send_cmd_req_default(espnow_base_t *self, uint8_t device_id,
                                            const void *cmd, size_t cmd_len,
                                            void *rsp, size_t rsp_max, size_t *rsp_len) {
    return espnow_base_send_cmd_req(self, device_id, cmd, cmd_len,
                                     rsp, rsp_max, rsp_len, ESPNOW_TIMEOUT_MS);
}

esp_err_t espnow_base_broadcast(espnow_base_t *self, const void *data, size_t len) {
    if (!self) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(self->tx_mutex, portMAX_DELAY);
    esp_err_t ret = build_and_send(self, IR_CTRL_BCAST, IR_PROTO_BROADCAST, data, len);
    xSemaphoreGive(self->tx_mutex);
    return ret;
}

void espnow_base_get_stats(espnow_base_t *self, espnow_base_stats_t *stats) {
    if (self && stats) { *stats = self->stats; }
}

void espnow_base_process_rx(espnow_base_t *self, const uint8_t *mac,
                             const uint8_t *frame, size_t len) {
    if (!self || !frame || len < ESPNOW_FRAME_OVERHEAD) return;

    const ir_proto_hdr_t *hdr = (const ir_proto_hdr_t *)frame;
    if (hdr->header != IR_PROTO_HEADER) return;

    if (hdr->master_id == self->id) return;

    size_t payload_len = len - ESPNOW_FRAME_OVERHEAD;
    uint16_t received_crc = (uint16_t)frame[len - 2] | ((uint16_t)frame[len - 1] << 8);
    uint16_t calc_crc = crc16_calc(&frame[2], len - 4);

    if (received_crc != calc_crc) {
        self->stats.rx_crc_errors++;
        return;
    }

    self->stats.rx_frames++;

    uint8_t type = ir_ctrl_type(hdr->ctrl);

    if (type == 0x60) {
        update_peer(self, hdr->master_id, mac, (const char *)hdr->data, 0);
        self->stats.announce_recv++;
        ESP_LOGI(TAG, "announce from 0x%02x", hdr->master_id);
        return;
    }

    if (type == IR_CTRL_RSP && hdr->slave_id == self->id) {
        xSemaphoreTake(self->tx_mutex, portMAX_DELAY);
        self->rsp_len = payload_len < IR_PROTO_MAX_PAYLOAD ? payload_len : IR_PROTO_MAX_PAYLOAD;
        memcpy(self->rsp_buf, hdr->data, self->rsp_len);
        self->rsp_device_id = hdr->master_id;
        xSemaphoreGive(self->tx_mutex);

        xSemaphoreGive(self->rsp_sem);

        if (self->on_rsp) {
            self->on_rsp(self, hdr->master_id, hdr->data, payload_len);
        }
    }
}
