/*
  fileinput.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/fileinput.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module fileinput (CPython 3.13's Lib/fileinput.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "fileinput_docs.hpp"

namespace neos::python_runtime::detail::fileinput_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "fileinput.name"))
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
                py_raise("SystemError", std::string{ "fileinput: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"fileinput.name">()))
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
            static module_writer const sWriter{ "fileinput", fileinput_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- fileinput (Lib/fileinput.py)

        void make_fileinput()
        {
            static module_writer const& w = writer("fileinput");
            define("fileinput", "__all__", py_list({ S<"input">(), S<"close">(), S<"nextfile">(), S<"filename">(), S<"lineno">(), S<"filelineno">(), S<"fileno">(), S<"isfirstline">(), S<"isstdin">(), S<"FileInput">(), S<"hook_compressed">(), S<"hook_encoded">() }));
            define("fileinput", "_state", none());
            define("fileinput", "input", w.function("input", "input", { py_positional("files", none()), py_positional("inplace", boolean(false)), py_positional("backup", S<"">()), py_keyword("mode", S<"r">()), py_keyword("openhook", none()), py_keyword("encoding", none()), py_keyword("errors", none()) }, [](arguments a) -> py
                {
                    py files = a[0];
                    py inplace_ = a[1];
                    py backup = a[2];
                    py mode = a[3];
                    py openhook = a[4];
                    py encoding = a[5];
                    py errors = a[6];
                    if (((G<"fileinput._state">()).truthy() && (G<"fileinput._state">()).attr("_file").truthy()))
                    {
                        py_raise((B<"RuntimeError">())(S<"input() already active">()));
                    }
                    set_global("fileinput", "_state", (G<"fileinput.FileInput">()).call({ files, inplace_, backup }, { { "mode", mode }, { "openhook", openhook }, { "encoding", encoding }, { "errors", errors } }));
                    return G<"fileinput._state">();
                }));

            define("fileinput", "close", w.function("close", "close", {  }, [](arguments a) -> py
                {
                    py state;
                    state = G<"fileinput._state">();
                    set_global("fileinput", "_state", none());
                    if (state.truthy())
                    {
                        state.method("close");
                    }
                    return none();
                }));

            define("fileinput", "nextfile", w.function("nextfile", "nextfile", {  }, [](arguments a) -> py
                {
                    if (!(G<"fileinput._state">()).truthy())
                    {
                        py_raise((B<"RuntimeError">())(S<"no active input()">()));
                    }
                    return (G<"fileinput._state">()).method("nextfile");
                }));

            define("fileinput", "filename", w.function("filename", "filename", {  }, [](arguments a) -> py
                {
                    if (!(G<"fileinput._state">()).truthy())
                    {
                        py_raise((B<"RuntimeError">())(S<"no active input()">()));
                    }
                    return (G<"fileinput._state">()).method("filename");
                }));

            define("fileinput", "lineno", w.function("lineno", "lineno", {  }, [](arguments a) -> py
                {
                    if (!(G<"fileinput._state">()).truthy())
                    {
                        py_raise((B<"RuntimeError">())(S<"no active input()">()));
                    }
                    return (G<"fileinput._state">()).method("lineno");
                }));

            define("fileinput", "filelineno", w.function("filelineno", "filelineno", {  }, [](arguments a) -> py
                {
                    if (!(G<"fileinput._state">()).truthy())
                    {
                        py_raise((B<"RuntimeError">())(S<"no active input()">()));
                    }
                    return (G<"fileinput._state">()).method("filelineno");
                }));

            define("fileinput", "fileno", w.function("fileno", "fileno", {  }, [](arguments a) -> py
                {
                    if (!(G<"fileinput._state">()).truthy())
                    {
                        py_raise((B<"RuntimeError">())(S<"no active input()">()));
                    }
                    return (G<"fileinput._state">()).method("fileno");
                }));

            define("fileinput", "isfirstline", w.function("isfirstline", "isfirstline", {  }, [](arguments a) -> py
                {
                    if (!(G<"fileinput._state">()).truthy())
                    {
                        py_raise((B<"RuntimeError">())(S<"no active input()">()));
                    }
                    return (G<"fileinput._state">()).method("isfirstline");
                }));

            define("fileinput", "isstdin", w.function("isstdin", "isstdin", {  }, [](arguments a) -> py
                {
                    if (!(G<"fileinput._state">()).truthy())
                    {
                        py_raise((B<"RuntimeError">())(S<"no active input()">()));
                    }
                    return (G<"fileinput._state">()).method("isstdin");
                }));

                // repeat with next file
                // repeat with next file
            define("fileinput", "FileInput", w.make_class("FileInput", {  }, {
                { "__init__", w.method("FileInput", "__init__", { py_positional("files", none()), py_positional("inplace", boolean(false)), py_positional("backup", S<"">()), py_keyword("mode", S<"r">()), py_keyword("openhook", none()), py_keyword("encoding", none()), py_keyword("errors", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py files = a[1];
                        py inplace_ = a[2];
                        py backup = a[3];
                        py mode = a[4];
                        py openhook = a[5];
                        py encoding = a[6];
                        py errors = a[7];
                        py warnings;
                        if (isinstance(files, B<"str">()))
                        {
                            files = py_tuple({ files });
                        }
                        else if (isinstance(files, Mod<"os">().attr("PathLike")))
                        {
                            files = py_tuple({ (Mod<"os">().attr("fspath"))(files) });
                        }
                        else
                        {
                            if (files.is_none())
                            {
                                files = (Mod<"sys">().attr("argv"))[B<"slice">()(I<1>(), none(), none())];
                            }
                            if (!files.truthy())
                            {
                                files = py_tuple({ S<"-">() });
                            }
                            else
                            {
                                files = (B<"tuple">())(files);
                            }
                        }
                        self.set_attr("_files", files);
                        self.set_attr("_inplace", inplace_);
                        self.set_attr("_backup", backup);
                        self.set_attr("_savestdout", none());
                        self.set_attr("_output", none());
                        self.set_attr("_filename", none());
                        self.set_attr("_startlineno", I<0>());
                        self.set_attr("_filelineno", I<0>());
                        self.set_attr("_file", none());
                        self.set_attr("_isstdin", boolean(false));
                        self.set_attr("_backupfilename", none());
                        self.set_attr("_encoding", encoding);
                        self.set_attr("_errors", errors);
                        // We can not use io.text_encoding() here because old openhook doesn't
                        // take encoding parameter.
                        if (((Mod<"sys">().attr("flags")).attr("warn_default_encoding").truthy() && (!mode.contains((S<"b">()))) && encoding.is_none() && openhook.is_none()))
                        {
                            warnings = module_of("warnings");
                            warnings.method("warn", { S<"'encoding' argument not specified.">(), B<"EncodingWarning">(), I<2>() });
                        }
                        // restrict mode argument to reading modes
                        if (!py_tuple({ S<"r">(), S<"rb">() }).contains(mode))
                        {
                            py_raise((B<"ValueError">())(S<"FileInput opening mode must be 'r' or 'rb'">()));
                        }
                        self.set_attr("_mode", mode);
                        self.set_attr("_write_mode", mode.method("replace", { S<"r">(), S<"w">() }));
                        if (openhook.truthy())
                        {
                            if (inplace_.truthy())
                            {
                                py_raise((B<"ValueError">())(S<"FileInput cannot use an opening hook in inplace mode">()));
                            }
                            if (!callable(openhook))
                            {
                                py_raise((B<"ValueError">())(S<"FileInput openhook must be callable">()));
                            }
                        }
                        self.set_attr("_openhook", openhook);
                        return none();
                    }) },
                { "__del__", w.method("FileInput", "__del__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("close");
                        return none();
                    }) },
                { "close", w.method("FileInput", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            std::optional<py> pending1;
                            [[maybe_unused]] int flow1 = 0;
                            [[maybe_unused]] py result1;
                            try
                            {
                                self.method("nextfile");
                            }
                            catch (raised const&)
                            {
                                pending1 = py_take_exception();
                            }
                            self.set_attr("_files", py_tuple());
                            if (pending1)
                                py_reraise(*pending1);
                            if (flow1 == 1)
                                return result1;
                        }
                        return none();
                    }) },
                { "__enter__", w.method("FileInput", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("FileInput", "__exit__", { py_positional("type"), py_positional("value"), py_positional("traceback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py type = a[1];
                        py value = a[2];
                        py traceback = a[3];
                        self.method("close");
                        return none();
                    }) },
                { "__iter__", w.method("FileInput", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__next__", w.method("FileInput", "__next__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line;
                        while (boolean(true).truthy())
                        {
                            line = self.method("_readline");
                            if (line.truthy())
                            {
                                {
                                    py const target3 = self;
                                    target3.set_attr("_filelineno", inplace(NEOS_PY_ADD, target3.attr("_filelineno"), I<1>()));
                                }
                                return line;
                            }
                            if (!self.attr("_file").truthy())
                            {
                                py_raise(B<"StopIteration">());
                            }
                            self.method("nextfile");
                        }
                        return none();
                    }) },
                { "nextfile", w.method("FileInput", "nextfile", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py backupfilename;
                        py file;
                        py output;
                        py savestdout;
                        savestdout = self.attr("_savestdout");
                        self.set_attr("_savestdout", none());
                        if (savestdout.truthy())
                        {
                            (Mod<"sys">()).set_attr("stdout", savestdout);
                        }
                        output = self.attr("_output");
                        self.set_attr("_output", none());
                        {
                            std::optional<py> pending4;
                            [[maybe_unused]] int flow4 = 0;
                            [[maybe_unused]] py result4;
                            try
                            {
                                if (output.truthy())
                                {
                                    output.method("close");
                                }
                            }
                            catch (raised const&)
                            {
                                pending4 = py_take_exception();
                            }
                            file = self.attr("_file");
                            self.set_attr("_file", none());
                            try
                            {
                                del_attr(self, "_readline");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler5;
                                if (matches(handler5.exception, B<"AttributeError">()))
                                {
                                }
                                else
                                    py_reraise(handler5.exception);
                            }
                            {
                                std::optional<py> pending6;
                                [[maybe_unused]] int flow6 = 0;
                                [[maybe_unused]] py result6;
                                try
                                {
                                    if ((file.truthy() && (!self.attr("_isstdin").truthy())))
                                    {
                                        file.method("close");
                                    }
                                }
                                catch (raised const&)
                                {
                                    pending6 = py_take_exception();
                                }
                                backupfilename = self.attr("_backupfilename");
                                self.set_attr("_backupfilename", none());
                                if ((backupfilename.truthy() && (!self.attr("_backup").truthy())))
                                {
                                    try
                                    {
                                        (Mod<"os">().attr("unlink"))(backupfilename);
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler7;
                                        if (matches(handler7.exception, B<"OSError">()))
                                        {
                                        }
                                        else
                                            py_reraise(handler7.exception);
                                    }
                                }
                                self.set_attr("_isstdin", boolean(false));
                                if (pending6)
                                    py_reraise(*pending6);
                                if (flow6 == 1)
                                    return result6;
                            }
                            if (pending4)
                                py_reraise(*pending4);
                            if (flow4 == 1)
                                return result4;
                        }
                        return none();
                    }) },
                { "readline", w.method("FileInput", "readline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line;
                        while (boolean(true).truthy())
                        {
                            line = self.method("_readline");
                            if (line.truthy())
                            {
                                {
                                    py const target9 = self;
                                    target9.set_attr("_filelineno", inplace(NEOS_PY_ADD, target9.attr("_filelineno"), I<1>()));
                                }
                                return line;
                            }
                            if (!self.attr("_file").truthy())
                            {
                                return line;
                            }
                            self.method("nextfile");
                        }
                        return none();
                    }) },
                { "_readline", w.method("FileInput", "_readline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py encoding;
                        py fd;
                        py mode;
                        py perm;
                        if (!self.attr("_files").truthy())
                        {
                            if (self.attr("_mode").contains((S<"b">())))
                            {
                                return py{ make_bytes(std::string{ "", 0 }) };
                            }
                            else
                            {
                                return S<"">();
                            }
                        }
                        self.set_attr("_filename", self.attr("_files")[I<0>()]);
                        self.set_attr("_files", self.attr("_files")[B<"slice">()(I<1>(), none(), none())]);
                        self.set_attr("_startlineno", self.method("lineno"));
                        self.set_attr("_filelineno", I<0>());
                        self.set_attr("_file", none());
                        self.set_attr("_isstdin", boolean(false));
                        self.set_attr("_backupfilename", I<0>());
                        // EncodingWarning is emitted in __init__() already
                        if (!self.attr("_mode").contains((S<"b">())))
                        {
                            encoding = [&]() -> py { py const t = self.attr("_encoding"); return t.truthy() ? t : S<"locale">(); }();
                        }
                        else
                        {
                            encoding = none();
                        }
                        if (self.attr("_filename") == (S<"-">()))
                        {
                            self.set_attr("_filename", S<"<stdin>">());
                            if (self.attr("_mode").contains((S<"b">())))
                            {
                                self.set_attr("_file", (B<"getattr">())(Mod<"sys">().attr("stdin"), S<"buffer">(), Mod<"sys">().attr("stdin")));
                            }
                            else
                            {
                                self.set_attr("_file", Mod<"sys">().attr("stdin"));
                            }
                            self.set_attr("_isstdin", boolean(true));
                        }
                        else if (self.attr("_inplace").truthy())
                        {
                            self.set_attr("_backupfilename", (Mod<"os">().attr("fspath"))(self.attr("_filename")) + ([&]() -> py { py const t = self.attr("_backup"); return t.truthy() ? t : S<".bak">(); }()));
                            try
                            {
                                (Mod<"os">().attr("unlink"))(self.attr("_backupfilename"));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler10;
                                if (matches(handler10.exception, B<"OSError">()))
                                {
                                }
                                else
                                    py_reraise(handler10.exception);
                            }
                            // The next few lines may raise OSError
                            (Mod<"os">().attr("rename"))(self.attr("_filename"), self.attr("_backupfilename"));
                            self.set_attr("_file", (B<"open">()).call({ self.attr("_backupfilename"), self.attr("_mode") }, { { "encoding", encoding }, { "errors", self.attr("_errors") } }));
                            {
                                bool otherwise11 = false;
                                try
                                {
                                    perm = (Mod<"os">().attr("fstat"))(self.attr("_file").method("fileno")).attr("st_mode");
                                    otherwise11 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler11;
                                    if (matches(handler11.exception, B<"OSError">()))
                                    {
                                        self.set_attr("_output", (B<"open">()).call({ self.attr("_filename"), self.attr("_write_mode") }, { { "encoding", encoding }, { "errors", self.attr("_errors") } }));
                                    }
                                    else
                                        py_reraise(handler11.exception);
                                }
                                if (otherwise11)
                                {
                                    mode = ((Mod<"os">().attr("O_CREAT")) | (Mod<"os">().attr("O_WRONLY"))) | (Mod<"os">().attr("O_TRUNC"));
                                    if (has_attr(Mod<"os">(), "O_BINARY"))
                                    {
                                        mode = inplace(NEOS_PY_OR, mode, Mod<"os">().attr("O_BINARY"));
                                    }
                                    fd = (Mod<"os">().attr("open"))(self.attr("_filename"), mode, perm);
                                    self.set_attr("_output", (Mod<"os">().attr("fdopen")).call({ fd, self.attr("_write_mode") }, { { "encoding", encoding }, { "errors", self.attr("_errors") } }));
                                    try
                                    {
                                        (Mod<"os">().attr("chmod"))(self.attr("_filename"), perm);
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler12;
                                        if (matches(handler12.exception, B<"OSError">()))
                                        {
                                        }
                                        else
                                            py_reraise(handler12.exception);
                                    }
                                }
                            }
                            self.set_attr("_savestdout", Mod<"sys">().attr("stdout"));
                            (Mod<"sys">()).set_attr("stdout", self.attr("_output"));
                        }
                        // This may raise OSError
                        else if (self.attr("_openhook").truthy())
                        {
                            // Custom hooks made previous to Python 3.10 didn't have
                            // encoding argument
                            if (self.attr("_encoding").is_none())
                            {
                                self.set_attr("_file", self.method("_openhook", { self.attr("_filename"), self.attr("_mode") }));
                            }
                            else
                            {
                                self.set_attr("_file", self.method("_openhook", { self.attr("_filename"), self.attr("_mode") }, { { "encoding", self.attr("_encoding") }, { "errors", self.attr("_errors") } }));
                            }
                        }
                        else
                        {
                            self.set_attr("_file", (B<"open">()).call({ self.attr("_filename"), self.attr("_mode") }, { { "encoding", encoding }, { "errors", self.attr("_errors") } }));
                        }
                        self.set_attr("_readline", self.attr("_file").attr("readline"));
                        return self.method("_readline");
                    }) },
                { "filename", w.method("FileInput", "filename", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_filename");
                    }) },
                { "lineno", w.method("FileInput", "lineno", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_startlineno") + self.attr("_filelineno");
                    }) },
                { "filelineno", w.method("FileInput", "filelineno", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_filelineno");
                    }) },
                { "fileno", w.method("FileInput", "fileno", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_file").truthy())
                        {
                            try
                            {
                                return self.attr("_file").method("fileno");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler13;
                                if (matches(handler13.exception, B<"ValueError">()))
                                {
                                    return I<-1>();
                                }
                                else
                                    py_reraise(handler13.exception);
                            }
                        }
                        else
                        {
                            return I<-1>();
                        }
                        return none();
                    }) },
                { "isfirstline", w.method("FileInput", "isfirstline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("_filelineno") == (I<1>()));
                    }) },
                { "isstdin", w.method("FileInput", "isstdin", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_isstdin");
                    }) },
                { "__class_getitem__", (B<"classmethod">())(Mod<"types">().attr("GenericAlias")) }, }));

            define("fileinput", "hook_compressed", w.function("hook_compressed", "hook_compressed", { py_positional("filename"), py_positional("mode"), py_keyword("encoding", none()), py_keyword("errors", none()) }, [](arguments a) -> py
                {
                    py filename = a[0];
                    py mode = a[1];
                    py encoding = a[2];
                    py errors = a[3];
                    py bz2;
                    py ext;
                    py gzip;
                    py stream;
                    if ((encoding.is_none() && (!mode.contains((S<"b">())))))
                    {
                        encoding = S<"locale">();
                    }
                    ext = (Mod<"os">().attr("path")).method("splitext", { filename })[I<1>()];
                    if (ext == (S<".gz">()))
                    {
                        gzip = module_of("gzip");
                        stream = gzip.method("open", { filename, mode });
                    }
                    else if (ext == (S<".bz2">()))
                    {
                        bz2 = module_of("bz2");
                        stream = bz2.method("BZ2File", { filename, mode });
                    }
                    else
                    {
                        return (B<"open">()).call({ filename, mode }, { { "encoding", encoding }, { "errors", errors } });
                    }
                    // gzip and bz2 are binary mode by default.
                    if (!mode.contains((S<"b">())))
                    {
                        stream = (Mod<"io">().attr("TextIOWrapper")).call({ stream }, { { "encoding", encoding }, { "errors", errors } });
                    }
                    return stream;
                }));

            define("fileinput", "hook_encoded", w.function("hook_encoded", "hook_encoded", { py_positional("encoding"), py_positional("errors", none()) }, [](arguments a) -> py
                {
                    auto const encoding = std::make_shared<py>(a[0]);
                    auto const errors = std::make_shared<py>(a[1]);
                    py openhook;
                    openhook = w.function("openhook", "hook_encoded.<locals>.openhook", { py_positional("filename"), py_positional("mode") }, [encoding, errors](arguments a) -> py
    {
        py filename = a[0];
        py mode = a[1];
        return (B<"open">()).call({ filename, mode }, { { "encoding", (*encoding) }, { "errors", (*errors) } });
    });
                    return openhook;
                }));

            define("fileinput", "_test", w.function("_test", "_test", {  }, [](arguments a) -> py
                {
                    py a_;
                    py args;
                    py backup;
                    py getopt;
                    py inplace_;
                    py line;
                    py o;
                    py opts;
                    getopt = module_of("getopt");
                    inplace_ = boolean(false);
                    backup = boolean(false);
                    auto const items14 = unpack(getopt.method("getopt", { (Mod<"sys">().attr("argv"))[B<"slice">()(I<1>(), none(), none())], S<"ib:">() }), 2);
                    opts = items14[0];
                    args = items14[1];
                    for (py const& item : iterate{ opts })
                    {
                        auto const items16 = unpack(item, 2);
                        o = items16[0];
                        a_ = items16[1];
                        if (o == (S<"-i">()))
                        {
                            inplace_ = boolean(true);
                        }
                        if (o == (S<"-b">()))
                        {
                            backup = a_;
                        }
                    }
                    for (py const& item : iterate{ (G<"fileinput.input">()).call({ args }, { { "inplace", inplace_ }, { "backup", backup } }) })
                    {
                        line = item;
                        if (line[B<"slice">()(I<-1>(), none(), none())] == (S<"\n">()))
                        {
                            line = line[B<"slice">()(none(), I<-1>(), none())];
                        }
                        if (line[B<"slice">()(I<-1>(), none(), none())] == (S<"\r">()))
                        {
                            line = line[B<"slice">()(none(), I<-1>(), none())];
                        }
                        (B<"print">())((S<"%d: %s[%d]%s %s">()) % py_tuple({ (G<"fileinput.lineno">())(), (G<"fileinput.filename">())(), (G<"fileinput.filelineno">())(), [&]() -> py { py const t = [&]() -> py { py const t = (G<"fileinput.isfirstline">())(); return !t.truthy() ? t : S<"*">(); }(); return t.truthy() ? t : S<"">(); }(), line }));
                    }
                    (B<"print">())((S<"%d: %s[%d]">()) % py_tuple({ (G<"fileinput.lineno">())(), (G<"fileinput.filename">())(), (G<"fileinput.filelineno">())() }));
                    return none();
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool fileinput_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_fileinput();
                }
                if (auto const doc = fileinput_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("GenericAlias", import_attribute("types", "GenericAlias").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("fileinput", fileinput_module);
    }
}
