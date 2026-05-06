#include "app_ir_master.h"

#include <string.h>

#include "app_ir.h"
#include "bsp_ir_hw.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ir_master";

#define IR_FRAME_OVERHEAD (sizeof(ir_proto_hdr_t) + 2)
#define IR_ECHO_GUARD_MS  5
#define IR_MASTER_TIMEOUT_MS 50

static uint16_t crc16_calc(const uint8_t *data, size_t len) {
    return esp_crc16_le(0, data, len);
}

static esp_err_t build_and_send(ir_master_t *self, uint8_t ctrl,
                                uint8_t slave_id, const void *data, size_t len) {
    if (len > IR_PROTO_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t frame[IR_FRAME_OVERHEAD + IR_PROTO_MAX_PAYLOAD];
    ir_proto_hdr_t *hdr = (ir_proto_hdr_t *)frame;

    hdr->header = IR_PROTO_HEADER;
    hdr->ctrl = ctrl;
    hdr->master_id = self->id;
    hdr->slave_id = slave_id;
    hdr->seq = self->seq++;
    hdr->data_len = (uint8_t)len;

    if (data && len > 0) {
        memcpy(hdr->data, data, len);
    }

    size_t payload_len = offsetof(ir_proto_hdr_t, data) + len;
    uint16_t crc = crc16_calc(&frame[2], payload_len - 2);
    frame[payload_len] = (uint8_t)(crc & 0xFF);
    frame[payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

    int written = bsp_ir_hw_write(frame, payload_len + 2);
    if (written <= 0) {
        return ESP_FAIL;
    }

    self->last_tx_tick = xTaskGetTickCount();
    self->stats.tx_frames++;
    return ESP_OK;
}

esp_err_t ir_master_init(ir_master_t *self, uint8_t id) {
    if (!self || id == 0 || id == IR_PROTO_BROADCAST) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(self, 0, sizeof(*self));
    self->id = id;
    self->mutex = xSemaphoreCreateMutex();
    self->rsp_sem = xSemaphoreCreateBinary();

    if (!self->mutex || !self->rsp_sem) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "init id=0x%02x", id);
    return ESP_OK;
}

void ir_master_deinit(ir_master_t *self) {
    if (!self) return;

    if (self->mutex) {
        vSemaphoreDelete(self->mutex);
        self->mutex = NULL;
    }
    if (self->rsp_sem) {
        vSemaphoreDelete(self->rsp_sem);
        self->rsp_sem = NULL;
    }
}

void ir_master_set_rsp_cb(ir_master_t *self, ir_master_rsp_cb_t cb) {
    if (self) {
        self->on_rsp = cb;
    }
}

esp_err_t ir_master_send_cmd(ir_master_t *self, uint8_t slave_id,
                             const void *data, size_t len) {
    if (!self) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(self->mutex, portMAX_DELAY);
    esp_err_t ret = build_and_send(self, IR_CTRL_CMD, slave_id, data, len);
    xSemaphoreGive(self->mutex);
    return ret;
}

esp_err_t ir_master_send_cmd_req(ir_master_t *self, uint8_t slave_id,
                                 const void *cmd, size_t cmd_len,
                                 void *rsp, size_t rsp_max, size_t *rsp_len,
                                 uint32_t timeout_ms) {
    if (!self || !rsp || !rsp_len) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(self->mutex, portMAX_DELAY);
    self->rsp_slave_id = slave_id;
    self->rsp_len = 0;

    esp_err_t ret = build_and_send(self, IR_CTRL_CMD_REQ, slave_id, cmd, cmd_len);
    xSemaphoreGive(self->mutex);

    if (ret != ESP_OK) {
        return ret;
    }

    if (xSemaphoreTake(self->rsp_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        self->stats.tx_timeouts++;
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(self->mutex, portMAX_DELAY);
    size_t copy_len = self->rsp_len < rsp_max ? self->rsp_len : rsp_max;
    memcpy(rsp, self->rsp_buf, copy_len);
    *rsp_len = copy_len;
    xSemaphoreGive(self->mutex);

    return ESP_OK;
}

esp_err_t ir_master_send_cmd_req_default(ir_master_t *self, uint8_t slave_id,
                                         const void *cmd, size_t cmd_len,
                                         void *rsp, size_t rsp_max, size_t *rsp_len) {
    return ir_master_send_cmd_req(self, slave_id, cmd, cmd_len, rsp, rsp_max, rsp_len,
                                  IR_MASTER_TIMEOUT_MS);
}

esp_err_t ir_master_broadcast(ir_master_t *self, const void *data, size_t len) {
    if (!self) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(self->mutex, portMAX_DELAY);
    esp_err_t ret = build_and_send(self, IR_CTRL_BCAST, IR_PROTO_BROADCAST, data, len);
    xSemaphoreGive(self->mutex);
    return ret;
}

void ir_master_get_stats(ir_master_t *self, ir_proto_stats_t *stats) {
    if (self && stats) {
        *stats = self->stats;
    }
}

void ir_master_process_rx(ir_master_t *self, const uint8_t *frame, size_t len) {
    if (!self || !frame || len < IR_FRAME_OVERHEAD) {
        return;
    }

    const ir_proto_hdr_t *hdr = (const ir_proto_hdr_t *)frame;

    if (hdr->header != IR_PROTO_HEADER) {
        return;
    }

    uint8_t type = ir_ctrl_type(hdr->ctrl);

    if (type != IR_CTRL_RSP) {
        return;
    }

    if (hdr->master_id != self->id) {
        return;
    }

    size_t payload_len = len - IR_FRAME_OVERHEAD;
    uint16_t received_crc = (uint16_t)frame[len - 2] | ((uint16_t)frame[len - 1] << 8);
    uint16_t calc_crc = crc16_calc(&frame[2], len - 4);

    if (received_crc != calc_crc) {
        self->stats.rx_crc_errors++;
        ESP_LOGW(TAG, "CRC mismatch: rx=0x%04x calc=0x%04x len=%d", received_crc, calc_crc, (int)len);
        return;
    }

    self->stats.rx_frames++;

    ESP_LOGI(TAG, "RSP from 0x%02x: payload_len=%d data=[%02x %02x %02x %02x]",
             hdr->slave_id, (int)payload_len,
             payload_len > 0 ? hdr->data[0] : 0,
             payload_len > 1 ? hdr->data[1] : 0,
             payload_len > 2 ? hdr->data[2] : 0,
             payload_len > 3 ? hdr->data[3] : 0);

    xSemaphoreTake(self->mutex, portMAX_DELAY);
    self->rsp_len = payload_len < IR_PROTO_MAX_PAYLOAD ? payload_len : IR_PROTO_MAX_PAYLOAD;
    memcpy(self->rsp_buf, hdr->data, self->rsp_len);
    self->rsp_slave_id = hdr->slave_id;
    xSemaphoreGive(self->mutex);

    xSemaphoreGive(self->rsp_sem);

    if (self->on_rsp) {
        self->on_rsp(self, hdr->slave_id, hdr->data, payload_len);
    }
}
