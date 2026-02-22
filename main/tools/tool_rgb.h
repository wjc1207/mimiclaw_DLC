#pragma once

#include "esp_err.h"
#include <stddef.h>

esp_err_t tool_rgb_execute(const char *input_json,
                           char *output,
                           size_t output_size);

esp_err_t tool_rgb_init(void);

esp_err_t write_rgb_to_file(int r, int g, int b);

esp_err_t read_rgb_from_file_and_apply(void);