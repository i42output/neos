/*
  compiler.cpp

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
#include <optional>
#include <cctype>
#include <cstddef>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <neos/bytecode/bytecode.hpp>
#include <bit>
#include <neos/language/emit.hpp>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <neos/language/unicode_names.hpp>
#include "compiler.hpp"
#include "tokens.hpp"

namespace neos::compilers::python
{
    using namespace emitter;
    using bytecode::opcode;

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

        // (a name of an imported module (_MN_name: see closures.cpp's rewrite_module_names): name)
        std::string_view unprefixed(std::string_view aName)
        {
            if (aName.size() > 4u && aName[0] == '_' && aName[1] == 'M' && std::isdigit(static_cast<unsigned char>(aName[2])))
            {
                auto const underscore = aName.find('_', 2u);
                if (underscore != std::string_view::npos && underscore + 1u < aName.size() &&
                    std::all_of(aName.begin() + 2, aName.begin() + static_cast<std::ptrdiff_t>(underscore), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
                    return aName.substr(underscore + 1u);
            }
            return aName;
        }

        // (a docstring as CPython 3.13 compiles it: tabs expanded, its first line's leading spaces and the common leading spaces of
        // its other lines removed)
        std::string clean_docstring(std::string const& aDoc)
        {
            std::string doc;
            std::size_t column = 0u;
            for (char c : aDoc)
            {
                if (c == '\t')
                {
                    auto const spaces = 8u - column % 8u;
                    doc.append(spaces, ' ');
                    column += spaces;
                    continue;
                }
                doc += c;
                column = c == '\n' ? 0u : column + 1u;
            }
            std::vector<std::string> lines;
            for (std::size_t start = 0u;;)
            {
                auto const end = doc.find('\n', start);
                lines.push_back(doc.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (end == std::string::npos)
                    break;
                start = end + 1u;
            }
            std::size_t margin = std::string::npos;
            for (std::size_t l = 1u; l < lines.size(); ++l)
            {
                auto const first = lines[l].find_first_not_of(' ');
                if (first != std::string::npos)
                    margin = std::min(margin, first);
            }
            if (margin == std::string::npos)
                margin = 0u;
            std::string result;
            for (std::size_t l = 0u; l < lines.size(); ++l)
            {
                auto const& line = lines[l];
                std::size_t skip = 0u;
                if (l == 0u)
                    skip = std::min(line.find_first_not_of(' '), line.size());
                else
                    while (skip < margin && skip < line.size() && line[skip] == ' ')
                        ++skip;
                if (l > 0u)
                    result += '\n';
                result += line.substr(skip);
            }
            return result;
        }

        // (the modules of Python's standard library that the Python runtime plugin implements (python::import))
        std::set<std::string> const& standard_modules()
        {
            static std::set<std::string> const sStandard = { "math", "cmath", "sys", "os", "time", "errno", "threading", "_thread", "queue",
                "collections", "itertools", "functools", "operator", "heapq", "bisect", "copy", "re", "string", "struct", "json", "base64", 
                "binascii", "hashlib", "random", "decimal", "fractions", "datetime", "io", "pathlib", "typing", "types", "abc", "contextlib", 
                "dataclasses", "enum", "textwrap", "statistics", "numbers", "warnings", "keyword", "platform", "shutil", "tempfile", "glob", 
                "fnmatch", "atexit", "gc", "weakref", "traceback", "inspect", "pprint", "reprlib", "array", "calendar", "zlib", "secrets", 
                "uuid", "locale", "codecs", "unicodedata", "signal", "argparse", "csv", "logging", "timeit", "_collections_abc", "posixpath", 
                "ntpath", "genericpath", "stat", "__future__", "builtins", "posix", "nt", "copyreg", "_string", "copy", "_codecs", "encodings", "_csv", 
                "linecache", "_colorize", "_suggestions", "select", "selectors", "_socket", "socket" };
            return sStandard;
        }

        // (a name of the form _<prefix>N_[rest] (e.g. _b1_C, a class's body; _e2_, a loop's else flag))
        bool numbered_name(std::string_view aName, char aPrefix, bool aRest)
        {
            aName = unprefixed(aName);
            if (aName.size() < 4u || aName[0] != '_' || aName[1] != aPrefix || !std::isdigit(static_cast<unsigned char>(aName[2])))
                return false;
            auto const underscore = aName.find('_', 2u);
            if (underscore == std::string_view::npos)
                return false;
            for (auto i = 2u; i < underscore; ++i)
                if (!std::isdigit(static_cast<unsigned char>(aName[i])))
                    return false;
            return aRest ? underscore + 1u < aName.size() : underscore + 1u == aName.size();
        }

        // (the function of a class's body (_aN_name: see statements.cpp and closures.cpp))
        bool class_body(std::string_view aName)
        {
            return numbered_name(aName, 'a', true);
        }

        // (a name that the compiler's rewrites of the source bind (not a name of the program's))
        bool synthetic_name(std::string_view aName)
        {
            return numbered_name(aName, 'e', false) || numbered_name(aName, 'm', false) || numbered_name(aName, 'c', false);
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

        // (a value that is a local's: the local)
        std::optional<std::uint32_t> local_of(fragment const& aValue)
        {
            auto const& code = aValue.fragmentCode.as_std_vector();
            if (!aValue.fragmentRelocations.empty() || code.size() < 2u || code[0] != 0x20u)
                return {};
            std::uint32_t index = 0u;
            std::size_t p = 1u;
            for (std::uint32_t shift = 0u; p < code.size() && shift < 35u; shift += 7u)
            {
                auto const b = code[p++];
                index |= static_cast<std::uint32_t>(b & 0x7Fu) << shift;
                if ((b & 0x80u) == 0u)
                    break;
            }
            if (p != code.size())
                return {};
            return index;
        }

        // (the code that identifies an int expression: its code and relocations)
        std::vector<std::uint8_t> integer_key(fragment const& aValue)
        {
            std::vector<std::uint8_t> key = aValue.fragmentCode.as_std_vector();
            for (auto const& r : aValue.fragmentRelocations)
                for (auto const v : { r.first(), r.second() })
                    for (int b = 0; b < 4; ++b)
                        key.push_back(static_cast<std::uint8_t>(v >> (8 * b)));
            key.push_back(static_cast<std::uint8_t>(aValue.fragmentRelocations.size()));
            return key;
        }

        fragment local_value(std::uint32_t aLocal, type aType)
        {
            fragment result;
            put_local(result, opcode::LocalGet, aLocal);
            result.valueType = aType;
            return result;
        }

        // (the roles of scratch locals (see py_scratch))
        enum scratch_role : std::uint32_t { ScratchPark, ScratchCoerce, ScratchCoerceI64, ScratchBind, ScratchReturn, ScratchResult, ScratchX, ScratchY, ScratchQ, ScratchM, ScratchN,
            ScratchZ, ScratchReturnValue, ScratchOwned = 0x100u /* + 2 * height + operand: an operand of a slow version (see py_integer_operation) */ };

        // (small integers: the integers that an integer value holds shifted left one bit)
        constexpr std::int64_t SmallLow = -(std::int64_t{ 1 } << 62);
        constexpr std::int64_t SmallHigh = (std::int64_t{ 1 } << 62) - 1;

        // the kind of value (Python's type) that a native type holds (an int: an integer of any size (type::Integer) if aBigIntegers 
        // (see compiler::py_big_integers), else a 64-bit integer)
        type kind_of(type aType, bool aBigIntegers)
        {
            if (aType == type::Integer)
                return aBigIntegers ? aType : type::I64;
            if (aType == type::Boolean || aType == type::String || aType == type::Object || aType == type::Void)
                return aType;
            if (is_float(aType))
                return type::F64;
            if (is_numeric(aType))
                return aBigIntegers ? type::Integer : type::I64;
            return aType;
        }

        bool is_number(type aType)
        {
            return is_numeric(aType) || aType == type::Integer; // (bool is a number)
        }

        bool is_integer(type aType)
        {
            return aType == type::Integer || (is_numeric(aType) && !is_float(aType) && aType != type::Boolean);
        }

        // whether a display ({a, b, ...}) has three or more items that are all constants (literals, and expressions of them that 
        // CPython folds: e.g. -1, 2 ** 8, (1, 'a'))
        bool py_constant_display(std::string_view aDisplay)
        {
            auto const open = aDisplay.find_first_of("[{(");
            auto const close = aDisplay.find_last_of("]})");
            if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
                return false;
            auto const text = aDisplay.substr(open + 1u, close - open - 1u);
            std::size_t items = 0u;
            bool item = false;
            int depth = 0;
            auto const alpha = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; };
            auto const alnum = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
            for (std::size_t i = 0u; i < text.size();)
            {
                char const c = text[i];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\\')
                    ++i;
                else if (c == '#')
                {
                    while (i < text.size() && text[i] != '\n')
                        ++i;
                }
                else if (c == ',' && depth == 0)
                {
                    if (!item)
                        return false;
                    ++items;
                    item = false;
                    ++i;
                }
                else if (c == '(' || c == ')' || c == ',')
                {
                    depth += c == '(' ? 1 : c == ')' ? -1 : 0;
                    item = true;
                    ++i;
                }
                else if (std::string_view{ "+-*/%~&|^<>" }.find(c) != std::string_view::npos)
                {
                    // (arithmetic and bitwise operators; not comparisons)
                    if ((c == '<' || c == '>') && (i + 1u >= text.size() || text[i + 1u] != c))
                        return false;
                    i += (c == '<' || c == '>') ? 2u : 1u;
                    item = true;
                }
                else if (c == '.' && text.substr(i, 3u) == "...")
                {
                    i += 3u;
                    item = true;
                }
                else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1u < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1u]))))
                {
                    bool const hex = c == '0' && i + 1u < text.size() && (text[i + 1u] == 'x' || text[i + 1u] == 'X');
                    ++i;
                    while (i < text.size() && (alnum(text[i]) || text[i] == '.' ||
                        (!hex && (text[i] == '+' || text[i] == '-') && (text[i - 1u] == 'e' || text[i - 1u] == 'E'))))
                        ++i;
                    item = true;
                }
                else if (alpha(c) || c == '\'' || c == '"')
                {
                    auto const start = i;
                    while (i < text.size() && alnum(text[i]))
                        ++i;
                    auto const word = text.substr(start, i - start);
                    if (i < text.size() && (text[i] == '\'' || text[i] == '"'))
                    {
                        // (a string literal: its prefix (not an f-string's))
                        if (word.size() > 2u || word.find_first_not_of("rRbBuU") != std::string_view::npos)
                            return false;
                        auto const quote = text[i];
                        bool const triple = text.substr(i, 3u) == std::string(3u, quote);
                        i += triple ? 3u : 1u;
                        for (;;)
                        {
                            if (i >= text.size())
                                return false;
                            if (text[i] == '\\')
                                i += 2u;
                            else if (triple ? text.substr(i, 3u) == std::string(3u, quote) : text[i] == quote)
                            {
                                i += triple ? 3u : 1u;
                                break;
                            }
                            else
                                ++i;
                        }
                    }
                    else if (word != "True" && word != "False" && word != "None")
                        return false;
                    item = true;
                }
                else
                    return false;
            }
            if (item)
                ++items;
            return depth == 0 && items >= 3u;
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
                "DeprecationWarning", "RuntimeWarning", "bytes", "bytearray", "UnicodeEncodeError", "UnicodeDecodeError", "UnicodeTranslateError",
                "BlockingIOError", "ChildProcessError", "ConnectionError", "BrokenPipeError", "ConnectionAbortedError", "ConnectionRefusedError",
                "ConnectionResetError", "FileExistsError", "InterruptedError", "IsADirectoryError", "NotADirectoryError", "PermissionError",
                "ProcessLookupError", "TimeoutError", "StopAsyncIteration", "SyntaxError", "IndentationError", "TabError", "ReferenceError",
                "PendingDeprecationWarning", "SyntaxWarning", "FutureWarning", "ImportWarning", "UnicodeWarning", "BytesWarning",
                "ResourceWarning", "EncodingWarning", "open", "delattr", "vars", "ascii"
            };
            return sNames;
        }
    }

    void compiler::py_complete_signature(i_function_signature& aSignature, neolib::i_string_view const& aSource)
    {
        // (a function can follow top-level statements: an if statement that they end with is complete)
        if (generator().indented_count() != 0u)
            generator().finalize_suspended(0u);
        // a parameter without an annotation is an object; so is the result of a function without one
        if (aSignature.parameters().has_value())
            for (auto& parameter : aSignature.parameters().value())
                if (parameter.parameter_type() == type::UNKNOWN)
                    parameter.set_parameter_type(type::Object);
        auto const text = aSource.to_std_string_view();
        auto const close = text.rfind(')');
        if (close == std::string_view::npos || text.find("->", close) == std::string_view::npos)
            aSignature.set_return_type(type::Object);
        // an int is an integer of any size (except for the VM's functions (__neos_f), which take and return 64-bit integers); with 
        // 64-bit ints (see py_big_integers) only a stub's (a runtime plugin's function, whose body is "...") are
        bool stub = false;
        {
            auto const all = generator().current_fragment().source().to_std_string_view();
            if (text.data() >= all.data() && text.data() + text.size() <= all.data() + all.size())
            {
                auto rest = all.substr(static_cast<std::size_t>(text.data() + text.size() - all.data()));
                while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t' || rest.front() == ':'))
                    rest.remove_prefix(1u);
                stub = rest.starts_with("...");
            }
        }
        if (!aSignature.name().to_std_string().starts_with("__neos_") && (py_big_integers() || stub))
        {
            if (aSignature.parameters().has_value())
                for (auto& parameter : aSignature.parameters().value())
                    if (parameter.parameter_type() == type::I64)
                        parameter.set_parameter_type(type::Integer);
            if (aSignature.return_type() == type::I64)
                aSignature.set_return_type(type::Integer);
        }
        // the parameters' kinds (*args, **kwargs, keyword-only) and default values
        auto const name = aSignature.name().to_std_string();
        if (auto const& path = generator().current_fragment().source_file_path(); path != std::nullopt &&
            std::filesystem::path{ path.value().to_std_string() }.parent_path().parent_path().filename() == "packages")
            iPyPackageFunctions.insert(name);
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
                    if (item == "/")
                    {
                        // (the parameters before it are positional-only)
                        for (auto& p : parameters)
                            p.positionalOnly = true;
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
            if (operands().empty())
                throw std::runtime_error("default value expected");
            values.insert(values.begin(), operands().back());
            operands().pop_back();
        }
        iPyParameters[name] = parameters;
        if (!defaults.empty() && !declaration_pass())
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
                generator().function_global(neolib::string{ name + "." + parameter.name }); // (a global: calls use it)
                auto value = materialize(values[i]);
                if (t == type::Object)
                {
                    if (value.valueType != type::Object)
                        coerce(value, type::Object);
                    auto const holder = held_by(value, py_function().pendingObjects);
                    append(statement, value);
                    if (holder)
                    {
                        py_function().pendingObjects.erase(std::find(py_function().pendingObjects.begin(), py_function().pendingObjects.end(), *holder));
                        put_local(statement, opcode::GlobalSet, global.index());
                        put_i32(statement, 0);
                        put_local(statement, opcode::LocalSet, *holder);
                        py_function().freeObjects.push_back(*holder);
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
                    if (t == type::Integer)
                    {
                        // (the global holds a reference of its own)
                        auto const scratch = py_scratch(ScratchBind, type::Integer);
                        put_local(statement, opcode::LocalSet, scratch);
                        append(statement, py_retain_integer(scratch));
                        put_local(statement, opcode::LocalGet, scratch);
                    }
                }
                put_local(statement, opcode::GlobalSet, global.index());
            }
            append(statement, py_flush());
            // (a lambda's function is defined at the end of the module: its (constant) default values are evaluated first)
            // (as are a class's methods (_cN_class_method))
            auto const shown = unprefixed(name);
            bool const lambda = shown.size() > 3u && shown.starts_with("_l") && shown.ends_with("_") &&
                std::all_of(shown.begin() + 2, shown.end() - 1, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
            bool const method = shown.size() > 3u && shown.starts_with("_c") && std::isdigit(static_cast<unsigned char>(shown[2])) &&
                shown.find('_', 2u) != std::string::npos && std::all_of(shown.begin() + 2, shown.begin() + static_cast<std::ptrdiff_t>(shown.find('_', 2u)),
                    [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
            if ((lambda || method) && in_function())
                operands().insert(operand_base(), operand_type{ statement });
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
                if ((parameters[p].kind == py_parameter::Positional || parameters[p].kind == py_parameter::KeywordOnly) && parameters[p].name == *aKeywords[i] &&
                    !parameters[p].positionalOnly)
                    slot = p;
            if (!slot && !varKeywords)
                for (std::size_t p = 0u; p < parameters.size(); ++p)
                    if (parameters[p].positionalOnly && parameters[p].name == *aKeywords[i])
                        throw std::runtime_error(aFunction + "() got some positional-only arguments passed as keyword arguments: '" + *aKeywords[i] + "'");
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
                auto* const global = find_symbol(generator().global_scope(), neolib::string_view{ neolib::string{ aFunction + "." + parameter.name } }, true);
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

        void append_utf8(std::string& aText, std::uint32_t aCodePoint)
        {
            if (aCodePoint < 0x80u)
                aText += static_cast<char>(aCodePoint);
            else if (aCodePoint < 0x800u)
            {
                aText += static_cast<char>(0xC0u | (aCodePoint >> 6));
                aText += static_cast<char>(0x80u | (aCodePoint & 0x3Fu));
            }
            else if (aCodePoint < 0x10000u)
            {
                aText += static_cast<char>(0xE0u | (aCodePoint >> 12));
                aText += static_cast<char>(0x80u | ((aCodePoint >> 6) & 0x3Fu));
                aText += static_cast<char>(0x80u | (aCodePoint & 0x3Fu));
            }
            else
            {
                aText += static_cast<char>(0xF0u | (aCodePoint >> 18));
                aText += static_cast<char>(0x80u | ((aCodePoint >> 12) & 0x3Fu));
                aText += static_cast<char>(0x80u | ((aCodePoint >> 6) & 0x3Fu));
                aText += static_cast<char>(0x80u | (aCodePoint & 0x3Fu));
            }
        }

        // (a \N{name} escape at aText[aAt] (its backslash): the character's code point, and the position of its '}')
        std::uint32_t named_escape(std::string const& aText, std::size_t aAt, std::size_t& aEnd)
        {
            auto const close = aAt + 2u < aText.size() && aText[aAt + 2u] == '{' ? aText.find('}', aAt + 3u) : std::string::npos;
            if (close == std::string::npos || close == aAt + 3u)
                throw std::runtime_error("(unicode error) 'unicodeescape' codec can't decode bytes in position " + std::to_string(aAt) + "-" +
                    std::to_string(close == std::string::npos ? aAt + 1u : close) + ": malformed \\N character escape");
            auto const character = neos::language::python::unicode_names::lookup(std::string_view{ aText }.substr(aAt + 3u, close - aAt - 3u), true, false);
            if (!character || character->size() != 1u)
                throw std::runtime_error("(unicode error) 'unicodeescape' codec can't decode bytes in position " + std::to_string(aAt) + "-" +
                    std::to_string(close) + ": unknown Unicode character name");
            aEnd = close;
            return static_cast<std::uint32_t>((*character)[0]);
        }

        // (a string literal's content without a prefix, as Python decodes it (a check for NUL characters, which a string constant cannot have))
        bool has_null_character(std::string const& aText)
        {
            auto const hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            for (std::size_t i = 0u; i < aText.size(); ++i)
            {
                if (aText[i] == '\0')
                    return true;
                if (aText[i] != '\\' || i + 1u == aText.size())
                    continue;
                char const e = aText[++i];
                if (e >= '0' && e <= '7')
                {
                    unsigned value = static_cast<unsigned>(e - '0');
                    for (int k = 0; k < 2 && i + 1u < aText.size() && aText[i + 1u] >= '0' && aText[i + 1u] <= '7'; ++k)
                        value = value * 8u + static_cast<unsigned>(aText[++i] - '0');
                    if (value == 0u)
                        return true;
                }
                else if (e == 'x' || e == 'u' || e == 'U')
                {
                    std::size_t const digits = e == 'x' ? 2u : e == 'u' ? 4u : 8u;
                    unsigned long value = 0u;
                    std::size_t k = 0u;
                    for (; k < digits && i + 1u + k < aText.size() && hex(aText[i + 1u + k]) >= 0; ++k)
                        value = value * 16u + static_cast<unsigned long>(hex(aText[i + 1u + k]));
                    if (k == digits && value == 0u)
                        return true;
                    i += k;
                }
                else if (e == 'N')
                {
                    std::size_t end = i;
                    if (named_escape(aText, i - 1u, end) == 0u)
                        return true;
                    i = end;
                }
            }
            return false;
        }

        std::string decoded_literal(std::string const& aText);

        // (a string literal without a prefix: a raw string's backslashes are escaped; an unknown escape is a backslash; \N{name} is the 
        // character's \U escape; a literal with a NUL character is the expression of its UTF-8 (a string constant cannot have NULs))
        std::string literal_part(std::string const& aPrefix, std::string const& aQuote, std::string const& aText)
        {
            bool const raw = aPrefix.find_first_of("rR") != std::string::npos;
            if (!raw && has_null_character(aText))
            {
                static char const digits[] = "0123456789abcdef";
                std::string hex;
                for (auto b : decoded_literal(aText))
                {
                    hex += digits[static_cast<unsigned char>(b) >> 4];
                    hex += digits[static_cast<unsigned char>(b) & 15u];
                }
                return "__bytes('" + hex + "').decode()";
            }
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
                if (!raw && next == 'N')
                {
                    std::size_t end = i;
                    auto const cp = named_escape(aText, i - 1u, end);
                    char escape[16];
                    std::snprintf(escape, sizeof(escape), "\\U%08X", static_cast<unsigned>(cp));
                    result += escape;
                    i = end;
                    continue;
                }
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

        // (a string literal's content (without a prefix) decoded as Python decodes it: its UTF-8)
        std::string decoded_literal(std::string const& aText)
        {
            auto const hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            std::string result;
            for (std::size_t i = 0u; i < aText.size(); ++i)
            {
                char const c = aText[i];
                if (c == '\r' && i + 1u < aText.size() && aText[i + 1u] == '\n')
                    continue;
                if (c != '\\' || i + 1u == aText.size())
                {
                    result += c;
                    continue;
                }
                char const e = aText[++i];
                switch (e)
                {
                case '\n': break;
                case '\r': if (i + 1u < aText.size() && aText[i + 1u] == '\n') ++i; break;
                case '\\': result += '\\'; break;
                case '\'': result += '\''; break;
                case '"': result += '"'; break;
                case 'a': result += '\a'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'v': result += '\v'; break;
                case 'x': case 'u': case 'U':
                    {
                        std::size_t const digits = e == 'x' ? 2u : e == 'u' ? 4u : 8u;
                        std::uint32_t value = 0u;
                        for (std::size_t k = 0u; k < digits; ++k)
                        {
                            if (i + 1u >= aText.size() || hex(aText[i + 1u]) < 0)
                                throw std::runtime_error("(unicode error) 'unicodeescape' codec can't decode bytes: truncated escape");
                            value = value * 16u + static_cast<std::uint32_t>(hex(aText[++i]));
                        }
                        append_utf8(result, value);
                    }
                    break;
                case 'N':
                    {
                        std::size_t end = i;
                        append_utf8(result, named_escape(aText, i - 1u, end));
                        i = end;
                    }
                    break;
                default:
                    if (e >= '0' && e <= '7')
                    {
                        std::uint32_t value = static_cast<std::uint32_t>(e - '0');
                        for (int k = 0; k < 2 && i + 1u < aText.size() && aText[i + 1u] >= '0' && aText[i + 1u] <= '7'; ++k)
                            value = value * 8u + static_cast<std::uint32_t>(aText[++i] - '0');
                        append_utf8(result, value);
                    }
                    else
                    {
                        result += '\\';
                        result += e;
                    }
                    break;
                }
            }
            return result;
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
                std::size_t debugEnd = j;
                if (aContent[j] == '=')
                {
                    // (the expression's text, with the spaces after '=', is the field's prefix)
                    debug = true;
                    ++j;
                    while (j < n && (aContent[j] == ' ' || aContent[j] == '\t'))
                        ++j;
                    debugEnd = j;
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
                    parts.push_back(literal_part(aPrefix, aQuote, aContent.substr(i + 1u, debugEnd - i - 1u)));
                    if (conversion == 0 && spec.empty())
                        conversion = 'r';
                }
                auto const specExpression = spec.empty() ? std::string{ "\"\"" } : fstring_expression(spec, aPrefix, aQuote);
                // (its expression's own literals (e.g. f-strings))
                bool nested = false;
                auto const rewritten = rewrite_fstrings(expression, nested);
                parts.push_back("__fstring((" + rewritten + "), " + std::to_string(static_cast<int>(conversion)) + ", " + specExpression + ")");
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

        // (the bytes of a bytes literal (its quote at aStart, its end at aEnd))
        std::string bytes_literal(std::string const& aText, std::size_t aStart, std::size_t aEnd, std::string const& aPrefix)
        {
            char const q = aText[aStart];
            bool const triple = aStart + 2u < aText.size() && aText[aStart + 1u] == q && aText[aStart + 2u] == q;
            std::size_t const quote = triple ? 3u : 1u;
            if (aEnd < aStart + 2u * quote || aText.compare(aEnd - quote, quote, std::string(quote, q)) != 0)
                throw std::runtime_error("unterminated bytes literal");
            auto const content = std::string_view{ aText }.substr(aStart + quote, aEnd - aStart - 2u * quote);
            bool const raw = aPrefix.find_first_of("rR") != std::string::npos;
            std::string result;
            auto const hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            for (std::size_t i = 0u; i < content.size(); ++i)
            {
                char const c = content[i];
                if (static_cast<unsigned char>(c) >= 0x80u)
                    throw std::runtime_error("bytes can only contain ASCII literal characters");
                if (c == '\r' && i + 1u < content.size() && content[i + 1u] == '\n')
                    continue;
                if (c != '\\' || i + 1u == content.size())
                {
                    result += c;
                    continue;
                }
                char const e = content[i + 1u];
                if (raw)
                {
                    result += c;
                    result += e;
                    ++i;
                    continue;
                }
                ++i;
                switch (e)
                {
                case '\n': break;
                case '\r': if (i + 1u < content.size() && content[i + 1u] == '\n') ++i; break;
                case '\\': result += '\\'; break;
                case '\'': result += '\''; break;
                case '"': result += '"'; break;
                case 'a': result += '\a'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'v': result += '\v'; break;
                case 'x':
                    {
                        if (i + 2u >= content.size() || hex(content[i + 1u]) < 0 || hex(content[i + 2u]) < 0)
                            throw std::runtime_error("(value error) invalid \\x escape at position " + std::to_string(i - 1u));
                        result += static_cast<char>(hex(content[i + 1u]) * 16 + hex(content[i + 2u]));
                        i += 2u;
                    }
                    break;
                default:
                    if (e >= '0' && e <= '7')
                    {
                        int value = e - '0';
                        for (int k = 0; k < 2 && i + 1u < content.size() && content[i + 1u] >= '0' && content[i + 1u] <= '7'; ++k)
                            value = value * 8 + (content[++i] - '0');
                        result += static_cast<char>(value & 0xFF);
                    }
                    else
                    {
                        if (static_cast<unsigned char>(e) >= 0x80u)
                            throw std::runtime_error("bytes can only contain ASCII literal characters");
                        result += '\\';
                        result += e;
                    }
                    break;
                }
            }
            return result;
        }

        std::string rewrite_fstrings(std::string const& aText, bool& aChanged)
        {
            std::string result;
            std::size_t i = 0u;
            auto const n = aText.size();
            int depth = 0;  // (brackets: line breaks within them are whitespace)
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
                    auto end = string_end(aText, i);
                    if (isPrefix && prefix.find_first_of("bB") != std::string::npos)
                    {
                        // (a bytes literal (and the bytes literals adjacent to it) is "__bytes('hexadecimal digits of its bytes')")
                        std::string data = bytes_literal(aText, i, end, prefix);
                        for (;;)
                        {
                            auto j = end;
                            while (j < n && (aText[j] == ' ' || aText[j] == '\t' || (aText[j] == '\\' && j + 1u < n && (aText[j + 1u] == '\n' || aText[j + 1u] == '\r'))))
                                j += aText[j] == '\\' ? (aText[j + 1u] == '\r' && j + 2u < n && aText[j + 2u] == '\n' ? 3u : 2u) : 1u;
                            auto k = j;
                            while (k < n && std::isalpha(static_cast<unsigned char>(aText[k])) && k - j < 2u)
                                ++k;
                            if (k >= n || (aText[k] != '\'' && aText[k] != '\"'))
                                break;
                            auto const nextPrefix = aText.substr(j, k - j);
                            if (nextPrefix.find_first_not_of("fFrRbBuU") != std::string::npos)
                                break;
                            if (nextPrefix.find_first_of("bB") == std::string::npos)
                                throw std::runtime_error("cannot mix bytes and nonbytes literals");
                            auto const nextEnd = string_end(aText, k);
                            data += bytes_literal(aText, k, nextEnd, nextPrefix);
                            end = nextEnd;
                        }
                        static char const digits[] = "0123456789abcdef";
                        std::string hex;
                        for (auto b : data)
                        {
                            hex += digits[static_cast<unsigned char>(b) >> 4];
                            hex += digits[static_cast<unsigned char>(b) & 15u];
                        }
                        result.erase(p);
                        result += "__bytes('" + hex + "')";
                        i = end;
                        aChanged = true;
                        continue;
                    }
                    // (adjacent string literals of which one is an f-string: their concatenation)
                    {
                        struct literal { std::string prefix; std::size_t quote; std::size_t end; };
                        std::vector<literal> run{ literal{ isPrefix ? prefix : std::string{}, i, end } };
                        std::size_t lineBreaks = 0u;
                        auto k = end;
                        for (;;)
                        {
                            auto j = k;
                            std::size_t breaks = 0u;
                            while (j < n)
                            {
                                if (aText[j] == ' ' || aText[j] == '\t' || aText[j] == '\r')
                                    ++j;
                                else if (aText[j] == '\\' && j + 1u < n && (aText[j + 1u] == '\n' || aText[j + 1u] == '\r'))
                                {
                                    j += aText[j + 1u] == '\r' && j + 2u < n && aText[j + 2u] == '\n' ? 3u : 2u;
                                    ++breaks;
                                }
                                else if (depth > 0 && aText[j] == '\n')
                                {
                                    ++j;
                                    ++breaks;
                                }
                                else if (depth > 0 && aText[j] == '#')
                                {
                                    while (j < n && aText[j] != '\n')
                                        ++j;
                                }
                                else
                                    break;
                            }
                            auto q = j;
                            while (q < n && std::isalpha(static_cast<unsigned char>(aText[q])) && q - j < 2u)
                                ++q;
                            if (q >= n || (aText[q] != '\'' && aText[q] != '\"'))
                                break;
                            auto const nextPrefix = aText.substr(j, q - j);
                            if (nextPrefix.find_first_not_of("fFrRbBuU") != std::string::npos)
                                break;
                            if (nextPrefix.find_first_of("bB") != std::string::npos)
                                break;
                            k = string_end(aText, q);
                            run.push_back(literal{ nextPrefix, q, k });
                            lineBreaks += breaks;
                        }
                        bool const anyF = std::any_of(run.begin(), run.end(), [](literal const& l) { return l.prefix.find_first_of("fF") != std::string::npos; });
                        // (a literal with a NUL character is an expression: adjacent literals are then joined as f-strings' are)
                        bool const anyNull = std::any_of(run.begin(), run.end(), [&](literal const& l)
                        {
                            if (l.prefix.find_first_of("rR") != std::string::npos)
                                return false;
                            char const q = aText[l.quote];
                            bool const triple = l.quote + 2u < n && aText[l.quote + 1u] == q && aText[l.quote + 2u] == q;
                            std::size_t const quoteSize = triple ? 3u : 1u;
                            return l.end >= l.quote + 2u * quoteSize && has_null_character(aText.substr(l.quote + quoteSize, l.end - l.quote - 2u * quoteSize));
                        });
                        if (run.size() > 1u && (anyF || anyNull))
                        {
                            std::string joined;
                            for (auto const& l : run)
                            {
                                char const q = aText[l.quote];
                                bool const triple = l.quote + 2u < n && aText[l.quote + 1u] == q && aText[l.quote + 2u] == q;
                                std::string const quote(triple ? 3u : 1u, q);
                                if (l.end < l.quote + 2u * quote.size() || aText.compare(l.end - quote.size(), quote.size(), quote) != 0)
                                    throw std::runtime_error("unterminated string literal");
                                auto const content = aText.substr(l.quote + quote.size(), l.end - l.quote - 2u * quote.size());
                                std::string otherPrefix;
                                for (auto x : l.prefix)
                                    if (x != 'f' && x != 'F')
                                        otherPrefix += x;
                                std::string piece;
                                if (l.prefix.find_first_of("fF") != std::string::npos)
                                {
                                    bool nested = false;
                                    piece = fstring_expression(content, otherPrefix, quote);
                                    (void)nested;
                                }
                                else
                                    piece = literal_part(otherPrefix, quote, content);
                                joined += (joined.empty() ? "" : " + ") + piece;
                            }
                            result.erase(p);
                            result += "(" + joined + ")" + std::string(lineBreaks, '\n');
                            i = k;
                            aChanged = true;
                            continue;
                        }
                    }
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
                if (c == '(' || c == '[' || c == '{')
                    ++depth;
                else if ((c == ')' || c == ']' || c == '}') && depth > 0)
                    --depth;
                result += c;
                ++i;
            }
            return result;
        }
    }

    void compiler::py_join_lines(i_source_fragment& aFragment)
    {
        // lines are joined within brackets: line breaks (and comments) there are whitespace (replaced by spaces, so that 
        // source positions are unchanged)
        auto const source = aFragment.source().to_std_string_view();
        // (a source that preparse has made (a later compilation pass): already rewritten)
        if (iPyPreparsed.contains(std::string{ source }))
            return;
        bool changed = false;
        std::string original{ source };
        // (eval: the value of the expression is its result)
        if (iPyMode == "eval" && !aFragment.imported())
        {
            auto const first = original.find_first_not_of(" \t");
            original = "__exec_result((" + (first == std::string::npos ? std::string{} : original.substr(first)) + "\n))\n";
            changed = true;
        }
        std::string text = rewrite_fstrings(original, changed);
        std::string const unjoined = text;
        int depth = 0;
        // (the open brackets (and their positions): a bracket that is never closed, or a closing bracket that does not match,
        // is a syntax error (CPython's messages))
        std::vector<std::size_t> open;
        // (the line of a position: in the source (line breaks within brackets become spaces in the text))
        auto const line_of = [&](std::size_t aPosition) { return 1 + std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(std::min(aPosition, source.size())), '\n'); };
        auto const syntax_error = [&](std::size_t aPosition, std::string const& aMessage)
        {
            generator().throw_error(aFragment.source().begin() + static_cast<std::ptrdiff_t>(aPosition), neolib::string{ aMessage }, "syntax error"_s);
        };
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
                for (; i < text.size() && text[i] != '\n' && text[i] != '\r'; ++i)
                    if (depth > 0)
                    {
                        text[i] = ' ';
                        changed = true;
                    }
                --i;
                continue;
            }
            if (c == '(' || c == '[' || c == '{')
            {
                ++depth;
                open.push_back(i);
            }
            else if (c == ')' || c == ']' || c == '}')
            {
                if (open.empty())
                    syntax_error(i, std::string{ "unmatched '" } + c + "'");
                char const opening = text[open.back()];
                if ((opening == '(' ? ')' : opening == '[' ? ']' : '}') != c)
                    syntax_error(i, std::string{ "closing parenthesis '" } + c + "' does not match opening parenthesis '" + opening + "'" +
                        (line_of(open.back()) != line_of(i) ? " on line " + std::to_string(line_of(open.back())) : std::string{}));
                open.pop_back();
                --depth;
            }
            else if (depth > 0 && (c == '\n' || c == '\r'))
            {
                text[i] = ' ';
                changed = true;
            }
            else if (depth > 0 && c == '\\' && i + 1u < text.size() && (text[i + 1u] == '\n' || text[i + 1u] == '\r'))
            {
                // (a line continuation within brackets: whitespace)
                text[i] = ' ';
                changed = true;
            }
        }
        if (!open.empty())
            syntax_error(open.back(), std::string{ "'" } + text[open.back()] + "' was never closed");
        // (the line of the source as written (1-based; 0: none) that each line of the text is (see source_line): the rewrites below keep 
        // the lines of the text (they add lines at its end) except where they say; lines joined within brackets are the first's)
        std::vector<std::uint32_t> lines{ 1u };
        for (std::size_t i = 0u, line = 1u; i < unjoined.size() && i < text.size(); ++i)
            if (unjoined[i] == '\n' && (++line, text[i] == '\n'))
                lines.push_back(static_cast<std::uint32_t>(line));
        auto const track_lines = [&]()
        {
            lines.resize(static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1u, 0u);
        };
        // import statements of several modules ("import a, b as c": "import a; import b as c") and parenthesized from imports
        {
            std::size_t lineStart = 0u;
            std::string rewritten;
            bool importsChanged = false;
            while (lineStart <= text.size())
            {
                auto lineEnd = text.find('\n', lineStart);
                if (lineEnd == std::string::npos)
                    lineEnd = text.size();
                std::string line = text.substr(lineStart, lineEnd - lineStart);
                auto const first = line.find_first_not_of(" \t");
                if (first != std::string::npos && (line.compare(first, 7u, "import ") == 0 || line.compare(first, 5u, "from ") == 0) &&
                    line.find_first_of("'\"#;") == std::string::npos)
                {
                    bool const fromImport = line.compare(first, 5u, "from ") == 0;
                    auto const importAt = fromImport ? line.find(" import ", first) : first;
                    if (importAt != std::string::npos)
                    {
                        auto const namesAt = importAt + (fromImport ? 8u : 7u);
                        auto names = line.substr(namesAt);
                        auto const trimmedEnd = names.find_last_not_of(" \t\r");
                        std::string const ending = trimmedEnd == std::string::npos ? std::string{} : names.substr(trimmedEnd + 1u);
                        names = trimmedEnd == std::string::npos ? std::string{} : names.substr(0u, trimmedEnd + 1u);
                        if (fromImport)
                        {
                            auto const open = names.find_first_not_of(" \t");
                            if (open != std::string::npos && names[open] == '(' && names.back() == ')')
                            {
                                names = names.substr(open + 1u, names.size() - open - 2u);
                                auto const last = names.find_last_not_of(" \t");
                                if (last != std::string::npos && names[last] == ',')
                                    names.erase(last);
                                line = line.substr(0u, namesAt) + names + ending;
                                importsChanged = true;
                            }
                        }
                        else if (names.find(',') != std::string::npos)
                        {
                            std::string replaced;
                            for (char c : names)
                                replaced += c == ',' ? std::string{ "; import" } : std::string(1u, c);
                            line = line.substr(0u, namesAt) + replaced + ending;
                            importsChanged = true;
                        }
                    }
                }
                rewritten += line;
                if (lineEnd < text.size())
                    rewritten += '\n';
                lineStart = lineEnd + 1u;
            }
            if (importsChanged)
            {
                text = rewritten;
                changed = true;
            }
        }
        // (a program (its fragments other than packages') that calls exec, eval or compile (a name that is not an attribute, e.g. not 
        // re.compile) compiles source while it runs: an image of it needs the compiler)
        if (auto const& path = aFragment.source_file_path(); !py_exec_mode() && (path == std::nullopt || 
            std::filesystem::path{ path.value().to_std_string() }.parent_path().parent_path().filename() != "packages"))
        {
            static std::regex const sCompiles{ "(^|[^.\\w])(exec|eval|compile)\\b" };
            if (std::regex_search(text, sCompiles))
                iPyCompilesAtRunTime = true;
        }
        // the body "..." of a program's def or class (not of a package's def (e.g. builtins.py's), which is a stub: the runtime plugin's 
        // function) is an empty statement ("pass")
        if (auto const& path = aFragment.source_file_path(); path == std::nullopt || std::filesystem::path{ path.value().to_std_string() }.parent_path().parent_path().filename() != "packages")
        {
            bool stubsChanged = false;
            std::size_t lineStart = 0u;
            auto const content_of = [&](std::size_t aStart, std::size_t aEnd) -> std::pair<std::size_t, std::size_t>
            {
                // (the line's statement: without its indentation, a comment or trailing whitespace)
                auto first = text.find_first_not_of(" \t", aStart);
                if (first == std::string::npos || first >= aEnd)
                    return { aEnd, aEnd };
                auto last = aEnd;
                if (auto const hash = text.find('#', first); hash != std::string::npos && hash < aEnd)
                    last = hash;
                while (last > first && (text[last - 1u] == ' ' || text[last - 1u] == '\t' || text[last - 1u] == '\r'))
                    --last;
                return { first, last };
            };
            auto const header = [&](std::size_t aFirst, std::size_t aLast)
            {
                std::string_view const statement{ text.data() + aFirst, aLast - aFirst };
                return statement.starts_with("def ") || statement.starts_with("async def ") || statement.starts_with("class ");
            };
            while (lineStart < text.size())
            {
                auto lineEnd = text.find('\n', lineStart);
                if (lineEnd == std::string::npos)
                    lineEnd = text.size();
                auto const [first, last] = content_of(lineStart, lineEnd);
                if (first < last && header(first, last))
                {
                    std::string_view const statement{ text.data() + first, last - first };
                    if (statement.ends_with(": ...") || statement.ends_with(":..."))
                    {
                        // (def f(): ...)
                        text.replace(last - 3u, 3u, "pass");
                        stubsChanged = true;
                        lineEnd = text.find('\n', lineStart);
                        if (lineEnd == std::string::npos)
                            lineEnd = text.size();
                    }
                    else if (statement.ends_with(":") && lineEnd < text.size())
                    {
                        // (def f():\n    ...: the next line, if it is all of the body)
                        auto next = lineEnd + 1u;
                        while (next < text.size())
                        {
                            auto const e = text.find('\n', next);
                            auto const [f2, l2] = content_of(next, e == std::string::npos ? text.size() : e);
                            if (f2 < l2)
                                break;
                            next = e == std::string::npos ? text.size() : e + 1u;
                        }
                        auto const nextEnd = std::min(text.find('\n', next), text.size());
                        auto const [f2, l2] = content_of(next, nextEnd);
                        if (f2 < l2 && std::string_view{ text.data() + f2, l2 - f2 } == "..." && f2 - next > first - lineStart)
                        {
                            // (the line after it: less indented (the body has ended))
                            auto after = nextEnd + 1u;
                            bool sole = true;
                            while (after < text.size())
                            {
                                auto const e = text.find('\n', after);
                                auto const [f3, l3] = content_of(after, e == std::string::npos ? text.size() : e);
                                if (f3 < l3)
                                {
                                    sole = f3 - after <= first - lineStart;
                                    break;
                                }
                                after = e == std::string::npos ? text.size() : e + 1u;
                            }
                            if (sole)
                            {
                                text.replace(f2, 3u, "pass");
                                stubsChanged = true;
                            }
                        }
                    }
                }
                lineStart = (lineEnd == std::string::npos ? text.size() : text.find('\n', lineStart)) ;
                lineStart = lineStart == std::string::npos ? text.size() : lineStart + 1u;
            }
            changed = changed || stubsChanged;
        }
        // the modules of source files that its import statements import
        py_find_imports(text);
        // (the annotations of the module's functions)
        std::map<std::string, std::vector<std::pair<std::string, std::string>>> annotations;
        // compound statements compiled as others (see statements.cpp)
        {
            bool statementsChanged = false;
            text = rewrite_statements(text, statementsChanged);
            changed = changed || statementsChanged;
        }
        // class statements (see statements.cpp)
        {
            bool classesChanged = false;
            text = rewrite_classes(text, classesChanged);
            changed = changed || classesChanged;
        }
        // comprehensions' variables (see statements.cpp)
        {
            bool comprehensionsChanged = false;
            text = rewrite_comprehensions(text, comprehensionsChanged);
            changed = changed || comprehensionsChanged;
        }
        // nested functions (closures) and decorated functions (see closures.cpp)
        {
            bool nestedChanged = false;
            track_lines();
            std::vector<std::size_t> nestedLines;
            text = rewrite_nested_functions(text, nestedChanged, &annotations, &nestedLines);
            if (nestedChanged)
            {
                std::vector<std::uint32_t> rewritten;
                for (auto const l : nestedLines)
                    rewritten.push_back(l < lines.size() ? lines[l] : 0u);
                lines = std::move(rewritten);
            }
            changed = changed || nestedChanged;
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
                // (a comprehension's for)
                else if (level == 0 && c == 'f' && text.compare(end, 3u, "for") == 0 && (end + 3u == text.size() || !name_character(text[end + 3u])) &&
                    end > 0u && !name_character(text[end - 1u]))
                    break;
                else if (name_character(c))
                    while (end + 1u < text.size() && name_character(text[end + 1u]))
                        ++end;
            }
            auto const parameters = text.substr(start + 6u, colon - start - 6u);
            auto const body = text.substr(colon + 1u, end - colon - 1u);
            auto const name = "_l" + std::to_string(++lambdas) + "_";
            if (name.size() > end - start)
                break;
            text.replace(start, end - start, name + std::string(end - start - name.size(), ' '));
            // (its function's lines: its own)
            track_lines();
            auto const lambdaLine = lines[static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(start), '\n'))];
            if (!text.empty() && text.back() != '\n')
                text += '\n';
            else
                lines.pop_back();
            text += "def " + name + "(" + std::string{ trimmed(parameters) } + "):\n    return " + std::string{ trimmed(body) } + "\n";
            lines.insert(lines.end(), { lambdaLine, lambdaLine, 0u });
            changed = true;
        }
        // the assignments of classes' bodies: through their namespaces (see statements.cpp)
        {
            bool namespacesChanged = false;
            text = rewrite_class_namespaces(text, namespacesChanged);
            changed = changed || namespacesChanged;
        }
        // (exec: the functions that its top level defines (its namespace's))
        if (py_exec_mode() && !aFragment.imported())
        {
            iPyExecDefs.clear();
            for (std::size_t lineStart = 0u; lineStart < text.size();)
            {
                auto const lineEnd = std::min(text.find('\n', lineStart), text.size());
                if (text.compare(lineStart, 4u, "def ") == 0)
                {
                    auto nameEnd = lineStart + 4u;
                    while (nameEnd < lineEnd && name_character(text[nameEnd]))
                        ++nameEnd;
                    std::string const name = text.substr(lineStart + 4u, nameEnd - lineStart - 4u);
                    bool hoisted = name.size() > 3u && name[0] == '_' && std::string_view{ "ndgablemcq" }.find(name[1]) != std::string_view::npos &&
                        std::isdigit(static_cast<unsigned char>(name[2]));
                    if (!name.empty() && !hoisted)
                        iPyExecDefs.push_back(name);
                }
                lineStart = lineEnd + 1u;
            }
        }
        // (a module that imports a runtime module's names (from m import *: they are bound in its namespace (see python::import_star)))
        auto const importsStar = [&]()
        {
            static std::regex const sStar{ "(^|\n)[ \t]*from[ \t]+([A-Za-z_][\\w.]*)[ \t]+import[ \t]+\\*" };
            for (auto i = std::sregex_iterator(text.begin(), text.end(), sStar); i != std::sregex_iterator(); ++i)
                if (!py_module_of((*i)[2].str()))
                    return true;
            return false;
        };
        // (a program that uses its namespace (globals()): its names are read by its getter (as an imported module's))
        if (!py_exec_mode() && !aFragment.imported())
        {
            // (or that reads its module object (sys.modules['__main__'], import __main__))
            static std::regex const sIntrospection{ "\\b(globals|vars|dir|exec|eval)\\b|\\bsys\\.modules\\b|\\bimport\\s+__main__\\b" };
            // (its functions' annotations are evaluated in its namespace; its classes' string annotations are resolved in it (e.g. by
            // dataclasses: "x: 'ClassVar[int]'", or any under "from __future__ import annotations"))
            static std::regex const sStringAnnotation{ "__annotations__\\['[^']*'\\] = \\(['\"]" };
            if (std::regex_search(text, sIntrospection) || !annotations.empty() || std::regex_search(text, sStringAnnotation) || importsStar())
            {
                iPyIntrospective.insert("__main__");
                std::vector<std::string> names;
                std::optional<std::vector<std::string>> all;
                text = rewrite_module_names(text, "", "_N0_getattr", names, all, builtin_names());
                changed = true;
            }
        }
        // an imported module's names are the program's variables _MN_name (see closures.cpp)
        if (auto const module = py_current_module())
        {
            auto& m = iPyModules[*module];
            m.names.clear();
            m.all = std::nullopt;
            if (std::regex_search(text, std::regex{ "\\b(globals|vars|dir|exec|eval)\\b" }) || importsStar())
                iPyIntrospective.insert(m.name);
            text = rewrite_module_names(text, m.prefix(), m.getter(), m.names, m.all, builtin_names());
            changed = true;
        }
        {
            auto const prefix = py_current_module() ? iPyModules[*py_current_module()].prefix() : std::string{};
            for (auto& a : annotations)
                iPyAnnotations[prefix + a.first] = std::move(a.second);
        }
        // the functions' docstrings (a string literal that is the first statement of a def's body)
        {
            std::size_t lineStart = 0u;
            while (lineStart < text.size())
            {
                auto lineEnd = text.find('\n', lineStart);
                if (lineEnd == std::string::npos)
                    lineEnd = text.size();
                std::string_view const line{ text.data() + lineStart, lineEnd - lineStart };
                auto const first = line.find_first_not_of(" \t");
                if (first != std::string_view::npos && line.substr(first).starts_with("def "))
                {
                    auto nameStart = line.find_first_not_of(" \t", first + 4u);
                    auto nameEnd = nameStart;
                    while (nameEnd < line.size() && name_character(line[nameEnd]))
                        ++nameEnd;
                    std::string const name{ nameStart == std::string_view::npos ? std::string_view{} : line.substr(nameStart, nameEnd - nameStart) };
                    // (the next non-blank line)
                    auto next = lineEnd + 1u;
                    while (next < text.size())
                    {
                        auto const e = text.find('\n', next);
                        std::string_view const l{ text.data() + next, (e == std::string::npos ? text.size() : e) - next };
                        if (l.find_first_not_of(" \t\r") != std::string_view::npos)
                            break;
                        next = e == std::string::npos ? text.size() : e + 1u;
                    }
                    // (a generator, coroutine or async generator function: its body returns __generator(...), __coroutine(...) or __async_generator(...))
                    if (!name.empty())
                    {
                        // (a def whose body is on its line: "def f(): return __generator(...)")
                        for (auto const& [call, flavour] : { std::pair{ "return __generator(", 'G' }, std::pair{ "return __coroutine(", 'C' },
                            std::pair{ "return __async_generator(", 'A' } })
                            if (auto const at = line.find(call); at != std::string_view::npos && (line.substr(0u, at).ends_with(": ") || line.substr(0u, at).ends_with("; ")))
                                iPyFlavours[name] = flavour;
                        auto const bodyIndent = text.find_first_not_of(" \t", next) - next;
                        for (auto l = next; l < text.size();)
                        {
                            auto const e = text.find('\n', l);
                            std::string_view const bodyLine{ text.data() + l, (e == std::string::npos ? text.size() : e) - l };
                            auto const indent = bodyLine.find_first_not_of(" \t");
                            if (indent != std::string_view::npos && bodyLine[indent] != '\r')
                            {
                                if (indent < bodyIndent)
                                    break;
                                if (indent == bodyIndent)
                                    for (auto const& [call, flavour] : { std::pair{ "return __generator(", 'G' }, std::pair{ "return __coroutine(", 'C' }, 
                                        std::pair{ "return __async_generator(", 'A' } })
                                        if (auto const at = bodyLine.find(call); at != std::string_view::npos && (at == indent || bodyLine.substr(0u, at).ends_with("; ")))
                                            iPyFlavours[name] = flavour;
                            }
                            l = e == std::string::npos ? text.size() : e + 1u;
                        }
                    }
                    auto p = text.find_first_not_of(" \t", next);
                    // (the cells that the function's body starts with (_k_x = __cell(...); ...): not its first statement)
                    while (p != std::string::npos && text.compare(p, 3u, "_k_") == 0)
                    {
                        auto const semicolon = text.find("; ", p);
                        auto const eol = text.find('\n', p);
                        if (semicolon == std::string::npos || (eol != std::string::npos && semicolon > eol) || text.find("= __cell(", p) > semicolon)
                            break;
                        p = text.find_first_not_of(" \t", semicolon + 1u);
                    }
                    bool raw = false;
                    if (p != std::string::npos && (text[p] == 'r' || text[p] == 'R' || text[p] == 'u' || text[p] == 'U') && p + 1u < text.size() && 
                        (text[p + 1u] == '\'' || text[p + 1u] == '\"'))
                    {
                        raw = text[p] == 'r' || text[p] == 'R';
                        ++p;
                    }
                    if (!name.empty() && p != std::string::npos && p < text.size() && (text[p] == '\'' || text[p] == '\"') && 
                        text.find_last_of('\n', p) == (next == 0u ? std::string::npos : next - 1u))
                    {
                        char const quote = text[p];
                        bool const triple = text.compare(p, 3u, std::string(3u, quote)) == 0;
                        auto const open = p + (triple ? 3u : 1u);
                        std::string value;
                        bool closed = false;
                        std::size_t q = open;
                        for (; q < text.size(); ++q)
                        {
                            if (triple ? text.compare(q, 3u, std::string(3u, quote)) == 0 : text[q] == quote)
                            {
                                closed = true;
                                break;
                            }
                            if (!triple && text[q] == '\n')
                                break;
                            if (text[q] == '\\' && !raw && q + 1u < text.size())
                            {
                                char const c = text[++q];
                                switch (c)
                                {
                                case 'n': value += '\n'; break;
                                case 't': value += '\t'; break;
                                case '\\': value += '\\'; break;
                                case '\'': value += '\''; break;
                                case '\"': value += '\"'; break;
                                case '\n': break;
                                default: value += '\\'; value += c; break;
                                }
                                continue;
                            }
                            if (text[q] != '\r')
                                value += text[q];
                        }
                        // (only the literal on its own (an expression statement))
                        if (closed)
                        {
                            auto const after = text.find_first_not_of(" \t\r", q + (triple ? 3u : 1u));
                            if (after == std::string::npos || text[after] == '\n' || text[after] == '#')
                                iPyDocstrings[name] = clean_docstring(value);
                        }
                    }
                }
                lineStart = lineEnd + 1u;
            }
        }
        // (the names of the functions that the rewrites made, as their closures, generators, coroutines and classes name them: their 
        // frames' names)
        {
            static std::regex const sNamed{ "\\b__(?:closure|generator|coroutine|async_generator)\\(\\s*(\\w+)\\s*,\\s*'([^']*)'" };
            for (auto m = std::sregex_iterator{ text.begin(), text.end(), sNamed }; m != std::sregex_iterator{}; ++m)
                iPyFrameNames[(*m)[1].str()] = (*m)[2].str();
        }
        if (changed && std::getenv("NEOS_PY_SOURCE") != nullptr)
            std::fprintf(stderr, "%s\n", text.c_str());
        iPyPreparsed.insert(text);
        track_lines();
        iPyLines[text] = std::move(lines);
        if (changed)
            aFragment.set_source(neolib::string_view{ text });
        if (auto const module = py_current_module())
        {
            iPyModules[*module].source = text;
            iPyModules[*module].preparsed = true;
        }
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
            if (!py_big_integers())
                return t == type::Ibig ? type::I64 : t == type::Fbig ? type::F64 : t;
            return t == type::Fbig ? type::F64 : t == type::Boolean || t == type::String || is_float(t) ? t : type::Integer;
        }
        return static_cast<type>(aOperand.get<i_data_type>().index());
    }

    operand_type compiler::py_pop()
    {
        if (operands().empty())
            throw std::runtime_error("expression expected");
        operand_type operand{ operands().back() };
        operands().pop_back();
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
        if (!py_big_integers())
        {
            // (64-bit ints: a literal that does not fit is an int object)
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
            if (declaration_pass())
                return result;
            return py_owned(py_runtime_call("python::int_from_string", { type::String }, type::Object, { py_string(text) }));
        }
        // (an int literal is a small integer (an integer of any size shifted left one bit) if it has at most 62 bits)
        try
        {
            auto const value = std::stoll(text);
            if (value >= -(std::int64_t{ 1 } << 62) && value < (std::int64_t{ 1 } << 62))
                return {};
        }
        catch (std::out_of_range const&)
        {
        }
        fragment result;
        result.valueType = type::Integer;
        if (declaration_pass())
            return result;
        return py_park_integer(py_runtime_call("python::integer_from_string", { type::String }, type::Integer, { py_string(text) }));
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
        if (!py_function().freeObjects.empty())
        {
            temporaryObject = py_function().freeObjects.back();
            py_function().freeObjects.pop_back();
        }
        else
            temporaryObject = temporary(type::Object);
        py_function().pendingObjects.push_back(temporaryObject);
        py_function().objectTemporaries.insert(temporaryObject);
        put_local(aValue, opcode::LocalTee, temporaryObject);
        aValue.valueType = type::Object;
        aValue.fragmentKind = fragment_kind::Value;
        return aValue;
    }

    // --- integers of any size (Python's int): type::Integer, a small integer shifted left one bit or an integer object's handle
    // shifted left one bit with the low bit set (see NEOS_RT_INTEGER); an integer value is borrowed: an integer that is a new 
    // reference (a call's result) has its object's reference held by a temporary (released at the end of the statement), a
    // variable holds a reference of its own

    namespace
    {
        // (the handle of an integer object (i32), or zero: (value >> 1) & -(value & 1))
        void put_integer_handle(fragment& aFragment, std::uint32_t aLocal)
        {
            put_local(aFragment, opcode::LocalGet, aLocal);
            put_i64(aFragment, 1);
            put(aFragment, opcode::I64ShrU);
            put_i64(aFragment, 0);
            put_local(aFragment, opcode::LocalGet, aLocal);
            put_i64(aFragment, 1);
            put(aFragment, opcode::I64And);
            put(aFragment, opcode::I64Sub);
            put(aFragment, opcode::I64And);
            put(aFragment, opcode::I32ConvertI64);
        }

        // (if an integer is an integer object: the value's low bit as an i32)
        void put_is_object(fragment& aFragment, std::uint32_t aLocal, bool aGlobal = false)
        {
            put_local(aFragment, aGlobal ? opcode::GlobalGet : opcode::LocalGet, aLocal);
            put_i64(aFragment, 1);
            put(aFragment, opcode::I64And);
            put(aFragment, opcode::I32ConvertI64);
        }
    }

    bool compiler::py_big_integers() const
    {
        // Python's int is an integer of any size (type::Integer) with bigint promotion (python.neos: bigint.promotion: true), else a 
        // 64-bit integer (which wraps, or traps on overflow if trap_on_overflow is on)
        return generator().bigint_promotion();
    }

    std::uint32_t compiler::py_scratch(std::uint32_t aRole, type aType)
    {
        // a local that code uses only between its own instructions (no other code that uses the role runs between them): one
        // for each role in a function (fewer locals: the JIT keeps the most used locals in registers)
        auto& scratch = py_function().pyScratch;
        auto const existing = scratch.find(aRole);
        if (existing != scratch.end())
            return existing->second;
        return scratch[aRole] = temporary(aType);
    }

    fragment compiler::py_park_integer(fragment aValue)
    {
        // an integer that is a new reference: an int expression (its value is in a temporary: if it is a small integer (its fast 
        // version), else an integer object whose reference is held by the expression's object temporary (its slow version))
        ensure_function();
        py_integer_expression e;
        auto const value = py_integer_temporary();
        e.prefix = aValue;
        put_local(e.prefix, opcode::LocalSet, value);
        put_local(e.fast, opcode::LocalGet, value);
        put_i64(e.fast, 1);
        put(e.fast, opcode::I64And);
        put(e.fast, opcode::I32ConvertI64);
        put_op(e.fast.fragmentCode.as_std_vector(), opcode::BrIf);
        put_u32(e.fast.fragmentCode.as_std_vector(), 0u);
        put_local(e.fast, opcode::LocalGet, value);
        e.bails = true;
        e.small.insert(value);
        e.stable = true;
        put_local(e.slow, opcode::LocalGet, value);
        e.low = SmallLow;
        e.high = SmallHigh;
        auto result = py_integer_fragment(std::move(e));
        if (aValue.fragmentKind == fragment_kind::Argument)
            result.fragmentKind = fragment_kind::Argument;
        return result;
    }

    fragment compiler::py_retain_integer(std::uint32_t aLocal)
    {
        // (an integer object: a reference for its holder)
        fragment result;
        put_is_object(result, aLocal);
        put_block(result, opcode::If, {});
        put_integer_handle(result, aLocal);
        emit_call_to(result, runtime_function("neos::object_retain", { type::Object }, type::Void));
        put(result, opcode::End);
        return result;
    }

    fragment compiler::py_release_integer(std::uint32_t aVariable, bool aGlobal)
    {
        // (an integer object: its holder's reference is released)
        fragment result;
        put_is_object(result, aVariable, aGlobal);
        put_block(result, opcode::If, {});
        put_local(result, aGlobal ? opcode::GlobalGet : opcode::LocalGet, aVariable);
        put_i64(result, 1);
        put(result, opcode::I64ShrU);
        put(result, opcode::I32ConvertI64);
        emit_call_to(result, runtime_function("neos::object_release", { type::Object }, type::Void));
        put(result, opcode::End);
        return result;
    }

    fragment compiler::py_small_integers(std::vector<std::uint32_t> const& aLocals)
    {
        // (integers that must be small (e.g. range()'s arguments): OverflowError if one is an integer object)
        fragment result;
        for (auto const local : aLocals)
        {
            put_is_object(result, local);
            put_block(result, opcode::If, {});
            append(result, py_runtime_call("python::integer_i64", { type::Integer }, type::I64, { local_value(local, type::Integer) }));
            put(result, opcode::Drop);
            put(result, opcode::End);
        }
        return result;
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
        if (!in_function())
            return result;
        // (the temporaries of int expressions that other code took over: reusable)
        auto& function = py_function();
        function.freeObjects.insert(function.freeObjects.end(), function.consumedObjects.begin(), function.consumedObjects.end());
        function.consumedObjects.clear();
        function.freeIntegers.insert(function.freeIntegers.end(), function.pendingIntegers.begin(), function.pendingIntegers.end());
        function.pendingIntegers.clear();
        if (function.pendingObjects.empty())
            return result;
        auto const release = runtime_function("neos::object_release", { type::Object }, type::Void);
        for (auto const t : function.pendingObjects)
        {
            // (a temporary that holds no object (e.g. that of an int that is a small integer): zero)
            put_local(result, opcode::LocalGet, t);
            put_block(result, opcode::If, {});
            put_local(result, opcode::LocalGet, t);
            emit_call_to(result, release);
            put_i32(result, 0);
            put_local(result, opcode::LocalSet, t);
            put(result, opcode::End);
            function.freeObjects.push_back(t);
        }
        function.pendingObjects.clear();
        return result;
    }

    fragment compiler::py_release_variables(bool aTemporaries, std::optional<std::uint32_t> aExcept)
    {
        // the function's objects are released (when it returns)
        auto result = aTemporaries ? py_flush() : make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
        if (!in_function())
            return result;
        for (auto const v : py_function().integerVariables)
            if (v != aExcept)
                append(result, py_release_integer(v, false));
        if (py_function().objectVariables.empty())
            return result;
        auto const release = runtime_function("neos::object_release", { type::Object }, type::Void);
        for (auto const v : py_function().objectVariables)
        {
            put_local(result, opcode::LocalGet, v);
            emit_call_to(result, release);
        }
        return result;
    }

    void compiler::py_coerce(fragment& aValue, type aTarget)
    {
        auto const from = aValue.valueType;
        if (from == type::Integer || aTarget == type::Integer)
        {
            py_coerce_integer(aValue, aTarget);
            return;
        }
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

    void compiler::py_coerce_integer(fragment& aValue, type aTarget)
    {
        // conversions of integers of any size (type::Integer)
        auto const from = aValue.valueType;
        auto const kind = aValue.fragmentKind == fragment_kind::Argument ? fragment_kind::Argument : fragment_kind::Value;
        auto const source = aValue.fragmentSource;
        auto const original = aValue;
        auto const finish = [&](fragment aResult, type aType)
        {
            aResult.valueType = aType;
            aResult.fragmentKind = kind;
            aResult.fragmentSource = source;
            aValue = aResult;
        };
        if (from == type::Integer)
        {
            if (auto const local = local_of(aValue); local && py_counter(*local) && (is_float(aTarget) || (is_numeric(aTarget) && aTarget != type::Boolean) ||
                aTarget == type::String))
            {
                // (a variable that only for loops over ranges bind: a small integer)
                fragment result = aValue;
                put_i64(result, 1);
                put(result, opcode::I64ShrS);
                if (aTarget == type::String)
                    emit_call_to(result, runtime_function("neos::string_from_i64", { type::I64 }, type::String));
                else if (aTarget != type::I64)
                    convert(result.fragmentCode.as_std_vector(), type::I64, aTarget);
                finish(result, aTarget);
                return;
            }
            if (aTarget == type::Boolean)
            {
                // (an integer object is never zero)
                put_i64(aValue, 0);
                put(aValue, opcode::I64Ne);
                finish(aValue, type::Boolean);
            }
            else if (aTarget == type::Object)
                finish(py_owned(py_runtime_call("python::from_integer", { type::Integer }, type::Object, { aValue })), type::Object);
            else if (aTarget == type::String)
            {
                // (a small integer: decimal (the VM's neos::string_from_i64); an integer object: by the runtime plugin)
                auto const scratch = py_scratch(ScratchCoerce, type::Integer);
                fragment result = aValue;
                put_local(result, opcode::LocalSet, scratch);
                put_is_object(result, scratch);
                put_block(result, opcode::If, type::String);
                append(result, py_runtime_call("python::integer_str", { type::Integer }, type::String, { local_value(scratch, type::Integer) }));
                put(result, opcode::Else);
                put_local(result, opcode::LocalGet, scratch);
                put_i64(result, 1);
                put(result, opcode::I64ShrS);
                emit_call_to(result, runtime_function("neos::string_from_i64", { type::I64 }, type::String));
                put(result, opcode::End);
                finish(result, type::String);
                // (a string followed by it: see py_operation)
                if (in_function())
                    py_function().pyIntegerStrings[integer_key(aValue)] = original;
            }
            else if (is_float(aTarget) || is_numeric(aTarget))
            {
                // (a small integer: shifted right one bit; an integer object: converted by the runtime plugin)
                bool const toFloat = is_float(aTarget);
                auto const scratch = py_scratch(ScratchCoerce, type::Integer);
                fragment result = aValue;
                put_local(result, opcode::LocalSet, scratch);
                put_is_object(result, scratch);
                put_block(result, opcode::If, toFloat ? type::F64 : type::I64);
                append(result, py_runtime_call(toFloat ? "python::integer_float" : "python::integer_i64", { type::Integer }, toFloat ? type::F64 : type::I64, 
                    { local_value(scratch, type::Integer) }));
                put(result, opcode::Else);
                put_local(result, opcode::LocalGet, scratch);
                put_i64(result, 1);
                put(result, opcode::I64ShrS);
                if (toFloat)
                    put(result, opcode::F64SConvertI64);
                put(result, opcode::End);
                if (aTarget != (toFloat ? type::F64 : type::I64))
                    convert(result.fragmentCode.as_std_vector(), toFloat ? type::F64 : type::I64, aTarget);
                finish(result, aTarget);
            }
            else
                throw std::runtime_error("cannot convert an int to " + type_name(aTarget));
            return;
        }
        // (to an integer)
        if (from == type::Object)
            finish(py_park_integer(py_runtime_call("python::to_integer", { type::Object }, type::Integer, { aValue })), type::Integer);
        else if (from == type::Void)
            throw std::runtime_error("cannot convert None to an int");
        else if (from == type::String)
            throw std::runtime_error("cannot convert a str to an int");
        else if (is_float(from))
        {
            if (from != type::F64)
                convert(aValue.fragmentCode.as_std_vector(), from, type::F64);
            aValue.valueType = type::F64;
            finish(py_park_integer(py_runtime_call("python::integer_from_float", { type::F64 }, type::Integer, { aValue })), type::Integer);
        }
        else if (is_numeric(from))
        {
            // (a value of at most 32 bits is a small integer; a 64-bit value is one if it has at most 63 bits, else an integer
            // object)
            bool const small = !is_64(from);
            if (small)
            {
                if (from == type::Boolean || is_unsigned(from))
                    put(aValue, opcode::I64UConvertI32);
                else
                    put(aValue, opcode::I64SConvertI32);
                put_i64(aValue, 1);
                put(aValue, opcode::I64Shl);
                finish(aValue, type::Integer);
                return;
            }
            auto const scratch = py_scratch(ScratchCoerceI64, type::I64);
            fragment result = aValue;
            put_local(result, opcode::LocalTee, scratch);
            put_i64(result, 1);
            put(result, opcode::I64Shl);
            put_i64(result, 1);
            put(result, opcode::I64ShrS);
            put_local(result, opcode::LocalGet, scratch);
            put(result, opcode::I64Eq);
            put_block(result, opcode::If, type::Integer);
            put_local(result, opcode::LocalGet, scratch);
            put_i64(result, 1);
            put(result, opcode::I64Shl);
            put(result, opcode::Else);
            append(result, py_park_integer(py_runtime_call("python::integer_from_i64", { type::I64 }, type::Integer, { local_value(scratch, type::I64) })));
            put(result, opcode::End);
            finish(result, type::Integer);
        }
        else
            throw std::runtime_error("cannot convert " + type_name(from) + " to an int");
    }

    namespace
    {
        // (an int literal (or a bool): its value)
        std::optional<ibig> int_immediate(operand_type const& aOperand)
        {
            if (!is_immediate(aOperand))
                return {};
            std::optional<ibig> result;
            std::visit([&](auto const& d)
                {
                    using vt = std::decay_t<decltype(d)>;
                    if constexpr (!std::is_same_v<vt, std::monostate>)
                    {
                        using value_type = typename vt::type;
                        if constexpr (std::is_same_v<value_type, ibig>)
                            result = d.value().value();
                        else if constexpr (std::is_same_v<value_type, boolean>)
                            result = ibig{ d.value().value() ? 1 : 0 };
                        else if constexpr (std::is_integral_v<value_type>)
                            result = ibig{ static_cast<long long>(d.value().value()) };
                    }
                }, std::get<data_type>(aOperand));
            return result;
        }

        bool ibig_negative(ibig const& aValue)
        {
            return aValue < ibig{ 0 };
        }
    }

    namespace
    {
        struct int_range
        {
            std::int64_t low = SmallLow;
            std::int64_t high = SmallHigh;
            bool small() const { return low >= SmallLow && high <= SmallHigh; }
        };

        // (of values within the range of small integers (or one beyond): without overflow)
        std::int64_t saturated_multiply(std::int64_t aLhs, std::int64_t aRhs)
        {
            if (aLhs == 0 || aRhs == 0)
                return 0;
            auto const max = std::numeric_limits<std::int64_t>::max();
            auto const l = static_cast<std::uint64_t>(aLhs < 0 ? -aLhs : aLhs);
            auto const r = static_cast<std::uint64_t>(aRhs < 0 ? -aRhs : aRhs);
            if (l > static_cast<std::uint64_t>(max) / r)
                return (aLhs < 0) != (aRhs < 0) ? -max : max;
            return aLhs * aRhs;
        }

        std::int64_t floor_divide(std::int64_t aLhs, std::int64_t aRhs)
        {
            auto const q = aLhs / aRhs;
            return (aLhs % aRhs != 0 && ((aLhs < 0) != (aRhs < 0))) ? q - 1 : q;
        }


    }

    std::optional<compiler::py_integer_expression> compiler::py_integer_expression_of(fragment const& aValue)
    {
        // the int expression that is a value (its code is unchanged): its object temporary (if any) is no longer the statement's (the
        // code that uses the expression takes over its slow version's result)
        if (!in_function() || aValue.valueType != type::Integer)
            return {};
        auto& function = py_function();
        auto const found = function.pyIntegers.find(integer_key(aValue));
        if (found == function.pyIntegers.end())
            return {};
        auto result = found->second;
        if (result.holder)
        {
            auto const pending = std::find(function.pendingObjects.begin(), function.pendingObjects.end(), *result.holder);
            if (pending == function.pendingObjects.end())
                return {};
            function.pendingObjects.erase(pending);
            function.consumedObjects.push_back(*result.holder);
        }
        return result;
    }

    fragment compiler::py_integer_value(py_integer_expression const& aExpression, bool aHeld)
    {
        // an int expression's value (without its prefix): its fast version, or (if that branches out) its slow version (whose result
        // is held by the expression's object temporary (aHeld) or is a new reference)
        fragment result;
        if (!aExpression.bails)
            append(result, aExpression.fast);
        else
        {
            put_block(result, opcode::Block, type::Integer);
            put_block(result, opcode::Block, {});
            append(result, aExpression.fast);
            put_op(result.fragmentCode.as_std_vector(), opcode::Br);
            put_u32(result.fragmentCode.as_std_vector(), 1u);
            put(result, opcode::End);
            append(result, aExpression.slow);
            if (aHeld && aExpression.holder)
            {
                auto const scratch = py_scratch(ScratchPark, type::Integer);
                put_local(result, opcode::LocalTee, scratch);
                put_integer_handle(result, scratch);
                put_local(result, opcode::LocalSet, *aExpression.holder);
            }
            put(result, opcode::End);
        }
        result.valueType = type::Integer;
        result.fragmentKind = fragment_kind::Value;
        return result;
    }

    void compiler::py_integer_operation(construct aConstruct, std::optional<operand_type> const& aLhs, operand_type const& aRhs)
    {
        // an operation on integers of any size: an int expression (see py_integer_expression) whose fast version is inline (operands
        // that are int expressions are part of it: one fast version for an expression as a whole, without intermediate checks where 
        // the operands' ranges show that the result is a small integer) and whose slow version calls the runtime plugin 
        // (python::integer_binary, integer_unary, integer_compare)
        bool const unary = !aLhs;
        // (constants: folded, exactly (as Python: floor division and modulo; a big power or shift is left to the program))
        {
            auto const l = unary ? std::optional<ibig>{ ibig{ 0 } } : int_immediate(*aLhs);
            auto const r = int_immediate(aRhs);
            if (l && r)
            {
                std::optional<ibig> folded;
                std::optional<bool> truth;
                auto const zero = ibig{ 0 };
                auto const floor_divmod = [&](ibig& q, ibig& m)
                {
                    q = *l / *r;
                    m = *l - q * *r;
                    if (m != zero && ibig_negative(m) != ibig_negative(*r))
                    {
                        q = q - ibig{ 1 };
                        m = m + *r;
                    }
                };
                auto const small = [](ibig const& aValue, std::int64_t aLimit) { return !(aValue < ibig{ -aLimit }) && aValue < ibig{ aLimit }; };
                switch (aConstruct)
                {
                case construct::Add: folded = *l + *r; break;
                case construct::Subtract: folded = *l - *r; break;
                case construct::Multiply: folded = *l * *r; break;
                case construct::FloorDivide:
                case construct::Modulo:
                    if (*r != zero)
                    {
                        ibig q, m;
                        floor_divmod(q, m);
                        folded = aConstruct == construct::FloorDivide ? q : m;
                    }
                    break;
                case construct::Power:
                    if (!ibig_negative(*r) && small(*r, 4096) && small(*l, std::int64_t{ 1 } << 32))
                    {
                        auto e = integer_cast<std::int64_t>(*r);
                        ibig p{ 1 }, b = *l;
                        for (; e > 0; e >>= 1)
                        {
                            if ((e & 1) != 0)
                                p = p * b;
                            if (e > 1)
                                b = b * b;
                        }
                        folded = p;
                    }
                    break;
                case construct::LeftShift:
                    if (!ibig_negative(*r) && small(*r, 4096))
                        folded = *l << integer_cast<std::int64_t>(*r);
                    break;
                case construct::RightShift:
                    if (!ibig_negative(*r))
                        folded = small(*r, 1 << 20) ? *l >> integer_cast<std::int64_t>(*r) : (ibig_negative(*l) ? ibig{ -1 } : zero);
                    break;
                case construct::BitAnd: folded = *l & *r; break;
                case construct::BitOr: folded = *l | *r; break;
                case construct::BitXor: folded = *l ^ *r; break;
                case construct::Negative: folded = -*r; break;
                case construct::Positive: folded = *r; break;
                case construct::Invert: folded = ~*r; break;
                case construct::Less: truth = *l < *r; break;
                case construct::LessEqual: truth = *l <= *r; break;
                case construct::Greater: truth = *l > *r; break;
                case construct::GreaterEqual: truth = *l >= *r; break;
                case construct::Equal: truth = *l == *r; break;
                case construct::NotEqual: truth = *l != *r; break;
                default: break;
                }
                if (folded)
                {
                    push_operand(operand_type{ data_type{ data<ibig>{ *folded } } });
                    return;
                }
                if (truth)
                {
                    push_operand(operand_type{ data_type{ data<boolean>{ *truth } } });
                    return;
                }
            }
        }
        ensure_function();
        if (aConstruct == construct::Divide || aConstruct == construct::Power)
        {
            py_integer_divide_or_power(aConstruct, aLhs, aRhs);
            return;
        }
        if (aConstruct == construct::Positive)
        {
            push_operand(operand_type{ materialize(aRhs, type::Integer) });
            return;
        }
        // (the operands: a constant small integer, a variable (a local, read where it is used), an int expression, or other code's 
        // value (a snapshot: a temporary that its code sets first))
        struct operand_info
        {
            enum kind_t { Constant, Variable, Expression } kind = Constant;
            std::int64_t value = 0;
            std::uint32_t local = 0u;
            py_integer_expression expression;
            fragment original;
            fragment prefix;
            bool stable = false;                // (its value is not a variable's (that other code can change))
            std::int64_t low = SmallLow;
            std::int64_t high = SmallHigh;
            int_range range() const { return { low, high }; }
            std::int64_t magnitude() const { return std::max(-low, high); }
        };
        auto const classify = [&](operand_type const& aOperand) -> operand_info
        {
            operand_info o;
            if (auto const value = int_immediate(aOperand); value && !(*value < ibig{ SmallLow }) && !(ibig{ SmallHigh } < *value))
            {
                o.value = integer_cast<std::int64_t>(*value);
                o.low = o.high = o.value;
                o.stable = true;
                return o;
            }
            auto value = materialize(aOperand, type::Integer);
            if (auto const local = local_of(value))
            {
                o.kind = operand_info::Variable;
                o.local = *local;
                if (auto const counter = py_counter(*local))
                {
                    o.low = counter->first;
                    o.high = counter->second;
                }
                return o;
            }
            if (in_function())
                if (auto const found = py_function().pyIntegers.find(integer_key(value)); found != py_function().pyIntegers.end())
                {
                    o.kind = operand_info::Expression;
                    o.expression = found->second;
                    o.original = value;
                    o.stable = o.expression.stable;
                    o.low = o.expression.low;
                    o.high = o.expression.high;
                    return o;
                }
            o.kind = operand_info::Variable;
            o.local = py_integer_temporary();
            o.prefix = value;
            put_local(o.prefix, opcode::LocalSet, o.local);
            o.stable = true;
            return o;
        };
        auto const has_prefix = [](operand_info const& o)
        {
            return !o.prefix.fragmentCode.empty() || (o.kind == operand_info::Expression && !o.expression.prefix.fragmentCode.empty());
        };
        std::optional<operand_info> lhsInfo;
        if (!unary)
            lhsInfo = classify(*aLhs);
        auto b = classify(aRhs);
        bool const lhsChanged = lhsInfo && (lhsInfo->kind != operand_info::Variable ||
            sets_local(b.kind == operand_info::Expression ? b.expression.prefix : b.prefix, lhsInfo->local));
        if (lhsInfo && !lhsInfo->stable && has_prefix(b) && lhsChanged)
        {
            // (the left operand is evaluated before the right operand's code runs: a snapshot)
            auto& o = *lhsInfo;
            auto const local = py_integer_temporary();
            fragment prefix = o.kind == operand_info::Expression ? o.original : local_value(o.local, type::Integer);
            put_local(prefix, opcode::LocalSet, local);
            o = operand_info{};
            o.kind = operand_info::Variable;
            o.local = local;
            o.prefix = prefix;
            o.stable = true;
        }
        operand_info dummy;
        auto& a = lhsInfo ? *lhsInfo : dummy;
        // (the expression)
        py_integer_expression e;
        e.stable = (!lhsInfo || lhsInfo->stable) && b.stable;
        for (auto* o : { lhsInfo ? &*lhsInfo : nullptr, &b })
        {
            if (o == nullptr)
                continue;
            if (o->kind == operand_info::Expression)
            {
                // (the operand's code is part of this expression's: its object temporary is not used)
                if (o->expression.holder)
                {
                    auto& function = py_function();
                    auto const pending = std::find(function.pendingObjects.begin(), function.pendingObjects.end(), *o->expression.holder);
                    if (pending != function.pendingObjects.end())
                    {
                        function.pendingObjects.erase(pending);
                        function.consumedObjects.push_back(*o->expression.holder);
                    }
                }
                append(e.prefix, o->expression.prefix);
                e.small.insert(o->expression.small.begin(), o->expression.small.end());
                e.height = std::max(e.height, o->expression.height + 1u);
                if (o->expression.bails)
                    e.bails = true;
            }
            else
                append(e.prefix, o->prefix);
        }
        // --- the fast version
        auto& f = e.fast;
        auto const op = [&](opcode aOpcode) { put(f, aOpcode); };
        auto const i64 = [&](std::int64_t aValue) { put_i64(f, aValue); };
        auto const get = [&](std::uint32_t aLocal) { put_local(f, opcode::LocalGet, aLocal); };
        auto const set = [&](std::uint32_t aLocal) { put_local(f, opcode::LocalSet, aLocal); };
        auto const tee = [&](std::uint32_t aLocal) { put_local(f, opcode::LocalTee, aLocal); };
        auto const scratch = [&](std::uint32_t aRole) { return py_scratch(aRole, type::I64); };
        // (the fast version branches out (to its slow version) if the condition (an i32) holds)
        auto const bail = [&]()
        {
            put_op(f.fragmentCode.as_std_vector(), opcode::BrIf);
            put_u32(f.fragmentCode.as_std_vector(), 0u);
            e.bails = true;
        };
        auto const always_bail = [&]()
        {
            put_op(f.fragmentCode.as_std_vector(), opcode::Br);
            put_u32(f.fragmentCode.as_std_vector(), 0u);
            e.bails = true;
            i64(0);
        };
        // (variables (that the expression has not checked) that must be small integers: one test of their low bits)
        auto const check_small = [&](std::initializer_list<operand_info const*> aOperands)
        {
            std::vector<std::uint32_t> locals;
            for (auto const* o : aOperands)
                if (o != nullptr && o->kind == operand_info::Variable && !e.small.contains(o->local) && !py_counter(o->local) && 
                    std::find(locals.begin(), locals.end(), o->local) == locals.end())
                    locals.push_back(o->local);
            if (locals.empty())
                return;
            for (std::size_t i = 0u; i < locals.size(); ++i)
            {
                get(locals[i]);
                if (i > 0u)
                    op(opcode::I64Ior);
            }
            i64(1); op(opcode::I64And); op(opcode::I32ConvertI64); bail();
            e.small.insert(locals.begin(), locals.end());
        };
        auto const push = [&](operand_info const& o)
        {
            switch (o.kind)
            {
            case operand_info::Constant:
                i64(o.value * 2);
                break;
            case operand_info::Variable:
                get(o.local);
                break;
            case operand_info::Expression:
                append(f, o.expression.fast);
                break;
            }
        };
        // (an operand (a variable, or an expression just pushed) whose value must be within [aLow, aHigh]: checked (if its range is 
        // not))
        auto const narrow = [&](operand_info& o, std::int64_t aLow, std::int64_t aHigh)
        {
            aLow = std::max(aLow, o.low);
            aHigh = std::min(aHigh, o.high);
            if (o.kind == operand_info::Constant || (aLow == o.low && aHigh == o.high))
                return;
            if (o.kind == operand_info::Variable)
                get(o.local);
            else
            {
                auto const z = scratch(ScratchZ);
                tee(z);
                get(z);
            }
            bool const below = aLow > o.low;
            bool const above = aHigh < o.high;
            if (below && above)
            {
                if (aHigh >= 0 && aLow == -aHigh - 1 && ((aHigh + 1) & aHigh) == 0)
                {
                    // (within [-2^k, 2^k): (v >> (k + 1)) + 1 is 0 or 1 (v shifted left one bit))
                    int k = 0;
                    while ((std::int64_t{ 1 } << k) != aHigh + 1)
                        ++k;
                    i64(k + 1); op(opcode::I64ShrS); i64(1); op(opcode::I64Add); i64(2); op(opcode::I64GeU);
                }
                else
                {
                    i64(aLow * 2); op(opcode::I64Sub);
                    i64(static_cast<std::int64_t>((static_cast<std::uint64_t>(aHigh) - static_cast<std::uint64_t>(aLow)) * 2u)); op(opcode::I64GtU);
                }
            }
            else if (above)
            {
                i64(aHigh * 2); op(opcode::I64GtS);
            }
            else
            {
                i64(aLow * 2); op(opcode::I64LtS);
            }
            bail();
            o.low = aLow;
            o.high = aHigh;
        };
        auto const result_range = [&](int_range const& aRange)
        {
            e.low = aRange.low;
            e.high = aRange.high;
        };
        // --- the slow version
        auto& g = e.slow;
        std::vector<std::uint32_t> owned;
        auto const slow_push = [&](operand_info const& o, std::uint32_t aOperand)
        {
            switch (o.kind)
            {
            case operand_info::Constant:
                put_i64(g, o.value * 2);
                break;
            case operand_info::Variable:
                put_local(g, opcode::LocalGet, o.local);
                break;
            case operand_info::Expression:
                append(g, py_integer_value(o.expression, false));
                if (o.expression.bails)
                {
                    // (a new reference: released after the call)
                    auto const t = py_scratch(ScratchOwned + 2u * e.height + aOperand, type::Integer);
                    put_local(g, opcode::LocalTee, t);
                    owned.push_back(t);
                }
                break;
            }
        };
        auto const slow_call = [&](std::string const& aName, std::int32_t aOperator, type aResult)
        {
            put_i32(g, aOperator);
            if (!unary)
                slow_push(a, 0u);
            slow_push(b, 1u);
            if (unary)
                emit_call_to(g, runtime_function(aName, { type::I32, type::Integer }, aResult));
            else
                emit_call_to(g, runtime_function(aName, { type::I32, type::Integer, type::Integer }, aResult));
            for (auto const t : owned)
                append(g, py_release_integer(t, false));
        };
        static std::map<construct, std::int32_t> const sBinary = { { construct::Add, PyAdd }, { construct::Subtract, PySubtract }, 
            { construct::Multiply, PyMultiply }, { construct::FloorDivide, PyFloorDivide }, { construct::Modulo, PyRemainder }, 
            { construct::LeftShift, PyLeftShift }, { construct::RightShift, PyRightShift }, 
            { construct::BitAnd, PyAnd }, { construct::BitOr, PyOr }, { construct::BitXor, PyXor } };
        static std::map<construct, std::pair<std::int32_t, opcode>> const sCompare = { { construct::Less, { PyLess, opcode::I64LtS } }, 
            { construct::LessEqual, { PyLessEqual, opcode::I64LeS } }, { construct::Equal, { PyEqual, opcode::I64Eq } }, 
            { construct::NotEqual, { PyNotEqual, opcode::I64Ne } }, { construct::Greater, { PyGreater, opcode::I64GtS } }, 
            { construct::GreaterEqual, { PyGreaterEqual, opcode::I64GeS } } };
        constexpr std::int64_t Half = std::int64_t{ 1 } << 61;
        bool comparison = false;
        switch (aConstruct)
        {
        case construct::Add:
        case construct::Subtract:
            {
                // (the operands' ranges: no overflow check if the result is within the range of small integers; else a variable is
                // checked to be within a range that makes it so; else an overflow check of the result)
                bool const add = aConstruct == construct::Add;
                auto const sum = [&]() { return add ? int_range{ a.low + b.low, a.high + b.high } : int_range{ a.low - b.high, a.high - b.low }; };
                auto const bounded = [&](operand_info const& o) { return o.low >= -Half && o.high <= Half; };
                std::optional<int_range> na, nb;
                if (!sum().small())
                {
                    if (a.kind != operand_info::Constant && bounded(b))
                        na = add ? int_range{ SmallLow - b.low, SmallHigh - b.high } : int_range{ SmallLow + b.high, SmallHigh + b.low };
                    else if (b.kind != operand_info::Constant && bounded(a))
                        nb = add ? int_range{ SmallLow - a.low, SmallHigh - a.high } : int_range{ a.high - SmallHigh, a.low - SmallLow };
                    else if (a.kind == operand_info::Variable && b.kind == operand_info::Variable)
                        na = nb = int_range{ -(Half >> 1), (Half >> 1) - 1 };
                }
                check_small({ &a, &b });
                // (variables first: then the operation is a variable's update (local.get, constant, add, local.set), if it is one)
                if (na && a.kind == operand_info::Variable)
                    narrow(a, na->low, na->high);
                if (nb && b.kind == operand_info::Variable)
                    narrow(b, nb->low, nb->high);
                push(a);
                if (na)
                    narrow(a, na->low, na->high);
                push(b);
                if (nb)
                    narrow(b, nb->low, nb->high);
                if (sum().small())
                {
                    op(add ? opcode::I64Add : opcode::I64Sub);
                    result_range(sum());
                }
                else
                {
                    // r = a + b (a - b); overflow: ((a ^ r) & (b ^ r)) < 0 (((a ^ b) & (a ^ r)) < 0)
                    auto const x = scratch(ScratchX), y = scratch(ScratchY), r = scratch(ScratchResult);
                    set(y); tee(x); get(y); op(add ? opcode::I64Add : opcode::I64Sub); set(r);
                    if (add)
                    {
                        get(x); get(r); op(opcode::I64Xor); get(y); get(r); op(opcode::I64Xor);
                    }
                    else
                    {
                        get(x); get(y); op(opcode::I64Xor); get(x); get(r); op(opcode::I64Xor);
                    }
                    op(opcode::I64And); i64(0); op(opcode::I64LtS); bail();
                    get(r);
                    result_range({});
                }
                slow_call("python::integer_binary", sBinary.at(aConstruct), type::Integer);
            }
            break;
        case construct::Multiply:
            {
                // (the operands' ranges: within ranges that make the product a small integer)
                bool const square = a.kind == operand_info::Variable && b.kind == operand_info::Variable && a.local == b.local;
                auto const product = [&]() -> int_range
                {
                    if (square)
                    {
                        auto const m = a.magnitude();
                        return { 0, saturated_multiply(m, m) };
                    }
                    std::int64_t const p[] = { saturated_multiply(a.low, b.low), saturated_multiply(a.low, b.high), saturated_multiply(a.high, b.low), saturated_multiply(a.high, b.high) };
                    return { *std::min_element(std::begin(p), std::end(p)), *std::max_element(std::begin(p), std::end(p)) };
                };
                auto const bound = [&](std::int64_t aMagnitude)
                {
                    // ([-2^k, 2^k): a range whose product with a value of at most aMagnitude is within [-2^61, 2^61])
                    auto const limit = static_cast<std::uint64_t>(Half) / static_cast<std::uint64_t>(std::max<std::int64_t>(aMagnitude, 1));
                    auto const k = static_cast<std::int64_t>(std::bit_floor(std::max<std::uint64_t>(limit, 1u)));
                    return int_range{ -k, k - 1 };
                };
                std::optional<int_range> na, nb;
                if (!product().small())
                {
                    if (a.kind == operand_info::Constant)
                        nb = bound(a.magnitude());
                    else if (b.kind == operand_info::Constant)
                        na = bound(b.magnitude());
                    else if (a.magnitude() <= (std::int64_t{ 1 } << 30))
                        nb = bound(a.magnitude());
                    else if (b.magnitude() <= (std::int64_t{ 1 } << 30))
                        na = bound(b.magnitude());
                    else
                    {
                        na = int_range{ -(std::int64_t{ 1 } << 30), (std::int64_t{ 1 } << 30) - 1 };
                        if (!square)
                            nb = na;
                    }
                }
                check_small({ &a, &b });
                if (a.kind == operand_info::Constant || b.kind == operand_info::Constant)
                {
                    auto& v = a.kind == operand_info::Constant ? b : a;
                    auto const& c = a.kind == operand_info::Constant ? a : b;
                    push(v);
                    if (auto const& n = a.kind == operand_info::Constant ? nb : na)
                        narrow(v, n->low, n->high);
                    i64(c.value); op(opcode::I64Mul);
                }
                else
                {
                    push(a);
                    if (na)
                        narrow(a, na->low, na->high);
                    if (square)
                    {
                        b.low = a.low;
                        b.high = a.high;
                    }
                    i64(1); op(opcode::I64ShrS);
                    push(b);
                    if (nb)
                        narrow(b, nb->low, nb->high);
                    op(opcode::I64Mul);
                }
                if (!product().small())
                    throw std::logic_error("neos::language::compiler::py_integer_operation: product range");
                result_range(product());
                slow_call("python::integer_binary", PyMultiply, type::Integer);
            }
            break;
        case construct::FloorDivide:
        case construct::Modulo:
            {
                bool const floor = aConstruct == construct::FloorDivide;
                if (b.kind == operand_info::Constant && b.value == 0)
                    always_bail();
                else if (b.kind == operand_info::Constant && b.value > 0)
                {
                    auto const c = b.value;
                    check_small({ &a });
                    push(a);
                    if ((c & (c - 1)) == 0)
                    {
                        // (by a power of two, 2^k: shifted right k bits (q), or the low k bits (m))
                        int k = 0;
                        while ((std::int64_t{ 1 } << k) != c)
                            ++k;
                        if (floor)
                        {
                            i64(k); op(opcode::I64ShrS); i64(-2); op(opcode::I64And);
                        }
                        else
                        {
                            i64((c - 1) * 2); op(opcode::I64And);
                        }
                    }
                    else if (a.low >= 0)
                    {
                        // (a value that is not negative: as 64-bit integers (both shifted left one bit))
                        i64(c * 2);
                        if (floor)
                        {
                            op(opcode::I64DivS); i64(1); op(opcode::I64Shl);
                        }
                        else
                            op(opcode::I64RemS);
                    }
                    else if (floor)
                    {
                        // (q = trunc(a / c), less one if the remainder is negative)
                        auto const x = scratch(ScratchX), q = scratch(ScratchQ);
                        tee(x); i64(c * 2); op(opcode::I64DivS); tee(q);
                        get(x); get(q); i64(c * 2); op(opcode::I64Mul); op(opcode::I64Sub); i64(63); op(opcode::I64ShrS); op(opcode::I64Add);
                        i64(1); op(opcode::I64Shl);
                    }
                    else
                    {
                        // (m = a % c, plus c if negative)
                        auto const m = scratch(ScratchM);
                        i64(c * 2); op(opcode::I64RemS);
                        e.zeroFast = f;
                        tee(m);
                        get(m); i64(63); op(opcode::I64ShrS); i64(c * 2); op(opcode::I64And); op(opcode::I64Add);
                    }
                    if (floor)
                        result_range({ floor_divide(a.low, c), floor_divide(a.high, c) });
                    else
                        result_range(a.low >= 0 && a.high < c ? a.range() : int_range{ 0, c - 1 });
                }
                else
                {
                    // (as 64-bit integers (both shifted left one bit): b != 0 and (floor division, if b can be -1) a is not the least small 
                    // integer: q = a / b, r = a - q * b (m = a % b), adjusted to the floor (r != 0 and its sign is not b's: q - 1, m + b))
                    auto const x = scratch(ScratchX), y = scratch(ScratchY), q = scratch(ScratchQ), m = scratch(ScratchM);
                    check_small({ &a, &b });
                    // (the adjustment: r != 0 and its sign is not b's: ((r ^ b) & (r | -r)) >> 63 (-1, else 0))
                    auto const adjustment = [&]()
                    {
                        get(m); get(y); op(opcode::I64Xor); i64(0); get(m); op(opcode::I64Sub); get(m); op(opcode::I64Ior); op(opcode::I64And);
                        i64(63); op(opcode::I64ShrS);
                    };
                    if (floor)
                    {
                        // (both operands first: the code of an operand can use the scratch locals)
                        push(a); push(b); set(y); set(x);
                        if (a.low == SmallLow && b.low <= -1 && b.high >= -1)
                        {
                            get(x); i64(SmallLow * 2); op(opcode::I64Eq); bail();
                        }
                        get(y); op(opcode::I64Eqz); bail();
                        get(x); get(y); op(opcode::I64DivS); tee(q);
                        get(x); get(q); get(y); op(opcode::I64Mul); op(opcode::I64Sub); set(m);
                        adjustment(); op(opcode::I64Add);
                        i64(1); op(opcode::I64Shl);
                    }
                    else
                    {
                        push(a);
                        push(b); tee(y); op(opcode::I64Eqz); bail();
                        get(y); op(opcode::I64RemS);
                        // (whether the result is zero: the remainder's)
                        e.zeroFast = f;
                        tee(m);
                        adjustment(); get(y); op(opcode::I64And); op(opcode::I64Add);
                    }
                    if (floor)
                    {
                        auto const ma = std::min(a.magnitude(), SmallHigh);
                        result_range({ -ma, ma });
                    }
                    else if (b.low > 0)
                        result_range({ 0, b.high - 1 });
                    else if (b.high < 0)
                        result_range({ b.low + 1, 0 });
                    else
                        result_range({ -std::min(b.magnitude(), SmallHigh), std::min(b.magnitude(), SmallHigh) });
                }
                slow_call("python::integer_binary", sBinary.at(aConstruct), type::Integer);
            }
            break;
        case construct::BitAnd:
        case construct::BitOr:
        case construct::BitXor:
            {
                // (the bits of small integers (shifted left one bit) are the bits of the result (shifted))
                check_small({ &a, &b });
                push(a); push(b);
                op(aConstruct == construct::BitAnd ? opcode::I64And : aConstruct == construct::BitOr ? opcode::I64Ior : opcode::I64Xor);
                if (aConstruct == construct::BitAnd && (a.low >= 0 || b.low >= 0))
                    result_range({ 0, a.low >= 0 && b.low >= 0 ? std::min(a.high, b.high) : a.low >= 0 ? a.high : b.high });
                else if (a.low >= 0 && b.low >= 0)
                    result_range({ 0, static_cast<std::int64_t>(std::bit_ceil(static_cast<std::uint64_t>(std::max(a.high, b.high)) + 1u)) - 1 });
                else
                    result_range({});
                slow_call("python::integer_binary", sBinary.at(aConstruct), type::Integer);
            }
            break;
        case construct::LeftShift:
            {
                if (b.kind == operand_info::Constant)
                {
                    if (b.value < 0 || b.value >= 62)
                        always_bail();
                    else
                    {
                        // (a within a range that makes the result a small integer)
                        auto const k = b.value;
                        auto const limit = std::int64_t{ 1 } << (61 - k);
                        check_small({ &a });
                        push(a);
                        narrow(a, -limit, limit - 1);
                        result_range({ a.low * (std::int64_t{ 1 } << k), a.high * (std::int64_t{ 1 } << k) });
                        i64(k); op(opcode::I64Shl);
                    }
                }
                else
                {
                    // (0 <= n <= 62 and (r >> n) == a: r = a << n)
                    auto const x = scratch(ScratchX), n = scratch(ScratchN), r = scratch(ScratchResult);
                    check_small({ &a, &b });
                    // (both operands first: the code of an operand can use the scratch locals)
                    push(a); push(b); i64(1); op(opcode::I64ShrS); set(n); set(x);
                    get(n); i64(62); op(opcode::I64GtU); bail();
                    get(x); get(n); op(opcode::I64Shl); tee(r); get(n); op(opcode::I64ShrS); get(x); op(opcode::I64Ne); bail();
                    get(r);
                    result_range({});
                }
                slow_call("python::integer_binary", PyLeftShift, type::Integer);
            }
            break;
        case construct::RightShift:
            {
                if (b.kind == operand_info::Constant)
                {
                    if (b.value < 0)
                        always_bail();
                    else
                    {
                        auto const k = std::min<std::int64_t>(b.value, 63);
                        check_small({ &a });
                        push(a); i64(k); op(opcode::I64ShrS); i64(-2); op(opcode::I64And);
                        result_range({ a.low >> k, a.high >> k });
                    }
                }
                else
                {
                    // (n >= 0: r = (a >> min(n, 63)) & ~1)
                    auto const n = scratch(ScratchN);
                    check_small({ &a, &b });
                    push(a);
                    push(b); i64(1); op(opcode::I64ShrS); tee(n); i64(0); op(opcode::I64LtS); bail();
                    get(n); i64(63); get(n); i64(63); op(opcode::I64LtU); op(opcode::Select);
                    op(opcode::I64ShrS); i64(-2); op(opcode::I64And);
                    result_range({ a.low < 0 ? a.low : 0, a.high >= 0 ? a.high : -1 });
                }
                slow_call("python::integer_binary", PyRightShift, type::Integer);
            }
            break;
        case construct::Negative:
            {
                check_small({ &b });
                i64(0);
                push(b);
                narrow(b, SmallLow + 1, SmallHigh);
                op(opcode::I64Sub);
                result_range({ -b.high, -b.low });
                slow_call("python::integer_unary", PyNegative, type::Integer);
            }
            break;
        case construct::Invert:
            {
                // (~a: -a - 1)
                check_small({ &b });
                push(b); i64(-2); op(opcode::I64Xor);
                result_range({ -b.high - 1, -b.low - 1 });
                slow_call("python::integer_unary", PyInvert, type::Integer);
            }
            break;
        case construct::Less:
        case construct::LessEqual:
        case construct::Greater:
        case construct::GreaterEqual:
        case construct::Equal:
        case construct::NotEqual:
            {
                // (small: as 64-bit integers (shifted left one bit, their order is kept); equality with a small integer: an integer 
                // object is never equal to one)
                auto const& c = sCompare.at(aConstruct);
                bool const equality = aConstruct == construct::Equal || aConstruct == construct::NotEqual;
                if (!(equality && (a.kind == operand_info::Constant || b.kind == operand_info::Constant)))
                    check_small({ &a, &b });
                // (equality with zero: an expression whose value is zero if a cheaper one is)
                auto const zero_test = [&](operand_info const& o, operand_info const& aOther)
                {
                    return equality && aOther.kind == operand_info::Constant && aOther.value == 0 && o.kind == operand_info::Expression && 
                        !o.expression.zeroFast.fragmentCode.empty();
                };
                if (zero_test(a, b))
                    append(f, a.expression.zeroFast);
                else
                    push(a);
                if (zero_test(b, a))
                    append(f, b.expression.zeroFast);
                else
                    push(b);
                op(c.second);
                slow_call("python::integer_compare", c.first, type::Boolean);
                comparison = true;
            }
            break;
        default:
            throw std::logic_error("neos::language::compiler::py_integer_operation");
        }
        if (comparison)
        {
            fragment result = e.prefix;
            if (!e.bails)
                append(result, e.fast);
            else
            {
                put_block(result, opcode::Block, type::Boolean);
                put_block(result, opcode::Block, {});
                append(result, e.fast);
                put_op(result.fragmentCode.as_std_vector(), opcode::Br);
                put_u32(result.fragmentCode.as_std_vector(), 1u);
                put(result, opcode::End);
                append(result, e.slow);
                put(result, opcode::End);
            }
            result.valueType = type::Boolean;
            result.fragmentKind = fragment_kind::Value;
            push_operand(operand_type{ result });
            return;
        }
        push_operand(operand_type{ py_integer_fragment(std::move(e)) });
    }

    fragment compiler::py_integer_fragment(py_integer_expression aExpression)
    {
        // an int expression as a value (its slow version's result is held by an object temporary: if code takes over the expression 
        // it takes over that result instead (see py_integer_expression_of))
        auto& function = py_function();
        if (aExpression.bails)
        {
            std::uint32_t holder;
            if (!function.freeObjects.empty())
            {
                holder = function.freeObjects.back();
                function.freeObjects.pop_back();
            }
            else
                holder = temporary(type::Object);
            function.pendingObjects.push_back(holder);
            function.objectTemporaries.insert(holder);
            aExpression.holder = holder;
        }
        else
            aExpression.slow = fragment{};
        fragment result = aExpression.prefix;
        append(result, py_integer_value(aExpression, true));
        result.valueType = type::Integer;
        result.fragmentKind = fragment_kind::Value;
        function.pyIntegers[integer_key(result)] = std::move(aExpression);
        return result;
    }

    std::uint32_t compiler::py_integer_temporary()
    {
        // a temporary integer of the statement (reusable after it)
        auto& function = py_function();
        std::uint32_t result;
        if (!function.freeIntegers.empty())
        {
            result = function.freeIntegers.back();
            function.freeIntegers.pop_back();
        }
        else
            result = temporary(type::Integer);
        function.pendingIntegers.push_back(result);
        return result;
    }

    void compiler::py_integer_divide_or_power(construct aConstruct, std::optional<operand_type> const& aLhs, operand_type const& aRhs)
    {
        // a true division (small integers of at most 53 bits: as doubles; otherwise by the runtime plugin) or a power (by the
        // runtime plugin)
        auto const constant = [&](operand_type const& aOperand) -> std::optional<std::int64_t>
        {
            auto const value = int_immediate(aOperand);
            if (!value || *value < ibig{ SmallLow } || ibig{ SmallHigh } < *value)
                return {};
            return integer_cast<std::int64_t>(*value);
        };
        fragment result;
        auto const operand = [&](operand_type const& aOperand) -> fragment
        {
            if (auto const c = constant(aOperand))
            {
                fragment value;
                put_i64(value, *c * 2);
                value.valueType = type::Integer;
                return value;
            }
            auto const local = temporary(type::Integer);
            append(result, materialize(aOperand, type::Integer));
            put_local(result, opcode::LocalSet, local);
            return local_value(local, type::Integer);
        };
        auto const lhs = operand(*aLhs);
        auto const rhs = operand(aRhs);
        auto const operator_code = [](std::int32_t aOperator) { fragment c; put_i32(c, aOperator); c.valueType = type::I32; return c; };
        if (aConstruct == construct::Power)
        {
            append(result, py_park_integer(py_runtime_call("python::integer_binary", { type::I32, type::Integer, type::Integer }, type::Integer,
                { operator_code(PyPower), lhs, rhs })));
            result.valueType = type::Integer;
        }
        else
        {
            auto const x = py_scratch(ScratchX, type::I64);
            auto const y = py_scratch(ScratchY, type::I64);
            auto const op = [&](opcode aOpcode) { put(result, aOpcode); };
            auto const i64 = [&](std::int64_t aValue) { put_i64(result, aValue); };
            // (small: ((a | b) & 1) == 0)
            append(result, lhs); append(result, rhs); op(opcode::I64Ior); i64(1); op(opcode::I64And); op(opcode::I64Eqz);
            append(result, lhs); i64(1); op(opcode::I64ShrS); put_local(result, opcode::LocalTee, x);
            i64(std::int64_t{ 1 } << 53); op(opcode::I64Add); i64(std::int64_t{ 1 } << 54); op(opcode::I64LeU); op(opcode::I32And);
            append(result, rhs); i64(1); op(opcode::I64ShrS); put_local(result, opcode::LocalTee, y);
            i64(std::int64_t{ 1 } << 53); op(opcode::I64Add); i64(std::int64_t{ 1 } << 54); op(opcode::I64LeU); op(opcode::I32And);
            append(result, rhs); i64(0); op(opcode::I64Ne); op(opcode::I32And);
            put_block(result, opcode::If, type::F64);
            put_local(result, opcode::LocalGet, x); op(opcode::F64SConvertI64); put_local(result, opcode::LocalGet, y); op(opcode::F64SConvertI64); op(opcode::F64Div);
            op(opcode::Else);
            append(result, py_runtime_call("python::integer_true_divide", { type::Integer, type::Integer }, type::F64, { lhs, rhs }));
            op(opcode::End);
            result.valueType = type::F64;
        }
        result.fragmentKind = fragment_kind::Value;
        push_operand(operand_type{ result });
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
        if (declaration_pass())
        {
            fragment dummy;
            dummy.valueType = type::Object;
            return dummy;
        }
        auto* const symbol = find_symbol(current_scope(), neolib::string_view{ neolib::string{ aName } });
        // (the module's name and file)
        if (symbol == nullptr && (aName == "__name__" || aName == "__file__") && !py_exec_mode())
        {
            std::string value = py_module_name();
            if (aName == "__file__")
            {
                auto const m = py_current_module();
                if (m)
                    value = iPyModules[*m].path;
                else if (!iFragments.empty() && iFragments.front().source != nullptr && iFragments.front().source->source_file_path().has_value())
                {
                    std::error_code ec;
                    value = std::filesystem::absolute(iFragments.front().source->source_file_path().value().to_std_string(), ec).generic_string();
                }
            }
            return py_owned(py_runtime_call("python::from_str", { type::String }, type::Object, { py_string(value) }));
        }
        // a function of the program as a value: a function object (python::function) that calls it
        if (symbol == nullptr || symbol->type() == type::Function)
            for (std::size_t d = 0u; d < generator().forward_declaration_count(); ++d)
                if (generator().forward_declaration_name(d).to_std_string_view() == aName && !generator().forward_declaration_stub(d) && iPyParameters.contains(aName) &&
                    !iPyPackageFunctions.contains(aName))
                {
                    struct { function_signature signature; } const declaration{ function_signature{ generator().forward_declaration_signature(d) } };
                    auto const type_code = [](type aType) -> char
                    {
                        if (aType == type::Object) return 'O';
                        if (aType == type::Boolean) return 'B';
                        if (aType == type::String) return 'S';
                        if (aType == type::Void) return 'V';
                        if (is_float(aType)) return 'F';
                        if (aType == type::Integer) return 'I';
                        return 'L';
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
                    for (std::size_t pi = 0u; pi < parameters.size(); ++pi)
                    {
                        auto const& parameter = parameters[pi];
                        if (!signature.empty())
                            signature += ',';
                        if (pi > 0u && parameters[pi - 1u].positionalOnly && !parameter.positionalOnly)
                            signature += "/,";
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
                            generator().declare_globals();
                            auto* const global = find_symbol(generator().global_scope(), neolib::string_view{ neolib::string{ aName + "." + parameter.name } }, true);
                            fragment value;
                            if (global != nullptr)
                            {
                                put_local(value, opcode::GlobalGet, global->index());
                                value.valueType = global->type();
                            }
                            else
                            {
                                // (defined later: by the next compilation pass)
                                { set_changed(); if (std::getenv("NEOS_PY_DEBUG")) std::fprintf(stderr, "changed at %d\n", __LINE__); }
                                value = py_runtime_call("python::none", {}, type::Object, {});
                            }
                            defaults.push_back(value);
                        }
                    }
                    if (!parameters.empty() && parameters.back().positionalOnly)
                        signature += ",/";
                    signature += ';';
                    signature += type_code(declaration.signature.return_type());
                    // (a generator function, a coroutine function or an async generator function: after a file separator)
                    if (auto const flavour = iPyFlavours.find(aName); flavour != iPyFlavours.end())
                        signature += std::string{ '\x1c' } + flavour->second;
                    // (its annotations' sources: after group separators)
                    if (auto const a = iPyAnnotations.find(aName); a != iPyAnnotations.end())
                        for (auto const& entry : a->second)
                            signature += '\x1d' + entry.first + '\x1f' + entry.second;
                    // (its docstring: after a record separator)
                    if (auto const doc = iPyDocstrings.find(aName); doc != iPyDocstrings.end())
                        signature += '\x1e' + doc->second;
                    // (the function's index: a relocation)
                    fragment index;
                    auto& code = index.fragmentCode.as_std_vector();
                    put_op(code, opcode::I32Const);
                    index.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()),
                        generator().deferred_call(neolib::string{ aName }, neolib::vector<type>{ parameterTypes.begin(), parameterTypes.end() }) });
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
                    auto const shown = unprefixed(aName);
                    bool const lambda = shown.size() > 3u && shown.starts_with("_l") && shown.ends_with("_") &&
                        std::all_of(shown.begin() + 2, shown.end() - 1, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
                    // (the name of a function of an imported module keeps its prefix: the runtime finds its module from it)
                    std::string const name = lambda ? aName.substr(0u, aName.size() - shown.size()) + "<lambda>" : aName;
                    return py_owned(py_runtime_call("python::function", { type::String, type::I32, type::String, type::Object }, type::Object,
                        { py_string(name), index, py_string(signature), defaultsValue }));
                }
        bool const inFunction = in_function() && !implicit_entry();
        if (symbol == nullptr && inFunction && iInference.pyBound.contains(current_function_name()) && iInference.pyBound.at(current_function_name()).contains(aName) &&
            !(iInference.pyGlobals.contains(current_function_name()) && iInference.pyGlobals.at(current_function_name()).contains(aName)))
        {
            // (a local variable read before it is bound: UnboundLocalError)
            fragment kind;
            put_i32(kind, 1);
            kind.valueType = type::I32;
            auto value = py_runtime_call("python::unbound", { type::String, type::I32 }, type::Void, { py_string(aName), kind });
            append(value, py_runtime_call("python::none", {}, type::Object, {}));
            value.valueType = type::Object;
            return py_owned(value);
        }
        if (symbol == nullptr && inFunction && !builtin_names().contains(aName) &&
            (!iInference.pyFree.contains(aName) || iInference.pyModuleNames.contains(aName)))
        {
            // (a global: the next compilation pass declares it)
            iInference.pyFree.insert(aName);
            { set_changed(); if (std::getenv("NEOS_PY_DEBUG")) std::fprintf(stderr, "changed at %d\n", __LINE__); }
            fragment dummy;
            put(dummy, opcode::Unreachable);
            dummy.valueType = type::Object;
            return dummy;
        }
        // (exec: a name of its namespaces (its locals', its globals') or a builtin)
        if (py_exec_mode())
            return py_owned(py_runtime_call("python::exec_lookup", { type::String }, type::Object, { py_string(aName) }));
        // (a module whose namespace (globals()) can bind names: one of those, or a builtin)
        if (iPyIntrospective.contains(py_module_name()))
            return py_owned(py_runtime_call("python::global_lookup", { type::String, type::String }, type::Object, { py_string(py_module_name()), py_string(aName) }));
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
        bool const module = implicit_entry();
        auto t = kind_of(aType, py_big_integers());
        if (t == type::Void)
            t = type::Object;
        if (!module)
            if (auto const declared = iInference.pyGlobals.find(current_function_name()); declared != iInference.pyGlobals.end() && declared->second.contains(aName))
            {
                // (declared global)
                generator().function_global(neolib::string{ aName });
                if (auto* const global = find_symbol(generator().global_scope(), neolib::string_view{ name }, true); global != nullptr && is_global(*global))
                    return *global;
                for (std::size_t g = 0u; g < generator().global_count(); ++g)
                    if (generator().global_name(g).to_std_string_view() == aName)
                        t = generator().global_type(g);
                return define_global(aName, py_exec_mode() ? type::Object : t);
            }
        if (module && py_exec_mode())
            t = type::Object;
        if (module)
        {
            iInference.pyModuleNames.insert(aName);
            // a top-level variable that functions use is a global variable
            if (auto* const global = find_symbol(generator().global_scope(), neolib::string_view{ name }, true); global != nullptr && is_global(*global))
            {
                generator().function_global(neolib::string{ aName });
                return *global;
            }
            if (iInference.pyFree.contains(aName))
            {
                generator().function_global(neolib::string{ aName });
                for (std::size_t g = 0u; g < generator().global_count(); ++g)
                    if (generator().global_name(g).to_std_string_view() == aName)
                        t = generator().global_type(g);
                return define_global(aName, py_exec_mode() ? type::Object : t);
            }
        }
        // (a variable that holds an object or whose type changes is declared at the start of its function)
        auto const& functionName = current_function_name();
        auto const predeclared = iInference.pyLocals.find(functionName);
        if (predeclared != iInference.pyLocals.end())
            if (auto const v = predeclared->second.find(aName); v != predeclared->second.end())
                t = v->second;
        generator().define_symbol(name, t, neolib::string_view{});
        auto& entry = *find_symbol(current_scope(), neolib::string_view{ name }, true);
        if (t == type::Integer)
            if (auto const function = iInference.pyCounters.find(functionName); function != iInference.pyCounters.end())
                if (auto const counter = function->second.find(aName); counter != function->second.end())
                {
                    // (only for loops over ranges bind it: a small integer within their range (its integer is never an object to 
                    // release))
                    py_function().pyCounters[entry.index()] = { counter->second.low, counter->second.high };
                    return entry;
                }
        if (t == type::Integer)
        {
            // (an int's integer object is released when the function returns)
            py_function().integerVariables.push_back(entry.index());
            if (py_function().returns && !module)
            {
                iInference.pyLocals[functionName][aName] = type::Integer;
                { set_changed(); if (std::getenv("NEOS_PY_DEBUG")) std::fprintf(stderr, "changed at %d\n", __LINE__); }
            }
        }
        if (t == type::Object)
        {
            py_function().objectVariables.push_back(entry.index());
            // (a function's return statements release its objects: one that precedes this variable's definition has not)
            if (py_function().returns && !module)
            {
                iInference.pyLocals[functionName][aName] = type::Object;
                { set_changed(); if (std::getenv("NEOS_PY_DEBUG")) std::fprintf(stderr, "changed at %d\n", __LINE__); }
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
            for (std::size_t g = 0u; g < generator().global_count(); ++g)
                if (generator().global_name(g).to_std_string_view() == aName)
                    generator().set_global_type(g, type::Object);
        }
        else
        {
            auto const& signature = generator().signature_of(current_function());
            if (signature.parameters().has_value())
                for (auto const& parameter : signature.parameters().value())
                    if (parameter.parameter_name().to_std_string() == aName)
                        throw std::runtime_error("cannot bind a value of another type to parameter '" + aName + "' (of type " + type_name(parameter.parameter_type()) + ")");
            auto& t = iInference.pyLocals[current_function_name()][aName];
            if (t == type::Object)
                throw std::runtime_error("cannot bind '" + aName + "'");
            t = type::Object;
        }
        { set_changed(); if (std::getenv("NEOS_PY_DEBUG")) std::fprintf(stderr, "changed at %d\n", __LINE__); }
    }

    void compiler::py_bound(std::string const& aName, std::optional<interval> const& aCounter)
    {
        // a variable of the current function is bound (by a for loop over a range (aCounter: the range of the values), or else by 
        // something else)
        if (declaration_pass() || !in_function())
            return;
        auto& bindings = iInference.pyCounterBindings[current_function_name()];
        auto const existing = bindings.find(aName);
        if (existing == bindings.end())
            bindings[aName] = aCounter;
        else if (existing->second && aCounter)
            existing->second = existing->second->join(*aCounter);
        else
            existing->second = std::nullopt;
    }

    void compiler::py_update_counters()
    {
        // the int variables that only for loops over ranges bind (if they are not those of the previous pass, another pass)
        std::map<std::string, std::map<std::string, interval>> counters;
        for (auto const& function : iInference.pyCounterBindings)
            for (auto const& variable : function.second)
                if (variable.second)
                    counters[function.first][variable.first] = *variable.second;
        bool same = counters.size() == iInference.pyCounters.size();
        for (auto const& function : counters)
        {
            auto const previous = iInference.pyCounters.find(function.first);
            if (!same || previous == iInference.pyCounters.end() || previous->second.size() != function.second.size())
            {
                same = false;
                break;
            }
            for (auto const& variable : function.second)
            {
                auto const p = previous->second.find(variable.first);
                if (p == previous->second.end() || p->second.low != variable.second.low || p->second.high != variable.second.high)
                    same = false;
            }
        }
        if (!same)
        {
            iInference.pyCounters = counters;
            set_changed();
        }
    }

    std::optional<std::pair<std::int64_t, std::int64_t>> compiler::py_counter(std::uint32_t aLocal)
    {
        // (a variable that only for loops over ranges bind: the range of its values)
        if (!in_function())
            return {};
        auto const& counters = py_function().pyCounters;
        auto const counter = counters.find(aLocal);
        if (counter == counters.end())
            return {};
        return counter->second;
    }

    void compiler::py_bind_name(std::string const& aName, operand_type const& aValue, fragment& aStatement)
    {
        py_bound(aName);
        if (!in_function() && !declaration_pass())
            ensure_function();
        if (in_function() && !declaration_pass())
        {
            if (auto& order = py_function().pyBindingOrder; std::find(order.begin(), order.end(), aName) == order.end())
                order.push_back(aName);
            iInference.pyBound[current_function_name()].insert(aName);
        }
        auto const valueType = kind_of(py_type(aValue), py_big_integers());
        auto& variable = py_variable(aName, valueType);
        bool const global = is_global(variable);
        auto const variableType = variable.type();
        auto const index = variable.index();
        if (variableType == type::Object)
        {
            auto value = py_object(aValue);
            auto& function = py_function();
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
        if (valueType != kind_of(variableType, py_big_integers()))
        {
            py_kind_change(aName);
            put(aStatement, opcode::Unreachable);
            return;
        }
        if (variableType == type::Integer)
        {
            // (the variable holds a reference of its own to an integer object: retained; the integer it held is released)
            auto value = materialize(aValue, type::Integer);
            if (auto const expression = py_integer_expression_of(value))
            {
                // (an int expression: its fast version's result is a small integer (as is the variable's integer, if the 
                // expression checks that); its slow version's result is a new reference (the variable's))
                auto const get = global ? opcode::GlobalGet : opcode::LocalGet;
                auto const set = global ? opcode::GlobalSet : opcode::LocalSet;
                bool const checked = !global && expression->small.contains(index);
                append(aStatement, expression->prefix);
                if (!expression->bails && checked)
                {
                    append(aStatement, expression->fast);
                    put_local(aStatement, set, index);
                    return;
                }
                put_block(aStatement, opcode::Block, {});
                put_block(aStatement, opcode::Block, {});
                append(aStatement, expression->fast);
                if (!checked)
                {
                    put_local(aStatement, get, index);
                    put_i64(aStatement, 1);
                    put(aStatement, opcode::I64And);
                    put(aStatement, opcode::I32ConvertI64);
                    put_op(aStatement.fragmentCode.as_std_vector(), opcode::BrIf);
                    put_u32(aStatement.fragmentCode.as_std_vector(), 0u);
                }
                put_local(aStatement, set, index);
                put_op(aStatement.fragmentCode.as_std_vector(), opcode::Br);
                put_u32(aStatement.fragmentCode.as_std_vector(), 1u);
                put(aStatement, opcode::End);
                auto const scratch = py_scratch(ScratchBind, type::Integer);
                append(aStatement, expression->bails ? expression->slow : expression->fast);
                put_local(aStatement, opcode::LocalSet, scratch);
                append(aStatement, py_release_integer(index, global));
                put_local(aStatement, opcode::LocalGet, scratch);
                put_local(aStatement, set, index);
                put(aStatement, opcode::End);
                return;
            }
            auto const scratch = py_scratch(ScratchBind, type::Integer);
            append(aStatement, value);
            put_local(aStatement, opcode::LocalTee, scratch);
            // (either an integer object: ((new | old) & 1) != 0)
            put_local(aStatement, global ? opcode::GlobalGet : opcode::LocalGet, index);
            put(aStatement, opcode::I64Ior);
            put_i64(aStatement, 1);
            put(aStatement, opcode::I64And);
            put(aStatement, opcode::I32ConvertI64);
            put_block(aStatement, opcode::If, {});
            append(aStatement, py_retain_integer(scratch));
            append(aStatement, py_release_integer(index, global));
            put(aStatement, opcode::End);
            put_local(aStatement, opcode::LocalGet, scratch);
            put_local(aStatement, global ? opcode::GlobalSet : opcode::LocalSet, index);
            return;
        }
        append(aStatement, materialize(aValue, variableType));
        put_local(aStatement, global ? opcode::GlobalSet : opcode::LocalSet, index);
    }

    fragment compiler::py_unpack_call(fragment const& aValue, std::vector<fragment_state::py_target> const& aTargets)
    {
        // (the values of an iterable bound to targets: a tuple of them; a starred target's values are a list)
        fragment count;
        put_i64(count, static_cast<std::int64_t>(aTargets.size()));
        count.valueType = type::I64;
        auto const starred = std::find_if(aTargets.begin(), aTargets.end(), [](auto const& t) { return t.starred; });
        if (starred == aTargets.end())
            return py_runtime_call("python::unpack", { type::Object, type::I64 }, type::Object, { aValue, count });
        if (std::count_if(aTargets.begin(), aTargets.end(), [](auto const& t) { return t.starred; }) > 1)
            throw std::runtime_error("multiple starred expressions in assignment");
        fragment index;
        put_i64(index, static_cast<std::int64_t>(starred - aTargets.begin()));
        index.valueType = type::I64;
        return py_runtime_call("python::unpack_ex", { type::Object, type::I64, type::I64 }, type::Object, { aValue, count, index });
    }

    void compiler::py_bind(fragment_state::py_target const& aTarget, operand_type const& aValue, fragment& aStatement)
    {
        switch (aTarget.kind)
        {
        case fragment_state::py_target::kind_t::Name:
            py_bind_name(aTarget.name, aValue, aStatement);
            break;
        case fragment_state::py_target::kind_t::Item:
            append(aStatement, py_runtime_call("python::setitem", { type::Object, type::Object, type::Object }, type::Void,
                { *aTarget.receiver, *aTarget.key, py_object(aValue) }));
            break;
        case fragment_state::py_target::kind_t::Attribute:
            append(aStatement, py_runtime_call("python::setattr", { type::Object, type::String, type::Object }, type::Void,
                { *aTarget.receiver, py_string(aTarget.name), py_object(aValue) }));
            break;
        case fragment_state::py_target::kind_t::Group:
            py_bind_targets(aTarget.children, aValue, aStatement);
            break;
        }
    }

    void compiler::py_bind_targets(std::vector<fragment_state::py_target> const& aTargets, operand_type const& aValue, fragment& aStatement)
    {
        // (the values of an iterable bound to targets)
        auto const sequence = temporary(type::I32);
        append(aStatement, py_owned(py_unpack_call(py_object(aValue), aTargets)));
        put_local(aStatement, opcode::LocalSet, sequence);
        for (std::size_t i = 0u; i < aTargets.size(); ++i)
        {
            fragment index;
            put_i64(index, static_cast<std::int64_t>(i));
            index.valueType = type::I64;
            auto const item = py_owned(py_runtime_call("python::tuple_get", { type::Object, type::I64 }, type::Object, { local_value(sequence, type::Object), index }));
            py_bind(aTargets[i], operand_type{ item }, aStatement);
        }
    }

    void compiler::py_declare_targets(std::vector<fragment_state::py_target> const& aTargets)
    {
        // (the names that targets bind are objects (e.g. a loop's): defined before they are bound)
        for (auto const& target : aTargets)
            if (target.kind == fragment_state::py_target::kind_t::Name)
            {
                auto& variable = py_variable(target.name, type::Object);
                if (variable.type() != type::Object)
                    py_kind_change(target.name);
            }
            else if (target.kind == fragment_state::py_target::kind_t::Group)
                py_declare_targets(target.children);
    }

    // --- operations

    void compiler::py_operation(construct aConstruct, bool aInplace, neolib::i_string_view const* aSource)
    {
        bool const unary = (aConstruct == construct::Not || aConstruct == construct::Negative || aConstruct == construct::Positive || aConstruct == construct::Invert);
        auto rhs = py_pop();
        std::optional<operand_type> lhs;
        if (!unary)
            lhs = py_pop();
        if (declaration_pass())
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
            auto const text = generator().current_fragment().source().to_std_string_view();
            auto const tail = aSource->to_std_string_view();
            auto next = static_cast<std::size_t>(tail.data() + tail.size() - text.data());
            while (next < text.size() && (text[next] == ' ' || text[next] == '\t'))
                ++next;
            auto const rest = text.substr(std::min(next, text.size()));
            auto const word = [&](std::string_view aWord) { return rest.starts_with(aWord) && (rest.size() == aWord.size() || !name_character(rest[aWord.size()])); };
            bool const follows = rest.starts_with("<") || rest.starts_with(">") || rest.starts_with("==") || rest.starts_with("!=") || 
                word("in") || word("not") || word("is");
            if (follows && !declaration_pass())
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
                operand_type const result{ operands().back() };
                operands().pop_back();
                push_operand(operand_type{ *chainedTo });
                push_operand(result);
                emit_python(construct::And, *aSource);
            }
            if (chainsTo)
            {
                fragment result{ operands().back().get<i_fragment>() };
                result.fragmentSource = *aSource;
                operands().set_back(operand_type{ result });
                state().pyChains[aSource->to_std_string_view().data()] = *chainsTo;
            }
            return;
        }
        auto const rt = kind_of(py_type(rhs), py_big_integers());
        auto const lt = lhs ? kind_of(py_type(*lhs), py_big_integers()) : rt;
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
        // ints (of any size) and bools: as integers (type::Integer)
        auto const int_like = [](type t) { return t == type::Integer || t == type::Boolean || is_integer(t); };
        if (native && int_like(lt) && int_like(rt) && py_big_integers())
        {
            bool const booleans = lt == type::Boolean && rt == type::Boolean;
            switch (aConstruct)
            {
            case construct::Add:
            case construct::Subtract:
            case construct::Multiply:
            case construct::Divide:
            case construct::FloorDivide:
            case construct::Modulo:
            case construct::LeftShift:
            case construct::RightShift:
            case construct::Less:
            case construct::LessEqual:
            case construct::Greater:
            case construct::GreaterEqual:
            case construct::Equal:
            case construct::NotEqual:
            case construct::Negative:
            case construct::Positive:
            case construct::Invert:
                py_integer_operation(aConstruct, lhs, rhs);
                return;
            case construct::BitAnd:
            case construct::BitOr:
            case construct::BitXor:
                if (!booleans)
                {
                    py_integer_operation(aConstruct, lhs, rhs);
                    return;
                }
                break;
            case construct::Power:
                if (auto const exponent = int_immediate(rhs); exponent && !(*exponent < ibig{ 0 }))
                {
                    if (int_immediate(*lhs) || *exponent != ibig{ 2 })
                    {
                        py_integer_operation(aConstruct, lhs, rhs);
                        return;
                    }
                    // (x ** 2: x * x)
                    auto x = materialize(*lhs, type::Integer);
                    auto const& code = x.fragmentCode.as_std_vector();
                    if (x.fragmentRelocations.empty() && code.size() >= 2u && code[0] == 0x20u && code.back() < 0x80u &&
                        std::all_of(code.begin() + 1, code.end() - 1, [](std::uint8_t b) { return b >= 0x80u; }))
                    {
                        // (a variable)
                        py_integer_operation(construct::Multiply, operand_type{ x }, operand_type{ x });
                        return;
                    }
                    auto const local = temporary(type::Integer);
                    put_local(x, opcode::LocalSet, local);
                    py_integer_operation(construct::Multiply, operand_type{ local_value(local, type::Integer) }, operand_type{ local_value(local, type::Integer) });
                    fragment square{ operands().back().get<i_fragment>() };
                    operands().pop_back();
                    fragment result = x;
                    append(result, square);
                    result.valueType = type::Integer;
                    result.fragmentKind = fragment_kind::Value;
                    value(result);
                    return;
                }
                break;
            case construct::Is:
            case construct::IsNot:
                if (lt == type::Integer && rt == type::Integer)
                {
                    py_integer_operation(aConstruct == construct::Is ? construct::Equal : construct::NotEqual, lhs, rhs);
                    return;
                }
                break;
            default:
                break;
            }
        }
        // (a float with an int or a bool: as floats)
        if (native && (is_float(lt) || is_float(rt)) && is_number(lt) && is_number(rt) && aConstruct != construct::Is && aConstruct != construct::IsNot)
        {
            if (lhs && !is_float(lt))
                lhs = operand_type{ materialize(*lhs, type::F64) };
            if (!is_float(rt) && !(aConstruct == construct::Power && is_immediate(rhs)))
                rhs = operand_type{ materialize(rhs, type::F64) };
        }
        bool const numbers = native && is_number(lt) && is_number(rt);
        bool const integers = native && (is_integer(lt) || lt == type::Boolean) && (is_integer(rt) || rt == type::Boolean) && !(lt == type::Boolean && rt == type::Boolean);
        switch (aConstruct)
        {
        case construct::Add:
            if (native && lt == type::String && rt == type::String && rhs.holds_alternative<i_fragment>() && in_function())
                if (auto const integer = py_function().pyIntegerStrings.find(integer_key(fragment{ rhs.get<i_fragment>() })); 
                    integer != py_function().pyIntegerStrings.end())
                {
                    // (a string followed by an int's str(): the int is formatted into the result (a small integer: the VM's
                    // neos::string_concat_i64))
                    auto const left = temporary(type::String);
                    auto const x = py_scratch(ScratchCoerce, type::Integer);
                    fragment result = materialize(*lhs, type::String);
                    put_local(result, opcode::LocalSet, left);
                    append(result, integer->second);
                    put_local(result, opcode::LocalSet, x);
                    put_is_object(result, x);
                    put_block(result, opcode::If, type::String);
                    put_local(result, opcode::LocalGet, left);
                    append(result, py_runtime_call("python::integer_str", { type::Integer }, type::String, { local_value(x, type::Integer) }));
                    emit_call_to(result, runtime_function("neos::string_concat", { type::String, type::String }, type::String));
                    put(result, opcode::Else);
                    put_local(result, opcode::LocalGet, left);
                    put_local(result, opcode::LocalGet, x);
                    put_i64(result, 1);
                    put(result, opcode::I64ShrS);
                    emit_call_to(result, runtime_function("neos::string_concat_i64", { type::String, type::I64 }, type::String));
                    put(result, opcode::End);
                    result.valueType = type::String;
                    result.fragmentKind = fragment_kind::Value;
                    value(result);
                    break;
                }
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
                auto x = materialize(*lhs, lt == type::Boolean ? type::I64 : kind_of(lt, py_big_integers()));
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
        if (operands().empty())
            throw std::runtime_error("function expected");
        operand_type const callee{ operands().back() };
        operands().pop_back();
        if (declaration_pass())
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
        // (exec: a builtin that its code does not define is its namespaces' (e.g. their __builtins__'s))
        std::optional<std::string> execBuiltin;
        if (callee.holds_alternative<i_symbol_reference>() && !anyStar)
        {
            auto const& reference = callee.get<i_symbol_reference>();
            auto const name = reference.name().to_std_string();
            auto const* symbol = find_symbol(current_scope(), neolib::string_view{ reference.name() });
            struct forward_declaration
            {
                function_signature signature;
                bool stub = false;
            };
            std::optional<forward_declaration> declared;
            for (std::size_t d = 0u; d < generator().forward_declaration_count(); ++d)
                if (generator().forward_declaration_name(d).to_std_string_view() == name)
                    declared = forward_declaration{ function_signature{ generator().forward_declaration_signature(d) }, generator().forward_declaration_stub(d) };
            // (the namespaces: globals(), locals(), vars(), dir(); exec and eval without them: the caller's)
            bool const introspection = symbol == nullptr && !(declared && !declared->stub);
            if (introspection && arguments.empty() && (name == "globals" || ((name == "locals" || name == "vars" || name == "dir") && (!in_function() || implicit_entry()))))
            {
                auto value = py_module_namespace();
                if (name == "dir")
                    value = py_owned(py_runtime_call("python::sorted_keys", { type::Object }, type::Object, { value }));
                push_operand(operand_type{ value });
                return;
            }
            if (introspection && arguments.empty() && (name == "locals" || name == "vars" || name == "dir"))
            {
                auto value = py_locals();
                if (name == "dir")
                    value = py_owned(py_runtime_call("python::sorted_keys", { type::Object }, type::Object, { value }));
                push_operand(operand_type{ value });
                return;
            }
            if (introspection && (name == "exec" || name == "eval") && arguments.size() == 1u && !anyKeyword)
            {
                arguments.push_back(py_module_namespace());
                arguments.back().fragmentKind = fragment_kind::Argument;
                keywords.push_back(std::nullopt);
                stars.push_back(0);
                if (in_function() && !implicit_entry())
                {
                    arguments.push_back(py_locals());
                    arguments.back().fragmentKind = fragment_kind::Argument;
                    keywords.push_back(std::nullopt);
                    stars.push_back(0);
                }
            }
            if (py_exec_mode() && builtin_names().contains(name) && std::find(iPyExecDefs.begin(), iPyExecDefs.end(), name) == iPyExecDefs.end() &&
                (symbol == nullptr || symbol->type() == type::Function))
                execBuiltin = name;
            // (str() of an int: a conversion)
            if (!execBuiltin && name == "str" && arguments.size() == 1u && !anyKeyword && declared && declared->stub && !py_big_integers() && 
                is_integer(arguments[0].valueType) && arguments[0].valueType != type::Integer)
            {
                // (64-bit ints: decimal (the VM's neos::string_from_i64))
                fragment value = arguments[0];
                if (value.valueType != type::I64)
                    convert(value.fragmentCode.as_std_vector(), value.valueType, type::I64);
                emit_call_to(value, runtime_function("neos::string_from_i64", { type::I64 }, type::String));
                value.valueType = type::String;
                value.fragmentKind = fragment_kind::Value;
                value.fragmentSource = make_fragment(fragment_kind::Value, type::String, aSource).fragmentSource;
                push_operand(operand_type{ value });
                return;
            }
            if (!execBuiltin && name == "str" && arguments.size() == 1u && !anyKeyword && arguments[0].valueType == type::Integer && declared && declared->stub)
            {
                fragment value = arguments[0];
                coerce(value, type::String);
                value.fragmentKind = fragment_kind::Value;
                value.fragmentSource = make_fragment(fragment_kind::Value, type::String, aSource).fragmentSource;
                push_operand(operand_type{ value });
                return;
            }
            if (!execBuiltin && ((symbol != nullptr && symbol->type() == type::Function) || (symbol == nullptr && declared)))
            {
                // a function of the program (or a builtin that the Python runtime plugin provides as a function)
                auto ordered = arguments;
                bool viable = true;
                std::optional<std::vector<fragment>> bound;
                bool mismatched = false;
                if (declared && !declared->stub)
                {
                    try
                    {
                        bound = py_bind_arguments(name, arguments, keywords);
                    }
                    catch (std::runtime_error const&)
                    {
                        // (the arguments do not match: the call raises TypeError when it is made (a call of the function's object))
                        mismatched = true;
                    }
                }
                if (mismatched)
                    viable = false;
                else
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
                    auto const base = operands().size();
                    try
                    {
                        for (auto const& argument : ordered)
                            push_operand(operand_type{ argument });
                        generator().emit_call(neolib::string_view{ neolib::string{ name } }, aSource);
                        operand_type result{ operands().back() };
                        if (result.holds_alternative<i_fragment>() && result.get<i_fragment>().value_type() == type::Object)
                            operands().set_back(operand_type{ py_owned(fragment{ result.get<i_fragment>() }) }); // (a function returns a new reference)
                        else if (result.holds_alternative<i_fragment>() && result.get<i_fragment>().value_type() == type::Integer)
                        {
                            result = operand_type{ py_park_integer(fragment{ result.get<i_fragment>() }) };
                            if (!py_big_integers())
                                result = operand_type{ materialize(result, type::I64) }; // (64-bit ints: a runtime plugin's int is one)
                            operands().set_back(result);
                        }
                        return;
                    }
                    catch (std::runtime_error const&)
                    {
                        operands().resize(base);
                        if (!builtin_names().contains(name))
                            throw;
                    }
                }
                else if (!mismatched && !builtin_names().contains(name))
                    throw std::runtime_error("invalid keyword arguments in call to '" + name + "'");
            }
        }
        // a call of an object: callable(*args, **kwargs)
        auto statement = make_fragment(fragment_kind::Value, type::Object, aSource);
        auto const callable = temporary(type::I32);
        if (execBuiltin)
            append(statement, py_owned(py_runtime_call("python::exec_lookup", { type::String }, type::Object, { py_string(*execBuiltin) })));
        else
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
        using py_target = fragment_state::py_target;
        auto const text = aSource.to_std_string_view();
        std::vector<py_target> targets;
        auto& pending = state().pyTargets;
        while (!pending.empty() && pending.back().position >= text.data() && pending.back().position < text.data() + text.size())
        {
            targets.insert(targets.begin(), pending.back());
            pending.pop_back();
        }
        if (operands().empty())
            throw std::runtime_error("expression expected");
        operand_type const value{ operands().back() };
        operands().pop_back();
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        if (declaration_pass() || targets.empty())
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
                {
                    // (exec: a name that the code does not bind is its namespaces')
                    if (!py_exec_mode())
                        throw std::runtime_error("name '" + target.name + "' is not defined");
                    current = operand_type{ py_owned(py_runtime_call("python::exec_lookup", { type::String }, type::Object, { py_string(target.name) })) };
                }
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
            operand_type const result{ operands().back() };
            operands().pop_back();
            py_bind(target, result, statement);
        }
        else
        {
            // targets = value
            auto equals = top_level(text, '=');
            std::erase_if(equals, [&](std::size_t p) { return (p + 1u < text.size() && text[p + 1u] == '=') || (p > 0u && std::string_view{ "=<>!:" }.find(text[p - 1u]) != std::string_view::npos); });
            if (equals.size() > 1u)
            {
                // a = b = value: the value is evaluated once, then bound to each target list in turn
                auto v = materialize(value);
                auto const t = v.valueType;
                auto const local = temporary(t == type::Object ? type::I32 : t);
                append(statement, v);
                put_local(statement, opcode::LocalSet, local);
                for (std::size_t list = 0u; list < equals.size(); ++list)
                {
                    auto const begin = list == 0u ? 0u : equals[list - 1u] + 1u;
                    auto const end = equals[list];
                    std::vector<py_target> group;
                    for (auto const& target : targets)
                        if (target.position >= text.data() + begin && target.position < text.data() + end)
                            group.push_back(target);
                    if (group.empty())
                        continue;
                    if (group.size() > 1u || !top_level(text.substr(begin, end - begin), ',').empty())
                        py_bind_targets(group, operand_type{ local_value(local, t) }, statement);
                    else
                        py_bind(group[0], operand_type{ local_value(local, t) }, statement);
                }
                push_operand(operand_type{ statement });
                return;
            }
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
                        if (v.valueType == type::Object && !held_by(v, py_function().pendingObjects))
                        {
                            // (a variable's object (borrowed): a reference of its own (a, b = b, a: binding a releases its object))
                            auto const scratch = temporary(type::I32);
                            put_local(v, opcode::LocalTee, scratch);
                            emit_call_to(v, runtime_function("neos::object_retain", { type::Object }, type::Void));
                            put_local(v, opcode::LocalGet, scratch);
                            v.valueType = type::Object;
                            v = py_owned(v);
                        }
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
                    append(statement, py_owned(py_unpack_call(py_object(value), targets)));
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
        if (declaration_pass())
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
        // a module: a source file's (see py_module_of), one of Python's standard library that the Python runtime plugin implements
        // (python::import), or the runtime plugin of the same name's (its function "module"); ModuleNotFoundError if there is none
        auto import_one = [&](std::string const& aName) -> fragment
        {
            if (auto const m = py_module_of(aName))
            {
                auto const& module = iPyModules[*m];
                fragment number;
                put_i32(number, static_cast<std::int32_t>(module.number));
                number.valueType = type::I32;
                return py_owned(py_runtime_call("python::import_module", { type::String, type::I32, type::Object, type::Object, type::String }, type::Object,
                    { py_string(aName), number, py_name(module.init()), py_name(module.getter()), py_string(module.path) }));
            }
            auto const top = aName.substr(0u, aName.find('.'));
            if (standard_modules().contains(top))
            {
                auto result = py_owned(py_runtime_call("python::import", { type::String }, type::Object, { py_string(top) }));
                // (a submodule of one (e.g. os.path): its attribute)
                std::size_t from = top.size();
                while (from < aName.size())
                {
                    auto const next = aName.find('.', from + 1u);
                    auto const part = aName.substr(from + 1u, next == std::string::npos ? std::string::npos : next - from - 1u);
                    result = py_owned(py_runtime_call("python::getattr", { type::Object, type::String }, type::Object, { result, py_string(part) }));
                    from = next == std::string::npos ? aName.size() : next;
                }
                return result;
            }
            if (aName.find('.') == std::string::npos && generator().runtime_plugin_available(neolib::string{ aName }))
            {
                auto context = py_owned(py_runtime_call("python::context", {}, type::Object, {}));
                return py_owned(py_runtime_call(aName + "::module", { type::Object }, type::Object, { context }));
            }
            return py_owned(py_runtime_call("python::module_not_found", { type::String }, type::Object, { py_string(aName) }));
        };
        // (import a.b.c: a, a.b and a.b.c are imported; the value: a.b.c)
        auto module = [&](std::vector<std::string> const& aPath, bool aTop = false) -> fragment
        {
            if (!py_module_of(aPath[0]))
            {
                // (not a source file's: import a.b binds a)
                std::string full;
                for (std::size_t i = 0u; i < (aTop ? 1u : aPath.size()); ++i)
                    full += (i == 0u ? "" : ".") + aPath[i];
                return import_one(full);
            }
            std::string name;
            fragment result;
            std::optional<fragment> top;
            for (std::size_t i = 0u; i < aPath.size(); ++i)
            {
                name += (i == 0u ? "" : ".") + aPath[i];
                result = import_one(name);
                if (i == 0u && aTop && aPath.size() > 1u)
                {
                    // (the value: the package (a temporary))
                    auto const t = temporary(type::I32);
                    put_local(result, opcode::LocalTee, t);
                    top = local_value(t, type::Object);
                    append(statement, result);
                    put(statement, opcode::Drop);
                    continue;
                }
                if (i + 1u < aPath.size())
                {
                    append(statement, result);
                    put(statement, opcode::Drop);
                }
            }
            if (top)
            {
                append(statement, result);
                put(statement, opcode::Drop);
                return *top;
            }
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
        if (aConstruct == construct::Import)
        {
            parse_path();
            // import a.b [as c]
            if (i + 1u < tokens.size() && tokens[i] == "as")
                py_bind_name(py_global_name(tokens[i + 1u]), operand_type{ module(path) }, statement);
            else
                py_bind_name(py_global_name(path[0]), operand_type{ module(path, true) }, statement);
        }
        else
        {
            // from [.]a.b import c [as d], ... (or *)
            auto const text = aSource.to_std_string_view();
            std::size_t level = 0u;
            {
                auto p = text.find_first_not_of(" \t", 4u);
                while (p != std::string_view::npos && p < text.size() && text[p] == '.')
                {
                    ++level;
                    ++p;
                }
            }
            std::string relative;
            while (i < tokens.size() && tokens[i] == ".")
                ++i;
            if (i < tokens.size() && tokens[i] != "import")
            {
                parse_path();
                for (auto const& part : path)
                    relative += (relative.empty() ? "" : ".") + part;
            }
            auto const absolute = py_absolute_module(relative, level);
            if (absolute.empty())
            {
                auto const failed = py_runtime_call("python::raise_import_error", { type::String }, type::Void, 
                    { py_string("attempted relative import with no known parent package") });
                append(statement, failed);
                push_operand(operand_type{ statement });
                return;
            }
            std::vector<std::string> absolutePath;
            for (std::size_t from = 0u;;)
            {
                auto const dot = absolute.find('.', from);
                absolutePath.push_back(absolute.substr(from, dot == std::string::npos ? std::string::npos : dot - from));
                if (dot == std::string::npos)
                    break;
                from = dot + 1u;
            }
            auto const m = temporary(type::I32);
            append(statement, module(absolutePath));
            put_local(statement, opcode::LocalSet, m);
            while (i < tokens.size() && tokens[i] != "import")
                ++i;
            ++i; // (import)
            auto const source = py_module_of(absolute);
            if (i >= tokens.size() && text.find('*') != std::string_view::npos)
            {
                // (from m import *: the module's public names (its __all__))
                if (source)
                {
                    auto const& sm = iPyModules[*source];
                    std::vector<std::string> names;
                    if (sm.all)
                        names = *sm.all;
                    else
                        for (auto const& n : sm.names)
                            if (!n.starts_with("_"))
                                names.push_back(n);
                    for (auto const& n : names)
                    {
                        auto const value = py_owned(py_runtime_call("python::getattr", { type::Object, type::String }, type::Object, { local_value(m, type::Object), py_string(n) }));
                        py_bind_name(py_global_name(n), operand_type{ value }, statement);
                    }
                }
                else
                    append(statement, py_runtime_call("python::import_star", { type::String, type::Object }, type::Void,
                        { py_string(py_module_name()), local_value(m, type::Object) }));
                push_operand(operand_type{ statement });
                return;
            }
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
                // (a package's submodule: imported)
                if (source && iPyModules[*source].package && py_module_of(absolute + "." + name))
                {
                    append(statement, import_one(absolute + "." + name));
                    put(statement, opcode::Drop);
                }
                auto const value = py_owned(py_runtime_call("python::import_from", { type::Object, type::String }, type::Object, { local_value(m, type::Object), py_string(name) }));
                py_bind_name(py_global_name(bound), operand_type{ value }, statement);
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
        for (auto i = generator().indented_count(); i-- > 0u;)
        {
            auto const kind = generator().indented_kind(i);
            if (kind == indented_construct::Function)
                break;
            if (aLoop && (kind == indented_construct::While || kind == indented_construct::For))
                break;
            if (kind == indented_construct::Try || kind == indented_construct::Except || (kind == indented_construct::Else && generator().indented_try_else(i)) ||
                kind == indented_construct::With)
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
            if (operands().empty())
                throw std::runtime_error("expression expected");
            value = operands().back();
            operands().pop_back();
        }
        if (declaration_pass())
        {
            push_operand(operand_type{ statement });
            return;
        }
        auto const resultType = generator().signature_of(current_function()).return_type();
        if (in_function() && !implicit_entry())
            py_register_parameters();
        std::optional<std::uint32_t> returned;
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
                auto const holder = held_by(result, py_function().pendingObjects);
                append(statement, result);
                if (holder)
                {
                    auto& pendingObjects = py_function().pendingObjects;
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
            auto result = materialize(*value, resultType);
            if (resultType == type::Integer)
            {
                // (a new reference: an int expression's (its slow version's result is one), a variable's (that is not released), or 
                // retained)
                auto const local = local_of(result);
                auto const& variables = py_function().integerVariables;
                if (auto const expression = py_integer_expression_of(result))
                {
                    append(statement, expression->prefix);
                    append(statement, py_integer_value(*expression, false));
                }
                else if (local && py_try_depth(false) == 0u && std::find(variables.begin(), variables.end(), *local) != variables.end())
                {
                    append(statement, result);
                    returned = *local;
                }
                else
                {
                    append(statement, result);
                    auto const scratch = py_scratch(ScratchReturn, type::Integer);
                    put_local(statement, opcode::LocalSet, scratch);
                    append(statement, py_retain_integer(scratch));
                    put_local(statement, opcode::LocalGet, scratch);
                }
            }
            else
                append(statement, result);
        }
        auto const release = py_release_variables(true, returned);
        auto const tries = py_try_depth(false);
        if (!release.fragmentCode.empty() || tries != 0u)
        {
            std::optional<std::uint32_t> result;
            if (resultType != type::Void)
            {
                result = py_scratch(ScratchReturnValue, resultType);
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
        py_function().returns = true;
        push_operand(operand_type{ statement });
    }

    void compiler::py_for_in(neolib::i_string_view const& aSource)
    {
        // for targets in iterable: the loop condition gets the iterator's next item (and assigns it to the targets)
        using py_target = fragment_state::py_target;
        auto const text = aSource.to_std_string_view();
        std::vector<py_target> targets;
        auto& pending = state().pyTargets;
        while (!pending.empty() && pending.back().position >= text.data() && pending.back().position < text.data() + text.size())
        {
            targets.insert(targets.begin(), pending.back());
            pending.pop_back();
        }
        auto inlineBody = pop_fragments(aSource, fragment_kind::Statement);
        if (operands().empty())
            throw std::runtime_error("iterable expected");
        operand_type const iterable{ operands().back() };
        operands().pop_back();
        std::optional<fragment> epilogue;
        if (declaration_pass())
        {
            fragment dummy;
            dummy.valueType = type::Boolean;
            push_operand(operand_type{ dummy });
        }
        else
        {
            ensure_function();
            bool const single = targets.size() == 1u && top_level(text.substr(0u, text.find(" in ")), ',').empty();
            // (a range's loop (if the iterable is a range at run time): a counted loop that calls nothing for its items; its target
            // (a variable of a function) is not bound if nothing else can see it (see py_observed))
            bool const speculative = single && targets[0].kind == py_target::kind_t::Name && !targets[0].starred;
            bool const bound = !speculative || !in_function() || implicit_entry() || py_observed(targets[0].name);
            std::uint32_t length = 0u;
            std::uint32_t counter = 0u;
            std::uint32_t start = 0u;
            std::uint32_t step = 0u;
            // it = iter(iterable) (a range: its length (else -1), start and step)
            auto const iterator = temporary(type::Object);
            py_function().objectVariables.push_back(iterator);
            auto prelude = make_fragment(fragment_kind::Statement, type::Void, aSource);
            if (speculative)
            {
                length = temporary(type::I64);
                counter = temporary(type::I64);
                auto const source = temporary(type::I32);
                append(prelude, py_object(iterable));
                put_local(prelude, opcode::LocalSet, source);
                append(prelude, py_runtime_call("python::range_length", { type::Object }, type::I64, { local_value(source, type::Object) }));
                put_local(prelude, opcode::LocalTee, length);
                put_i64(prelude, 0);
                put(prelude, opcode::I64LtS);
                put_block(prelude, opcode::If, {});
                append(prelude, py_runtime_call("python::iter", { type::Object }, type::Object, { local_value(source, type::Object) }));
                put_local(prelude, opcode::LocalSet, iterator);
                put(prelude, opcode::Else);
                if (bound)
                {
                    start = temporary(type::I64);
                    step = temporary(type::I64);
                    append(prelude, py_runtime_call("python::range_start", { type::Object }, type::I64, { local_value(source, type::Object) }));
                    put_local(prelude, opcode::LocalSet, start);
                    append(prelude, py_runtime_call("python::range_step", { type::Object }, type::I64, { local_value(source, type::Object) }));
                    put_local(prelude, opcode::LocalSet, step);
                }
                put_i64(prelude, 0);
                put_local(prelude, opcode::LocalSet, counter);
                put(prelude, opcode::End);
            }
            else
            {
                append(prelude, py_runtime_call("python::iter", { type::Object }, type::Object, { py_object(iterable) }));
                put_local(prelude, opcode::LocalSet, iterator);
            }
            append(prelude, py_flush());
            push_operand(operand_type{ prelude });
            // (item = next(it)) != NULL (and the targets are assigned)
            auto condition = make_fragment(fragment_kind::Value, type::Boolean, neolib::string_view{});
            auto const item = py_owned(py_runtime_call("python::next", { type::Object }, type::Object, { local_value(iterator, type::Object) }));
            auto const itemTemporary = py_function().pendingObjects.back();
            auto assignment = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
            if (single)
            {
                if (bound)
                    py_bind(targets[0], operand_type{ local_value(itemTemporary, type::Object) }, assignment);
            }
            else
            {
                fragment count;
                put_i64(count, static_cast<std::int64_t>(targets.size()));
                count.valueType = type::I64;
                auto const sequence = temporary(type::I32);
                append(assignment, py_owned(py_unpack_call(local_value(itemTemporary, type::Object), targets)));
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
            if (speculative)
            {
                // (a range: counter < length (and the target is its value: start + counter * step); else (not a range: the code after 
                // the inner block) the iterator's next item: the JIT makes that code cold (out of the loop's code))
                put_block(condition, opcode::Block, type::Boolean);
                put_block(condition, opcode::Block, {});
                put_local(condition, opcode::LocalGet, length);
                put_i64(condition, 0);
                put(condition, opcode::I64LtS);
                put(condition, opcode::BrIf);
                put_u32(condition.fragmentCode.as_std_vector(), 0u);
                put_local(condition, opcode::LocalGet, counter);
                put_local(condition, opcode::LocalGet, length);
                put(condition, opcode::I64LtS);
                put_block(condition, opcode::If, type::Boolean);
                if (bound)
                {
                    fragment value;
                    put_local(value, opcode::LocalGet, start);
                    put_local(value, opcode::LocalGet, counter);
                    put_local(value, opcode::LocalGet, step);
                    put(value, opcode::I64Mul);
                    put(value, opcode::I64Add);
                    value.valueType = type::I64;
                    auto rangeAssignment = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
                    auto const pending = py_function().pendingObjects.size();
                    py_bind(targets[0], operand_type{ value }, rangeAssignment);
                    // (the objects that it made are released here (the item's, in the cold code))
                    auto const release = runtime_function("neos::object_release", { type::Object }, type::Void);
                    for (auto t = pending; t < py_function().pendingObjects.size(); ++t)
                    {
                        auto const temporary = py_function().pendingObjects[t];
                        put_local(rangeAssignment, opcode::LocalGet, temporary);
                        put_block(rangeAssignment, opcode::If, {});
                        put_local(rangeAssignment, opcode::LocalGet, temporary);
                        emit_call_to(rangeAssignment, release);
                        put_i32(rangeAssignment, 0);
                        put_local(rangeAssignment, opcode::LocalSet, temporary);
                        put(rangeAssignment, opcode::End);
                    }
                    append(condition, rangeAssignment);
                }
                put_local(condition, opcode::LocalGet, counter);
                put_i64(condition, 1);
                put(condition, opcode::I64Add);
                put_local(condition, opcode::LocalSet, counter);
                put_i32(condition, 1);
                put(condition, opcode::Else);
                put_i32(condition, 0);
                put(condition, opcode::End);
                put(condition, opcode::Br);
                put_u32(condition.fragmentCode.as_std_vector(), 1u);
                put(condition, opcode::End);
            }
            append(condition, item);
            put_block(condition, opcode::If, type::Boolean);
            append(condition, assignment);
            put_i32(condition, 1);
            put(condition, opcode::Else);
            put_i32(condition, 0);
            put(condition, opcode::End);
            // (the item is released (a range's loop has none: in its cold code))
            append(condition, py_flush());
            if (speculative)
                put(condition, opcode::End);
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
        generator().begin_indented(indented_construct::While, aSource);
        if (epilogue)
            generator().set_loop_epilogue(*epilogue);
    }

    bool compiler::py_observed(std::string const& aName)
    {
        // whether something other than the binding of a for loop's target (aName, a variable of a function) can see the variable: 
        // (conservatively) the name occurs elsewhere in the fragment's source (strings and comments included), or the source can
        // see variables by name (e.g. locals())
        // (a class's body: its namespace is the names it binds)
        if (class_body(current_function_name()))
            return true;
        auto& f = state();
        if (f.source == nullptr)
            return true;
        if (!f.pyIdentifiers)
        {
            auto& identifiers = f.pyIdentifiers.emplace();
            auto const text = f.source->source().to_std_string_view();
            auto const identifier_character = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80u; };
            for (std::size_t i = 0u; i < text.size();)
            {
                if (!identifier_character(text[i]))
                {
                    ++i;
                    continue;
                }
                auto const begin = i;
                while (i < text.size() && identifier_character(text[i]))
                    ++i;
                if (!std::isdigit(static_cast<unsigned char>(text[begin])))
                    ++identifiers[std::string{ text.substr(begin, i - begin) }];
            }
        }
        auto const& identifiers = *f.pyIdentifiers;
        static char const* const sIntrospection[] = { "locals", "vars", "globals", "eval", "exec", "dir", "breakpoint", "_getframe", "f_locals", "inspect", 
            "settrace", "setprofile", "__import__" };
        for (auto const name : sIntrospection)
            if (identifiers.contains(std::string_view{ name }))
                return true;
        auto const occurrences = identifiers.find(std::string_view{ aName });
        return occurrences == identifiers.end() || occurrences->second != 1u;
    }

    void compiler::py_comprehension(neolib::i_string_view const& aSource, bool aDict, bool aSet)
    {
        // [element for targets in iterable if condition ...]: a list ({key: value for ...}: a dict; {element for ...}: a set)
        auto const text = aSource.to_std_string_view();
        std::vector<fragment_state::py_clause> clauses;
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
        if (declaration_pass())
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
        auto& function = py_function();
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
        auto const list = py_owned(py_runtime_call(aDict ? "python::dict" : aSet ? "python::set" : "python::list", {}, type::Object, {}));
        auto const listTemporary = py_function().pendingObjects.back();
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
                    append(code, py_runtime_call(aSet ? "python::set_add" : "python::list_append", { type::Object, type::Object }, type::Void, { local_value(listTemporary, type::Object), value }));
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
                append(assignment, py_unpack_call(local_value(item, type::Object), clause.targets));
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
        if (declaration_pass())
            return;
        // (the depth of calls (see py_error_checks): a global of the functions, and the runtime's check of it (imported while the 
        // function is compiled))
        if (!implicit_entry())
        {
            py_depth_global();
            runtime_function("python::depth_check", { type::I32 }, type::Void);
        }
        else
        {
            runtime_function("python::unhandled", { type::String }, type::I32);
            runtime_function("python::finish", {}, type::Void);
            runtime_function("neos::exit", { type::I32 }, type::Void);
        }
        py_register_parameters();
        auto const predeclared = iInference.pyLocals.find(current_function_name());
        if (predeclared != iInference.pyLocals.end())
            for (auto const& variable : predeclared->second)
                py_variable(variable.first, variable.second);
    }

    void compiler::py_register_parameters()
    {
        // (the function's parameters are released when it returns: registered once, when its body starts, or by a return statement 
        // compiled before then (the body of a def on the def's own line, e.g. "def f(x): return x"))
        if (py_function().parametersRegistered)
            return;
        py_function().parametersRegistered = true;
        auto const& signature = generator().signature_of(current_function());
        if (signature.parameters().has_value())
            for (auto const& parameter : signature.parameters().value())
                if (parameter.parameter_type() == type::Object)
                    py_function().objectVariables.push_back(find_symbol(current_scope(), neolib::string_view{ parameter.parameter_name() }, true)->index());
                else if (parameter.parameter_type() == type::Integer)
                    py_function().integerVariables.push_back(find_symbol(current_scope(), neolib::string_view{ parameter.parameter_name() }, true)->index());
    }

    fragment compiler::py_function_end(bool aImplicit)
    {
        // (a function whose body is "..." is provided by the Python runtime plugin)
        auto stack = operands();
        auto const base = operand_base();
        if (generator().function_stub(current_function()) && stack.size() == base + 1u && stack.back().holds_alternative<i_fragment>() &&
            trimmed(stack.back().get<i_fragment>().source().to_std_string_view()) == "...")
            return make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
        // the prologue retains the object parameters
        auto prologue = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
        auto const& signature = generator().signature_of(current_function());
        if (signature.parameters().has_value())
            for (auto const& parameter : signature.parameters().value())
                if (parameter.parameter_type() == type::Object)
                {
                    put_local(prologue, opcode::LocalGet, find_symbol(current_scope(), neolib::string_view{ parameter.parameter_name() }, true)->index());
                    emit_call_to(prologue, runtime_function("neos::object_retain", { type::Object }, type::Void));
                }
                else if (parameter.parameter_type() == type::Integer)
                    append(prologue, py_retain_integer(find_symbol(current_scope(), neolib::string_view{ parameter.parameter_name() }, true)->index()));
        // (the program's namespace (globals()): its getter, for the runtime (e.g. functions' annotations))
        if (aImplicit && implicit_entry() && !py_exec_mode() && !py_current_module() && iPyIntrospective.contains("__main__") && iPyParameters.contains("_N0_getattr") &&
            !declaration_pass())
            append(prologue, py_runtime_call("python::register_namespace", { type::String, type::Object }, type::Void, { py_string("__main__"), py_name("_N0_getattr") }));
        if (!prologue.fragmentCode.empty())
            stack.insert(std::min(base, stack.size()), operand_type{ prologue });
        // (a class's body (_bN_name, see statements.cpp): it returns its namespace (the names it binds, in the order of their first 
        // bindings; its cells' values (and the class's cell: __classcell__)))
        auto const functionName = current_function_name();
        if (!aImplicit && class_body(functionName) && !declaration_pass())
        {
            auto result = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
            auto const dict = temporary(type::I32);
            append(result, py_runtime_call("python::dict", {}, type::Object, {}));
            put_local(result, opcode::LocalSet, dict);
            for (auto const& name : py_function().pyBindingOrder)
            {
                if (name == "__bases__" || name == "__kw__" || name == "__class__" || synthetic_name(name))
                    continue;
                auto* const symbol = find_symbol(current_scope(), neolib::string_view{ neolib::string{ name } }, true);
                if (symbol == nullptr || is_global(*symbol))
                    continue;
                auto const value = local_value(symbol->index(), symbol->type());
                if (name.starts_with("_k_") && symbol->type() == type::Object)
                {
                    if (name == "_k___class__")
                        append(result, py_runtime_call("python::ns_classcell", { type::Object, type::Object }, type::Void, { local_value(dict, type::Object), value }));
                    else
                        append(result, py_runtime_call("python::ns_cell", { type::Object, type::String, type::Object }, type::Void,
                            { local_value(dict, type::Object), py_string(name.substr(3u)), value }));
                }
                else if (symbol->type() == type::Object)
                    append(result, py_runtime_call("python::ns_set", { type::Object, type::String, type::Object }, type::Void,
                        { local_value(dict, type::Object), py_string(name), value }));
                else
                {
                    // (a value that is not an object: the object of it)
                    append(result, py_runtime_call("python::ns_set", { type::Object, type::String, type::Object }, type::Void,
                        { local_value(dict, type::Object), py_string(name), py_object(operand_type{ symbol_reference{ current_scope(), neolib::string{ name } } }) }));
                }
            }
            append(result, py_release_variables(true));
            put_local(result, opcode::LocalGet, dict);
            put(result, opcode::Return);
            return result;
        }
        // (exec: the names that its top level binds are its namespace's (its locals'))
        if (aImplicit && py_exec_mode() && implicit_entry() && !declaration_pass())
        {
            auto stores = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
            for (auto const& name : py_function().pyBindingOrder)
            {
                if (synthetic_name(name) || name.starts_with("_k_"))
                    continue;
                auto* const symbol = find_symbol(current_scope(), neolib::string_view{ neolib::string{ name } });
                if (symbol == nullptr || symbol->type() == type::Function)
                    continue;
                fragment value;
                put_local(value, is_global(*symbol) ? opcode::GlobalGet : opcode::LocalGet, symbol->index());
                value.valueType = symbol->type();
                if (symbol->type() != type::Object)
                    value = py_object(operand_type{ symbol_reference{ current_scope(), neolib::string{ name } } });
                append(stores, py_runtime_call("python::exec_store", { type::String, type::Object }, type::Void, { py_string(name), value }));
            }
            for (auto const& name : iPyExecDefs)
                append(stores, py_runtime_call("python::exec_store", { type::String, type::Object }, type::Void, { py_string(name), py_name(name) }));
            auto result = py_release_variables(true);
            fragment combined{ stores };
            append(combined, result);
            return combined;
        }
        // a function that ends without a return statement releases its objects (and returns None); the program's exit functions 
        // (atexit) are called before its variables (its globals) are released
        fragment result;
        if (aImplicit && implicit_entry() && !py_exec_mode() && !py_current_module() && generator().function_entry(current_function()) && !declaration_pass())
        {
            result = py_runtime_call("python::finish", {}, type::Void, {});
            append(result, py_release_variables(true));
        }
        else
            result = py_release_variables(true);
        if (!aImplicit && generator().signature_of(current_function()).return_type() == type::Object)
        {
            append(result, py_runtime_call("python::none", {}, type::Object, {}));
            put(result, opcode::Return);
        }
        return result;
    }

    // --- the language compiler

    compiler::compiler() :
        language_compiler{ language::python::Language }
    {
    }

    void compiler::set_options(neolib::i_string const& aOptions)
    {
        iPyMode = aOptions.to_std_string();
    }

    void compiler::begin_compilation()
    {
        iInference = {};
        iPyModules.clear();
        iPySyntax.clear();
        iPyPreparsed.clear();
        iPyScriptDirectory = std::nullopt;
        iPyIntrospective.clear();
        // (source that the program compiles while it runs is compiled on its own: the program's own flag stands)
        if (!py_exec_mode())
            iPyCompilesAtRunTime = false;
    }

    bool compiler::compiles_at_run_time() const
    {
        return iPyCompilesAtRunTime;
    }

    void compiler::begin_pass()
    {
        iInference.pyCounterBindings.clear();
        iFunctions.clear();
    }

    void compiler::end_pass()
    {
        // (int variables that only for loops over ranges bind)
        py_update_counters();
    }

    void compiler::begin_fragment(i_source_fragment& aFragment)
    {
        iPyLineCache = {};
        iFragments.emplace_back();
        iFragments.back().source = &aFragment;
        if (aFragment.imported() && aFragment.source_file_path().has_value())
        {
            auto const path = aFragment.source_file_path().value().to_std_string();
            for (std::size_t m = 0u; m < iPyModules.size(); ++m)
                if (iPyModules[m].path == path)
                    iFragments.back().module = m;
        }
    }

    void compiler::fragment_function(i_source_fragment const& aFragment, neolib::i_string& aName)
    {
        aName.clear();
        if (!aFragment.imported() || !aFragment.source_file_path().has_value())
            return;
        auto const path = aFragment.source_file_path().value().to_std_string();
        for (auto const& m : iPyModules)
            if (m.path == path)
                aName = neolib::string{ m.init() };
    }

    // (the line of a position of a source as rewritten (see py_join_lines) in the source as written)
    std::uint32_t compiler::source_line(i_source_fragment const& aFragment, std::size_t aPosition)
    {
        auto const source = aFragment.source().to_std_string_view();
        auto& cache = iPyLineCache;
        if (cache.source != source.data() || cache.size != source.size())
        {
            cache.source = source.data();
            cache.size = source.size();
            auto const existing = iPyLines.find(std::string{ source });
            cache.lines = existing != iPyLines.end() ? &existing->second : nullptr;
            cache.starts.assign(1u, 0u);
            for (std::size_t i = 0u; i < source.size(); ++i)
                if (source[i] == '\n')
                    cache.starts.push_back(i + 1u);
        }
        if (cache.lines == nullptr)
            return 0u;
        auto const line = static_cast<std::size_t>(std::upper_bound(cache.starts.begin(), cache.starts.end(), aPosition) - cache.starts.begin()) - 1u;
        return line < cache.lines->size() ? (*cache.lines)[line] : 0u;
    }

    // (a function's frame's name: as CPython names its code (co_name); the functions that the compiler makes (e.g. a module's getter) 
    // have none)
    bool compiler::frame_name(std::uint32_t aFunction, neolib::i_string& aName)
    {
        std::string const name = generator().function_name(aFunction).to_std_string();
        auto const numbered = [](std::string_view aName, char aKind) -> std::optional<std::string_view>
        {
            // (_<kind>N_rest: rest)
            if (aName.size() < 4u || aName[0] != '_' || aName[1] != aKind || !std::isdigit(static_cast<unsigned char>(aName[2])))
                return {};
            auto const underscore = aName.find('_', 2u);
            if (underscore == std::string_view::npos || !std::all_of(aName.begin() + 2, aName.begin() + static_cast<std::ptrdiff_t>(underscore), 
                [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
                return {};
            return aName.substr(underscore + 1u);
        };
        // (top-level statements: the module's, as are an imported module's (_NN_init); the module's other functions (e.g. its getter): 
        // the compiler's)
        if (generator().function_implicit_entry(aFunction) || name == "__main__")
        {
            aName = neolib::string{ "<module>" };
            return true;
        }
        if (auto const rest = numbered(name, 'N'))
        {
            aName = neolib::string{ *rest == "init" ? "<module>" : "" };
            return true;
        }
        if (auto const named = iPyFrameNames.find(name); named != iPyFrameNames.end())
        {
            aName = neolib::string{ named->second };
            return true;
        }
        auto const base = unprefixed(name);
        if (auto const named = iPyFrameNames.find(std::string{ base }); named != iPyFrameNames.end())
        {
            aName = neolib::string{ named->second };
            return true;
        }
        if (numbered(base, 'l'))
        {
            aName = neolib::string{ "<lambda>" };
            return true;
        }
        for (char const kind : std::string_view{ "ndgab" })
            if (auto const rest = numbered(base, kind))
            {
                aName = neolib::string{ *rest };
                return true;
            }
        aName = neolib::string{ base };
        return true;
    }

    std::string compiler::py_module::prefix() const
    {
        return "_M" + std::to_string(number) + "_";
    }

    std::string compiler::py_module::init() const
    {
        return "_N" + std::to_string(number) + "_init";
    }

    std::string compiler::py_module::getter() const
    {
        return "_N" + std::to_string(number) + "_getattr";
    }

    std::optional<std::size_t> compiler::py_current_module() const
    {
        if (iFragments.empty())
            return {};
        return iFragments.back().module;
    }

    std::string compiler::py_module_name() const
    {
        auto const m = py_current_module();
        return m ? iPyModules[*m].name : std::string{ "__main__" };
    }

    std::string compiler::py_global_name(std::string const& aName) const
    {
        auto const m = py_current_module();
        if (!m || (in_function() && !implicit_entry()))
            return aName;
        return iPyModules[*m].prefix() + aName;
    }

    std::string compiler::py_absolute_module(std::string const& aName, std::size_t aLevel) const
    {
        if (aLevel == 0u)
            return aName;
        // (the package of the module being compiled)
        auto const m = py_current_module();
        if (!m)
            return {};
        std::string package = iPyModules[*m].package ? iPyModules[*m].name : 
            (iPyModules[*m].name.find('.') == std::string::npos ? std::string{} : iPyModules[*m].name.substr(0u, iPyModules[*m].name.rfind('.')));
        for (std::size_t l = 1u; l < aLevel; ++l)
        {
            if (package.empty())
                return {};
            auto const dot = package.rfind('.');
            package = dot == std::string::npos ? std::string{} : package.substr(0u, dot);
        }
        if (package.empty())
            return {};
        return aName.empty() ? package : package + "." + aName;
    }

    std::optional<std::size_t> compiler::py_module_of(std::string const& aName)
    {
        if (aName.empty())
            return {};
        for (std::size_t m = 0u; m < iPyModules.size(); ++m)
            if (iPyModules[m].name == aName)
                return m;
        auto const dot = aName.rfind('.');
        std::vector<std::filesystem::path> directories;
        if (dot == std::string::npos)
        {
            // (the standard library's modules that the runtime implements are built in)
            if (standard_modules().contains(aName))
                return {};
            directories.push_back(iPyScriptDirectory ? std::filesystem::path{ *iPyScriptDirectory } : std::filesystem::current_path());
            if (auto const list = std::getenv("PYTHONPATH"))
            {
#if defined(_WIN32)
                char const separator = ';';
#else
                char const separator = ':';
#endif
                std::string const paths{ list };
                std::size_t start = 0u;
                for (;;)
                {
                    auto const end = paths.find(separator, start);
                    auto const entry = paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
                    if (!entry.empty())
                        directories.push_back(entry);
                    if (end == std::string::npos)
                        break;
                    start = end + 1u;
                }
            }
        }
        else
        {
            auto const parent = py_module_of(aName.substr(0u, dot));
            if (!parent || !iPyModules[*parent].package)
                return {};
            directories.push_back(std::filesystem::path{ iPyModules[*parent].path }.parent_path());
        }
        auto const last = dot == std::string::npos ? aName : aName.substr(dot + 1u);
        for (auto const& directory : directories)
        {
            std::error_code ec;
            std::filesystem::path path;
            bool package = false;
            if (std::filesystem::is_regular_file(directory / last / "__init__.py", ec))
            {
                path = directory / last / "__init__.py";
                package = true;
            }
            else if (std::filesystem::is_regular_file(directory / (last + ".py"), ec))
                path = directory / (last + ".py");
            else
                continue;
            std::ifstream file{ path, std::ios::binary };
            if (!file)
                continue;
            std::ostringstream contents;
            contents << file.rdbuf();
            py_module module;
            module.name = aName;
            module.path = std::filesystem::absolute(path, ec).lexically_normal().generic_string();
            module.package = package;
            module.source = contents.str();
            if (!module.source.empty() && module.source.back() != '\n')
                module.source += '\n';
            module.number = iPyModules.size() + 1u;
            iPyParameters[module.init()] = {};
            iPyModules.push_back(std::move(module));
            return iPyModules.size() - 1u;
        }
        return {};
    }

    void compiler::py_find_imports(std::string const& aText)
    {
        using namespace tokens;
        auto const lines = tokenize(aText);
        auto const word = [&](token const& aToken) { return std::string_view{ aText }.substr(aToken.begin, aToken.end - aToken.begin); };
        auto const import = [&](std::string const& aName)
        {
            if (auto const m = py_module_of(aName))
            {
                // (its packages, then it)
                for (auto dot = aName.find('.'); dot != std::string::npos; dot = aName.find('.', dot + 1u))
                    if (auto const p = py_module_of(aName.substr(0u, dot)))
                        generator().import_fragment(neolib::string{ iPyModules[*p].path }, neolib::string{ iPyModules[*p].source });
                generator().import_fragment(neolib::string{ iPyModules[*m].path }, neolib::string{ iPyModules[*m].source });
            }
        };
        for (auto const& line : lines)
        {
            auto const& tokens = line.tokens;
            // (the line's simple statements (after a compound statement's header, e.g. "try: import x"))
            std::size_t start = 0u;
            for (std::size_t t = 0u; t <= tokens.size(); ++t)
            {
                if (t < tokens.size() && !(tokens[t].kind == token::Op && (word(tokens[t]) == ";" || word(tokens[t]) == ":")))
                    continue;
                auto const end = t;
                auto i = start;
                start = t + 1u;
                if (i >= end || tokens[i].kind != token::Name)
                    continue;
                auto const keyword = word(tokens[i]);
                if (keyword != "import" && keyword != "from")
                    continue;
                ++i;
                if (keyword == "import")
                {
                    std::string name;
                    bool alias = false;
                    for (; i <= end; ++i)
                    {
                        if (i == end || (tokens[i].kind == token::Op && word(tokens[i]) == ","))
                        {
                            if (!name.empty())
                                import(name);
                            name.clear();
                            alias = false;
                            continue;
                        }
                        if (tokens[i].kind == token::Name && word(tokens[i]) == "as")
                            alias = true;
                        else if (!alias)
                            name += word(tokens[i]);
                    }
                    continue;
                }
                std::size_t level = 0u;
                std::string name;
                for (; i < end && !(tokens[i].kind == token::Name && word(tokens[i]) == "import"); ++i)
                {
                    auto const w = word(tokens[i]);
                    if (tokens[i].kind == token::Op && name.empty() && (w == "." || w == "..."))
                        level += w.size();
                    else
                        name += w;
                }
                auto const absolute = py_absolute_module(name, level);
                if (absolute.empty())
                    continue;
                import(absolute);
                auto const m = py_module_of(absolute);
                if (!m || !iPyModules[*m].package)
                    continue;
                // (from a package import a submodule)
                for (++i; i < end; ++i)
                    if (tokens[i].kind == token::Name && word(tokens[i]) != "as" && !(i > 0u && word(tokens[i - 1u]) == "as"))
                        import(absolute + "." + std::string{ word(tokens[i]) });
            }
        }
    }

    void compiler::end_fragment()
    {
        iFragments.pop_back();
    }

    void compiler::preparse(i_source_fragment& aFragment)
    {
        if (!aFragment.imported() && !iPyScriptDirectory)
        {
            auto const& directory = aFragment.source_directory_path();
            std::error_code ec;
            iPyScriptDirectory = directory.has_value() && !directory.value().to_std_string().empty() ? 
                std::filesystem::absolute(directory.value().to_std_string(), ec).generic_string() : std::filesystem::current_path(ec).generic_string();
        }
        // (its syntax errors (CPython's): the tokenizer's (the parser reports the first error); the others are reported if it parses)
        std::string const original = aFragment.source().to_std_string();
        if (!iPyPreparsed.contains(original))
        {
            py_fragment_syntax info;
            info.original = original;
            info.expression = iPyMode == "eval" && !aFragment.imported();
            info.first = info.expression ? std::min(original.find_first_not_of(" \t"), original.size()) : 0u;
            if (aFragment.source_file_path())
            {
                info.file = aFragment.source_file_path()->to_std_string();
                std::error_code ec;
                if (!info.file.starts_with("<") && !py_exec_mode())
                    if (auto const absolute = std::filesystem::absolute(info.file, ec); !ec)
                        info.file = absolute.string();
            }
            info.issues = syntax_issues(original, info.expression);
            if (!info.issues.empty() && info.issues.front().stage == syntax_issue::Tokenizer)
            {
                // (its lines joined within brackets (as CPython's tokenizer joins them): the parser's error is the first)
                auto const joined = join_bracketed_lines(original);
                if (joined != original)
                    aFragment.set_source(neolib::string_view{ joined });
                iPySyntax[joined] = std::move(info);
                return;
            }
            py_join_lines(aFragment);
            iPySyntax[aFragment.source().to_std_string()] = std::move(info);
        }
        else
            py_join_lines(aFragment);
        // (the modules of source files that the program imports: fragments of the program (those that the previous compilation 
        // pass found))
        if (!aFragment.imported())
            for (auto const& m : iPyModules)
                generator().import_fragment(neolib::string{ m.path }, neolib::string{ m.source });
    }

    // (a fragment that parsed: the first syntax error of its tokenizer or parser, else its symbol table's, else its compiler's)
    void compiler::parsed(i_source_fragment& aFragment)
    {
        auto const s = iPySyntax.find(aFragment.source().to_std_string());
        if (s == iPySyntax.end())
            return;
        for (auto const stages : { std::vector{ syntax_issue::Tokenizer, syntax_issue::Parser }, std::vector{ syntax_issue::Symtable }, 
            std::vector{ syntax_issue::Compiler } })
            for (auto const& issue : s->second.issues)
                if (std::find(stages.begin(), stages.end(), issue.stage) != stages.end())
                {
                    iPyReporting = issue;
                    auto const position = std::min<std::size_t>(issue.position + (s->second.expression ? 15u - s->second.first : 0u), aFragment.source().size());
                    generator().throw_error(aFragment.begin() + static_cast<std::ptrdiff_t>(position), neolib::string{ issue.message }, neolib::string{ issue.type });
                }
    }

    // (a compile error: CPython's report of it (a syntax error))
    void compiler::diagnose(i_source_fragment const& aFragment, std::uint32_t& aLine, std::uint32_t& aColumn, std::uint32_t& aEndLine, std::uint32_t& aEndColumn,
        neolib::i_string& aType, neolib::i_string& aMessage, neolib::i_string& aReport)
    {
        std::string const text = aFragment.source().to_std_string();
        auto const s = iPySyntax.find(text);
        std::string message = aMessage.to_std_string();
        std::string const type = aType.to_std_string();
        for (auto const prefix : { "failed to fold semantic concepts: ", "failed to fold semantic concept: " })
            if (message.starts_with(prefix))
                message = message.substr(std::char_traits<char>::length(prefix));
        std::optional<syntax_issue> issue;
        if (iPyReporting)
        {
            issue = iPyReporting;
            iPyReporting.reset();
        }
        if (s == iPySyntax.end())
        {
            if (issue)
                message = issue->message;
            aType = neolib::string{ issue ? issue->type : std::string{ "SyntaxError" } };
            aMessage = neolib::string{ message };
            return;
        }
        auto const& info = s->second;
        if (!issue)
        {
            // (the error's position: in the source as written)
            std::size_t position = 0u;
            for (std::uint32_t l = 1u; l < aLine && position < text.size(); ++position)
                if (text[position] == '\n')
                    ++l;
            position = std::min<std::size_t>(position + (aColumn > 0u ? aColumn - 1u : 0u), text.size());
            if (info.expression)
            {
                // (eval's source is wrapped (see py_join_lines): a position after its expression is at its end)
                auto const last = info.original.find_last_not_of(" \t\r\n");
                position = position < 15u ? info.first : std::min(info.first + position - 15u, last == std::string::npos ? 0u : last + 1u);
            }
            if (type == "syntax error" && message.ends_with("was unexpected here."))
            {
                // (the parser's: a tokenizer's or parser's error before it is the first)
                for (auto const& i : info.issues)
                    if ((i.stage == syntax_issue::Tokenizer || i.stage == syntax_issue::Parser) && i.order <= position)
                    {
                        issue = i;
                        break;
                    }
                // (a statement that the grammar does not parse whose error is the symbol table's or the compiler's (e.g. yield, nonlocal))
                if (!issue)
                    for (auto const& i : info.issues)
                        if ((i.stage == syntax_issue::Symtable || i.stage == syntax_issue::Compiler) && i.position <= position && position <= i.endPosition)
                        {
                            issue = i;
                            break;
                        }
                if (!issue)
                    issue = parse_failure_issue(info.original, position, info.expression);
            }
            else
            {
                syntax_issue i;
                i.message = message;
                i.position = position;
                i.line = static_cast<std::uint32_t>(1 + std::count(info.original.begin(), info.original.begin() + static_cast<std::ptrdiff_t>(position), '\n'));
                auto const lineStart = info.original.rfind('\n', position == 0u ? 0u : position - 1u);
                std::size_t const begin = lineStart == std::string::npos || position == 0u ? 0u : lineStart + 1u;
                i.offset = 1;
                for (auto p = begin; p < position; ++p)
                    if ((static_cast<unsigned char>(info.original[p]) & 0xC0u) != 0x80u)
                        ++i.offset;
                i.endLine = i.line;
                i.endOffset = i.offset + 1;
                issue = i;
            }
        }
        // (its type: with what CPython's exception has (a tokenizer's: its text without its newline; the symbol table's and the compiler's: no text 
        // (of a source that is not a file); the symbol table's: no location in its arguments)
        std::string typeAndFlags = issue->type;
        if (!issue->lineText || info.expression)
            typeAndFlags += "#t";
        if (issue->stage == syntax_issue::Symtable)
            typeAndFlags += "#s";
        else if (issue->stage == syntax_issue::Compiler)
            typeAndFlags += "#c";
        aLine = issue->line;
        aColumn = static_cast<std::uint32_t>(issue->offset);
        aEndLine = issue->endLine != 0u ? issue->endLine : issue->line;
        aEndColumn = static_cast<std::uint32_t>(issue->endOffset);
        aType = neolib::string{ typeAndFlags };
        aMessage = neolib::string{ issue->message };
        aReport = neolib::string{ syntax_error_report(info.file.empty() ? std::string{ "<string>" } : info.file, info.original, *issue) };
    }

    compiler::fragment_state& compiler::state()
    {
        return iFragments.back();
    }

    compiler::function_state& compiler::py_function()
    {
        return iFunctions[current_function()];
    }

    compiler::function_state& compiler::py_function(std::uint32_t aFunction)
    {
        return iFunctions[aFunction];
    }

    i_fragment const* compiler::result(fragment const& aFragment)
    {
        iResult = aFragment;
        return &iResult;
    }

    void compiler::emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource)
    {
        emit_python(static_cast<construct>(aConstruct), aSource);
    }

    bool compiler::complete_signature(i_function_signature& aSignature, neolib::i_string_view const& aSource)
    {
        py_complete_signature(aSignature, aSource);
        return true;
    }

    bool compiler::late_binding() const
    {
        return true;
    }

    bool compiler::functions_follow_statements() const
    {
        return true;
    }

    i_fragment const* compiler::literal(i_operand_type const& aOperand)
    {
        // (an int literal beyond 64 bits: an int object)
        if (auto const big = py_big_literal(operand_type{ aOperand }))
            return result(*big);
        return nullptr;
    }

    // (globals(): the namespace of the module being compiled (a dict: its names' values (its getter's)))
    fragment compiler::py_module_namespace()
    {
        if (py_exec_mode())
            return py_owned(py_runtime_call("python::exec_globals", {}, type::Object, {}));
        auto const m = py_current_module();
        std::string const getter = m ? iPyModules[*m].getter() : std::string{ "_N0_getattr" };
        bool const has = iPyParameters.contains(getter);
        return py_owned(py_runtime_call("python::module_namespace", { type::String, type::Object }, type::Object,
            { py_string(py_module_name()), has ? py_name(getter) : py_owned(py_runtime_call("python::none", {}, type::Object, {})) }));
    }

    // (locals() in a function: a dict of its variables that are bound)
    fragment compiler::py_locals()
    {
        auto result = make_fragment(fragment_kind::Value, type::Object, neolib::string_view{});
        auto const dict = temporary(type::I32);
        append(result, py_owned(py_runtime_call("python::dict", {}, type::Object, {})));
        put_local(result, opcode::LocalSet, dict);
        std::vector<std::string> names;
        auto const& signature = generator().signature_of(current_function());
        if (signature.parameters().has_value())
            for (auto const& p : signature.parameters().value())
                names.push_back(p.parameter_name().to_std_string());
        for (auto const& n : py_function().pyBindingOrder)
            if (std::find(names.begin(), names.end(), n) == names.end())
                names.push_back(n);
        for (auto const& name : names)
        {
            if (synthetic_name(name) || name == "__class__")
                continue;
            auto* const symbol = find_symbol(current_scope(), neolib::string_view{ neolib::string{ name } }, true);
            if (symbol == nullptr || is_global(*symbol) || symbol->type() == type::Function)
                continue;
            if (name.starts_with("_k_") && symbol->type() == type::Object)
                append(result, py_runtime_call("python::ns_cell", { type::Object, type::String, type::Object }, type::Void,
                    { local_value(dict, type::Object), py_string(name.substr(3u)), local_value(symbol->index(), type::Object) }));
            else if (symbol->type() == type::Object)
                append(result, py_runtime_call("python::ns_set", { type::Object, type::String, type::Object }, type::Void,
                    { local_value(dict, type::Object), py_string(name), local_value(symbol->index(), type::Object) }));
            else
                append(result, py_runtime_call("python::ns_set", { type::Object, type::String, type::Object }, type::Void,
                    { local_value(dict, type::Object), py_string(name), py_object(operand_type{ symbol_reference{ current_scope(), neolib::string{ name } } }) }));
        }
        put_local(result, opcode::LocalGet, dict);
        result.valueType = type::Object;
        return result;
    }

    i_fragment const* compiler::void_value(i_fragment const& aStatement)
    {
        // (a call of a function that returns nothing (e.g. print) used as a value: None)
        fragment value{ aStatement };
        append(value, py_runtime_call("python::none", {}, type::Object, {}));
        value.valueType = type::Object;
        value.fragmentKind = fragment_kind::Value;
        return result(py_owned(value));
    }

    i_fragment const* compiler::unbound_name(neolib::i_string_view const& aName)
    {
        // (a name that is not a variable: a global, a builtin, ...)
        return result(py_name(aName.to_std_string()));
    }

    i_fragment const* compiler::variable_value(i_fragment const& aValue, neolib::i_string_view const& aName, i_symbol_table_entry const& aVariable)
    {
        // a variable that may be unbound (it is deleted, or it is read before (in the order of the source) a binding of it): checked 
        // (NameError, or UnboundLocalError)
        if (declaration_pass() || aVariable.type() != type::Object || !in_function())
            return nullptr;
        auto const name = aName.to_std_string();
        bool const global = is_global(aVariable);
        bool const module = implicit_entry() || global;
        // (a module's getter (see rewrite_module_names): a name that is not bound is null (its module object's: not an attribute))
        if (global && !implicit_entry())
        {
            auto const function = current_function_name();
            if (function.size() > 10u && function.starts_with("_N") && function.ends_with("_getattr"))
                return nullptr;
        }
        // (exec: a name of the module that is not bound is its namespaces')
        if (py_exec_mode() && module)
            return result(py_owned(py_runtime_call("python::exec_read", { type::String, type::Object }, type::Object, { py_string(name), fragment{ aValue } })));
        bool const deleted = module ? iInference.pyDeletedGlobals.contains(name) :
            (iInference.pyDeleted.contains(current_function_name()) && iInference.pyDeleted.at(current_function_name()).contains(name));
        auto const& order = py_function().pyBindingOrder;
        bool const boundBefore = std::find(order.begin(), order.end(), name) != order.end();
        if (!deleted && (boundBefore || (global && !implicit_entry())))
            return nullptr;
        fragment checked{ aValue };
        auto const scratch = temporary(type::I32);
        put_local(checked, opcode::LocalTee, scratch);
        put(checked, opcode::I32Eqz);
        put_block(checked, opcode::If, {});
        fragment kind;
        put_i32(kind, module ? 0 : 1);
        kind.valueType = type::I32;
        append(checked, py_runtime_call("python::unbound", { type::String, type::I32 }, type::Void, { py_string(name), kind }));
        put(checked, opcode::End);
        put_local(checked, opcode::LocalGet, scratch);
        checked.valueType = type::Object;
        return result(checked);
    }

    i_fragment const* compiler::convert_value(i_fragment const& aValue, type aTarget)
    {
        fragment value{ aValue };
        py_coerce(value, aTarget);
        return result(value);
    }

    i_fragment const* compiler::division_by_zero(std::int32_t aKind)
    {
        // (ZeroDivisionError (aKind selects its message))
        fragment code;
        put_i32(code, aKind);
        emit_call_to(code, runtime_function("python::zero_division", { type::I32 }, type::Void));
        return result(code);
    }

    type compiler::argument_type(i_operand_type const& aOperand)
    {
        // (an int literal is an integer of any size (type::Integer))
        return py_type(operand_type{ aOperand });
    }

    void compiler::function_start()
    {
        // (variables declared at its start)
        py_function_start();
    }

    i_fragment const* compiler::function_end(bool aImplicit)
    {
        return result(py_function_end(aImplicit));
    }

    void compiler::finishing_function(bool aEntry)
    {
        // (the imports that py_error_checks uses: imported before references to the module's functions are taken)
        runtime_function("neos::object_release", { type::Object }, type::Void);
        if (aEntry)
            runtime_function("neos::raise", { type::String }, type::Void);
    }

    void compiler::function_finished(std::uint32_t aFunction)
    {
        py_error_checks(aFunction);
    }

    i_fragment const* compiler::finish_construct(i_indented_construct const& aConstruct)
    {
        auto statement = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
        if (aConstruct.kind() == indented_construct::With)
            py_with(aConstruct, statement);
        else
            py_try(aConstruct, statement);
        return result(statement);
    }

    namespace
    {
        // (the names of a target list of names (e.g. "(a, b)" of "with m as (a, b)"): none if it is a single name ("(a)" is a)
        std::optional<std::vector<std::string>> target_names(std::string_view aText)
        {
            auto text = trimmed(aText);
            if (!text.starts_with("(") || !text.ends_with(")") || top_level(text.substr(1u, text.size() - 2u), ',').empty())
                return std::nullopt;
            std::vector<std::string> names;
            for (auto const& part : split(text.substr(1u, text.size() - 2u), ','))
                if (auto const name = std::string{ trimmed(part) }; !name.empty())
                    names.push_back(name);
            return names;
        }
    }

    namespace
    {
        // ("(a)": a)
        std::string target_name(std::string_view aText)
        {
            auto text = trimmed(aText);
            while (text.size() > 2u && text.front() == '(' && text.back() == ')')
                text = trimmed(text.substr(1u, text.size() - 2u));
            return std::string{ text };
        }
    }

    bool compiler::bind_name(neolib::i_string_view const& aName, type aType)
    {
        if (auto const names = target_names(aName.to_std_string_view()))
        {
            for (auto const& name : *names)
                bind_name(neolib::string_view{ name }, aType);
            return true;
        }
        auto const name = target_name(aName.to_std_string_view());
        auto& variable = py_variable(name, aType);
        if (variable.type() != aType)
            py_kind_change(name);
        return true;
    }

    bool compiler::declare_loop_variable(neolib::i_string_view const& aName, i_symbol_table_entry const* aSymbol)
    {
        if (in_function() && !declaration_pass())
            if (auto& order = py_function().pyBindingOrder; std::find(order.begin(), order.end(), aName.to_std_string()) == order.end())
                order.push_back(aName.to_std_string());
        if (aSymbol == nullptr)
        {
            if (declaration_pass())
                return false;
            py_variable(aName.to_std_string(), type::I64);
            return true;
        }
        if (aSymbol->type() == type::Object || aSymbol->type() == type::Integer)
            ; // (an object: assigned an int object; an integer: assigned the counter)
        else if (!is_numeric(aSymbol->type()) || is_float(aSymbol->type()) || aSymbol->type() == type::Boolean)
            py_kind_change(aName.to_std_string());
        return true;
    }

    i_fragment const* compiler::small_integers(neolib::i_vector<std::uint32_t> const& aLocals)
    {
        return result(py_small_integers(std::vector<std::uint32_t>{ aLocals.begin(), aLocals.end() }));
    }

    void compiler::loop_variable_range(neolib::i_string_view const& aName, std::int64_t aLow, std::int64_t aHigh)
    {
        py_bound(aName.to_std_string(), interval{ aLow, aHigh });
    }

    i_fragment const* compiler::release_integer(std::uint32_t aVariable, bool aGlobal)
    {
        return result(py_release_integer(aVariable, aGlobal));
    }

    // --- constructs

    void compiler::emit_python(construct aConstruct, neolib::i_string_view const& aSource)
    {
        auto stack = operands();
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
                if (declaration_pass() || !in_function())
                    break;
                if (!stack.empty() && !(stack.back().holds_alternative<i_fragment>() && stack.back().get<i_fragment>().kind() != fragment_kind::Value))
                    generator().emit_statement(aSource);
                auto const release = py_flush();
                if (!release.fragmentCode.empty() && !stack.empty() && stack.back().holds_alternative<i_fragment>())
                {
                    fragment statement{ stack.back().get<i_fragment>() };
                    append(statement, release);
                    statement.fragmentSource = aSource;
                    stack.set_back(operand_type{ statement });
                }
            }
            break;
        case construct::Condition:
            {
                // (an if, elif or while statement's condition: its temporaries are released once it has been tested)
                if (declaration_pass())
                    break;
                // (statements following the colon on the header's line have been compiled)
                auto inlineBody = pop_fragments(aSource, fragment_kind::Statement);
                if (stack.empty())
                    throw std::runtime_error("condition expected");
                operand_type const operand{ stack.back() };
                if (py_type(operand) != type::Boolean || (in_function() && !py_function().pendingObjects.empty()))
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
                operand_type const operand{ stack.back() };
                stack.pop_back();
                fragment_state::py_target target;
                target.position = aSource.to_std_string_view().data();
                auto text = trimmed(aSource.to_std_string_view());
                // (a starred target: *name)
                if (text.starts_with("*"))
                {
                    target.starred = true;
                    text = trimmed(text.substr(1u));
                }
                if (operand.holds_alternative<i_symbol_reference>())
                    target.name = operand.get<i_symbol_reference>().name().to_std_string();
                else if (!declaration_pass() && operand.holds_alternative<i_fragment>() &&
                    state().pyAccesses.contains(operand.get<i_fragment>().source().to_std_string_view().data()))
                {
                    auto const& access = state().pyAccesses.at(operand.get<i_fragment>().source().to_std_string_view().data());
                    target.receiver = access.receiver;
                    if (access.key)
                    {
                        target.kind = fragment_state::py_target::kind_t::Item;
                        target.key = access.key;
                    }
                    else
                    {
                        target.kind = fragment_state::py_target::kind_t::Attribute;
                        target.name = access.attribute;
                    }
                }
                else if (declaration_pass())
                    target.name = std::string{ text };
                else
                    throw std::runtime_error("cannot assign to expression '" + std::string{ text } + "'");
                state().pyTargets.push_back(target);
            }
            break;
        case construct::TargetGroup:
            {
                // ((targets) or [targets]: a group of the targets within it (unless it is a parenthesized target: "(a)"))
                auto const text = aSource.to_std_string_view();
                fragment_state::py_target group;
                group.kind = fragment_state::py_target::kind_t::Group;
                group.position = text.data();
                auto& pending = state().pyTargets;
                while (!pending.empty() && pending.back().position >= text.data() && pending.back().position < text.data() + text.size())
                {
                    group.children.insert(group.children.begin(), pending.back());
                    pending.pop_back();
                }
                auto const inner = trimmed(trimmed(text).substr(1u, trimmed(text).size() - 2u));
                if (group.children.size() == 1u && trimmed(text).starts_with("(") && top_level(inner, ',').empty())
                {
                    auto child = group.children[0];
                    child.position = text.data();
                    pending.push_back(child);
                }
                else
                    pending.push_back(group);
            }
            break;
        case construct::ForTargets:
            {
                // (the loop's variables are objects: they are defined before its body is compiled)
                if (declaration_pass())
                    break;
                auto const text = aSource.to_std_string_view();
                std::vector<fragment_state::py_target> targets;
                for (auto const& target : state().pyTargets)
                    if (target.position >= text.data() && target.position < text.data() + text.size())
                        targets.push_back(target);
                py_declare_targets(targets);
            }
            break;
        case construct::ForIn:
            py_for_in(aSource);
            break;
        case construct::AsName:
            {
                // (except ... as name and with ... as name: the name is an object, defined before a body on the header's line is compiled)
                if (declaration_pass())
                    break;
                for (auto const& name : target_names(aSource.to_std_string_view()).value_or(std::vector<std::string>{ target_name(aSource.to_std_string_view()) }))
                {
                    auto& variable = py_variable(name, type::Object);
                    if (variable.type() != type::Object)
                        py_kind_change(name);
                }
            }
            break;
        case construct::Conditional:
            {
                // a if condition else b
                auto const b = py_pop();
                auto const condition = py_pop();
                auto const a = py_pop();
                if (declaration_pass())
                {
                    dummy(type::Object);
                    break;
                }
                auto const at = kind_of(py_type(a), py_big_integers());
                auto const bt = kind_of(py_type(b), py_big_integers());
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
                if (declaration_pass())
                {
                    dummy(type::Object);
                    break;
                }
                auto const at = kind_of(py_type(a), py_big_integers());
                auto const bt = kind_of(py_type(b), py_big_integers());
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
                if (declaration_pass())
                {
                    dummy(type::Object);
                    break;
                }
                auto object = py_object(receiver);
                fragment key;
                // (receiver[*a, b]: the key is a tuple of the items (a's unpacked))
                auto const starred = [&](fragment const& aItem) { return trimmed(aItem.source().to_std_string_view()).starts_with("*"); };
                if (std::any_of(items.begin(), items.end(), starred))
                {
                    auto const list = temporary(type::I32);
                    append(key, py_owned(py_runtime_call("python::list", {}, type::Object, {})));
                    put_local(key, opcode::LocalSet, list);
                    for (auto const& item : items)
                        append(key, py_runtime_call(starred(item) ? "python::args_extend" : "python::list_append", { type::Object, type::Object }, type::Void,
                            { local_value(list, type::Object), py_object(operand_type{ item }) }));
                    append(key, py_owned(py_runtime_call("python::list_tuple", { type::Object }, type::Object, { local_value(list, type::Object) })));
                    key.valueType = type::Object;
                }
                else if (items.size() == 1u && top_level(trimmed(aSource.to_std_string_view()).substr(1u), ',').empty())
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
                state().pyAccesses[aSource.to_std_string_view().data()] = fragment_state::py_access{ object, key, {} };
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
                if (declaration_pass())
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
                if (declaration_pass())
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
                state().pyAccesses[aSource.to_std_string_view().data()] = fragment_state::py_access{ object, {}, name };
                push_operand(operand_type{ result });
            }
            break;
        case construct::EmptyTuple:
        case construct::Tuple:
        case construct::List:
            {
                auto items = aConstruct == construct::EmptyTuple ? std::vector<fragment>{} : pop_fragments(aSource, fragment_kind::Argument);
                if (declaration_pass())
                {
                    dummy(type::Object);
                    break;
                }
                auto const container = temporary(type::I32);
                fragment result;
                // (*iterable items: the items are collected in a list (a tuple: then made a tuple of))
                auto const starred = [&](fragment const& aItem) { return trimmed(aItem.source().to_std_string_view()).starts_with("*"); };
                if (aConstruct != construct::List && std::any_of(items.begin(), items.end(), starred))
                {
                    append(result, py_owned(py_runtime_call("python::list", {}, type::Object, {})));
                    put_local(result, opcode::LocalSet, container);
                    for (auto const& item : items)
                        append(result, py_runtime_call(starred(item) ? "python::args_extend" : "python::list_append", { type::Object, type::Object }, type::Void,
                            { local_value(container, type::Object), py_object(operand_type{ item }) }));
                    append(result, py_owned(py_runtime_call("python::list_tuple", { type::Object }, type::Object, { local_value(container, type::Object) })));
                    result.valueType = type::Object;
                    result.fragmentSource = aSource;
                    push_operand(operand_type{ result });
                    break;
                }
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
                        append(result, py_runtime_call(starred(items[i]) ? "python::args_extend" : "python::list_append", { type::Object, type::Object }, type::Void,
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
                if (declaration_pass())
                {
                    dummy(type::Object);
                    break;
                }
                auto const dict = temporary(type::I32);
                fragment result;
                append(result, py_owned(py_runtime_call("python::dict", {}, type::Object, {})));
                put_local(result, opcode::LocalSet, dict);
                for (std::size_t i = 0u; i < items.size();)
                {
                    // (**mapping: its items)
                    if (trimmed(items[i].source().to_std_string_view()).starts_with("**"))
                    {
                        append(result, py_runtime_call("python::dict_merge", { type::Object, type::Object }, type::Void,
                            { local_value(dict, type::Object), py_object(operand_type{ items[i] }) }));
                        ++i;
                        continue;
                    }
                    if (i + 1u >= items.size())
                        break;
                    append(result, py_runtime_call("python::dict_set", { type::Object, type::Object, type::Object }, type::Void,
                        { local_value(dict, type::Object), py_object(operand_type{ items[i] }), py_object(operand_type{ items[i + 1u] }) }));
                    i += 2u;
                }
                put_local(result, opcode::LocalGet, dict);
                result.valueType = type::Object;
                push_operand(operand_type{ result });
            }
            break;
        case construct::Set:
            {
                auto items = pop_fragments(aSource, fragment_kind::Argument);
                if (declaration_pass())
                {
                    dummy(type::Object);
                    break;
                }
                auto const set = temporary(type::I32);
                fragment result;
                append(result, py_owned(py_runtime_call("python::set", {}, type::Object, {})));
                put_local(result, opcode::LocalSet, set);
                for (auto const& item : items)
                    append(result, py_runtime_call(trimmed(item.source().to_std_string_view()).starts_with("*") ? "python::set_update" : "python::set_add",
                        { type::Object, type::Object }, type::Void, { local_value(set, type::Object), py_object(operand_type{ item }) }));
                // (a display of three or more constants: CPython makes it from a frozenset constant)
                if (py_constant_display(aSource.to_std_string_view()))
                    append(result, py_runtime_call("python::set_of_constants", { type::Object }, type::Void, { local_value(set, type::Object) }));
                put_local(result, opcode::LocalGet, set);
                result.valueType = type::Object;
                push_operand(operand_type{ result });
            }
            break;
        case construct::Ellipsis:
        case construct::None:
            if (declaration_pass())
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
            state().pyComprehensions.push_back(in_function() && !declaration_pass() ? py_function().pendingObjects.size() : 0u);
            break;
        case construct::ComprehensionFor:
        case construct::ComprehensionIf:
            {
                fragment_state::py_clause clause;
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
                if (!declaration_pass())
                {
                    clause.value = aConstruct == construct::ComprehensionFor ? py_object(value) : materialize(value, type::Boolean);
                    // (the comprehension's variables are objects: the next compilation pass defines them before its expression is compiled)
                    std::vector<fragment_state::py_target> names;
                    std::function<void(std::vector<fragment_state::py_target> const&)> flatten = [&](std::vector<fragment_state::py_target> const& aTargets)
                    {
                        for (auto const& t : aTargets)
                            if (t.kind == fragment_state::py_target::kind_t::Group)
                                flatten(t.children);
                            else
                                names.push_back(t);
                    };
                    flatten(clause.targets);
                    for (auto const& target : names)
                        if (target.kind == fragment_state::py_target::kind_t::Name)
                        {
                            neolib::string const name{ target.name };
                            if (find_symbol(current_scope(), neolib::string_view{ name }) == nullptr)
                            {
                                iInference.pyLocals[current_function_name()][target.name] = type::Object;
                                { set_changed(); if (std::getenv("NEOS_PY_DEBUG")) std::fprintf(stderr, "changed at %d\n", __LINE__); }
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
                if (!declaration_pass())
                {
                    if (bare)
                        append(statement, py_runtime_call("python::reraise", {}, type::Void, {}));
                    else if (values.size() == 2u)
                        append(statement, py_runtime_call("python::raise_from", { type::Object, type::Object }, type::Void, { py_object(values[0]), py_object(values[1]) }));
                    else
                        append(statement, py_runtime_call("python::raise", { type::Object }, type::Void, { py_object(values[0]) }));
                }
                push_operand(operand_type{ statement });
            }
            break;
        case construct::Walrus:
            {
                // name := value: the value, bound to the name
                auto const value = py_pop();
                auto const text = aSource.to_std_string_view();
                std::optional<fragment_state::py_target> target;
                auto& pending = state().pyTargets;
                if (!pending.empty() && pending.back().position >= text.data() && pending.back().position < text.data() + text.size())
                {
                    target = pending.back();
                    pending.pop_back();
                }
                if (declaration_pass() || !target)
                {
                    push_operand(value);
                    break;
                }
                auto v = materialize(value);
                auto const t = v.valueType;
                auto const local = temporary(t == type::Object ? type::I32 : t);
                fragment result;
                append(result, v);
                put_local(result, opcode::LocalSet, local);
                py_bind(*target, operand_type{ local_value(local, t) }, result);
                append(result, local_value(local, t));
                result.valueType = t;
                push_operand(operand_type{ result });
            }
            break;
        case construct::Del:
            {
                // del target, ...: an item (delitem), an attribute (delattr) or a variable (None: no longer the object it was)
                auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
                auto const text = aSource.to_std_string_view();
                std::vector<fragment_state::py_target> targets;
                auto& pending = state().pyTargets;
                while (!pending.empty() && pending.back().position >= text.data() && pending.back().position < text.data() + text.size())
                {
                    targets.insert(targets.begin(), pending.back());
                    pending.pop_back();
                }
                if (!declaration_pass())
                    for (auto const& target : targets)
                        switch (target.kind)
                        {
                        case fragment_state::py_target::kind_t::Item:
                            append(statement, py_runtime_call("python::delitem", { type::Object, type::Object }, type::Void, { *target.receiver, *target.key }));
                            break;
                        case fragment_state::py_target::kind_t::Attribute:
                            append(statement, py_runtime_call("python::delattr", { type::Object, type::String }, type::Void, { *target.receiver, py_string(target.name) }));
                            break;
                        case fragment_state::py_target::kind_t::Name:
                            {
                                // (the variable is unbound: an object variable that holds no object (reading it raises NameError (or 
                                // UnboundLocalError)))
                                auto const* const symbol = find_symbol(current_scope(), neolib::string_view{ neolib::string{ target.name } });
                                bool const global = symbol != nullptr && is_global(*symbol);
                                bool const module = implicit_entry() || global;
                                if (module ? iInference.pyDeletedGlobals.insert(target.name).second : iInference.pyDeleted[current_function_name()].insert(target.name).second)
                                    set_changed();
                                if (symbol == nullptr)
                                {
                                    append(statement, py_runtime_call("python::unbound", { type::String, type::I32 }, type::Void, { py_string(target.name), [&]() { fragment k; put_i32(k, module ? 0 : 1); k.valueType = type::I32; return k; }() }));
                                    break;
                                }
                                if (symbol->type() != type::Object)
                                {
                                    py_kind_change(target.name);
                                    break;
                                }
                                // (del name of an unbound name: NameError)
                                put_local(statement, global ? opcode::GlobalGet : opcode::LocalGet, symbol->index());
                                put(statement, opcode::I32Eqz);
                                put_block(statement, opcode::If, {});
                                fragment kind;
                                put_i32(kind, module ? 0 : 1);
                                kind.valueType = type::I32;
                                append(statement, py_runtime_call("python::unbound", { type::String, type::I32 }, type::Void, { py_string(target.name), kind }));
                                put(statement, opcode::End);
                                put_local(statement, global ? opcode::GlobalGet : opcode::LocalGet, symbol->index());
                                emit_call_to(statement, runtime_function("neos::object_release", { type::Object }, type::Void));
                                put_i32(statement, 0);
                                put_local(statement, global ? opcode::GlobalSet : opcode::LocalSet, symbol->index());
                            }
                            break;
                        default:
                            py_bind(target, operand_type{ py_owned(py_runtime_call("python::none", {}, type::Object, {})) }, statement);
                            break;
                        }
                push_operand(operand_type{ statement });
            }
            break;
        case construct::Global:
            {
                // global name, ...: the names are the module's variables (globals) in this function
                auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
                if (in_function() && !implicit_entry())
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
                        auto& names = iInference.pyGlobals[current_function_name()];
                        if (names.insert(name).second || !iInference.pyFree.contains(name))
                        {
                            iInference.pyFree.insert(name);
                            set_changed();
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
                if (declaration_pass())
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
                operand_type const condition{ stack.back() };
                stack.pop_back();
                if (!declaration_pass())
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
        case construct::SetComprehension:
            py_comprehension(aSource, aConstruct == construct::DictComprehension, aConstruct == construct::SetComprehension);
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

        // (the locals that code gets, sets or tees (decoded as py_error_checks decodes a function's body): none if it cannot be decoded)
        std::set<std::uint32_t> local_references(fragment const& aCode)
        {
            std::set<std::uint32_t> result;
            auto const& code = aCode.fragmentCode;
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
            auto const relocated = [&](std::size_t aAt)
            {
                for (auto const& r : aCode.fragmentRelocations)
                    if (r.first() == aAt)
                        return true;
                return false;
            };
            while (p < code.size())
            {
                auto const start = p;
                auto const op = code[p++];
                switch (op)
                {
                case 0x02u: case 0x03u: case 0x04u:
                    {
                        if (p >= code.size())
                            return {};
                        auto const blockType = code[p];
                        if (blockType == 0x40u || (blockType >= 0x7Bu && blockType <= 0x7Fu) || blockType == 0x70u || blockType == 0x6Fu)
                            ++p;
                        else
                            leb();
                    }
                    break;
                case 0x0Cu: case 0x0Du:
                    leb();
                    break;
                case 0x0Eu:
                    for (auto n = leb() + 1u; n > 0u; --n)
                        leb();
                    break;
                case 0x10u:
                    // (a relocation's index is 5 bytes: zero until the module is emitted)
                    if (relocated(start + 1u))
                        p = start + 6u;
                    else
                        leb();
                    break;
                case 0x11u:
                    leb(); leb();
                    break;
                case 0x20u: case 0x21u: case 0x22u:
                    result.insert(static_cast<std::uint32_t>(leb()));
                    break;
                case 0x23u: case 0x24u:
                    leb();
                    break;
                case 0x3Fu: case 0x40u:
                    ++p;
                    break;
                case 0x41u:
                    if (relocated(start + 1u))
                        p = start + 6u;
                    else
                        leb();
                    break;
                case 0x42u:
                    leb();
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
                            return {};
                    }
                    break;
                default:
                    if (op >= 0x28u && op <= 0x3Eu)
                    {
                        leb(); leb();
                    }
                    else if (!(op <= 0x01u || op == 0x05u || op == 0x0Bu || op == 0x0Fu || op == 0x1Au || op == 0x1Bu || (op >= 0x45u && op <= 0xC4u)))
                        return {};
                    break;
                }
            }
            return result;
        }
    }

    std::uint32_t compiler::py_depth_global()
    {
        // (the depth of the calls of the program's functions (RecursionError: see py_error_checks))
        neolib::string const name{ "__neos_py_depth" };
        generator().function_global(neolib::string{ "__neos_py_depth" });
        if (auto* const existing = find_symbol(generator().global_scope(), neolib::string_view{ name }, true); existing != nullptr && is_global(*existing))
            return existing->index();
        auto& global = define_global("__neos_py_depth", type::I32);
        generator().function_global(neolib::string{ "__neos_py_depth" });
        return global.index();
    }

    std::uint32_t compiler::py_error_global()
    {
        neolib::string const name{ "__neos_error" };
        generator().function_global(neolib::string{ "__neos_error" });
        if (auto* const existing = find_symbol(generator().global_scope(), neolib::string_view{ name }, true); existing != nullptr && is_global(*existing))
            return existing->index();
        auto& global = define_global("__neos_error", type::I32);
        generator().function_global(neolib::string{ "__neos_error" });
        return global.index();
    }

    namespace
    {
        // (an indented construct's parts)
        struct indented_parts
        {
            std::vector<fragment> conditions;
            std::vector<fragment> arms;
            std::optional<fragment> elseArm;
            std::optional<fragment> finallyArm;
            std::vector<std::optional<fragment>> handlerTypes;
            std::vector<std::optional<std::string>> handlerNames;
        };

        indented_parts parts_of(i_indented_construct const& aConstruct)
        {
            indented_parts result;
            for (std::size_t i = 0u; i < aConstruct.condition_count(); ++i)
                result.conditions.push_back(fragment{ aConstruct.condition(i) });
            for (std::size_t i = 0u; i < aConstruct.arm_count(); ++i)
                result.arms.push_back(fragment{ aConstruct.arm(i) });
            if (aConstruct.else_arm() != nullptr)
                result.elseArm = fragment{ *aConstruct.else_arm() };
            if (aConstruct.finally_arm() != nullptr)
                result.finallyArm = fragment{ *aConstruct.finally_arm() };
            for (std::size_t i = 0u; i < aConstruct.handler_type_count(); ++i)
                result.handlerTypes.push_back(aConstruct.handler_type(i) != nullptr ? std::optional<fragment>{ fragment{ *aConstruct.handler_type(i) } } : std::nullopt);
            for (std::size_t i = 0u; i < aConstruct.handler_name_count(); ++i)
                result.handlerNames.push_back(aConstruct.handler_name(i) != nullptr ? std::optional<std::string>{ aConstruct.handler_name(i)->to_std_string() } : std::nullopt);
            return result;
        }
    }

    fragment compiler::py_release_temporaries(fragment const& aCode, std::optional<std::uint32_t> aExcept)
    {
        // the object temporaries that code uses (except aExcept): released (and zeroed) if they hold objects
        auto result = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
        if (!in_function())
            return result;
        auto const& temporaries = py_function().objectTemporaries;
        auto const release = runtime_function("neos::object_release", { type::Object }, type::Void);
        for (auto const t : local_references(aCode))
        {
            if (!temporaries.contains(t) || t == aExcept)
                continue;
            put_local(result, opcode::LocalGet, t);
            put_block(result, opcode::If, {});
            put_local(result, opcode::LocalGet, t);
            emit_call_to(result, release);
            put_i32(result, 0);
            put_local(result, opcode::LocalSet, t);
            put(result, opcode::End);
        }
        return result;
    }

    void compiler::py_try(i_indented_construct const& aView, fragment& aStatement)
    {
        auto aConstruct = parts_of(aView);
        // try: body except type as name: handler ... else: body finally: body
        auto const error = py_error_global();
        bool const finally = aConstruct.finallyArm.has_value();
        std::uint32_t const outer = finally ? 1u : 0u;
        // (returns, breaks and continues that leave the try run its finally body first (and those that leave a handler end its
        // handling of the exception before that))
        for (std::size_t i = 0u; i < aConstruct.arms.size(); ++i)
        {
            if (i == 0u)
            {
                aConstruct.arms[i] = apply_finally(aConstruct.arms[i], aConstruct.finallyArm);
                continue;
            }
            auto leave = py_runtime_call("python::handled", {}, type::Void, {});
            if (aConstruct.finallyArm)
                append(leave, *aConstruct.finallyArm);
            aConstruct.arms[i] = apply_finally(aConstruct.arms[i], leave);
        }
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
        // (an exception: the temporaries of the body's statement that raised it are released (and zeroed), as that statement's end would
        // have released them (the statements after the handler reuse them))
        append(aStatement, py_release_temporaries(aConstruct.arms[0]));
        // (an exception: the handlers)
        auto const exception = temporary(type::I32);
        py_function().objectTemporaries.insert(exception);
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
            // (an exception that leaves the handler: its handling of the exception has ended)
            put_block(aStatement, opcode::Block, std::nullopt);
            put_marker(aStatement, HandlerMarker);
            append_nested(aStatement, aConstruct.arms[i + 1u], levels + 1u);
            put(aStatement, opcode::End);
            {
                auto const pending = temporary(type::I32);
                put_local(aStatement, opcode::GlobalGet, error);
                put_local(aStatement, opcode::LocalSet, pending);
                put_local(aStatement, opcode::LocalGet, pending);
                put_block(aStatement, opcode::If, std::nullopt);
                put_i32(aStatement, 0);
                put_local(aStatement, opcode::GlobalSet, error);
                append(aStatement, py_runtime_call("python::handled", {}, type::Void, {}));
                put_local(aStatement, opcode::LocalGet, pending);
                put_local(aStatement, opcode::GlobalSet, error);
                put(aStatement, opcode::End);
                put_marker(aStatement, CheckMarker);
            }
            // (except ... as name: the name is unbound when the handler ends (as "del name"))
            if (aConstruct.handlerNames[i])
                if (auto const* symbol = find_symbol(current_scope(), neolib::string_view{ neolib::string{ *aConstruct.handlerNames[i] } }); symbol != nullptr && symbol->type() == type::Object)
                {
                    bool const global = is_global(*symbol);
                    bool const module = implicit_entry() || global;
                    if (module ? iInference.pyDeletedGlobals.insert(*aConstruct.handlerNames[i]).second : iInference.pyDeleted[current_function_name()].insert(*aConstruct.handlerNames[i]).second)
                        set_changed();
                    put_local(aStatement, global ? opcode::GlobalGet : opcode::LocalGet, symbol->index());
                    emit_call_to(aStatement, runtime_function("neos::object_release", { type::Object }, type::Void));
                    put_i32(aStatement, 0);
                    put_local(aStatement, global ? opcode::GlobalSet : opcode::LocalSet, symbol->index());
                }
            append(aStatement, py_runtime_call("python::handled", {}, type::Void, {}));
            put(aStatement, opcode::Br);
            put_u32(aStatement.fragmentCode.as_std_vector(), levels - 1u - outer);
            if (aConstruct.handlerTypes[i])
                put(aStatement, opcode::End);
        }
        // (no handler: the exception propagates (raised again))
        append(aStatement, py_runtime_call("python::handled", {}, type::Void, {}));
        append(aStatement, py_runtime_call("python::propagate", { type::Object }, type::Void, { local_value(exception, type::Object) }));
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

    void compiler::py_with(i_indented_construct const& aView, fragment& aStatement)
    {
        auto aConstruct = parts_of(aView);
        // with manager as name: body
        auto const error = py_error_global();
        auto const manager = temporary(type::I32);
        py_function().objectTemporaries.insert(manager);
        auto value = aConstruct.conditions[0];
        if (value.valueType != type::Object)
            coerce(value, type::Object);
        auto const holder = held_by(value, py_function().pendingObjects);
        append(aStatement, value);
        if (holder)
        {
            py_function().pendingObjects.erase(std::find(py_function().pendingObjects.begin(), py_function().pendingObjects.end(), *holder));
            put_local(aStatement, opcode::LocalSet, manager);
            put_i32(aStatement, 0);
            put_local(aStatement, opcode::LocalSet, *holder);
            py_function().freeObjects.push_back(*holder);
        }
        else
        {
            put_local(aStatement, opcode::LocalTee, manager);
            emit_call_to(aStatement, runtime_function("neos::object_retain", { type::Object }, type::Void));
        }
        auto entered = py_owned(py_runtime_call("python::with_enter", { type::Object }, type::Object, { local_value(manager, type::Object) }));
        if (aConstruct.handlerNames[0] && target_names(*aConstruct.handlerNames[0]))
        {
            // (with manager as (a, b): the value unpacked)
            auto const names = *target_names(*aConstruct.handlerNames[0]);
            fragment count;
            put_i64(count, static_cast<std::int64_t>(names.size()));
            count.valueType = type::I64;
            auto const sequence = temporary(type::I32);
            append(aStatement, py_owned(py_runtime_call("python::unpack", { type::Object, type::I64 }, type::Object, { entered, count })));
            put_local(aStatement, opcode::LocalSet, sequence);
            for (std::size_t i = 0u; i < names.size(); ++i)
            {
                fragment index;
                put_i64(index, static_cast<std::int64_t>(i));
                index.valueType = type::I64;
                auto const item = py_owned(py_runtime_call("python::tuple_get", { type::Object, type::I64 }, type::Object, { local_value(sequence, type::Object), index }));
                py_bind_name(names[i], operand_type{ item }, aStatement);
            }
        }
        else if (aConstruct.handlerNames[0])
            py_bind_name(target_name(*aConstruct.handlerNames[0]), operand_type{ entered }, aStatement);
        else
            append(aStatement, entered), put(aStatement, opcode::Drop);
        append(aStatement, py_flush());
        // (a return, break or continue that leaves the body: the manager's exit (without an exception) first)
        {
            fragment exit;
            append(exit, py_runtime_call("python::with_exit", { type::Object }, type::Void, { local_value(manager, type::Object) }));
            put_local(exit, opcode::LocalGet, manager);
            emit_call_to(exit, runtime_function("neos::object_release", { type::Object }, type::Void));
            put_i32(exit, 0);
            put_local(exit, opcode::LocalSet, manager);
            aConstruct.arms[0] = apply_finally(aConstruct.arms[0], exit);
        }
        put_block(aStatement, opcode::Block, std::nullopt);
        put_marker(aStatement, HandlerMarker);
        append_nested(aStatement, aConstruct.arms[0], 1u);
        put(aStatement, opcode::End);
        // (an exception: the temporaries of the body's statement that raised it are released (and zeroed), as that statement's end would
        // have released them (the statements after the with statement reuse them if the manager suppresses it))
        append(aStatement, py_release_temporaries(aConstruct.arms[0], manager));
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

    void compiler::py_error_checks(std::uint32_t aFunctionIndex)
    {
        // (the function: its body and the objects it holds)
        auto const& function = py_function(aFunctionIndex);
        fragment const functionBody{ generator().function_body(aFunctionIndex) };
        struct
        {
            std::vector<std::uint8_t> body;
            std::vector<std::pair<std::uint32_t, std::uint32_t>> relocations;
            function_signature signature;
            std::vector<std::uint32_t> const& objectVariables;
            std::set<std::uint32_t> const& objectTemporaries;
            std::vector<std::uint32_t> const& integerVariables;
            bool entry;
            std::string name;
        } aFunction{ std::vector<std::uint8_t>{ functionBody.fragmentCode.begin(), functionBody.fragmentCode.end() }, {},
            function_signature{ generator().signature_of(aFunctionIndex) }, function.objectVariables, function.objectTemporaries, function.integerVariables,
            generator().function_entry(aFunctionIndex), generator().function_name(aFunctionIndex).to_std_string() };
        for (auto const& r : functionBody.fragmentRelocations)
            aFunction.relocations.emplace_back(r.first(), r.second());
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
            if (aTarget >= generator().function_count())
                return false;
            if (generator().function_intrinsic(aTarget))
                return false;
            auto name = generator().function_name(aTarget).to_std_string();
            if (generator().function_stub(aTarget))
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
        // (the depth of calls: counted (on entry, and before it returns); beyond a few, the runtime checks it against the recursion 
        // limit (RecursionError); not a generator's body (which runs on a stack of its own))
        std::uint32_t depth = 0u;
        bool counted = !aFunction.entry && !unprefixed(aFunction.name).starts_with("_g");
        if (counted)
        {
            // (defined as the function's global (see py_function_start))
            auto const* const global = find_symbol(generator().global_scope(), neolib::string_view{ neolib::string{ "__neos_py_depth" } }, true);
            if (global != nullptr && is_global(*global))
                depth = global->index();
            else
                counted = false;
        }
        auto const put_leave = [&]()
        {
            put_op(result, opcode::GlobalGet);
            put_u32(result, depth);
            put_op(result, opcode::I32Const);
            put_s64(result, 1);
            put_op(result, opcode::I32Sub);
            put_op(result, opcode::GlobalSet);
            put_u32(result, depth);
        };
        if (counted)
        {
            put_op(result, opcode::GlobalGet);
            put_u32(result, depth);
            put_op(result, opcode::I32Const);
            put_s64(result, 1);
            put_op(result, opcode::I32Add);
            put_op(result, opcode::GlobalSet);
            put_u32(result, depth);
            put_op(result, opcode::GlobalGet);
            put_u32(result, depth);
            put_op(result, opcode::I32Const);
            put_s64(result, 64);
            put_op(result, opcode::I32GtS);
            put_op(result, opcode::If);
            result.push_back(0x40u);
            put_op(result, opcode::GlobalGet);
            put_u32(result, depth);
            put_op(result, opcode::CallFunction);
            relocations.emplace_back(static_cast<std::uint32_t>(result.size()), runtime_function("python::depth_check", { type::I32 }, type::Void));
            result.resize(result.size() + 5u);
            // (RecursionError: the function returns at once; its prologue has not retained its parameters, so they are not released 
            // (as an exception's are))
            put_op(result, opcode::GlobalGet);
            put_u32(result, error);
            put_op(result, opcode::If);
            result.push_back(0x40u);
            put_leave();
            if (auto const resultType = aFunction.signature.return_type(); resultType != type::Void)
            {
                switch (wasm_type(resultType).value_or(0x7Fu))
                {
                case 0x7Eu: put_op(result, opcode::I64Const); put_s64(result, 0); break;
                case 0x7Du: put_op(result, opcode::F32Const); put_raw(result, 0.0f); break;
                case 0x7Cu: put_op(result, opcode::F64Const); put_raw(result, 0.0); break;
                default: put_op(result, opcode::I32Const); put_s64(result, 0); break;
                }
            }
            put_op(result, opcode::Return);
            put_op(result, opcode::End);
            put_op(result, opcode::End);
            // (a function that calls nothing that can raise is not counted (it cannot recurse))
            checked = false;
        }
        try
        {
            while (p < code.size())
            {
                auto const start = p;
                auto const op = code[p++];
                if (op == 0x0Fu && counted)
                    put_leave();
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
        if (counted)
            put_leave();
        auto const& signature = aFunction.signature;
        auto const resultType = signature.return_type();
        // (the end of the function's body: it returns)
        put_op(result, opcode::GlobalGet);
        put_u32(result, error);
        put_op(result, opcode::I32Eqz);
        put_op(result, opcode::If);
        result.push_back(0x40u);
        if (aFunction.entry && !py_exec_mode())
        {
            // (the program ends: its exit functions (atexit) are called)
            put_op(result, opcode::CallFunction);
            relocations.emplace_back(static_cast<std::uint32_t>(result.size()), runtime_function("python::finish", {}, type::Void));
            result.resize(result.size() + 5u);
        }
        if (resultType == type::Void)
            put_op(result, opcode::Return);
        else
            put_op(result, opcode::Unreachable);
        put_op(result, opcode::End);
        // (an exception: the function's objects are released)
        auto const putReleases = [&]()
        {
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
            // (and its ints' integer objects)
            for (auto const local : std::set<std::uint32_t>{ aFunction.integerVariables.begin(), aFunction.integerVariables.end() })
            {
                put_op(result, opcode::LocalGet);
                put_u32(result, local);
                put_op(result, opcode::I64Const);
                put_s64(result, 1);
                put_op(result, opcode::I64And);
                put_op(result, opcode::I32ConvertI64);
                put_op(result, opcode::If);
                result.push_back(0x40u);
                put_op(result, opcode::LocalGet);
                put_u32(result, local);
                put_op(result, opcode::I64Const);
                put_s64(result, 1);
                put_op(result, opcode::I64ShrU);
                put_op(result, opcode::I32ConvertI64);
                put_op(result, opcode::CallFunction);
                relocations.emplace_back(static_cast<std::uint32_t>(result.size()), release);
                result.resize(result.size() + 5u);
                put_op(result, opcode::End);
            }
        };
        if (aFunction.entry && !py_exec_mode())
        {
            // (an exception that the program does not handle: reported (as CPython reports it (SystemExit: the status it 
            // exits with)) and the program exits; it is no longer the program's error (the report may call the program's 
            // functions, e.g. sys.excepthook); the program's objects are released after the report (as CPython finalizes
            // its modules after it): a __del__ that they run does not precede it)
            put_op(result, opcode::GlobalGet);
            put_u32(result, error);
            put_op(result, opcode::I32Const);
            put_s64(result, 0);
            put_op(result, opcode::GlobalSet);
            put_u32(result, error);
            put_op(result, opcode::CallFunction);
            relocations.emplace_back(static_cast<std::uint32_t>(result.size()), runtime_function("python::unhandled", { type::String }, type::I32));
            result.resize(result.size() + 5u);
            putReleases();
            put_op(result, opcode::CallFunction);
            relocations.emplace_back(static_cast<std::uint32_t>(result.size()), runtime_function("neos::exit", { type::I32 }, type::Void));
            result.resize(result.size() + 5u);
        }
        else
            putReleases();
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
        fragment checkedBody;
        checkedBody.fragmentKind = fragment_kind::Statement;
        checkedBody.fragmentCode.as_std_vector() = result;
        for (auto const& r : relocations)
            checkedBody.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ r.first, r.second });
        generator().set_function_body(aFunctionIndex, checkedBody);
    }
}
