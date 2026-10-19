/*
  i_language_compiler.hpp

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

#pragma once

#include <neos/neos.hpp>
#include <neolib/core/i_reference_counted.hpp>
#include <neolib/core/i_string.hpp>
#include <neolib/core/i_string_view.hpp>
#include <neolib/core/uuid.hpp>
#include <neos/language/i_compiler.hpp>

namespace neos::language
{
    class i_code_generator;

    // An indented construct (i_compiler::begin_indented) whose code generation is a language's (e.g. try and with)
    class i_indented_construct
    {
    public:
        virtual ~i_indented_construct() = default;
    public:
        virtual indented_construct kind() const = 0;
        virtual bool try_else() const = 0;                      ///< (Else: of a try statement)
        virtual std::size_t condition_count() const = 0;        ///< (With: the context manager)
        virtual i_fragment const& condition(std::size_t aIndex) const = 0;
        virtual std::size_t arm_count() const = 0;              ///< (Try: the body, then the handlers' bodies)
        virtual i_fragment const& arm(std::size_t aIndex) const = 0;
        virtual i_fragment const* else_arm() const = 0;
        virtual i_fragment const* finally_arm() const = 0;
        virtual std::size_t handler_type_count() const = 0;     ///< (Try: the handlers' exception types (none: any))
        virtual i_fragment const* handler_type(std::size_t aIndex) const = 0;
        virtual std::size_t handler_name_count() const = 0;     ///< (the names the handlers (With: the manager) bind)
        virtual neolib::i_string const* handler_name(std::size_t aIndex) const = 0;
    };

    // A language's own code generation (a compiler plugin, ".ncp"): the compiler (i_code_generator) compiles a schema's
    // generic constructs and calls a language compiler (that of the schema's meta "compiler") for the constructs of its
    // language (i_compiler::emit_language_construct) and where the language's semantics are its own (the hooks below).
    // A hook that a language does not need does nothing (or returns false: the compiler's own).
    class i_language_compiler : public neolib::i_reference_counted
    {
    public:
        using abstract_type = i_language_compiler;
    public:
        virtual neolib::i_string const& language() const = 0;    ///< (e.g. "javascript": the language of its constructs)
        virtual void attach(i_code_generator& aGenerator) = 0;  ///< (the compiler whose services it uses)
        // compilation passes
    public:
        virtual void set_options(neolib::i_string const& aOptions) = 0; ///< (the language's options for the next compilation (e.g. a mode))
        virtual bool compiles_at_run_time() const = 0;          ///< (the program compiled last compiles source while it runs (e.g. Python's 
                                                                ///< exec): an image of it needs the compiler)
        virtual void begin_compilation() = 0;                   ///< (a program: the facts inferred by previous passes are forgotten)
        virtual void begin_pass() = 0;                          ///< (a pass: the facts it finds are reset)
        virtual void end_pass() = 0;                            ///< (a pass that succeeded: the facts it found are those of the next 
                                                                ///< pass (if they changed: i_code_generator::set_changed))
        virtual void begin_fragment(i_source_fragment& aFragment) = 0; ///< (a source fragment, before it is parsed (the language's own pass 
                                                                ///< over it, e.g. joining lines): a fragment compiled (e.g. imported) 
                                                                ///< while another is begins and ends within it)
        virtual void end_fragment() = 0;
        virtual void preparse(i_source_fragment& aFragment) = 0; ///< (the language's own pass over a source fragment before it is parsed)
        virtual void parsed(i_source_fragment& aFragment) = 0;  ///< (a source fragment that parsed, before it is compiled: e.g. the language's 
                                                                ///< errors that only a source that parses can have)
        virtual void diagnose(i_source_fragment const& aFragment, std::uint32_t& aLine, std::uint32_t& aColumn, std::uint32_t& aEndLine, 
            std::uint32_t& aEndColumn, neolib::i_string& aType, neolib::i_string& aMessage, neolib::i_string& aReport) = 0; ///< (a compile error 
                                                                ///< of a fragment (its location (columns are 1-based; an end of 0: none), type and 
                                                                ///< message: the compiler's) as the language reports it: it may change them; its 
                                                                ///< report (empty: the compiler's))
        // constructs
    public:
        virtual void emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource) = 0;
        virtual bool complete_signature(i_function_signature& aSignature, neolib::i_string_view const& aSource) = 0; ///< (false: the compiler's)
        // inferred types (of functions whose parameter and result types are inferred)
    public:
        virtual type inferred_type(i_operand_type const& aOperand) = 0;     ///< (of a value returned or passed: the language's type)
        virtual type parameter_type(neolib::i_string_view const& aFunction, std::uint32_t aIndex, type aType) = 0; ///< (a parameter's
                                                                ///< type (aType: inferred from the arguments passed to it))
        virtual bool value_range(i_operand_type const& aOperand, std::int64_t& aLow, std::int64_t& aHigh) = 0; ///< (an integer value's range)
        // names and values (a fragment returned is the language compiler's, valid until its next call; null: the compiler's own)
    public:
        virtual bool late_binding() const = 0;                  ///< (a name is that of the function being compiled (or a global): 
                                                                ///< looked up when it is used (top-level statements: the entry point's))
        virtual bool functions_follow_statements() const = 0;   ///< (a function can be defined after top-level statements)
        virtual void fragment_function(i_source_fragment const& aFragment, neolib::i_string& aName) = 0; ///< (the function that an imported 
                                                                ///< fragment's top-level statements form (not the entry point; defined even 
                                                                ///< if it has none): empty for none (they are the entry point's))
        virtual i_fragment const* literal(i_operand_type const& aOperand) = 0;  ///< (a literal operand: the language's value of it)
        virtual i_fragment const* unbound_name(neolib::i_string_view const& aName) = 0; ///< (a name that is not a variable (late binding))
        virtual i_fragment const* variable_value(i_fragment const& aValue, neolib::i_string_view const& aName, i_symbol_table_entry const& aVariable) = 0; ///< (a 
                                                                ///< variable's value (aValue) as the language reads it (e.g. checked: bound); null: aValue)
        virtual i_fragment const* convert_value(i_fragment const& aValue, type aTarget) = 0;
        virtual i_fragment const* void_value(i_fragment const& aStatement) = 0; ///< (a statement (e.g. a call of a function that returns
                                                                ///< nothing) used as a value: the language's value of it (null: an error))
        virtual i_fragment const* division_by_zero(std::int32_t aKind) = 0; ///< (aKind: 0 int /, 1 float /, 2 float //, 3 float %, 4 int //, 5 int %)
        virtual type argument_type(i_operand_type const& aOperand) = 0;     ///< (the type of an argument's value (UNKNOWN: its own))
        // functions, variables and constructs
    public:
        virtual void function_start() = 0;                      ///< (a function's body begins)
        virtual i_fragment const* function_end(bool aImplicit) = 0; ///< (a function's body ends: the statement that ends it)
        virtual void finishing_function(bool aEntry) = 0;       ///< (a function is finished (before its body is collected))
        virtual void function_finished(std::uint32_t aFunction) = 0; ///< (a function (not an import or stub) is finished: its body is collected)
        virtual i_fragment const* finish_construct(i_indented_construct const& aConstruct) = 0; ///< (e.g. try and with: the statement)
        virtual bool bind_name(neolib::i_string_view const& aName, type aType) = 0; ///< (a name that a construct binds, e.g. with ... as name)
        virtual bool declare_loop_variable(neolib::i_string_view const& aName, i_symbol_table_entry const* aSymbol) = 0; ///< (false: the compiler's)
        // integers of any size (type::Integer) that a for loop over a range counts
    public:
        virtual i_fragment const* small_integers(neolib::i_vector<std::uint32_t> const& aLocals) = 0; ///< (a check that their values are small)
        virtual void loop_variable_range(neolib::i_string_view const& aName, std::int64_t aLow, std::int64_t aHigh) = 0;
        virtual i_fragment const* release_integer(std::uint32_t aVariable, bool aGlobal) = 0;
        // frames (see neos_rt_host::frames)
    public:
        virtual std::uint32_t source_line(i_source_fragment const& aFragment, std::size_t aPosition) = 0; ///< (the line (1-based) of a position 
                                                                ///< in a fragment's source (as the language compiles it) in its source as written 
                                                                ///< (e.g. before the language's own pass over it): 0, the compiler's (counted))
        virtual bool frame_name(std::uint32_t aFunction, neolib::i_string& aName) = 0; ///< (the name of a function's frame (empty: it has none); 
                                                                ///< false: the compiler's (the function's name))
        // interface
    public:
        static const neolib::uuid& iid() { static neolib::uuid sId = neolib::make_uuid("6A3B5D0E-2C47-4C1F-9E58-7B1D2A90C3F4"); return sId; }
    };
}
