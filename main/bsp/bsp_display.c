#include "bsp_display.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bsp_i2c.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "oled_font.h"
#include "project_config.h"

#define OLED_ADDR WIRELESSID_DEFAULT_DISPLAY_I2C_ADDR
#define OLED_CMD  0x00
#define OLED_DATA 0x40
#define CHAR_W    6
#define CHAR_H    12
#define COLS_PER_ROW_0  21
#define COLS_PER_ROW_90 10
#define ROWS_0          5
#define ROWS_90         10

static const char *TAG = "bsp_display";

static uint8_t disp_gram[DISP_PAGES][DISP_WIDTH];
static i2c_master_dev_handle_t s_dev;
static disp_rotation_t s_rot;

static void apply_rot(uint8_t x, uint8_t y, uint8_t *rx, uint8_t *ry) {
  switch (s_rot) {
  case DISP_ROT_0:   *rx = x;      *ry = y;      return;
  case DISP_ROT_90:  *rx = 127 - y; *ry = x;      return;
  case DISP_ROT_180: *rx = 127 - x; *ry = 63 - y; return;
  case DISP_ROT_270: *rx = y;      *ry = 63 - x; return;
  }
}

static esp_err_t oled_write_cmd(uint8_t cmd) {
  static uint8_t buf[2];
  buf[0] = OLED_CMD; buf[1] = cmd;
  return i2c_master_transmit(s_dev, buf, sizeof(buf), pdMS_TO_TICKS(50));
}

static esp_err_t oled_write_data(const uint8_t *data, size_t len) {
  if (len > 128) return ESP_ERR_INVALID_ARG;
  static uint8_t buf[129];
  buf[0] = OLED_DATA;
  memcpy(buf + 1, data, len);
  return i2c_master_transmit(s_dev, buf, len + 1, pdMS_TO_TICKS(100));
}

static void oled_set_pos(uint8_t x, uint8_t page) {
  oled_write_cmd(0xb0 | page);
  oled_write_cmd(((x & 0xf0) >> 4) | 0x10);
  oled_write_cmd(x & 0x0f);
}

esp_err_t bsp_display_init(void)
{
  i2c_master_bus_handle_t bus = bsp_i2c_get_bus_handle();
  if (!bus) { ESP_LOGE(TAG, "i2c bus not ready"); return ESP_ERR_INVALID_STATE; }

  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = OLED_ADDR,
    .scl_speed_hz    = WIRELESSID_DEFAULT_I2C_FREQ_HZ,
  };

  esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
  if (ret != ESP_OK) { ESP_LOGE(TAG, "add device err %d", ret); return ret; }

  oled_write_cmd(0xae);
  oled_write_cmd(0x20); oled_write_cmd(0x10);
  oled_write_cmd(0xb0);
  oled_write_cmd(0xc8);
  oled_write_cmd(0x00); oled_write_cmd(0x10);
  oled_write_cmd(0x40);
  oled_write_cmd(0x81); oled_write_cmd(0xff);
  oled_write_cmd(0xa1);
  oled_write_cmd(0xa6);
  oled_write_cmd(0xa8); oled_write_cmd(0x3f);
  oled_write_cmd(0xa4);
  oled_write_cmd(0xd3); oled_write_cmd(0x00);
  oled_write_cmd(0xd5); oled_write_cmd(0xf0);
  oled_write_cmd(0xd9); oled_write_cmd(0x22);
  oled_write_cmd(0xda); oled_write_cmd(0x12);
  oled_write_cmd(0xdb); oled_write_cmd(0x20);
  oled_write_cmd(0x8d); oled_write_cmd(0x14);
  oled_write_cmd(0xaf);
  vTaskDelay(pdMS_TO_TICKS(5));

  memset(disp_gram, 0, sizeof(disp_gram));
  bsp_display_refresh();
  ESP_LOGI(TAG, "oled ok");
  return ESP_OK;
}

void bsp_display_set_rotation(disp_rotation_t rot) { s_rot = rot; }
void bsp_display_clear(void) { memset(disp_gram, 0, sizeof(disp_gram)); }

void bsp_display_draw_point(uint8_t x, uint8_t y, disp_pen_t pen) {
  if (s_rot == DISP_ROT_90 || s_rot == DISP_ROT_270) {
    if (x >= 64 || y >= 128) return;
  } else {
    if (x >= DISP_WIDTH || y >= DISP_HEIGHT) return;
  }
  uint8_t rx, ry; apply_rot(x, y, &rx, &ry);
  uint8_t page = ry >> 3, bit = 1 << (ry & 0x07);
  if      (pen == DISP_PEN_WRITE)  disp_gram[page][rx] |= bit;
  else if (pen == DISP_PEN_INVERT) disp_gram[page][rx] ^= bit;
  else                             disp_gram[page][rx] &= ~bit;
}

void bsp_display_draw_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, disp_pen_t pen) {
  if (y1 == y2) {
    uint8_t s = x1 < x2 ? x1 : x2, e = x1 < x2 ? x2 : x1;
    for (uint8_t x = s; x <= e; x++) bsp_display_draw_point(x, y1, pen);
    return;
  }
  uint8_t s = x1 < x2 ? x1 : x2, e = x1 < x2 ? x2 : x1;
  for (uint8_t x = s; x <= e; x++)
    bsp_display_draw_point(x, y1 + (uint8_t)((int)(y2 - y1) * (x - x1) / (int)(x2 - x1)), pen);
}

void bsp_display_show_char(uint8_t row, uint8_t col, char ch) {
  if (ch < ' ' || ch > '~') return;
  uint8_t mr = (s_rot == DISP_ROT_90 || s_rot == DISP_ROT_270) ? ROWS_90 : ROWS_0;
  uint8_t mc = (s_rot == DISP_ROT_90 || s_rot == DISP_ROT_270) ? COLS_PER_ROW_90 : COLS_PER_ROW_0;
  if (row > mr || col > mc) return;
  uint8_t x0 = col * CHAR_W, y0 = row * CHAR_H, x = x0, y = y0, idx = ch - ' ';
  for (uint8_t t = 0; t < CHAR_H; t++) {
    uint8_t data = asc2_1206[idx][t];
    for (uint8_t bit = 0; bit < 8; bit++) {
      bsp_display_draw_point(x, y, data & 0x80 ? DISP_PEN_WRITE : DISP_PEN_CLEAR);
      data <<= 1; y++;
      if ((y - y0) == CHAR_H) { y = y0; x++; break; }
    }
  }
}

void bsp_display_show_string(uint8_t row, uint8_t col, const char *str) {
  uint8_t mc = (s_rot == DISP_ROT_90 || s_rot == DISP_ROT_270) ? COLS_PER_ROW_90 : COLS_PER_ROW_0;
  uint8_t mr = (s_rot == DISP_ROT_90 || s_rot == DISP_ROT_270) ? ROWS_90 : ROWS_0;
  while (*str) { bsp_display_show_char(row, col, *str); col++;
    if (col > mc) { col = 0; row++; } if (row > mr) break; str++; }
}

void bsp_display_printf(uint8_t row, uint8_t col, const char *fmt, ...) {
  char buf[128]; va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  bsp_display_show_string(row, col, buf);
}

void bsp_display_refresh(void) {
  for (uint8_t page = 0; page < DISP_PAGES; page++) {
    oled_set_pos(0, page);
    oled_write_data(disp_gram[page], DISP_WIDTH);
  }
}
