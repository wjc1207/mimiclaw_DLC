// main/mpy/mpy_runner.c
//
// Fixes applied vs original:
//   1. mp_embed_init() — removed wrong &tc third argument
//   2. mp_hal_stdout_tx_strn return type — mp_uint_t not void
//   3. Use-after-free on timeout — src/heap freed AFTER task is gone
//   4. sys.path setup — /spiffs/lib inserted before user script runs
//   5. Mutex init — moved to one-time init function with error check

#include "mpy/mpy_runner.h"
#include "mpy/mpy_gpio_module.h"
#include "micropython_embed.h"
#include "py/mphal.h"       // mp_uint_t, mp_hal_stdout_tx_strn declaration
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "mpy_runner";

#define MPY_HEAP_SIZE    (64 * 1024)   // 64 KB from PSRAM
#define MPY_OUTPUT_SIZE  (4  * 1024)   // 4  KB output capture buffer
#define MPY_TASK_STACK   (16 * 1024)   // 16 KB task stack

// Mutex — serializes script executions, also protects s_output_buf.
// Thread-safety: s_output_buf is safe because the mutex guarantees
// only one mpy_exec_task writes to it at any given time.
static SemaphoreHandle_t s_runner_mutex = NULL;

// Output buffer — written by mp_hal_stdout_tx_strn below.
// Protected by s_runner_mutex: only one script runs at a time.
static char s_output_buf[MPY_OUTPUT_SIZE];
static int  s_output_len = 0;

// Called by MicroPython for ALL print() output.
// return type must be mp_uint_t to match mphal.h declaration.
mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    int avail = MPY_OUTPUT_SIZE - s_output_len - 1;
    if (avail <= 0) return (mp_uint_t)len;  // buffer full — drop, don't crash
    int copy = (int)len < avail ? (int)len : avail;
    memcpy(s_output_buf + s_output_len, str, copy);
    s_output_len += copy;
    return (mp_uint_t)len;
}

// ── Task context ─────────────────────────────────────────────
typedef struct {
    void             *heap;     // PSRAM heap for MicroPython GC
    char             *src;      // script source — owned here, freed after task ends
    SemaphoreHandle_t done_sem; // signaled when interpreter exits cleanly
} mpy_task_ctx_t;

// ── MicroPython execution task ───────────────────────────────
static void mpy_exec_task(void *arg)
{
    mpy_task_ctx_t *tc = (mpy_task_ctx_t *)arg;

    // stack_top marks the top of the current stack for MicroPython's GC.
    // Must be a local variable in the outermost frame MicroPython will use.
    volatile int stack_top;
    mp_embed_init(tc->heap, MPY_HEAP_SIZE, (void *)&stack_top);

    // Register custom C modules (GPIO, SPI, etc.)
    mpy_gpio_modules_register();

    // FIX 4: Prepend /spiffs/lib to sys.path so Python imports resolve.
    // This runs before the user script — enables: import urequests, etc.
    mp_embed_exec_str(
        "import sys\n"
        "if '/spiffs/lib' not in sys.path:\n"
        "    sys.path.insert(0, '/spiffs/lib')\n"
    );

    // Execute the user script
    mp_embed_exec_str(tc->src);

    mp_embed_deinit();

    xSemaphoreGive(tc->done_sem);
    vTaskDelete(NULL);
}

// ── One-time initialisation ──────────────────────────────────
static esp_err_t mpy_runner_init_once(void)
{
    if (s_runner_mutex != NULL) return ESP_OK;
    s_runner_mutex = xSemaphoreCreateMutex();
    if (!s_runner_mutex) {
        ESP_LOGE(TAG, "Failed to create runner mutex");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// ── Public API ───────────────────────────────────────────────

esp_err_t mpy_runner_exec(const char *script_path, int timeout_ms,
                          char **out_buf)
{
    if (!script_path || !out_buf) return ESP_ERR_INVALID_ARG;
    *out_buf = NULL;

    // FIX 5: Proper one-time init with error propagation
    esp_err_t err = mpy_runner_init_once();
    if (err != ESP_OK) {
        *out_buf = strdup("Failed to initialise runner");
        return err;
    }

    if (xSemaphoreTake(s_runner_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        *out_buf = strdup("Another script is still running");
        return ESP_FAIL;
    }

    // Reset output capture
    s_output_len = 0;
    memset(s_output_buf, 0, sizeof(s_output_buf));

    // ── Allocate PSRAM resources ─────────────────────────────
    void *heap = heap_caps_malloc(MPY_HEAP_SIZE, MALLOC_CAP_SPIRAM);
    if (!heap) {
        xSemaphoreGive(s_runner_mutex);
        *out_buf = strdup("Failed to allocate MicroPython heap (PSRAM exhausted)");
        return ESP_ERR_NO_MEM;
    }

    FILE *f = fopen(script_path, "r");
    if (!f) {
        heap_caps_free(heap);
        xSemaphoreGive(s_runner_mutex);
        *out_buf = strdup("Cannot open script file");
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);

    char *src = heap_caps_malloc(fsize + 1, MALLOC_CAP_SPIRAM);
    if (!src) {
        fclose(f);
        heap_caps_free(heap);
        xSemaphoreGive(s_runner_mutex);
        *out_buf = strdup("Failed to allocate script source buffer");
        return ESP_ERR_NO_MEM;
    }
    size_t nread = fread(src, 1, fsize, f);
    src[nread] = '\0';
    fclose(f);

    // ── Launch FreeRTOS task ─────────────────────────────────
    SemaphoreHandle_t done_sem = xSemaphoreCreateBinary();
    if (!done_sem) {
        heap_caps_free(src);
        heap_caps_free(heap);
        xSemaphoreGive(s_runner_mutex);
        *out_buf = strdup("Failed to create done semaphore");
        return ESP_FAIL;
    }

    mpy_task_ctx_t tc = {
        .heap     = heap,
        .src      = src,
        .done_sem = done_sem,
    };

    TaskHandle_t task_handle = NULL;
    BaseType_t created = xTaskCreatePinnedToCore(
        mpy_exec_task, "mpy_exec", MPY_TASK_STACK,
        &tc, tskIDLE_PRIORITY + 1, &task_handle, tskNO_AFFINITY);

    if (created != pdPASS) {
        vSemaphoreDelete(done_sem);
        heap_caps_free(src);
        heap_caps_free(heap);
        xSemaphoreGive(s_runner_mutex);
        *out_buf = strdup("Failed to create MicroPython task");
        return ESP_FAIL;
    }

    // ── Wait for completion or timeout ───────────────────────
    bool timed_out = (xSemaphoreTake(done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE);

    if (timed_out) {
        ESP_LOGW(TAG, "Script '%s' timed out after %d ms", script_path, timeout_ms);

        // Delete the task first
        vTaskDelete(task_handle);
        task_handle = NULL;

        // FIX 3: Give the idle task two ticks to run its deletion cleanup
        // before we free src/heap that the deleted task's stack references.
        // Without this delay, heap_caps_free() can corrupt still-referenced memory.
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    vSemaphoreDelete(done_sem);

    // FIX 3: Free only here — guaranteed safe because either:
    //   a) Task called vTaskDelete(NULL) itself and gave done_sem, OR
    //   b) We called vTaskDelete(task_handle) and waited 20ms for cleanup
    heap_caps_free(src);
    heap_caps_free(heap);

    // ── Build output string ──────────────────────────────────
    s_output_buf[s_output_len] = '\0';

    if (timed_out) {
        size_t needed = s_output_len + 80;
        char  *result = malloc(needed);
        if (result) {
            snprintf(result, needed, "%s\n[Timeout: script exceeded %d ms]",
                     s_output_buf, timeout_ms);
        }
        *out_buf = result ? result : strdup("[Timeout]");
        xSemaphoreGive(s_runner_mutex);
        return ESP_FAIL;
    }

    *out_buf = strdup(s_output_buf);
    ESP_LOGI(TAG, "Script '%s' done — %d bytes output", script_path, s_output_len);
    xSemaphoreGive(s_runner_mutex);
    return ESP_OK;
}