#include "tool_device_control.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "tool_device";

#define DEVICE_LED_GPIO      48
#define DEVICE_LED_DEFAULT_N 1
#define RMT_RESOLUTION_HZ    (10 * 1000 * 1000)

typedef struct {
    led_strip_handle_t strip;
    int pixels;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t brightness;
    bool initialized;
} device_led_state_t;

static device_led_state_t s_led = {
    .strip = NULL,
    .pixels = DEVICE_LED_DEFAULT_N,
    .r = 0,
    .g = 0,
    .b = 0,
    .brightness = 255,
    .initialized = false,
};

static SemaphoreHandle_t s_led_mutex = NULL;

static uint8_t clamp_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static bool parse_hex_color(const char *hex, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!hex || !r || !g || !b) {
        return false;
    }

    while (*hex == '#') {
        hex++;
    }

    if (strlen(hex) != 6) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        if (!isxdigit((unsigned char)hex[i])) {
            return false;
        }
    }

    char tmp[3] = {0};
    tmp[0] = hex[0]; tmp[1] = hex[1];
    *r = (uint8_t)strtol(tmp, NULL, 16);
    tmp[0] = hex[2]; tmp[1] = hex[3];
    *g = (uint8_t)strtol(tmp, NULL, 16);
    tmp[0] = hex[4]; tmp[1] = hex[5];
    *b = (uint8_t)strtol(tmp, NULL, 16);

    return true;
}

static esp_err_t ensure_led_ready(int pixels)
{
    if (pixels <= 0) {
        pixels = DEVICE_LED_DEFAULT_N;
    }

    if (s_led.strip && s_led.pixels == pixels) {
        return ESP_OK;
    }

    if (s_led.strip) {
        led_strip_del(s_led.strip);
        s_led.strip = NULL;
    }

    led_strip_config_t cfg = {
        .strip_gpio_num = DEVICE_LED_GPIO,
        .max_leds = (uint32_t)pixels,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&cfg, &rmt_cfg, &s_led.strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        return err;
    }

    s_led.pixels = pixels;
    s_led.initialized = true;
    ESP_LOGI(TAG, "WS2812 ready on GPIO%d, pixels=%d", DEVICE_LED_GPIO, pixels);
    return ESP_OK;
}

static esp_err_t apply_led_color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
    if (!s_led.strip) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t rr = ((uint32_t)r * brightness) / 255U;
    uint32_t gg = ((uint32_t)g * brightness) / 255U;
    uint32_t bb = ((uint32_t)b * brightness) / 255U;

    for (int i = 0; i < s_led.pixels; i++) {
        led_strip_set_pixel(s_led.strip, (uint32_t)i, rr, gg, bb);
    }

    esp_err_t err = led_strip_refresh(s_led.strip);
    if (err != ESP_OK) {
        return err;
    }

    s_led.r = r;
    s_led.g = g;
    s_led.b = b;
    s_led.brightness = brightness;
    return ESP_OK;
}

esp_err_t tool_device_control_execute(const char *input_json, char *output, size_t output_size)
{
    if (!input_json || !output || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_led_mutex) {
        s_led_mutex = xSemaphoreCreateMutex();
        if (!s_led_mutex) {
            snprintf(output, output_size, "Error: cannot create LED mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        snprintf(output, output_size, "Error: LED busy");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    cJSON *input = cJSON_Parse(input_json);
    if (!input) {
        snprintf(output, output_size, "Error: Invalid input JSON");
        ret = ESP_ERR_INVALID_ARG;
        goto done;
    }

    cJSON *action_item = cJSON_GetObjectItem(input, "action");
    const char *action = (cJSON_IsString(action_item) && action_item->valuestring)
                         ? action_item->valuestring
                         : NULL;
    if (!action) {
        snprintf(output, output_size, "Error: Missing 'action' (set/off/status)");
        ret = ESP_ERR_INVALID_ARG;
        cJSON_Delete(input);
        goto done;
    }

    int pixels = DEVICE_LED_DEFAULT_N;
    cJSON *pixels_item = cJSON_GetObjectItem(input, "pixels");
    if (cJSON_IsNumber(pixels_item) && pixels_item->valueint > 0) {
        pixels = pixels_item->valueint;
    }

    ret = ensure_led_ready(pixels);
    if (ret != ESP_OK) {
        snprintf(output, output_size, "Error: LED init failed (%s)", esp_err_to_name(ret));
        cJSON_Delete(input);
        goto done;
    }

    if (strcmp(action, "status") == 0) {
        snprintf(output, output_size,
                 "ok\nstatus=ready\ngpio=%d\npixels=%d\nr=%u\ng=%u\nb=%u\nbrightness=%u",
                 DEVICE_LED_GPIO,
                 s_led.pixels,
                 (unsigned)s_led.r,
                 (unsigned)s_led.g,
                 (unsigned)s_led.b,
                 (unsigned)s_led.brightness);
        cJSON_Delete(input);
        ret = ESP_OK;
        goto done;
    }

    if (strcmp(action, "off") == 0) {
        ret = apply_led_color(0, 0, 0, 255);
        if (ret != ESP_OK) {
            snprintf(output, output_size, "Error: LED off failed (%s)", esp_err_to_name(ret));
        } else {
            snprintf(output, output_size, "ok\naction=off\ngpio=%d\npixels=%d", DEVICE_LED_GPIO, s_led.pixels);
        }
        cJSON_Delete(input);
        goto done;
    }

    if (strcmp(action, "set") == 0) {
        uint8_t r = 0, g = 0, b = 0;
        bool has_hex = false;

        cJSON *hex_item = cJSON_GetObjectItem(input, "hex");
        if (cJSON_IsString(hex_item) && hex_item->valuestring && hex_item->valuestring[0]) {
            has_hex = parse_hex_color(hex_item->valuestring, &r, &g, &b);
            if (!has_hex) {
                snprintf(output, output_size, "Error: Invalid hex color, expected #RRGGBB");
                ret = ESP_ERR_INVALID_ARG;
                cJSON_Delete(input);
                goto done;
            }
        } else {
            cJSON *r_item = cJSON_GetObjectItem(input, "r");
            cJSON *g_item = cJSON_GetObjectItem(input, "g");
            cJSON *b_item = cJSON_GetObjectItem(input, "b");
            if (!cJSON_IsNumber(r_item) || !cJSON_IsNumber(g_item) || !cJSON_IsNumber(b_item)) {
                snprintf(output, output_size, "Error: set requires either hex or r/g/b");
                ret = ESP_ERR_INVALID_ARG;
                cJSON_Delete(input);
                goto done;
            }
            r = clamp_u8(r_item->valueint);
            g = clamp_u8(g_item->valueint);
            b = clamp_u8(b_item->valueint);
        }

        uint8_t brightness = 255;
        cJSON *brightness_item = cJSON_GetObjectItem(input, "brightness");
        if (cJSON_IsNumber(brightness_item)) {
            brightness = clamp_u8(brightness_item->valueint);
        }

        ret = apply_led_color(r, g, b, brightness);
        if (ret != ESP_OK) {
            snprintf(output, output_size, "Error: LED set failed (%s)", esp_err_to_name(ret));
        } else {
            snprintf(output, output_size,
                     "ok\naction=set\ngpio=%d\npixels=%d\nr=%u\ng=%u\nb=%u\nbrightness=%u",
                     DEVICE_LED_GPIO,
                     s_led.pixels,
                     (unsigned)r,
                     (unsigned)g,
                     (unsigned)b,
                     (unsigned)brightness);
        }
        cJSON_Delete(input);
        goto done;
    }

    snprintf(output, output_size, "Error: Unsupported action '%s'", action);
    ret = ESP_ERR_INVALID_ARG;
    cJSON_Delete(input);

done:
    xSemaphoreGive(s_led_mutex);
    return ret;
}
