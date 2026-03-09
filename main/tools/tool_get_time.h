#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Execute get_current_time tool.
 * Reads the SNTP-synced system clock and returns the current time as a JSON string.
 */
esp_err_t tool_get_time_execute(const char *input_json, char *output, size_t output_size);
