# 统一命令调度框架设计文档

> 日期：2026-05-05
> 状态：已实现

## 1. 概述

统一命令调度框架（Command Dispatch Framework）整合了 IR / ESP-NOW / CAN / UART0 / WebUI 等多链路的命令收发，提供统一的命令注册、路由和响应机制。

### 设计目标

- **统一接口**：所有链路使用相同的命令处理接口
- **可扩展性**：支持动态注册/注销命令处理器
- **多链路支持**：IR、ESP-NOW、CAN、UART、WebUI
- **统计功能**：记录各链路接收次数、成功/失败次数
- **线程安全**：使用 FreeRTOS 互斥锁保护共享数据

### 文件结构

```
main/app/
├── app_cmd.h              # 命令调度框架头文件
├── app_cmd.c              # 命令调度框架实现
├── app_cmd_handlers.h     # 内置命令处理器头文件
├── app_cmd_handlers.c     # 内置命令处理器实现
├── app_cmd_test.h         # 测试代码头文件
└── app_cmd_test.c         # 测试代码实现
```

## 2. 命令格式

### 统一命令帧格式

```
┌──────────┬──────────────┬─────────────────────────────────┐
│ cmd_type │ payload_len  │ payload (可选)                  │
│ 1 byte   │ 0-N bytes    │ 变长                            │
└──────────┴──────────────┴─────────────────────────────────┘
```

### 与 IR/ESP-NOW 协议的关系

命令帧作为 IR/ESP-NOW 协议的 payload 部分：

```
┌──────────┬──────┬──────────┬──────────┬─────────────────────┬──────┬──────┐
│ header   │ ctrl │ master_id│ slave_id │ cmd_type + payload  │ seq  │ crc  │
│ 2 bytes  │ 1B   │ 1B       │ 1B       │ N bytes             │ 1B   │ 2B   │
│ 0xAA55   │      │          │          │ (命令帧)             │      │      │
└──────────┴──────┴──────────┴──────────┴─────────────────────┴──────┴──────┘
```

## 3. 命令来源标识

| 来源 | 值 | 说明 |
|------|-----|------|
| CMD_SRC_IR | 0x01 | 红外链路 |
| CMD_SRC_ESPNOW | 0x02 | ESP-NOW 链路 |
| CMD_SRC_CAN | 0x03 | CAN 总线 |
| CMD_SRC_UART | 0x04 | UART 串口 |
| CMD_SRC_WEBUI | 0x05 | WebUI |
| CMD_SRC_LOCAL | 0x06 | 本地触发 |

## 4. 命令类型定义

### 系统命令 (0x01-0x0F)

| 命令 | 类型码 | 说明 | 请求格式 | 响应格式 |
|------|--------|------|----------|----------|
| QUERY_STATUS | 0x01 | 查询系统状态 | 无 | [device_id, version, uptime(4 bytes)] |
| REBOOT | 0x02 | 重启设备 | 无 | [0x01, 0x00] |
| QUERY_VERSION | 0x03 | 查询版本信息 | 无 | [major, minor, patch, name...] |
| QUERY_STATS | 0x04 | 查询统计信息 | 无 | [dispatched(2), success(2), fail(2), no_handler(2)] |
| SET_CONFIG | 0x05 | 设置配置参数 | [param_id, value...] | [status] |
| GET_CONFIG | 0x06 | 获取配置参数 | [param_id] | [status, value...] |

### 设备控制 (0x10-0x1F)

| 命令 | 类型码 | 说明 | 请求格式 | 响应格式 |
|------|--------|------|----------|----------|
| LED_CTRL | 0x10 | WS2812 灯光控制 | [R, G, B] | [status, reserved] |
| LED_EFFECT | 0x11 | 灯光效果控制 | [effect, R, G, B, brightness, period(2)] | [status, effect] |
| LED_BRIGHTNESS | 0x12 | 灯光亮度控制 | [brightness] | [status, brightness] |

**灯光效果类型**：
- 0x00: NONE (关闭)
- 0x01: SOLID (常亮)
- 0x02: BREATH (呼吸灯)
- 0x03: RAINBOW (彩虹)

### 显示控制 (0x20-0x2F)

| 命令 | 类型码 | 说明 | 请求格式 | 响应格式 |
|------|--------|------|----------|----------|
| OLED_CLEAR | 0x20 | 清除 OLED 显示 | 无 | [status] |
| OLED_TEXT | 0x21 | 显示文本 | [row, col, text...] | [status, row] |
| OLED_REFRESH | 0x22 | 刷新 OLED 显示 | 无 | [status] |

### 输入设备 (0x30-0x3F)

| 命令 | 类型码 | 说明 | 请求格式 | 响应格式 |
|------|--------|------|----------|----------|
| KEY_QUERY | 0x30 | 查询按键状态 | 无 | [status, key_state] |
| KEY_EVENT | 0x31 | 按键事件通知 | [event_type, key_id] | [status] |

### 桥接命令 (0x40-0x4F)

| 命令 | 类型码 | 说明 | 请求格式 | 响应格式 |
|------|--------|------|----------|----------|
| BRIDGE_IR_ESP | 0x40 | IR ↔ ESP-NOW 桥接 | [target_id, data...] | [status] |
| BRIDGE_CAN_ESP | 0x41 | CAN ↔ ESP-NOW 桥接 | [can_id(4), data...] | [status] |
| BRIDGE_IR_CAN | 0x42 | IR ↔ CAN 桥接 | [slave_id, can_id(4), data...] | [status] |

### 用户自定义 (0xF0-0xFF)

保留给用户自定义命令处理器。

## 5. API 接口

### 初始化

```c
#include "app_cmd.h"

// 初始化命令调度框架
esp_err_t app_cmd_init(void);

// 注册内置命令处理器
esp_err_t app_cmd_handlers_init(void);
```

### 命令处理器注册

```c
// 定义命令处理函数
esp_err_t my_handler(cmd_context_t *ctx) {
    // 处理命令
    ESP_LOGI(TAG, "cmd=0x%02x, src=0x%02x", ctx->cmd_type, ctx->src_id);
    
    // 处理负载数据
    if (ctx->payload_len > 0) {
        ESP_LOGI(TAG, "payload[0]=0x%02x", ctx->payload[0]);
    }
    
    // 设置响应
    if (ctx->rsp_buf != NULL && ctx->rsp_max_len >= 2) {
        ctx->rsp_buf[0] = 0x00;  // 成功
        ctx->rsp_buf[1] = 0x01;  // 数据
        ctx->rsp_len = 2;
        ctx->rsp_status = ESP_OK;
    }
    
    return ESP_OK;
}

// 注册命令处理器
app_cmd_register(CMD_TYPE_USER_MIN, my_handler, "my_handler");

// 注销命令处理器
app_cmd_unregister(CMD_TYPE_USER_MIN);
```

### 从各链路接收命令

```c
// 从 IR 链路接收
esp_err_t app_cmd_from_ir(uint8_t src_id, uint8_t cmd_type,
                          const uint8_t *payload, size_t len,
                          uint8_t *rsp_buf, size_t *rsp_len);

// 从 ESP-NOW 链路接收
esp_err_t app_cmd_from_espnow(uint8_t src_id, uint8_t cmd_type,
                              const uint8_t *payload, size_t len,
                              uint8_t *rsp_buf, size_t *rsp_len);

// 从 CAN 总线接收
esp_err_t app_cmd_from_can(uint32_t can_id, const uint8_t *payload, size_t len);

// 从 UART 接收
esp_err_t app_cmd_from_uart(const uint8_t *payload, size_t len);
```

### 本地发送命令

```c
// 本地发送命令（用于测试或内部调用）
esp_err_t app_cmd_send_local(uint8_t dst_id, uint8_t cmd_type,
                             const uint8_t *payload, size_t len,
                             uint8_t *rsp_buf, size_t rsp_max,
                             size_t *rsp_len, uint32_t timeout_ms);
```

### 统计和调试

```c
// 获取统计信息
app_cmd_stats_t stats;
app_cmd_get_stats(&stats);
ESP_LOGI(TAG, "dispatched=%" PRIu32 " success=%" PRIu32, 
         stats.total_dispatched, stats.total_success);

// 打印已注册的命令处理器
app_cmd_print_handlers();
```

## 6. 使用示例

### 示例 1：查询系统状态

```c
uint8_t rsp[32] = {0};
size_t rsp_len = 0;

esp_err_t ret = app_cmd_send_local(0, CMD_TYPE_QUERY_STATUS, NULL, 0,
                                   rsp, sizeof(rsp), &rsp_len, 500);
if (ret == ESP_OK && rsp_len >= 6) {
    uint8_t device_id = rsp[0];
    uint32_t uptime = rsp[2] | (rsp[3] << 8) | (rsp[4] << 16) | (rsp[5] << 24);
    ESP_LOGI(TAG, "device_id=0x%02x, uptime=%" PRIu32, device_id, uptime);
}
```

### 示例 2：WS2812 灯光控制

```c
// 设置红色常亮
uint8_t cmd[3] = {0xFF, 0x00, 0x00};  // R=255, G=0, B=0
uint8_t rsp[8] = {0};
size_t rsp_len = 0;

esp_err_t ret = app_cmd_send_local(0, CMD_TYPE_LED_CTRL, cmd, 3,
                                   rsp, sizeof(rsp), &rsp_len, 500);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "LED set to red");
}
```

### 示例 3：灯光效果控制

```c
// 设置绿色呼吸灯
uint8_t cmd[5] = {0x02, 0x00, 0xFF, 0x00, 0x80};  // effect=BREATH, R=0, G=255, B=0, brightness=128
uint8_t rsp[8] = {0};
size_t rsp_len = 0;

esp_err_t ret = app_cmd_send_local(0, CMD_TYPE_LED_EFFECT, cmd, 5,
                                   rsp, sizeof(rsp), &rsp_len, 500);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "LED effect set to green breath");
}
```

### 示例 4：OLED 显示文本

```c
// 在第 0 行第 0 列显示文本
uint8_t cmd[16] = {0x00, 0x00};  // row=0, col=0
memcpy(&cmd[2], "Hello!", 6);
uint8_t rsp[8] = {0};
size_t rsp_len = 0;

esp_err_t ret = app_cmd_send_local(0, CMD_TYPE_OLED_TEXT, cmd, 8,
                                   rsp, sizeof(rsp), &rsp_len, 500);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Text displayed on OLED");
}
```

### 示例 5：OLED 清屏和刷新

```c
// 清屏
app_cmd_send_local(0, CMD_TYPE_OLED_CLEAR, NULL, 0, NULL, 0, NULL, 500);

// 显示文本
uint8_t cmd[16] = {0x00, 0x00};
memcpy(&cmd[2], "WirelessID", 10);
app_cmd_send_local(0, CMD_TYPE_OLED_TEXT, cmd, 12, NULL, 0, NULL, 500);

// 刷新显示
app_cmd_send_local(0, CMD_TYPE_OLED_REFRESH, NULL, 0, NULL, 0, NULL, 500);
```

### 示例 6：从 ESP-NOW 接收并处理

```c
// 在 ESP-NOW 接收回调中
void espnow_rx_callback(uint8_t src_id, const uint8_t *data, size_t len) {
    if (len < 1) return;
    
    uint8_t cmd_type = data[0];
    const uint8_t *payload = len > 1 ? &data[1] : NULL;
    size_t payload_len = len > 1 ? len - 1 : 0;
    
    uint8_t rsp[64] = {0};
    size_t rsp_len = 0;
    
    esp_err_t ret = app_cmd_from_espnow(src_id, cmd_type, payload, payload_len,
                                        rsp, &rsp_len);
    if (ret == ESP_OK && rsp_len > 0) {
        // 发送响应
        espnow_send_response(src_id, rsp, rsp_len);
    }
}
```

### 示例 7：自定义命令处理器

```c
// 定义自定义命令处理函数
static esp_err_t handle_my_command(cmd_context_t *ctx) {
    if (ctx->payload_len < 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    uint8_t param = ctx->payload[0];
    ESP_LOGI(TAG, "My command: param=%d", param);
    
    if (ctx->rsp_buf != NULL && ctx->rsp_max_len >= 2) {
        ctx->rsp_buf[0] = 0x00;  // 成功
        ctx->rsp_buf[1] = param;
        ctx->rsp_len = 2;
        ctx->rsp_status = ESP_OK;
    }
    
    return ESP_OK;
}

// 注册自定义命令
app_cmd_register(CMD_TYPE_USER_MIN, handle_my_command, "my_command");
```

## 7. 集成指南

### 在 app_main.c 中初始化

```c
#include "app_cmd.h"
#include "app_cmd_handlers.h"

void app_main(void) {
    // ... 其他初始化 ...
    
    // 初始化命令框架
    esp_err_t ret = app_cmd_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cmd init failed: %d", ret);
        return;
    }
    
    // 注册内置命令处理器
    ret = app_cmd_handlers_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cmd handlers init failed: %d", ret);
        return;
    }
    
    // 注册自定义命令处理器
    app_cmd_register(CMD_TYPE_USER_MIN, my_handler, "my_handler");
    
    // ... 启动其他任务 ...
}
```

### 在 IR 接收回调中集成

```c
void ir_rx_callback(const uint8_t *data, size_t len) {
    if (len < 2) return;
    
    uint8_t cmd_type = data[0];
    const uint8_t *payload = &data[1];
    size_t payload_len = len - 1;
    
    uint8_t rsp[64] = {0};
    size_t rsp_len = 0;
    
    app_cmd_from_ir(src_id, cmd_type, payload, payload_len, rsp, &rsp_len);
    
    if (rsp_len > 0) {
        // 发送响应
        ir_send_response(rsp, rsp_len);
    }
}
```

### 在 ESP-NOW 接收回调中集成

```c
void espnow_rx_callback(uint8_t src_id, const uint8_t *data, size_t len) {
    if (len < 1) return;
    
    uint8_t cmd_type = data[0];
    const uint8_t *payload = len > 1 ? &data[1] : NULL;
    size_t payload_len = len > 1 ? len - 1 : 0;
    
    uint8_t rsp[64] = {0};
    size_t rsp_len = 0;
    
    app_cmd_from_espnow(src_id, cmd_type, payload, payload_len, rsp, &rsp_len);
    
    if (rsp_len > 0) {
        espnow_send_response(src_id, rsp, rsp_len);
    }
}
```

## 8. 测试方法

### 自动测试

启用测试开关（`project_config.h`）：

```c
#define WIRELESSID_CMD_TEST_ENABLE 1
```

烧录后，系统会自动运行测试任务，输出类似：

```
I (3844) cmd_test: === CMD Framework Test Start ===
I (3894) cmd_test: --- Test 1: Local QUERY_STATUS ---
I (3894) cmd_test:   result: 0, rsp_len=6
I (3894) cmd_test:   rsp: A0 01 03 00 00 00
...
I (4044) cmd_test: === CMD Framework Test Complete ===
I (4044) cmd_test: Stats: dispatched=5 success=5 fail=0
```

### UART 手动测试

通过串口终端发送命令（十六进制格式）：

| 命令 | 字节序列 | 说明 |
|------|----------|------|
| 查询状态 | `01` | 返回设备ID、运行时间 |
| 查询版本 | `03` | 返回版本号、设备名 |
| 查询统计 | `04` | 返回命令统计信息 |
| 重启 | `02` | 重启设备 |
| LED 红色 | `10 FF 00 00` | R=255, G=0, B=0 |
| LED 绿色 | `10 00 FF 00` | R=0, G=255, B=0 |
| LED 蓝色 | `10 00 00 FF` | R=0, G=0, B=255 |
| 呼吸灯效果 | `11 02 00 FF 00 80` | effect=BREATH, R=0, G=255, B=0, brightness=128 |
| 彩虹效果 | `11 03 00 00 00 80` | effect=RAINBOW, brightness=128 |
| OLED 清屏 | `20` | 清除显示 |
| OLED 显示文本 | `21 00 00 48 65 6C 6C 6F` | row=0, col=0, "Hello" |
| OLED 刷新 | `22` | 刷新显示 |

### ESP-NOW 测试

在另一块板子上（设备模式）发送命令：

```c
// 发送查询状态命令
uint8_t cmd[] = {CMD_TYPE_QUERY_STATUS};
uint8_t rsp[32];
size_t rsp_len;

espnow_base_send_cmd_req(&base, target_id, cmd, 1, 
                         rsp, sizeof(rsp), &rsp_len, 500);
```

### IR 红外测试

```c
// 发送命令到红外从机
uint8_t cmd[] = {CMD_TYPE_QUERY_STATUS};
uint8_t rsp[32];
size_t rsp_len;

ir_master_send_cmd_req(&master, slave_id, cmd, 1,
                       rsp, sizeof(rsp), &rsp_len, 500);
```

## 9. 扩展指南

### 添加新的命令类型

1. 在 `app_cmd.h` 中定义新的命令类型：

```c
typedef enum {
    // ... 现有命令 ...
    
    /* 用户自定义 0xF0-0xFF */
    CMD_TYPE_MY_COMMAND = 0xF1,
} cmd_type_t;
```

2. 实现命令处理函数：

```c
static esp_err_t handle_my_command(cmd_context_t *ctx) {
    // 处理命令
    return ESP_OK;
}
```

3. 注册命令处理器：

```c
app_cmd_register(CMD_TYPE_MY_COMMAND, handle_my_command, "my_command");
```

### 添加新的命令来源

1. 在 `app_cmd.h` 中定义新的来源：

```c
typedef enum {
    // ... 现有来源 ...
    CMD_SRC_NEW_LINK = 0x07,
} cmd_source_t;
```

2. 实现接收函数：

```c
esp_err_t app_cmd_from_new_link(uint8_t src_id, uint8_t cmd_type,
                                const uint8_t *payload, size_t len,
                                uint8_t *rsp_buf, size_t *rsp_len) {
    s_stats.new_link_received++;
    
    cmd_context_t ctx = {
        .source = CMD_SRC_NEW_LINK,
        .src_id = src_id,
        .cmd_type = cmd_type,
        .payload = payload,
        .payload_len = len,
        .rsp_buf = rsp_buf,
        .rsp_max_len = rsp_buf ? CMD_MAX_RSP_LEN : 0,
        .rsp_pending = (rsp_buf != NULL),
    };
    
    return app_cmd_dispatch(&ctx);
}
```

## 10. 配置选项

在 `project_config.h` 中：

```c
// 启用命令框架测试
#define WIRELESSID_CMD_TEST_ENABLE 1
```

## 11. 注意事项

1. **线程安全**：所有 API 都是线程安全的，可以在多个任务中调用
2. **响应缓冲区**：如果需要响应，必须提供非空的 `rsp_buf`
3. **超时处理**：本地命令支持超时参数，其他链路由各协议层处理
4. **内存使用**：每个命令处理器占用约 20 字节内存
5. **最大处理器数**：默认最多 32 个命令处理器

## 12. 故障排除

### 常见问题

1. **命令未处理**：检查是否注册了对应的命令处理器
2. **响应为空**：检查 `rsp_buf` 是否非空，`rsp_max_len` 是否足够
3. **统计不准确**：调用 `app_cmd_get_stats()` 查看详细统计

### 调试技巧

```c
// 打印已注册的处理器
app_cmd_print_handlers();

// 查看统计信息
app_cmd_stats_t stats;
app_cmd_get_stats(&stats);
ESP_LOGI(TAG, "dispatched=%" PRIu32 " no_handler=%" PRIu32,
         stats.total_dispatched, stats.total_no_handler);
```

## 13. 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-05-05 | 初始版本，支持基本命令调度 |
