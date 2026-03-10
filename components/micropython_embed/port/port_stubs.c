/*
 * port_stubs.c — MicroPython Port-Level Symbol Stubs
 * Project : EdgeClaw / SwarmClaw AI Agent
 * Target  : ESP32-S3 via ESP-IDF
 *
 * Place this file in: components/micropython_embed/port/port_stubs.c
 * Add to CMakeLists.txt SRCS: "port/port_stubs.c"
 *
 * Symbols provided HERE:
 *   mp_sys_stdin_obj          — sys.stdin  (EOF stream)
 *   mp_sys_stdout_obj         — sys.stdout (routes to mp_hal_stdout_tx_strn)
 *   mp_sys_stderr_obj         — sys.stderr (routes to mp_hal_stdout_tx_strn)
 *   mp_builtin_open_obj       — open() builtin (real fopen-based)
 *   mp_import_stat            — real stat() for import system
 *   mp_lexer_new_from_file    — real file loader for import system
 *
 * Symbols NOT provided here (defined elsewhere):
 *   mp_hal_stdout_tx_strn     — defined in main/mpy/mpy_runner.c
 *   mp_sys_stdout_print       — defined in py/modsys.c
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>    // stat(), S_ISDIR

#include "py/obj.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "py/builtin.h"
#include "py/lexer.h"
#include "py/reader.h"   // mp_reader_t, mp_reader_new_mem
#include "py/mphal.h"
#include "py/bc.h"       // mp_import_stat_t enum
#include "py/mperrno.h"  // MP_ENOENT, MP_EACCES

// ============================================================
// SECTION 1: UART OUTPUT
// mp_hal_stdout_tx_strn is defined in main/mpy/mpy_runner.c
// — it captures print() output into s_output_buf there.
// DO NOT define here.
// ============================================================

// ============================================================
// SECTION 2: sys.stdin / sys.stdout / sys.stderr
//
// modsys.c externs these as mp_obj_t.
// stdout/stderr route through mp_hal_stdout_tx_strn (output capture).
// stdin returns EOF — no interactive terminal on embedded.
// ============================================================

static mp_uint_t mp_stdout_write(mp_obj_t self_in, const void *buf, mp_uint_t size, int *errcode) {
    (void)self_in;
    mp_hal_stdout_tx_strn((const char *)buf, size);
    *errcode = 0;
    return size;
}

static mp_uint_t mp_stdin_read(mp_obj_t self_in, void *buf, mp_uint_t size, int *errcode) {
    (void)self_in; (void)buf; (void)size;
    *errcode = 0;
    return 0;  // EOF — no interactive input on embedded
}

// Stream protocol tables
static const mp_stream_p_t mp_stdout_stream_p = { .write = mp_stdout_write };
static const mp_stream_p_t mp_stdin_stream_p  = { .read  = mp_stdin_read  };
static const mp_stream_p_t mp_stderr_stream_p = { .write = mp_stdout_write };

// Object types — use MP_QSTR_object (always exists in qstrdefs)
MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_stdout_obj_type, MP_QSTR_object, MP_TYPE_FLAG_NONE,
    protocol, &mp_stdout_stream_p
);
MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_stdin_obj_type, MP_QSTR_object, MP_TYPE_FLAG_NONE,
    protocol, &mp_stdin_stream_p
);
MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_stderr_obj_type, MP_QSTR_object, MP_TYPE_FLAG_NONE,
    protocol, &mp_stderr_stream_p
);

// Singleton base objects
static const mp_obj_base_t stdin_obj_base  = { &mp_type_stdin_obj_type  };
static const mp_obj_base_t stdout_obj_base = { &mp_type_stdout_obj_type };
static const mp_obj_base_t stderr_obj_base = { &mp_type_stderr_obj_type };

// Exported as mp_obj_t — required by modsys.c externs
const mp_obj_t mp_sys_stdin_obj  = (mp_obj_t)&stdin_obj_base;
const mp_obj_t mp_sys_stdout_obj = (mp_obj_t)&stdout_obj_base;
const mp_obj_t mp_sys_stderr_obj = (mp_obj_t)&stderr_obj_base;

// NOTE: mp_sys_stdout_print is already defined in modsys.c

// ============================================================
// SECTION 3: open() builtin
//
// Real fopen-based implementation.
// Python: f = open('/spiffs/lib/urequests.py', 'r')
//
// For full VFS support replace with mp_vfs_open().
// This implementation covers the common read/write cases needed
// for importing modules and reading config files.
// ============================================================

// Simple file object backed by fopen
typedef struct _mp_file_obj_t {
    mp_obj_base_t base;
    FILE         *fp;
    bool          is_text;
} mp_file_obj_t;

static mp_uint_t file_obj_read(mp_obj_t self_in, void *buf, mp_uint_t size, int *errcode) {
    mp_file_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->fp) { *errcode = MP_EBADF; return MP_STREAM_ERROR; }
    size_t n = fread(buf, 1, size, self->fp);
    *errcode = 0;
    return (mp_uint_t)n;
}

static mp_uint_t file_obj_write(mp_obj_t self_in, const void *buf, mp_uint_t size, int *errcode) {
    mp_file_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->fp) { *errcode = MP_EBADF; return MP_STREAM_ERROR; }
    size_t n = fwrite(buf, 1, size, self->fp);
    *errcode = 0;
    return (mp_uint_t)n;
}

static mp_obj_t file_obj_close(mp_obj_t self_in) {
    mp_file_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->fp) {
        fclose(self->fp);
        self->fp = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(file_obj_close_obj, file_obj_close);

// __enter__ / __exit__ for 'with open(...) as f:'
static mp_obj_t file_obj_enter(mp_obj_t self_in) { return self_in; }
static MP_DEFINE_CONST_FUN_OBJ_1(file_obj_enter_obj, file_obj_enter);

static mp_obj_t file_obj_exit(size_t n_args, const mp_obj_t *args) {
    return file_obj_close(args[0]);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(file_obj_exit_obj, 4, 4, file_obj_exit);

static const mp_stream_p_t file_stream_p = {
    .read  = file_obj_read,
    .write = file_obj_write,
};

static const mp_rom_map_elem_t file_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_close),    MP_ROM_PTR(&file_obj_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&file_obj_enter_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__),  MP_ROM_PTR(&file_obj_exit_obj)  },
    { MP_ROM_QSTR(MP_QSTR_read),      MP_ROM_PTR(&mp_stream_read_obj)    },
    { MP_ROM_QSTR(MP_QSTR_write),     MP_ROM_PTR(&mp_stream_write_obj)   },
    { MP_ROM_QSTR(MP_QSTR_readinto),  MP_ROM_PTR(&mp_stream_readinto_obj) },
};
static MP_DEFINE_CONST_DICT(file_locals, file_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_file_obj, MP_QSTR_object, MP_TYPE_FLAG_NONE,
    protocol, &file_stream_p,
    locals_dict, &file_locals
);

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    if (n_args < 1) mp_raise_TypeError(MP_ERROR_TEXT("open requires filename"));

    const char *path = mp_obj_str_get_str(args[0]);
    const char *mode = "r";
    if (n_args >= 2) mode = mp_obj_str_get_str(args[1]);

    FILE *fp = fopen(path, mode);
    if (!fp) mp_raise_OSError(MP_ENOENT);

    mp_file_obj_t *obj = mp_obj_malloc(mp_file_obj_t, &mp_type_file_obj);
    obj->fp      = fp;
    obj->is_text = (strchr(mode, 'b') == NULL);
    return MP_OBJ_FROM_PTR(obj);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

// ============================================================
// SECTION 4: mp_import_stat
//
// Real stat()-based implementation.
// Enables: import urequests, import json, import your_module
//
// sys.path is set to ['/spiffs/lib'] in mpy_runner.c so MicroPython
// will call mp_import_stat('/spiffs/lib/urequests.py') etc.
// ============================================================
mp_import_stat_t mp_import_stat(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode)
            ? MP_IMPORT_STAT_DIR
            : MP_IMPORT_STAT_FILE;
    }
    return MP_IMPORT_STAT_NO_EXIST;
}

// ============================================================
// SECTION 5: mp_lexer_new_from_file
//
// Real file-reading implementation — loads .py source into memory
// and creates a MicroPython lexer for compilation.
//
// Called by builtinimport.c for every 'import mymodule' statement.
// ============================================================
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    const char *path = qstr_str(filename);

    FILE *f = fopen(path, "r");
    if (!f) {
        mp_raise_OSError(MP_ENOENT);
        return NULL;
    }

    // Read entire file into a MicroPython-managed buffer
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        mp_raise_OSError(MP_EIO);
        return NULL;
    }

    byte *buf = m_new(byte, size + 1);
    size_t nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);

    // Create in-memory reader (true = GC owns buf, will free it)
    mp_reader_t reader;
    mp_reader_new_mem(&reader, buf, nread, true);
    return mp_lexer_new(filename, reader);
}