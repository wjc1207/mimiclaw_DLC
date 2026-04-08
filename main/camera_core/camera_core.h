#pragma once

#include "esp_err.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"

esp_err_t camera_core_init(void);
esp_err_t camera_core_acquire_fb(camera_fb_t **out_fb,
                                 int retry_count,
                                 int delay_ms,
                                 TickType_t lock_timeout_ticks);
esp_err_t camera_core_acquire_fb_latest(camera_fb_t **out_fb,
                                        TickType_t lock_timeout_ticks);
void camera_core_release_fb(camera_fb_t *fb);