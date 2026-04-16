#include "tool_registry.h"
#include "tools/tool_web_search.h"
#include "tools/tool_get_time.h"
#include "tools/tool_files.h"
#include "tools/tool_cron.h"
#include "tools/tool_http_request.h"
#include "tools/tool_a2a_client.h"
#include "tools/tool_script.h"
#include "sdkconfig.h"
#include "tools/tool_sleep.h"

#if CONFIG_MIMI_TOOL_RGB_ENABLED
#include "tool_rgb_control.h"
#endif

#if CONFIG_MIMI_TOOL_CAMERA_ENABLED
#include "tool_camera_capture.h"
#endif

#if CONFIG_MIMI_TOOL_BLE_ENABLED
#include "tool_ble_listener.h"
#endif

#include <string.h>
#include "esp_log.h"
#include "cJSON.h"

#include "mimi_config.h"
#include "onboard/wifi_onboard.h"

static const char *TAG = "tools";

#define MAX_TOOLS 20

static mimi_tool_t s_tools[MAX_TOOLS];
static int s_tool_count = 0;
static char *s_tools_json = NULL;  /* cached JSON array string */

static void register_tool(const mimi_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        ESP_LOGE(TAG, "Tool registry full");
        return;
    }
    s_tools[s_tool_count++] = *tool;
    ESP_LOGI(TAG, "Registered tool: %s", tool->name);
}

static void build_tools_json(void)
{
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < s_tool_count; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", s_tools[i].name);
        cJSON_AddStringToObject(tool, "description", s_tools[i].description);

        cJSON *schema = cJSON_Parse(s_tools[i].input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }

        cJSON_AddItemToArray(arr, tool);
    }

    free(s_tools_json);
    s_tools_json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    ESP_LOGI(TAG, "Tools JSON built (%d tools)", s_tool_count);
}

esp_err_t tool_registry_init(void)
{
    s_tool_count = 0;

    /* Register web_search */
    tool_web_search_init();

    mimi_tool_t ws = {
        .name = "web_search",
        .description = "Search the web for current information. Use this when you need up-to-date facts, news, weather, or anything beyond your training data.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"The search query\"}},"
            "\"required\":[\"query\"]}",
        .execute = tool_web_search_execute,
    };
    register_tool(&ws);

    /* Register get_current_time */
    mimi_tool_t gt = {
        .name = "get_current_time",
        .description = "Get the current date and time. Call this when you need to know what time or date it is.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_get_time_execute,
    };
    register_tool(&gt);

    /* Register sleep */
    mimi_tool_t sl = {
        .name = "sleep",
        .description = "Make the agent sleep for a specified duration. Use this when you need to wait for other tasks or processes to complete without blocking the entire system. Supports milliseconds or seconds.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"duration_ms\":{\"type\":\"integer\",\"description\":\"Sleep duration in milliseconds (minimum 10ms, maximum 5 minutes)\"},"
            "\"duration_s\":{\"type\":\"integer\",\"description\":\"Sleep duration in seconds (minimum 0.01s, maximum 5 minutes)\"}"
            "},"
            "\"required\":[]}",
        .execute = tool_sleep_execute,
    };
    register_tool(&sl);

    /* Register read_file */
    mimi_tool_t rf = {
        .name = "read_file",
        .description = "Read a file from SPIFFS storage. Path must start with /spiffs/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with /spiffs/\"}},"
            "\"required\":[\"path\"]}",
        .execute = tool_read_file_execute,
    };
    register_tool(&rf);

    /* Register write_file */
    mimi_tool_t wf = {
        .name = "write_file",
        .description = "Write or overwrite a file on SPIFFS storage. Path must start with /spiffs/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with /spiffs/\"},"
            "\"content\":{\"type\":\"string\",\"description\":\"File content to write\"}},"
            "\"required\":[\"path\",\"content\"]}",
        .execute = tool_write_file_execute,
    };
    register_tool(&wf);

    /* Register edit_file */
    mimi_tool_t ef = {
        .name = "edit_file",
        .description = "Find and replace text in a file on SPIFFS. Replaces first occurrence of old_string with new_string.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with /spiffs/\"},"
            "\"old_string\":{\"type\":\"string\",\"description\":\"Text to find\"},"
            "\"new_string\":{\"type\":\"string\",\"description\":\"Replacement text\"}},"
            "\"required\":[\"path\",\"old_string\",\"new_string\"]}",
        .execute = tool_edit_file_execute,
    };
    register_tool(&ef);

    /* Register list_dir */
    mimi_tool_t ld = {
        .name = "list_dir",
        .description = "List files on SPIFFS storage, optionally filtered by path prefix.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"prefix\":{\"type\":\"string\",\"description\":\"Optional path prefix filter, e.g. /spiffs/memory/\"}},"
            "\"required\":[]}",
        .execute = tool_list_dir_execute,
    };
    register_tool(&ld);

    /* Register cron_add */
    mimi_tool_t ca = {
        .name = "cron_add",
        .description = "Schedule a recurring or one-shot task. The message will trigger an agent turn when the job fires.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"name\":{\"type\":\"string\",\"description\":\"Short name for the job\"},"
            "\"schedule_type\":{\"type\":\"string\",\"description\":\"'every' for recurring interval or 'at' for one-shot at a unix timestamp\"},"
            "\"interval_s\":{\"type\":\"integer\",\"description\":\"Interval in seconds (required for 'every')\"},"
            "\"at_epoch\":{\"type\":\"integer\",\"description\":\"Unix timestamp to fire at (required for 'at')\"},"
            "\"message\":{\"type\":\"string\",\"description\":\"Message to inject when the job fires, triggering an agent turn\"},"
            "\"channel\":{\"type\":\"string\",\"description\":\"Optional reply channel (e.g. 'telegram' or 'feishu'). Defaults to 'system'\"},"
            "\"chat_id\":{\"type\":\"string\",\"description\":\"Optional reply chat_id. Defaults to 'cron'\"}"
            "},"
            "\"required\":[\"name\",\"schedule_type\",\"message\"]}",
        .execute = tool_cron_add_execute,
    };
    register_tool(&ca);

    /* Register cron_list */
    mimi_tool_t cl = {
        .name = "cron_list",
        .description = "List all scheduled cron jobs with their status, schedule, and IDs.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_cron_list_execute,
    };
    register_tool(&cl);

    /* Register cron_remove */
    mimi_tool_t cr = {
        .name = "cron_remove",
        .description = "Remove a scheduled cron job by its ID.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"job_id\":{\"type\":\"string\",\"description\":\"The 8-character job ID to remove\"}},"
            "\"required\":[\"job_id\"]}",
        .execute = tool_cron_remove_execute,
    };
    register_tool(&cr);

    /* Register http_request */
    mimi_tool_t hr = {
        .name = "http_request",
        .description = "Make HTTP requests to external APIs and websites. Supports GET, POST, PUT, DELETE, PATCH, HEAD methods. Use for API calls, fetching data from URLs, etc. Set enable_image_analysis to true to capture an image from a URL and return it as base64 for vision analysis.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"url\":{\"type\":\"string\",\"description\":\"HTTP or HTTPS URL to request\"},"
            "\"method\":{\"type\":\"string\",\"description\":\"HTTP method: GET, POST, PUT, DELETE, PATCH, HEAD (default: GET)\"},"
            "\"headers\":{\"type\":\"object\",\"description\":\"Optional HTTP headers as key-value pairs\"},"
            "\"body\":{\"type\":\"string\",\"description\":\"Optional request body (for POST, PUT, PATCH)\"},"
            "\"enable_image_analysis\":{\"type\":\"boolean\",\"description\":\"When true, treat the response as an image, base64-encode it, and return it for LLM vision analysis\"}"
            "},"
            "\"required\":[\"url\"]}",
        .execute = tool_http_request_execute,
    };
    register_tool(&hr);

    /* Register a2a_client */
    mimi_tool_t ac = {
        .name = "a2a_client",
        .description = "Call an A2A server with auto-filled device client_id. Supports actions: send/get/cancel/agent_card. Optional server/server_url/base_url overrides the default local device.",
        .input_schema_json =
            "{\"type\":\"object\"," 
            "\"properties\":{"
            "\"action\":{\"type\":\"string\",\"description\":\"send | get | cancel | agent_card\"},"
            "\"message\":{\"type\":\"string\",\"description\":\"Message text, required for action=send\"},"
            "\"task_id\":{\"type\":\"string\",\"description\":\"Task ID, required for action=get/cancel\"},"
            "\"server\":{\"type\":\"string\",\"description\":\"Optional server host or base URL. Examples: 192.168.3.40, 192.168.3.40:18788, http://192.168.3.40:18788\"},"
            "\"timeout_ms\":{\"type\":\"integer\",\"description\":\"Optional HTTP timeout in ms\"}"
            "},"
            "\"required\":[\"action\"]}",
        .execute = tool_a2a_client_execute,
    };
    register_tool(&ac);

    if (mimi_feature_rgb_control_enabled()) {
#if CONFIG_MIMI_TOOL_RGB_ENABLED
        /* Register rgb_control */
        mimi_tool_t rc = {
            .name = "rgb_control",
            .description = "Set the onboard WS2812 RGB LED color using hex or r/g/b plus optional brightness.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{"
                "\"r\":{\"type\":\"integer\",\"description\":\"Red 0..255\"},"
                "\"g\":{\"type\":\"integer\",\"description\":\"Green 0..255\"},"
                "\"b\":{\"type\":\"integer\",\"description\":\"Blue 0..255\"},"
                "\"hex\":{\"type\":\"string\",\"description\":\"Hex color #RRGGBB\"},"
                "\"brightness\":{\"type\":\"integer\",\"description\":\"Brightness 0..255 (optional)\"}"
                "},"
                "\"required\":[]}",
            .execute = tool_rgb_control_execute,
        };
        register_tool(&rc);
#endif
    }

    if (mimi_feature_camera_tool_enabled()) {
#if CONFIG_MIMI_TOOL_CAMERA_ENABLED
        /* Register camera_capture */
        mimi_tool_t cam = {
            .name = "camera_capture",
            .description = "Capture a fresh camera image and return it for visual analysis.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{"
                "\"enable_image_analysis\":{\"type\":\"boolean\",\"description\":\"Whether to include image payload for LLM visual analysis. Default true.\"}"
                "},"
                "\"required\":[]}",
            .execute = tool_camera_capture_execute,
        };
        register_tool(&cam);
#endif
    }

    if (mimi_feature_ble_tool_enabled()) {
#if CONFIG_MIMI_TOOL_BLE_ENABLED
        /* Register ble_listener */
        mimi_tool_t ble = {
            .name = "ble_listener",
            .description = "Get the latest BLE BTHome temperature, humidity, and battery reading from this device.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{},"
                "\"required\":[]}",
            .execute = tool_ble_listener_execute,
        };
        register_tool(&ble);
#endif
    }

    /* Register script_write */
    mimi_tool_t sw = {
        .name = "script_write",
        .description = "Write a Lua script to SPIFFS. The script can use gpio, i2c, spi, rgb, pwm, sleep modules.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"path\":{\"type\":\"string\",\"description\":\"e.g. /spiffs/scripts/blink.lua\"},"
            "\"content\":{\"type\":\"string\",\"description\":\"Full Lua source code\"}"
            "},"
            "\"required\":[\"path\",\"content\"]}",
        .execute = tool_script_write_execute,
    };
    register_tool(&sw);

    /* Register script_run */
    mimi_tool_t sr = {
        .name = "script_run",
        .description = "Execute a Lua script on the ESP32-S3. Returns stdout output or error message.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"path\":{\"type\":\"string\",\"description\":\"e.g. /spiffs/scripts/blink.lua\"},"
            "\"timeout_ms\":{\"type\":\"integer\",\"description\":\"Max execution time in ms (default 5000)\"}"
            "},"
            "\"required\":[\"path\"]}",
        .execute = tool_script_run_execute,
    };
    register_tool(&sr);

    /* Register script_write_and_run */
    mimi_tool_t swr = {
        .name = "script_write_and_run",
        .description = "Write a temporary Lua script to SPIFFS, run it, and delete it afterward. Useful for command-style one-off scripts.",
        .input_schema_json =
            "{\"type\":\"object\"," 
            "\"properties\":{" 
            "\"content\":{\"type\":\"string\",\"description\":\"Full Lua source code\"},"
            "\"timeout_ms\":{\"type\":\"integer\",\"description\":\"Max execution time in ms (default 5000)\"}"
            "},"
            "\"required\":[\"content\"]}",
        .execute = tool_script_write_and_run_execute,
    };
    register_tool(&swr);

    // After registering all tools, build the JSON array string
    build_tools_json();

    ESP_LOGI(TAG, "Tool registry initialized");
    return ESP_OK;
}

const char *tool_registry_get_tools_json(void)
{
    return s_tools_json;
}

esp_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size)
{
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            ESP_LOGI(TAG, "Executing tool: %s", name);
            return s_tools[i].execute(input_json, output, output_size);
        }
    }

    ESP_LOGW(TAG, "Unknown tool: %s", name);
    snprintf(output, output_size, "Error: unknown tool '%s'", name);
    return ESP_ERR_NOT_FOUND;
}
