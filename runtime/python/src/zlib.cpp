/*
  zlib.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/zlibmodule.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// The module zlib (CPython 3.13's zlibmodule.c: its functions, Compress, Decompress and _ZlibDecompressor, their behaviour and messages)
// with the bundled zlib (3rdparty/zlib-1.3.1)

#include <algorithm>
#include <climits>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            constexpr std::size_t kBufferSize = 16u * 1024u;   // (DEF_BUF_SIZE)

            enum class zlib_kind { Compress, Decompress, Decompressor };

            struct zlib_object
            {
                header h;
                zlib_kind kind;
                z_stream zst{};
                bool initialised = false;
                bool eof = false;
                bool needsInput = true;
                object* unusedData = make_bytes("");
                object* unconsumedTail = make_bytes("");
                object* zdict = nullptr;
                std::string input;      ///< (_ZlibDecompressor: the input it has not consumed)
                ~zlib_object()
                {
                    if (initialised)
                    {
                        if (kind == zlib_kind::Compress)
                            deflateEnd(&zst);
                        else
                            inflateEnd(&zst);
                    }
                    release(unusedData);
                    release(unconsumedTail);
                    release(zdict);
                }
            };

            neos_py_type make_zlib_type(zlib_kind aKind);
            neos_py_type sCompressType = make_zlib_type(zlib_kind::Compress);
            neos_py_type sDecompressType = make_zlib_type(zlib_kind::Decompress);
            neos_py_type sDecompressorType = make_zlib_type(zlib_kind::Decompressor);

            zlib_object& as_zlib(object* aObject) { return *reinterpret_cast<zlib_object*>(aObject); }

            bool is_zlib(object* aObject, zlib_kind aKind)
            {
                auto const type = aKind == zlib_kind::Compress ? &sCompressType : aKind == zlib_kind::Decompress ? &sDecompressType : &sDecompressorType;
                return aObject != nullptr && aObject->type == &type->base;
            }

            object* make_zlib(zlib_kind aKind)
            {
                auto const type = aKind == zlib_kind::Compress ? &sCompressType : aKind == zlib_kind::Decompress ? &sDecompressType : &sDecompressorType;
                auto* const z = new zlib_object{ { { &type->base } }, aKind };
                return &z->h.base;
            }

            // (zlib.error)
            std::nullptr_t zlib_error(z_stream const& aZst, int aErr, char const* aMessage)
            {
                char const* zmsg = aErr == Z_VERSION_ERROR ? "library version mismatch" : aZst.msg;
                if (zmsg == nullptr)
                    switch (aErr)
                    {
                    case Z_BUF_ERROR: zmsg = "incomplete or truncated stream"; break;
                    case Z_STREAM_ERROR: zmsg = "inconsistent stream state"; break;
                    case Z_DATA_ERROR: zmsg = "invalid input data"; break;
                    default: break;
                    }
                std::string text = "Error " + std::to_string(aErr) + " " + aMessage;
                if (zmsg != nullptr)
                    text += std::string{ ": " } + std::string{ zmsg }.substr(0u, 200u);
                ref const cls{ module_exception("zlib.error", { "Exception" }) };
                ref const e{ cls ? invoke(cls.get(), { make_str(text) }) : nullptr };
                if (e)
                    fn_raise(e.get());
                return nullptr;
            }

            bool data_of(object* aObject, std::string_view& aData)
            {
                if (bytes_like(aObject, aData))
                    return true;
                return (error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(aObject) + "'"), false);
            }

            bool int_of(object* aObject, long long aDefault, long long& aResult)
            {
                if (aObject == nullptr)
                    return (aResult = aDefault, true);
                if (kind_of(aObject) == NEOS_PY_FLOAT)
                    return (error("TypeError", "'float' object cannot be interpreted as an integer"), false);
                return to_index(aObject, aResult);
            }

            // (the input, given to zlib in pieces of at most UINT_MAX bytes)
            void arrange_input(z_stream& aZst, std::size_t& aRemains)
            {
                aZst.avail_in = static_cast<uInt>(std::min<std::size_t>(aRemains, UINT_MAX));
                aRemains -= aZst.avail_in;
            }

            // (output space: the buffer grows (aLimit: at most this many bytes (-1: no limit)); false: the limit has been reached)
            bool arrange_output(z_stream& aZst, std::string& aOutput, std::size_t& aUsed, long long aLimit)
            {
                aUsed = aOutput.size() - aZst.avail_out;
                if (aZst.avail_out != 0u)
                    return true;
                if (aLimit >= 0 && aUsed >= static_cast<std::size_t>(aLimit))
                    return false;
                std::size_t grow = std::max<std::size_t>(kBufferSize, aOutput.size());
                if (aLimit >= 0)
                    grow = std::min<std::size_t>(grow, static_cast<std::size_t>(aLimit) - aUsed);
                aOutput.resize(aUsed + grow);
                aZst.next_out = reinterpret_cast<Bytef*>(aOutput.data() + aUsed);
                aZst.avail_out = static_cast<uInt>(grow);
                return true;
            }

            object* finish_output(z_stream& aZst, std::string& aOutput)
            {
                aOutput.resize(aOutput.size() - aZst.avail_out);
                aZst.avail_out = 0u;
                return make_bytes(std::move(aOutput));
            }

            // --- the functions

            object* compress(object* aData, object* aLevel, object* aWbits)
            {
                std::string_view data;
                long long level = 0, wbits = 0;
                if (!data_of(aData, data) || !int_of(aLevel, Z_DEFAULT_COMPRESSION, level) || !int_of(aWbits, MAX_WBITS, wbits))
                    return nullptr;
                z_stream zst{};
                zst.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
                auto err = deflateInit2(&zst, static_cast<int>(level), Z_DEFLATED, static_cast<int>(wbits), 8, Z_DEFAULT_STRATEGY);
                switch (err)
                {
                case Z_OK: break;
                case Z_MEM_ERROR: return error("MemoryError", "Out of memory while compressing data");
                case Z_STREAM_ERROR:
                    {
                        ref const cls{ module_exception("zlib.error", { "Exception" }) };
                        ref const e{ cls ? invoke(cls.get(), { make_str("Bad compression level") }) : nullptr };
                        if (e)
                            fn_raise(e.get());
                        return nullptr;
                    }
                default:
                    deflateEnd(&zst);
                    return zlib_error(zst, err, "while compressing data");
                }
                std::string output;
                std::size_t remains = data.size(), used = 0u;
                int flush = Z_NO_FLUSH;
                do
                {
                    arrange_input(zst, remains);
                    flush = remains == 0u ? Z_FINISH : Z_NO_FLUSH;
                    do
                    {
                        arrange_output(zst, output, used, -1);
                        err = deflate(&zst, flush);
                        if (err == Z_STREAM_ERROR)
                        {
                            deflateEnd(&zst);
                            return zlib_error(zst, err, "while compressing data");
                        }
                    } while (zst.avail_out == 0u);
                } while (flush != Z_FINISH);
                err = deflateEnd(&zst);
                if (err != Z_OK)
                    return zlib_error(zst, err, "while finishing compression");
                return finish_output(zst, output);
            }

            object* decompress(object* aData, object* aWbits, object* aBufsize)
            {
                std::string_view data;
                long long wbits = 0, bufsize = 0;
                if (!data_of(aData, data) || !int_of(aWbits, MAX_WBITS, wbits) || !int_of(aBufsize, static_cast<long long>(kBufferSize), bufsize))
                    return nullptr;
                if (bufsize < 0)
                    return error("ValueError", "bufsize must be non-negative");
                z_stream zst{};
                zst.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
                auto err = inflateInit2(&zst, static_cast<int>(wbits));
                switch (err)
                {
                case Z_OK: break;
                case Z_MEM_ERROR: return error("MemoryError", "Out of memory while decompressing data");
                default:
                    inflateEnd(&zst);
                    return zlib_error(zst, err, "while preparing to decompress data");
                }
                std::string output(static_cast<std::size_t>(std::max(bufsize, 1LL)), '\0');
                zst.next_out = reinterpret_cast<Bytef*>(output.data());
                zst.avail_out = static_cast<uInt>(output.size());
                std::size_t remains = data.size(), used = 0u;
                do
                {
                    arrange_input(zst, remains);
                    int const flush = remains == 0u ? Z_FINISH : Z_NO_FLUSH;
                    do
                    {
                        arrange_output(zst, output, used, -1);
                        err = inflate(&zst, flush);
                        switch (err)
                        {
                        case Z_OK: case Z_BUF_ERROR: case Z_STREAM_END: break;
                        case Z_MEM_ERROR:
                            inflateEnd(&zst);
                            return error("MemoryError", "Out of memory while decompressing data");
                        default:
                            inflateEnd(&zst);
                            return zlib_error(zst, err, "while decompressing data");
                        }
                    } while (zst.avail_out == 0u);
                } while (err != Z_STREAM_END && remains != 0u);
                if (err != Z_STREAM_END)
                {
                    inflateEnd(&zst);
                    return zlib_error(zst, err, "while decompressing data");
                }
                err = inflateEnd(&zst);
                if (err != Z_OK)
                    return zlib_error(zst, err, "while finishing decompression");
                return finish_output(zst, output);
            }

            bool set_inflate_zdict(zlib_object& aSelf)
            {
                std::string_view zdict;
                if (!bytes_like(aSelf.zdict, zdict))
                    return (error("TypeError", "zdict argument must support the buffer protocol"), false);
                if (zdict.size() > UINT_MAX)
                    return (error("OverflowError", "zdict length does not fit in an unsigned int"), false);
                auto const err = inflateSetDictionary(&aSelf.zst, reinterpret_cast<Bytef const*>(zdict.data()), static_cast<uInt>(zdict.size()));
                if (err != Z_OK)
                    return (zlib_error(aSelf.zst, err, "while setting zdict"), false);
                return true;
            }

            object* compressobj(std::vector<object*> const& a)
            {
                long long level = 0, method = 0, wbits = 0, memLevel = 0, strategy = 0;
                if (!int_of(a[0], Z_DEFAULT_COMPRESSION, level) || !int_of(a[1], Z_DEFLATED, method) || !int_of(a[2], MAX_WBITS, wbits) ||
                    !int_of(a[3], 8, memLevel) || !int_of(a[4], Z_DEFAULT_STRATEGY, strategy))
                    return nullptr;
                std::string_view zdict;
                bool const hasZdict = a[5] != nullptr && kind_of(a[5]) != NEOS_PY_NONE;
                if (hasZdict && !data_of(a[5], zdict))
                    return nullptr;
                if (zdict.size() > UINT_MAX)
                    return error("OverflowError", "zdict length does not fit in an unsigned int");
                ref const result{ make_zlib(zlib_kind::Compress) };
                auto& self = as_zlib(result.get());
                auto err = deflateInit2(&self.zst, static_cast<int>(level), static_cast<int>(method), static_cast<int>(wbits), static_cast<int>(memLevel),
                    static_cast<int>(strategy));
                switch (err)
                {
                case Z_OK:
                    self.initialised = true;
                    if (hasZdict)
                    {
                        err = deflateSetDictionary(&self.zst, reinterpret_cast<Bytef const*>(zdict.data()), static_cast<uInt>(zdict.size()));
                        if (err == Z_STREAM_ERROR)
                            return error("ValueError", "Invalid dictionary");
                        if (err != Z_OK)
                            return error("ValueError", "deflateSetDictionary()");
                    }
                    return new_reference(result.get());
                case Z_MEM_ERROR: return error("MemoryError", "Can't allocate memory for compression object");
                case Z_STREAM_ERROR: return error("ValueError", "Invalid initialization option");
                default: return zlib_error(self.zst, err, "while creating compression object");
                }
            }

            object* decompressobj(object* aWbits, object* aZdict, zlib_kind aKind)
            {
                long long wbits = 0;
                if (!int_of(aWbits, MAX_WBITS, wbits))
                    return nullptr;
                std::string_view check;
                if (aZdict != nullptr && !bytes_like(aZdict, check))
                    return error("TypeError", "zdict argument must support the buffer protocol");
                ref const result{ make_zlib(aKind) };
                auto& self = as_zlib(result.get());
                if (aZdict != nullptr)
                    self.zdict = new_reference(aZdict);
                auto const err = inflateInit2(&self.zst, static_cast<int>(wbits));
                switch (err)
                {
                case Z_OK:
                    self.initialised = true;
                    if (self.zdict != nullptr && wbits < 0 && !set_inflate_zdict(self))
                        return nullptr;
                    return new_reference(result.get());
                case Z_STREAM_ERROR: return error("ValueError", "Invalid initialization option");
                case Z_MEM_ERROR: return error("MemoryError", "Can't allocate memory for decompression object");
                default: return zlib_error(self.zst, err, "while creating decompression object");
                }
            }

            // --- Compress's methods

            object* compress_compress(zlib_object& aSelf, object* aData)
            {
                std::string_view data;
                if (!data_of(aData, data))
                    return nullptr;
                aSelf.zst.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
                std::size_t remains = data.size(), used = 0u;
                std::string output;
                aSelf.zst.avail_out = 0u;
                do
                {
                    arrange_input(aSelf.zst, remains);
                    do
                    {
                        arrange_output(aSelf.zst, output, used, -1);
                        auto const err = deflate(&aSelf.zst, Z_NO_FLUSH);
                        if (err == Z_STREAM_ERROR)
                            return zlib_error(aSelf.zst, err, "while compressing data");
                    } while (aSelf.zst.avail_out == 0u);
                } while (remains != 0u);
                return finish_output(aSelf.zst, output);
            }

            object* compress_flush(zlib_object& aSelf, object* aMode)
            {
                long long mode = 0;
                if (!int_of(aMode, Z_FINISH, mode))
                    return nullptr;
                if (mode == Z_NO_FLUSH)
                    return make_bytes("");
                aSelf.zst.avail_in = 0u;
                std::string output;
                std::size_t used = 0u;
                aSelf.zst.avail_out = 0u;
                int err = Z_OK;
                do
                {
                    arrange_output(aSelf.zst, output, used, -1);
                    err = deflate(&aSelf.zst, static_cast<int>(mode));
                    if (err == Z_STREAM_ERROR)
                        return zlib_error(aSelf.zst, err, "while flushing");
                } while (aSelf.zst.avail_out == 0u);
                if (err == Z_STREAM_END && mode == Z_FINISH)
                {
                    err = deflateEnd(&aSelf.zst);
                    if (err != Z_OK)
                        return zlib_error(aSelf.zst, err, "while finishing compression");
                    aSelf.initialised = false;
                }
                else if (err != Z_OK && err != Z_BUF_ERROR)
                    return zlib_error(aSelf.zst, err, "while flushing");
                return finish_output(aSelf.zst, output);
            }

            object* zlib_copy(zlib_object& aSelf)
            {
                ref const result{ make_zlib(aSelf.kind) };
                auto& copy = as_zlib(result.get());
                if (!aSelf.initialised)
                    return error("ValueError", "Inconsistent stream state");
                auto const err = aSelf.kind == zlib_kind::Compress ? deflateCopy(&copy.zst, &aSelf.zst) : inflateCopy(&copy.zst, &aSelf.zst);
                switch (err)
                {
                case Z_OK: break;
                case Z_STREAM_ERROR: return error("ValueError", "Inconsistent stream state");
                case Z_MEM_ERROR: return error("MemoryError", aSelf.kind == zlib_kind::Compress ? "Can't allocate memory for compression object" :
                    "Can't allocate memory for decompression object");
                default: return zlib_error(aSelf.zst, err, aSelf.kind == zlib_kind::Compress ? "while copying compression object" : "while copying decompression object");
                }
                release(copy.unusedData);
                release(copy.unconsumedTail);
                copy.unusedData = new_reference(aSelf.unusedData);
                copy.unconsumedTail = new_reference(aSelf.unconsumedTail);
                copy.zdict = aSelf.zdict != nullptr ? new_reference(aSelf.zdict) : nullptr;
                copy.eof = aSelf.eof;
                copy.initialised = true;
                return new_reference(result.get());
            }

            // --- Decompress's methods

            // (save_unconsumed_input: the input after the end of the stream (unused_data) and the input not yet used (unconsumed_tail))
            void save_unconsumed_input(zlib_object& aSelf, std::string_view aData, int aErr)
            {
                auto const consumedTo = static_cast<std::size_t>(reinterpret_cast<char const*>(aSelf.zst.next_in) - aData.data());
                if (aErr == Z_STREAM_END && aSelf.zst.avail_in > 0u)
                {
                    std::string_view unused;
                    bytes_like(aSelf.unusedData, unused);
                    auto const data = make_bytes(std::string{ unused } + std::string{ aData.substr(consumedTo) });
                    release(aSelf.unusedData);
                    aSelf.unusedData = data;
                    aSelf.zst.avail_in = 0u;
                }
                std::string_view tail;
                bytes_like(aSelf.unconsumedTail, tail);
                if (aSelf.zst.avail_in > 0u || !tail.empty())
                {
                    auto const data = make_bytes(std::string{ aData.substr(consumedTo) });
                    release(aSelf.unconsumedTail);
                    aSelf.unconsumedTail = data;
                }
            }

            object* decompress_decompress(zlib_object& aSelf, object* aData, object* aMaxLength)
            {
                std::string_view data;
                long long maxLength = 0;
                if (!data_of(aData, data) || !int_of(aMaxLength, 0, maxLength))
                    return nullptr;
                if (maxLength < 0)
                    return error("ValueError", "max_length must be non-negative");
                if (maxLength == 0)
                    maxLength = -1;
                // (the data is kept while zlib uses it: unconsumed_tail and unused_data are copies)
                std::string const input{ data };
                aSelf.zst.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
                std::size_t remains = input.size(), used = 0u;
                std::string output;
                aSelf.zst.avail_out = 0u;
                int err = Z_OK;
                bool limited = false;
                do
                {
                    arrange_input(aSelf.zst, remains);
                    do
                    {
                        if (!arrange_output(aSelf.zst, output, used, maxLength))
                        {
                            limited = true;
                            break;
                        }
                        err = inflate(&aSelf.zst, Z_SYNC_FLUSH);
                        if (err == Z_OK || err == Z_BUF_ERROR || err == Z_STREAM_END)
                            continue;
                        if (err == Z_NEED_DICT && aSelf.zdict != nullptr)
                        {
                            if (!set_inflate_zdict(aSelf))
                                return nullptr;
                            continue;
                        }
                        break;
                    } while (aSelf.zst.avail_out == 0u || err == Z_NEED_DICT);
                    if (limited || (err != Z_OK && err != Z_BUF_ERROR && err != Z_STREAM_END))
                        break;
                } while (err != Z_STREAM_END && remains != 0u);
                // (the input that zlib has not been given)
                aSelf.zst.avail_in += static_cast<uInt>(remains);
                save_unconsumed_input(aSelf, input, err);
                if (err == Z_STREAM_END)
                    aSelf.eof = true;
                else if (err != Z_OK && err != Z_BUF_ERROR)
                    return zlib_error(aSelf.zst, err, "while decompressing data");
                return finish_output(aSelf.zst, output);
            }

            object* decompress_flush(zlib_object& aSelf, object* aLength)
            {
                long long length = 0;
                if (!int_of(aLength, static_cast<long long>(kBufferSize), length))
                    return nullptr;
                if (length <= 0)
                    return error("ValueError", "length must be greater than zero");
                std::string_view tail;
                bytes_like(aSelf.unconsumedTail, tail);
                std::string const input{ tail };
                ref const keep{ new_reference(aSelf.unconsumedTail) };
                aSelf.zst.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
                std::size_t remains = input.size(), used = 0u;
                std::string output(static_cast<std::size_t>(length), '\0');
                aSelf.zst.next_out = reinterpret_cast<Bytef*>(output.data());
                aSelf.zst.avail_out = static_cast<uInt>(output.size());
                int err = Z_OK;
                do
                {
                    arrange_input(aSelf.zst, remains);
                    int const flush = remains == 0u ? Z_FINISH : Z_NO_FLUSH;
                    bool stop = false;
                    do
                    {
                        arrange_output(aSelf.zst, output, used, -1);
                        err = inflate(&aSelf.zst, flush);
                        if (err != Z_OK && err != Z_BUF_ERROR && err != Z_STREAM_END)
                        {
                            stop = true;
                            break;
                        }
                    } while (aSelf.zst.avail_out == 0u || err == Z_NEED_DICT);
                    if (stop)
                        break;
                } while (err != Z_STREAM_END && remains != 0u);
                aSelf.zst.avail_in += static_cast<uInt>(remains);
                save_unconsumed_input(aSelf, input, err);
                if (err == Z_STREAM_END)
                {
                    aSelf.eof = true;
                    aSelf.initialised = false;
                    err = inflateEnd(&aSelf.zst);
                    if (err != Z_OK)
                        return zlib_error(aSelf.zst, err, "while finishing decompression");
                }
                return finish_output(aSelf.zst, output);
            }

            // --- _ZlibDecompressor's decompress

            object* decompressor_decompress(zlib_object& aSelf, object* aData, object* aMaxLength)
            {
                std::string_view data;
                long long maxLength = 0;
                if (!data_of(aData, data) || !int_of(aMaxLength, -1, maxLength))
                    return nullptr;
                if (aSelf.eof)
                    return error("EOFError", "End of stream already reached");
                aSelf.input.append(data);
                aSelf.zst.next_in = reinterpret_cast<Bytef*>(aSelf.input.data());
                std::size_t remains = aSelf.input.size(), used = 0u;
                std::string output;
                if (maxLength >= 0)
                    output.reserve(static_cast<std::size_t>(std::min<long long>(maxLength, 8 * 1024 * 1024)));
                aSelf.zst.avail_out = 0u;
                int err = Z_OK;
                bool limited = false;
                do
                {
                    arrange_input(aSelf.zst, remains);
                    do
                    {
                        if (!arrange_output(aSelf.zst, output, used, maxLength))
                        {
                            limited = true;
                            break;
                        }
                        err = inflate(&aSelf.zst, Z_SYNC_FLUSH);
                        if (err == Z_NEED_DICT)
                            return error("ValueError", "zdict required");
                    } while (aSelf.zst.avail_out == 0u);
                    if (limited)
                        break;
                } while (err != Z_STREAM_END && remains != 0u);
                auto const left = remains + aSelf.zst.avail_in;
                if (err == Z_STREAM_END)
                {
                    aSelf.eof = true;
                    aSelf.initialised = false;
                    err = inflateEnd(&aSelf.zst);
                    if (err != Z_OK)
                        return zlib_error(aSelf.zst, err, "while finishing decompression");
                }
                else if (err != Z_OK && err != Z_BUF_ERROR)
                    return zlib_error(aSelf.zst, err, "while decompressing data");
                auto const rest = aSelf.input.substr(aSelf.input.size() - left);
                if (aSelf.eof)
                {
                    aSelf.needsInput = false;
                    if (!rest.empty())
                    {
                        release(aSelf.unusedData);
                        aSelf.unusedData = make_bytes(rest);
                    }
                    aSelf.input.clear();
                }
                else if (left == 0u)
                {
                    aSelf.needsInput = true;
                    aSelf.input.clear();
                }
                else
                {
                    aSelf.needsInput = false;
                    aSelf.input = rest;
                }
                aSelf.zst.next_in = nullptr;
                aSelf.zst.avail_in = 0u;
                return finish_output(aSelf.zst, output);
            }

            // --- the types

            object* zlib_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::string_view const name{ static_cast<char const*>(aData) };
                auto const& args = as<tuple_object>(aArgs)->items;
                auto& self = as_zlib(args[0]);
                std::vector<object*> const rest{ args.begin() + 1, args.end() };
                std::vector<object*> a;
                if (name == "compress")
                    return parse_arguments("compress", rest, aKwargs, { "data" }, 1u, 1u, a) ? compress_compress(self, a[0]) : nullptr;
                if (name == "flush" && self.kind == zlib_kind::Compress)
                    return parse_arguments("flush", rest, aKwargs, { "mode" }, 0u, 1u, a) ? compress_flush(self, a[0]) : nullptr;
                if (name == "flush")
                    return parse_arguments("flush", rest, aKwargs, { "length" }, 0u, 1u, a) ? decompress_flush(self, a[0]) : nullptr;
                if (name == "decompress" && self.kind == zlib_kind::Decompress)
                    return parse_arguments("decompress", rest, aKwargs, { "data", "max_length" }, 1u, 1u, a) ? decompress_decompress(self, a[0], a[1]) : nullptr;
                if (name == "decompress")
                    return parse_arguments("decompress", rest, aKwargs, { "data", "max_length" }, 1u, 0u, a) ? decompressor_decompress(self, a[0], a[1]) : nullptr;
                if (name == "copy" || name == "__copy__")
                    return parse_arguments(std::string{ name }.c_str(), rest, aKwargs, {}, 0u, 0u, a) ? zlib_copy(self) : nullptr;
                // (__deepcopy__(memo))
                return parse_arguments("__deepcopy__", rest, aKwargs, { "memo" }, 1u, 1u, a) ? zlib_copy(self) : nullptr;
            }

            neos_py_type make_zlib_type(zlib_kind aKind)
            {
                auto const qualified = aKind == zlib_kind::Compress ? "zlib.Compress" : aKind == zlib_kind::Decompress ? "zlib.Decompress" : "zlib._ZlibDecompressor";
                auto const name = aKind == zlib_kind::Compress ? "Compress" : aKind == zlib_kind::Decompress ? "Decompress" : "_ZlibDecompressor";
                auto type = make_type(qualified, name, retain_object, release_object<zlib_object>);
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& self = as_zlib(aSelf);
                    std::string_view const name{ aName };
                    static char const* const sCompressMethods[] = { "compress", "flush", "copy", "__copy__", "__deepcopy__" };
                    static char const* const sDecompressMethods[] = { "decompress", "flush", "copy", "__copy__", "__deepcopy__" };
                    static char const* const sDecompressorMethods[] = { "decompress" };
                    auto const find = [&](auto const& aMethods) -> char const*
                    {
                        for (auto m : aMethods)
                            if (name == m)
                                return m;
                        return nullptr;
                    };
                    char const* const m = self.kind == zlib_kind::Compress ? find(sCompressMethods) : self.kind == zlib_kind::Decompress ? find(sDecompressMethods) :
                        find(sDecompressorMethods);
                    if (m != nullptr)
                        return make_function(m, zlib_method, const_cast<char*>(m), aSelf);
                    if (self.kind != zlib_kind::Compress)
                    {
                        if (name == "unused_data")
                            return new_reference(self.unusedData);
                        if (name == "eof")
                            return make_bool(self.eof);
                        if (self.kind == zlib_kind::Decompress && name == "unconsumed_tail")
                            return new_reference(self.unconsumedTail);
                        if (self.kind == zlib_kind::Decompressor && name == "needs_input")
                            return make_bool(self.needsInput);
                    }
                    return nullptr;
                };
                return type;
            }

            object* decompressor_new(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("_ZlibDecompressor", as<tuple_object>(aArgs)->items, aKwargs, { "wbits", "zdict" }, 0u, 0u, a))
                    return nullptr;
                return decompressobj(a[0], a[1], zlib_kind::Decompressor);
            }

            int decompressor_instancecheck(neos_py_context*, void*, object* aObject) { return is_zlib(aObject, zlib_kind::Decompressor) ? 1 : 0; }

            unsigned long checksum_value(object* aValue, unsigned long aDefault, bool& aOk)
            {
                aOk = true;
                if (aValue == nullptr)
                    return aDefault;
                if (!is_int_like(aValue))
                {
                    aOk = false;
                    error("TypeError", std::string{ "'" } + type_name(aValue) + "' object cannot be interpreted as an integer");
                    return 0u;
                }
                // (the value's low 32 bits (unsigned int(bitwise=True)))
                ref const mask{ make_int(0xFFFFFFFFLL) };
                ref const low{ binary(NEOS_PY_AND, aValue, mask.get(), false) };
                long long v = 0;
                if (!low || !to_index(low.get(), v))
                {
                    aOk = false;
                    return 0u;
                }
                return static_cast<unsigned long>(v);
            }

            builtin_entry const sZlibFunctions[] =
            {
                { "adler32", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string_view data;
                        if (!parse_arguments("adler32", aArgs, aKwargs, { "data", "value" }, 1u, 2u, a) || !data_of(a[0], data))
                            return nullptr;
                        bool ok = true;
                        auto value = checksum_value(a[1], 1u, ok);
                        if (!ok)
                            return nullptr;
                        for (std::size_t i = 0u; i < data.size();)
                        {
                            auto const n = std::min<std::size_t>(data.size() - i, UINT_MAX);
                            value = adler32(value, reinterpret_cast<Bytef const*>(data.data() + i), static_cast<uInt>(n));
                            i += n;
                        }
                        return make_int(static_cast<long long>(value & 0xFFFFFFFFu));
                    } },
                { "crc32", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string_view data;
                        if (!parse_arguments("crc32", aArgs, aKwargs, { "data", "value" }, 1u, 2u, a) || !data_of(a[0], data))
                            return nullptr;
                        bool ok = true;
                        auto value = checksum_value(a[1], 0u, ok);
                        if (!ok)
                            return nullptr;
                        for (std::size_t i = 0u; i < data.size();)
                        {
                            auto const n = std::min<std::size_t>(data.size() - i, UINT_MAX);
                            value = crc32(value, reinterpret_cast<Bytef const*>(data.data() + i), static_cast<uInt>(n));
                            i += n;
                        }
                        return make_int(static_cast<long long>(value & 0xFFFFFFFFu));
                    } },
                { "compress", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("compress", aArgs, aKwargs, { "data", "level", "wbits" }, 1u, 1u, a))
                            return nullptr;
                        return compress(a[0], a[1], a[2]);
                    } },
                { "decompress", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("decompress", aArgs, aKwargs, { "data", "wbits", "bufsize" }, 1u, 1u, a))
                            return nullptr;
                        return decompress(a[0], a[1], a[2]);
                    } },
                { "compressobj", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("compressobj", aArgs, aKwargs, { "level", "method", "wbits", "memLevel", "strategy", "zdict" }, 0u, 0u, a))
                            return nullptr;
                        return compressobj(a);
                    } },
                { "decompressobj", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("decompressobj", aArgs, aKwargs, { "wbits", "zdict" }, 0u, 0u, a))
                            return nullptr;
                        return decompressobj(a[0], a[1], zlib_kind::Decompress);
                    } }
            };

            bool zlib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sZlibFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("_ZlibDecompressor", context()->type_new(context(), "_ZlibDecompressor", "zlib._ZlibDecompressor", decompressor_new, nullptr,
                    decompressor_instancecheck));
                aAttributes.emplace_back("error", module_exception("zlib.error", { "Exception" }));
                for (auto const& [name, value] : { std::pair{ "MAX_WBITS", MAX_WBITS }, std::pair{ "DEFLATED", Z_DEFLATED }, std::pair{ "DEF_MEM_LEVEL", 8 },
                    std::pair{ "DEF_BUF_SIZE", static_cast<int>(kBufferSize) }, std::pair{ "Z_NO_COMPRESSION", Z_NO_COMPRESSION }, std::pair{ "Z_BEST_SPEED", Z_BEST_SPEED },
                    std::pair{ "Z_BEST_COMPRESSION", Z_BEST_COMPRESSION }, std::pair{ "Z_DEFAULT_COMPRESSION", Z_DEFAULT_COMPRESSION },
                    std::pair{ "Z_FILTERED", Z_FILTERED }, std::pair{ "Z_HUFFMAN_ONLY", Z_HUFFMAN_ONLY }, std::pair{ "Z_RLE", Z_RLE }, std::pair{ "Z_FIXED", Z_FIXED },
                    std::pair{ "Z_DEFAULT_STRATEGY", Z_DEFAULT_STRATEGY }, std::pair{ "Z_NO_FLUSH", Z_NO_FLUSH }, std::pair{ "Z_PARTIAL_FLUSH", Z_PARTIAL_FLUSH },
                    std::pair{ "Z_SYNC_FLUSH", Z_SYNC_FLUSH }, std::pair{ "Z_FULL_FLUSH", Z_FULL_FLUSH }, std::pair{ "Z_FINISH", Z_FINISH }, std::pair{ "Z_BLOCK", Z_BLOCK },
                    std::pair{ "Z_TREES", Z_TREES } })
                    aAttributes.emplace_back(name, make_int(value));
                aAttributes.emplace_back("ZLIB_VERSION", make_str(ZLIB_VERSION));
                aAttributes.emplace_back("ZLIB_RUNTIME_VERSION", make_str(zlibVersion()));
                return true;
            }

            inline bool const sZlib = register_module("zlib", zlib_module);
        }
    }
}
