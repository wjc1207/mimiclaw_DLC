#include "lua/lua_gpio_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"

#include "lua.h"
#include "lauxlib.h"

static const char *TAG = "lua_gpio_lib";

#define GPIO_PIN_MAX        49
#define MAX_I2C_BUSES       2
#define MAX_SPI_BUSES       2
#define MAX_RGB_STRIPS      4
#define MAX_PWM_CHANNELS    8
#define MAX_TRANSFER_BYTES  256
#define RMT_RESOLUTION      (10 * 1000 * 1000)
#define LEDC_DUTY_MAX       8191
#define I2C_TIMEOUT_MS      1000

typedef enum {
    PIN_FREE = 0,
    PIN_GPIO,
    PIN_I2C,
    PIN_SPI,
    PIN_RGB,
    PIN_PWM,
} pin_usage_t;

typedef struct {
    int sda;
    int scl;
    i2c_master_bus_handle_t bus;
} i2c_bus_entry_t;

typedef struct {
    int mosi;
    int miso;
    int sclk;
    spi_host_device_t host;
} spi_bus_entry_t;

typedef struct {
    int pin;
    int num_pixels;
    led_strip_handle_t strip;
} rgb_entry_t;

typedef struct {
    int pin;
    ledc_channel_t channel;
    ledc_timer_t timer;
    bool active;
} pwm_entry_t;

static pin_usage_t s_pin_usage[GPIO_PIN_MAX];
static SemaphoreHandle_t s_mutex = NULL;
static i2c_bus_entry_t s_i2c[MAX_I2C_BUSES];
static int s_i2c_count = 0;
static spi_bus_entry_t s_spi[MAX_SPI_BUSES];
static int s_spi_count = 0;
static rgb_entry_t s_rgb[MAX_RGB_STRIPS];
static int s_rgb_count = 0;
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

static int parse_byte_array(lua_State *L, int index, uint8_t *buf, int max_len)
{
    luaL_checktype(L, index, LUA_TTABLE);
    int len = (int)lua_rawlen(L, index);
    if (len > max_len) {
        len = max_len;
    }

    for (int i = 1; i <= len; i++) {
        lua_rawgeti(L, index, i);
        buf[i - 1] = (uint8_t)luaL_checkinteger(L, -1);
        lua_pop(L, 1);
    }
    return len;
}

static void format_byte_array(const uint8_t *data, int len, char *buf, size_t sz)
{
    int pos = 0;
    pos += snprintf(buf + pos, sz - (size_t)pos, "[");
    for (int i = 0; i < len && (size_t)pos < sz - 6; i++) {
        if (i > 0) {
            pos += snprintf(buf + pos, sz - (size_t)pos, ",");
        }
        pos += snprintf(buf + pos, sz - (size_t)pos, "%d", data[i]);
    }
    snprintf(buf + pos, sz - (size_t)pos, "]");
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

/* ── i2c module ───────────────────────────────────────────── */

static i2c_master_bus_handle_t i2c_get_bus(lua_State *L, int sda, int scl)
{
    for (int i = 0; i < s_i2c_count; i++) {
        if (s_i2c[i].sda == sda && s_i2c[i].scl == scl) {
            return s_i2c[i].bus;
        }
    }

    if (s_i2c_count >= MAX_I2C_BUSES) {
        luaL_error(L, "i2c: no free bus slot");
        return NULL;
    }
    if (!pin_claim(sda, PIN_I2C) || !pin_claim(scl, PIN_I2C)) {
        luaL_error(L, "i2c: pins %d/%d are already in use", sda, scl);
        return NULL;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = (s_i2c_count == 0) ? I2C_NUM_0 : I2C_NUM_1,
        .sda_io_num = (gpio_num_t)sda,
        .scl_io_num = (gpio_num_t)scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        pin_release(sda);
        pin_release(scl);
        luaL_error(L, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return NULL;
    }

    s_i2c[s_i2c_count].sda = sda;
    s_i2c[s_i2c_count].scl = scl;
    s_i2c[s_i2c_count].bus = bus;
    s_i2c_count++;
    return bus;
}

static int l_i2c_write(lua_State *L)
{
    int sda = (int)luaL_checkinteger(L, 1);
    int scl = (int)luaL_checkinteger(L, 2);
    int addr = (int)luaL_checkinteger(L, 3);
    int freq = (int)luaL_optinteger(L, 5, 100000);
    uint8_t buf[MAX_TRANSFER_BYTES];
    int len = parse_byte_array(L, 4, buf, MAX_TRANSFER_BYTES);

    i2c_master_bus_handle_t bus = i2c_get_bus(L, sda, scl);
    if (!bus) {
        return luaL_error(L, "i2c.write: bus not available");
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = (uint16_t)addr,
        .scl_speed_hz = (uint32_t)freq,
    };
    i2c_master_dev_handle_t dev = NULL;

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        return luaL_error(L, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
    }

    err = i2c_master_transmit(dev, buf, (size_t)len, I2C_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev);
    if (err != ESP_OK) {
        return luaL_error(L, "i2c_master_transmit failed: %s", esp_err_to_name(err));
    }

    return 0;
}

static int l_i2c_read(lua_State *L)
{
    int sda = (int)luaL_checkinteger(L, 1);
    int scl = (int)luaL_checkinteger(L, 2);
    int addr = (int)luaL_checkinteger(L, 3);
    int len = (int)luaL_checkinteger(L, 4);
    int freq = (int)luaL_optinteger(L, 5, 100000);

    if (len <= 0) {
        return luaL_error(L, "i2c.read: invalid length %d", len);
    }
    if (len > MAX_TRANSFER_BYTES) {
        len = MAX_TRANSFER_BYTES;
    }

    i2c_master_bus_handle_t bus = i2c_get_bus(L, sda, scl);
    if (!bus) {
        return luaL_error(L, "i2c.read: bus not available");
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = (uint16_t)addr,
        .scl_speed_hz = (uint32_t)freq,
    };
    i2c_master_dev_handle_t dev = NULL;

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        return luaL_error(L, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
    }

    uint8_t rbuf[MAX_TRANSFER_BYTES];
    err = i2c_master_receive(dev, rbuf, (size_t)len, I2C_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev);
    if (err != ESP_OK) {
        return luaL_error(L, "i2c_master_receive failed: %s", esp_err_to_name(err));
    }

    char arr[MAX_TRANSFER_BYTES * 4 + 4];
    format_byte_array(rbuf, len, arr, sizeof(arr));
    lua_pushstring(L, arr);
    return 1;
}

static const luaL_Reg i2c_lib[] = {
    {"write", l_i2c_write},
    {"read", l_i2c_read},
    {NULL, NULL},
};

/* ── spi module ───────────────────────────────────────────── */

static int spi_get_bus_index(lua_State *L, int mosi, int miso, int sclk)
{
    for (int i = 0; i < s_spi_count; i++) {
        if (s_spi[i].mosi == mosi && s_spi[i].miso == miso && s_spi[i].sclk == sclk) {
            return i;
        }
    }

    if (s_spi_count >= MAX_SPI_BUSES) {
        luaL_error(L, "spi: no free bus slot");
        return -1;
    }
    if (!pin_claim(mosi, PIN_SPI) || !pin_claim(sclk, PIN_SPI) || (miso >= 0 && !pin_claim(miso, PIN_SPI))) {
        luaL_error(L, "spi: pins are already in use");
        return -1;
    }

    spi_host_device_t host = (s_spi_count == 0) ? SPI2_HOST : SPI3_HOST;
    spi_bus_config_t buscfg = {
        .mosi_io_num = mosi,
        .miso_io_num = (miso >= 0) ? miso : -1,
        .sclk_io_num = sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = MAX_TRANSFER_BYTES,
    };

    esp_err_t err = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        luaL_error(L, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return -1;
    }

    int idx = s_spi_count;
    s_spi[idx].mosi = mosi;
    s_spi[idx].miso = miso;
    s_spi[idx].sclk = sclk;
    s_spi[idx].host = host;
    s_spi_count++;
    return idx;
}

static int l_spi_transfer(lua_State *L)
{
    int mosi = (int)luaL_checkinteger(L, 1);
    int miso = (int)luaL_checkinteger(L, 2);
    int sclk = (int)luaL_checkinteger(L, 3);
    int cs = (int)luaL_checkinteger(L, 4);
    int mode = (int)luaL_optinteger(L, 6, 0);
    int speed = (int)luaL_optinteger(L, 7, 1000000);
    uint8_t tx_buf[MAX_TRANSFER_BYTES];
    uint8_t rx_buf[MAX_TRANSFER_BYTES];

    int len = parse_byte_array(L, 5, tx_buf, MAX_TRANSFER_BYTES);
    memset(rx_buf, 0, sizeof(rx_buf));

    if (!pin_claim(cs, PIN_SPI)) {
        return luaL_error(L, "spi.transfer: pin %d is already in use", cs);
    }

    int idx = spi_get_bus_index(L, mosi, miso, sclk);
    if (idx < 0) {
        return luaL_error(L, "spi.transfer: bus unavailable");
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = speed,
        .mode = mode,
        .spics_io_num = cs,
        .queue_size = 1,
    };

    spi_device_handle_t dev;
    esp_err_t err = spi_bus_add_device(s_spi[idx].host, &devcfg, &dev);
    if (err != ESP_OK) {
        return luaL_error(L, "spi_bus_add_device failed: %s", esp_err_to_name(err));
    }

    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    err = spi_device_polling_transmit(dev, &t);
    spi_bus_remove_device(dev);
    if (err != ESP_OK) {
        return luaL_error(L, "spi_device_polling_transmit failed: %s", esp_err_to_name(err));
    }

    char arr[MAX_TRANSFER_BYTES * 4 + 4];
    format_byte_array(rx_buf, len, arr, sizeof(arr));
    lua_pushstring(L, arr);
    return 1;
}

static const luaL_Reg spi_lib[] = {
    {"transfer", l_spi_transfer},
    {NULL, NULL},
};

/* ── rgb module ───────────────────────────────────────────── */

static led_strip_handle_t rgb_get_strip(lua_State *L, int pin, int num_pixels)
{
    for (int i = 0; i < s_rgb_count; i++) {
        if (s_rgb[i].pin == pin) {
            if (s_rgb[i].num_pixels < num_pixels) {
                led_strip_del(s_rgb[i].strip);
                led_strip_config_t cfg = {
                    .strip_gpio_num = pin,
                    .max_leds = (uint32_t)num_pixels,
                };
                led_strip_rmt_config_t rmt = {
                    .clk_src = RMT_CLK_SRC_DEFAULT,
                    .resolution_hz = RMT_RESOLUTION,
                    .mem_block_symbols = 64,
                    .flags.with_dma = false,
                };
                if (led_strip_new_rmt_device(&cfg, &rmt, &s_rgb[i].strip) != ESP_OK) {
                    luaL_error(L, "led_strip_new_rmt_device resize failed");
                    return NULL;
                }
                s_rgb[i].num_pixels = num_pixels;
            }
            return s_rgb[i].strip;
        }
    }

    if (s_rgb_count >= MAX_RGB_STRIPS) {
        luaL_error(L, "rgb: no free strip slot");
        return NULL;
    }
    if (!pin_claim(pin, PIN_RGB)) {
        luaL_error(L, "rgb: pin %d is already in use", pin);
        return NULL;
    }

    led_strip_config_t cfg = {
        .strip_gpio_num = pin,
        .max_leds = (uint32_t)num_pixels,
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION,
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };

    led_strip_handle_t strip = NULL;
    esp_err_t err = led_strip_new_rmt_device(&cfg, &rmt, &strip);
    if (err != ESP_OK) {
        pin_release(pin);
        luaL_error(L, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        return NULL;
    }

    s_rgb[s_rgb_count].pin = pin;
    s_rgb[s_rgb_count].num_pixels = num_pixels;
    s_rgb[s_rgb_count].strip = strip;
    s_rgb_count++;
    return strip;
}

static int l_rgb_fill(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int n = (int)luaL_checkinteger(L, 2);
    int r = (int)luaL_checkinteger(L, 3);
    int g = (int)luaL_checkinteger(L, 4);
    int b = (int)luaL_checkinteger(L, 5);

    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;

    led_strip_handle_t strip = rgb_get_strip(L, pin, n);
    if (!strip) {
        return luaL_error(L, "rgb.fill: strip unavailable");
    }

    for (int i = 0; i < n; i++) {
        led_strip_set_pixel(strip, (uint32_t)i, (uint32_t)r, (uint32_t)g, (uint32_t)b);
    }
    return 0;
}

static int l_rgb_show(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int n = (int)luaL_checkinteger(L, 2);

    led_strip_handle_t strip = rgb_get_strip(L, pin, n);
    if (!strip) {
        return luaL_error(L, "rgb.show: strip unavailable");
    }

    esp_err_t err = led_strip_refresh(strip);
    if (err != ESP_OK) {
        return luaL_error(L, "led_strip_refresh failed: %s", esp_err_to_name(err));
    }
    return 0;
}

static const luaL_Reg rgb_lib[] = {
    {"fill", l_rgb_fill},
    {"show", l_rgb_show},
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
    luaL_newlib(L, i2c_lib);
    lua_setglobal(L, "i2c");
    luaL_newlib(L, spi_lib);
    lua_setglobal(L, "spi");
    luaL_newlib(L, rgb_lib);
    lua_setglobal(L, "rgb");
    luaL_newlib(L, pwm_lib);
    lua_setglobal(L, "pwm");
    luaL_newlib(L, sleep_lib);
    lua_setglobal(L, "sleep");

    ESP_LOGI(TAG, "Lua hardware libraries registered");
}
