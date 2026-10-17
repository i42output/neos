/*
  compiler.python.cpp

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
*/

// Python code generation
//
// int, float, str and bool values are native and statically typed (as before); every other value is an object of the
// Python object protocol (type::Object: a handle; see neos/runtime/python/object.h) whose operations are calls to the
// Python runtime plugin (python::f). A value of a native type is converted to an object (boxed) where an object is
// needed.
//
// References: an object produced by an operation (a new reference) is held by a temporary that is released at the end
// of the statement (or condition) that produced it; a variable holds a reference of its own (assignment retains the
// value and releases the variable's previous value; a temporary's reference is transferred where possible); object
// parameters are retained by the function's prologue; a function releases its object variables when it returns and
// returns a new reference.

#include <neolib/neolib.hpp>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <neos/bytecode/bytecode.hpp>
#include <neos/language/compiler.hpp>
#include "compiler.emit.hpp"

namespace neos::language
{
    using namespace emitter;
    using bytecode::opcode;
    using python::construct;

    namespace
    {
        // (neos/runtime/python/object.h)
        enum : std::int32_t { PyAdd, PySubtract, PyMultiply, PyTrueDivide, PyFloorDivide, PyRemainder, PyPower, PyMatMul, PyLeftShift, PyRightShift, PyAnd, PyOr, PyXor };
        enum : std::int32_t { PyNegative, PyPositive, PyInvert };
        enum : std::int32_t { PyLess, PyLessEqual, PyEqual, PyNotEqual, PyGreater, PyGreaterEqual };

        std::string_view trimmed(std::string_view aText)
        {
            while (!aText.empty() && std::isspace(static_cast<unsigned char>(aText.front())))
                aText.remove_prefix(1u);
            while (!aText.empty() && std::isspace(static_cast<unsigned char>(aText.back())))
                aText.remove_suffix(1u);
            return aText;
        }

        bool name_character(char aCharacter)
        {
            return std::isalnum(static_cast<unsigned char>(aCharacter)) || aCharacter == '_';
        }

        // the positions in aText of aSeparator that are not within brackets or strings
        std::vector<std::size_t> top_level(std::string_view aText, char aSeparator)
        {
            std::vector<std::size_t> result;
            int depth = 0;
            for (std::size_t i = 0u; i < aText.size(); ++i)
            {
                char const c = aText[i];
                if (c == '\'' || c == '\"')
                {
                    for (++i; i < aText.size() && aText[i] != c; ++i)
                        if (aText[i] == '\\')
                            ++i;
                    continue;
                }
                if (c == '#')
                {
                    while (i < aText.size() && aText[i] != '\n')
                        ++i;
                    continue;
                }
                if (c == '(' || c == '[' || c == '{')
                    ++depth;
                else if (c == ')' || c == ']' || c == '}')
                    --depth;
                else if (depth == 0 && c == aSeparator)
                    result.push_back(i);
            }
            return result;
        }

        // the parts of aText separated by aSeparator (at the top level)
        std::vector<std::string_view> split(std::string_view aText, char aSeparator)
        {
            std::vector<std::string_view> result;
            std::size_t start = 0u;
            for (auto const position : top_level(aText, aSeparator))
            {
                result.push_back(aText.substr(start, position - start));
                start = position + 1u;
            }
            result.push_back(aText.substr(start));
            return result;
        }

        // the keyword of a keyword argument ("name = value")
        std::optional<std::string> keyword_of(std::string_view aArgument)
        {
            aArgument = trimmed(aArgument);
            std::size_t i = 0u;
            while (i < aArgument.size() && name_character(aArgument[i]))
                ++i;
            if (i == 0u || std::isdigit(static_cast<unsigned char>(aArgument[0])))
                return {};
            auto j = i;
            while (j < aArgument.size() && std::isspace(static_cast<unsigned char>(aArgument[j])))
                ++j;
            if (j < aArgument.size() && aArgument[j] == '=' && (j + 1u == aArgument.size() || aArgument[j + 1u] != '='))
                return std::string{ aArgument.substr(0u, i) };
            return {};
        }

        std::optional<std::uint32_t> held_by(fragment const& aValue, std::vector<std::uint32_t> const& aPending);

        void put_local(fragment& aFragment, opcode aOpcode, std::uint32_t aLocal)
        {
            put_op(aFragment.fragmentCode.as_std_vector(), aOpcode);
            put_u32(aFragment.fragmentCode.as_std_vector(), aLocal);
        }

        void put_i32(fragment& aFragment, std::int32_t aValue)
        {
            put_op(aFragment.fragmentCode.as_std_vector(), opcode::I32Const);
            put_s64(aFragment.fragmentCode.as_std_vector(), aValue);
        }

        void put_i64(fragment& aFragment, std::int64_t aValue)
        {
            put_op(aFragment.fragmentCode.as_std_vector(), opcode::I64Const);
            put_s64(aFragment.fragmentCode.as_std_vector(), aValue);
        }

        void put(fragment& aFragment, opcode aOpcode)
        {
            put_op(aFragment.fragmentCode.as_std_vector(), aOpcode);
        }

        void put_block(fragment& aFragment, opcode aOpcode, std::optional<type> aResult)
        {
            put_op(aFragment.fragmentCode.as_std_vector(), aOpcode);
            aFragment.fragmentCode.push_back(aResult ? *wasm_type(*aResult) : 0x40u);
        }

        fragment local_value(std::uint32_t aLocal, type aType)
        {
            fragment result;
            put_local(result, opcode::LocalGet, aLocal);
            result.valueType = aType;
            return result;
        }

        // the kind of value (Python's type) that a native type holds
        type kind_of(type aType)
        {
            if (aType == type::Boolean || aType == type::String || aType == type::Object || aType == type::Void)
                return aType;
            if (is_float(aType))
                return type::F64;
            if (is_numeric(aType))
                return type::I64;
            return aType;
        }

        bool is_number(type aType)
        {
            return is_numeric(aType); // (bool is a number)
        }

        bool is_integer(type aType)
        {
            return is_numeric(aType) && !is_float(aType) && aType != type::Boolean;
        }

        std::set<std::string> const& builtin_names()
        {
            static std::set<std::string> const sNames =
            {
                "abs", "all", "any", "bin", "bool", "callable", "chr", "complex", "dict", "divmod", "enumerate", "filter", "float",
                "format", "frozenset", "getattr", "hasattr", "hash", "hex", "id", "input", "int", "isinstance", "issubclass",
                "iter", "len", "list", "map", "max", "min", "next", "object", "oct", "ord", "pow", "print", "range", "repr",
                "reversed", "round", "set", "setattr", "slice", "sorted", "str", "sum", "tuple", "type", "zip", "NotImplemented",
                "Ellipsis", "BaseException", "Exception", "SystemExit", "KeyboardInterrupt", "GeneratorExit", "ArithmeticError",
                "ZeroDivisionError", "OverflowError", "FloatingPointError", "LookupError", "IndexError", "KeyError", "ValueError",
                "UnicodeError", "TypeError", "AttributeError", "NameError", "UnboundLocalError", "AssertionError", "StopIteration",
                "ImportError", "ModuleNotFoundError", "RuntimeError", "NotImplementedError", "RecursionError", "OSError",
                "FileNotFoundError", "EOFError", "MemoryError", "SystemError", "BufferError", "Warning", "UserWarning",
                "DeprecationWarning", "RuntimeWarning"
            };
            return sNames;
        }
    }

    bool compiler::is_python() const
    {
        return !iCompilationStateStack.empty() && state().unit != nullptr && state().unit->schema->meta().language == python::Language;
    }

    void compiler::py_complete_signature(i_function_signature& aSignature, neolib::i_string_view const& aSource)
    {
        // (a function can follow top-level statements: an if statement that they end with is complete)
        if (!state().indentStack.empty())
            finalize_suspended(0u);
        // a parameter without an annotation is an object; so is the result of a function without one
        if (aSignature.parameters().has_value())
            for (auto& parameter : aSignature.parameters().value())
                if (parameter.parameter_type() == type::UNKNOWN)
                    parameter.set_parameter_type(type::Object);
        auto const text = aSource.to_std_string_view();
        auto const close = text.rfind(')');
        if (close == std::string_view::npos || text.find("->", close) == std::string_view::npos)
            aSignature.set_return_type(type::Object);
        // the parameters' kinds (*args, **kwargs, keyword-only) and default values
        auto const name = aSignature.name().to_std_string();
        auto const open = text.find('(');
        std::vector<py_parameter> parameters;
        std::vector<std::size_t> defaults;  // (indices of the parameters with defaults)
        if (open != std::string_view::npos && close != std::string_view::npos && close > open)
        {
            bool keywordOnly = false;
            auto const list = text.substr(open + 1u, close - open - 1u);
            if (!trimmed(list).empty())
                for (auto item : split(list, ','))
                {
                    item = trimmed(item);
                    if (item.empty())
                        continue;
                    py_parameter parameter;
                    if (item == "*")
                    {
                        keywordOnly = true;
                        continue;
                    }
                    if (item.starts_with("**"))
                    {
                        parameter.kind = py_parameter::VarKeywords;
                        item.remove_prefix(2u);
                    }
                    else if (item.starts_with("*"))
                    {
                        parameter.kind = py_parameter::VarArgs;
                        keywordOnly = true;
                        item.remove_prefix(1u);
                    }
                    else if (keywordOnly)
                        parameter.kind = py_parameter::KeywordOnly;
                    item = trimmed(item);
                    std::size_t n = 0u;
                    while (n < item.size() && name_character(item[n]))
                        ++n;
                    parameter.name = std::string{ item.substr(0u, n) };
                    auto const equals = top_level(item, '=');
                    parameter.hasDefault = !equals.empty();
                    if (parameter.hasDefault)
                        defaults.push_back(parameters.size());
                    parameters.push_back(parameter);
                }
        }
        // (the default values: evaluated (in order) when the function is defined, and kept in globals)
        std::vector<operand_type> values;
        for (std::size_t i = 0u; i < defaults.size(); ++i)
        {
            if (state().operandStack.empty())
                throw std::runtime_error("default value expected");
            values.insert(values.begin(), state().operandStack.back());
            state().operandStack.pop_back();
        }
        iPyParameters[name] = parameters;
        if (!defaults.empty() && !iDeclarationPass)
        {
            ensure_function();
            auto statement = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
            for (std::size_t i = 0u; i < defaults.size(); ++i)
            {
                auto const& parameter = parameters[defaults[i]];
                type t = type::Object;
                if (aSignature.parameters().has_value())
                    for (auto const& p : aSignature.parameters().value())
                        if (p.parameter_name().to_std_string() == parameter.name)
                            t = p.parameter_type();
                auto& global = define_global(name + "." + parameter.name, t);
                iInference.functionGlobals.insert(name + "." + parameter.name); // (a global: calls use it)
                auto value = materialize(values[i]);
                if (t == type::Object)
                {
                    if (value.valueType != type::Object)
                        coerce(value, type::Object);
                    auto const holder = held_by(value, current_function().pendingObjects);
                    append(statement, value);
                    if (holder)
                    {
                        current_function().pendingObjects.erase(std::find(current_function().pendingObjects.begin(), current_function().pendingObjects.end(), *holder));
                        put_local(statement, opcode::GlobalSet, global.index());
                        put_i32(statement, 0);
                        put_local(statement, opcode::LocalSet, *holder);
                        current_function().freeObjects.push_back(*holder);
                        continue;
                    }
                    else
                    {
                        auto const scratch = temporary(type::I32);
                        put_local(statement, opcode::LocalTee, scratch);
                        emit_call_to(statement, runtime_function("neos::object_retain", { type::Object }, type::Void));
                        put_local(statement, opcode::LocalGet, scratch);
                    }
                }
                else
                {
                    if (value.valueType != t)
                        coerce(value, t);
                    append(statement, value);
                }
                put_local(statement, opcode::GlobalSet, global.index());
            }
            append(statement, py_flush());
            // (a lambda's function is defined at the end of the module: its (constant) default values are evaluated first)
            // (as are a class's methods (_cN_class_method))
            bool const lambda = name.size() > 3u && name.starts_with("_l") && name.ends_with("_") &&
                std::all_of(name.begin() + 2, name.end() - 1, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
            bool const method = name.size() > 3u && name.starts_with("_c") && std::isdigit(static_cast<unsigned char>(name[2])) &&
                name.find('_', 2u) != std::string::npos && std::all_of(name.begin() + 2, name.begin() + static_cast<std::ptrdiff_t>(name.find('_', 2u)),
                    [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
            if ((lambda || method) && state().function)
                state().operandStack.insert(state().operandStack.begin() + static_cast<std::ptrdiff_t>(current_function().operandBase), operand_type{ statement });
            else
                push_operand(operand_type{ statement });
        }
    }

    std::optional<std::vector<fragment>> compiler::py_bind_arguments(std::string const& aFunction, std::vector<fragment> const& aArguments,
        std::vector<std::optional<std::string>> const& aKeywords)
    {
        // the arguments of a call of a function of the program, in the order of its parameters (with the default values of
        // those not given, and *args and **kwargs packed into a tuple and a dict); TypeError (as Python) if they do not match
        auto const found = iPyParameters.find(aFunction);
        if (found == iPyParameters.end())
            return {};
        auto const& parameters = found->second;
        std::vector<std::optional<fragment>> slots(parameters.size());
        std::vector<fragment> extraPositional;
        std::vector<std::pair<std::string, fragment>> extraKeywords;
        std::optional<std::size_t> varArgs, varKeywords;
        std::size_t positionalCount = 0u;
        for (std::size_t p = 0u; p < parameters.size(); ++p)
        {
            if (parameters[p].kind == py_parameter::VarArgs)
                varArgs = p;
            else if (parameters[p].kind == py_parameter::VarKeywords)
                varKeywords = p;
            else if (parameters[p].kind == py_parameter::Positional)
                ++positionalCount;
        }
        std::size_t next = 0u;
        std::size_t givenPositional = 0u;
        for (std::size_t i = 0u; i < aArguments.size(); ++i)
        {
            if (!aKeywords[i])
            {
                ++givenPositional;
                while (next < parameters.size() && parameters[next].kind != py_parameter::Positional)
                    ++next;
                if (next < parameters.size() && parameters[next].kind == py_parameter::Positional)
                    slots[next++] = aArguments[i];
                else if (varArgs)
                    extraPositional.push_back(aArguments[i]);
                else
                {
                    std::size_t required = 0u;
                    for (auto const& parameter : parameters)
                        if (parameter.kind == py_parameter::Positional && !parameter.hasDefault)
                            ++required;
                    std::size_t given = 0u;
                    for (auto const& k : aKeywords)
                        if (!k)
                            ++given;
                    std::string const takes = required == positionalCount ? std::to_string(positionalCount) :
                        "from " + std::to_string(required) + " to " + std::to_string(positionalCount);
                    throw std::runtime_error(aFunction + "() takes " + takes + " positional argument" + (positionalCount == 1u && required == positionalCount ? "" : "s") +
                        " but " + std::to_string(given) + (given == 1u ? " was" : " were") + " given");
                }
                continue;
            }
            std::optional<std::size_t> slot;
            for (std::size_t p = 0u; p < parameters.size(); ++p)
                if ((parameters[p].kind == py_parameter::Positional || parameters[p].kind == py_parameter::KeywordOnly) && parameters[p].name == *aKeywords[i])
                    slot = p;
            if (slot)
            {
                if (slots[*slot])
                    throw std::runtime_error(aFunction + "() got multiple values for argument '" + *aKeywords[i] + "'");
                slots[*slot] = aArguments[i];
            }
            else if (varKeywords)
                extraKeywords.emplace_back(*aKeywords[i], aArguments[i]);
            else
                throw std::runtime_error(aFunction + "() got an unexpected keyword argument '" + *aKeywords[i] + "'");
        }
        (void)givenPositional;
        std::vector<std::string> missing;
        bool missingKeywordOnly = false;
        std::vector<fragment> result;
        for (std::size_t p = 0u; p < parameters.size(); ++p)
        {
            auto const& parameter = parameters[p];
            if (parameter.kind == py_parameter::VarArgs)
            {
                fragment count;
                put_i64(count, static_cast<std::int64_t>(extraPositional.size()));
                count.valueType = type::I64;
                auto tuple = py_owned(py_runtime_call("python::tuple", { type::I64 }, type::Object, { count }));
                auto const local = temporary(type::I32);
                fragment value = tuple;
                put_local(value, opcode::LocalSet, local);
                for (std::size_t i = 0u; i < extraPositional.size(); ++i)
                {
                    fragment position;
                    put_i64(position, static_cast<std::int64_t>(i));
                    position.valueType = type::I64;
                    append(value, py_runtime_call("python::tuple_set", { type::Object, type::I64, type::Object }, type::Void,
                        { local_value(local, type::Object), position, py_object(operand_type{ extraPositional[i] }) }));
                }
                put_local(value, opcode::LocalGet, local);
                value.valueType = type::Object;
                value.fragmentKind = fragment_kind::Argument;
                result.push_back(value);
            }
            else if (parameter.kind == py_parameter::VarKeywords)
            {
                auto dict = py_owned(py_runtime_call("python::dict", {}, type::Object, {}));
                auto const local = temporary(type::I32);
                fragment value = dict;
                put_local(value, opcode::LocalSet, local);
                for (auto const& keyword : extraKeywords)
                    append(value, py_runtime_call("python::kwarg", { type::Object, type::String, type::Object }, type::Void,
                        { local_value(local, type::Object), py_string(keyword.first), py_object(operand_type{ keyword.second }) }));
                put_local(value, opcode::LocalGet, local);
                value.valueType = type::Object;
                value.fragmentKind = fragment_kind::Argument;
                result.push_back(value);
            }
            else if (slots[p])
                result.push_back(*slots[p]);
            else if (parameter.hasDefault)
            {
                auto* const global = find_symbol(state().program->scope, neolib::string_view{ neolib::string{ aFunction + "." + parameter.name } }, true);
                if (global == nullptr)
                    return {};
                fragment value;
                put_local(value, opcode::GlobalGet, global->index());
                value.valueType = global->type();
                value.fragmentKind = fragment_kind::Argument;
                result.push_back(value);
            }
            else
            {
                missing.push_back(parameter.name);
                missingKeywordOnly = parameter.kind == py_parameter::KeywordOnly;
            }
        }
        if (!missing.empty())
        {
            std::string names;
            for (std::size_t i = 0u; i < missing.size(); ++i)
                names += (i == 0u ? "'" : i + 1u == missing.size() ? (missing.size() > 2u ? "', and '" : "' and '") : "', '") + missing[i];
            names += "'";
            throw std::runtime_error(aFunction + "() missing " + std::to_string(missing.size()) + " required " + (missingKeywordOnly ? "keyword-only" : "positional") +
                " argument" + (missing.size() == 1u ? "" : "s") + ": " + names);
        }
        return result;
    }

    namespace
    {
        // the position of the next lambda keyword (not in a string or comment)
        std::optional<std::size_t> find_lambda(std::string const& aText)
        {
            for (std::size_t i = 0u; i < aText.size(); ++i)
            {
                char const c = aText[i];
                if (c == '\'' || c == '\"')
                {
                    bool const triple = i + 2u < aText.size() && aText[i + 1u] == c && aText[i + 2u] == c;
                    std::size_t j = i + (triple ? 3u : 1u);
                    for (; j < aText.size(); ++j)
                    {
                        if (aText[j] == '\\')
                            ++j;
                        else if (aText[j] == c && (!triple || (j + 2u < aText.size() && aText[j + 1u] == c && aText[j + 2u] == c)))
                            break;
                        else if (!triple && aText[j] == '\n')
                            break;
                    }
                    i = triple ? j + 2u : j;
                    continue;
                }
                if (c == '#')
                {
                    while (i < aText.size() && aText[i] != '\n')
                        ++i;
                    continue;
                }
                if (aText.compare(i, 6u, "lambda") == 0 && (i == 0u || !name_character(aText[i - 1u])) &&
                    (i + 6u == aText.size() || !name_character(aText[i + 6u])))
                    return i;
                if (name_character(c))
                    while (i + 1u < aText.size() && name_character(aText[i + 1u]))
                        ++i;
            }
            return {};
        }
    }

    namespace
    {
        // f-strings are expressions: f"a{x!r:>{w}}b" becomes ("a" + __fstring((x), 114, (">" + __fstring((w), 0, ""))) + "b")
        // (literal parts keep their escapes; {{ and }} are braces)
        std::string rewrite_fstrings(std::string const& aText, bool& aChanged);

        // (the end of a string literal that starts at aStart (its quote))
        std::size_t string_end(std::string const& aText, std::size_t aStart)
        {
            char const q = aText[aStart];
            bool const triple = aStart + 2u < aText.size() && aText[aStart + 1u] == q && aText[aStart + 2u] == q;
            std::size_t j = aStart + (triple ? 3u : 1u);
            for (; j < aText.size(); ++j)
            {
                if (aText[j] == '\\')
                    ++j;
                else if (aText[j] == q && (!triple || (j + 2u < aText.size() && aText[j + 1u] == q && aText[j + 2u] == q)))
                    return triple ? j + 3u : j + 1u;
                else if (!triple && aText[j] == '\n')
                    return j;
            }
            return aText.size();
        }

        // (a string literal without a prefix: a raw string's backslashes are escaped; an unknown escape is a backslash)
        std::string literal_part(std::string const& aPrefix, std::string const& aQuote, std::string const& aText)
        {
            bool const raw = aPrefix.find_first_of("rR") != std::string::npos;
            std::string result;
            for (std::size_t i = 0u; i < aText.size(); ++i)
            {
                char const c = aText[i];
                if (c != '\\' || i + 1u == aText.size())
                {
                    result += c == '\\' ? "\\\\" : std::string(1u, c);
                    continue;
                }
                char const next = aText[++i];
                if (raw)
                {
                    result += "\\\\";
                    if (next == aQuote[0])
                        result += std::string{ "\\" } + next;
                    else if (next == '\\')
                        result += "\\\\";
                    else
                        result += next;
                }
                else if (std::string_view{ "\\'\"abfnrtvxuU01234567\n\r" }.find(next) != std::string_view::npos)
                {
                    result += '\\';
                    result += next;
                }
                else
                {
                    result += "\\\\";
                    result += next;
                }
            }
            return aQuote + result + aQuote;
        }

        // (the expression for an f-string's content (between its quotes))
        std::string fstring_expression(std::string const& aContent, std::string const& aPrefix, std::string const& aQuote)
        {
            std::vector<std::string> parts;
            std::string literal;
            auto const flush = [&]()
            {
                if (!literal.empty())
                    parts.push_back(literal_part(aPrefix, aQuote, literal));
                literal.clear();
            };
            std::size_t i = 0u;
            auto const n = aContent.size();
            while (i < n)
            {
                char const c = aContent[i];
                if (c == '\\' && i + 1u < n && aPrefix.find_first_of("rR") == std::string::npos)
                {
                    literal += aContent.substr(i, 2u);
                    i += 2u;
                    continue;
                }
                if (c == '{' && i + 1u < n && aContent[i + 1u] == '{')
                {
                    literal += '{';
                    i += 2u;
                    continue;
                }
                if (c == '}' && i + 1u < n && aContent[i + 1u] == '}')
                {
                    literal += '}';
                    i += 2u;
                    continue;
                }
                if (c != '{')
                {
                    literal += c;
                    ++i;
                    continue;
                }
                // (a replacement field: the expression, up to a top-level '!', ':', '=' (debugging) or '}')
                flush();
                std::size_t j = i + 1u;
                int depth = 0;
                std::size_t expressionEnd = std::string::npos;
                for (; j < n; ++j)
                {
                    char const d = aContent[j];
                    if (d == '\'' || d == '\"')
                    {
                        j = string_end(aContent, j) - 1u;
                        continue;
                    }
                    if (d == '(' || d == '[' || d == '{')
                        ++depth;
                    else if ((d == ')' || d == ']' || d == '}') && depth > 0)
                        --depth;
                    else if (depth == 0 && (d == '}' || d == ':' || (d == '!' && j + 1u < n && aContent[j + 1u] != '=') ||
                        (d == '=' && j + 1u < n && aContent[j + 1u] != '=' && j > i + 1u && std::string_view{ "=!<>" }.find(aContent[j - 1u]) == std::string_view::npos)))
                    {
                        expressionEnd = j;
                        break;
                    }
                }
                if (expressionEnd == std::string::npos)
                    throw std::runtime_error("f-string: expecting '}'");
                auto const expression = aContent.substr(i + 1u, expressionEnd - i - 1u);
                j = expressionEnd;
                bool debug = false;
                if (aContent[j] == '=')
                {
                    debug = true;
                    ++j;
                }
                char conversion = 0;
                if (j < n && aContent[j] == '!')
                {
                    if (j + 1u >= n)
                        throw std::runtime_error("f-string: missing conversion character");
                    conversion = aContent[j + 1u];
                    if (conversion != 'r' && conversion != 's' && conversion != 'a')
                        throw std::runtime_error(std::string{ "f-string: invalid conversion character '" } + conversion + "': expected 's', 'r', or 'a'");
                    j += 2u;
                }
                std::string spec;
                if (j < n && aContent[j] == ':')
                {
                    // (the format specification: up to the matching '}'; it can contain replacement fields)
                    std::size_t k = j + 1u;
                    int braces = 0;
                    for (; k < n; ++k)
                    {
                        if (aContent[k] == '{')
                            ++braces;
                        else if (aContent[k] == '}')
                        {
                            if (braces == 0)
                                break;
                            --braces;
                        }
                    }
                    spec = aContent.substr(j + 1u, k - j - 1u);
                    j = k;
                }
                if (j >= n || aContent[j] != '}')
                    throw std::runtime_error("f-string: expecting '}'");
                if (debug)
                {
                    parts.push_back(literal_part(aPrefix, aQuote, aContent.substr(i + 1u, expressionEnd + 1u - i - 1u)));
                    if (conversion == 0 && spec.empty())
                        conversion = 'r';
                }
                auto const specExpression = spec.empty() ? std::string{ "\"\"" } : fstring_expression(spec, aPrefix, aQuote);
                parts.push_back("__fstring((" + expression + "), " + std::to_string(static_cast<int>(conversion)) + ", " + specExpression + ")");
                i = j + 1u;
            }
            flush();
            if (parts.empty())
                return aPrefix + aQuote + aQuote;
            if (parts.size() == 1u)
                return "(" + parts[0] + ")";
            std::string result = "(";
            for (std::size_t p = 0u; p < parts.size(); ++p)
                result += (p != 0u ? " + " : "") + parts[p];
            return result + ")";
        }

        std::string rewrite_fstrings(std::string const& aText, bool& aChanged)
        {
            std::string result;
            std::size_t i = 0u;
            auto const n = aText.size();
            while (i < n)
            {
                char const c = aText[i];
                if (c == '#')
                {
                    auto const end = aText.find('\n', i);
                    result += aText.substr(i, end == std::string::npos ? std::string::npos : end - i);
                    i = end == std::string::npos ? n : end;
                    continue;
                }
                if (c == '\'' || c == '\"')
                {
                    // (the prefix of the string: the letters before its quote)
                    std::size_t p = result.size();
                    while (p > 0u && std::isalpha(static_cast<unsigned char>(result[p - 1u])))
                        --p;
                    auto const prefix = result.substr(p);
                    bool const isPrefix = (p == 0u || !name_character(result[p - 1u])) && !prefix.empty() && prefix.size() <= 2u &&
                        prefix.find_first_not_of("fFrRbBuU") == std::string::npos;
                    auto const end = string_end(aText, i);
                    if (isPrefix && prefix.find_first_of("fF") != std::string::npos)
                    {
                        bool const triple = i + 2u < n && aText[i + 1u] == c && aText[i + 2u] == c;
                        std::string const quote(triple ? 3u : 1u, c);
                        // (the end of an f-string: its expressions can contain strings with the same quotes)
                        std::size_t j = i + quote.size();
                        int depth = 0;
                        for (; j < n; ++j)
                        {
                            char const d = aText[j];
                            if (d == '\\' && depth == 0)
                            {
                                ++j;
                                continue;
                            }
                            if (depth > 0 && (d == '\'' || d == '\"'))
                            {
                                j = string_end(aText, j) - 1u;
                                continue;
                            }
                            if (d == '{')
                            {
                                if (depth == 0 && j + 1u < n && aText[j + 1u] == '{')
                                    ++j;
                                else
                                    ++depth;
                            }
                            else if (d == '}' && depth > 0)
                                --depth;
                            else if (depth == 0 && aText.compare(j, quote.size(), quote) == 0)
                                break;
                            else if (!triple && d == '\n')
                                break;
                        }
                        if (j >= n || aText.compare(j, quote.size(), quote) != 0)
                            throw std::runtime_error("unterminated f-string literal");
                        std::string otherPrefix;
                        for (auto x : prefix)
                            if (x != 'f' && x != 'F')
                                otherPrefix += x;
                        result.erase(p);
                        result += fstring_expression(aText.substr(i + quote.size(), j - i - quote.size()), otherPrefix, quote);
                        i = j + quote.size();
                        aChanged = true;
                        continue;
                    }
                    {
                        // (a string with a prefix (r, u) or with an escape that is not one: a plain string literal)
                        bool const triple = i + 2u < n && aText[i + 1u] == c && aText[i + 2u] == c;
                        std::string const quote(triple ? 3u : 1u, c);
                        if (end < i + 2u * quote.size() || aText.compare(end - quote.size(), quote.size(), quote) != 0)
                        {
                            result += aText.substr(i, end - i);
                            i = end;
                            continue;
                        }
                        auto const contentEnd = end - quote.size();
                        auto const content = aText.substr(i + quote.size(), contentEnd - i - quote.size());
                        std::string const usedPrefix = isPrefix ? prefix : std::string{};
                        auto const plain = literal_part(usedPrefix, quote, content);
                        if (plain != aText.substr(i, end - i) || !usedPrefix.empty())
                        {
                            if (isPrefix && prefix.find_first_of("bB") != std::string::npos)
                                throw std::runtime_error("bytes literals are not supported");
                            result.erase(result.size() - usedPrefix.size());
                            result += plain;
                            aChanged = true;
                        }
                        else
                            result += aText.substr(i, end - i);
                    }
                    i = end;
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c)) && (result.empty() || (!name_character(result.back()) && result.back() != '.')))
                {
                    // (a number: hexadecimal, octal and binary integers are decimal; digits can be separated by '_')
                    std::size_t j = i;
                    std::uint32_t base = 10u;
                    if (c == '0' && i + 1u < n && std::string_view{ "xXoObB" }.find(aText[i + 1u]) != std::string_view::npos)
                    {
                        auto const p = static_cast<char>(std::tolower(static_cast<unsigned char>(aText[i + 1u])));
                        base = p == 'x' ? 16u : p == 'o' ? 8u : 2u;
                        j = i + 2u;
                    }
                    auto const digit_value = [](char d) -> std::uint32_t
                    {
                        if (d >= '0' && d <= '9') return static_cast<std::uint32_t>(d - '0');
                        if (d >= 'a' && d <= 'z') return static_cast<std::uint32_t>(d - 'a' + 10);
                        if (d >= 'A' && d <= 'Z') return static_cast<std::uint32_t>(d - 'A' + 10);
                        return 99u;
                    };
                    if (base != 10u)
                    {
                        // (the value in decimal: digits of an arbitrary length)
                        std::string decimal = "0";
                        for (; j < n && (aText[j] == '_' || digit_value(aText[j]) < base); ++j)
                        {
                            if (aText[j] == '_')
                                continue;
                            std::uint32_t carry = digit_value(aText[j]);
                            for (auto k = decimal.size(); k-- > 0u;)
                            {
                                auto const v = static_cast<std::uint32_t>(decimal[k] - '0') * base + carry;
                                decimal[k] = static_cast<char>('0' + v % 10u);
                                carry = v / 10u;
                            }
                            while (carry != 0u)
                            {
                                decimal.insert(decimal.begin(), static_cast<char>('0' + carry % 10u));
                                carry /= 10u;
                            }
                        }
                        auto const first = decimal.find_first_not_of('0');
                        result += first == std::string::npos ? "0" : decimal.substr(first);
                        aChanged = true;
                        i = j;
                        continue;
                    }
                    // (a decimal number: its underscores are removed)
                    for (; j < n; ++j)
                    {
                        char const d = aText[j];
                        if (std::isdigit(static_cast<unsigned char>(d)) || d == '.' || d == '_')
                            continue;
                        if ((d == 'e' || d == 'E') && j + 1u < n && (std::isdigit(static_cast<unsigned char>(aText[j + 1u])) ||
                            ((aText[j + 1u] == '+' || aText[j + 1u] == '-') && j + 2u < n && std::isdigit(static_cast<unsigned char>(aText[j + 2u])))))
                        {
                            ++j;
                            continue;
                        }
                        break;
                    }
                    auto number = aText.substr(i, j - i);
                    if (number.find('_') != std::string::npos)
                    {
                        number.erase(std::remove(number.begin(), number.end(), '_'), number.end());
                        aChanged = true;
                    }
                    result += number;
                    i = j;
                    continue;
                }
                result += c;
                ++i;
            }
            return result;
        }
    }

    namespace
    {
        // class statements are module code: "class A(B): body" becomes "A = __class('A', (B,), {namespace})" (its body's 
        // lines are blank: line numbers are unchanged) and its methods are functions (_cN_A_name) defined at the end of
        // the module; super() in a method is super(A, <its first parameter>)
        struct source_line
        {
            std::string text;       ///< (without its line break)
            std::string ending;
            bool inString = false;  ///< (it starts in a triple quoted string)
        };

        std::vector<source_line> split_lines(std::string const& aText)
        {
            std::vector<source_line> lines;
            bool inString = false;
            char quote = 0;
            std::size_t start = 0u;
            while (start <= aText.size())
            {
                auto end = aText.find('\n', start);
                source_line line;
                line.inString = inString;
                if (end == std::string::npos)
                {
                    line.text = aText.substr(start);
                    if (line.text.empty() && start == aText.size())
                        break;
                    end = aText.size();
                }
                else
                {
                    line.text = aText.substr(start, end - start);
                    line.ending = "\n";
                    if (!line.text.empty() && line.text.back() == '\r')
                    {
                        line.text.pop_back();
                        line.ending = "\r\n";
                    }
                }
                // (triple quoted strings that continue to the next line)
                auto const& t = line.text;
                for (std::size_t i = 0u; i < t.size(); ++i)
                {
                    if (inString)
                    {
                        if (t[i] == '\\')
                            ++i;
                        else if (t.compare(i, 3u, std::string(3u, quote)) == 0)
                        {
                            inString = false;
                            i += 2u;
                        }
                        continue;
                    }
                    if (t[i] == '#')
                        break;
                    if (t[i] == '\'' || t[i] == '\"')
                    {
                        if (t.compare(i, 3u, std::string(3u, t[i])) == 0)
                        {
                            inString = true;
                            quote = t[i];
                            i += 2u;
                            continue;
                        }
                        auto const q = t[i];
                        for (++i; i < t.size() && t[i] != q; ++i)
                            if (t[i] == '\\')
                                ++i;
                    }
                }
                lines.push_back(line);
                start = end + 1u;
            }
            return lines;
        }

        std::size_t indent_of(std::string const& aLine)
        {
            std::size_t i = 0u;
            while (i < aLine.size() && (aLine[i] == ' ' || aLine[i] == '\t'))
                ++i;
            return i;
        }

        bool blank_line(source_line const& aLine)
        {
            if (aLine.inString)
                return false;
            auto const i = indent_of(aLine.text);
            return i == aLine.text.size() || aLine.text[i] == '#';
        }

        // (the position of a top-level character (not in brackets or strings), or npos)
        std::size_t find_top_level(std::string const& aText, char aCharacter, std::size_t aFrom = 0u)
        {
            int depth = 0;
            for (std::size_t i = aFrom; i < aText.size(); ++i)
            {
                char const c = aText[i];
                if (c == '\'' || c == '\"')
                {
                    for (++i; i < aText.size() && aText[i] != c; ++i)
                        if (aText[i] == '\\')
                            ++i;
                    continue;
                }
                if (c == '(' || c == '[' || c == '{')
                    ++depth;
                else if (c == ')' || c == ']' || c == '}')
                    --depth;
                else if (c == aCharacter && depth == 0)
                    return i;
            }
            return std::string::npos;
        }

        // (super() in a method's code: super(Class, first parameter))
        std::string replace_super(std::string const& aText, std::string const& aClass, std::string const& aFirst)
        {
            std::string result;
            for (std::size_t i = 0u; i < aText.size(); ++i)
            {
                if (aText.compare(i, 7u, "super()") == 0 && (i == 0u || !name_character(aText[i - 1u])) && !aFirst.empty())
                {
                    result += "super(" + aClass + ", " + aFirst + ")";
                    i += 6u;
                    continue;
                }
                result += aText[i];
            }
            return result;
        }

        std::string rewrite_classes(std::string const& aText, bool& aChanged, std::size_t& aClasses)
        {
            auto lines = split_lines(aText);
            std::string methods;
            for (std::size_t l = 0u; l < lines.size(); ++l)
            {
                auto& header = lines[l];
                if (header.inString)
                    continue;
                auto const indent = indent_of(header.text);
                auto const& t = header.text;
                if (t.compare(indent, 5u, "class") != 0 || indent + 5u >= t.size() || (t[indent + 5u] != ' ' && t[indent + 5u] != '\t'))
                    continue;
                // class Name[(bases)]: [statement]
                std::size_t p = indent + 5u;
                while (p < t.size() && (t[p] == ' ' || t[p] == '\t'))
                    ++p;
                auto const nameStart = p;
                while (p < t.size() && name_character(t[p]))
                    ++p;
                auto const name = t.substr(nameStart, p - nameStart);
                if (name.empty())
                    continue;
                while (p < t.size() && (t[p] == ' ' || t[p] == '\t'))
                    ++p;
                std::string bases;
                if (p < t.size() && t[p] == '(')
                {
                    int depth = 0;
                    auto const open = p;
                    for (; p < t.size(); ++p)
                    {
                        if (t[p] == '(') ++depth;
                        else if (t[p] == ')' && --depth == 0)
                            break;
                    }
                    bases = std::string{ trimmed(t.substr(open + 1u, p - open - 1u)) };
                    ++p;
                }
                while (p < t.size() && (t[p] == ' ' || t[p] == '\t'))
                    ++p;
                if (p >= t.size() || t[p] != ':')
                    continue;
                auto const inlineBody = std::string{ trimmed(t.substr(p + 1u)) };
                auto const classNumber = ++aClasses;
                // (the body: the lines indented more than the class statement)
                std::vector<std::size_t> body;
                std::size_t end = l + 1u;
                if (inlineBody.empty() || inlineBody[0] == '#')
                    for (; end < lines.size(); ++end)
                    {
                        if (!lines[end].inString && !blank_line(lines[end]) && indent_of(lines[end].text) <= indent)
                            break;
                        body.push_back(end);
                    }
                // (trailing blank lines are not the body's)
                while (!body.empty() && blank_line(lines[body.back()]))
                {
                    body.pop_back();
                    --end;
                }
                std::vector<std::pair<std::string, std::string>> entries;
                auto const add_entry = [&](std::string const& aName, std::string const& aValue)
                {
                    for (auto& e : entries)
                        if (e.first == aName)
                        {
                            e.second = aValue;
                            return;
                        }
                    entries.emplace_back(aName, aValue);
                };
                auto const statement = [&](std::string const& aStatement)
                {
                    auto const s = std::string{ trimmed(aStatement) };
                    if (s.empty() || s == "pass" || s == "..." || s[0] == '#')
                        return;
                    if (s[0] == '\'' || s[0] == '\"')
                    {
                        add_entry("__doc__", s);
                        return;
                    }
                    // name [: annotation] = value
                    std::size_t q = 0u;
                    while (q < s.size() && name_character(s[q]))
                        ++q;
                    auto const target = s.substr(0u, q);
                    auto const equals = find_top_level(s, '=');
                    if (!target.empty() && equals != std::string::npos && (equals + 1u >= s.size() || s[equals + 1u] != '=') &&
                        std::string_view{ trimmed(s.substr(q, equals - q)) }.empty() == (s.substr(q, equals - q).find(':') == std::string::npos))
                    {
                        add_entry(target, std::string{ trimmed(s.substr(equals + 1u)) });
                        return;
                    }
                    if (!target.empty() && find_top_level(s, ':') == q)
                        return; // (an annotation without a value)
                    throw std::runtime_error("unsupported statement in class '" + name + "': " + s);
                };
                if (!inlineBody.empty() && inlineBody[0] != '#')
                    statement(inlineBody);
                std::size_t bodyIndent = 0u;
                for (auto b : body)
                    if (!blank_line(lines[b]))
                    {
                        bodyIndent = indent_of(lines[b].text);
                        break;
                    }
                std::vector<std::string> decorators;
                std::map<std::string, std::uint32_t> methodOccurrences;
                for (std::size_t k = 0u; k < body.size(); ++k)
                {
                    auto const& line = lines[body[k]];
                    if (blank_line(line))
                        continue;
                    auto const lineText = line.text.substr(std::min(bodyIndent, line.text.size()));
                    if (!lineText.empty() && lineText[0] == '@')
                    {
                        decorators.push_back(std::string{ trimmed(lineText.substr(1u)) });
                        continue;
                    }
                    if (lineText.compare(0u, 4u, "def ") == 0)
                    {
                        // (a method: the def and the lines indented more than it)
                        std::size_t m = 4u;
                        while (m < lineText.size() && lineText[m] == ' ')
                            ++m;
                        auto const methodStart = m;
                        while (m < lineText.size() && name_character(lineText[m]))
                            ++m;
                        auto const method = lineText.substr(methodStart, m - methodStart);
                        // (a method defined again (e.g. a property's setter) is another function)
                        auto const occurrence = ++methodOccurrences[method];
                        auto const function = "_c" + std::to_string(classNumber) + "_" + name + "_" + method + (occurrence > 1u ? "_" + std::to_string(occurrence) : std::string{});
                        // (its first parameter)
                        std::string first;
                        auto const open = lineText.find('(', m);
                        if (open != std::string::npos)
                        {
                            std::size_t f = open + 1u;
                            while (f < lineText.size() && lineText[f] == ' ')
                                ++f;
                            while (f < lineText.size() && name_character(lineText[f]))
                                first += lineText[f++];
                        }
                        std::string text = "def " + function + lineText.substr(m) + "\n";
                        std::size_t k2 = k + 1u;
                        for (; k2 < body.size(); ++k2)
                        {
                            auto const& inner = lines[body[k2]];
                            if (!inner.inString && !blank_line(inner) && indent_of(inner.text) <= bodyIndent)
                                break;
                            text += (inner.inString ? inner.text : inner.text.substr(std::min(bodyIndent, indent_of(inner.text)))) + "\n";
                        }
                        k = k2 - 1u;
                        methods += replace_super(text, name, first);
                        // (its decorators: staticmethod, classmethod, property, x.setter or others (called))
                        std::string value = function;
                        for (auto d = decorators.rbegin(); d != decorators.rend(); ++d)
                        {
                            if (d->size() > 7u && d->ends_with(".setter"))
                            {
                                auto const property = d->substr(0u, d->size() - 7u);
                                std::string getter = "None";
                                for (auto const& e : entries)
                                    if (e.first == property && e.second.starts_with("property(") && e.second.ends_with(")"))
                                        getter = e.second.substr(9u, e.second.size() - 10u);
                                value = "property(" + getter + ", " + value + ")";
                                add_entry(property, value);
                                value.clear();
                                break;
                            }
                            value = *d + "(" + value + ")";
                        }
                        decorators.clear();
                        if (!value.empty())
                            add_entry(method, value);
                        continue;
                    }
                    if (lineText.compare(0u, 6u, "class ") == 0)
                        throw std::runtime_error("nested classes are not supported (class '" + name + "')");
                    statement(lineText);
                }
                std::string namespaceText = "{";
                for (std::size_t e = 0u; e < entries.size(); ++e)
                    namespaceText += (e != 0u ? ", '" : "'") + entries[e].first + "': " + entries[e].second;
                namespaceText += "}";
                std::string basesText = bases.empty() ? "()" : "(" + bases + (find_top_level(bases, ',') == std::string::npos ? ",)" : ")");
                header.text = t.substr(0u, indent) + name + " = __class('" + name + "', " + basesText + ", " + namespaceText + ")";
                for (auto b : body)
                {
                    lines[b].text.clear();
                    lines[b].inString = false;
                }
                aChanged = true;
                l = end - 1u;
            }
            if (!aChanged)
                return aText;
            std::string result;
            for (auto const& line : lines)
                result += line.text + line.ending;
            if (!methods.empty())
            {
                if (!result.empty() && result.back() != '\n')
                    result += '\n';
                result += methods;
            }
            return result;
        }
    }

    void compiler::py_join_lines(i_source_fragment& aFragment)
    {
        // lines are joined within brackets: line breaks (and comments) there are whitespace (replaced by spaces, so that 
        // source positions are unchanged)
        auto const source = aFragment.source().to_std_string_view();
        bool changed = false;
        std::string text = rewrite_fstrings(std::string{ source }, changed);
        int depth = 0;
        for (std::size_t i = 0u; i < text.size(); ++i)
        {
            char const c = text[i];
            if (c == '\'' || c == '\"')
            {
                bool const triple = i + 2u < text.size() && text[i + 1u] == c && text[i + 2u] == c;
                std::size_t j = i + (triple ? 3u : 1u);
                for (; j < text.size(); ++j)
                {
                    if (text[j] == '\\')
                        ++j;
                    else if (text[j] == c && (!triple || (j + 2u < text.size() && text[j + 1u] == c && text[j + 2u] == c)))
                        break;
                    else if (!triple && text[j] == '\n')
                        break;
                }
                i = triple ? j + 2u : j;
                continue;
            }
            if (c == '#')
            {
                for (; i < text.size() && text[i] != '\n'; ++i)
                    if (depth > 0 && text[i] != '\r')
                    {
                        text[i] = ' ';
                        changed = true;
                    }
                --i;
                continue;
            }
            if (c == '(' || c == '[' || c == '{')
                ++depth;
            else if ((c == ')' || c == ']' || c == '}') && depth > 0)
                --depth;
            else if (depth > 0 && (c == '\n' || c == '\r'))
            {
                text[i] = ' ';
                changed = true;
            }
        }
        // class statements (see rewrite_classes)
        {
            std::size_t classes = 0u;
            bool classesChanged = false;
            text = rewrite_classes(text, classesChanged, classes);
            changed = changed || classesChanged;
        }
        // lambda expressions are functions: "lambda params: expression" becomes the name of a function (_lN_, padded with
        // spaces: source positions are unchanged) defined at the end of the module ("def _lN_(params): return expression")
        std::size_t lambdas = 0u;
        for (;;)
        {
            auto const position = find_lambda(text);
            if (!position)
                break;
            auto const start = *position;
            std::size_t i = start + 6u;
            int level = 0;
            // (the parameters: up to the colon)
            std::size_t colon = std::string::npos;
            for (; i < text.size(); ++i)
            {
                char const c = text[i];
                if (c == '\'' || c == '\"')
                {
                    for (++i; i < text.size() && text[i] != c; ++i)
                        if (text[i] == '\\')
                            ++i;
                    continue;
                }
                if (c == '(' || c == '[' || c == '{')
                    ++level;
                else if (c == ')' || c == ']' || c == '}')
                    --level;
                else if (c == ':' && level == 0)
                {
                    colon = i;
                    break;
                }
                else if (c == '\n')
                    break;
            }
            if (colon == std::string::npos)
                break;
            // (the expression: up to a comma or closing bracket at its level, or the end of the line)
            std::size_t end = colon + 1u;
            level = 0;
            for (; end < text.size(); ++end)
            {
                char const c = text[end];
                if (c == '\'' || c == '\"')
                {
                    for (++end; end < text.size() && text[end] != c; ++end)
                        if (text[end] == '\\')
                            ++end;
                    continue;
                }
                if (c == '(' || c == '[' || c == '{')
                    ++level;
                else if (c == ')' || c == ']' || c == '}')
                {
                    if (level == 0)
                        break;
                    --level;
                }
                else if ((c == ',' && level == 0) || c == '\n' || c == '\r' || c == '#')
                    break;
            }
            auto const parameters = text.substr(start + 6u, colon - start - 6u);
            auto const body = text.substr(colon + 1u, end - colon - 1u);
            auto const name = "_l" + std::to_string(++lambdas) + "_";
            if (name.size() > end - start)
                break;
            text.replace(start, end - start, name + std::string(end - start - name.size(), ' '));
            if (!text.empty() && text.back() != '\n')
                text += '\n';
            text += "def " + name + "(" + std::string{ trimmed(parameters) } + "):\n    return " + std::string{ trimmed(body) } + "\n";
            changed = true;
        }
        if (changed)
            aFragment.set_source(neolib::string_view{ text });
    }

    // --- values

    type compiler::py_type(operand_type const& aOperand) const
    {
        if (aOperand.holds_alternative<i_fragment>())
            return aOperand.get<i_fragment>().value_type();
        if (aOperand.holds_alternative<i_symbol_reference>())
        {
            auto const& reference = aOperand.get<i_symbol_reference>();
            auto const* symbol = find_symbol(current_scope(), neolib::string_view{ reference.name() });
            return symbol != nullptr && symbol->type() != type::Function ? symbol->type() : type::Object;
        }
        if (is_immediate(aOperand))
        {
            auto const t = get_immediate(aOperand).t;
            return t == type::Ibig ? type::I64 : t == type::Fbig ? type::F64 : t;
        }
        return static_cast<type>(aOperand.get<i_data_type>().index());
    }

    operand_type compiler::py_pop()
    {
        if (state().operandStack.empty())
            throw std::runtime_error("expression expected");
        auto operand = state().operandStack.back();
        state().operandStack.pop_back();
        if (operand.holds_alternative<i_symbol_reference>())
            return operand_type{ materialize(operand) };
        return operand;
    }

    // an int literal beyond 64 bits: an int object (python::int_from_string)
    std::optional<fragment> compiler::py_big_literal(operand_type const& aOperand)
    {
        if (!is_immediate(aOperand))
            return {};
        std::string text;
        std::visit([&](auto const& d)
            {
                using vt = std::decay_t<decltype(d)>;
                if constexpr (!std::is_same_v<vt, std::monostate>)
                    if constexpr (std::is_same_v<typename vt::type, ibig>)
                        text = d.value().value().to_string();
            }, std::get<data_type>(aOperand));
        if (text.empty())
            return {};
        try
        {
            (void)std::stoll(text);
            return {};
        }
        catch (std::out_of_range const&)
        {
        }
        fragment result;
        result.valueType = type::Object;
        if (iDeclarationPass)
            return result;
        return py_owned(py_runtime_call("python::int_from_string", { type::String }, type::Object, { py_string(text) }));
    }

    fragment compiler::py_string(std::string const& aValue)
    {
        fragment result;
        put_i32(result, static_cast<std::int32_t>(string_literal(aValue)));
        result.valueType = type::String;
        return result;
    }

    fragment compiler::py_runtime_call(std::string const& aName, std::vector<type> const& aParameters, type aResult, std::vector<fragment> const& aArguments)
    {
        fragment result;
        for (std::size_t i = 0u; i < aArguments.size(); ++i)
        {
            auto argument = aArguments[i];
            if (argument.valueType != aParameters[i])
                coerce(argument, aParameters[i]);
            append(result, argument);
        }
        emit_call_to(result, runtime_function(aName, aParameters, aResult));
        result.valueType = aResult;
        result.fragmentKind = aResult == type::Void ? fragment_kind::Statement : fragment_kind::Value;
        return result;
    }

    fragment compiler::py_owned(fragment aValue)
    {
        // a new reference: held by a temporary (released at the end of the statement)
        ensure_function();
        std::uint32_t temporaryObject;
        if (!current_function().freeObjects.empty())
        {
            temporaryObject = current_function().freeObjects.back();
            current_function().freeObjects.pop_back();
        }
        else
            temporaryObject = temporary(type::Object);
        current_function().pendingObjects.push_back(temporaryObject);
        current_function().objectTemporaries.insert(temporaryObject);
        put_local(aValue, opcode::LocalTee, temporaryObject);
        aValue.valueType = type::Object;
        aValue.fragmentKind = fragment_kind::Value;
        return aValue;
    }

    namespace
    {
        // the temporary a value is the reference of (if it is a temporary's value)
        std::optional<std::uint32_t> held_by(fragment const& aValue, std::vector<std::uint32_t> const& aPending)
        {
            auto const& code = aValue.fragmentCode.as_std_vector();
            for (auto const t : aPending)
                for (auto const o : { opcode::LocalTee, opcode::LocalGet })
                {
                    bytes tail;
                    put_op(tail, o);
                    put_u32(tail, t);
                    if (code.size() >= tail.size() && std::equal(tail.begin(), tail.end(), code.end() - static_cast<std::ptrdiff_t>(tail.size())) &&
                        (aValue.fragmentRelocations.empty() || aValue.fragmentRelocations.back().first() + 5u <= code.size() - tail.size()))
                        return t;
                }
            return {};
        }
    }

    fragment compiler::py_flush()
    {
        // the temporaries of the statement are released (and zeroed: they may not be set by the next statement that uses them)
        auto result = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
        if (!state().function || current_function().pendingObjects.empty())
            return result;
        auto const release = runtime_function("neos::object_release", { type::Object }, type::Void);
        auto& function = current_function();
        for (auto const t : function.pendingObjects)
        {
            put_local(result, opcode::LocalGet, t);
            emit_call_to(result, release);
            put_i32(result, 0);
            put_local(result, opcode::LocalSet, t);
            function.freeObjects.push_back(t);
        }
        function.pendingObjects.clear();
        return result;
    }

    fragment compiler::py_release_variables(bool aTemporaries)
    {
        // the function's objects are released (when it returns)
        auto result = aTemporaries ? py_flush() : make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
        if (!state().function || current_function().objectVariables.empty())
            return result;
        auto const release = runtime_function("neos::object_release", { type::Object }, type::Void);
        for (auto const v : current_function().objectVariables)
        {
            put_local(result, opcode::LocalGet, v);
            emit_call_to(result, release);
        }
        return result;
    }

    void compiler::py_coerce(fragment& aValue, type aTarget)
    {
        auto const from = aValue.valueType;
        if (from == type::Object)
        {
            if (aTarget == type::Boolean)
                aValue = py_runtime_call("python::truth", { type::Object }, type::Boolean, { aValue });
            else if (aTarget == type::String)
                aValue = py_runtime_call("python::str", { type::Object }, type::String, { aValue });
            else if (is_float(aTarget))
            {
                aValue = py_runtime_call("python::to_float", { type::Object }, type::F64, { aValue });
                convert(aValue.fragmentCode.as_std_vector(), type::F64, aTarget);
            }
            else if (is_numeric(aTarget))
            {
                aValue = py_runtime_call("python::to_int", { type::Object }, type::I64, { aValue });
                convert(aValue.fragmentCode.as_std_vector(), type::I64, aTarget);
            }
            else
                throw std::runtime_error("cannot convert an object to " + type_name(aTarget));
        }
        else if (aTarget == type::Object)
        {
            auto const kind = aValue.fragmentKind == fragment_kind::Argument ? fragment_kind::Argument : fragment_kind::Value;
            auto const source = aValue.fragmentSource;
            if (from == type::Void)
            {
                // (a function that returns no value returns None)
                auto none = py_runtime_call("python::none", {}, type::Object, {});
                append(aValue, none);
            }
            else if (from == type::Boolean)
                aValue = py_runtime_call("python::from_bool", { type::Boolean }, type::Object, { aValue });
            else if (from == type::String)
                aValue = py_runtime_call("python::from_str", { type::String }, type::Object, { aValue });
            else if (is_float(from))
                aValue = py_runtime_call("python::from_float", { type::F64 }, type::Object, { aValue });
            else if (is_numeric(from))
                aValue = py_runtime_call("python::from_int", { type::I64 }, type::Object, { aValue });
            else
                throw std::runtime_error("cannot convert " + type_name(from) + " to an object");
            aValue = py_owned(aValue);
            aValue.fragmentKind = kind;
            aValue.fragmentSource = source;
        }
        else if (from == type::String && aTarget == type::Boolean)
        {
            // (a string is true if it is not empty: its length in bytes is in its header)
            auto& code = aValue.fragmentCode.as_std_vector();
            put_op(code, opcode::I32LoadMem);
            put_u32(code, 2u);
            put_u32(code, static_cast<std::uint32_t>(offsetof(bytecode::string_header, bytes)));
            put_op(code, opcode::I32Const);
            put_s64(code, 0);
            put_op(code, opcode::I32Ne);
        }
        else
            convert(aValue.fragmentCode.as_std_vector(), from, aTarget);
        aValue.valueType = aTarget;
    }

    fragment compiler::py_object(operand_type const& aOperand)
    {
        auto value = materialize(aOperand);
        if (value.valueType != type::Object)
            coerce(value, type::Object);
        return value;
    }

    fragment compiler::py_name(std::string const& aName)
    {
        // a name that is not a variable: a global variable (that a function uses before the statement that binds it has
        // been compiled) or a builtin
        if (iDeclarationPass)
        {
            fragment dummy;
            dummy.valueType = type::Object;
            return dummy;
        }
        auto* const symbol = find_symbol(current_scope(), neolib::string_view{ neolib::string{ aName } });
        // a function of the program as a value: a function object (python::function) that calls it
        if (symbol == nullptr || symbol->type() == type::Function)
            for (auto const& declaration : iForwardDeclarations)
                if (declaration.name == aName && !declaration.stub && iPyParameters.contains(aName))
                {
                    auto const type_code = [](type aType) -> char
                    {
                        if (aType == type::Object) return 'O';
                        if (aType == type::Boolean) return 'B';
                        if (aType == type::String) return 'S';
                        if (aType == type::Void) return 'V';
                        if (is_float(aType)) return 'F';
                        return 'I';
                    };
                    auto const& parameters = iPyParameters.at(aName);
                    std::vector<type> parameterTypes;
                    if (declaration.signature.parameters().has_value())
                        for (auto const& p : declaration.signature.parameters().value())
                            parameterTypes.push_back(p.parameter_type());
                    std::string signature;
                    std::size_t typeIndex = 0u;
                    std::vector<fragment> defaults;
                    bool keywordOnlyMarked = false;
                    for (auto const& parameter : parameters)
                    {
                        if (!signature.empty())
                            signature += ',';
                        if (parameter.kind == py_parameter::KeywordOnly && !keywordOnlyMarked)
                        {
                            signature += "*,";
                            keywordOnlyMarked = true;
                        }
                        if (parameter.kind == py_parameter::VarArgs)
                        {
                            signature += '*';
                            keywordOnlyMarked = true;
                        }
                        else if (parameter.kind == py_parameter::VarKeywords)
                            signature += "**";
                        signature += parameter.name + ':' + type_code(typeIndex < parameterTypes.size() ? parameterTypes[typeIndex] : type::Object);
                        ++typeIndex;
                        if (parameter.hasDefault)
                        {
                            signature += '=';
                            declare_globals();
                            auto* const global = find_symbol(state().program->scope, neolib::string_view{ neolib::string{ aName + "." + parameter.name } }, true);
                            fragment value;
                            if (global != nullptr)
                            {
                                put_local(value, opcode::GlobalGet, global->index());
                                value.valueType = global->type();
                            }
                            else
                            {
                                // (defined later: by the next compilation pass)
                                { iInference.changed = true; if (std::getenv("NEOS_PY_DEBUG")) std::fprintf(stderr, "changed at %d\n", __LINE__); }
                                value = py_runtime_call("python::none", {}, type::Object, {});
                            }
                            defaults.push_back(value);
                        }
                    }
                    signature += ';';
                    signature += type_code(declaration.signature.return_type());
                    // (the function's index: a relocation)
                    fragment index;
                    auto& code = index.fragmentCode.as_std_vector();
                    put_op(code, opcode::I32Const);
                    index.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()),
                        DeferredCall | static_cast<std::uint32_t>(iModule.deferredCalls.size()) });
                    iModule.deferredCalls.emplace_back(aName, parameterTypes);
                    code.resize(code.size() + 5u);
                    index.valueType = type::I32;
                    // (the default values: a tuple)
                    fragment count;
                    put_i64(count, static_cast<std::int64_t>(defaults.size()));
                    count.valueType = type::I64;
                    auto tuple = py_owned(py_runtime_call("python::tuple", { type::I64 }, type::Object, { count }));
                    auto const local = temporary(type::I32);
                    fragment defaultsValue = tuple;
                    put_local(defaultsValue, opcode::LocalSet, local);
                    for (std::size_t i = 0u; i < defaults.size(); ++i)
                    {
                        fragment position;
                        put_i64(position, static_cast<std::int64_t>(i));
                        position.valueType = type::I64;
                        auto value = defaults[i];
                        if (value.valueType != type::Object)
                            coerce(value, type::Object);
                        append(defaultsValue, py_runtime_call("python::tuple_set", { type::Object, type::I64, type::Object }, type::Void,
                            { local_value(local, type::Object), position, value }));
                    }
                    put_local(defaultsValue, opcode::LocalGet, local);
                    defaultsValue.valueType = type::Object;
                    bool const lambda = aName.size() > 3u && aName.starts_with("_l") && aName.ends_with("_") &&
                        std::all_of(aName.begin() + 2, aName.end() - 1, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
                    return py_owned(py_runtime_call("python::function", { type::String, type::I32, type::String, type::Object }, type::Object,
                        { py_string(lambda ? "<lambda>" : aName), index, py_string(signature), defaultsValue }));
                }
        bool const inFunction = state().function && !current_function().implicitEntry;
        if (symbol == nullptr && inFunction && !builtin_names().contains(aName) &&
            (!iInference.pyFree.contains(aName) || iInference.pyModuleNames.contains(aName)))
        {
            // (a global: the next compilation pass declares it)
            iInference.pyFree.insert(aName);
            { iInference.changed = true; if (std::getenv("NEOS_PY_DEBUG")) std::fprintf(stderr, "changed at %d\n", __LINE__); }
            fragment dummy;
            put(dummy, opcode::Unreachable);
            dummy.valueType = type::Object;
            return dummy;
        }
        // a builtin (as an object; NameError if there is no such builtin)
        return py_owned(py_runtime_call("python::builtin", { type::String }, type::Object, { py_string(aName) }));
    }

    // --- variables

    i_symbol_table_entry& compiler::py_variable(std::string const& aName, type aType)
    {
        // the variable that a binding binds (defined by its first binding)
        ensure_function();
        neolib::string const name{ aName };
        if (auto* const existing = find_symbol(current_scope(), neolib::string_view{ name }, true))
            return *existing;
        bool const module = current_function().implicitEntry;
        auto t = kind_of(aType);
        if (t == type::Void)
            t = type::Object;
        if (!module)
            if (auto const declared = iInference.pyGlobals.find(current_function().name); declared != iInference.pyGlobals.end() && declared->second.contains(aName))
            {
                // (declared global)
                iInference.functionGlobals.insert(aName);
                if (auto* const global = find_symbol(state().program->scope, neolib::string_view{ name }, true); global != nullptr && is_global(*global))
                    return *global;
                for (auto const& global : iInference.globals)
                    if (global.first == aName)
                        t = global.second;
                return define_global(aName, t);
            }
        if (module)
        {
            iInference.pyModuleNames.insert(aName);
            // a top-level variable that functions use is a global variable
            if (auto* const global = find_symbol(state().program->scope, neolib::string_view{ name }, true); global != nullptr && is_global(*global))
            {
                iInference.functionGlobals.insert(aName);
                return *global;
            }
            if (iInference.pyFree.contains(aName))
            {
                iInference.functionGlobals.insert(aName);
                for (auto const& global : iInference.globals)
                    if (global.first == aName)
                        t = global.second;
                return define_global(aName, t);
            }
        }
        // (a variable that holds an object or whose type changes is declared at the start of its function)
        auto const& functionName = current_function().name;
        auto const predeclared = iInference.pyLocals.find(functionName);
        if (predeclared != iInference.pyLocals.end())
            if (auto const v = predeclared->second.find(aName); v != predeclared->second.end())
                t = v->second;
        define_symbol(name, t, neolib::string_view{});
        auto& entry = *find_symbol(current_scope(), neolib::string_view{ name }, true);
        if (t == type::Object)
        {
            current_function().objectVariables.push_back(entry.index());
            // (a function's return statements release its objects: one that precedes this variable's definition has not)
            if (current_function().returns && !module)
            {
                iInference.pyLocals[functionName][aName] = type::Object;
                { iInference.changed = true; if (std::getenv("NEOS_PY_DEBUG")) std::fprintf(stderr, "changed at %d\n", __LINE__); }
            }
        }
        return entry;
    }

    void compiler::py_kind_change(std::string const& aName)
    {
        // a variable bound to values of different types is an object (declared as such by the next compilation pass)
        neolib::string const name{ aName };
        auto* const symbol = find_symbol(current_scope(), neolib::string_view{ name });
        if (symbol != nullptr && is_global(*symbol))
        {
            for (auto& global : iInference.globals)
                if (global.first == aName)
                    global.second = type::Object;
        }
        else
        {
            auto const& signature = current_function().scope->function_signature();
            if (signature.parameters().has_value())
                for (auto const& parameter : signature.parameters().value())
                    if (parameter.parameter_name().to_std_string() == aName)
                        throw std::runtime_error("cannot bind a value of another type to parameter '" + aName + "' (of type " + type_name(parameter.parameter_type()) + ")");
            auto& t = iInference.pyLocals[current_function().name][aName];
            if (t == type::Object)
                throw std::runtime_error("cannot bind '" + aName + "'");
            t = type::Object;
        }
        { iInference.changed = true; if (std::getenv("NEOS_PY_DEBUG")) std::fprintf(stderr, "changed at %d\n", __LINE__); }
    }

    void compiler::py_bind_name(std::string const& aName, operand_type const& aValue, fragment& aStatement)
    {
        auto const valueType = kind_of(py_type(aValue));
        auto& variable = py_variable(aName, valueType);
        bool const global = is_global(variable);
        auto const variableType = variable.type();
        auto const index = variable.index();
        if (variableType == type::Object)
        {
            auto value = py_object(aValue);
            auto& function = current_function();
            auto const holder = held_by(value, function.pendingObjects);
            append(aStatement, value);
            if (holder)
            {
                // (the temporary's reference is the variable's)
                function.pendingObjects.erase(std::find(function.pendingObjects.begin(), function.pendingObjects.end(), *holder));
            }
            else
            {
                auto const scratch = temporary(type::I32);
                put_local(aStatement, opcode::LocalTee, scratch);
                emit_call_to(aStatement, runtime_function("neos::object_retain", { type::Object }, type::Void));
                put_local(aStatement, opcode::LocalGet, scratch);
            }
            put_local(aStatement, global ? opcode::GlobalGet : opcode::LocalGet, index);
            emit_call_to(aStatement, runtime_function("neos::object_release", { type::Object }, type::Void));
            put_local(aStatement, global ? opcode::GlobalSet : opcode::LocalSet, index);
            if (holder)
            {
                // (the temporary no longer holds a reference: it can be reused)
                put_i32(aStatement, 0);
                put_local(aStatement, opcode::LocalSet, *holder);
                function.freeObjects.push_back(*holder);
            }
            return;
        }
        if (valueType != kind_of(variableType))
        {
            py_kind_change(aName);
            put(aStatement, opcode::Unreachable);
            return;
        }
        append(aStatement, materialize(aValue, variableType));
        put_local(aStatement, global ? opcode::GlobalSet : opcode::LocalSet, index);
    }

    void compiler::py_bind(compilation_state::py_target const& aTarget, operand_type const& aValue, fragment& aStatement)
    {
        switch (aTarget.kind)
        {
        case compilation_state::py_target::kind_t::Name:
            py_bind_name(aTarget.name, aValue, aStatement);
            break;
        case compilation_state::py_target::kind_t::Item:
            append(aStatement, py_runtime_call("python::setitem", { type::Object, type::Object, type::Object }, type::Void,
                { *aTarget.receiver, *aTarget.key, py_object(aValue) }));
            break;
        case compilation_state::py_target::kind_t::Attribute:
            append(aStatement, py_runtime_call("python::setattr", { type::Object, type::String, type::Object }, type::Void,
                { *aTarget.receiver, py_string(aTarget.name), py_object(aValue) }));
            break;
        }
    }

    // --- operations

    void compiler::py_operation(construct aConstruct, bool aInplace, neolib::i_string_view const* aSource)
    {
        bool const unary = (aConstruct == construct::Not || aConstruct == construct::Negative || aConstruct == construct::Positive || aConstruct == construct::Invert);
        auto rhs = py_pop();
        std::optional<operand_type> lhs;
        if (!unary)
            lhs = py_pop();
        if (iDeclarationPass)
        {
            fragment dummy;
            dummy.valueType = type::Object;
            push_operand(operand_type{ dummy });
            return;
        }
        // a < b < c: (a < b) and (b < c) (b is evaluated once)
        bool const comparison = (aConstruct >= construct::LessEqual && aConstruct <= construct::Is);
        std::optional<fragment> chainedTo;
        std::optional<std::pair<std::uint32_t, type>> chainsTo;
        if (comparison && aSource != nullptr)
        {
            if (lhs->holds_alternative<i_fragment>())
                if (auto const previous = state().pyChains.find(lhs->get<i_fragment>().source().to_std_string_view().data()); previous != state().pyChains.end())
                {
                    chainedTo = fragment{ lhs->get<i_fragment>() };
                    lhs = operand_type{ local_value(previous->second.first, previous->second.second) };
                }
            auto const text = current_fragment().source().to_std_string_view();
            auto const tail = aSource->to_std_string_view();
            auto next = static_cast<std::size_t>(tail.data() + tail.size() - text.data());
            while (next < text.size() && (text[next] == ' ' || text[next] == '\t'))
                ++next;
            auto const rest = text.substr(std::min(next, text.size()));
            auto const word = [&](std::string_view aWord) { return rest.starts_with(aWord) && (rest.size() == aWord.size() || !name_character(rest[aWord.size()])); };
            bool const follows = rest.starts_with("<") || rest.starts_with(">") || rest.starts_with("==") || rest.starts_with("!=") || 
                word("in") || word("not") || word("is");
            if (follows && !iDeclarationPass)
            {
                auto value = materialize(rhs);
                auto const t = value.valueType;
                auto const local = temporary(t == type::Object ? type::I32 : t);
                put_local(value, opcode::LocalTee, local);
                value.valueType = t;
                rhs = operand_type{ value };
                chainsTo = std::make_pair(local, t);
            }
        }
        if (chainedTo || chainsTo)
        {
            if (lhs)
                push_operand(*lhs);
            push_operand(rhs);
            py_operation(aConstruct, aInplace);
            if (chainedTo)
            {
                auto const result = state().operandStack.back();
                state().operandStack.pop_back();
                push_operand(operand_type{ *chainedTo });
                push_operand(result);
                emit_python(construct::And, *aSource);
            }
            if (chainsTo)
            {
                auto& top = state().operandStack.back();
                fragment result{ top.get<i_fragment>() };
                result.fragmentSource = *aSource;
                top = operand_type{ result };
                state().pyChains[aSource->to_std_string_view().data()] = *chainsTo;
            }
            return;
        }
        auto const rt = kind_of(py_type(rhs));
        auto const lt = lhs ? kind_of(py_type(*lhs)) : rt;
        bool const native = lt != type::Object && rt != type::Object && lt != type::Void && rt != type::Void;
        auto const generic = [&](operation aOperation)
        {
            if (lhs)
                push_operand(*lhs);
            push_operand(rhs);
            emit_operation(aOperation);
        };
        auto const value = [&](fragment aResult)
        {
            push_operand(operand_type{ aResult });
        };
        auto const object_binary = [&](std::int32_t aOperator)
        {
            fragment op;
            put_i32(op, aOperator);
            op.valueType = type::I32;
            value(py_owned(py_runtime_call(aInplace ? "python::inplace" : "python::binary", { type::I32, type::Object, type::Object }, type::Object,
                { op, py_object(*lhs), py_object(rhs) })));
        };
        auto const object_compare = [&](std::int32_t aOperator)
        {
            fragment op;
            put_i32(op, aOperator);
            op.valueType = type::I32;
            value(py_owned(py_runtime_call("python::compare", { type::I32, type::Object, type::Object }, type::Object,
                { op, py_object(*lhs), py_object(rhs) })));
        };
        auto const object_unary = [&](std::int32_t aOperator)
        {
            fragment op;
            put_i32(op, aOperator);
            op.valueType = type::I32;
            value(py_owned(py_runtime_call("python::unary", { type::I32, type::Object }, type::Object, { op, py_object(rhs) })));
        };
        bool const numbers = native && is_number(lt) && is_number(rt);
        bool const integers = native && (is_integer(lt) || lt == type::Boolean) && (is_integer(rt) || rt == type::Boolean) && !(lt == type::Boolean && rt == type::Boolean);
        switch (aConstruct)
        {
        case construct::Add:
            if (numbers || (native && lt == type::String && rt == type::String))
                generic(operation::Add);
            else
                object_binary(PyAdd);
            break;
        case construct::Subtract:
            numbers ? generic(operation::Subtract) : object_binary(PySubtract);
            break;
        case construct::Multiply:
            numbers ? generic(operation::Multiply) : object_binary(PyMultiply);
            break;
        case construct::Divide:
            numbers ? generic(operation::TrueDivide) : object_binary(PyTrueDivide);
            break;
        case construct::FloorDivide:
            numbers ? generic(operation::FloorDivide) : object_binary(PyFloorDivide);
            break;
        case construct::Modulo:
            numbers ? generic(operation::Modulo) : object_binary(PyRemainder);
            break;
        case construct::MatMul:
            object_binary(PyMatMul);
            break;
        case construct::Power:
            if (numbers && is_immediate(rhs) && !is_float(get_immediate(rhs).t) && get_immediate(rhs).t != type::String &&
                get_immediate(rhs).i >= 1 && (get_immediate(rhs).i <= 2 || (integers && get_immediate(rhs).i <= 64)))
            {
                // x ** n (an integer, or a float squared): multiplication
                auto const n = get_immediate(rhs).i;
                auto x = materialize(*lhs, lt == type::Boolean ? type::I64 : kind_of(lt));
                auto const t = x.valueType;
                auto const local = temporary(t);
                fragment result;
                append(result, x);
                put_local(result, opcode::LocalSet, local);
                put_local(result, opcode::LocalGet, local);
                for (std::int64_t i = 1; i < n; ++i)
                {
                    put_local(result, opcode::LocalGet, local);
                    put(result, is_float(t) ? opcode::F64Mul : opcode::I64Mul);
                }
                result.valueType = t;
                value(result);
            }
            else
                object_binary(PyPower);
            break;
        case construct::BitAnd:
        case construct::BitOr:
        case construct::BitXor:
        case construct::LeftShift:
        case construct::RightShift:
            {
                static std::map<construct, std::int32_t> const sOperators = { { construct::BitAnd, PyAnd }, { construct::BitOr, PyOr },
                    { construct::BitXor, PyXor }, { construct::LeftShift, PyLeftShift }, { construct::RightShift, PyRightShift } };
                bool const booleans = native && lt == type::Boolean && rt == type::Boolean && aConstruct != construct::LeftShift && aConstruct != construct::RightShift;
                if (integers || booleans)
                {
                    auto const t = booleans ? type::Boolean : type::I64;
                    fragment result;
                    append(result, materialize(*lhs, t));
                    append(result, materialize(rhs, t));
                    switch (aConstruct)
                    {
                    case construct::BitAnd: put(result, booleans ? opcode::I32And : opcode::I64And); break;
                    case construct::BitOr: put(result, booleans ? opcode::I32Ior : opcode::I64Ior); break;
                    case construct::BitXor: put(result, booleans ? opcode::I32Xor : opcode::I64Xor); break;
                    case construct::LeftShift: put(result, opcode::I64Shl); break;
                    default: put(result, opcode::I64ShrS); break;
                    }
                    result.valueType = t;
                    value(result);
                }
                else
                    object_binary(sOperators.at(aConstruct));
            }
            break;
        case construct::Less:
        case construct::LessEqual:
        case construct::Greater:
        case construct::GreaterEqual:
        case construct::Equal:
        case construct::NotEqual:
            {
                static std::map<construct, std::pair<operation, std::int32_t>> const sOperators = {
                    { construct::Less, { operation::LessThan, PyLess } }, { construct::LessEqual, { operation::LessThanOrEqual, PyLessEqual } },
                    { construct::Greater, { operation::GreaterThan, PyGreater } }, { construct::GreaterEqual, { operation::GreaterThanOrEqual, PyGreaterEqual } },
                    { construct::Equal, { operation::Equal, PyEqual } }, { construct::NotEqual, { operation::NotEqual, PyNotEqual } } };
                auto const& op = sOperators.at(aConstruct);
                if (numbers || (native && lt == type::String && rt == type::String))
                    generic(op.first);
                else if (native && (aConstruct == construct::Equal || aConstruct == construct::NotEqual))
                {
                    // (a string and a number are never equal)
                    fragment result;
                    append(result, materialize(*lhs));
                    put(result, opcode::Drop);
                    append(result, materialize(rhs));
                    put(result, opcode::Drop);
                    put_i32(result, aConstruct == construct::NotEqual ? 1 : 0);
                    result.valueType = type::Boolean;
                    value(result);
                }
                else
                    object_compare(op.second);
            }
            break;
        case construct::Is:
        case construct::IsNot:
            {
                fragment result;
                if (native && lt == rt)
                {
                    generic(operation::Equal);
                    result = materialize(pop_value(), type::Boolean);
                }
                else if (native)
                {
                    append(result, materialize(*lhs));
                    put(result, opcode::Drop);
                    append(result, materialize(rhs));
                    put(result, opcode::Drop);
                    put_i32(result, 0);
                }
                else
                    result = py_runtime_call("python::is", { type::Object, type::Object }, type::Boolean, { py_object(*lhs), py_object(rhs) });
                if (aConstruct == construct::IsNot)
                    put(result, opcode::I32Eqz);
                result.valueType = type::Boolean;
                value(result);
            }
            break;
        case construct::In:
        case construct::NotIn:
            {
                auto result = py_runtime_call("python::contains", { type::Object, type::Object }, type::Boolean, { py_object(rhs), py_object(*lhs) });
                if (aConstruct == construct::NotIn)
                    put(result, opcode::I32Eqz);
                value(result);
            }
            break;
        case construct::Not:
            {
                auto result = materialize(rhs, type::Boolean);
                put(result, opcode::I32Eqz);
                result.valueType = type::Boolean;
                value(result);
            }
            break;
        case construct::Negative:
            if (native && is_number(rt))
            {
                push_operand(rt == type::Boolean ? operand_type{ materialize(rhs, type::I64) } : rhs);
                emit_operation(operation::Negate);
            }
            else
                object_unary(PyNegative);
            break;
        case construct::Positive:
            if (native && is_number(rt))
                push_operand(rt == type::Boolean ? operand_type{ materialize(rhs, type::I64) } : rhs);
            else
                object_unary(PyPositive);
            break;
        case construct::Invert:
            if (native && (is_integer(rt) || rt == type::Boolean))
            {
                auto result = materialize(rhs, type::I64);
                put_i64(result, -1);
                put(result, opcode::I64Xor);
                result.valueType = type::I64;
                value(result);
            }
            else
                object_unary(PyInvert);
            break;
        default:
            throw std::logic_error("neos::language::compiler::py_operation");
        }
    }

    // --- calls

    void compiler::py_call(neolib::i_string_view const& aSource)
    {
        auto arguments = pop_fragments(aSource, fragment_kind::Argument);
        if (state().operandStack.empty())
            throw std::runtime_error("function expected");
        auto const callee = state().operandStack.back();
        state().operandStack.pop_back();
        if (iDeclarationPass)
        {
            fragment dummy;
            dummy.valueType = type::Object;
            push_operand(operand_type{ dummy });
            return;
        }
        std::vector<std::optional<std::string>> keywords;
        std::vector<int> stars;     // (1: *iterable, 2: **mapping)
        bool anyKeyword = false;
        bool anyStar = false;
        for (auto const& argument : arguments)
        {
            auto const text = trimmed(argument.source().to_std_string_view());
            stars.push_back(text.starts_with("**") ? 2 : text.starts_with("*") ? 1 : 0);
            anyStar = anyStar || stars.back() != 0;
            keywords.push_back(stars.back() != 0 ? std::nullopt : keyword_of(text));
            anyKeyword = anyKeyword || keywords.back().has_value();
        }
        if (callee.holds_alternative<i_symbol_reference>() && !anyStar)
        {
            auto const& reference = callee.get<i_symbol_reference>();
            auto const name = reference.name().to_std_string();
            auto const* symbol = find_symbol(current_scope(), neolib::string_view{ reference.name() });
            std::optional<forward_declaration> declared;
            for (auto const& declaration : iForwardDeclarations)
                if (declaration.name == name)
                    declared = declaration;
            if ((symbol != nullptr && symbol->type() == type::Function) || (symbol == nullptr && declared))
            {
                // a function of the program (or a builtin that the Python runtime plugin provides as a function)
                auto ordered = arguments;
                bool viable = true;
                std::optional<std::vector<fragment>> bound;
                if (declared && !declared->stub)
                    bound = py_bind_arguments(name, arguments, keywords);
                if (bound)
                {
                    ordered = *bound;
                    // (the arguments the call does not give are the call's)
                    for (auto& argument : ordered)
                        if (!within(argument.source(), aSource))
                            argument.fragmentSource = make_fragment(fragment_kind::Argument, argument.valueType, aSource).fragmentSource;
                }
                else if (anyKeyword)
                {
                    // (keyword arguments of a function defined by the program: in parameter order)
                    viable = declared && !declared->stub && declared->signature.parameters().has_value() &&
                        declared->signature.parameters().value().size() == arguments.size();
                    if (viable)
                    {
                        auto const& parameters = declared->signature.parameters().value();
                        std::vector<std::optional<fragment>> slots(parameters.size());
                        std::size_t next = 0u;
                        for (std::size_t i = 0u; i < arguments.size() && viable; ++i)
                        {
                            std::size_t slot = next++;
                            if (keywords[i])
                            {
                                slot = parameters.size();
                                for (std::size_t p = 0u; p < parameters.size(); ++p)
                                    if (parameters[p].parameter_name().to_std_string() == *keywords[i])
                                        slot = p;
                            }
                            if (slot >= parameters.size() || slots[slot])
                                viable = false;
                            else
                                slots[slot] = arguments[i];
                        }
                        for (std::size_t i = 0u; viable && i < slots.size(); ++i)
                            ordered[i] = *slots[i];
                    }
                }
                if (viable)
                {
                    auto const base = state().operandStack.size();
                    try
                    {
                        for (auto const& argument : ordered)
                            push_operand(operand_type{ argument });
                        emit_call(neolib::string_view{ neolib::string{ name } }, aSource);
                        auto& result = state().operandStack.back();
                        if (result.holds_alternative<i_fragment>() && result.get<i_fragment>().value_type() == type::Object)
                            result = operand_type{ py_owned(fragment{ result.get<i_fragment>() }) }; // (a function returns a new reference)
                        return;
                    }
                    catch (std::runtime_error const&)
                    {
                        state().operandStack.resize(base);
                        if (!builtin_names().contains(name))
                            throw;
                    }
                }
                else if (!builtin_names().contains(name))
                    throw std::runtime_error("invalid keyword arguments in call to '" + name + "'");
            }
        }
        // a call of an object: callable(*args, **kwargs)
        auto statement = make_fragment(fragment_kind::Value, type::Object, aSource);
        auto const callable = temporary(type::I32);
        append(statement, py_object(callee));
        put_local(statement, opcode::LocalSet, callable);
        std::size_t positional = 0u;
        for (std::size_t i = 0u; i < keywords.size(); ++i)
            if (!keywords[i] && stars[i] == 0)
                ++positional;
        fragment count;
        put_i64(count, static_cast<std::int64_t>(positional));
        count.valueType = type::I64;
        auto const args = temporary(type::I32);
        // (with *iterable arguments the positional arguments are collected in a list first)
        auto tuple = anyStar ? py_owned(py_runtime_call("python::list", {}, type::Object, {})) : py_owned(py_runtime_call("python::tuple", { type::I64 }, type::Object, { count }));
        append(statement, tuple);
        put_local(statement, opcode::LocalSet, args);
        std::optional<std::uint32_t> kwargs;
        std::int64_t index = 0;
        for (std::size_t i = 0u; i < arguments.size(); ++i)
        {
            if (stars[i] == 2 || keywords[i])
            {
                if (!kwargs)
                {
                    kwargs = temporary(type::I32);
                    append(statement, py_owned(py_runtime_call("python::dict", {}, type::Object, {})));
                    put_local(statement, opcode::LocalSet, *kwargs);
                }
                // (the value of a keyword argument follows its "name =")
                auto value = arguments[i];
                if (stars[i] == 2)
                    append(statement, py_runtime_call("python::kwargs_update", { type::Object, type::Object }, type::Void,
                        { local_value(*kwargs, type::Object), py_object(operand_type{ value }) }));
                else
                    append(statement, py_runtime_call("python::kwarg", { type::Object, type::String, type::Object }, type::Void,
                        { local_value(*kwargs, type::Object), py_string(*keywords[i]), py_object(operand_type{ value }) }));
            }
            else if (anyStar)
            {
                if (stars[i] == 1)
                    append(statement, py_runtime_call("python::args_extend", { type::Object, type::Object }, type::Void,
                        { local_value(args, type::Object), py_object(operand_type{ arguments[i] }) }));
                else
                    append(statement, py_runtime_call("python::list_append", { type::Object, type::Object }, type::Void,
                        { local_value(args, type::Object), py_object(operand_type{ arguments[i] }) }));
            }
            else
            {
                fragment position;
                put_i64(position, index++);
                position.valueType = type::I64;
                append(statement, py_runtime_call("python::tuple_set", { type::Object, type::I64, type::Object }, type::Void,
                    { local_value(args, type::Object), position, py_object(operand_type{ arguments[i] }) }));
            }
        }
        if (anyStar)
        {
            append(statement, py_owned(py_runtime_call("python::list_tuple", { type::Object }, type::Object, { local_value(args, type::Object) })));
            put_local(statement, opcode::LocalSet, args);
        }
        fragment none;
        put_i32(none, 0);
        none.valueType = type::Object;
        auto call = py_owned(py_runtime_call("python::call", { type::Object, type::Object, type::Object }, type::Object,
            { local_value(callable, type::Object), local_value(args, type::Object), kwargs ? local_value(*kwargs, type::Object) : none }));
        append(statement, call);
        statement.valueType = type::Object;
        push_operand(operand_type{ statement });
    }

    // --- statements

    void compiler::py_assign(construct aConstruct, neolib::i_string_view const& aSource)
    {
        using py_target = compilation_state::py_target;
        auto const text = aSource.to_std_string_view();
        std::vector<py_target> targets;
        auto& pending = state().pyTargets;
        while (!pending.empty() && pending.back().position >= text.data() && pending.back().position < text.data() + text.size())
        {
            targets.insert(targets.begin(), pending.back());
            pending.pop_back();
        }
        if (state().operandStack.empty())
            throw std::runtime_error("expression expected");
        auto const value = state().operandStack.back();
        state().operandStack.pop_back();
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        if (iDeclarationPass || targets.empty())
        {
            push_operand(operand_type{ statement });
            return;
        }
        if (aConstruct == construct::AugmentedAssign)
        {
            // target op= value
            static std::vector<std::pair<std::string_view, construct>> const sOperators = {
                { "**=", construct::Power }, { "//=", construct::FloorDivide }, { "<<=", construct::LeftShift }, { ">>=", construct::RightShift },
                { "+=", construct::Add }, { "-=", construct::Subtract }, { "*=", construct::Multiply }, { "/=", construct::Divide },
                { "%=", construct::Modulo }, { "@=", construct::MatMul }, { "&=", construct::BitAnd }, { "|=", construct::BitOr }, { "^=", construct::BitXor } };
            std::optional<construct> op;
            std::size_t position = text.size();
            for (auto const& candidate : sOperators)
                for (auto const p : top_level(text, candidate.first[0]))
                    if (p < position && text.substr(p, candidate.first.size()) == candidate.first)
                    {
                        position = p;
                        op = candidate.second;
                    }
            if (!op)
                throw std::runtime_error("augmented assignment operator expected");
            auto target = targets.back();
            operand_type current;
            switch (target.kind)
            {
            case py_target::kind_t::Name:
                current = operand_type{ symbol_reference{ current_scope(), neolib::string{ target.name } } };
                if (find_symbol(current_scope(), neolib::string_view{ neolib::string{ target.name } }) == nullptr)
                    throw std::runtime_error("name '" + target.name + "' is not defined");
                break;
            case py_target::kind_t::Item:
            case py_target::kind_t::Attribute:
                {
                    // (the receiver and key are evaluated once)
                    auto const receiver = temporary(type::I32);
                    append(statement, *target.receiver);
                    put_local(statement, opcode::LocalSet, receiver);
                    target.receiver = local_value(receiver, type::Object);
                    if (target.kind == py_target::kind_t::Item)
                    {
                        auto const key = temporary(type::I32);
                        append(statement, *target.key);
                        put_local(statement, opcode::LocalSet, key);
                        target.key = local_value(key, type::Object);
                        current = operand_type{ py_owned(py_runtime_call("python::getitem", { type::Object, type::Object }, type::Object, { *target.receiver, *target.key })) };
                    }
                    else
                        current = operand_type{ py_owned(py_runtime_call("python::getattr", { type::Object, type::String }, type::Object, { *target.receiver, py_string(target.name) })) };
                }
                break;
            }
            push_operand(current);
            push_operand(value);
            py_operation(*op, true);
            auto const result = state().operandStack.back();
            state().operandStack.pop_back();
            py_bind(target, result, statement);
        }
        else
        {
            // targets = value
            auto const equals = top_level(text, '=');
            std::string_view const left = text.substr(0u, equals.empty() ? text.size() : equals[0]);
            bool const unpack = targets.size() > 1u || !top_level(left, ',').empty();
            if (!unpack)
                py_bind(targets[0], value, statement);
            else
            {
                auto const items = value.holds_alternative<i_fragment>() ?
                    state().pyTuples.find(value.get<i_fragment>().source().to_std_string_view().data()) : state().pyTuples.end();
                if (items != state().pyTuples.end() && items->second.size() == targets.size())
                {
                    // a, b = x, y: the values (which are evaluated first) are not packed into a tuple
                    std::vector<fragment> values;
                    for (auto const& item : items->second)
                    {
                        auto v = materialize(operand_type{ item });
                        auto const t = v.valueType == type::Object ? type::I32 : v.valueType;
                        auto const local = temporary(t);
                        append(statement, v);
                        put_local(statement, opcode::LocalSet, local);
                        values.push_back(local_value(local, v.valueType));
                    }
                    for (std::size_t i = 0u; i < targets.size(); ++i)
                        py_bind(targets[i], operand_type{ values[i] }, statement);
                }
                else
                {
                    fragment count;
                    put_i64(count, static_cast<std::int64_t>(targets.size()));
                    count.valueType = type::I64;
                    auto const sequence = temporary(type::I32);
                    append(statement, py_owned(py_runtime_call("python::unpack", { type::Object, type::I64 }, type::Object, { py_object(value), count })));
                    put_local(statement, opcode::LocalSet, sequence);
                    for (std::size_t i = 0u; i < targets.size(); ++i)
                    {
                        fragment index;
                        put_i64(index, static_cast<std::int64_t>(i));
                        index.valueType = type::I64;
                        auto const item = py_owned(py_runtime_call("python::tuple_get", { type::Object, type::I64 }, type::Object, { local_value(sequence, type::Object), index }));
                        py_bind(targets[i], operand_type{ item }, statement);
                    }
                }
            }
        }
        push_operand(operand_type{ statement });
    }

    void compiler::py_import(construct aConstruct, neolib::i_string_view const& aSource)
    {
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        if (iDeclarationPass)
        {
            push_operand(operand_type{ statement });
            return;
        }
        auto words = [](std::string_view aText)
        {
            std::vector<std::string> result;
            std::string word;
            for (auto c : aText)
                if (name_character(c))
                    word += c;
                else
                {
                    if (!word.empty())
                        result.push_back(word);
                    word.clear();
                    if (c == '.' || c == ',')
                        result.push_back(std::string(1u, c));
                }
            if (!word.empty())
                result.push_back(word);
            return result;
        };
        // a module: the runtime plugin of the same name provides it (its function "module"); the Python runtime plugin 
        // provides those of Python's standard library that it implements (python::import)
        auto module = [&](std::vector<std::string> const& aPath)
        {
            static std::set<std::string> const sStandard = { "math", "cmath", "sys" };
            fragment result;
            if (sStandard.contains(aPath[0]))
                result = py_owned(py_runtime_call("python::import", { type::String }, type::Object, { py_string(aPath[0]) }));
            else
            {
                auto context = py_owned(py_runtime_call("python::context", {}, type::Object, {}));
                result = py_owned(py_runtime_call(aPath[0] + "::module", { type::Object }, type::Object, { context }));
            }
            for (std::size_t i = 1u; i < aPath.size(); ++i)
                result = py_owned(py_runtime_call("python::getattr", { type::Object, type::String }, type::Object, { result, py_string(aPath[i]) }));
            return result;
        };
        auto const tokens = words(aSource.to_std_string_view());
        std::size_t i = 1u;
        std::vector<std::string> path;
        auto parse_path = [&]()
        {
            path.clear();
            path.push_back(tokens.at(i++));
            while (i + 1u < tokens.size() && tokens[i] == ".")
            {
                path.push_back(tokens[i + 1u]);
                i += 2u;
            }
        };
        parse_path();
        if (aConstruct == construct::Import)
        {
            // import a.b [as c]
            if (i + 1u < tokens.size() && tokens[i] == "as")
                py_bind_name(tokens[i + 1u], operand_type{ module(path) }, statement);
            else
                py_bind_name(path[0], operand_type{ module({ path[0] }) }, statement);
        }
        else
        {
            // from a.b import c [as d], ...
            auto const m = temporary(type::I32);
            append(statement, module(path));
            put_local(statement, opcode::LocalSet, m);
            ++i; // (import)
            while (i < tokens.size())
            {
                if (tokens[i] == ",")
                {
                    ++i;
                    continue;
                }
                auto const name = tokens[i++];
                auto bound = name;
                if (i + 1u < tokens.size() && tokens[i] == "as")
                {
                    bound = tokens[i + 1u];
                    i += 2u;
                }
                auto const value = py_owned(py_runtime_call("python::getattr", { type::Object, type::String }, type::Object, { local_value(m, type::Object), py_string(name) }));
                py_bind_name(bound, operand_type{ value }, statement);
            }
        }
        push_operand(operand_type{ statement });
    }

    namespace
    {
        // (a return, break or continue that leaves try statements: the finally bodies of aCount of them run first (py_try
        // inserts them before the marker))
        constexpr std::int32_t FinallyMarker = 0x5E7E1200;

        void put_finally_marker(fragment& aFragment, std::uint32_t aCount)
        {
            auto& code = aFragment.fragmentCode.as_std_vector();
            auto const v = static_cast<std::uint32_t>(FinallyMarker) + aCount;
            put_op(code, opcode::I32Const);
            // (a fixed width LEB128: the marker can be found and changed in place)
            code.push_back(static_cast<std::uint8_t>((v & 0x7Fu) | 0x80u));
            code.push_back(static_cast<std::uint8_t>(((v >> 7) & 0x7Fu) | 0x80u));
            code.push_back(static_cast<std::uint8_t>(((v >> 14) & 0x7Fu) | 0x80u));
            code.push_back(static_cast<std::uint8_t>(((v >> 21) & 0x7Fu) | 0x80u));
            code.push_back(static_cast<std::uint8_t>((v >> 28) & 0x0Fu));
            put_op(code, opcode::Drop);
        }

        // (the count of a marker at aAt, if one is there)
        std::optional<std::uint32_t> finally_marker_at(std::vector<std::uint8_t> const& aCode, std::size_t aAt)
        {
            if (aAt + 7u > aCode.size() || aCode[aAt] != 0x41u || aCode[aAt + 6u] != 0x1Au)
                return {};
            for (std::size_t i = 1u; i < 5u; ++i)
                if ((aCode[aAt + i] & 0x80u) == 0u)
                    return {};
            if ((aCode[aAt + 5u] & 0xF0u) != 0u)
                return {};
            std::uint32_t v = 0u;
            for (std::size_t i = 0u; i < 5u; ++i)
                v |= static_cast<std::uint32_t>(aCode[aAt + 1u + i] & 0x7Fu) << (7u * i);
            if ((v & ~0xFFu) != static_cast<std::uint32_t>(FinallyMarker))
                return {};
            return v & 0xFFu;
        }

        // (the bytes [aFrom, aTo) of a fragment, with their relocations)
        fragment fragment_slice(fragment const& aFragment, std::size_t aFrom, std::size_t aTo)
        {
            fragment result;
            result.fragmentKind = aFragment.fragmentKind;
            result.valueType = aFragment.valueType;
            auto const& code = aFragment.fragmentCode.as_std_vector();
            for (auto i = aFrom; i < aTo; ++i)
                result.fragmentCode.as_std_vector().push_back(code[i]);
            for (auto const& r : aFragment.fragmentRelocations)
                if (r.first() >= aFrom && r.first() < aTo)
                    result.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(r.first() - aFrom), r.second() });
            return result;
        }

        // (a try statement's arm: its exits that leave the try run its finally body (if any) first)
        fragment apply_finally(fragment const& aArm, std::optional<fragment> const& aFinally)
        {
            auto const& code = aArm.fragmentCode.as_std_vector();
            fragment result;
            result.fragmentKind = aArm.fragmentKind;
            result.valueType = aArm.valueType;
            result.fragmentSource = aArm.fragmentSource;
            std::size_t from = 0u;
            for (std::size_t i = 0u; i < code.size(); ++i)
            {
                auto const count = finally_marker_at(code, i);
                if (!count || *count == 0u)
                    continue;
                append(result, fragment_slice(aArm, from, i));
                if (aFinally)
                    append(result, *aFinally);
                put_finally_marker(result, *count - 1u);
                i += 6u;
                from = i + 1u;
            }
            append(result, fragment_slice(aArm, from, code.size()));
            return result;
        }
    }

    // (the number of try statements (whose bodies, handlers or else are being compiled) that a return (or a break or 
    // continue: up to the innermost loop) leaves)
    std::uint32_t compiler::py_try_depth(bool aLoop)
    {
        std::uint32_t count = 0u;
        auto const& stack = state().indentStack;
        for (auto i = stack.rbegin(); i != stack.rend(); ++i)
        {
            if (i->kind == indented_construct::Function)
                break;
            if (aLoop && (i->kind == indented_construct::While || i->kind == indented_construct::For))
                break;
            if (i->kind == indented_construct::Try || i->kind == indented_construct::Except || (i->kind == indented_construct::Else && i->tryElse))
                ++count;
        }
        return count;
    }

    void compiler::py_return(neolib::i_string_view const& aSource)
    {
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        bool const bare = (trimmed(aSource.to_std_string_view()) == "return");
        std::optional<operand_type> value;
        if (!bare)
        {
            if (state().operandStack.empty())
                throw std::runtime_error("expression expected");
            value = state().operandStack.back();
            state().operandStack.pop_back();
        }
        if (iDeclarationPass)
        {
            push_operand(operand_type{ statement });
            return;
        }
        auto const resultType = current_function().scope->function_signature().return_type();
        if (resultType == type::Void)
        {
            if (value)
            {
                auto const t = py_type(*value);
                if (t == type::Void)
                    append(statement, value->get<i_fragment>());
                else
                {
                    append(statement, materialize(*value));
                    put(statement, opcode::Drop);
                }
            }
        }
        else if (resultType == type::Object)
        {
            // (a new reference)
            if (!value)
                append(statement, py_runtime_call("python::none", {}, type::Object, {}));
            else
            {
                auto result = py_object(*value);
                auto const holder = held_by(result, current_function().pendingObjects);
                append(statement, result);
                if (holder)
                {
                    auto& pendingObjects = current_function().pendingObjects;
                    pendingObjects.erase(std::find(pendingObjects.begin(), pendingObjects.end(), *holder));
                }
                else
                {
                    auto const scratch = temporary(type::I32);
                    put_local(statement, opcode::LocalTee, scratch);
                    emit_call_to(statement, runtime_function("neos::object_retain", { type::Object }, type::Void));
                    put_local(statement, opcode::LocalGet, scratch);
                }
            }
        }
        else
        {
            if (!value)
                throw std::runtime_error("function must return a value");
            append(statement, materialize(*value, resultType));
        }
        auto const release = py_release_variables(true);
        auto const tries = py_try_depth(false);
        if (!release.fragmentCode.empty() || tries != 0u)
        {
            std::optional<std::uint32_t> result;
            if (resultType != type::Void)
            {
                result = temporary(resultType);
                put_local(statement, opcode::LocalSet, *result);
            }
            // (finally bodies run (before the function's objects are released))
            if (tries != 0u)
                put_finally_marker(statement, tries);
            append(statement, release);
            if (result)
                put_local(statement, opcode::LocalGet, *result);
        }
        put(statement, opcode::Return);
        current_function().returns = true;
        push_operand(operand_type{ statement });
    }

    void compiler::py_for_in(neolib::i_string_view const& aSource)
    {
        // for targets in iterable: the loop condition gets the iterator's next item (and assigns it to the targets)
        using py_target = compilation_state::py_target;
        auto const text = aSource.to_std_string_view();
        std::vector<py_target> targets;
        auto& pending = state().pyTargets;
        while (!pending.empty() && pending.back().position >= text.data() && pending.back().position < text.data() + text.size())
        {
            targets.insert(targets.begin(), pending.back());
            pending.pop_back();
        }
        auto inlineBody = pop_fragments(aSource, fragment_kind::Statement);
        if (state().operandStack.empty())
            throw std::runtime_error("iterable expected");
        auto const iterable = state().operandStack.back();
        state().operandStack.pop_back();
        std::optional<fragment> epilogue;
        if (iDeclarationPass)
        {
            fragment dummy;
            dummy.valueType = type::Boolean;
            push_operand(operand_type{ dummy });
        }
        else
        {
            ensure_function();
            // it = iter(iterable)
            auto const iterator = temporary(type::Object);
            current_function().objectVariables.push_back(iterator);
            auto prelude = make_fragment(fragment_kind::Statement, type::Void, aSource);
            append(prelude, py_runtime_call("python::iter", { type::Object }, type::Object, { py_object(iterable) }));
            put_local(prelude, opcode::LocalSet, iterator);
            append(prelude, py_flush());
            push_operand(operand_type{ prelude });
            // (item = next(it)) != NULL (and the targets are assigned)
            auto condition = make_fragment(fragment_kind::Value, type::Boolean, neolib::string_view{});
            auto const item = py_owned(py_runtime_call("python::next", { type::Object }, type::Object, { local_value(iterator, type::Object) }));
            auto const itemTemporary = current_function().pendingObjects.back();
            append(condition, item);
            put_block(condition, opcode::If, type::Boolean);
            auto assignment = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
            if (targets.size() == 1u && top_level(text.substr(0u, text.find(" in ")), ',').empty())
                py_bind(targets[0], operand_type{ local_value(itemTemporary, type::Object) }, assignment);
            else
            {
                fragment count;
                put_i64(count, static_cast<std::int64_t>(targets.size()));
                count.valueType = type::I64;
                auto const sequence = temporary(type::I32);
                append(assignment, py_owned(py_runtime_call("python::unpack", { type::Object, type::I64 }, type::Object, { local_value(itemTemporary, type::Object), count })));
                put_local(assignment, opcode::LocalSet, sequence);
                for (std::size_t i = 0u; i < targets.size(); ++i)
                {
                    fragment index;
                    put_i64(index, static_cast<std::int64_t>(i));
                    index.valueType = type::I64;
                    py_bind(targets[i], operand_type{ py_owned(py_runtime_call("python::tuple_get", { type::Object, type::I64 }, type::Object,
                        { local_value(sequence, type::Object), index })) }, assignment);
                }
            }
            append(condition, assignment);
            put_i32(condition, 1);
            put(condition, opcode::Else);
            put_i32(condition, 0);
            put(condition, opcode::End);
            append(condition, py_flush());
            condition.valueType = type::Boolean;
            push_operand(operand_type{ condition });
            // (the iterator is released when the loop ends)
            epilogue = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
            put_local(*epilogue, opcode::LocalGet, iterator);
            emit_call_to(*epilogue, runtime_function("neos::object_release", { type::Object }, type::Void));
            put_i32(*epilogue, 0);
            put_local(*epilogue, opcode::LocalSet, iterator);
        }
        for (auto const& statement : inlineBody)
            push_operand(operand_type{ statement });
        begin_indented(indented_construct::While, aSource);
        if (epilogue)
            state().indentStack.back().epilogue = epilogue;
    }

    void compiler::py_comprehension(neolib::i_string_view const& aSource, bool aDict)
    {
        // [element for targets in iterable if condition ...]: a list ({key: value for ...}: a dict)
        auto const text = aSource.to_std_string_view();
        std::vector<compilation_state::py_clause> clauses;
        auto& pending = state().pyClauses;
        while (!pending.empty() && pending.back().position >= text.data() && pending.back().position < text.data() + text.size())
        {
            clauses.insert(clauses.begin(), pending.back());
            pending.pop_back();
        }
        auto const element = py_pop();
        std::optional<operand_type> key;
        if (aDict)
            key = py_pop();
        auto const mark = state().pyComprehensions.empty() ? 0u : state().pyComprehensions.back();
        if (!state().pyComprehensions.empty())
            state().pyComprehensions.pop_back();
        if (iDeclarationPass)
        {
            fragment dummy;
            dummy.valueType = type::Object;
            push_operand(operand_type{ dummy });
            return;
        }
        auto value = py_object(element);
        std::optional<fragment> keyValue;
        if (key)
            keyValue = py_object(*key);
        // the comprehension's temporaries are released at the start of each iteration of its loops (and are zero when it ends)
        auto& function = current_function();
        std::vector<std::uint32_t> temporaries{ function.pendingObjects.begin() + static_cast<std::ptrdiff_t>(std::min(mark, function.pendingObjects.size())), function.pendingObjects.end() };
        function.pendingObjects.resize(std::min(mark, function.pendingObjects.size()));
        auto const release = runtime_function("neos::object_release", { type::Object }, type::Void);
        fragment flush;
        for (auto const t : temporaries)
        {
            put_local(flush, opcode::LocalGet, t);
            emit_call_to(flush, release);
            put_i32(flush, 0);
            put_local(flush, opcode::LocalSet, t);
        }
        auto const list = py_owned(py_runtime_call(aDict ? "python::dict" : "python::list", {}, type::Object, {}));
        auto const listTemporary = current_function().pendingObjects.back();
        fragment result;
        append(result, list);
        put(result, opcode::Drop);
        // (the loops, innermost last)
        std::function<fragment(std::size_t)> generate = [&](std::size_t aClause) -> fragment
        {
            fragment code;
            if (aClause == clauses.size())
            {
                if (keyValue)
                    append(code, py_runtime_call("python::dict_set", { type::Object, type::Object, type::Object }, type::Void, { local_value(listTemporary, type::Object), *keyValue, value }));
                else
                    append(code, py_runtime_call("python::list_append", { type::Object, type::Object }, type::Void, { local_value(listTemporary, type::Object), value }));
                return code;
            }
            auto const& clause = clauses[aClause];
            if (clause.targets.empty())
            {
                append(code, clause.value);
                put_block(code, opcode::If, {});
                append(code, generate(aClause + 1u));
                put(code, opcode::End);
                return code;
            }
            auto const iterator = temporary(type::I32);
            auto const item = temporary(type::I32);
            append(code, py_runtime_call("python::iter", { type::Object }, type::Object, { clause.value }));
            put_local(code, opcode::LocalSet, iterator);
            put_block(code, opcode::Block, {});
            put_block(code, opcode::Loop, {});
            append(code, flush);
            append(code, py_runtime_call("python::next", { type::Object }, type::Object, { local_value(iterator, type::Object) }));
            put_local(code, opcode::LocalTee, item);
            put(code, opcode::I32Eqz);
            put(code, opcode::BrIf);
            put_u32(code.fragmentCode.as_std_vector(), 1u);
            auto assignment = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
            if (clause.targets.size() == 1u)
                py_bind(clause.targets[0], operand_type{ local_value(item, type::Object) }, assignment);
            else
            {
                fragment count;
                put_i64(count, static_cast<std::int64_t>(clause.targets.size()));
                count.valueType = type::I64;
                auto const sequence = temporary(type::I32);
                append(assignment, py_runtime_call("python::unpack", { type::Object, type::I64 }, type::Object, { local_value(item, type::Object), count }));
                put_local(assignment, opcode::LocalSet, sequence);
                for (std::size_t i = 0u; i < clause.targets.size(); ++i)
                {
                    fragment index;
                    put_i64(index, static_cast<std::int64_t>(i));
                    index.valueType = type::I64;
                    auto const part = temporary(type::I32);
                    append(assignment, py_runtime_call("python::tuple_get", { type::Object, type::I64 }, type::Object, { local_value(sequence, type::Object), index }));
                    put_local(assignment, opcode::LocalSet, part);
                    py_bind(clause.targets[i], operand_type{ local_value(part, type::Object) }, assignment);
                    put_local(assignment, opcode::LocalGet, part);
                    emit_call_to(assignment, release);
                }
                put_local(assignment, opcode::LocalGet, sequence);
                emit_call_to(assignment, release);
            }
            append(code, assignment);
            put_local(code, opcode::LocalGet, item);
            emit_call_to(code, release);
            append(code, generate(aClause + 1u));
            put(code, opcode::Br);
            put_u32(code.fragmentCode.as_std_vector(), 0u);
            put(code, opcode::End);
            put(code, opcode::End);
            put_local(code, opcode::LocalGet, iterator);
            emit_call_to(code, release);
            return code;
        };
        append(result, generate(0u));
        put_local(result, opcode::LocalGet, listTemporary);
        result.valueType = type::Object;
        result.fragmentSource = aSource;
        // (the comprehension's temporaries are not reused: code compiled after it may be evaluated before it, e.g. the function 
        // that a generator expression is the argument of)
        push_operand(operand_type{ result });
    }

    void compiler::py_function_start()
    {
        // object parameters are retained by the function's prologue (see py_function_end) and released when it returns; 
        // variables that hold objects (or whose type changes) are declared
        if (iDeclarationPass)
            return;
        auto const& signature = current_function().scope->function_signature();
        if (signature.parameters().has_value())
            for (auto const& parameter : signature.parameters().value())
                if (parameter.parameter_type() == type::Object)
                    current_function().objectVariables.push_back(find_symbol(current_scope(), neolib::string_view{ parameter.parameter_name() }, true)->index());
        auto const predeclared = iInference.pyLocals.find(current_function().name);
        if (predeclared != iInference.pyLocals.end())
            for (auto const& variable : predeclared->second)
                py_variable(variable.first, variable.second);
    }

    fragment compiler::py_function_end(bool aImplicit)
    {
        // (a function whose body is "..." is provided by the Python runtime plugin)
        auto& stack = state().operandStack;
        auto const base = current_function().operandBase;
        if (current_function().stub && stack.size() == base + 1u && stack.back().holds_alternative<i_fragment>() &&
            trimmed(stack.back().get<i_fragment>().source().to_std_string_view()) == "...")
            return make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
        // the prologue retains the object parameters
        auto prologue = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
        auto const& signature = current_function().scope->function_signature();
        if (signature.parameters().has_value())
            for (auto const& parameter : signature.parameters().value())
                if (parameter.parameter_type() == type::Object)
                {
                    put_local(prologue, opcode::LocalGet, find_symbol(current_scope(), neolib::string_view{ parameter.parameter_name() }, true)->index());
                    emit_call_to(prologue, runtime_function("neos::object_retain", { type::Object }, type::Void));
                }
        if (!prologue.fragmentCode.empty())
            stack.insert(stack.begin() + static_cast<std::ptrdiff_t>(std::min(base, stack.size())), operand_type{ prologue });
        // a function that ends without a return statement releases its objects (and returns None)
        auto result = py_release_variables(true);
        if (!aImplicit && current_function().scope->function_signature().return_type() == type::Object)
        {
            append(result, py_runtime_call("python::none", {}, type::Object, {}));
            put(result, opcode::Return);
        }
        return result;
    }

    // --- constructs

    void compiler::emit_python(construct aConstruct, neolib::i_string_view const& aSource)
    {
        auto& stack = state().operandStack;
        auto const dummy = [&](type aType)
        {
            fragment result;
            result.valueType = aType;
            push_operand(operand_type{ result });
        };
        switch (aConstruct)
        {
        case construct::Statement:
            {
                // the statement's temporaries are released
                if (iDeclarationPass || !state().function)
                    break;
                if (!stack.empty() && !(stack.back().holds_alternative<i_fragment>() && stack.back().get<i_fragment>().kind() != fragment_kind::Value))
                    emit_statement(aSource);
                auto const release = py_flush();
                if (!release.fragmentCode.empty() && !stack.empty() && stack.back().holds_alternative<i_fragment>())
                {
                    fragment statement{ stack.back().get<i_fragment>() };
                    append(statement, release);
                    statement.fragmentSource = aSource;
                    stack.back() = operand_type{ statement };
                }
            }
            break;
        case construct::Condition:
            {
                // (an if, elif or while statement's condition: its temporaries are released once it has been tested)
                if (iDeclarationPass)
                    break;
                // (statements following the colon on the header's line have been compiled)
                auto inlineBody = pop_fragments(aSource, fragment_kind::Statement);
                if (stack.empty())
                    throw std::runtime_error("condition expected");
                auto const operand = stack.back();
                if (py_type(operand) != type::Boolean || (state().function && !current_function().pendingObjects.empty()))
                {
                    stack.pop_back();
                    auto condition = materialize(operand, type::Boolean);
                    append(condition, py_flush());
                    condition.valueType = type::Boolean;
                    condition.fragmentKind = fragment_kind::Value;
                    push_operand(operand_type{ condition });
                }
                for (auto const& statement : inlineBody)
                    push_operand(operand_type{ statement });
            }
            break;
        case construct::Function:
            py_function_start();
            break;
        case construct::Return:
            py_return(aSource);
            break;
        case construct::Break:
        case construct::Continue:
            {
                auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
                if (auto const tries = py_try_depth(true); tries != 0u)
                    put_finally_marker(statement, tries);
                put(statement, opcode::Br);
                auto& code = statement.fragmentCode.as_std_vector();
                statement.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()),
                    BranchTarget | (aConstruct == construct::Continue ? ContinueTarget : 0u) });
                code.resize(code.size() + 5u);
                push_operand(operand_type{ statement });
            }
            break;
        case construct::Import:
        case construct::ImportFrom:
            py_import(aConstruct, aSource);
            break;
        case construct::Assign:
        case construct::AugmentedAssign:
            py_assign(aConstruct, aSource);
            break;
        case construct::Target:
            {
                // (an assignment target: the assignment that follows binds it)
                if (stack.empty())
                    throw std::runtime_error("target expected");
                auto const operand = stack.back();
                stack.pop_back();
                compilation_state::py_target target;
                target.position = aSource.to_std_string_view().data();
                auto const text = trimmed(aSource.to_std_string_view());
                if (operand.holds_alternative<i_symbol_reference>())
                    target.name = operand.get<i_symbol_reference>().name().to_std_string();
                else if (!iDeclarationPass && operand.holds_alternative<i_fragment>() &&
                    state().pyAccesses.contains(operand.get<i_fragment>().source().to_std_string_view().data()))
                {
                    auto const& access = state().pyAccesses.at(operand.get<i_fragment>().source().to_std_string_view().data());
                    target.receiver = access.receiver;
                    if (access.key)
                    {
                        target.kind = compilation_state::py_target::kind_t::Item;
                        target.key = access.key;
                    }
                    else
                    {
                        target.kind = compilation_state::py_target::kind_t::Attribute;
                        target.name = access.attribute;
                    }
                }
                else if (iDeclarationPass)
                    target.name = std::string{ text };
                else
                    throw std::runtime_error("cannot assign to expression '" + std::string{ text } + "'");
                state().pyTargets.push_back(target);
            }
            break;
        case construct::ForTargets:
            {
                // (the loop's variables are objects: they are defined before its body is compiled)
                if (iDeclarationPass)
                    break;
                auto const text = aSource.to_std_string_view();
                for (auto const& target : state().pyTargets)
                    if (target.kind == compilation_state::py_target::kind_t::Name && target.position >= text.data() && target.position < text.data() + text.size())
                    {
                        auto& variable = py_variable(target.name, type::Object);
                        if (variable.type() != type::Object)
                            py_kind_change(target.name);
                    }
            }
            break;
        case construct::ForIn:
            py_for_in(aSource);
            break;
        case construct::Conditional:
            {
                // a if condition else b
                auto const b = py_pop();
                auto const condition = py_pop();
                auto const a = py_pop();
                if (iDeclarationPass)
                {
                    dummy(type::Object);
                    break;
                }
                auto const at = kind_of(py_type(a));
                auto const bt = kind_of(py_type(b));
                auto const t = at == bt && at != type::Object && at != type::Void ? at : type::Object;
                auto result = materialize(condition, type::Boolean);
                put_block(result, opcode::If, t);
                append(result, t == type::Object ? py_object(a) : materialize(a, t));
                put(result, opcode::Else);
                append(result, t == type::Object ? py_object(b) : materialize(b, t));
                put(result, opcode::End);
                result.valueType = t;
                result.fragmentKind = fragment_kind::Value;
                push_operand(operand_type{ result });
            }
            break;
        case construct::Or:
        case construct::And:
            {
                // a or b: a if a is true, else b; a and b: a if a is false, else b
                auto const b = py_pop();
                auto const a = py_pop();
                if (iDeclarationPass)
                {
                    dummy(type::Object);
                    break;
                }
                auto const at = kind_of(py_type(a));
                auto const bt = kind_of(py_type(b));
                if (at == type::Boolean && bt == type::Boolean)
                {
                    push_operand(a);
                    push_operand(b);
                    emit_operation(aConstruct == construct::Or ? operation::LogicalOr : operation::LogicalAnd);
                    break;
                }
                auto const t = at == bt && at != type::Void ? at : type::Object;
                auto first = t == type::Object ? py_object(a) : materialize(a, t);
                auto const local = temporary(t == type::Object ? type::I32 : t);
                fragment result;
                append(result, first);
                put_local(result, opcode::LocalSet, local);
                auto test = local_value(local, t);
                coerce(test, type::Boolean);
                append(result, test);
                put_block(result, opcode::If, t);
                if (aConstruct == construct::Or)
                {
                    put_local(result, opcode::LocalGet, local);
                    put(result, opcode::Else);
                    append(result, t == type::Object ? py_object(b) : materialize(b, t));
                }
                else
                {
                    append(result, t == type::Object ? py_object(b) : materialize(b, t));
                    put(result, opcode::Else);
                    put_local(result, opcode::LocalGet, local);
                }
                put(result, opcode::End);
                result.valueType = t;
                push_operand(operand_type{ result });
            }
            break;
        case construct::Not:
        case construct::LessEqual:
        case construct::GreaterEqual:
        case construct::Equal:
        case construct::NotEqual:
        case construct::Less:
        case construct::Greater:
        case construct::NotIn:
        case construct::In:
        case construct::IsNot:
        case construct::Is:
        case construct::BitOr:
        case construct::BitXor:
        case construct::BitAnd:
        case construct::LeftShift:
        case construct::RightShift:
        case construct::Add:
        case construct::Subtract:
        case construct::Multiply:
        case construct::MatMul:
        case construct::FloorDivide:
        case construct::Divide:
        case construct::Modulo:
        case construct::Negative:
        case construct::Positive:
        case construct::Invert:
        case construct::Power:
            py_operation(aConstruct, false, &aSource);
            break;
        case construct::Call:
            py_call(aSource);
            break;
        case construct::Subscript:
            {
                // receiver[key] (receiver[a, b]: the key is a tuple)
                auto items = pop_fragments(aSource, fragment_kind::Argument);
                auto const receiver = py_pop();
                if (iDeclarationPass)
                {
                    dummy(type::Object);
                    break;
                }
                auto object = py_object(receiver);
                fragment key;
                if (items.size() == 1u && top_level(trimmed(aSource.to_std_string_view()).substr(1u), ',').empty())
                    key = py_object(operand_type{ items[0] });
                else
                {
                    fragment count;
                    put_i64(count, static_cast<std::int64_t>(items.size()));
                    count.valueType = type::I64;
                    auto const tuple = temporary(type::I32);
                    append(key, py_owned(py_runtime_call("python::tuple", { type::I64 }, type::Object, { count })));
                    put_local(key, opcode::LocalSet, tuple);
                    for (std::size_t i = 0u; i < items.size(); ++i)
                    {
                        fragment index;
                        put_i64(index, static_cast<std::int64_t>(i));
                        index.valueType = type::I64;
                        append(key, py_runtime_call("python::tuple_set", { type::Object, type::I64, type::Object }, type::Void,
                            { local_value(tuple, type::Object), index, py_object(operand_type{ items[i] }) }));
                    }
                    put_local(key, opcode::LocalGet, tuple);
                    key.valueType = type::Object;
                }
                auto result = py_owned(py_runtime_call("python::getitem", { type::Object, type::Object }, type::Object, { object, key }));
                result.fragmentSource = aSource;
                state().pyAccesses[aSource.to_std_string_view().data()] = compilation_state::py_access{ object, key, {} };
                push_operand(operand_type{ result });
            }
            break;
        case construct::Slice:
            {
                // [start]:[stop][:[step]]
                auto const parts = split(aSource.to_std_string_view(), ':');
                std::vector<std::optional<operand_type>> values(3u);
                for (std::size_t i = parts.size(); i-- > 0u;)
                    if (!trimmed(parts[i]).empty())
                        values[i] = py_pop();
                if (iDeclarationPass)
                {
                    dummy(type::Object);
                    break;
                }
                std::vector<fragment> arguments;
                for (auto const& v : values)
                    arguments.push_back(v ? py_object(*v) : py_owned(py_runtime_call("python::none", {}, type::Object, {})));
                push_operand(operand_type{ py_owned(py_runtime_call("python::slice", { type::Object, type::Object, type::Object }, type::Object, arguments)) });
            }
            break;
        case construct::Attribute:
            {
                // receiver.name
                auto const receiver = py_pop();
                if (iDeclarationPass)
                {
                    dummy(type::Object);
                    break;
                }
                auto text = trimmed(aSource.to_std_string_view());
                text.remove_prefix(1u);
                std::string const name{ trimmed(text) };
                auto object = py_object(receiver);
                auto result = py_owned(py_runtime_call("python::getattr", { type::Object, type::String }, type::Object, { object, py_string(name) }));
                result.fragmentSource = aSource;
                state().pyAccesses[aSource.to_std_string_view().data()] = compilation_state::py_access{ object, {}, name };
                push_operand(operand_type{ result });
            }
            break;
        case construct::EmptyTuple:
        case construct::Tuple:
        case construct::List:
            {
                auto items = aConstruct == construct::EmptyTuple ? std::vector<fragment>{} : pop_fragments(aSource, fragment_kind::Argument);
                if (iDeclarationPass)
                {
                    dummy(type::Object);
                    break;
                }
                auto const container = temporary(type::I32);
                fragment result;
                if (aConstruct == construct::List)
                    append(result, py_owned(py_runtime_call("python::list", {}, type::Object, {})));
                else
                {
                    fragment count;
                    put_i64(count, static_cast<std::int64_t>(items.size()));
                    count.valueType = type::I64;
                    append(result, py_owned(py_runtime_call("python::tuple", { type::I64 }, type::Object, { count })));
                }
                put_local(result, opcode::LocalSet, container);
                for (std::size_t i = 0u; i < items.size(); ++i)
                {
                    if (aConstruct == construct::List)
                        append(result, py_runtime_call("python::list_append", { type::Object, type::Object }, type::Void,
                            { local_value(container, type::Object), py_object(operand_type{ items[i] }) }));
                    else
                    {
                        fragment index;
                        put_i64(index, static_cast<std::int64_t>(i));
                        index.valueType = type::I64;
                        append(result, py_runtime_call("python::tuple_set", { type::Object, type::I64, type::Object }, type::Void,
                            { local_value(container, type::Object), index, py_object(operand_type{ items[i] }) }));
                    }
                }
                put_local(result, opcode::LocalGet, container);
                result.valueType = type::Object;
                result.fragmentSource = aSource;
                if (aConstruct == construct::Tuple)
                    state().pyTuples[aSource.to_std_string_view().data()] = items;
                push_operand(operand_type{ result });
            }
            break;
        case construct::Dict:
            {
                auto items = pop_fragments(aSource, fragment_kind::Argument);
                if (iDeclarationPass)
                {
                    dummy(type::Object);
                    break;
                }
                auto const dict = temporary(type::I32);
                fragment result;
                append(result, py_owned(py_runtime_call("python::dict", {}, type::Object, {})));
                put_local(result, opcode::LocalSet, dict);
                for (std::size_t i = 0u; i + 1u < items.size(); i += 2u)
                    append(result, py_runtime_call("python::dict_set", { type::Object, type::Object, type::Object }, type::Void,
                        { local_value(dict, type::Object), py_object(operand_type{ items[i] }), py_object(operand_type{ items[i + 1u] }) }));
                put_local(result, opcode::LocalGet, dict);
                result.valueType = type::Object;
                push_operand(operand_type{ result });
            }
            break;
        case construct::Ellipsis:
        case construct::None:
            if (iDeclarationPass)
                dummy(type::Object);
            else
                push_operand(operand_type{ py_owned(py_runtime_call(aConstruct == construct::None ? "python::none" : "python::ellipsis", {}, type::Object, {})) });
            break;
        case construct::Builtin:
            // (int, float, str, bool, range or object: the builtin function, or object, of that name)
            push_operand(operand_type{ symbol_reference{ current_scope(), neolib::string{ std::string{ trimmed(aSource.to_std_string_view()) } } } });
            break;
        case construct::ComprehensionStart:
            // (the statement's temporaries that precede the comprehension are not its own)
            state().pyComprehensions.push_back(state().function && !iDeclarationPass ? current_function().pendingObjects.size() : 0u);
            break;
        case construct::ComprehensionFor:
        case construct::ComprehensionIf:
            {
                compilation_state::py_clause clause;
                clause.position = aSource.to_std_string_view().data();
                auto const text = aSource.to_std_string_view();
                if (aConstruct == construct::ComprehensionFor)
                {
                    auto& pending = state().pyTargets;
                    while (!pending.empty() && pending.back().position >= text.data() && pending.back().position < text.data() + text.size())
                    {
                        clause.targets.insert(clause.targets.begin(), pending.back());
                        pending.pop_back();
                    }
                }
                auto const value = py_pop();
                if (!iDeclarationPass)
                {
                    clause.value = aConstruct == construct::ComprehensionFor ? py_object(value) : materialize(value, type::Boolean);
                    // (the comprehension's variables are objects: the next compilation pass defines them before its expression is compiled)
                    for (auto const& target : clause.targets)
                        if (target.kind == compilation_state::py_target::kind_t::Name)
                        {
                            neolib::string const name{ target.name };
                            if (find_symbol(current_scope(), neolib::string_view{ name }) == nullptr)
                            {
                                iInference.pyLocals[current_function().name][target.name] = type::Object;
                                { iInference.changed = true; if (std::getenv("NEOS_PY_DEBUG")) std::fprintf(stderr, "changed at %d\n", __LINE__); }
                            }
                            if (py_variable(target.name, type::Object).type() != type::Object)
                                py_kind_change(target.name);
                        }
                }
                state().pyClauses.push_back(clause);
            }
            break;
        case construct::Raise:
            {
                // raise [exception [from cause]] (raise alone: the exception being handled)
                auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
                auto const text = trimmed(aSource.to_std_string_view());
                bool const bare = trimmed(text.substr(5u)).empty();
                std::size_t operands = bare ? 0u : 1u;
                for (auto const position : top_level(text, ' '))
                    if (text.substr(position + 1u).starts_with("from ") && !bare)
                        operands = 2u;
                std::vector<operand_type> values;
                for (std::size_t i = 0u; i < operands; ++i)
                {
                    if (stack.empty())
                        throw std::runtime_error("exception expected");
                    values.insert(values.begin(), stack.back());
                    stack.pop_back();
                }
                if (!iDeclarationPass)
                {
                    if (bare)
                        append(statement, py_runtime_call("python::reraise", {}, type::Void, {}));
                    else
                        append(statement, py_runtime_call("python::raise", { type::Object }, type::Void, { py_object(values[0]) }));
                }
                push_operand(operand_type{ statement });
            }
            break;
        case construct::Global:
            {
                // global name, ...: the names are the module's variables (globals) in this function
                auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
                if (state().function && !current_function().implicitEntry)
                {
                    auto const text = trimmed(aSource.to_std_string_view());
                    std::string name;
                    auto const add = [&]()
                    {
                        if (name.empty() || name == "global")
                        {
                            name.clear();
                            return;
                        }
                        auto& names = iInference.pyGlobals[current_function().name];
                        if (names.insert(name).second || !iInference.pyFree.contains(name))
                        {
                            iInference.pyFree.insert(name);
                            iInference.changed = true;
                        }
                        name.clear();
                    };
                    for (auto c : text)
                    {
                        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
                            name += c;
                        else
                            add();
                    }
                    add();
                }
                push_operand(operand_type{ statement });
            }
            break;
        case construct::Imaginary:
            {
                // (an imaginary literal: a complex object)
                if (iDeclarationPass)
                {
                    dummy(type::Object);
                    break;
                }
                std::string text{ trimmed(aSource.to_std_string_view()) };
                text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
                text.pop_back();
                auto const constant = [](double aValue)
                {
                    fragment result;
                    put_op(result.fragmentCode.as_std_vector(), opcode::F64Const);
                    put_raw(result.fragmentCode.as_std_vector(), aValue);
                    result.valueType = type::F64;
                    result.fragmentKind = fragment_kind::Value;
                    return result;
                };
                push_operand(operand_type{ py_owned(py_runtime_call("python::from_complex", { type::F64, type::F64 }, type::Object,
                    { constant(0.0), constant(std::strtod(text.c_str(), nullptr)) })) });
            }
            break;
        case construct::Assert:
            {
                // assert condition [, message]
                auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
                bool const message = !top_level(trimmed(aSource.to_std_string_view()), ',').empty();
                std::optional<operand_type> text;
                if (message)
                {
                    if (stack.empty())
                        throw std::runtime_error("message expected");
                    text = stack.back();
                    stack.pop_back();
                }
                if (stack.empty())
                    throw std::runtime_error("condition expected");
                auto const condition = stack.back();
                stack.pop_back();
                if (!iDeclarationPass)
                {
                    append(statement, materialize(condition, type::Boolean));
                    put(statement, opcode::I32Eqz);
                    put_block(statement, opcode::If, std::nullopt);
                    fragment value = text ? py_object(*text) : py_runtime_call("python::none", {}, type::Object, {});
                    append(statement, py_runtime_call("python::assertion", { type::Object }, type::Void, { value }));
                    put(statement, opcode::End);
                }
                push_operand(operand_type{ statement });
            }
            break;
        case construct::ParameterDefault:
            // (its value is evaluated when the function is defined: see py_complete_signature)
            break;
        case construct::ListComprehension:
        case construct::DictComprehension:
            py_comprehension(aSource, aConstruct == construct::DictComprehension);
            break;
        default:
            throw std::runtime_error("unsupported Python construct");
        }
    }

    // --- exceptions
    //
    // An exception is raised by a runtime plugin (the Python runtime plugin for raise statements): the VM stores its 
    // "type: message" (a string) in the global __neos_error. After each call that can raise, a Python function branches to
    // the innermost enclosing exception handler if that global is set; a function without one releases its objects and 
    // returns (its caller then branches in turn); an exception that the entry point does not handle stops the program 
    // (neos::raise).

    namespace
    {
        // (markers: a block that is an exception handler's target (it follows the block's type), and a point at which an
        // exception is checked for)
        constexpr std::int32_t HandlerMarker = 0x5E7E1101;
        constexpr std::int32_t CheckMarker = 0x5E7E1102;

        void put_marker(fragment& aFragment, std::int32_t aMarker)
        {
            put_i32(aFragment, aMarker);
            put(aFragment, opcode::Drop);
        }
    }

    std::uint32_t compiler::py_error_global()
    {
        neolib::string const name{ "__neos_error" };
        iInference.functionGlobals.insert("__neos_error");
        if (auto* const existing = find_symbol(state().program->scope, neolib::string_view{ name }, true); existing != nullptr && is_global(*existing))
            return existing->index();
        auto& global = define_global("__neos_error", type::I32);
        iInference.functionGlobals.insert("__neos_error");
        return global.index();
    }

    void compiler::py_try(compilation_state::indented& aConstruct, fragment& aStatement)
    {
        // try: body except type as name: handler ... else: body finally: body
        auto const error = py_error_global();
        bool const finally = aConstruct.finallyArm.has_value();
        std::uint32_t const outer = finally ? 1u : 0u;
        // (returns, breaks and continues that leave the try run its finally body first)
        for (auto& arm : aConstruct.arms)
            arm = apply_finally(arm, aConstruct.finallyArm);
        if (aConstruct.elseArm)
            aConstruct.elseArm = apply_finally(*aConstruct.elseArm, aConstruct.finallyArm);
        if (finally)
        {
            put_block(aStatement, opcode::Block, std::nullopt);
            put_marker(aStatement, HandlerMarker);
        }
        put_block(aStatement, opcode::Block, std::nullopt);    // (done)
        put_block(aStatement, opcode::Block, std::nullopt);    // (no exception)
        put_block(aStatement, opcode::Block, std::nullopt);    // (the handlers)
        put_marker(aStatement, HandlerMarker);
        append_nested(aStatement, aConstruct.arms[0], 3u + outer);
        put(aStatement, opcode::Br);
        put_u32(aStatement.fragmentCode.as_std_vector(), 1u);
        put(aStatement, opcode::End);
        // (an exception: the handlers)
        auto const exception = temporary(type::I32);
        current_function().objectTemporaries.insert(exception);
        fragment message;
        put_local(message, opcode::GlobalGet, error);
        message.valueType = type::String;
        append(aStatement, py_runtime_call("python::caught", { type::String }, type::Object, { message }));
        put_local(aStatement, opcode::LocalSet, exception);
        put_i32(aStatement, 0);
        put_local(aStatement, opcode::GlobalSet, error);
        for (std::size_t i = 0u; i < aConstruct.handlerTypes.size(); ++i)
        {
            std::uint32_t levels = 2u + outer;
            if (aConstruct.handlerTypes[i])
            {
                auto type = *aConstruct.handlerTypes[i];
                if (type.valueType != type::Object)
                    coerce(type, type::Object);
                append(aStatement, py_runtime_call("python::exception_matches", { type::Object, type::Object }, type::Boolean, { local_value(exception, type::Object), type }));
                put_block(aStatement, opcode::If, std::nullopt);
                ++levels;
            }
            if (aConstruct.handlerNames[i])
                py_bind_name(*aConstruct.handlerNames[i], operand_type{ local_value(exception, type::Object) }, aStatement);
            append(aStatement, py_flush());
            append_nested(aStatement, aConstruct.arms[i + 1u], levels);
            append(aStatement, py_runtime_call("python::handled", {}, type::Void, {}));
            put(aStatement, opcode::Br);
            put_u32(aStatement.fragmentCode.as_std_vector(), levels - 1u - outer);
            if (aConstruct.handlerTypes[i])
                put(aStatement, opcode::End);
        }
        // (no handler: the exception propagates)
        append(aStatement, py_runtime_call("python::handled", {}, type::Void, {}));
        append(aStatement, py_runtime_call("python::raise", { type::Object }, type::Void, { local_value(exception, type::Object) }));
        put(aStatement, opcode::Br);
        put_u32(aStatement.fragmentCode.as_std_vector(), 1u);
        put(aStatement, opcode::End);   // (no exception)
        if (aConstruct.elseArm)
            append_nested(aStatement, *aConstruct.elseArm, 1u + outer);
        put(aStatement, opcode::End);   // (done)
        put_local(aStatement, opcode::LocalGet, exception);
        emit_call_to(aStatement, runtime_function("neos::object_release", { type::Object }, type::Void));
        put_i32(aStatement, 0);
        put_local(aStatement, opcode::LocalSet, exception);
        if (finally)
        {
            put(aStatement, opcode::End);
            // (finally: an exception is kept while its body runs, then propagates)
            auto const pending = temporary(type::I32);
            put_local(aStatement, opcode::GlobalGet, error);
            put_local(aStatement, opcode::LocalSet, pending);
            put_i32(aStatement, 0);
            put_local(aStatement, opcode::GlobalSet, error);
            append(aStatement, *aConstruct.finallyArm);
            put_local(aStatement, opcode::LocalGet, pending);
            put_block(aStatement, opcode::If, std::nullopt);
            put_local(aStatement, opcode::LocalGet, pending);
            put_local(aStatement, opcode::GlobalSet, error);
            put(aStatement, opcode::End);
            put_marker(aStatement, CheckMarker);
        }
    }

    void compiler::py_with(compilation_state::indented& aConstruct, fragment& aStatement)
    {
        // with manager as name: body
        auto const error = py_error_global();
        auto const manager = temporary(type::I32);
        current_function().objectTemporaries.insert(manager);
        auto value = aConstruct.conditions[0];
        if (value.valueType != type::Object)
            coerce(value, type::Object);
        auto const holder = held_by(value, current_function().pendingObjects);
        append(aStatement, value);
        if (holder)
        {
            current_function().pendingObjects.erase(std::find(current_function().pendingObjects.begin(), current_function().pendingObjects.end(), *holder));
            put_local(aStatement, opcode::LocalSet, manager);
            put_i32(aStatement, 0);
            put_local(aStatement, opcode::LocalSet, *holder);
            current_function().freeObjects.push_back(*holder);
        }
        else
        {
            put_local(aStatement, opcode::LocalTee, manager);
            emit_call_to(aStatement, runtime_function("neos::object_retain", { type::Object }, type::Void));
        }
        auto entered = py_owned(py_runtime_call("python::with_enter", { type::Object }, type::Object, { local_value(manager, type::Object) }));
        if (aConstruct.handlerNames[0])
            py_bind_name(*aConstruct.handlerNames[0], operand_type{ entered }, aStatement);
        else
            append(aStatement, entered), put(aStatement, opcode::Drop);
        append(aStatement, py_flush());
        put_block(aStatement, opcode::Block, std::nullopt);
        put_marker(aStatement, HandlerMarker);
        append_nested(aStatement, aConstruct.arms[0], 1u);
        put(aStatement, opcode::End);
        // (the manager's exit: an exception that it does not suppress propagates)
        auto const pending = temporary(type::I32);
        put_local(aStatement, opcode::GlobalGet, error);
        put_local(aStatement, opcode::LocalSet, pending);
        put_i32(aStatement, 0);
        put_local(aStatement, opcode::GlobalSet, error);
        put_local(aStatement, opcode::LocalGet, pending);
        put_block(aStatement, opcode::If, type::I32);
        append(aStatement, py_runtime_call("python::with_exit_exception", { type::Object, type::String }, type::Boolean,
            { local_value(manager, type::Object), local_value(pending, type::String) }));
        put(aStatement, opcode::I32Eqz);
        put(aStatement, opcode::Else);
        append(aStatement, py_runtime_call("python::with_exit", { type::Object }, type::Void, { local_value(manager, type::Object) }));
        put_i32(aStatement, 0);
        put(aStatement, opcode::End);
        put_block(aStatement, opcode::If, std::nullopt);
        put_local(aStatement, opcode::LocalGet, pending);
        put_local(aStatement, opcode::GlobalSet, error);
        put(aStatement, opcode::End);
        put_local(aStatement, opcode::LocalGet, manager);
        emit_call_to(aStatement, runtime_function("neos::object_release", { type::Object }, type::Void));
        put_i32(aStatement, 0);
        put_local(aStatement, opcode::LocalSet, manager);
        put_marker(aStatement, CheckMarker);
    }

    void compiler::py_error_checks(wasm_function& aFunction)
    {
        // after each call that can raise an exception: a branch to the innermost handler (or to the function's exit)
        auto const& code = aFunction.body;
        std::size_t p = 0u;
        auto const leb = [&]() -> std::uint64_t
        {
            std::uint64_t value = 0u;
            for (std::uint32_t shift = 0u; p < code.size(); shift += 7u)
            {
                auto const b = code[p++];
                if (shift < 64u)
                    value |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
                if ((b & 0x80u) == 0u)
                    break;
            }
            return value;
        };
        auto const sleb = [&]() -> std::int64_t
        {
            std::int64_t value = 0;
            std::uint32_t shift = 0u;
            std::uint8_t b = 0u;
            do
            {
                if (p >= code.size())
                    break;
                b = code[p++];
                if (shift < 64u)
                    value |= static_cast<std::int64_t>(b & 0x7Fu) << shift;
                shift += 7u;
            } while ((b & 0x80u) != 0u);
            if (shift < 64u && (b & 0x40u) != 0u)
                value |= -(static_cast<std::int64_t>(1) << shift);
            return value;
        };
        // (a call's target can raise: a function of the program, or of a runtime plugin other than the VM's)
        static std::set<std::string> const sCannotRaise = { "python::none", "python::from_int", "python::from_float", "python::from_bool", "python::from_str",
            "python::tuple", "python::tuple_set", "python::list", "python::dict", "python::kwarg", "python::context", "python::caught", "python::list_tuple",
            "python::ellipsis", "python::exception_matches", "python::handled" };
        auto const can_raise = [&](std::uint32_t aTarget)
        {
            if ((aTarget & DeferredCall) != 0u)
                return true;
            if (aTarget >= iModule.functions.size())
                return false;
            auto const& f = iModule.functions[aTarget];
            if (f.intrinsic)
                return false;
            auto name = f.name;
            if (f.stub)
                name = name.substr(0u, name.find('('));
            if (name.starts_with("neos::") || name.starts_with("__neos"))
                return false;
            return !sCannotRaise.contains(name);
        };
        auto const relocationAt = [&](std::size_t aStart, std::size_t aEnd) -> std::optional<std::uint32_t>
        {
            for (auto const& r : aFunction.relocations)
                if (r.first >= aStart && r.first < aEnd)
                    return r.second;
            return {};
        };
        bytes result;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> relocations;
        std::vector<bool> blocks{ true };   // (the function's exit)
        bool checked = false;
        auto const error = py_error_global();
        auto const put_check = [&]()
        {
            std::size_t handler = blocks.size() - 1u;
            while (!blocks[handler])
                --handler;
            put_op(result, opcode::GlobalGet);
            put_u32(result, error);
            put_op(result, opcode::BrIf);
            put_u32(result, static_cast<std::uint32_t>(blocks.size() - 1u - handler));
            checked = true;
        };
        auto const is_marker = [&](std::size_t aAt, std::int32_t aMarker) -> bool
        {
            if (aAt >= code.size() || code[aAt] != 0x41u)
                return false;
            auto const saved = p;
            p = aAt + 1u;
            bool const matches = sleb() == aMarker && p < code.size() && code[p] == 0x1Au;
            auto const end = p + 1u;
            p = saved;
            if (matches)
                p = end;
            return matches;
        };
        put_op(result, opcode::Block);
        result.push_back(0x40u);
        try
        {
            while (p < code.size())
            {
                auto const start = p;
                auto const op = code[p++];
                switch (op)
                {
                case 0x02u: case 0x03u: case 0x04u:
                    {
                        auto const blockType = code.at(p);
                        if (blockType == 0x40u || (blockType >= 0x7Bu && blockType <= 0x7Fu) || blockType == 0x70u || blockType == 0x6Fu)
                            ++p;
                        else
                            sleb();
                        result.insert(result.end(), code.begin() + static_cast<std::ptrdiff_t>(start), code.begin() + static_cast<std::ptrdiff_t>(p));
                        bool handler = false;
                        if (op == 0x02u && is_marker(p, HandlerMarker))
                            handler = true;
                        blocks.push_back(handler);
                    }
                    continue;
                case 0x0Bu:
                    if (blocks.size() > 1u)
                        blocks.pop_back();
                    break;
                case 0x0Cu: case 0x0Du:
                    leb();
                    break;
                case 0x0Eu:
                    for (auto n = leb() + 1u; n > 0u; --n)
                        leb();
                    break;
                case 0x10u:
                    {
                        // (a relocation's index is 5 bytes: zero until the module is emitted)
                        if (relocationAt(start + 1u, start + 2u))
                            p = start + 6u;
                        else
                            leb();
                        auto const target = relocationAt(start, p);
                        result.insert(result.end(), code.begin() + static_cast<std::ptrdiff_t>(start), code.begin() + static_cast<std::ptrdiff_t>(p));
                        if (target)
                            relocations.emplace_back(static_cast<std::uint32_t>(result.size() - (p - start - 1u)), *target);
                        if (target && can_raise(*target))
                            put_check();
                    }
                    continue;
                case 0x11u:
                    leb(); leb();
                    break;
                case 0x20u: case 0x21u: case 0x22u: case 0x23u: case 0x24u:
                    leb();
                    break;
                case 0x3Fu: case 0x40u:
                    ++p;
                    break;
                case 0x41u:
                    if (is_marker(start, CheckMarker))
                    {
                        put_check();
                        continue;
                    }
                    if (relocationAt(start + 1u, start + 2u))
                        p = start + 6u;
                    else
                        sleb();
                    break;
                case 0x42u:
                    sleb();
                    break;
                case 0x43u:
                    p += 4u;
                    break;
                case 0x44u:
                    p += 8u;
                    break;
                case 0xFCu:
                    {
                        auto const sub = leb();
                        if (sub == 8u) { leb(); ++p; }
                        else if (sub == 9u) leb();
                        else if (sub == 10u) p += 2u;
                        else if (sub == 11u) ++p;
                        else if (sub > 7u)
                            throw std::runtime_error("unknown instruction");
                    }
                    break;
                default:
                    if (op >= 0x28u && op <= 0x3Eu)
                    {
                        leb(); leb();
                    }
                    else if (!(op <= 0x01u || op == 0x05u || op == 0x0Fu || op == 0x1Au || op == 0x1Bu || (op >= 0x45u && op <= 0xC4u)))
                        throw std::runtime_error("unknown instruction");
                    break;
                }
                // (relocations of other instructions, e.g. an i32.const that is a function's index)
                if (auto const target = relocationAt(start, p))
                {
                    std::uint32_t offset = 0u;
                    for (auto const& r : aFunction.relocations)
                        if (r.first >= start && r.first < p)
                            offset = static_cast<std::uint32_t>(r.first - start);
                    relocations.emplace_back(static_cast<std::uint32_t>(result.size() + offset), *target);
                }
                result.insert(result.end(), code.begin() + static_cast<std::ptrdiff_t>(start), code.begin() + static_cast<std::ptrdiff_t>(p));
            }
        }
        catch (std::exception const&)
        {
            return;
        }
        if (!checked)
            return;
        put_op(result, opcode::End);
        auto const& signature = aFunction.signature;
        auto const resultType = signature.return_type();
        // (the end of the function's body: it returns)
        put_op(result, opcode::GlobalGet);
        put_u32(result, error);
        put_op(result, opcode::I32Eqz);
        put_op(result, opcode::If);
        result.push_back(0x40u);
        if (resultType == type::Void)
            put_op(result, opcode::Return);
        else
            put_op(result, opcode::Unreachable);
        put_op(result, opcode::End);
        // (an exception: the function's objects are released)
        auto const release = runtime_function("neos::object_release", { type::Object }, type::Void);
        std::set<std::uint32_t> objects{ aFunction.objectVariables.begin(), aFunction.objectVariables.end() };
        objects.insert(aFunction.objectTemporaries.begin(), aFunction.objectTemporaries.end());
        for (auto const local : objects)
        {
            put_op(result, opcode::LocalGet);
            put_u32(result, local);
            put_op(result, opcode::CallFunction);
            relocations.emplace_back(static_cast<std::uint32_t>(result.size()), release);
            result.resize(result.size() + 5u);
        }
        if (aFunction.entry)
        {
            // (an exception that the program does not handle)
            put_op(result, opcode::GlobalGet);
            put_u32(result, error);
            put_op(result, opcode::CallFunction);
            relocations.emplace_back(static_cast<std::uint32_t>(result.size()), runtime_function("neos::raise", { type::String }, type::Void));
            result.resize(result.size() + 5u);
        }
        if (resultType != type::Void)
        {
            auto const t = wasm_type(resultType).value_or(0x7Fu);
            switch (t)
            {
            case 0x7Eu: put_op(result, opcode::I64Const); put_s64(result, 0); break;
            case 0x7Du: put_op(result, opcode::F32Const); put_raw(result, 0.0f); break;
            case 0x7Cu: put_op(result, opcode::F64Const); put_raw(result, 0.0); break;
            default: put_op(result, opcode::I32Const); put_s64(result, 0); break;
            }
        }
        put_op(result, opcode::Return);
        if (std::getenv("NEOS_PY_DEBUG"))
        {
            std::fprintf(stderr, "%s:", aFunction.name.c_str());
            for (auto b : result) std::fprintf(stderr, " %02x", b);
            std::fprintf(stderr, "\nrelocations:");
            for (auto const& r : relocations) std::fprintf(stderr, " %u->%x", r.first, r.second);
            std::fprintf(stderr, "\n");
        }
        aFunction.body = result;
        aFunction.relocations.clear();
        for (auto const& r : relocations)
            aFunction.relocations.emplace_back(r.first, r.second);
    }
}
