# WirelessID

基于 **ESP32-C3 + ESP-IDF v5.4** 的嵌入式工程，支持 CAN / 红外 / ESP-NOW / I2C 屏幕 / WS2812B 多模块协同。

## 当前状态

| 模块 | 状态 |
|------|------|
| 工程架构 (app/bsp/pinmux 分层) | ✅ 完成 |
| 系统入口 & 心跳日志 | ✅ 完成 |
| WS2812B 状态灯驱动 & 效果引擎 | ✅ 完成 |
| TWAI (CAN) 驱动, 1Mbps NO_ACK | ✅ 完成 |
| UART0 初始化骨架 | ✅ 骨架 |
| I2C / OLED 屏幕 (SSD1306) | ✅ 稳定运行（已修复 I2C 报错） |
| KEY 按键初始化骨架 | ✅ 骨架 |
| ESP-NOW 初始化骨架 | ✅ 骨架 |
| 红外 UART + 38kHz carrier 骨架 | ✅ 骨架 |

## 已知问题 (Known Issues)

### I2C / OLED 历史问题（已修复）

历史现象：串口持续输出以下报错，伴随 `I2C software timeout`，严重时 OLED 黑屏：

```
E (xxxxx) i2c.master: s_i2c_synchronous_transaction(945): I2C transaction failed
E (xxxxx) i2c.master: i2c_master_multi_buffer_transmit(1207): I2C transaction failed
```

**本次根因定位**（基于 ESP-IDF v5.4 官方 `i2c_master` API 语义）：
- `i2c_master_transmit/receive/transmit_receive` 的 timeout 参数单位是 **毫秒（int）**，不是 `TickType_t`
- 代码中将 `pdMS_TO_TICKS(...)` 结果传入上述 API，导致超时参数语义错误，引发连续事务失败与软件超时

**修复动作**：
- 统一将 I2C 事务 timeout 改为毫秒 int（如 50/100）
- 保留并显式启用 I2C 内部上拉（`enable_internal_pullup` + `gpio_set_pull_mode(..., GPIO_PULLUP_ONLY)`）
- 发生 `ESP_ERR_TIMEOUT` 时执行 `i2c_master_bus_reset()` 尝试总线恢复
- OLED init 全部命令增加返回值检查，避免“失败却打印 `oled ok`”的假成功

**当前结果**：
- OLED 显示正常
- 串口 I2C 报错消失
- TWAI 收发正常并可与 OLED 同时稳定运行

## WS2812B 状态灯 API

```c
// 头文件: bsp/include/bsp_ws2812.h
// 一个函数完成所有操作，内部 FreeRTOS 任务自动管理刷新

// 关灯
bsp_ws2812_play(BSP_WS2812_EFFECT_NONE, 0, 0, 0, 0, 0);

// 单色常亮
bsp_ws2812_play(BSP_WS2812_EFFECT_SOLID, r, g, b, 0, 0);

// 单色呼吸 (r,g,b=颜色, brightness=最高亮度0-255, period_ms=周期ms)
bsp_ws2812_play(BSP_WS2812_EFFECT_BREATH, r, g, b, brightness, period_ms);

// 彩虹呼吸 (brightness=最高亮度, period_ms=周期ms)
bsp_ws2812_play(BSP_WS2812_EFFECT_RAINBOW, 0, 0, 0, brightness, period_ms);
```

调用一次即可，后续刷新由后台任务自动完成，无需手动 update。

## OLED 屏幕 API

**驱动芯片**: SSD1306，I2C 地址 0x3C，128×64 分辨率，DMA 刷新

```c
// 头文件: bsp/include/bsp_display.h

// 初始化前设置旋转方向（必须放在 app_system_init() 之前）
bsp_display_set_rotation(DISP_ROT_0);    // 0°
bsp_display_set_rotation(DISP_ROT_90);   // 90° 顺时针
bsp_display_set_rotation(DISP_ROT_180);  // 180° 倒置
bsp_display_set_rotation(DISP_ROT_270);  // 270°

// 文字输出（row: 0-4, col: 0-20）
bsp_display_printf(row, col, "fmt", ...);
bsp_display_show_string(row, col, "text");
bsp_display_show_char(row, col, 'A');

// 像素绘制
bsp_display_draw_point(x, y, DISP_PEN_WRITE);
bsp_display_draw_line(x1, y1, x2, y2, DISP_PEN_WRITE);

// 刷新到屏幕
bsp_display_clear();
bsp_display_refresh();
```

0° 和 180° 使用 SSD1306 硬件 remap（零开销），90°/270° 使用软件坐标变换。

> **注意**: 当前 init 序列中 seg/com 映射为硬编码（0xc8/0xa1），`bsp_display_set_rotation()` 仅影响 `draw_point` 软件的坐标变换，不影响硬件方向。如需更改硬件方向，需修改 `bsp_display_init()` 中的 `oled_write_cmd(0xc8)` / `oled_write_cmd(0xa1)` 两行。

## TWAI (CAN) API

**引脚**: TX=IO7, RX=IO6，默认 1Mbps NO_ACK 模式，应用层接收任务 + 回调

```c
// 头文件: bsp/include/bsp_twai.h

// 消息结构 (HAL 风格)
bsp_twai_msg_t msg = {
    .id   = 0x123,
    .extd = false,
    .rtr  = false,
    .dlc  = 8,
    .data = {0x01, 0x02, ...},
};

// 发送（阻塞/非阻塞）
bsp_twai_transmit(&msg, pdMS_TO_TICKS(100));

// 轮询接收
bsp_twai_receive(&msg, pdMS_TO_TICKS(100));

// 中断接收回调
void on_rx(const bsp_twai_msg_t *msg) { ... }
bsp_twai_register_rx_cb(on_rx);

// 错误回调
void on_err(uint32_t alerts, uint32_t tx_err, uint32_t rx_err) { ... }
bsp_twai_register_err_cb(on_err);

// 过滤器配置
bsp_twai_filter_t filters[] = {
    {.id = 0x100, .mask = 0x7F0, .extd = false},
};
bsp_twai_config_filter(filters, 1);
```

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

## 自动串口发现

优先按顺序搜索：`/host-dev/serial/by-id/*` → `/dev/serial/by-id/*` → ttyACM* → ttyUSB*。

多设备时需手动指定 `PORT`：
```bash
make flash PORT=/host-dev/serial/by-id/your-device
```
