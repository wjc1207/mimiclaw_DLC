#include "camera_server.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include "mimi_config.h"
#include "camera_core.h"

static const char *TAG = "cam_server";

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
    config.server_port = MIMI_CAMERA_SERVER_PORT;
    config.ctrl_port = MIMI_CAMERA_SERVER_PORT + 1;
    config.max_uri_handlers = 4;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start camera server: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = http_get_capture,
    };

    ret = httpd_register_uri_handler(s_server, &capture_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /capture: %s", esp_err_to_name(ret));
        httpd_stop(s_server);
        s_server = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "Camera server started on port %d, endpoint /capture", MIMI_CAMERA_SERVER_PORT);
    return ESP_OK;
}

esp_err_t camera_server_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Camera server stopped");
    }

    return ESP_OK;
}
