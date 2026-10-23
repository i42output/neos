/*
  gzip.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/gzip.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module gzip (CPython 3.13's Lib/gzip.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "gzip_docs.hpp"

namespace neos::python_runtime::detail::gzip_module
{
    namespace
    {
        using pyclass::S;
        using pyclass::I;
        using pyclass::F;
        using pyclass::B;
        using pyclass::Mod;
        using pyclass::none;
        using pyclass::boolean;
        using pyclass::M;
        using pyclass::has_attr;
        using pyclass::get_attr;
        using pyclass::callable;
        using pyclass::module_writer;
        using pyclass::namespace_type;
        using pyclass::try_except;
        using pyclass::try_finally;
        using pyclass::with_;
        using pyclass::unpack2;
        using pyclass::format;
        using pyclass::handler_scope;
        using pyclass::matches;
        using pyclass::iterate;
        using pyclass::unpack;
        using pyclass::call_ex;
        using pyclass::dict_of;
        using pyclass::set_of;
        using pyclass::inplace;
        using pyclass::del_attr;
        using pyclass::del_item;
        using pyclass::formatted;
        using pyclass::yield_from;
        using pyclass::truth_of;
        using pyclass::module_of;
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "gzip.name"))
            bool made = false;
        };
        state& the_state()
        {
            static state* sState = new state{};
            return *sState;
        }

        // (its entry: the object, as it is defined (or assigned) now)
        py const& object_entry(char const* aQualifiedName)
        {
            auto& s = the_state();
            auto const o = s.objects.find(aQualifiedName);
            if (o == s.objects.end())
                py_raise("SystemError", std::string{ "gzip: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"gzip.name">()))
        template <pyclass::fixed_string Name>
        py const& G() { static py const* const sEntry = &object_entry(Name.value); return *sEntry; }
        // (an object made: one of the module's attributes)
        void define(char const* aModule, char const* aName, py const& aValue)
        {
            auto& s = the_state();
            s.objects[std::string{ aModule } + "." + aName] = aValue;
            for (auto& a : s.attributes)
                if (a.first == aName)
                {
                    a.second = aValue;
                    return;
                }
            s.attributes.emplace_back(aName, aValue);
        }
        // (del name: no longer one of the module's attributes (its object remains, for the module's functions))
        [[maybe_unused]] void undefine(char const* aModule, char const* aName)
        {
            auto& s = the_state();
            (void)aModule;
            std::erase_if(s.attributes, [&](auto const& a) { return a.first == aName; });
        }
        // (a module's global assigned (global name; name = value): its object, and the module's attribute (if it has been imported))
        void set_global(char const* aModule, char const* aName, py const& aValue)
        {
            define(aModule, aName, aValue);
            if (modules_imported(aModule))
                py{ fn_import(aModule) }.set_attr(aName, aValue);
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "gzip", gzip_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- gzip (Lib/gzip.py)

        void make_gzip()
        {
            static module_writer const& w = writer("gzip");
            // based on Andrew Kuchling's minigzip.py distributed with the zlib module
            define("gzip", "__all__", py_list({ S<"BadGzipFile">(), S<"GzipFile">(), S<"open">(), S<"compress">(), S<"decompress">() }));
            {
                auto const items1 = unpack(py_tuple({ I<1>(), I<2>(), I<4>(), I<8>(), I<16>() }), 5);
                define("gzip", "FTEXT", items1[0]);
                define("gzip", "FHCRC", items1[1]);
                define("gzip", "FEXTRA", items1[2]);
                define("gzip", "FNAME", items1[3]);
                define("gzip", "FCOMMENT", items1[4]);
            }
            define("gzip", "READ", S<"rb">());
            define("gzip", "WRITE", S<"wb">());
            define("gzip", "_COMPRESS_LEVEL_FAST", I<1>());
            define("gzip", "_COMPRESS_LEVEL_TRADEOFF", I<6>());
            define("gzip", "_COMPRESS_LEVEL_BEST", I<9>());
            define("gzip", "READ_BUFFER_SIZE", (I<128>()) * (I<1024>()));
            define("gzip", "_WRITE_BUFFER_SIZE", (I<4>()) * (Mod<"io">().attr("DEFAULT_BUFFER_SIZE")));
            define("gzip", "open", w.function("open", "open", { py_positional("filename"), py_positional("mode", S<"rb">()), py_positional("compresslevel", G<"gzip._COMPRESS_LEVEL_BEST">()), py_positional("encoding", none()), py_positional("errors", none()), py_positional("newline", none()) }, [](arguments a) -> py
                {
                    py filename = a[0];
                    py mode = a[1];
                    py compresslevel = a[2];
                    py encoding = a[3];
                    py errors = a[4];
                    py newline = a[5];
                    py binary_file;
                    py gz_mode;
                    if (mode.contains((S<"t">())))
                    {
                        if (mode.contains((S<"b">())))
                        {
                            py_raise((B<"ValueError">())((S<"Invalid mode: %r">()) % py_tuple({ mode })));
                        }
                    }
                    else
                    {
                        if (!encoding.is_none())
                        {
                            py_raise((B<"ValueError">())(S<"Argument 'encoding' not supported in binary mode">()));
                        }
                        if (!errors.is_none())
                        {
                            py_raise((B<"ValueError">())(S<"Argument 'errors' not supported in binary mode">()));
                        }
                        if (!newline.is_none())
                        {
                            py_raise((B<"ValueError">())(S<"Argument 'newline' not supported in binary mode">()));
                        }
                    }
                    gz_mode = mode.method("replace", { S<"t">(), S<"">() });
                    if (isinstance(filename, py_tuple({ B<"str">(), B<"bytes">(), Mod<"os">().attr("PathLike") })))
                    {
                        binary_file = (G<"gzip.GzipFile">())(filename, gz_mode, compresslevel);
                    }
                    else if ((has_attr(filename, "read") || has_attr(filename, "write")))
                    {
                        binary_file = (G<"gzip.GzipFile">())(none(), gz_mode, compresslevel, filename);
                    }
                    else
                    {
                        py_raise((B<"TypeError">())(S<"filename must be a str or bytes object, or a file">()));
                    }
                    if (mode.contains((S<"t">())))
                    {
                        encoding = (Mod<"io">().attr("text_encoding"))(encoding);
                        return (Mod<"io">().attr("TextIOWrapper"))(binary_file, encoding, errors, newline);
                    }
                    else
                    {
                        return binary_file;
                    }
                    return none();
                }));

            define("gzip", "write32u", w.function("write32u", "write32u", { py_positional("output"), py_positional("value") }, [](arguments a) -> py
                {
                    py output = a[0];
                    py value = a[1];
                    output.method("write", { (Mod<"struct">().attr("pack"))(S<"<L">(), value) });
                    return none();
                }));

            define("gzip", "_PaddedFile", w.make_class("_PaddedFile", {  }, {
                { "__init__", w.method("_PaddedFile", "__init__", { py_positional("f"), py_positional("prepend", py{ make_bytes(std::string{ "", 0 }) }) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        py prepend = a[2];
                        self.set_attr("_buffer", prepend);
                        self.set_attr("_length", py{ prepend.size() });
                        self.set_attr("file", f);
                        self.set_attr("_read", I<0>());
                        return none();
                    }) },
                { "read", w.method("_PaddedFile", "read", { py_positional("size") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py read;
                        if (self.attr("_read").is_none())
                        {
                            return self.attr("file").method("read", { size });
                        }
                        if ((self.attr("_read") + size) <= self.attr("_length"))
                        {
                            read = self.attr("_read");
                            {
                                py const target2 = self;
                                target2.set_attr("_read", inplace(NEOS_PY_ADD, target2.attr("_read"), size));
                            }
                            return self.attr("_buffer")[B<"slice">()(read, self.attr("_read"), none())];
                        }
                        else
                        {
                            read = self.attr("_read");
                            self.set_attr("_read", none());
                            return self.attr("_buffer")[B<"slice">()(read, none(), none())] + self.attr("file").method("read", { (size - self.attr("_length")) + read });
                        }
                        return none();
                    }) },
                { "prepend", w.method("_PaddedFile", "prepend", { py_positional("prepend", py{ make_bytes(std::string{ "", 0 }) }) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prepend = a[1];
                        if (self.attr("_read").is_none())
                        {
                            self.set_attr("_buffer", prepend);
                        }
                        else
                        {
                            {
                                py const target3 = self;
                                target3.set_attr("_read", inplace(NEOS_PY_SUBTRACT, target3.attr("_read"), py{ prepend.size() }));
                            }
                            return none();
                        }
                        self.set_attr("_length", py{ self.attr("_buffer").size() });
                        self.set_attr("_read", I<0>());
                        return none();
                    }) },
                { "seek", w.method("_PaddedFile", "seek", { py_positional("off") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py off = a[1];
                        self.set_attr("_read", none());
                        self.set_attr("_buffer", none());
                        return self.attr("file").method("seek", { off });
                    }) },
                { "seekable", w.method("_PaddedFile", "seekable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) }, }));

            define("gzip", "BadGzipFile", w.make_class("BadGzipFile", { B<"OSError">() }, {
 }));

            define("gzip", "_WriteBufferStream", w.make_class("_WriteBufferStream", { Mod<"io">().attr("RawIOBase") }, {
                { "__init__", w.method("_WriteBufferStream", "__init__", { py_positional("gzip_file") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py gzip_file = a[1];
                        self.set_attr("gzip_file", (Mod<"weakref">().attr("ref"))(gzip_file));
                        return none();
                    }) },
                { "write", w.method("_WriteBufferStream", "write", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py gzip_file;
                        gzip_file = self.method("gzip_file");
                        if (gzip_file.is_none())
                        {
                            py_raise((B<"RuntimeError">())(S<"lost gzip_file">()));
                        }
                        return gzip_file.method("_write_raw", { data });
                    }) },
                { "seekable", w.method("_WriteBufferStream", "seekable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(false);
                    }) },
                { "writable", w.method("_WriteBufferStream", "writable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) }, }));

                // Overridden with internal file object to be closed, if only a filename
                // is passed in
            define("gzip", "GzipFile", w.make_class("GzipFile", { Mod<"_compression">().attr("BaseStream") }, {
                { "myfileobj", none() },
                { "__init__", w.method("GzipFile", "__init__", { py_positional("filename", none()), py_positional("mode", none()), py_positional("compresslevel", G<"gzip._COMPRESS_LEVEL_BEST">()), py_positional("fileobj", none()), py_positional("mtime", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py mode = a[2];
                        py compresslevel = a[3];
                        py fileobj = a[4];
                        py mtime = a[5];
                        py origmode;
                        py raw;
                        py warnings;
                        if ((mode.truthy() && ([&]() -> py { py const t = boolean(mode.contains((S<"t">()))); return t.truthy() ? t : boolean(mode.contains((S<"U">()))); }()).truthy()))
                        {
                            py_raise((B<"ValueError">())((S<"Invalid mode: {!r}">()).method("format", { mode })));
                        }
                        if ((mode.truthy() && (!mode.contains((S<"b">())))))
                        {
                            mode = inplace(NEOS_PY_ADD, mode, S<"b">());
                        }
                        try
                        {
                            if (fileobj.is_none())
                            {
                                py const value5 = (Mod<"builtins">().attr("open"))(filename, [&]() -> py { py const t = mode; return t.truthy() ? t : S<"rb">(); }());
                                fileobj = value5;
                                self.set_attr("myfileobj", value5);
                            }
                            if (filename.is_none())
                            {
                                filename = (B<"getattr">())(fileobj, S<"name">(), S<"">());
                                if (!isinstance(filename, py_tuple({ B<"str">(), B<"bytes">() })))
                                {
                                    filename = S<"">();
                                }
                            }
                            else
                            {
                                filename = (Mod<"os">().attr("fspath"))(filename);
                            }
                            origmode = mode;
                            if (mode.is_none())
                            {
                                mode = (B<"getattr">())(fileobj, S<"mode">(), S<"rb">());
                            }
                            if (mode.method("startswith", { S<"r">() }).truthy())
                            {
                                self.set_attr("mode", G<"gzip.READ">());
                                raw = (G<"gzip._GzipReader">())(fileobj);
                                self.set_attr("_buffer", (Mod<"io">().attr("BufferedReader"))(raw));
                                self.set_attr("name", filename);
                            }
                            else if (mode.method("startswith", { py_tuple({ S<"w">(), S<"a">(), S<"x">() }) }).truthy())
                            {
                                if (origmode.is_none())
                                {
                                    warnings = module_of("warnings");
                                    warnings.method("warn", { S<"GzipFile was opened for writing, but this will change in future Python releases.  Specify the mode argument for opening it for writing.">(), B<"FutureWarning">(), I<2>() });
                                }
                                self.set_attr("mode", G<"gzip.WRITE">());
                                self.method("_init_write", { filename });
                                self.set_attr("compress", (Mod<"zlib">().attr("compressobj"))(compresslevel, Mod<"zlib">().attr("DEFLATED"), -(Mod<"zlib">().attr("MAX_WBITS")), Mod<"zlib">().attr("DEF_MEM_LEVEL"), I<0>()));
                                self.set_attr("_write_mtime", mtime);
                                self.set_attr("_buffer_size", G<"gzip._WRITE_BUFFER_SIZE">());
                                self.set_attr("_buffer", (Mod<"io">().attr("BufferedWriter")).call({ (G<"gzip._WriteBufferStream">())(self) }, { { "buffer_size", self.attr("_buffer_size") } }));
                            }
                            else
                            {
                                py_raise((B<"ValueError">())((S<"Invalid mode: {!r}">()).method("format", { mode })));
                            }
                            self.set_attr("fileobj", fileobj);
                            if (self.attr("mode") == (G<"gzip.WRITE">()))
                            {
                                self.method("_write_gzip_header", { compresslevel });
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler4;
                            {
                                // Avoid a ResourceWarning if the write fails,
                                // eg read-only file or KeyboardInterrupt
                                self.method("_close");
                                py_reraise(handler4.exception);
                            }
                        }
                        return none();
                    }) },
                { "mtime", B<"property">()(w.method("GzipFile", "mtime", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_buffer").attr("raw").attr("_last_mtime");
                    })) },
                { "__repr__", w.method("GzipFile", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s;
                        s = S(self.attr("fileobj").repr());
                        return ((((S<"<gzip ">()) + s[B<"slice">()(I<1>(), I<-1>(), none())]) + (S<" ">())) + (B<"hex">())((B<"id">())(self))) + (S<">">());
                    }) },
                { "_init_write", w.method("GzipFile", "_init_write", { py_positional("filename") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        self.set_attr("name", filename);
                        self.set_attr("crc", (Mod<"zlib">().attr("crc32"))(py{ make_bytes(std::string{ "", 0 }) }));
                        self.set_attr("size", I<0>());
                        self.set_attr("writebuf", py_list());
                        self.set_attr("bufsize", I<0>());
                        self.set_attr("offset", I<0>());
                        return none();
                    }) },
                { "tell", w.method("GzipFile", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_check_not_closed");
                        self.attr("_buffer").method("flush");
                        return (B<"super">()(G<"gzip.GzipFile">(), self)).method("tell");
                    }) },
                { "_write_gzip_header", w.method("GzipFile", "_write_gzip_header", { py_positional("compresslevel") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py compresslevel = a[1];
                        py flags;
                        py fname;
                        py mtime;
                        py xfl;
                        self.attr("fileobj").method("write", { py{ make_bytes(std::string{ "\x1f\x8b", 2 }) } });
                        self.attr("fileobj").method("write", { py{ make_bytes(std::string{ "\x08", 1 }) } });
                        try
                        {
                            // RFC 1952 requires the FNAME field to be Latin-1. Do not
                            // include filenames that cannot be represented that way.
                            fname = (Mod<"os">().attr("path")).method("basename", { self.attr("name") });
                            if (!isinstance(fname, B<"bytes">()))
                            {
                                fname = fname.method("encode", { S<"latin-1">() });
                            }
                            if (fname.method("endswith", { py{ make_bytes(std::string{ "\x2e\x67\x7a", 3 }) } }).truthy())
                            {
                                fname = fname[B<"slice">()(none(), I<-3>(), none())];
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler6;
                            if (matches(handler6.exception, B<"UnicodeEncodeError">()))
                            {
                                fname = py{ make_bytes(std::string{ "", 0 }) };
                            }
                            else
                                py_reraise(handler6.exception);
                        }
                        flags = I<0>();
                        if (fname.truthy())
                        {
                            flags = G<"gzip.FNAME">();
                        }
                        self.attr("fileobj").method("write", { (B<"chr">())(flags).method("encode", { S<"latin-1">() }) });
                        mtime = self.attr("_write_mtime");
                        if (mtime.is_none())
                        {
                            mtime = (Mod<"time">().attr("time"))();
                        }
                        (G<"gzip.write32u">())(self.attr("fileobj"), (B<"int">())(mtime));
                        if (compresslevel == (G<"gzip._COMPRESS_LEVEL_BEST">()))
                        {
                            xfl = py{ make_bytes(std::string{ "\x02", 1 }) };
                        }
                        else if (compresslevel == (G<"gzip._COMPRESS_LEVEL_FAST">()))
                        {
                            xfl = py{ make_bytes(std::string{ "\x04", 1 }) };
                        }
                        else
                        {
                            xfl = py{ make_bytes(std::string{ "\x00", 1 }) };
                        }
                        self.attr("fileobj").method("write", { xfl });
                        self.attr("fileobj").method("write", { py{ make_bytes(std::string{ "\xff", 1 }) } });
                        if (fname.truthy())
                        {
                            self.attr("fileobj").method("write", { fname + py{ make_bytes(std::string{ "\x00", 1 }) } });
                        }
                        return none();
                    }) },
                { "write", w.method("GzipFile", "write", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py errno_;
                        self.method("_check_not_closed");
                        if (self.attr("mode") != (G<"gzip.WRITE">()))
                        {
                            errno_ = module_of("errno");
                            py_raise((B<"OSError">())(errno_.attr("EBADF"), S<"write() on read-only GzipFile object">()));
                        }
                        if (self.attr("fileobj").is_none())
                        {
                            py_raise((B<"ValueError">())(S<"write() on closed GzipFile object">()));
                        }
                        return self.attr("_buffer").method("write", { data });
                    }) },
                { "_write_raw", w.method("GzipFile", "_write_raw", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py length;
                        if (isinstance(data, py_tuple({ B<"bytes">(), B<"bytearray">() })))
                        {
                            length = py{ data.size() };
                        }
                        else
                        {
                            // accept any data that supports the buffer protocol
                            data = (B<"memoryview">())(data);
                            length = data.attr("nbytes");
                        }
                        if (length > (I<0>()))
                        {
                            self.attr("fileobj").method("write", { self.attr("compress").method("compress", { data }) });
                            {
                                py const target7 = self;
                                target7.set_attr("size", inplace(NEOS_PY_ADD, target7.attr("size"), length));
                            }
                            self.set_attr("crc", (Mod<"zlib">().attr("crc32"))(data, self.attr("crc")));
                            {
                                py const target8 = self;
                                target8.set_attr("offset", inplace(NEOS_PY_ADD, target8.attr("offset"), length));
                            }
                        }
                        return length;
                    }) },
                { "read", w.method("GzipFile", "read", { py_positional("size", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py errno_;
                        self.method("_check_not_closed");
                        if (self.attr("mode") != (G<"gzip.READ">()))
                        {
                            errno_ = module_of("errno");
                            py_raise((B<"OSError">())(errno_.attr("EBADF"), S<"read() on write-only GzipFile object">()));
                        }
                        return self.attr("_buffer").method("read", { size });
                    }) },
                { "read1", w.method("GzipFile", "read1", { py_positional("size", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py errno_;
                        self.method("_check_not_closed");
                        if (self.attr("mode") != (G<"gzip.READ">()))
                        {
                            errno_ = module_of("errno");
                            py_raise((B<"OSError">())(errno_.attr("EBADF"), S<"read1() on write-only GzipFile object">()));
                        }
                        if (size < (I<0>()))
                        {
                            size = Mod<"io">().attr("DEFAULT_BUFFER_SIZE");
                        }
                        return self.attr("_buffer").method("read1", { size });
                    }) },
                { "peek", w.method("GzipFile", "peek", { py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py errno_;
                        self.method("_check_not_closed");
                        if (self.attr("mode") != (G<"gzip.READ">()))
                        {
                            errno_ = module_of("errno");
                            py_raise((B<"OSError">())(errno_.attr("EBADF"), S<"peek() on write-only GzipFile object">()));
                        }
                        return self.attr("_buffer").method("peek", { n });
                    }) },
                { "closed", B<"property">()(w.method("GzipFile", "closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("fileobj").is_none());
                    })) },
                { "close", w.method("GzipFile", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fileobj;
                        fileobj = self.attr("fileobj");
                        if ((fileobj.is_none() || self.attr("_buffer").attr("closed").truthy()))
                        {
                            return none();
                        }
                        {
                            std::optional<py> pending9;
                            [[maybe_unused]] int flow9 = 0;
                            [[maybe_unused]] py result9;
                            try
                            {
                                if (self.attr("mode") == (G<"gzip.WRITE">()))
                                {
                                    self.attr("_buffer").method("flush");
                                    fileobj.method("write", { self.attr("compress").method("flush") });
                                    (G<"gzip.write32u">())(fileobj, self.attr("crc"));
                                    // self.size may exceed 2 GiB, or even 4 GiB
                                    (G<"gzip.write32u">())(fileobj, self.attr("size") & (I<4294967295ll>()));
                                }
                                else if (self.attr("mode") == (G<"gzip.READ">()))
                                {
                                    self.attr("_buffer").method("close");
                                }
                            }
                            catch (raised const&)
                            {
                                pending9 = py_take_exception();
                            }
                            finally9:
                            self.method("_close");
                            if (pending9)
                                py_reraise(*pending9);
                            if (flow9 == 1)
                                return result9;
                        }
                        return none();
                    }) },
                { "_close", w.method("GzipFile", "_close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py myfileobj;
                        self.set_attr("fileobj", none());
                        myfileobj = self.attr("myfileobj");
                        if (!myfileobj.is_none())
                        {
                            self.set_attr("myfileobj", none());
                            myfileobj.method("close");
                        }
                        return none();
                    }) },
                { "flush", w.method("GzipFile", "flush", { py_positional("zlib_mode", Mod<"zlib">().attr("Z_SYNC_FLUSH")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py zlib_mode = a[1];
                        self.method("_check_not_closed");
                        if (self.attr("mode") == (G<"gzip.WRITE">()))
                        {
                            self.attr("_buffer").method("flush");
                            // Ensure the compressor's buffer is flushed
                            self.attr("fileobj").method("write", { self.attr("compress").method("flush", { zlib_mode }) });
                            self.attr("fileobj").method("flush");
                        }
                        return none();
                    }) },
                { "fileno", w.method("GzipFile", "fileno", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("fileobj").method("fileno");
                    }) },
                { "rewind", w.method("GzipFile", "rewind", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("mode") != (G<"gzip.READ">()))
                        {
                            py_raise((B<"OSError">())(S<"Can't rewind in write mode">()));
                        }
                        self.attr("_buffer").method("seek", { I<0>() });
                        return none();
                    }) },
                { "readable", w.method("GzipFile", "readable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("mode") == (G<"gzip.READ">()));
                    }) },
                { "writable", w.method("GzipFile", "writable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("mode") == (G<"gzip.WRITE">()));
                    }) },
                { "seekable", w.method("GzipFile", "seekable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) },
                { "seek", w.method("GzipFile", "seek", { py_positional("offset"), py_positional("whence", Mod<"io">().attr("SEEK_SET")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py offset = a[1];
                        py whence = a[2];
                        py chunk;
                        py count;
                        py i;
                        if (self.attr("mode") == (G<"gzip.WRITE">()))
                        {
                            self.method("_check_not_closed");
                            // Flush buffer to ensure validity of self.offset
                            self.attr("_buffer").method("flush");
                            if (whence != (Mod<"io">().attr("SEEK_SET")))
                            {
                                if (whence == (Mod<"io">().attr("SEEK_CUR")))
                                {
                                    offset = self.attr("offset") + offset;
                                }
                                else
                                {
                                    py_raise((B<"ValueError">())(S<"Seek from end not supported">()));
                                }
                            }
                            if (offset < self.attr("offset"))
                            {
                                py_raise((B<"OSError">())(S<"Negative seek in write mode">()));
                            }
                            count = offset - self.attr("offset");
                            chunk = py{ make_bytes(std::string{ "\x00", 1 }) } * self.attr("_buffer_size");
                            for (py const& item : iterate{ (B<"range">())(floordiv(count, self.attr("_buffer_size"))) })
                            {
                                i = item;
                                self.method("write", { chunk });
                            }
                            self.method("write", { py{ make_bytes(std::string{ "\x00", 1 }) } * (count % self.attr("_buffer_size")) });
                        }
                        else if (self.attr("mode") == (G<"gzip.READ">()))
                        {
                            self.method("_check_not_closed");
                            return self.attr("_buffer").method("seek", { offset, whence });
                        }
                        return self.attr("offset");
                    }) },
                { "readline", w.method("GzipFile", "readline", { py_positional("size", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        self.method("_check_not_closed");
                        return self.attr("_buffer").method("readline", { size });
                    }) }, }));

            define("gzip", "_read_exact", w.function("_read_exact", "_read_exact", { py_positional("fp"), py_positional("n") }, [](arguments a) -> py
                {
                    py fp = a[0];
                    py n = a[1];
                    py b;
                    py data;
                    data = fp.method("read", { n });
                    while (py{ data.size() } < n)
                    {
                        b = fp.method("read", { n - py{ data.size() } });
                        if (!b.truthy())
                        {
                            py_raise((B<"EOFError">())(S<"Compressed file ended before the end-of-stream marker was reached">()));
                        }
                        data = inplace(NEOS_PY_ADD, data, b);
                    }
                    return data;
                }));

            define("gzip", "_read_gzip_header", w.function("_read_gzip_header", "_read_gzip_header", { py_positional("fp") }, [](arguments a) -> py
                {
                    py fp = a[0];
                    py extra_len;
                    py flag;
                    py last_mtime;
                    py magic;
                    py method;
                    py s;
                    magic = fp.method("read", { I<2>() });
                    if (magic == py{ make_bytes(std::string{ "", 0 }) })
                    {
                        return none();
                    }
                    if (magic != py{ make_bytes(std::string{ "\x1f\x8b", 2 }) })
                    {
                        py_raise((G<"gzip.BadGzipFile">())((S<"Not a gzipped file (%r)">()) % magic));
                    }
                    auto const items12 = unpack((Mod<"struct">().attr("unpack"))(S<"<BBIxx">(), (G<"gzip._read_exact">())(fp, I<8>())), 3);
                    method = items12[0];
                    flag = items12[1];
                    last_mtime = items12[2];
                    if (method != (I<8>()))
                    {
                        py_raise((G<"gzip.BadGzipFile">())(S<"Unknown compression method">()));
                    }
                    if ((flag & (G<"gzip.FEXTRA">())).truthy())
                    {
                        // Read & discard the extra field, if present
                        auto const items13 = unpack((Mod<"struct">().attr("unpack"))(S<"<H">(), (G<"gzip._read_exact">())(fp, I<2>())), 1);
                        extra_len = items13[0];
                        (G<"gzip._read_exact">())(fp, extra_len);
                    }
                    if ((flag & (G<"gzip.FNAME">())).truthy())
                    {
                        // Read and discard a null-terminated string containing the filename
                        while (boolean(true).truthy())
                        {
                            s = fp.method("read", { I<1>() });
                            if (((!s.truthy()) || (s == py{ make_bytes(std::string{ "\x00", 1 }) })))
                            {
                                break;
                            }
                        }
                    }
                    if ((flag & (G<"gzip.FCOMMENT">())).truthy())
                    {
                        // Read and discard a null-terminated string containing a comment
                        while (boolean(true).truthy())
                        {
                            s = fp.method("read", { I<1>() });
                            if (((!s.truthy()) || (s == py{ make_bytes(std::string{ "\x00", 1 }) })))
                            {
                                break;
                            }
                        }
                    }
                    if ((flag & (G<"gzip.FHCRC">())).truthy())
                    {
                        (G<"gzip._read_exact">())(fp, I<2>());
                    }
                    return last_mtime;
                }));

            define("gzip", "_GzipReader", w.make_class("_GzipReader", { Mod<"_compression">().attr("DecompressReader") }, {
                { "__init__", w.method("_GzipReader", "__init__", { py_positional("fp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        (B<"super">()(G<"gzip._GzipReader">(), self)).method("__init__", { (G<"gzip._PaddedFile">())(fp), Mod<"zlib">().attr("_ZlibDecompressor") }, { { "wbits", -(Mod<"zlib">().attr("MAX_WBITS")) } });
                        // Set flag indicating start of a new member
                        self.set_attr("_new_member", boolean(true));
                        self.set_attr("_last_mtime", none());
                        return none();
                    }) },
                { "_init_read", w.method("_GzipReader", "_init_read", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_crc", (Mod<"zlib">().attr("crc32"))(py{ make_bytes(std::string{ "", 0 }) }));
                        self.set_attr("_stream_size", I<0>());
                        return none();
                    }) },
                { "_read_gzip_header", w.method("_GzipReader", "_read_gzip_header", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py last_mtime;
                        last_mtime = (G<"gzip._read_gzip_header">())(self.attr("_fp"));
                        if (last_mtime.is_none())
                        {
                            return boolean(false);
                        }
                        self.set_attr("_last_mtime", last_mtime);
                        return boolean(true);
                    }) },
                { "read", w.method("_GzipReader", "read", { py_positional("size", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py buf;
                        py uncompress;
                        if (size < (I<0>()))
                        {
                            return self.method("readall");
                        }
                        // size=0 is special because decompress(max_length=0) is not supported
                        if (!size.truthy())
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        // For certain input data, a single
                        // call to decompress() may not return
                        // any data. In this case, retry until we get some data or reach EOF.
                        while (boolean(true).truthy())
                        {
                            if (self.attr("_decompressor").attr("eof").truthy())
                            {
                                // Ending case: we've come to the end of a member in the file,
                                // so finish up this member, and read a new gzip header.
                                // Check the CRC and file size, and set the flag so we read
                                // a new member
                                self.method("_read_eof");
                                self.set_attr("_new_member", boolean(true));
                                self.set_attr("_decompressor", call_ex(self.attr("_decomp_factory"), {  }, py{}, self.attr("_decomp_args")));
                            }
                            if (self.attr("_new_member").truthy())
                            {
                                // If the _new_member flag is set, we have to
                                // jump to the next member, if there is one.
                                self.method("_init_read");
                                if (!self.method("_read_gzip_header").truthy())
                                {
                                    self.set_attr("_size", self.attr("_pos"));
                                    return py{ make_bytes(std::string{ "", 0 }) };
                                }
                                self.set_attr("_new_member", boolean(false));
                            }
                            // Read a chunk of data from the file
                            if (self.attr("_decompressor").attr("needs_input").truthy())
                            {
                                buf = self.attr("_fp").method("read", { G<"gzip.READ_BUFFER_SIZE">() });
                            }
                            else
                            {
                                buf = py{ make_bytes(std::string{ "", 0 }) };
                            }
                            uncompress = self.attr("_decompressor").method("decompress", { buf, size });
                            if (self.attr("_decompressor").attr("unused_data") != py{ make_bytes(std::string{ "", 0 }) })
                            {
                                // Prepend the already read bytes to the fileobj so they can
                                // be seen by _read_eof() and _read_gzip_header()
                                self.attr("_fp").method("prepend", { self.attr("_decompressor").attr("unused_data") });
                            }
                            if (uncompress != py{ make_bytes(std::string{ "", 0 }) })
                            {
                                break;
                            }
                            if (buf == py{ make_bytes(std::string{ "", 0 }) })
                            {
                                py_raise((B<"EOFError">())(S<"Compressed file ended before the end-of-stream marker was reached">()));
                            }
                        }
                        self.set_attr("_crc", (Mod<"zlib">().attr("crc32"))(uncompress, self.attr("_crc")));
                        {
                            py const target17 = self;
                            target17.set_attr("_stream_size", inplace(NEOS_PY_ADD, target17.attr("_stream_size"), py{ uncompress.size() }));
                        }
                        {
                            py const target18 = self;
                            target18.set_attr("_pos", inplace(NEOS_PY_ADD, target18.attr("_pos"), py{ uncompress.size() }));
                        }
                        return uncompress;
                    }) },
                { "_read_eof", w.method("_GzipReader", "_read_eof", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py c;
                        py crc32;
                        py isize;
                        auto const items19 = unpack((Mod<"struct">().attr("unpack"))(S<"<II">(), (G<"gzip._read_exact">())(self.attr("_fp"), I<8>())), 2);
                        crc32 = items19[0];
                        isize = items19[1];
                        if (crc32 != self.attr("_crc"))
                        {
                            py_raise((G<"gzip.BadGzipFile">())((S<"CRC check failed %s != %s">()) % py_tuple({ (B<"hex">())(crc32), (B<"hex">())(self.attr("_crc")) })));
                        }
                        else if (isize != (self.attr("_stream_size") & (I<4294967295ll>())))
                        {
                            py_raise((G<"gzip.BadGzipFile">())(S<"Incorrect length of data produced">()));
                        }
                        // Gzip files can be padded with zeroes and still have archives.
                        // Consume all zero bytes and set the file position to the first
                        // non-zero byte. See http://www.gzip.org/#faq8
                        c = py{ make_bytes(std::string{ "\x00", 1 }) };
                        while (c == py{ make_bytes(std::string{ "\x00", 1 }) })
                        {
                            c = self.attr("_fp").method("read", { I<1>() });
                        }
                        if (c.truthy())
                        {
                            self.attr("_fp").method("prepend", { c });
                        }
                        return none();
                    }) },
                { "_rewind", w.method("_GzipReader", "_rewind", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (B<"super">()(G<"gzip._GzipReader">(), self)).method("_rewind");
                        self.set_attr("_new_member", boolean(true));
                        return none();
                    }) }, }));

            define("gzip", "compress", w.function("compress", "compress", { py_positional("data"), py_positional("compresslevel", G<"gzip._COMPRESS_LEVEL_BEST">()), py_keyword("mtime", none()) }, [](arguments a) -> py
                {
                    py data = a[0];
                    py compresslevel = a[1];
                    py mtime = a[2];
                    py gzip_data;
                    py header;
                    // Wbits=31 automatically includes a gzip header and trailer.
                    gzip_data = (Mod<"zlib">().attr("compress")).call({ data }, { { "level", compresslevel }, { "wbits", I<31>() } });
                    if (mtime.is_none())
                    {
                        mtime = (Mod<"time">().attr("time"))();
                    }
                    // Reuse gzip header created by zlib, replace mtime and OS byte for
                    // consistency.
                    header = (Mod<"struct">().attr("pack"))(S<"<4sLBB">(), gzip_data, (B<"int">())(mtime), gzip_data[I<8>()], I<255>());
                    return header + gzip_data[B<"slice">()(I<10>(), none(), none())];
                }));

            define("gzip", "decompress", w.function("decompress", "decompress", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py crc;
                    py decompressed;
                    py decompressed_members;
                    py do_;
                    py fp;
                    py length;
                    decompressed_members = py_list();
                    while (boolean(true).truthy())
                    {
                        fp = (Mod<"io">().attr("BytesIO"))(data);
                        if ((G<"gzip._read_gzip_header">())(fp).is_none())
                        {
                            return py{ make_bytes(std::string{ "", 0 }) }.method("join", { decompressed_members });
                        }
                        // Use a zlib raw deflate compressor
                        do_ = (Mod<"zlib">().attr("decompressobj")).call({  }, { { "wbits", -(Mod<"zlib">().attr("MAX_WBITS")) } });
                        // Read all the data except the header
                        decompressed = do_.method("decompress", { data[B<"slice">()(fp.method("tell"), none(), none())] });
                        if (((!do_.attr("eof").truthy()) || (py{ do_.attr("unused_data").size() } < (I<8>()))))
                        {
                            py_raise((B<"EOFError">())(S<"Compressed file ended before the end-of-stream marker was reached">()));
                        }
                        auto const items22 = unpack((Mod<"struct">().attr("unpack"))(S<"<II">(), do_.attr("unused_data")[B<"slice">()(none(), I<8>(), none())]), 2);
                        crc = items22[0];
                        length = items22[1];
                        if (crc != (Mod<"zlib">().attr("crc32"))(decompressed))
                        {
                            py_raise((G<"gzip.BadGzipFile">())(S<"CRC check failed">()));
                        }
                        if (length != (py{ decompressed.size() } & (I<4294967295ll>())))
                        {
                            py_raise((G<"gzip.BadGzipFile">())(S<"Incorrect length of data produced">()));
                        }
                        decompressed_members.method("append", { decompressed });
                        data = do_.attr("unused_data")[B<"slice">()(I<8>(), none(), none())].method("lstrip", { py{ make_bytes(std::string{ "\x00", 1 }) } });
                    }
                    return none();
                }));

            define("gzip", "main", w.function("main", "main", {  }, [](arguments a) -> py
                {
                    py ArgumentParser;
                    py arg;
                    py args;
                    py chunk;
                    py compresslevel;
                    py f;
                    py g;
                    py group;
                    py parser;
                    ArgumentParser = M("argparse", "ArgumentParser");
                    parser = ArgumentParser.call({  }, { { "description", S<"A simple command line interface for the gzip module: act like gzip, but do not delete the input file.">() } });
                    group = parser.method("add_mutually_exclusive_group");
                    group.method("add_argument", { S<"--fast">() }, { { "action", S<"store_true">() }, { "help", S<"compress faster">() } });
                    group.method("add_argument", { S<"--best">() }, { { "action", S<"store_true">() }, { "help", S<"compress better">() } });
                    group.method("add_argument", { S<"-d">(), S<"--decompress">() }, { { "action", S<"store_true">() }, { "help", S<"act like gunzip instead of gzip">() } });
                    parser.method("add_argument", { S<"args">() }, { { "nargs", S<"*">() }, { "default", py_list({ S<"-">() }) }, { "metavar", S<"file">() } });
                    args = parser.method("parse_args");
                    compresslevel = G<"gzip._COMPRESS_LEVEL_TRADEOFF">();
                    if (args.attr("fast").truthy())
                    {
                        compresslevel = G<"gzip._COMPRESS_LEVEL_FAST">();
                    }
                    else if (args.attr("best").truthy())
                    {
                        compresslevel = G<"gzip._COMPRESS_LEVEL_BEST">();
                    }
                    for (py const& item : iterate{ args.attr("args") })
                    {
                        arg = item;
                        if (args.attr("decompress").truthy())
                        {
                            if (arg == (S<"-">()))
                            {
                                f = (G<"gzip.GzipFile">()).call({  }, { { "filename", S<"">() }, { "mode", S<"rb">() }, { "fileobj", (Mod<"sys">().attr("stdin")).attr("buffer") } });
                                g = (Mod<"sys">().attr("stdout")).attr("buffer");
                            }
                            else
                            {
                                if (arg[B<"slice">()(I<-3>(), none(), none())] != (S<".gz">()))
                                {
                                    (Mod<"sys">().attr("exit"))(S(std::string{ "filename doesn't end in .gz: " } + arg.repr()));
                                }
                                f = (G<"gzip.open">())(arg, S<"rb">());
                                g = (Mod<"builtins">().attr("open"))(arg[B<"slice">()(none(), I<-3>(), none())], S<"wb">());
                            }
                        }
                        else if (arg == (S<"-">()))
                        {
                            f = (Mod<"sys">().attr("stdin")).attr("buffer");
                            g = (G<"gzip.GzipFile">()).call({  }, { { "filename", S<"">() }, { "mode", S<"wb">() }, { "fileobj", (Mod<"sys">().attr("stdout")).attr("buffer") }, { "compresslevel", compresslevel } });
                        }
                        else
                        {
                            f = (Mod<"builtins">().attr("open"))(arg, S<"rb">());
                            g = (G<"gzip.open">())(arg + (S<".gz">()), S<"wb">());
                        }
                        while (boolean(true).truthy())
                        {
                            chunk = f.method("read", { G<"gzip.READ_BUFFER_SIZE">() });
                            if (!chunk.truthy())
                            {
                                break;
                            }
                            g.method("write", { chunk });
                        }
                        if (!g.is((Mod<"sys">().attr("stdout")).attr("buffer")))
                        {
                            g.method("close");
                        }
                        if (!f.is((Mod<"sys">().attr("stdin")).attr("buffer")))
                        {
                            f.method("close");
                        }
                    }
                    return none();
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool gzip_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_gzip();
                }
                if (auto const doc = gzip_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("_compression", py{ fn_import("_compression") }.release());
                aAttributes.emplace_back("builtins", py{ fn_import("builtins") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("struct", py{ fn_import("struct") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("weakref", py{ fn_import("weakref") }.release());
                aAttributes.emplace_back("zlib", py{ fn_import("zlib") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("gzip", gzip_module);
    }
}
