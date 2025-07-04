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
#include <neos/language/function.hpp>
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
        // emit
    public:
        bool can_fold() const override
        {
            return false;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
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
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.name() == "language.namespace.name")
                return true;
            return false;
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            data<neolib::i_string>() = aRhs.data<neolib::i_string>();
            aResult = instance();
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
    };

    class language_scope_open : public semantic_concept<language_scope_open>
    {
        // folding
    public:
        static constexpr bool Unstructured = true;
        // construction
    public:
        language_scope_open() :
            semantic_concept{ "language.scope.open", neos::language::emit_type::Infix }
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
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.name() == "language.namespace.name" ||
                aRhs.name() == "language.namespace")
                return true;
            return false;
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
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
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            return true;
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
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
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.name() == "language.function.scope")
                return true;
            return false;
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            aResult = instance();
        }
    };

    class language_function_name : public semantic_concept<language_function_name>
    {
        // data
    public:
        using data_type = neolib::string;
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

    class language_function_signature : public semantic_concept<language_function_signature>
    {
        // data
    public:
        using data_type = neos::language::function_signature;
    public:
        static constexpr bool HasGhosts = true;
        static constexpr bool Unstructured = true;
        // construction
    public:
        language_function_signature() :
            semantic_concept{ "language.function.signature", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.name() == "language.function.name")
                return true;
            else if (aRhs.name() == "language.function.parameters")
                return true;
            else if (aRhs.name() == "language.function.return.type")
                return true;
            else if (aRhs.name() == "language.scope.open")
                return true;
            return false;
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (aRhs.name() == "language.function.name")
            {
                data<neos::language::i_function_signature>().set_function_name(aRhs.data<neolib::i_string>());
                aResult = instance();
            }
            else if (aRhs.name() == "language.function.parameters")
            {
                data<neos::language::i_function_signature>().parameters() = aRhs.data<neos::language::i_function_parameters>();
                aResult = instance();
            }
            else if (aRhs.name() == "language.function.return.type")
            {
                data<neos::language::i_function_signature>().set_return_type(aRhs.data<neos::language::type>());
                aResult = instance();
            }
            else if (aRhs.name() == "language.scope.open")
            {
                auto& scope = static_cast<neos::language::i_function_scope&>(
                    aContext.compiler().enter_scope(neos::language::scope_type::Function,
                    data<neos::language::i_function_signature>().function_name()));
                scope.set_function_signature(data<neos::language::i_function_signature>());
            }
        }
    };

    class language_function_parameters : public semantic_concept<language_function_parameters>
    {
        // data
    public:
        using data_type = neos::language::function_parameters;
        // folding
    public:
        static constexpr bool HasGhosts = true;
        // construction
    public:
        language_function_parameters() :
            semantic_concept{ "language.function.parameters", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.is("language.type"_s) || 
                aRhs.is("language.function.parameter"_s) ||
                aRhs.is("language.function.name"_s))
                return true;
            return false;
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (aRhs.is("language.type"_s))
            {
                for (auto i = data<neos::language::i_function_parameters>().rbegin();
                    i != data<neos::language::i_function_parameters>().rend() && 
                        i->parameter_type() == neos::language::type::UNKNOWN;
                    ++i)
                    i->set_parameter_type(aRhs.data<neos::language::type>());
                aResult = instance();
            }
            else if (aRhs.is("language.function.parameter"_s))
            {
                data<neos::language::i_function_parameters>().push_back(aRhs.data<neos::language::i_function_parameter>());
                aResult = instance();
            }
            else if (aRhs.is("language.function.name"_s))
            {
                aResult = language_function_signature{}.instantiate(aContext, aRhs.source());
                aResult->data<neos::language::i_function_signature>().set_function_name(aRhs.data<neolib::i_string>());
                aResult->data<neos::language::i_function_signature>().parameters() = data<neos::language::i_function_parameters>();
            }
        }
    };

    class language_function_parameter : public semantic_concept<language_function_parameter>
    {
        // data
    public:
        using data_type = neos::language::function_parameter;
        // construction
    public:
        language_function_parameter() :
            semantic_concept{ "language.function.parameter", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            return !holds_data();
        }
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.is("language.identifier"_s) || 
                aRhs.is("language.type"_s) || 
                aRhs.is("language.function.parameter"_s))
                return true;
            return false;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            data<neos::language::i_function_parameter>().set_parameter_name(source());
            aResult = instance();
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (aRhs.is("language.identifier"_s))
            {
                data<neos::language::i_function_parameter>().set_parameter_name(aRhs.source());
                aResult = instance();
            }
            else if (aRhs.is("language.type"_s))
            {
                data<neos::language::i_function_parameter>().set_parameter_type(aRhs.data<neos::language::type>());
                aResult = instance();
            }
            else if (aRhs.is("language.function.parameter"_s))
            {
                aResult = language_function_parameters{}.instantiate(aContext, source());
                aResult->data<neos::language::i_function_parameters>().push_back(data<neos::language::i_function_parameter>());
                aResult->data<neos::language::i_function_parameters>().push_back(aRhs.data<neos::language::i_function_parameter>());
            }
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

    class language_function_return_type : public semantic_concept<language_function_return_type>
    {
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_function_return_type() :
            semantic_concept{ "language.function.return.type", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.is("language.type"_s))
                return true;
            return false;
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (aRhs.is("language.type"_s))
            {
                data<neos::language::type>() = aRhs.data<neos::language::type>();
                aResult = instance();
            }
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

    class language_expression : public semantic_concept<language_expression>
    {
        // construction
    public:
        language_expression() :
            semantic_concept{ "language.expression", neos::language::emit_type::Infix }
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

    class language_type : public semantic_concept<language_type>
    {
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_type() :
            semantic_concept{ "language.type", neos::language::emit_type::Infix }
        {
        }
    };

    class language_type_tuple : public semantic_concept<language_type_tuple>
    {
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_type_tuple(i_semantic_concept& aParent, const std::string& aName) :
            semantic_concept<language_type_tuple>{ aParent, aName, neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            return !this->holds_data();
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            this->data<neos::language::type>() = neos::language::type::Tuple;
            aResult = instance();
        }
    };

    template <typename Float>
    class language_type_float : public semantic_concept<language_type_float<Float>>
    {
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_type_float(i_semantic_concept& aParent, const std::string& aName) :
            semantic_concept<language_type_float<Float>>{ aParent, aName, neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            return !this->holds_data();
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            this->data<neos::language::type>() = neos::language::type_to_enum_v<Float>;
            aResult = this->instance();
        }
    };

    template <typename Integer>
    class language_type_integer : public semantic_concept<language_type_integer<Integer>>
    {
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_type_integer(i_semantic_concept& aParent, const std::string& aName) :
            semantic_concept<language_type_integer<Integer>>{ aParent, aName, neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            return !this->holds_data();
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            this->data<neos::language::type>() = neos::language::type_to_enum_v<Integer>;
            aResult = this->instance();
        }
    };

    template <typename Character>
    class language_type_string : public semantic_concept<language_type_string<Character>>
    {
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_type_string(i_semantic_concept& aParent, const std::string& aName) :
            semantic_concept<language_type_string<Character>>{ aParent, aName, neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            return !this->holds_data();
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            this->data<neos::language::type>() = neos::language::type::String;
            aResult = this->instance();
        }
    };

    class language_type_custom : public semantic_concept<language_type_custom>
    {
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_type_custom(i_semantic_concept& aParent) :
            semantic_concept{ aParent, "language.type.custom", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            return !this->holds_data();
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            this->data<neos::language::type>() = neos::language::type::Struct;
            aResult = instance();
        }
    };

    language::language(neos::language::i_concept_library& aParent) :
        language::concept_library
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
        concepts()[neolib::string{ "language.function.parameter.inout" }] =
            neolib::make_ref<language_function_parameter_inout>();
        concepts()[neolib::string{ "language.function.return.type" }] =
            neolib::make_ref<language_function_return_type>();
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
        concepts()[neolib::string{ "language.expression" }] =
            neolib::make_ref<language_expression>();
        concepts()[neolib::string{ "language.expression.operand" }] =
            neolib::make_ref<neos::language::unimplemented_semantic_concept>("language.expression.operand");
        concepts()[neolib::string{ "language.type" }] =
            neolib::make_ref<language_type>();
        concepts()[neolib::string{ "language.type.tuple" }] = 
            neolib::make_ref<language_type_tuple>(*concepts()[neolib::string{ "language.type" }], "language.type.tuple");
        concepts()[neolib::string{ "language.type.float" }] = 
            neolib::make_ref<language_type_float<float>>(*concepts()[neolib::string{ "language.type" }], "language.type.float");
        concepts()[neolib::string{ "language.type.double" }] = 
            neolib::make_ref<language_type_float<double>>(*concepts()[neolib::string{ "language.type" }], "language.type.double");
        concepts()[neolib::string{ "language.type.i8" }] = 
            neolib::make_ref<language_type_integer<std::int8_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i8");
        concepts()[neolib::string{ "language.type.u8" }] = 
            neolib::make_ref<language_type_integer<std::uint8_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.u8");
        concepts()[neolib::string{ "language.type.i16" }] = 
            neolib::make_ref<language_type_integer<int16_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i16");
        concepts()[neolib::string{ "language.type.u16" }] = 
            neolib::make_ref<language_type_integer<std::uint16_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.u16");
        concepts()[neolib::string{ "language.type.i32" }] = 
            neolib::make_ref<language_type_integer<std::int32_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i32");
        concepts()[neolib::string{ "language.type.u32" }] = 
            neolib::make_ref<language_type_integer<std::uint32_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.u32");
        concepts()[neolib::string{ "language.type.i64" }] = 
            neolib::make_ref<language_type_integer<std::int64_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.i64");
        concepts()[neolib::string{ "language.type.u64" }] = 
            neolib::make_ref<language_type_integer<std::uint64_t>>(*concepts()[neolib::string{ "language.type" }], "language.type.u64");
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

