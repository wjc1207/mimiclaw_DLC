#include "tool_rgb.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "led_strip.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_spiffs.h"
#include "esp_err.h"
#include "stdio.h"
#include "errno.h"

static const char *TAG = "tool_rgb";

/* ===== CONFIG ===== */
#define RGB_GPIO        48
#define RGB_LED_COUNT   1
#define RMT_RESOLUTION  (10 * 1000 * 1000)   // 10 MHz for better timing stability
#define RGB_FILE_PATH "/spiffs/rgb.txt"

/* Static handle */
static led_strip_handle_t s_strip = NULL;

/* -----------------------------------------------------------
   Internal initialization (called once automatically)
----------------------------------------------------------- */
esp_err_t tool_rgb_init(void)
{
    if (s_strip != NULL) {
        return ESP_OK;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_GPIO,
        .max_leds = RGB_LED_COUNT,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION,
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(
        &strip_config,
        &rmt_config,
        &s_strip
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "RGB initialized on GPIO %d", RGB_GPIO);

    /* Ensure LED starts OFF */
    led_strip_clear(s_strip);
    led_strip_refresh(s_strip);

    return ESP_OK;
}

/* -----------------------------------------------------------
   Write RGB values to a file in SPIFFS
----------------------------------------------------------- */
esp_err_t write_rgb_to_file(int r, int g, int b)
{
    FILE *file = fopen(RGB_FILE_PATH, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "fopen failed, errno=%d", errno);
        ESP_LOGE(TAG, "Check SPIFFS mount and base_path");
        return ESP_FAIL;
    }

    fprintf(file, "{\"r\":%d,\"g\":%d,\"b\":%d}", r, g, b);
    fclose(file);

    return ESP_OK;
}

/* -----------------------------------------------------------
   Read RGB values from the file and apply them to the LED strip
----------------------------------------------------------- */
esp_err_t read_rgb_from_file_and_apply(void)
{
    FILE *file = fopen(RGB_FILE_PATH, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for reading", RGB_FILE_PATH);
        return ESP_ERR_INVALID_ARG;
    }

    int r = 0, g = 0, b = 0;
    if (fscanf(file, "{\"r\": %d, \"g\": %d, \"b\": %d}", &r, &g, &b) != 3) {
        ESP_LOGE(TAG, "Invalid data in %s", RGB_FILE_PATH);
        fclose(file);
        return ESP_ERR_INVALID_ARG;
    }
    fclose(file);

    /* Apply the RGB color */
    esp_err_t err = led_strip_set_pixel(s_strip, 0, r, g, b);
    if (err == ESP_OK) {
        err = led_strip_refresh(s_strip);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB update failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "RGB set to R:%d G:%d B:%d from file", r, g, b);
    return ESP_OK;
}

/* -----------------------------------------------------------
   Tool Execution Entry
----------------------------------------------------------- */
esp_err_t tool_rgb_execute(const char *input_json,
                           char *output,
                           size_t output_size)
{
    if (!input_json || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Ensure initialization */
    esp_err_t err = tool_rgb_init();
    if (err != ESP_OK) {
        snprintf(output, output_size, "RGB init failed");
        return err;
    }

    /* -------- Parse JSON -------- */
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }

    int r = 0, g = 0, b = 0;

    cJSON *jr = cJSON_GetObjectItem(root, "r");
    cJSON *jg = cJSON_GetObjectItem(root, "g");
    cJSON *jb = cJSON_GetObjectItem(root, "b");

    if (cJSON_IsNumber(jr)) r = jr->valueint;
    if (cJSON_IsNumber(jg)) g = jg->valueint;
    if (cJSON_IsNumber(jb)) b = jb->valueint;

    cJSON_Delete(root);

    /* Clamp values safely */
    r = (r < 0) ? 0 : (r > 255 ? 255 : r);
    g = (g < 0) ? 0 : (g > 255 ? 255 : g);
    b = (b < 0) ? 0 : (b > 255 ? 255 : b);

    /* -------- Apply color -------- */
    err = led_strip_set_pixel(s_strip, 0, r, g, b);
    if (err == ESP_OK) {
        err = led_strip_refresh(s_strip);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB update failed: %s", esp_err_to_name(err));
        snprintf(output, output_size, "RGB error");
        return err;
    }

    /* Write RGB values to file */
    err = write_rgb_to_file(r, g, b);
    if (err != ESP_OK) {
        snprintf(output, output_size, "Failed to write RGB values to file");
        return err;
    }

    ESP_LOGI(TAG, "RGB set to R:%d G:%d B:%d", r, g, b);

    snprintf(output, output_size,
             "RGB set to R:%d G:%d B:%d", r, g, b);

    return ESP_OK;
}

