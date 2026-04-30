#pragma once

#include "buddy.h"
#include "esp_err.h"

/**
 * Start the buddy contact processing task on Core 1.
 * Consumes buddy events from ESP-NOW queue, checks contacts,
 * triggers cloud LLM match, sends Feishu notification.
 */
esp_err_t buddy_agent_start(void);

/**
 * Initialize the buddy agent (register Feishu channel, etc.).
 */
esp_err_t buddy_agent_init(void);
