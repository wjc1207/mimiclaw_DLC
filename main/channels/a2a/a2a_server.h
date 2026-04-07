#pragma once

#include "esp_err.h"

esp_err_t a2a_server_start(void);

esp_err_t a2a_server_handle_agent_reply(const char *session_key, const char *text);
