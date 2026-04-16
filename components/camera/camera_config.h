#pragma once

#include "esp_camera.h"

typedef struct {
    const char *name;      // 外部使用（NVS/CLI/JSON）
    framesize_t value;     // 内部使用（驱动）
} frame_map_t;

static const frame_map_t frame_size_map[] = {
    {"160x120", FRAMESIZE_QQVGA},
    {"176x144", FRAMESIZE_QCIF},
    {"240x176", FRAMESIZE_HQVGA},
    {"320x240", FRAMESIZE_QVGA},
    {"400x296", FRAMESIZE_CIF},
    {"480x320", FRAMESIZE_HVGA},
    {"640x480", FRAMESIZE_VGA},
    {"800x600", FRAMESIZE_SVGA},
    {"1024x768", FRAMESIZE_XGA},
    {"1280x720", FRAMESIZE_HD},
    {"1280x1024", FRAMESIZE_SXGA},
    {"1600x1200", FRAMESIZE_UXGA},
};

// Camera runtime settings
#define CAMERA_STARTUP_SELF_TEST 1
#define CAMERA_STREAM_FRAME_SIZE FRAMESIZE_VGA
#define CAMERA_STREAM_JPEG_QUALITY 8
#define CAMERA_FB_COUNT 1
#define CAMERA_GRAB_MODE CAMERA_GRAB_LATEST

// /capture latest-frame behavior: drop stale frames before final capture
#define CAMERA_CAPTURE_LATEST_DROP_COUNT 2
#define CAMERA_CAPTURE_RETRY_COUNT 3
#define CAMERA_CAPTURE_RETRY_DELAY_MS 30

#define CAMERA_INIT_FRAME_SIZE CAMERA_STREAM_FRAME_SIZE

// Sensor tuning
#define CAMERA_TUNE_BRIGHTNESS 0
#define CAMERA_TUNE_CONTRAST 0
#define CAMERA_TUNE_SATURATION 0
#define CAMERA_TUNE_SHARPNESS 1
#define CAMERA_TUNE_DENOISE 1

// Pin and clock mapping
#define CAM_EXTERNAL_XCLK_OSC 1

// WROVER-KIT PIN Map
#ifdef BOARD_WROVER_KIT
#define CAM_PIN_PWDN -1  //power down is not used
#define CAM_PIN_RESET -1 //software reset will be performed
#define CAM_PIN_XCLK 21
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27
#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 19
#define CAM_PIN_D2 18
#define CAM_PIN_D1 5
#define CAM_PIN_D0 4
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22
#elif defined(BOARD_ESP32CAM_AITHINKER)
// ESP32Cam (AiThinker) PIN Map
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1 //software reset will be performed
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27
#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22
#elif defined(BOARD_ESP32S3_WROOM)
// ESP32S3 (WROOM) PIN Map
#define CAM_PIN_PWDN 38
#define CAM_PIN_RESET -1   //software reset will be performed
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_D0 11
#define CAM_PIN_D1 9
#define CAM_PIN_D2 8
#define CAM_PIN_D3 10
#define CAM_PIN_D4 12
#define CAM_PIN_D5 18
#define CAM_PIN_D6 17
#define CAM_PIN_D7 16
#elif defined(BOARD_ESP32S3_GOOUUU)
// ESP32S3 (GOOUU TECH)
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1   //software reset will be performed
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_D0 11
#define CAM_PIN_D1 9
#define CAM_PIN_D2 8
#define CAM_PIN_D3 10
#define CAM_PIN_D4 12
#define CAM_PIN_D5 18
#define CAM_PIN_D6 17
#define CAM_PIN_D7 16
#elif defined(BOARD_ESP32S3_XIAO)
// ESP32S3 (XIAO)
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1   //software reset will be performed
#define CAM_PIN_VSYNC 38
#define CAM_PIN_HREF 47
#define CAM_PIN_PCLK 13
#define CAM_PIN_XCLK 10
#define CAM_PIN_SIOD 40
#define CAM_PIN_SIOC 39
#define CAM_PIN_D0 15
#define CAM_PIN_D1 17
#define CAM_PIN_D2 18
#define CAM_PIN_D3 16
#define CAM_PIN_D4 14
#define CAM_PIN_D5 12
#define CAM_PIN_D6 11
#define CAM_PIN_D7 48
#else
// Default PIN Map (custom) - modify these values for your hardware
#define CAM_PIN_PWDN 12
#define CAM_PIN_RESET 40
#if CAM_EXTERNAL_XCLK_OSC
#define CAM_PIN_XCLK -1
#define CAM_XCLK_FREQ_HZ 12000000
#else
#define CAM_PIN_XCLK 10
#define CAM_XCLK_FREQ_HZ 10000000
#endif
#define CAM_PIN_SIOD 16
#define CAM_PIN_SIOC 15
#define CAM_PIN_D7 21
#define CAM_PIN_D6 10
#define CAM_PIN_D5 47
#define CAM_PIN_D4 9
#define CAM_PIN_D3 38
#define CAM_PIN_D2 18
#define CAM_PIN_D1 39
#define CAM_PIN_D0 17
#define CAM_PIN_VSYNC 42
#define CAM_PIN_HREF 41
#define CAM_PIN_PCLK 11
#endif