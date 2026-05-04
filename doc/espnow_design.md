# ESP-NOW 组网设计

> 日期：2026-05-04
> 状态：设计阶段

## 1. 硬件能力

| 特性 | 状态 | 说明 |
|------|------|------|
| Wi-Fi STA | ✅ | ESP-NOW 基础，已初始化 |
| ESP-NOW | ✅ | BSP 层已实现收发回调 |
| 最大 payload | 250 字节 | ESP-NOW 单帧限制 |
| 传输距离 | ~200m（空旷） | 取决于天线和环境 |
| 延迟 | <5ms | 单跳典型值 |

## 2. 设计目标

### 核心能力

1. **设备发现**：自动发现同一信道上的其他 WirelessID 设备
2. **点对点通信**：任意两设备间可靠数据传输
3. **广播通信**：一对多消息分发
4. **桥接转发**：IR ↔ ESP-NOW、CAN ↔ ESP-NOW

### 与现有协议的关系

```
┌─────────────────────────────────────────────────────────┐
│                    应用层                                │
│              app_devtest / 业务逻辑                      │
├──────────┬──────────┬───────────────────────────────────┤
│ app_ir   │ app_twai │         app_espnow                │
│ 主从协议 │  CAN驱动 │       ESP-NOW 协议                │
├──────────┼──────────┼───────────────────────────────────┤
│ bsp_ir   │ bsp_twai │        bsp_espnow                 │
│  硬件层  │  硬件层  │         硬件层                     │
└──────────┴──────────┴───────────────────────────────────┘
     ↕            ↕              ↕
   红外链路     CAN总线        无线信道
```

## 3. 设备身份模型

### 统一设备 ID

与 IR 协议共用设备 ID 体系：

| ID | 用途 |
|----|------|
| 0x00 | 保留 |
| 0x01-0xFE | 设备地址 |
| 0xFF | 广播地址 |

每个设备有唯一 ID，配置在 `project_config.h` 中：

```c
#define WIRELESSID_DEVICE_ID  0x01  // 主机
#define WIRELESSID_DEVICE_ID  0x02  // 从机A
#define WIRELESSID_DEVICE_ID  0x03  // 从机B
```

### MAC 地址管理

ESP-NOW 使用 MAC 地址寻址。需要维护 **ID ↔ MAC 映射表**：

```c
typedef struct {
    uint8_t device_id;
    uint8_t mac[6];
    int8_t  rssi;
    uint32_t last_seen_tick;
} espnow_peer_t;
```

## 4. 帧格式设计

### 复用 IR 协议帧格式

为减少协议碎片化，ESP-NOW 帧**复用 IR 协议的帧格式**：

```
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│  帧头    │  控制字  │ 源设备ID │ 目标ID   │  数据    │  序列号  │  CRC16   │
│ 2 bytes  │ 1 byte   │ 1 byte   │ 1 byte   │ N bytes  │ 1 byte   │ 2 bytes  │
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
  0xAA55    见下表      0x01-     0x01-      payload    去重用      校验码
                       0xFE      0xFF=广播
```

### 控制字扩展

在 IR 协议基础上扩展 ESP-NOW 专用类型：

```
Bit 7-4: 帧类型
  0001: CMD       - 命令帧
  0002: RSP       - 应答帧
  0003: DATA      - 数据帧
  0004: BCAST     - 广播帧
  0005: DISCOVER  - 设备发现请求
  0006: ANNOUNCE  - 设备上线通告
  0007: HEARTBEAT - 心跳帧
  0008: BRIDGE    - 桥接转发帧

Bit 3: 请求位 (REQ)
Bit 2-0: 保留
```

### 新增控制字

| 控制字 | 二进制 | 含义 |
|--------|--------|------|
| 0x50 | 0101_0000 | DISCOVER：设备发现请求 |
| 0x60 | 0110_0000 | ANNOUNCE：设备上线通告 |
| 0x70 | 0111_0000 | HEARTBEAT：周期心跳 |
| 0x80 | 1000_0000 | BRIDGE：桥接转发帧 |

## 5. 设备发现与配对

### 发现流程

```
设备A (新上线)                     设备B/C/D (已在线)
    │                                  │
    │── BCAST DISCOVER ──────────────→│
    │   (目标=0xFF, 携带本机ID)        │
    │                                  │
    │←── UNICAST ANNOUNCE ────────────│
    │   (携带本机ID、MAC、设备名)       │
    │                                  │
    │  记录到 peer 表                   │
    │                                  │
```

### 心跳维持

```
设备A                                设备B
    │                                  │
    │── HEARTBEAT (每30s) ───────────→│
    │   (携带本机状态、负载信息)        │
    │                                  │
    │                              更新 last_seen
    │                              超时 90s 标记离线
```

### 对等体管理

```c
#define ESPNOW_MAX_PEERS    10
#define ESPNOW_PEER_TIMEOUT_MS  90000

typedef struct {
    uint8_t device_id;
    uint8_t mac[6];
    char    name[16];
    int8_t  rssi;
    uint32_t last_seen_ms;
    bool    online;
} espnow_peer_t;

typedef struct {
    espnow_peer_t peers[ESPNOW_MAX_PEERS];
    size_t count;
    SemaphoreHandle_t mutex;
} espnow_peer_table_t;
```

## 6. 通信模式

### 点对点（单播）

```c
// 发送数据到指定设备
esp_err_t espnow_send(uint8_t dst_id, const void *data, size_t len);

// 发送命令并等待应答
esp_err_t espnow_send_cmd_req(uint8_t dst_id, const void *cmd, size_t cmd_len,
                               void *rsp, size_t rsp_max, size_t *rsp_len,
                               uint32_t timeout_ms);
```

### 广播

```c
// 广播到所有设备
esp_err_t espnow_broadcast(const void *data, size_t len);
```

### 桥接转发

```c
// 将 IR 帧转发到 ESP-NOW
esp_err_t espnow_bridge_ir_to_wn(const uint8_t *ir_frame, size_t len);

// 将 ESP-NOW 帧转发到 IR
esp_err_t espnow_bridge_wn_to_ir(const uint8_t *wn_frame, size_t len);

// 将 CAN 帧转发到 ESP-NOW
esp_err_t espnow_bridge_can_to_wn(const bsp_twai_msg_t *can_msg);

// 将 ESP-NOW 帧转发到 CAN
esp_err_t espnow_bridge_wn_to_can(const uint8_t *wn_frame, size_t len);
```

## 7. 软件架构

### 模块划分

```
main/app/
├── app_espnow.c         # ESP-NOW 协议层
├── app_espnow.h         # 接口定义
├── app_espnow_bridge.c  # 桥接逻辑（IR/CAN ↔ ESP-NOW）
├── app_espnow_bridge.h  # 桥接接口
└── app_devtest.c        # 测试逻辑
```

### 核心结构体

```c
typedef struct {
    uint8_t device_id;
    char device_name[16];
    espnow_peer_table_t peers;
    SemaphoreHandle_t mutex;
    espnow_stats_t stats;
} espnow_ctx_t;
```

### API 设计

```c
// ===== 初始化 =====
esp_err_t app_espnow_init(espnow_ctx_t *ctx, uint8_t device_id, const char *name);
void app_espnow_deinit(espnow_ctx_t *ctx);

// ===== 对等体管理 =====
esp_err_t app_espnow_discover(espnow_ctx_t *ctx);
esp_err_t app_espnow_get_peers(espnow_ctx_t *ctx, espnow_peer_t *peers, size_t max, size_t *count);
bool app_espnow_is_peer_online(espnow_ctx_t *ctx, uint8_t device_id);

// ===== 发送接口 =====
esp_err_t app_espnow_send(espnow_ctx_t *ctx, uint8_t dst_id, const void *data, size_t len);
esp_err_t app_espnow_broadcast(espnow_ctx_t *ctx, const void *data, size_t len);
esp_err_t app_espnow_send_cmd_req(espnow_ctx_t *ctx, uint8_t dst_id,
                                   const void *cmd, size_t cmd_len,
                                   void *rsp, size_t rsp_max, size_t *rsp_len,
                                   uint32_t timeout_ms);

// ===== 接收回调 =====
typedef void (*espnow_rx_cb_t)(espnow_ctx_t *ctx, uint8_t src_id,
                                const uint8_t *data, size_t len);
void app_espnow_set_rx_cb(espnow_ctx_t *ctx, espnow_rx_cb_t cb);

// ===== 统计 =====
void app_espnow_get_stats(espnow_ctx_t *ctx, espnow_stats_t *stats);
```

### 桥接接口

```c
// ===== IR ↔ ESP-NOW 桥接 =====
typedef struct {
    espnow_ctx_t *espnow;
    ir_master_t *ir_master;  // 可选，用于 IR 主机转发
    uint8_t bridge_ir_slave_id;  // 要桥接的 IR 从机 ID
} espnow_ir_bridge_t;

esp_err_t espnow_ir_bridge_init(espnow_ir_bridge_t *bridge, espnow_ctx_t *espnow);
esp_err_t espnow_ir_bridge_start(espnow_ir_bridge_t *bridge);

// ===== CAN ↔ ESP-NOW 桥接 =====
typedef struct {
    espnow_ctx_t *espnow;
    uint16_t can_filter_id;     // 要转发的 CAN ID
    uint16_t can_filter_mask;   // 过滤掩码
} espnow_can_bridge_t;

esp_err_t espnow_can_bridge_init(espnow_can_bridge_t *bridge, espnow_ctx_t *espnow);
esp_err_t espnow_can_bridge_start(espnow_can_bridge_t *bridge);
```

## 8. 通信流程

### 设备发现

```
设备A (ID=0x01)                    设备B (ID=0x02)                 设备C (ID=0x03)
    │                                  │                              │
    │── BCAST DISCOVER ──────────────→│──────────────────────────────→│
    │                                  │                              │
    │←── UNICAST ANNOUNCE ────────────│                              │
    │   (ID=0x02, MAC=xx:xx, name=B)  │                              │
    │                                  │                              │
    │←── UNICAST ANNOUNCE ───────────────────────────────────────────│
    │   (ID=0x03, MAC=yy:yy, name=C)  │                              │
    │                                  │                              │
    │  peer表: [0x02, 0x03]            │  peer表: [0x01]              │
```

### 单播通信

```
设备A (ID=0x01)                    设备B (ID=0x02)
    │                                  │
    │── UNICAST CMD_REQ ─────────────→│
    │   (src=0x01, dst=0x02, data)     │
    │                                  │ 处理命令
    │←── UNICAST RSP ────────────────│
    │   (src=0x02, dst=0x01, data)     │
    │                                  │
```

### IR ↔ ESP-NOW 桥接

```
IR 从机 (ID=0x02)     桥接节点 (ID=0x01)     ESP-NOW 设备 (ID=0x03)
    │                      │                       │
    │── IR CMD_REQ ──────→│                       │
    │                      │── ESP-NOW CMD_REQ ──→│
    │                      │   (dst=0x03, data)    │
    │                      │                       │
    │                      │←── ESP-NOW RSP ──────│
    │                      │   (src=0x03, data)    │
    │←── IR RSP ─────────│                       │
    │                      │                       │
```

### CAN ↔ ESP-NOW 桥接

```
CAN 总线              桥接节点 (ID=0x01)     ESP-NOW 设备 (ID=0x03)
    │                      │                       │
    │── CAN 帧 ──────────→│                       │
    │   (ID=0x114, data)   │                       │
    │                      │── ESP-NOW DATA ─────→│
    │                      │   (dst=0x03, CAN帧)   │
    │                      │                       │
```

## 9. 信道与安全

### Wi-Fi 信道

- 默认信道：1（可通过配置修改）
- 所有设备必须在同一信道
- ESP-NOW 与 Wi-Fi STA 共存，但建议专用信道

### 安全机制（可选）

```c
// ESP-NOW 支持加密（PMK + LMK）
esp_now_set_pmk((uint8_t *)"pmk1234567890123");  // 16字节主密钥

// 添加对等体时设置本地密钥
esp_now_peer_info_t peer = {
    .lmk = {0x01, 0x02, ...},  // 16字节本地密钥
    .encrypt = true,
};
esp_now_add_peer(&peer);
```

> 初期可不启用加密，后期按需添加。

## 10. 资源开销

| 资源 | 估算 | 说明 |
|------|------|------|
| RAM | ~2KB | peer 表 + 缓冲区 |
| Flash | ~4KB | 代码 |
| Wi-Fi | 常开 | ESP-NOW 需要 Wi-Fi 驱动 |
| 信道 | 固定 | 与 IR/CAN 无冲突 |

## 11. 测试计划

### 阶段1：基础通信（当前）

**配置开关**（`project_config.h`）：

| 板子 | ESPNOW_BASE | ESPNOW_DEVICE | DEVICE_ID | 角色 |
|------|-------------|---------------|-----------|------|
| 板A | 1 | 0 | 0x01 | 基站 |
| 板B | 0 | 1 | 0x02 | 设备 |

**基站行为**：
- 每 2 秒广播一次数据（`payload: [0xAA, 0xBB, seq]`）
- OLED 显示：`BASE peers:n` / `BC:n OK:n` / `TX:n RX:n`
- 串口日志：`BCAST sent #n`

**设备行为**：
- 收到广播后通过 `device_data_handler` 打印日志
- OLED 显示：`DEV 0x02` / `CMD:n`
- 串口日志：`DATA from 0x01 [3]: AA BB xx`

**测试步骤**：
1. 板A 配置为基站，烧录
2. 板B 配置为设备，烧录
3. 两板上电，观察：
   - 板A 串口：`BCAST sent #1, #2, ...`
   - 板B 串口：`DATA from 0x01 [3]: AA BB xx`
   - 板B OLED：`CMD` 计数增长

### 阶段2：可靠性测试

1. 长时间广播稳定性（1000+ 帧）
2. 多设备同时接收（3-5 块板子）
3. 信号弱场景（远距离、遮挡）

### 阶段3：桥接测试

1. IR → ESP-NOW → IR 链路
2. CAN → ESP-NOW → CAN 链路
3. 混合桥接（IR + CAN 同时转发）

## 12. 与 IR 协议的差异

| 特性 | IR 协议 | ESP-NOW 协议 |
|------|---------|-------------|
| 传输介质 | 红外光 | 2.4GHz 无线电 |
| 最大 payload | 248 字节 | 250 字节 |
| 传输距离 | ~10m | ~200m |
| 延迟 | ~100ms | <5ms |
| 可靠性 | ~65% | >99% |
| 双向性 | 半双工（回声问题） | 全双工 |
| 寻址方式 | 设备ID | MAC + 设备ID |
| 帧格式 | 自定义 | 复用 IR 帧格式 |

## 13. 实施步骤

1. [x] 编写设计文档（本文档）
2. [x] 实现 `app_espnow.c/h`（基站端：广播、单播、peer 管理）
3. [x] 实现 `app_espnow_device.c/h`（设备端：接收、应答、announce）
4. [x] 基础广播通信测试（基站 10Hz 广播，设备接收几乎无丢包）
5. [ ] 设备发现与心跳
6. [ ] 单播 CMD_REQ/RSP 测试
7. [ ] 实现桥接模块（IR/CAN ↔ ESP-NOW）
8. [ ] 多设备组网测试
