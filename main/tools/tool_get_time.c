#include "tool_get_time.h"
#include "mimi_config.h"

#include <time.h>
#include <sys/time.h>
#include "esp_log.h"

static const char *TAG = "tool_time";

esp_err_t tool_get_time_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    setenv("TZ", MIMI_TIMEZONE, 1);
    tzset();

    time_t now = time(NULL);
    if (now < 100000) {
        ESP_LOGW(TAG, "System clock not set yet (epoch=%ld)", (long)now);
        snprintf(output, output_size, "Error: system clock not set (SNTP pending)");
        return ESP_ERR_INVALID_STATE;
    }

    struct tm local;
    localtime_r(&now, &local);

    char time_str[128];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S %Z (%A)", &local);
    snprintf(output, output_size, "{\"time\":\"%s\",\"epoch\":%ld}", time_str, (long)now);

    ESP_LOGI(TAG, "Time: %s", output);
    return ESP_OK;
}
