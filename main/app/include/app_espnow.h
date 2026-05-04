#pragma once

#include "ir_proto_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define ESPNOW_MAX_PEERS      10
#define ESPNOW_PEER_TIMEOUT_MS 90000
#define ESPNOW_NAME_MAX_LEN   16

typedef struct espnow_base espnow_base_t;

typedef struct {
    uint8_t device_id;
    uint8_t mac[6];
    char name[ESPNOW_NAME_MAX_LEN];
    int8_t rssi;
    uint32_t last_seen_ms;
    bool online;
} espnow_peer_t;

typedef struct {
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t rx_crc_errors;
    uint32_t tx_timeouts;
    uint32_t discover_sent;
    uint32_t announce_recv;
} espnow_base_stats_t;

typedef void (*espnow_base_rsp_cb_t)(espnow_base_t *self, uint8_t device_id,
                                     const uint8_t *data, size_t len);

struct espnow_base {
    uint8_t id;
    char name[ESPNOW_NAME_MAX_LEN];
    uint8_t seq;

    espnow_peer_t peers[ESPNOW_MAX_PEERS];
    size_t peer_count;
    SemaphoreHandle_t peer_mutex;

    SemaphoreHandle_t rsp_sem;
    uint8_t rsp_buf[IR_PROTO_MAX_PAYLOAD];
    size_t rsp_len;
    uint8_t rsp_device_id;

    espnow_base_rsp_cb_t on_rsp;
    espnow_base_stats_t stats;
    SemaphoreHandle_t tx_mutex;
};

esp_err_t espnow_base_init(espnow_base_t *self, uint8_t id, const char *name);
void espnow_base_deinit(espnow_base_t *self);

void espnow_base_set_rsp_cb(espnow_base_t *self, espnow_base_rsp_cb_t cb);

esp_err_t espnow_base_discover(espnow_base_t *self);
esp_err_t espnow_base_get_peers(espnow_base_t *self, espnow_peer_t *peers,
                                 size_t max, size_t *count);
bool espnow_base_is_device_online(espnow_base_t *self, uint8_t device_id);

esp_err_t espnow_base_send_cmd(espnow_base_t *self, uint8_t device_id,
                                const void *data, size_t len);
esp_err_t espnow_base_send_cmd_req(espnow_base_t *self, uint8_t device_id,
                                    const void *cmd, size_t cmd_len,
                                    void *rsp, size_t rsp_max, size_t *rsp_len,
                                    uint32_t timeout_ms);
esp_err_t espnow_base_send_cmd_req_default(espnow_base_t *self, uint8_t device_id,
                                            const void *cmd, size_t cmd_len,
                                            void *rsp, size_t rsp_max, size_t *rsp_len);
esp_err_t espnow_base_broadcast(espnow_base_t *self, const void *data, size_t len);

void espnow_base_get_stats(espnow_base_t *self, espnow_base_stats_t *stats);

void espnow_base_process_rx(espnow_base_t *self, const uint8_t *mac,
                             const uint8_t *frame, size_t len);
