#include "app_status.h"

static app_status_t s_status;

void app_status_init(void)
{
    s_status = (app_status_t) {0};
}

app_status_t app_status_get(void)
{
    return s_status;
}

void app_status_set_twai_ready(bool ready)
{
    s_status.twai_ready = ready;
}

void app_status_set_espnow_ready(bool ready)
{
    s_status.espnow_ready = ready;
}

void app_status_set_display_ready(bool ready)
{
    s_status.display_ready = ready;
}

void app_status_set_ir_ready(bool ready)
{
    s_status.ir_ready = ready;
}

void app_status_set_key_ready(bool ready)
{
    s_status.key_ready = ready;
}

void app_status_set_uart0_ready(bool ready)
{
    s_status.uart0_ready = ready;
}

void app_status_set_ws2812_ready(bool ready)
{
    s_status.ws2812_ready = ready;
}
