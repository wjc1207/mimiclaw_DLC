#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    float temperature_c;
    float humidity_percent;
    int battery_percent;
    bool temperature_valid;
    bool humidity_valid;
    bool battery_valid;
    bool encrypted;
    char source_addr[18];
    uint32_t last_seen_ms;
} thome_reading_t;

esp_err_t bthome_listener_start(const char *target_addr);
bool bthome_listener_get_latest(thome_reading_t *out);