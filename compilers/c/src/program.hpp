/*
  program.hpp

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

// C: the program: its translation units analysed (types, declarations, scopes and linkage, expressions, statements, constant
// expressions and initializers), then its code generated (a WebAssembly function for each function, the static data, and the
// entry point)

#pragma once

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "compiler.hpp"

namespace neos::compilers::c
{
    // the WebAssembly value of a C type's values (an aggregate's: its address)
    enum class vkind { None, I32, I64, F32, F64 };

    // the attributes (of attribute specifiers) that the compiler implements: noreturn, and GNU's aligned, packed, constructor and destructor
    struct attribute_info
    {
        bool noreturn = false;
        bool packed = false;
        std::uint64_t aligned = 0u;                 ///< (gnu::aligned: the alignment (0: none))
        std::uint64_t pack = 0u;                    ///< (neos::pack (from #pragma pack): the members' alignment limit (0: none))
        std::optional<std::int64_t> constructor;    ///< (gnu::constructor: its priority (65535: none given))
        std::optional<std::int64_t> destructor;
        node* cleanup = nullptr;                    ///< (gnu::cleanup: its argument (a function's name))
        void merge(attribute_info const& aOther);
    };

    // the declaration specifiers of a declaration
    struct declaration_specifiers
    {
        qtype type;
        storage_class storage = storage_class::None;
        bool threadLocal = false;
        bool isInline = false;
        bool noreturn = false;
        bool isConstexpr = false;
        bool autoType = false;              ///< (auto with no type specifier: the type is inferred from the initializer)
        bool typeSpecified = false;
        std::uint64_t alignment = 0u;       ///< (alignas's)
        attribute_info attributes;          ///< (of the attribute specifiers among the specifiers, and before them)
        node* where = nullptr;
    };

    // a declarator's result: the name it declares (if any) and the type
    struct declarator_result
    {
        node* name = nullptr;
        qtype type;
        node* function = nullptr;           ///< (the function suffix that made the outermost function type: its parameters' declarations)
    };

    // an initializer's parts (see program::walk_initializer)
    struct initializer_visitor
    {
        std::function<void(qtype, node*, std::uint64_t)> scalar;                ///< (a scalar from an expression, at an offset)
        std::function<void(member_info const&, node*, std::uint64_t)> bits;     ///< (a bit-field (the offset: its storage unit's))
        std::function<void(qtype, node*, std::uint64_t)> characters;            ///< (an array of characters from a string literal)
        std::function<void(qtype, node*, std::uint64_t)> record;                ///< (a structure or union from an expression of its type)
    };

    // the builtin functions (__builtin_...) that the headers use (their calls are generated inline)
    enum class builtin_kind : std::uint32_t
    {
        None,
        VaStart,            ///< (__builtin_va_start(): the address of the function's variable arguments)
        VaNext,             ///< (__builtin_va_next(char** ap, size_t size): the next argument's address (*ap advanced past it))
        Setjmp,             ///< (__builtin_setjmp(long long* env))
        Longjmp,            ///< (__builtin_longjmp(long long* env, int value))
        Unreachable,
        Trap,
        Exit,               ///< (__builtin_neos_exit(int status): the program stops)
        Clz, Ctz, Popcount, Clzll, Ctzll, Popcountll,
        Bswap16, Bswap32, Bswap64,
        Expect,
        Alloca,
        CkdAdd, CkdSub, CkdMul,
        ConstantP,
        Sqrt, Sqrtf, Fabs, Fabsf, Floor, Ceil, Trunc, Nearbyint, Copysign, Fmin, Fmax,
        AtomicExchange, AtomicCompareExchange, AtomicFetchAdd, AtomicFetchSub, AtomicFetchOr, AtomicFetchXor, AtomicFetchAnd,
        AtomicLoad, AtomicStore, AtomicFence
    };

    class function_generator;

    class program
    {
        friend class function_generator;
    public:
        program(compiler& aCompiler, std::vector<translation_unit*> const& aUnits);
        ~program();
    public:
        void generate();
        compiler& comp() { return iCompiler; }
        i_code_generator& gen() { return iCompiler.gen(); }
        [[noreturn]] void error(node const* aWhere, std::string const& aMessage) const;
        void warning(node const* aWhere, std::string const& aMessage) const;
        // --- types (types.cpp)
    public:
        c_type const* basic(type_kind aKind);
        c_type const* pointer_to(qtype aType);
        c_type const* array_of(qtype aElement, std::optional<std::uint64_t> aLength, bool aVla = false, node* aSize = nullptr);
        c_type const* function_of(qtype aResult, std::vector<parameter_type> const& aParameters, bool aVariadic, bool aPrototype = true);
        qtype qualified(qtype aType, std::uint8_t aQualifiers);     ///< (qualifiers added (an array's: to its element type))
        c_type const* bit_int(std::uint32_t aWidth, bool aSigned);
        c_type const* record_type(record_info* aRecord);
        c_type const* enum_type(enum_info* aEnum);
        qtype size_type() { return qtype{ basic(type_kind::ULong) }; }
        qtype ptrdiff_type() { return qtype{ basic(type_kind::Long) }; }
        qtype int_type() { return qtype{ basic(type_kind::Int) }; }
        std::uint64_t size_of(qtype aType, node const* aWhere = nullptr) const;
        std::uint64_t align_of(qtype aType) const;
        bool compatible(qtype aA, qtype aB, bool aIgnoreQualifiers = false) const;
        qtype composite(qtype aA, qtype aB);
        qtype promote(qtype aType);
        qtype usual_arithmetic(qtype aA, qtype aB);
        int rank(c_type const* aType) const;
        std::uint32_t width_of(c_type const* aType) const;      ///< (an integer type's width in bits)
        c_type const* integer_type(c_type const* aType) const;  ///< (an enumerated type's underlying type; else itself)
        c_type const* unsigned_of(c_type const* aType);
        std::string type_name(qtype aType) const;
        void layout(record_info& aRecord, node const* aWhere);
        vkind kind_of(qtype aType) const;
        bool null_pointer_constant(node const* aExpression) const;
        // --- scopes and declarations (decls.cpp)
    public:
        void analyse_unit(translation_unit& aUnit);
        void external_declaration(node* aDeclaration);
        void declaration(node* aDeclaration, bool aFileScope);
        void function_definition(node* aDefinition);
        declaration_specifiers specifiers(std::vector<node*> const& aNodes, node* aWhere, bool aAllowStorage = true);
        declarator_result declarator(node* aDeclarator, qtype aType, bool aParameter = false);
        qtype type_name_type(node* aTypeName);
        void old_style_parameters(declarator_result& aResult, std::vector<node*> const& aDeclarations, std::vector<entity*>& aParameters);
        std::vector<parameter_type> parameters_of(node* aFunctionSuffix, bool& aVariadic, bool& aPrototype, std::vector<entity*>* aEntities = nullptr);
        c_type const* struct_specifier(node* aSpecifier, bool aDeclarationOnly, attribute_info const* aAttributes = nullptr);
        void read_attributes(node* aAttributes, attribute_info& aResult);
        attribute_info declarator_attributes(node* aDeclarator);
        void startup_function(entity* aFunction, attribute_info const& aAttributes, node* aWhere);
        void cleanup_function(entity* aObject, node* aFunction, node* aWhere);
        c_type const* enum_specifier(node* aSpecifier, bool aDeclarationOnly);
        entity* declare(node* aName, qtype aType, declaration_specifiers const& aSpecifiers, bool aFileScope, bool aHasInitializer, node* aDecl);
        entity* lookup(std::string const& aName) const;
        c_type const* lookup_tag(std::string const& aName, bool aCurrentOnly) const;
        void push_scope();
        void pop_scope();
        bool file_scope() const { return iScopes.size() == 1u; }
        void initializer(entity* aObject, node* aInitializer);
        qtype initializer_type(qtype aType, node* aInitializer);     ///< (an array of unknown size: its size from the initializer)
        void check_initializer(qtype aType, node* aInitializer, bool aStatic);
        void static_assertion(node* aAssert);
        // --- expressions (exprs.cpp)
    public:
        void expression(node* aExpression);
        void condition(node* aExpression);              ///< (a controlling expression: a scalar)
        qtype value_type(node const* aExpression) const;   ///< (its type after lvalue conversion, array to pointer and function to pointer conversions)
        void check_assignable(qtype aTarget, node* aValue, node const* aWhere, char const* aContext);
        std::optional<constant> evaluate(node* aExpression, bool aIntegerOnly);
        std::int64_t integer_constant(node* aExpression, char const* aWhat);
        bool is_lvalue(node const* aExpression) const { return aExpression->lvalue; }
        void mark_address_taken(node* aExpression);
        void literal(node* aLiteral);
        void string_literal(node* aStrings);
        entity* builtin(std::string const& aName);
        // --- statements (stmts.cpp)
    public:
        void statement(node* aStatement);
        void compound(node* aCompound, bool aNewScope = true);
        void block_item(node* aItem);
        // --- static data and functions (codegen.cpp)
    public:
        std::uint64_t static_data(std::uint64_t aSize, std::uint64_t aAlignment);
        std::uint64_t string_literal_address(node* aStrings);
        std::uint64_t descriptor_of(entity* aFunction);
        void static_initializer(entity* aObject);
        void store_constant(std::vector<std::uint8_t>& aImage, std::uint64_t aOffset, qtype aType, constant const& aValue, node const* aWhere);
        void initialize_static(std::vector<std::uint8_t>& aImage, std::uint64_t aOffset, qtype aType, node* aInitializer);
        void walk_initializer(qtype aType, node* aInitializer, std::uint64_t aOffset, initializer_visitor const& aVisitor);
        std::uint32_t runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult);
        std::uint32_t global(std::string const& aName, type aType);
        void define_function(entity* aFunction);
        std::uint32_t function_index(entity* aFunction);
        bool exported(entity* aFunction);               ///< (a function that the code of a linked program's other translation units can call 
                                                        ///< by its name)
        void linked_object(entity* aObject);            ///< (a function that gives an object's address to the code of a linked program's 
                                                        ///< other translation units)
        std::uint32_t unprototyped_call(entity* aFunction, std::vector<type> const& aArguments); ///< (a function that calls a function 
                                                                                                ///< without a prototype with arguments)
        node* statement_expression_value(node* aExpression) const;
        static builtin_kind builtin_kind_of(std::string const& aName);  ///< (a statement expression's last expression statement's expression)
        std::uint32_t descriptor_target(entity* aFunction);   ///< (the function that a function's descriptor holds)
        std::uint32_t adapter(std::string const& aName, std::vector<type> const& aParameters, type aResult, std::uint32_t aTarget,
            std::vector<type> const& aTargetParameters, type aTargetResult);   ///< (a function that calls another, its arguments and result converted)
        std::vector<type> wasm_parameters(c_type const* aFunctionType) const;
        type wasm_result(c_type const* aFunctionType) const;
        type wasm(vkind aKind) const;
        bool returns_record(c_type const* aFunctionType) const;
        std::uint32_t sp_global() const { return iSpGlobal; }
        std::uint32_t limit_global() const { return iLimitGlobal; }
        std::uint32_t tls_global() const { return iTlsGlobal; }
        std::uint64_t tls_template() const { return iTlsTemplate; }
        std::uint32_t jump_global() const { return iJumpGlobal; }
        std::uint32_t jump_value_global() const { return iJumpValueGlobal; }
        bool uses_longjmp() const { return iUsesLongjmp; }
        entity* function_entity(std::string const& aName) const;
        void note_function_type(std::uint32_t aFunction, std::vector<type> const& aParameters, type aResult);
        void note_indirect_type(std::uint32_t aSignature, std::vector<type> const& aParameters, type aResult);
        void note_global_type(std::uint32_t aGlobal, type aType);
        std::map<std::uint32_t, std::string> iFunctionTypes;     ///< (for validation (NEOS_C_VALIDATE): functions' signatures)
        std::map<std::uint32_t, std::string> iIndirectTypes;
        std::map<std::uint32_t, char> iGlobalTypes;
    private:
        void assign_static_addresses();
        void generate_code();
        void reachable_functions();
    public:
        compiler& iCompiler;
        std::vector<translation_unit*> iUnits;
        translation_unit* iUnit = nullptr;              ///< (the translation unit being analysed)
        // (types)
        std::deque<c_type> iTypes;
        std::map<type_kind, c_type const*> iBasic;
        std::map<std::pair<c_type const*, std::uint8_t>, c_type const*> iPointers;
        std::map<std::pair<std::uint32_t, bool>, c_type const*> iBitInts;
        std::map<record_info*, c_type const*> iRecordTypes;
        std::map<enum_info*, c_type const*> iEnumTypes;
        std::deque<record_info> iRecords;
        std::deque<enum_info> iEnums;
        // (scopes: the ordinary identifiers and the tags)
        struct scope
        {
            std::unordered_map<std::string, entity*> names;
            std::unordered_map<std::string, c_type const*> tags;
        };
        std::vector<scope> iScopes;
        std::deque<entity> iEntities;
        std::deque<function_info> iFunctionInfos;
        std::unordered_map<std::string, entity*> iExternals;                  ///< (entities with external linkage, by name)
        std::map<std::pair<translation_unit*, std::string>, entity*> iInternals; ///< (entities with internal linkage)
        std::map<std::string, std::uint32_t> iUnprototypedCalls;
        std::map<std::pair<translation_unit*, std::string>, entity*> iUnitDeclarations;  ///< (a unit's declarations of a function with external 
                                                                                        ///< linkage that conflict with another unit's)
        std::vector<entity*> iObjects;                  ///< (objects of static storage duration, canonical)
        std::vector<entity*> iFunctions;                ///< (functions, canonical)
        std::vector<std::pair<std::int64_t, entity*>> iConstructors;   ///< (gnu::constructor functions: their priorities and functions)
        std::vector<std::pair<std::int64_t, entity*>> iDestructors;
        std::deque<node> iSyntheticNodes;
        // (the function being analysed)
        entity* iFunction = nullptr;
        std::vector<node*> iBreakables;                 ///< (loops and switches)
        std::vector<node*> iSwitches;
        std::map<std::string, node*> iLabels;
        std::vector<node*> iGotos;
        std::vector<node*> iLabelAddresses;             ///< (&&label (GNU))
        int iLoopDepth = 0;
        // (code generation)
        std::vector<std::uint8_t> iStatic;              ///< (the static data image (from StaticBase))
        std::map<std::string, std::uint64_t> iStringLiterals;
        std::vector<std::pair<std::uint64_t, entity*>> iDescriptors;    ///< (functions' descriptors: their addresses and functions)
        std::map<std::string, std::uint32_t> iGlobals;
        std::uint32_t iSpGlobal = 0u;
        std::uint32_t iLimitGlobal = 0u;
        std::uint32_t iJumpGlobal = 0u;
        std::uint32_t iJumpValueGlobal = 0u;
        std::uint32_t iTlsGlobal = 0u;                  ///< (%c.tls: the thread's block of thread-local objects' address)
        std::uint64_t iTlsTemplate = 0u;                ///< (the block's template (its objects' initial values): 0 if none)
        std::uint64_t iTlsSize = 0u;
        std::uint64_t iTlsAlignment = 16u;
        std::uint64_t iTlsMain = 0u;                    ///< (the program's own thread's block)
        bool iUsesLongjmp = false;
        std::uint32_t iFunctionCount = 0u;
        std::vector<entity*> iToGenerate;
        std::set<std::string> iRuntimeWarned;
    };
}
