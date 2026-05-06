#include "app_webui.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "project_config.h"

static const char *TAG = "webui";

#define WEBUI_AP_PASS     "12345678"
#define WEBUI_AP_CHANNEL  1
#define WEBUI_AP_MAX_CONN 4

static httpd_handle_t s_httpd;
static SemaphoreHandle_t s_mutex;
static bool s_initialized = false;
static app_webui_status_t s_status;
static app_webui_log_t s_log_twai;
static app_webui_log_t s_log_ir;
static app_webui_log_t s_log_espnow;
static esp_netif_t *s_ap_netif;
static char s_ssid[32];

static void log_append(app_webui_log_t *log, const char *msg) {
    if (!log || !msg) return;
    uint8_t idx = log->head;
    strncpy(log->lines[idx], msg, WEBUI_LOG_LINE_MAX - 1);
    log->lines[idx][WEBUI_LOG_LINE_MAX - 1] = '\0';
    log->head = (idx + 1) % WEBUI_LOG_LINES;
    if (log->count < WEBUI_LOG_LINES) log->count++;
}

static void log_to_json(const app_webui_log_t *log, char *buf, size_t max) {
    size_t pos = 0;
    pos += snprintf(buf + pos, max - pos, "[");
    for (uint8_t i = 0; i < log->count && pos < max - 4; i++) {
        uint8_t idx = (log->head + WEBUI_LOG_LINES - log->count + i) % WEBUI_LOG_LINES;
        if (i > 0) pos += snprintf(buf + pos, max - pos, ",");
        pos += snprintf(buf + pos, max - pos, "\"%s\"", log->lines[idx]);
    }
    snprintf(buf + pos, max - pos, "]");
}

static const char HTML_PAGE[] =
    "<!DOCTYPE html>"
    "<html lang='en' data-theme='light'>"
    "<head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>WirelessID Dashboard</title>"
    "<style>"
    ":root{--bg:#f8f9fa;--fg:#1a1a2e;--card-bg:#fff;--card-border:#e9ecef;"
    "--muted:#6c757d;--primary:#0d6efd;--success:#198754;--danger:#dc3545;"
    "--term-bg:#1e1e1e;--term-fg:#d4d4d4;--term-green:#4ec9b0;--term-cyan:#9cdcfe;"
    "--radius:12px;--gap:1rem;--font:system-ui,-apple-system,sans-serif;"
    "--mono:'Cascadia Code','Fira Code','JetBrains Mono',Consolas,monospace}"
    "@media(prefers-color-scheme:dark){"
    ":root{--bg:#1a1a2e;--fg:#e0e0e0;--card-bg:#16213e;--card-border:#0f3460;--muted:#a0a0a0}}"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:var(--font);background:var(--bg);color:var(--fg);"
    "padding:var(--gap);max-width:720px;margin:0 auto}"
    "header{text-align:center;padding:1.2rem 0 0.8rem}"
    "header h1{font-size:1.4rem;font-weight:700}"
    "header p{color:var(--muted);font-size:0.85rem;margin-top:0.2rem}"
    ".grid{display:grid;gap:var(--gap)}"
    "@media(min-width:480px){.grid{grid-template-columns:repeat(2,1fr)}}"
    "article{background:var(--card-bg);border:1px solid var(--card-border);"
    "border-radius:var(--radius);padding:var(--gap);transition:box-shadow .2s}"
    "article:hover{box-shadow:0 2px 8px rgba(0,0,0,.06)}"
    "article header{padding:0 0 0.6rem;margin-bottom:0.4rem;"
    "border-bottom:1px solid var(--card-border)}"
    "article header h2{font-size:0.8rem;font-weight:600;text-transform:uppercase;"
    "letter-spacing:.06em;color:var(--muted)}"
    ".row{display:flex;justify-content:space-between;align-items:center;"
    "padding:0.35rem 0}"
    ".lbl{color:var(--muted);font-size:0.85rem}"
    ".val{font-weight:600;font-variant-numeric:tabular-nums}"
    ".val.big{font-size:1.3rem;color:var(--primary)}"
    ".terminal{background:var(--term-bg);color:var(--term-fg);"
    "font-family:var(--mono);font-size:0.7rem;line-height:1.4;"
    "padding:0.5rem;border-radius:6px;max-height:160px;overflow-y:auto;"
    "margin-top:0.5rem;white-space:pre;word-break:break-all}"
    ".terminal .tx{color:var(--term-green)}"
    ".terminal .rx{color:var(--term-cyan)}"
    ".terminal .err{color:#f44747}"
    "footer{text-align:center;padding:1.2rem 0;color:var(--muted);font-size:0.75rem}"
    "</style>"
    "</head>"
    "<body>"
    "<header><h1>\xf0\x9f\x93\xa1 WirelessID</h1><p>Base Station Dashboard</p></header>"
    "<div class='grid'>"
    "<article><header><h2>Device</h2></header>"
    "<div class='row'><span class='lbl'>ID</span><span class='val' id='d-id'>-</span></div>"
    "<div class='row'><span class='lbl'>Uptime</span><span class='val' id='d-up'>-</span></div>"
    "<div class='row'><span class='lbl'>Online</span><span class='val big' id='d-peer'>-</span></div>"
    "<div class='row'><span class='lbl'>Peers</span><span class='val' id='d-ids' style='font-size:0.8rem;word-break:break-all'>-</span></div>"
    "</article>"
    "<article><header><h2>TWAI (CAN)</h2></header>"
    "<div class='row'><span class='lbl'>TX</span><span class='val' id='c-tx'>-</span></div>"
    "<div class='row'><span class='lbl'>RX</span><span class='val' id='c-rx'>-</span></div>"
    "<div class='row'><span class='lbl'>Drops</span><span class='val' id='c-dr'>-</span></div>"
    "<div class='terminal' id='c-log'></div>"
    "</article>"
    "<article><header><h2>Infrared</h2></header>"
    "<div class='row'><span class='lbl'>TX</span><span class='val' id='i-tx'>-</span></div>"
    "<div class='row'><span class='lbl'>RX</span><span class='val' id='i-rx'>-</span></div>"
    "<div class='row'><span class='lbl'>CRC Err</span><span class='val' id='i-err'>-</span></div>"
    "<div class='terminal' id='i-log'></div>"
    "</article>"
    "<article><header><h2>ESP-NOW</h2></header>"
    "<div class='row'><span class='lbl'>TX</span><span class='val' id='e-tx'>-</span></div>"
    "<div class='row'><span class='lbl'>RX</span><span class='val' id='e-rx'>-</span></div>"
    "<div class='row'><span class='lbl'>Ann</span><span class='val' id='e-an'>-</span></div>"
    "<div class='terminal' id='e-log'></div>"
    "</article>"
    "</div>"
    "<footer>WirelessID &bull; ESP32-C3</footer>"
    "<script>"
    "function u(i,v){var e=document.getElementById(i);if(e)e.textContent=v;}"
    "function renderLog(id,lines){"
    "var el=document.getElementById(id);if(!el)return;"
    "el.innerHTML=lines.map(function(l){"
    "if(l.startsWith('TX'))return'<span class=tx>'+l+'</span>';"
    "if(l.startsWith('RX'))return'<span class=rx>'+l+'</span>';"
    "if(l.startsWith('!'))return'<span class=err>'+l+'</span>';"
    "return l;"
    "}).join('\\n');"
    "el.scrollTop=el.scrollHeight;}"
    "function poll(){"
    "fetch('/api/status').then(function(r){return r.json()})"
    ".then(function(d){"
    "u('d-id','0x'+d.id.toString(16).toUpperCase().padStart(2,'0'));"
    "u('d-up',d.uptime+'s');"
    "u('d-peer',d.peers);u('d-ids',d.peer_ids||'none');"
    "u('c-tx',d.twai.tx);u('c-rx',d.twai.rx);u('c-dr',d.twai.drop);"
    "u('i-tx',d.ir.tx);u('i-rx',d.ir.rx);u('i-err',d.ir.crc_err);"
    "u('e-tx',d.espnow.tx);u('e-rx',d.espnow.rx);u('e-an',d.espnow.announce);"
    "if(d.log_twai)renderLog('c-log',d.log_twai);"
    "if(d.log_ir)renderLog('i-log',d.log_ir);"
    "if(d.log_espnow)renderLog('e-log',d.log_espnow);"
    "}).catch(function(){})}"
    "poll();setInterval(poll,1000);"
    "</script>"
    "</body></html>";

static esp_err_t handle_index(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_PAGE, sizeof(HTML_PAGE) - 1);
    return ESP_OK;
}

static esp_err_t handle_api_status(httpd_req_t *req) {
    app_webui_status_t st;
    app_webui_log_t log_t, log_i, log_e;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    st = s_status;
    log_t = s_log_twai;
    log_i = s_log_ir;
    log_e = s_log_espnow;
    xSemaphoreGive(s_mutex);

    static char twai_log[WEBUI_LOG_LINES * (WEBUI_LOG_LINE_MAX + 4)];
    static char ir_log[WEBUI_LOG_LINES * (WEBUI_LOG_LINE_MAX + 4)];
    static char espnow_log[WEBUI_LOG_LINES * (WEBUI_LOG_LINE_MAX + 4)];
    static char json[1024];

    log_to_json(&log_t, twai_log, sizeof(twai_log));
    log_to_json(&log_i, ir_log, sizeof(ir_log));
    log_to_json(&log_e, espnow_log, sizeof(espnow_log));

    int len = snprintf(json, sizeof(json),
        "{\"id\":%d,\"uptime\":%" PRIu32 ",\"peers\":%d,\"peer_ids\":\"%s\","
        "\"twai\":{\"tx\":%" PRIu32 ",\"rx\":%" PRIu32 ",\"drop\":%" PRIu32 "},"
        "\"ir\":{\"tx\":%" PRIu32 ",\"rx\":%" PRIu32 ",\"crc_err\":%" PRIu32 "},"
        "\"espnow\":{\"tx\":%" PRIu32 ",\"rx\":%" PRIu32 ",\"announce\":%" PRIu32 "},"
        "\"log_twai\":%s,\"log_ir\":%s,\"log_espnow\":%s}",
        st.device_id, st.uptime_sec, (int)st.peer_count,
        st.peer_ids,
        st.twai_tx_frames, st.twai_rx_frames,
        st.twai_tx_drops,
        st.ir_tx_frames, st.ir_rx_frames,
        st.ir_rx_crc_err,
        st.espnow_tx_frames, st.espnow_rx_frames,
        st.espnow_announce_recv,
        twai_log, ir_log, espnow_log);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, len);
    return ESP_OK;
}

static esp_err_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 8;

    esp_err_t ret = httpd_start(&s_httpd, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed: %d", ret);
        return ret;
    }

    httpd_uri_t uri_index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_index,
    };
    httpd_register_uri_handler(s_httpd, &uri_index);

    httpd_uri_t uri_status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = handle_api_status,
    };
    httpd_register_uri_handler(s_httpd, &uri_status);

    ESP_LOGI(TAG, "httpd started on port %d", config.server_port);
    return ESP_OK;
}

esp_err_t app_webui_init(uint8_t device_id) {
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    snprintf(s_ssid, sizeof(s_ssid), "WirelessID-%02X", device_id);

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        ESP_LOGE(TAG, "create ap netif failed");
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info = {
        .ip = {.addr = ESP_IP4TOADDR(192, 168, 4, 1)},
        .gw = {.addr = ESP_IP4TOADDR(192, 168, 4, 1)},
        .netmask = {.addr = ESP_IP4TOADDR(255, 255, 255, 0)},
    };
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip_info(s_ap_netif, &ip_info);
    esp_netif_dhcps_start(s_ap_netif);

    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(s_ssid),
            .channel = WEBUI_AP_CHANNEL,
            .password = WEBUI_AP_PASS,
            .max_connection = WEBUI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };
    memcpy(ap_config.ap.ssid, s_ssid, strlen(s_ssid));

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi ap config failed: %d", ret);
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "webui init ok, AP SSID=%s IP=192.168.4.1", s_ssid);
    return ESP_OK;
}

esp_err_t app_webui_start(void) {
    return start_webserver();
}

void app_webui_update_status(const app_webui_status_t *status) {
    if (!status) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status = *status;
    xSemaphoreGive(s_mutex);
}

void app_webui_log_twai(const char *msg) {
    if (!msg || !s_initialized) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        log_append(&s_log_twai, msg);
        xSemaphoreGive(s_mutex);
    }
}

void app_webui_log_ir(const char *msg) {
    if (!msg || !s_initialized) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        log_append(&s_log_ir, msg);
        xSemaphoreGive(s_mutex);
    }
}

void app_webui_log_espnow(const char *msg) {
    if (!msg || !s_initialized) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    log_append(&s_log_espnow, msg);
    xSemaphoreGive(s_mutex);
}
