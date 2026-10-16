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
    using any_char = string_utf8_character_range<'\x00', '\xFF'>;

    namespace
    {
        std::string unescape_string(std::string_view const& aSource)
        {
            std::string result;
            for (std::size_t i = 0u; i < aSource.size(); ++i)
            {
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
                case 'x':
                    if (i + 2u < aSource.size())
                    {
                        result += static_cast<char>(std::stoi(std::string{ aSource.substr(i + 1u, 2u) }, nullptr, 16));
                        i += 2u;
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

