#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "mimi_config.h"

typedef enum {
    WIFI_ONBOARD_MODE_CAPTIVE = 0,
    WIFI_ONBOARD_MODE_ADMIN,
} wifi_onboard_mode_t;

/**
 * Start WiFi onboarding/configuration portal.
 * CAPTIVE mode opens DNS hijack + config page and blocks forever.
 * ADMIN mode keeps a local config hotspot alive without captive redirects.
 */
esp_err_t wifi_onboard_start(wifi_onboard_mode_t mode);

/* Feature config APIs (migrated from feature_config.h). */
bool mimi_feature_rgb_control_enabled(void);
bool mimi_feature_camera_tool_enabled(void);
bool mimi_feature_camera_server_enabled(void);
bool mimi_feature_ble_tool_enabled(void);
bool mimi_feature_telegram_bot_enabled(void);
bool mimi_feature_feishu_bot_enabled(void);

const char *mimi_ble_target_addr(void);

int mimi_camera_frame_size(void);
int mimi_camera_jpeg_quality(void);

int mimi_cam_pin_pwdn(void);
int mimi_cam_pin_reset(void);
int mimi_cam_pin_xclk(void);
int mimi_cam_pin_siod(void);
int mimi_cam_pin_sioc(void);
int mimi_cam_pin_d7(void);
int mimi_cam_pin_d6(void);
int mimi_cam_pin_d5(void);
int mimi_cam_pin_d4(void);
int mimi_cam_pin_d3(void);
int mimi_cam_pin_d2(void);
int mimi_cam_pin_d1(void);
int mimi_cam_pin_d0(void);
int mimi_cam_pin_vsync(void);
int mimi_cam_pin_href(void);
int mimi_cam_pin_pclk(void);
int mimi_cam_xclk_freq(void);
esp_err_t mimi_set_cam_xclk_freq(int freq);

esp_err_t mimi_set_feature_rgb_control(bool enabled);
esp_err_t mimi_set_feature_camera_tool(bool enabled);
esp_err_t mimi_set_feature_camera_server(bool enabled);
esp_err_t mimi_set_feature_ble_tool(bool enabled);
esp_err_t mimi_set_feature_telegram_bot(bool enabled);
esp_err_t mimi_set_feature_feishu_bot(bool enabled);
esp_err_t mimi_set_ble_target_addr(const char *addr);
esp_err_t mimi_set_camera_frame_size(int frame_size);
esp_err_t mimi_set_camera_jpeg_quality(int quality);
esp_err_t mimi_set_cam_pin_pwdn(int pin);
esp_err_t mimi_set_cam_pin_reset(int pin);
esp_err_t mimi_set_cam_pin_xclk(int pin);
esp_err_t mimi_set_cam_pin_siod(int pin);
esp_err_t mimi_set_cam_pin_sioc(int pin);
esp_err_t mimi_set_cam_pin_d7(int pin);
esp_err_t mimi_set_cam_pin_d6(int pin);
esp_err_t mimi_set_cam_pin_d5(int pin);
esp_err_t mimi_set_cam_pin_d4(int pin);
esp_err_t mimi_set_cam_pin_d3(int pin);
esp_err_t mimi_set_cam_pin_d2(int pin);
esp_err_t mimi_set_cam_pin_d1(int pin);
esp_err_t mimi_set_cam_pin_d0(int pin);
esp_err_t mimi_set_cam_pin_vsync(int pin);
esp_err_t mimi_set_cam_pin_href(int pin);
esp_err_t mimi_set_cam_pin_pclk(int pin);
