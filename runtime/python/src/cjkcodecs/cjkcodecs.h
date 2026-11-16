/*
  cjkcodecs.h

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

  Portions of this file are derived from CPython 3.13 (Modules/cjkcodecs/cjkcodecs.h, written by Hye-Shik Chang),
  Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software
  Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: the codec modules' common header without CPython's
  object model (a module's codecs and mappings are its accessor's, neos_cjk_module_<locale>; imported maps are found through
  neos_cjk_module_mappings; the encoders read code points and the decoders write them to a buffer), for the neos Python
  runtime plugin (see runtime/python/NOTICE).
*/

/* The common header of CPython's CJK codec modules (_codecs_cn.c, _codecs_hk.c, _codecs_iso2022.c, _codecs_jp.c, _codecs_kr.c and
   _codecs_tw.c, which are CPython's and compiled as they are (C)) */

#ifndef _CJKCODECS_H_
#define _CJKCODECS_H_

#include <assert.h>
#include <string.h>
#include "multibytecodec.h"


/* a unicode "undefined" code point */
#define UNIINV  0xFFFE

/* internal-use DBCS code points which aren't used by any charsets */
#define NOCHAR  0xFFFF
#define MULTIC  0xFFFE
#define DBCINV  0xFFFD

/* shorter macros to save source size of mapping tables */
#define U UNIINV
#define N NOCHAR
#define M MULTIC
#define D DBCINV

struct dbcs_index {
    const ucs2_t *map;
    unsigned char bottom, top;
};
typedef struct dbcs_index decode_map;

struct widedbcs_index {
    const Py_UCS4 *map;
    unsigned char bottom, top;
};
typedef struct widedbcs_index widedecode_map;

struct unim_index {
    const DBCHAR *map;
    unsigned char bottom, top;
};
typedef struct unim_index encode_map;

struct unim_index_bytebased {
    const unsigned char *map;
    unsigned char bottom, top;
};

struct dbcs_map {
    const char *charset;
    const struct unim_index *encmap;
    const struct dbcs_index *decmap;
};

struct pair_encodemap {
    Py_UCS4 uniseq;
    DBCHAR code;
};

#ifndef CJK_MOD_SPECIFIC_STATE
#define CJK_MOD_SPECIFIC_STATE
#endif

typedef struct _cjk_mod_state {
    int num_mappings;
    int num_codecs;
    struct dbcs_map *mapping_list;
    MultibyteCodec *codec_list;

    CJK_MOD_SPECIFIC_STATE
} cjkcodecs_module_state;

#define CODEC_INIT(encoding)                                            \
    static int encoding##_codec_init(const MultibyteCodec *codec)

#define ENCODER_INIT(encoding)                                          \
    static int encoding##_encode_init(                                  \
        MultibyteCodec_State *state, const MultibyteCodec *codec)
#define ENCODER(encoding)                                               \
    static Py_ssize_t encoding##_encode(                                \
        MultibyteCodec_State *state, const MultibyteCodec *codec,       \
        int kind, const void *data,                                     \
        Py_ssize_t *inpos, Py_ssize_t inlen,                            \
        unsigned char **outbuf, Py_ssize_t outleft, int flags)
#define ENCODER_RESET(encoding)                                         \
    static Py_ssize_t encoding##_encode_reset(                          \
        MultibyteCodec_State *state, const MultibyteCodec *codec,       \
        unsigned char **outbuf, Py_ssize_t outleft)

#define DECODER_INIT(encoding)                                          \
    static int encoding##_decode_init(                                  \
        MultibyteCodec_State *state, const MultibyteCodec *codec)
#define DECODER(encoding)                                               \
    static Py_ssize_t encoding##_decode(                                \
        MultibyteCodec_State *state, const MultibyteCodec *codec,       \
        const unsigned char **inbuf, Py_ssize_t inleft,                 \
        _PyUnicodeWriter *writer)
#define DECODER_RESET(encoding)                                         \
    static Py_ssize_t encoding##_decode_reset(                          \
        MultibyteCodec_State *state, const MultibyteCodec *codec)

#define NEXT_IN(i)                              \
    do {                                        \
        (*inbuf) += (i);                        \
        (inleft) -= (i);                        \
    } while (0)
#define NEXT_INCHAR(i)                          \
    do {                                        \
        (*inpos) += (i);                        \
    } while (0)
#define NEXT_OUT(o)                             \
    do {                                        \
        (*outbuf) += (o);                       \
        (outleft) -= (o);                       \
    } while (0)
#define NEXT(i, o)                              \
    do {                                        \
        NEXT_INCHAR(i);                         \
        NEXT_OUT(o);                            \
    } while (0)

#define REQUIRE_INBUF(n)                        \
    do {                                        \
        if (inleft < (n))                       \
            return MBERR_TOOFEW;                \
    } while (0)

#define REQUIRE_OUTBUF(n)                       \
    do {                                        \
        if (outleft < (n))                      \
            return MBERR_TOOSMALL;              \
    } while (0)

#define INBYTE1 ((*inbuf)[0])
#define INBYTE2 ((*inbuf)[1])
#define INBYTE3 ((*inbuf)[2])
#define INBYTE4 ((*inbuf)[3])

/* (the input: its code points) */
#define INCHAR1 (((const Py_UCS4 *)(data))[*inpos])
#define INCHAR2 (((const Py_UCS4 *)(data))[*inpos + 1])

#define OUTCHAR(c)                                                         \
    do {                                                                   \
        if (neos_cjk_write_char(writer, (c)) < 0)                          \
            return MBERR_EXCEPTION;                                        \
    } while (0)

#define OUTCHAR2(c1, c2)                                                   \
    do {                                                                   \
        Py_UCS4 _c1 = (c1);                                                \
        Py_UCS4 _c2 = (c2);                                                \
        if (neos_cjk_write_char(writer, _c1) < 0 ||                        \
            neos_cjk_write_char(writer, _c2) < 0)                          \
            return MBERR_EXCEPTION;                                        \
    } while (0)

#define OUTBYTEI(c, i)                     \
    do {                                   \
        assert((unsigned char)(c) == (c)); \
        ((*outbuf)[i]) = (c);              \
    } while (0)

#define OUTBYTE1(c) OUTBYTEI(c, 0)
#define OUTBYTE2(c) OUTBYTEI(c, 1)
#define OUTBYTE3(c) OUTBYTEI(c, 2)
#define OUTBYTE4(c) OUTBYTEI(c, 3)

#define WRITEBYTE1(c1)              \
    do {                            \
        REQUIRE_OUTBUF(1);          \
        OUTBYTE1(c1);               \
    } while (0)
#define WRITEBYTE2(c1, c2)          \
    do {                            \
        REQUIRE_OUTBUF(2);          \
        OUTBYTE1(c1);               \
        OUTBYTE2(c2);               \
    } while (0)
#define WRITEBYTE3(c1, c2, c3)      \
    do {                            \
        REQUIRE_OUTBUF(3);          \
        OUTBYTE1(c1);               \
        OUTBYTE2(c2);               \
        OUTBYTE3(c3);               \
    } while (0)
#define WRITEBYTE4(c1, c2, c3, c4)  \
    do {                            \
        REQUIRE_OUTBUF(4);          \
        OUTBYTE1(c1);               \
        OUTBYTE2(c2);               \
        OUTBYTE3(c3);               \
        OUTBYTE4(c4);               \
    } while (0)

#define _TRYMAP_ENC(m, assi, val)                               \
    ((m)->map != NULL && (val) >= (m)->bottom &&                \
        (val)<= (m)->top && ((assi) = (m)->map[(val) -          \
        (m)->bottom]) != NOCHAR)
#define TRYMAP_ENC(charset, assi, uni)                     \
    _TRYMAP_ENC(&charset##_encmap[(uni) >> 8], assi, (uni) & 0xff)
#define TRYMAP_ENC_ST(charset, assi, uni) \
    _TRYMAP_ENC(&(codec->modstate->charset##_encmap)[(uni) >> 8], \
                assi, (uni) & 0xff)

#define _TRYMAP_DEC(m, assi, val)                             \
    ((m)->map != NULL &&                                        \
     (val) >= (m)->bottom &&                                    \
     (val)<= (m)->top &&                                        \
     ((assi) = (m)->map[(val) - (m)->bottom]) != UNIINV)
#define TRYMAP_DEC(charset, assi, c1, c2)                     \
    _TRYMAP_DEC(&charset##_decmap[c1], assi, c2)
#define TRYMAP_DEC_ST(charset, assi, c1, c2) \
    _TRYMAP_DEC(&(codec->modstate->charset##_decmap)[c1], assi, c2)

#define BEGIN_MAPPINGS_LIST(NUM)                                    \
static int                                                          \
add_mappings(cjkcodecs_module_state *st)                            \
{                                                                   \
    int idx = 0;                                                    \
    (void)idx;                                                      \
    st->num_mappings = NUM;                                         \
    st->mapping_list = (struct dbcs_map *)calloc((NUM) > 0 ? (NUM) : 1, sizeof(struct dbcs_map)); \
    if (st->mapping_list == NULL) {                                 \
        return -1;                                                  \
    }

#define MAPPING_ENCONLY(enc) \
    st->mapping_list[idx++] = (struct dbcs_map){#enc, (void*)enc##_encmap, NULL};
#define MAPPING_DECONLY(enc) \
    st->mapping_list[idx++] = (struct dbcs_map){#enc, NULL, (void*)enc##_decmap};
#define MAPPING_ENCDEC(enc) \
    st->mapping_list[idx++] = (struct dbcs_map){#enc, (void*)enc##_encmap, (void*)enc##_decmap};

#define END_MAPPINGS_LIST               \
    assert(st->num_mappings == idx);    \
    return 0;                           \
}

#define BEGIN_CODECS_LIST(NUM)                                  \
static int                                                      \
add_codecs(cjkcodecs_module_state *st)                          \
{                                                               \
    int idx = 0;                                                \
    (void)idx;                                                  \
    st->num_codecs = NUM;                                       \
    st->codec_list = (MultibyteCodec *)calloc(NUM, sizeof(MultibyteCodec)); \
    if (st->codec_list == NULL) {                               \
        return -1;                                              \
    }

#define _STATEFUL_METHODS(enc)          \
    enc##_encode,                       \
    enc##_encode_init,                  \
    enc##_encode_reset,                 \
    enc##_decode,                       \
    enc##_decode_init,                  \
    enc##_decode_reset,
#define _STATELESS_METHODS(enc)         \
    enc##_encode, NULL, NULL,           \
    enc##_decode, NULL, NULL,

#define NEXT_CODEC \
    st->codec_list[idx++]

#define CODEC_STATEFUL(enc) \
    NEXT_CODEC = (MultibyteCodec){#enc, NULL, NULL, _STATEFUL_METHODS(enc)};
#define CODEC_STATELESS(enc) \
    NEXT_CODEC = (MultibyteCodec){#enc, NULL, NULL, _STATELESS_METHODS(enc)};
#define CODEC_STATELESS_WINIT(enc) \
    NEXT_CODEC = (MultibyteCodec){#enc, NULL, enc##_codec_init, _STATELESS_METHODS(enc)};

#define END_CODECS_LIST                         \
    assert(st->num_codecs == idx);              \
    for (int i = 0; i < st->num_codecs; i++) {  \
        st->codec_list[i].modstate = st;        \
    }                                           \
    return 0;                                   \
}

#ifdef USING_BINARY_PAIR_SEARCH
static DBCHAR
find_pairencmap(ucs2_t body, ucs2_t modifier,
                const struct pair_encodemap *haystack, int haystacksize)
{
    int pos, min, max;
    Py_UCS4 value = body << 16 | modifier;

    min = 0;
    max = haystacksize;

    for (pos = haystacksize >> 1; min != max; pos = (min + max) >> 1) {
        if (value < haystack[pos].uniseq) {
            if (max != pos) {
                max = pos;
                continue;
            }
        }
        else if (value > haystack[pos].uniseq) {
            if (min != pos) {
                min = pos;
                continue;
            }
        }
        break;
    }

    if (value == haystack[pos].uniseq) {
        return haystack[pos].code;
    }
    return DBCINV;
}
#endif

#ifdef USING_IMPORTED_MAPS
#define IMPORT_MAP(locale, charset, encmap, decmap) \
    importmap("_codecs_" #locale, #charset, \
              (const void**)encmap, (const void**)decmap)

/* (another module's map (CPython's importmap: its "__map_<charset>" capsule)) */
static int
importmap(const char *modname, const char *charset,
          const void **encmap, const void **decmap)
{
    int num_mappings = 0;
    const struct dbcs_map *mappings = NULL;
    if (neos_cjk_module_mappings(modname, &num_mappings, &mappings) != 0)
        return -1;
    for (int i = 0; i < num_mappings; i++) {
        if (strcmp(mappings[i].charset, charset) == 0) {
            if (encmap != NULL)
                *encmap = mappings[i].encmap;
            if (decmap != NULL)
                *decmap = mappings[i].decmap;
            return 0;
        }
    }
    return -1;
}
#endif

static int add_mappings(cjkcodecs_module_state *);
static int add_codecs(cjkcodecs_module_state *);

/* (the module: its state, made when first needed (CPython's _cjk_exec)) */
#define I_AM_A_MODULE_FOR(loc)                                          \
    static cjkcodecs_module_state neos_cjk_state;                       \
    static int neos_cjk_state_made;                                     \
                                                                        \
    int                                                                 \
    neos_cjk_module_##loc(int *num_codecs, const MultibyteCodec **codecs, \
        int *num_mappings, const struct dbcs_map **mappings)            \
    {                                                                   \
        if (!neos_cjk_state_made) {                                     \
            if (add_mappings(&neos_cjk_state) < 0 ||                    \
                add_codecs(&neos_cjk_state) < 0)                        \
                return -1;                                              \
            neos_cjk_state_made = 1;                                    \
        }                                                               \
        *num_codecs = neos_cjk_state.num_codecs;                        \
        *codecs = neos_cjk_state.codec_list;                            \
        *num_mappings = neos_cjk_state.num_mappings;                    \
        *mappings = neos_cjk_state.mapping_list;                        \
        return 0;                                                       \
    }

#endif
