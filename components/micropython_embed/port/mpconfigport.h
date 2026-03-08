<<<<<<< HEAD
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

// components/micropython_embed/port/mpconfigport.h
// Add this line — tells GC to use setjmp fallback instead of arch-specific registers
#define MICROPY_GCREGS_SETJMP               (1)
=======
// MicroPython configuration for EdgeClaw (ESP32-S3 embed)

#include <port/mpconfigport_common.h>

// Enable commonly needed features for IoT scripting
#define MICROPY_CONFIG_ROM_LEVEL        (MICROPY_CONFIG_ROM_LEVEL_BASIC_FEATURES)
#define MICROPY_ENABLE_COMPILER         (1)
#define MICROPY_ENABLE_GC               (1)
#define MICROPY_PY_GC                   (1)
>>>>>>> 4f6d161cc529d9c7a4b43413520c4036a228fe2d
