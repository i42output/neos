/*
  core.cpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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

#include <neonumeric/integer.hpp>
#include <neonumeric/real.hpp>
#include <neos/i_context.hpp>
#include <neos/language/type.hpp>
#include "core.hpp"
#include "math.universal.hpp"

namespace neos::concepts::core
{   
    template <typename Derived = void>
    class math_universal_number : public semantic_concept_t<Derived, math_universal_number<Derived>>
    {
        // concept
    public:
        static constexpr auto Name = "math.universal.number";
        // construction
    public:
        using semantic_concept_t<Derived, math_universal_number<Derived>>::semantic_concept_t;
    };

    class math_universal_integer : public math_universal_number<math_universal_integer>
    {
        // concept
    public:
        static constexpr auto Name = "math.universal.integer";
        // types
    public:
        using data_type = language::data<language::ibig>;
        // construction
    public:
        using math_universal_number<math_universal_integer>::math_universal_number;
        // emit
    public:
        bool can_fold() const override
        {
            return !holds_data();
        }
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.is("language.identifier"_sv) ||
                aRhs.is("math.expression.operand"_sv))
                return true;
            return false;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (!holds_data())
            {
                std::string value = source().to_std_string();
                data<data_type>().v = language::ibig{ value };
            }
            aResult = instance();
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (aRhs.is("language.identifier"_sv))
                ; ///< @todo
            else if (aRhs.is("math.expression.operand"_sv))
                aContext.compiler().push_operand(data<data_type>());
        }
    };

    class math_universal_real : public math_universal_number<math_universal_real>
    {
        // concept
    public:
        static constexpr auto Name = "math.universal.real";
        // types
    public:
        using data_type = language::data<language::fbig>;
        // construction
    public:
        using math_universal_number<math_universal_real>::math_universal_number;
        // emit
    public:
        bool can_fold() const override
        {
            return !holds_data();
        }
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.is("language.identifier"_sv) ||
                aRhs.is("math.expression.operand"_sv))
                return true;
            return false;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (!holds_data())
            {
                std::string value = source().to_std_string();
                data<data_type>().v = language::fbig{ value };
            }
            aResult = instance();
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (aRhs.is("language.identifier"_sv))
                ; ///< @todo
            else if (aRhs.is("math.expression.operand"_sv))
                aContext.compiler().push_operand(data<data_type>());
        }
    };

    class math_universal_number_digit : public semantic_concept<math_universal_number_digit>
    {
    public:
        using semantic_concept::semantic_concept;
    };

    class math_universal_number_hexdigit : public semantic_concept<math_universal_number_hexdigit>
    {
    public:
        using semantic_concept::semantic_concept;
    };

    math_universal::math_universal(const std::string& aLibraryUri) :
        neos::language::concept_library
        { 
            library_id(), 
            aLibraryUri, 
            library_name(), 
            "neos math universal number concept", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2025 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        concepts()[neolib::string{ "math.universal.number" }] = neolib::make_ref<math_universal_number<>>();
        concepts()[neolib::string{ "math.universal.integer" }] = neolib::make_ref<math_universal_integer>();
        concepts()[neolib::string{ "math.universal.real" }] = neolib::make_ref<math_universal_real>();
        concepts()[neolib::string{ "math.universal.number.digit" }] = neolib::make_ref<math_universal_number_digit>("math.universal.number.digit", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.point" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number.point", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.exponent" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number.exponent", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.base" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number.base", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.hexdigit" }] = neolib::make_ref<math_universal_number_hexdigit>("math.universal.number.hexdigit", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.exponent.positive" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number.exponent.positive", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.exponent.negative" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number.exponent.negative", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.exponent.digit" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number.exponent.digit", language::emit_type::Infix);
    }

    const std::string& math_universal::library_name()
    {
        static const std::string sName = "neos.math.universal";
        return sName;
    }

    const neolib::uuid& math_universal::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("6EF80912-B773-4F79-BC11-7FEBF375B224");
        return sId;
    }
}

