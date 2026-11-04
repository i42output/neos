/*
  lib_stdio.cpp

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

// The C language's runtime: <stdio.h> (7.23). A FILE is an object in the C memory (the standard streams' at fixed addresses, see
// neos/language/c.hpp; files' allocated) that the runtime's stream object for it is found by. The standard streams are the
// program's (its host's: see neos_rt_host::std_write); standard output is line buffered if the process's is a terminal, else
// fully buffered; standard error is unbuffered.

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#if defined(_WIN32)
#include <io.h>
#define NEOS_ISATTY _isatty
#else
#include <unistd.h>
#define NEOS_ISATTY isatty
#endif
#include "format.hpp"

namespace neos::c_runtime
{
    namespace
    {
        constexpr int IOFBF = 0;
        constexpr int IOLBF = 1;
        constexpr int IONBF = 2;

        struct stream
        {
            address cell = 0u;
            int standard = -1;              ///< (0, 1, 2: a standard stream (its host's))
            std::FILE* file = nullptr;      ///< (a file (the host's))
            int mode = IOFBF;
            std::string output;             ///< (a standard stream's buffered output)
            std::string input;              ///< (characters put back (ungetc), the last first)
            bool eof = false;
            bool error = false;
            bool temporary = false;
            std::string path;
        };

        std::map<address, std::unique_ptr<stream>>& streams()
        {
            static std::map<address, std::unique_ptr<stream>> sStreams;
            return sStreams;
        }

        // (the streams' lock: held by each call of a stream function (the program's threads use the streams at once))
        std::recursive_mutex& streams_mutex()
        {
            static std::recursive_mutex sMutex;
            return sMutex;
        }
        template <auto Function>
        struct locked;
        template <typename Result, typename... Parameters, Result (*Function)(Parameters...)>
        struct locked<Function>
        {
            static Result call(Parameters... aArguments)
            {
                std::lock_guard lock{ streams_mutex() };
                return Function(aArguments...);
            }
        };

        stream* stream_of(std::int64_t aFile)
        {
            auto const found = streams().find(static_cast<address>(aFile));
            return found != streams().end() ? found->second.get() : nullptr;
        }

        int flush(stream& s)
        {
            if (s.standard >= 0)
            {
                if (!s.output.empty())
                {
                    auto const written = std_write(s.standard, s.output.data(), s.output.size());
                    bool const ok = written == s.output.size();
                    s.output.clear();
                    std_flush(s.standard);
                    if (!ok)
                    {
                        s.error = true;
                        return EOF;
                    }
                }
                return 0;
            }
            if (s.file != nullptr)
                return std::fflush(s.file) == 0 ? 0 : EOF;
            return 0;
        }

        void flush_line_buffered()
        {
            // (input from a stream: the line buffered (and unbuffered) output streams flushed)
            for (auto& [cell, s] : streams())
                if (s->standard >= 0 && s->mode != IOFBF)
                    flush(*s);
            if (auto* out = stream_of(static_cast<std::int64_t>(language::c::layout::Streams + language::c::layout::StreamSize)))
                flush(*out);
        }

        std::size_t write(stream& s, char const* aData, std::size_t aCount)
        {
            if (s.standard >= 0)
            {
                if (s.mode == IONBF)
                {
                    flush(s);
                    auto const n = std_write(s.standard, aData, aCount);
                    std_flush(s.standard);
                    if (n != aCount)
                        s.error = true;
                    return n;
                }
                s.output.append(aData, aCount);
                if (s.output.size() >= 8192u || (s.mode == IOLBF && std::memchr(aData, '\n', aCount) != nullptr))
                    if (flush(s) != 0)
                        return 0;
                return aCount;
            }
            if (s.file == nullptr)
            {
                s.error = true;
                return 0;
            }
            auto const n = std::fwrite(aData, 1u, aCount, s.file);
            if (n != aCount)
                s.error = true;
            return n;
        }

        int get(stream& s)
        {
            if (!s.input.empty())
            {
                int const c = static_cast<unsigned char>(s.input.back());
                s.input.pop_back();
                return c;
            }
            if (s.standard >= 0)
            {
                flush_line_buffered();
                int const c = std_getc(s.standard);
                if (c == EOF)
                    s.eof = true;
                return c;
            }
            if (s.file == nullptr)
                return EOF;
            int const c = std::fgetc(s.file);
            if (c == EOF)
            {
                if (std::feof(s.file))
                    s.eof = true;
                else
                    s.error = true;
            }
            return c;
        }

        int unget(stream& s, int aCharacter)
        {
            if (aCharacter == EOF)
                return EOF;
            s.input.push_back(static_cast<char>(aCharacter));
            s.eof = false;
            return static_cast<unsigned char>(aCharacter);
        }

        address new_cell()
        {
            return allocate(language::c::layout::StreamSize);
        }

        // --- operations on files

        std::int32_t c_remove(std::int64_t aPath)
        {
            int const r = std::remove(str(static_cast<address>(aPath)));
            if (r != 0)
                errno_value() = errno;
            return r;
        }

        std::int32_t c_rename(std::int64_t aOld, std::int64_t aNew)
        {
            int const r = std::rename(str(static_cast<address>(aOld)), str(static_cast<address>(aNew)));
            if (r != 0)
                errno_value() = errno;
            return r;
        }

        std::int64_t c_fopen(std::int64_t aPath, std::int64_t aMode)
        {
            std::string mode = str(static_cast<address>(aMode));
            // (C11's exclusive mode 'x' and binary 'b' are the host's)
            auto* file = std::fopen(str(static_cast<address>(aPath)), mode.c_str());
            if (file == nullptr)
            {
                errno_value() = errno;
                return 0;
            }
            auto s = std::make_unique<stream>();
            s->cell = new_cell();
            s->file = file;
            s->path = str(static_cast<address>(aPath));
            auto const cell = s->cell;
            streams()[cell] = std::move(s);
            return static_cast<std::int64_t>(cell);
        }

        std::int64_t c_tmpfile()
        {
            auto* file = std::tmpfile();
            if (file == nullptr)
            {
                errno_value() = errno;
                return 0;
            }
            auto s = std::make_unique<stream>();
            s->cell = new_cell();
            s->file = file;
            s->temporary = true;
            auto const cell = s->cell;
            streams()[cell] = std::move(s);
            return static_cast<std::int64_t>(cell);
        }

        std::int64_t c_tmpnam(std::int64_t aBuffer)
        {
            static int sCount = 0;
            static address sBuffer = 0u;
            std::string name = "neos_tmp_" + std::to_string(++sCount) + "_" + std::to_string(std::rand());
            address target = static_cast<address>(aBuffer);
            if (target == 0u)
            {
                if (sBuffer == 0u)
                    sBuffer = allocate(64u);
                target = sBuffer;
            }
            std::memcpy(at<char>(target), name.c_str(), name.size() + 1u);
            return static_cast<std::int64_t>(target);
        }

        std::int32_t c_fclose(std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr)
                return EOF;
            int result = flush(*s);
            if (s->standard >= 0)
            {
                // (a standard stream: flushed, no longer usable)
                s->standard = -2;
                return result;
            }
            if (s->file != nullptr && std::fclose(s->file) != 0)
                result = EOF;
            auto const cell = s->cell;
            streams().erase(cell);
            deallocate(cell);
            return result;
        }

        std::int32_t c_fflush(std::int64_t aFile)
        {
            if (aFile == 0)
            {
                int result = 0;
                for (auto& [cell, s] : streams())
                    if (flush(*s) != 0)
                        result = EOF;
                return result;
            }
            auto* s = stream_of(aFile);
            return s != nullptr ? flush(*s) : EOF;
        }

        std::int64_t c_freopen(std::int64_t aPath, std::int64_t aMode, std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr)
                return 0;
            flush(*s);
            if (aPath == 0)
                return aFile;   // (a mode change: nothing to do)
            auto* file = std::fopen(str(static_cast<address>(aPath)), str(static_cast<address>(aMode)));
            if (file == nullptr)
            {
                errno_value() = errno;
                return 0;
            }
            if (s->file != nullptr)
                std::fclose(s->file);
            s->file = file;
            s->standard = -1;
            s->eof = s->error = false;
            s->input.clear();
            return aFile;
        }

        void c_setbuf(std::int64_t aFile, std::int64_t aBuffer)
        {
            if (auto* s = stream_of(aFile))
                s->mode = aBuffer == 0 ? IONBF : IOFBF;
        }

        std::int32_t c_setvbuf(std::int64_t aFile, std::int64_t, std::int32_t aMode, std::int64_t)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr || aMode < 0 || aMode > 2)
                return -1;
            s->mode = aMode;
            if (s->file != nullptr)
                std::setvbuf(s->file, nullptr, aMode == IONBF ? _IONBF : aMode == IOLBF ? _IOLBF : _IOFBF, BUFSIZ);
            return 0;
        }

        // --- formatted output

        std::int32_t c_vfprintf(std::int64_t aFile, std::int64_t aFormat, std::int64_t aArguments)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr)
                return -1;
            arguments args{ static_cast<address>(aArguments) };
            auto const text = format(str(static_cast<address>(aFormat)), args);
            if (write(*s, text.data(), text.size()) != text.size())
                return -1;
            return static_cast<std::int32_t>(text.size());
        }

        std::int32_t c_vprintf(std::int64_t aFormat, std::int64_t aArguments)
        {
            return c_vfprintf(static_cast<std::int64_t>(language::c::layout::Streams + language::c::layout::StreamSize), aFormat, aArguments);
        }

        std::int32_t c_vsnprintf(std::int64_t aBuffer, std::int64_t aSize, std::int64_t aFormat, std::int64_t aArguments)
        {
            arguments args{ static_cast<address>(aArguments) };
            auto const text = format(str(static_cast<address>(aFormat)), args);
            if (aSize > 0 && aBuffer != 0)
            {
                auto const n = std::min<std::size_t>(text.size(), static_cast<std::size_t>(aSize) - 1u);
                std::memcpy(at<char>(static_cast<address>(aBuffer)), text.data(), n);
                at<char>(static_cast<address>(aBuffer))[n] = '\0';
            }
            return static_cast<std::int32_t>(text.size());
        }

        std::int32_t c_vsprintf(std::int64_t aBuffer, std::int64_t aFormat, std::int64_t aArguments)
        {
            arguments args{ static_cast<address>(aArguments) };
            auto const text = format(str(static_cast<address>(aFormat)), args);
            std::memcpy(at<char>(static_cast<address>(aBuffer)), text.c_str(), text.size() + 1u);
            return static_cast<std::int32_t>(text.size());
        }

        // --- formatted input

        character_source source_of(stream& s)
        {
            character_source source;
            source.get = [&s]() { return get(s); };
            source.unget = [&s](int c) { unget(s, c); };
            return source;
        }

        std::int32_t c_vfscanf(std::int64_t aFile, std::int64_t aFormat, std::int64_t aArguments)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr)
                return EOF;
            arguments args{ static_cast<address>(aArguments) };
            auto source = source_of(*s);
            return scan(source, str(static_cast<address>(aFormat)), args);
        }

        std::int32_t c_vscanf(std::int64_t aFormat, std::int64_t aArguments)
        {
            return c_vfscanf(static_cast<std::int64_t>(language::c::layout::Streams), aFormat, aArguments);
        }

        std::int32_t c_vsscanf(std::int64_t aString, std::int64_t aFormat, std::int64_t aArguments)
        {
            char const* text = str(static_cast<address>(aString));
            std::size_t position = 0u;
            character_source source;
            source.get = [&]() { return text[position] == '\0' ? EOF : static_cast<unsigned char>(text[position++]); };
            source.unget = [&](int) { if (position > 0u) --position; };
            arguments args{ static_cast<address>(aArguments) };
            return scan(source, str(static_cast<address>(aFormat)), args);
        }

        // --- character input and output

        std::int32_t c_fgetc(std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            return s != nullptr ? get(*s) : EOF;
        }

        std::int64_t c_fgets(std::int64_t aBuffer, std::int32_t aSize, std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr || aSize <= 0)
                return 0;
            auto* buffer = at<char>(static_cast<address>(aBuffer));
            int n = 0;
            while (n < aSize - 1)
            {
                int const c = get(*s);
                if (c == EOF)
                    break;
                buffer[n++] = static_cast<char>(c);
                if (c == '\n')
                    break;
            }
            if (n == 0)
                return 0;
            buffer[n] = '\0';
            return aBuffer;
        }

        std::int32_t c_fputc(std::int32_t aCharacter, std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr)
                return EOF;
            char const c = static_cast<char>(aCharacter);
            return write(*s, &c, 1u) == 1u ? static_cast<unsigned char>(c) : EOF;
        }

        std::int32_t c_fputs(std::int64_t aString, std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr)
                return EOF;
            auto const* text = str(static_cast<address>(aString));
            auto const n = std::strlen(text);
            return write(*s, text, n) == n ? 0 : EOF;
        }

        std::int32_t c_getchar()
        {
            return c_fgetc(static_cast<std::int64_t>(language::c::layout::Streams));
        }

        std::int32_t c_putchar(std::int32_t aCharacter)
        {
            return c_fputc(aCharacter, static_cast<std::int64_t>(language::c::layout::Streams + language::c::layout::StreamSize));
        }

        std::int32_t c_puts(std::int64_t aString)
        {
            auto const out = static_cast<std::int64_t>(language::c::layout::Streams + language::c::layout::StreamSize);
            if (c_fputs(aString, out) == EOF)
                return EOF;
            return c_fputc('\n', out) == EOF ? EOF : 0;
        }

        std::int32_t c_ungetc(std::int32_t aCharacter, std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            return s != nullptr ? unget(*s, aCharacter) : EOF;
        }

        // --- direct input and output

        std::int64_t c_fread(std::int64_t aBuffer, std::int64_t aSize, std::int64_t aCount, std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr || aSize == 0 || aCount == 0)
                return 0;
            auto* buffer = at<char>(static_cast<address>(aBuffer));
            auto const total = static_cast<std::size_t>(aSize) * static_cast<std::size_t>(aCount);
            std::size_t n = 0u;
            while (n < total && !s->input.empty())
            {
                buffer[n++] = s->input.back();
                s->input.pop_back();
            }
            if (s->file != nullptr)
            {
                auto const r = std::fread(buffer + n, 1u, total - n, s->file);
                n += r;
                if (n < total)
                {
                    if (std::feof(s->file)) s->eof = true;
                    else s->error = true;
                }
            }
            else if (s->standard >= 0)
            {
                flush_line_buffered();
                n += std_read(s->standard, buffer + n, total - n);
                if (n < total)
                    s->eof = true;
            }
            return static_cast<std::int64_t>(n / static_cast<std::size_t>(aSize));
        }

        std::int64_t c_fwrite(std::int64_t aBuffer, std::int64_t aSize, std::int64_t aCount, std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr || aSize == 0 || aCount == 0)
                return 0;
            auto const total = static_cast<std::size_t>(aSize) * static_cast<std::size_t>(aCount);
            auto const n = write(*s, at<char>(static_cast<address>(aBuffer)), total);
            return static_cast<std::int64_t>(n / static_cast<std::size_t>(aSize));
        }

        // --- file positioning

        std::int32_t c_fseek(std::int64_t aFile, std::int64_t aOffset, std::int32_t aWhence)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr || s->file == nullptr)
                return -1;
            s->input.clear();
            s->eof = false;
#if defined(_WIN32)
            int const r = _fseeki64(s->file, aOffset, aWhence);
#else
            int const r = fseeko(s->file, static_cast<off_t>(aOffset), aWhence);
#endif
            if (r != 0)
                errno_value() = errno;
            return r == 0 ? 0 : -1;
        }

        std::int64_t c_ftell(std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr || s->file == nullptr)
            {
                errno_value() = ESPIPE;
                return -1;
            }
#if defined(_WIN32)
            auto const r = _ftelli64(s->file);
#else
            auto const r = static_cast<std::int64_t>(ftello(s->file));
#endif
            return r < 0 ? r : r - static_cast<std::int64_t>(s->input.size());
        }

        std::int32_t c_fgetpos(std::int64_t aFile, std::int64_t aPosition)
        {
            auto const p = c_ftell(aFile);
            if (p < 0)
                return -1;
            *at<std::int64_t>(static_cast<address>(aPosition)) = p;
            return 0;
        }

        std::int32_t c_fsetpos(std::int64_t aFile, std::int64_t aPosition)
        {
            return c_fseek(aFile, *at<std::int64_t>(static_cast<address>(aPosition)), SEEK_SET);
        }

        void c_rewind(std::int64_t aFile)
        {
            c_fseek(aFile, 0, SEEK_SET);
            if (auto* s = stream_of(aFile))
                s->error = false;
        }

        // --- errors

        void c_clearerr(std::int64_t aFile)
        {
            if (auto* s = stream_of(aFile))
            {
                s->eof = s->error = false;
                if (s->file != nullptr)
                    std::clearerr(s->file);
            }
        }

        std::int32_t c_feof(std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            return s != nullptr && s->eof ? 1 : 0;
        }

        std::int32_t c_ferror(std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            return s != nullptr && s->error ? 1 : 0;
        }

        void c_perror(std::int64_t aPrefix)
        {
            std::string text;
            if (aPrefix != 0 && *str(static_cast<address>(aPrefix)) != '\0')
                text = std::string{ str(static_cast<address>(aPrefix)) } + ": ";
            text += std::strerror(errno_value());
            text += '\n';
            if (auto* s = stream_of(static_cast<std::int64_t>(language::c::layout::Streams + 2u * language::c::layout::StreamSize)))
                write(*s, text.data(), text.size());
        }

        // --- POSIX's getline, getdelim, fileno

        std::int64_t c_getdelim(std::int64_t aLine, std::int64_t aSize, std::int32_t aDelimiter, std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr || aLine == 0 || aSize == 0)
            {
                errno_value() = EINVAL;
                return -1;
            }
            std::string text;
            int c;
            while ((c = get(*s)) != EOF)
            {
                text += static_cast<char>(c);
                if (c == aDelimiter)
                    break;
            }
            if (text.empty())
                return -1;
            auto& buffer = *at<std::uint64_t>(static_cast<address>(aLine));
            auto& size = *at<std::uint64_t>(static_cast<address>(aSize));
            if (buffer == 0u || size < text.size() + 1u)
            {
                auto const block = reallocate(buffer, text.size() + 1u);
                if (block == 0u)
                    return -1;
                buffer = block;
                size = text.size() + 1u;
            }
            std::memcpy(at<char>(buffer), text.c_str(), text.size() + 1u);
            return static_cast<std::int64_t>(text.size());
        }

        std::int64_t c_getline(std::int64_t aLine, std::int64_t aSize, std::int64_t aFile)
        {
            return c_getdelim(aLine, aSize, '\n', aFile);
        }

        std::int32_t c_fileno(std::int64_t aFile)
        {
            auto* s = stream_of(aFile);
            if (s == nullptr)
                return -1;
            if (s->standard >= 0)
                return s->standard;
#if defined(_WIN32)
            return _fileno(s->file);
#else
            return ::fileno(s->file);
#endif
        }

        function_table const sStdioFunctions
        {
            library("remove", NEOS_RT_FUNCTION(locked<c_remove>::call)), library("rename", NEOS_RT_FUNCTION(locked<c_rename>::call)),
            library("tmpfile", NEOS_RT_FUNCTION(locked<c_tmpfile>::call)), library("tmpnam", NEOS_RT_FUNCTION(locked<c_tmpnam>::call)),
            library("fclose", NEOS_RT_FUNCTION(locked<c_fclose>::call)), library("fflush", NEOS_RT_FUNCTION(locked<c_fflush>::call)),
            library("fopen", NEOS_RT_FUNCTION(locked<c_fopen>::call)), library("freopen", NEOS_RT_FUNCTION(locked<c_freopen>::call)),
            library("setbuf", NEOS_RT_FUNCTION(locked<c_setbuf>::call)), library("setvbuf", NEOS_RT_FUNCTION(locked<c_setvbuf>::call)),
            library("fprintf", NEOS_RT_FUNCTION(locked<c_vfprintf>::call)), library("fscanf", NEOS_RT_FUNCTION(locked<c_vfscanf>::call)),
            library("printf", NEOS_RT_FUNCTION(locked<c_vprintf>::call)), library("scanf", NEOS_RT_FUNCTION(locked<c_vscanf>::call)),
            library("snprintf", NEOS_RT_FUNCTION(locked<c_vsnprintf>::call)), library("sprintf", NEOS_RT_FUNCTION(locked<c_vsprintf>::call)),
            library("sscanf", NEOS_RT_FUNCTION(locked<c_vsscanf>::call)), library("vfprintf", NEOS_RT_FUNCTION(locked<c_vfprintf>::call)),
            library("vfscanf", NEOS_RT_FUNCTION(locked<c_vfscanf>::call)), library("vprintf", NEOS_RT_FUNCTION(locked<c_vprintf>::call)),
            library("vscanf", NEOS_RT_FUNCTION(locked<c_vscanf>::call)), library("vsnprintf", NEOS_RT_FUNCTION(locked<c_vsnprintf>::call)),
            library("vsprintf", NEOS_RT_FUNCTION(locked<c_vsprintf>::call)), library("vsscanf", NEOS_RT_FUNCTION(locked<c_vsscanf>::call)),
            library("fgetc", NEOS_RT_FUNCTION(locked<c_fgetc>::call)), library("fgets", NEOS_RT_FUNCTION(locked<c_fgets>::call)),
            library("fputc", NEOS_RT_FUNCTION(locked<c_fputc>::call)), library("fputs", NEOS_RT_FUNCTION(locked<c_fputs>::call)),
            library("getc", NEOS_RT_FUNCTION(locked<c_fgetc>::call)), library("getchar", NEOS_RT_FUNCTION(locked<c_getchar>::call)),
            library("putc", NEOS_RT_FUNCTION(locked<c_fputc>::call)), library("putchar", NEOS_RT_FUNCTION(locked<c_putchar>::call)),
            library("puts", NEOS_RT_FUNCTION(locked<c_puts>::call)), library("ungetc", NEOS_RT_FUNCTION(locked<c_ungetc>::call)),
            library("fread", NEOS_RT_FUNCTION(locked<c_fread>::call)), library("fwrite", NEOS_RT_FUNCTION(locked<c_fwrite>::call)),
            library("fgetpos", NEOS_RT_FUNCTION(locked<c_fgetpos>::call)), library("fseek", NEOS_RT_FUNCTION(locked<c_fseek>::call)),
            library("fsetpos", NEOS_RT_FUNCTION(locked<c_fsetpos>::call)), library("ftell", NEOS_RT_FUNCTION(locked<c_ftell>::call)),
            library("rewind", NEOS_RT_FUNCTION(locked<c_rewind>::call)), library("clearerr", NEOS_RT_FUNCTION(locked<c_clearerr>::call)),
            library("feof", NEOS_RT_FUNCTION(locked<c_feof>::call)), library("ferror", NEOS_RT_FUNCTION(locked<c_ferror>::call)),
            library("perror", NEOS_RT_FUNCTION(locked<c_perror>::call)), library("getline", NEOS_RT_FUNCTION(locked<c_getline>::call)),
            library("getdelim", NEOS_RT_FUNCTION(locked<c_getdelim>::call)), library("fileno", NEOS_RT_FUNCTION(locked<c_fileno>::call))
        };
    }

    void streams_init()
    {
        std::lock_guard lock{ streams_mutex() };
        // (the standard streams' FILE objects (fixed addresses): their streams made again for each program)
        for (int i = 0; i < 3; ++i)
        {
            auto const cell = language::c::layout::Streams + static_cast<address>(i) * language::c::layout::StreamSize;
            auto s = std::make_unique<stream>();
            s->cell = cell;
            s->standard = i;
            s->mode = i == 2 ? IONBF : i == 1 ? (NEOS_ISATTY(1) ? IOLBF : IOFBF) : IOLBF;
            if (host() != nullptr && host()->version >= 9u && i == 1 && !NEOS_ISATTY(1))
                s->mode = IOFBF;
            streams()[cell] = std::move(s);
        }
    }

    void streams_end()
    {
        std::lock_guard lock{ streams_mutex() };
        // (the streams flushed and the files closed)
        for (auto& [cell, s] : streams())
        {
            flush(*s);
            if (s->file != nullptr)
                std::fclose(s->file);
            s->file = nullptr;
        }
        streams().clear();
    }

    // (for the wide functions (lib_wchar.cpp): a character to a stream)
    std::size_t stream_write(std::int64_t aFile, char const* aData, std::size_t aCount)
    {
        std::lock_guard lock{ streams_mutex() };
        auto* s = stream_of(aFile);
        return s != nullptr ? write(*s, aData, aCount) : 0u;
    }

    int stream_get(std::int64_t aFile)
    {
        std::lock_guard lock{ streams_mutex() };
        auto* s = stream_of(aFile);
        return s != nullptr ? get(*s) : EOF;
    }

    void stream_unget(std::int64_t aFile, int aCharacter)
    {
        std::lock_guard lock{ streams_mutex() };
        if (auto* s = stream_of(aFile))
            unget(*s, aCharacter);
    }
}
