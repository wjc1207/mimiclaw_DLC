/* This file is part of the MicroPython project, http://micropython.org/
 * The MIT License (MIT)
 * Copyright (c) 2022-2023 Damien P. George
 */

// Include common MicroPython embed configuration.
#include <port/mpconfigport_common.h>

// Use the minimal starting configuration (disables all optional features).
#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

// MicroPython configuration.
#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_PY_GC                           (1)
#define MICROPY_PY_SYS                          (0)

// ESP32-S3: Xtensa architecture not supported by native GC register scanner
// Must use setjmp fallback for both GC and NLR
#ifndef MICROPY_GCREGS_SETJMP
#define MICROPY_GCREGS_SETJMP   (1)
#endif
#ifndef MICROPY_NLR_SETJMP
#define MICROPY_NLR_SETJMP      (1)
#endif