#include "camera_core.h"

#include <esp_log.h>

#include "freertos/task.h"
#include "freertos/semphr.h"

#include "camera_config.h"

static const char *TAG = "camera";

static camera_config_t s_camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,
    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,
    .xclk_freq_hz = CAM_XCLK_FREQ_HZ,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = CAMERA_INIT_FRAME_SIZE,
    .jpeg_quality = CAMERA_STREAM_JPEG_QUALITY,
    .fb_count = CAMERA_FB_COUNT,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_MODE,
};

static SemaphoreHandle_t s_camera_mutex;

static camera_fb_t *camera_fb_get_retry(int retry_count, int delay_ms)
{
    for (int i = 0; i < retry_count; ++i) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb && fb->len > 0) {
            return fb;
        }

        if (fb) {
            ESP_LOGW(TAG, "Invalid frame len=0 on attempt %d", i + 1);
            esp_camera_fb_return(fb);
        } else {
            ESP_LOGW(TAG, "Failed to get frame: timeout (attempt %d)", i + 1);
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    return NULL;
}

static void apply_sensor_tuning(sensor_t *s)
{
    if (!s) {
        return;
    }

    if (s->set_brightness) {
        s->set_brightness(s, CAMERA_TUNE_BRIGHTNESS);
    }
    if (s->set_contrast) {
        s->set_contrast(s, CAMERA_TUNE_CONTRAST);
    }
    if (s->set_saturation) {
        s->set_saturation(s, CAMERA_TUNE_SATURATION);
    }
    if (s->set_sharpness) {
        s->set_sharpness(s, CAMERA_TUNE_SHARPNESS);
    }
    if (s->set_denoise) {
        s->set_denoise(s, CAMERA_TUNE_DENOISE);
    }
    if (s->set_whitebal) {
        s->set_whitebal(s, 1);
    }
    if (s->set_awb_gain) {
        s->set_awb_gain(s, 1);
    }
    if (s->set_gain_ctrl) {
        s->set_gain_ctrl(s, 1);
    }
    if (s->set_exposure_ctrl) {
        s->set_exposure_ctrl(s, 1);
    }

    ESP_LOGI(TAG,
             "Sensor tuning applied: sharpness=%d contrast=%d quality=%d auto_exposure=1 auto_gain=1",
             CAMERA_TUNE_SHARPNESS,
             CAMERA_TUNE_CONTRAST,
             CAMERA_STREAM_JPEG_QUALITY);
}

esp_err_t camera_core_init(void)
{
    if (s_camera_config.pin_xclk < 0) {
        ESP_LOGW(TAG, "XCLK pin disabled. Make sure camera module has onboard oscillator.");
    } else {
        ESP_LOGI(TAG, "Using ESP32 output XCLK on GPIO %d @ %d Hz", s_camera_config.pin_xclk, s_camera_config.xclk_freq_hz);
    }

    if (s_camera_config.pin_pwdn < 0) {
        ESP_LOGW(TAG, "PWDN pin is not controlled by ESP. Ensure OV_PWDN is hard-wired LOW.");
    }
    if (s_camera_config.pin_reset < 0) {
        ESP_LOGW(TAG, "RESET pin is not controlled by ESP. Ensure OV_RESET is hard-wired HIGH.");
    }

    ESP_LOGI(TAG,
             "Camera init frame_size=%d frame_size=%d",
             CAMERA_INIT_FRAME_SIZE,
             CAMERA_STREAM_FRAME_SIZE);

    esp_err_t err = esp_camera_init(&s_camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        ESP_LOGI(TAG, "Sensor detected PID=0x%04X", s->id.PID);
        s->set_framesize(s, CAMERA_STREAM_FRAME_SIZE);
        s->set_quality(s, CAMERA_STREAM_JPEG_QUALITY);
        apply_sensor_tuning(s);
    }

    if (s_camera_mutex == NULL) {
        s_camera_mutex = xSemaphoreCreateMutex();
        if (s_camera_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create camera mutex");
            esp_camera_deinit();
            return ESP_ERR_NO_MEM;
        }
    }

#if CAMERA_STARTUP_SELF_TEST
    camera_fb_t *test_fb = camera_fb_get_retry(2, 40);
    if (!test_fb) {
        ESP_LOGW(TAG, "Startup frame self-test failed. Check PCLK/VSYNC/HREF wiring and XCLK source.");
    } else {
        ESP_LOGI(TAG, "Startup frame self-test OK: %ux%u len=%u", test_fb->width, test_fb->height, (unsigned)test_fb->len);
        esp_camera_fb_return(test_fb);
    }
#endif

    return ESP_OK;
}

esp_err_t camera_core_acquire_fb(camera_fb_t **out_fb,
                                 int retry_count,
                                 int delay_ms,
                                 TickType_t lock_timeout_ticks)
{
    if (out_fb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_fb = NULL;

    if (s_camera_mutex == NULL || xSemaphoreTake(s_camera_mutex, lock_timeout_ticks) != pdTRUE) {
        ESP_LOGW(TAG, "Timed out waiting for camera mutex");
        return ESP_ERR_TIMEOUT;
    }

    camera_fb_t *fb = camera_fb_get_retry(retry_count, delay_ms);
    if (fb == NULL) {
        xSemaphoreGive(s_camera_mutex);
        return ESP_FAIL;
    }

    *out_fb = fb;
    return ESP_OK;
}

esp_err_t camera_core_acquire_fb_latest(camera_fb_t **out_fb,
                                        TickType_t lock_timeout_ticks)
{
    if (out_fb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_fb = NULL;

    if (s_camera_mutex == NULL || xSemaphoreTake(s_camera_mutex, lock_timeout_ticks) != pdTRUE) {
        ESP_LOGW(TAG, "Timed out waiting for camera mutex");
        return ESP_ERR_TIMEOUT;
    }

    for (int i = 0; i < CAMERA_CAPTURE_LATEST_DROP_COUNT; ++i) {
        camera_fb_t *stale_fb = camera_fb_get_retry(1, CAMERA_CAPTURE_RETRY_DELAY_MS);
        if (stale_fb == NULL) {
            ESP_LOGW(TAG, "Failed to drop stale frame %d", i + 1);
            break;
        }
        esp_camera_fb_return(stale_fb);
    }

    camera_fb_t *fb = camera_fb_get_retry(CAMERA_CAPTURE_RETRY_COUNT,
                                          CAMERA_CAPTURE_RETRY_DELAY_MS);
    if (fb == NULL) {
        xSemaphoreGive(s_camera_mutex);
        return ESP_FAIL;
    }

    *out_fb = fb;
    return ESP_OK;
}

void camera_core_release_fb(camera_fb_t *fb)
{
    if (fb != NULL) {
        esp_camera_fb_return(fb);
    }

    if (s_camera_mutex != NULL) {
        xSemaphoreGive(s_camera_mutex);
    }
}