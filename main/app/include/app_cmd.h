#pragma once

/**
 * @file app_cmd.h
 * @brief 统一命令调度框架
 *
 * 整合 IR / ESP-NOW / CAN / UART0 / WebUI 等多链路的命令收发，
 * 提供统一的命令注册、路由和响应机制。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CMD_MAX_HANDLERS       32
#define CMD_MAX_PAYLOAD        200
#define CMD_MAX_RSP_LEN        200
#define CMD_NAME_MAX_LEN       16

typedef enum {
    CMD_SRC_IR       = 0x01,
    CMD_SRC_ESPNOW   = 0x02,
    CMD_SRC_CAN      = 0x03,
    CMD_SRC_UART     = 0x04,
    CMD_SRC_WEBUI    = 0x05,
    CMD_SRC_LOCAL    = 0x06,
} cmd_source_t;

typedef enum {
    /* 系统命令 0x01-0x0F */
    CMD_TYPE_QUERY_STATUS   = 0x01,
    CMD_TYPE_REBOOT         = 0x02,
    CMD_TYPE_QUERY_VERSION  = 0x03,
    CMD_TYPE_QUERY_STATS    = 0x04,
    CMD_TYPE_SET_CONFIG     = 0x05,
    CMD_TYPE_GET_CONFIG     = 0x06,

    /* 设备控制 0x10-0x1F */
    CMD_TYPE_LED_CTRL       = 0x10,
    CMD_TYPE_LED_EFFECT     = 0x11,
    CMD_TYPE_LED_BRIGHTNESS = 0x12,

    /* 显示控制 0x20-0x2F */
    CMD_TYPE_OLED_CLEAR     = 0x20,
    CMD_TYPE_OLED_TEXT      = 0x21,
    CMD_TYPE_OLED_REFRESH   = 0x22,

    /* 输入设备 0x30-0x3F */
    CMD_TYPE_KEY_QUERY      = 0x30,
    CMD_TYPE_KEY_EVENT      = 0x31,

    /* 桥接命令 0x40-0x4F */
    CMD_TYPE_BRIDGE_IR_ESP  = 0x40,
    CMD_TYPE_BRIDGE_CAN_ESP = 0x41,
    CMD_TYPE_BRIDGE_IR_CAN  = 0x42,

    /* 用户自定义 0xF0-0xFF */
    CMD_TYPE_USER_MIN       = 0xF0,
    CMD_TYPE_USER_MAX       = 0xFF,
} cmd_type_t;

typedef struct cmd_context cmd_context_t;

/**
 * @brief 命令上下文结构
 *
 * 包含命令的来源、发送者信息、负载数据等，
 * 用于命令处理器获取命令详情并发送响应。
 */
struct cmd_context {
    cmd_source_t source;
    uint8_t      src_id;
    uint8_t      dst_id;
    uint8_t      cmd_type;
    uint8_t      seq;
    uint32_t     timestamp;

    const uint8_t *payload;
    size_t        payload_len;

    uint8_t      *rsp_buf;
    size_t        rsp_max_len;
    size_t        rsp_len;
    esp_err_t     rsp_status;

    bool          rsp_pending;
    void         *transport_ctx;
};

/**
 * @brief 命令处理回调函数类型
 * @param ctx 命令上下文
 * @return ESP_OK 成功处理，其他值表示错误
 */
typedef esp_err_t (*cmd_handler_fn_t)(cmd_context_t *ctx);

/**
 * @brief 初始化命令调度框架
 * @return ESP_OK 成功
 */
esp_err_t app_cmd_init(void);

/**
 * @brief 注册命令处理器
 * @param cmd_type 命令类型
 * @param handler  处理函数
 * @param name     处理器名称 (用于调试)
 * @return ESP_OK 成功
 */
esp_err_t app_cmd_register(uint8_t cmd_type, cmd_handler_fn_t handler,
                           const char *name);

/**
 * @brief 注销命令处理器
 * @param cmd_type 命令类型
 * @return ESP_OK 成功
 */
esp_err_t app_cmd_unregister(uint8_t cmd_type);

/**
 * @brief 分发命令到对应处理器
 * @param ctx 命令上下文
 * @return ESP_OK 成功处理
 */
esp_err_t app_cmd_dispatch(cmd_context_t *ctx);

/**
 * @brief 从 IR 链路接收的命令
 * @param src_id    发送者 ID
 * @param cmd_type  命令类型
 * @param payload   负载数据
 * @param len       负载长度
 * @param rsp_buf   响应缓冲区 (可选)
 * @param rsp_len   响应长度输出 (可选)
 * @return ESP_OK 成功
 */
esp_err_t app_cmd_from_ir(uint8_t src_id, uint8_t cmd_type,
                          const uint8_t *payload, size_t len,
                          uint8_t *rsp_buf, size_t *rsp_len);

/**
 * @brief 从 ESP-NOW 链路接收的命令
 * @param src_id    发送者 ID
 * @param cmd_type  命令类型
 * @param payload   负载数据
 * @param len       负载长度
 * @param rsp_buf   响应缓冲区 (可选)
 * @param rsp_len   响应长度输出 (可选)
 * @return ESP_OK 成功
 */
esp_err_t app_cmd_from_espnow(uint8_t src_id, uint8_t cmd_type,
                              const uint8_t *payload, size_t len,
                              uint8_t *rsp_buf, size_t *rsp_len);

/**
 * @brief 从 CAN 总线接收的命令
 * @param can_id    CAN 帧 ID
 * @param payload   负载数据
 * @param len       负载长度
 * @return ESP_OK 成功
 */
esp_err_t app_cmd_from_can(uint32_t can_id, const uint8_t *payload, size_t len);

/**
 * @brief 从 UART 接收的命令
 * @param payload   负载数据
 * @param len       负载长度
 * @return ESP_OK 成功
 */
esp_err_t app_cmd_from_uart(const uint8_t *payload, size_t len);

/**
 * @brief 本地发送命令
 * @param dst_id    目标设备 ID
 * @param cmd_type  命令类型
 * @param payload   负载数据
 * @param len       负载长度
 * @param rsp_buf   响应缓冲区 (可选)
 * @param rsp_max   响应缓冲区最大长度
 * @param rsp_len   响应长度输出 (可选)
 * @param timeout_ms 超时时间 (ms)
 * @return ESP_OK 成功
 */
esp_err_t app_cmd_send_local(uint8_t dst_id, uint8_t cmd_type,
                             const uint8_t *payload, size_t len,
                             uint8_t *rsp_buf, size_t rsp_max,
                             size_t *rsp_len, uint32_t timeout_ms);

typedef struct {
    uint32_t total_dispatched;
    uint32_t total_success;
    uint32_t total_fail;
    uint32_t total_no_handler;
    uint32_t ir_received;
    uint32_t espnow_received;
    uint32_t can_received;
    uint32_t uart_received;
} app_cmd_stats_t;

/**
 * @brief 获取命令统计信息
 * @param stats 统计信息输出
 */
void app_cmd_get_stats(app_cmd_stats_t *stats);

/**
 * @brief 打印已注册的命令处理器列表
 */
void app_cmd_print_handlers(void);

#ifdef __cplusplus
}
#endif
