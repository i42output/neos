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
        // concept
    public:
        static constexpr auto Name = "language.whitespace";
        // construction
    public:
        language_whitespace() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_comment : public semantic_concept<language_comment>
    {
        // concept
    public:
        static constexpr auto Name = "language.comment";
        // construction
    public:
        language_comment() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_keyword : public semantic_concept<language_keyword>
    {
        // concept
    public:
        static constexpr auto Name = "language.keyword";
        // construction
    public:
        language_keyword() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_identifier : public semantic_concept<language_identifier>
    {
        // concept
    public:
        static constexpr auto Name = "language.identifier";
        // types
    public:
        using data_type = neolib::string;
        // construction
    public:
        language_identifier() :
            semantic_concept{ neos::language::emit_type::Infix }
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
        // concept
    public:
        static constexpr auto Name = "language.program";
        // construction
    public:
        language_program() :
            semantic_concept{ neos::language::emit_type::Infix }
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
        // concept
    public:
        static constexpr auto Name = "language.scope";
        // construction
    public:
        language_scope() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_namespace : public semantic_concept<language_namespace>
    {
        // concept
    public:
        static constexpr auto Name = "language.namespace";
        // types
    public:
        using data_type = neolib::string;
        // construction
    public:
        language_namespace() :
            semantic_concept{ neos::language::emit_type::Infix }
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
        // concept
    public:
        static constexpr auto Name = "language.namespace.name";
        // types
    public:
        using data_type = neolib::string;
        // construction
    public:
        language_namespace_name() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    template <typename Derived = void>
    class language_scope_open : public semantic_concept_t<Derived, language_scope_open<Derived>>
    {
        // concept
    public:
        static constexpr auto Name = "language.scope.open";
        // folding
    public:
        static constexpr bool Unstructured = true;
        // construction
    public:
        language_scope_open() :
            semantic_concept_t<Derived, language_scope_open<Derived>>{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_namespace_scope_open : public language_scope_open<language_namespace_scope_open>
    {
        // concept
    public:
        static constexpr auto Name = "language.namespace.scope.open";
        // construction
    public:
        language_namespace_scope_open() :
            language_scope_open{}
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

    class language_scope_open_by_indentation : public language_scope_open<language_scope_open_by_indentation>
    {
        // concept
    public:
        static constexpr auto Name = "language.scope.open.by_indentation";
        // construction
    public:
        language_scope_open_by_indentation() :
            language_scope_open{}
        {
        }
    };

    template <typename Derived = void>
    class language_scope_close : public semantic_concept_t<Derived, language_scope_close<Derived>>
    {
        // concept
    public:
        static constexpr auto Name = "language.scope.close";
        // construction
    public:
        language_scope_close() :
            semantic_concept_t<Derived, language_scope_close<Derived>>{ neos::language::emit_type::Infix }
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

    class language_namespace_scope_close : public language_scope_close<language_namespace_scope_close>
    {
        // concept
    public:
        static constexpr auto Name = "language.namespace.scope.close";
        // construction
    public:
        language_namespace_scope_close() :
            language_scope_close{}
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
        // concept
    public:
        static constexpr auto Name = "language.scope.add.package";
        // construction
    public:
        language_scope_add_package() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function : public semantic_concept<language_function>
    {
        // concept
    public:
        static constexpr auto Name = "language.function";
        // construction
    public:
        language_function() :
            semantic_concept{ neos::language::emit_type::Infix }
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
        // concept
    public:
        static constexpr auto Name = "language.function.name";
        // data
    public:
        using data_type = neolib::string;
        // construction
    public:
        language_function_name() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_scope : public semantic_concept<language_function_scope>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.scope";        
        // construction
    public:
        language_function_scope() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_body : public semantic_concept<language_function_body>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.body";
        // construction
    public:
        language_function_body() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_signature : public semantic_concept<language_function_signature>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.signature";
        // data
    public:
        using data_type = neos::language::function_signature;
    public:
        static constexpr bool Unstructured = true;
        // construction
    public:
        language_function_signature() :
            semantic_concept{ neos::language::emit_type::Infix }
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
                data<neos::language::i_function_signature>().set_name(aRhs.data<neolib::i_string>());
                aResult = instance();
            }
            else if (aRhs.name() == "language.function.parameters")
            {
                data<neos::language::i_function_signature>().set_parameters(aRhs.data<neos::language::i_function_parameters>());
                aResult = instance();
            }
            else if (aRhs.name() == "language.function.parameter")
            {
                if (!data<neos::language::i_function_signature>().parameters().has_value())
                    data<neos::language::i_function_signature>().set_parameters(neos::language::function_parameters{});
                data<neos::language::i_function_signature>().parameters().value().push_back(aRhs.data<neos::language::i_function_parameter>());
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
                    data<neos::language::i_function_signature>().name()));
                scope.set_function_signature(data<neos::language::i_function_signature>());
            }
        }
    };

    class language_function_parameters : public semantic_concept<language_function_parameters>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.parameters";
        // data
    public:
        using data_type = neos::language::function_parameters;
        // folding
    public:
        static constexpr bool HasGhosts = true;
        // construction
    public:
        language_function_parameters() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.is("language.type"_sv) || 
                aRhs.is("language.function.parameter"_sv))
                return true;
            return false;
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (aRhs.is("language.type"_sv))
            {
                for (auto i = data<neos::language::i_function_parameters>().rbegin();
                    i != data<neos::language::i_function_parameters>().rend() && 
                        i->parameter_type() == neos::language::type::UNKNOWN;
                    ++i)
                    i->set_parameter_type(aRhs.data<neos::language::type>());
                aResult = instance();
            }
            else if (aRhs.is("language.function.parameter"_sv))
            {
                data<neos::language::i_function_parameters>().push_back(aRhs.data<neos::language::i_function_parameter>());
                aResult = instance();
            }
        }
    };

    class language_function_parameter : public semantic_concept<language_function_parameter>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.parameter";
        // data
    public:
        using data_type = neos::language::function_parameter;
        // construction
    public:
        language_function_parameter() :
            semantic_concept{ neos::language::emit_type::Infix }
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
            if (aRhs.is("language.identifier"_sv) || 
                aRhs.is("language.type"_sv) || 
                aRhs.is("language.function.parameter"_sv))
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
            if (aRhs.is("language.identifier"_sv))
            {
                data<neos::language::i_function_parameter>().set_parameter_name(aRhs.source());
                aResult = instance();
            }
            else if (aRhs.is("language.type"_sv))
            {
                data<neos::language::i_function_parameter>().set_parameter_type(aRhs.data<neos::language::type>());
                aResult = instance();
            }
            else if (aRhs.is("language.function.parameter"_sv))
            {
                aResult = language_function_parameters{}.instantiate(aContext, source());
                aResult->data<neos::language::i_function_parameters>().push_back(data<neos::language::i_function_parameter>());
                aResult->data<neos::language::i_function_parameters>().push_back(aRhs.data<neos::language::i_function_parameter>());
            }
        }
    };

    class language_function_parameter_in : public semantic_concept<language_function_parameter_in>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.parameter.in";
        // construction
    public:
        language_function_parameter_in() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_parameter_out : public semantic_concept<language_function_parameter_out>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.parameter.out";
        // construction
    public:
        language_function_parameter_out() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_parameter_inout : public semantic_concept<language_function_parameter_inout>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.parameter.inout";
        // construction
    public:
        language_function_parameter_inout() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_return_type : public semantic_concept<language_function_return_type>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.return.type";
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_function_return_type() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.is("language.type"_sv))
                return true;
            return false;
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (aRhs.is("language.type"_sv))
            {
                data<neos::language::type>() = aRhs.data<neos::language::type>();
                aResult = instance();
            }
        }
    };

    class language_function_locals : public semantic_concept<language_function_locals>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.locals";
        // construction
    public:
        language_function_locals() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_local : public semantic_concept<language_function_local>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.local";
        // construction
    public:
        language_function_local() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_import : public semantic_concept<language_function_import>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.import";
        // construction
    public:
        language_function_import() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_arguments : public semantic_concept<language_function_arguments>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.arguments";
        // construction
    public:
        language_function_arguments() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_argument : public semantic_concept<language_function_argument>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.argument";
        // construction
    public:
        language_function_argument() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_call : public semantic_concept<language_function_call>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.call";
        // construction
    public:
        language_function_call() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_function_return : public semantic_concept<language_function_return>
    {
        // concept
    public:
        static constexpr auto Name = "language.function.return";
        // construction
    public:
        language_function_return() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
    };

    class language_expression : public semantic_concept<language_expression>
    {
        // concept
    public:
        static constexpr auto Name = "language.expression";
        // construction
    public:
        language_expression() :
            semantic_concept{ neos::language::emit_type::Infix }
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

    class language_initialize : public semantic_concept<language_initialize>
    {
        // concept
    public:
        static constexpr auto Name = "language.initialize";
        // construction
    public:
        language_initialize() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            // todo
            return false;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            // todo
        }
    };

    class language_assignment : public semantic_concept<language_assignment>
    {
        // concept
    public:
        static constexpr auto Name = "language.assignment";
        // construction
    public:
        language_assignment() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            // todo
            return false;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            // todo
        }
    };

    template <typename Derived = void>
    class language_type : public semantic_concept_t<Derived, language_type<Derived>>
    {
        // concept
    public:
        static constexpr auto Name = "language.type";
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_type() :
            semantic_concept_t<Derived, language_type<Derived>>{ neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.is("language.identifier"_sv))
                return true;
            return false;
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            if (aRhs.is("language.identifier"_sv))
            {
                // todo
                aResult = this->instance();
            }
        }
    };

    class language_type_tuple : public language_type<language_type_tuple>
    {
        // concept
    public:
        static constexpr auto Name = "language.type.tuple";
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_type_tuple() :
            language_type<language_type_tuple>{}
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

    namespace
    {
        template <typename Type> struct float_type_name;
        template<> struct float_type_name<float> { static constexpr auto Name = "language.type.float"; };
        template<> struct float_type_name<double> { static constexpr auto Name = "language.type.double"; };
    }

    template <typename Float>
    class language_type_float : public language_type<language_type_float<Float>>
    {
        // concept
    public:
        static constexpr auto Name = float_type_name<Float>::Name;
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_type_float() :
            language_type<language_type_float<Float>>{}
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

    namespace
    {
        template <typename Type> struct integer_type_name;
        template<> struct integer_type_name<std::int8_t> { static constexpr auto Name = "language.type.i8"; };
        template<> struct integer_type_name<std::uint8_t> { static constexpr auto Name = "language.type.u8"; };
        template<> struct integer_type_name<std::int16_t> { static constexpr auto Name = "language.type.i16"; };
        template<> struct integer_type_name<std::uint16_t> { static constexpr auto Name = "language.type.u16"; };
        template<> struct integer_type_name<std::int32_t> { static constexpr auto Name = "language.type.i32"; };
        template<> struct integer_type_name<std::uint32_t> { static constexpr auto Name = "language.type.u32"; };
        template<> struct integer_type_name<std::int64_t> { static constexpr auto Name = "language.type.i64"; };
        template<> struct integer_type_name<std::uint64_t> { static constexpr auto Name = "language.type.u64"; };
    }

    template <typename Integer>
    class language_type_integer : public language_type<language_type_integer<Integer>>
    {
        // concept
    public:
        static constexpr auto Name = integer_type_name<Integer>::Name;
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_type_integer() :
            language_type<language_type_integer<Integer>>{}
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
    class language_type_string : public language_type<language_type_string<Character>>
    {
        // concept
    public:
        static constexpr auto Name = "language.type.string";
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_type_string() :
            language_type<language_type_string<Character>>{}
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

    class language_type_custom : public language_type<language_type_custom>
    {
        // concept
    public:
        static constexpr auto Name = "language.type.custom";
        // data
    public:
        using data_type = neos::language::type;
        // construction
    public:
        language_type_custom() :
            language_type{}
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
        concepts()[neolib::string{ language_program::Name }] =
            neolib::make_ref<language_program>();
        concepts()[neolib::string{ language_namespace::Name }] =
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
        concepts()[neolib::string{ language_keyword::Name }] =
            neolib::make_ref<language_keyword>();
        concepts()[neolib::string{ language_identifier::Name }] =
            neolib::make_ref<language_identifier>();
        concepts()[neolib::string{ language_scope::Name }] =
            neolib::make_ref<language_scope>();
        concepts()[neolib::string{ language_namespace_name::Name }] =
            neolib::make_ref<language_namespace_name>();
        concepts()[neolib::string{ language_scope_open<>::Name }] =
            neolib::make_ref<language_scope_open<>>();
        concepts()[neolib::string{ language_namespace_scope_open::Name }] =
            neolib::make_ref<language_namespace_scope_open>();
        concepts()[neolib::string{ language_scope_open_by_indentation::Name }] =
            neolib::make_ref<language_scope_open_by_indentation>();
        concepts()[neolib::string{ language_scope_close<>::Name }] =
            neolib::make_ref<language_scope_close<>>();
        concepts()[neolib::string{ language_namespace_scope_close::Name }] =
            neolib::make_ref<language_namespace_scope_close>();
        concepts()[neolib::string{ language_scope_add_package::Name }] =
            neolib::make_ref<language_scope_add_package>();
        concepts()[neolib::string{ language_function::Name }] =
            neolib::make_ref<language_function>();
        concepts()[neolib::string{ language_function_name::Name }] =
            neolib::make_ref<language_function_name>();
        concepts()[neolib::string{ language_function_scope::Name }] =
            neolib::make_ref<language_function_scope>();
        concepts()[neolib::string{ language_function_body::Name }] =
            neolib::make_ref<language_function_body>();
        concepts()[neolib::string{ language_function_parameters::Name }] =
            neolib::make_ref<language_function_parameters>();
        concepts()[neolib::string{ language_function_parameter::Name }] =
            neolib::make_ref<language_function_parameter>();
        concepts()[neolib::string{ language_function_parameter_in::Name }] =
            neolib::make_ref<language_function_parameter_in>();
        concepts()[neolib::string{ language_function_parameter_out::Name }] =
            neolib::make_ref<language_function_parameter_out>();
        concepts()[neolib::string{ language_function_parameter_inout::Name }] =
            neolib::make_ref<language_function_parameter_inout>();
        concepts()[neolib::string{ language_function_return_type::Name }] =
            neolib::make_ref<language_function_return_type>();
        concepts()[neolib::string{ language_function_locals::Name }] =
            neolib::make_ref<language_function_locals>();
        concepts()[neolib::string{ language_function_local::Name }] =
            neolib::make_ref<language_function_local>();
        concepts()[neolib::string{ language_function_signature::Name }] =
            neolib::make_ref<language_function_signature>();
        concepts()[neolib::string{ language_function_import::Name }] =
            neolib::make_ref<language_function_import>();
        concepts()[neolib::string{ language_function_arguments::Name }] =
            neolib::make_ref<language_function_arguments>();
        concepts()[neolib::string{ language_function_argument::Name }] =
            neolib::make_ref<language_function_argument>();
        concepts()[neolib::string{ language_function_call::Name }] =
            neolib::make_ref<language_function_call>();
        concepts()[neolib::string{ language_function_return::Name }] =
            neolib::make_ref<language_function_return>();
        concepts()[neolib::string{ language_expression::Name }] =
            neolib::make_ref<language_expression>();
        concepts()[neolib::string{ "language.expression.operand" }] =
            neolib::make_ref<neos::language::unimplemented_semantic_concept>("language.expression.operand");
        concepts()[neolib::string{ language_initialize::Name }] =
            neolib::make_ref<language_initialize>();
        concepts()[neolib::string{ language_assignment::Name }] =
            neolib::make_ref<language_assignment>();
        concepts()[neolib::string{ language_type<>::Name }] =
            neolib::make_ref<language_type<>>();
        concepts()[neolib::string{ language_type_tuple::Name }] =
            neolib::make_ref<language_type_tuple>();
        concepts()[neolib::string{ language_type_float<float>::Name }] =
            neolib::make_ref<language_type_float<float>>();
        concepts()[neolib::string{ language_type_float<double>::Name }] =
            neolib::make_ref<language_type_float<double>>();
        concepts()[neolib::string{ language_type_integer<std::int8_t>::Name }] =
            neolib::make_ref<language_type_integer<std::int8_t>>();
        concepts()[neolib::string{ language_type_integer<std::uint8_t>::Name }] =
            neolib::make_ref<language_type_integer<std::uint8_t>>();
        concepts()[neolib::string{ language_type_integer<int16_t>::Name }] =
            neolib::make_ref<language_type_integer<int16_t>>();
        concepts()[neolib::string{ language_type_integer<std::uint16_t>::Name }] =
            neolib::make_ref<language_type_integer<std::uint16_t>>();
        concepts()[neolib::string{ language_type_integer<std::int32_t>::Name }] =
            neolib::make_ref<language_type_integer<std::int32_t>>();
        concepts()[neolib::string{ language_type_integer<std::uint32_t>::Name }] =
            neolib::make_ref<language_type_integer<std::uint32_t>>();
        concepts()[neolib::string{ language_type_integer<std::int64_t>::Name }] =
            neolib::make_ref<language_type_integer<std::int64_t>>();
        concepts()[neolib::string{ language_type_integer<std::uint64_t>::Name }] =
            neolib::make_ref<language_type_integer<std::uint64_t>>();
// bignum
//         concepts()[neolib::string{ language_type_integer<neonumerical::xxx>::Name }] = 
//            neolib::make_ref<language_type_integer<neonumerical::xxx>>();
//         concepts()[neolib::string{ language_type_integer<neonumerical::xxx>::Name }] = 
//            neolib::make_ref<language_type_integer<neonumerical::xxx>>();
//         concepts()[neolib::string{ language_type_integer<neonumerical::xxx>::Name }] = 
//            neolib::make_ref<language_type_integer<neonumerical::xxx>>();
        concepts()[neolib::string{ language_type_string<char>::Name }] =
            neolib::make_ref<language_type_string<char>>();
        concepts()[neolib::string{ language_type_custom::Name }] =
            neolib::make_ref<language_type_custom>();
        concepts()[neolib::string{ language_comment::Name }] =
            neolib::make_ref<language_comment>();
        concepts()[neolib::string{ language_whitespace::Name }] =
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

