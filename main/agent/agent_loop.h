#pragma once

#include "esp_err.h"

/**
 * Initialize the agent loop.
 */
esp_err_t agent_loop_init(void);

/**
 * Start the agent loop task (runs on Core 1).
 * Consumes from inbound queue, calls Claude API, pushes to outbound queue.
 */
esp_err_t agent_loop_start(void);

/**
 * Get current agent state: "idle" or "running"
 */
const char *agent_loop_get_state(void);
