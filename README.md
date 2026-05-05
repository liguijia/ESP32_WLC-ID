# WirelessID

基于 **ESP32-C3 + ESP-IDF v5.4** 的嵌入式工程，支持 CAN / 红外 / ESP-NOW / I2C 屏幕 / WS2812B 多模块协同。

## 当前状态

### 通信协议层（已完成）

| 模块 | 状态 | 说明 |
|------|------|------|
| TWAI (CAN) | ✅ | 1Mbps，bus-off 自动恢复，WebUI 实时监控 |
| 红外串口 | ✅ | UART1 + 38kHz PWM，4800 波特率稳定 |
| 红外主从协议 | ✅ | CMD_REQ/RSP 双向，~65% 成功率 |
| ESP-NOW 组网 | ✅ | 设备发现/心跳/CMD_REQ-RSP，1Hz 稳定 |
| CAN-IR 桥接 | ✅ | TX→IR→RX→TWAI 链路验证通过 |

### 硬件驱动层（已完成）

| 模块 | 状态 | 说明 |
|------|------|------|
| I2C / OLED | ✅ | SSD1306 128×64，I2C DMA，旋转支持 |
| WS2812B | ✅ | RMT 驱动 + 效果引擎（SOLID/BREATH/RAINBOW） |
| UART0 TX | ✅ | 发送稳定，事件驱动接收 |
| WebUI 仪表盘 | ✅ | Wi-Fi AP + HTTP，实时状态监控 |
| KEY 按键 | ⚠️ | 仅 GPIO 初始化骨架 |

### 应用层（已完成）

| 模块 | 状态 | 说明 |
|------|------|------|
| 命令调度框架 | ✅ | 统一命令注册/路由/响应，支持多链路 |
| 系统命令 | ✅ | 查询状态/版本/统计、重启、配置 |
| 设备控制 | ✅ | WS2812 灯光控制（颜色/效果/亮度） |
| 显示控制 | ✅ | OLED 清屏/文本显示/刷新 |
| 输入设备 | ✅ | 按键查询/事件（骨架） |

### 待开发

| 模块 | 状态 | 说明 |
|------|------|------|
| KEY 按键 | 待完善 | 软件消抖、短按/长按事件 |
| UART0 RX | 待修复 | 需重映射到 IO18/IO19 |
| IR↔ESP-NOW 桥接 | 待开发 | 跨链路数据转发 |
| 看门狗 | 待开发 | 系统稳定性保障 |

## 代码结构

```
main/
├── app_main.c              # 简洁入口（初始化 + 启动测试）
├── app/
│   ├── app_cmd.c           # 命令调度框架
│   ├── app_cmd_handlers.c  # 内置命令处理器
│   ├── app_cmd_test.c      # 命令框架测试
│   ├── app_devtest.c       # 开发测试逻辑
│   ├── app_ir.c            # 红外底层帧收发
│   ├── app_ir_master.c     # 红外主从协议 - 主机
│   ├── app_ir_slave.c      # 红外主从协议 - 从机
│   ├── app_espnow.c        # ESP-NOW 基站协议
│   ├── app_espnow_device.c # ESP-NOW 设备协议
│   ├── app_twai.c          # TWAI 应用层接口
│   ├── app_uart0.c         # UART0 应用层接口
│   ├── app_webui.c         # WebUI 仪表盘
│   └── include/
│       ├── app_cmd.h           # 命令框架接口
│       ├── app_cmd_handlers.h  # 命令处理器接口
│       ├── app_cmd_test.h      # 测试接口
│       └── ir_proto_common.h   # 统一协议定义
├── bsp/                    # 硬件驱动层
├── pinmux/                 # 引脚配置
└── include/                # 公共配置
```

## 硬件引脚映射

| GPIO | 功能 | 说明 |
|------|------|------|
| IO0 | UART1_RX | 红外接收 |
| IO1 | UART1_TX | 红外发射 |
| IO2 | PWM | 38kHz 载波 |
| IO3 | WS2812B | 状态灯 |
| IO4 | I2C_SCL | OLED |
| IO5 | I2C_SDA | OLED |
| IO6 | TWAI_RX | CAN |
| IO7 | TWAI_TX | CAN |
| IO10 | KEY | 按键输入 |
| IO18/IO19 | UART0 重映射 | 计划中 |

## ID 分配方案

| 范围 | 用途 |
|------|------|
| 0x00 | 保留 |
| 0xA0-0xBF | 基站 |
| 0xC0-0xCF | 保留 |
| 0xD0-0xFE | 设备 |
| 0xFF | 广播 |

## 常用命令

```bash
cd /workspaces/ESP32_dev/WirelessID
make build           # 编译
make flash-monitor   # 烧录并查看日志
make log             # 仅查看日志
make doctor          # 环境自检
make erase-app       # 擦除 app 分区
```

串口日志退出用 `Ctrl + ]`。

## 命令框架快速参考

### 启用测试

在 `project_config.h` 中设置：
```c
#define WIRELESSID_CMD_TEST_ENABLE 1
```

### UART 命令格式

通过串口终端发送十六进制命令：

| 命令 | 字节序列 | 说明 |
|------|----------|------|
| 查询状态 | `01` | 返回设备ID、运行时间 |
| 查询版本 | `03` | 返回版本号、设备名 |
| 查询统计 | `04` | 返回命令统计信息 |
| 重启 | `02` | 重启设备 |
| LED 红色 | `10 FF 00 00` | R=255, G=0, B=0 |
| LED 绿色 | `10 00 FF 00` | R=0, G=255, B=0 |
| 呼吸灯效果 | `11 02 00 FF 00 80` | effect=BREATH, R=0, G=255, B=0 |
| OLED 清屏 | `20` | 清除显示 |
| OLED 显示文本 | `21 00 00 48 65 6C 6C 6F` | row=0, col=0, "Hello" |

### 命令类型码

| 范围 | 用途 |
|------|------|
| 0x01-0x0F | 系统命令 |
| 0x10-0x1F | 设备控制（LED） |
| 0x20-0x2F | 显示控制（OLED） |
| 0x30-0x3F | 输入设备（按键） |
| 0x40-0x4F | 桥接命令 |
| 0xF0-0xFF | 用户自定义 |

详细文档见 `doc/cmd_design.md`。
