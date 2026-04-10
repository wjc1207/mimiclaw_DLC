#include "feature_config.h"
#include "mimi_config.h"
#include "camera_core/camera_config.h"
#include "sdkconfig.h"

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
#if !CONFIG_MIMI_TOOL_RGB_ENABLED
    return false;
#endif
    return get_feature_bool(MIMI_NVS_KEY_RGB_CONTROL, MIMI_FEATURE_RGB_CONTROL);
}

bool mimi_feature_camera_tool_enabled(void)
{
#if !CONFIG_MIMI_TOOL_CAMERA_ENABLED
    return false;
#endif
    return get_feature_bool(MIMI_NVS_KEY_CAMERA_TOOL, MIMI_FEATURE_CAMERA_TOOL);
}

bool mimi_feature_ble_tool_enabled(void)
{
#if !CONFIG_MIMI_TOOL_BLE_ENABLED
    return false;
#endif
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

int mimi_camera_frame_size(void)
{
    int default_val = CAMERA_STREAM_FRAME_SIZE;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t frame_size = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAMERA_FRAME_SIZE, &frame_size) == ESP_OK) {
            default_val = (int)frame_size;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_camera_jpeg_quality(void)
{
    int default_val = CAMERA_STREAM_JPEG_QUALITY;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t quality = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAMERA_JPEG_QUALITY, &quality) == ESP_OK) {
            default_val = (int)quality;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_pwdn(void)
{
    int default_val = CAM_PIN_PWDN;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_PWDN, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_reset(void)
{
    int default_val = CAM_PIN_RESET;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_RESET, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_xclk(void)
{
    int default_val = CAM_PIN_XCLK;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_XCLK, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_siod(void)
{
    int default_val = CAM_PIN_SIOD;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_SIOD, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_sioc(void)
{
    int default_val = CAM_PIN_SIOC;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_SIOC, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_d7(void)
{
    int default_val = CAM_PIN_D7;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D7, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_d6(void)
{
    int default_val = CAM_PIN_D6;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D6, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_d5(void)
{
    int default_val = CAM_PIN_D5;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D5, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_d4(void)
{
    int default_val = CAM_PIN_D4;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D4, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_d3(void)
{
    int default_val = CAM_PIN_D3;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D3, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_d2(void)
{
    int default_val = CAM_PIN_D2;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D2, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_d1(void)
{
    int default_val = CAM_PIN_D1;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D1, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_d0(void)
{
    int default_val = CAM_PIN_D0;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D0, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_vsync(void)
{
    int default_val = CAM_PIN_VSYNC;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_VSYNC, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_href(void)
{
    int default_val = CAM_PIN_HREF;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_HREF, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_pin_pclk(void)
{
    int default_val = CAM_PIN_PCLK;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t pin = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_PIN_PCLK, &pin) == ESP_OK) {
            default_val = (int)pin;
        }
        nvs_close(nvs);
    }

    return default_val;
}

int mimi_cam_xclk_freq(void)
{
    int default_val = CAM_XCLK_FREQ_HZ;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEATURE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t freq = 0;
        if (nvs_get_i32(nvs, MIMI_NVS_KEY_CAM_XCLK_FREQ, &freq) == ESP_OK) {
            default_val = (int)freq;
        }
        nvs_close(nvs);
    }

    return default_val;
}

esp_err_t mimi_set_cam_xclk_freq(int freq)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_XCLK_FREQ, (int32_t)freq);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
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

esp_err_t mimi_set_camera_frame_size(int frame_size)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAMERA_FRAME_SIZE, (int32_t)frame_size);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_camera_jpeg_quality(int quality)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAMERA_JPEG_QUALITY, (int32_t)quality);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_pwdn(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_PWDN, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_reset(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_RESET, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_xclk(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_XCLK, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_siod(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_SIOD, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_sioc(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_SIOC, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_d7(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D7, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_d6(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D6, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_d5(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D5, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_d4(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D4, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_d3(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D3, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_d2(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D2, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_d1(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D1, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_d0(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_D0, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_vsync(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_VSYNC, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_href(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_HREF, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t mimi_set_cam_pin_pclk(int pin)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_FEATURE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs, MIMI_NVS_KEY_CAM_PIN_PCLK, (int32_t)pin);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}