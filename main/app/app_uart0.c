#include "app_uart0.h"

#include "bsp_uart0.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

#define APP_UART0_RX_TASK_STACK 4096
#define APP_UART0_RX_TASK_PRIO  3
#define APP_UART0_RX_CHUNK_SIZE 256
#define APP_UART0_RX_RING_SIZE  4096

static const char *TAG = "app_uart0";

static app_uart0_mode_t s_mode = APP_UART0_MODE_DEBUG;
static app_uart0_rx_cb_t s_rx_cb;
static bool s_started;
static app_uart0_stats_t s_stats;
static RingbufHandle_t s_rx_ring;

static void app_uart0_default_on_rx(const uint8_t *data, size_t len)
{
    if (s_mode == APP_UART0_MODE_NORMAL) {
        return;
    }
    ESP_LOGI(TAG, "rx bytes=%u", (unsigned)len);
    (void)data;
}

static void app_uart0_dispatch_rx(const uint8_t *data, size_t len)
{
    if (s_rx_cb) {
        s_rx_cb(data, len);
    } else {
        app_uart0_default_on_rx(data, len);
    }
}

static void app_uart0_drain_ring_and_dispatch(void)
{
    while (1) {
        size_t item_size = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(
            s_rx_ring, &item_size, 0, APP_UART0_RX_CHUNK_SIZE);
        if (item == NULL || item_size == 0) {
            return;
        }

        s_stats.rx_frames++;
        s_stats.rx_bytes += (uint32_t)item_size;
        app_uart0_dispatch_rx(item, item_size);
        vRingbufferReturnItem(s_rx_ring, item);
    }
}

static void uart0_rx_task(void *arg)
{
    (void)arg;

    QueueHandle_t q = bsp_uart0_get_event_queue();
    uint8_t buf[APP_UART0_RX_CHUNK_SIZE];

    while (1) {
        uart_event_t evt;
        if (xQueueReceive(q, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (evt.type == UART_DATA) {
            while (1) {
                size_t avail = 0;
                if (bsp_uart0_get_buffered_data_len(&avail) != ESP_OK || avail == 0) {
                    break;
                }

                size_t to_read = avail > sizeof(buf) ? sizeof(buf) : avail;
                int n = bsp_uart0_read(buf, to_read, 0);
                if (n <= 0) {
                    break;
                }

                BaseType_t ok = xRingbufferSend(s_rx_ring, buf, (size_t)n, 0);
                if (ok != pdTRUE) {
                    s_stats.rx_drops++;
                    break;
                }
            }

            app_uart0_drain_ring_and_dispatch();
            continue;
        }

        if (evt.type == UART_FIFO_OVF || evt.type == UART_BUFFER_FULL) {
            bsp_uart0_flush_input();
            xQueueReset(q);
            s_stats.rx_drops++;
        }
    }
}

esp_err_t app_uart0_init(app_uart0_mode_t mode)
{
    s_mode = mode;

    if (mode == APP_UART0_MODE_NORMAL) {
        // 尽量减少非业务串口输出（ROM/boot 阶段日志除外）
        esp_log_level_set("*", ESP_LOG_NONE);
    }

    s_stats = (app_uart0_stats_t){0};
    if (s_rx_ring) {
        vRingbufferDelete(s_rx_ring);
        s_rx_ring = NULL;
    }

    s_rx_ring = xRingbufferCreate(APP_UART0_RX_RING_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (s_rx_ring == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return bsp_uart0_init();
}

esp_err_t app_uart0_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    if (bsp_uart0_get_event_queue() == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ok = xTaskCreate(uart0_rx_task,
                                "uart0_rx",
                                APP_UART0_RX_TASK_STACK,
                                NULL,
                                APP_UART0_RX_TASK_PRIO,
                                NULL);
    if (ok != pdPASS) {
        return ESP_FAIL;
    }

    s_started = true;
    return ESP_OK;
}

esp_err_t app_uart0_send(const void *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = bsp_uart0_write(data, len);
    if (written < 0 || (size_t)written != len) {
        s_stats.tx_errors++;
        return ESP_FAIL;
    }

    s_stats.tx_frames++;
    s_stats.tx_bytes += (uint32_t)written;

    return ESP_OK;
}

void app_uart0_set_rx_cb(app_uart0_rx_cb_t cb)
{
    s_rx_cb = cb;
}

void app_uart0_get_stats(app_uart0_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    *stats = s_stats;
}
