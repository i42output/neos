/*
  multibytecodec.h

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

     * Neither the name of the copyright holder nor the names of its
       contributors may be used to endorse or promote products derived from
       this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  Portions of this file are derived from CPython 3.13 (Modules/cjkcodecs/multibytecodec.h, written by Hye-Shik Chang),
  Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software
  Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: the codecs' interface without CPython's object
  types (the decoders' output is a buffer of code points; the encoders' input is code points), for the neos Python runtime
  plugin (see runtime/python/NOTICE).
*/

/* The interface of the CJK codecs (C: the codec modules _codecs_cn.c etc. are CPython's, compiled as they are) and of their
   multibyte codec objects (multibytecodec.cpp: the module _multibytecodec) */

#ifndef NEOS_PYTHON_MULTIBYTECODEC_H
#define NEOS_PYTHON_MULTIBYTECODEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t Py_UCS4;
typedef ptrdiff_t Py_ssize_t;
typedef uint16_t ucs2_t, DBCHAR;

/*
 * A struct that provides 8 bytes of state for multibyte
 * codecs. Codecs are free to use this how they want. Note: if you
 * need to add a new field to this struct, ensure that its byte order
 * is independent of CPU endianness so that the return value of
 * getstate doesn't differ between little and big endian CPUs.
 */
typedef struct {
    unsigned char c[8];
} MultibyteCodec_State;

/* the decoders' output (CPython's is a _PyUnicodeWriter): its code points */
typedef struct {
    Py_UCS4 *data;
    Py_ssize_t pos;
    Py_ssize_t capacity;
} _PyUnicodeWriter;

/* (a code point appended: -1 if there is no memory for it) */
static inline int
neos_cjk_write_char(_PyUnicodeWriter *writer, Py_UCS4 c)
{
    if (writer->pos == writer->capacity) {
        Py_ssize_t capacity = writer->capacity < 16 ? 16 : writer->capacity * 2;
        Py_UCS4 *data = (Py_UCS4 *)realloc(writer->data, (size_t)capacity * sizeof(Py_UCS4));
        if (data == NULL)
            return -1;
        writer->data = data;
        writer->capacity = capacity;
    }
    writer->data[writer->pos++] = c;
    return 0;
}

struct _cjk_mod_state;
struct _multibyte_codec;
struct dbcs_map;

typedef int (*mbcodec_init)(const struct _multibyte_codec *codec);
/* (kind: unused; data: the input's code points (const Py_UCS4 *)) */
typedef Py_ssize_t (*mbencode_func)(MultibyteCodec_State *state,
                        const struct _multibyte_codec *codec,
                        int kind, const void *data,
                        Py_ssize_t *inpos, Py_ssize_t inlen,
                        unsigned char **outbuf, Py_ssize_t outleft,
                        int flags);
typedef int (*mbencodeinit_func)(MultibyteCodec_State *state,
                                 const struct _multibyte_codec *codec);
typedef Py_ssize_t (*mbencodereset_func)(MultibyteCodec_State *state,
                        const struct _multibyte_codec *codec,
                        unsigned char **outbuf, Py_ssize_t outleft);
typedef Py_ssize_t (*mbdecode_func)(MultibyteCodec_State *state,
                        const struct _multibyte_codec *codec,
                        const unsigned char **inbuf, Py_ssize_t inleft,
                        _PyUnicodeWriter *writer);
typedef int (*mbdecodeinit_func)(MultibyteCodec_State *state,
                                 const struct _multibyte_codec *codec);
typedef Py_ssize_t (*mbdecodereset_func)(MultibyteCodec_State *state,
                                         const struct _multibyte_codec *codec);

typedef struct _multibyte_codec {
    const char *encoding;
    const void *config;
    mbcodec_init codecinit;
    mbencode_func encode;
    mbencodeinit_func encinit;
    mbencodereset_func encreset;
    mbdecode_func decode;
    mbdecodeinit_func decinit;
    mbdecodereset_func decreset;
    struct _cjk_mod_state *modstate;
} MultibyteCodec;

#define MAXENCPENDING   2
#define MAXDECPENDING   8

/* positive values for illegal sequences */
#define MBERR_TOOSMALL          (-1) /* insufficient output buffer space */
#define MBERR_TOOFEW            (-2) /* incomplete input buffer */
#define MBERR_INTERNAL          (-3) /* internal runtime error */
#define MBERR_EXCEPTION         (-4) /* an exception has been raised */

#define MBENC_FLUSH             0x0001 /* encode all characters encodable */
#define MBENC_MAX               MBENC_FLUSH

/* a codec module (_codecs_cn, _codecs_hk, _codecs_iso2022, _codecs_jp, _codecs_kr, _codecs_tw: CPython's I_AM_A_MODULE_FOR):
   its codecs and mappings (made when first needed); -1 if there is no memory for them */
int neos_cjk_module_cn(int *num_codecs, const MultibyteCodec **codecs, int *num_mappings, const struct dbcs_map **mappings);
int neos_cjk_module_hk(int *num_codecs, const MultibyteCodec **codecs, int *num_mappings, const struct dbcs_map **mappings);
int neos_cjk_module_iso2022(int *num_codecs, const MultibyteCodec **codecs, int *num_mappings, const struct dbcs_map **mappings);
int neos_cjk_module_jp(int *num_codecs, const MultibyteCodec **codecs, int *num_mappings, const struct dbcs_map **mappings);
int neos_cjk_module_kr(int *num_codecs, const MultibyteCodec **codecs, int *num_mappings, const struct dbcs_map **mappings);
int neos_cjk_module_tw(int *num_codecs, const MultibyteCodec **codecs, int *num_mappings, const struct dbcs_map **mappings);
/* (a module by its name ("_codecs_jp"): its mappings (multibytecodec.cpp); -1 if it is not one) */
int neos_cjk_module_mappings(const char *module, int *num_mappings, const struct dbcs_map **mappings);

#ifdef __cplusplus
}
#endif

#endif
