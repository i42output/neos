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

// The Lua compiler plugin's language compiler: it builds a syntax tree from the Lua constructs that the schema's concepts emit
// (see neos/language/lua.hpp), resolves its names (locals, upvalues and globals) and generates a WebAssembly function for each
// Lua function (and one for the chunk) when the chunk's construct (its root) is emitted.
//
// Values are NaN-boxed i64s (see neos/language/lua.hpp); the runtime plugin (lua::f) implements everything that is not compiled
// inline (tables, strings, metatables, calls of functions that are not the program's, the standard library ...).
//
// Every Lua function has the signature (callee, argument count, the first four arguments) -> its first result; a call's arguments
// that follow the first four are on the runtime's extra arguments stack (pushed by the caller, which pops them when the call
// returns). The number of a call's results is in the program's global %lua.nret (the function that returns sets it) and its
// results after the first are in the runtime's results (lua::result). An error makes a call return the value Exception (the
// error is pending in the runtime): the code that called it returns Exception too (having closed its to-be-closed variables).

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
#include <neos/language/language_compiler.hpp>
#include <neos/language/lua.hpp>
#include <neos/bytecode/opcodes.hpp>

namespace neos::compilers::lua
{
    using namespace neos::language;
    using language::lua::construct;
    using bytecode::opcode;
    namespace values = language::lua::values;
    namespace layout = language::lua::layout;

    // --- the syntax tree

    struct variable;
    struct function_info;
    struct label_info;

    struct node
    {
        construct kind;
        std::string_view source;
        std::vector<node*> children;
        node* parent = nullptr;
        variable* resolved = nullptr;       ///< (an identifier: the local variable it refers to (none: a global))
        variable* environment = nullptr;    ///< (a global's identifier in the scope of a local variable named _ENV: that variable)
        variable* declared = nullptr;       ///< (a name that declares a local variable (or a parameter): its variable)
        function_info* function = nullptr;  ///< (a function (or the chunk): its function)
        label_info* label = nullptr;        ///< (a label or a goto: its label)
        bool loopBody = false;              ///< (a statement list that is a loop's body: break leaves it)
        bool closing = false;               ///< (a generic for: it may have a closing value (a fourth value))

        bool is(construct aKind) const { return kind == aKind; }
        node* child(std::size_t aIndex) const { return aIndex < children.size() ? children[aIndex] : nullptr; }
        std::size_t size() const { return children.size(); }
        std::string text() const { return std::string{ source }; }
    };

    bool is_statement(construct aKind);
    bool is_expression(construct aKind);
    bool is_multiple(node const* aNode);    ///< (a call or a vararg expression: it can have any number of values)

    // (the parts of a construct whose children are a statement list (a block) and other things)
    struct block_view
    {
        std::vector<node*> statements;
    };

    // --- names

    enum class attribute
    {
        None,
        Const,
        Close
    };

    struct variable
    {
        std::string name;
        attribute attrib = attribute::None;
        function_info* function = nullptr;
        node* declaration = nullptr;
        bool captured = false;              ///< (an inner function uses it: its value is in a box (a runtime cell) that its local holds)
        bool parameter = false;
        bool integer = false;               ///< (its values are all integers (see compiler::infer_integers): its local holds an i64)
        bool floating = false;              ///< (its values are all floats (see compiler::infer_integers): its local holds an f64)
        bool assigned = false;              ///< (an assignment statement assigns it)
        function_info* constant = nullptr;  ///< (a local function that is never assigned another value: its function (calls of it are direct))
        std::uint32_t local = 0u;           ///< (its local (while its function's code is generated))
        std::uint32_t tbcDepth = 0u;        ///< (a to-be-closed variable: the number of its function's to-be-closed variables before it)
    };

    struct label_info
    {
        std::string name;
        node* statement = nullptr;          ///< (the label statement)
        node* owner = nullptr;              ///< (the construct whose statement list it is in)
        std::size_t position = 0u;          ///< (its position in the statement list)
        bool forward = false;               ///< (a goto before it jumps to it)
        bool backward = false;              ///< (a goto at or after it jumps to it)
        std::size_t open = 0u;              ///< (forward: the position in the statement list where the block that ends at it begins)
        std::size_t tbcCount = 0u;          ///< (the to-be-closed variables in scope where it is)
        std::optional<std::size_t> forwardControl;  ///< (while its statement list is generated: the controls that jumps to it branch to)
        std::optional<std::size_t> backwardControl;
    };

    struct function_info
    {
        node* declaration = nullptr;        ///< (the function's construct (the chunk's: the chunk))
        function_info* parent = nullptr;
        std::string name;                   ///< (its frame's name)
        bool chunk = false;
        bool vararg = false;
        bool method = false;                ///< (a method (function a:b()): its first parameter is self)
        bool hasClose = false;              ///< (it has to-be-closed variables)
        std::vector<variable*> parameters;
        struct upvalue
        {
            variable* target;
            bool parentLocal;               ///< (a local variable of the function that encloses it (else one of its upvalues))
            std::uint32_t parentIndex;      ///< (not a local: its index in the upvalues of the function that encloses it)
        };
        std::vector<upvalue> upvalues;
        std::map<variable*, std::uint32_t> upvalueIndex;
        std::uint32_t index = 0u;           ///< (its WebAssembly function)
        std::uint32_t number = 0u;          ///< (its number (the dispatcher's))
        std::uint32_t line = 1u;
        // (the variables that are integers if its integer parameters' arguments are (see compiler::infer_integers): its code has a 
        // version for such arguments)
        std::vector<variable*> speculative;
        std::vector<variable*> speculativeParameters;
    };

    // a compile error: its position in the fragment's source and its message (reported as a syntax error)
    struct syntax_error : std::runtime_error
    {
        syntax_error(std::string_view aSource, std::string const& aMessage) : std::runtime_error{ aMessage }, source{ aSource } {}
        std::string_view source;
    };

    class compiler;

    // --- code generation of one WebAssembly function

    class function_generator
    {
        friend class compiler;
    public:
        // (locals of the signature: callee, the argument count and the first four arguments)
        static constexpr std::uint32_t CalleeLocal = 0u;
        static constexpr std::uint32_t ArgcLocal = 1u;
        static constexpr std::uint32_t FirstArgumentLocal = 2u;
        static constexpr std::uint32_t RegisterArguments = 4u;
        static constexpr std::uint32_t ParameterCount = 6u;
    public:
        function_generator(compiler& aCompiler, function_info& aFunction);
    public:
        void generate();
    private:
        // control flow
        enum class control_kind
        {
            Block,              ///< (a block that is not a target of break or goto, e.g. an if's)
            Loop,
            Exit,               ///< (the function's: errors branch to its end)
            Break,              ///< (a loop's exit)
            Label               ///< (a goto's target)
        };
        struct control
        {
            control_kind kind;
            std::size_t tbcCount = 0u;      ///< (the to-be-closed variables in scope where it begins)
        };
        std::size_t depth_of(std::size_t aControl) const { return iControls.size() - 1u - aControl; }
        void begin(control_kind aKind, std::optional<type> aResult = {});
        void begin_if(std::optional<type> aResult = {});
        void else_();
        void end_control();
        std::size_t exit_control() const { return 0u; }
        std::size_t break_control() const;
        void branch(std::size_t aControl, bool aConditional = false);
        // code
        void op(opcode aOpcode);
        void u32(std::uint32_t aValue);
        void i32(std::int32_t aValue);
        void i64(std::int64_t aValue);
        void constant(std::uint64_t aValue) { i64(static_cast<std::int64_t>(aValue)); }
        void f64(double aValue);
        void get(std::uint32_t aLocal);
        void set(std::uint32_t aLocal);
        void tee(std::uint32_t aLocal);
        void drop();
        void nil() { constant(values::Nil); }
        std::uint32_t local(type aType = type::I64);
        std::uint32_t temporary(type aType = type::I64);
        void release(std::uint32_t aLocal);
        void call(std::uint32_t aFunction);
        void runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult);
        void check();               ///< (the i64 on the stack: Exception branches to the function's exit)
        void check_drop();          ///< (and dropped)
        void literal_string(std::string const& aText);  ///< (a string's value)
        void description(std::string const& aText);     ///< (a description of an expression for error messages (e.g. "global 'x'"): a string, or nil)
        void set_results(std::uint32_t aCount);         ///< (the number of results of the function's return)
        void line_of(node* aNode);                      ///< (a line marker: its line)
        // values (inline)
        void is_integer();          ///< (the i64 on the stack: whether it is an inline integer (an i32))
        void is_float();            ///< (whether it is a float)
        void unbox_integer();       ///< (an inline integer: its i64)
        void box_integer();         ///< (an i64: its value (inline if it fits, else boxed by the runtime))
        void truth();               ///< (the value on the stack: its truth (an i32))
        // statements
        void block(node* aOwner, std::vector<node*> const& aStatements, std::size_t aFirst = 0u, bool aLoopBody = false);
        void statement(node* aNode);
        void local_statement(node* aNode);
        void local_function(node* aNode);
        void function_statement(node* aNode);
        void assignment(node* aNode);
        void if_statement(node* aNode);
        void while_statement(node* aNode);
        void repeat_statement(node* aNode);
        void numeric_for(node* aNode);
        void generic_for(node* aNode);
        void return_statement(node* aNode);
        void break_statement(node* aNode);
        void goto_statement(node* aNode);
        void close_to(std::size_t aCount);   ///< (the to-be-closed variables in scope beyond the first aCount: closed)
        // variables
        void declare(variable* aVariable);   ///< (the value on the stack: the new variable's (consumed))
        void load(variable* aVariable);
        void store(variable* aVariable);     ///< (the value on the stack: consumed)
        void box_of(variable* aVariable);    ///< (a captured variable: its box)
        // expressions
        void expression(node* aNode);       ///< (its value (the first, if it has many))
        void multiple(node* aNode);         ///< (a call or vararg expression: its first value (and the others in the runtime's results))
        void condition(node* aNode);        ///< (its truth: an i32)
        void identifier(node* aNode);
        void literal(node* aNode);
        void number(node* aNode);
        void string(node* aNode);
        void function_expression(node* aNode, std::optional<std::string> aName = {});
        void closure(function_info& aFunction);
        void table_constructor(node* aNode);
        void index_expression(node* aNode);
        void call_expression(node* aNode);
        void method_call(node* aNode);
        void call_value(std::uint32_t aCallee, std::vector<std::uint32_t> const& aArguments, std::optional<std::uint32_t> aMultiple,
            std::string const& aDescription, function_info const* aKnown = nullptr);  ///< (aKnown: the callee's function, if it is known)   ///< (aMultiple: the last argument is a call's or vararg's values (its first, in the local; the others in the results))
        std::vector<std::uint32_t> arguments_of(std::vector<node*> const& aArguments, std::optional<std::uint32_t>& aMultiple);
        void binary(node* aNode);
        // (integer arithmetic of locals and integer numerals (e.g. (i * i) % 7): computed inline as 64 bit integers, boxed once (its 
        // intermediate results need not fit inline); otherwise (an operand is not an integer, or a divisor is not positive) as binary)
        bool fusable(node* aNode, bool aOperand = false) const;
        void fused_arithmetic(node* aNode);
        // (an i64: aSlow: a fused expression's (its variables' values are in aLeaves; a divisor that is not positive branches to aSlow); 
        // none: an integer expression's (see compiler::infer_integers))
        void raw_integer(node* aNode, std::map<variable*, std::uint32_t> const& aLeaves, std::optional<std::size_t> aSlow);
        void to_raw_integer();                  ///< (an integer value (inline or boxed by the runtime) as an i64)
        // the runtime's heap (the external memory: see neos::language::lua::layout): its cells read and written inline
        void mem_load(std::uint32_t aOffset);   ///< ([address (i64)] -> i64: the word at the address + aOffset)
        void mem_store(std::uint32_t aOffset);  ///< ([address (i64), value (i64)] -> ())
        void payload();                         ///< ([value] -> its cell's offset)
        // (a table's (its cell's offset in aTable) hash part's entry of the interned string aKey: its value in aResult (and its node's
        // address in aNode), else (absent, or nil) a branch to aMiss, or (not found in the first slots of its chain) to aUnknown)
        void probe_string(std::uint32_t aTable, std::uint32_t aKey, std::uint32_t aResult, std::optional<std::uint32_t> aNode, std::size_t aMiss,
            std::size_t aUnknown);
        // (a field (aKey: a short string literal's value) of a value: inline if it is a table that has it (or whose metatable's __index
        // table has it), else aSlow() (its value))
        void field_get(std::uint32_t aObject, std::uint32_t aKey, std::function<void()> const& aSlow);
        void field_set(std::uint32_t aObject, std::uint32_t aKey, std::uint32_t aValue, std::function<void()> const& aSlow);
        // (an element (aKey: a value, or an i64 if aRaw) of a table's array part: inline if it is there, else aSlow())
        void element_get(std::uint32_t aObject, std::uint32_t aKey, bool aRaw, std::function<void()> const& aSlow);
        void element_set(std::uint32_t aObject, std::uint32_t aKey, bool aRaw, std::uint32_t aValue, std::function<void()> const& aSlow);
        void upvalue_box(std::uint32_t aIndex);  ///< (the box of the running function's upvalue)
        bool short_literal(std::string const& aText) const;     ///< (a literal key that generated code looks up (an interned string))

        void raw_float(node* aNode);            ///< (a float expression's (see float_expression) value as an f64)
        bool integer_comparison(node* aNode);   ///< (a comparison of integer expressions: an i32 (else false: nothing))
        void to_float(std::uint32_t aValue);    ///< (an integer or float value as an f64)
        void arithmetic(construct aOperator, std::uint32_t aLeft, std::uint32_t aRight, node* aLeftNode, node* aRightNode);
        void comparison(construct aOperator, std::uint32_t aLeft, std::uint32_t aRight, bool aCondition);
        void concatenation(node* aNode);
        void logical(node* aNode);
        void unary(node* aNode);
        void vararg_value();
        std::string describe(node* aNode);  ///< (a description of an expression for error messages (e.g. "local 'x'"); empty: none)
        // (an expression list's values into locals (aCount of them: the last expression's extra values, or nil))
        std::vector<std::uint32_t> values_of(std::vector<node*> const& aExpressions, std::size_t aCount);
        // (an expression list's values as the runtime's argument stack (its base: an i32 local; the values are pushed))
        std::uint32_t push_values(std::vector<node*> const& aExpressions);
    private:
        compiler& iCompiler;
        function_info& iFunction;
        std::vector<std::uint8_t> iCode;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> iRelocations;
        std::vector<type> iLocals;
        std::vector<std::uint32_t> iFreeTemporaries;
        std::vector<control> iControls;
        std::vector<std::vector<label_info*>> iLabels;  ///< (the labels of the statement lists being generated)
        std::size_t iTbcCount = 0u;                     ///< (the to-be-closed variables in scope)
        std::uint32_t iScratch = 0u;                    ///< (an i64 local for checks)
        bool iFusing = true;                            ///< (false: generating a fused expression's general case)
        std::uint32_t iArgumentsBase = 0u;              ///< (an i32 local: the extra arguments' base)
        std::optional<std::uint32_t> iVarargs;          ///< (a vararg function: the local that holds its varargs)
        std::uint32_t iTbcMark = 0u;                    ///< (an i32 local: the runtime's to-be-closed stack's depth when it began)
        std::uint32_t iLastLine = 0u;
    };

    class compiler : public language_compiler
    {
        friend class function_generator;
    public:
        compiler();
        // i_language_compiler
    public:
        void begin_compilation() override;
        void set_options(neolib::i_string const& aOptions) override;
        bool compiles_at_run_time() const override;
        void begin_fragment(i_source_fragment& aFragment) override;
        void end_fragment() override;
        void emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource) override;
        bool frame_name(std::uint32_t aFunction, neolib::i_string& aName) override;
    private:
        // the syntax tree
        node* make_node(construct aKind, std::string_view aSource);
        void build(construct aKind, std::string_view aSource);
        void dump(node* aNode, int aDepth = 0) const;
        // analysis
        struct name_scope
        {
            std::vector<std::pair<std::string, variable*>> names;
            std::vector<label_info*> labels;
            function_info* function = nullptr;
        };
        void analyse(node* aChunk);
        function_info* new_function(node* aDeclaration, function_info* aParent);
        variable* new_variable(std::string const& aName, function_info* aFunction, node* aDeclaration, attribute aAttribute = attribute::None);
        void analyse_function(node* aDeclaration, function_info* aFunction, node* aParameters, std::vector<node*> const& aBody);
        void analyse_block(node* aOwner, std::vector<node*> const& aStatements, std::size_t aFirst = 0u, std::vector<variable*> const& aDeclare = {},
            std::function<void()> const& aEpilogue = {});
        void analyse_statement(node* aNode);
        void analyse_expression(node* aNode);
        void declare(variable* aVariable);
        variable* find(std::string_view aName) const;
        void use(variable* aVariable);      ///< (a variable that the function being analysed uses: an upvalue if it is not its own)
    public:
        static std::uint32_t upvalue_index(function_info& aFunction, variable* aVariable);    ///< (its index in the function's upvalues (added if need be))
        void resolve_gotos(node* aOwner, std::vector<node*> const& aStatements, std::size_t aFirst);
    public:
        // (the statement list of a construct: its statements (after its other parts))
        static std::vector<node*> statements_of(node* aNode, std::size_t aFirst);
        static std::size_t first_statement(node* aNode);    ///< (the position of its first statement (of its body))
        // code generation
        // (the local variables whose values are all integers: not captured, and each assigned only integer numerals, such variables and
        // arithmetic (+ - * // % and unary -) of them (a numeric for's variable: its start's and step's); and each function's variables 
        // that are such integers if its parameters (those that are only assigned such integers) are integers)
        void infer_integers(node* aChunk);
        void generate(node* aChunk);
        std::uint32_t literal_string(std::string const& aText);     ///< (the global (an i64) that holds the string's value)
        std::uint32_t global_index(std::string const& aName, type aType);
        std::uint32_t source_line_of(std::string_view aSource) const;
        std::string_view fragment_source() const { return iSource; }
        [[noreturn]] void error(std::string_view aSource, std::string const& aMessage) const;
        std::string chunk_name() const { return iChunkName; }
    private:
        std::string_view iSource;
        std::string iChunkName;
        std::deque<node> iNodes;
        std::vector<node*> iStack;
        std::deque<function_info> iFunctions;
        std::deque<variable> iVariables;
        std::deque<label_info> iLabelInfos;
        std::vector<name_scope> iScopes;
        std::vector<function_info*> iFunctionStack;
        std::map<std::string, std::uint32_t> iLiterals;     ///< (string literals -> their globals)
        std::vector<std::pair<std::string, std::uint32_t>> iLiteralOrder;
        std::unordered_map<std::string, std::uint32_t> iGlobalIndex;
        std::size_t iGlobalIndexed = 0u;
        std::uint32_t iDispatcher = 0u;
        std::uint32_t iResultsGlobal = 0u;                  ///< (%lua.nret: the number of results of the last call)
        std::uint32_t iProgramGlobal = 0u;                  ///< (%lua.pid: the program's identity (the runtime's, from init))
        std::map<std::uint32_t, std::string> iFrameNames;
        int iDepth = 0;                                     ///< (fragments being compiled)
        bool iLoad = false;                                 ///< (a chunk compiled by load (its function is the program's result))
        bool iCompilesAtRunTime = false;
    };
}
