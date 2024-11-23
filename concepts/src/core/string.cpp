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

#include <neos/language/semantic_concept.hpp>
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
    };

    class string_utf8 : public semantic_concept<string_utf8>
    {
        // types
    public:
        typedef semantic_concept<string_utf8> base_type;
        typedef typename base_type::source_iterator source_iterator;
        // construction
    public:
        using base_type::base_type;
        // emit
    public:
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
            neolib::make_ref<string_utf8>(*concepts()[neolib::string{ "string" }], "string.utf8");
        concepts()[neolib::string{ "string.utf8.character" }] = 
            neolib::make_ref<string_utf8_character<any_char>>(*concepts()[neolib::string{ "string.character" }], "string.utf8.character");
        concepts()[neolib::string{ "string.utf8.character.LF" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'\n'>>>(*concepts()[neolib::string{ "string.utf8.character" }], "string.utf8.character.LF");
        concepts()[neolib::string{ "string.utf8.character.CR" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'\r'>>>(*concepts()[neolib::string{ "string.utf8.character" }], "string.utf8.character.CR");
        concepts()[neolib::string{ "string.utf8.character.tab" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'\t'>>>(*concepts()[neolib::string{ "string.utf8.character" }], "string.utf8.character.tab");
        concepts()[neolib::string{ "string.utf8.character.backslash" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'\\'>>>(*concepts()[neolib::string{ "string.utf8.character" }], "string.utf8.character.backslash");
        concepts()[neolib::string{ "string.utf8.character.alpha" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'A', 'Z'>, specific_char<'a', 'z'>>>(*concepts()[neolib::string{ "string.utf8.character" }], "string.utf8.character.alpha");
        concepts()[neolib::string{ "string.utf8.character.digit" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'0', '9'>>>(*concepts()[neolib::string{ "string.utf8.character" }], "string.utf8.character.digit");
        concepts()[neolib::string{ "string.utf8.character.hexdigit" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'0', '9'>, specific_char<'A', 'F'>, specific_char<'a', 'f'>>>(*concepts()[neolib::string{ "string.utf8.character" }], "string.utf8.character.hexdigit");
        concepts()[neolib::string{ "string.utf8.character.period" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'.'>>>(*concepts()[neolib::string{ "string.utf8.character" }], "string.utf8.character.period");
        concepts()[neolib::string{ "string.utf8.character.comma" }] = 
            neolib::make_ref<string_utf8_character<specific_char<','>>>(*concepts()[neolib::string{ "string.utf8.character" }], "string.utf8.character.comma");
        concepts()[neolib::string{ "string.utf8.character.underscore" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'_'>>>(*concepts()[neolib::string{ "string.utf8.character" }], "string.utf8.character.underscore");
        concepts()[neolib::string{ "string.utf8.character.doublequote" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'\"'>>>(*concepts()[neolib::string{ "string.utf8.character" }], "string.utf8.character.double_quote");
        concepts()[neolib::string{ "string.utf8.character.singlequote" }] = 
            neolib::make_ref<string_utf8_character<specific_char<'\''>>>(*concepts()[neolib::string{ "string.utf8.character" }], "string.utf8.character.single_quote");
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

