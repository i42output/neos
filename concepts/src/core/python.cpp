/*
  python.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

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
#include "python.hpp"

// Python specific concepts: scoping by significant whitespace (indentation), name binding by assignment, 
// Python's division/modulo operators, stub ("...") function bodies

namespace neos::concepts::core
{   
    class python_module : public semantic_concept<python_module>
    {
        // concept
    public:
        static constexpr auto Name = "python.module";
        // construction
    public:
        python_module() :
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
            // end of module: close all indented blocks
            aContext.compiler().end_indented();
        }
    };

    class python_indentation : public semantic_concept<python_indentation>
    {
        // concept
    public:
        static constexpr auto Name = "python.indentation";
        // construction
    public:
        python_indentation() :
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
            // a line break followed by the next line's indentation: closes blocks indented further than the next line
            aContext.compiler().indentation(source());
        }
    };

    class python_statement : public semantic_concept<python_statement>
    {
        // concept
    public:
        static constexpr auto Name = "python.statement";
        // construction
    public:
        python_statement() :
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
            aContext.compiler().emit_indented_statement(source());
        }
    };

    namespace
    {
        std::string_view assignment_target(std::string_view const& aSource)
        {
            auto const end = aSource.find_first_of(" \t+-*/%=");
            return aSource.substr(0u, end);
        }
    }

    class python_assignment : public semantic_concept<python_assignment>
    {
        // concept
    public:
        static constexpr auto Name = "python.assignment";
        // construction
    public:
        python_assignment() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            return aRhs.is("language.identifier"_sv);
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            // binding a name: defines the variable (type inferred from the value) if it does not yet exist
            aContext.compiler().emit_binding(aRhs.source(), source());
        }
    };

    class python_assignment_augmented : public semantic_concept<python_assignment_augmented>
    {
        // concept
    public:
        static constexpr auto Name = "python.assignment.augmented";
        // construction
    public:
        python_assignment_augmented() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            return aRhs.is("language.identifier"_sv);
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            auto const text = source().to_std_string_view();
            auto const target = assignment_target(text);
            auto const op = text.substr(text.find_first_not_of(" \t", target.size()));
            language::operation operation;
            if (op.starts_with("+="))
                operation = language::operation::Add;
            else if (op.starts_with("-="))
                operation = language::operation::Subtract;
            else if (op.starts_with("*="))
                operation = language::operation::Multiply;
            else if (op.starts_with("//="))
                operation = language::operation::FloorDivide;
            else if (op.starts_with("/="))
                operation = language::operation::TrueDivide;
            else if (op.starts_with("%="))
                operation = language::operation::Modulo;
            else
                throw std::runtime_error("unsupported augmented assignment");
            aContext.compiler().emit_compound_assignment(operation, neolib::string_view{ target }, source());
        }
    };

    namespace
    {
        template <language::indented_construct Construct> struct indented_name;
        template <> struct indented_name<language::indented_construct::If> { static constexpr auto Name = "python.if"; };
        template <> struct indented_name<language::indented_construct::ElseIf> { static constexpr auto Name = "python.elif"; };
        template <> struct indented_name<language::indented_construct::Else> { static constexpr auto Name = "python.else"; };
        template <> struct indented_name<language::indented_construct::While> { static constexpr auto Name = "python.while"; };
        template <> struct indented_name<language::indented_construct::For> { static constexpr auto Name = "python.for"; };
        template <> struct indented_name<language::indented_construct::Function> { static constexpr auto Name = "python.function"; };
    }

    template <language::indented_construct Construct>
    class python_compound_statement : public semantic_concept<python_compound_statement<Construct>>
    {
        // concept
    public:
        static constexpr auto Name = indented_name<Construct>::Name;
        // construction
    public:
        python_compound_statement() :
            semantic_concept<python_compound_statement<Construct>>{ neos::language::emit_type::Infix }
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
            // a header ending in ':' opens a block whose extent is determined by indentation
            aContext.compiler().begin_indented(Construct, this->source());
        }
    };

    class python_for_variable : public semantic_concept<python_for_variable>
    {
        // concept
    public:
        static constexpr auto Name = "python.for.variable";
        // construction
    public:
        python_for_variable() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            return aRhs.is("language.identifier"_sv);
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            // the loop variable is bound before the loop body (which may follow the header on the same line) is compiled
            aContext.compiler().declare_loop_variable(aRhs.source(), source());
        }
    };

    class python_pass : public semantic_concept<python_pass>
    {
        // concept
    public:
        static constexpr auto Name = "python.pass";
        // construction
    public:
        python_pass() :
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
            // an empty statement
            aContext.compiler().emit_block(source());
        }
    };

    class python_stub : public semantic_concept<python_stub>
    {
        // concept
    public:
        static constexpr auto Name = "python.stub";
        // construction
    public:
        python_stub() :
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
            // a function whose body is "..." is provided by the host
            // (only when "..." is the entire body; elsewhere it is an empty statement)
            aContext.compiler().declare_stub();
            aContext.compiler().emit_block(source());
        }
    };

    namespace
    {
        template <language::operation Operation> struct python_operator_name;
        template <> struct python_operator_name<language::operation::TrueDivide> { static constexpr auto Name = "python.operator.divide.true"; };
        template <> struct python_operator_name<language::operation::FloorDivide> { static constexpr auto Name = "python.operator.divide.floor"; };
        template <> struct python_operator_name<language::operation::Modulo> { static constexpr auto Name = "python.operator.modulo"; };
    }

    template <language::operation Operation>
    class python_operator : public semantic_concept<python_operator<Operation>>
    {
        // concept
    public:
        static constexpr auto Name = python_operator_name<Operation>::Name;
        // construction
    public:
        python_operator() :
            semantic_concept<python_operator<Operation>>{ neos::language::emit_type::Infix }
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
            aContext.compiler().emit_operation(Operation);
        }
    };

    python::python(language::i_concept_library& aParent) :
        neos::language::concept_library
        { 
            aParent,
            library_id(), 
            aParent.uri().to_std_string(),
            library_name(), 
            "Python language concepts", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2026 Leigh Johnston.  All Rights Reserved."
        }
    {
        concepts()[neolib::string{ python_module::Name }] = neolib::make_ref<python_module>();
        concepts()[neolib::string{ python_indentation::Name }] = neolib::make_ref<python_indentation>();
        concepts()[neolib::string{ python_statement::Name }] = neolib::make_ref<python_statement>();
        concepts()[neolib::string{ python_assignment::Name }] = neolib::make_ref<python_assignment>();
        concepts()[neolib::string{ python_assignment_augmented::Name }] = neolib::make_ref<python_assignment_augmented>();
        concepts()[neolib::string{ python_compound_statement<language::indented_construct::If>::Name }] = 
            neolib::make_ref<python_compound_statement<language::indented_construct::If>>();
        concepts()[neolib::string{ python_compound_statement<language::indented_construct::ElseIf>::Name }] = 
            neolib::make_ref<python_compound_statement<language::indented_construct::ElseIf>>();
        concepts()[neolib::string{ python_compound_statement<language::indented_construct::Else>::Name }] = 
            neolib::make_ref<python_compound_statement<language::indented_construct::Else>>();
        concepts()[neolib::string{ python_compound_statement<language::indented_construct::While>::Name }] = 
            neolib::make_ref<python_compound_statement<language::indented_construct::While>>();
        concepts()[neolib::string{ python_compound_statement<language::indented_construct::For>::Name }] = 
            neolib::make_ref<python_compound_statement<language::indented_construct::For>>();
        concepts()[neolib::string{ python_compound_statement<language::indented_construct::Function>::Name }] = 
            neolib::make_ref<python_compound_statement<language::indented_construct::Function>>();
        concepts()[neolib::string{ python_for_variable::Name }] = neolib::make_ref<python_for_variable>();
        concepts()[neolib::string{ python_pass::Name }] = neolib::make_ref<python_pass>();
        concepts()[neolib::string{ python_stub::Name }] = neolib::make_ref<python_stub>();
        concepts()[neolib::string{ python_operator<language::operation::TrueDivide>::Name }] = 
            neolib::make_ref<python_operator<language::operation::TrueDivide>>();
        concepts()[neolib::string{ python_operator<language::operation::FloorDivide>::Name }] = 
            neolib::make_ref<python_operator<language::operation::FloorDivide>>();
        concepts()[neolib::string{ python_operator<language::operation::Modulo>::Name }] = 
            neolib::make_ref<python_operator<language::operation::Modulo>>();
    }

    const std::string& python::library_name()
    {
        static const std::string sName = "neos.python";
        return sName;
    }

    const neolib::uuid& python::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("FAB37CBB-7032-4510-A461-7E69A3AED237");
        return sId;
    }
}
