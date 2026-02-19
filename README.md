# mimiclaw DLC

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A Device Level Component (DLC) extension for [mimiclaw](https://github.com/memovai/mimiclaw) that enables LLM-controlled hardware interaction with ESP32 devices in your local network.

## Overview

This DLC provides two essential tools for the mimiclaw project:
- **RGB LED Control**: Control WS2812 RGB LEDs connected to your ESP32
- **Camera Capture**: Capture and retrieve images from ESP32-CAM devices via HTTP

## Features

- 🎨 **RGB LED Control**: Set custom colors on WS2812 RGB LEDs with simple JSON commands
- 📷 **ESP32-CAM Integration**: Capture images from ESP32-CAM and receive them as base64-encoded data
- 🚀 **LLM-Ready**: Designed to work seamlessly with Large Language Models through the mimiclaw framework
- 🔧 **Easy Configuration**: Simple configuration through compile-time definitions
- 💾 **Optimized Memory**: Uses PSRAM for image buffering to handle large camera captures

## Hardware Requirements

### For RGB LED Control
- ESP32 development board (tested on ESP32-S3)
- WS2812 RGB LED (or compatible LED strip)
- Connected to GPIO 48 (configurable in `tool_rgb.c`)

### For Camera Capture
- ESP32-CAM module with HTTP server running
- Network connectivity between your ESP32 and ESP32-CAM
- ESP32 with PSRAM (for image buffering)

## Installation

1. Clone this repository into your mimiclaw project's components directory:
```bash
cd /path/to/your/mimiclaw/project/components
git clone https://github.com/wjc1207/mimiclaw_DLC.git
```

2. Include the component in your main application by adding the header files:
```c
#include "tool_rgb.h"
#include "tool_capture.h"
```

3. Register the tools with your mimiclaw instance (refer to [mimiclaw documentation](https://github.com/memovai/mimiclaw))

## Configuration

### RGB LED Configuration

Edit the following definitions in `tool_rgb.c`:

```c
#define RGB_GPIO        48              // GPIO pin for RGB LED data
#define RGB_LED_COUNT   1               // Number of LEDs in the strip
#define RMT_RESOLUTION  10000000        // RMT clock resolution (10 MHz)
```

### Camera Configuration

Edit the following definitions in `tool_capture.c`:

```c
#define CAMERA_URL      "http://192.168.3.40/capture"  // Your ESP32-CAM URL
#define MAX_IMAGE_SIZE  (200 * 1024)                   // Maximum image size (200KB)
```

## Usage

### RGB LED Control

The RGB tool accepts JSON input to set LED colors:

```json
{
  "r": 255,
  "g": 0,
  "b": 128
}
```

**Example C code:**
```c
char output[256];
const char *input = "{\"r\": 255, \"g\": 0, \"b\": 128}";
esp_err_t err = tool_rgb_execute(input, output, sizeof(output));

if (err == ESP_OK) {
    printf("Success: %s\n", output);
} else {
    printf("Error: %s\n", output);
}
```

**Parameters:**
- `r`: Red value (0-255)
- `g`: Green value (0-255)
- `b`: Blue value (0-255)

### Camera Capture

The capture tool retrieves an image from the ESP32-CAM and returns it as base64-encoded data:

```c
char *output = malloc(300 * 1024);  // Allocate sufficient space for base64 image
esp_err_t err = tool_capture_execute(NULL, output, 300 * 1024);

if (err == ESP_OK) {
    // output contains base64-encoded JPEG image
    printf("Image captured successfully (base64)\n");
    // Send to LLM for vision processing...
} else {
    printf("Capture failed: %s\n", output);
}

free(output);
```

**Note:** The output buffer must be large enough to hold the base64-encoded image (approximately 33% larger than the original image size).

## API Reference

### tool_rgb_execute()

```c
esp_err_t tool_rgb_execute(const char *input_json, char *output, size_t output_size);
```

**Parameters:**
- `input_json`: JSON string containing RGB values
- `output`: Buffer for status message
- `output_size`: Size of output buffer

**Returns:**
- `ESP_OK` on success
- `ESP_ERR_INVALID_ARG` if JSON is invalid
- Other error codes on hardware failure

### tool_capture_execute()

```c
esp_err_t tool_capture_execute(const char *input_json, char *output, size_t output_size);
```

**Parameters:**
- `input_json`: Reserved (currently unused, pass NULL)
- `output`: Buffer for base64-encoded image
- `output_size`: Size of output buffer (must be large enough for base64 data)

**Returns:**
- `ESP_OK` on success
- `ESP_ERR_NO_MEM` if memory allocation fails
- `ESP_FAIL` on HTTP or encoding errors

## Dependencies

- ESP-IDF framework
- `led_strip` component (for RGB LED control)
- `esp_http_client` (for camera capture)
- `cJSON` (for JSON parsing)
- `mbedtls` (for base64 encoding)

## Troubleshooting

### RGB LED not lighting up
- Verify GPIO pin number matches your hardware connection
- Check power supply to the LED
- Ensure LED strip type is WS2812 compatible

### Camera capture fails
- Verify ESP32-CAM is running and accessible at the configured URL
- Check network connectivity between devices
- Ensure ESP32 has PSRAM enabled in menuconfig
- Verify image size doesn't exceed `MAX_IMAGE_SIZE`

### Memory allocation errors
- Enable PSRAM in ESP-IDF menuconfig: `Component config → ESP PSRAM → Support for external PSRAM`
- Increase heap size if needed

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## Acknowledgments

- Part of the [mimiclaw](https://github.com/memovai/mimiclaw) project
- Built for ESP32 using the ESP-IDF framework
- Thanks to all contributors and the ESP32 community

## Author

Junchi Wang

## Related Projects

- [mimiclaw](https://github.com/memovai/mimiclaw) - Main project for LLM-controlled hardware interaction
