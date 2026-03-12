#include "mpy/mpy_runner.h"
#include "mpy/mpy_gpio_module.h"
#include "mpy/mpy_stdout.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "port/micropython_embed.h"
#include "py/stackctrl.h"           /* mp_stack_set_limit */
#include "py/compile.h"             /* mp_compile           */
#include "py/lexer.h"               /* mp_lexer_new_from_str_len */
#include "py/runtime.h"             /* mp_sys_path, mp_call_function_0 */
#include "py/obj.h"                 /* mp_obj_list_append, mp_obj_new_str */

static const char *TAG = "mpy_runner";

#define CAPTURE_BUF_MAX   4096
#define MPY_HEAP_SIZE     (32 * 1024)
#define MPY_TASK_STACK    (16 * 1024)
/* Reserve 2 KB for FreeRTOS task overhead; MicroPython gets the rest */
#define MPY_STACK_LIMIT   (MPY_TASK_STACK - 2048)

/* ── Capture buffer ───────────────────────────────────────── */

typedef struct {
    char   buf[CAPTURE_BUF_MAX];
    size_t len;
} capture_ctx_t;

static capture_ctx_t *s_capture_ctx = NULL;

static void capture_hook(const char *str, size_t len)
{
    capture_ctx_t *ctx = s_capture_ctx;
    if (!ctx) return;
    size_t avail = CAPTURE_BUF_MAX - 1 - ctx->len;
    size_t copy  = (len < avail) ? len : avail;
    if (copy > 0) {
        memcpy(ctx->buf + ctx->len, str, copy);
        ctx->len += copy;
    }
}

/* ── Custom exec with real filename ───────────────────────── */

/**
 * Like mp_embed_exec_str() but uses @p filename as the source name
 * so tracebacks display the actual script path.
 */
static void mpy_exec_str_with_filename(const char *src, const char *filename)
{
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        qstr src_name = qstr_from_str(filename);
        mp_lexer_t *lex = mp_lexer_new_from_str_len(src_name, src, strlen(src), 0);
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, src_name, false);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}

/* ── Task ─────────────────────────────────────────────────── */

typedef struct {
    const char       *script_src;
    const char       *script_path;
    int               result;
    SemaphoreHandle_t done_sem;
    capture_ctx_t    *ctx;
} mpy_task_ctx_t;

static void mpy_exec_task(void *arg)
{
    mpy_task_ctx_t *tc = (mpy_task_ctx_t *)arg;

    void *heap = heap_caps_malloc(MPY_HEAP_SIZE,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!heap) {
        ESP_LOGE(TAG, "Failed to allocate MicroPython heap (%d B)", MPY_HEAP_SIZE);
        tc->result = -1;
        xSemaphoreGive(tc->done_sem);
        vTaskDelete(NULL);
        return;
    }

    s_capture_ctx = tc->ctx;
    mpy_stdout_set_hook(capture_hook);

    /* Use a local variable on THIS task's stack as the stack top marker.
     * Then immediately set the stack limit so MICROPY_STACK_CHECK works
     * correctly within the FreeRTOS task stack bounds.                  */
    volatile int stack_top;
    mp_embed_init(heap, MPY_HEAP_SIZE, (void *)&stack_top);
    mp_stack_set_limit(MPY_STACK_LIMIT);   /* ← must call after mp_embed_init */

    mpy_register_hw_modules();

    /* Populate sys.path so 'import mylib' finds /spiffs/scripts/mylib.py */
    mp_obj_list_append(mp_sys_path, mp_obj_new_str("/spiffs", 7));
    mp_obj_list_append(mp_sys_path, mp_obj_new_str("/spiffs/scripts", 15));

    mpy_exec_str_with_filename(tc->script_src, tc->script_path);
    mp_embed_deinit();

    mpy_stdout_set_hook(NULL);
    s_capture_ctx = NULL;
    heap_caps_free(heap);

    tc->result = 0;
    xSemaphoreGive(tc->done_sem);
    vTaskDelete(NULL);
}

/* ── Public API ───────────────────────────────────────────── */

esp_err_t mpy_runner_exec(const char *script_path, int timeout_ms,
                          char **out_buf)
{
    if (!script_path || !out_buf) return ESP_ERR_INVALID_ARG;

    FILE *f = fopen(script_path, "r");
    if (!f) { *out_buf = strdup("Cannot open script file"); return ESP_FAIL; }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 64 * 1024) {
        fclose(f);
        *out_buf = strdup("Script file empty or too large (max 64 KB)");
        return ESP_FAIL;
    }

    char *script_src = malloc((size_t)fsize + 1);
    if (!script_src) {
        fclose(f);
        *out_buf = strdup("Out of memory reading script");
        return ESP_FAIL;
    }
    size_t nread = fread(script_src, 1, (size_t)fsize, f);
    fclose(f);
    script_src[nread] = '\0';

    capture_ctx_t ctx = { .len = 0 };
    ctx.buf[0] = '\0';

    SemaphoreHandle_t done_sem = xSemaphoreCreateBinary();
    if (!done_sem) {
        free(script_src);
        *out_buf = strdup("Failed to create semaphore");
        return ESP_FAIL;
    }

    mpy_task_ctx_t tc = {
        .script_src  = script_src,
        .script_path = script_path,
        .result      = -1,
        .done_sem    = done_sem,
        .ctx         = &ctx,
    };

    TaskHandle_t task_handle = NULL;
    BaseType_t created = xTaskCreatePinnedToCore(
        mpy_exec_task, "mpy_exec", MPY_TASK_STACK / sizeof(StackType_t),
        &tc, tskIDLE_PRIORITY + 1, &task_handle, tskNO_AFFINITY);

    if (created != pdPASS) {
        vSemaphoreDelete(done_sem);
        free(script_src);
        *out_buf = strdup("Failed to create MicroPython task");
        return ESP_FAIL;
    }

    bool timed_out = (xSemaphoreTake(done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE);
    if (timed_out) {
        ESP_LOGW(TAG, "Script timed out after %d ms", timeout_ms);
        vTaskDelete(task_handle);
        mpy_stdout_set_hook(NULL);
        s_capture_ctx = NULL;
    }
    vSemaphoreDelete(done_sem);
    free(script_src);

    ctx.buf[ctx.len] = '\0';

    if (timed_out) {
        size_t needed = ctx.len + 64;
        char *result  = malloc(needed);
        if (result) snprintf(result, needed, "%s\n[Timeout: exceeded %d ms]",
                             ctx.buf, timeout_ms);
        *out_buf = result ? result : strdup("[Timeout]");
        return ESP_FAIL;
    }

    if (ctx.len > 0 &&
        strstr(ctx.buf, "Traceback (most recent call last):") != NULL) {
        *out_buf = strdup(ctx.buf);
        ESP_LOGI(TAG, "Script %s finished with exception", script_path);
        return ESP_FAIL;
    }

    *out_buf = strdup(ctx.buf);
    ESP_LOGI(TAG, "Script %s finished (ok)", script_path);
    return ESP_OK;
}