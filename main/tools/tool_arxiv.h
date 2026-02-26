#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Initialize arXiv search tool (allocates cache buffer, creates mutex).
 */
esp_err_t tool_arxiv_init(void);

/**
 * Execute an arXiv keyword search.
 *
 * @param input_json   JSON string with fields:
 *                       "keywords"    (string, required) – search terms
 *                       "start"       (integer, optional) – pagination offset, default 0
 *                       "max_results" (integer, optional) – results to return, default 5
 * @param output       Output buffer for formatted results
 * @param output_size  Size of output buffer
 * @return ESP_OK on success
 */
esp_err_t tool_arxiv_execute(const char *input_json, char *output, size_t output_size);
