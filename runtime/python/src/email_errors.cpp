/*
  email_errors.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/errors.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.errors (CPython 3.13's Lib/email/errors.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_errors_docs.hpp"

namespace neos::python_runtime::detail::email_errors_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.errors.name"))
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
                py_raise("SystemError", std::string{ "email.errors: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.errors.name">()))
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
        // (globals(): the module's namespace (its __dict__ once it has been imported; while it is being made, its attributes so far))
        [[maybe_unused]] py module_globals()
        {
            if (modules_imported("email.errors"))
                return py{ fn_import("email.errors") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.errors" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.errors", email_errors_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.errors (Lib/email/errors.py)

        void make_email_errors()
        {
            static module_writer const& w = writer("email.errors");
            // Copyright (C) 2001-2006 Python Software Foundation
            // Author: Barry Warsaw
            // Contact: email-sig@python.org
            define("email.errors", "MessageError", w.make_class("MessageError", { B<"Exception">() }, {
 }));

            define("email.errors", "MessageParseError", w.make_class("MessageParseError", { G<"email.errors.MessageError">() }, {
 }));

            define("email.errors", "HeaderParseError", w.make_class("HeaderParseError", { G<"email.errors.MessageParseError">() }, {
 }));

            define("email.errors", "BoundaryError", w.make_class("BoundaryError", { G<"email.errors.MessageParseError">() }, {
 }));

            define("email.errors", "MultipartConversionError", w.make_class("MultipartConversionError", { G<"email.errors.MessageError">(), B<"TypeError">() }, {
 }));

            define("email.errors", "CharsetError", w.make_class("CharsetError", { G<"email.errors.MessageError">() }, {
 }));

            define("email.errors", "HeaderWriteError", w.make_class("HeaderWriteError", { G<"email.errors.MessageError">() }, {
 }));

            // These are parsing defects which the parser was able to work around.
            define("email.errors", "MessageDefect", w.make_class("MessageDefect", { B<"ValueError">() }, {
                { "__init__", w.method("MessageDefect", "__init__", { py_positional("line", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        if (!line.is_none())
                        {
                            (B<"super">()(G<"email.errors.MessageDefect">(), self)).method("__init__", { line });
                        }
                        self.set_attr("line", line);
                        return none();
                    }) }, }));

            define("email.errors", "NoBoundaryInMultipartDefect", w.make_class("NoBoundaryInMultipartDefect", { G<"email.errors.MessageDefect">() }, {
 }));

            define("email.errors", "StartBoundaryNotFoundDefect", w.make_class("StartBoundaryNotFoundDefect", { G<"email.errors.MessageDefect">() }, {
 }));

            define("email.errors", "CloseBoundaryNotFoundDefect", w.make_class("CloseBoundaryNotFoundDefect", { G<"email.errors.MessageDefect">() }, {
 }));

            define("email.errors", "FirstHeaderLineIsContinuationDefect", w.make_class("FirstHeaderLineIsContinuationDefect", { G<"email.errors.MessageDefect">() }, {
 }));

            define("email.errors", "MisplacedEnvelopeHeaderDefect", w.make_class("MisplacedEnvelopeHeaderDefect", { G<"email.errors.MessageDefect">() }, {
 }));

            define("email.errors", "MissingHeaderBodySeparatorDefect", w.make_class("MissingHeaderBodySeparatorDefect", { G<"email.errors.MessageDefect">() }, {
 }));

            // XXX: backward compatibility, just in case (it was never emitted).
            define("email.errors", "MalformedHeaderDefect", G<"email.errors.MissingHeaderBodySeparatorDefect">());
            define("email.errors", "MultipartInvariantViolationDefect", w.make_class("MultipartInvariantViolationDefect", { G<"email.errors.MessageDefect">() }, {
 }));

            define("email.errors", "InvalidMultipartContentTransferEncodingDefect", w.make_class("InvalidMultipartContentTransferEncodingDefect", { G<"email.errors.MessageDefect">() }, {
 }));

            define("email.errors", "UndecodableBytesDefect", w.make_class("UndecodableBytesDefect", { G<"email.errors.MessageDefect">() }, {
 }));

            define("email.errors", "InvalidBase64PaddingDefect", w.make_class("InvalidBase64PaddingDefect", { G<"email.errors.MessageDefect">() }, {
 }));

            define("email.errors", "InvalidBase64CharactersDefect", w.make_class("InvalidBase64CharactersDefect", { G<"email.errors.MessageDefect">() }, {
 }));

            define("email.errors", "InvalidBase64LengthDefect", w.make_class("InvalidBase64LengthDefect", { G<"email.errors.MessageDefect">() }, {
 }));

            // These errors are specific to header parsing.
            define("email.errors", "HeaderDefect", w.make_class("HeaderDefect", { G<"email.errors.MessageDefect">() }, {
                { "__init__", w.method("HeaderDefect", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        call_ex((B<"super">()(G<"email.errors.HeaderDefect">(), self)).attr("__init__"), {  }, args, kw);
                        return none();
                    }, true, true) }, }));

            define("email.errors", "InvalidHeaderDefect", w.make_class("InvalidHeaderDefect", { G<"email.errors.HeaderDefect">() }, {
 }));

            define("email.errors", "HeaderMissingRequiredValue", w.make_class("HeaderMissingRequiredValue", { G<"email.errors.HeaderDefect">() }, {
 }));

            define("email.errors", "NonPrintableDefect", w.make_class("NonPrintableDefect", { G<"email.errors.HeaderDefect">() }, {
                { "__init__", w.method("NonPrintableDefect", "__init__", { py_positional("non_printables") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py non_printables = a[1];
                        (B<"super">()(G<"email.errors.NonPrintableDefect">(), self)).method("__init__", { non_printables });
                        self.set_attr("non_printables", non_printables);
                        return none();
                    }) },
                { "__str__", w.method("NonPrintableDefect", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"the following ASCII non-printables found in header: {}">()).method("format", { self.attr("non_printables") });
                    }) }, }));

            define("email.errors", "ObsoleteHeaderDefect", w.make_class("ObsoleteHeaderDefect", { G<"email.errors.HeaderDefect">() }, {
 }));

            define("email.errors", "NonASCIILocalPartDefect", w.make_class("NonASCIILocalPartDefect", { G<"email.errors.HeaderDefect">() }, {
 }));

            // This defect only occurs during unicode parsing, not when
            // parsing messages decoded from binary.
            define("email.errors", "InvalidDateDefect", w.make_class("InvalidDateDefect", { G<"email.errors.HeaderDefect">() }, {
 }));
        }

        bool email_errors_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_errors();
                }
                if (auto const doc = email_errors_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.errors", email_errors_module);
    }
}
