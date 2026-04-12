#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Execute A2A client request against local A2A server.
 *
 * Input JSON:
 * - action (required): "send" | "get" | "cancel" | "status" | "agent_card"
 * - message (required for send): message text
 * - task_id (required for get/cancel): target task id
 * - server/server_url/base_url (optional): target server
 * - timeout_ms (optional): HTTP timeout, default 10000
 *
 * The tool auto-fills local device client_id.
 * Status action returns: {"agent_state": "idle" | "running"}
 */
esp_err_t tool_a2a_client_execute(const char *input_json, char *output, size_t output_size);
