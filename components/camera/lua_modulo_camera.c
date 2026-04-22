#include "lua_modulo_camera.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "img_converters.h"
#include "lauxlib.h"
#include "mbedtls/base64.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_http_server.h"

// Camera core API declarations
esp_err_t camera_core_init(void);
esp_err_t camera_core_acquire_fb(camera_fb_t **out_fb,
                                 int retry_count,
                                 int delay_ms,
                                 TickType_t lock_timeout_ticks);
esp_err_t camera_core_acquire_fb_latest(camera_fb_t **out_fb,
                                        TickType_t lock_timeout_ticks);
void camera_core_release_fb(camera_fb_t *fb);

static const char *TAG = "lua_camera";
static const char *TAG_SERVER = "cam_server";
static httpd_handle_t s_server = NULL;

static esp_err_t http_get_capture(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t acq = camera_core_acquire_fb_latest(&fb, pdMS_TO_TICKS(1500));
    if (acq != ESP_OK || fb == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera frame unavailable");
        return ESP_FAIL;
    }

    if (fb->format != PIXFORMAT_JPEG) {
        camera_core_release_fb(fb);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not in jpeg mode");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");

    esp_err_t ret = httpd_resp_send(req, (const char *)fb->buf, (ssize_t)fb->len);
    camera_core_release_fb(fb);
    return ret;
}

esp_err_t camera_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 18787; // MIMI_CAMERA_SERVER_PORT
    config.ctrl_port = 18787 + 1;
    config.max_uri_handlers = 4;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SERVER, "Failed to start camera server: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = http_get_capture,
    };

    ret = httpd_register_uri_handler(s_server, &capture_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SERVER, "Failed to register /capture: %s", esp_err_to_name(ret));
        httpd_stop(s_server);
        s_server = NULL;
        return ret;
    }

    ESP_LOGI(TAG_SERVER, "Camera server started on port %d, endpoint /capture", 18787);
    return ESP_OK;
}

esp_err_t camera_server_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG_SERVER, "Camera server stopped");
    }

    return ESP_OK;
}

static int lua_camera_server_start(lua_State *L)
{
    esp_err_t ret = camera_server_start();
    if (ret != ESP_OK) {
        return luaL_error(L, "camera_server.start failed: %s", esp_err_to_name(ret));
    }
    lua_pushboolean(L, true);
    return 1;
}

static int lua_camera_server_stop(lua_State *L)
{
    esp_err_t ret = camera_server_stop();
    if (ret != ESP_OK) {
        return luaL_error(L, "camera_server.stop failed: %s", esp_err_to_name(ret));
    }
    lua_pushboolean(L, true);
    return 1;
}

#define CAMERA_STARTUP_SELF_TEST 1
#define CAMERA_STREAM_FRAME_SIZE FRAMESIZE_VGA
#define CAMERA_STREAM_JPEG_QUALITY 8
#define CAMERA_FB_COUNT 1
#define CAMERA_GRAB_MODE CAMERA_GRAB_LATEST

#define CAMERA_CAPTURE_LATEST_DROP_COUNT 2
#define CAMERA_CAPTURE_RETRY_COUNT 3
#define CAMERA_CAPTURE_RETRY_DELAY_MS 30

#define CAMERA_INIT_FRAME_SIZE CAMERA_STREAM_FRAME_SIZE

#define CAMERA_TUNE_BRIGHTNESS 0
#define CAMERA_TUNE_CONTRAST 0
#define CAMERA_TUNE_SATURATION 0
#define CAMERA_TUNE_SHARPNESS 1
#define CAMERA_TUNE_DENOISE 1

#define CAM_EXTERNAL_XCLK_OSC 1

typedef struct {
    const char *name;
    framesize_t value;
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

#define CAMERA_LUA_CAPTURE_BUF_MAX (512 * 1024)

static camera_config_t s_camera_config = {
    .pin_pwdn = -1,
    .pin_reset = -1,
    .pin_xclk = -1,
    .pin_sccb_sda = -1,
    .pin_sccb_scl = -1,
    .pin_d7 = -1,
    .pin_d6 = -1,
    .pin_d5 = -1,
    .pin_d4 = -1,
    .pin_d3 = -1,
    .pin_d2 = -1,
    .pin_d1 = -1,
    .pin_d0 = -1,
    .pin_vsync = -1,
    .pin_href = -1,
    .pin_pclk = -1,
    .xclk_freq_hz = 10000000,
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

static const char* frame_size_to_str(framesize_t size)
{
    for (int i = 0; i < sizeof(frame_size_map)/sizeof(frame_map_t); i++) {
        if (frame_size_map[i].value == size) {
            return frame_size_map[i].name;
        }
    }
    return "UNKNOWN";
}

esp_err_t camera_core_init(void)
{
    s_camera_config.pin_pwdn = 12;
    s_camera_config.pin_reset = 40;
    #if CAM_EXTERNAL_XCLK_OSC
    s_camera_config.pin_xclk = -1;
    s_camera_config.xclk_freq_hz = 12000000;
    #else
    s_camera_config.pin_xclk = 10;
    s_camera_config.xclk_freq_hz = 10000000;
    #endif
    s_camera_config.pin_sccb_sda = 16;
    s_camera_config.pin_sccb_scl = 15;
    s_camera_config.pin_d7 = 21;
    s_camera_config.pin_d6 = 10;
    s_camera_config.pin_d5 = 47;
    s_camera_config.pin_d4 = 9;
    s_camera_config.pin_d3 = 38;
    s_camera_config.pin_d2 = 18;
    s_camera_config.pin_d1 = 39;
    s_camera_config.pin_d0 = 17;
    s_camera_config.pin_vsync = 42;
    s_camera_config.pin_href = 41;
    s_camera_config.pin_pclk = 11;

    ESP_LOGI(TAG,
             "Camera init frame_size=%s",
             frame_size_to_str(s_camera_config.frame_size));

    esp_err_t err = esp_camera_init(&s_camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        ESP_LOGI(TAG, "Sensor detected PID=0x%04X", s->id.PID);
        s->set_framesize(s, s_camera_config.frame_size);
        s->set_quality(s, s_camera_config.jpeg_quality);
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

static esp_err_t camera_core_set_option(const char *key, int value)
{
    if (strcmp(key, "frame_size") == 0) {
        if (value >= 0 && value < sizeof(frame_size_map)/sizeof(frame_map_t)) {
            s_camera_config.frame_size = frame_size_map[value].value;
            return ESP_OK;
        }
    } else if (strcmp(key, "jpeg_quality") == 0) {
        if (value >= 1 && value <= 63) {
            s_camera_config.jpeg_quality = value;
            return ESP_OK;
        }
    } else if (strcmp(key, "xclk_freq_hz") == 0) {
        s_camera_config.xclk_freq_hz = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_pwdn") == 0) {
        s_camera_config.pin_pwdn = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_reset") == 0) {
        s_camera_config.pin_reset = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_xclk") == 0) {
        s_camera_config.pin_xclk = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_siod") == 0) {
        s_camera_config.pin_sccb_sda = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_sioc") == 0) {
        s_camera_config.pin_sccb_scl = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_d7") == 0) {
        s_camera_config.pin_d7 = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_d6") == 0) {
        s_camera_config.pin_d6 = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_d5") == 0) {
        s_camera_config.pin_d5 = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_d4") == 0) {
        s_camera_config.pin_d4 = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_d3") == 0) {
        s_camera_config.pin_d3 = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_d2") == 0) {
        s_camera_config.pin_d2 = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_d1") == 0) {
        s_camera_config.pin_d1 = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_d0") == 0) {
        s_camera_config.pin_d0 = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_vsync") == 0) {
        s_camera_config.pin_vsync = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_href") == 0) {
        s_camera_config.pin_href = value;
        return ESP_OK;
    } else if (strcmp(key, "pin_pclk") == 0) {
        s_camera_config.pin_pclk = value;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

static int lua_camera_configure(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "frame_size");
    if (!lua_isnil(L, -1)) {
        int frame_size = (int)luaL_checkinteger(L, -1);
        if (camera_core_set_option("frame_size", frame_size) != ESP_OK) {
            lua_pop(L, 1);
            return luaL_error(L, "camera_core.configure failed: invalid frame_size");
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "jpeg_quality");
    if (!lua_isnil(L, -1)) {
        int jpeg_quality = (int)luaL_checkinteger(L, -1);
        if (camera_core_set_option("jpeg_quality", jpeg_quality) != ESP_OK) {
            lua_pop(L, 1);
            return luaL_error(L, "camera_core.configure failed: invalid jpeg_quality");
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "xclk_freq_hz");
    if (!lua_isnil(L, -1)) {
        int xclk = (int)luaL_checkinteger(L, -1);
        if (camera_core_set_option("xclk_freq_hz", xclk) != ESP_OK) {
            lua_pop(L, 1);
            return luaL_error(L, "camera_core.configure failed: invalid xclk_freq_hz");
        }
    }
    lua_pop(L, 1);

    static const char *pin_keys[] = {
        "pin_pwdn", "pin_reset", "pin_xclk", "pin_siod", "pin_sioc",
        "pin_d7", "pin_d6", "pin_d5", "pin_d4", "pin_d3", "pin_d2", "pin_d1", "pin_d0",
        "pin_vsync", "pin_href", "pin_pclk",
    };

    for (size_t i = 0; i < sizeof(pin_keys) / sizeof(pin_keys[0]); i++) {
        lua_getfield(L, 1, pin_keys[i]);
        if (!lua_isnil(L, -1)) {
            int v = (int)luaL_checkinteger(L, -1);
            if (camera_core_set_option(pin_keys[i], v) != ESP_OK) {
                lua_pop(L, 1);
                return luaL_error(L, "camera_core.configure failed: invalid %s", pin_keys[i]);
            }
        }
        lua_pop(L, 1);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int lua_camera_init(lua_State *L)
{
    (void)L;
    esp_err_t err = camera_core_init();
    if (err != ESP_OK) {
        return luaL_error(L, "camera_core.init failed: %s", esp_err_to_name(err));
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_camera_capture(lua_State *L)
{
    bool encode_base64 = true;
    const char *save_path = "/spiffs/capture.jpg";
    if (!lua_isnoneornil(L, 1)) {
        encode_base64 = lua_toboolean(L, 1);
    }
    if (!lua_isnoneornil(L, 2)) {
        save_path = luaL_checkstring(L, 2);
    }

    esp_err_t init_err = camera_core_init();
    if (init_err != ESP_OK) {
        return luaL_error(L, "camera_core.init failed: %s", esp_err_to_name(init_err));
    }

    camera_fb_t *fb = NULL;
    esp_err_t acq = camera_core_acquire_fb_latest(&fb, pdMS_TO_TICKS(1500));
    if (acq != ESP_OK || fb == NULL) {
        return luaL_error(L, "camera_capture failed: %s", esp_err_to_name(acq));
    }

    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;
    bool converted = false;

    if (fb->format == PIXFORMAT_JPEG) {
        jpg_buf = fb->buf;
        jpg_len = fb->len;
    } else {
        if (!fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format, 80, &jpg_buf, &jpg_len)) {
            camera_core_release_fb(fb);
            return luaL_error(L, "camera_capture: jpeg encode failed");
        }
        converted = true;
    }

    // Save to file
    FILE *f = fopen(save_path, "wb");
    if (f) {
        fwrite(jpg_buf, 1, jpg_len, f);
        fclose(f);
        ESP_LOGI(TAG, "Camera capture saved to file: %s (%u bytes)", save_path, (unsigned)jpg_len);
    } else {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", save_path);
    }

    if (!encode_base64) {
        lua_pushlstring(L, (const char *)jpg_buf, jpg_len);
        if (converted) {
            free(jpg_buf);
        }
        camera_core_release_fb(fb);
        return 1;
    }

    unsigned char *b64 = malloc(CAMERA_LUA_CAPTURE_BUF_MAX);
    if (!b64) {
        if (converted) {
            free(jpg_buf);
        }
        camera_core_release_fb(fb);
        return luaL_error(L, "camera_capture: out of memory");
    }

    size_t out_len = 0;
    int rc = mbedtls_base64_encode(b64, CAMERA_LUA_CAPTURE_BUF_MAX, &out_len,
                                   (const unsigned char *)jpg_buf, jpg_len);
    if (rc != 0) {
        free(b64);
        if (converted) {
            free(jpg_buf);
        }
        camera_core_release_fb(fb);
        return luaL_error(L, "camera_capture: base64 encode failed rc=%d", rc);
    }

    lua_newtable(L);
    lua_pushstring(L, "image/jpeg");
    lua_setfield(L, -2, "media_type");
    lua_pushlstring(L, (const char *)b64, out_len);
    lua_setfield(L, -2, "data");
    lua_pushinteger(L, (lua_Integer)jpg_len);
    lua_setfield(L, -2, "jpeg_bytes");
    lua_pushinteger(L, fb->width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, fb->height);
    lua_setfield(L, -2, "height");
    lua_pushstring(L, save_path);
    lua_setfield(L, -2, "file_path");

    free(b64);
    if (converted) {
        free(jpg_buf);
    }
    camera_core_release_fb(fb);

    ESP_LOGI(TAG, "Lua camera capture done, jpeg=%u", (unsigned)jpg_len);
    return 1;
}

int luaopen_camera(lua_State *L)
{
    static const luaL_Reg funcs[] = {
        {"init", lua_camera_init},
        {"configure", lua_camera_configure},
        {"capture", lua_camera_capture},
        {NULL, NULL},
    };

    static const luaL_Reg server_funcs[] = {
        {"start", lua_camera_server_start},
        {"stop", lua_camera_server_stop},
        {NULL, NULL},
    };

    lua_newtable(L);
    luaL_setfuncs(L, funcs, 0);

    // Create server table
    lua_newtable(L);
    luaL_setfuncs(L, server_funcs, 0);
    lua_setfield(L, -2, "server");

    return 1;
}

int luaopen_modulo_camera(lua_State *L)
{
    return luaopen_camera(L);
}

void lua_register_modulo_camera_lib(lua_State *L)
{
    luaL_requiref(L, "modulo_camera", luaopen_modulo_camera, 1);
    lua_setglobal(L, "modulo_camera");
    luaL_requiref(L, "camera", luaopen_camera, 1);
    lua_setglobal(L, "camera");
}
