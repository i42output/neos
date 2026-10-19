/*
  csv.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/_csv.c and Lib/csv.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C and Python to C++ for the neos Python runtime plugin and adapted
  to its object model (see runtime/python/NOTICE).
*/

// The modules _csv (CPython 3.13's _csv.c: Dialect, reader, writer, the dialect registry, their behaviour and messages) and csv (csv.py:
// Dialect, excel, excel_tab, unix_dialect, DictReader, DictWriter and Sniffer)

#include <algorithm>
#include <climits>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            constexpr char32_t NOT_SET = static_cast<char32_t>(-1);
            constexpr char32_t EOL = static_cast<char32_t>(-2);

            enum quote_style { QUOTE_MINIMAL, QUOTE_ALL, QUOTE_NONNUMERIC, QUOTE_NONE, QUOTE_STRINGS, QUOTE_NOTNULL };
            char const* const sQuoteStyles[] = { "QUOTE_MINIMAL", "QUOTE_ALL", "QUOTE_NONNUMERIC", "QUOTE_NONE", "QUOTE_STRINGS", "QUOTE_NOTNULL" };

            enum parser_state { START_RECORD, START_FIELD, ESCAPED_CHAR, IN_FIELD, IN_QUOTED_FIELD, ESCAPE_IN_QUOTED_FIELD, QUOTE_IN_QUOTED_FIELD, EAT_CRNL,
                AFTER_ESCAPED_CRNL };

            long long sFieldLimit = 128 * 1024;

            object* dialects()
            {
                static object* const sDialects = make_dict();
                return sDialects;
            }

            std::nullptr_t csv_error(std::string const& aMessage)
            {
                ref const cls{ module_exception("_csv.Error", { "Exception" }) };
                ref const e{ cls ? invoke(cls.get(), { make_str(aMessage) }) : nullptr };
                if (e)
                    fn_raise(e.get());
                return nullptr;
            }

            // --- Dialect

            struct dialect_object
            {
                header h;
                bool doublequote = true;
                bool skipinitialspace = false;
                bool strict = false;
                int quoting = QUOTE_MINIMAL;
                char32_t delimiter = U',';
                char32_t quotechar = U'"';
                char32_t escapechar = NOT_SET;
                std::string lineterminator = "\r\n";
                std::u32string lineterminatorCharacters = U"\r\n";
            };

            neos_py_type make_dialect_type();
            neos_py_type sDialectType = make_dialect_type();

            bool is_dialect(object* aObject) { return aObject != nullptr && aObject->type == &sDialectType.base; }
            dialect_object& as_dialect(object* aObject) { return *reinterpret_cast<dialect_object*>(aObject); }

            object* char_or_none(char32_t aCharacter)
            {
                if (aCharacter == NOT_SET)
                    return new_reference(none());
                std::string text;
                append_code_point(text, aCharacter);
                return make_str(std::move(text));
            }

            bool set_char(char const* aName, char32_t& aTarget, object* aSource, char32_t aDefault, bool aOrNone)
            {
                if (aSource == nullptr)
                {
                    aTarget = aDefault;
                    return true;
                }
                aTarget = NOT_SET;
                if (aOrNone && aSource == none())
                    return true;
                auto const text = str_of_value(builtin_value(aSource));
                if (text == nullptr)
                    return (error("TypeError", std::string{ "\"" } + aName + (aOrNone ? "\" must be string or None, not " : "\" must be string, not ") +
                        type_name(aSource)), false);
                auto const characters = to_code_points(*text);
                if (characters.size() != 1u)
                    return (error("TypeError", std::string{ "\"" } + aName + "\" must be a 1-character string"), false);
                aTarget = characters[0];
                return true;
            }

            bool set_bool(bool& aTarget, object* aSource, bool aDefault)
            {
                if (aSource == nullptr)
                {
                    aTarget = aDefault;
                    return true;
                }
                auto const t = truth(aSource);
                if (t < 0)
                    return false;
                aTarget = t == 1;
                return true;
            }

            bool check_char(char const* aName, char32_t aCharacter, dialect_object const& aDialect, bool aAllowSpace)
            {
                if (aCharacter == U'\r' || aCharacter == U'\n' || (aCharacter == U' ' && !aAllowSpace))
                    return (error("ValueError", std::string{ "bad " } + aName + " value"), false);
                if (aDialect.lineterminatorCharacters.find(aCharacter) != std::u32string::npos)
                    return (error("ValueError", std::string{ "bad " } + aName + " or lineterminator value"), false);
                return true;
            }

            bool check_chars(char const* aName1, char const* aName2, char32_t aCharacter1, char32_t aCharacter2)
            {
                if (aCharacter1 == aCharacter2 && aCharacter1 != NOT_SET)
                    return (error("ValueError", std::string{ "bad " } + aName1 + " or " + aName2 + " value"), false);
                return true;
            }

            char const* const sDialectKeywords[] = { "dialect", "delimiter", "doublequote", "escapechar", "lineterminator", "quotechar", "quoting",
                "skipinitialspace", "strict" };

            // (dialect_new: Dialect(dialect, delimiter, doublequote, escapechar, lineterminator, quotechar, quoting, skipinitialspace, strict))
            object* new_dialect(std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<ref> values(9u);
                if (aArgs.size() > 9u)
                    return error("TypeError", "function takes at most 9 arguments (" + std::to_string(aArgs.size()) + " given)");
                for (std::size_t i = 0u; i < aArgs.size(); ++i)
                    values[i] = ref::borrowed(aArgs[i]);
                if (aKwargs != nullptr)
                    for (auto const& [key, value] : as<dict_object>(aKwargs)->items)
                    {
                        auto const& name = as<str_object>(key)->value;
                        auto const k = std::find_if(std::begin(sDialectKeywords), std::end(sDialectKeywords), [&](char const* n) { return name == n; });
                        if (k == std::end(sDialectKeywords))
                            return error("TypeError", "this function got an unexpected keyword argument '" + name + "'");
                        auto const index = static_cast<std::size_t>(k - std::begin(sDialectKeywords));
                        if (values[index])
                            return error("TypeError", "argument for function given by name ('" + name + "') and position (" + std::to_string(index + 1u) + ")");
                        values[index] = ref::borrowed(value);
                    }
                ref dialect = values[0];
                if (dialect)
                {
                    if (kind_of(dialect.get()) == NEOS_PY_STR)
                    {
                        auto const position = dict_find(dialects(), dialect.get());
                        if (position < 0)
                            return position == -2 ? nullptr : csv_error("unknown dialect");
                        dialect = ref::borrowed(as<dict_object>(dialects())->items[static_cast<std::size_t>(position)].second);
                    }
                    if (is_dialect(dialect.get()) && std::all_of(values.begin() + 1, values.end(), [](ref const& v) { return !v; }))
                        return dialect.release_ownership();
                    for (std::size_t i = 1u; i < values.size(); ++i)
                        if (!values[i])
                        {
                            values[i] = ref{ getattr(dialect.get(), sDialectKeywords[i]) };
                            if (!values[i])
                                clear_error();
                        }
                }
                ref result{ &(new dialect_object{ { { &sDialectType.base } } })->h.base };
                auto& self = as_dialect(result.get());
                auto const delimiter = values[1].get(), doublequote = values[2].get(), escapechar = values[3].get(), lineterminator = values[4].get(),
                    quotechar = values[5].get(), quoting = values[6].get(), skipinitialspace = values[7].get(), strict = values[8].get();
                if (!set_char("delimiter", self.delimiter, delimiter, U',', false) || !set_bool(self.doublequote, doublequote, true) ||
                    !set_char("escapechar", self.escapechar, escapechar, NOT_SET, true))
                    return nullptr;
                bool hasLineterminator = true;
                if (lineterminator != nullptr)
                {
                    if (lineterminator == none())
                        hasLineterminator = false;
                    else if (auto const text = str_of_value(builtin_value(lineterminator)))
                    {
                        self.lineterminator = *text;
                        self.lineterminatorCharacters = to_code_points(*text);
                    }
                    else
                        return error("TypeError", "\"lineterminator\" must be a string");
                }
                if (!set_char("quotechar", self.quotechar, quotechar, U'"', true))
                    return nullptr;
                if (quoting != nullptr)
                {
                    if (kind_of(quoting) != NEOS_PY_INT)
                        return error("TypeError", "\"quoting\" must be an integer");
                    long long q = 0;
                    if (!to_index(quoting, q))
                        return nullptr;
                    if (q < INT_MIN || q > INT_MAX)
                        return error("OverflowError", "Python int too large to convert to C int");
                    self.quoting = static_cast<int>(q);
                }
                if (!set_bool(self.skipinitialspace, skipinitialspace, false) || !set_bool(self.strict, strict, false))
                    return nullptr;
                if (self.quoting < QUOTE_MINIMAL || self.quoting > QUOTE_NOTNULL)
                    return error("TypeError", "bad \"quoting\" value");
                if (self.delimiter == NOT_SET)
                    return error("TypeError", "\"delimiter\" must be a 1-character string");
                if (quotechar == none() && quoting == nullptr)
                    self.quoting = QUOTE_NONE;
                if (self.quoting != QUOTE_NONE && self.quotechar == NOT_SET)
                    return error("TypeError", "quotechar must be set if quoting enabled");
                if (!hasLineterminator)
                    return error("TypeError", "lineterminator must be set");
                if (!check_char("delimiter", self.delimiter, self, true) || !check_char("escapechar", self.escapechar, self, !self.skipinitialspace) ||
                    !check_char("quotechar", self.quotechar, self, !self.skipinitialspace) ||
                    !check_chars("delimiter", "escapechar", self.delimiter, self.escapechar) ||
                    !check_chars("delimiter", "quotechar", self.delimiter, self.quotechar) ||
                    !check_chars("escapechar", "quotechar", self.escapechar, self.quotechar))
                    return nullptr;
                return result.release_ownership();
            }

            object* dialect_reduce(neos_py_context*, void*, object*, object*)
            {
                return error("TypeError", "cannot pickle 'Dialect' instances");
            }

            neos_py_type make_dialect_type()
            {
                auto type = make_type("_csv.Dialect", "_csv.Dialect", retain_object, release_object<dialect_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object* { return make_str("<_csv.Dialect object at " + object_address(aSelf) + ">"); };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& self = as_dialect(aSelf);
                    std::string_view const name{ aName };
                    if (name == "delimiter")
                        return char_or_none(self.delimiter);
                    if (name == "quotechar")
                        return char_or_none(self.quotechar);
                    if (name == "escapechar")
                        return char_or_none(self.escapechar);
                    if (name == "lineterminator")
                        return make_str(self.lineterminator);
                    if (name == "quoting")
                        return make_int(self.quoting);
                    if (name == "doublequote")
                        return make_bool(self.doublequote);
                    if (name == "skipinitialspace")
                        return make_bool(self.skipinitialspace);
                    if (name == "strict")
                        return make_bool(self.strict);
                    if (name == "__reduce__" || name == "__reduce_ex__")
                        return make_function(aName, dialect_reduce, nullptr, aSelf);
                    return nullptr;
                };
                type.setattr = [](neos_py_context*, object*, char const* aName, object*) -> int
                {
                    std::string_view const name{ aName };
                    if (name == "delimiter" || name == "quotechar" || name == "escapechar" || name == "lineterminator" || name == "quoting")
                        error("AttributeError", std::string{ "attribute '" } + aName + "' of '_csv.Dialect' objects is not writable");
                    else if (name == "doublequote" || name == "skipinitialspace" || name == "strict")
                        error("AttributeError", "readonly attribute");
                    else
                        error("AttributeError", std::string{ "'_csv.Dialect' object has no attribute '" } + aName + "' and no __dict__ for setting new attributes");
                    return -1;
                };
                return type;
            }

            // (_call_dialect: a Dialect from a dialect (or none) and keyword arguments)
            object* call_dialect(object* aDialect, object* aKwargs)
            {
                std::vector<object*> args;
                if (aDialect != nullptr)
                    args.push_back(aDialect);
                return new_dialect(args, aKwargs);
            }

            // --- reader

            struct reader_object
            {
                header h;
                object* input = nullptr;        ///< (an iterator)
                object* dialect = nullptr;
                object* fields = nullptr;       ///< (a list: the current record's)
                parser_state state = START_RECORD;
                std::u32string field;
                bool unquotedField = false;
                unsigned long lineNum = 0u;
                ~reader_object()
                {
                    release(input);
                    release(dialect);
                    release(fields);
                }
            };

            neos_py_type make_reader_type();
            neos_py_type sReaderType = make_reader_type();

            reader_object& as_reader(object* aObject) { return *reinterpret_cast<reader_object*>(aObject); }

            bool save_field(reader_object& aReader)
            {
                auto const quoting = as_dialect(aReader.dialect).quoting;
                ref field;
                if (aReader.unquotedField && aReader.field.empty() && (quoting == QUOTE_NOTNULL || quoting == QUOTE_STRINGS))
                    field = ref::borrowed(none());
                else
                {
                    field = ref{ make_str(from_code_points(aReader.field)) };
                    if (aReader.unquotedField && !aReader.field.empty() && (quoting == QUOTE_NONNUMERIC || quoting == QUOTE_STRINGS))
                    {
                        ref const floatType{ fn_builtin("float") };
                        field = ref{ invoke(floatType.get(), { field.release_ownership() }) };
                        if (!field)
                            return false;
                    }
                    aReader.field.clear();
                }
                as<list_object>(aReader.fields)->items.push_back(field.release_ownership());
                return true;
            }

            bool add_char(reader_object& aReader, char32_t aCharacter)
            {
                if (static_cast<long long>(aReader.field.size()) >= sFieldLimit)
                    return (csv_error("field larger than field limit (" + std::to_string(sFieldLimit) + ")"), false);
                aReader.field.push_back(aCharacter);
                return true;
            }

            bool process_char(reader_object& aReader, char32_t c)
            {
                auto const& dialect = as_dialect(aReader.dialect);
                switch (aReader.state)
                {
                case START_RECORD:
                    if (c == EOL)
                        break;
                    if (c == U'\n' || c == U'\r')
                    {
                        aReader.state = EAT_CRNL;
                        break;
                    }
                    aReader.state = START_FIELD;
                    [[fallthrough]];
                case START_FIELD:
                    aReader.unquotedField = true;
                    if (c == U'\n' || c == U'\r' || c == EOL)
                    {
                        if (!save_field(aReader))
                            return false;
                        aReader.state = c == EOL ? START_RECORD : EAT_CRNL;
                    }
                    else if (c == dialect.quotechar && dialect.quoting != QUOTE_NONE)
                    {
                        aReader.unquotedField = false;
                        aReader.state = IN_QUOTED_FIELD;
                    }
                    else if (c == dialect.escapechar)
                        aReader.state = ESCAPED_CHAR;
                    else if (c == U' ' && dialect.skipinitialspace)
                        ;
                    else if (c == dialect.delimiter)
                    {
                        if (!save_field(aReader))
                            return false;
                    }
                    else
                    {
                        if (!add_char(aReader, c))
                            return false;
                        aReader.state = IN_FIELD;
                    }
                    break;
                case ESCAPED_CHAR:
                    if (c == U'\n' || c == U'\r')
                    {
                        if (!add_char(aReader, c))
                            return false;
                        aReader.state = AFTER_ESCAPED_CRNL;
                        break;
                    }
                    if (c == EOL)
                        c = U'\n';
                    if (!add_char(aReader, c))
                        return false;
                    aReader.state = IN_FIELD;
                    break;
                case AFTER_ESCAPED_CRNL:
                    if (c == EOL)
                        break;
                    [[fallthrough]];
                case IN_FIELD:
                    if (c == U'\n' || c == U'\r' || c == EOL)
                    {
                        if (!save_field(aReader))
                            return false;
                        aReader.state = c == EOL ? START_RECORD : EAT_CRNL;
                    }
                    else if (c == dialect.escapechar)
                        aReader.state = ESCAPED_CHAR;
                    else if (c == dialect.delimiter)
                    {
                        if (!save_field(aReader))
                            return false;
                        aReader.state = START_FIELD;
                    }
                    else if (!add_char(aReader, c))
                        return false;
                    break;
                case IN_QUOTED_FIELD:
                    if (c == EOL)
                        ;
                    else if (c == dialect.escapechar)
                        aReader.state = ESCAPE_IN_QUOTED_FIELD;
                    else if (c == dialect.quotechar && dialect.quoting != QUOTE_NONE)
                        aReader.state = dialect.doublequote ? QUOTE_IN_QUOTED_FIELD : IN_FIELD;
                    else if (!add_char(aReader, c))
                        return false;
                    break;
                case ESCAPE_IN_QUOTED_FIELD:
                    if (c == EOL)
                        c = U'\n';
                    if (!add_char(aReader, c))
                        return false;
                    aReader.state = IN_QUOTED_FIELD;
                    break;
                case QUOTE_IN_QUOTED_FIELD:
                    if (dialect.quoting != QUOTE_NONE && c == dialect.quotechar)
                    {
                        if (!add_char(aReader, c))
                            return false;
                        aReader.state = IN_QUOTED_FIELD;
                    }
                    else if (c == dialect.delimiter)
                    {
                        if (!save_field(aReader))
                            return false;
                        aReader.state = START_FIELD;
                    }
                    else if (c == U'\n' || c == U'\r' || c == EOL)
                    {
                        if (!save_field(aReader))
                            return false;
                        aReader.state = c == EOL ? START_RECORD : EAT_CRNL;
                    }
                    else if (!dialect.strict)
                    {
                        if (!add_char(aReader, c))
                            return false;
                        aReader.state = IN_FIELD;
                    }
                    else
                    {
                        std::string message = "'";
                        append_code_point(message, dialect.delimiter);
                        message += "' expected after '";
                        append_code_point(message, dialect.quotechar);
                        message += "'";
                        return (csv_error(message), false);
                    }
                    break;
                case EAT_CRNL:
                    if (c == U'\n' || c == U'\r')
                        ;
                    else if (c == EOL)
                        aReader.state = START_RECORD;
                    else
                        return (csv_error("new-line character seen in unquoted field - do you need to open the file with newline=''?"), false);
                    break;
                }
                return true;
            }

            void reset_parser(reader_object& aReader)
            {
                release(aReader.fields);
                aReader.fields = make_list();
                aReader.field.clear();
                aReader.state = START_RECORD;
                aReader.unquotedField = false;
            }

            object* reader_next(object* aSelf)
            {
                auto& self = as_reader(aSelf);
                reset_parser(self);
                do
                {
                    ref const line{ next(self.input) };
                    if (!line)
                    {
                        if (!failed() && (!self.field.empty() || self.state == IN_QUOTED_FIELD))
                        {
                            if (as_dialect(self.dialect).strict)
                                return csv_error("unexpected end of data");
                            if (save_field(self))
                                break;
                        }
                        return nullptr;
                    }
                    auto const text = str_of_value(builtin_value(line.get()));
                    if (text == nullptr)
                        return csv_error(std::string{ "iterator should return strings, not " } + type_name(line.get()) + " (the file should be opened in text mode)");
                    if (self.fields == nullptr)
                        return csv_error("iterator has already advanced the reader");
                    ++self.lineNum;
                    for (auto c : to_code_points(*text))
                        if (!process_char(self, c))
                            return nullptr;
                    if (!process_char(self, EOL))
                        return nullptr;
                } while (self.state != START_RECORD);
                auto const fields = self.fields;
                self.fields = nullptr;
                return fields;
            }

            neos_py_type make_reader_type()
            {
                auto type = make_type("_csv.reader", "_csv.reader", retain_object, release_object<reader_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object* { return make_str("<_csv.reader object at " + object_address(aSelf) + ">"); };
                type.iter = [](neos_py_context*, object* aSelf) -> object* { return new_reference(aSelf); };
                type.next = [](neos_py_context*, object* aSelf) -> object* { return reader_next(aSelf); };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& self = as_reader(aSelf);
                    std::string_view const name{ aName };
                    if (name == "dialect")
                        return new_reference(self.dialect);
                    if (name == "line_num")
                        return make_int(static_cast<long long>(self.lineNum));
                    return nullptr;
                };
                return type;
            }

            // (PyArg_UnpackTuple(args, "", 1, 2))
            bool unpack_arguments(std::vector<object*> const& aArgs, object*& aFirst, object*& aSecond)
            {
                if (aArgs.empty())
                    return (error("TypeError", " expected at least 1 argument, got 0"), false);
                if (aArgs.size() > 2u)
                    return (error("TypeError", " expected at most 2 arguments, got " + std::to_string(aArgs.size())), false);
                aFirst = aArgs[0];
                aSecond = aArgs.size() > 1u ? aArgs[1] : nullptr;
                return true;
            }

            object* csv_reader(std::vector<object*> const& aArgs, object* aKwargs)
            {
                object* iterable = nullptr;
                object* dialect = nullptr;
                if (!unpack_arguments(aArgs, iterable, dialect))
                    return nullptr;
                ref const input{ iter(iterable) };
                if (!input)
                    return nullptr;
                ref const d{ call_dialect(dialect, aKwargs) };
                if (!d)
                    return nullptr;
                auto const result = new reader_object{ { { &sReaderType.base } } };
                result->input = new_reference(input.get());
                result->dialect = new_reference(d.get());
                reset_parser(*result);
                return &result->h.base;
            }

            // --- writer

            struct writer_object
            {
                header h;
                object* write = nullptr;
                object* dialect = nullptr;
                ~writer_object()
                {
                    release(write);
                    release(dialect);
                }
            };

            neos_py_type make_writer_type();
            neos_py_type sWriterType = make_writer_type();

            writer_object& as_writer(object* aObject) { return *reinterpret_cast<writer_object*>(aObject); }

            struct record
            {
                std::u32string text;
                int fields = 0;
            };

            // (join_append: aField null: None)
            bool join_append(dialect_object const& aDialect, record& aRecord, std::u32string const* aField, bool aQuoted)
            {
                std::u32string const empty;
                auto const& field = aField != nullptr ? *aField : empty;
                if (field.empty() && aDialect.delimiter == U' ' && aDialect.skipinitialspace)
                {
                    if (aDialect.quoting == QUOTE_NONE || (aField == nullptr && (aDialect.quoting == QUOTE_STRINGS || aDialect.quoting == QUOTE_NOTNULL)))
                        return (csv_error("empty field must be quoted if delimiter is a space and skipinitialspace is true"), false);
                    aQuoted = true;
                }
                std::u32string body;
                for (auto c : field)
                {
                    bool wantEscape = false;
                    if (c == aDialect.delimiter || c == aDialect.escapechar || c == aDialect.quotechar || c == U'\n' || c == U'\r' ||
                        aDialect.lineterminatorCharacters.find(c) != std::u32string::npos)
                    {
                        if (aDialect.quoting == QUOTE_NONE)
                            wantEscape = true;
                        else
                        {
                            if (c == aDialect.quotechar)
                            {
                                if (aDialect.doublequote)
                                    body.push_back(aDialect.quotechar);
                                else
                                    wantEscape = true;
                            }
                            else if (c == aDialect.escapechar)
                                wantEscape = true;
                            if (!wantEscape)
                                aQuoted = true;
                        }
                        if (wantEscape)
                        {
                            if (aDialect.escapechar == NOT_SET)
                                return (csv_error("need to escape, but no escapechar set"), false);
                            body.push_back(aDialect.escapechar);
                        }
                    }
                    body.push_back(c);
                }
                if (aRecord.fields > 0)
                    aRecord.text.push_back(aDialect.delimiter);
                if (aQuoted)
                    aRecord.text.push_back(aDialect.quotechar);
                aRecord.text += body;
                if (aQuoted)
                    aRecord.text.push_back(aDialect.quotechar);
                ++aRecord.fields;
                return true;
            }

            // (PyNumber_Check)
            bool is_number(object* aObject)
            {
                switch (kind_of(aObject))
                {
                case NEOS_PY_INT: case NEOS_PY_BOOL: case NEOS_PY_FLOAT: case NEOS_PY_COMPLEX:
                    return true;
                case NEOS_PY_STR: case NEOS_PY_NONE:
                    return false;
                default:
                    break;
                }
                if (is_instance(aObject))
                {
                    for (auto name : { "__index__", "__int__", "__float__", "__complex__" })
                        if (ref const method{ special_method(aObject, name) })
                            return true;
                    return false;
                }
                auto const type = python_type(aObject);
                return type != nullptr && (type->index != nullptr || type->to_float != nullptr || type->to_int != nullptr);
            }

            object* writer_writerow(object* aSelf, object* aRow)
            {
                auto& self = as_writer(aSelf);
                auto const& dialect = as_dialect(self.dialect);
                ref const iterator{ iter(aRow) };
                if (!iterator)
                {
                    if (std::string_view{ error_text() }.starts_with("TypeError"))
                    {
                        clear_error();
                        return csv_error(std::string{ "iterable expected, not " } + type_name(aRow));
                    }
                    return nullptr;
                }
                record r;
                bool nullField = false;
                for (;;)
                {
                    ref const field{ next(iterator.get()) };
                    if (!field)
                    {
                        if (failed())
                            return nullptr;
                        break;
                    }
                    bool quoted = false;
                    switch (dialect.quoting)
                    {
                    case QUOTE_NONNUMERIC: quoted = !is_number(field.get()); break;
                    case QUOTE_ALL: quoted = true; break;
                    case QUOTE_STRINGS: quoted = str_of_value(builtin_value(field.get())) != nullptr; break;
                    case QUOTE_NOTNULL: quoted = field.get() != none(); break;
                    default: break;
                    }
                    nullField = field.get() == none();
                    if (nullField)
                    {
                        if (!join_append(dialect, r, nullptr, quoted))
                            return nullptr;
                        continue;
                    }
                    std::string text;
                    if (auto const s = str_of_value(builtin_value(field.get())))
                        text = *s;
                    else if (!str_of(field.get(), text))
                        return nullptr;
                    auto const characters = to_code_points(text);
                    if (!join_append(dialect, r, &characters, quoted))
                        return nullptr;
                }
                if (r.fields > 0 && r.text.empty())
                {
                    if (dialect.quoting == QUOTE_NONE || (nullField && (dialect.quoting == QUOTE_STRINGS || dialect.quoting == QUOTE_NOTNULL)))
                        return csv_error("single empty field record must be quoted");
                    --r.fields;
                    if (!join_append(dialect, r, nullptr, true))
                        return nullptr;
                }
                r.text += dialect.lineterminatorCharacters;
                return invoke(self.write, { make_str(from_code_points(r.text)) });
            }

            object* writer_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& items = as<tuple_object>(aArgs)->items;
                std::string_view const name{ static_cast<char const*>(aData) };
                std::string const qualified = "_csv.writer." + std::string{ name };
                if (!no_keywords(aKwargs, qualified.c_str()))
                    return nullptr;
                if (items.size() != 2u)
                    return error("TypeError", qualified + "() takes exactly one argument (" + std::to_string(items.size() - 1u) + " given)");
                if (name == "writerow")
                    return writer_writerow(items[0], items[1]);
                ref const rows{ iter(items[1]) };
                if (!rows)
                    return nullptr;
                for (;;)
                {
                    ref const row{ next(rows.get()) };
                    if (!row)
                        return failed() ? nullptr : new_reference(none());
                    ref const result{ writer_writerow(items[0], row.get()) };
                    if (!result)
                        return nullptr;
                }
            }

            neos_py_type make_writer_type()
            {
                auto type = make_type("_csv.writer", "_csv.writer", retain_object, release_object<writer_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object* { return make_str("<_csv.writer object at " + object_address(aSelf) + ">"); };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& self = as_writer(aSelf);
                    std::string_view const name{ aName };
                    if (name == "dialect")
                        return new_reference(self.dialect);
                    if (name == "writerow")
                        return make_function("writerow", writer_method, const_cast<char*>("writerow"), aSelf);
                    if (name == "writerows")
                        return make_function("writerows", writer_method, const_cast<char*>("writerows"), aSelf);
                    return nullptr;
                };
                return type;
            }

            object* csv_writer(std::vector<object*> const& aArgs, object* aKwargs)
            {
                object* file = nullptr;
                object* dialect = nullptr;
                if (!unpack_arguments(aArgs, file, dialect))
                    return nullptr;
                ref const write{ getattr(file, "write") };
                if (!write)
                {
                    if (!std::string_view{ error_text() }.starts_with("AttributeError"))
                        return nullptr;
                    clear_error();
                }
                if (!write || !is_callable(write.get()))
                    return error("TypeError", "argument 1 must have a \"write\" method");
                ref const d{ call_dialect(dialect, aKwargs) };
                if (!d)
                    return nullptr;
                auto const result = new writer_object{ { { &sWriterType.base } } };
                result->write = new_reference(write.get());
                result->dialect = new_reference(d.get());
                return &result->h.base;
            }

            // --- the dialect registry

            builtin_entry const sCsvFunctions[] =
            {
                { "reader", csv_reader },
                { "writer", csv_writer },
                { "register_dialect", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        object* name = nullptr;
                        object* dialect = nullptr;
                        if (!unpack_arguments(aArgs, name, dialect))
                            return nullptr;
                        if (kind_of(builtin_value(name)) != NEOS_PY_STR)
                            return error("TypeError", "dialect name must be a string");
                        ref const d{ call_dialect(dialect, aKwargs) };
                        if (!d || dict_set(dialects(), name, d.get()) < 0)
                            return nullptr;
                        return new_reference(none());
                    } },
                { "unregister_dialect", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("unregister_dialect", aArgs, aKwargs, { "name" }, 1u, 0u, a))
                            return nullptr;
                        auto const position = dict_find(dialects(), a[0]);
                        if (position == -2)
                            return nullptr;
                        if (position < 0)
                            return csv_error("unknown dialect");
                        dict_erase(*as<dict_object>(dialects()), static_cast<std::size_t>(position));
                        return new_reference(none());
                    } },
                { "get_dialect", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("get_dialect", aArgs, aKwargs, { "name" }, 1u, 0u, a))
                            return nullptr;
                        auto const position = dict_find(dialects(), a[0]);
                        if (position == -2)
                            return nullptr;
                        if (position < 0)
                            return csv_error("unknown dialect");
                        return new_reference(as<dict_object>(dialects())->items[static_cast<std::size_t>(position)].second);
                    } },
                { "list_dialects", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("list_dialects", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        std::vector<object*> names;
                        for (auto const& item : as<dict_object>(dialects())->items)
                            names.push_back(new_reference(item.first));
                        return make_list(std::move(names));
                    } },
                { "field_size_limit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("field_size_limit", aArgs, aKwargs, { "new_limit" }, 0u, 0u, a))
                            return nullptr;
                        auto const old = sFieldLimit;
                        if (a[0] != nullptr)
                        {
                            if (kind_of(a[0]) != NEOS_PY_INT)
                                return error("TypeError", "limit must be an integer");
                            long long limit = 0;
                            if (!to_index(a[0], limit))
                                return nullptr;
                            sFieldLimit = limit;
                        }
                        return make_int(old);
                    } }
            };

            object* dialect_new(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                return new_dialect(as<tuple_object>(aArgs)->items, aKwargs);
            }

            int dialect_instancecheck(neos_py_context*, void*, object* aObject) { return is_dialect(aObject) ? 1 : 0; }
            int reader_instancecheck(neos_py_context*, void*, object* aObject) { return aObject != nullptr && aObject->type == &sReaderType.base ? 1 : 0; }
            int writer_instancecheck(neos_py_context*, void*, object* aObject) { return aObject != nullptr && aObject->type == &sWriterType.base ? 1 : 0; }

            object* cannot_instantiate(neos_py_context*, void* aData, object*, object*)
            {
                return error("TypeError", std::string{ "cannot create '" } + static_cast<char const*>(aData) + "' instances");
            }

            bool csv_internal_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sCsvFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("Dialect", context()->type_new(context(), "Dialect", "_csv.Dialect", dialect_new, nullptr, dialect_instancecheck));
                aAttributes.emplace_back("Reader", context()->type_new(context(), "reader", "_csv.reader", cannot_instantiate, const_cast<char*>("_csv.reader"),
                    reader_instancecheck));
                aAttributes.emplace_back("Writer", context()->type_new(context(), "writer", "_csv.writer", cannot_instantiate, const_cast<char*>("_csv.writer"),
                    writer_instancecheck));
                aAttributes.emplace_back("_dialects", new_reference(dialects()));
                for (int i = 0; i <= QUOTE_NOTNULL; ++i)
                    aAttributes.emplace_back(sQuoteStyles[i], make_int(i));
                aAttributes.emplace_back("Error", module_exception("_csv.Error", { "Exception" }));
                return true;
            }

            inline bool const sCsvInternal = register_module("_csv", csv_internal_module);

            // --- csv (csv.py)

            object* sDialectClass = nullptr;     ///< (csv.Dialect)

            builtin_entry const sDialectMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("Dialect.__init__", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        ref const cls{ class_of(aArgs[0]) };
                        if (cls.get() != sDialectClass)
                        {
                            ref const valid{ make_bool(true) };
                            if (setattr(aArgs[0], "_valid", valid.get()) < 0)
                                return nullptr;
                        }
                        return call_method_named(aArgs[0], "_validate", {});
                    } },
                { "_validate", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("Dialect._validate", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        ref const d{ new_dialect({ aArgs[0] }, nullptr) };
                        if (d)
                            return new_reference(none());
                        if (!std::string_view{ error_text() }.starts_with("TypeError"))
                            return nullptr;
                        auto const e = take_exception();
                        return csv_error(text_of(e.get()));
                    } }
            };

            object* make_dialect_class(char const* aName, object* aBase, std::vector<std::pair<std::string, object*>> aAttributes)
            {
                if (aBase == nullptr)
                    return make_class_of("csv", aName, {}, sDialectMethods, std::move(aAttributes));
                auto const result = make_class("csv", aName, { aBase }, aAttributes);
                for (auto& a : aAttributes)
                    release(a.second);
                return result;
            }

            // (DictReader)

            object* dict_reader_fieldnames(object* aSelf)
            {
                ref names{ getattr(aSelf, "_fieldnames") };
                if (!names)
                    return nullptr;
                ref const reader{ getattr(aSelf, "reader") };
                if (!reader)
                    return nullptr;
                if (names.get() == none())
                {
                    ref const row{ next(reader.get()) };
                    if (row)
                    {
                        if (setattr(aSelf, "_fieldnames", row.get()) < 0)
                            return nullptr;
                        names = row;
                    }
                    else if (failed())
                        return nullptr;
                }
                ref const lineNum{ getattr(reader.get(), "line_num") };
                if (!lineNum || setattr(aSelf, "line_num", lineNum.get()) < 0)
                    return nullptr;
                return names.release_ownership();
            }

            object* fieldnames_of(object* aFieldnames)
            {
                // (an iterator: a list of its items)
                if (aFieldnames != none())
                {
                    ref const i{ iter(aFieldnames) };
                    if (!i)
                        return nullptr;
                    if (i.get() == aFieldnames)
                        return b_list({ aFieldnames }, nullptr);
                }
                return new_reference(aFieldnames);
            }

            builtin_entry const sDictReaderMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        // (f, fieldnames=None, restkey=None, restval=None, dialect="excel", *args, **kwds)
                        if (aArgs.size() < 2u)
                            return error("TypeError", "DictReader.__init__() missing 1 required positional argument: 'f'");
                        static char const* const sNames[] = { "f", "fieldnames", "restkey", "restval", "dialect" };
                        std::vector<object*> a(5u, nullptr);
                        std::vector<object*> extra;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                        {
                            if (i - 1u < 5u)
                                a[i - 1u] = aArgs[i];
                            else
                                extra.push_back(aArgs[i]);
                        }
                        ref kwds{ make_dict() };
                        if (aKwargs != nullptr)
                            for (auto const& [key, value] : as<dict_object>(aKwargs)->items)
                            {
                                auto const& name = as<str_object>(key)->value;
                                auto const k = std::find_if(std::begin(sNames), std::end(sNames), [&](char const* n) { return name == n; });
                                if (k == std::end(sNames))
                                    dict_set(kwds.get(), key, value);
                                else if (a[static_cast<std::size_t>(k - std::begin(sNames))] != nullptr)
                                    return error("TypeError", "DictReader.__init__() got multiple values for argument '" + name + "'");
                                else
                                    a[static_cast<std::size_t>(k - std::begin(sNames))] = value;
                            }
                        if (a[0] == nullptr)
                            return error("TypeError", "DictReader.__init__() missing 1 required positional argument: 'f'");
                        auto const self = aArgs[0];
                        ref const fieldnames{ fieldnames_of(a[1] != nullptr ? a[1] : none()) };
                        if (!fieldnames)
                            return nullptr;
                        ref const dialect{ a[4] != nullptr ? new_reference(a[4]) : make_str("excel") };
                        std::vector<object*> readerArgs{ new_reference(a[0]), new_reference(dialect.get()) };
                        for (auto e : extra)
                            readerArgs.push_back(new_reference(e));
                        ref const args{ make_tuple(std::move(readerArgs)) };
                        ref const reader{ csv_reader(as<tuple_object>(args.get())->items, kwds.get()) };
                        if (!reader)
                            return nullptr;
                        ref const zero{ make_int(0) };
                        if (setattr(self, "_fieldnames", fieldnames.get()) < 0 || setattr(self, "restkey", a[2] != nullptr ? a[2] : none()) < 0 ||
                            setattr(self, "restval", a[3] != nullptr ? a[3] : none()) < 0 || setattr(self, "reader", reader.get()) < 0 ||
                            setattr(self, "dialect", dialect.get()) < 0 || setattr(self, "line_num", zero.get()) < 0)
                            return nullptr;
                        return new_reference(none());
                    } },
                { "__iter__", [](std::vector<object*> const& aArgs, object*) -> object* { return new_reference(aArgs.at(0u)); } },
                { "__next__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("DictReader.__next__", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        long long lineNum = 0;
                        if (!attribute_int(self, "line_num", lineNum))
                            return nullptr;
                        if (lineNum == 0)
                        {
                            ref const names{ getattr(self, "fieldnames") };
                            if (!names)
                                return nullptr;
                        }
                        ref const reader{ getattr(self, "reader") };
                        if (!reader)
                            return nullptr;
                        ref row;
                        for (;;)
                        {
                            row = ref{ next(reader.get()) };
                            if (!row)
                                return failed() ? nullptr : error("StopIteration", "");
                            ref const n{ getattr(reader.get(), "line_num") };
                            if (!n || setattr(self, "line_num", n.get()) < 0)
                                return nullptr;
                            if (kind_of(row.get()) != NEOS_PY_LIST || !as<list_object>(row.get())->items.empty())
                                break;
                        }
                        ref const names{ getattr(self, "fieldnames") };
                        if (!names)
                            return nullptr;
                        std::vector<object*> keys;
                        if (!collect(names.get(), keys))
                            return nullptr;
                        std::vector<ref> owned;
                        for (auto k : keys)
                            owned.emplace_back(k);
                        auto const& values = as<list_object>(row.get())->items;
                        ref result{ make_dict() };
                        for (std::size_t i = 0u; i < keys.size() && i < values.size(); ++i)
                            if (dict_set(result.get(), keys[i], values[i]) < 0)
                                return nullptr;
                        if (keys.size() < values.size())
                        {
                            ref const restkey{ getattr(self, "restkey") };
                            if (!restkey)
                                return nullptr;
                            std::vector<object*> rest;
                            for (std::size_t i = keys.size(); i < values.size(); ++i)
                                rest.push_back(new_reference(values[i]));
                            ref const list{ make_list(std::move(rest)) };
                            if (dict_set(result.get(), restkey.get(), list.get()) < 0)
                                return nullptr;
                        }
                        else if (keys.size() > values.size())
                        {
                            ref const restval{ getattr(self, "restval") };
                            if (!restval)
                                return nullptr;
                            for (std::size_t i = values.size(); i < keys.size(); ++i)
                                if (dict_set(result.get(), keys[i], restval.get()) < 0)
                                    return nullptr;
                        }
                        return result.release_ownership();
                    } },
                { "_fieldnames_get", [](std::vector<object*> const& aArgs, object*) -> object* { return dict_reader_fieldnames(aArgs.at(0u)); } },
                { "_fieldnames_set", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "fieldnames setter takes exactly one argument");
                        return setattr(aArgs[0], "_fieldnames", aArgs[1]) < 0 ? nullptr : new_reference(none());
                    } }
            };

            // (DictWriter)

            object* dict_to_list(object* aSelf, object* aRow)
            {
                ref const extrasaction{ getattr(aSelf, "extrasaction") };
                ref const fieldnames{ getattr(aSelf, "fieldnames") };
                ref const restval{ getattr(aSelf, "restval") };
                if (!extrasaction || !fieldnames || !restval)
                    return nullptr;
                std::vector<object*> names;
                if (!collect(fieldnames.get(), names))
                    return nullptr;
                std::vector<ref> owned;
                for (auto n : names)
                    owned.emplace_back(n);
                if (str_of_value(extrasaction.get()) != nullptr && *str_of_value(extrasaction.get()) == "raise")
                {
                    ref const keys{ call_method_named(aRow, "keys", {}) };
                    if (!keys)
                        return nullptr;
                    std::vector<object*> rowKeys;
                    if (!collect(keys.get(), rowKeys))
                        return nullptr;
                    std::vector<ref> ownedKeys;
                    for (auto k : rowKeys)
                        ownedKeys.emplace_back(k);
                    ref const fieldnameSet{ invoke(ref{ fn_builtin("set") }.get(), { new_reference(fieldnames.get()) }) };
                    if (!fieldnameSet)
                        return nullptr;
                    std::vector<std::string> wrong;
                    for (auto k : rowKeys)
                    {
                        auto const c = contains(fieldnameSet.get(), k);
                        if (c < 0)
                            return nullptr;
                        if (c == 0)
                        {
                            std::string text;
                            if (!repr_of(k, text))
                                return nullptr;
                            wrong.push_back(text);
                        }
                    }
                    if (!wrong.empty())
                    {
                        std::string message = "dict contains fields not in fieldnames: ";
                        for (std::size_t i = 0u; i < wrong.size(); ++i)
                            message += (i > 0u ? ", " : "") + wrong[i];
                        return error("ValueError", message);
                    }
                }
                std::vector<object*> values;
                for (auto n : names)
                {
                    ref const value{ call_method_named(aRow, "get", { new_reference(n), new_reference(restval.get()) }) };
                    if (!value)
                    {
                        for (auto v : values)
                            release(v);
                        return nullptr;
                    }
                    values.push_back(new_reference(value.get()));
                }
                return make_list(std::move(values));
            }

            builtin_entry const sDictWriterMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        // (f, fieldnames, restval="", extrasaction="raise", dialect="excel", *args, **kwds)
                        static char const* const sNames[] = { "f", "fieldnames", "restval", "extrasaction", "dialect" };
                        std::vector<object*> a(5u, nullptr);
                        std::vector<object*> extra;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                        {
                            if (i - 1u < 5u)
                                a[i - 1u] = aArgs[i];
                            else
                                extra.push_back(aArgs[i]);
                        }
                        ref kwds{ make_dict() };
                        if (aKwargs != nullptr)
                            for (auto const& [key, value] : as<dict_object>(aKwargs)->items)
                            {
                                auto const& name = as<str_object>(key)->value;
                                auto const k = std::find_if(std::begin(sNames), std::end(sNames), [&](char const* n) { return name == n; });
                                if (k == std::end(sNames))
                                    dict_set(kwds.get(), key, value);
                                else if (a[static_cast<std::size_t>(k - std::begin(sNames))] != nullptr)
                                    return error("TypeError", "DictWriter.__init__() got multiple values for argument '" + name + "'");
                                else
                                    a[static_cast<std::size_t>(k - std::begin(sNames))] = value;
                            }
                        if (a[0] == nullptr || a[1] == nullptr)
                            return error("TypeError", std::string{ "DictWriter.__init__() missing " } + (a[0] == nullptr && a[1] == nullptr ?
                                "2 required positional arguments: 'f' and 'fieldnames'" : a[0] == nullptr ? "1 required positional argument: 'f'" :
                                "1 required positional argument: 'fieldnames'"));
                        auto const self = aArgs[0];
                        ref const fieldnames{ fieldnames_of(a[1]) };
                        if (!fieldnames || setattr(self, "fieldnames", fieldnames.get()) < 0)
                            return nullptr;
                        ref const restval{ a[2] != nullptr ? new_reference(a[2]) : make_str("") };
                        if (setattr(self, "restval", restval.get()) < 0)
                            return nullptr;
                        ref const action{ a[3] != nullptr ? call_method_named(a[3], "lower", {}) : make_str("raise") };
                        if (!action)
                            return nullptr;
                        auto const text = str_of_value(action.get());
                        if (text == nullptr || (*text != "raise" && *text != "ignore"))
                            return error("ValueError", "extrasaction (" + text_of(action.get()) + ") must be 'raise' or 'ignore'");
                        if (setattr(self, "extrasaction", action.get()) < 0)
                            return nullptr;
                        ref const dialect{ a[4] != nullptr ? new_reference(a[4]) : make_str("excel") };
                        std::vector<object*> writerArgs{ new_reference(a[0]), new_reference(dialect.get()) };
                        for (auto e : extra)
                            writerArgs.push_back(new_reference(e));
                        ref const args{ make_tuple(std::move(writerArgs)) };
                        ref const writer{ csv_writer(as<tuple_object>(args.get())->items, kwds.get()) };
                        if (!writer || setattr(self, "writer", writer.get()) < 0)
                            return nullptr;
                        return new_reference(none());
                    } },
                { "writeheader", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("DictWriter.writeheader", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        ref const fieldnames{ getattr(aArgs[0], "fieldnames") };
                        if (!fieldnames)
                            return nullptr;
                        std::vector<object*> names;
                        if (!collect(fieldnames.get(), names))
                            return nullptr;
                        ref header{ make_dict() };
                        for (auto n : names)
                        {
                            dict_set(header.get(), n, n);
                            release(n);
                        }
                        return call_method_named(aArgs[0], "writerow", { header.release_ownership() });
                    } },
                { "_dict_to_list", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("DictWriter._dict_to_list", aArgs, aKwargs, { "rowdict" }, 1u, 0u, a))
                            return nullptr;
                        ref const list{ dict_to_list(aArgs[0], a[0]) };
                        return list ? iter(list.get()) : nullptr;
                    } },
                { "writerow", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("DictWriter.writerow", aArgs, aKwargs, { "rowdict" }, 1u, 0u, a))
                            return nullptr;
                        ref const writer{ getattr(aArgs[0], "writer") };
                        ref const row{ writer ? call_method_named(aArgs[0], "_dict_to_list", { new_reference(a[0]) }) : nullptr };
                        if (!row)
                            return nullptr;
                        return call_method_named(writer.get(), "writerow", { new_reference(row.get()) });
                    } },
                { "writerows", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("DictWriter.writerows", aArgs, aKwargs, { "rowdicts" }, 1u, 0u, a))
                            return nullptr;
                        ref const writer{ getattr(aArgs[0], "writer") };
                        ref const rows{ writer ? iter(a[0]) : nullptr };
                        if (!rows)
                            return nullptr;
                        for (;;)
                        {
                            ref const rowdict{ next(rows.get()) };
                            if (!rowdict)
                                return failed() ? nullptr : new_reference(none());
                            ref const row{ call_method_named(aArgs[0], "_dict_to_list", { new_reference(rowdict.get()) }) };
                            ref const r{ row ? call_method_named(writer.get(), "writerow", { new_reference(row.get()) }) : nullptr };
                            if (!r)
                                return nullptr;
                        }
                    } }
            };

            // (Sniffer)

            std::string text_of_character(char32_t aCharacter)
            {
                std::string text;
                append_code_point(text, aCharacter);
                return text;
            }

            // (str.count of a substring (non-overlapping))
            long long count_of(std::u32string const& aText, std::u32string const& aSub)
            {
                long long n = 0;
                for (std::size_t i = aText.find(aSub); i != std::u32string::npos; i = aText.find(aSub, i + aSub.size()))
                    ++n;
                return n;
            }

            // (an insertion ordered map)
            template <typename Key, typename Value>
            struct ordered
            {
                std::vector<std::pair<Key, Value>> items;
                Value* find(Key const& aKey)
                {
                    for (auto& i : items)
                        if (i.first == aKey)
                            return &i.second;
                    return nullptr;
                }
                Value& operator[](Key const& aKey)
                {
                    if (auto const v = find(aKey))
                        return *v;
                    items.emplace_back(aKey, Value{});
                    return items.back().second;
                }
            };

            object* guess_quote_and_delimiter(std::string const& aData, object* aDelimiters, std::string& aQuote, bool& aDoublequote,
                std::string& aDelimiter, bool& aSkip, bool& aFound)
            {
                aFound = false;
                char const* const body = R"((?:(?P=quote){2}|(?!(?P=quote)).)*+)";
                char const* const patterns[] =
                {
                    R"((?P<delim>[^\w\n"'])(?P<space> ?)(?P<quote>["'])%s(?P=quote)(?P=delim))",
                    R"((?:^|\n)(?P<quote>["'])%s(?P=quote)(?P<delim>[^\w\n"'])(?P<space> ?))",
                    R"((?P<delim>[^\w\n"'])(?P<space> ?)(?P<quote>["'])%s(?P=quote)(?:$|\n))",
                    R"((?:^|\n)(?P<quote>["'])%s(?P=quote)(?:$|\n))"
                };
                ref const compile{ module_attribute("re", "compile") };
                ref const dotall{ module_attribute("re", "DOTALL") };
                ref const multiline{ module_attribute("re", "MULTILINE") };
                if (!compile || !dotall || !multiline)
                    return nullptr;
                ref const flags{ binary(NEOS_PY_OR, dotall.get(), multiline.get(), false) };
                ref const data{ make_str(aData) };
                ref regexp;
                std::vector<object*> matches;
                std::vector<ref> owned;
                for (auto p : patterns)
                {
                    std::string pattern = p;
                    pattern.replace(pattern.find("%s"), 2u, body);
                    regexp = ref{ invoke(compile.get(), { make_str(pattern), new_reference(flags.get()) }) };
                    if (!regexp)
                        return nullptr;
                    ref const found{ call_method_named(regexp.get(), "findall", { new_reference(data.get()) }) };
                    if (!found || !collect(found.get(), matches))
                        return nullptr;
                    for (auto m : matches)
                        owned.emplace_back(m);
                    if (!matches.empty())
                        break;
                }
                if (matches.empty())
                {
                    aQuote.clear();
                    aDoublequote = false;
                    aDelimiter.clear();
                    aSkip = false;
                    return new_reference(none());
                }
                aFound = true;
                ref const groupindex{ getattr(regexp.get(), "groupindex") };
                if (!groupindex)
                    return nullptr;
                auto const group = [&](char const* aName) -> long long
                {
                    ref const key{ make_str(aName) };
                    ref const value{ getitem(groupindex.get(), key.get()) };
                    if (!value)
                    {
                        clear_error();
                        return -1;
                    }
                    long long n = 0;
                    to_index(value.get(), n);
                    return n - 1;
                };
                auto const quoteGroup = group("quote");
                auto const delimGroup = group("delim");
                auto const spaceGroup = group("space");
                ordered<std::string, long long> quotes;
                ordered<std::string, long long> delims;
                long long spaces = 0;
                for (auto m : matches)
                {
                    auto const item = [&](long long aIndex) -> std::string
                    {
                        if (kind_of(m) == NEOS_PY_TUPLE)
                            return as<str_object>(as<tuple_object>(m)->items[static_cast<std::size_t>(aIndex)])->value;
                        return as<str_object>(m)->value;
                    };
                    auto const key = item(quoteGroup);
                    if (!key.empty())
                        ++quotes[key];
                    if (delimGroup < 0)
                        continue;
                    auto const delim = item(delimGroup);
                    if (!delim.empty())
                    {
                        bool allowed = aDelimiters == nullptr || aDelimiters == none();
                        if (!allowed)
                        {
                            ref const d{ make_str(delim) };
                            auto const c = contains(aDelimiters, d.get());
                            if (c < 0)
                                return nullptr;
                            allowed = c == 1;
                        }
                        if (allowed)
                            ++delims[delim];
                    }
                    if (spaceGroup < 0)
                        continue;
                    if (!item(spaceGroup).empty())
                        ++spaces;
                }
                auto const most = [](auto const& aMap)
                {
                    auto best = aMap.items.begin();
                    for (auto i = aMap.items.begin(); i != aMap.items.end(); ++i)
                        if (i->second > best->second)
                            best = i;
                    return best;
                };
                aQuote = most(quotes)->first;
                if (!delims.items.empty())
                {
                    auto const d = most(delims);
                    aDelimiter = d->first;
                    aSkip = d->second == spaces;
                    if (aDelimiter == "\n")
                        aDelimiter.clear();
                }
                else
                {
                    aDelimiter.clear();
                    aSkip = false;
                }
                aDoublequote = false;
                if (!aDelimiter.empty())
                {
                    ref const escape{ module_attribute("re", "escape") };
                    ref const escaped{ escape ? invoke(escape.get(), { make_str(aDelimiter) }) : nullptr };
                    if (!escaped)
                        return nullptr;
                    std::string const delim = as<str_object>(escaped.get())->value;
                    std::string const space = aDelimiter != " " ? " *+" : "";
                    std::string const pattern = "(?:(?<=" + delim + ")|^)" + space + aQuote + "((?:" + aQuote + aQuote + "|[^" + aQuote + "]++)*+)" + aQuote +
                        "(?:" + delim + "|$)";
                    ref const dq{ invoke(compile.get(), { make_str(pattern), new_reference(multiline.get()) }) };
                    ref const found{ dq ? call_method_named(dq.get(), "finditer", { new_reference(data.get()) }) : nullptr };
                    if (!found)
                        return nullptr;
                    std::string const doubled = aQuote + aQuote;
                    for (;;)
                    {
                        ref const m{ next(found.get()) };
                        if (!m)
                        {
                            if (failed())
                                return nullptr;
                            break;
                        }
                        ref const one{ make_int(1) };
                        ref const g{ getitem(m.get(), one.get()) };
                        if (!g)
                            return nullptr;
                        if (as<str_object>(g.get())->value.find(doubled) != std::string::npos)
                        {
                            aDoublequote = true;
                            break;
                        }
                    }
                }
                return new_reference(none());
            }

            object* guess_delimiter(object* aSelf, std::string const& aData, object* aDelimiters, std::string& aDelimiter, bool& aSkip)
            {
                std::vector<std::u32string> data;
                {
                    auto const all = to_code_points(aData);
                    std::size_t start = 0u;
                    for (;;)
                    {
                        auto const end = all.find(U'\n', start);
                        auto const line = all.substr(start, end == std::u32string::npos ? std::u32string::npos : end - start);
                        if (!line.empty())
                            data.push_back(line);
                        if (end == std::u32string::npos)
                            break;
                        start = end + 1u;
                    }
                }
                auto const allowed = [&](char32_t aCharacter, bool& aResult) -> bool
                {
                    aResult = true;
                    if (aDelimiters == nullptr || aDelimiters == none())
                        return true;
                    ref const d{ make_str(text_of_character(aCharacter)) };
                    auto const c = contains(aDelimiters, d.get());
                    if (c < 0)
                        return false;
                    aResult = c == 1;
                    return true;
                };
                auto const skip_of = [&](char32_t aDelimiter)
                {
                    return count_of(data[0], std::u32string(1u, aDelimiter)) == count_of(data[0], std::u32string{ aDelimiter, U' ' });
                };
                using mode = std::pair<long long, long long>;   // (frequency, count)
                std::size_t const chunkLength = std::min<std::size_t>(10u, data.size());
                long long iteration = 0;
                ordered<char32_t, ordered<long long, long long>> charFrequency;
                ordered<char32_t, mode> modes;
                ordered<char32_t, mode> delims;
                std::size_t start = 0u, end = chunkLength;
                while (start < data.size())
                {
                    ++iteration;
                    for (std::size_t l = start; l < end && l < data.size(); ++l)
                    {
                        auto const& line = data[l];
                        for (char32_t c = 0u; c < 127u; ++c)
                        {
                            auto& metaFrequency = charFrequency[c];
                            auto const freq = static_cast<long long>(std::count(line.begin(), line.end(), c));
                            ++metaFrequency[freq];
                        }
                    }
                    for (auto& [c, frequencies] : charFrequency.items)
                    {
                        auto items = frequencies.items;
                        if (items.size() == 1u && items[0].first == 0)
                            continue;
                        if (items.size() > 1u)
                        {
                            auto best = items.begin();
                            for (auto i = items.begin(); i != items.end(); ++i)
                                if (i->second > best->second)
                                    best = i;
                            mode m = *best;
                            items.erase(best);
                            long long rest = 0;
                            for (auto const& i : items)
                                rest += i.second;
                            modes[c] = mode{ m.first, m.second - rest };
                        }
                        else
                            modes[c] = items[0];
                    }
                    double const total = static_cast<double>(std::min<std::size_t>(chunkLength * static_cast<std::size_t>(iteration), data.size()));
                    double consistency = 1.0;
                    double const threshold = 0.9;
                    while (delims.items.empty() && consistency >= threshold)
                    {
                        for (auto const& [k, v] : modes.items)
                            if (v.first > 0 && v.second > 0)
                            {
                                bool ok = false;
                                if (static_cast<double>(v.second) / total >= consistency)
                                {
                                    if (!allowed(k, ok))
                                        return nullptr;
                                    if (ok)
                                        delims[k] = v;
                                }
                            }
                        consistency -= 0.01;
                    }
                    if (delims.items.size() == 1u)
                    {
                        auto const delim = delims.items[0].first;
                        aDelimiter = text_of_character(delim);
                        aSkip = skip_of(delim);
                        return new_reference(none());
                    }
                    start = end;
                    end += chunkLength;
                }
                if (delims.items.empty())
                {
                    aDelimiter.clear();
                    aSkip = false;
                    return new_reference(none());
                }
                if (delims.items.size() > 1u)
                {
                    ref const preferred{ getattr(aSelf, "preferred") };
                    if (!preferred)
                        return nullptr;
                    std::vector<object*> candidates;
                    if (!collect(preferred.get(), candidates))
                        return nullptr;
                    std::vector<ref> owned;
                    for (auto c : candidates)
                        owned.emplace_back(c);
                    for (auto d : candidates)
                    {
                        auto const text = str_of_value(d);
                        if (text == nullptr)
                            continue;
                        auto const characters = to_code_points(*text);
                        if (characters.size() == 1u && delims.find(characters[0]) != nullptr)
                        {
                            aDelimiter = *text;
                            aSkip = skip_of(characters[0]);
                            return new_reference(none());
                        }
                    }
                }
                auto items = delims.items;
                std::sort(items.begin(), items.end(), [](auto const& a, auto const& b) { return std::tie(a.second, a.first) < std::tie(b.second, b.first); });
                auto const delim = items.back().first;
                aDelimiter = text_of_character(delim);
                aSkip = skip_of(delim);
                return new_reference(none());
            }

            object* sniff(object* aSelf, object* aSample, object* aDelimiters)
            {
                auto const text = str_of_value(builtin_value(aSample));
                if (text == nullptr)
                    return error("AttributeError", std::string{ "'" } + type_name(aSample) + "' object has no attribute 'replace'");
                std::string sample;
                for (std::size_t i = 0u; i < text->size(); ++i)
                {
                    if ((*text)[i] == '\r')
                    {
                        sample += '\n';
                        if (i + 1u < text->size() && (*text)[i + 1u] == '\n')
                            ++i;
                    }
                    else
                        sample += (*text)[i];
                }
                std::string quotechar, delimiter;
                bool doublequote = false, skipinitialspace = false, found = false;
                if (ref const r{ guess_quote_and_delimiter(sample, aDelimiters, quotechar, doublequote, delimiter, skipinitialspace, found) }; !r)
                    return nullptr;
                if (delimiter.empty())
                    if (ref const r{ guess_delimiter(aSelf, sample, aDelimiters, delimiter, skipinitialspace) }; !r)
                        return nullptr;
                if (delimiter.empty())
                    return csv_error("Could not determine delimiter");
                ref dialect{ make_dialect_class("dialect", sDialectClass, { { "_name", make_str("sniffed") }, { "lineterminator", make_str("\r\n") },
                    { "quoting", make_int(QUOTE_MINIMAL) } }) };
                ref const qualname{ make_str("Sniffer.sniff.<locals>.dialect") };
                if (!dialect || setattr(dialect.get(), "__qualname__", qualname.get()) < 0)
                    return nullptr;
                ref const dq{ make_bool(doublequote) };
                ref const d{ make_str(delimiter) };
                ref const q{ make_str(quotechar.empty() ? std::string{ "\"" } : quotechar) };
                // (skipinitialspace: a bool, or 0 (no delimiter from the quotes and none from the frequencies))
                ref const s{ make_bool(skipinitialspace) };
                if (setattr(dialect.get(), "doublequote", dq.get()) < 0 || setattr(dialect.get(), "delimiter", d.get()) < 0 ||
                    setattr(dialect.get(), "quotechar", q.get()) < 0 || setattr(dialect.get(), "skipinitialspace", s.get()) < 0)
                    return nullptr;
                return dialect.release_ownership();
            }

            object* has_header(object* aSelf, object* aSample)
            {
                ref const dialect{ sniff(aSelf, aSample, nullptr) };
                if (!dialect)
                    return nullptr;
                ref const stringIO{ module_attribute("io", "StringIO") };
                ref const file{ stringIO ? invoke(stringIO.get(), { new_reference(aSample) }) : nullptr };
                if (!file)
                    return nullptr;
                ref const reader{ csv_reader({ file.get(), dialect.get() }, nullptr) };
                if (!reader)
                    return nullptr;
                ref const header{ next(reader.get()) };
                if (!header)
                    return failed() ? nullptr : error("StopIteration", "");
                auto const& headerItems = as<list_object>(header.get())->items;
                auto const columns = headerItems.size();
                constexpr long long None = -2, Complex = -1;
                ordered<std::size_t, long long> columnTypes;
                for (std::size_t i = 0u; i < columns; ++i)
                    columnTypes[i] = None;
                ref const complexType{ fn_builtin("complex") };
                // (complex(text): Complex; its length if that raises ValueError or OverflowError (aOtherErrors: TypeError too))
                auto const type_of = [&](object* aText, bool aOtherErrors, long long& aResult) -> bool
                {
                    ref const value{ invoke(complexType.get(), { new_reference(aText) }) };
                    if (value)
                    {
                        aResult = Complex;
                        return true;
                    }
                    std::string_view const e{ error_text() };
                    if (!e.starts_with("ValueError") && !e.starts_with("OverflowError") && !(aOtherErrors && e.starts_with("TypeError")))
                        return false;
                    clear_error();
                    aResult = static_cast<long long>(code_point_count(as<str_object>(aText)->value));
                    return true;
                };
                long long checked = 0;
                for (;;)
                {
                    ref const row{ next(reader.get()) };
                    if (!row)
                    {
                        if (failed())
                            return nullptr;
                        break;
                    }
                    if (checked > 20)
                        break;
                    ++checked;
                    auto const& items = as<list_object>(row.get())->items;
                    if (items.size() != columns)
                        continue;
                    auto const keys = columnTypes.items;
                    for (auto const& [col, unused] : keys)
                    {
                        (void)unused;
                        long long thisType = 0;
                        if (!type_of(items[col], false, thisType))
                            return nullptr;
                        auto const current = columnTypes.find(col);
                        if (thisType != *current)
                        {
                            if (*current == None)
                                *current = thisType;
                            else
                                columnTypes.items.erase(std::find_if(columnTypes.items.begin(), columnTypes.items.end(), [&](auto const& i) { return i.first == col; }));
                        }
                    }
                }
                long long hasHeader = 0;
                for (auto const& [col, colType] : columnTypes.items)
                {
                    if (colType >= 0)
                        hasHeader += static_cast<long long>(code_point_count(as<str_object>(headerItems[col])->value)) != colType ? 1 : -1;
                    else if (colType == Complex)
                    {
                        ref const value{ invoke(complexType.get(), { new_reference(headerItems[col]) }) };
                        if (value)
                            --hasHeader;
                        else
                        {
                            std::string_view const e{ error_text() };
                            if (!e.starts_with("ValueError") && !e.starts_with("TypeError"))
                                return nullptr;
                            clear_error();
                            ++hasHeader;
                        }
                    }
                    else
                    {
                        // (None(header[col]): TypeError)
                        ++hasHeader;
                    }
                }
                return make_bool(hasHeader > 0);
            }

            builtin_entry const sSnifferMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("Sniffer.__init__", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        std::vector<object*> preferred;
                        for (auto d : { ",", "\t", ";", " ", ":" })
                            preferred.push_back(make_str(d));
                        ref const list{ make_list(std::move(preferred)) };
                        return setattr(aArgs[0], "preferred", list.get()) < 0 ? nullptr : new_reference(none());
                    } },
                { "sniff", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("Sniffer.sniff", aArgs, aKwargs, { "sample", "delimiters" }, 1u, 0u, a))
                            return nullptr;
                        return sniff(aArgs[0], a[0], a[1]);
                    } },
                { "has_header", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("Sniffer.has_header", aArgs, aKwargs, { "sample" }, 1u, 0u, a))
                            return nullptr;
                        return has_header(aArgs[0], a[0]);
                    } }
            };

            object* class_getitem()
            {
                static builtin_entry const sClassGetItem{ "__class_getitem__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "__class_getitem__ expected 1 argument");
                        return make_generic_alias(aArgs[0], aArgs[1]);
                    } };
                ref const function{ make_builtin(sClassGetItem) };
                return b_classmethod({ function.get() }, nullptr);
            }

            bool csv_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                ref const internal{ fn_import("_csv") };
                if (!internal)
                    return false;
                for (auto name : { "Error", "writer", "reader", "register_dialect", "unregister_dialect", "get_dialect", "list_dialects", "field_size_limit",
                    "QUOTE_MINIMAL", "QUOTE_ALL", "QUOTE_NONNUMERIC", "QUOTE_NONE", "QUOTE_STRINGS", "QUOTE_NOTNULL" })
                {
                    auto const value = getattr(internal.get(), name);
                    if (value == nullptr)
                        return false;
                    aAttributes.emplace_back(name, value);
                }
                aAttributes.emplace_back("_Dialect", getattr(internal.get(), "Dialect"));
                ref const stringIO{ module_attribute("io", "StringIO") };
                if (!stringIO)
                    return false;
                aAttributes.emplace_back("StringIO", new_reference(stringIO.get()));
                std::vector<object*> all;
                for (auto name : { "QUOTE_MINIMAL", "QUOTE_ALL", "QUOTE_NONNUMERIC", "QUOTE_NONE", "QUOTE_STRINGS", "QUOTE_NOTNULL", "Error", "Dialect", "excel",
                    "excel_tab", "field_size_limit", "reader", "writer", "register_dialect", "get_dialect", "list_dialects", "Sniffer", "unregister_dialect",
                    "DictReader", "DictWriter", "unix_dialect" })
                    all.push_back(make_str(name));
                aAttributes.emplace_back("__all__", make_list(std::move(all)));
                aAttributes.emplace_back("__version__", make_str("1.0"));
                // (Dialect, excel, excel_tab and unix_dialect, registered)
                if (sDialectClass == nullptr)
                    sDialectClass = make_dialect_class("Dialect", nullptr, { { "_name", make_str("") }, { "_valid", make_bool(false) },
                        { "delimiter", new_reference(none()) }, { "quotechar", new_reference(none()) }, { "escapechar", new_reference(none()) },
                        { "doublequote", new_reference(none()) }, { "skipinitialspace", new_reference(none()) }, { "lineterminator", new_reference(none()) },
                        { "quoting", new_reference(none()) } });
                if (sDialectClass == nullptr)
                    return false;
                aAttributes.emplace_back("Dialect", new_reference(sDialectClass));
                auto const excel = make_dialect_class("excel", sDialectClass, { { "delimiter", make_str(",") }, { "quotechar", make_str("\"") },
                    { "doublequote", make_bool(true) }, { "skipinitialspace", make_bool(false) }, { "lineterminator", make_str("\r\n") },
                    { "quoting", make_int(QUOTE_MINIMAL) } });
                if (excel == nullptr)
                    return false;
                aAttributes.emplace_back("excel", excel);
                auto const excelTab = make_dialect_class("excel_tab", excel, { { "delimiter", make_str("\t") } });
                if (excelTab == nullptr)
                    return false;
                aAttributes.emplace_back("excel_tab", excelTab);
                auto const unixDialect = make_dialect_class("unix_dialect", sDialectClass, { { "delimiter", make_str(",") }, { "quotechar", make_str("\"") },
                    { "doublequote", make_bool(true) }, { "skipinitialspace", make_bool(false) }, { "lineterminator", make_str("\n") },
                    { "quoting", make_int(QUOTE_ALL) } });
                if (unixDialect == nullptr)
                    return false;
                aAttributes.emplace_back("unix_dialect", unixDialect);
                for (auto const& [name, cls] : { std::pair{ "excel", excel }, std::pair{ "excel-tab", excelTab }, std::pair{ "unix", unixDialect } })
                {
                    ref const key{ make_str(name) };
                    ref const d{ call_dialect(cls, nullptr) };
                    if (!d || dict_set(dialects(), key.get(), d.get()) < 0)
                        return false;
                }
                // (DictReader, DictWriter and Sniffer)
                ref const dictReader{ make_class_of("csv", "DictReader", {}, sDictReaderMethods, { { "__class_getitem__", class_getitem() } }) };
                if (!dictReader)
                    return false;
                ref const getter{ getattr(dictReader.get(), "_fieldnames_get") };
                ref const setter{ getattr(dictReader.get(), "_fieldnames_set") };
                ref const property{ getter && setter ? b_property({ getter.get(), setter.get() }, nullptr) : nullptr };
                if (!property || class_setattr_raw(dictReader.get(), "fieldnames", property.get()) < 0 ||
                    class_setattr_raw(dictReader.get(), "_fieldnames_get", nullptr) < 0 || class_setattr_raw(dictReader.get(), "_fieldnames_set", nullptr) < 0)
                    return false;
                aAttributes.emplace_back("DictReader", new_reference(dictReader.get()));
                aAttributes.emplace_back("DictWriter", make_class_of("csv", "DictWriter", {}, sDictWriterMethods, { { "__class_getitem__", class_getitem() } }));
                aAttributes.emplace_back("Sniffer", make_class_of("csv", "Sniffer", {}, sSnifferMethods));
                return true;
            }

            inline bool const sCsv = register_module("csv", csv_module);
        }
    }
}
