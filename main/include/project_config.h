#pragma once

#define WIRELESSID_APP_NAME "WirelessID"
#define WIRELESSID_LOG_TAG "WirelessID"
#define WIRELESSID_STARTUP_BANNER "ESP32-C3 application started"

#define WIRELESSID_DEFAULT_UART0_BAUD_RATE 115200
#define WIRELESSID_DEFAULT_IR_UART_BAUD_RATE 4800
#define WIRELESSID_DEFAULT_I2C_FREQ_HZ 400000
#define WIRELESSID_DEFAULT_DISPLAY_I2C_ADDR 0x3C
#define WIRELESSID_DEFAULT_IR_CARRIER_HZ 38000
#define WIRELESSID_DEFAULT_IR_CARRIER_DUTY_PERCENT 33
#define WIRELESSID_DEFAULT_TWAI_BAUD_RATE 1000000

// ===== Debug/Test feature switches =====
// UART0 loopback test: RX callback echoes data back
#define WIRELESSID_UART0_LOOPBACK_TEST_ENABLE 0
// UART0 alive probe: periodically send "U0_TX_ALIVE"
#define WIRELESSID_UART0_ALIVE_PROBE_ENABLE 0
// TWAI periodic test TX in app_main loop
#define WIRELESSID_TWAI_TEST_TX_ENABLE 0
// IR TX test: periodically send test frame via infrared
#define WIRELESSID_IR_TEST_TX_ENABLE 0
// IR RX test: receive IR data and print to log
#define WIRELESSID_IR_TEST_RX_ENABLE 0
// ESP-NOW base test: broadcast data, devices echo back
#define WIRELESSID_ESPNOW_BASE_ENABLE 0
// ESP-NOW device test: receive broadcast and echo back
#define WIRELESSID_ESPNOW_DEVICE_ENABLE 0
// CMD framework test: test command dispatch and handlers
#define WIRELESSID_CMD_TEST_ENABLE 0

// Device ID allocation:
//   0x00      : reserved
//   0xA0-0xBF : base stations
//   0xC0-0xCF : reserved
//   0xD0-0xFE : devices
//   0xFF      : broadcast
#define WIRELESSID_DEVICE_ID 0xA0

//

// Business logic test: IR CAN passthrough
#define WIRELESSID_BIZ_TEST_ENABLE 1
// Device role: 0=BASE, 1=DEVICE
#define WIRELESSID_BIZ_ROLE 1
// Device ID for business logic
#define WIRELESSID_BIZ_ID 0xD0

#define WIRELESSID_STATUS_TASK_STACK_SIZE 4096
#define WIRELESSID_STATUS_TASK_PRIORITY 5
