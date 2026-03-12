#include "mpy/mpy_stdout.h"
#include <stdio.h>

static mpy_stdout_hook_t s_hook = NULL;

void mpy_stdout_set_hook(mpy_stdout_hook_t hook) { s_hook = hook; }
mpy_stdout_hook_t mpy_stdout_get_hook(void)       { return s_hook; }

/* Invoked by MP_PLAT_PRINT_STRN — replaces the default printf path. */
void mpy_stdout_write(const char *str, size_t len)
{
    if (s_hook) {
        s_hook(str, len);
    } else {
        /* Fallback when no capture is active */
        fwrite(str, 1, len, stdout);
    }
}