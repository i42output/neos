/*
  c.c

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
  neos.runtime.c: the C runtime as a neos runtime plugin (script namespace "c").

  Functions of the C runtime are called directly (through libffi); a few small wrappers adapt functions whose
  signatures cannot be used as they are (pointer arguments, implementation defined types).

  Functions that can change the host environment (file I/O, removing and renaming files, running commands,
  setting environment variables) are flagged NEOS_RT_RESTRICTED: scripts running in restricted mode cannot call
  them.
*/

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* (setenv) */
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <neos/runtime/plugin.h>

/* wrappers */

static int64_t c_time(void)
{
    return (int64_t)time(NULL);
}

static int64_t c_clock(void)
{
    return (int64_t)clock();
}

static int64_t c_clocks_per_sec(void)
{
    return (int64_t)CLOCKS_PER_SEC;
}

static char const* c_getenv(char const* name)
{
    char const* const value = getenv(name);
    return value != NULL ? value : "";
}

static int c_flush(void)
{
    return fflush(stdout);
}

/* (output is flushed so that it interleaves with the host's output: the plugin may have its own C runtime) */
static int c_puts(char const* s)
{
    int const result = puts(s);
    fflush(stdout);
    return result;
}

static int c_putchar(int c)
{
    int const result = putchar(c);
    fflush(stdout);
    return result;
}

static bool c_has_env(char const* name)
{
    return getenv(name) != NULL;
}

static char const* c_strerror(int error)
{
    return strerror(error);
}

static char const* c_ctime(int64_t seconds)
{
    /* (a static buffer: the VM copies the result) */
    static char buffer[64];
    time_t const t = (time_t)seconds;
    struct tm const* const utc = gmtime(&t);
    if (utc == NULL || strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", utc) == 0)
        return "";
    return buffer;
}

static char const* c_strstr(char const* haystack, char const* needle)
{
    /* the rest of haystack from the first occurrence of needle (empty if none) */
    char const* const found = strstr(haystack, needle);
    return found != NULL ? found : "";
}

static int64_t c_strspn(char const* s, char const* accept)
{
    return (int64_t)strspn(s, accept);
}

static int64_t c_strcspn(char const* s, char const* reject)
{
    return (int64_t)strcspn(s, reject);
}

/* file I/O: scripts identify files by handles (1..C_MAX_FILES; 0 is "no file") */

#define C_MAX_FILES 64

typedef struct c_file
{
    FILE* file;
    char* line;
    size_t capacity;
} c_file;

static c_file sFiles[C_MAX_FILES];

static c_file* c_file_of(int32_t handle)
{
    return (handle >= 1 && handle <= C_MAX_FILES && sFiles[handle - 1].file != NULL) ? &sFiles[handle - 1] : NULL;
}

static int32_t c_fopen(char const* path, char const* mode)
{
    int32_t i;
    for (i = 0; i < C_MAX_FILES; ++i)
        if (sFiles[i].file == NULL)
        {
            FILE* const file = fopen(path, mode);
            if (file == NULL)
                return 0;
            sFiles[i].file = file;
            return i + 1;
        }
    return 0;
}

static int32_t c_fclose(int32_t handle)
{
    c_file* const f = c_file_of(handle);
    int result;
    if (f == NULL)
        return EOF;
    result = fclose(f->file);
    free(f->line);
    f->file = NULL;
    f->line = NULL;
    f->capacity = 0u;
    return result;
}

static int32_t c_fputs(char const* s, int32_t handle)
{
    c_file* const f = c_file_of(handle);
    return f != NULL ? fputs(s, f->file) : EOF;
}

static char const* c_fgets(int32_t handle)
{
    /* the next line (including its newline); empty at the end of the file */
    c_file* const f = c_file_of(handle);
    size_t length = 0u;
    int c;
    if (f == NULL)
        return "";
    for (;;)
    {
        if (length + 1u >= f->capacity)
        {
            size_t const capacity = f->capacity == 0u ? 256u : f->capacity * 2u;
            char* const line = (char*)realloc(f->line, capacity);
            if (line == NULL)
                break;
            f->line = line;
            f->capacity = capacity;
        }
        c = fgetc(f->file);
        if (c == EOF)
            break;
        f->line[length++] = (char)c;
        if (c == '\n')
            break;
    }
    if (f->line == NULL)
        return "";
    f->line[length] = '\0';
    return f->line;
}

static bool c_feof(int32_t handle)
{
    /* true if there is nothing more to read */
    c_file* const f = c_file_of(handle);
    int c;
    if (f == NULL || feof(f->file))
        return true;
    c = fgetc(f->file);
    if (c == EOF)
        return true;
    ungetc(c, f->file);
    return false;
}

static int32_t c_fflush_file(int32_t handle)
{
    c_file* const f = c_file_of(handle);
    return f != NULL ? fflush(f->file) : EOF;
}

static int32_t c_setenv(char const* name, char const* value)
{
#if defined(_WIN32)
    return _putenv_s(name, value) == 0 ? 0 : -1;
#else
    return setenv(name, value, 1);
#endif
}

static int32_t c_system(char const* command)
{
    fflush(stdout);
    return system(command);
}

static int64_t c_llabs(int64_t n)
{
    return (int64_t)llabs((long long)n);
}

static int64_t c_atoll(char const* s)
{
    return (int64_t)atoll(s);
}

static int64_t c_strtoll(char const* s, int base)
{
    return (int64_t)strtoll(s, NULL, base);
}

static char c_char_at(char const* s, int64_t index)
{
    /* (bounds checked) */
    size_t const length = strlen(s);
    return (index >= 0 && (size_t)index < length) ? s[index] : '\0';
}

#define F0(n, f, r) { n, NEOS_RT_FUNCTION(f), r, 0u, { NEOS_RT_VOID }, 0u }
#define F1(n, f, r, a) { n, NEOS_RT_FUNCTION(f), r, 1u, { a }, 0u }
#define F2(n, f, r, a, b) { n, NEOS_RT_FUNCTION(f), r, 2u, { a, b }, 0u }
#define F3(n, f, r, a, b, c) { n, NEOS_RT_FUNCTION(f), r, 3u, { a, b, c }, 0u }
/* functions that can change the host environment */
#define R1(n, f, r, a) { n, NEOS_RT_FUNCTION(f), r, 1u, { a }, NEOS_RT_RESTRICTED }
#define R2(n, f, r, a, b) { n, NEOS_RT_FUNCTION(f), r, 2u, { a, b }, NEOS_RT_RESTRICTED }

static neos_rt_function const sFunctions[] =
{
    /* <math.h> */
    F1("sqrt", sqrt, NEOS_RT_F64, NEOS_RT_F64),
    F1("cbrt", cbrt, NEOS_RT_F64, NEOS_RT_F64),
    F2("pow", pow, NEOS_RT_F64, NEOS_RT_F64, NEOS_RT_F64),
    F1("exp", exp, NEOS_RT_F64, NEOS_RT_F64),
    F1("exp2", exp2, NEOS_RT_F64, NEOS_RT_F64),
    F1("log", log, NEOS_RT_F64, NEOS_RT_F64),
    F1("log2", log2, NEOS_RT_F64, NEOS_RT_F64),
    F1("log10", log10, NEOS_RT_F64, NEOS_RT_F64),
    F1("sin", sin, NEOS_RT_F64, NEOS_RT_F64),
    F1("cos", cos, NEOS_RT_F64, NEOS_RT_F64),
    F1("tan", tan, NEOS_RT_F64, NEOS_RT_F64),
    F1("asin", asin, NEOS_RT_F64, NEOS_RT_F64),
    F1("acos", acos, NEOS_RT_F64, NEOS_RT_F64),
    F1("atan", atan, NEOS_RT_F64, NEOS_RT_F64),
    F2("atan2", atan2, NEOS_RT_F64, NEOS_RT_F64, NEOS_RT_F64),
    F1("sinh", sinh, NEOS_RT_F64, NEOS_RT_F64),
    F1("cosh", cosh, NEOS_RT_F64, NEOS_RT_F64),
    F1("tanh", tanh, NEOS_RT_F64, NEOS_RT_F64),
    F1("floor", floor, NEOS_RT_F64, NEOS_RT_F64),
    F1("ceil", ceil, NEOS_RT_F64, NEOS_RT_F64),
    F1("trunc", trunc, NEOS_RT_F64, NEOS_RT_F64),
    F1("round", round, NEOS_RT_F64, NEOS_RT_F64),
    F1("fabs", fabs, NEOS_RT_F64, NEOS_RT_F64),
    F2("fmod", fmod, NEOS_RT_F64, NEOS_RT_F64, NEOS_RT_F64),
    F2("hypot", hypot, NEOS_RT_F64, NEOS_RT_F64, NEOS_RT_F64),
    F2("fmin", fmin, NEOS_RT_F64, NEOS_RT_F64, NEOS_RT_F64),
    F2("fmax", fmax, NEOS_RT_F64, NEOS_RT_F64, NEOS_RT_F64),
    F2("copysign", copysign, NEOS_RT_F64, NEOS_RT_F64, NEOS_RT_F64),
    F1("sqrtf", sqrtf, NEOS_RT_F32, NEOS_RT_F32),
    F1("sinf", sinf, NEOS_RT_F32, NEOS_RT_F32),
    F1("cosf", cosf, NEOS_RT_F32, NEOS_RT_F32),
    F2("powf", powf, NEOS_RT_F32, NEOS_RT_F32, NEOS_RT_F32),
    F1("fabsf", fabsf, NEOS_RT_F32, NEOS_RT_F32),
    /* <stdlib.h> */
    F1("abs", abs, NEOS_RT_I32, NEOS_RT_I32),
    F1("llabs", c_llabs, NEOS_RT_I64, NEOS_RT_I64),
    F0("rand", rand, NEOS_RT_I32),
    F1("srand", srand, NEOS_RT_VOID, NEOS_RT_U32),
    F1("atoi", atoi, NEOS_RT_I32, NEOS_RT_STRING),
    F1("atoll", c_atoll, NEOS_RT_I64, NEOS_RT_STRING),
    F1("atof", atof, NEOS_RT_F64, NEOS_RT_STRING),
    F2("strtoll", c_strtoll, NEOS_RT_I64, NEOS_RT_STRING, NEOS_RT_I32),
    F1("getenv", c_getenv, NEOS_RT_STRING, NEOS_RT_STRING),
    F1("has_env", c_has_env, NEOS_RT_BOOL, NEOS_RT_STRING),
    /* <string.h> */
    F1("strlen", strlen, NEOS_RT_SIZE, NEOS_RT_STRING),
    F2("strcmp", strcmp, NEOS_RT_I32, NEOS_RT_STRING, NEOS_RT_STRING),
    F3("strncmp", strncmp, NEOS_RT_I32, NEOS_RT_STRING, NEOS_RT_STRING, NEOS_RT_SIZE),
    F2("strstr", c_strstr, NEOS_RT_STRING, NEOS_RT_STRING, NEOS_RT_STRING),
    F2("strspn", c_strspn, NEOS_RT_I64, NEOS_RT_STRING, NEOS_RT_STRING),
    F2("strcspn", c_strcspn, NEOS_RT_I64, NEOS_RT_STRING, NEOS_RT_STRING),
    F1("strerror", c_strerror, NEOS_RT_STRING, NEOS_RT_I32),
    F2("char_at", c_char_at, NEOS_RT_CHAR, NEOS_RT_STRING, NEOS_RT_I64),
    /* <ctype.h> */
    F1("toupper", toupper, NEOS_RT_I32, NEOS_RT_I32),
    F1("tolower", tolower, NEOS_RT_I32, NEOS_RT_I32),
    F1("isalpha", isalpha, NEOS_RT_I32, NEOS_RT_I32),
    F1("isdigit", isdigit, NEOS_RT_I32, NEOS_RT_I32),
    F1("isalnum", isalnum, NEOS_RT_I32, NEOS_RT_I32),
    F1("isspace", isspace, NEOS_RT_I32, NEOS_RT_I32),
    F1("isupper", isupper, NEOS_RT_I32, NEOS_RT_I32),
    F1("islower", islower, NEOS_RT_I32, NEOS_RT_I32),
    F1("ispunct", ispunct, NEOS_RT_I32, NEOS_RT_I32),
    F1("isxdigit", isxdigit, NEOS_RT_I32, NEOS_RT_I32),
    /* <stdio.h> */
    F1("puts", c_puts, NEOS_RT_I32, NEOS_RT_STRING),
    F1("putchar", c_putchar, NEOS_RT_I32, NEOS_RT_I32),
    F0("fflush", c_flush, NEOS_RT_I32),
    /* <time.h> */
    F0("time", c_time, NEOS_RT_I64),
    F0("clock", c_clock, NEOS_RT_I64),
    F0("clocks_per_sec", c_clocks_per_sec, NEOS_RT_I64),
    F1("ctime", c_ctime, NEOS_RT_STRING, NEOS_RT_I64),
    /* file I/O (restricted) */
    R2("fopen", c_fopen, NEOS_RT_I32, NEOS_RT_STRING, NEOS_RT_STRING),
    R1("fclose", c_fclose, NEOS_RT_I32, NEOS_RT_I32),
    R2("fputs", c_fputs, NEOS_RT_I32, NEOS_RT_STRING, NEOS_RT_I32),
    R1("fgets", c_fgets, NEOS_RT_STRING, NEOS_RT_I32),
    R1("feof", c_feof, NEOS_RT_BOOL, NEOS_RT_I32),
    R1("fflush_file", c_fflush_file, NEOS_RT_I32, NEOS_RT_I32),
    R1("remove", remove, NEOS_RT_I32, NEOS_RT_STRING),
    R2("rename", rename, NEOS_RT_I32, NEOS_RT_STRING, NEOS_RT_STRING),
    /* processes and the environment (restricted) */
    R1("system", c_system, NEOS_RT_I32, NEOS_RT_STRING),
    R2("setenv", c_setenv, NEOS_RT_I32, NEOS_RT_STRING, NEOS_RT_STRING),
};

static neos_rt_plugin const sPlugin =
{
    NEOS_RUNTIME_PLUGIN_VERSION,
    "c",
    "The C runtime",
    sizeof(sFunctions) / sizeof(sFunctions[0]),
    sFunctions
};

NEOS_RUNTIME_PLUGIN_ENTRY
{
    return &sPlugin;
}
