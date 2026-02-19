#pragma once

#include "esp_err.h"
#include <stddef.h>

esp_err_t tool_rgb_execute(const char *input_json,
                           char *output,
                           size_t output_size);

esp_err_t tool_rgb_init(void);