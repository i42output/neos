/*
  python.cpp

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

#include <neos/i_context.hpp>
#include <neos/language/semantic_concept.hpp>
#include "python.hpp"
#include <neos/language/python.hpp>

// Python specific concepts: scoping by significant whitespace (indentation), name binding by assignment, 
// Python's division/modulo operators, stub ("...") function bodies

namespace neos::concepts::python
{
    namespace
    {
        void emit(i_context& aContext, language::python::construct aConstruct, neolib::i_string_view const& aSource)
        {
            aContext.compiler().emit_language_construct(neolib::string_view{ language::python::Language }, static_cast<std::uint32_t>(aConstruct), aSource);
        }
    }
   
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
            semantic_concept{ neolib::string_view{ Name }, neos::language::emit_type::Infix, neolib::string_view{ "language.whitespace" } }
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
            semantic_concept{ neolib::string_view{ Name }, neos::language::emit_type::Infix, neolib::string_view{ "language.statement" } }
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
            emit(aContext, language::python::construct::Statement, source());
            aContext.compiler().emit_indented_statement(source());
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
        template <> struct indented_name<language::indented_construct::Try> { static constexpr auto Name = "python.try"; };
        template <> struct indented_name<language::indented_construct::Except> { static constexpr auto Name = "python.except"; };
        template <> struct indented_name<language::indented_construct::Finally> { static constexpr auto Name = "python.finally"; };
        template <> struct indented_name<language::indented_construct::With> { static constexpr auto Name = "python.with"; };
        // (the generic concept (neos.core's) that a compound statement's concept derives from, if any: e.g. for a script's outline (see 
        // neos::context::outline))
        template <language::indented_construct Construct> struct indented_generic { static constexpr auto Name = ""; };
        template <> struct indented_generic<language::indented_construct::If> { static constexpr auto Name = "language.statement.if"; };
        template <> struct indented_generic<language::indented_construct::ElseIf> { static constexpr auto Name = "language.statement.elseif"; };
        template <> struct indented_generic<language::indented_construct::Else> { static constexpr auto Name = "language.statement.else"; };
        template <> struct indented_generic<language::indented_construct::While> { static constexpr auto Name = "language.statement.loop"; };
        template <> struct indented_generic<language::indented_construct::For> { static constexpr auto Name = "language.statement.loop"; };
        template <> struct indented_generic<language::indented_construct::Function> { static constexpr auto Name = "language.function"; };
        template <> struct indented_generic<language::indented_construct::Try> { static constexpr auto Name = "language.statement.try"; };
        template <> struct indented_generic<language::indented_construct::Except> { static constexpr auto Name = "language.statement.catch"; };
        template <> struct indented_generic<language::indented_construct::Finally> { static constexpr auto Name = "language.statement.finally"; };
        template <> struct indented_generic<language::indented_construct::With> { static constexpr auto Name = "language.statement.with"; };
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
            semantic_concept<python_compound_statement<Construct>>{ neolib::string_view{ Name }, neos::language::emit_type::Infix, 
                neolib::string_view{ indented_generic<Construct>::Name } }
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
            if constexpr (Construct == language::indented_construct::If || Construct == language::indented_construct::ElseIf ||
                Construct == language::indented_construct::While)
                emit(aContext, language::python::construct::Condition, this->source());
            aContext.compiler().begin_indented(Construct, this->source());
            if constexpr (Construct == language::indented_construct::Function)
                emit(aContext, language::python::construct::Function, this->source());
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
            semantic_concept{ neolib::string_view{ Name }, neos::language::emit_type::Infix, neolib::string_view{ "language.function.local" } }
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
        using language::python::construct;
        template <construct Construct> struct python_construct_name;
        template <> struct python_construct_name<construct::Return> { static constexpr auto Name = "python.return"; };
        template <> struct python_construct_name<construct::Break> { static constexpr auto Name = "python.break"; };
        template <> struct python_construct_name<construct::Continue> { static constexpr auto Name = "python.continue"; };
        template <> struct python_construct_name<construct::Import> { static constexpr auto Name = "python.import"; };
        template <> struct python_construct_name<construct::ImportFrom> { static constexpr auto Name = "python.import.from"; };
        template <> struct python_construct_name<construct::Assign> { static constexpr auto Name = "python.assign"; };
        template <> struct python_construct_name<construct::AugmentedAssign> { static constexpr auto Name = "python.assign.augmented"; };
        template <> struct python_construct_name<construct::Target> { static constexpr auto Name = "python.target"; };
        template <> struct python_construct_name<construct::ForTargets> { static constexpr auto Name = "python.for.targets"; };
        template <> struct python_construct_name<construct::ForIn> { static constexpr auto Name = "python.for.in"; };
        template <> struct python_construct_name<construct::AsName> { static constexpr auto Name = "python.as.name"; };
        template <> struct python_construct_name<construct::Conditional> { static constexpr auto Name = "python.conditional"; };
        template <> struct python_construct_name<construct::Or> { static constexpr auto Name = "python.or"; };
        template <> struct python_construct_name<construct::And> { static constexpr auto Name = "python.and"; };
        template <> struct python_construct_name<construct::Not> { static constexpr auto Name = "python.not"; };
        template <> struct python_construct_name<construct::LessEqual> { static constexpr auto Name = "python.lessequal"; };
        template <> struct python_construct_name<construct::GreaterEqual> { static constexpr auto Name = "python.greaterequal"; };
        template <> struct python_construct_name<construct::Equal> { static constexpr auto Name = "python.equal"; };
        template <> struct python_construct_name<construct::NotEqual> { static constexpr auto Name = "python.notequal"; };
        template <> struct python_construct_name<construct::Less> { static constexpr auto Name = "python.less"; };
        template <> struct python_construct_name<construct::Greater> { static constexpr auto Name = "python.greater"; };
        template <> struct python_construct_name<construct::NotIn> { static constexpr auto Name = "python.notin"; };
        template <> struct python_construct_name<construct::In> { static constexpr auto Name = "python.in"; };
        template <> struct python_construct_name<construct::IsNot> { static constexpr auto Name = "python.isnot"; };
        template <> struct python_construct_name<construct::Is> { static constexpr auto Name = "python.is"; };
        template <> struct python_construct_name<construct::BitOr> { static constexpr auto Name = "python.bitor"; };
        template <> struct python_construct_name<construct::BitXor> { static constexpr auto Name = "python.bitxor"; };
        template <> struct python_construct_name<construct::BitAnd> { static constexpr auto Name = "python.bitand"; };
        template <> struct python_construct_name<construct::LeftShift> { static constexpr auto Name = "python.lshift"; };
        template <> struct python_construct_name<construct::RightShift> { static constexpr auto Name = "python.rshift"; };
        template <> struct python_construct_name<construct::Add> { static constexpr auto Name = "python.add"; };
        template <> struct python_construct_name<construct::Subtract> { static constexpr auto Name = "python.subtract"; };
        template <> struct python_construct_name<construct::Multiply> { static constexpr auto Name = "python.multiply"; };
        template <> struct python_construct_name<construct::MatMul> { static constexpr auto Name = "python.matmul"; };
        template <> struct python_construct_name<construct::FloorDivide> { static constexpr auto Name = "python.floordivide"; };
        template <> struct python_construct_name<construct::Divide> { static constexpr auto Name = "python.divide"; };
        template <> struct python_construct_name<construct::Modulo> { static constexpr auto Name = "python.modulo"; };
        template <> struct python_construct_name<construct::Negative> { static constexpr auto Name = "python.negative"; };
        template <> struct python_construct_name<construct::Positive> { static constexpr auto Name = "python.positive"; };
        template <> struct python_construct_name<construct::Invert> { static constexpr auto Name = "python.invert"; };
        template <> struct python_construct_name<construct::Power> { static constexpr auto Name = "python.power"; };
        template <> struct python_construct_name<construct::Call> { static constexpr auto Name = "python.call"; };
        template <> struct python_construct_name<construct::Subscript> { static constexpr auto Name = "python.subscript"; };
        template <> struct python_construct_name<construct::Slice> { static constexpr auto Name = "python.slice"; };
        template <> struct python_construct_name<construct::Attribute> { static constexpr auto Name = "python.attribute"; };
        template <> struct python_construct_name<construct::EmptyTuple> { static constexpr auto Name = "python.tuple.empty"; };
        template <> struct python_construct_name<construct::Tuple> { static constexpr auto Name = "python.tuple"; };
        template <> struct python_construct_name<construct::List> { static constexpr auto Name = "python.list"; };
        template <> struct python_construct_name<construct::Dict> { static constexpr auto Name = "python.dict"; };
        template <> struct python_construct_name<construct::Ellipsis> { static constexpr auto Name = "python.ellipsis"; };
        template <> struct python_construct_name<construct::None> { static constexpr auto Name = "python.none"; };
        template <> struct python_construct_name<construct::Builtin> { static constexpr auto Name = "python.builtin"; };
        template <> struct python_construct_name<construct::ComprehensionStart> { static constexpr auto Name = "python.comprehension.start"; };
        template <> struct python_construct_name<construct::ComprehensionFor> { static constexpr auto Name = "python.comprehension.for"; };
        template <> struct python_construct_name<construct::ComprehensionIf> { static constexpr auto Name = "python.comprehension.if"; };
        template <> struct python_construct_name<construct::ListComprehension> { static constexpr auto Name = "python.list.comprehension"; };
        template <> struct python_construct_name<construct::ParameterDefault> { static constexpr auto Name = "python.parameter.default"; };
        template <> struct python_construct_name<construct::Raise> { static constexpr auto Name = "python.raise"; };
        template <> struct python_construct_name<construct::Assert> { static constexpr auto Name = "python.assert"; };
        template <> struct python_construct_name<construct::Imaginary> { static constexpr auto Name = "python.imaginary"; };
        template <> struct python_construct_name<construct::Global> { static constexpr auto Name = "python.global"; };
        template <> struct python_construct_name<construct::DictComprehension> { static constexpr auto Name = "python.dict.comprehension"; };
        template <> struct python_construct_name<construct::Set> { static constexpr auto Name = "python.set"; };
        template <> struct python_construct_name<construct::SetComprehension> { static constexpr auto Name = "python.set.comprehension"; };
        template <> struct python_construct_name<construct::Del> { static constexpr auto Name = "python.del"; };
        template <> struct python_construct_name<construct::Walrus> { static constexpr auto Name = "python.walrus"; };
        template <> struct python_construct_name<construct::TargetGroup> { static constexpr auto Name = "python.target.group"; };
        // (the generic concept (neos.core's) that a construct's concept derives from, if any: e.g. for a script's outline (see 
        // neos::context::outline))
        template <construct Construct> struct python_construct_generic { static constexpr auto Name = ""; };
        template <> struct python_construct_generic<construct::Return> { static constexpr auto Name = "language.function.return"; };
        template <> struct python_construct_generic<construct::ForIn> { static constexpr auto Name = "language.statement.loop"; };
        template <> struct python_construct_generic<construct::Break> { static constexpr auto Name = "language.statement.break"; };
        template <> struct python_construct_generic<construct::Continue> { static constexpr auto Name = "language.statement.continue"; };
        template <> struct python_construct_generic<construct::Raise> { static constexpr auto Name = "language.statement.throw"; };
        template <> struct python_construct_generic<construct::Target> { static constexpr auto Name = "language.function.local"; };
        template <> struct python_construct_generic<construct::TargetGroup> { static constexpr auto Name = "language.function.local"; };
        template <> struct python_construct_generic<construct::ForTargets> { static constexpr auto Name = "language.function.local"; };
        template <> struct python_construct_generic<construct::AsName> { static constexpr auto Name = "language.function.local"; };
    }

    // a construct whose code the compiler generates once its parts have been compiled
    template <construct Construct>
    class python_construct : public semantic_concept<python_construct<Construct>>
    {
        // concept
    public:
        static constexpr auto Name = python_construct_name<Construct>::Name;
        // construction
    public:
        python_construct() :
            semantic_concept<python_construct<Construct>>{ neolib::string_view{ Name }, neos::language::emit_type::Infix, 
                neolib::string_view{ python_construct_generic<Construct>::Name } }
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
            emit(aContext, Construct, this->source());
        }
    };

    namespace
    {
        template <construct... Constructs>
        void add_constructs(language::concept_library::concepts_t& aConcepts)
        {
            ((aConcepts[neolib::string{ python_construct<Constructs>::Name }] = neolib::make_ref<python_construct<Constructs>>()), ...);
        }
    }

    python::python(const std::string& aLibraryUri) :
        neos::language::concept_library
        { 
            library_id(), 
            aLibraryUri,
            library_name(), 
            "Python language concepts", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2026 Leigh Johnston.  All Rights Reserved."
        }
    {
        concepts()[neolib::string{ python_module::Name }] = neolib::make_ref<python_module>();
        concepts()[neolib::string{ python_indentation::Name }] = neolib::make_ref<python_indentation>();
        concepts()[neolib::string{ python_statement::Name }] = neolib::make_ref<python_statement>();
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
        concepts()[neolib::string{ python_compound_statement<language::indented_construct::Try>::Name }] = 
            neolib::make_ref<python_compound_statement<language::indented_construct::Try>>();
        concepts()[neolib::string{ python_compound_statement<language::indented_construct::Except>::Name }] = 
            neolib::make_ref<python_compound_statement<language::indented_construct::Except>>();
        concepts()[neolib::string{ python_compound_statement<language::indented_construct::Finally>::Name }] = 
            neolib::make_ref<python_compound_statement<language::indented_construct::Finally>>();
        concepts()[neolib::string{ python_compound_statement<language::indented_construct::With>::Name }] = 
            neolib::make_ref<python_compound_statement<language::indented_construct::With>>();
        concepts()[neolib::string{ python_for_variable::Name }] = neolib::make_ref<python_for_variable>();
        concepts()[neolib::string{ python_pass::Name }] = neolib::make_ref<python_pass>();
        concepts()[neolib::string{ python_stub::Name }] = neolib::make_ref<python_stub>();
        add_constructs<construct::Return, construct::Break, construct::Continue, construct::Import, construct::ImportFrom, construct::Assign, construct::AugmentedAssign, construct::Target, construct::ForTargets, construct::ForIn, construct::Conditional, construct::Or, construct::And, construct::Not, construct::LessEqual, construct::GreaterEqual, construct::Equal, construct::NotEqual, construct::Less, construct::Greater, construct::NotIn, construct::In, construct::IsNot, construct::Is, construct::BitOr, construct::BitXor, construct::BitAnd, construct::LeftShift, construct::RightShift, construct::Add, construct::Subtract, construct::Multiply, construct::MatMul, construct::FloorDivide, construct::Divide, construct::Modulo, construct::Negative, construct::Positive, construct::Invert, construct::Power, construct::Call, construct::Subscript, construct::Slice, construct::Attribute, construct::EmptyTuple, construct::Tuple, construct::List, construct::Dict, construct::Ellipsis, construct::None, construct::Builtin,
            construct::ComprehensionStart, construct::ComprehensionFor, construct::ComprehensionIf, construct::ListComprehension, construct::ParameterDefault, construct::Raise, construct::Assert, construct::Imaginary, construct::Global, construct::DictComprehension, construct::Set, construct::SetComprehension, construct::Del, construct::Walrus, construct::AsName, construct::TargetGroup>(concepts());
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
