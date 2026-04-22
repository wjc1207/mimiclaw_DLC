#include "lua_modulo_rgb.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "esp_err.h"
#include "lauxlib.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "tool_rgb";

#define RGB_LED_GPIO         48
#define RGB_LED_DEFAULT_N    1
#define RMT_RESOLUTION_HZ    (10 * 1000 * 1000)

typedef struct {
    led_strip_handle_t strip;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t brightness;
    bool initialized;
} rgb_led_state_t;

static rgb_led_state_t s_led = {
    .strip = NULL,
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

static esp_err_t ensure_led_ready(void)
{
    if (s_led.strip) {
        return ESP_OK;
    }

    led_strip_config_t cfg = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = RGB_LED_DEFAULT_N,
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

    s_led.initialized = true;
    ESP_LOGI(TAG, "WS2812 ready on GPIO%d", RGB_LED_GPIO);
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

    led_strip_set_pixel(s_led.strip, 0, rr, gg, bb);

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

static int lua_rgb_set(lua_State *L)
{
    int r = (int)luaL_checkinteger(L, 1);
    int g = (int)luaL_checkinteger(L, 2);
    int b = (int)luaL_checkinteger(L, 3);
    int brightness = (int)luaL_optinteger(L, 4, 255);

    if (!s_led_mutex) {
        s_led_mutex = xSemaphoreCreateMutex();
        if (!s_led_mutex) {
            return luaL_error(L, "Error: cannot create LED mutex");
        }
    }

    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return luaL_error(L, "Error: LED busy");
    }

    esp_err_t ret = ensure_led_ready();
    if (ret != ESP_OK) {
        xSemaphoreGive(s_led_mutex);
        return luaL_error(L, "Error: LED init failed (%s)", esp_err_to_name(ret));
    }

    uint8_t rr = clamp_u8(r);
    uint8_t gg = clamp_u8(g);
    uint8_t bb = clamp_u8(b);
    uint8_t br = clamp_u8(brightness);

    ret = apply_led_color(rr, gg, bb, br);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_led_mutex);
        return luaL_error(L, "Error: LED set failed (%s)", esp_err_to_name(ret));
    }

    xSemaphoreGive(s_led_mutex);
    return 0;
}

static int lua_rgb_set_hex(lua_State *L)
{
    const char *hex = luaL_checkstring(L, 1);
    int brightness = (int)luaL_optinteger(L, 2, 255);

    if (!s_led_mutex) {
        s_led_mutex = xSemaphoreCreateMutex();
        if (!s_led_mutex) {
            return luaL_error(L, "Error: cannot create LED mutex");
        }
    }

    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return luaL_error(L, "Error: LED busy");
    }

    esp_err_t ret = ensure_led_ready();
    if (ret != ESP_OK) {
        xSemaphoreGive(s_led_mutex);
        return luaL_error(L, "Error: LED init failed (%s)", esp_err_to_name(ret));
    }

    uint8_t r, g, b;
    if (!parse_hex_color(hex, &r, &g, &b)) {
        xSemaphoreGive(s_led_mutex);
        return luaL_error(L, "Error: Invalid hex color, expected #RRGGBB");
    }

    uint8_t br = clamp_u8(brightness);
    ret = apply_led_color(r, g, b, br);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_led_mutex);
        return luaL_error(L, "Error: LED set failed (%s)", esp_err_to_name(ret));
    }

    xSemaphoreGive(s_led_mutex);
    return 0;
}

static int lua_rgb_off(lua_State *L)
{
    (void)L;

    if (!s_led_mutex) {
        s_led_mutex = xSemaphoreCreateMutex();
        if (!s_led_mutex) {
            return luaL_error(L, "Error: cannot create LED mutex");
        }
    }

    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return luaL_error(L, "Error: LED busy");
    }

    esp_err_t ret = ensure_led_ready();
    if (ret != ESP_OK) {
        xSemaphoreGive(s_led_mutex);
        return luaL_error(L, "Error: LED init failed (%s)", esp_err_to_name(ret));
    }

    ret = apply_led_color(0, 0, 0, 0);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_led_mutex);
        return luaL_error(L, "Error: LED set failed (%s)", esp_err_to_name(ret));
    }

    xSemaphoreGive(s_led_mutex);
    return 0;
}

int luaopen_rgb(lua_State *L)
{
    static const luaL_Reg funcs[] = {
        {"set", lua_rgb_set},
        {"set_hex", lua_rgb_set_hex},
        {"off", lua_rgb_off},
        {NULL, NULL},
    };

    lua_newtable(L);
    luaL_setfuncs(L, funcs, 0);
    return 1;
}

int luaopen_modulo_rgb(lua_State *L)
{
    return luaopen_rgb(L);
}

void lua_register_modulo_rgb_lib(lua_State *L)
{
    luaL_requiref(L, "modulo_rgb", luaopen_modulo_rgb, 1);
    lua_setglobal(L, "modulo_rgb");
    luaL_requiref(L, "rgb", luaopen_rgb, 1);
    lua_setglobal(L, "rgb");
}
