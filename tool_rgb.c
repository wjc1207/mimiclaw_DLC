#include "tool_rgb.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip.h"
#include "cJSON.h"

static const char *TAG = "tool_rgb";

/* ===== CONFIG ===== */
#define RGB_GPIO        48
#define RGB_LED_COUNT   1
#define RMT_RESOLUTION  10000000  // 10 MHz

static led_strip_handle_t s_strip = NULL;
static bool s_initialized = false;

/* ===== Initialize WS2812 ===== */
static esp_err_t tool_rgb_init(void)
{
    if (s_initialized) return ESP_OK;

    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_GPIO,
        .max_leds = RGB_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION,
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config,
                                             &rmt_config,
                                             &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init LED strip: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "RGB tool initialized on GPIO %d", RGB_GPIO);
    return ESP_OK;
}

/* ===== Tool Execute ===== */
esp_err_t tool_rgb_execute(const char *input_json,
                           char *output,
                           size_t output_size)
{
    if (!input_json) {
        snprintf(output, output_size, "Error: no input JSON");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = tool_rgb_init();
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: RGB init failed");
        return err;
    }

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }

    int r = 0, g = 0, b = 0;

    cJSON *jr = cJSON_GetObjectItem(root, "r");
    cJSON *jg = cJSON_GetObjectItem(root, "g");
    cJSON *jb = cJSON_GetObjectItem(root, "b");

    if (jr && cJSON_IsNumber(jr)) r = jr->valueint;
    if (jg && cJSON_IsNumber(jg)) g = jg->valueint;
    if (jb && cJSON_IsNumber(jb)) b = jb->valueint;

    if (r < 0) {
        r = 0;
    }
    if (r > 255) {
        r = 255;
    }

    if (g < 0) {
        g = 0;
    }
    if (g > 255) {
        g = 255;
    }

    if (b < 0) {
        b = 0;
    }
    if (b > 255) {
        b = 255;
    }

    cJSON_Delete(root);

    /* Set pixel */
    err = led_strip_set_pixel(s_strip, 0, r, g, b);
    if (err == ESP_OK) {
        err = led_strip_refresh(s_strip);
    }

    if (err == ESP_OK) {
        snprintf(output, output_size,
                 "RGB set to R:%d G:%d B:%d", r, g, b);
        ESP_LOGI(TAG, "%s", output);
    } else {
        snprintf(output, output_size,
                 "Error: failed to set RGB (%s)",
                 esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", output);
    }

    return err;
}
