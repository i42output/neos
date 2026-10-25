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

// The io library: files are userdata (their metatable: FILE*); the standard streams are the host's (see neos_rt_host::std_write ...),
// so that programs that run at the same time (e.g. sessions of an IDE) each have their own. Opening a file for writing, and popen,
// are refused in restricted mode.

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    void check_unrestricted(char const* aFunction);
    void file_result(native_call& c, bool aOk, std::string const& aName);

    namespace
    {
        struct lua_file
        {
            std::FILE* file = nullptr;
            int stream = -1;            ///< (a standard stream (0, 1, 2): the host's)
            bool closed = false;
        };

        static_values tIo;    ///< (0: the files' metatable, 1: the default input, 2: the default output, 3, 4, 5: stdin, stdout, stderr)

        void destroy_file(void* aFile)
        {
            auto* f = static_cast<lua_file*>(aFile);
            if (f->file != nullptr && !f->closed)
                std::fclose(f->file);
            delete f;
        }

        value make_file(std::FILE* aFile, int aStream)
        {
            auto& u = the_heap().make<userdata_cell>();
            u.metatable = tIo[0];
            u.data = new lua_file{ aFile, aStream, false };
            u.destroy = &destroy_file;
            return TagOther | u.handle;
        }

        lua_file* file_of(value aValue)
        {
            if (!is_type(aValue, cell_type::Userdata))
                return nullptr;
            auto& u = cell_of<userdata_cell>(aValue);
            if (u.destroy != &destroy_file)
                return nullptr;
            return static_cast<lua_file*>(u.data);
        }

        lua_file& check_file(native_call& c, std::size_t aIndex)
        {
            auto* f = file_of(c.arg(aIndex));
            if (f == nullptr)
                type_error(c, aIndex, "FILE*");
            if (f->closed)
                runtime_error("attempt to use a closed file");
            return *f;
        }

        // --- the stream operations (a standard stream's are the host's)

        int file_getc(lua_file& f)
        {
            if (f.stream >= 0)
            {
                auto const* h = host();
                if (h != nullptr && h->version >= 9u && h->std_getc != nullptr)
                    return h->std_getc(f.stream);
                return std::fgetc(stdin);
            }
            return std::fgetc(f.file);
        }

        void file_ungetc(lua_file& f, int aCharacter)
        {
            if (aCharacter == EOF)
                return;
            if (f.stream >= 0)
            {
                auto const* h = host();
                if (h != nullptr && h->version >= 9u && h->std_ungetc != nullptr)
                    h->std_ungetc(aCharacter, f.stream);
                else
                    std::ungetc(aCharacter, stdin);
                return;
            }
            std::ungetc(aCharacter, f.file);
        }

        bool file_write(lua_file& f, std::string_view aText)
        {
            if (f.stream >= 0)
            {
                write_standard(f.stream, aText);
                return true;
            }
            return std::fwrite(aText.data(), 1u, aText.size(), f.file) == aText.size();
        }

        int file_flush(lua_file& f)
        {
            if (f.stream >= 0)
            {
                flush_standard(f.stream);
                return 0;
            }
            return std::fflush(f.file);
        }

        // --- reading

        bool read_line(lua_file& f, std::string& aResult, bool aKeepNewline)
        {
            int ch;
            bool any = false;
            while ((ch = file_getc(f)) != EOF)
            {
                any = true;
                if (ch == '\n')
                {
                    if (aKeepNewline)
                        aResult.push_back('\n');
                    return true;
                }
                aResult.push_back(static_cast<char>(ch));
            }
            return any;
        }

        std::string read_all(lua_file& f)
        {
            std::string result;
            if (f.stream < 0)
            {
                char buffer[4096];
                std::size_t n;
                while ((n = std::fread(buffer, 1u, sizeof(buffer), f.file)) > 0u)
                    result.append(buffer, n);
                return result;
            }
            int ch;
            while ((ch = file_getc(f)) != EOF)
                result.push_back(static_cast<char>(ch));
            return result;
        }

        bool read_chars(lua_file& f, std::size_t aCount, std::string& aResult)
        {
            if (f.stream < 0)
            {
                aResult.resize(aCount);
                auto const n = std::fread(aResult.data(), 1u, aCount, f.file);
                aResult.resize(n);
                return n > 0u;
            }
            int ch;
            while (aResult.size() < aCount && (ch = file_getc(f)) != EOF)
                aResult.push_back(static_cast<char>(ch));
            return !aResult.empty();
        }

        bool at_end(lua_file& f)
        {
            auto const ch = file_getc(f);
            file_ungetc(f, ch);
            return ch == EOF;
        }

        value read_number(lua_file& f)
        {
            // (a numeral: at most 200 characters of one, after white space)
            int ch;
            do
                ch = file_getc(f);
            while (ch != EOF && std::isspace(ch));
            std::string text;
            auto const accept = [&](char const* aSet) -> bool
            {
                if (ch != EOF && std::strchr(aSet, ch) != nullptr && text.size() < 200u)
                {
                    text.push_back(static_cast<char>(ch));
                    ch = file_getc(f);
                    return true;
                }
                return false;
            };
            accept("-+");
            bool hex = false;
            int digits = 0;
            if (accept("0"))
            {
                ++digits;
                hex = accept("xX");
                if (hex)
                    digits = 0;
            }
            auto const digit_set = hex ? "0123456789abcdefABCDEF" : "0123456789";
            while (accept(digit_set))
                ++digits;
            if (accept("."))
                while (accept(digit_set))
                    ++digits;
            if (digits > 0 && accept(hex ? "pP" : "eE"))
            {
                accept("-+");
                while (accept("0123456789"))
                    ;
            }
            file_ungetc(f, ch);
            value result;
            if (string_to_number(text, result))
                return result;
            return Nil;
        }

        void read_formats(native_call& c, lua_file& f, std::size_t aFirst)
        {
            if (c.count() <= aFirst)
            {
                std::string line;
                if (read_line(f, line, false))
                    c.ret(make_string(std::move(line)));
                else
                    c.ret(Nil);
                return;
            }
            for (auto i = aFirst; i < c.count(); ++i)
            {
                auto const format = c.arg(i);
                if (is_integer(format) || is_float(format))
                {
                    auto const n = static_cast<std::size_t>(check_integer(c, i));
                    if (n == 0u)
                    {
                        if (at_end(f))
                        {
                            c.ret(Nil);
                            return;
                        }
                        c.ret(make_string(std::string_view{}));
                        continue;
                    }
                    std::string chars;
                    if (!read_chars(f, n, chars))
                    {
                        c.ret(Nil);
                        return;
                    }
                    c.ret(make_string(std::move(chars)));
                    continue;
                }
                auto spec = std::string{ check_string(c, i) };
                if (!spec.empty() && spec[0] == '*')
                    spec.erase(0u, 1u);
                switch (spec.empty() ? '\0' : spec[0])
                {
                case 'n':
                    {
                        auto const n = read_number(f);
                        c.ret(n);
                        if (is_nil(n))
                            return;
                    }
                    break;
                case 'l':
                case 'L':
                    {
                        std::string line;
                        if (!read_line(f, line, spec[0] == 'L'))
                        {
                            c.ret(Nil);
                            return;
                        }
                        c.ret(make_string(std::move(line)));
                    }
                    break;
                case 'a':
                    c.ret(make_string(read_all(f)));
                    break;
                default:
                    argument_error(c, i, "invalid format");
                }
            }
        }

        void write_values(native_call& c, lua_file& f, std::size_t aFirst, value aFile)
        {
            bool ok = true;
            for (auto i = aFirst; i < c.count(); ++i)
            {
                auto const v = c.arg(i);
                if (is_string(v))
                    ok = file_write(f, str_view(v)) && ok;
                else if (is_number(v))
                {
                    // (a float as "%.14g": without ".0")
                    char buffer[64];
                    if (is_integer(v))
                        ok = file_write(f, integer_text(integer_of(v))) && ok;
                    else
                    {
                        std::snprintf(buffer, sizeof(buffer), "%.14g", float_of(v));
                        ok = file_write(f, buffer) && ok;
                    }
                }
                else
                    type_error(c, i, "string");
            }
            if (ok)
                c.ret(aFile);
            else
                file_result(c, false, "");
        }

        // --- the default files

        value default_file(std::size_t aWhich)
        {
            return tIo[aWhich];
        }

        lua_file& default_open(std::size_t aWhich, char const* aName)
        {
            auto* f = file_of(default_file(aWhich));
            if (f == nullptr || f->closed)
                runtime_error(std::string{ "default " } + aName + " file is closed");
            return *f;
        }

        // --- the io functions

        value open_file(std::string const& aPath, std::string const& aMode, std::string& aError)
        {
            static char const* const modes[] = { "r", "w", "a", "r+", "w+", "a+", "rb", "wb", "ab", "r+b", "w+b", "a+b" };
            bool valid = false;
            for (auto const* m : modes)
                if (aMode == m)
                    valid = true;
            if (!valid)
            {
                aError = "invalid mode";
                return Empty;
            }
            if (aMode[0] != 'r' || aMode.find('+') != std::string::npos)
                check_unrestricted("io.open");
            auto* file = std::fopen(aPath.c_str(), aMode.c_str());
            if (file == nullptr)
            {
                aError = aPath + ": " + std::strerror(errno);
                return Nil;
            }
            return make_file(file, -1);
        }

        void io_open(native_call& c)
        {
            auto const path = std::string{ check_string(c, 0u) };
            auto const mode = std::string{ optional_string(c, 1u, "r") };
            std::string error;
            auto const f = open_file(path, mode, error);
            if (f == Empty)
                argument_error(c, 1u, error);
            if (is_nil(f))
            {
                auto const code = errno;
                c.ret(Nil);
                c.ret(make_string(error));
                c.ret(make_integer(code));
                return;
            }
            c.ret(f);
        }

        void close_file(native_call& c, lua_file& f)
        {
            if (f.stream >= 0)
            {
                c.ret(Nil);
                c.ret(make_string("cannot close standard file"));
                return;
            }
            bool const ok = std::fclose(f.file) == 0;
            f.closed = true;
            f.file = nullptr;
            file_result(c, ok, "");
        }

        void io_close(native_call& c)
        {
            if (c.none(0u) || is_nil(c.arg(0u)))
            {
                close_file(c, default_open(2u, "output"));
                return;
            }
            close_file(c, check_file(c, 0u));
        }

        void io_write(native_call& c)
        {
            write_values(c, default_open(2u, "output"), 0u, default_file(2u));
        }

        void io_read(native_call& c)
        {
            read_formats(c, default_open(1u, "input"), 0u);
        }

        void select_default(native_call& c, std::size_t aWhich, char const* aMode)
        {
            if (!c.none(0u) && !is_nil(c.arg(0u)))
            {
                auto const v = c.arg(0u);
                if (is_string(v) || is_number(v))
                {
                    auto const path = std::string{ check_string(c, 0u) };
                    std::string error;
                    auto const f = open_file(path, aMode, error);
                    if (is_nil(f) || f == Empty)
                        runtime_error("cannot open file '" + path + "' (" + std::strerror(errno) + ")");
                    tIo[aWhich] = f;
                }
                else
                {
                    check_file(c, 0u);
                    tIo[aWhich] = v;
                }
            }
            c.ret(tIo[aWhich]);
        }

        void io_input(native_call& c)
        {
            select_default(c, 1u, "r");
        }

        void io_output(native_call& c)
        {
            select_default(c, 2u, "w");
        }

        void lines_iterator(native_call& c)
        {
            // (upvalues: the file, whether to close it at the end, and the formats)
            auto const file = c.self.upvalues[0];
            auto* f = file_of(file);
            if (f == nullptr || f->closed)
                runtime_error("file is already closed");
            value_list arguments{ file };
            for (std::size_t i = 2u; i < c.self.upvalues.size(); ++i)
                arguments.push_back(c.self.upvalues[i]);
            native_call reading{ arguments.span(), c.results, c.self, c.direct };
            auto const before = c.results.size();
            read_formats(reading, *f, 1u);
            if (c.results.size() == before || is_nil(c.results[before]))
            {
                if (truthy(c.self.upvalues[1]) && f->stream < 0)
                {
                    std::fclose(f->file);
                    f->file = nullptr;
                    f->closed = true;
                }
            }
        }

        value make_lines(native_call& c, value aFile, bool aClose, std::size_t aFirstFormat)
        {
            value_list upvalues{ aFile, boolean(aClose) };
            for (auto i = aFirstFormat; i < c.count(); ++i)
                upvalues.push_back(c.args[i]);
            auto& f = the_heap().make<function_cell>();
            f.native = true;
            f.function = &lines_iterator;
            f.name = "lines";
            f.qualified = "io.lines";
            f.upvalues.assign(upvalues.begin(), upvalues.end());
            return TagFunction | f.handle;
        }

        void io_lines(native_call& c)
        {
            if (c.none(0u) || is_nil(c.arg(0u)))
            {
                default_open(1u, "input");
                c.ret(make_lines(c, default_file(1u), false, 1u));
                return;
            }
            auto const path = std::string{ check_string(c, 0u) };
            std::string error;
            auto const f = open_file(path, "r", error);
            if (is_nil(f) || f == Empty)
                runtime_error(error);
            value_list keep{ f };
            c.ret(make_lines(c, f, true, 1u));
            c.ret(Nil);
            c.ret(Nil);
            c.ret(f);
        }

        void io_type(native_call& c)
        {
            check_any(c, 0u);
            auto* f = file_of(c.arg(0u));
            if (f == nullptr)
                c.ret(Nil);
            else
                c.ret(intern(f->closed ? "closed file" : "file"));
        }

        void io_tmpfile(native_call& c)
        {
            check_unrestricted("io.tmpfile");
            auto* file = std::tmpfile();
            if (file == nullptr)
            {
                file_result(c, false, "");
                return;
            }
            c.ret(make_file(file, -1));
        }

        void io_popen(native_call& c)
        {
            check_unrestricted("io.popen");
            runtime_error("'popen' not supported");
        }

        // --- the files' methods

        void file_close(native_call& c)
        {
            close_file(c, check_file(c, 0u));
        }

        void file_read(native_call& c)
        {
            read_formats(c, check_file(c, 0u), 1u);
        }

        void file_write_method(native_call& c)
        {
            write_values(c, check_file(c, 0u), 1u, c.arg(0u));
        }

        void file_lines(native_call& c)
        {
            check_file(c, 0u);
            c.ret(make_lines(c, c.arg(0u), false, 1u));
        }

        void file_flush_method(native_call& c)
        {
            auto& f = check_file(c, 0u);
            if (file_flush(f) == 0)
                c.ret(c.arg(0u));
            else
                file_result(c, false, "");
        }

        void file_seek(native_call& c)
        {
            auto& f = check_file(c, 0u);
            auto const whence = std::string{ optional_string(c, 1u, "cur") };
            auto const offset = optional_integer(c, 2u, 0);
            int mode;
            if (whence == "set")
                mode = SEEK_SET;
            else if (whence == "cur")
                mode = SEEK_CUR;
            else if (whence == "end")
                mode = SEEK_END;
            else
                argument_error(c, 1u, "invalid option '" + whence + "'");
            if (f.stream >= 0)
            {
                file_result(c, false, "");
                return;
            }
            if (std::fseek(f.file, static_cast<long>(offset), mode) != 0)
            {
                file_result(c, false, "");
                return;
            }
            c.ret(make_integer(static_cast<std::int64_t>(std::ftell(f.file))));
        }

        void file_setvbuf(native_call& c)
        {
            auto& f = check_file(c, 0u);
            auto const mode = std::string{ check_string(c, 1u) };
            if (f.stream >= 0)
            {
                c.ret(True);
                return;
            }
            int m = mode == "no" ? _IONBF : mode == "full" ? _IOFBF : mode == "line" ? _IOLBF : -1;
            if (m < 0)
                argument_error(c, 1u, "invalid option '" + mode + "'");
            file_result(c, std::setvbuf(f.file, nullptr, m, static_cast<std::size_t>(optional_integer(c, 2u, BUFSIZ))) == 0, "");
        }

        void file_tostring(native_call& c)
        {
            auto* f = file_of(c.arg(0u));
            if (f == nullptr)
            {
                c.ret(make_string("file"));
                return;
            }
            if (f->closed)
                c.ret(make_string("file (closed)"));
            else
            {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "file (%p)", static_cast<void*>(f));
                c.ret(make_string(buffer));
            }
        }

        void file_gc(native_call& c)
        {
            auto* f = file_of(c.arg(0u));
            if (f != nullptr && !f->closed && f->stream < 0 && f->file != nullptr)
            {
                std::fclose(f->file);
                f->file = nullptr;
                f->closed = true;
            }
        }
    }

    void open_io()
    {
        auto const t = library_table("io");
        value_list keep{ t };
        register_library(t, "io", {
            { "open", &io_open },
            { "close", &io_close },
            { "write", &io_write },
            { "read", &io_read },
            { "input", &io_input },
            { "output", &io_output },
            { "lines", &io_lines },
            { "type", &io_type },
            { "tmpfile", &io_tmpfile },
            { "popen", &io_popen } });
        // (the files' metatable)
        auto const mt = make_table();
        keep.push_back(mt);
        auto const methods = make_table();
        keep.push_back(methods);
        register_library(methods, "file", {
            { "close", &file_close },
            { "read", &file_read },
            { "write", &file_write_method },
            { "lines", &file_lines },
            { "flush", &file_flush_method },
            { "seek", &file_seek },
            { "setvbuf", &file_setvbuf } });
        raw_set(mt, name(NameIndex), methods);
        raw_set(mt, name(NameName), intern("FILE*"));
        register_library(mt, "file", { { "__tostring", &file_tostring }, { "__gc", &file_gc }, { "__close", &file_gc } });
        tIo.clear();
        tIo.push_back(mt);
        tIo.push_back(Nil);
        tIo.push_back(Nil);
        for (int stream = 0; stream < 3; ++stream)
            tIo.push_back(make_file(nullptr, stream));
        tIo[1] = tIo[3];
        tIo[2] = tIo[4];
        raw_set(t, intern("stdin"), tIo[3]);
        raw_set(t, intern("stdout"), tIo[4]);
        raw_set(t, intern("stderr"), tIo[5]);
    }
}
