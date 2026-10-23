/*
  optparse.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/optparse.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/optparse.py's copyright: Copyright (c) 2001-2006 Gregory P. Ward. All
  rights reserved. Copyright (c) 2002-2006 Python Software Foundation. All rights reserved. (BSD license: see
  runtime/python/NOTICE).
*/

// The module optparse (CPython 3.13's Lib/optparse.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "optparse_docs.hpp"

namespace neos::python_runtime::detail::optparse_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "optparse.name"))
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
                py_raise("SystemError", std::string{ "optparse: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"optparse.name">()))
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
            if (modules_imported("optparse"))
                return py{ fn_import("optparse") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "optparse" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "optparse", optparse_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- optparse (Lib/optparse.py)

        void make_optparse()
        {
            static module_writer const& w = writer("optparse");
            define("optparse", "__version__", S<"1.5.3">());
            define("optparse", "__all__", py_list({ S<"Option">(), S<"make_option">(), S<"SUPPRESS_HELP">(), S<"SUPPRESS_USAGE">(), S<"Values">(), S<"OptionContainer">(), S<"OptionGroup">(), S<"OptionParser">(), S<"HelpFormatter">(), S<"IndentedHelpFormatter">(), S<"TitledHelpFormatter">(), S<"OptParseError">(), S<"OptionError">(), S<"OptionConflictError">(), S<"OptionValueError">(), S<"BadOptionError">(), S<"check_choice">() }));
            define("optparse", "__copyright__", S<"\nCopyright (c) 2001-2006 Gregory P. Ward.  All rights reserved.\nCopyright (c) 2002-2006 Python Software Foundation.  All rights reserved.\n\nRedistribution and use in source and binary forms, with or without\nmodification, are permitted provided that the following conditions are\nmet:\n\n  * Redistributions of source code must retain the above copyright\n    notice, this list of conditions and the following disclaimer.\n\n  * Redistributions in binary form must reproduce the above copyright\n    notice, this list of conditions and the following disclaimer in the\n    documentation and/or other materials provided with the distribution.\n\n  * Neither the name of the author nor the names of its\n    contributors may be used to endorse or promote products derived from\n    this software without specific prior written permission.\n\nTHIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS\nIS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED\nTO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A\nPARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR\nCONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,\nEXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,\nPROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR\nPROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF\nLIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING\nNEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS\nSOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n">());
            define("optparse", "_repr", w.function("_repr", "_repr", { py_positional("self") }, [](arguments a) -> py
                {
                    py self = a[0];
                    return (S<"<%s at 0x%x: %s>">()) % py_tuple({ self.attr("__class__").attr("__name__"), (B<"id">())(self), self });
                }));

            // This file was generated from:
            // Id: option_parser.py 527 2006-07-23 15:21:30Z greg
            // Id: option.py 522 2006-06-11 16:22:03Z gward
            // Id: help.py 527 2006-07-23 15:21:30Z greg
            // Id: errors.py 509 2006-04-20 00:58:24Z gward
            define("optparse", "_", Mod<"gettext">().attr("gettext"));
            define("optparse", "OptParseError", w.make_class("OptParseError", { B<"Exception">() }, {
                { "__init__", w.method("OptParseError", "__init__", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        self.set_attr("msg", msg);
                        return none();
                    }) },
                { "__str__", w.method("OptParseError", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("msg");
                    }) }, }));

            define("optparse", "OptionError", w.make_class("OptionError", { G<"optparse.OptParseError">() }, {
                { "__init__", w.method("OptionError", "__init__", { py_positional("msg"), py_positional("option") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py option = a[2];
                        self.set_attr("msg", msg);
                        self.set_attr("option_id", (B<"str">())(option));
                        return none();
                    }) },
                { "__str__", w.method("OptionError", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("option_id").truthy())
                        {
                            return (S<"option %s: %s">()) % py_tuple({ self.attr("option_id"), self.attr("msg") });
                        }
                        else
                        {
                            return self.attr("msg");
                        }
                        return none();
                    }) }, }));

            define("optparse", "OptionConflictError", w.make_class("OptionConflictError", { G<"optparse.OptionError">() }, {
 }));

            define("optparse", "OptionValueError", w.make_class("OptionValueError", { G<"optparse.OptParseError">() }, {
 }));

            define("optparse", "BadOptionError", w.make_class("BadOptionError", { G<"optparse.OptParseError">() }, {
                { "__init__", w.method("BadOptionError", "__init__", { py_positional("opt_str") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opt_str = a[1];
                        self.set_attr("opt_str", opt_str);
                        return none();
                    }) },
                { "__str__", w.method("BadOptionError", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"optparse._">())(S<"no such option: %s">()) % self.attr("opt_str");
                    }) }, }));

            define("optparse", "AmbiguousOptionError", w.make_class("AmbiguousOptionError", { G<"optparse.BadOptionError">() }, {
                { "__init__", w.method("AmbiguousOptionError", "__init__", { py_positional("opt_str"), py_positional("possibilities") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opt_str = a[1];
                        py possibilities = a[2];
                        (G<"optparse.BadOptionError">()).method("__init__", { self, opt_str });
                        self.set_attr("possibilities", possibilities);
                        return none();
                    }) },
                { "__str__", w.method("AmbiguousOptionError", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"optparse._">())(S<"ambiguous option: %s (%s?)">()) % py_tuple({ self.attr("opt_str"), (S<", ">()).method("join", { self.attr("possibilities") }) });
                    }) }, }));

            define("optparse", "HelpFormatter", w.make_class("HelpFormatter", {  }, {
                { "NO_DEFAULT_VALUE", S<"none">() },
                { "__init__", w.method("HelpFormatter", "__init__", { py_positional("indent_increment"), py_positional("max_help_position"), py_positional("width"), py_positional("short_first") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py indent_increment = a[1];
                        py max_help_position = a[2];
                        py width = a[3];
                        py short_first = a[4];
                        self.set_attr("parser", none());
                        self.set_attr("indent_increment", indent_increment);
                        if (width.is_none())
                        {
                            try
                            {
                                width = (B<"int">())((Mod<"os">().attr("environ"))[S<"COLUMNS">()]);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler1;
                                if (matches(handler1.exception, py_tuple({ B<"KeyError">(), B<"ValueError">() })))
                                {
                                    width = I<80>();
                                }
                                else
                                    py_reraise(handler1.exception);
                            }
                            width = inplace(NEOS_PY_SUBTRACT, width, I<2>());
                        }
                        self.set_attr("width", width);
                        py const value2 = (B<"min">())(max_help_position, (B<"max">())(width - (I<20>()), indent_increment * (I<2>())));
                        self.set_attr("help_position", value2);
                        self.set_attr("max_help_position", value2);
                        self.set_attr("current_indent", I<0>());
                        self.set_attr("level", I<0>());
                        self.set_attr("help_width", none());
                        self.set_attr("short_first", short_first);
                        self.set_attr("default_tag", S<"%default">());
                        self.set_attr("option_strings", py_dict());
                        self.set_attr("_short_opt_fmt", S<"%s %s">());
                        self.set_attr("_long_opt_fmt", S<"%s=%s">());
                        return none();
                    }) },
                { "set_parser", w.method("HelpFormatter", "set_parser", { py_positional("parser") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser = a[1];
                        self.set_attr("parser", parser);
                        return none();
                    }) },
                { "set_short_opt_delimiter", w.method("HelpFormatter", "set_short_opt_delimiter", { py_positional("delim") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py delim = a[1];
                        if (!py_tuple({ S<"">(), S<" ">() }).contains(delim))
                        {
                            py_raise((B<"ValueError">())((S<"invalid metavar delimiter for short options: %r">()) % delim));
                        }
                        self.set_attr("_short_opt_fmt", ((S<"%s">()) + delim) + (S<"%s">()));
                        return none();
                    }) },
                { "set_long_opt_delimiter", w.method("HelpFormatter", "set_long_opt_delimiter", { py_positional("delim") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py delim = a[1];
                        if (!py_tuple({ S<"=">(), S<" ">() }).contains(delim))
                        {
                            py_raise((B<"ValueError">())((S<"invalid metavar delimiter for long options: %r">()) % delim));
                        }
                        self.set_attr("_long_opt_fmt", ((S<"%s">()) + delim) + (S<"%s">()));
                        return none();
                    }) },
                { "indent", w.method("HelpFormatter", "indent", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            py const target3 = self;
                            target3.set_attr("current_indent", inplace(NEOS_PY_ADD, target3.attr("current_indent"), self.attr("indent_increment")));
                        }
                        {
                            py const target4 = self;
                            target4.set_attr("level", inplace(NEOS_PY_ADD, target4.attr("level"), I<1>()));
                        }
                        return none();
                    }) },
                { "dedent", w.method("HelpFormatter", "dedent", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            py const target5 = self;
                            target5.set_attr("current_indent", inplace(NEOS_PY_SUBTRACT, target5.attr("current_indent"), self.attr("indent_increment")));
                        }
                        if (!(self.attr("current_indent") >= (I<0>())))
                            py_raise(B<"AssertionError">()(S<"Indent decreased below 0.">()));
                        {
                            py const target6 = self;
                            target6.set_attr("level", inplace(NEOS_PY_SUBTRACT, target6.attr("level"), I<1>()));
                        }
                        return none();
                    }) },
                { "format_usage", w.method("HelpFormatter", "format_usage", { py_positional("usage") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py usage = a[1];
                        py_raise((B<"NotImplementedError">())(S<"subclasses must implement">()));
                    }) },
                { "format_heading", w.method("HelpFormatter", "format_heading", { py_positional("heading") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py heading = a[1];
                        py_raise((B<"NotImplementedError">())(S<"subclasses must implement">()));
                    }) },
                { "_format_text", w.method("HelpFormatter", "_format_text", { py_positional("text") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py indent;
                        py text_width;
                        text_width = (B<"max">())(self.attr("width") - self.attr("current_indent"), I<11>());
                        indent = (S<" ">()) * self.attr("current_indent");
                        return (Mod<"textwrap">().attr("fill")).call({ text, text_width }, { { "initial_indent", indent }, { "subsequent_indent", indent } });
                    }) },
                { "format_description", w.method("HelpFormatter", "format_description", { py_positional("description") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py description = a[1];
                        if (description.truthy())
                        {
                            return self.method("_format_text", { description }) + (S<"\n">());
                        }
                        else
                        {
                            return S<"">();
                        }
                        return none();
                    }) },
                { "format_epilog", w.method("HelpFormatter", "format_epilog", { py_positional("epilog") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py epilog = a[1];
                        if (epilog.truthy())
                        {
                            return ((S<"\n">()) + self.method("_format_text", { epilog })) + (S<"\n">());
                        }
                        else
                        {
                            return S<"">();
                        }
                        return none();
                    }) },
                { "expand_default", w.method("HelpFormatter", "expand_default", { py_positional("option") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py option = a[1];
                        py default_value;
                        if ((self.attr("parser").is_none() || (!self.attr("default_tag").truthy())))
                        {
                            return option.attr("help");
                        }
                        default_value = self.attr("parser").attr("defaults").method("get", { option.attr("dest") });
                        if ((default_value.is((G<"optparse.NO_DEFAULT">())) || default_value.is_none()))
                        {
                            default_value = self.attr("NO_DEFAULT_VALUE");
                        }
                        return option.attr("help").method("replace", { self.attr("default_tag"), (B<"str">())(default_value) });
                    }) },
                { "format_option", w.method("HelpFormatter", "format_option", { py_positional("option") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py option = a[1];
                        py help_lines;
                        py help_text;
                        py indent_first;
                        py opt_width;
                        py opts;
                        py result;
                        result = py_list();
                        opts = self.attr("option_strings")[option];
                        opt_width = (self.attr("help_position") - self.attr("current_indent")) - (I<2>());
                        if (py{ opts.size() } > opt_width)
                        {
                            opts = (S<"%*s%s\n">()) % py_tuple({ self.attr("current_indent"), S<"">(), opts });
                            indent_first = self.attr("help_position");
                        }
                        else
                        {
                            opts = (S<"%*s%-*s  ">()) % py_tuple({ self.attr("current_indent"), S<"">(), opt_width, opts });
                            indent_first = I<0>();
                        }
                        result.method("append", { opts });
                        if (option.attr("help").truthy())
                        {
                            help_text = self.method("expand_default", { option });
                            help_lines = (Mod<"textwrap">().attr("wrap"))(help_text, self.attr("help_width"));
                            result.method("append", { (S<"%*s%s\n">()) % py_tuple({ indent_first, S<"">(), help_lines[I<0>()] }) });
                            result.method("extend", { [&]() -> py { py const comprehension7 = py_list(); py line; for (py const& item : iterate{ help_lines[B<"slice">()(I<1>(), none(), none())] }) { line = item; comprehension7.method("append", { (S<"%*s%s\n">()) % py_tuple({ self.attr("help_position"), S<"">(), line }) }); } return comprehension7; }() });
                        }
                        else if (opts[I<-1>()] != (S<"\n">()))
                        {
                            result.method("append", { S<"\n">() });
                        }
                        return (S<"">()).method("join", { result });
                    }) },
                { "store_option_strings", w.method("HelpFormatter", "store_option_strings", { py_positional("parser") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser = a[1];
                        py group;
                        py max_len;
                        py opt;
                        py strings;
                        self.method("indent");
                        max_len = I<0>();
                        for (py const& item : iterate{ parser.attr("option_list") })
                        {
                            opt = item;
                            strings = self.method("format_option_strings", { opt });
                            self.attr("option_strings").set_item(opt, strings);
                            max_len = (B<"max">())(max_len, py{ strings.size() } + self.attr("current_indent"));
                        }
                        self.method("indent");
                        for (py const& item : iterate{ parser.attr("option_groups") })
                        {
                            group = item;
                            for (py const& item : iterate{ group.attr("option_list") })
                            {
                                opt = item;
                                strings = self.method("format_option_strings", { opt });
                                self.attr("option_strings").set_item(opt, strings);
                                max_len = (B<"max">())(max_len, py{ strings.size() } + self.attr("current_indent"));
                            }
                        }
                        self.method("dedent");
                        self.method("dedent");
                        self.set_attr("help_position", (B<"min">())(max_len + (I<2>()), self.attr("max_help_position")));
                        self.set_attr("help_width", (B<"max">())(self.attr("width") - self.attr("help_position"), I<11>()));
                        return none();
                    }) },
                { "format_option_strings", w.method("HelpFormatter", "format_option_strings", { py_positional("option") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py option = a[1];
                        py long_opts;
                        py metavar;
                        py opts;
                        py short_opts;
                        if (option.method("takes_value").truthy())
                        {
                            metavar = [&]() -> py { py const t = option.attr("metavar"); return t.truthy() ? t : option.attr("dest").method("upper"); }();
                            short_opts = [&]() -> py { py const comprehension11 = py_list(); py sopt; for (py const& item : iterate{ option.attr("_short_opts") }) { sopt = item; comprehension11.method("append", { self.attr("_short_opt_fmt") % py_tuple({ sopt, metavar }) }); } return comprehension11; }();
                            long_opts = [&]() -> py { py const comprehension12 = py_list(); py lopt; for (py const& item : iterate{ option.attr("_long_opts") }) { lopt = item; comprehension12.method("append", { self.attr("_long_opt_fmt") % py_tuple({ lopt, metavar }) }); } return comprehension12; }();
                        }
                        else
                        {
                            short_opts = option.attr("_short_opts");
                            long_opts = option.attr("_long_opts");
                        }
                        if (self.attr("short_first").truthy())
                        {
                            opts = short_opts + long_opts;
                        }
                        else
                        {
                            opts = long_opts + short_opts;
                        }
                        return (S<", ">()).method("join", { opts });
                    }) }, }));

            define("optparse", "IndentedHelpFormatter", w.make_class("IndentedHelpFormatter", { G<"optparse.HelpFormatter">() }, {
                { "__init__", w.method("IndentedHelpFormatter", "__init__", { py_positional("indent_increment", I<2>()), py_positional("max_help_position", I<24>()), py_positional("width", none()), py_positional("short_first", I<1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py indent_increment = a[1];
                        py max_help_position = a[2];
                        py width = a[3];
                        py short_first = a[4];
                        (G<"optparse.HelpFormatter">()).method("__init__", { self, indent_increment, max_help_position, width, short_first });
                        return none();
                    }) },
                { "format_usage", w.method("IndentedHelpFormatter", "format_usage", { py_positional("usage") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py usage = a[1];
                        return (G<"optparse._">())(S<"Usage: %s\n">()) % usage;
                    }) },
                { "format_heading", w.method("IndentedHelpFormatter", "format_heading", { py_positional("heading") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py heading = a[1];
                        return (S<"%*s%s:\n">()) % py_tuple({ self.attr("current_indent"), S<"">(), heading });
                    }) }, }));

            define("optparse", "TitledHelpFormatter", w.make_class("TitledHelpFormatter", { G<"optparse.HelpFormatter">() }, {
                { "__init__", w.method("TitledHelpFormatter", "__init__", { py_positional("indent_increment", I<0>()), py_positional("max_help_position", I<24>()), py_positional("width", none()), py_positional("short_first", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py indent_increment = a[1];
                        py max_help_position = a[2];
                        py width = a[3];
                        py short_first = a[4];
                        (G<"optparse.HelpFormatter">()).method("__init__", { self, indent_increment, max_help_position, width, short_first });
                        return none();
                    }) },
                { "format_usage", w.method("TitledHelpFormatter", "format_usage", { py_positional("usage") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py usage = a[1];
                        return (S<"%s  %s\n">()) % py_tuple({ self.method("format_heading", { (G<"optparse._">())(S<"Usage">()) }), usage });
                    }) },
                { "format_heading", w.method("TitledHelpFormatter", "format_heading", { py_positional("heading") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py heading = a[1];
                        return (S<"%s\n%s\n">()) % py_tuple({ heading, (S<"=-">())[self.attr("level")] * py{ heading.size() } });
                    }) }, }));

            define("optparse", "_parse_num", w.function("_parse_num", "_parse_num", { py_positional("val"), py_positional("type") }, [](arguments a) -> py
                {
                    py val = a[0];
                    py type = a[1];
                    py radix;
                    if (val[B<"slice">()(none(), I<2>(), none())].method("lower") == (S<"0x">()))
                    {
                        radix = I<16>();
                    }
                    else if (val[B<"slice">()(none(), I<2>(), none())].method("lower") == (S<"0b">()))
                    {
                        radix = I<2>();
                        val = [&]() -> py { py const t = val[B<"slice">()(I<2>(), none(), none())]; return t.truthy() ? t : S<"0">(); }();
                    }
                    else if (val[B<"slice">()(none(), I<1>(), none())] == (S<"0">()))
                    {
                        radix = I<8>();
                    }
                    else
                    {
                        radix = I<10>();
                    }
                    return type(val, radix);
                }));

            define("optparse", "_parse_int", w.function("_parse_int", "_parse_int", { py_positional("val") }, [](arguments a) -> py
                {
                    py val = a[0];
                    return (G<"optparse._parse_num">())(val, B<"int">());
                }));

            define("optparse", "_builtin_cvt", dict_of({ { S<"int">(), py_tuple({ G<"optparse._parse_int">(), (G<"optparse._">())(S<"integer">()) }) }, { S<"long">(), py_tuple({ G<"optparse._parse_int">(), (G<"optparse._">())(S<"integer">()) }) }, { S<"float">(), py_tuple({ B<"float">(), (G<"optparse._">())(S<"floating-point">()) }) }, { S<"complex">(), py_tuple({ B<"complex">(), (G<"optparse._">())(S<"complex">()) }) } }));
            define("optparse", "check_builtin", w.function("check_builtin", "check_builtin", { py_positional("option"), py_positional("opt"), py_positional("value") }, [](arguments a) -> py
                {
                    py option = a[0];
                    py opt = a[1];
                    py value = a[2];
                    py cvt;
                    py what;
                    auto const items13 = unpack((G<"optparse._builtin_cvt">())[option.attr("type")], 2);
                    cvt = items13[0];
                    what = items13[1];
                    try
                    {
                        return cvt(value);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler14;
                        if (matches(handler14.exception, B<"ValueError">()))
                        {
                            py_raise((G<"optparse.OptionValueError">())((G<"optparse._">())(S<"option %s: invalid %s value: %r">()) % py_tuple({ opt, what, value })));
                        }
                        else
                            py_reraise(handler14.exception);
                    }
                    return none();
                }));

            define("optparse", "check_choice", w.function("check_choice", "check_choice", { py_positional("option"), py_positional("opt"), py_positional("value") }, [](arguments a) -> py
                {
                    py option = a[0];
                    py opt = a[1];
                    py value = a[2];
                    py choices;
                    if (option.attr("choices").contains(value))
                    {
                        return value;
                    }
                    else
                    {
                        choices = (S<", ">()).method("join", { (B<"map">())(B<"repr">(), option.attr("choices")) });
                        py_raise((G<"optparse.OptionValueError">())((G<"optparse._">())(S<"option %s: invalid choice: %r (choose from %s)">()) % py_tuple({ opt, value, choices })));
                    }
                    return none();
                }));

            // Not supplying a default is different from a default of None,
            // so we need an explicit "not supplied" value.
            define("optparse", "NO_DEFAULT", py_tuple({ S<"NO">(), S<"DEFAULT">() }));
                // The list of instance attributes that may be set through
                // keyword args to the constructor.
                // The set of actions allowed by option parsers.  Explicitly listed
                // here so the constructor can validate its arguments.
                // The set of actions that involve storing a value somewhere;
                // also listed just for constructor argument validation.  (If
                // the action is one of these, there must be a destination.)
                // The set of actions for which it makes sense to supply a value
                // type, ie. which may consume an argument from the command line.
                // The set of actions which *require* a value type, ie. that
                // always consume an argument from the command line.
                // The set of actions which take a 'const' attribute.
                // The set of known types for option parsers.  Again, listed here for
                // constructor argument validation.
                // Dictionary of argument checking functions, which convert and
                // validate option arguments according to the option type.
                // 
                // Signature of checking functions is:
                // check(option : Option, opt : string, value : string) -> any
                // where
                // option is the Option instance calling the checker
                // opt is the actual option seen on the command-line
                // (eg. "-a", "--file")
                // value is the option argument seen on the command-line
                // 
                // The return value should be in the appropriate Python type
                // for option.type -- eg. an integer if option.type == "int".
                // 
                // If no checker is defined for a type, arguments will be
                // unchecked and remain strings.
                // CHECK_METHODS is a list of unbound method objects; they are called
                // by the constructor, in order, after all attributes are
                // initialized.  The list is created and filled in later, after all
                // the methods are actually defined.  (I just put it here because I
                // like to define and document all class attributes in the same
                // place.)  Subclasses that add another _check_*() method should
                // define their own CHECK_METHODS list that adds their check method
                // to those from this class.
                // -- Constructor/initialization methods ----------------------------
                // -- Constructor validation methods --------------------------------
                // -- Miscellaneous methods -----------------------------------------
                // -- Processing methods --------------------------------------------
            define("optparse", "Option", w.make_class("Option", {  }, {
                { "ATTRS", py_list({ S<"action">(), S<"type">(), S<"dest">(), S<"default">(), S<"nargs">(), S<"const">(), S<"choices">(), S<"callback">(), S<"callback_args">(), S<"callback_kwargs">(), S<"help">(), S<"metavar">() }) },
                { "ACTIONS", py_tuple({ S<"store">(), S<"store_const">(), S<"store_true">(), S<"store_false">(), S<"append">(), S<"append_const">(), S<"count">(), S<"callback">(), S<"help">(), S<"version">() }) },
                { "STORE_ACTIONS", py_tuple({ S<"store">(), S<"store_const">(), S<"store_true">(), S<"store_false">(), S<"append">(), S<"append_const">(), S<"count">() }) },
                { "TYPED_ACTIONS", py_tuple({ S<"store">(), S<"append">(), S<"callback">() }) },
                { "ALWAYS_TYPED_ACTIONS", py_tuple({ S<"store">(), S<"append">() }) },
                { "CONST_ACTIONS", py_tuple({ S<"store_const">(), S<"append_const">() }) },
                { "TYPES", py_tuple({ S<"string">(), S<"int">(), S<"long">(), S<"float">(), S<"complex">(), S<"choice">() }) },
                { "TYPE_CHECKER", dict_of({ { S<"int">(), G<"optparse.check_builtin">() }, { S<"long">(), G<"optparse.check_builtin">() }, { S<"float">(), G<"optparse.check_builtin">() }, { S<"complex">(), G<"optparse.check_builtin">() }, { S<"choice">(), G<"optparse.check_choice">() } }) },
                { "CHECK_METHODS", none() },
                { "__init__", w.method("Option", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opts = a[1];
                        py attrs = a[2];
                        py checker;
                        self.set_attr("_short_opts", py_list());
                        self.set_attr("_long_opts", py_list());
                        opts = self.method("_check_opt_strings", { opts });
                        self.method("_set_opt_strings", { opts });
                        // Set all other attrs (action, type, etc.) from 'attrs' dict
                        self.method("_set_attrs", { attrs });
                        // Check all the attributes we just set.  There are lots of
                        // complicated interdependencies, but luckily they can be farmed
                        // out to the _check_*() methods listed in CHECK_METHODS -- which
                        // could be handy for subclasses!  The one thing these all share
                        // is that they raise OptionError if they discover a problem.
                        for (py const& item : iterate{ self.attr("CHECK_METHODS") })
                        {
                            checker = item;
                            checker(self);
                        }
                        return none();
                    }, true, true) },
                { "_check_opt_strings", w.method("Option", "_check_opt_strings", { py_positional("opts") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opts = a[1];
                        opts = [&]() -> py { py const comprehension16 = py_list(); py opt; for (py const& item : iterate{ opts }) { opt = item; if (!opt.truthy()) continue; comprehension16.method("append", { opt }); } return comprehension16; }();
                        if (!opts.truthy())
                        {
                            py_raise((B<"TypeError">())(S<"at least one option string must be supplied">()));
                        }
                        return opts;
                    }) },
                { "_set_opt_strings", w.method("Option", "_set_opt_strings", { py_positional("opts") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opts = a[1];
                        py opt;
                        for (py const& item : iterate{ opts })
                        {
                            opt = item;
                            if (py{ opt.size() } < (I<2>()))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"invalid option string %r: must be at least two characters long">()) % opt, self));
                            }
                            else if (py{ opt.size() } == (I<2>()))
                            {
                                if (!([&]() -> py { py const t = boolean(opt[I<0>()] == (S<"-">())); return !t.truthy() ? t : boolean(opt[I<1>()] != (S<"-">())); }()).truthy())
                                {
                                    py_raise((G<"optparse.OptionError">())((S<"invalid short option string %r: must be of the form -x, (x any non-dash char)">()) % opt, self));
                                }
                                self.attr("_short_opts").method("append", { opt });
                            }
                            else
                            {
                                if (!([&]() -> py { py const t = boolean(opt[B<"slice">()(I<0>(), I<2>(), none())] == (S<"--">())); return !t.truthy() ? t : boolean(opt[I<2>()] != (S<"-">())); }()).truthy())
                                {
                                    py_raise((G<"optparse.OptionError">())((S<"invalid long option string %r: must start with --, followed by non-dash">()) % opt, self));
                                }
                                self.attr("_long_opts").method("append", { opt });
                            }
                        }
                        return none();
                    }) },
                { "_set_attrs", w.method("Option", "_set_attrs", { py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        py attr;
                        for (py const& item : iterate{ self.attr("ATTRS") })
                        {
                            attr = item;
                            if (attrs.contains(attr))
                            {
                                (B<"setattr">())(self, attr, attrs[attr]);
                                del_item(attrs, attr);
                            }
                            else if (attr == (S<"default">()))
                            {
                                (B<"setattr">())(self, attr, G<"optparse.NO_DEFAULT">());
                            }
                            else
                            {
                                (B<"setattr">())(self, attr, none());
                            }
                        }
                        if (attrs.truthy())
                        {
                            attrs = (B<"sorted">())(attrs.method("keys"));
                            py_raise((G<"optparse.OptionError">())((S<"invalid keyword arguments: %s">()) % (S<", ">()).method("join", { attrs }), self));
                        }
                        return none();
                    }) },
                { "_check_action", w.method("Option", "_check_action", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("action").is_none())
                        {
                            self.set_attr("action", S<"store">());
                        }
                        else if (!self.attr("ACTIONS").contains(self.attr("action")))
                        {
                            py_raise((G<"optparse.OptionError">())((S<"invalid action: %r">()) % self.attr("action"), self));
                        }
                        return none();
                    }) },
                { "_check_type", w.method("Option", "_check_type", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("type").is_none())
                        {
                            if (self.attr("ALWAYS_TYPED_ACTIONS").contains(self.attr("action")))
                            {
                                if (!self.attr("choices").is_none())
                                {
                                    // The "choices" attribute implies "choice" type.
                                    self.set_attr("type", S<"choice">());
                                }
                                else
                                {
                                    // No type given?  "string" is the most sensible default.
                                    self.set_attr("type", S<"string">());
                                }
                            }
                        }
                        else
                        {
                            // Allow type objects or builtin type conversion functions
                            // (int, str, etc.) as an alternative to their names.
                            if (isinstance(self.attr("type"), B<"type">()))
                            {
                                self.set_attr("type", self.attr("type").attr("__name__"));
                            }
                            if (self.attr("type") == (S<"str">()))
                            {
                                self.set_attr("type", S<"string">());
                            }
                            if (!self.attr("TYPES").contains(self.attr("type")))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"invalid option type: %r">()) % self.attr("type"), self));
                            }
                            if (!self.attr("TYPED_ACTIONS").contains(self.attr("action")))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"must not supply a type for action %r">()) % self.attr("action"), self));
                            }
                        }
                        return none();
                    }) },
                { "_check_choice", w.method("Option", "_check_choice", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("type") == (S<"choice">()))
                        {
                            if (self.attr("choices").is_none())
                            {
                                py_raise((G<"optparse.OptionError">())(S<"must supply a list of choices for type 'choice'">(), self));
                            }
                            else if (!isinstance(self.attr("choices"), py_tuple({ B<"tuple">(), B<"list">() })))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"choices must be a list of strings ('%s' supplied)">()) % (B<"str">())((B<"type">())(self.attr("choices"))).method("split", { S<"'">() })[I<1>()], self));
                            }
                        }
                        else if (!self.attr("choices").is_none())
                        {
                            py_raise((G<"optparse.OptionError">())((S<"must not supply choices for type %r">()) % self.attr("type"), self));
                        }
                        return none();
                    }) },
                { "_check_dest", w.method("Option", "_check_dest", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py takes_value;
                        takes_value = [&]() -> py { py const t = boolean(self.attr("STORE_ACTIONS").contains(self.attr("action"))); return t.truthy() ? t : boolean(!self.attr("type").is_none()); }();
                        if ((self.attr("dest").is_none() && takes_value.truthy()))
                        {
                            // Glean a destination from the first long option string,
                            // or from the first short option string if no long options.
                            if (self.attr("_long_opts").truthy())
                            {
                                // eg. "--foo-bar" -> "foo_bar"
                                self.set_attr("dest", self.attr("_long_opts")[I<0>()][B<"slice">()(I<2>(), none(), none())].method("replace", { S<"-">(), S<"_">() }));
                            }
                            else
                            {
                                self.set_attr("dest", self.attr("_short_opts")[I<0>()][I<1>()]);
                            }
                        }
                        return none();
                    }) },
                { "_check_const", w.method("Option", "_check_const", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (((!self.attr("CONST_ACTIONS").contains(self.attr("action"))) && (!self.attr("const").is_none())))
                        {
                            py_raise((G<"optparse.OptionError">())((S<"'const' must not be supplied for action %r">()) % self.attr("action"), self));
                        }
                        return none();
                    }) },
                { "_check_nargs", w.method("Option", "_check_nargs", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("TYPED_ACTIONS").contains(self.attr("action")))
                        {
                            if (self.attr("nargs").is_none())
                            {
                                self.set_attr("nargs", I<1>());
                            }
                        }
                        else if (!self.attr("nargs").is_none())
                        {
                            py_raise((G<"optparse.OptionError">())((S<"'nargs' must not be supplied for action %r">()) % self.attr("action"), self));
                        }
                        return none();
                    }) },
                { "_check_callback", w.method("Option", "_check_callback", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("action") == (S<"callback">()))
                        {
                            if (!callable(self.attr("callback")))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"callback not callable: %r">()) % self.attr("callback"), self));
                            }
                            if (((!self.attr("callback_args").is_none()) && (!isinstance(self.attr("callback_args"), B<"tuple">()))))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"callback_args, if supplied, must be a tuple: not %r">()) % self.attr("callback_args"), self));
                            }
                            if (((!self.attr("callback_kwargs").is_none()) && (!isinstance(self.attr("callback_kwargs"), B<"dict">()))))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"callback_kwargs, if supplied, must be a dict: not %r">()) % self.attr("callback_kwargs"), self));
                            }
                        }
                        else
                        {
                            if (!self.attr("callback").is_none())
                            {
                                py_raise((G<"optparse.OptionError">())((S<"callback supplied (%r) for non-callback option">()) % self.attr("callback"), self));
                            }
                            if (!self.attr("callback_args").is_none())
                            {
                                py_raise((G<"optparse.OptionError">())(S<"callback_args supplied for non-callback option">(), self));
                            }
                            if (!self.attr("callback_kwargs").is_none())
                            {
                                py_raise((G<"optparse.OptionError">())(S<"callback_kwargs supplied for non-callback option">(), self));
                            }
                        }
                        return none();
                    }) },
                { "CHECK_METHODS", py_list({ w.method("Option", "_check_action", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("action").is_none())
                        {
                            self.set_attr("action", S<"store">());
                        }
                        else if (!self.attr("ACTIONS").contains(self.attr("action")))
                        {
                            py_raise((G<"optparse.OptionError">())((S<"invalid action: %r">()) % self.attr("action"), self));
                        }
                        return none();
                    }), w.method("Option", "_check_type", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("type").is_none())
                        {
                            if (self.attr("ALWAYS_TYPED_ACTIONS").contains(self.attr("action")))
                            {
                                if (!self.attr("choices").is_none())
                                {
                                    // The "choices" attribute implies "choice" type.
                                    self.set_attr("type", S<"choice">());
                                }
                                else
                                {
                                    // No type given?  "string" is the most sensible default.
                                    self.set_attr("type", S<"string">());
                                }
                            }
                        }
                        else
                        {
                            // Allow type objects or builtin type conversion functions
                            // (int, str, etc.) as an alternative to their names.
                            if (isinstance(self.attr("type"), B<"type">()))
                            {
                                self.set_attr("type", self.attr("type").attr("__name__"));
                            }
                            if (self.attr("type") == (S<"str">()))
                            {
                                self.set_attr("type", S<"string">());
                            }
                            if (!self.attr("TYPES").contains(self.attr("type")))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"invalid option type: %r">()) % self.attr("type"), self));
                            }
                            if (!self.attr("TYPED_ACTIONS").contains(self.attr("action")))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"must not supply a type for action %r">()) % self.attr("action"), self));
                            }
                        }
                        return none();
                    }), w.method("Option", "_check_choice", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("type") == (S<"choice">()))
                        {
                            if (self.attr("choices").is_none())
                            {
                                py_raise((G<"optparse.OptionError">())(S<"must supply a list of choices for type 'choice'">(), self));
                            }
                            else if (!isinstance(self.attr("choices"), py_tuple({ B<"tuple">(), B<"list">() })))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"choices must be a list of strings ('%s' supplied)">()) % (B<"str">())((B<"type">())(self.attr("choices"))).method("split", { S<"'">() })[I<1>()], self));
                            }
                        }
                        else if (!self.attr("choices").is_none())
                        {
                            py_raise((G<"optparse.OptionError">())((S<"must not supply choices for type %r">()) % self.attr("type"), self));
                        }
                        return none();
                    }), w.method("Option", "_check_dest", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py takes_value;
                        takes_value = [&]() -> py { py const t = boolean(self.attr("STORE_ACTIONS").contains(self.attr("action"))); return t.truthy() ? t : boolean(!self.attr("type").is_none()); }();
                        if ((self.attr("dest").is_none() && takes_value.truthy()))
                        {
                            // Glean a destination from the first long option string,
                            // or from the first short option string if no long options.
                            if (self.attr("_long_opts").truthy())
                            {
                                // eg. "--foo-bar" -> "foo_bar"
                                self.set_attr("dest", self.attr("_long_opts")[I<0>()][B<"slice">()(I<2>(), none(), none())].method("replace", { S<"-">(), S<"_">() }));
                            }
                            else
                            {
                                self.set_attr("dest", self.attr("_short_opts")[I<0>()][I<1>()]);
                            }
                        }
                        return none();
                    }), w.method("Option", "_check_const", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (((!self.attr("CONST_ACTIONS").contains(self.attr("action"))) && (!self.attr("const").is_none())))
                        {
                            py_raise((G<"optparse.OptionError">())((S<"'const' must not be supplied for action %r">()) % self.attr("action"), self));
                        }
                        return none();
                    }), w.method("Option", "_check_nargs", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("TYPED_ACTIONS").contains(self.attr("action")))
                        {
                            if (self.attr("nargs").is_none())
                            {
                                self.set_attr("nargs", I<1>());
                            }
                        }
                        else if (!self.attr("nargs").is_none())
                        {
                            py_raise((G<"optparse.OptionError">())((S<"'nargs' must not be supplied for action %r">()) % self.attr("action"), self));
                        }
                        return none();
                    }), w.method("Option", "_check_callback", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("action") == (S<"callback">()))
                        {
                            if (!callable(self.attr("callback")))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"callback not callable: %r">()) % self.attr("callback"), self));
                            }
                            if (((!self.attr("callback_args").is_none()) && (!isinstance(self.attr("callback_args"), B<"tuple">()))))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"callback_args, if supplied, must be a tuple: not %r">()) % self.attr("callback_args"), self));
                            }
                            if (((!self.attr("callback_kwargs").is_none()) && (!isinstance(self.attr("callback_kwargs"), B<"dict">()))))
                            {
                                py_raise((G<"optparse.OptionError">())((S<"callback_kwargs, if supplied, must be a dict: not %r">()) % self.attr("callback_kwargs"), self));
                            }
                        }
                        else
                        {
                            if (!self.attr("callback").is_none())
                            {
                                py_raise((G<"optparse.OptionError">())((S<"callback supplied (%r) for non-callback option">()) % self.attr("callback"), self));
                            }
                            if (!self.attr("callback_args").is_none())
                            {
                                py_raise((G<"optparse.OptionError">())(S<"callback_args supplied for non-callback option">(), self));
                            }
                            if (!self.attr("callback_kwargs").is_none())
                            {
                                py_raise((G<"optparse.OptionError">())(S<"callback_kwargs supplied for non-callback option">(), self));
                            }
                        }
                        return none();
                    }) }) },
                { "__str__", w.method("Option", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"/">()).method("join", { self.attr("_short_opts") + self.attr("_long_opts") });
                    }) },
                { "__repr__", G<"optparse._repr">() },
                { "takes_value", w.method("Option", "takes_value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(!self.attr("type").is_none());
                    }) },
                { "get_opt_string", w.method("Option", "get_opt_string", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_long_opts").truthy())
                        {
                            return self.attr("_long_opts")[I<0>()];
                        }
                        else
                        {
                            return self.attr("_short_opts")[I<0>()];
                        }
                        return none();
                    }) },
                { "check_value", w.method("Option", "check_value", { py_positional("opt"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opt = a[1];
                        py value = a[2];
                        py checker;
                        checker = self.attr("TYPE_CHECKER").method("get", { self.attr("type") });
                        if (checker.is_none())
                        {
                            return value;
                        }
                        else
                        {
                            return checker(self, opt, value);
                        }
                        return none();
                    }) },
                { "convert_value", w.method("Option", "convert_value", { py_positional("opt"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opt = a[1];
                        py value = a[2];
                        if (!value.is_none())
                        {
                            if (self.attr("nargs") == (I<1>()))
                            {
                                return self.method("check_value", { opt, value });
                            }
                            else
                            {
                                return (B<"tuple">())([&]() -> py { py const comprehension19 = py_list(); py v; for (py const& item : iterate{ value }) { v = item; comprehension19.method("append", { self.method("check_value", { opt, v }) }); } return comprehension19; }());
                            }
                        }
                        return none();
                    }) },
                { "process", w.method("Option", "process", { py_positional("opt"), py_positional("value"), py_positional("values"), py_positional("parser") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opt = a[1];
                        py value = a[2];
                        py values = a[3];
                        py parser = a[4];
                        value = self.method("convert_value", { opt, value });
                        // And then take whatever action is expected of us.
                        // This is a separate method to make life easier for
                        // subclasses to add new actions.
                        return self.method("take_action", { self.attr("action"), self.attr("dest"), opt, value, values, parser });
                    }) },
                { "take_action", w.method("Option", "take_action", { py_positional("action"), py_positional("dest"), py_positional("opt"), py_positional("value"), py_positional("values"), py_positional("parser") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py action = a[1];
                        py dest = a[2];
                        py opt = a[3];
                        py value = a[4];
                        py values = a[5];
                        py parser = a[6];
                        py args;
                        py kwargs;
                        if (action == (S<"store">()))
                        {
                            (B<"setattr">())(values, dest, value);
                        }
                        else if (action == (S<"store_const">()))
                        {
                            (B<"setattr">())(values, dest, self.attr("const"));
                        }
                        else if (action == (S<"store_true">()))
                        {
                            (B<"setattr">())(values, dest, boolean(true));
                        }
                        else if (action == (S<"store_false">()))
                        {
                            (B<"setattr">())(values, dest, boolean(false));
                        }
                        else if (action == (S<"append">()))
                        {
                            values.method("ensure_value", { dest, py_list() }).method("append", { value });
                        }
                        else if (action == (S<"append_const">()))
                        {
                            values.method("ensure_value", { dest, py_list() }).method("append", { self.attr("const") });
                        }
                        else if (action == (S<"count">()))
                        {
                            (B<"setattr">())(values, dest, values.method("ensure_value", { dest, I<0>() }) + (I<1>()));
                        }
                        else if (action == (S<"callback">()))
                        {
                            args = [&]() -> py { py const t = self.attr("callback_args"); return t.truthy() ? t : py_tuple(); }();
                            kwargs = [&]() -> py { py const t = self.attr("callback_kwargs"); return t.truthy() ? t : py_dict(); }();
                            call_ex(self.attr("callback"), { self, opt, value, parser }, args, kwargs);
                        }
                        else if (action == (S<"help">()))
                        {
                            parser.method("print_help");
                            parser.method("exit");
                        }
                        else if (action == (S<"version">()))
                        {
                            parser.method("print_version");
                            parser.method("exit");
                        }
                        else
                        {
                            py_raise((B<"ValueError">())((S<"unknown action %r">()) % self.attr("action")));
                        }
                        return I<1>();
                    }) }, }));

            // class Option
            define("optparse", "SUPPRESS_HELP", (S<"SUPPRESS">()) + (S<"HELP">()));
            define("optparse", "SUPPRESS_USAGE", (S<"SUPPRESS">()) + (S<"USAGE">()));
            define("optparse", "Values", w.make_class("Values", {  }, {
                { "__init__", w.method("Values", "__init__", { py_positional("defaults", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py defaults = a[1];
                        py attr;
                        py val;
                        if (defaults.truthy())
                        {
                            for (py const& item : iterate{ defaults.method("items") })
                            {
                                auto const items21 = unpack(item, 2);
                                attr = items21[0];
                                val = items21[1];
                                (B<"setattr">())(self, attr, val);
                            }
                        }
                        return none();
                    }) },
                { "__str__", w.method("Values", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"str">())(self.attr("__dict__"));
                    }) },
                { "__repr__", G<"optparse._repr">() },
                { "__eq__", w.method("Values", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (isinstance(other, G<"optparse.Values">()))
                        {
                            return boolean(self.attr("__dict__") == other.attr("__dict__"));
                        }
                        else if (isinstance(other, B<"dict">()))
                        {
                            return boolean(self.attr("__dict__") == other);
                        }
                        else
                        {
                            return py::borrowed(not_implemented());
                        }
                        return none();
                    }) },
                { "_update_careful", w.method("Values", "_update_careful", { py_positional("dict") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dict = a[1];
                        py attr;
                        py dval;
                        for (py const& item : iterate{ (B<"dir">())(self) })
                        {
                            attr = item;
                            if (dict.contains(attr))
                            {
                                dval = dict[attr];
                                if (!dval.is_none())
                                {
                                    (B<"setattr">())(self, attr, dval);
                                }
                            }
                        }
                        return none();
                    }) },
                { "_update_loose", w.method("Values", "_update_loose", { py_positional("dict") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dict = a[1];
                        self.attr("__dict__").method("update", { dict });
                        return none();
                    }) },
                { "_update", w.method("Values", "_update", { py_positional("dict"), py_positional("mode") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dict = a[1];
                        py mode = a[2];
                        if (mode == (S<"careful">()))
                        {
                            self.method("_update_careful", { dict });
                        }
                        else if (mode == (S<"loose">()))
                        {
                            self.method("_update_loose", { dict });
                        }
                        else
                        {
                            py_raise((B<"ValueError">())((S<"invalid update mode: %r">()) % mode));
                        }
                        return none();
                    }) },
                { "read_module", w.method("Values", "read_module", { py_positional("modname"), py_positional("mode", S<"careful">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py modname = a[1];
                        py mode = a[2];
                        py mod;
                        (B<"__import__">())(modname);
                        mod = (Mod<"sys">().attr("modules"))[modname];
                        self.method("_update", { (B<"vars">())(mod), mode });
                        return none();
                    }) },
                { "read_file", w.method("Values", "read_file", { py_positional("filename"), py_positional("mode", S<"careful">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py mode = a[2];
                        py vars;
                        vars = py_dict();
                        (B<"exec">())((B<"open">())(filename).method("read"), vars);
                        self.method("_update", { vars, mode });
                        return none();
                    }) },
                { "ensure_value", w.method("Values", "ensure_value", { py_positional("attr"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attr = a[1];
                        py value = a[2];
                        if (((!(B<"hasattr">())(self, attr).truthy()) || (B<"getattr">())(self, attr).is_none()))
                        {
                            (B<"setattr">())(self, attr, value);
                        }
                        return (B<"getattr">())(self, attr);
                    }) }, }));

                // -- Option-adding methods -----------------------------------------
                // -- Option query/removal methods ----------------------------------
                // -- Help-formatting methods ---------------------------------------
            define("optparse", "OptionContainer", w.make_class("OptionContainer", {  }, {
                { "__init__", w.method("OptionContainer", "__init__", { py_positional("option_class"), py_positional("conflict_handler"), py_positional("description") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py option_class = a[1];
                        py conflict_handler = a[2];
                        py description = a[3];
                        self.method("_create_option_list");
                        self.set_attr("option_class", option_class);
                        self.method("set_conflict_handler", { conflict_handler });
                        self.method("set_description", { description });
                        return none();
                    }) },
                { "_create_option_mappings", w.method("OptionContainer", "_create_option_mappings", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_short_opt", py_dict());
                        self.set_attr("_long_opt", py_dict());
                        self.set_attr("defaults", py_dict());
                        return none();
                    }) },
                { "_share_option_mappings", w.method("OptionContainer", "_share_option_mappings", { py_positional("parser") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser = a[1];
                        self.set_attr("_short_opt", parser.attr("_short_opt"));
                        self.set_attr("_long_opt", parser.attr("_long_opt"));
                        self.set_attr("defaults", parser.attr("defaults"));
                        return none();
                    }) },
                { "set_conflict_handler", w.method("OptionContainer", "set_conflict_handler", { py_positional("handler") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handler = a[1];
                        if (!py_tuple({ S<"error">(), S<"resolve">() }).contains(handler))
                        {
                            py_raise((B<"ValueError">())((S<"invalid conflict_resolution value %r">()) % handler));
                        }
                        self.set_attr("conflict_handler", handler);
                        return none();
                    }) },
                { "set_description", w.method("OptionContainer", "set_description", { py_positional("description") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py description = a[1];
                        self.set_attr("description", description);
                        return none();
                    }) },
                { "get_description", w.method("OptionContainer", "get_description", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("description");
                    }) },
                { "destroy", w.method("OptionContainer", "destroy", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        del_attr(self, "_short_opt");
                        del_attr(self, "_long_opt");
                        del_attr(self, "defaults");
                        return none();
                    }) },
                { "_check_conflict", w.method("OptionContainer", "_check_conflict", { py_positional("option") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py option = a[1];
                        py c_option;
                        py conflict_opts;
                        py handler;
                        py opt;
                        conflict_opts = py_list();
                        for (py const& item : iterate{ option.attr("_short_opts") })
                        {
                            opt = item;
                            if (self.attr("_short_opt").contains(opt))
                            {
                                conflict_opts.method("append", { py_tuple({ opt, self.attr("_short_opt")[opt] }) });
                            }
                        }
                        for (py const& item : iterate{ option.attr("_long_opts") })
                        {
                            opt = item;
                            if (self.attr("_long_opt").contains(opt))
                            {
                                conflict_opts.method("append", { py_tuple({ opt, self.attr("_long_opt")[opt] }) });
                            }
                        }
                        if (conflict_opts.truthy())
                        {
                            handler = self.attr("conflict_handler");
                            if (handler == (S<"error">()))
                            {
                                py_raise((G<"optparse.OptionConflictError">())((S<"conflicting option string(s): %s">()) % (S<", ">()).method("join", { [&]() -> py { py const comprehension25 = py_list(); py co; for (py const& item : iterate{ conflict_opts }) { co = item; comprehension25.method("append", { co[I<0>()] }); } return comprehension25; }() }), option));
                            }
                            else if (handler == (S<"resolve">()))
                            {
                                for (py const& item : iterate{ conflict_opts })
                                {
                                    auto const items27 = unpack(item, 2);
                                    opt = items27[0];
                                    c_option = items27[1];
                                    if (opt.method("startswith", { S<"--">() }).truthy())
                                    {
                                        c_option.attr("_long_opts").method("remove", { opt });
                                        del_item(self.attr("_long_opt"), opt);
                                    }
                                    else
                                    {
                                        c_option.attr("_short_opts").method("remove", { opt });
                                        del_item(self.attr("_short_opt"), opt);
                                    }
                                    if (!([&]() -> py { py const t = c_option.attr("_short_opts"); return t.truthy() ? t : c_option.attr("_long_opts"); }()).truthy())
                                    {
                                        c_option.attr("container").attr("option_list").method("remove", { c_option });
                                    }
                                }
                            }
                        }
                        return none();
                    }) },
                { "add_option", w.method("OptionContainer", "add_option", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py opt;
                        py option;
                        if (isinstance(args[I<0>()], B<"str">()))
                        {
                            option = call_ex(self.attr("option_class"), {  }, args, kwargs);
                        }
                        else if (((py{ args.size() } == (I<1>())) && (!kwargs.truthy())))
                        {
                            option = args[I<0>()];
                            if (!isinstance(option, G<"optparse.Option">()))
                            {
                                py_raise((B<"TypeError">())((S<"not an Option instance: %r">()) % option));
                            }
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"invalid arguments">()));
                        }
                        self.method("_check_conflict", { option });
                        self.attr("option_list").method("append", { option });
                        option.set_attr("container", self);
                        for (py const& item : iterate{ option.attr("_short_opts") })
                        {
                            opt = item;
                            self.attr("_short_opt").set_item(opt, option);
                        }
                        for (py const& item : iterate{ option.attr("_long_opts") })
                        {
                            opt = item;
                            self.attr("_long_opt").set_item(opt, option);
                        }
                        if (!option.attr("dest").is_none())
                        {
                            if (!option.attr("default").is((G<"optparse.NO_DEFAULT">())))
                            {
                                self.attr("defaults").set_item(option.attr("dest"), option.attr("default"));
                            }
                            else if (!self.attr("defaults").contains(option.attr("dest")))
                            {
                                self.attr("defaults").set_item(option.attr("dest"), none());
                            }
                        }
                        return option;
                    }, true, true) },
                { "add_options", w.method("OptionContainer", "add_options", { py_positional("option_list") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py option_list = a[1];
                        py option;
                        for (py const& item : iterate{ option_list })
                        {
                            option = item;
                            self.method("add_option", { option });
                        }
                        return none();
                    }) },
                { "get_option", w.method("OptionContainer", "get_option", { py_positional("opt_str") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opt_str = a[1];
                        return [&]() -> py { py const t = self.attr("_short_opt").method("get", { opt_str }); return t.truthy() ? t : self.attr("_long_opt").method("get", { opt_str }); }();
                    }) },
                { "has_option", w.method("OptionContainer", "has_option", { py_positional("opt_str") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opt_str = a[1];
                        return [&]() -> py { py const t = boolean(self.attr("_short_opt").contains(opt_str)); return t.truthy() ? t : boolean(self.attr("_long_opt").contains(opt_str)); }();
                    }) },
                { "remove_option", w.method("OptionContainer", "remove_option", { py_positional("opt_str") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opt_str = a[1];
                        py opt;
                        py option;
                        option = self.attr("_short_opt").method("get", { opt_str });
                        if (option.is_none())
                        {
                            option = self.attr("_long_opt").method("get", { opt_str });
                        }
                        if (option.is_none())
                        {
                            py_raise((B<"ValueError">())((S<"no such option %r">()) % opt_str));
                        }
                        for (py const& item : iterate{ option.attr("_short_opts") })
                        {
                            opt = item;
                            del_item(self.attr("_short_opt"), opt);
                        }
                        for (py const& item : iterate{ option.attr("_long_opts") })
                        {
                            opt = item;
                            del_item(self.attr("_long_opt"), opt);
                        }
                        option.attr("container").attr("option_list").method("remove", { option });
                        return none();
                    }) },
                { "format_option_help", w.method("OptionContainer", "format_option_help", { py_positional("formatter") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py formatter = a[1];
                        py option;
                        py result;
                        if (!self.attr("option_list").truthy())
                        {
                            return S<"">();
                        }
                        result = py_list();
                        for (py const& item : iterate{ self.attr("option_list") })
                        {
                            option = item;
                            if (!option.attr("help").is((G<"optparse.SUPPRESS_HELP">())))
                            {
                                result.method("append", { formatter.method("format_option", { option }) });
                            }
                        }
                        return (S<"">()).method("join", { result });
                    }) },
                { "format_description", w.method("OptionContainer", "format_description", { py_positional("formatter") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py formatter = a[1];
                        return formatter.method("format_description", { self.method("get_description") });
                    }) },
                { "format_help", w.method("OptionContainer", "format_help", { py_positional("formatter") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py formatter = a[1];
                        py result;
                        result = py_list();
                        if (self.attr("description").truthy())
                        {
                            result.method("append", { self.method("format_description", { formatter }) });
                        }
                        if (self.attr("option_list").truthy())
                        {
                            result.method("append", { self.method("format_option_help", { formatter }) });
                        }
                        return (S<"\n">()).method("join", { result });
                    }) }, }));

                // -- Help-formatting methods ---------------------------------------
            define("optparse", "OptionGroup", w.make_class("OptionGroup", { G<"optparse.OptionContainer">() }, {
                { "__init__", w.method("OptionGroup", "__init__", { py_positional("parser"), py_positional("title"), py_positional("description", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser = a[1];
                        py title = a[2];
                        py description = a[3];
                        self.set_attr("parser", parser);
                        (G<"optparse.OptionContainer">()).method("__init__", { self, parser.attr("option_class"), parser.attr("conflict_handler"), description });
                        self.set_attr("title", title);
                        return none();
                    }) },
                { "_create_option_list", w.method("OptionGroup", "_create_option_list", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("option_list", py_list());
                        self.method("_share_option_mappings", { self.attr("parser") });
                        return none();
                    }) },
                { "set_title", w.method("OptionGroup", "set_title", { py_positional("title") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py title = a[1];
                        self.set_attr("title", title);
                        return none();
                    }) },
                { "destroy", w.method("OptionGroup", "destroy", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (G<"optparse.OptionContainer">()).method("destroy", { self });
                        del_attr(self, "option_list");
                        return none();
                    }) },
                { "format_help", w.method("OptionGroup", "format_help", { py_positional("formatter") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py formatter = a[1];
                        py result;
                        result = formatter.method("format_heading", { self.attr("title") });
                        formatter.method("indent");
                        result = inplace(NEOS_PY_ADD, result, (G<"optparse.OptionContainer">()).method("format_help", { self, formatter }));
                        formatter.method("dedent");
                        return result;
                    }) }, }));

                // -- Private methods -----------------------------------------------
                // (used by our or OptionContainer's constructor)
                // -- Simple modifier methods ---------------------------------------
                // -- OptionGroup methods -------------------------------------------
                // -- Option-parsing methods ----------------------------------------
                // Say this is the original argument list:
                // [arg0, arg1, ..., arg(i-1), arg(i), arg(i+1), ..., arg(N-1)]
                // ^
                // (we are about to process arg(i)).
                // 
                // Then rargs is [arg(i), ..., arg(N-1)] and largs is a *subset* of
                // [arg0, ..., arg(i-1)] (any options and their arguments will have
                // been removed from largs).
                // 
                // The while loop will usually consume 1 or more arguments per pass.
                // If it consumes 1 (eg. arg is an option that takes no arguments),
                // then after _process_arg() is done the situation is:
                // 
                // largs = subset of [arg0, ..., arg(i)]
                // rargs = [arg(i+1), ..., arg(N-1)]
                // 
                // If allow_interspersed_args is false, largs will always be
                // *empty* -- still a subset of [arg0, ..., arg(i-1)], but
                // not a very interesting subset!
                // -- Feedback methods ----------------------------------------------
            define("optparse", "OptionParser", w.make_class("OptionParser", { G<"optparse.OptionContainer">() }, {
                { "standard_option_list", py_list() },
                { "__init__", w.method("OptionParser", "__init__", { py_positional("usage", none()), py_positional("option_list", none()), py_positional("option_class", G<"optparse.Option">()), py_positional("version", none()), py_positional("conflict_handler", S<"error">()), py_positional("description", none()), py_positional("formatter", none()), py_positional("add_help_option", boolean(true)), py_positional("prog", none()), py_positional("epilog", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py usage = a[1];
                        py option_list = a[2];
                        py option_class = a[3];
                        py version = a[4];
                        py conflict_handler = a[5];
                        py description = a[6];
                        py formatter = a[7];
                        py add_help_option = a[8];
                        py prog = a[9];
                        py epilog = a[10];
                        (G<"optparse.OptionContainer">()).method("__init__", { self, option_class, conflict_handler, description });
                        self.method("set_usage", { usage });
                        self.set_attr("prog", prog);
                        self.set_attr("version", version);
                        self.set_attr("allow_interspersed_args", boolean(true));
                        self.set_attr("process_default_values", boolean(true));
                        if (formatter.is_none())
                        {
                            formatter = (G<"optparse.IndentedHelpFormatter">())();
                        }
                        self.set_attr("formatter", formatter);
                        self.attr("formatter").method("set_parser", { self });
                        self.set_attr("epilog", epilog);
                        // Populate the option list; initial sources are the
                        // standard_option_list class attribute, the 'option_list'
                        // argument, and (if applicable) the _add_version_option() and
                        // _add_help_option() methods.
                        self.method("_populate_option_list", { option_list }, { { "add_help", add_help_option } });
                        self.method("_init_parsing_state");
                        return none();
                    }) },
                { "destroy", w.method("OptionParser", "destroy", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py group;
                        (G<"optparse.OptionContainer">()).method("destroy", { self });
                        for (py const& item : iterate{ self.attr("option_groups") })
                        {
                            group = item;
                            group.method("destroy");
                        }
                        del_attr(self, "option_list");
                        del_attr(self, "option_groups");
                        del_attr(self, "formatter");
                        return none();
                    }) },
                { "_create_option_list", w.method("OptionParser", "_create_option_list", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("option_list", py_list());
                        self.set_attr("option_groups", py_list());
                        self.method("_create_option_mappings");
                        return none();
                    }) },
                { "_add_help_option", w.method("OptionParser", "_add_help_option", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("add_option", { S<"-h">(), S<"--help">() }, { { "action", S<"help">() }, { "help", (G<"optparse._">())(S<"show this help message and exit">()) } });
                        return none();
                    }) },
                { "_add_version_option", w.method("OptionParser", "_add_version_option", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("add_option", { S<"--version">() }, { { "action", S<"version">() }, { "help", (G<"optparse._">())(S<"show program's version number and exit">()) } });
                        return none();
                    }) },
                { "_populate_option_list", w.method("OptionParser", "_populate_option_list", { py_positional("option_list"), py_positional("add_help", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py option_list = a[1];
                        py add_help = a[2];
                        if (self.attr("standard_option_list").truthy())
                        {
                            self.method("add_options", { self.attr("standard_option_list") });
                        }
                        if (option_list.truthy())
                        {
                            self.method("add_options", { option_list });
                        }
                        if (self.attr("version").truthy())
                        {
                            self.method("_add_version_option");
                        }
                        if (add_help.truthy())
                        {
                            self.method("_add_help_option");
                        }
                        return none();
                    }) },
                { "_init_parsing_state", w.method("OptionParser", "_init_parsing_state", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("rargs", none());
                        self.set_attr("largs", none());
                        self.set_attr("values", none());
                        return none();
                    }) },
                { "set_usage", w.method("OptionParser", "set_usage", { py_positional("usage") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py usage = a[1];
                        if (usage.is_none())
                        {
                            self.set_attr("usage", (G<"optparse._">())(S<"%prog [options]">()));
                        }
                        else if (usage.is((G<"optparse.SUPPRESS_USAGE">())))
                        {
                            self.set_attr("usage", none());
                        }
                        // For backwards compatibility with Optik 1.3 and earlier.
                        else if (usage.method("lower").method("startswith", { S<"usage: ">() }).truthy())
                        {
                            self.set_attr("usage", usage[B<"slice">()(I<7>(), none(), none())]);
                        }
                        else
                        {
                            self.set_attr("usage", usage);
                        }
                        return none();
                    }) },
                { "enable_interspersed_args", w.method("OptionParser", "enable_interspersed_args", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("allow_interspersed_args", boolean(true));
                        return none();
                    }) },
                { "disable_interspersed_args", w.method("OptionParser", "disable_interspersed_args", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("allow_interspersed_args", boolean(false));
                        return none();
                    }) },
                { "set_process_default_values", w.method("OptionParser", "set_process_default_values", { py_positional("process") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py process = a[1];
                        self.set_attr("process_default_values", process);
                        return none();
                    }) },
                { "set_default", w.method("OptionParser", "set_default", { py_positional("dest"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dest = a[1];
                        py value = a[2];
                        self.attr("defaults").set_item(dest, value);
                        return none();
                    }) },
                { "set_defaults", w.method("OptionParser", "set_defaults", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py kwargs = a[1];
                        self.attr("defaults").method("update", { kwargs });
                        return none();
                    }, false, true) },
                { "_get_all_options", w.method("OptionParser", "_get_all_options", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py group;
                        py options;
                        options = self.attr("option_list")[B<"slice">()(none(), none(), none())];
                        for (py const& item : iterate{ self.attr("option_groups") })
                        {
                            group = item;
                            options.method("extend", { group.attr("option_list") });
                        }
                        return options;
                    }) },
                { "get_default_values", w.method("OptionParser", "get_default_values", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py default_;
                        py defaults;
                        py opt_str;
                        py option;
                        if (!self.attr("process_default_values").truthy())
                        {
                            // Old, pre-Optik 1.5 behaviour.
                            return (G<"optparse.Values">())(self.attr("defaults"));
                        }
                        defaults = self.attr("defaults").method("copy");
                        for (py const& item : iterate{ self.method("_get_all_options") })
                        {
                            option = item;
                            default_ = defaults.method("get", { option.attr("dest") });
                            if (isinstance(default_, B<"str">()))
                            {
                                opt_str = option.method("get_opt_string");
                                defaults.set_item(option.attr("dest"), option.method("check_value", { opt_str, default_ }));
                            }
                        }
                        return (G<"optparse.Values">())(defaults);
                    }) },
                { "add_option_group", w.method("OptionParser", "add_option_group", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py group;
                        if (isinstance(args[I<0>()], B<"str">()))
                        {
                            group = call_ex((G<"optparse.OptionGroup">()), { self }, args, kwargs);
                        }
                        else if (((py{ args.size() } == (I<1>())) && (!kwargs.truthy())))
                        {
                            group = args[I<0>()];
                            if (!isinstance(group, G<"optparse.OptionGroup">()))
                            {
                                py_raise((B<"TypeError">())((S<"not an OptionGroup instance: %r">()) % group));
                            }
                            if (!group.attr("parser").is(self))
                            {
                                py_raise((B<"ValueError">())(S<"invalid OptionGroup (wrong parser)">()));
                            }
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"invalid arguments">()));
                        }
                        self.attr("option_groups").method("append", { group });
                        return group;
                    }, true, true) },
                { "get_option_group", w.method("OptionParser", "get_option_group", { py_positional("opt_str") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opt_str = a[1];
                        py option;
                        option = [&]() -> py { py const t = self.attr("_short_opt").method("get", { opt_str }); return t.truthy() ? t : self.attr("_long_opt").method("get", { opt_str }); }();
                        if ((option.truthy() && (!option.attr("container").is(self))))
                        {
                            return option.attr("container");
                        }
                        return none();
                    }) },
                { "_get_args", w.method("OptionParser", "_get_args", { py_positional("args") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        if (args.is_none())
                        {
                            return (Mod<"sys">().attr("argv"))[B<"slice">()(I<1>(), none(), none())];
                        }
                        else
                        {
                            return args[B<"slice">()(none(), none(), none())];
                        }
                        return none();
                    }) },
                { "parse_args", w.method("OptionParser", "parse_args", { py_positional("args", none()), py_positional("values", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py values = a[2];
                        py err;
                        py largs;
                        py rargs;
                        py stop;
                        rargs = self.method("_get_args", { args });
                        if (values.is_none())
                        {
                            values = self.method("get_default_values");
                        }
                        // Store the halves of the argument list as attributes for the
                        // convenience of callbacks:
                        // rargs
                        // the rest of the command-line (the "r" stands for
                        // "remaining" or "right-hand")
                        // largs
                        // the leftover arguments -- ie. what's left after removing
                        // options and their arguments (the "l" stands for "leftover"
                        // or "left-hand")
                        self.set_attr("rargs", rargs);
                        py const value37 = py_list();
                        self.set_attr("largs", value37);
                        largs = value37;
                        self.set_attr("values", values);
                        try
                        {
                            stop = self.method("_process_args", { largs, rargs, values });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler38;
                            if (matches(handler38.exception, py_tuple({ G<"optparse.BadOptionError">(), G<"optparse.OptionValueError">() })))
                            {
                                err = handler38.exception;
                                self.method("error", { (B<"str">())(err) });
                            }
                            else
                                py_reraise(handler38.exception);
                        }
                        args = largs + rargs;
                        return self.method("check_values", { values, args });
                    }) },
                { "check_values", w.method("OptionParser", "check_values", { py_positional("values"), py_positional("args") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py values = a[1];
                        py args = a[2];
                        return py_tuple({ values, args });
                    }) },
                { "_process_args", w.method("OptionParser", "_process_args", { py_positional("largs"), py_positional("rargs"), py_positional("values") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py largs = a[1];
                        py rargs = a[2];
                        py values = a[3];
                        py arg;
                        while (rargs.truthy())
                        {
                            arg = rargs[I<0>()];
                            // We handle bare "--" explicitly, and bare "-" is handled by the
                            // standard arg handler since the short arg case ensures that the
                            // len of the opt string is greater than 1.
                            if (arg == (S<"--">()))
                            {
                                del_item(rargs, I<0>());
                                return none();
                            }
                            else if (arg[B<"slice">()(I<0>(), I<2>(), none())] == (S<"--">()))
                            {
                                // process a single long option (possibly with value(s))
                                self.method("_process_long_opt", { rargs, values });
                            }
                            else if (((arg[B<"slice">()(none(), I<1>(), none())] == (S<"-">())) && (py{ arg.size() } > (I<1>()))))
                            {
                                // process a cluster of short options (possibly with
                                // value(s) for the last one only)
                                self.method("_process_short_opts", { rargs, values });
                            }
                            else if (self.attr("allow_interspersed_args").truthy())
                            {
                                largs.method("append", { arg });
                                del_item(rargs, I<0>());
                            }
                            else
                            {
                                return none();
                            }
                        }
                        return none();
                    }) },
                { "_match_long_opt", w.method("OptionParser", "_match_long_opt", { py_positional("opt") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opt = a[1];
                        return (G<"optparse._match_abbrev">())(opt, self.attr("_long_opt"));
                    }) },
                { "_process_long_opt", w.method("OptionParser", "_process_long_opt", { py_positional("rargs"), py_positional("values") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py rargs = a[1];
                        py values = a[2];
                        py arg;
                        py had_explicit_value;
                        py nargs;
                        py next_arg;
                        py opt;
                        py option;
                        py value;
                        arg = rargs.method("pop", { I<0>() });
                        // Value explicitly attached to arg?  Pretend it's the next
                        // argument.
                        if (arg.contains((S<"=">())))
                        {
                            auto const items40 = unpack(arg.method("split", { S<"=">(), I<1>() }), 2);
                            opt = items40[0];
                            next_arg = items40[1];
                            rargs.method("insert", { I<0>(), next_arg });
                            had_explicit_value = boolean(true);
                        }
                        else
                        {
                            opt = arg;
                            had_explicit_value = boolean(false);
                        }
                        opt = self.method("_match_long_opt", { opt });
                        option = self.attr("_long_opt")[opt];
                        if (option.method("takes_value").truthy())
                        {
                            nargs = option.attr("nargs");
                            if (py{ rargs.size() } < nargs)
                            {
                                self.method("error", { (Mod<"gettext">().attr("ngettext"))(S<"%(option)s option requires %(number)d argument">(), S<"%(option)s option requires %(number)d arguments">(), nargs) % dict_of({ { S<"option">(), opt }, { S<"number">(), nargs } }) });
                            }
                            else if (nargs == (I<1>()))
                            {
                                value = rargs.method("pop", { I<0>() });
                            }
                            else
                            {
                                value = (B<"tuple">())(rargs[B<"slice">()(I<0>(), nargs, none())]);
                                del_item(rargs, B<"slice">()(I<0>(), nargs, none()));
                            }
                        }
                        else if (had_explicit_value.truthy())
                        {
                            self.method("error", { (G<"optparse._">())(S<"%s option does not take a value">()) % opt });
                        }
                        else
                        {
                            value = none();
                        }
                        option.method("process", { opt, value, values, self });
                        return none();
                    }) },
                { "_process_short_opts", w.method("OptionParser", "_process_short_opts", { py_positional("rargs"), py_positional("values") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py rargs = a[1];
                        py values = a[2];
                        py arg;
                        py ch;
                        py i;
                        py nargs;
                        py opt;
                        py option;
                        py stop;
                        py value;
                        arg = rargs.method("pop", { I<0>() });
                        stop = boolean(false);
                        i = I<1>();
                        for (py const& item : iterate{ arg[B<"slice">()(I<1>(), none(), none())] })
                        {
                            ch = item;
                            opt = (S<"-">()) + ch;
                            option = self.attr("_short_opt").method("get", { opt });
                            i = inplace(NEOS_PY_ADD, i, I<1>());
                            if (!option.truthy())
                            {
                                py_raise((G<"optparse.BadOptionError">())(opt));
                            }
                            if (option.method("takes_value").truthy())
                            {
                                // Any characters left in arg?  Pretend they're the
                                // next arg, and stop consuming characters of arg.
                                if (i < py{ arg.size() })
                                {
                                    rargs.method("insert", { I<0>(), arg[B<"slice">()(i, none(), none())] });
                                    stop = boolean(true);
                                }
                                nargs = option.attr("nargs");
                                if (py{ rargs.size() } < nargs)
                                {
                                    self.method("error", { (Mod<"gettext">().attr("ngettext"))(S<"%(option)s option requires %(number)d argument">(), S<"%(option)s option requires %(number)d arguments">(), nargs) % dict_of({ { S<"option">(), opt }, { S<"number">(), nargs } }) });
                                }
                                else if (nargs == (I<1>()))
                                {
                                    value = rargs.method("pop", { I<0>() });
                                }
                                else
                                {
                                    value = (B<"tuple">())(rargs[B<"slice">()(I<0>(), nargs, none())]);
                                    del_item(rargs, B<"slice">()(I<0>(), nargs, none()));
                                }
                            }
                            else
                            {
                                value = none();
                            }
                            option.method("process", { opt, value, values, self });
                            if (stop.truthy())
                            {
                                break;
                            }
                        }
                        return none();
                    }) },
                { "get_prog_name", w.method("OptionParser", "get_prog_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("prog").is_none())
                        {
                            return (Mod<"os">().attr("path")).method("basename", { (Mod<"sys">().attr("argv"))[I<0>()] });
                        }
                        else
                        {
                            return self.attr("prog");
                        }
                        return none();
                    }) },
                { "expand_prog_name", w.method("OptionParser", "expand_prog_name", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        return s.method("replace", { S<"%prog">(), self.method("get_prog_name") });
                    }) },
                { "get_description", w.method("OptionParser", "get_description", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("expand_prog_name", { self.attr("description") });
                    }) },
                { "exit", w.method("OptionParser", "exit", { py_positional("status", I<0>()), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py status = a[1];
                        py msg = a[2];
                        if (msg.truthy())
                        {
                            (Mod<"sys">().attr("stderr")).method("write", { msg });
                        }
                        (Mod<"sys">().attr("exit"))(status);
                        return none();
                    }) },
                { "error", w.method("OptionParser", "error", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        self.method("print_usage", { Mod<"sys">().attr("stderr") });
                        self.method("exit", { I<2>(), (S<"%s: error: %s\n">()) % py_tuple({ self.method("get_prog_name"), msg }) });
                        return none();
                    }) },
                { "get_usage", w.method("OptionParser", "get_usage", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("usage").truthy())
                        {
                            return self.attr("formatter").method("format_usage", { self.method("expand_prog_name", { self.attr("usage") }) });
                        }
                        else
                        {
                            return S<"">();
                        }
                        return none();
                    }) },
                { "print_usage", w.method("OptionParser", "print_usage", { py_positional("file", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        if (self.attr("usage").truthy())
                        {
                            (B<"print">()).call({ self.method("get_usage") }, { { "file", file } });
                        }
                        return none();
                    }) },
                { "get_version", w.method("OptionParser", "get_version", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("version").truthy())
                        {
                            return self.method("expand_prog_name", { self.attr("version") });
                        }
                        else
                        {
                            return S<"">();
                        }
                        return none();
                    }) },
                { "print_version", w.method("OptionParser", "print_version", { py_positional("file", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        if (self.attr("version").truthy())
                        {
                            (B<"print">()).call({ self.method("get_version") }, { { "file", file } });
                        }
                        return none();
                    }) },
                { "format_option_help", w.method("OptionParser", "format_option_help", { py_positional("formatter", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py formatter = a[1];
                        py group;
                        py result;
                        if (formatter.is_none())
                        {
                            formatter = self.attr("formatter");
                        }
                        formatter.method("store_option_strings", { self });
                        result = py_list();
                        result.method("append", { formatter.method("format_heading", { (G<"optparse._">())(S<"Options">()) }) });
                        formatter.method("indent");
                        if (self.attr("option_list").truthy())
                        {
                            result.method("append", { (G<"optparse.OptionContainer">()).method("format_option_help", { self, formatter }) });
                            result.method("append", { S<"\n">() });
                        }
                        for (py const& item : iterate{ self.attr("option_groups") })
                        {
                            group = item;
                            result.method("append", { group.method("format_help", { formatter }) });
                            result.method("append", { S<"\n">() });
                        }
                        formatter.method("dedent");
                        // Drop the last "\n", or the header if no options or option groups:
                        return (S<"">()).method("join", { result[B<"slice">()(none(), I<-1>(), none())] });
                    }) },
                { "format_epilog", w.method("OptionParser", "format_epilog", { py_positional("formatter") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py formatter = a[1];
                        return formatter.method("format_epilog", { self.attr("epilog") });
                    }) },
                { "format_help", w.method("OptionParser", "format_help", { py_positional("formatter", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py formatter = a[1];
                        py result;
                        if (formatter.is_none())
                        {
                            formatter = self.attr("formatter");
                        }
                        result = py_list();
                        if (self.attr("usage").truthy())
                        {
                            result.method("append", { self.method("get_usage") + (S<"\n">()) });
                        }
                        if (self.attr("description").truthy())
                        {
                            result.method("append", { self.method("format_description", { formatter }) + (S<"\n">()) });
                        }
                        result.method("append", { self.method("format_option_help", { formatter }) });
                        result.method("append", { self.method("format_epilog", { formatter }) });
                        return (S<"">()).method("join", { result });
                    }) },
                { "print_help", w.method("OptionParser", "print_help", { py_positional("file", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        if (file.is_none())
                        {
                            file = Mod<"sys">().attr("stdout");
                        }
                        file.method("write", { self.method("format_help") });
                        return none();
                    }) }, }));

            // class OptionParser
            define("optparse", "_match_abbrev", w.function("_match_abbrev", "_match_abbrev", { py_positional("s"), py_positional("wordmap") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py wordmap = a[1];
                    py possibilities;
                    // Is there an exact match?
                    if (wordmap.contains(s))
                    {
                        return s;
                    }
                    else
                    {
                        // Isolate all words with s as a prefix.
                        possibilities = [&]() -> py { py const comprehension43 = py_list(); py word; for (py const& item : iterate{ wordmap.method("keys") }) { word = item; if (!word.method("startswith", { s }).truthy()) continue; comprehension43.method("append", { word }); } return comprehension43; }();
                        // No exact match, so there had better be just one possibility.
                        if (py{ possibilities.size() } == (I<1>()))
                        {
                            return possibilities[I<0>()];
                        }
                        else if (!possibilities.truthy())
                        {
                            py_raise((G<"optparse.BadOptionError">())(s));
                        }
                        else
                        {
                            // More than one possible completion: ambiguous prefix.
                            possibilities.method("sort");
                            py_raise((G<"optparse.AmbiguousOptionError">())(s, possibilities));
                        }
                    }
                    return none();
                }));

            // Some day, there might be many Option classes.  As of Optik 1.3, the
            // preferred way to instantiate Options is indirectly, via make_option(),
            // which will become a factory function when there are many Option
            // classes.
            define("optparse", "make_option", G<"optparse.Option">());
        }

        bool optparse_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_optparse();
                }
                if (auto const doc = optparse_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("textwrap", py{ fn_import("textwrap") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("optparse", optparse_module);
    }
}
