#include "tools/tool_files.h"
#include "mimi_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "cJSON.h"
#include "mbedtls/base64.h"

static const char *TAG = "tool_files";

#define MAX_FILE_SIZE (32 * 1024)

static bool is_image_path(const char *path, const char **out_media_type)
{
    if (!path) {
        return false;
    }

    const char *ext = strrchr(path, '.');
    if (!ext) {
        return false;
    }

    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        if (out_media_type) {
            *out_media_type = "image/jpeg";
        }
        return true;
    }
    if (strcasecmp(ext, ".png") == 0) {
        if (out_media_type) {
            *out_media_type = "image/png";
        }
        return true;
    }
    if (strcasecmp(ext, ".webp") == 0) {
        if (out_media_type) {
            *out_media_type = "image/webp";
        }
        return true;
    }
    if (strcasecmp(ext, ".gif") == 0) {
        if (out_media_type) {
            *out_media_type = "image/gif";
        }
        return true;
    }

    return false;
}

static bool is_valid_image_file(FILE *f, const char *path, const char **out_media_type)
{
    if (!f || !path) {
        return false;
    }

    const char *ext = strrchr(path, '.');
    if (!ext) {
        return false;
    }

    unsigned char head[16] = {0};
    size_t n = fread(head, 1, sizeof(head), f);
    if (n < 2) {
        return false;
    }

    if ((strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) &&
        head[0] == 0xFF && head[1] == 0xD8) {
        long file_size = 0;
        if (fseek(f, 0, SEEK_END) == 0) {
            file_size = ftell(f);
            if (file_size >= 4 && fseek(f, file_size - 2, SEEK_SET) == 0) {
                unsigned char tail[2] = {0};
                if (fread(tail, 1, sizeof(tail), f) == sizeof(tail) &&
                    tail[0] == 0xFF && tail[1] == 0xD9) {
                    if (out_media_type) {
                        *out_media_type = "image/jpeg";
                    }
                    rewind(f);
                    return true;
                }
            }
        }
        rewind(f);
        return false;
    }

    if (strcasecmp(ext, ".png") == 0) {
        static const unsigned char png_sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        if (n >= sizeof(png_sig) && memcmp(head, png_sig, sizeof(png_sig)) == 0) {
            if (out_media_type) {
                *out_media_type = "image/png";
            }
            rewind(f);
            return true;
        }
        rewind(f);
        return false;
    }

    if (strcasecmp(ext, ".gif") == 0) {
        if ((n >= 6 && (memcmp(head, "GIF87a", 6) == 0 || memcmp(head, "GIF89a", 6) == 0))) {
            if (out_media_type) {
                *out_media_type = "image/gif";
            }
            rewind(f);
            return true;
        }
        rewind(f);
        return false;
    }

    if (strcasecmp(ext, ".webp") == 0) {
        if (n >= 12 && memcmp(head, "RIFF", 4) == 0 && memcmp(head + 8, "WEBP", 4) == 0) {
            if (out_media_type) {
                *out_media_type = "image/webp";
            }
            rewind(f);
            return true;
        }
        rewind(f);
        return false;
    }

    rewind(f);
    return false;
}

/**
 * Validate that a path starts with /spiffs/ and contains no ".." traversal.
 */
static bool validate_path(const char *path)
{
    if (!path) return false;
    if (strncmp(path, "/spiffs/", 8) != 0) return false;
    if (strstr(path, "..") != NULL) return false;
    return true;
}

/* ── read_file ─────────────────────────────────────────────── */

esp_err_t tool_read_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    if (!validate_path(path)) {
        snprintf(output, output_size, "Error: path must start with /spiffs/ and must not contain '..'");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const char *media_type = NULL;
    bool image_mode = is_image_path(path, &media_type);

    FILE *f = fopen(path, image_mode ? "rb" : "r");
    if (!f) {
        snprintf(output, output_size, "Error: file not found: %s", path);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    if (image_mode) {
        if (!is_valid_image_file(f, path, &media_type)) {
            fclose(f);
            snprintf(output, output_size,
                     "Error: file is not a valid image format: %s", path);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }

        size_t media_len = strlen(media_type);
        if (output_size <= media_len + 2) {
            snprintf(output, output_size, "Error: output buffer too small");
            fclose(f);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_SIZE;
        }

        size_t b64_cap = output_size - media_len - 2; /* "<media>\\n<base64>" */
        size_t raw_cap = (b64_cap / 4) * 3;
        if (raw_cap > MAX_FILE_SIZE) {
            raw_cap = MAX_FILE_SIZE;
        }
        if (raw_cap == 0) {
            snprintf(output, output_size, "Error: output buffer too small for image data");
            fclose(f);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_SIZE;
        }

        unsigned char *raw = (unsigned char *)malloc(raw_cap);
        if (!raw) {
            snprintf(output, output_size, "Error: out of memory");
            fclose(f);
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }

        size_t raw_len = fread(raw, 1, raw_cap, f);
        fclose(f);

        memcpy(output, media_type, media_len);
        output[media_len] = '\n';

        size_t out_len = 0;
        int rc = mbedtls_base64_encode((unsigned char *)(output + media_len + 1),
                                       output_size - media_len - 2,
                                       &out_len,
                                       raw,
                                       raw_len);
        free(raw);
        if (rc != 0) {
            snprintf(output, output_size, "Error: image base64 encode failed rc=%d", rc);
            cJSON_Delete(root);
            return ESP_FAIL;
        }

        output[media_len + 1 + out_len] = '\0';
        ESP_LOGI(TAG, "read_file(image): %s (%d bytes raw, %d bytes base64)",
                 path, (int)raw_len, (int)out_len);
        cJSON_Delete(root);
        return ESP_OK;
    }

    size_t max_read = output_size - 1;
    if (max_read > MAX_FILE_SIZE) max_read = MAX_FILE_SIZE;

    size_t n = fread(output, 1, max_read, f);
    output[n] = '\0';
    fclose(f);

    ESP_LOGI(TAG, "read_file: %s (%d bytes)", path, (int)n);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── write_file ────────────────────────────────────────────── */

esp_err_t tool_write_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(root, "content"));

    if (!validate_path(path)) {
        snprintf(output, output_size, "Error: path must start with /spiffs/ and must not contain '..'");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (!content) {
        snprintf(output, output_size, "Error: missing 'content' field");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(output, output_size, "Error: cannot open file for writing: %s", path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    if (written != len) {
        snprintf(output, output_size, "Error: wrote %d of %d bytes to %s", (int)written, (int)len, path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    snprintf(output, output_size, "OK: wrote %d bytes to %s", (int)written, path);
    ESP_LOGI(TAG, "write_file: %s (%d bytes)", path, (int)written);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── edit_file ─────────────────────────────────────────────── */

esp_err_t tool_edit_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *old_str = cJSON_GetStringValue(cJSON_GetObjectItem(root, "old_string"));
    const char *new_str = cJSON_GetStringValue(cJSON_GetObjectItem(root, "new_string"));

    if (!validate_path(path)) {
        snprintf(output, output_size, "Error: path must start with /spiffs/ and must not contain '..'");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (!old_str || !new_str) {
        snprintf(output, output_size, "Error: missing 'old_string' or 'new_string' field");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    /* Read existing file */
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(output, output_size, "Error: file not found: %s", path);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || file_size > MAX_FILE_SIZE) {
        snprintf(output, output_size, "Error: file too large or empty (%ld bytes)", file_size);
        fclose(f);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Allocate buffer for the result (old content + possible expansion) */
    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    size_t max_result = file_size + (new_len > old_len ? new_len - old_len : 0) + 1;
    char *buf = malloc(file_size + 1);
    char *result = malloc(max_result);
    if (!buf || !result) {
        free(buf);
        free(result);
        fclose(f);
        snprintf(output, output_size, "Error: out of memory");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    size_t n = fread(buf, 1, file_size, f);
    buf[n] = '\0';
    fclose(f);

    /* Find and replace first occurrence */
    char *pos = strstr(buf, old_str);
    if (!pos) {
        snprintf(output, output_size, "Error: old_string not found in %s", path);
        free(buf);
        free(result);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    size_t prefix_len = pos - buf;
    memcpy(result, buf, prefix_len);
    memcpy(result + prefix_len, new_str, new_len);
    size_t suffix_start = prefix_len + old_len;
    size_t suffix_len = n - suffix_start;
    memcpy(result + prefix_len + new_len, buf + suffix_start, suffix_len);
    size_t total = prefix_len + new_len + suffix_len;
    result[total] = '\0';

    free(buf);

    /* Write back */
    f = fopen(path, "w");
    if (!f) {
        snprintf(output, output_size, "Error: cannot open file for writing: %s", path);
        free(result);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    fwrite(result, 1, total, f);
    fclose(f);
    free(result);

    snprintf(output, output_size, "OK: edited %s (replaced %d bytes with %d bytes)", path, (int)old_len, (int)new_len);
    ESP_LOGI(TAG, "edit_file: %s", path);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── list_dir ──────────────────────────────────────────────── */

esp_err_t tool_list_dir_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    const char *prefix = NULL;
    if (root) {
        cJSON *pfx = cJSON_GetObjectItem(root, "prefix");
        if (pfx && cJSON_IsString(pfx)) {
            prefix = pfx->valuestring;
        }
    }

    DIR *dir = opendir(MIMI_SPIFFS_BASE);
    if (!dir) {
        snprintf(output, output_size, "Error: cannot open /spiffs directory");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t off = 0;
    struct dirent *ent;
    int count = 0;

    while ((ent = readdir(dir)) != NULL && off < output_size - 1) {
        /* Build full path: SPIFFS entries are just filenames with embedded slashes */
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", MIMI_SPIFFS_BASE, ent->d_name);

        if (prefix && strncmp(full_path, prefix, strlen(prefix)) != 0) {
            continue;
        }

        off += snprintf(output + off, output_size - off, "%s\n", full_path);
        count++;
    }

    closedir(dir);

    if (count == 0) {
        snprintf(output, output_size, "(no files found)");
    }

    ESP_LOGI(TAG, "list_dir: %d files (prefix=%s)", count, prefix ? prefix : "(none)");
    cJSON_Delete(root);
    return ESP_OK;
}
