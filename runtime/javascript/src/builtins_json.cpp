/*
  builtins_json.cpp

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

// The JSON object: JSON.parse (with a reviver's source text context), JSON.stringify, JSON.rawJSON and JSON.isRawJSON

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <neos/language/ecmascript.hpp>
#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace es = neos::ecmascript;

    namespace
    {
        constexpr std::size_t MaxNesting = 10000u;    ///< (the deepest nesting of arrays and objects that is parsed or serialized)

        [[noreturn]] void too_deep()
        {
            throw_range_error("Maximum call stack size exceeded");
        }

        // --- JSON.parse

        // (a JSON Parse Record: a parsed value and, for a primitive, the range of its source text; for an array its elements'
        // records; for an object its entries' records (the last definition of each key))
        struct parse_record
        {
            value val = Undefined;
            std::size_t start = 0u;
            std::size_t end = 0u;
            rooted_vector<parse_record> elements;
            rooted_vector<std::pair<value, parse_record>> entries;
            std::unique_ptr<std::unordered_map<value, std::size_t>> index;  ///< (entries' keys -> entries (built when needed))

            parse_record* entry(value aKey)
            {
                if (entries.size() <= 8u)
                {
                    for (auto& e : entries)
                        if (e.first == aKey)
                            return &e.second;
                    return nullptr;
                }
                if (!index)
                {
                    index = std::make_unique<std::unordered_map<value, std::size_t>>();
                    for (std::size_t i = 0u; i < entries.size(); ++i)
                        (*index)[entries[i].first] = i;
                }
                auto const existing = index->find(aKey);
                return existing != index->end() ? &entries[existing->second].second : nullptr;
            }
        };

        // (a parser of JSON text (ECMA-404) as UTF-16 code units)
        class json_parser
        {
        public:
            json_parser(std::u16string_view aText) : iText{ aText }
            {
            }
        public:
            // (the text's value; aRecord: its parse record (if wanted); aPrimitiveOnly: a JSON.rawJSON's text)
            value parse(parse_record* aRecord, bool aPrimitiveOnly = false)
            {
                skip_whitespace();
                if (aPrimitiveOnly && iPosition < iText.size() && (iText[iPosition] == u'[' || iText[iPosition] == u'{'))
                    fail("JSON.rawJSON cannot be an object or array");
                auto const result = parse_value(aRecord);
                skip_whitespace();
                if (iPosition != iText.size())
                    fail("Unexpected non-whitespace character after JSON");
                return result;
            }
        private:
            [[noreturn]] void fail(char const* aMessage)
            {
                std::string message = aMessage;
                if (iPosition >= iText.size())
                    message = "Unexpected end of JSON input";
                throw_syntax_error(message + " at position " + std::to_string(iPosition) + " (JSON.parse)");
            }
            [[noreturn]] void unexpected()
            {
                if (iPosition >= iText.size())
                    fail("Unexpected end of JSON input");
                auto const c = iText[iPosition];
                std::string message = "Unexpected token ";
                if (c >= 0x20u && c < 0x7Fu)
                    message += "'" + std::string(1u, static_cast<char>(c)) + "'";
                else
                {
                    char buffer[16];
                    std::snprintf(buffer, sizeof(buffer), "U+%04X", static_cast<unsigned>(c));
                    message += buffer;
                }
                fail(message.c_str());
            }
            void skip_whitespace()
            {
                while (iPosition < iText.size())
                {
                    auto const c = iText[iPosition];
                    if (c != u' ' && c != u'\t' && c != u'\n' && c != u'\r')
                        break;
                    ++iPosition;
                }
            }
            void expect_word(std::u16string_view aWord)
            {
                if (iText.substr(iPosition, aWord.size()) != aWord)
                {
                    for (std::size_t i = 0u; i < aWord.size() && iPosition < iText.size() && iText[iPosition] == aWord[i]; ++i)
                        ++iPosition;
                    unexpected();
                }
                iPosition += aWord.size();
            }
            value parse_value(parse_record* aRecord)
            {
                if (iPosition >= iText.size())
                    unexpected();
                auto const start = iPosition;
                value result;
                switch (iText[iPosition])
                {
                case u'{':
                    result = parse_object(aRecord);
                    break;
                case u'[':
                    result = parse_array(aRecord);
                    break;
                case u'"':
                    result = parse_string();
                    break;
                case u't':
                    expect_word(u"true");
                    result = True;
                    break;
                case u'f':
                    expect_word(u"false");
                    result = False;
                    break;
                case u'n':
                    expect_word(u"null");
                    result = Null;
                    break;
                default:
                    result = parse_number();
                    break;
                }
                if (aRecord != nullptr)
                {
                    aRecord->val = result;
                    aRecord->start = start;
                    aRecord->end = iPosition;
                }
                return result;
            }
            value parse_object(parse_record* aRecord)
            {
                if (++iDepth > MaxNesting)
                    too_deep();
                ++iPosition;    // ({)
                auto& o = the_heap().make<object>(the_realm().objectPrototype);
                skip_whitespace();
                if (iPosition < iText.size() && iText[iPosition] == u'}')
                    ++iPosition;
                else
                    for (;;)
                    {
                        if (iPosition >= iText.size() || iText[iPosition] != u'"')
                            unexpected();
                        auto const key = parse_key();
                        skip_whitespace();
                        if (iPosition >= iText.size() || iText[iPosition] != u':')
                            unexpected();
                        ++iPosition;
                        skip_whitespace();
                        parse_record* child = nullptr;
                        if (aRecord != nullptr)
                        {
                            // (the last definition of a key is its entry)
                            if (o.own(key) != nullptr)
                            {
                                for (auto& e : aRecord->entries)
                                    if (e.first == key)
                                    {
                                        e.second = parse_record{};
                                        child = &e.second;
                                        break;
                                    }
                            }
                            else
                                child = &aRecord->entries.emplace_back(key, parse_record{}).second;
                        }
                        auto const v = parse_value(child);
                        if (auto existing = o.own(key))
                            existing->set_data(v);
                        else
                            o.properties.add(key, v, Undefined, AttributesDefault);
                        skip_whitespace();
                        if (iPosition < iText.size() && iText[iPosition] == u',')
                        {
                            ++iPosition;
                            skip_whitespace();
                            continue;
                        }
                        if (iPosition < iText.size() && iText[iPosition] == u'}')
                        {
                            ++iPosition;
                            break;
                        }
                        unexpected();
                    }
                --iDepth;
                return value_of(o);
            }
            value parse_array(parse_record* aRecord)
            {
                if (++iDepth > MaxNesting)
                    too_deep();
                ++iPosition;    // ([)
                value_list elements;
                skip_whitespace();
                if (iPosition < iText.size() && iText[iPosition] == u']')
                    ++iPosition;
                else
                    for (;;)
                    {
                        parse_record* child = nullptr;
                        if (aRecord != nullptr)
                            child = &aRecord->elements.emplace_back();
                        elements.push_back(parse_value(child));
                        skip_whitespace();
                        if (iPosition < iText.size() && iText[iPosition] == u',')
                        {
                            ++iPosition;
                            skip_whitespace();
                            continue;
                        }
                        if (iPosition < iText.size() && iText[iPosition] == u']')
                        {
                            ++iPosition;
                            break;
                        }
                        unexpected();
                    }
                --iDepth;
                return make_array(std::move(elements));
            }
            // (a string's contents: a view of the text if it has no escapes, otherwise in the buffer)
            std::u16string_view parse_string_contents()
            {
                ++iPosition;    // (")
                auto const start = iPosition;
                while (iPosition < iText.size())
                {
                    auto const c = iText[iPosition];
                    if (c == u'"')
                        return iText.substr(start, iPosition++ - start);
                    if (c == u'\\')
                        break;
                    if (c < 0x20u)
                        unexpected();
                    ++iPosition;
                }
                iBuffer.assign(iText.substr(start, iPosition - start));
                while (iPosition < iText.size())
                {
                    auto const c = iText[iPosition];
                    if (c == u'"')
                    {
                        ++iPosition;
                        return iBuffer;
                    }
                    if (c < 0x20u)
                        unexpected();
                    if (c != u'\\')
                    {
                        // (a run of plain characters)
                        auto const run = iPosition;
                        while (iPosition < iText.size() && iText[iPosition] != u'"' && iText[iPosition] != u'\\' && iText[iPosition] >= 0x20u)
                            ++iPosition;
                        iBuffer.append(iText.substr(run, iPosition - run));
                        continue;
                    }
                    ++iPosition;
                    if (iPosition >= iText.size())
                        unexpected();
                    switch (iText[iPosition])
                    {
                    case u'"': iBuffer.push_back(u'"'); break;
                    case u'\\': iBuffer.push_back(u'\\'); break;
                    case u'/': iBuffer.push_back(u'/'); break;
                    case u'b': iBuffer.push_back(u'\b'); break;
                    case u'f': iBuffer.push_back(u'\f'); break;
                    case u'n': iBuffer.push_back(u'\n'); break;
                    case u'r': iBuffer.push_back(u'\r'); break;
                    case u't': iBuffer.push_back(u'\t'); break;
                    case u'u':
                        {
                            unsigned code = 0u;
                            for (int i = 0; i < 4; ++i)
                            {
                                ++iPosition;
                                if (iPosition >= iText.size())
                                    unexpected();
                                auto const d = iText[iPosition];
                                unsigned digit;
                                if (d >= u'0' && d <= u'9')
                                    digit = d - u'0';
                                else if (d >= u'a' && d <= u'f')
                                    digit = d - u'a' + 10u;
                                else if (d >= u'A' && d <= u'F')
                                    digit = d - u'A' + 10u;
                                else
                                    fail("Bad Unicode escape in JSON");
                                code = code * 16u + digit;
                            }
                            iBuffer.push_back(static_cast<char16_t>(code));
                        }
                        break;
                    default:
                        fail("Bad escaped character in JSON");
                    }
                    ++iPosition;
                }
                fail("Unterminated string in JSON");
            }
            value parse_string()
            {
                auto const contents = parse_string_contents();
                if (contents.empty())
                    return the_names().empty;
                if (contents.size() == 1u)
                    return make_string(contents[0]);
                return make_string(contents);
            }
            value parse_key()
            {
                return intern(parse_string_contents());
            }
            value parse_number()
            {
                auto const start = iPosition;
                auto const digit = [&]() { return iPosition < iText.size() && iText[iPosition] >= u'0' && iText[iPosition] <= u'9'; };
                bool integral = true;
                if (iText[iPosition] == u'-')
                    ++iPosition;
                if (!digit())
                    unexpected();
                if (iText[iPosition] == u'0')
                    ++iPosition;
                else
                    while (digit())
                        ++iPosition;
                if (iPosition < iText.size() && iText[iPosition] == u'.')
                {
                    integral = false;
                    ++iPosition;
                    if (!digit())
                        unexpected();
                    while (digit())
                        ++iPosition;
                }
                if (iPosition < iText.size() && (iText[iPosition] == u'e' || iText[iPosition] == u'E'))
                {
                    integral = false;
                    ++iPosition;
                    if (iPosition < iText.size() && (iText[iPosition] == u'+' || iText[iPosition] == u'-'))
                        ++iPosition;
                    if (!digit())
                        unexpected();
                    while (digit())
                        ++iPosition;
                }
                auto const length = iPosition - start;
                bool const negative = iText[start] == u'-';
                // (small integers: exact)
                if (integral && length - (negative ? 1u : 0u) <= 15u)
                {
                    double n = 0.0;
                    for (auto i = start + (negative ? 1u : 0u); i < iPosition; ++i)
                        n = n * 10.0 + static_cast<double>(iText[i] - u'0');
                    return number(negative ? -n : n);
                }
                char small[64];
                std::string large;
                char* digits = small;
                if (length >= sizeof(small))
                {
                    large.resize(length);
                    digits = large.data();
                }
                for (std::size_t i = 0u; i < length; ++i)
                    digits[i] = static_cast<char>(iText[start + i]);
                double result = 0.0;
                auto const converted = std::from_chars(digits, digits + length, result);
                if (converted.ec == std::errc::result_out_of_range)
                {
                    std::string text{ digits, length };
                    result = std::strtod(text.c_str(), nullptr);
                }
                return number(result);
            }
        private:
            std::u16string_view iText;
            std::size_t iPosition = 0u;
            std::size_t iDepth = 0u;
            std::u16string iBuffer;
        };

        // (InternalizeJSONProperty)
        value internalize_json_property(value aHolder, value aName, value aReviver, parse_record* aRecord, std::u16string_view aSource)
        {
            auto const val = get(aHolder, aName);
            auto const context = make_object();
            parse_record* record = nullptr;
            if (aRecord != nullptr && same_value(aRecord->val, val))
            {
                if (!is_object(val))
                    create_data_property_or_throw(context, ascii("source"), make_string(aSource.substr(aRecord->start, aRecord->end - aRecord->start)));
                record = aRecord;
            }
            if (is_object(val))
            {
                if (is_array(val))
                {
                    auto const length = length_of_array_like(val);
                    for (double i = 0.0; i < length; ++i)
                    {
                        auto const index = static_cast<std::uint64_t>(i);
                        auto const key = index_key(index);
                        parse_record* elementRecord = record != nullptr && index < record->elements.size() ? &record->elements[index] : nullptr;
                        auto const newElement = internalize_json_property(val, key, aReviver, elementRecord, aSource);
                        if (newElement == Undefined)
                            obj(val).delete_property(key);
                        else
                            create_data_property(val, key, newElement);
                    }
                }
                else
                {
                    auto const keys = enumerable_own_properties(val, 0);
                    for (auto const key : keys)
                    {
                        auto* entryRecord = record != nullptr ? record->entry(key) : nullptr;
                        auto const newElement = internalize_json_property(val, key, aReviver, entryRecord, aSource);
                        if (newElement == Undefined)
                            obj(val).delete_property(key);
                        else
                            create_data_property(val, key, newElement);
                    }
                }
            }
            value const arguments_[] = { aName, val, context };
            return call(aReviver, aHolder, arguments_);
        }

        value json_parse(value, arguments aArguments, value, function_object&)
        {
            auto const jsonString = to_string(argument(aArguments, 0u));
            auto const reviver = argument(aArguments, 1u);
            std::u16string_view const text = str(jsonString);
            json_parser parser{ text };
            if (!is_callable(reviver))
                return parser.parse(nullptr);
            parse_record record;
            auto const unfiltered = parser.parse(&record);
            auto const root = make_object();
            auto const rootName = the_names().empty;
            create_data_property_or_throw(root, rootName, unfiltered);
            return internalize_json_property(root, rootName, reviver, &record, text);
        }

        // --- JSON.rawJSON and JSON.isRawJSON

        value json_raw_json(value, arguments aArguments, value, function_object&)
        {
            auto const jsonString = to_string(argument(aArguments, 0u));
            std::u16string_view const text = str(jsonString);
            auto const blank = [](char16_t c) { return c == u'\t' || c == u'\n' || c == u'\r' || c == u' '; };
            if (text.empty())
                throw_syntax_error("JSON.rawJSON cannot be empty");
            if (blank(text.front()) || blank(text.back()))
                throw_syntax_error("JSON.rawJSON cannot start or end with whitespace");
            json_parser{ text }.parse(nullptr, true);
            auto& o = the_heap().make<object>(Null, object_class::RawJSON);
            o.properties.add(ascii("rawJSON"), jsonString, Undefined, AttributeEnumerable);
            o.extensible = false;
            return value_of(o);
        }

        value json_is_raw_json(value, arguments aArguments, value, function_object&)
        {
            auto const o = argument(aArguments, 0u);
            return boolean(is_object(o) && obj(o).cls == object_class::RawJSON);
        }

        // --- JSON.stringify

        void append_ascii(std::u16string& aOutput, std::string_view aText)
        {
            auto const size = aOutput.size();
            aOutput.resize(size + aText.size());
            for (std::size_t i = 0u; i < aText.size(); ++i)
                aOutput[size + i] = static_cast<unsigned char>(aText[i]);
        }

        // (QuoteJSONString (well-formed: lone surrogates are escaped))
        void quote_json_string(std::u16string& aOutput, std::u16string_view aText)
        {
            static char const sHex[] = "0123456789abcdef";
            aOutput.push_back(u'"');
            std::size_t run = 0u;
            auto const flush = [&](std::size_t aEnd)
            {
                if (aEnd > run)
                    aOutput.append(aText.substr(run, aEnd - run));
            };
            for (std::size_t i = 0u; i < aText.size(); ++i)
            {
                auto const c = aText[i];
                if (c >= 0x20u && c != u'"' && c != u'\\' && (c < 0xD800u || c >= 0xE000u))
                    continue;
                if (c >= 0xD800u && c < 0xDC00u && i + 1u < aText.size() && aText[i + 1u] >= 0xDC00u && aText[i + 1u] < 0xE000u)
                {
                    ++i;    // (a surrogate pair)
                    continue;
                }
                flush(i);
                run = i + 1u;
                aOutput.push_back(u'\\');
                switch (c)
                {
                case u'\b': aOutput.push_back(u'b'); break;
                case u'\t': aOutput.push_back(u't'); break;
                case u'\n': aOutput.push_back(u'n'); break;
                case u'\f': aOutput.push_back(u'f'); break;
                case u'\r': aOutput.push_back(u'r'); break;
                case u'"': aOutput.push_back(u'"'); break;
                case u'\\': aOutput.push_back(u'\\'); break;
                default:
                    aOutput.push_back(u'u');
                    aOutput.push_back(static_cast<char16_t>(sHex[(c >> 12) & 0xFu]));
                    aOutput.push_back(static_cast<char16_t>(sHex[(c >> 8) & 0xFu]));
                    aOutput.push_back(static_cast<char16_t>(sHex[(c >> 4) & 0xFu]));
                    aOutput.push_back(static_cast<char16_t>(sHex[c & 0xFu]));
                    break;
                }
            }
            flush(aText.size());
            aOutput.push_back(u'"');
        }

        void append_number(std::u16string& aOutput, double aValue)
        {
            if (!std::isfinite(aValue))
            {
                append_ascii(aOutput, "null");
                return;
            }
            if (std::trunc(aValue) == aValue && std::fabs(aValue) < 9007199254740992.0)
            {
                // (an integer: its digits)
                char buffer[24];
                auto const integer = static_cast<std::int64_t>(aValue);
                auto const result = std::to_chars(buffer, buffer + sizeof(buffer), integer);
                append_ascii(aOutput, std::string_view{ buffer, static_cast<std::size_t>(result.ptr - buffer) });
                return;
            }
            append_ascii(aOutput, es::number_to_string(aValue));
        }

        // (the state of a serialization (a JSON Serialization Record) and its output)
        class json_serializer
        {
        public:
            json_serializer(value aReplacerFunction, value_list const* aPropertyList, std::u16string aGap) :
                iReplacerFunction{ aReplacerFunction }, iPropertyList{ aPropertyList }, iGap{ std::move(aGap) }, iToJSON{ ascii("toJSON") }, iRawJSON{ ascii("rawJSON") }
            {
            }
        public:
            std::u16string& output() { return iOutput; }
            // (SerializeJSONProperty: false if the result is undefined (nothing is output))
            bool serialize_property(value aKey, value aHolder)
            {
                return serialize_value(get_property(aHolder, aKey), aKey, aHolder);
            }
        private:
            static value get_property(value aHolder, value aKey)
            {
                auto& h = obj(aHolder);
                if (h.cls == object_class::Ordinary)
                {
                    if (auto const p = h.own(aKey); p != nullptr && !p->accessor())
                        return p->data();
                }
                else if (h.cls == object_class::Array)
                {
                    value element;
                    if (auto const index = array_index(aKey); index && static_cast<array_object&>(h).dense_get(*index, element))
                        return element;
                }
                return get(aHolder, aKey);
            }
            bool serialize_value(value aValue, value aKey, value aHolder)
            {
                auto v = aValue;
                if (is_object(v) || is_bigint(v))
                {
                    auto const toJSON = get_v(v, iToJSON);
                    if (is_callable(toJSON))
                    {
                        value const arguments_[] = { aKey };
                        v = call(toJSON, v, arguments_);
                    }
                }
                if (iReplacerFunction != Undefined)
                {
                    value const arguments_[] = { aKey, v };
                    v = call(iReplacerFunction, aHolder, arguments_);
                }
                if (is_object(v))
                {
                    auto& o = obj(v);
                    switch (o.cls)
                    {
                    case object_class::RawJSON:
                        iOutput.append(str(get(v, iRawJSON)));
                        return true;
                    case object_class::Number:
                        v = number(to_number(v));
                        break;
                    case object_class::String:
                        v = to_string(v);
                        break;
                    case object_class::Boolean:
                    case object_class::BigInt:
                        v = o.internal();
                        break;
                    default:
                        break;
                    }
                }
                if (is_number(v))
                {
                    append_number(iOutput, as_number(v));
                    return true;
                }
                if (is_string(v))
                {
                    quote_json_string(iOutput, str(v));
                    return true;
                }
                if (v == Null)
                {
                    append_ascii(iOutput, "null");
                    return true;
                }
                if (v == True)
                {
                    append_ascii(iOutput, "true");
                    return true;
                }
                if (v == False)
                {
                    append_ascii(iOutput, "false");
                    return true;
                }
                if (is_bigint(v))
                    throw_type_error("Do not know how to serialize a BigInt");
                if (is_object(v) && !is_callable(v))
                {
                    if (is_array(v))
                        serialize_array(v);
                    else
                        serialize_object(v);
                    return true;
                }
                return false;
            }
            void enter(value aValue)
            {
                for (auto const s : iStack)
                    if (s == aValue)
                        throw_type_error("Converting circular structure to JSON");
                if (iStack.size() >= MaxNesting)
                    too_deep();
                iStack.push_back(aValue);
            }
            void leave()
            {
                iStack.pop_back();
            }
            // (a new line and the current indentation before a member (if there is a gap))
            void new_line()
            {
                if (iGap.empty())
                    return;
                iOutput.push_back(u'\n');
                iOutput.append(iIndent);
            }
            static value_list keys_of(value aObject)
            {
                auto& o = obj(aObject);
                if (o.cls != object_class::Ordinary)
                    return enumerable_own_properties(aObject, 0);
                // (an ordinary object: its enumerable string keys, indices first (ascending) then the others in order of creation)
                value_list result;
                rooted_vector<std::pair<std::uint32_t, value>> indices;
                result.reserve(o.properties.size());
                o.properties.for_each([&](property const& p)
                {
                    if (!p.enumerable() || is_symbol(p.key))
                        return;
                    if (auto const index = array_index(p.key))
                        indices.emplace_back(*index, p.key);
                    else
                        result.push_back(p.key);
                });
                if (!indices.empty())
                {
                    std::sort(indices.begin(), indices.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
                    value_list all;
                    all.reserve(indices.size() + result.size());
                    for (auto const& i : indices)
                        all.push_back(i.second);
                    all.insert(all.end(), result.begin(), result.end());
                    return all;
                }
                return result;
            }
            void serialize_object(value aValue)
            {
                enter(aValue);
                auto const stepback = iIndent.size();
                iIndent += iGap;
                value_list ownKeys;
                if (iPropertyList == nullptr)
                    ownKeys = keys_of(aValue);
                auto const& keys = iPropertyList != nullptr ? *iPropertyList : ownKeys;
                iOutput.push_back(u'{');
                bool any = false;
                for (auto const key : keys)
                {
                    auto const mark = iOutput.size();
                    if (any)
                        iOutput.push_back(u',');
                    new_line();
                    quote_json_string(iOutput, str(key));
                    iOutput.push_back(u':');
                    if (!iGap.empty())
                        iOutput.push_back(u' ');
                    if (serialize_property(key, aValue))
                        any = true;
                    else
                        iOutput.resize(mark);
                }
                iIndent.resize(stepback);
                if (any)
                    new_line();
                iOutput.push_back(u'}');
                leave();
            }
            void serialize_array(value aValue)
            {
                enter(aValue);
                auto const stepback = iIndent.size();
                iIndent += iGap;
                auto const length = length_of_array_like(aValue);
                iOutput.push_back(u'[');
                for (double i = 0.0; i < length; ++i)
                {
                    if (i != 0.0)
                        iOutput.push_back(u',');
                    new_line();
                    if (!serialize_property(index_key(static_cast<std::uint64_t>(i)), aValue))
                        append_ascii(iOutput, "null");
                }
                iIndent.resize(stepback);
                if (length > 0.0)
                    new_line();
                iOutput.push_back(u']');
                leave();
            }
        private:
            value iReplacerFunction;
            value_list const* iPropertyList;
            std::u16string iGap;
            std::u16string iIndent;
            value_list iStack;
            std::u16string iOutput;
            value iToJSON;
            value iRawJSON;
        };

        value json_stringify(value, arguments aArguments, value, function_object&)
        {
            auto const v = argument(aArguments, 0u);
            auto const replacer = argument(aArguments, 1u);
            auto space = argument(aArguments, 2u);
            value replacerFunction = Undefined;
            std::optional<value_list> propertyList;
            if (is_object(replacer))
            {
                if (is_callable(replacer))
                    replacerFunction = replacer;
                else if (is_array(replacer))
                {
                    propertyList.emplace();
                    std::unordered_set<value> seen;
                    auto const length = length_of_array_like(replacer);
                    for (double k = 0.0; k < length; ++k)
                    {
                        auto const element = get(replacer, index_key(static_cast<std::uint64_t>(k)));
                        value item = Undefined;
                        if (is_string(element))
                            item = element;
                        else if (is_number(element))
                            item = to_string(element);
                        else if (is_object(element) && (obj(element).cls == object_class::String || obj(element).cls == object_class::Number))
                            item = to_string(element);
                        if (item == Undefined)
                            continue;
                        item = intern(item);
                        if (seen.insert(item).second)
                            propertyList->push_back(item);
                    }
                }
            }
            if (is_object(space))
            {
                if (obj(space).cls == object_class::Number)
                    space = number(to_number(space));
                else if (obj(space).cls == object_class::String)
                    space = to_string(space);
            }
            std::u16string gap;
            if (is_number(space))
            {
                auto const n = std::min(10.0, to_integer_or_infinity(space));
                if (n >= 1.0)
                    gap.assign(static_cast<std::size_t>(n), u' ');
            }
            else if (is_string(space))
                gap = std::u16string{ std::u16string_view{ str(space) }.substr(0u, 10u) };
            auto const wrapper = make_object();
            auto const emptyKey = the_names().empty;
            obj(wrapper).properties.add(emptyKey, v, Undefined, AttributesDefault);
            json_serializer serializer{ replacerFunction, propertyList ? &*propertyList : nullptr, std::move(gap) };
            if (!serializer.serialize_property(emptyKey, wrapper))
                return Undefined;
            return make_string(std::move(serializer.output()));
        }
    }

    void init_json(realm& aRealm)
    {
        auto const json = make_object();
        method(json, "parse", 2u, json_parse);
        method(json, "stringify", 3u, json_stringify);
        method(json, "rawJSON", 1u, json_raw_json);
        method(json, "isRawJSON", 1u, json_is_raw_json);
        obj(json).put(aRealm.symToStringTag, ascii("JSON"), AttributeConfigurable);
        obj(aRealm.global).put(ascii("JSON"), json);
    }
}
