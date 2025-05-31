/*
  language.cpp

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

#include <neos/i_context.hpp>
#include <neos/language/semantic_concept.hpp>
#include <neos/language/type.hpp>
#include "language.hpp"

namespace neos::concepts::core
{   
    class language_whitespace : public semantic_concept<language_whitespace>
    {
        // construction
    public:
        language_whitespace() :
            semantic_concept{ "language.whitespace", neos::language::emit_type::Infix }
        {
        }
    };

    class language_comment : public semantic_concept<language_comment>
    {
        // construction
    public:
        language_comment() :
            semantic_concept{ "language.comment", neos::language::emit_type::Infix }
        {
        }
    };

    class language_keyword : public semantic_concept<language_keyword>
    {
        // construction
    public:
        language_keyword() :
            semantic_concept{ "language.keyword", neos::language::emit_type::Infix }
        {
        }
    };

    class language_identifier : public semantic_concept<language_identifier>
    {
        // types
    public:
        using data_type = neolib::string;
        // construction
    public:
        language_identifier() :
            semantic_concept{ "language.identifier", neos::language::emit_type::Infix }
        {
        }
    };

    class language_program : public semantic_concept<language_program>
    {
        // construction
    public:
        language_program() :
            semantic_concept{ "language.program", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            return true;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
        }
    };

    class language_scope : public semantic_concept<language_scope>
    {
        // construction
    public:
        language_scope() :
            semantic_concept{ "language.scope", neos::language::emit_type::Infix }
        {
        }
    };

    class language_namespace : public semantic_concept<language_namespace>
    {
        // types
    public:
        using data_type = neolib::string;
        // construction
    public:
        language_namespace() :
            semantic_concept{ "language.namespace", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(const i_semantic_concept& aRhs) const override
        {
            if (aRhs.name() == "language.namespace.name")
                return true;
            return false;
        }
        void do_fold(i_context& aContext, const i_semantic_concept& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            data<neolib::i_string>() = aRhs.data<neolib::i_string>();
            aResult.reset(this);
        }
    };

    class language_namespace_name : public semantic_concept<language_namespace_name>
    {
        // types
    public:
        using data_type = neolib::string;
        // construction
    public:
        language_namespace_name() :
            semantic_concept{ "language.namespace.name", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            return false;
        }
        bool can_fold(const i_semantic_concept& aRhs) const override
        {
            if (aRhs.name() == "language.namespace.scope.open")
                return true;
            return false;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
        }
        void do_fold(i_context& aContext, const i_semantic_concept& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            aContext.compiler().enter_namespace(data<neolib::i_string>());
        }
    };

    class language_scope_open : public semantic_concept<language_scope_open>
    {
        // construction
    public:
        language_scope_open() :
            semantic_concept{ "language.scope.open", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            return true;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
        }
    };

    class language_namespace_scope_open : public semantic_concept<language_namespace_scope_open>
    {
        // construction
    public:
        language_namespace_scope_open(i_semantic_concept& aParent) :
            semantic_concept{ aParent, "language.namespace.scope.open", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            return true;
        }
        bool can_fold(const i_semantic_concept& aRhs) const override
        {
            if (aRhs.name() == "language.namespace.name" ||
                aRhs.name() == "language.namespace")
                return true;
            return false;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
        }
        void do_fold(i_context& aContext, const i_semantic_concept& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            aContext.compiler().enter_namespace(aRhs.data<neolib::i_string>());
        }
    };

    class language_scope_open_by_indentation : public semantic_concept<language_scope_open_by_indentation>
    {
        // construction
    public:
        language_scope_open_by_indentation() :
            semantic_concept{ "language.scope.open.by_indentation", neos::language::emit_type::Infix }
        {
        }
    };

    class language_scope_close : public semantic_concept<language_scope_close>
    {
        // construction
    public:
        language_scope_close() :
            semantic_concept{ "language.scope.close", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(const i_semantic_concept& aRhs) const override
        {
            return true;
        }
        void do_fold(i_context& aContext, const i_semantic_concept& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
        }
    };

    class language_namespace_scope_close : public semantic_concept<language_namespace_scope_close>
    {
        // construction
    public:
        language_namespace_scope_close(i_semantic_concept& aParent) :
            semantic_concept{ aParent, "language.namespace.scope.close", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            return true;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            aContext.compiler().leave_namespace();
        }
    };

    class language_scope_add_package : public semantic_concept<language_scope_add_package>
    {
        // construction
    public:
        language_scope_add_package() :
            semantic_concept{ "language.scope.add.package", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function : public semantic_concept<language_function>
    {
        // construction
    public:
        language_function() :
            semantic_concept{ "language.function", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(const i_semantic_concept& aRhs) const override
        {
            if (aRhs.name() == "language.function.scope")
                return true;
            return false;
        }
        void do_fold(i_context& aContext, const i_semantic_concept& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            aResult.reset(this);
        }
    };

    class language_function_name : public semantic_concept<language_function_name>
    {
        // construction
    public:
        language_function_name() :
            semantic_concept{ "language.function.name", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_scope : public semantic_concept<language_function_scope>
    {
        // construction
    public:
        language_function_scope(i_semantic_concept& aParent) :
            semantic_concept{ aParent, "language.function.scope", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_body : public semantic_concept<language_function_body>
    {
        // construction
    public:
        language_function_body(i_semantic_concept& aParent) :
            semantic_concept{ aParent, "language.function.body", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_parameters : public semantic_concept<language_function_parameters>
    {
        // construction
    public:
        language_function_parameters() :
            semantic_concept{ "language.function.parameters", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_parameter : public semantic_concept<language_function_parameter>
    {
        // construction
    public:
        language_function_parameter() :
            semantic_concept{ "language.function.parameter", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_parameter_in : public semantic_concept<language_function_parameter_in>
    {
        // construction
    public:
        language_function_parameter_in() :
            semantic_concept{ "language.function.parameter.in", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_parameter_out : public semantic_concept<language_function_parameter_out>
    {
        // construction
    public:
        language_function_parameter_out() :
            semantic_concept{ "language.function.parameter.out", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_parameter_inout : public semantic_concept<language_function_parameter_inout>
    {
        // construction
    public:
        language_function_parameter_inout() :
            semantic_concept{ "language.function.parameter.inout", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_locals : public semantic_concept<language_function_locals>
    {
        // construction
    public:
        language_function_locals() :
            semantic_concept{ "language.function.locals", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_local : public semantic_concept<language_function_local>
    {
        // construction
    public:
        language_function_local() :
            semantic_concept{ "language.function.local", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_signature : public semantic_concept<language_function_signature>
    {
        // data
    public:
        using i_parameter_list = neolib::i_optional<neolib::i_vector<neolib::i_pair<neos::language::type, neolib::i_string>>>;
        struct i_data_type
        {
            virtual neolib::i_string const& function_name() const = 0;
            virtual void set_function_name(neolib::i_string_view const& aFunctionName) = 0;
            virtual i_parameter_list const& parameters() const = 0;
            virtual i_parameter_list& parameters() = 0;
        };
        using parameter_list = neolib::optional<neolib::vector<neolib::pair<neos::language::type, neolib::string>>>;
        struct data_type : i_data_type
        {
            neolib::string functionName;
            parameter_list functionParameters;
            neolib::i_string const& function_name() const final { return functionName; }
            void set_function_name(neolib::i_string_view const& aFunctionName) final { functionName = aFunctionName; }
            parameter_list const& parameters() const final { return functionParameters; }
            parameter_list& parameters() { return functionParameters; }
        };
        // construction
    public:
        language_function_signature() :
            semantic_concept{ "language.function.signature", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(const i_semantic_concept& aRhs) const override
        {
            if (aRhs.name() == "language.function.name")
                return true;
            else if (aRhs.name() == "language.function.parameters")
                return aRhs.holds_data();
            return false;
        }
        void do_fold(i_context& aContext, const i_semantic_concept& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (aRhs.name() == "language.function.name")
            {
                data<i_data_type>().set_function_name(aRhs.source());
                aResult.reset(this);
            }
            else if (aRhs.name() == "language.function.parameters")
            {
                data<i_data_type>().parameters();
                aResult.reset(this);
            }
        }
    };

    class language_function_import : public semantic_concept<language_function_import>
    {
        // construction
    public:
        language_function_import() :
            semantic_concept{ "language.function.import", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_arguments : public semantic_concept<language_function_arguments>
    {
        // construction
    public:
        language_function_arguments() :
            semantic_concept{ "language.function.arguments", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_argument : public semantic_concept<language_function_argument>
    {
        // construction
    public:
        language_function_argument() :
            semantic_concept{ "language.function.argument", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_call : public semantic_concept<language_function_call>
    {
        // construction
    public:
        language_function_call() :
            semantic_concept{ "language.function.call", neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_return : public semantic_concept<language_function_return>
    {
        // construction
    public:
        language_function_return() :
            semantic_concept{ "language.function.return", neos::language::emit_type::Infix }
        {
        }
    };

    class language_type : public semantic_concept<language_type>
    {
        // construction
    public:
        language_type() :
            semantic_concept{ "language.type", neos::language::emit_type::Infix }
        {
        }
    };

    class language_type_tuple : public semantic_concept<language_type_tuple>
    {
        // construction
    public:
        language_type_tuple() :
            semantic_concept{ "language.type.tuple", neos::language::emit_type::Infix }
        {
        }
    };

    template <typename Float>
    class language_type_float : public semantic_concept<language_type_float<Float>>
    {
        // construction
    public:
        language_type_float(i_semantic_concept& aParent, const std::string& aName) :
            semantic_concept<language_type_float<Float>>{ aParent, aName, neos::language::emit_type::Infix }
        {
        }
    };

    template <typename Integer>
    class language_type_integer : public semantic_concept<language_type_integer<Integer>>
    {
        // construction
    public:
        language_type_integer(i_semantic_concept& aParent, const std::string& aName) :
            semantic_concept<language_type_integer<Integer>>{ aParent, aName, neos::language::emit_type::Infix }
        {
        }
    };

    template <typename Character>
    class language_type_string : public semantic_concept<language_type_string<Character>>
    {
        // construction
    public:
        language_type_string(i_semantic_concept& aParent, const std::string& aName) :
            semantic_concept<language_type_string<Character>>{ aParent, aName, neos::language::emit_type::Infix }
        {
        }
    };

    class language_type_custom : public semantic_concept<language_type_custom>
    {
        // construction
    public:
        language_type_custom(i_semantic_concept& aParent) :
            semantic_concept{ aParent, "language.type.custom", neos::language::emit_type::Infix }
        {
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
            "Copyright (c) 2025 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        concepts()[neolib::string{ "concept.unrealized" }] = 
            neolib::make_ref<neos::language::unimplemented_semantic_concept>("concept.unimplemented");
        concepts()[neolib::string{ "language.program" }] =
            neolib::make_ref<language_program>();
        concepts()[neolib::string{ "language.namespace" }] =
            neolib::make_ref<language_namespace>();
        concepts()[neolib::string{ "language.expression" }] =
            neolib::make_ref<neos::language::unimplemented_semantic_concept>("language.expression");
        concepts()[neolib::string{ "language.expression.operand" }] = 
            neolib::make_ref<neos::language::unimplemented_semantic_concept>("language.expression.operand");
        concepts()[neolib::string{ "language.statement" }] = 
            neolib::make_ref<neos::language::unimplemented_semantic_concept>("language.statement");
        concepts()[neolib::string{ "language.statement.if" }] =
            neolib::make_ref<neos::language::unimplemented_semantic_concept>("language.statement.if");
        concepts()[neolib::string{ "language.statement.elseif" }] =
            neolib::make_ref<neos::language::unimplemented_semantic_concept>("language.statement.elseif");
        concepts()[neolib::string{ "language.statement.else" }] =
            neolib::make_ref<neos::language::unimplemented_semantic_concept>("language.statement.else");
        concepts()[neolib::string{ "language.statement.loop" }] =
            neolib::make_ref<neos::language::unimplemented_semantic_concept>("language.statement.loop");
        concepts()[neolib::string{ "language.keyword" }] =
            neolib::make_ref<language_keyword>();
        concepts()[neolib::string{ "language.identifier" }] = 
            neolib::make_ref<language_identifier>();
        concepts()[neolib::string{ "language.scope" }] =
            neolib::make_ref<language_scope>();
        concepts()[neolib::string{ "language.namespace.name" }] =
            neolib::make_ref<language_namespace_name>();
        concepts()[neolib::string{ "language.scope.open" }] =
            neolib::make_ref<language_scope_open>();
        concepts()[neolib::string{ "language.namespace.scope.open" }] =
            neolib::make_ref<language_namespace_scope_open>(*concepts()[neolib::string{ "language.scope.open" }]);
        concepts()[neolib::string{ "language.scope.open.by_indentation" }] =
            neolib::make_ref<language_scope_open_by_indentation>();
        concepts()[neolib::string{ "language.scope.close" }] = 
            neolib::make_ref<language_scope_close>();
        concepts()[neolib::string{ "language.namespace.scope.close" }] =
            neolib::make_ref<language_namespace_scope_close>(*concepts()[neolib::string{ "language.scope.close" }]);
        concepts()[neolib::string{ "language.scope.add.package" }] =
            neolib::make_ref<language_scope_add_package>();
        concepts()[neolib::string{ "language.function" }] = 
            neolib::make_ref<language_function>();
        concepts()[neolib::string{ "language.function.name" }] =
            neolib::make_ref<language_function_name>();
        concepts()[neolib::string{ "language.function.scope" }] =
            neolib::make_ref<language_function_scope>(*concepts()[neolib::string{ "language.scope" }]);
        concepts()[neolib::string{ "language.function.body" }] =
            neolib::make_ref<language_function_body>(*concepts()[neolib::string{ "language.function.scope" }]);
        concepts()[neolib::string{ "language.function.parameters" }] =
            neolib::make_ref<language_function_parameters>();
        concepts()[neolib::string{ "language.function.parameter" }] = 
            neolib::make_ref<language_function_parameter>();
        concepts()[neolib::string{ "language.function.parameter.in" }] = 
            neolib::make_ref<language_function_parameter_in>();
        concepts()[neolib::string{ "language.function.parameter.out" }] = 
            neolib::make_ref<language_function_parameter_out>();
        concepts()[neolib::string{ "language.function.locals" }] = 
            neolib::make_ref<language_function_locals>();
        concepts()[neolib::string{ "language.function.local" }] = 
            neolib::make_ref<language_function_local>();
        concepts()[neolib::string{ "language.function.signature" }] = 
            neolib::make_ref<language_function_signature>();
        concepts()[neolib::string{ "language.function.import" }] = 
            neolib::make_ref<language_function_import>();
        concepts()[neolib::string{ "language.function.arguments" }] = 
            neolib::make_ref<language_function_arguments>();
        concepts()[neolib::string{ "language.function.argument" }] = 
            neolib::make_ref<language_function_argument>();
        concepts()[neolib::string{ "language.function.call" }] = 
            neolib::make_ref<language_function_call>();
        concepts()[neolib::string{ "language.function.return" }] = 
            neolib::make_ref<language_function_return>();
        concepts()[neolib::string{ "language.type" }] = 
            neolib::make_ref<language_type>();
        concepts()[neolib::string{ "language.type.tuple" }] = 
            neolib::make_ref<language_type_tuple>();
        concepts()[neolib::string{ "language.type.float" }] = 
            neolib::make_ref<language_type_float<float>>(*concepts()[neolib::string{ "language.type" }], "language.type.float");
        concepts()[neolib::string{ "language.type.double" }] = 
            neolib::make_ref<language_type_float<double>>(*concepts()[neolib::string{ "language.type" }], "language.type.double");
        concepts()[neolib::string{ "language.type.i8" }] = 
            neolib::make_ref<language_type_integer<std::int8_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i8");
        concepts()[neolib::string{ "language.type.u8" }] = 
            neolib::make_ref<language_type_integer<std::uint8_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i8");
        concepts()[neolib::string{ "language.type.i16" }] = 
            neolib::make_ref<language_type_integer<int16_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i16");
        concepts()[neolib::string{ "language.type.u16" }] = 
            neolib::make_ref<language_type_integer<std::uint16_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i16");
        concepts()[neolib::string{ "language.type.i32" }] = 
            neolib::make_ref<language_type_integer<std::int32_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i32");
        concepts()[neolib::string{ "language.type.u32" }] = 
            neolib::make_ref<language_type_integer<std::uint32_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i32");
        concepts()[neolib::string{ "language.type.i64" }] = 
            neolib::make_ref<language_type_integer<std::int64_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i64");
        concepts()[neolib::string{ "language.type.u64" }] = 
            neolib::make_ref<language_type_integer<std::uint64_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i64");
// bignum
//        concepts()[neolib::string{ "language.type.integer" }] = 
//            neolib::make_ref<language_type_integer<neonumerical::xxx>>(*concepts()[neolib::string{ "language.type" }], "language.type.integer");
//        concepts()[neolib::string{ "language.type.real" }] = 
//            neolib::make_ref<language_type_integer<neonumerical::xxx>>(*concepts()[neolib::string{ "language.type" }], "language.type.real");
//        concepts()[neolib::string{ "language.type.rational" }] = 
//            neolib::make_ref<language_type_integer<neonumerical::xxx>>(*concepts()[neolib::string{ "language.type" }], "language.type.rational");
        concepts()[neolib::string{ "language.type.string" }] = 
            neolib::make_ref<language_type_string<char>>(*concepts()[neolib::string{ "language.type" }], "language.type.string");
        concepts()[neolib::string{ "language.type.custom" }] = 
            neolib::make_ref<language_type_custom>(*concepts()[neolib::string{ "language.type" }]);
        concepts()[neolib::string{ "language.comment" }] = 
            neolib::make_ref<language_comment>();
        concepts()[neolib::string{ "language.whitespace" }] = 
            neolib::make_ref<language_whitespace>();
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

