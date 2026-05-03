#include "bsp_ws2812.h"

#include <math.h>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip_encoder.h"
#include "pinmux_ws2812.h"

#define WS2812_RMT_RESOLUTION_HZ 10000000
#define WS2812_TASK_STACK 2048
#define WS2812_TASK_PRIO 2
#define WS2812_TICK_MS 20

static const char *TAG = "bsp_ws2812";

static bool s_initialized;
static rmt_channel_handle_t s_rmt;
static rmt_encoder_handle_t s_encoder;
static SemaphoreHandle_t s_mutex;

static bsp_ws2812_effect_t s_effect;
static bsp_rgb_t s_color;
static uint8_t s_max_brightness;
static uint32_t s_period_ms;
static float s_phase;

static void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g,
                       uint8_t *b) {
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float rr, gg, bb;

  if (h < 60) {
    rr = c;
    gg = x;
    bb = 0;
  } else if (h < 120) {
    rr = x;
    gg = c;
    bb = 0;
  } else if (h < 180) {
    rr = 0;
    gg = c;
    bb = x;
  } else if (h < 240) {
    rr = 0;
    gg = x;
    bb = c;
  } else if (h < 300) {
    rr = x;
    gg = 0;
    bb = c;
  } else {
    rr = c;
    gg = 0;
    bb = x;
  }

  *r = (uint8_t)((rr + m) * 255.0f);
  *g = (uint8_t)((gg + m) * 255.0f);
  *b = (uint8_t)((bb + m) * 255.0f);
}

static void ws2812_output(bsp_rgb_t c) {
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }

  uint8_t data[3] = {c.g, c.r, c.b};
  rmt_transmit_config_t cfg = {.loop_count = 0};

  esp_err_t err = rmt_transmit(s_rmt, s_encoder, data, sizeof(data), &cfg);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "transmit err %d", err);
    xSemaphoreGive(s_mutex);
    return;
  }

  err = rmt_tx_wait_all_done(s_rmt, portMAX_DELAY);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "wait_all_done err %d", err);
  }

  xSemaphoreGive(s_mutex);
}

static void ws2812_task(void *arg) {
  (void)arg;

  while (1) {
    bsp_rgb_t out = {0};

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    switch (s_effect) {
    case BSP_WS2812_EFFECT_NONE:
      xSemaphoreGive(s_mutex);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;

    case BSP_WS2812_EFFECT_SOLID:
      out = s_color;
      xSemaphoreGive(s_mutex);
      ws2812_output(out);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;

    case BSP_WS2812_EFFECT_BREATH:
      s_phase += 2.0f * (float)M_PI * WS2812_TICK_MS / s_period_ms;
      if (s_phase > 2.0f * (float)M_PI) {
        s_phase -= 2.0f * (float)M_PI;
      }
      {
        float breath = (sinf(s_phase) + 1.0f) * 0.5f;
        uint8_t br = (uint8_t)(breath * s_max_brightness);
        uint16_t rr = (uint16_t)s_color.r * br / 255;
        uint16_t gg = (uint16_t)s_color.g * br / 255;
        uint16_t bb = (uint16_t)s_color.b * br / 255;
        out.r = (uint8_t)(rr > 255 ? 255 : rr);
        out.g = (uint8_t)(gg > 255 ? 255 : gg);
        out.b = (uint8_t)(bb > 255 ? 255 : bb);
      }
      xSemaphoreGive(s_mutex);
      ws2812_output(out);
      vTaskDelay(pdMS_TO_TICKS(WS2812_TICK_MS));
      continue;

    case BSP_WS2812_EFFECT_RAINBOW:
      s_phase += 2.0f * (float)M_PI * WS2812_TICK_MS / s_period_ms;
      if (s_phase > 2.0f * (float)M_PI) {
        s_phase -= 2.0f * (float)M_PI;
      }
      {
        float breath = (sinf(s_phase) + 1.0f) * 0.5f;
        float v = breath * s_max_brightness / 255.0f;
        float hue = fmodf(s_phase * 180.0f / (float)M_PI, 360.0f);
        hsv_to_rgb(hue, 1.0f, v, &out.r, &out.g, &out.b);
      }
      xSemaphoreGive(s_mutex);
      ws2812_output(out);
      vTaskDelay(pdMS_TO_TICKS(WS2812_TICK_MS));
      continue;
    }

    xSemaphoreGive(s_mutex);
    vTaskDelay(pdMS_TO_TICKS(WS2812_TICK_MS));
  }
}

esp_err_t bsp_ws2812_init(void) {
  const pinmux_ws2812_config_t *cfg = pinmux_ws2812_get_config();

  if (cfg->data_gpio == GPIO_NUM_NC) {
    ESP_LOGW(TAG, "pin is NC, skipping");
    return ESP_ERR_NOT_SUPPORTED;
  }

  ESP_LOGI(TAG, "init gpio=%d leds=%d", cfg->data_gpio, cfg->led_count);

  rmt_tx_channel_config_t tx_cfg = {
      .gpio_num = cfg->data_gpio,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = WS2812_RMT_RESOLUTION_HZ,
      .mem_block_symbols = 64,
      .trans_queue_depth = 2,
      .intr_priority = 0,
  };
  ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s_rmt), TAG, "tx chan");

  led_strip_encoder_config_t enc_cfg = {
      .resolution = WS2812_RMT_RESOLUTION_HZ,
  };
  ESP_RETURN_ON_ERROR(rmt_new_led_strip_encoder(&enc_cfg, &s_encoder), TAG,
                      "encoder");

  ESP_RETURN_ON_ERROR(rmt_enable(s_rmt), TAG, "enable");

  s_mutex = xSemaphoreCreateMutex();
  if (!s_mutex) {
    return ESP_ERR_NO_MEM;
  }

  s_initialized = true;

  xTaskCreate(ws2812_task, "ws2812", WS2812_TASK_STACK, NULL, WS2812_TASK_PRIO,
              NULL);

  return ESP_OK;
}

esp_err_t bsp_ws2812_play(bsp_ws2812_effect_t effect, uint8_t r, uint8_t g,
                          uint8_t b, uint8_t max_brightness,
                          uint32_t period_ms) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  s_effect = effect;
  s_color.r = r;
  s_color.g = g;
  s_color.b = b;
  s_max_brightness = max_brightness;
  s_period_ms = period_ms ? period_ms : 1000;
  s_phase = 0;

  xSemaphoreGive(s_mutex);

  return ESP_OK;
}
