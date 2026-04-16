#include "tool_a2a_client.h"
#include "mimi_config.h"

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "wifi/wifi_manager.h"

static const char *TAG = "tool_a2a";

#define A2A_CLIENT_TIMEOUT_MS 15000
#define A2A_CLIENT_BUF_SIZE   8192

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *hb = (http_buf_t *)evt->user_data;
    if (!hb) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        size_t needed = hb->len + (size_t)evt->data_len + 1;
        if (needed > hb->cap) {
            size_t new_cap = hb->cap * 2;
            if (new_cap < needed) {
                new_cap = needed;
            }
            char *tmp = (char *)realloc(hb->data, new_cap);
            if (!tmp) {
                return ESP_ERR_NO_MEM;
            }
            hb->data = tmp;
            hb->cap = new_cap;
        }
        memcpy(hb->data + hb->len, evt->data, (size_t)evt->data_len);
        hb->len += (size_t)evt->data_len;
        hb->data[hb->len] = '\0';
    }

    return ESP_OK;
}

static esp_err_t build_local_client_id(char *out, size_t out_size)
{
    if (!out || out_size < 17) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac[6];
    char mac_hex[13];
    unsigned char hash[32];

    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        return err;
    }

    snprintf(mac_hex, sizeof(mac_hex), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const unsigned char *)mac_hex, strlen(mac_hex));
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    snprintf(out, out_size, "%02x%02x%02x%02x%02x%02x%02x%02x",
             hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7]);
    return ESP_OK;
}

static bool action_to_paths(const char *action, const char **endpoint, const char **rpc_method)
{
    if (strcmp(action, "agent_card") == 0) {
        *endpoint = NULL;
        *rpc_method = NULL;
        return true;
    }
    if (strcmp(action, "send") == 0) {
        *endpoint = "/message/send";
        *rpc_method = "message/send";
        return true;
    }
    if (strcmp(action, "get") == 0) {
        *endpoint = "/tasks/get";
        *rpc_method = "tasks/get";
        return true;
    }
    if (strcmp(action, "cancel") == 0) {
        *endpoint = "/tasks/cancel";
        *rpc_method = "tasks/cancel";
        return true;
    }
    return false;
}

static esp_err_t http_call(const char *url,
                           const char *method,
                           const char *content_type,
                           const char *body,
                           int timeout_ms,
                           http_buf_t *hb,
                           int *status_out)
{
    if (!url || !method || !hb || !status_out) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = hb,
        .timeout_ms = timeout_ms,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    if (strcmp(method, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        if (content_type) {
            esp_http_client_set_header(client, "Content-Type", content_type);
        }
        if (body) {
            esp_http_client_set_post_field(client, body, strlen(body));
        }
    } else {
        esp_http_client_set_method(client, HTTP_METHOD_GET);
    }

    esp_err_t err = esp_http_client_perform(client);
    *status_out = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err;
}

static bool build_base_url(const char *server, char *url, size_t url_size)
{
    if (!url || url_size == 0) {
        return false;
    }

    if (!server || server[0] == '\0') {
        snprintf(url, url_size, "http://localhost:%d", MIMI_A2A_PORT);
        return true;
    }

    const char *s = server;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    if (*s == '\0') {
        snprintf(url, url_size, "http://localhost:%d", MIMI_A2A_PORT);
        return true;
    }

    if (*s == ':') {
        return false;
    }

    if (strstr(s, "://")) {
        snprintf(url, url_size, "%s", s);
    } else {
        if (strchr(s, ':')) {
            snprintf(url, url_size, "http://%s", s);
        } else {
            snprintf(url, url_size, "http://%s:%d", s, MIMI_A2A_PORT);
        }
    }

    const char *scheme = strstr(url, "://");
    const char *host_start = scheme ? (scheme + 3) : url;
    if (!host_start || host_start[0] == '\0' || host_start[0] == ':' || host_start[0] == '/') {
        return false;
    }

    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/') {
        url[--len] = '\0';
    }
    return true;
}

static char *build_request_body(const char *action,
                                const char *message,
                                const char *task_id,
                                const char *client_id)
{
    const char *rpc_method = NULL;
    const char *endpoint = NULL;
    if (!action_to_paths(action, &endpoint, &rpc_method)) {
        return NULL;
    }
    (void)endpoint;

    cJSON *root = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    if (!root || !params) {
        cJSON_Delete(root);
        cJSON_Delete(params);
        return NULL;
    }

    char req_id[48];
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
    snprintf(req_id, sizeof(req_id), "a2a-client-%" PRIu64, now_ms);

    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "id", req_id);
    cJSON_AddStringToObject(root, "method", rpc_method);

    if (strcmp(action, "send") == 0) {
        cJSON_AddStringToObject(params, "client_id", client_id);
        cJSON_AddStringToObject(params, "message_text", message ? message : "");
    } else {
        cJSON_AddStringToObject(params, "task_id", task_id ? task_id : "");
    }

    cJSON_AddItemToObject(root, "params", params);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

static const char *get_input_string(cJSON *input, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(input, key);
    return (cJSON_IsString(item) && item->valuestring && item->valuestring[0] != '\0') ? item->valuestring : NULL;
}

esp_err_t tool_a2a_client_execute(const char *input_json, char *output, size_t output_size)
{
    if (!input_json || !output || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *input = cJSON_Parse(input_json);
    if (!input) {
        snprintf(output, output_size, "Error: Invalid input JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *action_item = cJSON_GetObjectItem(input, "action");
    const char *action = (cJSON_IsString(action_item) && action_item->valuestring) ? action_item->valuestring : NULL;
    if (!action) {
        cJSON_Delete(input);
        snprintf(output, output_size, "Error: Missing 'action' (send/get/cancel/agent_card)");
        return ESP_ERR_INVALID_ARG;
    }

    const char *endpoint = NULL;
    const char *rpc_method = NULL;
    if (!action_to_paths(action, &endpoint, &rpc_method)) {
        cJSON_Delete(input);
        snprintf(output, output_size, "Error: Unsupported action '%s'", action);
        return ESP_ERR_INVALID_ARG;
    }

    bool is_agent_card = (strcmp(action, "agent_card") == 0);

    cJSON *message_item = cJSON_GetObjectItem(input, "message");
    const char *message = (cJSON_IsString(message_item) && message_item->valuestring) ? message_item->valuestring : NULL;

    cJSON *task_id_item = cJSON_GetObjectItem(input, "task_id");
    const char *task_id = (cJSON_IsString(task_id_item) && task_id_item->valuestring) ? task_id_item->valuestring : NULL;

    const char *server = get_input_string(input, "server");
    if (!server) {
        server = get_input_string(input, "server_url");
    }
    if (!server) {
        server = get_input_string(input, "base_url");
    }

    char server_copy[160] = {0};
    if (server && server[0] != '\0') {
        strlcpy(server_copy, server, sizeof(server_copy));
    }

    if (strcmp(action, "send") == 0 && (!message || message[0] == '\0')) {
        cJSON_Delete(input);
        snprintf(output, output_size, "Error: 'message' is required for action=send");
        return ESP_ERR_INVALID_ARG;
    }
    if ((strcmp(action, "get") == 0 || strcmp(action, "cancel") == 0) && (!task_id || task_id[0] == '\0')) {
        cJSON_Delete(input);
        snprintf(output, output_size, "Error: 'task_id' is required for action=%s", action);
        return ESP_ERR_INVALID_ARG;
    }

    int timeout_ms = A2A_CLIENT_TIMEOUT_MS;
    cJSON *timeout_item = cJSON_GetObjectItem(input, "timeout_ms");
    if (cJSON_IsNumber(timeout_item) && timeout_item->valueint > 0) {
        timeout_ms = timeout_item->valueint;
    }

    char client_id[17] = {0};
    char *body = NULL;
    if (!is_agent_card) {
        esp_err_t cid_err = build_local_client_id(client_id, sizeof(client_id));
        if (cid_err != ESP_OK) {
            cJSON_Delete(input);
            snprintf(output, output_size, "Error: Failed to build local client_id (%s)", esp_err_to_name(cid_err));
            return cid_err;
        }

        body = build_request_body(action, message, task_id, client_id);
        if (!body) {
            cJSON_Delete(input);
            snprintf(output, output_size, "Error: Failed to build request body");
            return ESP_ERR_NO_MEM;
        }
    }
    cJSON_Delete(input);

    const char *server_final = (server_copy[0] != '\0') ? server_copy : NULL;

    char base_url[160];
    if (!build_base_url(server_final, base_url, sizeof(base_url))) {
        free(body);
        snprintf(output, output_size, "Error: Invalid target server '%s'", server_final ? server_final : "");
        return ESP_ERR_INVALID_ARG;
    }

    http_buf_t hb = {
        .data = (char *)calloc(1, A2A_CLIENT_BUF_SIZE),
        .len = 0,
        .cap = A2A_CLIENT_BUF_SIZE,
    };
    if (!hb.data) {
        free(body);
        snprintf(output, output_size, "Error: Out of memory");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ESP_OK;
    int status = 0;

    if (is_agent_card) {
        char url_card[224];
        char url_legacy[224];
        snprintf(url_card, sizeof(url_card), "%s/.well-known/agent-card.json", base_url);
        snprintf(url_legacy, sizeof(url_legacy), "%s/.well-known/agent.json", base_url);

        ESP_LOGI(TAG, "A2A agent_card -> %s", url_card);
        err = http_call(url_card, "GET", NULL, NULL, timeout_ms, &hb, &status);
        if (err == ESP_OK && status >= 200 && status < 300) {
            snprintf(output, output_size,
                     "status=%d\nserver=%s\npath=/.well-known/agent-card.json\n%s",
                     status,
                     base_url,
                     hb.data ? hb.data : "");
            free(hb.data);
            return ESP_OK;
        }

        hb.len = 0;
        hb.data[0] = '\0';

        ESP_LOGI(TAG, "A2A agent_card fallback -> %s", url_legacy);
        err = http_call(url_legacy, "GET", NULL, NULL, timeout_ms, &hb, &status);
        if (err != ESP_OK) {
            free(hb.data);
            snprintf(output, output_size, "Error: HTTP failed (%s)", esp_err_to_name(err));
            return err;
        }

        snprintf(output, output_size,
                 "status=%d\nserver=%s\npath=/.well-known/agent.json\n%s",
                 status,
                 base_url,
                 hb.data ? hb.data : "");
        free(hb.data);
        return ESP_OK;
    }

    char url[224];
    snprintf(url, sizeof(url), "%s%s", base_url, endpoint);

    ESP_LOGI(TAG, "A2A %s -> %s (server=%s)", rpc_method, url, server_final ? server_final : "(default)");
    err = http_call(url, "POST", "application/json", body, timeout_ms, &hb, &status);
    free(body);

    if (err != ESP_OK) {
        free(hb.data);
        snprintf(output, output_size, "Error: HTTP failed (%s)", esp_err_to_name(err));
        return err;
    }

    snprintf(output, output_size,
             "status=%d\nclient_id=%s\nserver=%s\n%s",
             status,
             client_id,
             base_url,
             hb.data ? hb.data : "");

    free(hb.data);
    return ESP_OK;
}
