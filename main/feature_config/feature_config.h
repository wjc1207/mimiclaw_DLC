#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "mimi_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Get feature configuration from NVS, or fallback to default macro values.
 * Returns true if feature is enabled, false otherwise.
 */
bool mimi_feature_rgb_control_enabled(void);
bool mimi_feature_camera_tool_enabled(void);
bool mimi_feature_ble_tool_enabled(void);
bool mimi_feature_telegram_bot_enabled(void);
bool mimi_feature_feishu_bot_enabled(void);

/*
 * Get BLE target address from NVS, or fallback to MIMI_BLE_TARGET_ADDR.
 * Returns a static string that does not need to be freed.
 */
const char *mimi_ble_target_addr(void);

/*
 * Set feature configuration in NVS.
 */
esp_err_t mimi_set_feature_rgb_control(bool enabled);
esp_err_t mimi_set_feature_camera_tool(bool enabled);
esp_err_t mimi_set_feature_ble_tool(bool enabled);
esp_err_t mimi_set_feature_telegram_bot(bool enabled);
esp_err_t mimi_set_feature_feishu_bot(bool enabled);
esp_err_t mimi_set_ble_target_addr(const char *addr);

#ifdef __cplusplus
}
#endif