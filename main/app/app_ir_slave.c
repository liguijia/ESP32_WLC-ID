#include "app_ir_slave.h"

#include <string.h>

#include "bsp_ir_hw.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ir_slave";

#define IR_FRAME_OVERHEAD (sizeof(ir_proto_hdr_t) + 2)
#define IR_ECHO_GUARD_MS  50

static uint16_t crc16_calc(const uint8_t *data, size_t len) {
    return esp_crc16_le(0, data, len);
}

static esp_err_t send_rsp(ir_slave_t *self, uint8_t master_id,
                          const void *data, size_t len) {
    if (len > IR_PROTO_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t frame[IR_FRAME_OVERHEAD + IR_PROTO_MAX_PAYLOAD];
    ir_proto_hdr_t *hdr = (ir_proto_hdr_t *)frame;

    hdr->header = IR_PROTO_HEADER;
    hdr->ctrl = IR_CTRL_RSP;
    hdr->master_id = master_id;
    hdr->slave_id = self->id;
    hdr->seq = self->seq++;
    hdr->data_len = (uint8_t)len;

    if (data && len > 0) {
        memcpy(hdr->data, data, len);
    }

    size_t payload_len = offsetof(ir_proto_hdr_t, data) + len;
    uint16_t crc = crc16_calc(&frame[2], payload_len - 2);
    frame[payload_len] = (uint8_t)(crc & 0xFF);
    frame[payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

    ESP_LOGI(TAG, "send_rsp: to=0x%02x from=0x%02x len=%d ctrl=0x%02x payload_len=%d crc=0x%04x",
             master_id, self->id, (int)len, hdr->ctrl, (int)payload_len, crc);

    int written = bsp_ir_hw_write(frame, payload_len + 2);
    if (written <= 0) {
        ESP_LOGE(TAG, "send_rsp failed: written=%d", written);
        return ESP_FAIL;
    }

    self->last_tx_tick = xTaskGetTickCount();
    self->stats.tx_frames++;

    ESP_LOGI(TAG, "send_rsp OK: written=%d", written);
    return ESP_OK;
}

esp_err_t ir_slave_init(ir_slave_t *self, uint8_t id) {
    if (!self || id == 0 || id == IR_PROTO_BROADCAST) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(self, 0, sizeof(*self));
    self->id = id;

    ESP_LOGI(TAG, "init id=0x%02x", id);
    return ESP_OK;
}

void ir_slave_deinit(ir_slave_t *self) {
    if (self) {
        memset(self, 0, sizeof(*self));
    }
}

void ir_slave_set_cmd_cb(ir_slave_t *self, ir_slave_cmd_cb_t cb) {
    if (self) {
        self->on_cmd = cb;
    }
}

void ir_slave_set_data_cb(ir_slave_t *self, ir_slave_data_cb_t cb) {
    if (self) {
        self->on_data = cb;
    }
}

void ir_slave_get_stats(ir_slave_t *self, ir_proto_stats_t *stats) {
    if (self && stats) {
        *stats = self->stats;
    }
}

void ir_slave_process_rx(ir_slave_t *self, const uint8_t *frame, size_t len) {
    if (!self || !frame || len < IR_FRAME_OVERHEAD) {
        return;
    }

    const ir_proto_hdr_t *hdr = (const ir_proto_hdr_t *)frame;

    if (hdr->header != IR_PROTO_HEADER) {
        return;
    }

    if (hdr->slave_id != self->id && hdr->slave_id != IR_PROTO_BROADCAST) {
        return;
    }

    uint32_t elapsed_ms = (xTaskGetTickCount() - self->last_tx_tick) * portTICK_PERIOD_MS;
    if (elapsed_ms < IR_ECHO_GUARD_MS) {
        self->stats.rx_filtered++;
        return;
    }

    size_t payload_len = len - IR_FRAME_OVERHEAD;
    uint16_t received_crc = (uint16_t)frame[len - 2] | ((uint16_t)frame[len - 1] << 8);
    uint16_t calc_crc = crc16_calc(&frame[2], len - 4);

    if (received_crc != calc_crc) {
        self->stats.rx_crc_errors++;
        return;
    }

    self->stats.rx_frames++;

    uint8_t type = ir_ctrl_type(hdr->ctrl);
    bool req = ir_ctrl_is_req(hdr->ctrl);

    ESP_LOGI(TAG, "RX frame: type=0x%02x req=%d master_id=0x%02x slave_id=0x%02x payload_len=%d",
             type, req, hdr->master_id, hdr->slave_id, (int)payload_len);

    if (type == IR_CTRL_CMD || type == IR_CTRL_CMD_REQ) {
        if (self->on_cmd) {
            uint8_t rsp[IR_PROTO_MAX_PAYLOAD];
            size_t rsp_len = 0;

            self->on_cmd(self, hdr->master_id, hdr->data, payload_len, rsp, &rsp_len);

            if (rsp_len > 0) {
                send_rsp(self, hdr->master_id, rsp, rsp_len);
            }
        } else if (self->on_data) {
            self->on_data(self, hdr->master_id, hdr->data, payload_len);
        }
    } else if (type == IR_CTRL_BCAST) {
        if (self->on_data) {
            self->on_data(self, hdr->master_id, hdr->data, payload_len);
        }
    } else if (type == IR_CTRL_DATA) {
        if (self->on_data) {
            self->on_data(self, hdr->master_id, hdr->data, payload_len);
        }
    }
}
