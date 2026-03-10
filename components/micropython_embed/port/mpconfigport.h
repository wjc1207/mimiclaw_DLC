/*
 * mpconfigport.h — MicroPython Embed Configuration
 * Project  : EdgeClaw AI Agent
 * Target   : ESP32-S3 (Xtensa LX7) via ESP-IDF component
 *
 * Lessons from build debugging:
 *   - mpconfigport_common.h already defines: mp_off_t, alloca, MICROPY_MPHALPORT_H
 *     → do NOT redefine those here
 *   - mpconfig.h owns: typedef intptr_t mp_int_t / uintptr_t mp_uint_t
 *     → do NOT typedef those here (conflicting types error)
 *   - Xtensa LX7 needs MICROPY_GCREGS_SETJMP + MICROPY_NLR_SETJMP
 *   - MICROPY_PY_BUILTINS_EXECFILE requires mp_lexer_new_from_file → disabled
 *   - MICROPY_PY_IO_FILEIO + MICROPY_PY_OS require mp_builtin_open_obj
 *     → provided in port/open_stub.c
 */

#ifndef MICROPY_INCLUDED_MPCONFIGPORT_H
#define MICROPY_INCLUDED_MPCONFIGPORT_H

// ============================================================
// mpconfigport_common.h first — it provides:
//   typedef long mp_off_t
//   alloca() includes (platform-aware)
//   #define MICROPY_MPHALPORT_H "port/mphalport.h"
// It uses no unconditional overrides — safe to include first.
// ============================================================
#include <port/mpconfigport_common.h>
#include <stdint.h>
#include <limits.h>      // provides SSIZE_MAX on most platforms

// ESP-IDF newlib does not always expose SSIZE_MAX from limits.h (POSIX-only).
// Define manually as safety net — ssize_t is 32-bit on ESP32-S3 Xtensa.
#ifndef SSIZE_MAX
#define SSIZE_MAX  (0x7FFFFFFF)
#endif

// ============================================================
// ARCHITECTURE — ESP32-S3 Xtensa LX7
// ============================================================

// Xtensa has no native gc_helper_get_regs implementation.
// Must use setjmp fallback or linker error occurs in gchelper_generic.c.
#define MICROPY_GCREGS_SETJMP                   (1)

// Xtensa J instruction has ~128KB range limit.
// nlrxtensa.c causes "dangerous relocation: cannot encode: nlr_push_tail".
// Use setjmp-based NLR instead. Pair with nlrsetjmp.c in CMakeLists SRCS.
#define MICROPY_NLR_SETJMP                      (1)

// Platform identity strings — required when MICROPY_PY_SYS=1.
// mpconfig.h builds MICROPY_BANNER_MACHINE by concatenating these.
#define MICROPY_PY_SYS_PLATFORM                 "esp32s3"

// ============================================================
// ROM LEVEL — start minimal, enable features selectively below
// ============================================================
#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

// ============================================================
// CORE VM
// ============================================================
#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_GC_ALLOC_THRESHOLD              (1)
#define MICROPY_ENABLE_FINALISER                (1)
#define MICROPY_STACK_CHECK                     (1)
#define MICROPY_MALLOC_USES_ALLOCATED_SIZE      (1)
#define MICROPY_MEM_STATS                       (0)   // set 1 to debug heap usage
#define MICROPY_OPT_COMPUTED_GOTO               (1)   // faster bytecode dispatch on Xtensa

// ============================================================
// COMPILER / LANGUAGE FEATURES
// ============================================================
#define MICROPY_ENABLE_SOURCE_LINE              (1)   // line numbers in tracebacks
#define MICROPY_CPYTHON_COMPAT                  (1)   // better CPython compatibility
#define MICROPY_LONGINT_IMPL                    (MICROPY_LONGINT_IMPL_LONGLONG)
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_FLOAT)

// String features
#define MICROPY_PY_BUILTINS_STR_UNICODE         (1)
#define MICROPY_PY_BUILTINS_STR_FORMAT          (1)   // str.format()
#define MICROPY_PY_BUILTINS_STR_FORMAT_SPEC_MODS (1)

// Built-in types
#define MICROPY_PY_BUILTINS_BYTEARRAY           (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW          (1)
#define MICROPY_PY_BUILTINS_SET                 (1)
#define MICROPY_PY_BUILTINS_FROZENSET           (1)
#define MICROPY_PY_BUILTINS_SLICE               (1)
#define MICROPY_PY_BUILTINS_SLICE_ATTRS         (1)
#define MICROPY_PY_BUILTINS_ROUND_INT           (1)
#define MICROPY_PY_BUILTINS_ENUMERATE           (1)
#define MICROPY_PY_BUILTINS_FILTER              (1)
#define MICROPY_PY_BUILTINS_REVERSED            (1)
#define MICROPY_PY_BUILTINS_ZIP                 (1)
#define MICROPY_PY_BUILTINS_MAP                 (1)
#define MICROPY_PY_BUILTINS_RANGE_ATTRS         (1)
#define MICROPY_PY_BUILTINS_NOTIMPLEMENTED      (1)
#define MICROPY_PY_BUILTINS_INPUT               (0)   // ❌ requires shared/readline/readline.h
#define MICROPY_PY_BUILTINS_HELP                (0)   // ❌ requires shared/readline/readline.h
#define MICROPY_PY_BUILTINS_HELP_MODULES        (0)   // ❌ requires shared/readline/readline.h

// ============================================================
// EXCEPTION HANDLING
// ============================================================
#define MICROPY_PY_BUILTINS_EXCEPTION_CAUSE     (1)   // raise X from Y
#define MICROPY_PY_ALL_SPECIAL_METHODS          (1)
#define MICROPY_ERROR_REPORTING                 (MICROPY_ERROR_REPORTING_DETAILED)
#define MICROPY_WARNINGS                        (1)

// ============================================================
// DYNAMIC CODE EXECUTION — core feature for AI agent
//
// exec(code_string) and eval(expr) work when MICROPY_ENABLE_COMPILER=1.
// compile() gives the agent fine-grained control over code objects.
//
// EXECFILE is disabled: it requires mp_lexer_new_from_file which needs
// a full VFS/filesystem reader implementation. Use exec(open(f).read())
// as the equivalent — it works with the open_stub.c implementation.
// ============================================================
#define MICROPY_PY_BUILTINS_COMPILE             (1)   // compile()  ✅
#define MICROPY_PY_BUILTINS_EXECFILE            (0)   // execfile() ❌ needs mp_lexer_new_from_file

// ============================================================
// STANDARD MODULES
// ============================================================

// sys — required for sys.path, sys.modules, sys.exc_info
#define MICROPY_PY_SYS                          (1)
#define MICROPY_PY_SYS_MODULES                  (1)
#define MICROPY_PY_SYS_EXC_INFO                 (1)
#define MICROPY_PY_SYS_EXIT                     (1)
#define MICROPY_PY_SYS_MAXSIZE                  (0)   // ❌ uses SSIZE_MAX — unreliable on newlib
#define MICROPY_PY_SYS_STDFILES                 (1)   // stdin/stdout/stderr provided in port_stubs.c
#define MICROPY_PY_SYS_STDIO_BUFFER             (0)   // disable — not needed for agent use

// io — BytesIO and StringIO are safe; FileIO requires open_stub.c
// mp_builtin_open_obj must be provided in port/open_stub.c
#define MICROPY_PY_IO                           (1)
#define MICROPY_PY_IO_IOBASE                    (1)
#define MICROPY_PY_IO_FILEIO                    (1)   // needs open_stub.c
#define MICROPY_PY_IO_BYTESIO                   (1)   // in-memory bytes buffer
#define MICROPY_PY_IO_STRINGIO                  (1)   // in-memory string buffer

// collections
#define MICROPY_PY_COLLECTIONS                  (1)
#define MICROPY_PY_COLLECTIONS_DEQUE            (1)
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT      (1)

// math
#define MICROPY_PY_MATH                         (1)
#define MICROPY_PY_CMATH                        (0)

// struct — binary packing/unpacking
#define MICROPY_PY_STRUCT                       (1)

// json — essential for parsing AI API responses
#define MICROPY_PY_JSON                         (1)

// re — parsing and extracting code blocks from LLM output
#define MICROPY_PY_RE                           (1)
#define MICROPY_PY_RE_MATCH_GROUPS              (1)
#define MICROPY_PY_RE_MATCH_SPAN_START_END      (1)
#define MICROPY_PY_RE_SUB                       (1)

// os — file path ops, listdir, stat; needs open_stub.c
#define MICROPY_PY_OS                           (1)

// time — timestamps, ticks for agent scheduling
#define MICROPY_PY_TIME                         (1)
#define MICROPY_PY_TIME_TICKS_PERIOD            (1)

// binascii — base64 encode/decode for HTTP payloads
#define MICROPY_PY_BINASCII                     (1)
#define MICROPY_PY_BINASCII_CRC32               (1)

// hashlib — sha256 for API request signing
#define MICROPY_PY_HASHLIB                      (1)
#define MICROPY_PY_HASHLIB_SHA256               (1)
#define MICROPY_PY_HASHLIB_SHA1                 (1)

// random — sampling, temperature simulation
#define MICROPY_PY_RANDOM                       (1)
#define MICROPY_PY_RANDOM_EXTRA_FUNCS           (1)

// gc — expose GC control to Python scripts
#define MICROPY_PY_GC                           (1)

// errno — error codes for hardware drivers
#define MICROPY_PY_ERRNO                        (1)

// uctypes — C struct access from Python (useful for hardware registers)
#define MICROPY_PY_UCTYPES                      (1)

// deflate — zlib decompression (useful for compressed API responses)
#define MICROPY_PY_DEFLATE                      (1)

// ============================================================
// IMPORT SYSTEM
// ============================================================
#define MICROPY_ENABLE_EXTERNAL_IMPORT          (1)   // allow: import mymodule
#define MICROPY_MODULE_FROZEN_MPY               (0)   // no frozen .mpy (using filesystem)
#define MICROPY_MODULE_FROZEN_STR               (0)
#define MICROPY_PERSISTENT_CODE_LOAD            (1)   // load pre-compiled .mpy bytecode
#define MICROPY_MODULE_GETATTR                  (1)
#define MICROPY_COMP_MODULE_CONST               (1)

// ============================================================
// OBJECT REPRESENTATION
// ============================================================
#define MICROPY_OBJ_REPR                        (MICROPY_OBJ_REPR_A)

// ============================================================
// TYPES
// Note: mp_int_t and mp_uint_t are owned by mpconfig.h.
//       It typedefs them as intptr_t / uintptr_t.
//       Do NOT redefine them here — causes "conflicting types" error.
//       mp_off_t is already typedef'd in mpconfigport_common.h.
//       Do NOT redefine it here either.
// ============================================================

// ============================================================
// PORT-LEVEL BUILT-IN MODULE REGISTRATION
// Uncomment and extend to register custom C modules:
//
//   extern const struct _mp_obj_module_t mp_module_mygpio;
//   #define MICROPY_PORT_BUILTIN_MODULES 
//       { MP_ROM_QSTR(MP_QSTR_mygpio), MP_ROM_PTR(&mp_module_mygpio) },
// ============================================================
#define MICROPY_PORT_BUILTIN_MODULES

#endif // MICROPY_INCLUDED_MPCONFIGPORT_H