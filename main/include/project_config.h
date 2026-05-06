#pragma once

// =============================================================================
// 基础信息
// =============================================================================
#define WIRELESSID_APP_NAME "WirelessID"
#define WIRELESSID_LOG_TAG "WirelessID"
#define WIRELESSID_STARTUP_BANNER "ESP32-C3 application started"

// =============================================================================
// 硬件参数配置
// =============================================================================
#define WIRELESSID_DEFAULT_UART0_BAUD_RATE 115200
#define WIRELESSID_DEFAULT_IR_UART_BAUD_RATE 4800
#define WIRELESSID_DEFAULT_I2C_FREQ_HZ 400000
#define WIRELESSID_DEFAULT_DISPLAY_I2C_ADDR 0x3C
#define WIRELESSID_DEFAULT_IR_CARRIER_HZ 38000
#define WIRELESSID_DEFAULT_IR_CARRIER_DUTY_PERCENT 33
#define WIRELESSID_DEFAULT_TWAI_BAUD_RATE 1000000

// =============================================================================
// 设备 ID 配置（唯一需要修改的地方）
// =============================================================================
// ID 分配方案：
//   0x00      : 保留
//   0xA0-0xBF : 基站端（1 个发射端通讯板）
//   0xC0-0xCF : 保留
//   0xD0-0xFE : 设备端（最多 4 个接收端通讯板）
//   0xFF      : 广播
//
// 使用方法：只需修改下方 ID，角色自动推断
//   基站端示例: #define WIRELESSID_DEVICE_ID 0xA0
//   设备端示例: #define WIRELESSID_DEVICE_ID 0xD0
#define WIRELESSID_DEVICE_ID 0xA0

// 自动角色推断宏（根据 ID 范围，无需手动配置）
#define WIRELESSID_IS_BASE                                                     \
  (WIRELESSID_DEVICE_ID >= 0xA0 && WIRELESSID_DEVICE_ID <= 0xBF)
#define WIRELESSID_IS_DEVICE                                                   \
  (WIRELESSID_DEVICE_ID >= 0xD0 && WIRELESSID_DEVICE_ID <= 0xFE)

// =============================================================================
// 业务功能开关
// =============================================================================
// 红外 CAN 透传业务（核心功能，设备端发送 CAN 数据，基站端接收转发）
#define WIRELESSID_BIZ_ENABLE 1

// 无线功能开关（WebUI + ESP-NOW 组网）
//   1 = 开启：WebUI 仪表盘 + ESP-NOW 设备发现/心跳
//   0 = 关闭：仅保留红外 CAN 透传，不启动 Wi-Fi
#define WIRELESSID_WIRELESS_ENABLE 1

// =============================================================================
// CAN 软件过滤器配置
// =============================================================================
// 过滤器模式：
//   0 = 禁用（接收所有 CAN ID）
//   1 = 白名单模式（只接收列表中的 CAN ID）
#define WIRELESSID_CAN_FILTER_MODE 0

// 白名单 CAN ID 列表（最多 4 个）
// 未使用的条目设为 0xFFFFFFFF
#define WIRELESSID_CAN_FILTER_ID_0 0x123
#define WIRELESSID_CAN_FILTER_ID_1 0x456
#define WIRELESSID_CAN_FILTER_ID_2 0xFFFFFFFF
#define WIRELESSID_CAN_FILTER_ID_3 0xFFFFFFFF

// =============================================================================
// 调试/测试功能开关（默认关闭，开发调试时按需打开）
// =============================================================================
// UART0 回环测试：RX 回调将数据回显
#define WIRELESSID_UART0_LOOPBACK_TEST_ENABLE 0
// UART0 存活探测：周期发送 "U0_TX_ALIVE"
#define WIRELESSID_UART0_ALIVE_PROBE_ENABLE 0
// TWAI 周期测试发送
#define WIRELESSID_TWAI_TEST_TX_ENABLE 0
// 红外 TX 测试：周期发送测试帧
#define WIRELESSID_IR_TEST_TX_ENABLE 0
// 红外 RX 测试：接收 IR 数据并打印日志
#define WIRELESSID_IR_TEST_RX_ENABLE 0
// ESP-NOW 基站测试：广播数据，设备回显
#define WIRELESSID_ESPNOW_BASE_ENABLE 0
// ESP-NOW 设备测试：接收广播并回显
#define WIRELESSID_ESPNOW_DEVICE_ENABLE 0
// 命令框架测试：测试命令分发和处理器
#define WIRELESSID_CMD_TEST_ENABLE 0

// =============================================================================
// 系统任务参数
// =============================================================================
#define WIRELESSID_STATUS_TASK_STACK_SIZE 4096
#define WIRELESSID_STATUS_TASK_PRIORITY 5
