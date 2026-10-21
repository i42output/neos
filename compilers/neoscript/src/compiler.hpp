/*
  compiler.hpp

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

#pragma once

// The neoscript compiler (a language compiler (i_language_compiler)): the types that a program defines (structs, classes and 
// interfaces) and their members; the compiler's own code generation compiles the rest of the language (functions, statements 
// and expressions), with these hooks:
//
//   - a method is a function in its type's scope whose first parameter is its object (this)
//   - an object is a reference (type::Pointer) to its memory (neos::allocate): its type's id (i32) and then its fields (those of 
//     its base class first); the compiler knows the type of a variable that refers to one from its declaration and the type of 
//     a value from the construct (or variable) that produced it
//   - a struct is a value: it is copied when it is stored (initialized, assigned or passed) or converted to an interface (and 
//     a struct that is a field is the field's own object); a class's object is shared by its references
//   - a method of a class is virtual (unless it or its class is final): a call through a reference whose type has types derived 
//     from it calls a dispatch function (generated) that calls the method of the object's type

#include <neos/neos.hpp>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <neos/language/language_compiler.hpp>
#include <neos/language/neoscript.hpp>

namespace neos::compilers::neoscript
{
    using namespace neos::language;
    using language::neoscript::construct;

    enum class type_kind : std::uint32_t
    {
        Struct,
        Class,
        Interface
    };

    enum class access_kind : std::uint32_t
    {
        Public,
        Protected,
        Private
    };

    struct type_info;

    // the type of a value: a built-in type, or (type::Pointer) an object of a type that the program defines
    struct value_type
    {
        type builtin = type::UNKNOWN;
        type_info* object = nullptr;

        bool is_object() const { return builtin == type::Pointer; }
        bool operator==(value_type const&) const = default;
    };

    struct field_info
    {
        std::string name;
        std::string typeText;
        std::string scope;                      ///< (the scope that its type's name is resolved in)
        value_type valueType;
        access_kind access = access_kind::Public;
        bool isMutable = false;                 ///< (a function that is not mutable can change it)
        std::optional<operand_type> initializer; ///< (a literal)
        std::uint32_t offset = 0u;
        type_info* owner = nullptr;
    };

    struct parameter_info
    {
        std::string name;
        std::string typeText;
        parameter_direction direction = parameter_direction::In;
        value_type valueType;
    };

    struct method_info
    {
        std::string name;
        std::string function;                   ///< (the qualified name of its function)
        std::vector<parameter_info> parameters; ///< (not its object (this))
        std::string resultText;
        value_type result{ type::Void };
        bool procedure = false;
        bool isMutable = false;
        bool isFinal = false;
        bool isOverride = false;
        bool isAbstract = false;                ///< (an interface's)
        bool isConstructor = false;
        access_kind access = access_kind::Public;
        type_info* owner = nullptr;
    };

    struct type_info
    {
        std::string name;
        std::string qualifiedName;
        std::string scope;                      ///< (the scope it is defined in: names are resolved in it)
        type_kind kind = type_kind::Class;
        bool isFinal = false;
        std::optional<std::string> baseText;
        std::vector<std::string> interfaceTexts;
        type_info* base = nullptr;
        std::vector<type_info*> interfaces;
        std::deque<field_info> fields;          ///< (its own)
        std::deque<method_info> methods;        ///< (its own, constructors included)
        std::vector<field_info*> layout;        ///< (all of its fields: its base class's first)
        std::uint32_t size = 0u;
        std::uint32_t id = 0u;
        bool laidOut = false;
        bool layingOut = false;
        std::optional<std::string> error;       ///< (an error that its definition has (reported where it is defined))
        std::size_t methodOrdinal = 0u;         ///< (the methods (with bodies) of its definition compiled by this pass)
        std::size_t fieldOrdinal = 0u;
    };

    struct alias_info
    {
        std::string name;
        std::string targetText;
        std::string scope;
        type_info* target = nullptr;
        bool resolving = false;
    };

    // a function (or method) of the program as it was declared (pass 1)
    struct function_record
    {
        std::string name;                       ///< (qualified)
        std::string function;                   ///< (the name of its function: its name, unless overloads of it have the same 
                                                ///< parameters but for the types of the objects they take (its name, '$' and an index))
        std::vector<parameter_info> parameters; ///< (a method's: its object first)
        std::string resultText;
        std::string scope;
        type_info* object = nullptr;            ///< (a method's type)
    };

    class compiler : public language_compiler
    {
    public:
        compiler();
        // i_language_compiler
    public:
        void begin_compilation() override;
        void begin_pass() override;
        void begin_fragment(i_source_fragment& aFragment) override;
        void end_fragment() override;
        void emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource) override;
        bool complete_signature(i_function_signature& aSignature, neolib::i_string_view const& aSource) override;
        bool frame_name(std::uint32_t aFunction, neolib::i_string& aName) override;
        i_fragment const* literal(i_operand_type const& aOperand) override;
        // constructs
    private:
        void type_definition(std::string_view aSource, bool aInterface);
        void type_definition_end(std::string_view aSource);
        void field(std::string_view aSource);
        void abstract_method(std::string_view aSource);
        void alias(std::string_view aSource);
        void object_initialize(std::string_view aSource);
        void declaration(std::string_view aSource);
        void auto_initialize(std::string_view aSource);
        void assignment(std::string_view aSource);
        void call(std::string_view aSource);
        void method_call(std::string_view aSource);
        void member(std::string_view aSource);
        // types
    private:
        void resolve_types();
        void resolve_type(type_info& aType);
        void lay_out(type_info& aType);
        void check_methods(type_info& aType);
        void distinguish_overloads();
        value_type find_type(std::string const& aText, std::string const& aScope, std::string_view aSource = {}) const;
        type_info* find_object_type(std::string const& aText, std::string const& aScope) const;
        type_info* resolve_alias(alias_info& aAlias) const;
        std::string scope_name() const;         ///< (of the current scope, unqualified)
        bool derives(type_info const& aType, type_info const& aBase) const;     ///< (a class from another class (or itself))
        bool implements(type_info const& aType, type_info const& aInterface) const;
        bool assignable(value_type const& aFrom, value_type const& aTo) const;
        std::vector<type_info*> concrete_types(type_info& aType) const;          ///< (the types whose objects a reference to it can refer to)
        field_info* find_field(type_info& aType, std::string const& aName) const;
        std::vector<method_info*> find_methods(type_info& aType, std::string const& aName) const;
        method_info* implementation(type_info& aType, method_info const& aMethod) const;   ///< (the method that an object of the type calls)
        std::uint32_t size_of(type aType) const;
        type declared_result(method_info const& aMethod) const;
        // values
    private:
        struct function_context
        {
            type_info* object = nullptr;        ///< (a method's type)
            method_info* method = nullptr;
            std::map<std::string, value_type> variables;    ///< (its parameters and locals)
        };
        struct annotation
        {
            std::uint32_t function;
            std::vector<std::uint8_t> code;
            std::vector<std::pair<std::uint32_t, std::uint32_t>> relocations;
            value_type valueType;
            bool fresh = false;                 ///< (a new object: storing it need not copy it)
            bool viaThis = false;               ///< (the object of the method being compiled (or a struct within it))
        };
        function_context* current_context();
        std::uint32_t context_function() const;
        void annotate(fragment const& aValue, value_type const& aType, bool aFresh = false, bool aViaThis = false);
        annotation const* annotation_of(i_fragment const& aValue) const;
        value_type type_of(i_operand_type const& aOperand, bool* aFresh = nullptr, bool* aViaThis = nullptr);
        value_type variable_type(i_symbol_table_entry const& aSymbol, std::string const& aName);
        void define_variable(std::string const& aName, value_type const& aType, std::string_view aSource);
        fragment this_value(std::string_view aSource);
        fragment stored(fragment aValue, value_type const& aValueType, bool aFresh, value_type const& aTarget, std::string_view aSource);
        fragment construct_object(type_info& aType, std::vector<fragment> const& aArguments, std::vector<value_type> const& aArgumentTypes, std::string_view aSource);
        void initialize_object(fragment& aCode, type_info& aType, std::uint32_t aObject, std::string_view aSource);
        fragment call_method(fragment const& aObject, type_info& aType, bool aViaThis, std::string const& aName, std::vector<fragment> aArguments, std::string_view aSource);
        method_info* choose(std::vector<method_info*> const& aCandidates, std::vector<value_type> const& aArguments) const;
        std::int32_t score(method_info const& aMethod, std::vector<value_type> const& aArguments) const;
        void check_access(access_kind aAccess, type_info& aOwner, std::string const& aName, std::string_view aSource);
        void check_change(bool aViaThis, std::string const& aWhat, std::string_view aSource);
        std::uint32_t allocate_function();
        std::uint32_t copy_function(type_info& aType);
        std::uint32_t dispatch_function(type_info& aType, method_info& aMethod);
        void load(fragment& aCode, field_info const& aField);
        void store(fragment& aCode, field_info const& aField);
        fragment constant(field_info const& aField, std::string_view aSource);
        fragment zero(value_type const& aType);
        fragment dummy(type aType, fragment_kind aKind = fragment_kind::Value, std::string_view aSource = {});
        std::vector<type> parameter_types(method_info const& aMethod) const;
        function_record const* choose_function(std::string const& aName, std::vector<value_type> const& aArguments) const;
        std::int32_t score(std::vector<parameter_info> const& aParameters, std::vector<value_type> const& aArguments) const;
        fragment call_function(function_record const& aFunction, std::vector<fragment> aArguments, std::string_view aSource);
        // errors
    private:
        [[noreturn]] void error(std::string_view aSource, std::string const& aMessage) const;
    private:
        std::uint32_t iPass = 0u;
        bool iResolved = false;
        std::deque<type_info> iTypes;
        std::map<std::string, type_info*> iTypeNames;   ///< (by qualified name)
        std::map<std::string, alias_info> iAliases;     ///< (by qualified name)
        std::map<std::string, std::vector<function_record>> iFunctionRecords;
        std::vector<i_source_fragment*> iFragments;
        // (this pass)
        std::size_t iTypeOrdinal = 0u;
        std::vector<type_info*> iTypeStack;
        std::map<std::uint32_t, function_context> iContexts;   ///< (by function)
        std::map<std::string, std::size_t> iDefinitions;        ///< (the functions of each name defined by this pass)
        std::map<std::uint32_t, std::string> iFrameNames;       ///< (of functions whose names are distinguished)
        std::map<i_symbol_table_entry const*, value_type> iVariables; ///< (variables that the compiler defined)
        std::vector<annotation> iAnnotations;
        std::map<type_info*, std::uint32_t> iCopyFunctions;
        std::map<std::pair<type_info*, method_info*>, std::uint32_t> iDispatchFunctions;
        fragment iResult;
    };
}
