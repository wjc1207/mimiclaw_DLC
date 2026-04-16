#include "tool_ble_listener.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_timer.h"
#include "bthome_listener.h"

static void tool_ble_execute(char *out, size_t out_size)
{
    thome_reading_t reading = {0};
    bool has = bthome_listener_get_latest(&reading);
    uint64_t now_ms = esp_timer_get_time() / 1000ULL;

    if (!has) {
        snprintf(out, out_size,
                 "{\"ok\":true,\"status\":\"no_data\",\"message\":\"No BLE BTHome data yet.\"}");
        return;
    }

    uint64_t age_ms = now_ms >= reading.last_seen_ms ? (now_ms - reading.last_seen_ms) : 0;
    snprintf(out, out_size,
             "{\"ok\":true,\"status\":\"ok\","
             "\"source_addr\":\"%s\","
             "\"age_ms\":%" PRIu64 ","
             "\"temperature\":%.2f,\"temperature_valid\":%s,"
             "\"humidity\":%.2f,\"humidity_valid\":%s,"
             "\"battery\":%d,\"battery_valid\":%s}",
             reading.source_addr,
             age_ms,
             reading.temperature_c,
             reading.temperature_valid ? "true" : "false",
             reading.humidity_percent,
             reading.humidity_valid ? "true" : "false",
             reading.battery_percent,
             reading.battery_valid ? "true" : "false");
}

esp_err_t tool_ble_listener_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    if (!output || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    tool_ble_execute(output, output_size);
    return ESP_OK;
}