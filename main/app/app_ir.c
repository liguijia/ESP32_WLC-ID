#include "app_ir.h"

#include <string.h>

#include "bsp_ir_hw.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define APP_IR_RX_TASK_STACK 4096
#define APP_IR_RX_TASK_PRIO  3
#define APP_IR_RX_BUF_SIZE   512

#define APP_IR_FRAME_OVERHEAD 6

static const char *TAG = "app_ir";

static app_ir_rx_cb_t s_rx_cb;
static app_ir_stats_t s_stats;
static bool s_started;

static uint16_t crc16_calc(const uint8_t *data, size_t len)
{
    return esp_crc16_le(0, data, len);
}

static void app_ir_dispatch_rx(const uint8_t *data, size_t len)
{
    if (s_rx_cb) {
        s_rx_cb(data, len);
    } else {
        ESP_LOGI(TAG, "RX[%d]: %.*s", len, (int)len, data);
    }
}

static int parse_frame(const uint8_t *buf, size_t buf_len, size_t *consumed)
{
    if (buf_len < 2) {
        *consumed = 0;
        return -1;
    }

    size_t pos = 0;
    while (pos < buf_len - 1) {
        if (buf[pos] == 0xAA && buf[pos + 1] == 0x55) {
            break;
        }
        pos++;
    }

    if (pos > 0) {
        *consumed = pos;
        return -1;
    }

    if (buf_len < 4) {
        *consumed = 0;
        return -1;
    }

    uint16_t payload_len = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
    if (payload_len > APP_IR_MAX_PAYLOAD) {
        s_stats.rx_len_errors++;
        *consumed = 2;
        return -1;
    }

    size_t frame_len = APP_IR_FRAME_OVERHEAD + payload_len;
    if (buf_len < frame_len) {
        *consumed = 0;
        return -1;
    }

    uint16_t received_crc = (uint16_t)buf[frame_len - 2] | ((uint16_t)buf[frame_len - 1] << 8);
    uint16_t calc_crc = crc16_calc(&buf[4], payload_len);

    if (received_crc != calc_crc) {
        s_stats.rx_crc_errors++;
        ESP_LOGW(TAG, "CRC error: rx=0x%04x calc=0x%04x", received_crc, calc_crc);
        *consumed = frame_len;
        return -1;
    }

    *consumed = frame_len;
    return 4;
}

static void ir_rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[APP_IR_RX_BUF_SIZE];
    size_t buf_pos = 0;

    ESP_LOGI(TAG, "ir_rx_task started");

    while (1) {
        int n = bsp_ir_hw_read(&buf[buf_pos], sizeof(buf) - buf_pos, pdMS_TO_TICKS(100));
        if (n > 0) {
            ESP_LOGI(TAG, "raw recv %d bytes: %.*s", n, n, &buf[buf_pos]);
            buf_pos += (size_t)n;
        } else if (n < 0) {
            ESP_LOGW(TAG, "ir read error: %d", n);
            continue;
        }

        if (buf_pos == 0) {
            continue;
        }

        while (buf_pos >= 4) {
            size_t consumed;
            int offset = parse_frame(buf, buf_pos, &consumed);

            if (consumed == 0) {
                break;
            }

            if (offset > 0) {
                size_t payload_len = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
                app_ir_dispatch_rx(&buf[offset], payload_len);
                s_stats.rx_frames++;
            }

            memmove(buf, &buf[consumed], buf_pos - consumed);
            buf_pos -= consumed;
        }

        if (buf_pos >= sizeof(buf)) {
            s_stats.rx_drops++;
            buf_pos = 0;
        }
    }
}

esp_err_t app_ir_init(void)
{
    s_stats = (app_ir_stats_t){0};
    s_started = false;
    return ESP_OK;
}

esp_err_t app_ir_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreate(ir_rx_task,
                                "ir_rx",
                                APP_IR_RX_TASK_STACK,
                                NULL,
                                APP_IR_RX_TASK_PRIO,
                                NULL);
    if (ok != pdPASS) {
        return ESP_FAIL;
    }

    s_started = true;
    return ESP_OK;
}

esp_err_t app_ir_send(const void *data, size_t len)
{
    if (data == NULL || len == 0 || len > APP_IR_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t frame[APP_IR_FRAME_OVERHEAD + APP_IR_MAX_PAYLOAD];

    frame[0] = 0xAA;
    frame[1] = 0x55;
    frame[2] = (uint8_t)(len & 0xFF);
    frame[3] = (uint8_t)((len >> 8) & 0xFF);
    memcpy(&frame[4], data, len);

    uint16_t crc = crc16_calc(data, len);
    frame[4 + len] = (uint8_t)(crc & 0xFF);
    frame[5 + len] = (uint8_t)((crc >> 8) & 0xFF);

    int written = bsp_ir_hw_write(frame, APP_IR_FRAME_OVERHEAD + len);
    if (written <= 0) {
        return ESP_FAIL;
    }

    s_stats.tx_frames++;
    return ESP_OK;
}

void app_ir_set_rx_cb(app_ir_rx_cb_t cb)
{
    s_rx_cb = cb;
}

void app_ir_get_stats(app_ir_stats_t *stats)
{
    if (stats) {
        *stats = s_stats;
    }
}

esp_err_t app_ir_send_can(const bsp_twai_msg_t *msg)
{
    if (msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t payload[11];
    size_t dlc = msg->dlc > 8 ? 8 : msg->dlc;

    payload[0] = (uint8_t)(msg->id & 0xFF);
    payload[1] = (uint8_t)((msg->id >> 8) & 0x07);
    payload[2] = (uint8_t)dlc;
    memcpy(&payload[3], msg->data, dlc);

    return app_ir_send(payload, 3 + dlc);
}

esp_err_t app_ir_parse_can(const uint8_t *payload, size_t payload_len, bsp_twai_msg_t *msg)
{
    if (payload == NULL || msg == NULL || payload_len < 3) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t id = (uint16_t)payload[0] | ((uint16_t)(payload[1] & 0x07) << 8);
    uint8_t dlc = payload[2];

    if (dlc > 8 || payload_len < (size_t)(3 + dlc)) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(msg, 0, sizeof(*msg));
    msg->id = id;
    msg->dlc = dlc;
    memcpy(msg->data, &payload[3], dlc);

    return ESP_OK;
}
