#pragma once

#include "esp_err.h"

/**
 * @brief 睡眠工具函数
 *
 * 使当前任务睡眠指定的时长，支持以毫秒或秒为单位指定。
 * 这对于需要在等待其他任务完成或避免资源竞争时使用。
 *
 * @param input_json JSON格式的输入参数，支持：
 *        - duration_ms: 睡眠时长（毫秒）
 *        - duration_s: 睡眠时长（秒）
 *        如果两个参数都提供，duration_ms 优先
 * @param output 输出缓冲区，用于返回结果
 * @param output_size 输出缓冲区大小
 *
 * @return ESP_OK 成功
 * @return 其他错误码 失败
 */
esp_err_t tool_sleep_execute(const char *input_json, char *output, size_t output_size);
