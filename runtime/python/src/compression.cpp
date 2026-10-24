/*
  compression.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/_compression.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module _compression (CPython 3.13's Lib/_compression.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "compression_docs.hpp"

namespace neos::python_runtime::detail::_compression_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "_compression.name"))
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
                py_raise("SystemError", std::string{ "_compression: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"_compression.name">()))
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
        [[maybe_unused]] void set_global(char const* aModule, char const* aName, py const& aValue)
        {
            define(aModule, aName, aValue);
            if (modules_imported(aModule))
                py{ fn_import(aModule) }.set_attr(aName, aValue);
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "_compression", compression_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- _compression (Lib/_compression.py)

        void make__compression()
        {
            static module_writer const& w = writer("_compression");
            define("_compression", "BUFFER_SIZE", Mod<"io">().attr("DEFAULT_BUFFER_SIZE"));
            define("_compression", "BaseStream", w.make_class("BaseStream", { Mod<"io">().attr("BufferedIOBase") }, {
                { "_check_not_closed", w.method("BaseStream", "_check_not_closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("closed").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"I/O operation on closed file">()));
                        }
                        return none();
                    }) },
                { "_check_can_read", w.method("BaseStream", "_check_can_read", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.method("readable").truthy())
                        {
                            py_raise((Mod<"io">().attr("UnsupportedOperation"))(S<"File not open for reading">()));
                        }
                        return none();
                    }) },
                { "_check_can_write", w.method("BaseStream", "_check_can_write", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.method("writable").truthy())
                        {
                            py_raise((Mod<"io">().attr("UnsupportedOperation"))(S<"File not open for writing">()));
                        }
                        return none();
                    }) },
                { "_check_can_seek", w.method("BaseStream", "_check_can_seek", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.method("readable").truthy())
                        {
                            py_raise((Mod<"io">().attr("UnsupportedOperation"))(S<"Seeking is only supported on files open for reading">()));
                        }
                        if (!self.method("seekable").truthy())
                        {
                            py_raise((Mod<"io">().attr("UnsupportedOperation"))(S<"The underlying file object does not support seeking">()));
                        }
                        return none();
                    }) }, }));

                // Rewind the file to the beginning of the data stream.
            define("_compression", "DecompressReader", w.make_class("DecompressReader", { Mod<"io">().attr("RawIOBase") }, {
                { "readable", w.method("DecompressReader", "readable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) },
                { "__init__", w.method("DecompressReader", "__init__", { py_positional("fp"), py_positional("decomp_factory"), py_positional("trailing_error", py_tuple()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py decomp_factory = a[2];
                        py trailing_error = a[3];
                        py decomp_args = a[4];
                        self.set_attr("_fp", fp);
                        self.set_attr("_eof", boolean(false));
                        self.set_attr("_pos", I<0>());
                        // Set to size of decompressed stream once it is known, for SEEK_END
                        self.set_attr("_size", I<-1>());
                        // Save the decompressor factory and arguments.
                        // If the file contains multiple compressed streams, each
                        // stream will need a separate decompressor object. A new decompressor
                        // object is also needed when implementing a backwards seek().
                        self.set_attr("_decomp_factory", decomp_factory);
                        self.set_attr("_decomp_args", decomp_args);
                        self.set_attr("_decompressor", call_ex(self.attr("_decomp_factory"), {  }, py{}, self.attr("_decomp_args")));
                        // Exception class to catch from decompressor signifying invalid
                        // trailing data to ignore
                        self.set_attr("_trailing_error", trailing_error);
                        return none();
                    }, false, true) },
                { "close", w.method("DecompressReader", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_decompressor", none());
                        return (B<"super">()(G<"_compression.DecompressReader">(), self)).method("close");
                    }) },
                { "seekable", w.method("DecompressReader", "seekable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_fp").method("seekable");
                    }) },
                { "readinto", w.method("DecompressReader", "readinto", { py_positional("b") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py b = a[1];
                        py byte_view;
                        py data;
                        py view;
                        {
                            py const manager1 = (B<"memoryview">())(b);
                            py const exit1 = pyclass::special_method(manager1, "__exit__");
                            py const value1 = pyclass::special_method(manager1, "__enter__")(manager1);
                            view = value1;
                            [[maybe_unused]] int flow1 = 0;
                            [[maybe_unused]] py result1;
                            bool exited1 = false;
                            try
                            {
                                {
                                    py const manager2 = view.method("cast", { S<"B">() });
                                    py const exit2 = pyclass::special_method(manager2, "__exit__");
                                    py const value2 = pyclass::special_method(manager2, "__enter__")(manager2);
                                    byte_view = value2;
                                    [[maybe_unused]] int flow2 = 0;
                                    [[maybe_unused]] py result2;
                                    bool exited2 = false;
                                    try
                                    {
                                        data = self.method("read", { py{ byte_view.size() } });
                                        byte_view.set_item(B<"slice">()(none(), py{ data.size() }, none()), data);
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler2;
                                        exited2 = true;
                                        if (!exit2(manager2, type_of(handler2.exception), handler2.exception, handler2.exception.attr("__traceback__")).truthy())
                                            py_reraise(handler2.exception);
                                    }
                                    if (!exited2)
                                        exit2(manager2, none(), none(), none());
                                    if (flow2 == 1)
                                        { result1 = result2; flow1 = 1; goto finally1; }
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler1;
                                exited1 = true;
                                if (!exit1(manager1, type_of(handler1.exception), handler1.exception, handler1.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler1.exception);
                            }
                            finally1:
                            if (!exited1)
                                exit1(manager1, none(), none(), none());
                            if (flow1 == 1)
                                return result1;
                        }
                        return py{ data.size() };
                    }) },
                { "read", w.method("DecompressReader", "read", { py_positional("size", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py data;
                        py rawblock;
                        if (size < (I<0>()))
                        {
                            return self.method("readall");
                        }
                        if (((!size.truthy()) || self.attr("_eof").truthy()))
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        data = none();
                        // Depending on the input data, our call to the decompressor may not
                        // return any data. In this case, try again after reading another block.
                        while (boolean(true).truthy())
                        {
                            if (self.attr("_decompressor").attr("eof").truthy())
                            {
                                rawblock = [&]() -> py { py const t = self.attr("_decompressor").attr("unused_data"); return t.truthy() ? t : self.attr("_fp").method("read", { G<"_compression.BUFFER_SIZE">() }); }();
                                if (!rawblock.truthy())
                                {
                                    break;
                                }
                                // Continue to next stream.
                                self.set_attr("_decompressor", call_ex(self.attr("_decomp_factory"), {  }, py{}, self.attr("_decomp_args")));
                                try
                                {
                                    data = self.attr("_decompressor").method("decompress", { rawblock, size });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler4;
                                    if (matches(handler4.exception, self.attr("_trailing_error")))
                                    {
                                        // Trailing data isn't a valid compressed stream; ignore it.
                                        break;
                                    }
                                    else
                                        py_reraise(handler4.exception);
                                }
                            }
                            else
                            {
                                if (self.attr("_decompressor").attr("needs_input").truthy())
                                {
                                    rawblock = self.attr("_fp").method("read", { G<"_compression.BUFFER_SIZE">() });
                                    if (!rawblock.truthy())
                                    {
                                        py_raise((B<"EOFError">())(S<"Compressed file ended before the end-of-stream marker was reached">()));
                                    }
                                }
                                else
                                {
                                    rawblock = py{ make_bytes(std::string{ "", 0 }) };
                                }
                                data = self.attr("_decompressor").method("decompress", { rawblock, size });
                            }
                            if (data.truthy())
                            {
                                break;
                            }
                        }
                        if (!data.truthy())
                        {
                            self.set_attr("_eof", boolean(true));
                            self.set_attr("_size", self.attr("_pos"));
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        {
                            py const target5 = self;
                            target5.set_attr("_pos", inplace(NEOS_PY_ADD, target5.attr("_pos"), py{ data.size() }));
                        }
                        return data;
                    }) },
                { "readall", w.method("DecompressReader", "readall", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py chunks;
                        py data;
                        chunks = py_list();
                        // sys.maxsize means the max length of output buffer is unlimited,
                        // so that the whole input buffer can be decompressed within one
                        // .decompress() call.
                        while (([&]() -> py { data = self.method("read", { Mod<"sys">().attr("maxsize") }); return data; }()).truthy())
                        {
                            chunks.method("append", { data });
                        }
                        return py{ make_bytes(std::string{ "", 0 }) }.method("join", { chunks });
                    }) },
                { "_rewind", w.method("DecompressReader", "_rewind", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_fp").method("seek", { I<0>() });
                        self.set_attr("_eof", boolean(false));
                        self.set_attr("_pos", I<0>());
                        self.set_attr("_decompressor", call_ex(self.attr("_decomp_factory"), {  }, py{}, self.attr("_decomp_args")));
                        return none();
                    }) },
                { "seek", w.method("DecompressReader", "seek", { py_positional("offset"), py_positional("whence", Mod<"io">().attr("SEEK_SET")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py offset = a[1];
                        py whence = a[2];
                        py data;
                        if (whence == (Mod<"io">().attr("SEEK_SET")))
                        {
                        }
                        else if (whence == (Mod<"io">().attr("SEEK_CUR")))
                        {
                            offset = self.attr("_pos") + offset;
                        }
                        else if (whence == (Mod<"io">().attr("SEEK_END")))
                        {
                            // Seeking relative to EOF - we need to know the file's size.
                            if (self.attr("_size") < (I<0>()))
                            {
                                while (self.method("read", { Mod<"io">().attr("DEFAULT_BUFFER_SIZE") }).truthy())
                                {
                                }
                            }
                            offset = self.attr("_size") + offset;
                        }
                        else
                        {
                            py_raise((B<"ValueError">())((S<"Invalid value for whence: {}">()).method("format", { whence })));
                        }
                        // Make it so that offset is the number of bytes to skip forward.
                        if (offset < self.attr("_pos"))
                        {
                            self.method("_rewind");
                        }
                        else
                        {
                            offset = inplace(NEOS_PY_SUBTRACT, offset, self.attr("_pos"));
                        }
                        // Read and discard data until we reach the desired position.
                        while (offset > (I<0>()))
                        {
                            data = self.method("read", { (B<"min">())(Mod<"io">().attr("DEFAULT_BUFFER_SIZE"), offset) });
                            if (!data.truthy())
                            {
                                break;
                            }
                            offset = inplace(NEOS_PY_SUBTRACT, offset, py{ data.size() });
                        }
                        return self.attr("_pos");
                    }) },
                { "tell", w.method("DecompressReader", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_pos");
                    }) }, }));
        }

        bool _compression_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make__compression();
                }
                if (auto const doc = compression_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("_compression", _compression_module);
    }
}
