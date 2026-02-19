#include "rgb/rgb.h"

#include "esp_check.h"
#include "led_strip.h"

#define RGB_GPIO 38

static led_strip_handle_t s_strip = NULL;

esp_err_t rgb_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_GPIO,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip), "rgb", "led_strip init failed");
    led_strip_clear(s_strip);
    return ESP_OK;
}

void rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) {
        return;
    }
    /* Swap R/G for this board's LED ordering */
    led_strip_set_pixel(s_strip, 0, g, r, b);
    led_strip_refresh(s_strip);
}
