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


esp_err_t mimi_set_feature_rgb_control(bool enabled);
esp_err_t mimi_set_feature_camera_tool(bool enabled);
esp_err_t mimi_set_feature_camera_server(bool enabled);
esp_err_t mimi_set_feature_ble_tool(bool enabled);
esp_err_t mimi_set_feature_telegram_bot(bool enabled);
esp_err_t mimi_set_feature_feishu_bot(bool enabled);
esp_err_t mimi_set_ble_target_addr(const char *addr);
