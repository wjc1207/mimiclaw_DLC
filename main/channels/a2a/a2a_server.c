#include "channels/a2a/a2a_server.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

#include "bus/message_bus.h"
#include "mimi_config.h"
#include "agent/agent_loop.h"

#define A2A_REQ_MAX_BYTES   4096
#define A2A_RESP_MAX_BYTES  2048

#define A2A_STATE_QUEUED     "queued"
#define A2A_STATE_RUNNING    "running"
#define A2A_STATE_COMPLETED  "completed"
#define A2A_STATE_FAILED     "failed"
#define A2A_STATE_CANCELED   "canceled"

typedef struct {
    bool used;
    bool awaiting_reply;
    uint64_t seq;
    uint64_t created_ms;
    uint64_t updated_ms;
    char id[40];
    char client_id[24];
    char session_key[96];
    char state[16];
    char input[320];
    char output[A2A_RESP_MAX_BYTES];
} a2a_task_t;

static const char *TAG = "a2a";

static httpd_handle_t s_server = NULL;
static SemaphoreHandle_t s_task_mutex = NULL;
static uint64_t s_task_seq = 0;
static a2a_task_t *s_tasks = NULL;

static bool is_terminal_state(const char *state)
{
    return state && (
        strcmp(state, A2A_STATE_COMPLETED) == 0 ||
        strcmp(state, A2A_STATE_FAILED) == 0 ||
        strcmp(state, A2A_STATE_CANCELED) == 0);
}

static void set_common_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
}

static char *build_jsonrpc_error(cJSON *id, int code, const char *message, const char *data)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON *err = cJSON_CreateObject();
    if (!resp || !err) {
        cJSON_Delete(resp);
        cJSON_Delete(err);
        return NULL;
    }

    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
    cJSON_AddItemToObject(resp, "id", id ? cJSON_Duplicate(id, 1) : cJSON_CreateNull());
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message ? message : "Error");
    if (data) {
        cJSON_AddStringToObject(err, "data", data);
    }
    cJSON_AddItemToObject(resp, "error", err);

    char *out = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    return out;
}

static char *build_task_result(cJSON *id, const a2a_task_t *task)
{
    if (!task) {
        return NULL;
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON *result = cJSON_CreateObject();
    cJSON *status = cJSON_CreateObject();
    cJSON *meta = cJSON_CreateObject();
    cJSON *artifacts = cJSON_CreateArray();
    cJSON *artifact = cJSON_CreateObject();
    cJSON *parts = cJSON_CreateArray();
    cJSON *part = cJSON_CreateObject();
    if (!resp || !result || !status || !meta || !artifacts || !artifact || !parts || !part) {
        cJSON_Delete(resp);
        cJSON_Delete(result);
        cJSON_Delete(status);
        cJSON_Delete(meta);
        cJSON_Delete(artifacts);
        cJSON_Delete(artifact);
        cJSON_Delete(parts);
        cJSON_Delete(part);
        return NULL;
    }

    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
    cJSON_AddItemToObject(resp, "id", id ? cJSON_Duplicate(id, 1) : cJSON_CreateNull());

    cJSON_AddStringToObject(result, "id", task->id);
    cJSON_AddStringToObject(result, "kind", "task");

    cJSON_AddStringToObject(status, "state", task->state);
    cJSON_AddNumberToObject(status, "timestamp", (double)task->updated_ms);
    cJSON_AddItemToObject(result, "status", status);

    cJSON_AddStringToObject(meta, "client_id", task->client_id);
    cJSON_AddStringToObject(meta, "session_key", task->session_key);
    cJSON_AddItemToObject(result, "metadata", meta);

    cJSON_AddStringToObject(artifact, "name", "assistant_response");
    cJSON_AddStringToObject(artifact, "mimeType", "text/plain");
    cJSON_AddStringToObject(part, "type", "text");
    cJSON_AddStringToObject(part, "text", task->output);
    cJSON_AddItemToArray(parts, part);
    cJSON_AddItemToObject(artifact, "parts", parts);
    cJSON_AddItemToArray(artifacts, artifact);
    cJSON_AddItemToObject(result, "artifacts", artifacts);

    cJSON_AddItemToObject(resp, "result", result);

    char *out = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    return out;
}

static bool parse_rpc_request(const char *payload,
                              cJSON **out_root,
                              cJSON **out_id,
                              const char **out_method,
                              cJSON **out_params,
                              char **out_error_json)
{
    if (!payload || !out_root || !out_id || !out_method || !out_params || !out_error_json) {
        return false;
    }

    *out_root = NULL;
    *out_id = NULL;
    *out_method = NULL;
    *out_params = NULL;
    *out_error_json = NULL;

    cJSON *root = cJSON_Parse(payload);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        *out_error_json = build_jsonrpc_error(NULL, -32700, "Parse error", "invalid_json");
        return false;
    }

    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *jsonrpc = cJSON_GetObjectItem(root, "jsonrpc");
    cJSON *method = cJSON_GetObjectItem(root, "method");
    cJSON *params = cJSON_GetObjectItem(root, "params");

    if (!cJSON_IsString(jsonrpc) || strcmp(jsonrpc->valuestring, "2.0") != 0) {
        *out_error_json = build_jsonrpc_error(id, -32600, "Invalid Request", "jsonrpc must be 2.0");
        cJSON_Delete(root);
        return false;
    }

    if (!cJSON_IsString(method) || !method->valuestring || method->valuestring[0] == '\0') {
        *out_error_json = build_jsonrpc_error(id, -32600, "Invalid Request", "method is required");
        cJSON_Delete(root);
        return false;
    }

    if (!params || !cJSON_IsObject(params)) {
        *out_error_json = build_jsonrpc_error(id, -32602, "Invalid params", "params object is required");
        cJSON_Delete(root);
        return false;
    }

    *out_root = root;
    *out_id = id;
    *out_method = method->valuestring;
    *out_params = params;
    return true;
}

static const char *extract_message_text(cJSON *params)
{
    cJSON *message_text = cJSON_GetObjectItem(params, "message_text");
    if (cJSON_IsString(message_text) && message_text->valuestring) {
        return message_text->valuestring;
    }

    cJSON *message = cJSON_GetObjectItem(params, "message");
    if (cJSON_IsString(message) && message->valuestring) {
        return message->valuestring;
    }
    return NULL;
}

static const char *extract_client_id(cJSON *params)
{
    cJSON *client_id = cJSON_GetObjectItem(params, "client_id");
    if (cJSON_IsString(client_id) && client_id->valuestring) {
        return client_id->valuestring;
    }
    return NULL;
}

static const char *extract_task_id(cJSON *params)
{
    cJSON *task_id = cJSON_GetObjectItem(params, "task_id");
    if (cJSON_IsString(task_id) && task_id->valuestring) {
        return task_id->valuestring;
    }

    cJSON *id = cJSON_GetObjectItem(params, "id");
    if (cJSON_IsString(id) && id->valuestring) {
        return id->valuestring;
    }

    return NULL;
}

static bool validate_client_id(const char *client_id)
{
    if (!client_id || client_id[0] == '\0') {
        return false;
    }

    size_t len = strlen(client_id);
    if (len > 20) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char c = client_id[i];
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) {
            return false;
        }
    }

    return true;
}

static int read_request_body(httpd_req_t *req, char **out_payload)
{
    if (!req || !out_payload) {
        return ESP_ERR_INVALID_ARG;
    }

    if (req->content_len <= 0 || req->content_len > A2A_REQ_MAX_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *payload = (char *)heap_caps_calloc(1, req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!payload) {
        payload = (char *)calloc(1, req->content_len + 1);
    }
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }

    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, payload + received, req->content_len - received);
        if (ret <= 0) {
            free(payload);
            return ESP_FAIL;
        }
        received += ret;
    }

    *out_payload = payload;
    return ESP_OK;
}

static a2a_task_t *task_find_by_id_locked(const char *task_id)
{
    if (!s_tasks) {
        return NULL;
    }

    for (int i = 0; i < MIMI_A2A_MAX_TASKS; i++) {
        if (s_tasks[i].used && strcmp(s_tasks[i].id, task_id) == 0) {
            return &s_tasks[i];
        }
    }
    return NULL;
}

static int task_find_reclaimable_slot_locked(void)
{
    if (!s_tasks) {
        return -1;
    }

    int reclaim_idx = -1;
    uint64_t oldest = UINT64_MAX;

    for (int i = 0; i < MIMI_A2A_MAX_TASKS; i++) {
        if (!s_tasks[i].used) {
            return i;
        }
    }

    for (int i = 0; i < MIMI_A2A_MAX_TASKS; i++) {
        if (!s_tasks[i].awaiting_reply && is_terminal_state(s_tasks[i].state)) {
            if (s_tasks[i].updated_ms < oldest) {
                oldest = s_tasks[i].updated_ms;
                reclaim_idx = i;
            }
        }
    }

    return reclaim_idx;
}

static int task_create(const char *client_id, const char *session_key, const char *input, char *task_id_out, size_t out_len)
{
    int idx = -1;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);

    xSemaphoreTake(s_task_mutex, portMAX_DELAY);
    idx = task_find_reclaimable_slot_locked();
    if (idx >= 0) {
        s_task_seq++;
        memset(&s_tasks[idx], 0, sizeof(s_tasks[idx]));
        s_tasks[idx].used = true;
        s_tasks[idx].awaiting_reply = true;
        s_tasks[idx].seq = s_task_seq;
        s_tasks[idx].created_ms = now_ms;
        s_tasks[idx].updated_ms = now_ms;
        snprintf(s_tasks[idx].id, sizeof(s_tasks[idx].id), "a2a-%llu-%lu",
                 (unsigned long long)now_ms, (unsigned long)s_task_seq);
        strlcpy(s_tasks[idx].client_id, client_id, sizeof(s_tasks[idx].client_id));
        strlcpy(s_tasks[idx].session_key, session_key, sizeof(s_tasks[idx].session_key));
        strlcpy(s_tasks[idx].state, A2A_STATE_QUEUED, sizeof(s_tasks[idx].state));
        strlcpy(s_tasks[idx].input, input ? input : "", sizeof(s_tasks[idx].input));
        s_tasks[idx].output[0] = '\0';
        if (task_id_out && out_len > 0) {
            strlcpy(task_id_out, s_tasks[idx].id, out_len);
        }
    }
    xSemaphoreGive(s_task_mutex);

    return idx;
}

static bool task_snapshot(const char *task_id, a2a_task_t *out)
{
    bool found = false;

    xSemaphoreTake(s_task_mutex, portMAX_DELAY);
    a2a_task_t *task = task_find_by_id_locked(task_id);
    if (task) {
        *out = *task;
        found = true;
    }
    xSemaphoreGive(s_task_mutex);

    return found;
}

static int task_update(const char *task_id, const char *state, const char *output, bool clear_awaiting)
{
    int ret = -1;

    xSemaphoreTake(s_task_mutex, portMAX_DELAY);
    a2a_task_t *task = task_find_by_id_locked(task_id);
    if (task) {
        if (state) {
            strlcpy(task->state, state, sizeof(task->state));
        }
        if (output) {
            strlcpy(task->output, output, sizeof(task->output));
        }
        if (clear_awaiting) {
            task->awaiting_reply = false;
        }
        task->updated_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
        ret = 0;
    }
    xSemaphoreGive(s_task_mutex);

    return ret;
}

static bool task_cancel(const char *task_id)
{
    bool ok = false;

    xSemaphoreTake(s_task_mutex, portMAX_DELAY);
    a2a_task_t *task = task_find_by_id_locked(task_id);
    if (task && !is_terminal_state(task->state)) {
        strlcpy(task->state, A2A_STATE_CANCELED, sizeof(task->state));
        task->updated_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
        ok = true;
    }
    xSemaphoreGive(s_task_mutex);

    return ok;
}

static bool task_complete_for_session(const char *session_key, const char *text)
{
    bool mapped = false;

    xSemaphoreTake(s_task_mutex, portMAX_DELAY);

    int best = -1;
    uint64_t best_seq = UINT64_MAX;
    for (int i = 0; i < MIMI_A2A_MAX_TASKS; i++) {
        if (!s_tasks[i].used || !s_tasks[i].awaiting_reply) {
            continue;
        }
        if (strcmp(s_tasks[i].session_key, session_key) != 0) {
            continue;
        }
        if (s_tasks[i].seq < best_seq) {
            best_seq = s_tasks[i].seq;
            best = i;
        }
    }

    if (best >= 0) {
        a2a_task_t *task = &s_tasks[best];
        task->awaiting_reply = false;
        task->updated_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);

        if (strcmp(task->state, A2A_STATE_CANCELED) == 0) {
            ESP_LOGI(TAG, "Discarded response for canceled task %s", task->id);
        } else {
            strlcpy(task->state, A2A_STATE_COMPLETED, sizeof(task->state));
            strlcpy(task->output, text ? text : "", sizeof(task->output));
            ESP_LOGI(TAG, "Task %s completed", task->id);
        }
        mapped = true;
    }

    xSemaphoreGive(s_task_mutex);
    return mapped;
}

esp_err_t a2a_server_handle_agent_reply(const char *session_key, const char *text)
{
    if (!session_key || session_key[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (!task_complete_for_session(session_key, text ? text : "")) {
        ESP_LOGW(TAG, "No pending task for session %s", session_key);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static esp_err_t message_send_handler(httpd_req_t *req)
{
    set_common_headers(req);

    char *payload = NULL;
    esp_err_t body_ret = read_request_body(req, &payload);
    if (body_ret != ESP_OK) {
        char *err = build_jsonrpc_error(NULL, -32600, "Invalid Request", "invalid_body_length");
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    cJSON *root = NULL;
    cJSON *id = NULL;
    const char *method = NULL;
    cJSON *params = NULL;
    char *parse_error = NULL;
    if (!parse_rpc_request(payload, &root, &id, &method, &params, &parse_error)) {
        free(payload);
        if (!parse_error) {
            parse_error = build_jsonrpc_error(NULL, -32603, "Internal error", "oom");
        }
        if (!parse_error) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, parse_error);
        free(parse_error);
        return ret;
    }

    if (strcmp(method, "message/send") != 0) {
        char *err = build_jsonrpc_error(id, -32601, "Method not found", "supported: message/send");
        cJSON_Delete(root);
        free(payload);
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    const char *message_text = extract_message_text(params);
    const char *client_id = extract_client_id(params);

    if (!message_text || message_text[0] == '\0') {
        char *err = build_jsonrpc_error(id, -32602, "Invalid params", "message_text is required");
        cJSON_Delete(root);
        free(payload);
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    if (!validate_client_id(client_id)) {
        char *err = build_jsonrpc_error(id, -32602, "Invalid params", "client_id is required and must be alnum/_/-");
        cJSON_Delete(root);
        free(payload);
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    char session_key[96];
    snprintf(session_key, sizeof(session_key), "a2a:%s", client_id);

    char task_id[40];
    if (task_create(client_id, session_key, message_text, task_id, sizeof(task_id)) < 0) {
        char *err = build_jsonrpc_error(id, -32603, "Internal error", "task_capacity_exceeded");
        cJSON_Delete(root);
        free(payload);
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    mimi_msg_t msg = {0};
    strlcpy(msg.channel, MIMI_CHAN_A2A, sizeof(msg.channel));
    strlcpy(msg.chat_id, session_key, sizeof(msg.chat_id));
    strlcpy(msg.type, "text", sizeof(msg.type));
    msg.payload.text = strdup(message_text);
    if (!msg.payload.text) {
        task_update(task_id, A2A_STATE_FAILED, "{\"ok\":false,\"error\":\"oom\"}", true);
    } else {
        esp_err_t push_ret = message_bus_push_inbound(&msg);
        if (push_ret != ESP_OK) {
            free(msg.payload.text);
            task_update(task_id, A2A_STATE_FAILED, "{\"ok\":false,\"error\":\"inbound_queue_full\"}", true);
        } else {
            task_update(task_id, A2A_STATE_RUNNING, NULL, false);
        }
    }

    uint64_t start_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
    a2a_task_t snapshot = {0};
    while (true) {
        if (!task_snapshot(task_id, &snapshot)) {
            break;
        }

        if (is_terminal_state(snapshot.state)) {
            break;
        }

        uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
        if ((now_ms - start_ms) >= MIMI_A2A_SYNC_TIMEOUT_MS) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!task_snapshot(task_id, &snapshot)) {
        char *err = build_jsonrpc_error(id, -32004, "Task not found", task_id);
        cJSON_Delete(root);
        free(payload);
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    char *resp = build_task_result(id, &snapshot);
    cJSON_Delete(root);
    free(payload);

    if (!resp) {
        return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\",\"data\":\"oom\"}}");
    }

    esp_err_t send_ret = httpd_resp_sendstr(req, resp);
    free(resp);
    return send_ret;
}

static esp_err_t tasks_get_handler(httpd_req_t *req)
{
    set_common_headers(req);

    char *payload = NULL;
    esp_err_t body_ret = read_request_body(req, &payload);
    if (body_ret != ESP_OK) {
        char *err = build_jsonrpc_error(NULL, -32600, "Invalid Request", "invalid_body_length");
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    cJSON *root = NULL;
    cJSON *id = NULL;
    const char *method = NULL;
    cJSON *params = NULL;
    char *parse_error = NULL;
    if (!parse_rpc_request(payload, &root, &id, &method, &params, &parse_error)) {
        free(payload);
        if (!parse_error) {
            parse_error = build_jsonrpc_error(NULL, -32603, "Internal error", "oom");
        }
        if (!parse_error) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, parse_error);
        free(parse_error);
        return ret;
    }

    if (strcmp(method, "tasks/get") != 0) {
        char *err = build_jsonrpc_error(id, -32601, "Method not found", "supported: tasks/get");
        cJSON_Delete(root);
        free(payload);
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    const char *task_id = extract_task_id(params);
    if (!task_id || task_id[0] == '\0') {
        char *err = build_jsonrpc_error(id, -32602, "Invalid params", "task_id is required");
        cJSON_Delete(root);
        free(payload);
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    a2a_task_t snapshot = {0};
    if (!task_snapshot(task_id, &snapshot)) {
        char *err = build_jsonrpc_error(id, -32004, "Task not found", task_id);
        cJSON_Delete(root);
        free(payload);
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    char *resp = build_task_result(id, &snapshot);
    cJSON_Delete(root);
    free(payload);
    if (!resp) {
        return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
    }

    esp_err_t ret = httpd_resp_sendstr(req, resp);
    free(resp);
    return ret;
}

static esp_err_t tasks_cancel_handler(httpd_req_t *req)
{
    set_common_headers(req);

    char *payload = NULL;
    esp_err_t body_ret = read_request_body(req, &payload);
    if (body_ret != ESP_OK) {
        char *err = build_jsonrpc_error(NULL, -32600, "Invalid Request", "invalid_body_length");
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    cJSON *root = NULL;
    cJSON *id = NULL;
    const char *method = NULL;
    cJSON *params = NULL;
    char *parse_error = NULL;
    if (!parse_rpc_request(payload, &root, &id, &method, &params, &parse_error)) {
        free(payload);
        if (!parse_error) {
            parse_error = build_jsonrpc_error(NULL, -32603, "Internal error", "oom");
        }
        if (!parse_error) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, parse_error);
        free(parse_error);
        return ret;
    }

    if (strcmp(method, "tasks/cancel") != 0) {
        char *err = build_jsonrpc_error(id, -32601, "Method not found", "supported: tasks/cancel");
        cJSON_Delete(root);
        free(payload);
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    const char *task_id = extract_task_id(params);
    if (!task_id || task_id[0] == '\0') {
        char *err = build_jsonrpc_error(id, -32602, "Invalid params", "task_id is required");
        cJSON_Delete(root);
        free(payload);
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    if (!task_cancel(task_id)) {
        char *err = build_jsonrpc_error(id, -32005, "Task cannot be canceled", "Task is already terminal or missing");
        cJSON_Delete(root);
        free(payload);
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    a2a_task_t snapshot = {0};
    if (!task_snapshot(task_id, &snapshot)) {
        char *err = build_jsonrpc_error(id, -32004, "Task not found", task_id);
        cJSON_Delete(root);
        free(payload);
        if (!err) {
            return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
        }
        esp_err_t ret = httpd_resp_sendstr(req, err);
        free(err);
        return ret;
    }

    char *resp = build_task_result(id, &snapshot);
    cJSON_Delete(root);
    free(payload);
    if (!resp) {
        return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
    }

    esp_err_t ret = httpd_resp_sendstr(req, resp);
    free(resp);
    return ret;
}

static void log_device_client_id(void)
{
    uint8_t mac[6];
    char mac_hex[13];
    unsigned char hash[32];
    char client_id[17];

    /* Get device base MAC address */
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
    
    /* Convert MAC to hex string (without colons) */
    snprintf(mac_hex, sizeof(mac_hex), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    /* Calculate SHA256 of MAC hex string */
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const unsigned char *)mac_hex, strlen(mac_hex));
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    
    /* Extract first 16 hex characters from hash */
    snprintf(client_id, sizeof(client_id), "%02x%02x%02x%02x%02x%02x%02x%02x",
             hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7]);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Device MAC:     %s", mac_hex);
    ESP_LOGI(TAG, "  A2A Client ID:  %s", client_id);
    ESP_LOGI(TAG, "========================================");
}

static esp_err_t status_handler(httpd_req_t *req)
{
    set_common_headers(req);

    const char *state = agent_loop_get_state();

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
    }

    cJSON_AddStringToObject(resp, "agent_state", state);

    char *body = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    if (!body) {
        return httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}");
    }

    esp_err_t ret = httpd_resp_sendstr(req, body);
    free(body);
    return ret;
}

static esp_err_t well_known_handler(httpd_req_t *req)
{
    set_common_headers(req);

    static const char *body =
        "{"
        "\"schema_version\":\"a2a-protocol-v1.0.0\"," 
        "\"id\":\"swarmclaw\"," 
        "\"name\":\"Swarmclaw\"," 
        "\"description\":\"A2A channel backed by message_bus and agent ReACT loop.\"," 
        "\"endpoints\":{"
            "\"message_send\":\"/message/send\"," 
            "\"tasks_get\":\"/tasks/get\"," 
            "\"tasks_cancel\":\"/tasks/cancel\""
        "},"
        "\"task_mode\":\"hybrid_sync_async\""
        "}";

    return httpd_resp_sendstr(req, body);
}

esp_err_t a2a_server_start(void)
{
    if (!s_task_mutex) {
        s_task_mutex = xSemaphoreCreateMutex();
        if (!s_task_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_server) {
        return ESP_OK;
    }

    if (!s_tasks) {
        s_tasks = (a2a_task_t *)heap_caps_calloc(MIMI_A2A_MAX_TASKS, sizeof(a2a_task_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_tasks) {
            s_tasks = (a2a_task_t *)calloc(MIMI_A2A_MAX_TASKS, sizeof(a2a_task_t));
        }
        if (!s_tasks) {
            ESP_LOGE(TAG, "Failed to allocate A2A task pool");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "A2A task pool allocated: %d tasks (%u bytes)",
                 MIMI_A2A_MAX_TASKS,
                 (unsigned)(MIMI_A2A_MAX_TASKS * sizeof(a2a_task_t)));
    } else {
        memset(s_tasks, 0, MIMI_A2A_MAX_TASKS * sizeof(a2a_task_t));
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = MIMI_A2A_PORT;
    config.ctrl_port = MIMI_A2A_PORT + 1;
    config.max_uri_handlers = 12;
    config.stack_size = MIMI_A2A_STACK;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start A2A server: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Log device client ID for debugging */
    log_device_client_id();

    httpd_uri_t message_send_uri = {
        .uri = "/message/send",
        .method = HTTP_POST,
        .handler = message_send_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &message_send_uri);

    httpd_uri_t tasks_get_uri = {
        .uri = "/tasks/get",
        .method = HTTP_POST,
        .handler = tasks_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &tasks_get_uri);

    httpd_uri_t tasks_cancel_uri = {
        .uri = "/tasks/cancel",
        .method = HTTP_POST,
        .handler = tasks_cancel_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &tasks_cancel_uri);

    httpd_uri_t well_known_card_uri = {
        .uri = "/.well-known/agent-card.json",
        .method = HTTP_GET,
        .handler = well_known_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &well_known_card_uri);

    httpd_uri_t well_known_uri = {
        .uri = "/.well-known/agent.json",
        .method = HTTP_GET,
        .handler = well_known_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &well_known_uri);

    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &status_uri);

    ESP_LOGI(TAG, "A2A server started on port %d", MIMI_A2A_PORT);
    return ESP_OK;
}
