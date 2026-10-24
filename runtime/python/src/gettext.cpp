/*
  gettext.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/gettext.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module gettext (CPython 3.13's Lib/gettext.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "gettext_docs.hpp"

namespace neos::python_runtime::detail::gettext_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "gettext.name"))
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
                py_raise("SystemError", std::string{ "gettext: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"gettext.name">()))
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
            if (modules_imported("gettext"))
                return py{ fn_import("gettext") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "gettext" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "gettext", gettext_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- gettext (Lib/gettext.py)

        void make_gettext()
        {
            static module_writer const& w = writer("gettext");
            // This module represents the integration of work, contributions, feedback, and
            // suggestions from the following people:
            // 
            // Martin von Loewis, who wrote the initial implementation of the underlying
            // C-based libintlmodule (later renamed _gettext), along with a skeletal
            // gettext.py implementation.
            // 
            // Peter Funk, who wrote fintl.py, a fairly complete wrapper around intlmodule,
            // which also included a pure-Python implementation to read .mo files if
            // intlmodule wasn't available.
            // 
            // James Henstridge, who also wrote a gettext.py module, which has some
            // interesting, but currently unsupported experimental features: the notion of
            // a Catalog class and instances, and the ability to add to a catalog file via
            // a Python API.
            // 
            // Barry Warsaw integrated these modules, wrote the .install() API and code,
            // and conformed all C and Python code to Python's coding standards.
            // 
            // Francois Pinard and Marc-Andre Lemburg also contributed valuably to this
            // module.
            // 
            // J. David Ibanez implemented plural forms. Bruno Haible fixed some bugs.
            // 
            // TODO:
            // - Lazy loading of .mo files.  Currently the entire catalog is loaded into
            // memory, but that's probably bad for large translated programs.  Instead,
            // the lexical sort of original strings in GNU .mo files should be exploited
            // to do binary searches and lazy initializations.  Or you might want to use
            // the undocumented double-hash algorithm for .mo files with hash tables, but
            // you'll need to study the GNU gettext code to do this.
            // 
            // - Support Solaris .mo file formats.  Unfortunately, we've been unable to
            // find this format documented anywhere.
            define("gettext", "__all__", py_list({ S<"NullTranslations">(), S<"GNUTranslations">(), S<"Catalog">(), S<"bindtextdomain">(), S<"find">(), S<"translation">(), S<"install">(), S<"textdomain">(), S<"dgettext">(), S<"dngettext">(), S<"gettext">(), S<"ngettext">(), S<"pgettext">(), S<"dpgettext">(), S<"npgettext">(), S<"dnpgettext">() }));
            define("gettext", "_default_localedir", (Mod<"os">().attr("path")).method("join", { Mod<"sys">().attr("base_prefix"), S<"share">(), S<"locale">() }));
            // Expression parsing for plural form selection.
            // 
            // The gettext library supports a small subset of C syntax.  The only
            // incompatible difference is that integer literals starting with zero are
            // decimal.
            // 
            // https://www.gnu.org/software/gettext/manual/gettext.html#Plural-forms
            // http://git.savannah.gnu.org/cgit/gettext.git/tree/gettext-runtime/intl/plural.y
            define("gettext", "_token_pattern", (Mod<"re">()).method("compile", { S<"\n        (?P<WHITESPACES>[ \\t]+)                    | # spaces and horizontal tabs\n        (?P<NUMBER>[0-9]+\\b)                       | # decimal integer\n        (?P<NAME>n\\b)                              | # only n is allowed\n        (?P<PARENTHESIS>[()])                      |\n        (?P<OPERATOR>[-*/%+?:]|[><!]=?|==|&&|\\|\\|) | # !, *, /, %, +, -, <, >,\n                                                     # <=, >=, ==, !=, &&, ||,\n                                                     # ? :\n                                                     # unary and bitwise ops\n                                                     # not allowed\n        (?P<INVALID>\\w+|.)                           # invalid token\n    ">(), (Mod<"re">().attr("VERBOSE")) | (Mod<"re">().attr("DOTALL")) }));
            define("gettext", "_tokenize", w.function("_tokenize", "_tokenize", { py_positional("plural") }, [](arguments a) -> py
                {
                    return make_native_generator("_tokenize", "_tokenize", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py plural = a[0];
                        py kind;
                        py mo;
                        py value;
                        for (py const& item : iterate{ (Mod<"re">().attr("finditer"))(G<"gettext._token_pattern">(), plural) })
                        {
                            mo = item;
                            kind = mo.attr("lastgroup");
                            if (kind == (S<"WHITESPACES">()))
                            {
                                continue;
                            }
                            value = mo.method("group", { kind });
                            if (kind == (S<"INVALID">()))
                            {
                                py_raise((B<"ValueError">())((S<"invalid token in plural form: %s">()) % value));
                            }
                            py_yield(value);
                        }
                        py_yield(S<"">());
                        return none();
                    });
                }));

            define("gettext", "_error", w.function("_error", "_error", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    if (value.truthy())
                    {
                        return (B<"ValueError">())((S<"unexpected token in plural form: %s">()) % value);
                    }
                    else
                    {
                        return (B<"ValueError">())(S<"unexpected end of plural form">());
                    }
                    return none();
                }));

            define("gettext", "_binary_ops", py_tuple({ py_tuple({ S<"||">() }), py_tuple({ S<"&&">() }), py_tuple({ S<"==">(), S<"!=">() }), py_tuple({ S<"<">(), S<">">(), S<"<=">(), S<">=">() }), py_tuple({ S<"+">(), S<"-">() }), py_tuple({ S<"*">(), S<"/">(), S<"%">() }) }));
            define("gettext", "_binary_ops", [&]() -> py { py const comprehension2 = py_dict(); py i; py op; py ops; for (py const& item : iterate{ (B<"enumerate">())(G<"gettext._binary_ops">(), I<1>()) }) { auto const items3 = unpack(item, 2); i = items3[0]; ops = items3[1]; for (py const& item : iterate{ ops }) { op = item; comprehension2.set_item(op, i); } } return comprehension2; }());
            define("gettext", "_c2py_ops", dict_of({ { S<"||">(), S<"or">() }, { S<"&&">(), S<"and">() }, { S<"/">(), S<"//">() } }));
            define("gettext", "_parse", w.function("_parse", "_parse", { py_positional("tokens"), py_positional("priority", I<-1>()) }, [](arguments a) -> py
                {
                    py tokens = a[0];
                    py priority = a[1];
                    py i;
                    py if_false;
                    py if_true;
                    py j;
                    py nexttok;
                    py op;
                    py result;
                    py right;
                    py sub;
                    py value;
                    result = S<"">();
                    nexttok = (B<"next">())(tokens);
                    while (nexttok == (S<"!">()))
                    {
                        result = inplace(NEOS_PY_ADD, result, S<"not ">());
                        nexttok = (B<"next">())(tokens);
                    }
                    if (nexttok == (S<"(">()))
                    {
                        auto const items5 = unpack((G<"gettext._parse">())(tokens), 2);
                        sub = items5[0];
                        nexttok = items5[1];
                        result = (S<"%s(%s)">()) % py_tuple({ result, sub });
                        if (nexttok != (S<")">()))
                        {
                            py_raise((B<"ValueError">())(S<"unbalanced parenthesis in plural form">()));
                        }
                    }
                    else if (nexttok == (S<"n">()))
                    {
                        result = (S<"%s%s">()) % py_tuple({ result, nexttok });
                    }
                    else
                    {
                        try
                        {
                            value = (B<"int">())(nexttok, I<10>());
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler6;
                            if (matches(handler6.exception, B<"ValueError">()))
                            {
                                fn_raise_from((G<"gettext._error">())(nexttok).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler6.exception);
                        }
                        result = (S<"%s%d">()) % py_tuple({ result, value });
                    }
                    // Unary '!' binds tighter than binary operators in C, unlike 'not'.
                    if (result.method("startswith", { S<"not ">() }).truthy())
                    {
                        result = (S<"(%s)">()) % result;
                    }
                    nexttok = (B<"next">())(tokens);
                    j = I<100>();
                    while ((G<"gettext._binary_ops">()).contains(nexttok))
                    {
                        i = (G<"gettext._binary_ops">())[nexttok];
                        if (i < priority)
                        {
                            break;
                        }
                        // Break chained comparisons
                        if ((py_tuple({ I<3>(), I<4>() }).contains(i) && py_tuple({ I<3>(), I<4>() }).contains(j)))
                        {
                            result = (S<"(%s)">()) % result;
                        }
                        // Replace some C operators by their Python equivalents
                        op = (G<"gettext._c2py_ops">()).method("get", { nexttok, nexttok });
                        auto const items8 = unpack((G<"gettext._parse">())(tokens, i + (I<1>())), 2);
                        right = items8[0];
                        nexttok = items8[1];
                        result = (S<"%s %s %s">()) % py_tuple({ result, op, right });
                        j = i;
                    }
                    if ((j == priority) && (priority == (I<4>())))
                    {
                        result = (S<"(%s)">()) % result;
                    }
                    if (((nexttok == (S<"?">())) && (priority <= (I<0>()))))
                    {
                        auto const items9 = unpack((G<"gettext._parse">())(tokens, I<0>()), 2);
                        if_true = items9[0];
                        nexttok = items9[1];
                        if (nexttok != (S<":">()))
                        {
                            py_raise((G<"gettext._error">())(nexttok));
                        }
                        auto const items10 = unpack((G<"gettext._parse">())(tokens), 2);
                        if_false = items10[0];
                        nexttok = items10[1];
                        result = (S<"%s if %s else %s">()) % py_tuple({ if_true, result, if_false });
                        if (priority == (I<0>()))
                        {
                            result = (S<"(%s)">()) % result;
                        }
                    }
                    return py_tuple({ result, nexttok });
                }));

            define("gettext", "_as_int", w.function("_as_int", "_as_int", { py_positional("n") }, [](arguments a) -> py
                {
                    py n = a[0];
                    try
                    {
                        (B<"round">())(n);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler11;
                        if (matches(handler11.exception, B<"TypeError">()))
                        {
                            fn_raise_from((B<"TypeError">())((S<"Plural value must be an integer, got %s">()) % py_tuple({ n.attr("__class__").attr("__name__") })).get(), none().get());
                            throw raised{};
                        }
                        else
                            py_reraise(handler11.exception);
                    }
                    return (G<"gettext._as_int2">())(n);
                }));

            define("gettext", "_as_int2", w.function("_as_int2", "_as_int2", { py_positional("n") }, [](arguments a) -> py
                {
                    py n = a[0];
                    py frame;
                    py stacklevel;
                    py warnings;
                    try
                    {
                        return (Mod<"operator">().attr("index"))(n);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler12;
                        if (matches(handler12.exception, B<"TypeError">()))
                        {
                        }
                        else
                            py_reraise(handler12.exception);
                    }
                    warnings = module_of("warnings");
                    frame = (Mod<"sys">().attr("_getframe"))(I<0>());
                    stacklevel = I<2>();
                    while (((!frame.attr("f_back").is_none()) && (frame.attr("f_globals").method("get", { S<"__name__">() }) == (S<"gettext">()))))
                    {
                        stacklevel = inplace(NEOS_PY_ADD, stacklevel, I<1>());
                        frame = frame.attr("f_back");
                    }
                    warnings.method("warn", { (S<"Plural value must be an integer, got %s">()) % py_tuple({ n.attr("__class__").attr("__name__") }), B<"DeprecationWarning">(), stacklevel });
                    return n;
                }));

            define("gettext", "c2py", w.function("c2py", "c2py", { py_positional("plural") }, [](arguments a) -> py
                {
                    py plural = a[0];
                    py c;
                    py depth;
                    py nexttok;
                    py ns;
                    py result;
                    if (py{ plural.size() } > (I<1000>()))
                    {
                        py_raise((B<"ValueError">())(S<"plural form expression is too long">()));
                    }
                    try
                    {
                        auto const items15 = unpack((G<"gettext._parse">())((G<"gettext._tokenize">())(plural)), 2);
                        result = items15[0];
                        nexttok = items15[1];
                        if (nexttok.truthy())
                        {
                            py_raise((G<"gettext._error">())(nexttok));
                        }
                        depth = I<0>();
                        for (py const& item : iterate{ result })
                        {
                            c = item;
                            if (c == (S<"(">()))
                            {
                                depth = inplace(NEOS_PY_ADD, depth, I<1>());
                                if (depth > (I<20>()))
                                {
                                    // Python compiler limit is about 90.
                                    // The most complex example has 2.
                                    py_raise((B<"ValueError">())(S<"plural form expression is too complex">()));
                                }
                            }
                            else if (c == (S<")">()))
                            {
                                depth = inplace(NEOS_PY_SUBTRACT, depth, I<1>());
                            }
                        }
                        ns = dict_of({ { S<"_as_int">(), G<"gettext._as_int">() }, { S<"__name__">(), S<"gettext">() } });
                        (B<"exec">())((S<"if True:\n            def func(n):\n                if not isinstance(n, int):\n                    n = _as_int(n)\n                return int(%s)\n            ">()) % result, ns);
                        return ns[S<"func">()];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler14;
                        if (matches(handler14.exception, B<"RecursionError">()))
                        {
                            // Recursion error can be raised in _parse() or exec().
                            py_raise((B<"ValueError">())(S<"plural form expression is too complex">()));
                        }
                        else
                            py_reraise(handler14.exception);
                    }
                    return none();
                }));

            define("gettext", "_expand_lang", w.function("_expand_lang", "_expand_lang", { py_positional("loc") }, [](arguments a) -> py
                {
                    py loc = a[0];
                    py COMPONENT_CODESET;
                    py COMPONENT_MODIFIER;
                    py COMPONENT_TERRITORY;
                    py codeset;
                    py i;
                    py language;
                    py locale;
                    py mask;
                    py modifier;
                    py pos;
                    py ret;
                    py territory;
                    py val;
                    locale = module_of("locale");
                    loc = locale.method("normalize", { loc });
                    COMPONENT_CODESET = (I<1>()) << (I<0>());
                    COMPONENT_TERRITORY = (I<1>()) << (I<1>());
                    COMPONENT_MODIFIER = (I<1>()) << (I<2>());
                    // split up the locale into its base components
                    mask = I<0>();
                    pos = loc.method("find", { S<"@">() });
                    if (pos >= (I<0>()))
                    {
                        modifier = loc[B<"slice">()(pos, none(), none())];
                        loc = loc[B<"slice">()(none(), pos, none())];
                        mask = inplace(NEOS_PY_OR, mask, COMPONENT_MODIFIER);
                    }
                    else
                    {
                        modifier = S<"">();
                    }
                    pos = loc.method("find", { S<".">() });
                    if (pos >= (I<0>()))
                    {
                        codeset = loc[B<"slice">()(pos, none(), none())];
                        loc = loc[B<"slice">()(none(), pos, none())];
                        mask = inplace(NEOS_PY_OR, mask, COMPONENT_CODESET);
                    }
                    else
                    {
                        codeset = S<"">();
                    }
                    pos = loc.method("find", { S<"_">() });
                    if (pos >= (I<0>()))
                    {
                        territory = loc[B<"slice">()(pos, none(), none())];
                        loc = loc[B<"slice">()(none(), pos, none())];
                        mask = inplace(NEOS_PY_OR, mask, COMPONENT_TERRITORY);
                    }
                    else
                    {
                        territory = S<"">();
                    }
                    language = loc;
                    ret = py_list();
                    for (py const& item : iterate{ (B<"range">())(mask + (I<1>())) })
                    {
                        i = item;
                        if (!(i & py{ unary(NEOS_PY_INVERT, mask.get()) }).truthy())
                        {
                            val = language;
                            if ((i & COMPONENT_TERRITORY).truthy())
                            {
                                val = inplace(NEOS_PY_ADD, val, territory);
                            }
                            if ((i & COMPONENT_CODESET).truthy())
                            {
                                val = inplace(NEOS_PY_ADD, val, codeset);
                            }
                            if ((i & COMPONENT_MODIFIER).truthy())
                            {
                                val = inplace(NEOS_PY_ADD, val, modifier);
                            }
                            ret.method("append", { val });
                        }
                    }
                    ret.method("reverse");
                    return ret;
                }));

            define("gettext", "NullTranslations", w.make_class("NullTranslations", {  }, {
                { "__init__", w.method("NullTranslations", "__init__", { py_positional("fp", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        self.set_attr("_info", py_dict());
                        self.set_attr("_charset", none());
                        self.set_attr("_fallback", none());
                        if (!fp.is_none())
                        {
                            self.method("_parse", { fp });
                        }
                        return none();
                    }) },
                { "_parse", w.method("NullTranslations", "_parse", { py_positional("fp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        return none();
                    }) },
                { "add_fallback", w.method("NullTranslations", "add_fallback", { py_positional("fallback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fallback = a[1];
                        if (self.attr("_fallback").truthy())
                        {
                            self.attr("_fallback").method("add_fallback", { fallback });
                        }
                        else
                        {
                            self.set_attr("_fallback", fallback);
                        }
                        return none();
                    }) },
                { "gettext", w.method("NullTranslations", "gettext", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        if (self.attr("_fallback").truthy())
                        {
                            return self.attr("_fallback").method("gettext", { message });
                        }
                        return message;
                    }) },
                { "ngettext", w.method("NullTranslations", "ngettext", { py_positional("msgid1"), py_positional("msgid2"), py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msgid1 = a[1];
                        py msgid2 = a[2];
                        py n = a[3];
                        if (self.attr("_fallback").truthy())
                        {
                            return self.attr("_fallback").method("ngettext", { msgid1, msgid2, n });
                        }
                        n = (G<"gettext._as_int2">())(n);
                        if (n == (I<1>()))
                        {
                            return msgid1;
                        }
                        else
                        {
                            return msgid2;
                        }
                        return none();
                    }) },
                { "pgettext", w.method("NullTranslations", "pgettext", { py_positional("context"), py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py context = a[1];
                        py message = a[2];
                        if (self.attr("_fallback").truthy())
                        {
                            return self.attr("_fallback").method("pgettext", { context, message });
                        }
                        return message;
                    }) },
                { "npgettext", w.method("NullTranslations", "npgettext", { py_positional("context"), py_positional("msgid1"), py_positional("msgid2"), py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py context = a[1];
                        py msgid1 = a[2];
                        py msgid2 = a[3];
                        py n = a[4];
                        if (self.attr("_fallback").truthy())
                        {
                            return self.attr("_fallback").method("npgettext", { context, msgid1, msgid2, n });
                        }
                        n = (G<"gettext._as_int2">())(n);
                        if (n == (I<1>()))
                        {
                            return msgid1;
                        }
                        else
                        {
                            return msgid2;
                        }
                        return none();
                    }) },
                { "info", w.method("NullTranslations", "info", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_info");
                    }) },
                { "charset", w.method("NullTranslations", "charset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_charset");
                    }) },
                { "install", w.method("NullTranslations", "install", { py_positional("names", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py names = a[1];
                        py allowed;
                        py builtins;
                        py name;
                        builtins = module_of("builtins");
                        builtins.attr("__dict__").set_item(S<"_">(), self.attr("gettext"));
                        if (!names.is_none())
                        {
                            allowed = set_of({ S<"gettext">(), S<"ngettext">(), S<"npgettext">(), S<"pgettext">() });
                            for (py const& item : iterate{ allowed & (B<"set">())(names) })
                            {
                                name = item;
                                builtins.attr("__dict__").set_item(name, (B<"getattr">())(self, name));
                            }
                        }
                        return none();
                    }) }, }));

                // Magic number of .mo files
                // The encoding of a msgctxt and a msgid in a .mo file is
                // msgctxt + "\x04" + msgid (gettext version >= 0.15)
                // Acceptable .mo versions
            define("gettext", "GNUTranslations", w.make_class("GNUTranslations", { G<"gettext.NullTranslations">() }, {
                { "LE_MAGIC", I<2500072158ll>() },
                { "BE_MAGIC", I<3725722773ll>() },
                { "CONTEXT", S<"%s\u0004%s">() },
                { "VERSIONS", py_tuple({ I<0>(), I<1>() }) },
                { "_get_versions", w.method("GNUTranslations", "_get_versions", { py_positional("version") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py version = a[1];
                        return py_tuple({ version >> (I<16>()), version & (I<65535>()) });
                    }) },
                { "_parse", w.method("GNUTranslations", "_parse", { py_positional("fp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py b_item;
                        py buf;
                        py buflen;
                        py catalog;
                        py charset;
                        py filename;
                        py i;
                        py ii;
                        py item_;
                        py k;
                        py lastk;
                        py magic;
                        py major_version;
                        py masteridx;
                        py mend;
                        py minor_version;
                        py mlen;
                        py moff;
                        py msg;
                        py msgcount;
                        py msgid1;
                        py msgid2;
                        py plural;
                        py tend;
                        py tlen;
                        py tmsg;
                        py toff;
                        py transidx;
                        py unpack_;
                        py v;
                        py version;
                        py x;
                        // Delay struct import for speeding up gettext import when .mo files
                        // are not used.
                        unpack_ = M("struct", "unpack");
                        filename = (B<"getattr">())(fp, S<"name">(), S<"">());
                        // Parse the .mo file header, which consists of 5 little endian 32
                        // bit words.
                        py const value19 = py_dict();
                        self.set_attr("_catalog", value19);
                        catalog = value19;
                        self.set_attr("plural", w.function("<lambda>", "GNUTranslations._parse.<locals>.<lambda>", { py_positional("n") }, [](arguments a) -> py
    {
        py n = a[0];
        return (B<"int">())(boolean(n != (I<1>())));
    }));
                        buf = fp.method("read");
                        buflen = py{ buf.size() };
                        // Are we big endian or little endian?
                        magic = unpack_(S<"<I">(), buf[B<"slice">()(none(), I<4>(), none())])[I<0>()];
                        if (magic == self.attr("LE_MAGIC"))
                        {
                            auto const items20 = unpack(unpack_(S<"<4I">(), buf[B<"slice">()(I<4>(), I<20>(), none())]), 4);
                            version = items20[0];
                            msgcount = items20[1];
                            masteridx = items20[2];
                            transidx = items20[3];
                            ii = S<"<II">();
                        }
                        else if (magic == self.attr("BE_MAGIC"))
                        {
                            auto const items21 = unpack(unpack_(S<">4I">(), buf[B<"slice">()(I<4>(), I<20>(), none())]), 4);
                            version = items21[0];
                            msgcount = items21[1];
                            masteridx = items21[2];
                            transidx = items21[3];
                            ii = S<">II">();
                        }
                        else
                        {
                            py_raise((B<"OSError">())(I<0>(), S<"Bad magic number">(), filename));
                        }
                        auto const items22 = unpack(self.method("_get_versions", { version }), 2);
                        major_version = items22[0];
                        minor_version = items22[1];
                        if (!self.attr("VERSIONS").contains(major_version))
                        {
                            py_raise((B<"OSError">())(I<0>(), (S<"Bad version number ">()) + (B<"str">())(major_version), filename));
                        }
                        // Now put all messages from the .mo file buffer into the catalog
                        // dictionary.
                        for (py const& item : iterate{ (B<"range">())(I<0>(), msgcount) })
                        {
                            i = item;
                            auto const items24 = unpack(unpack_(ii, buf[B<"slice">()(masteridx, masteridx + (I<8>()), none())]), 2);
                            mlen = items24[0];
                            moff = items24[1];
                            mend = moff + mlen;
                            auto const items25 = unpack(unpack_(ii, buf[B<"slice">()(transidx, transidx + (I<8>()), none())]), 2);
                            tlen = items25[0];
                            toff = items25[1];
                            tend = toff + tlen;
                            if (((mend < buflen) && (tend < buflen)))
                            {
                                msg = buf[B<"slice">()(moff, mend, none())];
                                tmsg = buf[B<"slice">()(toff, tend, none())];
                            }
                            else
                            {
                                py_raise((B<"OSError">())(I<0>(), S<"File is corrupt">(), filename));
                            }
                            // See if we're looking at GNU .mo conventions for metadata
                            if (mlen == (I<0>()))
                            {
                                // Catalog description
                                lastk = none();
                                for (py const& item : iterate{ tmsg.method("split", { py{ make_bytes(std::string{ "\x0a", 1 }) } }) })
                                {
                                    b_item = item;
                                    item_ = b_item.method("decode").method("strip");
                                    if (!item_.truthy())
                                    {
                                        continue;
                                    }
                                    // Skip over comment lines:
                                    if ((item_.method("startswith", { S<"#-#-#-#-#">() }).truthy() && item_.method("endswith", { S<"#-#-#-#-#">() }).truthy()))
                                    {
                                        continue;
                                    }
                                    py const value27 = none();
                                    k = value27;
                                    v = value27;
                                    if (item_.contains((S<":">())))
                                    {
                                        auto const items28 = unpack(item_.method("split", { S<":">(), I<1>() }), 2);
                                        k = items28[0];
                                        v = items28[1];
                                        k = k.method("strip").method("lower");
                                        v = v.method("strip");
                                        self.attr("_info").set_item(k, v);
                                        lastk = k;
                                    }
                                    else if (lastk.truthy())
                                    {
                                        {
                                            py const target29 = self.attr("_info");
                                            py const key29 = lastk;
                                            target29.set_item(key29, inplace(NEOS_PY_ADD, target29[key29], (S<"\n">()) + item_));
                                        }
                                    }
                                    if (k == (S<"content-type">()))
                                    {
                                        self.set_attr("_charset", v.method("split", { S<"charset=">() })[I<1>()]);
                                    }
                                    else if (k == (S<"plural-forms">()))
                                    {
                                        v = v.method("split", { S<";">() });
                                        plural = v[I<1>()].method("split", { S<"plural=">() })[I<1>()];
                                        self.set_attr("plural", (G<"gettext.c2py">())(plural));
                                    }
                                }
                            }
                            // Note: we unconditionally convert both msgids and msgstrs to
                            // Unicode using the character encoding specified in the charset
                            // parameter of the Content-Type header.  The gettext documentation
                            // strongly encourages msgids to be us-ascii, but some applications
                            // require alternative encodings (e.g. Zope's ZCML and ZPT).  For
                            // traditional gettext applications, the msgid conversion will
                            // cause no problems since us-ascii should always be a subset of
                            // the charset encoding.  We may want to fall back to 8-bit msgids
                            // if the Unicode conversion fails.
                            charset = [&]() -> py { py const t = self.attr("_charset"); return t.truthy() ? t : S<"ascii">(); }();
                            if (msg.contains(py{ make_bytes(std::string{ "\x00", 1 }) }))
                            {
                                // Plural forms
                                auto const items30 = unpack(msg.method("split", { py{ make_bytes(std::string{ "\x00", 1 }) } }), 2);
                                msgid1 = items30[0];
                                msgid2 = items30[1];
                                tmsg = tmsg.method("split", { py{ make_bytes(std::string{ "\x00", 1 }) } });
                                msgid1 = (B<"str">())(msgid1, charset);
                                for (py const& item : iterate{ (B<"enumerate">())(tmsg) })
                                {
                                    auto const items32 = unpack(item, 2);
                                    i = items32[0];
                                    x = items32[1];
                                    catalog.set_item(py_tuple({ msgid1, i }), (B<"str">())(x, charset));
                                }
                            }
                            else
                            {
                                catalog.set_item((B<"str">())(msg, charset), (B<"str">())(tmsg, charset));
                            }
                            // advance to next entry in the seek tables
                            masteridx = inplace(NEOS_PY_ADD, masteridx, I<8>());
                            transidx = inplace(NEOS_PY_ADD, transidx, I<8>());
                        }
                        return none();
                    }) },
                { "gettext", w.method("GNUTranslations", "gettext", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py missing;
                        py tmsg;
                        missing = (B<"object">())();
                        tmsg = self.attr("_catalog").method("get", { message, missing });
                        if (tmsg.is(missing))
                        {
                            tmsg = self.attr("_catalog").method("get", { py_tuple({ message, self.method("plural", { I<1>() }) }), missing });
                        }
                        if (!tmsg.is(missing))
                        {
                            return tmsg;
                        }
                        if (self.attr("_fallback").truthy())
                        {
                            return self.attr("_fallback").method("gettext", { message });
                        }
                        return message;
                    }) },
                { "ngettext", w.method("GNUTranslations", "ngettext", { py_positional("msgid1"), py_positional("msgid2"), py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msgid1 = a[1];
                        py msgid2 = a[2];
                        py n = a[3];
                        py tmsg;
                        try
                        {
                            tmsg = self.attr("_catalog")[py_tuple({ msgid1, self.method("plural", { n }) })];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler33;
                            if (matches(handler33.exception, B<"KeyError">()))
                            {
                                if (self.attr("_fallback").truthy())
                                {
                                    return self.attr("_fallback").method("ngettext", { msgid1, msgid2, n });
                                }
                                if (n == (I<1>()))
                                {
                                    tmsg = msgid1;
                                }
                                else
                                {
                                    tmsg = msgid2;
                                }
                            }
                            else
                                py_reraise(handler33.exception);
                        }
                        return tmsg;
                    }) },
                { "pgettext", w.method("GNUTranslations", "pgettext", { py_positional("context"), py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py context = a[1];
                        py message = a[2];
                        py ctxt_msg_id;
                        py missing;
                        py tmsg;
                        ctxt_msg_id = self.attr("CONTEXT") % py_tuple({ context, message });
                        missing = (B<"object">())();
                        tmsg = self.attr("_catalog").method("get", { ctxt_msg_id, missing });
                        if (tmsg.is(missing))
                        {
                            tmsg = self.attr("_catalog").method("get", { py_tuple({ ctxt_msg_id, self.method("plural", { I<1>() }) }), missing });
                        }
                        if (!tmsg.is(missing))
                        {
                            return tmsg;
                        }
                        if (self.attr("_fallback").truthy())
                        {
                            return self.attr("_fallback").method("pgettext", { context, message });
                        }
                        return message;
                    }) },
                { "npgettext", w.method("GNUTranslations", "npgettext", { py_positional("context"), py_positional("msgid1"), py_positional("msgid2"), py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py context = a[1];
                        py msgid1 = a[2];
                        py msgid2 = a[3];
                        py n = a[4];
                        py ctxt_msg_id;
                        py tmsg;
                        ctxt_msg_id = self.attr("CONTEXT") % py_tuple({ context, msgid1 });
                        try
                        {
                            tmsg = self.attr("_catalog")[py_tuple({ ctxt_msg_id, self.method("plural", { n }) })];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler34;
                            if (matches(handler34.exception, B<"KeyError">()))
                            {
                                if (self.attr("_fallback").truthy())
                                {
                                    return self.attr("_fallback").method("npgettext", { context, msgid1, msgid2, n });
                                }
                                if (n == (I<1>()))
                                {
                                    tmsg = msgid1;
                                }
                                else
                                {
                                    tmsg = msgid2;
                                }
                            }
                            else
                                py_reraise(handler34.exception);
                        }
                        return tmsg;
                    }) }, }));

            // Locate a .mo file using the gettext strategy
            define("gettext", "find", w.function("find", "find", { py_positional("domain"), py_positional("localedir", none()), py_positional("languages", none()), py_positional("all", boolean(false)) }, [](arguments a) -> py
                {
                    py domain = a[0];
                    py localedir = a[1];
                    py languages = a[2];
                    py all = a[3];
                    py envar;
                    py lang;
                    py mofile;
                    py nelang;
                    py nelangs;
                    py result;
                    py val;
                    if (localedir.is_none())
                    {
                        localedir = G<"gettext._default_localedir">();
                    }
                    if (languages.is_none())
                    {
                        languages = py_list();
                        for (py const& item : iterate{ py_tuple({ S<"LANGUAGE">(), S<"LC_ALL">(), S<"LC_MESSAGES">(), S<"LANG">() }) })
                        {
                            envar = item;
                            val = (Mod<"os">().attr("environ")).method("get", { envar });
                            if (val.truthy())
                            {
                                languages = val.method("split", { S<":">() });
                                break;
                            }
                        }
                        if (!languages.contains((S<"C">())))
                        {
                            languages.method("append", { S<"C">() });
                        }
                    }
                    // now normalize and expand the languages
                    nelangs = py_list();
                    for (py const& item : iterate{ languages })
                    {
                        lang = item;
                        for (py const& item : iterate{ (G<"gettext._expand_lang">())(lang) })
                        {
                            nelang = item;
                            if (!nelangs.contains(nelang))
                            {
                                nelangs.method("append", { nelang });
                            }
                        }
                    }
                    // select a language
                    if (all.truthy())
                    {
                        result = py_list();
                    }
                    else
                    {
                        result = none();
                    }
                    for (py const& item : iterate{ nelangs })
                    {
                        lang = item;
                        if (lang == (S<"C">()))
                        {
                            break;
                        }
                        mofile = (Mod<"os">().attr("path")).method("join", { localedir, lang, S<"LC_MESSAGES">(), (S<"%s.mo">()) % domain });
                        if ((Mod<"os">().attr("path")).method("exists", { mofile }).truthy())
                        {
                            if (all.truthy())
                            {
                                result.method("append", { mofile });
                            }
                            else
                            {
                                return mofile;
                            }
                        }
                    }
                    return result;
                }));

            // a mapping between absolute .mo file path and Translation object
            define("gettext", "_translations", py_dict());
            define("gettext", "translation", w.function("translation", "translation", { py_positional("domain"), py_positional("localedir", none()), py_positional("languages", none()), py_positional("class_", none()), py_positional("fallback", boolean(false)) }, [](arguments a) -> py
                {
                    py domain = a[0];
                    py localedir = a[1];
                    py languages = a[2];
                    py class_ = a[3];
                    py fallback = a[4];
                    py ENOENT_;
                    py copy;
                    py fp;
                    py key;
                    py mofile;
                    py mofiles;
                    py result;
                    py t;
                    if (class_.is_none())
                    {
                        class_ = G<"gettext.GNUTranslations">();
                    }
                    mofiles = (G<"gettext.find">()).call({ domain, localedir, languages }, { { "all", boolean(true) } });
                    if (!mofiles.truthy())
                    {
                        if (fallback.truthy())
                        {
                            return (G<"gettext.NullTranslations">())();
                        }
                        ENOENT_ = M("errno", "ENOENT");
                        py_raise((B<"FileNotFoundError">())(ENOENT_, S<"No translation file found for domain">(), domain));
                    }
                    // Avoid opening, reading, and parsing the .mo file after it's been done
                    // once.
                    result = none();
                    for (py const& item : iterate{ mofiles })
                    {
                        mofile = item;
                        key = py_tuple({ class_, (Mod<"os">().attr("path")).method("abspath", { mofile }) });
                        t = (G<"gettext._translations">()).method("get", { key });
                        if (t.is_none())
                        {
                            {
                                py const manager40 = (B<"open">())(mofile, S<"rb">());
                                py const exit40 = pyclass::special_method(manager40, "__exit__");
                                py const value40 = pyclass::special_method(manager40, "__enter__")(manager40);
                                fp = value40;
                                [[maybe_unused]] int flow40 = 0;
                                [[maybe_unused]] py result40;
                                bool exited40 = false;
                                try
                                {
                                    t = (G<"gettext._translations">()).method("setdefault", { key, class_(fp) });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler40;
                                    exited40 = true;
                                    if (!exit40(manager40, type_of(handler40.exception), handler40.exception, handler40.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler40.exception);
                                }
                                if (!exited40)
                                    exit40(manager40, none(), none(), none());
                                if (flow40 == 1)
                                    return result40;
                                if (flow40 == 2)
                                    break;
                                if (flow40 == 3)
                                    continue;
                            }
                        }
                        // Copy the translation object to allow setting fallbacks and
                        // output charset. All other instance data is shared with the
                        // cached object.
                        // Delay copy import for speeding up gettext import when .mo files
                        // are not used.
                        copy = module_of("copy");
                        t = copy.method("copy", { t });
                        if (result.is_none())
                        {
                            result = t;
                        }
                        else
                        {
                            result.method("add_fallback", { t });
                        }
                    }
                    return result;
                }));

            define("gettext", "install", w.function("install", "install", { py_positional("domain"), py_positional("localedir", none()), py_keyword("names", none()) }, [](arguments a) -> py
                {
                    py domain = a[0];
                    py localedir = a[1];
                    py names = a[2];
                    py t;
                    t = (G<"gettext.translation">()).call({ domain, localedir }, { { "fallback", boolean(true) } });
                    t.method("install", { names });
                    return none();
                }));

            // a mapping b/w domains and locale directories
            define("gettext", "_localedirs", py_dict());
            // current global domain, `messages' used for compatibility w/ GNU gettext
            define("gettext", "_current_domain", S<"messages">());
            define("gettext", "textdomain", w.function("textdomain", "textdomain", { py_positional("domain", none()) }, [](arguments a) -> py
                {
                    py domain = a[0];
                    if (!domain.is_none())
                    {
                        set_global("gettext", "_current_domain", domain);
                    }
                    return G<"gettext._current_domain">();
                }));

            define("gettext", "bindtextdomain", w.function("bindtextdomain", "bindtextdomain", { py_positional("domain"), py_positional("localedir", none()) }, [](arguments a) -> py
                {
                    py domain = a[0];
                    py localedir = a[1];
                    if (!localedir.is_none())
                    {
                        (G<"gettext._localedirs">()).set_item(domain, localedir);
                    }
                    return (G<"gettext._localedirs">()).method("get", { domain, G<"gettext._default_localedir">() });
                }));

            define("gettext", "dgettext", w.function("dgettext", "dgettext", { py_positional("domain"), py_positional("message") }, [](arguments a) -> py
                {
                    py domain = a[0];
                    py message = a[1];
                    py t;
                    try
                    {
                        t = (G<"gettext.translation">())(domain, (G<"gettext._localedirs">()).method("get", { domain, none() }));
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler41;
                        if (matches(handler41.exception, B<"OSError">()))
                        {
                            return message;
                        }
                        else
                            py_reraise(handler41.exception);
                    }
                    return t.method("gettext", { message });
                }));

            define("gettext", "dngettext", w.function("dngettext", "dngettext", { py_positional("domain"), py_positional("msgid1"), py_positional("msgid2"), py_positional("n") }, [](arguments a) -> py
                {
                    py domain = a[0];
                    py msgid1 = a[1];
                    py msgid2 = a[2];
                    py n = a[3];
                    py t;
                    try
                    {
                        t = (G<"gettext.translation">())(domain, (G<"gettext._localedirs">()).method("get", { domain, none() }));
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler42;
                        if (matches(handler42.exception, B<"OSError">()))
                        {
                            n = (G<"gettext._as_int2">())(n);
                            if (n == (I<1>()))
                            {
                                return msgid1;
                            }
                            else
                            {
                                return msgid2;
                            }
                        }
                        else
                            py_reraise(handler42.exception);
                    }
                    return t.method("ngettext", { msgid1, msgid2, n });
                }));

            define("gettext", "dpgettext", w.function("dpgettext", "dpgettext", { py_positional("domain"), py_positional("context"), py_positional("message") }, [](arguments a) -> py
                {
                    py domain = a[0];
                    py context = a[1];
                    py message = a[2];
                    py t;
                    try
                    {
                        t = (G<"gettext.translation">())(domain, (G<"gettext._localedirs">()).method("get", { domain, none() }));
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler43;
                        if (matches(handler43.exception, B<"OSError">()))
                        {
                            return message;
                        }
                        else
                            py_reraise(handler43.exception);
                    }
                    return t.method("pgettext", { context, message });
                }));

            define("gettext", "dnpgettext", w.function("dnpgettext", "dnpgettext", { py_positional("domain"), py_positional("context"), py_positional("msgid1"), py_positional("msgid2"), py_positional("n") }, [](arguments a) -> py
                {
                    py domain = a[0];
                    py context = a[1];
                    py msgid1 = a[2];
                    py msgid2 = a[3];
                    py n = a[4];
                    py t;
                    try
                    {
                        t = (G<"gettext.translation">())(domain, (G<"gettext._localedirs">()).method("get", { domain, none() }));
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler44;
                        if (matches(handler44.exception, B<"OSError">()))
                        {
                            n = (G<"gettext._as_int2">())(n);
                            if (n == (I<1>()))
                            {
                                return msgid1;
                            }
                            else
                            {
                                return msgid2;
                            }
                        }
                        else
                            py_reraise(handler44.exception);
                    }
                    return t.method("npgettext", { context, msgid1, msgid2, n });
                }));

            define("gettext", "gettext", w.function("gettext", "gettext", { py_positional("message") }, [](arguments a) -> py
                {
                    py message = a[0];
                    return (G<"gettext.dgettext">())(G<"gettext._current_domain">(), message);
                }));

            define("gettext", "ngettext", w.function("ngettext", "ngettext", { py_positional("msgid1"), py_positional("msgid2"), py_positional("n") }, [](arguments a) -> py
                {
                    py msgid1 = a[0];
                    py msgid2 = a[1];
                    py n = a[2];
                    return (G<"gettext.dngettext">())(G<"gettext._current_domain">(), msgid1, msgid2, n);
                }));

            define("gettext", "pgettext", w.function("pgettext", "pgettext", { py_positional("context"), py_positional("message") }, [](arguments a) -> py
                {
                    py context = a[0];
                    py message = a[1];
                    return (G<"gettext.dpgettext">())(G<"gettext._current_domain">(), context, message);
                }));

            define("gettext", "npgettext", w.function("npgettext", "npgettext", { py_positional("context"), py_positional("msgid1"), py_positional("msgid2"), py_positional("n") }, [](arguments a) -> py
                {
                    py context = a[0];
                    py msgid1 = a[1];
                    py msgid2 = a[2];
                    py n = a[3];
                    return (G<"gettext.dnpgettext">())(G<"gettext._current_domain">(), context, msgid1, msgid2, n);
                }));

            // dcgettext() has been deemed unnecessary and is not implemented.
            // James Henstridge's Catalog constructor from GNOME gettext.  Documented usage
            // was:
            // 
            // import gettext
            // cat = gettext.Catalog(PACKAGE, localedir=LOCALEDIR)
            // _ = cat.gettext
            // print _('Hello World')
            // The resulting catalog object currently don't support access through a
            // dictionary API, which was supported (but apparently unused) in GNOME
            // gettext.
            define("gettext", "Catalog", G<"gettext.translation">());
        }

        bool gettext_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_gettext();
                }
                if (auto const doc = gettext_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("operator", py{ fn_import("operator") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("gettext", gettext_module);
    }
}
