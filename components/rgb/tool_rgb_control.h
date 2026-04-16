#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Control onboard WS2812 RGB LED (GPIO48).
 *
 * Input JSON:
 * - r,g,b (optional): 0..255
 * - hex (optional): #RRGGBB or RRGGBB
 * - brightness (optional): 0..255, default 255
 */
esp_err_t tool_rgb_control_execute(const char *input_json, char *output, size_t output_size);