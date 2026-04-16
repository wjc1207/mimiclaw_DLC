#include "tool_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tool_sleep";

esp_err_t tool_sleep_execute(const char *input_json, char *output, size_t output_size)
{
    int duration_ms = 1000;  // 默认 1 秒

    if (input_json && input_json[0] != '\0') {
        cJSON *root = cJSON_Parse(input_json);
        if (root) {
            cJSON *duration_ms_item = cJSON_GetObjectItem(root, "duration_ms");
            if (cJSON_IsNumber(duration_ms_item)) {
                duration_ms = duration_ms_item->valueint;
            } else {
                cJSON *duration_s_item = cJSON_GetObjectItem(root, "duration_s");
                if (cJSON_IsNumber(duration_s_item)) {
                    duration_ms = duration_s_item->valueint * 1000;
                }
            }

            cJSON_Delete(root);
        }
    }

    // 确保睡眠时长在合理范围内
    if (duration_ms < 10) {
        duration_ms = 10;  // 最小 10ms
    } else if (duration_ms > 300000) {
        duration_ms = 300000;  // 最大 5 分钟
    }

    ESP_LOGI(TAG, "Sleeping for %d ms", duration_ms);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    snprintf(output, output_size, "{\"success\":true,\"duration_ms\":%d,\"message\":\"Slept for %d milliseconds\"}",
             duration_ms, duration_ms);

    return ESP_OK;
}
