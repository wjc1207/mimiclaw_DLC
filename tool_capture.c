#include "tool_capture.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include "mbedtls/base64.h"

static const char *TAG = "tool_capture";

/* ===== CONFIG ===== */
#define CAMERA_URL      "http://192.168.3.40/capture"
#define MAX_IMAGE_SIZE  (200 * 1024)   // 200KB safe for JPEG

/* ===== Capture Context ===== */
typedef struct {
    uint8_t *buffer;
    size_t length;
    size_t max_length;
} capture_ctx_t;

/* ===== HTTP Event Handler ===== */
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    capture_ctx_t *ctx = (capture_ctx_t *)evt->user_data;

    switch (evt->event_id) {

        case HTTP_EVENT_ON_DATA:

            if (!ctx || !ctx->buffer) break;

            if (ctx->length + evt->data_len <= ctx->max_length) {
                memcpy(ctx->buffer + ctx->length,
                       evt->data,
                       evt->data_len);

                ctx->length += evt->data_len;
            } else {
                ESP_LOGW(TAG, "Image too large, truncating");
            }
            break;

        default:
            break;
    }

    return ESP_OK;
}

/* ===== Tool Execute ===== */
esp_err_t tool_capture_execute(const char *input_json,
                               char *output,
                               size_t output_size)
{
    ESP_LOGI(TAG, "Starting camera capture...");

    capture_ctx_t ctx = {0};

    ctx.buffer = heap_caps_malloc(
        MAX_IMAGE_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (!ctx.buffer) {
        snprintf(output, output_size, "Error: PSRAM allocation failed");
        ESP_LOGE(TAG, "PSRAM allocation failed");
        return ESP_ERR_NO_MEM;
    }

    ctx.length = 0;
    ctx.max_length = MAX_IMAGE_SIZE;

    esp_http_client_config_t config = {
        .url = CAMERA_URL,
        .method = HTTP_METHOD_GET,
        .event_handler = _http_event_handler,
        .user_data = &ctx,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (!client) {
        free(ctx.buffer);
        snprintf(output, output_size, "Error: HTTP init failed");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        snprintf(output, output_size,
                 "Error: camera fetch failed (%s)",
                 esp_err_to_name(err));

        ESP_LOGE(TAG, "%s", output);
        esp_http_client_cleanup(client);
        free(ctx.buffer);
        return err;
    }

    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Captured image: %d bytes", (int)ctx.length);

    /* ===== BASE64 ENCODE ===== */

    size_t b64_len = 0;

    // calculate required size
    mbedtls_base64_encode(NULL, 0, &b64_len,
                          ctx.buffer, ctx.length);

    if (b64_len + 1 > output_size) {
        ESP_LOGE(TAG, "Output buffer too small (%d needed)", (int)b64_len);
        free(ctx.buffer);
        return ESP_ERR_NO_MEM;
    }

    int ret = mbedtls_base64_encode(
        (unsigned char *)output,
        output_size,
        &b64_len,
        ctx.buffer,
        ctx.length
    );

    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 encode failed");
        free(ctx.buffer);
        return ESP_FAIL;
    }

    output[b64_len] = '\0';

    ESP_LOGI(TAG, "Returning base64 image: %d bytes", (int)b64_len);

    free(ctx.buffer);

    return ESP_OK;
}