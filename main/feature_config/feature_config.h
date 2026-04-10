#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "mimi_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RGB control is mandatory in minimal profile. */
bool mimi_feature_rgb_control_enabled(void);
bool mimi_feature_telegram_bot_enabled(void);
bool mimi_feature_feishu_bot_enabled(void);

/* Set feature configuration in NVS. */
esp_err_t mimi_set_feature_telegram_bot(bool enabled);
esp_err_t mimi_set_feature_feishu_bot(bool enabled);

#ifdef __cplusplus
}
#endif