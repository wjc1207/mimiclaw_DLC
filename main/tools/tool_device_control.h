#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Control onboard WS2812 RGB LED (GPIO48).
 *
 * Input JSON:
 * - action (required): "set" | "off" | "status"
 * - r,g,b (optional for set): 0..255
 * - hex (optional for set): #RRGGBB or RRGGBB
 * - brightness (optional for set): 0..255, default 255
 * - pixels (optional): number of LEDs to drive, default 1
 */
esp_err_t tool_device_control_execute(const char *input_json, char *output, size_t output_size);
