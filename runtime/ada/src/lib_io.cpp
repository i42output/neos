/*
  lib_io.cpp

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
*/

// The Ada runtime: the native primitives of the standard library (io): see ada.hpp
//
// The input-output packages (Ada.Text_IO, Ada.Sequential_IO, Ada.Direct_IO, Ada.Streams.Stream_IO and their wide variants)
// use external files through integer handles: 0, 1 and 2 are the program's standard input, output and error (the host's
// std_* functions); the others are files opened by io_open/io_open_temp. Errors are returned as negative codes: -1 name
// (Name_Error), -2 use (Use_Error), -3 device (Device_Error).

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>
#include "ada.hpp"

namespace
{
    using namespace neos::ada_runtime;

    constexpr std::int64_t NameError = -1;
    constexpr std::int64_t UseError = -2;
    constexpr std::int64_t DeviceError = -3;

    enum class last_op { None, Read, Write };

    struct file
    {
        std::FILE* stream = nullptr;
        std::string name;               // full name
        bool temporary = false;
        bool canRead = false;
        bool canWrite = false;
        last_op last = last_op::None;
    };

    std::vector<std::unique_ptr<file>>& files()
    {
        static std::vector<std::unique_ptr<file>> sFiles(3u);
        return sFiles;
    }

    std::recursive_mutex& files_mutex()
    {
        static std::recursive_mutex sMutex;
        return sMutex;
    }

    bool is_std(std::int64_t aHandle)
    {
        return aHandle >= 0 && aHandle <= 2;
    }

    file* find(std::int64_t aHandle)
    {
        auto& all = files();
        if (aHandle < 3 || aHandle >= static_cast<std::int64_t>(all.size()))
            return nullptr;
        return all[static_cast<std::size_t>(aHandle)].get();
    }

    std::int64_t add(std::unique_ptr<file> aFile)
    {
        auto& all = files();
        for (std::size_t i = 3u; i < all.size(); ++i)
            if (!all[i])
            {
                all[i] = std::move(aFile);
                return static_cast<std::int64_t>(i);
            }
        all.push_back(std::move(aFile));
        return static_cast<std::int64_t>(all.size() - 1u);
    }

    std::int64_t error_code(int aErrno)
    {
        switch (aErrno)
        {
        case ENOENT:
        case ENOTDIR:
        case ENAMETOOLONG:
        case EINVAL:
#ifdef ELOOP
        case ELOOP:
#endif
            return NameError;
        case EIO:
            return DeviceError;
        default:
            return UseError;
        }
    }

    std::string to_string(address aName, std::int64_t aLength)
    {
        return aLength > 0 ? std::string{ chars(aName), static_cast<std::size_t>(aLength) } : std::string{};
    }

    char const* fopen_mode(std::int64_t aMode, bool& aRead, bool& aWrite)
    {
        // 0: read; 1: write (create/truncate); 2: append (create); 3: read/write (existing); 4: read/write (create/truncate);
        // 5: read/append (create); (+ 8: the file must exist)
        static char const* const sModes[] = { "rb", "wb", "ab", "r+b", "w+b", "a+b" };
        aMode &= 7;
        if (aMode < 0 || aMode > 5)
            return nullptr;
        aRead = aMode == 0 || aMode >= 3;
        aWrite = aMode != 0;
        return sModes[aMode];
    }

    std::string full_name(std::string const& aName)
    {
        std::error_code ec;
        auto const absolute = std::filesystem::absolute(std::filesystem::path{ aName }, ec);
        if (ec)
            return aName;
        auto const canonical = std::filesystem::weakly_canonical(absolute, ec);
        return (ec ? absolute : canonical).string();
    }

    // switching between reading and writing a stream needs a positioning call (C stdio)
    void prepare(file& aFile, last_op aOp)
    {
        if (aFile.last != last_op::None && aFile.last != aOp)
            std::fseek(aFile.stream, 0, SEEK_CUR);
        aFile.last = aOp;
    }
}

// io_open(Name, Name_Length, Mode): open (or create) the named external file (Mode: 0: read; 1: write (created/truncated);
// 2: append (created if need be); 3: read/write; 4: read/write (created/truncated); 5: read/append (created if need be);
// plus 8: the file must exist); result: its handle, or an error code
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_open(address aName, std::int64_t aLength, std::int64_t aMode)
{
    std::lock_guard<std::recursive_mutex> lock{ files_mutex() };
    auto const name = to_string(aName, aLength);
    if (name.empty() || name.find('\0') != std::string::npos)
        return NameError;
    bool canRead = false, canWrite = false;
    char const* const mode = fopen_mode(aMode, canRead, canWrite);
    if (mode == nullptr)
        return UseError;
    std::error_code ec;
    if (std::filesystem::is_directory(std::filesystem::path{ name }, ec))
        return UseError;
    if ((aMode & 8) != 0 && !std::filesystem::exists(std::filesystem::path{ name }, ec))
        return NameError;
    errno = 0;
    std::FILE* const stream = std::fopen(name.c_str(), mode);
    if (stream == nullptr)
        return error_code(errno);
    auto newFile = std::make_unique<file>();
    newFile->stream = stream;
    newFile->name = full_name(name);
    newFile->canRead = canRead;
    newFile->canWrite = canWrite;
    return add(std::move(newFile));
}

// io_open_temp(Mode): create a temporary file (deleted when closed); result: its handle, or an error code
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_open_temp(std::int64_t aMode)
{
    std::lock_guard<std::recursive_mutex> lock{ files_mutex() };
    bool canRead = false, canWrite = false;
    if (fopen_mode(aMode, canRead, canWrite) == nullptr)
        return UseError;
    std::error_code ec;
    auto const folder = std::filesystem::temp_directory_path(ec);
    if (ec)
        return UseError;
    static unsigned long sCounter = 0u;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        auto const unique = std::to_string(static_cast<unsigned long>(std::rand())) + "_" + std::to_string(++sCounter);
        auto const path = (folder / ("neos_ada_" + unique + ".tmp")).string();
        if (std::filesystem::exists(std::filesystem::path{ path }, ec))
            continue;
        std::FILE* const stream = std::fopen(path.c_str(), "w+b");
        if (stream == nullptr)
            return error_code(errno);
        auto newFile = std::make_unique<file>();
        newFile->stream = stream;
        newFile->name = full_name(path);
        newFile->temporary = true;
        newFile->canRead = true;
        newFile->canWrite = true;
        return add(std::move(newFile));
    }
    return UseError;
}

// io_reopen(Handle, Mode): reopen an open file with another mode (Reset); result: 0, or an error code
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_reopen(std::int64_t aHandle, std::int64_t aMode)
{
    std::lock_guard<std::recursive_mutex> lock{ files_mutex() };
    if (is_std(aHandle))
        return 0;
    file* const f = find(aHandle);
    if (f == nullptr)
        return UseError;
    bool canRead = false, canWrite = false;
    char const* const mode = fopen_mode(aMode, canRead, canWrite);
    if (mode == nullptr)
        return UseError;
    std::fclose(f->stream);
    errno = 0;
    f->stream = std::fopen(f->name.c_str(), mode);
    f->last = last_op::None;
    if (f->stream == nullptr)
    {
        auto const result = error_code(errno);
        if (f->temporary)
        {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path{ f->name }, ec);
        }
        files()[static_cast<std::size_t>(aHandle)].reset();
        return result == NameError ? UseError : result;
    }
    f->canRead = canRead;
    f->canWrite = canWrite;
    return 0;
}

// io_close(Handle): result: 0, or an error code
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_close(std::int64_t aHandle)
{
    std::lock_guard<std::recursive_mutex> lock{ files_mutex() };
    if (is_std(aHandle))
        return std_flush(static_cast<int>(aHandle)) == 0 || aHandle == 0 ? 0 : DeviceError;
    file* const f = find(aHandle);
    if (f == nullptr)
        return UseError;
    std::int64_t result = std::fclose(f->stream) == 0 ? 0 : DeviceError;
    if (f->temporary)
    {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path{ f->name }, ec);
    }
    files()[static_cast<std::size_t>(aHandle)].reset();
    return result;
}

// io_delete(Name, Name_Length): delete the named external file; result: 0, or an error code
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_delete(address aName, std::int64_t aLength)
{
    auto const name = to_string(aName, aLength);
    if (name.empty())
        return NameError;
    errno = 0;
    if (std::remove(name.c_str()) != 0)
        return error_code(errno);
    return 0;
}

// io_name(Handle, Buffer, Buffer_Length): the full name of an open file, written to Buffer; result: its length (which can be
// greater than Buffer_Length: then nothing is written)
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_name(std::int64_t aHandle, address aBuffer, std::int64_t aBufferLength)
{
    std::lock_guard<std::recursive_mutex> lock{ files_mutex() };
    static char const* const sStdNames[] = { "*stdin", "*stdout", "*stderr" };
    std::string name;
    if (is_std(aHandle))
        name = sStdNames[aHandle];
    else if (file* const f = find(aHandle))
        name = f->name;
    else
        return UseError;
    auto const length = static_cast<std::int64_t>(name.size());
    if (length <= aBufferLength && length > 0)
        std::memcpy(chars(aBuffer), name.data(), name.size());
    return length;
}

// io_read(Handle, Buffer, Buffer_Length): read at most Buffer_Length bytes (standard input: at most one line); result: the
// number of bytes read (0: end of file), or an error code
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_read(std::int64_t aHandle, address aBuffer, std::int64_t aBufferLength)
{
    if (aBufferLength <= 0)
        return 0;
    if (aHandle == 0)
    {
        std_flush(1);
        char* const buffer = chars(aBuffer);
        std::int64_t count = 0;
        while (count < aBufferLength)
        {
            int const ch = std_getc(0);
            if (ch == EOF)
                break;
            buffer[count++] = static_cast<char>(ch);
            if (ch == '\n')
                break;
        }
        return count;
    }
    if (is_std(aHandle))
        return UseError;
    std::lock_guard<std::recursive_mutex> lock{ files_mutex() };
    file* const f = find(aHandle);
    if (f == nullptr || !f->canRead)
        return UseError;
    prepare(*f, last_op::Read);
    auto const count = std::fread(chars(aBuffer), 1u, static_cast<std::size_t>(aBufferLength), f->stream);
    if (count == 0u && std::ferror(f->stream))
    {
        std::clearerr(f->stream);
        return DeviceError;
    }
    if (count < static_cast<std::size_t>(aBufferLength))
        std::clearerr(f->stream); // (so that a file that grows can be read further)
    return static_cast<std::int64_t>(count);
}

// io_write(Handle, Data, Data_Length, Line_Feeds): write Data followed by Line_Feeds line feed characters; result: 0, or an
// error code
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_write(std::int64_t aHandle, address aData, std::int64_t aLength, std::int64_t aLineFeeds)
{
    if (aHandle == 1 || aHandle == 2)
    {
        int const stream = static_cast<int>(aHandle);
        if (aHandle == 2)
            std_flush(1); // (so that the output to both appears in order)
        if (aLength > 0 && std_write(stream, chars(aData), static_cast<std::size_t>(aLength)) != static_cast<std::size_t>(aLength))
            return DeviceError;
        for (std::int64_t i = 0; i < aLineFeeds; ++i)
            if (std_write(stream, "\n", 1u) != 1u)
                return DeviceError;
        if (aHandle == 2)
            std_flush(2);
        return 0;
    }
    if (is_std(aHandle))
        return UseError;
    std::lock_guard<std::recursive_mutex> lock{ files_mutex() };
    file* const f = find(aHandle);
    if (f == nullptr || !f->canWrite)
        return UseError;
    prepare(*f, last_op::Write);
    if (aLength > 0 && std::fwrite(chars(aData), 1u, static_cast<std::size_t>(aLength), f->stream) != static_cast<std::size_t>(aLength))
        return DeviceError;
    for (std::int64_t i = 0; i < aLineFeeds; ++i)
        if (std::fputc('\n', f->stream) == EOF)
            return DeviceError;
    return 0;
}

// io_flush(Handle): result: 0, or an error code
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_flush(std::int64_t aHandle)
{
    if (is_std(aHandle))
        return aHandle == 0 || std_flush(static_cast<int>(aHandle)) == 0 ? 0 : DeviceError;
    std::lock_guard<std::recursive_mutex> lock{ files_mutex() };
    file* const f = find(aHandle);
    if (f == nullptr)
        return UseError;
    return std::fflush(f->stream) == 0 ? 0 : DeviceError;
}

// io_at_end(Handle): whether the next read would find the end of the file; result: 1 or 0, or an error code
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_at_end(std::int64_t aHandle)
{
    if (aHandle == 0)
    {
        std_flush(1);
        int const ch = std_getc(0);
        if (ch == EOF)
            return 1;
        std_ungetc(ch, 0);
        return 0;
    }
    if (is_std(aHandle))
        return UseError;
    std::lock_guard<std::recursive_mutex> lock{ files_mutex() };
    file* const f = find(aHandle);
    if (f == nullptr || !f->canRead)
        return UseError;
    prepare(*f, last_op::Read);
    int const ch = std::fgetc(f->stream);
    if (ch == EOF)
    {
        std::clearerr(f->stream);
        return 1;
    }
    std::ungetc(ch, f->stream);
    return 0;
}

// io_seek(Handle, Position): set the position (in bytes from the start) of the next read or write; result: 0, or an error code
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_seek(std::int64_t aHandle, std::int64_t aPosition)
{
    std::lock_guard<std::recursive_mutex> lock{ files_mutex() };
    file* const f = find(aHandle);
    if (f == nullptr || aPosition < 0)
        return UseError;
    f->last = last_op::None;
#ifdef _WIN32
    return _fseeki64(f->stream, aPosition, SEEK_SET) == 0 ? 0 : UseError;
#else
    return fseeko(f->stream, static_cast<off_t>(aPosition), SEEK_SET) == 0 ? 0 : UseError;
#endif
}

// io_size(Handle): the size of an open file in bytes; result: the size, or an error code
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_size(std::int64_t aHandle)
{
    std::lock_guard<std::recursive_mutex> lock{ files_mutex() };
    file* const f = find(aHandle);
    if (f == nullptr)
        return UseError;
    std::fflush(f->stream);
    std::error_code ec;
    auto const size = std::filesystem::file_size(std::filesystem::path{ f->name }, ec);
    if (ec)
        return UseError;
    return static_cast<std::int64_t>(size);
}

// io_float_digits(Value, Buffer, Buffer_Length): the exact decimal value of Value: a sign ('+' or '-'), its significant digits
// (none for zero) and 'e' followed by the decimal exponent X, so that Value = sign 0.digits * 10 ** X ("+15e1" for 1.5, "-e0"
// for -0.0); result: the length written (Buffer_Length must be at least 800), or -1 (NaN), -2 (+infinity), -3 (-infinity)
NEOS_ADA_PRIMITIVE std::int64_t neos_ada_io_float_digits(double aValue, address aBuffer, std::int64_t aBufferLength)
{
    if (std::isnan(aValue))
        return -1;
    if (std::isinf(aValue))
        return aValue > 0.0 ? -2 : -3;
    if (aBufferLength < 800)
        return 0;
    // a double's exact decimal expansion has at most 767 significant digits
    char text[1024];
    int const length = std::snprintf(text, sizeof(text), "%.780e", std::fabs(aValue));
    if (length <= 0 || length >= static_cast<int>(sizeof(text)))
        return 0;
    char* const buffer = chars(aBuffer);
    std::int64_t count = 0;
    buffer[count++] = std::signbit(aValue) ? '-' : '+';
    std::int64_t const firstDigit = count;
    int i = 0;
    for (; i < length && text[i] != 'e' && text[i] != 'E'; ++i)
        if (text[i] >= '0' && text[i] <= '9')
            buffer[count++] = text[i];
    while (count > firstDigit && buffer[count - 1] == '0')
        --count;
    long exponent = i < length ? std::strtol(text + i + 1, nullptr, 10) : 0;
    if (count == firstDigit)
        exponent = -1; // (zero)
    char exponentText[32];
    int const exponentLength = std::snprintf(exponentText, sizeof(exponentText), "e%ld", exponent + 1);
    std::memcpy(buffer + count, exponentText, static_cast<std::size_t>(exponentLength));
    return count + exponentLength;
}

// io_float_value(Text, Text_Length): the value of a decimal literal ("ddd.dddE+xx", or "0x" hexadecimal digits "p+xx"),
// correctly rounded (infinite if it overflows)
NEOS_ADA_PRIMITIVE double neos_ada_io_float_value(address aText, std::int64_t aLength)
{
    auto text = to_string(aText, aLength);
    char const decimalPoint = *std::localeconv()->decimal_point;
    if (decimalPoint != '.')
        for (auto& ch : text)
            if (ch == '.')
                ch = decimalPoint;
    return std::strtod(text.c_str(), nullptr);
}

namespace
{
    [[maybe_unused]] constexpr auto I64 = NEOS_RT_I64;
    [[maybe_unused]] constexpr auto F64 = NEOS_RT_F64;

    function_table const sioFunctions{
        { "io_open", NEOS_RT_FUNCTION(neos_ada_io_open), I64, 3u, { I64, I64, I64 }, NEOS_RT_RESTRICTED },
        { "io_open_temp", NEOS_RT_FUNCTION(neos_ada_io_open_temp), I64, 1u, { I64 }, NEOS_RT_RESTRICTED },
        { "io_reopen", NEOS_RT_FUNCTION(neos_ada_io_reopen), I64, 2u, { I64, I64 }, NEOS_RT_RESTRICTED },
        { "io_close", NEOS_RT_FUNCTION(neos_ada_io_close), I64, 1u, { I64 }, 0 },
        { "io_delete", NEOS_RT_FUNCTION(neos_ada_io_delete), I64, 2u, { I64, I64 }, NEOS_RT_RESTRICTED },
        { "io_name", NEOS_RT_FUNCTION(neos_ada_io_name), I64, 3u, { I64, I64, I64 }, 0 },
        { "io_read", NEOS_RT_FUNCTION(neos_ada_io_read), I64, 3u, { I64, I64, I64 }, 0 },
        { "io_write", NEOS_RT_FUNCTION(neos_ada_io_write), I64, 4u, { I64, I64, I64, I64 }, 0 },
        { "io_flush", NEOS_RT_FUNCTION(neos_ada_io_flush), I64, 1u, { I64 }, 0 },
        { "io_at_end", NEOS_RT_FUNCTION(neos_ada_io_at_end), I64, 1u, { I64 }, 0 },
        { "io_seek", NEOS_RT_FUNCTION(neos_ada_io_seek), I64, 2u, { I64, I64 }, 0 },
        { "io_size", NEOS_RT_FUNCTION(neos_ada_io_size), I64, 1u, { I64 }, 0 },
        { "io_float_digits", NEOS_RT_FUNCTION(neos_ada_io_float_digits), I64, 3u, { F64, I64, I64 }, 0 },
        { "io_float_value", NEOS_RT_FUNCTION(neos_ada_io_float_value), F64, 2u, { I64, I64 }, 0 },
    };
}
