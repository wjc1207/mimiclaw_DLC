#pragma once

#include "esp_camera.h"

// Camera runtime settings
#define CAMERA_STARTUP_SELF_TEST 0
#define CAMERA_STREAM_FRAME_SIZE FRAMESIZE_VGA
#define CAMERA_STREAM_JPEG_QUALITY 8
#define CAMERA_FB_COUNT 1
#define CAMERA_GRAB_MODE CAMERA_GRAB_WHEN_EMPTY

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