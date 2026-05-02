#pragma once

#include <stdint.h>
#include "bsp_common.h"

#define DISP_WIDTH  128
#define DISP_HEIGHT 64
#define DISP_PAGES  (DISP_HEIGHT / 8)

typedef enum {
    DISP_PEN_CLEAR = 0,
    DISP_PEN_WRITE = 1,
    DISP_PEN_INVERT = 2,
} disp_pen_t;

typedef enum {
    DISP_ROT_0 = 0,
    DISP_ROT_90,
    DISP_ROT_180,
    DISP_ROT_270,
} disp_rotation_t;

esp_err_t bsp_display_init(void);
void bsp_display_set_rotation(disp_rotation_t rot);
void bsp_display_clear(void);
void bsp_display_refresh(void);

void bsp_display_draw_point(uint8_t x, uint8_t y, disp_pen_t pen);
void bsp_display_draw_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, disp_pen_t pen);

void bsp_display_show_char(uint8_t row, uint8_t col, char ch);
void bsp_display_show_string(uint8_t row, uint8_t col, const char *str);
void bsp_display_printf(uint8_t row, uint8_t col, const char *fmt, ...);
