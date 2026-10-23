/*
  wave.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/wave.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module wave (CPython 3.13's Lib/wave.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "wave_docs.hpp"

namespace neos::python_runtime::detail::wave_module
{
    namespace
    {
        using pyclass::S;
        using pyclass::I;
        using pyclass::BigI;
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
        using pyclass::handled_exception;
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "wave.name"))
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
                py_raise("SystemError", std::string{ "wave: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"wave.name">()))
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
        // (globals(): the module's namespace (its __dict__ once it has been imported; while it is being made, its attributes so far))
        [[maybe_unused]] py module_globals()
        {
            if (modules_imported("wave"))
                return py{ fn_import("wave") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "wave" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "wave", wave_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- wave (Lib/wave.py)

        void make_wave()
        {
            static module_writer const& w = writer("wave");
            define("wave", "__all__", py_list({ S<"open">(), S<"Error">(), S<"Wave_read">(), S<"Wave_write">() }));
            define("wave", "Error", w.make_class("Error", { B<"Exception">() }, {
 }));

            define("wave", "WAVE_FORMAT_PCM", I<1>());
            define("wave", "WAVE_FORMAT_EXTENSIBLE", I<65534>());
            // Derived from uuid.UUID("00000001-0000-0010-8000-00aa00389b71").bytes_le
            define("wave", "KSDATAFORMAT_SUBTYPE_PCM", py{ make_bytes(std::string{ "\x01\x00\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71", 16 }) });
            define("wave", "_array_fmts", py_tuple({ none(), S<"b">(), S<"h">(), none(), S<"i">() }));
            define("wave", "_wave_params", (Mod<"collections">().attr("namedtuple"))(S<"_wave_params">(), S<"nchannels sampwidth framerate nframes comptype compname">()));
            define("wave", "_byteswap", w.function("_byteswap", "_byteswap", { py_positional("data"), py_positional("width") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py width = a[1];
                    py i;
                    py j;
                    py swapped_data;
                    swapped_data = (B<"bytearray">())(py{ data.size() });
                    for (py const& item : iterate{ (B<"range">())(I<0>(), py{ data.size() }, width) })
                    {
                        i = item;
                        for (py const& item : iterate{ (B<"range">())(width) })
                        {
                            j = item;
                            swapped_data.set_item(((i + width) - (I<1>())) - j, data[i + j]);
                        }
                    }
                    return (B<"bytes">())(swapped_data);
                }));

            define("wave", "_Chunk", w.make_class("_Chunk", {  }, {
                { "__init__", w.method("_Chunk", "__init__", { py_positional("file"), py_positional("align", boolean(true)), py_positional("bigendian", boolean(true)), py_positional("inclheader", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py align = a[2];
                        py bigendian = a[3];
                        py inclheader = a[4];
                        py strflag;
                        self.set_attr("closed", boolean(false));
                        self.set_attr("align", align);
                        if (bigendian.truthy())
                        {
                            strflag = S<">">();
                        }
                        else
                        {
                            strflag = S<"<">();
                        }
                        self.set_attr("file", file);
                        self.set_attr("chunkname", file.method("read", { I<4>() }));
                        if (py{ self.attr("chunkname").size() } < (I<4>()))
                        {
                            py_raise(B<"EOFError">());
                        }
                        try
                        {
                            self.set_attr("chunksize", (Mod<"struct">().attr("unpack_from"))(strflag + (S<"L">()), file.method("read", { I<4>() }))[I<0>()]);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler3;
                            if (matches(handler3.exception, Mod<"struct">().attr("error")))
                            {
                                fn_raise_from((B<"EOFError">()).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler3.exception);
                        }
                        if (inclheader.truthy())
                        {
                            self.set_attr("chunksize", self.attr("chunksize") - (I<8>()));
                        }
                        self.set_attr("size_read", I<0>());
                        {
                            bool otherwise4 = false;
                            try
                            {
                                self.set_attr("offset", self.attr("file").method("tell"));
                                otherwise4 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler4;
                                if (matches(handler4.exception, py_tuple({ B<"AttributeError">(), B<"OSError">() })))
                                {
                                    self.set_attr("seekable", boolean(false));
                                }
                                else
                                    py_reraise(handler4.exception);
                            }
                            if (otherwise4)
                            {
                                self.set_attr("seekable", boolean(true));
                            }
                        }
                        return none();
                    }) },
                { "getname", w.method("_Chunk", "getname", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("chunkname");
                    }) },
                { "close", w.method("_Chunk", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("closed").truthy())
                        {
                            {
                                std::optional<py> pending5;
                                [[maybe_unused]] int flow5 = 0;
                                [[maybe_unused]] py result5;
                                try
                                {
                                    self.method("skip");
                                }
                                catch (raised const&)
                                {
                                    pending5 = py_take_exception();
                                }
                                finally5:
                                self.set_attr("closed", boolean(true));
                                if (pending5)
                                    py_reraise(*pending5);
                                if (flow5 == 1)
                                    return result5;
                            }
                        }
                        return none();
                    }) },
                { "seek", w.method("_Chunk", "seek", { py_positional("pos"), py_positional("whence", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pos = a[1];
                        py whence = a[2];
                        if (self.attr("closed").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"I/O operation on closed file">()));
                        }
                        if (!self.attr("seekable").truthy())
                        {
                            py_raise((B<"OSError">())(S<"cannot seek">()));
                        }
                        if (whence == (I<1>()))
                        {
                            pos = pos + self.attr("size_read");
                        }
                        else if (whence == (I<2>()))
                        {
                            pos = pos + self.attr("chunksize");
                        }
                        if (((pos < (I<0>())) || (pos > self.attr("chunksize"))))
                        {
                            py_raise(B<"RuntimeError">());
                        }
                        self.attr("file").method("seek", { self.attr("offset") + pos, I<0>() });
                        self.set_attr("size_read", pos);
                        return none();
                    }) },
                { "tell", w.method("_Chunk", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("closed").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"I/O operation on closed file">()));
                        }
                        return self.attr("size_read");
                    }) },
                { "read", w.method("_Chunk", "read", { py_positional("size", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py data;
                        py dummy;
                        if (self.attr("closed").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"I/O operation on closed file">()));
                        }
                        if (self.attr("size_read") >= self.attr("chunksize"))
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        if (size < (I<0>()))
                        {
                            size = self.attr("chunksize") - self.attr("size_read");
                        }
                        if (size > (self.attr("chunksize") - self.attr("size_read")))
                        {
                            size = self.attr("chunksize") - self.attr("size_read");
                        }
                        data = self.attr("file").method("read", { size });
                        self.set_attr("size_read", self.attr("size_read") + py{ data.size() });
                        if (((self.attr("size_read") == self.attr("chunksize")) && self.attr("align").truthy() && (self.attr("chunksize") & (I<1>())).truthy()))
                        {
                            dummy = self.attr("file").method("read", { I<1>() });
                            self.set_attr("size_read", self.attr("size_read") + py{ dummy.size() });
                        }
                        return data;
                    }) },
                { "skip", w.method("_Chunk", "skip", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dummy;
                        py n;
                        if (self.attr("closed").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"I/O operation on closed file">()));
                        }
                        if (self.attr("seekable").truthy())
                        {
                            try
                            {
                                n = self.attr("chunksize") - self.attr("size_read");
                                // maybe fix alignment
                                if ((self.attr("align").truthy() && (self.attr("chunksize") & (I<1>())).truthy()))
                                {
                                    n = n + (I<1>());
                                }
                                self.attr("file").method("seek", { n, I<1>() });
                                self.set_attr("size_read", self.attr("size_read") + n);
                                return none();
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler6;
                                if (matches(handler6.exception, B<"OSError">()))
                                {
                                }
                                else
                                    py_reraise(handler6.exception);
                            }
                        }
                        while (self.attr("size_read") < self.attr("chunksize"))
                        {
                            n = (B<"min">())(I<8192>(), self.attr("chunksize") - self.attr("size_read"));
                            dummy = self.method("read", { n });
                            if (!dummy.truthy())
                            {
                                py_raise(B<"EOFError">());
                            }
                        }
                        return none();
                    }) }, }));

                // 
                // User visible methods.
                // 
                // 
                // Internal methods.
                // 
            define("wave", "Wave_read", w.make_class("Wave_read", {  }, {
                { "initfp", w.method("Wave_read", "initfp", { py_positional("file") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py chunk;
                        py chunkname;
                        self.set_attr("_convert", none());
                        self.set_attr("_soundpos", I<0>());
                        self.set_attr("_file", (G<"wave._Chunk">()).call({ file }, { { "bigendian", I<0>() } }));
                        if (self.attr("_file").method("getname") != py{ make_bytes(std::string{ "\x52\x49\x46\x46", 4 }) })
                        {
                            py_raise((G<"wave.Error">())(S<"file does not start with RIFF id">()));
                        }
                        if (self.attr("_file").method("read", { I<4>() }) != py{ make_bytes(std::string{ "\x57\x41\x56\x45", 4 }) })
                        {
                            py_raise((G<"wave.Error">())(S<"not a WAVE file">()));
                        }
                        self.set_attr("_fmt_chunk_read", I<0>());
                        self.set_attr("_data_chunk", none());
                        while ((I<1>()).truthy())
                        {
                            self.set_attr("_data_seek_needed", I<1>());
                            try
                            {
                                chunk = (G<"wave._Chunk">()).call({ self.attr("_file") }, { { "bigendian", I<0>() } });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler9;
                                if (matches(handler9.exception, B<"EOFError">()))
                                {
                                    break;
                                }
                                else
                                    py_reraise(handler9.exception);
                            }
                            chunkname = chunk.method("getname");
                            if (chunkname == py{ make_bytes(std::string{ "\x66\x6d\x74\x20", 4 }) })
                            {
                                self.method("_read_fmt_chunk", { chunk });
                                self.set_attr("_fmt_chunk_read", I<1>());
                            }
                            else if (chunkname == py{ make_bytes(std::string{ "\x64\x61\x74\x61", 4 }) })
                            {
                                if (!self.attr("_fmt_chunk_read").truthy())
                                {
                                    py_raise((G<"wave.Error">())(S<"data chunk before fmt chunk">()));
                                }
                                self.set_attr("_data_chunk", chunk);
                                self.set_attr("_nframes", floordiv(chunk.attr("chunksize"), self.attr("_framesize")));
                                self.set_attr("_data_seek_needed", I<0>());
                                break;
                            }
                            chunk.method("skip");
                        }
                        if (((!self.attr("_fmt_chunk_read").truthy()) || (!self.attr("_data_chunk").truthy())))
                        {
                            py_raise((G<"wave.Error">())(S<"fmt chunk and/or data chunk missing">()));
                        }
                        return none();
                    }) },
                { "__init__", w.method("Wave_read", "__init__", { py_positional("f") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        self.set_attr("_i_opened_the_file", none());
                        if (isinstance(f, B<"str">()))
                        {
                            f = (Mod<"builtins">().attr("open"))(f, S<"rb">());
                            self.set_attr("_i_opened_the_file", f);
                        }
                        // else, assume it is an open file object already
                        try
                        {
                            self.method("initfp", { f });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler10;
                            {
                                if (self.attr("_i_opened_the_file").truthy())
                                {
                                    f.method("close");
                                }
                                py_reraise(handler10.exception);
                            }
                        }
                        return none();
                    }) },
                { "__del__", w.method("Wave_read", "__del__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("close");
                        return none();
                    }) },
                { "__enter__", w.method("Wave_read", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("Wave_read", "__exit__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        self.method("close");
                        return none();
                    }, true) },
                { "getfp", w.method("Wave_read", "getfp", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_file");
                    }) },
                { "rewind", w.method("Wave_read", "rewind", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_data_seek_needed", I<1>());
                        self.set_attr("_soundpos", I<0>());
                        return none();
                    }) },
                { "close", w.method("Wave_read", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file;
                        self.set_attr("_file", none());
                        file = self.attr("_i_opened_the_file");
                        if (file.truthy())
                        {
                            self.set_attr("_i_opened_the_file", none());
                            file.method("close");
                        }
                        return none();
                    }) },
                { "tell", w.method("Wave_read", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_soundpos");
                    }) },
                { "getnchannels", w.method("Wave_read", "getnchannels", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_nchannels");
                    }) },
                { "getnframes", w.method("Wave_read", "getnframes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_nframes");
                    }) },
                { "getsampwidth", w.method("Wave_read", "getsampwidth", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_sampwidth");
                    }) },
                { "getframerate", w.method("Wave_read", "getframerate", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_framerate");
                    }) },
                { "getcomptype", w.method("Wave_read", "getcomptype", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_comptype");
                    }) },
                { "getcompname", w.method("Wave_read", "getcompname", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_compname");
                    }) },
                { "getparams", w.method("Wave_read", "getparams", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"wave._wave_params">())(self.method("getnchannels"), self.method("getsampwidth"), self.method("getframerate"), self.method("getnframes"), self.method("getcomptype"), self.method("getcompname"));
                    }) },
                { "getmarkers", w.method("Wave_read", "getmarkers", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py warnings;
                        warnings = module_of("warnings");
                        warnings.method("_deprecated", { S<"Wave_read.getmarkers">() }, { { "remove", py_tuple({ I<3>(), I<15>() }) } });
                        return none();
                    }) },
                { "getmark", w.method("Wave_read", "getmark", { py_positional("id") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py id = a[1];
                        py warnings;
                        warnings = module_of("warnings");
                        warnings.method("_deprecated", { S<"Wave_read.getmark">() }, { { "remove", py_tuple({ I<3>(), I<15>() }) } });
                        py_raise((G<"wave.Error">())(S<"no marks">()));
                    }) },
                { "setpos", w.method("Wave_read", "setpos", { py_positional("pos") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pos = a[1];
                        if (((pos < (I<0>())) || (pos > self.attr("_nframes"))))
                        {
                            py_raise((G<"wave.Error">())(S<"position not in range">()));
                        }
                        self.set_attr("_soundpos", pos);
                        self.set_attr("_data_seek_needed", I<1>());
                        return none();
                    }) },
                { "readframes", w.method("Wave_read", "readframes", { py_positional("nframes") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py nframes = a[1];
                        py data;
                        py pos;
                        if (self.attr("_data_seek_needed").truthy())
                        {
                            self.attr("_data_chunk").method("seek", { I<0>(), I<0>() });
                            pos = self.attr("_soundpos") * self.attr("_framesize");
                            if (pos.truthy())
                            {
                                self.attr("_data_chunk").method("seek", { pos, I<0>() });
                            }
                            self.set_attr("_data_seek_needed", I<0>());
                        }
                        if (nframes == (I<0>()))
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        data = self.attr("_data_chunk").method("read", { nframes * self.attr("_framesize") });
                        if (((self.attr("_sampwidth") != (I<1>())) && ((Mod<"sys">().attr("byteorder")) == (S<"big">()))))
                        {
                            data = (G<"wave._byteswap">())(data, self.attr("_sampwidth"));
                        }
                        if ((self.attr("_convert").truthy() && data.truthy()))
                        {
                            data = self.method("_convert", { data });
                        }
                        self.set_attr("_soundpos", self.attr("_soundpos") + floordiv(py{ data.size() }, (self.attr("_nchannels") * self.attr("_sampwidth"))));
                        return data;
                    }) },
                { "_read_fmt_chunk", w.method("Wave_read", "_read_fmt_chunk", { py_positional("chunk") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py chunk = a[1];
                        py SubFormat;
                        py cbSize;
                        py dwAvgBytesPerSec;
                        py dwChannelMask;
                        py sampwidth;
                        py subformat_msg;
                        py uuid;
                        py wBlockAlign;
                        py wFormatTag;
                        py wValidBitsPerSample;
                        try
                        {
                            auto const items12 = unpack((Mod<"struct">().attr("unpack_from"))(S<"<HHLLH">(), chunk.method("read", { I<14>() })), 5);
                            wFormatTag = items12[0];
                            self.set_attr("_nchannels", items12[1]);
                            self.set_attr("_framerate", items12[2]);
                            dwAvgBytesPerSec = items12[3];
                            wBlockAlign = items12[4];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler11;
                            if (matches(handler11.exception, Mod<"struct">().attr("error")))
                            {
                                fn_raise_from((B<"EOFError">()).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler11.exception);
                        }
                        if (((wFormatTag != (G<"wave.WAVE_FORMAT_PCM">())) && (wFormatTag != (G<"wave.WAVE_FORMAT_EXTENSIBLE">()))))
                        {
                            py_raise((G<"wave.Error">())((S<"unknown format: %r">()) % py_tuple({ wFormatTag })));
                        }
                        try
                        {
                            sampwidth = (Mod<"struct">().attr("unpack_from"))(S<"<H">(), chunk.method("read", { I<2>() }))[I<0>()];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler13;
                            if (matches(handler13.exception, Mod<"struct">().attr("error")))
                            {
                                fn_raise_from((B<"EOFError">()).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler13.exception);
                        }
                        if (wFormatTag == (G<"wave.WAVE_FORMAT_EXTENSIBLE">()))
                        {
                            try
                            {
                                auto const items15 = unpack((Mod<"struct">().attr("unpack_from"))(S<"<HHL">(), chunk.method("read", { I<8>() })), 3);
                                cbSize = items15[0];
                                wValidBitsPerSample = items15[1];
                                dwChannelMask = items15[2];
                                // Read the entire UUID from the chunk
                                SubFormat = chunk.method("read", { I<16>() });
                                if (py{ SubFormat.size() } < (I<16>()))
                                {
                                    py_raise(B<"EOFError">());
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler14;
                                if (matches(handler14.exception, Mod<"struct">().attr("error")))
                                {
                                    fn_raise_from((B<"EOFError">()).get(), none().get());
                                    throw raised{};
                                }
                                else
                                    py_reraise(handler14.exception);
                            }
                            if (SubFormat != (G<"wave.KSDATAFORMAT_SUBTYPE_PCM">()))
                            {
                                try
                                {
                                    uuid = module_of("uuid");
                                    subformat_msg = S(std::string{ "unknown extended format: " } + uuid.method("UUID", {  }, { { "bytes_le", SubFormat } }).str());
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler16;
                                    if (matches(handler16.exception, B<"Exception">()))
                                    {
                                        subformat_msg = S<"unknown extended format">();
                                    }
                                    else
                                        py_reraise(handler16.exception);
                                }
                                py_raise((G<"wave.Error">())(subformat_msg));
                            }
                        }
                        self.set_attr("_sampwidth", floordiv((sampwidth + (I<7>())), (I<8>())));
                        if (!self.attr("_sampwidth").truthy())
                        {
                            py_raise((G<"wave.Error">())(S<"bad sample width">()));
                        }
                        if (!self.attr("_nchannels").truthy())
                        {
                            py_raise((G<"wave.Error">())(S<"bad # of channels">()));
                        }
                        self.set_attr("_framesize", self.attr("_nchannels") * self.attr("_sampwidth"));
                        self.set_attr("_comptype", S<"NONE">());
                        self.set_attr("_compname", S<"not compressed">());
                        return none();
                    }) }, }));

                // 
                // User visible methods.
                // 
                // 
                // Internal methods.
                // 
            define("wave", "Wave_write", w.make_class("Wave_write", {  }, {
                { "_file", none() },
                { "__init__", w.method("Wave_write", "__init__", { py_positional("f") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        self.set_attr("_i_opened_the_file", none());
                        if (isinstance(f, B<"str">()))
                        {
                            f = (Mod<"builtins">().attr("open"))(f, S<"wb">());
                            self.set_attr("_i_opened_the_file", f);
                        }
                        try
                        {
                            self.method("initfp", { f });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler17;
                            {
                                if (self.attr("_i_opened_the_file").truthy())
                                {
                                    f.method("close");
                                }
                                py_reraise(handler17.exception);
                            }
                        }
                        return none();
                    }) },
                { "initfp", w.method("Wave_write", "initfp", { py_positional("file") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        self.set_attr("_file", file);
                        self.set_attr("_convert", none());
                        self.set_attr("_nchannels", I<0>());
                        self.set_attr("_sampwidth", I<0>());
                        self.set_attr("_framerate", I<0>());
                        self.set_attr("_nframes", I<0>());
                        self.set_attr("_nframeswritten", I<0>());
                        self.set_attr("_datawritten", I<0>());
                        self.set_attr("_datalength", I<0>());
                        self.set_attr("_headerwritten", boolean(false));
                        return none();
                    }) },
                { "__del__", w.method("Wave_write", "__del__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("close");
                        return none();
                    }) },
                { "__enter__", w.method("Wave_write", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("Wave_write", "__exit__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        self.method("close");
                        return none();
                    }, true) },
                { "setnchannels", w.method("Wave_write", "setnchannels", { py_positional("nchannels") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py nchannels = a[1];
                        if (self.attr("_datawritten").truthy())
                        {
                            py_raise((G<"wave.Error">())(S<"cannot change parameters after starting to write">()));
                        }
                        if (nchannels < (I<1>()))
                        {
                            py_raise((G<"wave.Error">())(S<"bad # of channels">()));
                        }
                        self.set_attr("_nchannels", nchannels);
                        return none();
                    }) },
                { "getnchannels", w.method("Wave_write", "getnchannels", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_nchannels").truthy())
                        {
                            py_raise((G<"wave.Error">())(S<"number of channels not set">()));
                        }
                        return self.attr("_nchannels");
                    }) },
                { "setsampwidth", w.method("Wave_write", "setsampwidth", { py_positional("sampwidth") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sampwidth = a[1];
                        if (self.attr("_datawritten").truthy())
                        {
                            py_raise((G<"wave.Error">())(S<"cannot change parameters after starting to write">()));
                        }
                        if (((sampwidth < (I<1>())) || (sampwidth > (I<4>()))))
                        {
                            py_raise((G<"wave.Error">())(S<"bad sample width">()));
                        }
                        self.set_attr("_sampwidth", sampwidth);
                        return none();
                    }) },
                { "getsampwidth", w.method("Wave_write", "getsampwidth", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_sampwidth").truthy())
                        {
                            py_raise((G<"wave.Error">())(S<"sample width not set">()));
                        }
                        return self.attr("_sampwidth");
                    }) },
                { "setframerate", w.method("Wave_write", "setframerate", { py_positional("framerate") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py framerate = a[1];
                        if (self.attr("_datawritten").truthy())
                        {
                            py_raise((G<"wave.Error">())(S<"cannot change parameters after starting to write">()));
                        }
                        if (framerate <= (I<0>()))
                        {
                            py_raise((G<"wave.Error">())(S<"bad frame rate">()));
                        }
                        self.set_attr("_framerate", (B<"int">())((B<"round">())(framerate)));
                        return none();
                    }) },
                { "getframerate", w.method("Wave_write", "getframerate", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_framerate").truthy())
                        {
                            py_raise((G<"wave.Error">())(S<"frame rate not set">()));
                        }
                        return self.attr("_framerate");
                    }) },
                { "setnframes", w.method("Wave_write", "setnframes", { py_positional("nframes") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py nframes = a[1];
                        if (self.attr("_datawritten").truthy())
                        {
                            py_raise((G<"wave.Error">())(S<"cannot change parameters after starting to write">()));
                        }
                        self.set_attr("_nframes", nframes);
                        return none();
                    }) },
                { "getnframes", w.method("Wave_write", "getnframes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_nframeswritten");
                    }) },
                { "setcomptype", w.method("Wave_write", "setcomptype", { py_positional("comptype"), py_positional("compname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py comptype = a[1];
                        py compname = a[2];
                        if (self.attr("_datawritten").truthy())
                        {
                            py_raise((G<"wave.Error">())(S<"cannot change parameters after starting to write">()));
                        }
                        if (!py_tuple({ S<"NONE">() }).contains(comptype))
                        {
                            py_raise((G<"wave.Error">())(S<"unsupported compression type">()));
                        }
                        self.set_attr("_comptype", comptype);
                        self.set_attr("_compname", compname);
                        return none();
                    }) },
                { "getcomptype", w.method("Wave_write", "getcomptype", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_comptype");
                    }) },
                { "getcompname", w.method("Wave_write", "getcompname", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_compname");
                    }) },
                { "setparams", w.method("Wave_write", "setparams", { py_positional("params") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py params = a[1];
                        py compname;
                        py comptype;
                        py framerate;
                        py nchannels;
                        py nframes;
                        py sampwidth;
                        auto const items18 = unpack(params, 6);
                        nchannels = items18[0];
                        sampwidth = items18[1];
                        framerate = items18[2];
                        nframes = items18[3];
                        comptype = items18[4];
                        compname = items18[5];
                        if (self.attr("_datawritten").truthy())
                        {
                            py_raise((G<"wave.Error">())(S<"cannot change parameters after starting to write">()));
                        }
                        self.method("setnchannels", { nchannels });
                        self.method("setsampwidth", { sampwidth });
                        self.method("setframerate", { framerate });
                        self.method("setnframes", { nframes });
                        self.method("setcomptype", { comptype, compname });
                        return none();
                    }) },
                { "getparams", w.method("Wave_write", "getparams", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (((!self.attr("_nchannels").truthy()) || (!self.attr("_sampwidth").truthy()) || (!self.attr("_framerate").truthy())))
                        {
                            py_raise((G<"wave.Error">())(S<"not all parameters set">()));
                        }
                        return (G<"wave._wave_params">())(self.attr("_nchannels"), self.attr("_sampwidth"), self.attr("_framerate"), self.attr("_nframes"), self.attr("_comptype"), self.attr("_compname"));
                    }) },
                { "setmark", w.method("Wave_write", "setmark", { py_positional("id"), py_positional("pos"), py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py id = a[1];
                        py pos = a[2];
                        py name = a[3];
                        py warnings;
                        warnings = module_of("warnings");
                        warnings.method("_deprecated", { S<"Wave_write.setmark">() }, { { "remove", py_tuple({ I<3>(), I<15>() }) } });
                        py_raise((G<"wave.Error">())(S<"setmark() not supported">()));
                    }) },
                { "getmark", w.method("Wave_write", "getmark", { py_positional("id") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py id = a[1];
                        py warnings;
                        warnings = module_of("warnings");
                        warnings.method("_deprecated", { S<"Wave_write.getmark">() }, { { "remove", py_tuple({ I<3>(), I<15>() }) } });
                        py_raise((G<"wave.Error">())(S<"no marks">()));
                    }) },
                { "getmarkers", w.method("Wave_write", "getmarkers", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py warnings;
                        warnings = module_of("warnings");
                        warnings.method("_deprecated", { S<"Wave_write.getmarkers">() }, { { "remove", py_tuple({ I<3>(), I<15>() }) } });
                        return none();
                    }) },
                { "tell", w.method("Wave_write", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_nframeswritten");
                    }) },
                { "writeframesraw", w.method("Wave_write", "writeframesraw", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py nframes;
                        if (!isinstance(data, py_tuple({ B<"bytes">(), B<"bytearray">() })))
                        {
                            data = (B<"memoryview">())(data).method("cast", { S<"B">() });
                        }
                        self.method("_ensure_header_written", { py{ data.size() } });
                        nframes = floordiv(py{ data.size() }, (self.attr("_sampwidth") * self.attr("_nchannels")));
                        if (self.attr("_convert").truthy())
                        {
                            data = self.method("_convert", { data });
                        }
                        if (((self.attr("_sampwidth") != (I<1>())) && ((Mod<"sys">().attr("byteorder")) == (S<"big">()))))
                        {
                            data = (G<"wave._byteswap">())(data, self.attr("_sampwidth"));
                        }
                        self.attr("_file").method("write", { data });
                        {
                            py const target19 = self;
                            target19.set_attr("_datawritten", inplace(NEOS_PY_ADD, target19.attr("_datawritten"), py{ data.size() }));
                        }
                        self.set_attr("_nframeswritten", self.attr("_nframeswritten") + nframes);
                        return none();
                    }) },
                { "writeframes", w.method("Wave_write", "writeframes", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("writeframesraw", { data });
                        if (self.attr("_datalength") != self.attr("_datawritten"))
                        {
                            self.method("_patchheader");
                        }
                        return none();
                    }) },
                { "close", w.method("Wave_write", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file;
                        {
                            std::optional<py> pending20;
                            [[maybe_unused]] int flow20 = 0;
                            [[maybe_unused]] py result20;
                            try
                            {
                                if (self.attr("_file").truthy())
                                {
                                    self.method("_ensure_header_written", { I<0>() });
                                    if (self.attr("_datalength") != self.attr("_datawritten"))
                                    {
                                        self.method("_patchheader");
                                    }
                                    self.attr("_file").method("flush");
                                }
                            }
                            catch (raised const&)
                            {
                                pending20 = py_take_exception();
                            }
                            finally20:
                            self.set_attr("_file", none());
                            file = self.attr("_i_opened_the_file");
                            if (file.truthy())
                            {
                                self.set_attr("_i_opened_the_file", none());
                                file.method("close");
                            }
                            if (pending20)
                                py_reraise(*pending20);
                            if (flow20 == 1)
                                return result20;
                        }
                        return none();
                    }) },
                { "_ensure_header_written", w.method("Wave_write", "_ensure_header_written", { py_positional("datasize") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py datasize = a[1];
                        if (!self.attr("_headerwritten").truthy())
                        {
                            if (!self.attr("_nchannels").truthy())
                            {
                                py_raise((G<"wave.Error">())(S<"# channels not specified">()));
                            }
                            if (!self.attr("_sampwidth").truthy())
                            {
                                py_raise((G<"wave.Error">())(S<"sample width not specified">()));
                            }
                            if (!self.attr("_framerate").truthy())
                            {
                                py_raise((G<"wave.Error">())(S<"sampling rate not specified">()));
                            }
                            self.method("_write_header", { datasize });
                        }
                        return none();
                    }) },
                { "_write_header", w.method("Wave_write", "_write_header", { py_positional("initlength") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py initlength = a[1];
                        if (self.attr("_headerwritten").truthy())
                            py_raise(B<"AssertionError">()());
                        self.attr("_file").method("write", { py{ make_bytes(std::string{ "\x52\x49\x46\x46", 4 }) } });
                        if (!self.attr("_nframes").truthy())
                        {
                            self.set_attr("_nframes", floordiv(initlength, (self.attr("_nchannels") * self.attr("_sampwidth"))));
                        }
                        self.set_attr("_datalength", (self.attr("_nframes") * self.attr("_nchannels")) * self.attr("_sampwidth"));
                        try
                        {
                            self.set_attr("_form_length_pos", self.attr("_file").method("tell"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler21;
                            if (matches(handler21.exception, py_tuple({ B<"AttributeError">(), B<"OSError">() })))
                            {
                                self.set_attr("_form_length_pos", none());
                            }
                            else
                                py_reraise(handler21.exception);
                        }
                        self.attr("_file").method("write", { (Mod<"struct">().attr("pack"))(S<"<L4s4sLHHLLHH4s">(), (I<36>()) + self.attr("_datalength"), py{ make_bytes(std::string{ "\x57\x41\x56\x45", 4 }) }, py{ make_bytes(std::string{ "\x66\x6d\x74\x20", 4 }) }, I<16>(), G<"wave.WAVE_FORMAT_PCM">(), self.attr("_nchannels"), self.attr("_framerate"), (self.attr("_nchannels") * self.attr("_framerate")) * self.attr("_sampwidth"), self.attr("_nchannels") * self.attr("_sampwidth"), self.attr("_sampwidth") * (I<8>()), py{ make_bytes(std::string{ "\x64\x61\x74\x61", 4 }) }) });
                        if (!self.attr("_form_length_pos").is_none())
                        {
                            self.set_attr("_data_length_pos", self.attr("_file").method("tell"));
                        }
                        self.attr("_file").method("write", { (Mod<"struct">().attr("pack"))(S<"<L">(), self.attr("_datalength")) });
                        self.set_attr("_headerwritten", boolean(true));
                        return none();
                    }) },
                { "_patchheader", w.method("Wave_write", "_patchheader", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py curpos;
                        if (!self.attr("_headerwritten").truthy())
                            py_raise(B<"AssertionError">()());
                        if (self.attr("_datawritten") == self.attr("_datalength"))
                        {
                            return none();
                        }
                        curpos = self.attr("_file").method("tell");
                        self.attr("_file").method("seek", { self.attr("_form_length_pos"), I<0>() });
                        self.attr("_file").method("write", { (Mod<"struct">().attr("pack"))(S<"<L">(), (I<36>()) + self.attr("_datawritten")) });
                        self.attr("_file").method("seek", { self.attr("_data_length_pos"), I<0>() });
                        self.attr("_file").method("write", { (Mod<"struct">().attr("pack"))(S<"<L">(), self.attr("_datawritten")) });
                        self.attr("_file").method("seek", { curpos, I<0>() });
                        self.set_attr("_datalength", self.attr("_datawritten"));
                        return none();
                    }) }, }));

            define("wave", "open", w.function("open", "open", { py_positional("f"), py_positional("mode", none()) }, [](arguments a) -> py
                {
                    py f = a[0];
                    py mode = a[1];
                    if (mode.is_none())
                    {
                        if (has_attr(f, "mode"))
                        {
                            mode = f.attr("mode");
                        }
                        else
                        {
                            mode = S<"rb">();
                        }
                    }
                    if (py_tuple({ S<"r">(), S<"rb">() }).contains(mode))
                    {
                        return (G<"wave.Wave_read">())(f);
                    }
                    else if (py_tuple({ S<"w">(), S<"wb">() }).contains(mode))
                    {
                        return (G<"wave.Wave_write">())(f);
                    }
                    else
                    {
                        py_raise((G<"wave.Error">())(S<"mode must be 'r', 'rb', 'w', or 'wb'">()));
                    }
                    return none();
                }));
        }

        bool wave_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_wave();
                }
                if (auto const doc = wave_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("namedtuple", import_attribute("collections", "namedtuple").release());
                aAttributes.emplace_back("builtins", py{ fn_import("builtins") }.release());
                aAttributes.emplace_back("struct", py{ fn_import("struct") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("wave", wave_module);
    }
}
