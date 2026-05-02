#include "bsp_espnow.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "bsp_espnow";
static bsp_espnow_recv_cb_t s_recv_cb;
static bsp_espnow_send_cb_t s_send_cb;
static bool s_espnow_initialized;

static void bsp_espnow_on_send(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (s_send_cb != NULL) {
        s_send_cb(mac_addr, status);
    }
}

static void bsp_espnow_on_recv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (s_recv_cb != NULL && recv_info != NULL) {
        s_recv_cb(recv_info->src_addr, data, len);
    }
}

esp_err_t bsp_espnow_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "nvs init failed");

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_config), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi set storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp-now init failed");
    ESP_RETURN_ON_ERROR(esp_now_register_send_cb(bsp_espnow_on_send), TAG, "esp-now send cb failed");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(bsp_espnow_on_recv), TAG, "esp-now recv cb failed");

    s_espnow_initialized = true;
    return ESP_OK;
}

esp_err_t bsp_espnow_register_recv_cb(bsp_espnow_recv_cb_t cb)
{
    s_recv_cb = cb;
    return ESP_OK;
}

esp_err_t bsp_espnow_register_send_cb(bsp_espnow_send_cb_t cb)
{
    s_send_cb = cb;
    return ESP_OK;
}

esp_err_t bsp_espnow_send(const uint8_t *mac, const uint8_t *data, size_t len)
{
    if (!s_espnow_initialized || mac == NULL || data == NULL || len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_now_send(mac, data, len);
}
