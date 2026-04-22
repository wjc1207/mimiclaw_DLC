#include "lua/lua_gpio_lib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lua.h"
#include "lauxlib.h"

static const char *TAG = "lua_gpio_lib";

#define GPIO_PIN_MAX     49
#define MAX_PWM_CHANNELS 8
#define LEDC_DUTY_MAX    8191

typedef enum {
    PIN_FREE = 0,
    PIN_GPIO,
    PIN_PWM,
} pin_usage_t;

typedef struct {
    int pin;
    ledc_channel_t channel;
    ledc_timer_t timer;
    bool active;
} pwm_entry_t;

static pin_usage_t s_pin_usage[GPIO_PIN_MAX];
static SemaphoreHandle_t s_mutex = NULL;

static pwm_entry_t s_pwm[MAX_PWM_CHANNELS];
static int s_pwm_count = 0;

static bool pin_valid(int pin)
{
    return pin >= 0 && pin < GPIO_PIN_MAX;
}

static bool pin_claim(int pin, pin_usage_t usage)
{
    if (!pin_valid(pin)) {
        return false;
    }

    if (s_pin_usage[pin] != PIN_FREE && s_pin_usage[pin] != usage) {
        return false;
    }

    s_pin_usage[pin] = usage;
    return true;
}

static void pin_release(int pin)
{
    if (pin_valid(pin)) {
        s_pin_usage[pin] = PIN_FREE;
    }
}

/* ── gpio module ──────────────────────────────────────────── */

static int l_gpio_write(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);

    if (!pin_valid(pin)) {
        return luaL_error(L, "gpio.write: invalid pin %d", pin);
    }
    if (!pin_claim(pin, PIN_GPIO)) {
        return luaL_error(L, "gpio.write: pin %d is already in use", pin);
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return luaL_error(L, "gpio.write: gpio_config failed: %s", esp_err_to_name(err));
    }

    err = gpio_set_level((gpio_num_t)pin, val ? 1 : 0);
    if (err != ESP_OK) {
        return luaL_error(L, "gpio.write: gpio_set_level failed: %s", esp_err_to_name(err));
    }

    return 0;
}

static int l_gpio_read(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);

    if (!pin_valid(pin)) {
        return luaL_error(L, "gpio.read: invalid pin %d", pin);
    }
    if (!pin_claim(pin, PIN_GPIO)) {
        return luaL_error(L, "gpio.read: pin %d is already in use", pin);
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return luaL_error(L, "gpio.read: gpio_config failed: %s", esp_err_to_name(err));
    }

    lua_pushinteger(L, gpio_get_level((gpio_num_t)pin) ? 1 : 0);
    return 1;
}

static const luaL_Reg gpio_lib[] = {
    {"write", l_gpio_write},
    {"read", l_gpio_read},
    {NULL, NULL},
};

/* ── pwm module ───────────────────────────────────────────── */

static int pwm_find(int pin)
{
    for (int i = 0; i < s_pwm_count; i++) {
        if (s_pwm[i].pin == pin && s_pwm[i].active) {
            return i;
        }
    }
    return -1;
}

static int l_pwm_start(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int freq = (int)luaL_checkinteger(L, 2);
    int duty = (int)luaL_checkinteger(L, 3);

    if (!pin_valid(pin)) {
        return luaL_error(L, "pwm.start: invalid pin %d", pin);
    }
    if (!pin_claim(pin, PIN_PWM)) {
        return luaL_error(L, "pwm.start: pin %d is already in use", pin);
    }
    if (s_pwm_count >= MAX_PWM_CHANNELS) {
        return luaL_error(L, "pwm.start: no free PWM channel");
    }

    ledc_channel_t ch = (ledc_channel_t)s_pwm_count;
    ledc_timer_t tmr = (ledc_timer_t)(s_pwm_count / 2);

    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = tmr,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = (uint32_t)freq,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        return luaL_error(L, "ledc_timer_config failed: %s", esp_err_to_name(err));
    }

    uint32_t duty_val = ((uint32_t)duty * LEDC_DUTY_MAX) / 100;
    ledc_channel_config_t ccfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = ch,
        .timer_sel = tmr,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = pin,
        .duty = duty_val,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) {
        return luaL_error(L, "ledc_channel_config failed: %s", esp_err_to_name(err));
    }

    s_pwm[s_pwm_count].pin = pin;
    s_pwm[s_pwm_count].channel = ch;
    s_pwm[s_pwm_count].timer = tmr;
    s_pwm[s_pwm_count].active = true;
    s_pwm_count++;

    return 0;
}

static int l_pwm_set_duty(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int duty = (int)luaL_checkinteger(L, 2);
    int idx = pwm_find(pin);
    if (idx < 0) {
        return luaL_error(L, "pwm.set_duty: PWM not active on pin %d", pin);
    }

    uint32_t duty_val = ((uint32_t)duty * LEDC_DUTY_MAX) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, s_pwm[idx].channel, duty_val);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, s_pwm[idx].channel);
    return 0;
}

static int l_pwm_set_freq(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int freq = (int)luaL_checkinteger(L, 2);
    int idx = pwm_find(pin);
    if (idx < 0) {
        return luaL_error(L, "pwm.set_freq: PWM not active on pin %d", pin);
    }

    ledc_set_freq(LEDC_LOW_SPEED_MODE, s_pwm[idx].timer, (uint32_t)freq);
    return 0;
}

static int l_pwm_stop(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int idx = pwm_find(pin);
    if (idx < 0) {
        return luaL_error(L, "pwm.stop: PWM not active on pin %d", pin);
    }

    ledc_stop(LEDC_LOW_SPEED_MODE, s_pwm[idx].channel, 0);
    s_pwm[idx].active = false;
    pin_release(pin);
    return 0;
}

static const luaL_Reg pwm_lib[] = {
    {"start", l_pwm_start},
    {"set_duty", l_pwm_set_duty},
    {"set_freq", l_pwm_set_freq},
    {"stop", l_pwm_stop},
    {NULL, NULL},
};

/* ── sleep module ─────────────────────────────────────────── */

static int l_sleep_ms(lua_State *L)
{
    int ms = (int)luaL_checkinteger(L, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    return 0;
}

static const luaL_Reg sleep_lib[] = {
    {"ms", l_sleep_ms},
    {NULL, NULL},
};

void lua_open_gpio_libs(lua_State *L)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }

    luaL_newlib(L, gpio_lib);
    lua_setglobal(L, "gpio");
    luaL_newlib(L, pwm_lib);
    lua_setglobal(L, "pwm");
    luaL_newlib(L, sleep_lib);
    lua_setglobal(L, "sleep");

    ESP_LOGI(TAG, "Lua hardware libraries registered");
}
