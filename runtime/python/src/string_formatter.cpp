/*
  string_formatter.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Portions of this file are derived from CPython 3.13 (Lib/string.py and Objects/stringlib/unicode_format.h), Copyright
  (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation
  License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from Python and C to C++ for the neos Python
  runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The module _string (CPython's: formatter_parser and formatter_field_name_split, str.format's parsing of a format string) and the
// class string.Formatter (CPython 3.13's string.py: made (by type) of closures, so a program's subclasses override its methods)

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        py none() { return py::none(); }
        py S(std::string const& aText) { return py{ aText }; }

        // (_string.formatter_parser(format_string): of its parts, (literal_text, field_name, format_spec, conversion) (CPython's MarkupIterator))
        py formatter_parser(std::string const& aFormat)
        {
            return make_native_generator("formatter_parser", "formatter_parser", "_string", [aFormat]() -> py
            {
                auto const n = aFormat.size();
                std::size_t i = 0u;
                while (i < n)
                {
                    // (the literal text: to a '{' or '}' (an escaped one ends it, as its last character))
                    std::string literal;
                    bool markup = false;
                    while (i < n)
                    {
                        auto const c = aFormat[i++];
                        if (c == '{' || c == '}')
                        {
                            if (i < n && aFormat[i] == c)
                            {
                                literal += c;
                                ++i;
                                break;
                            }
                            if (c == '}')
                                py_raise("ValueError", "Single '}' encountered in format string");
                            if (i >= n)
                                py_raise("ValueError", "Single '{' encountered in format string");
                            markup = true;
                            break;
                        }
                        literal += c;
                    }
                    if (!markup)
                    {
                        py_yield(py_tuple({ S(literal), none(), none(), none() }));
                        continue;
                    }
                    // (the field: to its matching '}')
                    std::size_t depth = 1u;
                    auto const start = i;
                    while (i < n)
                    {
                        auto const c = aFormat[i++];
                        if (c == '{')
                            ++depth;
                        else if (c == '}' && --depth == 0u)
                            break;
                    }
                    if (depth != 0u)
                        py_raise("ValueError", "expected '}' before end of string");
                    std::string const field = aFormat.substr(start, i - 1u - start);
                    // (field_name [!conversion] [:format_spec])
                    std::size_t k = 0u;
                    for (std::size_t bracket = 0u; k < field.size(); ++k)
                    {
                        auto const c = field[k];
                        if (c == '[')
                            ++bracket;
                        else if (c == ']' && bracket != 0u)
                            --bracket;
                        else if (bracket == 0u && (c == ':' || c == '!'))
                            break;
                    }
                    std::string const name = field.substr(0u, k);
                    py conversion = none();
                    std::string spec;
                    if (k < field.size() && field[k] == '!')
                    {
                        if (k + 1u >= field.size())
                            py_raise("ValueError", "end of string while looking for conversion specifier");
                        conversion = S(std::string(1u, field[k + 1u]));
                        k += 2u;
                        if (k < field.size() && field[k] != ':')
                            py_raise("ValueError", "expected ':' after conversion specifier");
                    }
                    if (k < field.size() && field[k] == ':')
                        spec = field.substr(k + 1u);
                    py_yield(py_tuple({ S(literal), S(name), S(spec), conversion }));
                }
                return none();
            });
        }

        bool all_digits(std::string const& aText)
        {
            return !aText.empty() && std::all_of(aText.begin(), aText.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
        }
        py key_of(std::string const& aText) { return all_digits(aText) ? py{ std::stoll(aText) } : S(aText); }

        // (_string.formatter_field_name_split(field_name): (first, an iterator of (is_attribute, key)))
        py field_name_split(std::string const& aName)
        {
            auto const first = aName.find_first_of(".[");
            std::string const head = aName.substr(0u, first);
            std::vector<py> rest;
            std::size_t i = first == std::string::npos ? aName.size() : first;
            while (i < aName.size())
            {
                if (aName[i] == '.')
                {
                    auto const end = aName.find_first_of(".[", i + 1u);
                    std::string const attribute = aName.substr(i + 1u, (end == std::string::npos ? aName.size() : end) - i - 1u);
                    if (attribute.empty())
                        py_raise("ValueError", "Empty attribute in format string");
                    rest.push_back(py_tuple({ py::boolean(true), S(attribute) }));
                    i = end == std::string::npos ? aName.size() : end;
                }
                else
                {
                    auto const close = aName.find(']', i + 1u);
                    if (close == std::string::npos)
                        py_raise("ValueError", "Missing ']' in format string");
                    rest.push_back(py_tuple({ py::boolean(false), key_of(aName.substr(i + 1u, close - i - 1u)) }));
                    i = close + 1u;
                    if (i < aName.size() && aName[i] != '.' && aName[i] != '[')
                        py_raise("ValueError", "Only '.' or '[' may follow ']' in format field specifier");
                }
            }
            return py_tuple({ key_of(head), builtin("iter")(py_list(rest)) });
        }

        using body_type = std::function<py(std::vector<py> const&)>;

        py function(std::string const& aName, std::string const& aQualname, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false,
            bool aVarKeywords = false, char const* aModule = "string")
        {
            return make_native_function(aName, aQualname, "", aModule, [aQualname, aParameters, aBody, aVarArgs, aVarKeywords](std::vector<py> const& aArgs, object* aKwargs)
            {
                py rest;
                py extra;
                auto a = py_bind(aQualname, aParameters, aArgs, aKwargs, aVarArgs ? &rest : nullptr, aVarKeywords ? &extra : nullptr);
                if (aVarArgs)
                    a.push_back(rest);
                if (aVarKeywords)
                    a.push_back(extra);
                return aBody(a);
            });
        }
        py method(char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false, bool aVarKeywords = false)
        {
            aParameters.insert(aParameters.begin(), py_positional("self"));
            return function(aName, std::string{ "Formatter." } + aName, std::move(aParameters), std::move(aBody), aVarArgs, aVarKeywords);
        }

        // (Formatter._vformat: (the result, auto_arg_index))
        py vformat(py const& self, py const& aFormat, py const& aArgs, py const& aKwargs, py const& aUsedArgs, long long aDepth, py aAutoArgIndex)
        {
            if (aDepth < 0)
                py_raise("ValueError", "Max string recursion exceeded");
            py const result = py_list();
            for (auto const& part : self.method("parse", { aFormat }).items())
            {
                auto const fields = part.items();
                py const& literalText = fields.at(0);
                py fieldName = fields.at(1);
                py formatSpec = fields.at(2);
                py const& conversion = fields.at(3);
                if (literalText.truthy())
                    result.method("append", { literalText });
                if (!fieldName.is_none())
                {
                    if (fieldName == S(""))
                    {
                        if (aAutoArgIndex.is(py::boolean(false)))
                            py_raise("ValueError", "cannot switch from manual field specification to automatic field numbering");
                        fieldName = builtin("str")(aAutoArgIndex);
                        aAutoArgIndex = aAutoArgIndex + py{ 1 };
                    }
                    else if (fieldName.method("isdigit").truthy())
                    {
                        if (aAutoArgIndex.truthy())
                            py_raise("ValueError", "cannot switch from manual field specification to automatic field numbering");
                        aAutoArgIndex = py::boolean(false);
                    }
                    py const field = self.method("get_field", { fieldName, aArgs, aKwargs });
                    py obj = field[py{ 0 }];
                    aUsedArgs.method("add", { field[py{ 1 }] });
                    obj = self.method("convert_field", { obj, conversion });
                    py const nested = vformat(self, formatSpec, aArgs, aKwargs, aUsedArgs, aDepth - 1, aAutoArgIndex);
                    formatSpec = nested[py{ 0 }];
                    aAutoArgIndex = nested[py{ 1 }];
                    result.method("append", { self.method("format_field", { obj, formatSpec }) });
                }
            }
            return py_tuple({ S("").method("join", { result }), aAutoArgIndex });
        }

        py make_formatter_class()
        {
            py const ns = py_dict();
            ns.set_item(S("__module__"), S("string"));
            ns.set_item(S("__qualname__"), S("Formatter"));
            auto const def = [&](char const* aName, py const& aValue) { ns.set_item(S(aName), aValue); };
            def("format", method("format", { py_positional_only("format_string") }, [](std::vector<py> const& a)
            {
                return a[0].method("vformat", { a[1], a[2], a[3] });
            }, true, true));
            def("vformat", method("vformat", { py_positional("format_string"), py_positional("args"), py_positional("kwargs") }, [](std::vector<py> const& a)
            {
                py const usedArgs = builtin("set")();
                py const result = a[0].method("_vformat", { a[1], a[2], a[3], usedArgs, py{ 2 } });
                a[0].method("check_unused_args", { usedArgs, a[2], a[3] });
                return result[py{ 0 }];
            }));
            def("_vformat", method("_vformat", { py_positional("format_string"), py_positional("args"), py_positional("kwargs"), py_positional("used_args"),
                py_positional("recursion_depth"), py_positional("auto_arg_index", py{ 0 }) }, [](std::vector<py> const& a)
            {
                return vformat(a[0], a[1], a[2], a[3], a[4], a[5].as_index(), a[6]);
            }));
            def("get_value", method("get_value", { py_positional("key"), py_positional("args"), py_positional("kwargs") }, [](std::vector<py> const& a)
            {
                if (isinstance(a[1], builtin("int")))
                    return a[2][a[1]];
                return a[3][a[1]];
            }));
            def("check_unused_args", method("check_unused_args", { py_positional("used_args"), py_positional("args"), py_positional("kwargs") },
                [](std::vector<py> const&) { return none(); }));
            def("format_field", method("format_field", { py_positional("value"), py_positional("format_spec") }, [](std::vector<py> const& a)
            {
                return builtin("format")(a[1], a[2]);
            }));
            def("convert_field", method("convert_field", { py_positional("value"), py_positional("conversion") }, [](std::vector<py> const& a)
            {
                py const& conversion = a[2];
                if (conversion.is_none())
                    return a[1];
                if (conversion == S("s"))
                    return builtin("str")(a[1]);
                if (conversion == S("r"))
                    return builtin("repr")(a[1]);
                if (conversion == S("a"))
                    return builtin("ascii")(a[1]);
                py_raise("ValueError", "Unknown conversion specifier " + conversion.str());
            }));
            def("parse", method("parse", { py_positional("format_string") }, [](std::vector<py> const& a)
            {
                return formatter_parser(a[1].str());
            }));
            def("get_field", method("get_field", { py_positional("field_name"), py_positional("args"), py_positional("kwargs") }, [](std::vector<py> const& a)
            {
                py const split = field_name_split(a[1].str());
                py const first = split[py{ 0 }];
                py obj = a[0].method("get_value", { first, a[2], a[3] });
                for (auto const& item : split[py{ 1 }].items())
                {
                    if (item[py{ 0 }].truthy())
                        obj = builtin("getattr")(obj, item[py{ 1 }]);
                    else
                        obj = obj[item[py{ 1 }]];
                }
                return py_tuple({ obj, first });
            }));
            return builtin("type")(S("Formatter"), py_tuple({ builtin("object") }), ns);
        }

        bool underscore_string_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                aAttributes.emplace_back("formatter_parser", function("formatter_parser", "formatter_parser", { py_positional_only("format_string") },
                    [](std::vector<py> const& a)
                    {
                        if (!isinstance(a[0], builtin("str")))
                            py_raise("TypeError", "expected str, got " + type_of(a[0]).attr("__name__").str());
                        return formatter_parser(a[0].str());
                    }, false, false, "_string").release());
                aAttributes.emplace_back("formatter_field_name_split", function("formatter_field_name_split", "formatter_field_name_split", { py_positional_only("field_name") },
                    [](std::vector<py> const& a)
                    {
                        if (!isinstance(a[0], builtin("str")))
                            py_raise("TypeError", "expected str, got " + type_of(a[0]).attr("__name__").str());
                        return field_name_split(a[0].str());
                    }, false, false, "_string").release());
                return none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sUnderscoreString = register_module("_string", underscore_string_module);
    }

    // (string.Formatter (textmodules.cpp's string module): a new reference, or null)
    object* make_string_formatter()
    {
        static py* sFormatter = nullptr;
        return py_guarded([&]()
        {
            if (sFormatter == nullptr)
                sFormatter = new py{ make_formatter_class() };
            return *sFormatter;
        });
    }
}
