/*
  quopri.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/quopri.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module quopri (CPython 3.13's Lib/quopri.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "quopri_docs.hpp"

namespace neos::python_runtime::detail::quopri_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "quopri.name"))
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
                py_raise("SystemError", std::string{ "quopri: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"quopri.name">()))
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
            static module_writer const sWriter{ "quopri", quopri_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- quopri (Lib/quopri.py)

        void make_quopri()
        {
            static module_writer const& w = writer("quopri");
            // ! /usr/bin/env python3
            // (Dec 1991 version).
            define("quopri", "__all__", py_list({ S<"encode">(), S<"decode">(), S<"encodestring">(), S<"decodestring">() }));
            define("quopri", "ESCAPE", py{ make_bytes(std::string{ "\x3d", 1 }) });
            define("quopri", "MAXLINESIZE", I<76>());
            define("quopri", "HEX", py{ make_bytes(std::string{ "\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x41\x42\x43\x44\x45\x46", 16 }) });
            define("quopri", "EMPTYSTRING", py{ make_bytes(std::string{ "", 0 }) });
            define("quopri", "a2b_qp", none());
            define("quopri", "b2a_qp", none());
            define("quopri", "needsquoting", w.function("needsquoting", "needsquoting", { py_positional("c"), py_positional("quotetabs"), py_positional("header") }, [](arguments a) -> py
                {
                    py c = a[0];
                    py quotetabs = a[1];
                    py header = a[2];
                    if (!isinstance(c, B<"bytes">()))
                        py_raise(B<"AssertionError">()());
                    if (py{ make_bytes(std::string{ "\x20\x09", 2 }) }.contains(c))
                    {
                        return quotetabs;
                    }
                    // if header, we have to escape _ because _ is used to escape space
                    if (c == py{ make_bytes(std::string{ "\x5f", 1 }) })
                    {
                        return header;
                    }
                    return [&]() -> py { py const t = boolean(c == (G<"quopri.ESCAPE">())); return t.truthy() ? t : boolean(!((py{ make_bytes(std::string{ "\x20", 1 }) } <= c) && (c <= py{ make_bytes(std::string{ "\x7e", 1 }) }))); }();
                }));

            define("quopri", "quote", w.function("quote", "quote", { py_positional("c") }, [](arguments a) -> py
                {
                    py c = a[0];
                    if (!([&]() -> py { py const t = boolean(isinstance(c, B<"bytes">())); return !t.truthy() ? t : boolean(py{ c.size() } == (I<1>())); }()).truthy())
                        py_raise(B<"AssertionError">()());
                    c = (B<"ord">())(c);
                    return (G<"quopri.ESCAPE">()) + (B<"bytes">())(py_tuple({ (G<"quopri.HEX">())[floordiv(c, (I<16>()))], (G<"quopri.HEX">())[c % (I<16>())] }));
                }));

            define("quopri", "encode", w.function("encode", "encode", { py_positional("input"), py_positional("output"), py_positional("quotetabs"), py_positional("header", boolean(false)) }, [](arguments a) -> py
                {
                    py input = a[0];
                    py output = a[1];
                    py quotetabs = a[2];
                    py header = a[3];
                    py c;
                    py data;
                    py line;
                    py odata;
                    py outline;
                    py prevline;
                    py stripped;
                    py thisline;
                    py write;
                    if (!(G<"quopri.b2a_qp">()).is_none())
                    {
                        data = input.method("read");
                        odata = (G<"quopri.b2a_qp">()).call({ data }, { { "quotetabs", quotetabs }, { "header", header } });
                        output.method("write", { odata });
                        return none();
                    }
                    write = w.function("write", "encode.<locals>.write", { py_positional("s"), py_positional("output", output), py_positional("lineEnd", py{ make_bytes(std::string{ "\x0a", 1 }) }) }, [](arguments a) -> py
    {
        py s = a[0];
        py output = a[1];
        py lineEnd = a[2];
        if ((s.truthy() && py{ make_bytes(std::string{ "\x20\x09", 2 }) }.contains(s[B<"slice">()(I<-1>(), none(), none())])))
        {
            output.method("write", { (s[B<"slice">()(none(), I<-1>(), none())] + (G<"quopri.quote">())(s[B<"slice">()(I<-1>(), none(), none())])) + lineEnd });
        }
        else if (s == py{ make_bytes(std::string{ "\x2e", 1 }) })
        {
            output.method("write", { (G<"quopri.quote">())(s) + lineEnd });
        }
        else
        {
            output.method("write", { s + lineEnd });
        }
        return none();
    });
                    prevline = none();
                    while (([&]() -> py { line = input.method("readline"); return line; }()).truthy())
                    {
                        outline = py_list();
                        // Strip off any readline induced trailing newline
                        stripped = py{ make_bytes(std::string{ "", 0 }) };
                        if (line[B<"slice">()(I<-1>(), none(), none())] == py{ make_bytes(std::string{ "\x0a", 1 }) })
                        {
                            line = line[B<"slice">()(none(), I<-1>(), none())];
                            stripped = py{ make_bytes(std::string{ "\x0a", 1 }) };
                        }
                        // Calculate the un-length-limited encoded line
                        for (py const& item : iterate{ line })
                        {
                            c = item;
                            c = (B<"bytes">())(py_tuple({ c }));
                            if ((G<"quopri.needsquoting">())(c, quotetabs, header).truthy())
                            {
                                c = (G<"quopri.quote">())(c);
                            }
                            if ((header.truthy() && (c == py{ make_bytes(std::string{ "\x20", 1 }) })))
                            {
                                outline.method("append", { py{ make_bytes(std::string{ "\x5f", 1 }) } });
                            }
                            else
                            {
                                outline.method("append", { c });
                            }
                        }
                        // First, write out the previous line
                        if (!prevline.is_none())
                        {
                            write(prevline);
                        }
                        // Now see if we need any soft line breaks because of RFC-imposed
                        // length limitations.  Then do the thisline->prevline dance.
                        thisline = (G<"quopri.EMPTYSTRING">()).method("join", { outline });
                        while (py{ thisline.size() } > (G<"quopri.MAXLINESIZE">()))
                        {
                            // Don't forget to include the soft line break `=' sign in the
                            // length calculation!
                            write.call({ thisline[B<"slice">()(none(), (G<"quopri.MAXLINESIZE">()) - (I<1>()), none())] }, { { "lineEnd", py{ make_bytes(std::string{ "\x3d\x0a", 2 }) } } });
                            thisline = thisline[B<"slice">()((G<"quopri.MAXLINESIZE">()) - (I<1>()), none(), none())];
                        }
                        // Write out the current line
                        prevline = thisline;
                    }
                    // Write out the last line, without a trailing newline
                    if (!prevline.is_none())
                    {
                        write.call({ prevline }, { { "lineEnd", stripped } });
                    }
                    return none();
                }));

            define("quopri", "encodestring", w.function("encodestring", "encodestring", { py_positional("s"), py_positional("quotetabs", boolean(false)), py_positional("header", boolean(false)) }, [](arguments a) -> py
                {
                    py s = a[0];
                    py quotetabs = a[1];
                    py header = a[2];
                    py BytesIO;
                    py infp;
                    py outfp;
                    if (!(G<"quopri.b2a_qp">()).is_none())
                    {
                        return (G<"quopri.b2a_qp">()).call({ s }, { { "quotetabs", quotetabs }, { "header", header } });
                    }
                    BytesIO = M("io", "BytesIO");
                    infp = BytesIO(s);
                    outfp = BytesIO();
                    (G<"quopri.encode">())(infp, outfp, quotetabs, header);
                    return outfp.method("getvalue");
                }));

            define("quopri", "decode", w.function("decode", "decode", { py_positional("input"), py_positional("output"), py_positional("header", boolean(false)) }, [](arguments a) -> py
                {
                    py input = a[0];
                    py output = a[1];
                    py header = a[2];
                    py c;
                    py data;
                    py i;
                    py line;
                    py n;
                    py new_;
                    py odata;
                    py partial;
                    if (!(G<"quopri.a2b_qp">()).is_none())
                    {
                        data = input.method("read");
                        odata = (G<"quopri.a2b_qp">()).call({ data }, { { "header", header } });
                        output.method("write", { odata });
                        return none();
                    }
                    new_ = py{ make_bytes(std::string{ "", 0 }) };
                    while (([&]() -> py { line = input.method("readline"); return line; }()).truthy())
                    {
                        auto const items5 = unpack(py_tuple({ I<0>(), py{ line.size() } }), 2);
                        i = items5[0];
                        n = items5[1];
                        if (((n > (I<0>())) && (line[B<"slice">()(n - (I<1>()), n, none())] == py{ make_bytes(std::string{ "\x0a", 1 }) })))
                        {
                            partial = I<0>();
                            n = n - (I<1>());
                            // Strip trailing whitespace
                            while (((n > (I<0>())) && py{ make_bytes(std::string{ "\x20\x09\x0d", 3 }) }.contains(line[B<"slice">()(n - (I<1>()), n, none())])))
                            {
                                n = n - (I<1>());
                            }
                        }
                        else
                        {
                            partial = I<1>();
                        }
                        while (i < n)
                        {
                            c = line[B<"slice">()(i, i + (I<1>()), none())];
                            if (((c == py{ make_bytes(std::string{ "\x5f", 1 }) }) && header.truthy()))
                            {
                                new_ = new_ + py{ make_bytes(std::string{ "\x20", 1 }) };
                                i = i + (I<1>());
                            }
                            else if (c != (G<"quopri.ESCAPE">()))
                            {
                                new_ = new_ + c;
                                i = i + (I<1>());
                            }
                            else if ((((i + (I<1>())) == n) && (!partial.truthy())))
                            {
                                partial = I<1>();
                                break;
                            }
                            else if ((((i + (I<1>())) < n) && (line[B<"slice">()(i + (I<1>()), i + (I<2>()), none())] == (G<"quopri.ESCAPE">()))))
                            {
                                new_ = new_ + (G<"quopri.ESCAPE">());
                                i = i + (I<2>());
                            }
                            else if ((((i + (I<2>())) < n) && (G<"quopri.ishex">())(line[B<"slice">()(i + (I<1>()), i + (I<2>()), none())]).truthy() && (G<"quopri.ishex">())(line[B<"slice">()(i + (I<2>()), i + (I<3>()), none())]).truthy()))
                            {
                                new_ = new_ + (B<"bytes">())(py_tuple({ (G<"quopri.unhex">())(line[B<"slice">()(i + (I<1>()), i + (I<3>()), none())]) }));
                                i = i + (I<3>());
                            }
                            else
                            {
                                new_ = new_ + c;
                                i = i + (I<1>());
                            }
                        }
                        if (!partial.truthy())
                        {
                            output.method("write", { new_ + py{ make_bytes(std::string{ "\x0a", 1 }) } });
                            new_ = py{ make_bytes(std::string{ "", 0 }) };
                        }
                    }
                    if (new_.truthy())
                    {
                        output.method("write", { new_ });
                    }
                    return none();
                }));

            define("quopri", "decodestring", w.function("decodestring", "decodestring", { py_positional("s"), py_positional("header", boolean(false)) }, [](arguments a) -> py
                {
                    py s = a[0];
                    py header = a[1];
                    py BytesIO;
                    py infp;
                    py outfp;
                    if (!(G<"quopri.a2b_qp">()).is_none())
                    {
                        return (G<"quopri.a2b_qp">()).call({ s }, { { "header", header } });
                    }
                    BytesIO = M("io", "BytesIO");
                    infp = BytesIO(s);
                    outfp = BytesIO();
                    (G<"quopri.decode">()).call({ infp, outfp }, { { "header", header } });
                    return outfp.method("getvalue");
                }));

            // Other helper functions
            define("quopri", "ishex", w.function("ishex", "ishex", { py_positional("c") }, [](arguments a) -> py
                {
                    py c = a[0];
                    if (!isinstance(c, B<"bytes">()))
                        py_raise(B<"AssertionError">()());
                    return [&]() -> py { py const t = boolean(((py{ make_bytes(std::string{ "\x30", 1 }) } <= c) && (c <= py{ make_bytes(std::string{ "\x39", 1 }) }))); return t.truthy() ? t : [&]() -> py { py const t = boolean(((py{ make_bytes(std::string{ "\x61", 1 }) } <= c) && (c <= py{ make_bytes(std::string{ "\x66", 1 }) }))); return t.truthy() ? t : boolean(((py{ make_bytes(std::string{ "\x41", 1 }) } <= c) && (c <= py{ make_bytes(std::string{ "\x46", 1 }) }))); }(); }();
                }));

            define("quopri", "unhex", w.function("unhex", "unhex", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py bits;
                    py c;
                    py i;
                    bits = I<0>();
                    for (py const& item : iterate{ s })
                    {
                        c = item;
                        c = (B<"bytes">())(py_tuple({ c }));
                        if ((py{ make_bytes(std::string{ "\x30", 1 }) } <= c) && (c <= py{ make_bytes(std::string{ "\x39", 1 }) }))
                        {
                            i = (B<"ord">())(S<"0">());
                        }
                        else if ((py{ make_bytes(std::string{ "\x61", 1 }) } <= c) && (c <= py{ make_bytes(std::string{ "\x66", 1 }) }))
                        {
                            i = (B<"ord">())(S<"a">()) - (I<10>());
                        }
                        else if ((py{ make_bytes(std::string{ "\x41", 1 }) } <= c) && (c <= py{ make_bytes(std::string{ "\x46", 1 }) }))
                        {
                            i = (B<"ord">())(py{ make_bytes(std::string{ "\x41", 1 }) }) - (I<10>());
                        }
                        else
                        {
                            if (!boolean(false).truthy())
                                py_raise(B<"AssertionError">()((S<"non-hex digit ">()) + S(c.repr())));
                        }
                        bits = (bits * (I<16>())) + ((B<"ord">())(c) - i);
                    }
                    return bits;
                }));

            define("quopri", "main", w.function("main", "main", {  }, [](arguments a) -> py
                {
                    py a_;
                    py args;
                    py deco;
                    py file;
                    py fp;
                    py getopt;
                    py msg;
                    py o;
                    py opts;
                    py sts;
                    py sys;
                    py tabs;
                    sys = module_of("sys");
                    getopt = module_of("getopt");
                    try
                    {
                        auto const items10 = unpack(getopt.method("getopt", { sys.attr("argv")[B<"slice">()(I<1>(), none(), none())], S<"td">() }), 2);
                        opts = items10[0];
                        args = items10[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler9;
                        if (matches(handler9.exception, getopt.attr("error")))
                        {
                            msg = handler9.exception;
                            sys.set_attr("stdout", sys.attr("stderr"));
                            (B<"print">())(msg);
                            (B<"print">())(S<"usage: quopri [-t | -d] [file] ...">());
                            (B<"print">())(S<"-t: quote tabs">());
                            (B<"print">())(S<"-d: decode; default encode">());
                            sys.method("exit", { I<2>() });
                        }
                        else
                            py_reraise(handler9.exception);
                    }
                    deco = boolean(false);
                    tabs = boolean(false);
                    for (py const& item : iterate{ opts })
                    {
                        auto const items12 = unpack(item, 2);
                        o = items12[0];
                        a_ = items12[1];
                        if (o == (S<"-t">()))
                        {
                            tabs = boolean(true);
                        }
                        if (o == (S<"-d">()))
                        {
                            deco = boolean(true);
                        }
                    }
                    if ((tabs.truthy() && deco.truthy()))
                    {
                        sys.set_attr("stdout", sys.attr("stderr"));
                        (B<"print">())(S<"-t and -d are mutually exclusive">());
                        sys.method("exit", { I<2>() });
                    }
                    if (!args.truthy())
                    {
                        args = py_list({ S<"-">() });
                    }
                    sts = I<0>();
                    for (py const& item : iterate{ args })
                    {
                        file = item;
                        if (file == (S<"-">()))
                        {
                            fp = sys.attr("stdin").attr("buffer");
                        }
                        else
                        {
                            try
                            {
                                fp = (B<"open">())(file, S<"rb">());
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler14;
                                if (matches(handler14.exception, B<"OSError">()))
                                {
                                    msg = handler14.exception;
                                    sys.attr("stderr").method("write", { (S<"%s: can't open (%s)\n">()) % py_tuple({ file, msg }) });
                                    sts = I<1>();
                                    continue;
                                }
                                else
                                    py_reraise(handler14.exception);
                            }
                        }
                        {
                            std::optional<py> pending15;
                            [[maybe_unused]] int flow15 = 0;
                            [[maybe_unused]] py result15;
                            try
                            {
                                if (deco.truthy())
                                {
                                    (G<"quopri.decode">())(fp, sys.attr("stdout").attr("buffer"));
                                }
                                else
                                {
                                    (G<"quopri.encode">())(fp, sys.attr("stdout").attr("buffer"), tabs);
                                }
                            }
                            catch (raised const&)
                            {
                                pending15 = py_take_exception();
                            }
                            finally15:
                            if (file != (S<"-">()))
                            {
                                fp.method("close");
                            }
                            if (pending15)
                                py_reraise(*pending15);
                            if (flow15 == 1)
                                return result15;
                            if (flow15 == 2)
                                break;
                            if (flow15 == 3)
                                continue;
                        }
                    }
                    if (sts.truthy())
                    {
                        sys.method("exit", { sts });
                    }
                    return none();
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool quopri_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_quopri();
                }
                if (auto const doc = quopri_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("quopri", quopri_module);
    }
}
