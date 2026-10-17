/*
  string.cpp

  Copyright (c) 2024 Leigh Johnston.  All Rights Reserved.

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

#include <neos/i_context.hpp>
#include <neos/language/semantic_concept.hpp>
#include <neos/language/type.hpp>
#include "string.hpp"

namespace neos::concepts::core
{   
    template <char8_t First, char8_t Last>
    struct string_utf8_character_range
    {
        static constexpr char8_t first = First;
        static constexpr char8_t last = Last;
    };

    template <char8_t First, char8_t Last = First>
    using specific_char = string_utf8_character_range<First, Last>;
    using any_char = string_utf8_character_range<char8_t{ 0x00 }, char8_t{ 0xFF }>;

    namespace
    {
        void append_utf8(std::string& aResult, std::uint32_t c)
        {
            if (c < 0x80u)
                aResult += static_cast<char>(c);
            else if (c < 0x800u)
            {
                aResult += static_cast<char>(0xC0u | (c >> 6));
                aResult += static_cast<char>(0x80u | (c & 0x3Fu));
            }
            else if (c < 0x10000u)
            {
                aResult += static_cast<char>(0xE0u | (c >> 12));
                aResult += static_cast<char>(0x80u | ((c >> 6) & 0x3Fu));
                aResult += static_cast<char>(0x80u | (c & 0x3Fu));
            }
            else
            {
                aResult += static_cast<char>(0xF0u | (c >> 18));
                aResult += static_cast<char>(0x80u | ((c >> 12) & 0x3Fu));
                aResult += static_cast<char>(0x80u | ((c >> 6) & 0x3Fu));
                aResult += static_cast<char>(0x80u | (c & 0x3Fu));
            }
        }

        std::string unescape_string(std::string_view const& aSource)
        {
            std::string result;
            for (std::size_t i = 0u; i < aSource.size(); ++i)
            {
                // (a line break in the source is a line feed)
                if (aSource[i] == '\r' && i + 1u < aSource.size() && aSource[i + 1u] == '\n')
                    continue;
                if (aSource[i] != '\\' || i + 1u == aSource.size())
                {
                    result += aSource[i];
                    continue;
                }
                switch (aSource[++i])
                {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case '\r': if (i + 1u < aSource.size() && aSource[i + 1u] == '\n') ++i; break; // (a backslash continues the line)
                case '\n': break;
                case 'a': result += '\a'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'v': result += '\v'; break;
                case 'x':
                case 'u':
                case 'U':
                    {
                        // (a code point: \xhh, \uhhhh or \Uhhhhhhhh, as UTF-8)
                        std::size_t const digits = aSource[i] == 'x' ? 2u : aSource[i] == 'u' ? 4u : 8u;
                        if (i + digits < aSource.size())
                        {
                            auto const c = static_cast<std::uint32_t>(std::stoul(std::string{ aSource.substr(i + 1u, digits) }, nullptr, 16));
                            i += digits;
                            append_utf8(result, c);
                        }
                    }
                    break;
                case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
                    {
                        // (an octal escape: up to three digits)
                        std::uint32_t c = 0u;
                        std::size_t n = 0u;
                        for (; n < 3u && i < aSource.size() && aSource[i] >= '0' && aSource[i] <= '7'; ++n, ++i)
                            c = c * 8u + static_cast<std::uint32_t>(aSource[i] - '0');
                        --i;
                        append_utf8(result, c);
                    }
                    break;
                default: result += aSource[i]; break;
                }
            }
            return result;
        }

        bool is_character_concept(i_semantic_concept const& aConcept)
        {
            return aConcept.name().to_std_string_view().starts_with("string.utf8.character");
        }
    }

    template <typename... Ranges>
    class string_utf8_character : public semantic_concept<string_utf8_character<Ranges...>>
    {
        // types
    public:
        typedef semantic_concept<string_utf8_character<Ranges...>> base_type;
        typedef typename base_type::source_iterator source_iterator;
        typedef char representation_type;
        // construction
    public:
        using base_type::base_type;
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            // (a character literal absorbs the (escaped) character it encloses)
            return (is_character_concept(aRhs) && aRhs.source().size() < this->source().size()) || aRhs.name() == "math.expression.operand";
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (is_character_concept(aRhs))
                aResult = this->instance();
            else
            {
                auto literal = this->source().to_std_string_view();
                if (literal.size() >= 2u && literal.front() == '\'' && literal.back() == '\'')
                    literal = literal.substr(1u, literal.size() - 2u); // (a character literal)
                auto const value = unescape_string(literal);
                if (value.size() != 1u)
                    throw std::runtime_error("invalid character literal");
                aContext.compiler().push_operand(language::data<language::u8>{ static_cast<language::u8>(value[0]) });
            }
        }
    };

    class string_utf8 : public semantic_concept<string_utf8>
    {
        // types
    public:
        typedef semantic_concept<string_utf8> base_type;
        typedef typename base_type::source_iterator source_iterator;
        using data_type = neolib::string;
        // construction
    public:
        using base_type::base_type;
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            return is_character_concept(aRhs) || aRhs.name() == "string.utf8" || aRhs.name() == "string.utf8.empty" || aRhs.name() == "math.expression.operand";
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (is_character_concept(aRhs) || aRhs.name() == "string.utf8.empty")
            {
                value();
                aResult = instance();
            }
            else if (aRhs.name() == "string.utf8")
            {
                auto const rhsValue = aRhs.holds_data() ? 
                    aRhs.data<neolib::i_string>().to_std_string() : unescape_string(aRhs.source().to_std_string_view());
                value() += neolib::string{ rhsValue };
                aResult = instance();
            }
            else
                aContext.compiler().push_operand(language::data<language::string>{ value() });
        }
    private:
        neolib::string& value()
        {
            if (!holds_data())
            {
                auto const unescaped = unescape_string(source().to_std_string_view());
                data<neolib::string>() = neolib::string{ unescaped };
            }
            return data<neolib::string>();
        }
    };

    class string_utf8_empty : public semantic_concept<string_utf8_empty>
    {
        // types
    public:
        typedef semantic_concept<string_utf8_empty> base_type;
        // construction
    public:
        using base_type::base_type;
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            return aRhs.name() == "string.utf8.empty" || aRhs.name() == "math.expression.operand";
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            // an empty string literal (a pair of quotes), possibly concatenated with another
            if (aRhs.name() == "string.utf8.empty")
                aResult = instance();
            else
                aContext.compiler().push_operand(language::data<language::string>{ neolib::string{} });
        }
    };

    string::string(neos::language::i_concept_library& aParent) :
        neos::language::concept_library
        { 
            aParent,
            library_id(), 
            aParent.uri().to_std_string(),
            library_name(), 
            "Core string concepts", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2024 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        concepts()[neolib::string{ "string" }] = 
            neolib::make_ref<neos::language::unimplemented_semantic_concept>("string");
        concepts()[neolib::string{ "string.character" }] = 
            neolib::make_ref<neos::language::unimplemented_semantic_concept>("string.character");
        concepts()[neolib::string{ "string.utf8" }] = 
            neolib::make_ref<string_utf8>("string.utf8");
        concepts()[neolib::string{ "string.utf8.empty" }] = 
            neolib::make_ref<string_utf8_empty>("string.utf8.empty");
        concepts()[neolib::string{ "string.utf8.character" }] = 
            neolib::make_ref<string_utf8_character<any_char>>("string.utf8.character");
        concepts()[neolib::string{ "string.utf8.character.LF" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'\n'>>>("string.utf8.character.LF");
        concepts()[neolib::string{ "string.utf8.character.CR" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'\r'>>>("string.utf8.character.CR");
        concepts()[neolib::string{ "string.utf8.character.tab" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'\t'>>>("string.utf8.character.tab");
        concepts()[neolib::string{ "string.utf8.character.backslash" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'\\'>>>("string.utf8.character.backslash");
        concepts()[neolib::string{ "string.utf8.character.alpha" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'A', 'Z'>, specific_char<'a', 'z'>>>("string.utf8.character.alpha");
        concepts()[neolib::string{ "string.utf8.character.digit" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'0', '9'>>>("string.utf8.character.digit");
        concepts()[neolib::string{ "string.utf8.character.hexdigit" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'0', '9'>, specific_char<'A', 'F'>, specific_char<'a', 'f'>>>("string.utf8.character.hexdigit");
        concepts()[neolib::string{ "string.utf8.character.period" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'.'>>>("string.utf8.character.period");
        concepts()[neolib::string{ "string.utf8.character.comma" }] = 
            neolib::make_ref<string_utf8_character<specific_char<','>>>("string.utf8.character.comma");
        concepts()[neolib::string{ "string.utf8.character.underscore" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'_'>>>("string.utf8.character.underscore");
        concepts()[neolib::string{ "string.utf8.character.doublequote" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'\"'>>>("string.utf8.character.double_quote");
        concepts()[neolib::string{ "string.utf8.character.singlequote" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'\''>>>("string.utf8.character.single_quote");
    }

    const std::string& string::library_name()
    {
        static const std::string sName = "neos.string";
        return sName;
    }

    const neolib::uuid& string::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("C281D6BD-CBC7-40F4-8E1A-BE4FC38B8F83");
        return sId;
    }
}

