#include "agent_loop.h"
#include "agent/context_builder.h"
#include "mimi_config.h"
#include "bus/message_bus.h"
#include "llm/llm_proxy.h"
#include "wifi/wifi_manager.h"
#include "memory/session_mgr.h"
#include "tools/tool_registry.h"
#include "bus/message_bus.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "agent";

#define TOOL_OUTPUT_SIZE  (120 * 1024)

/* Agent state management for A2A /status endpoint */
#define AGENT_STATE_IDLE     "idle"
#define AGENT_STATE_WORK     "work"

static SemaphoreHandle_t s_state_mutex = NULL;
static const char *s_agent_state = AGENT_STATE_IDLE;

const char *agent_loop_get_state(void)
{
    if (!s_state_mutex) {
        return AGENT_STATE_IDLE;
    }
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return AGENT_STATE_IDLE;
    }
    const char *state = s_agent_state;
    xSemaphoreGive(s_state_mutex);
    return state;
}

static void agent_loop_set_state(const char *state)
{
    if (!state || !s_state_mutex) return;
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    s_agent_state = state;
    xSemaphoreGive(s_state_mutex);
}

/* Build the assistant content array from llm_response_t for the messages history.
 * Returns a cJSON array with text and tool_use blocks. */
static cJSON *build_assistant_content(const llm_response_t *resp)
{
    cJSON *content = cJSON_CreateArray();

    /* Text block */
    if (resp->text && resp->text_len > 0) {
        cJSON *text_block = cJSON_CreateObject();
        cJSON_AddStringToObject(text_block, "type", "text");
        cJSON_AddStringToObject(text_block, "text", resp->text);
        cJSON_AddItemToArray(content, text_block);
    }

    /* Tool use blocks */
    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        cJSON *tool_block = cJSON_CreateObject();
        cJSON_AddStringToObject(tool_block, "type", "tool_use");
        cJSON_AddStringToObject(tool_block, "id", call->id);
        cJSON_AddStringToObject(tool_block, "name", call->name);

        cJSON *input = cJSON_Parse(call->input);
        if (input) {
            cJSON_AddItemToObject(tool_block, "input", input);
        } else {
            cJSON_AddItemToObject(tool_block, "input", cJSON_CreateObject());
        }

        cJSON_AddItemToArray(content, tool_block);
    }

    return content;
}

static void json_set_string(cJSON *obj, const char *key, const char *value)
{
    if (!obj || !key || !value) {
        return;
    }
    cJSON_DeleteItemFromObject(obj, key);
    cJSON_AddStringToObject(obj, key, value);
}

static void append_turn_context_prompt(char *prompt, size_t size, const mimi_msg_t *msg)
{
    if (!prompt || size == 0 || !msg) {
        return;
    }

    size_t off = strnlen(prompt, size - 1);
    if (off >= size - 1) {
        return;
    }

    int n = snprintf(
        prompt + off, size - off,
        "\n## Current Turn Context\n"
        "- source_channel: %s\n"
        "- source_chat_id: %s\n",
        msg->channel[0] ? msg->channel : "(unknown)",
        msg->chat_id[0] ? msg->chat_id : "(empty)");

    if (n < 0 || (size_t)n >= (size - off)) {
        prompt[size - 1] = '\0';
    }
}

static char *patch_tool_input_with_context(const llm_tool_call_t *call, const mimi_msg_t *msg)
{
    if (!call || !msg || strcmp(call->name, "cron_add") != 0) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    if (!root) {
        return NULL;
    }

    bool changed = false;

    cJSON *channel_item = cJSON_GetObjectItem(root, "channel");
    const char *channel = cJSON_IsString(channel_item) ? channel_item->valuestring : NULL;

    if ((!channel || channel[0] == '\0') && msg->channel[0] != '\0') {
        json_set_string(root, "channel", msg->channel);
        channel = msg->channel;
        changed = true;
    }

    if (channel && strcmp(channel, msg->channel) == 0 && msg->chat_id[0] != '\0' &&
        (strcmp(channel, MIMI_CHAN_TELEGRAM) == 0 || strcmp(channel, MIMI_CHAN_FEISHU) == 0)) {
        cJSON *chat_item = cJSON_GetObjectItem(root, "chat_id");
        const char *chat_id = cJSON_IsString(chat_item) ? chat_item->valuestring : NULL;
        if (!chat_id || chat_id[0] == '\0' || strcmp(chat_id, "cron") == 0) {
            json_set_string(root, "chat_id", msg->chat_id);
            changed = true;
        }
    }

    char *patched = NULL;
    if (changed) {
        patched = cJSON_PrintUnformatted(root);
        if (patched) {
            ESP_LOGI(TAG, "Patched cron_add target to %s:%s", msg->channel, msg->chat_id);
        }
    }

    cJSON_Delete(root);
    return patched;
}

static bool should_block_self_a2a_message_send(const llm_tool_call_t *call,
                                               const mimi_msg_t *msg,
                                               char *reason,
                                               size_t reason_size)
{
    if (!call || !msg) {
        return false;
    }

    if (strcmp(msg->channel, MIMI_CHAN_A2A) != 0) {
        return false;
    }

    if (!call->input) {
        return false;
    }

    cJSON *tool_input = cJSON_Parse(call->input);
    if (!tool_input) {
        return false;
    }

    bool blocked = false;
    if (strcmp(call->name, "http_request") == 0) {
        cJSON *url_item = cJSON_GetObjectItem(tool_input, "url");
        cJSON *method_item = cJSON_GetObjectItem(tool_input, "method");

        const char *url = cJSON_IsString(url_item) ? url_item->valuestring : "";
        const char *method = cJSON_IsString(method_item) ? method_item->valuestring : "GET";

        if (strcmp(method, "POST") == 0) {
            bool local_host = (strstr(url, "://localhost:") != NULL) ||
                              (strstr(url, "://127.0.0.1:") != NULL);
            bool a2a_port = strstr(url, ":18788/") != NULL;
            bool send_path = strstr(url, "/message/send") != NULL;
            if (local_host && a2a_port && send_path) {
                blocked = true;
            }
        }
    } else if (strcmp(call->name, "a2a_client") == 0) {
        cJSON *server_item = cJSON_GetObjectItem(tool_input, "server");
        if (!cJSON_IsString(server_item) || !server_item->valuestring || server_item->valuestring[0] == '\0') {
            server_item = cJSON_GetObjectItem(tool_input, "server_url");
        }
        if (!cJSON_IsString(server_item) || !server_item->valuestring || server_item->valuestring[0] == '\0') {
            server_item = cJSON_GetObjectItem(tool_input, "base_url");
        }

        const char *server = (cJSON_IsString(server_item) && server_item->valuestring) ? server_item->valuestring : NULL;
        const char *local_ip = wifi_manager_get_ip();
        bool is_local_target = false;

        if (!server || server[0] == '\0') {
            is_local_target = true;
        } else if (strstr(server, "localhost") || strstr(server, "127.0.0.1")) {
            is_local_target = true;
        } else if (local_ip && local_ip[0] != '\0' && strstr(server, local_ip)) {
            is_local_target = true;
        }

        cJSON *action_item = cJSON_GetObjectItem(tool_input, "action");
        const char *action = cJSON_IsString(action_item) ? action_item->valuestring : "";
        if (strcmp(action, "send") == 0 && is_local_target) {
            blocked = true;
        }
    }

    if (blocked && reason && reason_size > 0) {
        if (strcmp(call->name, "http_request") == 0) {
            snprintf(reason, reason_size,
                     "blocked self A2A loop: http_request POST /message/send in A2A session");
        } else {
            snprintf(reason, reason_size,
                     "blocked self A2A loop: a2a_client action=send to local A2A server in A2A session");
        }
    }

    cJSON_Delete(tool_input);
    return blocked;
}

/* Build the user message with tool_result blocks */
static cJSON *build_tool_results(const llm_response_t *resp, const mimi_msg_t *msg,
                                 char *tool_output, size_t tool_output_size)
{
    cJSON *content = cJSON_CreateArray();
    bool is_anthropic = llm_provider_is_anthropic();

    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        const char *tool_input = call->input ? call->input : "{}";
        char *patched_input = patch_tool_input_with_context(call, msg);
        if (patched_input) {
            tool_input = patched_input;
        }

        /* Execute tool */
        tool_output[0] = '\0';
        char block_reason[192] = {0};
        if (should_block_self_a2a_message_send(call, msg, block_reason, sizeof(block_reason))) {
            ESP_LOGW(TAG, "%s", block_reason);
            snprintf(tool_output, tool_output_size,
                     "Error: self-loop protection active; do not call local A2A message/send from an A2A session");
        } else {
            tool_registry_execute(call->name, tool_input, tool_output, tool_output_size);
        }
        free(patched_input);

        ESP_LOGI(TAG, "Tool %s result: %d bytes", call->name, (int)strlen(tool_output));

        /* Determine if this tool result is an image for LLM vision */
        bool is_image_result = false;
        char media_type[64] = "image/jpeg";
        const char *image_b64 = tool_output;

        if (strcmp(call->name, "http_request") == 0 || strcmp(call->name, "camera_capture") == 0) {
            cJSON *tool_input_json = cJSON_Parse(tool_input);
            if (tool_input_json) {
                cJSON *enable_img = cJSON_GetObjectItem(tool_input_json, "enable_image_analysis");
                if (cJSON_IsTrue(enable_img)) {
                    is_image_result = true;
                    /* Output format: "<media_type>\n<base64_data>" */
                    char *newline = strchr(tool_output, '\n');
                    if (newline && (size_t)(newline - tool_output) < sizeof(media_type)) {
                        size_t mt_len = newline - tool_output;
                        memcpy(media_type, tool_output, mt_len);
                        media_type[mt_len] = '\0';
                        image_b64 = newline + 1;
                    }
                }
                cJSON_Delete(tool_input_json);
            }
        }

        /* Build tool_result block */
        cJSON *result_block = cJSON_CreateObject();
        cJSON_AddStringToObject(result_block, "type", "tool_result");
        cJSON_AddStringToObject(result_block, "tool_use_id", call->id);

        if (is_anthropic && is_image_result) {
            /* Anthropic: embed image as a base64 content-block array */
            cJSON *content_array = cJSON_CreateArray();
            cJSON *image_block = cJSON_CreateObject();
            cJSON_AddStringToObject(image_block, "type", "image");
            cJSON *source = cJSON_CreateObject();
            cJSON_AddStringToObject(source, "type", "base64");
            cJSON_AddStringToObject(source, "media_type", media_type);
            cJSON_AddStringToObject(source, "data", image_b64);
            cJSON_AddItemToObject(image_block, "source", source);
            cJSON_AddItemToArray(content_array, image_block);
            cJSON_AddItemToObject(result_block, "content", content_array);
        } else if (!is_anthropic && is_image_result) {
            /* OpenAI-compatible: image_url content array.
             * convert_messages_openai() will forward the array as-is on the
             * role=tool message, giving the model vision access to the image. */
            char prefix[80];
            snprintf(prefix, sizeof(prefix), "data:%s;base64,", media_type);
            size_t prefix_len = strlen(prefix);
            size_t b64_len = strlen(image_b64);
            size_t url_len = prefix_len + b64_len;
            char *url_buf = malloc(url_len + 1);
            if (url_buf) {
                memcpy(url_buf, prefix, prefix_len);
                memcpy(url_buf + prefix_len, image_b64, b64_len);
                url_buf[url_len] = '\0';
                cJSON *content_array = cJSON_CreateArray();
                cJSON *img_block = cJSON_CreateObject();
                cJSON_AddStringToObject(img_block, "type", "image_url");
                cJSON *image_url = cJSON_CreateObject();
                cJSON_AddStringToObject(image_url, "url", url_buf);
                free(url_buf); /* cJSON_AddStringToObject copied the string */
                cJSON_AddItemToObject(img_block, "image_url", image_url);
                cJSON_AddItemToArray(content_array, img_block);
                cJSON_AddItemToObject(result_block, "content", content_array);
            } else {
                cJSON_AddStringToObject(result_block, "content", "[image: out of memory]");
            }
        } else {
            /* OpenAI-compatible providers require a plain string for tool
             * result content.  convert_messages_openai() in llm_proxy.c
             * then promotes each tool_result block to its own role=tool
             * message, forwarding this string as-is. */
            cJSON_AddStringToObject(result_block, "content", tool_output);
        }

        cJSON_AddItemToArray(content, result_block);
    }

    return content;
}

static void agent_loop_task(void *arg)
{
    ESP_LOGI(TAG, "Agent loop started on core %d", xPortGetCoreID());

    /* Allocate large buffers from PSRAM */
    char *system_prompt = heap_caps_calloc(1, MIMI_CONTEXT_BUF_SIZE, MALLOC_CAP_SPIRAM);
    char *history_json = heap_caps_calloc(1, MIMI_LLM_STREAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    char *tool_output = heap_caps_calloc(1, TOOL_OUTPUT_SIZE, MALLOC_CAP_SPIRAM);

    if (!system_prompt || !history_json || !tool_output) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffers");
        vTaskDelete(NULL);
        return;
    }

    const char *tools_json = tool_registry_get_tools_json();

    /* Initialize state mutex once */
    if (!s_state_mutex) {
        s_state_mutex = xSemaphoreCreateMutex();
    }

    while (1) {
        mimi_msg_t msg;
        esp_err_t err = message_bus_pop_inbound(&msg, UINT32_MAX);
        if (err != ESP_OK) continue;

        /* Mark agent as RUNNING */
        agent_loop_set_state(AGENT_STATE_WORK);
        ESP_LOGI(TAG, "Processing message from %s:%s", msg.channel, msg.chat_id);

        /* 1. Build system prompt */
        context_build_system_prompt(system_prompt, MIMI_CONTEXT_BUF_SIZE);
        append_turn_context_prompt(system_prompt, MIMI_CONTEXT_BUF_SIZE, &msg);
        ESP_LOGI(TAG, "LLM turn context: channel=%s chat_id=%s", msg.channel, msg.chat_id);

        /* 2. Load session history into cJSON array */
        session_get_history_json(msg.chat_id, history_json,
                                 MIMI_LLM_STREAM_BUF_SIZE, MIMI_AGENT_MAX_HISTORY);

        cJSON *messages = cJSON_Parse(history_json);
        if (!messages) {
            ESP_LOGW(TAG, "History parse failed for chat_id=%s, fallback to empty history", msg.chat_id);
            messages = cJSON_CreateArray();
        }
        ESP_LOGI(TAG, "Loaded history messages: %d for chat_id=%s",
                 cJSON_GetArraySize(messages), msg.chat_id);

        /* 3. Append current user message */
        cJSON *user_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(user_msg, "role", "user");
        cJSON_AddStringToObject(user_msg, "content", msg.payload.text);
        cJSON_AddItemToArray(messages, user_msg);

        /* 4. ReAct loop */
        char *final_text = NULL;
        int iteration = 0;
        int tool_calls_total = 0;
        char tool_name_buf[MIMI_AGENT_MAX_TOOL_ITER*MIMI_MAX_TOOL_CALLS][32] = {{0}};
        bool sent_working_status = false;

        while (iteration < MIMI_AGENT_MAX_TOOL_ITER) {
            /* Send "working" indicator before each API call */
#if MIMI_AGENT_SEND_WORKING_STATUS
            if (!sent_working_status && strcmp(msg.channel, MIMI_CHAN_SYSTEM) != 0 && strcmp(msg.channel, MIMI_CHAN_A2A) != 0) {
                mimi_msg_t status = {0};
                strncpy(status.channel, msg.channel, sizeof(status.channel) - 1);
                strncpy(status.chat_id, msg.chat_id, sizeof(status.chat_id) - 1);
                strncpy(status.type, "text", sizeof(status.type) - 1);
                status.payload.text = strdup("\xF0\x9F\x90\x9Dswarm is working...");
                if (status.payload.text) {
                    if (message_bus_push_outbound(&status) != ESP_OK) {
                        ESP_LOGW(TAG, "Outbound queue full, drop working status");
                        free(status.payload.text);
                    } else {
                        sent_working_status = true;
                    }
                }
            }
#endif

            llm_response_t resp;
            err = llm_chat_tools(system_prompt, messages, tools_json, &resp);

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "LLM call failed: %s", esp_err_to_name(err));
                break;
            }

            if (!resp.tool_use) {
                /* Normal completion — save final text and break */
                if (resp.text && resp.text_len > 0) {
                    final_text = strdup(resp.text);
                }
                llm_response_free(&resp);
                break;
            }

            ESP_LOGI(TAG, "Tool use iteration %d: %d calls", iteration + 1, resp.call_count);
            for (int i = tool_calls_total, j = 0; j < resp.call_count && i < MIMI_AGENT_MAX_TOOL_ITER*MIMI_MAX_TOOL_CALLS; i++, j++) {
                strncpy(tool_name_buf[i], resp.calls[j].name, sizeof(tool_name_buf[i]) - 1);
            }
            tool_calls_total += resp.call_count;

            /* Append assistant message with content array */
            cJSON *asst_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(asst_msg, "role", "assistant");
            cJSON_AddItemToObject(asst_msg, "content", build_assistant_content(&resp));
            cJSON_AddItemToArray(messages, asst_msg);

            /* Execute tools and append results */
            cJSON *tool_results = build_tool_results(&resp, &msg, tool_output, TOOL_OUTPUT_SIZE);
            cJSON *result_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(result_msg, "role", "user");
            cJSON_AddItemToObject(result_msg, "content", tool_results);
            cJSON_AddItemToArray(messages, result_msg);

            llm_response_free(&resp);
            iteration++;
        }

        cJSON_Delete(messages);

        /* 5. Send response */
        if (final_text && final_text[0]) {
            if (tool_calls_total > 0 &&
                (strcmp(msg.channel, MIMI_CHAN_FEISHU) == 0 ||
                 strcmp(msg.channel, MIMI_CHAN_TELEGRAM) == 0)) {
                    char summary[128];
                    snprintf(summary, sizeof(summary), "本次调用了 %d 个工具", tool_calls_total);
                    char tool_list[1024] = "";
                    for (int i = 0; i < tool_calls_total; i++) {
                        char tool_entry[64];
                        snprintf(tool_entry, sizeof(tool_entry), "- %s \n ", tool_name_buf[i]);
                        strncat(tool_list, tool_entry, sizeof(tool_list) - strlen(tool_list) - 1);
                    }
                if (summary[0] && tool_list[0]) {
                    mimi_msg_t tool_msg = {0};
                    strncpy(tool_msg.channel, msg.channel, sizeof(tool_msg.channel) - 1);
                    strncpy(tool_msg.chat_id, msg.chat_id, sizeof(tool_msg.chat_id) - 1);
                    strncpy(tool_msg.type, "collapsible", sizeof(tool_msg.type) - 1);
                    tool_msg.payload.collapsible.title = strdup(summary);
                    tool_msg.payload.collapsible.body = strdup(tool_list);
                    if (message_bus_push_outbound(&tool_msg) != ESP_OK) {
                        ESP_LOGW(TAG, "Outbound queue full, drop tool summary message");
                        mimi_msg_free(&tool_msg);
                    }
                }
            }

            /* Save to session (only user text + final assistant text) */
            esp_err_t save_user = session_append(msg.chat_id, "user", msg.payload.text);
            esp_err_t save_asst = session_append(msg.chat_id, "assistant", final_text);
            if (save_user != ESP_OK || save_asst != ESP_OK) {
                ESP_LOGW(TAG, "Session save failed for chat %s (user=%s, assistant=%s)",
                         msg.chat_id,
                         esp_err_to_name(save_user),
                         esp_err_to_name(save_asst));
            } else {
                ESP_LOGI(TAG, "Session saved for chat %s", msg.chat_id);
            }

            /* Push response to outbound */
            mimi_msg_t out = {0};
            strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
            strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
            strncpy(out.type, "text", sizeof(out.type) - 1);
            out.payload.text = final_text;  /* transfer ownership */
            ESP_LOGI(TAG, "Queue final response to %s:%s (%d bytes)",
                     out.channel, out.chat_id, (int)strlen(final_text));
            if (message_bus_push_outbound(&out) != ESP_OK) {
                ESP_LOGW(TAG, "Outbound queue full, drop final response");
                free(final_text);
            } else {
                final_text = NULL;
            }
        } else {
            /* Error or empty response */
            free(final_text);
            mimi_msg_t out = {0};
            strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
            strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
            strncpy(out.type, "text", sizeof(out.type) - 1);
            out.payload.text = strdup("Sorry, I encountered an error.");
            if (out.payload.text) {
                if (message_bus_push_outbound(&out) != ESP_OK) {
                    ESP_LOGW(TAG, "Outbound queue full, drop error response");
                    free(out.payload.text);
                }
            }
        }

        /* Free inbound message content */
        mimi_msg_free(&msg);

        /* Log memory status */
        ESP_LOGI(TAG, "Free PSRAM: %d bytes",
                 (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

        /* Mark agent as IDLE after processing */
        agent_loop_set_state(AGENT_STATE_IDLE);
    }
}

esp_err_t agent_loop_init(void)
{
    ESP_LOGI(TAG, "Agent loop initialized");
    return ESP_OK;
}

esp_err_t agent_loop_start(void)
{
    const uint32_t stack_candidates[] = {
        MIMI_AGENT_STACK,
        20 * 1024,
        16 * 1024,
        14 * 1024,
        12 * 1024,
    };

    for (size_t i = 0; i < (sizeof(stack_candidates) / sizeof(stack_candidates[0])); i++) {
        uint32_t stack_size = stack_candidates[i];
        BaseType_t ret = xTaskCreatePinnedToCore(
            agent_loop_task, "agent_loop",
            stack_size, NULL,
            MIMI_AGENT_PRIO, NULL, MIMI_AGENT_CORE);

        if (ret == pdPASS) {
            ESP_LOGI(TAG, "agent_loop task created with stack=%u bytes", (unsigned)stack_size);
            return ESP_OK;
        }

        ESP_LOGW(TAG,
                 "agent_loop create failed (stack=%u, free_internal=%u, largest_internal=%u), retrying...",
                 (unsigned)stack_size,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }

    return ESP_FAIL;
}
