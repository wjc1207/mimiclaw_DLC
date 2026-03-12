#pragma once
#include <stddef.h>

/* Called by MicroPython's print() via MP_PLAT_PRINT_STRN override
 * in mpconfigport.h.  No mphalport.c/h involvement. */
typedef void (*mpy_stdout_hook_t)(const char *str, size_t len);

void              mpy_stdout_set_hook(mpy_stdout_hook_t hook);
mpy_stdout_hook_t mpy_stdout_get_hook(void);

/* Called from the MP_PLAT_PRINT_STRN macro — do not call directly. */
void mpy_stdout_write(const char *str, size_t len);