/*
  language.cpp

  Copyright (c) 2019 Leigh Johnston.  All Rights Reserved.

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

#include <neos/language/concept.hpp>
#include "language.hpp"

namespace neos::concepts::core
{   
    class language_eof : public neos_concept<>
    {
        // construction
    public:
        language_eof() :
            neos_concept{ "language.eof", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = true;
            return aSource;
        }
    };

    class language_whitespace : public neos_concept<>
    {
        // construction
    public:
        language_whitespace() :
            neos_concept{ "language.whitespace", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_comment : public neos_concept<>
    {
        // construction
    public:
        language_comment() :
            neos_concept{ "language.comment", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_keyword : public neos_concept<>
    {
        // construction
    public:
        language_keyword() :
            neos_concept{ "language.keyword", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_identifier : public neos_concept<language_identifier>
    {
        // types
    public:
        typedef neolib::string representation_type;
        // construction
    public:
        language_identifier() :
            neos_concept{ "language.identifier", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
        // emit
    protected:
        bool can_fold(const i_concept& aRhs) const override
        {
            return aRhs.name() == "string.utf8";
        }
    };

    class language_scope : public neos_concept<>
    {
        // construction
    public:
        language_scope() :
            neos_concept{ "language.scope", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_scope_open : public neos_concept<>
    {
        // construction
    public:
        language_scope_open() :
            neos_concept{ "language.scope.open", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_scope_open_by_indentation : public neos_concept<>
    {
        // construction
    public:
        language_scope_open_by_indentation() :
            neos_concept{ "language.scope.open.by_indentation", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_scope_close : public neos_concept<>
    {
        // construction
    public:
        language_scope_close() :
            neos_concept{ "language.scope.close", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_scope_add_package : public neos_concept<>
    {
        // construction
    public:
        language_scope_add_package() :
            neos_concept{ "language.scope.add.package", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function : public neos_concept<language_function>
    {
        // types
    public:
        typedef void representation_type;
        // construction
    public:
        language_function() :
            neos_concept{ "language.function", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
        // emit
    protected:
        bool can_fold() const override
        {
            return false;
        }
        i_concept* do_fold(i_context& aContext) override
        {
            return nullptr;
        }
        bool can_fold(const i_concept& aRhs) const override
        {
            if (aRhs.name() == "language.function.scope")
                return true;
            return false;
        }
        i_concept* do_fold(i_context& aContext, const i_concept& aRhs) override
        {
            return this;
        }
    };

    class language_function_scope : public neos_concept<>
    {
        // construction
    public:
        language_function_scope(i_concept& aParent) :
            neos_concept{ aParent, "language.function.scope", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function_parameters : public neos_concept<>
    {
        // construction
    public:
        language_function_parameters() :
            neos_concept{ "language.function.parameters", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function_parameter : public neos_concept<>
    {
        // construction
    public:
        language_function_parameter() :
            neos_concept{ "language.function.parameter", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function_parameter_direction_in : public neos_concept<>
    {
        // construction
    public:
        language_function_parameter_direction_in() :
            neos_concept{ "language.function.parameter.direction.in", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function_parameter_direction_out : public neos_concept<>
    {
        // construction
    public:
        language_function_parameter_direction_out() :
            neos_concept{ "language.function.parameter.direction.out", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function_locals : public neos_concept<>
    {
        // construction
    public:
        language_function_locals() :
            neos_concept{ "language.function.locals", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function_local : public neos_concept<>
    {
        // construction
    public:
        language_function_local() :
            neos_concept{ "language.function.local", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function_return : public neos_concept<>
    {
        // construction
    public:
        language_function_return() :
            neos_concept{ "language.function.return", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function_signature : public neos_concept<>
    {
        // construction
    public:
        language_function_signature() :
            neos_concept{ "language.function.signature", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function_import : public neos_concept<>
    {
        // construction
    public:
        language_function_import() :
            neos_concept{ "language.function.import", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function_arguments : public neos_concept<>
    {
        // construction
    public:
        language_function_arguments() :
            neos_concept{ "language.function.arguments", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function_argument : public neos_concept<>
    {
        // construction
    public:
        language_function_argument() :
            neos_concept{ "language.function.argument", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_function_call : public neos_concept<>
    {
        // construction
    public:
        language_function_call() :
            neos_concept{ "language.function.call", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_type : public neos_concept<>
    {
        // construction
    public:
        language_type() :
            neos_concept{ "language.type", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_type_tuple : public neos_concept<>
    {
        // construction
    public:
        language_type_tuple() :
            neos_concept{ "language.type.tuple", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    template <typename Float>
    class language_type_float : public neos_concept<>
    {
        // construction
    public:
        language_type_float(i_concept& aParent, const std::string& aName) :
            neos_concept{ aParent, aName, neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    template <typename Integer>
    class language_type_integer : public neos_concept<>
    {
        // construction
    public:
        language_type_integer(i_concept& aParent, const std::string& aName) :
            neos_concept{ aParent, aName, neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    template <typename Character>
    class language_type_string : public neos_concept<>
    {
        // construction
    public:
        language_type_string(i_concept& aParent, const std::string& aName) :
            neos_concept{ aParent, aName, neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    class language_type_custom : public neos_concept<>
    {
        // construction
    public:
        language_type_custom(i_concept& aParent) :
            neos_concept{ aParent, "language.type.custom", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
    };

    language::language(neos::language::i_concept_library& aParent) :
        neos::language::concept_library
        { 
            aParent,
            library_id(), 
            aParent.uri().to_std_string(),
            library_name(), 
            "Core language concepts", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2019 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        concepts()[neolib::string{ "concept.unrealized" }] = neolib::make_ref<neos::language::unimplemented_concept>("concept.unimplemented");
        concepts()[neolib::string{ "language.expression" }] = neolib::make_ref<neos::language::unimplemented_concept>("language.expression");
        concepts()[neolib::string{ "language.expression.operand" }] = neolib::make_ref<neos::language::unimplemented_concept>("language.expression.operand");
        concepts()[neolib::string{ "language.statement" }] = neolib::make_ref<neos::language::unimplemented_concept>("language.statement");
        concepts()[neolib::string{ "language.keyword" }] = neolib::make_ref<language_keyword>();
        concepts()[neolib::string{ "language.identifier" }] = neolib::make_ref<language_identifier>();
        concepts()[neolib::string{ "language.scope" }] = neolib::make_ref<language_scope>();
        concepts()[neolib::string{ "language.scope.open" }] = neolib::make_ref<language_scope_open>();
        concepts()[neolib::string{ "language.scope.open.by_indentation" }] = neolib::make_ref<language_scope_open_by_indentation>();
        concepts()[neolib::string{ "language.scope.close" }] = neolib::make_ref<language_scope_close>();
        concepts()[neolib::string{ "language.scope.add.package" }] = neolib::make_ref<language_scope_add_package>();
        concepts()[neolib::string{ "language.function" }] = neolib::make_ref<language_function>();
        concepts()[neolib::string{ "language.function.scope" }] = neolib::make_ref<language_function_scope>(*concepts()[neolib::string{ "language.scope" }]);
        concepts()[neolib::string{ "language.function.parameters" }] = neolib::make_ref<language_function_parameters>();
        concepts()[neolib::string{ "language.function.parameter" }] = neolib::make_ref<language_function_parameter>();
        concepts()[neolib::string{ "language.function.parameter.direction.in" }] = neolib::make_ref<language_function_parameter_direction_in>();
        concepts()[neolib::string{ "language.function.parameter.direction.out" }] = neolib::make_ref<language_function_parameter_direction_out>();
        concepts()[neolib::string{ "language.function.locals" }] = neolib::make_ref<language_function_locals>();
        concepts()[neolib::string{ "language.function.local" }] = neolib::make_ref<language_function_local>();
        concepts()[neolib::string{ "language.function.return" }] = neolib::make_ref<language_function_return>();
        concepts()[neolib::string{ "language.function.signature" }] = neolib::make_ref<language_function_signature>();
        concepts()[neolib::string{ "language.function.import" }] = neolib::make_ref<language_function_import>();
        concepts()[neolib::string{ "language.function.arguments" }] = neolib::make_ref<language_function_arguments>();
        concepts()[neolib::string{ "language.function.argument" }] = neolib::make_ref<language_function_argument>();
        concepts()[neolib::string{ "language.function.call" }] = neolib::make_ref<language_function_call>();
        concepts()[neolib::string{ "language.type" }] = neolib::make_ref<language_type>();
        concepts()[neolib::string{ "language.type.tuple" }] = neolib::make_ref<language_type_tuple>();
        concepts()[neolib::string{ "language.type.f32" }] = neolib::make_ref<language_type_float<float>>(*concepts()[neolib::string{ "language.type" }], "language.type.f32");
        concepts()[neolib::string{ "language.type.f64" }] = neolib::make_ref<language_type_float<double>>(*concepts()[neolib::string{ "language.type" }], "language.type.f64");
        concepts()[neolib::string{ "language.type.i8" }] = neolib::make_ref<language_type_integer<std::int8_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i8");
        concepts()[neolib::string{ "language.type.u8" }] = neolib::make_ref<language_type_integer<std::uint8_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i8");
        concepts()[neolib::string{ "language.type.i16" }] = neolib::make_ref<language_type_integer<int16_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i16");
        concepts()[neolib::string{ "language.type.u16" }] = neolib::make_ref<language_type_integer<std::uint16_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i16");
        concepts()[neolib::string{ "language.type.i32" }] = neolib::make_ref<language_type_integer<std::int32_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i32");
        concepts()[neolib::string{ "language.type.u32" }] = neolib::make_ref<language_type_integer<std::uint32_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i32");
        concepts()[neolib::string{ "language.type.i64" }] = neolib::make_ref<language_type_integer<std::int64_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i64");
        concepts()[neolib::string{ "language.type.u64" }] = neolib::make_ref<language_type_integer<std::uint64_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i64");
// bignum
//        concepts()[neolib::string{ "language.type.integer" }] = neolib::make_ref<language_type_integer<neonumerical::xxx>>(*concepts()[neolib::string{ "language.type" }], "language.type.integer");
//        concepts()[neolib::string{ "language.type.real" }] = neolib::make_ref<language_type_integer<neonumerical::xxx>>(*concepts()[neolib::string{ "language.type" }], "language.type.real");
//        concepts()[neolib::string{ "language.type.rational" }] = neolib::make_ref<language_type_integer<neonumerical::xxx>>(*concepts()[neolib::string{ "language.type" }], "language.type.rational");
        concepts()[neolib::string{ "language.type.string" }] = neolib::make_ref<language_type_string<char>>(*concepts()[neolib::string{ "language.type" }], "language.type.string");
        concepts()[neolib::string{ "language.type.custom" }] = neolib::make_ref<language_type_custom>(*concepts()[neolib::string{ "language.type" }]);
        concepts()[neolib::string{ "language.comment" }] = neolib::make_ref<language_comment>();
        concepts()[neolib::string{ "language.whitespace" }] = neolib::make_ref<language_whitespace>();
        concepts()[neolib::string{ "language.eof" }] = neolib::make_ref<language_eof>();
    }

    const std::string& language::library_name()
    {
        static const std::string sName = "neos.language";
        return sName;
    }

    const neolib::uuid& language::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("C33C5C53-937B-434B-A3F9-C2A1BB71D391");
        return sId;
    }
}

