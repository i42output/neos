/*
  multibytecodec.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/cjkcodecs/multibytecodec.c, written by Hye-Shik Chang, and
  Lib/encodings's CJK codec modules (big5.py etc.)), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved.
  They are used under the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated
  from C (and Python) to C++ for the neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The CJK codecs (CPython 3.13's Modules/cjkcodecs: the codec modules (_codecs_cn.c etc., cjkcodecs/, compiled as they are) and
// multibytecodec.c's objects): the modules _multibytecodec (MultibyteCodec, MultibyteIncrementalEncoder/Decoder and
// MultibyteStreamReader/Writer) and _codecs_cn, _codecs_hk, _codecs_iso2022, _codecs_jp, _codecs_kr and _codecs_tw (getcodec),
// and the CodecInfo of each of their encodings (as Lib/encodings/big5.py etc. make them; codecs.lookup's: see codecs.cpp)

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include "objects.hpp"

extern "C" {
#include "cjkcodecs/multibytecodec.h"
}

namespace neos::python_runtime
{
    namespace detail
    {
        bool is_big(object* aObject);   // (objects.cpp: an int beyond 64 bits)
        object* multibyte_codec_info(std::string const& aModule, std::string const& aName);   // (codecs.cpp's builtin_search)

        namespace
        {
            // --- the codec modules

            struct cjk_module
            {
                char const* name;
                int (*get)(int*, MultibyteCodec const**, int*, dbcs_map const**);
            };
            cjk_module const sCjkModules[] = { { "_codecs_cn", neos_cjk_module_cn }, { "_codecs_hk", neos_cjk_module_hk }, { "_codecs_iso2022", neos_cjk_module_iso2022 },
                { "_codecs_jp", neos_cjk_module_jp }, { "_codecs_kr", neos_cjk_module_kr }, { "_codecs_tw", neos_cjk_module_tw } };

            cjk_module const* cjk_module_named(std::string_view aName)
            {
                for (auto const& m : sCjkModules)
                    if (aName == m.name)
                        return &m;
                return nullptr;
            }

            // (a module's codec of an encoding (CPython's getcodec's search): null if it has none)
            MultibyteCodec const* module_codec(cjk_module const& aModule, std::string_view aEncoding)
            {
                int numCodecs = 0, numMappings = 0;
                MultibyteCodec const* codecs = nullptr;
                dbcs_map const* mappings = nullptr;
                if (aModule.get(&numCodecs, &codecs, &numMappings, &mappings) != 0)
                {
                    error("MemoryError", "");
                    return nullptr;
                }
                for (int i = 0; i < numCodecs; ++i)
                    if (aEncoding == codecs[i].encoding)
                        return &codecs[i];
                return nullptr;
            }
        }
    }
}

extern "C" int neos_cjk_module_mappings(const char* aModule, int* aNumMappings, const struct dbcs_map** aMappings)
{
    using namespace neos::python_runtime::detail;
    auto const module = cjk_module_named(aModule);
    if (module == nullptr)
        return -1;
    int numCodecs = 0;
    MultibyteCodec const* codecs = nullptr;
    return module->get(&numCodecs, &codecs, aNumMappings, aMappings);
}

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            bool is_bytes_object(object* aObject)
            {
                auto const type = python_type(builtin_value(aObject));
                return type != nullptr && std::strcmp(type->base.name, "bytes") == 0;
            }

            object* make_unicode_error(char const* aClass, std::vector<object*> aArgs)
            {
                ref const cls{ exception_class(aClass) };
                if (!cls)
                    return nullptr;
                ref const args{ make_tuple(std::move(aArgs)) };
                return call(cls.get(), args.get(), nullptr);
            }

            // --- error schemes (internal_error_callback: strict, ignore and replace are the codecs'; others are codecs.lookup_error's)

            enum class error_kind { Strict, Ignore, Replace, Custom };
            struct error_scheme
            {
                error_kind kind = error_kind::Strict;
                std::string name;   ///< (a custom scheme's)
            };

            error_scheme scheme_of(char const* aErrors)
            {
                if (aErrors == nullptr || std::strcmp(aErrors, "strict") == 0)
                    return {};
                if (std::strcmp(aErrors, "ignore") == 0)
                    return { error_kind::Ignore, {} };
                if (std::strcmp(aErrors, "replace") == 0)
                    return { error_kind::Replace, {} };
                return { error_kind::Custom, aErrors };
            }

            std::string scheme_name(error_scheme const& aScheme)
            {
                switch (aScheme.kind)
                {
                case error_kind::Strict: return "strict";
                case error_kind::Ignore: return "ignore";
                case error_kind::Replace: return "replace";
                default: return aScheme.name;
                }
            }

            // (an errors argument: str or None (Argument Clinic's str(accept={str, NoneType})))
            bool errors_argument(char const* aFunction, object* aErrors, error_scheme& aResult)
            {
                if (aErrors == nullptr || kind_of(aErrors) == NEOS_PY_NONE)
                {
                    aResult = {};
                    return true;
                }
                if (kind_of(builtin_value(aErrors)) != NEOS_PY_STR)
                {
                    error("TypeError", std::string{ aFunction } + "() argument 'errors' must be str or None, not " + type_name(aErrors));
                    return false;
                }
                auto const& text = as<str_object>(builtin_value(aErrors))->value;
                if (text.find('\0') != std::string::npos)
                {
                    error("ValueError", "embedded null character");
                    return false;
                }
                aResult = scheme_of(text.c_str());
                return true;
            }

            // (call_error_callback: the scheme's handler of the exception)
            object* call_error_callback(error_scheme const& aScheme, object* aException)
            {
                ref const lookup{ module_attribute("_codecs", "lookup_error") };
                if (!lookup)
                    return nullptr;
                ref const handler{ invoke(lookup.get(), { make_str(aScheme.name) }) };
                if (!handler)
                    return nullptr;
                return invoke(handler.get(), { new_reference(aException) });
            }

            // (an error handler's position (PyLong_AsSsize_t: an int beyond 64 bits is out of bounds))
            Py_ssize_t handler_position(object* aPosition)
            {
                if (is_big(aPosition))
                    return -1;
                long long position = 0;
                if (!to_index(aPosition, position))
                {
                    clear_error();
                    return -1;
                }
                return static_cast<Py_ssize_t>(position);
            }

            bool is_int_object(object* aObject)
            {
                auto const k = kind_of(builtin_value(aObject));
                return k == NEOS_PY_INT || k == NEOS_PY_BOOL;
            }

            // --- MultibyteCodec objects

            struct codec_object
            {
                header h;
                MultibyteCodec const* codec;
            };

            neos_py_type make_codec_type();
            neos_py_type sCodecType = make_codec_type();

            bool is_codec_object(object* aObject) { return aObject != nullptr && aObject->type == &sCodecType.base; }
            MultibyteCodec const* codec_of(object* aObject) { return reinterpret_cast<codec_object*>(aObject)->codec; }

            // --- encoding (multibytecodec_encode, multibytecodec_encerror)

            constexpr int MBENC_RESET = MBENC_MAX << 1;    // (reset after an encoding session)

            struct encode_buffer
            {
                object* inobj;                  ///< (the input, a str (for the exception))
                std::u32string const* in;       ///< (its code points)
                Py_ssize_t inpos;
                Py_ssize_t inlen;
                std::vector<unsigned char> out;
                unsigned char* outbuf;
                unsigned char* outbuf_end;
                ref excobj;
            };

            bool expand_encodebuffer(encode_buffer& aBuffer, Py_ssize_t aSize)
            {
                auto const orgpos = static_cast<Py_ssize_t>(aBuffer.outbuf - aBuffer.out.data());
                auto const orgsize = static_cast<Py_ssize_t>(aBuffer.out.size());
                auto const incsize = aSize < (orgsize >> 1) ? (orgsize >> 1) | 1 : aSize;
                try
                {
                    aBuffer.out.resize(static_cast<std::size_t>(orgsize + incsize));
                }
                catch (std::exception const&)
                {
                    error("MemoryError", "");
                    return false;
                }
                aBuffer.outbuf = aBuffer.out.data() + orgpos;
                aBuffer.outbuf_end = aBuffer.out.data() + aBuffer.out.size();
                return true;
            }

            bool require_encodebuffer(encode_buffer& aBuffer, Py_ssize_t aSize)
            {
                if (aSize < 0 || aSize > aBuffer.outbuf_end - aBuffer.outbuf)
                    return expand_encodebuffer(aBuffer, aSize);
                return true;
            }

            object* multibytecodec_encode(MultibyteCodec const* aCodec, MultibyteCodec_State* aState, object* aText, std::u32string const& aPoints,
                Py_ssize_t* aInpos, error_scheme const& aErrors, int aFlags);

            bool multibytecodec_encerror(MultibyteCodec const* aCodec, MultibyteCodec_State* aState, encode_buffer& aBuffer, error_scheme const& aErrors,
                Py_ssize_t e)
            {
                char const* reason = nullptr;
                Py_ssize_t esize = 0;
                if (e > 0)
                {
                    reason = "illegal multibyte sequence";
                    esize = e;
                }
                else
                    switch (e)
                    {
                    case MBERR_TOOSMALL:
                        return require_encodebuffer(aBuffer, -1);   // (retry it)
                    case MBERR_TOOFEW:
                        reason = "incomplete multibyte sequence";
                        esize = aBuffer.inpos;
                        break;
                    case MBERR_INTERNAL:
                        error("RuntimeError", "internal codec error");
                        return false;
                    default:
                        error("RuntimeError", "unknown runtime error");
                        return false;
                    }
                if (aErrors.kind == error_kind::Replace)
                {
                    Py_UCS4 const replacement[] = { '?' };
                    Py_ssize_t r = 0;
                    Py_ssize_t inpos = 0;
                    for (;;)
                    {
                        auto const outleft = static_cast<Py_ssize_t>(aBuffer.outbuf_end - aBuffer.outbuf);
                        r = aCodec->encode(aState, aCodec, 0, replacement, &inpos, 1, &aBuffer.outbuf, outleft, 0);
                        if (r == MBERR_TOOSMALL)
                        {
                            if (!require_encodebuffer(aBuffer, -1))
                                return false;
                            continue;
                        }
                        break;
                    }
                    if (r != 0)
                    {
                        if (!require_encodebuffer(aBuffer, 1))
                            return false;
                        *aBuffer.outbuf++ = '?';
                    }
                }
                if (aErrors.kind == error_kind::Ignore || aErrors.kind == error_kind::Replace)
                {
                    aBuffer.inpos += esize;
                    return true;
                }
                auto const start = aBuffer.inpos;
                auto const end = start + esize;
                // (the exception: made once (the buffer's))
                if (!aBuffer.excobj)
                {
                    aBuffer.excobj = ref{ make_unicode_error("UnicodeEncodeError", { make_str(aCodec->encoding), new_reference(aBuffer.inobj),
                        make_int(static_cast<long long>(start)), make_int(static_cast<long long>(end)), make_str(reason) }) };
                    if (!aBuffer.excobj)
                        return false;
                }
                else
                {
                    ref const s{ make_int(static_cast<long long>(start)) };
                    ref const n{ make_int(static_cast<long long>(end)) };
                    ref const r{ make_str(reason) };
                    if (setattr(aBuffer.excobj.get(), "start", s.get()) != 0 || setattr(aBuffer.excobj.get(), "end", n.get()) != 0 ||
                        setattr(aBuffer.excobj.get(), "reason", r.get()) != 0)
                        return false;
                }
                if (aErrors.kind == error_kind::Strict)
                {
                    raise_exception(aBuffer.excobj.get());
                    return false;
                }
                ref const retobj{ call_error_callback(aErrors, aBuffer.excobj.get()) };
                if (!retobj)
                    return false;
                object* tobj = nullptr;
                if (kind_of(retobj.get()) != NEOS_PY_TUPLE || as<tuple_object>(retobj.get())->items.size() != 2u ||
                    (kind_of(builtin_value(tobj = as<tuple_object>(retobj.get())->items[0])) != NEOS_PY_STR && !is_bytes_object(tobj)) ||
                    !is_int_object(as<tuple_object>(retobj.get())->items[1]))
                {
                    error("TypeError", "encoding error handler must return (str, int) tuple");
                    return false;
                }
                std::string retstr;
                if (kind_of(builtin_value(tobj)) == NEOS_PY_STR)
                {
                    Py_ssize_t inpos = 0;
                    auto const points = to_code_points(as<str_object>(builtin_value(tobj))->value);
                    ref const encoded{ multibytecodec_encode(aCodec, aState, tobj, points, &inpos, error_scheme{}, MBENC_FLUSH) };
                    if (!encoded)
                        return false;
                    std::string_view data;
                    bytes_like(encoded.get(), data);
                    retstr = std::string{ data };
                }
                else
                {
                    std::string_view data;
                    bytes_like(tobj, data);
                    retstr = std::string{ data };
                }
                if (!retstr.empty())
                {
                    if (!require_encodebuffer(aBuffer, static_cast<Py_ssize_t>(retstr.size())))
                        return false;
                    std::memcpy(aBuffer.outbuf, retstr.data(), retstr.size());
                    aBuffer.outbuf += retstr.size();
                }
                auto newpos = handler_position(as<tuple_object>(retobj.get())->items[1]);
                bool const overflow = is_big(as<tuple_object>(retobj.get())->items[1]);
                if (newpos < 0 && !overflow)
                    newpos += aBuffer.inlen;
                if (newpos < 0 || newpos > aBuffer.inlen)
                {
                    error("IndexError", "position " + std::to_string(overflow ? -1 : newpos) + " from error handler out of bounds");
                    return false;
                }
                aBuffer.inpos = newpos;
                return true;
            }

            object* multibytecodec_encode(MultibyteCodec const* aCodec, MultibyteCodec_State* aState, object* aText, std::u32string const& aPoints,
                Py_ssize_t* aInpos, error_scheme const& aErrors, int aFlags)
            {
                auto const datalen = static_cast<Py_ssize_t>(aPoints.size());
                if (datalen == 0 && !(aFlags & MBENC_RESET))
                    return make_bytes("");
                encode_buffer buffer{ aText, &aPoints, 0, datalen, {}, nullptr, nullptr, {} };
                try
                {
                    buffer.out.resize(static_cast<std::size_t>(datalen * 2 + 16));
                }
                catch (std::exception const&)
                {
                    return error("MemoryError", "");
                }
                buffer.outbuf = buffer.out.data();
                buffer.outbuf_end = buffer.outbuf + buffer.out.size();
                while (buffer.inpos < buffer.inlen)
                {
                    // (we don't reuse inleft and outleft here: error callbacks can relocate the cursor anywhere on buffer)
                    auto const outleft = static_cast<Py_ssize_t>(buffer.outbuf_end - buffer.outbuf);
                    auto const r = aCodec->encode(aState, aCodec, 0, aPoints.data(), &buffer.inpos, buffer.inlen, &buffer.outbuf, outleft, aFlags);
                    if (r == 0 || (r == MBERR_TOOFEW && !(aFlags & MBENC_FLUSH)))
                        break;
                    else if (!multibytecodec_encerror(aCodec, aState, buffer, aErrors, r))
                        return nullptr;
                    else if (r == MBERR_TOOFEW)
                        break;
                }
                if (aCodec->encreset != nullptr && (aFlags & MBENC_RESET))
                    for (;;)
                    {
                        auto const outleft = static_cast<Py_ssize_t>(buffer.outbuf_end - buffer.outbuf);
                        auto const r = aCodec->encreset(aState, aCodec, &buffer.outbuf, outleft);
                        if (r == 0)
                            break;
                        else if (!multibytecodec_encerror(aCodec, aState, buffer, aErrors, r))
                            return nullptr;
                    }
                auto const finalsize = static_cast<std::size_t>(buffer.outbuf - buffer.out.data());
                if (aInpos != nullptr)
                    *aInpos = buffer.inpos;
                return make_bytes(std::string{ reinterpret_cast<char const*>(buffer.out.data()), finalsize });
            }

            // --- decoding (multibytecodec_decerror)

            struct decode_buffer
            {
                unsigned char const* inbuf;
                unsigned char const* inbuf_top;
                unsigned char const* inbuf_end;
                ref excobj;
                _PyUnicodeWriter writer{ nullptr, 0, 0 };
                decode_buffer() = default;
                decode_buffer(decode_buffer const&) = delete;
                ~decode_buffer() { std::free(writer.data); }
                std::u32string_view text() const { return { reinterpret_cast<char32_t const*>(writer.data), static_cast<std::size_t>(writer.pos) }; }
            };

            bool write_text(_PyUnicodeWriter& aWriter, std::u32string_view aText)
            {
                for (auto c : aText)
                    if (neos_cjk_write_char(&aWriter, static_cast<Py_UCS4>(c)) < 0)
                    {
                        error("MemoryError", "");
                        return false;
                    }
                return true;
            }

            bool multibytecodec_decerror(MultibyteCodec const* aCodec, decode_buffer& aBuffer, error_scheme const& aErrors, Py_ssize_t e)
            {
                char const* reason = nullptr;
                Py_ssize_t esize = 0;
                if (e > 0)
                {
                    reason = "illegal multibyte sequence";
                    esize = e;
                }
                else
                    switch (e)
                    {
                    case MBERR_TOOSMALL:
                        return true;    // (retry it)
                    case MBERR_TOOFEW:
                        reason = "incomplete multibyte sequence";
                        esize = static_cast<Py_ssize_t>(aBuffer.inbuf_end - aBuffer.inbuf);
                        break;
                    case MBERR_INTERNAL:
                        error("RuntimeError", "internal codec error");
                        return false;
                    case MBERR_EXCEPTION:
                        // (the decoder's output could not grow)
                        if (!failed())
                            error("MemoryError", "");
                        return false;
                    default:
                        error("RuntimeError", "unknown runtime error");
                        return false;
                    }
                if (aErrors.kind == error_kind::Replace && !write_text(aBuffer.writer, U"�"))
                    return false;
                if (aErrors.kind == error_kind::Ignore || aErrors.kind == error_kind::Replace)
                {
                    aBuffer.inbuf += esize;
                    return true;
                }
                auto const start = static_cast<Py_ssize_t>(aBuffer.inbuf - aBuffer.inbuf_top);
                auto const end = start + esize;
                if (!aBuffer.excobj)
                {
                    aBuffer.excobj = ref{ make_unicode_error("UnicodeDecodeError", { make_str(aCodec->encoding),
                        make_bytes(std::string{ reinterpret_cast<char const*>(aBuffer.inbuf_top), static_cast<std::size_t>(aBuffer.inbuf_end - aBuffer.inbuf_top) }),
                        make_int(static_cast<long long>(start)), make_int(static_cast<long long>(end)), make_str(reason) }) };
                    if (!aBuffer.excobj)
                        return false;
                }
                else
                {
                    ref const s{ make_int(static_cast<long long>(start)) };
                    ref const n{ make_int(static_cast<long long>(end)) };
                    ref const r{ make_str(reason) };
                    if (setattr(aBuffer.excobj.get(), "start", s.get()) != 0 || setattr(aBuffer.excobj.get(), "end", n.get()) != 0 ||
                        setattr(aBuffer.excobj.get(), "reason", r.get()) != 0)
                        return false;
                }
                if (aErrors.kind == error_kind::Strict)
                {
                    raise_exception(aBuffer.excobj.get());
                    return false;
                }
                ref const retobj{ call_error_callback(aErrors, aBuffer.excobj.get()) };
                if (!retobj)
                    return false;
                if (kind_of(retobj.get()) != NEOS_PY_TUPLE || as<tuple_object>(retobj.get())->items.size() != 2u ||
                    kind_of(builtin_value(as<tuple_object>(retobj.get())->items[0])) != NEOS_PY_STR || !is_int_object(as<tuple_object>(retobj.get())->items[1]))
                {
                    error("TypeError", "decoding error handler must return (str, int) tuple");
                    return false;
                }
                if (!write_text(aBuffer.writer, to_code_points(as<str_object>(builtin_value(as<tuple_object>(retobj.get())->items[0]))->value)))
                    return false;
                auto newpos = handler_position(as<tuple_object>(retobj.get())->items[1]);
                bool const overflow = is_big(as<tuple_object>(retobj.get())->items[1]);
                if (newpos < 0 && !overflow)
                    newpos += static_cast<Py_ssize_t>(aBuffer.inbuf_end - aBuffer.inbuf_top);
                if (newpos < 0 || aBuffer.inbuf_top + newpos > aBuffer.inbuf_end)
                {
                    error("IndexError", "position " + std::to_string(overflow ? -1 : newpos) + " from error handler out of bounds");
                    return false;
                }
                aBuffer.inbuf = aBuffer.inbuf_top + newpos;
                return true;
            }

            // (MultibyteCodec.encode(input, errors=None) and .decode(input, errors=None))
            object* codec_encode(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const& items = as<tuple_object>(aArgs)->items;
                auto const codec = codec_of(items.at(0u));
                std::vector<object*> a;
                if (!parse_arguments("encode", { items.begin() + 1, items.end() }, aKwargs, { "input", "errors" }, 1u, 0u, a))
                    return nullptr;
                ref input{ new_reference(a[0]) };
                if (kind_of(builtin_value(input.get())) != NEOS_PY_STR)
                {
                    std::string text;
                    if (!str_of(input.get(), text))
                        return nullptr;
                    input = ref{ make_str(std::move(text)) };
                }
                error_scheme errors;
                if (!errors_argument("encode", a[1], errors))
                    return nullptr;
                auto const points = to_code_points(as<str_object>(builtin_value(input.get()))->value);
                MultibyteCodec_State state{};
                if (codec->encinit != nullptr && codec->encinit(&state, codec) != 0)
                    return nullptr;
                ref const r{ multibytecodec_encode(codec, &state, input.get(), points, nullptr, errors, MBENC_FLUSH | MBENC_RESET) };
                if (!r)
                    return nullptr;
                return make_tuple({ new_reference(r.get()), make_int(static_cast<long long>(points.size())) });
            }

            object* codec_decode(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const& items = as<tuple_object>(aArgs)->items;
                auto const codec = codec_of(items.at(0u));
                std::vector<object*> a;
                if (!parse_arguments("decode", { items.begin() + 1, items.end() }, aKwargs, { "input", "errors" }, 1u, 0u, a))
                    return nullptr;
                std::string_view data;
                if (kind_of(builtin_value(a[0])) == NEOS_PY_STR || !bytes_like(a[0], data))
                    return error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(a[0]) + "'");
                std::string const input{ data };
                error_scheme errors;
                if (!errors_argument("decode", a[1], errors))
                    return nullptr;
                if (input.empty())
                    return make_tuple({ make_str(""), make_int(0) });
                decode_buffer buffer;
                buffer.inbuf = buffer.inbuf_top = reinterpret_cast<unsigned char const*>(input.data());
                buffer.inbuf_end = buffer.inbuf_top + input.size();
                MultibyteCodec_State state{};
                if (codec->decinit != nullptr && codec->decinit(&state, codec) != 0)
                    return nullptr;
                while (buffer.inbuf < buffer.inbuf_end)
                {
                    auto const inleft = static_cast<Py_ssize_t>(buffer.inbuf_end - buffer.inbuf);
                    auto const r = codec->decode(&state, codec, &buffer.inbuf, inleft, &buffer.writer);
                    if (r == 0)
                        break;
                    else if (!multibytecodec_decerror(codec, buffer, errors, r))
                        return nullptr;
                }
                return make_tuple({ make_str(from_code_points(buffer.text())), make_int(static_cast<long long>(input.size())) });
            }

            neos_py_type make_codec_type()
            {
                auto type = make_type("_multibytecodec.MultibyteCodec", "MultibyteCodec", retain_object, release_object<codec_object>);
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    if (std::strcmp(aName, "encode") == 0)
                        return make_function("encode", codec_encode, nullptr, aSelf);
                    if (std::strcmp(aName, "decode") == 0)
                        return make_function("decode", codec_decode, nullptr, aSelf);
                    return error("AttributeError", std::string{ "'_multibytecodec.MultibyteCodec' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            // (__create_codec: the codec object of a codec (its codecinit called))
            object* create_codec(MultibyteCodec const* aCodec)
            {
                if (aCodec->codecinit != nullptr && aCodec->codecinit(aCodec) != 0)
                    return failed() ? nullptr : error("MemoryError", "");
                return &(new codec_object{ { { &sCodecType.base } }, aCodec })->h.base;
            }

            // --- the stateful codecs' contexts (MultibyteStatefulEncoderContext and MultibyteStatefulDecoderContext: an instance's)

            struct context_object
            {
                header h;
                MultibyteCodec const* codec = nullptr;
                MultibyteCodec_State state{};
                error_scheme errors;
                std::optional<std::u32string> pending;      ///< (an encoder's)
                unsigned char decoderPending[MAXDECPENDING] = {};
                Py_ssize_t pendingsize = 0;                 ///< (a decoder's)
                object* stream = nullptr;
                ~context_object() { release(stream); }
            };

            neos_py_type make_context_type()
            {
                return make_type("_multibytecodec.context", "context", retain_object, release_object<context_object>);
            }
            neos_py_type sContextType = make_context_type();

            char const* const sContextAttribute = "_multibytecodec__context";

            // (an instance's context: null if it has none (TypeError))
            context_object* context_of(object* aSelf)
            {
                ref const c{ getattr(aSelf, sContextAttribute) };
                if (!c || c.get()->type != &sContextType.base)
                {
                    clear_error();
                    error("TypeError", std::string{ "descriptor requires a '_multibytecodec' object but received '" } + type_name(aSelf) + "'");
                    return nullptr;
                }
                return reinterpret_cast<context_object*>(c.get());     // (the instance holds it)
            }

            // (encoder_encode_stateful)
            object* encoder_encode_stateful(context_object& aContext, object* aText, bool aFinal)
            {
                ref unistr{ new_reference(aText) };
                if (kind_of(builtin_value(unistr.get())) != NEOS_PY_STR)
                {
                    std::string text;
                    if (!str_of(unistr.get(), text))
                        return nullptr;
                    unistr = ref{ make_str(std::move(text)) };
                }
                auto const origpending = aContext.pending;
                std::u32string inbuf = aContext.pending ? *aContext.pending : std::u32string{};
                inbuf += to_code_points(as<str_object>(builtin_value(unistr.get()))->value);
                aContext.pending.reset();
                ref const inobj{ make_str(from_code_points(inbuf)) };
                Py_ssize_t inpos = 0;
                auto const datalen = static_cast<Py_ssize_t>(inbuf.size());
                ref r{ multibytecodec_encode(aContext.codec, &aContext.state, inobj.get(), inbuf, &inpos, aContext.errors, aFinal ? MBENC_FLUSH | MBENC_RESET : 0) };
                if (!r)
                {
                    // (the original pending buffer recovered)
                    aContext.pending = origpending;
                    return nullptr;
                }
                if (inpos < datalen)
                {
                    if (datalen - inpos > MAXENCPENDING)
                    {
                        // (normal codecs can't reach here)
                        ref const e{ make_unicode_error("UnicodeEncodeError", { make_str(aContext.codec->encoding), new_reference(inobj.get()),
                            make_int(static_cast<long long>(inpos)), make_int(static_cast<long long>(datalen)), make_str("pending buffer overflow") }) };
                        if (e)
                            raise_exception(e.get());
                        return nullptr;
                    }
                    aContext.pending = inbuf.substr(static_cast<std::size_t>(inpos));
                }
                return r.release_ownership();
            }

            // (decoder_append_pending)
            bool decoder_append_pending(context_object& aContext, decode_buffer& aBuffer)
            {
                auto const npendings = static_cast<Py_ssize_t>(aBuffer.inbuf_end - aBuffer.inbuf);
                if (npendings + aContext.pendingsize > MAXDECPENDING)
                {
                    auto const bufsize = static_cast<std::size_t>(aBuffer.inbuf_end - aBuffer.inbuf_top);
                    ref const e{ make_unicode_error("UnicodeDecodeError", { make_str(aContext.codec->encoding),
                        make_bytes(std::string{ reinterpret_cast<char const*>(aBuffer.inbuf_top), bufsize }), make_int(0), make_int(static_cast<long long>(bufsize)),
                        make_str("pending buffer overflow") }) };
                    if (e)
                        raise_exception(e.get());
                    return false;
                }
                std::memcpy(aContext.decoderPending + aContext.pendingsize, aBuffer.inbuf, static_cast<std::size_t>(npendings));
                aContext.pendingsize += npendings;
                return true;
            }

            void decoder_prepare_buffer(decode_buffer& aBuffer, std::string const& aData)
            {
                aBuffer.inbuf = aBuffer.inbuf_top = reinterpret_cast<unsigned char const*>(aData.data());
                aBuffer.inbuf_end = aBuffer.inbuf_top + aData.size();
            }

            // (decoder_feed_buffer)
            bool decoder_feed_buffer(context_object& aContext, decode_buffer& aBuffer)
            {
                while (aBuffer.inbuf < aBuffer.inbuf_end)
                {
                    auto const inleft = static_cast<Py_ssize_t>(aBuffer.inbuf_end - aBuffer.inbuf);
                    auto const r = aContext.codec->decode(&aContext.state, aContext.codec, &aBuffer.inbuf, inleft, &aBuffer.writer);
                    if (r == 0 || r == MBERR_TOOFEW)
                        break;
                    else if (!multibytecodec_decerror(aContext.codec, aBuffer, aContext.errors, r))
                        return false;
                }
                return true;
            }

            // (an int's bytes, little-endian, unsigned: of aSize bytes (_PyLong_AsByteArray's OverflowErrors); and an int of bytes)
            bool int_bytes(object* aValue, std::size_t aSize, std::string& aResult)
            {
                ref const method{ getattr(aValue, "to_bytes") };
                ref const result{ method ? invoke(method.get(), { make_int(static_cast<long long>(aSize)), make_str("little") }) : nullptr };
                if (!result)
                    return false;
                std::string_view data;
                bytes_like(result.get(), data);
                aResult = std::string{ data };
                return true;
            }
            object* int_of_bytes(std::string const& aBytes)
            {
                ref const intType{ fn_builtin("int") };
                ref const fromBytes{ intType ? getattr(intType.get(), "from_bytes") : nullptr };
                return fromBytes ? invoke(fromBytes.get(), { make_bytes(aBytes), make_str("little") }) : nullptr;
            }

            // --- the classes (of the instance's context (its attribute): MultibyteIncrementalEncoder etc. are classes, of which the
            // encodings' classes are subclasses (with codecs' classes))

            enum class context_kind { Encoder, Decoder, Reader, Writer };

            // (__new__(cls, ...): the instance and its context (mbiencoder_new etc.: the class's codec, its errors (and stream)))
            object* context_new(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const kind = static_cast<context_kind>(reinterpret_cast<std::uintptr_t>(aData));
                auto const& items = as<tuple_object>(aArgs)->items;
                if (items.empty())
                    return error("TypeError", "__new__() missing required argument 'cls'");
                auto const cls = items[0];
                bool const stream = kind == context_kind::Reader || kind == context_kind::Writer;
                char const* const name = kind == context_kind::Encoder ? "IncrementalEncoder" : kind == context_kind::Decoder ? "IncrementalDecoder" :
                    kind == context_kind::Reader ? "StreamReader" : "StreamWriter";
                std::vector<object*> a;
                if (stream ? !parse_arguments(name, { items.begin() + 1, items.end() }, aKwargs, { "stream", "errors" }, 1u, 0u, a) :
                    !parse_arguments(name, { items.begin() + 1, items.end() }, aKwargs, { "errors" }, 0u, 0u, a))
                    return nullptr;
                auto const errorsArgument = stream ? a[1] : a[0];
                if (errorsArgument != nullptr && kind_of(builtin_value(errorsArgument)) != NEOS_PY_STR)
                    return error("TypeError", std::string{ name } + "() argument 'errors' must be str, not " + type_name(errorsArgument));
                ref const objectNew{ class_function("object", "__new__") };
                ref self{ objectNew ? invoke(objectNew.get(), { new_reference(cls) }) : nullptr };
                if (!self)
                    return nullptr;
                ref const codec{ getattr(cls, "codec") };
                if (!codec)
                    return nullptr;
                if (!is_codec_object(codec.get()))
                    return error("TypeError", "codec is unexpected type");
                ref const context{ &(new context_object{ { { &sContextType.base } } })->h.base };
                auto& c = *reinterpret_cast<context_object*>(context.get());
                c.codec = codec_of(codec.get());
                c.errors = scheme_of(errorsArgument != nullptr ? as<str_object>(builtin_value(errorsArgument))->value.c_str() : nullptr);
                if (stream)
                    c.stream = new_reference(a[0]);
                bool const encoder = kind == context_kind::Encoder || kind == context_kind::Writer;
                if (encoder ? c.codec->encinit != nullptr && c.codec->encinit(&c.state, c.codec) != 0 :
                    c.codec->decinit != nullptr && c.codec->decinit(&c.state, c.codec) != 0)
                    return failed() ? nullptr : error("RuntimeError", "internal codec error");
                if (setattr(self.get(), sContextAttribute, context.get()) != 0)
                    return nullptr;
                return self.release_ownership();
            }

            // (__init__: nothing (mbiencoder_init etc.))
            object* context_init(neos_py_context*, void*, object*, object*)
            {
                return new_reference(none());
            }

            // (the errors property (codecctx_getsets))
            object* errors_get(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const context = context_of(as<tuple_object>(aArgs)->items.at(0u));
                return context != nullptr ? make_str(scheme_name(context->errors)) : nullptr;
            }
            object* errors_set(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const& items = as<tuple_object>(aArgs)->items;
                auto const context = context_of(items.at(0u));
                if (context == nullptr)
                    return nullptr;
                if (items.size() < 2u || kind_of(builtin_value(items[1])) != NEOS_PY_STR)
                    return error("TypeError", "errors must be a string");
                context->errors = scheme_of(as<str_object>(builtin_value(items[1]))->value.c_str());
                return new_reference(none());
            }
            object* errors_delete(neos_py_context*, void*, object*, object*)
            {
                return error("AttributeError", "cannot delete attribute");
            }
            object* stream_get(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const context = context_of(as<tuple_object>(aArgs)->items.at(0u));
                return context != nullptr ? new_reference(context->stream) : nullptr;
            }

            object* rest_arguments(object* aArgs, std::vector<object*>& aRest)
            {
                auto const& items = as<tuple_object>(aArgs)->items;
                aRest.assign(items.begin() + 1, items.end());
                return items.at(0u);
            }

            // (MultibyteIncrementalEncoder.encode(input, final=False))
            object* encoder_encode(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> rest;
                auto const context = context_of(rest_arguments(aArgs, rest));
                std::vector<object*> a;
                if (context == nullptr || !parse_arguments("encode", rest, aKwargs, { "input", "final" }, 1u, 0u, a))
                    return nullptr;
                int final = 0;
                if (a[1] != nullptr && (final = truth(a[1])) < 0)
                    return nullptr;
                return encoder_encode_stateful(*context, a[0], final != 0);
            }

            // (MultibyteIncrementalEncoder.getstate(): an int of its pending characters (UTF-8) and its state)
            object* encoder_getstate(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> rest;
                auto const context = context_of(rest_arguments(aArgs, rest));
                std::vector<object*> a;
                if (context == nullptr || !parse_arguments("getstate", rest, aKwargs, {}, 0u, 0u, a))
                    return nullptr;
                std::string statebytes;
                if (context->pending)
                {
                    auto const pendingbuffer = from_code_points(*context->pending);
                    if (pendingbuffer.size() > MAXENCPENDING * 4)
                    {
                        ref const e{ make_unicode_error("UnicodeEncodeError", { make_str(context->codec->encoding), make_str(pendingbuffer), make_int(0),
                            make_int(static_cast<long long>(context->pending->size())), make_str("pending buffer too large") }) };
                        if (e)
                            raise_exception(e.get());
                        return nullptr;
                    }
                    statebytes += static_cast<char>(pendingbuffer.size());
                    statebytes += pendingbuffer;
                }
                else
                    statebytes += '\0';
                statebytes.append(reinterpret_cast<char const*>(context->state.c), sizeof(context->state.c));
                return int_of_bytes(statebytes);
            }

            // (MultibyteIncrementalEncoder.setstate(state, /))
            object* encoder_setstate(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> rest;
                auto const context = context_of(rest_arguments(aArgs, rest));
                std::vector<object*> a;
                if (context == nullptr || !parse_arguments("setstate", rest, aKwargs, { "state" }, 1u, 1u, a))
                    return nullptr;
                if (kind_of(builtin_value(a[0])) != NEOS_PY_INT)
                    return error("TypeError", std::string{ "setstate() argument must be int, not " } + type_name(a[0]));
                std::string statebytes;
                if (!int_bytes(a[0], 1u + MAXENCPENDING * 4 + sizeof(context->state.c), statebytes))
                    return nullptr;
                auto const size = static_cast<unsigned char>(statebytes[0]);
                if (size > MAXENCPENDING * 4)
                    return error("UnicodeError", "pending buffer too large");
                ref const pending{ make_bytes(statebytes.substr(1u, size)) };
                ref const decode{ getattr(pending.get(), "decode") };
                ref const text{ decode ? invoke(decode.get(), { make_str("utf-8"), make_str("strict") }) : nullptr };
                if (!text)
                    return nullptr;
                context->pending = to_code_points(as<str_object>(text.get())->value);
                std::memcpy(context->state.c, statebytes.data() + 1 + size, sizeof(context->state.c));
                return new_reference(none());
            }

            // (MultibyteIncrementalEncoder.reset())
            object* encoder_reset(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> rest;
                auto const context = context_of(rest_arguments(aArgs, rest));
                std::vector<object*> a;
                if (context == nullptr || !parse_arguments("reset", rest, aKwargs, {}, 0u, 0u, a))
                    return nullptr;
                // (longest output: 4 bytes (b'\x0F\x1F(B') with ISO 2022)
                if (context->codec->encreset != nullptr)
                {
                    unsigned char buffer[4];
                    unsigned char* outbuf = buffer;
                    if (context->codec->encreset(&context->state, context->codec, &outbuf, sizeof(buffer)) != 0)
                        return failed() ? nullptr : error("RuntimeError", "internal codec error");
                }
                context->pending.reset();
                return new_reference(none());
            }

            // (MultibyteIncrementalDecoder.decode(input, final=False))
            object* decoder_decode(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> rest;
                auto const context = context_of(rest_arguments(aArgs, rest));
                std::vector<object*> a;
                if (context == nullptr || !parse_arguments("decode", rest, aKwargs, { "input", "final" }, 1u, 0u, a))
                    return nullptr;
                std::string_view data;
                if (kind_of(builtin_value(a[0])) == NEOS_PY_STR || !bytes_like(a[0], data))
                    return error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(a[0]) + "'");
                int final = 0;
                if (a[1] != nullptr && (final = truth(a[1])) < 0)
                    return nullptr;
                auto const origpending = context->pendingsize;
                std::string const wdata = std::string{ reinterpret_cast<char const*>(context->decoderPending), static_cast<std::size_t>(context->pendingsize) } +
                    std::string{ data };
                context->pendingsize = 0;
                decode_buffer buffer;
                decoder_prepare_buffer(buffer, wdata);
                if (!decoder_feed_buffer(*context, buffer))
                    return nullptr;
                if (final && buffer.inbuf < buffer.inbuf_end)
                    if (!multibytecodec_decerror(context->codec, buffer, context->errors, MBERR_TOOFEW))
                    {
                        // (the original pending buffer recovered)
                        std::memcpy(context->decoderPending, wdata.data(), static_cast<std::size_t>(origpending));
                        context->pendingsize = origpending;
                        return nullptr;
                    }
                // (a pending sequence still exists)
                if (buffer.inbuf < buffer.inbuf_end && !decoder_append_pending(*context, buffer))
                    return nullptr;
                return make_str(from_code_points(buffer.text()));
            }

            // (MultibyteIncrementalDecoder.getstate(): (pending bytes, an int of its state))
            object* decoder_getstate(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> rest;
                auto const context = context_of(rest_arguments(aArgs, rest));
                std::vector<object*> a;
                if (context == nullptr || !parse_arguments("getstate", rest, aKwargs, {}, 0u, 0u, a))
                    return nullptr;
                ref statelong{ int_of_bytes(std::string{ reinterpret_cast<char const*>(context->state.c), sizeof(context->state.c) }) };
                if (!statelong)
                    return nullptr;
                return make_tuple({ make_bytes(std::string{ reinterpret_cast<char const*>(context->decoderPending), static_cast<std::size_t>(context->pendingsize) }),
                    statelong.release_ownership() });
            }

            // (MultibyteIncrementalDecoder.setstate(state, /): a tuple (bytes, int))
            object* decoder_setstate(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> rest;
                auto const context = context_of(rest_arguments(aArgs, rest));
                std::vector<object*> a;
                if (context == nullptr || !parse_arguments("setstate", rest, aKwargs, { "state" }, 1u, 1u, a))
                    return nullptr;
                if (kind_of(builtin_value(a[0])) != NEOS_PY_TUPLE)
                    return error("TypeError", std::string{ "setstate() argument must be tuple, not " } + type_name(a[0]));
                auto const& state = as<tuple_object>(builtin_value(a[0]))->items;
                if (state.size() != 2u || !is_bytes_object(state[0]) || kind_of(builtin_value(state[1])) != NEOS_PY_INT)
                    return error("TypeError", "setstate(): illegal state argument");
                std::string statebytes;
                if (!int_bytes(state[1], sizeof(context->state.c), statebytes))
                    return nullptr;
                std::string_view buffer;
                bytes_like(state[0], buffer);
                if (buffer.size() > MAXDECPENDING)
                {
                    ref const e{ make_unicode_error("UnicodeDecodeError", { make_str(context->codec->encoding), make_bytes(std::string{ buffer }), make_int(0),
                        make_int(static_cast<long long>(buffer.size())), make_str("pending buffer too large") }) };
                    if (e)
                        raise_exception(e.get());
                    return nullptr;
                }
                context->pendingsize = static_cast<Py_ssize_t>(buffer.size());
                std::memcpy(context->decoderPending, buffer.data(), buffer.size());
                std::memcpy(context->state.c, statebytes.data(), sizeof(context->state.c));
                return new_reference(none());
            }

            // (MultibyteIncrementalDecoder.reset() and MultibyteStreamReader.reset())
            object* decoder_reset(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> rest;
                auto const context = context_of(rest_arguments(aArgs, rest));
                std::vector<object*> a;
                if (context == nullptr || !parse_arguments("reset", rest, aKwargs, {}, 0u, 0u, a))
                    return nullptr;
                if (context->codec->decreset != nullptr && context->codec->decreset(&context->state, context->codec) != 0)
                    return failed() ? nullptr : error("RuntimeError", "internal codec error");
                context->pendingsize = 0;
                return new_reference(none());
            }

            // (mbstreamreader_iread)
            object* mbstreamreader_iread(context_object& aContext, char const* aMethod, Py_ssize_t aSizehint)
            {
                if (aSizehint == 0)
                    return make_str("");
                decode_buffer buffer;
                for (;;)
                {
                    ref cres{ aSizehint < 0 ? call_method_named(aContext.stream, aMethod, {}) :
                        call_method_named(aContext.stream, aMethod, { make_int(static_cast<long long>(aSizehint)) }) };
                    if (!cres)
                        return nullptr;
                    if (!is_bytes_object(cres.get()))
                        return error("TypeError", std::string{ "stream function returned a non-bytes object (" } + type_name(cres.get()) + ")");
                    std::string_view data;
                    bytes_like(cres.get(), data);
                    bool const endoffile = data.empty();
                    std::string const rdata = std::string{ reinterpret_cast<char const*>(aContext.decoderPending), static_cast<std::size_t>(aContext.pendingsize) } +
                        std::string{ data };
                    aContext.pendingsize = 0;
                    auto const rsize = rdata.size();
                    decoder_prepare_buffer(buffer, rdata);
                    if (rsize > 0u && !decoder_feed_buffer(aContext, buffer))
                        return nullptr;
                    if (endoffile || aSizehint < 0)
                        if (buffer.inbuf < buffer.inbuf_end && !multibytecodec_decerror(aContext.codec, buffer, aContext.errors, MBERR_TOOFEW))
                            return nullptr;
                    // (a pending sequence exists)
                    if (buffer.inbuf < buffer.inbuf_end && !decoder_append_pending(aContext, buffer))
                        return nullptr;
                    if (aSizehint < 0 || buffer.writer.pos != 0 || rsize == 0u)
                        break;
                    aSizehint = 1;  // (read 1 more byte and retry)
                }
                return make_str(from_code_points(buffer.text()));
            }

            bool size_argument(object* aSize, Py_ssize_t& aResult)
            {
                if (aSize == nullptr || kind_of(aSize) == NEOS_PY_NONE)
                {
                    aResult = -1;
                    return true;
                }
                if (!is_int_object(aSize))
                {
                    error("TypeError", "arg 1 must be an integer");
                    return false;
                }
                long long size = 0;
                if (!to_index(aSize, size))
                    return false;
                aResult = static_cast<Py_ssize_t>(size);
                return true;
            }

            // (MultibyteStreamReader.read(sizeobj=None, /), readline(sizeobj=None, /) and readlines(sizehintobj=None, /))
            template <int Which>
            object* reader_read(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                static char const* const sNames[] = { "read", "readline", "readlines" };
                std::vector<object*> rest;
                auto const context = context_of(rest_arguments(aArgs, rest));
                std::vector<object*> a;
                if (context == nullptr || !parse_arguments(sNames[Which], rest, aKwargs, { Which == 2 ? "sizehintobj" : "sizeobj" }, 0u, 1u, a))
                    return nullptr;
                Py_ssize_t size = -1;
                if (!size_argument(a[0], size))
                    return nullptr;
                ref r{ mbstreamreader_iread(*context, Which == 1 ? "readline" : "read", size) };
                if (!r || Which != 2)
                    return r.release_ownership();
                return call_method_named(r.get(), "splitlines", { make_bool(true) });
            }

            // (mbstreamwriter_iwrite)
            bool mbstreamwriter_iwrite(context_object& aContext, object* aText)
            {
                ref const str{ encoder_encode_stateful(aContext, aText, false) };
                if (!str)
                    return false;
                ref const wr{ call_method_named(aContext.stream, "write", { new_reference(str.get()) }) };
                return static_cast<bool>(wr);
            }

            // (MultibyteStreamWriter.write(strobj, /), writelines(lines, /) and reset())
            object* writer_write(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> rest;
                auto const context = context_of(rest_arguments(aArgs, rest));
                std::vector<object*> a;
                if (context == nullptr || !parse_arguments("write", rest, aKwargs, { "strobj" }, 1u, 1u, a))
                    return nullptr;
                return mbstreamwriter_iwrite(*context, a[0]) ? new_reference(none()) : nullptr;
            }

            object* writer_writelines(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> rest;
                auto const context = context_of(rest_arguments(aArgs, rest));
                std::vector<object*> a;
                if (context == nullptr || !parse_arguments("writelines", rest, aKwargs, { "lines" }, 1u, 1u, a))
                    return nullptr;
                // (a sequence: its items by index, its length found each time (it can change))
                ref const itemGetter{ kind_of(builtin_value(a[0])) == NEOS_PY_DICT ? nullptr : optional_attribute(a[0], "__getitem__") };
                if (!itemGetter)
                {
                    clear_error();
                    return error("TypeError", "arg must be a sequence object");
                }
                for (long long i = 0;; ++i)
                {
                    auto const n = length(a[0]);
                    if (n < 0)
                        return nullptr;
                    if (i >= n)
                        break;
                    ref const index{ make_int(i) };
                    ref const strobj{ getitem(a[0], index.get()) };
                    if (!strobj || !mbstreamwriter_iwrite(*context, strobj.get()))
                        return nullptr;
                }
                return new_reference(none());
            }

            object* writer_reset(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> rest;
                auto const context = context_of(rest_arguments(aArgs, rest));
                std::vector<object*> a;
                if (context == nullptr || !parse_arguments("reset", rest, aKwargs, {}, 0u, 0u, a))
                    return nullptr;
                if (!context->pending)
                    return new_reference(none());
                auto const pending = *context->pending;
                ref const pendingText{ make_str(from_code_points(pending)) };
                ref const pwrt{ multibytecodec_encode(context->codec, &context->state, pendingText.get(), pending, nullptr, context->errors, MBENC_FLUSH | MBENC_RESET) };
                // (some pending buffer can be truncated when UnicodeEncodeError is raised on 'strict' mode. but, 'reset' method is designed to
                // reset the pending buffer or states so failed string sequence ought to be missed)
                context->pending.reset();
                if (!pwrt)
                    return nullptr;
                std::string_view data;
                bytes_like(pwrt.get(), data);
                if (!data.empty())
                {
                    ref const wr{ call_method_named(context->stream, "write", { new_reference(pwrt.get()) }) };
                    if (!wr)
                        return nullptr;
                }
                return new_reference(none());
            }

            // (the module _multibytecodec's classes, made once)
            struct context_classes
            {
                object* encoder = nullptr;
                object* decoder = nullptr;
                object* reader = nullptr;
                object* writer = nullptr;
            };

            object* make_property(native_function aGet, native_function aSet, native_function aDelete, char const* aName)
            {
                ref const property{ fn_builtin("property") };
                std::vector<object*> args{ make_function(aName, aGet, nullptr) };
                args.push_back(aSet != nullptr ? make_function(aName, aSet, nullptr) : new_reference(none()));
                args.push_back(aDelete != nullptr ? make_function(aName, aDelete, nullptr) : new_reference(none()));
                return property ? invoke(property.get(), std::move(args)) : nullptr;
            }

            context_classes const* classes()
            {
                static context_classes sClasses;
                if (sClasses.encoder != nullptr)
                    return &sClasses;
                ref const staticmethod{ fn_builtin("staticmethod") };
                ref const objectClass{ fn_builtin("object") };
                if (!staticmethod || !objectClass)
                    return nullptr;
                auto const make = [&](char const* aName, context_kind aKind, std::vector<std::pair<char const*, native_function>> const& aMethods) -> object*
                {
                    std::vector<std::pair<std::string, object*>> ns;
                    ns.emplace_back("__new__", invoke(staticmethod.get(), { make_function("__new__", context_new, reinterpret_cast<void*>(static_cast<std::uintptr_t>(aKind))) }));
                    ns.emplace_back("__init__", make_function("__init__", context_init, nullptr));
                    for (auto const& m : aMethods)
                        ns.emplace_back(m.first, make_function(m.first, m.second, nullptr));
                    ns.emplace_back("errors", make_property(errors_get, errors_set, errors_delete, "errors"));
                    if (aKind == context_kind::Reader || aKind == context_kind::Writer)
                        ns.emplace_back("stream", make_property(stream_get, nullptr, nullptr, "stream"));
                    bool ok = true;
                    for (auto const& n : ns)
                        ok = ok && n.second != nullptr;
                    auto const result = ok ? make_class("_multibytecodec", aName, { objectClass.get() }, ns) : nullptr;
                    for (auto& n : ns)
                        if (n.second != nullptr)
                            release(n.second);
                    return result;
                };
                auto const encoder = make("MultibyteIncrementalEncoder", context_kind::Encoder, { { "encode", encoder_encode }, { "getstate", encoder_getstate },
                    { "setstate", encoder_setstate }, { "reset", encoder_reset } });
                auto const decoder = make("MultibyteIncrementalDecoder", context_kind::Decoder, { { "decode", decoder_decode }, { "getstate", decoder_getstate },
                    { "setstate", decoder_setstate }, { "reset", decoder_reset } });
                auto const reader = make("MultibyteStreamReader", context_kind::Reader, { { "read", reader_read<0> }, { "readline", reader_read<1> },
                    { "readlines", reader_read<2> }, { "reset", decoder_reset } });
                auto const writer = make("MultibyteStreamWriter", context_kind::Writer, { { "write", writer_write }, { "writelines", writer_writelines },
                    { "reset", writer_reset } });
                if (encoder == nullptr || decoder == nullptr || reader == nullptr || writer == nullptr)
                {
                    for (auto c : { encoder, decoder, reader, writer })
                        release(c);
                    return nullptr;
                }
                sClasses = { encoder, decoder, reader, writer };
                return &sClasses;
            }

            bool multibytecodec_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                auto const c = classes();
                if (c == nullptr)
                    return false;
                aAttributes.emplace_back("MultibyteIncrementalEncoder", new_reference(c->encoder));
                aAttributes.emplace_back("MultibyteIncrementalDecoder", new_reference(c->decoder));
                aAttributes.emplace_back("MultibyteStreamReader", new_reference(c->reader));
                aAttributes.emplace_back("MultibyteStreamWriter", new_reference(c->writer));
                // (__create_codec(arg, /): its argument is a codec module's capsule (there are none: getcodec makes the codec objects))
                static builtin_entry const sCreateCodec{ "__create_codec", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("__create_codec", aArgs, aKwargs, { "arg" }, 1u, 1u, a))
                            return nullptr;
                        return error("ValueError", "argument type invalid");
                    } };
                aAttributes.emplace_back("__create_codec", make_builtin(sCreateCodec));
                return true;
            }

            inline bool const sMultibytecodec = register_module("_multibytecodec", multibytecodec_module);

            // --- the codec modules' getcodec(encoding)

            template <std::size_t Module>
            bool cjk_codec_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                static builtin_entry const sGetcodec{ "getcodec", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_keywords(aKwargs, "getcodec"))
                            return nullptr;
                        if (aArgs.size() != 1u)
                            return error("TypeError", "getcodec() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                        if (kind_of(builtin_value(aArgs[0])) != NEOS_PY_STR)
                            return error("TypeError", "encoding name must be a string.");
                        auto const codec = module_codec(sCjkModules[Module], as<str_object>(builtin_value(aArgs[0]))->value);
                        if (codec == nullptr)
                            return failed() ? nullptr : error("LookupError", "no such codec is supported.");
                        return create_codec(codec);
                    } };
                aAttributes.emplace_back("getcodec", make_builtin(sGetcodec));
                return true;
            }

            inline bool const sCodecsCn = register_module("_codecs_cn", cjk_codec_module<0>);
            inline bool const sCodecsHk = register_module("_codecs_hk", cjk_codec_module<1>);
            inline bool const sCodecsIso2022 = register_module("_codecs_iso2022", cjk_codec_module<2>);
            inline bool const sCodecsJp = register_module("_codecs_jp", cjk_codec_module<3>);
            inline bool const sCodecsKr = register_module("_codecs_kr", cjk_codec_module<4>);
            inline bool const sCodecsTw = register_module("_codecs_tw", cjk_codec_module<5>);

            // (the encodings' modules: their codec modules (Lib/encodings/big5.py etc.: _codecs_tw.getcodec('big5')))
            struct cjk_encoding
            {
                char const* name;
                std::size_t module;
            };
            cjk_encoding const sEncodings[] = { { "gb2312", 0u }, { "gbk", 0u }, { "gb18030", 0u }, { "hz", 0u }, { "big5hkscs", 1u },
                { "iso2022_kr", 2u }, { "iso2022_jp", 2u }, { "iso2022_jp_1", 2u }, { "iso2022_jp_2", 2u }, { "iso2022_jp_2004", 2u }, { "iso2022_jp_3", 2u },
                { "iso2022_jp_ext", 2u }, { "shift_jis", 3u }, { "cp932", 3u }, { "euc_jp", 3u }, { "shift_jis_2004", 3u }, { "euc_jis_2004", 3u },
                { "euc_jisx0213", 3u }, { "shift_jisx0213", 3u }, { "euc_kr", 4u }, { "cp949", 4u }, { "johab", 4u }, { "big5", 5u }, { "cp950", 5u } };
        }

        // (a CJK encoding's CodecInfo (its module's (encodings.<module>) getregentry()'s): None if aModule is not one of theirs)
        object* multibyte_codec_info(std::string const& aModule, std::string const& aName)
        {
            auto const encoding = std::find_if(std::begin(sEncodings), std::end(sEncodings), [&](cjk_encoding const& e) { return aModule == e.name; });
            if (encoding == std::end(sEncodings))
                return new_reference(none());
            auto const c = classes();
            if (c == nullptr)
                return nullptr;
            auto const codecModule = module_codec(sCjkModules[encoding->module], encoding->name);
            if (codecModule == nullptr)
                return failed() ? nullptr : error("LookupError", "no such codec is supported.");
            ref const codec{ create_codec(codecModule) };
            if (!codec)
                return nullptr;
            ref const codecs{ fn_import("codecs") };
            if (!codecs)
                return nullptr;
            auto const base = [&](char const* aName) { return ref{ getattr(codecs.get(), aName) }; };
            ref const codecBase{ base("Codec") }, incrementalEncoderBase{ base("IncrementalEncoder") }, incrementalDecoderBase{ base("IncrementalDecoder") },
                streamReaderBase{ base("StreamReader") }, streamWriterBase{ base("StreamWriter") }, codecInfo{ base("CodecInfo") };
            if (!codecBase || !incrementalEncoderBase || !incrementalDecoderBase || !streamReaderBase || !streamWriterBase || !codecInfo)
                return nullptr;
            std::string const module = std::string{ "encodings." } + aModule;
            auto const make = [&](char const* aName, std::vector<object*> const& aBases, std::vector<std::pair<std::string, object*>> aNamespace)
            {
                ref result{ make_class(module, aName, aBases, aNamespace) };
                for (auto& n : aNamespace)
                    release(n.second);
                return result;
            };
            // (class Codec(codecs.Codec): encode = codec.encode; decode = codec.decode)
            ref const encode{ getattr(codec.get(), "encode") };
            ref const decode{ getattr(codec.get(), "decode") };
            if (!encode || !decode)
                return nullptr;
            ref const codecClass{ make("Codec", { codecBase.get() }, { { "encode", new_reference(encode.get()) }, { "decode", new_reference(decode.get()) } }) };
            ref const incrementalEncoder{ make("IncrementalEncoder", { c->encoder, incrementalEncoderBase.get() }, { { "codec", new_reference(codec.get()) } }) };
            ref const incrementalDecoder{ make("IncrementalDecoder", { c->decoder, incrementalDecoderBase.get() }, { { "codec", new_reference(codec.get()) } }) };
            ref const streamReader{ codecClass ? make("StreamReader", { codecClass.get(), c->reader, streamReaderBase.get() }, { { "codec", new_reference(codec.get()) } }) : ref{} };
            ref const streamWriter{ codecClass ? make("StreamWriter", { codecClass.get(), c->writer, streamWriterBase.get() }, { { "codec", new_reference(codec.get()) } }) : ref{} };
            if (!codecClass || !incrementalEncoder || !incrementalDecoder || !streamReader || !streamWriter)
                return nullptr;
            ref const instance{ call(codecClass.get(), ref{ make_tuple() }.get(), nullptr) };
            ref const instanceEncode{ instance ? getattr(instance.get(), "encode") : nullptr };
            ref const instanceDecode{ instance ? getattr(instance.get(), "decode") : nullptr };
            if (!instanceEncode || !instanceDecode)
                return nullptr;
            ref const name{ make_str(aName) };
            ref const kw{ keywords({ { "name", name.get() }, { "encode", instanceEncode.get() }, { "decode", instanceDecode.get() },
                { "incrementalencoder", incrementalEncoder.get() }, { "incrementaldecoder", incrementalDecoder.get() }, { "streamreader", streamReader.get() },
                { "streamwriter", streamWriter.get() } }) };
            ref const args{ make_tuple() };
            return call(codecInfo.get(), args.get(), kw.get());
        }
    }
}
