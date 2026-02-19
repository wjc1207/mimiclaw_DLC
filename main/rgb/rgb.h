#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rgb_init(void);
void rgb_set(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif
