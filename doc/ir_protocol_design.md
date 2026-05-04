# 红外双向通信协议设计

> 日期：2026-05-04
> 状态：设计阶段
> 更新：采用主从架构，主机控制通信，从机只响应

## 1. 硬件能力

| 特性 | 状态 | 说明 |
|------|------|------|
| UART1 全双工 | ✅ | 独立 TX(IO1)/RX(IO0) 引脚 |
| 发射链路 | ✅ | IO1 → 反相器 → 与门 → IR LED |
| 接收链路 | ✅ | IRM-H638T → IO0 |
| 载波 | ✅ | 38kHz 常驻，与门自动调制 |

## 2. 设计思路

### 核心思想：主从架构 + 设备识别 + 软件过滤

```
主机控制通信，从机只响应
发送时：帧中携带源设备ID
接收时：过滤掉本机发送的消息（src_id == 本机ID）
```

### 串扰处理

经测试：
- RX TX 接近时串扰不严重
- 距离较远时，环境反射可能导致本机收到自己发送的帧
- **必须过滤自发送**

### 冲突处理

采用请求-应答模式：
- 主机发送命令，从机响应
- 避免多设备同时发送
- 配合 CRC 校验和重发机制

## 3. 帧格式设计

### 主从帧格式

```
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│  帧头    │  控制字  │ 主机ID   │ 从机ID   │  数据    │  序列号  │  CRC16   │
│ 2 bytes  │ 1 byte   │ 1 byte   │ 1 byte   │ N bytes  │ 1 byte   │ 2 bytes  │
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
  0xAA55    见下表      0x01      0x01-0xFE   payload    去重用      校验码
```

### 控制字格式

```
Bit 7-4: 帧类型
  0001: CMD   - 命令帧（主机→从机）
  0002: RSP   - 应答帧（从机→主机）
  0003: DATA  - 数据帧（双向）
  0004: BCAST - 广播帧（主机→所有从机）

Bit 3: 请求位 (REQ)
  0: 不需要对方返回数据
  1: 需要对方返回数据

Bit 2-0: 保留
```

### 控制字示例

| 控制字 | 二进制 | 含义 |
|--------|--------|------|
| 0x10 | 0001_0000 | CMD + REQ=0：主机发命令，不需要从机数据 |
| 0x18 | 0001_1000 | CMD + REQ=1：主机发命令，需要从机返回数据 |
| 0x20 | 0010_0000 | RSP + REQ=0：从机应答（响应主机请求） |
| 0x30 | 0011_0000 | DATA + REQ=0：普通数据帧 |
| 0x40 | 0100_0000 | BCAST + REQ=0：广播帧 |

### 设备ID分配

| ID | 用途 |
|----|------|
| 0x00 | 保留 |
| 0x01 | 主机ID（固定） |
| 0x02-0xFE | 从机ID |
| 0xFF | 广播地址 |

## 4. 通信流程

### 主从通信模式

```
场景1：主机发命令，不需要从机数据
┌────────┐                    ┌────────┐
│  主机  │                    │  从机  │
└───┬────┘                    └────┬───┘
    │                              │
    │──── CMD(req=0) ────────────→│
    │         主机ID, 从机ID       │ 从机执行命令
    │                              │
    │         （无需应答）          │
    └──────────────────────────────┘

场景2：主机发命令，需要从机数据
┌────────┐                    ┌────────┐
│  主机  │                    │  从机  │
└───┬────┘                    └────┬───┘
    │                              │
    │──── CMD(req=1) ────────────→│
    │         主机ID, 从机ID       │ 从机处理请求
    │                              │
    │←──── RSP(req=0) ───────────│
    │         从机ID, 主机ID       │ 主机接收数据
    │                              │
    └──────────────────────────────┘

场景3：主机广播
┌────────┐    ┌────────┐    ┌────────┐
│  主机  │    │  从机A │    │  从机B │
└───┬────┘    └────┬───┘    └────┬───┘
    │              │              │
    │── BCAST ───→│              │
    │──────────────────→│         │
    │              │              │
    │      （无需应答）            │
    └──────────────────────────────┘
```

### 应答超时和重发

```
主机                          从机
  │                              │
  │──── CMD(req=1) ────────────→│
  │                              │
  │      (等待 50ms)             │
  │                              │
  │  超时？重发（最多3次）        │
  │──── CMD(req=1) ────────────→│
  │                              │
  │←──── RSP(req=0) ───────────│
  │                              │
```
设备A (ID=0x01)                     设备B (ID=0x02)
      │                                    │
      │──── REQ, src=01, dst=02 ─────────→│
      │                                    │ 处理请求
      │←──── ACK, src=02, dst=01 ─────────│
      │                                    │
```

### 广播

```
设备A (ID=0x01)                     设备B (ID=0x02)
      │                                    │
      │──── BCAST, src=01, dst=00 ───────→│
      │                                    │ 处理数据
      │      （无需应答）                   │
```

### 伪全双工

```
设备A (ID=0x01)                     设备B (ID=0x02)
      │                                    │
      │──── REQ, src=01, dst=02 ─────────→│
      │                                    │
      │←──── DATA, src=02, dst=01 ────────│  （同时发送）
      │                                    │
   过滤掉src=01的帧                     过滤掉src=02的帧
```

## 5. 软件架构

### 模块划分

```
main/app/
├── app_ir.c           # 现有：底层收发
├── app_ir_master.c    # 新增：主机协议
├── app_ir_master.h    # 新增：主机接口
├── app_ir_slave.c     # 新增：从机协议
├── app_ir_slave.h     # 新增：从机接口
└── app_devtest.c      # 修改：添加主从测试
```

### 主机 API (app_ir_master.h)

```c
// 初始化主机
esp_err_t app_ir_master_init(uint8_t master_id);

// 发送命令（不需要从机数据）
esp_err_t app_ir_master_send_cmd(uint8_t slave_id, const void *data, size_t len);

// 发送命令并等待从机数据
esp_err_t app_ir_master_send_cmd_req(uint8_t slave_id, const void *cmd, size_t cmd_len,
                                      void *rsp, size_t rsp_max, size_t *rsp_len,
                                      uint32_t timeout_ms);

// 广播命令
esp_err_t app_ir_master_broadcast(const void *data, size_t len);

// 获取统计
void app_ir_master_get_stats(app_ir_master_stats_t *stats);
```

### 从机 API (app_ir_slave.h)

```c
// 初始化从机
esp_err_t app_ir_slave_init(uint8_t slave_id);

// 设置命令处理回调
typedef void (*app_ir_slave_cmd_cb_t)(uint8_t master_id, const uint8_t *cmd, size_t cmd_len,
                                      uint8_t *rsp, size_t *rsp_len);
void app_ir_slave_set_cmd_cb(app_ir_slave_cmd_cb_t cb);

// 设置数据回调（无应答）
typedef void (*app_ir_slave_data_cb_t)(uint8_t src_id, const uint8_t *data, size_t len);
void app_ir_slave_set_data_cb(app_ir_slave_data_cb_t cb);

// 获取统计
void app_ir_slave_get_stats(app_ir_slave_stats_t *stats);
```
main/app/
├── app_ir.c           # 现有：底层收发
├── app_ir_proto.c     # 新增：协议层
├── app_ir_proto.h     # 新增：协议接口
└── app_devtest.c      # 修改：添加双向测试
```

### API 设计

```c
// ===== 初始化 =====
esp_err_t app_ir_proto_init(uint8_t device_id);

// ===== 发送接口 =====

// 发送请求（等待应答）
esp_err_t app_ir_proto_send_req(uint8_t dst_id, const void *data, size_t len, uint32_t timeout_ms);

// 发送数据（无需应答）
esp_err_t app_ir_proto_send_data(uint8_t dst_id, const void *data, size_t len);

// 发送广播（无需应答）
esp_err_t app_ir_proto_send_bcast(const void *data, size_t len);

// 发送应答
esp_err_t app_ir_proto_send_ack(uint8_t dst_id, uint8_t seq);

// ===== 接收接口 =====

// 设置接收回调
typedef void (*app_ir_proto_rx_cb_t)(uint8_t src_id, const uint8_t *data, size_t len);
void app_ir_proto_set_rx_cb(app_ir_proto_rx_cb_t cb);

// ===== 统计 =====
typedef struct {
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t rx_filtered;    // 被过滤的本机帧
    uint32_t rx_crc_errors;
    uint32_t tx_retries;
    uint32_t tx_timeout;
} app_ir_proto_stats_t;

void app_ir_proto_get_stats(app_ir_proto_stats_t *stats);
```

### 接收处理流程

```c
static void ir_rx_handler(const uint8_t *frame, size_t len) {
    // 1. 解析帧
    ir_proto_frame_t f;
    if (parse_frame(frame, len, &f) != ESP_OK) {
        return;
    }

    // 2. 过滤本机发送的帧（关键！）
    if (f.src_id == s_my_device_id) {
        s_stats.rx_filtered++;
        return;  // 丢弃
    }

    // 3. 检查目标地址
    if (f.dst_id != s_my_device_id && f.dst_id != 0x00) {
        return;  // 不是发给本机的
    }

    // 4. 处理帧类型
    switch (f.type) {
    case IR_FRAME_REQ:
        // 收到请求，回调上层
        if (s_rx_cb) {
            s_rx_cb(f.src_id, f.data, f.data_len);
        }
        // 自动发送应答
        app_ir_proto_send_ack(f.src_id, f.seq);
        break;

    case IR_FRAME_ACK:
        // 收到应答，唤醒等待任务
        xTaskNotify(s_wait_task, f.seq, eSetValueWithOverwrite);
        break;

    case IR_FRAME_DATA:
    case IR_FRAME_BCAST:
        // 收到数据/广播，回调上层
        if (s_rx_cb) {
            s_rx_cb(f.src_id, f.data, f.data_len);
        }
        break;
    }
}
```

## 6. 测试计划

### 阶段1：串扰测试

1. 单板测试：发送数据，观察本机是否收到
2. 记录串扰强度
3. 确定是否需要硬件改进

### 阶段2：双板测试

1. 配置不同设备ID
2. 测试单播请求-应答
3. 测试广播
4. 测试双向并发

### 阶段3：稳定性测试

1. 长时间双向通信
2. 统计丢包率、重发率
3. 优化参数

## 7. 与现有代码的兼容性

### 保留现有接口

```c
// 现有接口继续保留，用于单向场景
esp_err_t app_ir_send_can(const bsp_twai_msg_t *msg);
esp_err_t app_ir_parse_can(const uint8_t *payload, size_t len, bsp_twai_msg_t *msg);
```

### 新增双向接口

```c
// 新增双向通信接口
esp_err_t app_ir_proto_send_req(uint8_t dst_id, const void *data, size_t len, uint32_t timeout_ms);
esp_err_t app_ir_proto_send_data(uint8_t dst_id, const void *data, size_t len);
```

## 8. 下一步行动

1. [x] 记录方案到 doc 目录（本文档）
2. [x] 串扰测试完成（软件过滤必须）
3. [x] 实现主机协议 (app_ir_master.c)
4. [x] 实现从机协议 (app_ir_slave.c)
5. [x] 双板主从通信测试（CMD_REQ/RSP 双向，~65% 成功率）
6. [ ] 稳定性长时间测试
7. [ ] 减小帧开销提高有效吞吐率
