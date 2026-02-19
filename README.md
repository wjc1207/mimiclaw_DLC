# mimiclaw DLC

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A DLC extension for [mimiclaw](https://github.com/memovai/mimiclaw) that enables LLM-controlled hardware interaction with ESP32 devices.

## Features

- 🎨 **RGB LED Control** - Control WS2812 RGB LEDs with JSON commands
- 📷 **ESP32-CAM Integration** - Capture images and return as base64-encoded data
- 🚀 **LLM-Ready** - Seamlessly integrates with the mimiclaw framework

## Quick Start

Clone into your mimiclaw project's components directory:
```bash
cd /path/to/your/mimiclaw/project/components
git clone https://github.com/wjc1207/mimiclaw_DLC.git
```

Include in your application:
```c
#include "tool_rgb.h"
#include "tool_capture.h"
```

## Usage

**RGB LED Control:**
```c
const char *input = "{\"r\": 255, \"g\": 0, \"b\": 128}";
char output[256];
tool_rgb_execute(input, output, sizeof(output));
```

**Camera Capture:**
```c
char *output = malloc(300 * 1024);
tool_capture_execute(NULL, output, 300 * 1024);
free(output);
```

## Configuration

Edit `tool_rgb.c` for GPIO pin and LED count.  
Edit `tool_capture.c` for camera URL and image size limits.

## Hardware Requirements

- ESP32 development board with PSRAM
- WS2812 RGB LED (default GPIO 48)
- ESP32-CAM module with HTTP server (for camera capture)

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Links

- [mimiclaw](https://github.com/memovai/mimiclaw) - Main project
- Author: Junchi Wang
