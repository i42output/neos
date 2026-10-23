/*
  token.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/token.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module token (CPython 3.13's Lib/token.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "token_docs.hpp"

namespace neos::python_runtime::detail::token_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "token.name"))
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
                py_raise("SystemError", std::string{ "token: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"token.name">()))
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
            if (modules_imported("token"))
                return py{ fn_import("token") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "token" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "token", token_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- token (Lib/token.py)

        void make_token()
        {
            static module_writer const& w = writer("token");
            // Auto-generated by Tools/build/generate_token.py
            define("token", "__all__", py_list({ S<"tok_name">(), S<"ISTERMINAL">(), S<"ISNONTERMINAL">(), S<"ISEOF">(), S<"EXACT_TOKEN_TYPES">() }));
            define("token", "ENDMARKER", I<0>());
            define("token", "NAME", I<1>());
            define("token", "NUMBER", I<2>());
            define("token", "STRING", I<3>());
            define("token", "NEWLINE", I<4>());
            define("token", "INDENT", I<5>());
            define("token", "DEDENT", I<6>());
            define("token", "LPAR", I<7>());
            define("token", "RPAR", I<8>());
            define("token", "LSQB", I<9>());
            define("token", "RSQB", I<10>());
            define("token", "COLON", I<11>());
            define("token", "COMMA", I<12>());
            define("token", "SEMI", I<13>());
            define("token", "PLUS", I<14>());
            define("token", "MINUS", I<15>());
            define("token", "STAR", I<16>());
            define("token", "SLASH", I<17>());
            define("token", "VBAR", I<18>());
            define("token", "AMPER", I<19>());
            define("token", "LESS", I<20>());
            define("token", "GREATER", I<21>());
            define("token", "EQUAL", I<22>());
            define("token", "DOT", I<23>());
            define("token", "PERCENT", I<24>());
            define("token", "LBRACE", I<25>());
            define("token", "RBRACE", I<26>());
            define("token", "EQEQUAL", I<27>());
            define("token", "NOTEQUAL", I<28>());
            define("token", "LESSEQUAL", I<29>());
            define("token", "GREATEREQUAL", I<30>());
            define("token", "TILDE", I<31>());
            define("token", "CIRCUMFLEX", I<32>());
            define("token", "LEFTSHIFT", I<33>());
            define("token", "RIGHTSHIFT", I<34>());
            define("token", "DOUBLESTAR", I<35>());
            define("token", "PLUSEQUAL", I<36>());
            define("token", "MINEQUAL", I<37>());
            define("token", "STAREQUAL", I<38>());
            define("token", "SLASHEQUAL", I<39>());
            define("token", "PERCENTEQUAL", I<40>());
            define("token", "AMPEREQUAL", I<41>());
            define("token", "VBAREQUAL", I<42>());
            define("token", "CIRCUMFLEXEQUAL", I<43>());
            define("token", "LEFTSHIFTEQUAL", I<44>());
            define("token", "RIGHTSHIFTEQUAL", I<45>());
            define("token", "DOUBLESTAREQUAL", I<46>());
            define("token", "DOUBLESLASH", I<47>());
            define("token", "DOUBLESLASHEQUAL", I<48>());
            define("token", "AT", I<49>());
            define("token", "ATEQUAL", I<50>());
            define("token", "RARROW", I<51>());
            define("token", "ELLIPSIS", I<52>());
            define("token", "COLONEQUAL", I<53>());
            define("token", "EXCLAMATION", I<54>());
            define("token", "OP", I<55>());
            define("token", "TYPE_IGNORE", I<56>());
            define("token", "TYPE_COMMENT", I<57>());
            define("token", "SOFT_KEYWORD", I<58>());
            define("token", "FSTRING_START", I<59>());
            define("token", "FSTRING_MIDDLE", I<60>());
            define("token", "FSTRING_END", I<61>());
            define("token", "COMMENT", I<62>());
            define("token", "NL", I<63>());
            // These aren't used by the C tokenizer but are needed for tokenize.py
            define("token", "ERRORTOKEN", I<64>());
            define("token", "ENCODING", I<65>());
            define("token", "N_TOKENS", I<66>());
            // Special definitions for cooperation with parser
            define("token", "NT_OFFSET", I<256>());
            define("token", "tok_name", [&]() -> py { py const comprehension1 = py_dict(); py name; py value; for (py const& item : iterate{ module_globals().method("items") }) { auto const items2 = unpack(item, 2); name = items2[0]; value = items2[1]; if (!([&]() -> py { py const t = boolean(isinstance(value, B<"int">())); return !t.truthy() ? t : boolean(!name.method("startswith", { S<"_">() }).truthy()); }()).truthy()) continue; comprehension1.set_item(value, name); } return comprehension1; }());
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (G<"token.__all__">()).method("extend", { (G<"token.tok_name">()).method("values") });
                    return none();
                }))();
            define("token", "EXACT_TOKEN_TYPES", dict_of({ { S<"!">(), G<"token.EXCLAMATION">() }, { S<"!=">(), G<"token.NOTEQUAL">() }, { S<"%">(), G<"token.PERCENT">() }, { S<"%=">(), G<"token.PERCENTEQUAL">() }, { S<"&">(), G<"token.AMPER">() }, { S<"&=">(), G<"token.AMPEREQUAL">() }, { S<"(">(), G<"token.LPAR">() }, { S<")">(), G<"token.RPAR">() }, { S<"*">(), G<"token.STAR">() }, { S<"**">(), G<"token.DOUBLESTAR">() }, { S<"**=">(), G<"token.DOUBLESTAREQUAL">() }, { S<"*=">(), G<"token.STAREQUAL">() }, { S<"+">(), G<"token.PLUS">() }, { S<"+=">(), G<"token.PLUSEQUAL">() }, { S<",">(), G<"token.COMMA">() }, { S<"-">(), G<"token.MINUS">() }, { S<"-=">(), G<"token.MINEQUAL">() }, { S<"->">(), G<"token.RARROW">() }, { S<".">(), G<"token.DOT">() }, { S<"...">(), G<"token.ELLIPSIS">() }, { S<"/">(), G<"token.SLASH">() }, { S<"//">(), G<"token.DOUBLESLASH">() }, { S<"//=">(), G<"token.DOUBLESLASHEQUAL">() }, { S<"/=">(), G<"token.SLASHEQUAL">() }, { S<":">(), G<"token.COLON">() }, { S<":=">(), G<"token.COLONEQUAL">() }, { S<";">(), G<"token.SEMI">() }, { S<"<">(), G<"token.LESS">() }, { S<"<<">(), G<"token.LEFTSHIFT">() }, { S<"<<=">(), G<"token.LEFTSHIFTEQUAL">() }, { S<"<=">(), G<"token.LESSEQUAL">() }, { S<"=">(), G<"token.EQUAL">() }, { S<"==">(), G<"token.EQEQUAL">() }, { S<">">(), G<"token.GREATER">() }, { S<">=">(), G<"token.GREATEREQUAL">() }, { S<">>">(), G<"token.RIGHTSHIFT">() }, { S<">>=">(), G<"token.RIGHTSHIFTEQUAL">() }, { S<"@">(), G<"token.AT">() }, { S<"@=">(), G<"token.ATEQUAL">() }, { S<"[">(), G<"token.LSQB">() }, { S<"]">(), G<"token.RSQB">() }, { S<"^">(), G<"token.CIRCUMFLEX">() }, { S<"^=">(), G<"token.CIRCUMFLEXEQUAL">() }, { S<"{">(), G<"token.LBRACE">() }, { S<"|">(), G<"token.VBAR">() }, { S<"|=">(), G<"token.VBAREQUAL">() }, { S<"}">(), G<"token.RBRACE">() }, { S<"~">(), G<"token.TILDE">() } }));
            define("token", "ISTERMINAL", w.function("ISTERMINAL", "ISTERMINAL", { py_positional("x") }, [](arguments a) -> py
                {
                    py x = a[0];
                    return boolean(x < (G<"token.NT_OFFSET">()));
                }));

            define("token", "ISNONTERMINAL", w.function("ISNONTERMINAL", "ISNONTERMINAL", { py_positional("x") }, [](arguments a) -> py
                {
                    py x = a[0];
                    return boolean(x >= (G<"token.NT_OFFSET">()));
                }));

            define("token", "ISEOF", w.function("ISEOF", "ISEOF", { py_positional("x") }, [](arguments a) -> py
                {
                    py x = a[0];
                    return boolean(x == (G<"token.ENDMARKER">()));
                }));
        }

        bool token_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_token();
                }
                if (auto const doc = token_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("token", token_module);
    }
}
