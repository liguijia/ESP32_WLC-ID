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

### 待开发

| 模块 | 状态 | 说明 |
|------|------|------|
| KEY 按键 | 待完善 | 软件消抖、短按/长按事件 |
| UART0 RX | 待修复 | 需重映射到 IO18/IO19 |
| IR↔ESP-NOW 桥接 | 待开发 | 跨链路数据转发 |
| Wi-Fi AP + WebUI | 待评估 | 手机配置+监控 |
| 看门狗 | 待开发 | 系统稳定性保障 |

## 代码结构

```
main/
├── app_main.c              # 简洁入口（初始化 + 启动测试）
├── app/
│   ├── app_devtest.c       # 开发测试逻辑
│   ├── app_ir.c            # 红外底层帧收发
│   ├── app_ir_master.c     # 红外主从协议 - 主机
│   ├── app_ir_slave.c      # 红外主从协议 - 从机
│   ├── app_espnow.c        # ESP-NOW 基站协议
│   ├── app_espnow_device.c # ESP-NOW 设备协议
│   ├── app_twai.c          # TWAI 应用层接口
│   ├── app_uart0.c         # UART0 应用层接口
│   └── include/
│       └── ir_proto_common.h  # 统一协议定义
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
