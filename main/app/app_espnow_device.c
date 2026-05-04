#include "app_espnow_device.h"

#include <string.h>

#include "bsp_espnow.h"
#include "esp_crc.h"
#include "esp_log.h"

static const char *TAG = "espnow_dev";

#define ESPNOW_FRAME_OVERHEAD (sizeof(ir_proto_hdr_t) + 2)

static uint16_t crc16_calc(const uint8_t *data, size_t len) {
    return esp_crc16_le(0, data, len);
}

static esp_err_t send_rsp(espnow_device_t *self, uint8_t base_id,
                           const void *data, size_t len) {
    if (len > IR_PROTO_MAX_PAYLOAD) return ESP_ERR_INVALID_SIZE;

    uint8_t frame[ESPNOW_FRAME_OVERHEAD + IR_PROTO_MAX_PAYLOAD];
    ir_proto_hdr_t *hdr = (ir_proto_hdr_t *)frame;

    hdr->header = IR_PROTO_HEADER;
    hdr->ctrl = IR_CTRL_RSP;
    hdr->master_id = base_id;
    hdr->slave_id = self->id;
    hdr->seq = self->seq++;

    if (data && len > 0) {
        memcpy(hdr->data, data, len);
    }

    size_t payload_len = offsetof(ir_proto_hdr_t, data) + len;
    uint16_t crc = crc16_calc(&frame[2], payload_len - 2);
    frame[payload_len] = (uint8_t)(crc & 0xFF);
    frame[payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return bsp_espnow_send(broadcast_mac, frame, payload_len + 2);
}

esp_err_t espnow_device_init(espnow_device_t *self, uint8_t id, const char *name) {
    if (!self || id == 0 || id == IR_PROTO_BROADCAST) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(self, 0, sizeof(*self));
    self->id = id;
    if (name != NULL) {
        strncpy(self->name, name, sizeof(self->name) - 1);
    }

    ESP_LOGI(TAG, "device init id=0x%02x name=%s", id, self->name);
    return ESP_OK;
}

void espnow_device_deinit(espnow_device_t *self) {
    if (self) memset(self, 0, sizeof(*self));
}

void espnow_device_set_cmd_cb(espnow_device_t *self, espnow_device_cmd_cb_t cb) {
    if (self) self->on_cmd = cb;
}

void espnow_device_set_data_cb(espnow_device_t *self, espnow_device_data_cb_t cb) {
    if (self) self->on_data = cb;
}

esp_err_t espnow_device_announce(espnow_device_t *self) {
    if (!self) return ESP_ERR_INVALID_STATE;

    uint8_t data[1 + 16];
    data[0] = self->id;
    memcpy(&data[1], self->name, 16);

    uint8_t frame[ESPNOW_FRAME_OVERHEAD + IR_PROTO_MAX_PAYLOAD];
    ir_proto_hdr_t *hdr = (ir_proto_hdr_t *)frame;

    hdr->header = IR_PROTO_HEADER;
    hdr->ctrl = 0x60;
    hdr->master_id = self->id;
    hdr->slave_id = IR_PROTO_BROADCAST;
    hdr->seq = self->seq++;

    memcpy(hdr->data, data, 17);

    size_t payload_len = offsetof(ir_proto_hdr_t, data) + 17;
    uint16_t crc = crc16_calc(&frame[2], payload_len - 2);
    frame[payload_len] = (uint8_t)(crc & 0xFF);
    frame[payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t ret = bsp_espnow_send(broadcast_mac, frame, payload_len + 2);
    if (ret == ESP_OK) {
        self->registered = true;
    }
    return ret;
}

void espnow_device_process_rx(espnow_device_t *self, const uint8_t *mac,
                               const uint8_t *frame, size_t len) {
    if (!self || !frame || len < ESPNOW_FRAME_OVERHEAD) return;

    const ir_proto_hdr_t *hdr = (const ir_proto_hdr_t *)frame;
    if (hdr->header != IR_PROTO_HEADER) return;

    if (hdr->slave_id != self->id && hdr->slave_id != IR_PROTO_BROADCAST) {
        return;
    }

    size_t payload_len = len - ESPNOW_FRAME_OVERHEAD;
    uint16_t received_crc = (uint16_t)frame[len - 2] | ((uint16_t)frame[len - 1] << 8);
    uint16_t calc_crc = crc16_calc(&frame[2], len - 4);

    if (received_crc != calc_crc) return;

    uint8_t type = ir_ctrl_type(hdr->ctrl);
    bool req = ir_ctrl_is_req(hdr->ctrl);

    if (type == 0x50) {
        espnow_device_announce(self);
        return;
    }

    if (type == IR_CTRL_CMD || type == IR_CTRL_CMD_REQ) {
        if (self->on_cmd && req) {
            uint8_t rsp[IR_PROTO_MAX_PAYLOAD];
            size_t rsp_len = 0;

            self->on_cmd(self, hdr->master_id, hdr->data, payload_len, rsp, &rsp_len);

            if (rsp_len > 0) {
                send_rsp(self, hdr->master_id, rsp, rsp_len);
            }
        } else if (self->on_data) {
            self->on_data(self, hdr->master_id, hdr->data, payload_len);
        }
    } else if (type == IR_CTRL_BCAST || type == IR_CTRL_DATA) {
        if (self->on_data) {
            self->on_data(self, hdr->master_id, hdr->data, payload_len);
        }
    }
}
