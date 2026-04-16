#include "tool_camera_capture.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "img_converters.h"
#include "mbedtls/base64.h"

#include "camera_core.h"
#include "cJSON.h"

static const char *TAG = "tool_camera";

static bool tool_parse_analyze_flag(const char *input)
{
    if (!input || input[0] == '\0') {
        return true;
    }

    cJSON *args = cJSON_Parse(input);
    if (!args) {
        return true;
    }

    cJSON *analyze = cJSON_GetObjectItemCaseSensitive(args, "enable_image_analysis");
    bool enabled = true;
    if (cJSON_IsBool(analyze)) {
        enabled = cJSON_IsTrue(analyze);
    }

    cJSON_Delete(args);
    return enabled;
}

static esp_err_t tool_camera_execute(const char *input, char *out, size_t out_size)
{
    bool analyze_enabled = tool_parse_analyze_flag(input);
    camera_fb_t *fb = NULL;
    esp_err_t acq = camera_core_acquire_fb_latest(&fb, pdMS_TO_TICKS(1500));
    if (acq != ESP_OK || fb == NULL) {
        snprintf(out, out_size,
                 "{\"ok\":false,\"error\":\"capture_failed\",\"esp_err\":\"0x%x\"}",
                 (unsigned int)acq);
        return acq;
    }

    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;
    bool converted = false;

    if (fb->format == PIXFORMAT_JPEG) {
        jpg_buf = fb->buf;
        jpg_len = fb->len;
    } else {
        if (!fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format, 80, &jpg_buf, &jpg_len)) {
            camera_core_release_fb(fb);
            snprintf(out, out_size, "{\"ok\":false,\"error\":\"jpeg_encode_failed\"}");
            return ESP_FAIL;
        }
        converted = true;
    }

    if (analyze_enabled) {
        // 对于图像分析，需要输出格式：<media_type>\n<base64_data>
        size_t b64_len = ((jpg_len + 2) / 3) * 4;
        size_t media_type_len = strlen("image/jpeg") + 1;
        size_t total_required = media_type_len + 1 + b64_len + 1; // media_type + \n + b64_data + \0

        if (total_required > out_size) {
            if (converted) {
                free(jpg_buf);
            }
            camera_core_release_fb(fb);
            return ESP_ERR_INVALID_SIZE;
        }

        // 写入媒体类型和换行符
        snprintf(out, out_size, "image/jpeg\n");

        // 计算 base64 编码所需的缓冲区大小
        size_t encoded_len = 0;
        int rc = mbedtls_base64_encode(
            (unsigned char *)(out + strlen(out)),
            out_size - strlen(out),
            &encoded_len,
            (const unsigned char *)jpg_buf,
            jpg_len);

        if (rc != 0) {
            if (converted) {
                free(jpg_buf);
            }
            camera_core_release_fb(fb);
            snprintf(out, out_size, "{\"ok\":false,\"error\":\"base64_encode_failed\",\"rc\":%d}", rc);
            return ESP_FAIL;
        }
    } else {
        // 对于非图像分析，直接返回原始 JPEG 数据
        if (jpg_len > out_size) {
            if (converted) {
                free(jpg_buf);
            }
            camera_core_release_fb(fb);
            return ESP_ERR_INVALID_SIZE;
        }

        memcpy(out, jpg_buf, jpg_len);
        out[jpg_len] = '\0';
    }

    if (converted) {
        free(jpg_buf);
    }
    camera_core_release_fb(fb);

    ESP_LOGI(TAG, "tool_camera captured image, bytes=%u analyze=%s",
             (unsigned int)jpg_len,
             analyze_enabled ? "true" : "false");

    return ESP_OK;
}

esp_err_t tool_camera_capture_execute(const char *input_json, char *output, size_t output_size)
{
    if (!input_json || !output || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return tool_camera_execute(input_json, output, output_size);
}