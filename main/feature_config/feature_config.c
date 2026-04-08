#include "feature_config.h"
#include "mimi_config.h"

#include "esp_log.h"
#include "nvs_flash.h"

static bool get_feature_bool(const char *nvs_key, bool default_val)
{
    bool value = default_val;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t bool_val = 0;
        if (nvs_get_u8(nvs, nvs_key, &bool_val) == ESP_OK) {
            value = bool_val ? true : false;
        }
        nvs_close(nvs);
    }

    return value;
}

static esp_err_t set_feature_bool(const char *nvs_key, bool value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(nvs, nvs_key, value ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

static const char *get_feature_str(const char *nvs_key, const char *default_val)
{
    static char value[18] = {0}; // 17 chars for MAC address + null terminator
    strlcpy(value, default_val, sizeof(value));

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(value);
        if (nvs_get_str(nvs, nvs_key, value, &len) == ESP_OK) {
            // Value successfully read from NVS
        }
        nvs_close(nvs);
    }

    return value;
}

static esp_err_t set_feature_str(const char *nvs_key, const char *value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, nvs_key, value);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

bool mimi_feature_rgb_control_enabled(void)
{
    return get_feature_bool(MIMI_NVS_KEY_RGB_CONTROL, MIMI_FEATURE_RGB_CONTROL);
}

bool mimi_feature_camera_tool_enabled(void)
{
    return get_feature_bool(MIMI_NVS_KEY_CAMERA_TOOL, MIMI_FEATURE_CAMERA_TOOL);
}

bool mimi_feature_ble_tool_enabled(void)
{
    return get_feature_bool(MIMI_NVS_KEY_BLE_TOOL, MIMI_FEATURE_BLE_TOOL);
}

bool mimi_feature_telegram_bot_enabled(void)
{
    return get_feature_bool(MIMI_NVS_KEY_TELEGRAM_BOT, MIMI_FEATURE_TELEGRAM_BOT);
}

bool mimi_feature_feishu_bot_enabled(void)
{
    return get_feature_bool(MIMI_NVS_KEY_FEISHU_BOT, MIMI_FEATURE_FEISHU_BOT);
}

const char *mimi_ble_target_addr(void)
{
    return get_feature_str(MIMI_NVS_KEY_BLE_TARGET_ADDR, MIMI_BLE_TARGET_ADDR);
}

esp_err_t mimi_set_feature_rgb_control(bool enabled)
{
    return set_feature_bool(MIMI_NVS_KEY_RGB_CONTROL, enabled);
}

esp_err_t mimi_set_feature_camera_tool(bool enabled)
{
    return set_feature_bool(MIMI_NVS_KEY_CAMERA_TOOL, enabled);
}

esp_err_t mimi_set_feature_ble_tool(bool enabled)
{
    return set_feature_bool(MIMI_NVS_KEY_BLE_TOOL, enabled);
}

esp_err_t mimi_set_feature_telegram_bot(bool enabled)
{
    return set_feature_bool(MIMI_NVS_KEY_TELEGRAM_BOT, enabled);
}

esp_err_t mimi_set_feature_feishu_bot(bool enabled)
{
    return set_feature_bool(MIMI_NVS_KEY_FEISHU_BOT, enabled);
}

esp_err_t mimi_set_ble_target_addr(const char *addr)
{
    return set_feature_str(MIMI_NVS_KEY_BLE_TARGET_ADDR, addr);
}