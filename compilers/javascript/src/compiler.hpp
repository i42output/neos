// The JavaScript compiler plugin's language compiler: it builds a syntax tree from the JavaScript constructs that the
// schema's concepts emit (see neos/language/javascript.hpp), analyses its scopes and generates a WebAssembly function for
// each JavaScript function (and one for the script's own code) when the script's construct (its root) is emitted.
//
// Values are NaN-boxed i64s (see the JavaScript runtime plugin's js.hpp); the runtime plugin (javascript::f) implements
// everything that is not compiled inline (objects, properties, conversions, calls of functions that are not the program's,
// ...).

#pragma once

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <neos/language/language_compiler.hpp>
#include <neos/language/javascript.hpp>
#include <neos/bytecode/opcodes.hpp>

namespace neos::compilers::javascript
{
    using namespace neos::language;
    using language::javascript::construct;
    using bytecode::opcode;

    // --- values (as the runtime boxes them)

    namespace values
    {
        inline constexpr std::uint64_t TagSpecial = 0xFFF9000000000000ull;
        inline constexpr std::uint64_t TagString = 0xFFFA000000000000ull;
        inline constexpr std::uint64_t TagObject = 0xFFFD000000000000ull;
        inline constexpr std::uint64_t Undefined = TagSpecial | 0u;
        inline constexpr std::uint64_t Null = TagSpecial | 1u;
        inline constexpr std::uint64_t False = TagSpecial | 2u;
        inline constexpr std::uint64_t True = TagSpecial | 3u;
        inline constexpr std::uint64_t Empty = TagSpecial | 4u;
        inline constexpr std::uint64_t Exception = TagSpecial | 5u;
        inline constexpr std::uint64_t Return = TagSpecial | 6u;   ///< (a generator resumed to return)
    }

    // --- the syntax tree

    struct scope;
    struct function_info;

    struct node
    {
        construct kind;
        std::string_view source;
        std::vector<node*> children;
        node* parent = nullptr;
        scope* ownScope = nullptr;          ///< (a node that creates a scope: its scope)
        function_info* function = nullptr;  ///< (a function node: its function)
        std::int32_t site = -1;             ///< (a tagged template: its site)
        bool annexB = false;                ///< (a block's function declaration that also assigns its function's var binding (Annex B.3.3))

        bool is(construct aKind) const { return kind == aKind; }
        node* child(std::size_t aIndex) const { return aIndex < children.size() ? children[aIndex] : nullptr; }
        std::size_t size() const { return children.size(); }
        std::string text() const { return std::string{ source }; }
    };

    // --- scopes

    enum class binding_kind
    {
        Var,
        Let,
        Const,
        Class,
        Function,           ///< (a function declaration)
        Parameter,
        Catch,
        Callee,             ///< (a named function expression's own name (immutable))
        Import,             ///< (a module's imported binding (immutable): its slot holds the binding it refers to (see the runtime's import_get))
        Internal            ///< (this, new.target, the home object and arguments of a function whose arrow functions use them)
    };

    struct binding
    {
        std::string name;
        binding_kind kind = binding_kind::Var;
        scope* owner = nullptr;
        bool captured = false;              ///< (an inner function uses it: it is in an environment)
        bool tdz = false;                   ///< (it can be used before it is initialized)
        std::uint32_t slot = 0u;            ///< (captured: its slot in its scope's environment; otherwise its local)
        node* declaration = nullptr;
        bool lexical() const { return kind == binding_kind::Let || kind == binding_kind::Const || kind == binding_kind::Class; }
    };

    enum class scope_kind
    {
        Function,
        Block,
        Catch,
        Class,
        Script,
        With
    };

    struct scope
    {
        scope_kind kind = scope_kind::Block;
        scope* parent = nullptr;
        function_info* function = nullptr;
        node* owner = nullptr;
        std::vector<std::unique_ptr<binding>> bindings;
        std::map<std::string, binding*, std::less<>> names;
        bool hasEnvironment = false;        ///< (it has captured bindings: an environment is created when it is entered)
        std::uint32_t environmentSize = 0u;
        std::optional<std::uint32_t> environmentLocal;  ///< (while its code is generated: the local that holds its environment)
        bool dynamic = false;               ///< (its bindings can be found by name (direct eval, with): all are captured)

        binding* find(std::string_view aName) const
        {
            auto const existing = names.find(aName);
            return existing != names.end() ? existing->second : nullptr;
        }
    };

    struct function_info
    {
        node* declaration = nullptr;        ///< (null: the script)
        function_info* parent = nullptr;
        std::string name;
        bool script = false;
        bool strict = false;
        bool arrow = false;
        bool method = false;                ///< (a method, getter, setter or a class's constructor or field initializer: it has a home object)
        bool getter = false;
        bool setter = false;
        bool generator = false;
        bool async = false;
        bool classConstructor = false;
        bool derived = false;
        bool fieldInitializer = false;      ///< (a class's fields' initializer)
        bool staticBlock = false;
        bool expression = false;            ///< (a function expression)
        bool simpleParameters = true;
        bool usesArguments = false;
        bool usesThis = false;              ///< (it or its arrow functions use this)
        bool usesNewTarget = false;
        bool usesSuper = false;
        bool usesSuperCall = false;
        bool directEval = false;
        bool needsOuterEnvironment = false; ///< (it uses a binding of a function that encloses it)
        scope* top = nullptr;               ///< (its parameters and var declarations)
        scope* body = nullptr;              ///< (its lexical declarations, if its parameters are not simple; otherwise top)
        std::vector<function_info*> functions;
        std::uint32_t index = 0u;           ///< (its WebAssembly function)
        std::uint32_t bodyIndex = 0u;       ///< (a generator or async function: the function its body runs as)
        std::uint32_t length = 0u;          ///< (the number of its parameters before the first with a default or a rest)
        binding* argumentsBinding = nullptr;
        binding* thisBinding = nullptr;     ///< (captured this (arrow functions use it))
        binding* newTargetBinding = nullptr;
        binding* homeBinding = nullptr;
        binding* calleeBinding = nullptr;   ///< (the function object, for its arrow functions' super and arguments)
    };

    // a compile error: its position in the fragment's source and its message (reported as a SyntaxError)
    struct syntax_error : std::runtime_error
    {
        syntax_error(std::string_view aSource, std::string const& aMessage) : std::runtime_error{ aMessage }, source{ aSource } {}
        std::string_view source;
    };

    bool is_class_constructor_method(node const* aNode);   ///< (codegen.cpp)

    class compiler;

    enum class numeric_rep : std::uint8_t
    {
        None,           ///< (a value)
        F64,            ///< (a number: its double)
        I64             ///< (an integer (a counter): an i64)
    };

    // --- code generation of one WebAssembly function

    class function_generator
    {
        friend class compiler;
    public:
        // (locals of the signature: callee, this, new.target, the argument count and the first four arguments)
        static constexpr std::uint32_t CalleeLocal = 0u;
        static constexpr std::uint32_t ThisLocal = 1u;
        static constexpr std::uint32_t NewTargetLocal = 2u;
        static constexpr std::uint32_t ArgcLocal = 3u;
        static constexpr std::uint32_t FirstArgumentLocal = 4u;
        static constexpr std::uint32_t RegisterArguments = 4u;
        static constexpr std::uint32_t ParameterCount = 8u;
    public:
        function_generator(compiler& aCompiler, function_info& aFunction, bool aBody = false);
    public:
        void generate();
    private:
        // control flow
        enum class control_kind
        {
            Block,              ///< (a plain block: no label)
            Loop,
            Handler,            ///< (exceptions branch to its end)
            Exit,               ///< (the function's)
            Breakable,          ///< (a labelled statement, a loop's or switch's exit)
            Continuable,        ///< (a loop's continue target)
            Finally,            ///< (abrupt completions branch to its end with the completion stored)
            Cleanup             ///< (a for-of loop's iterator: closed by abrupt completions that leave it)
        };
        struct control
        {
            control_kind kind;
            std::vector<std::string> labels;
            bool loop = false;                  ///< (Breakable/Continuable: of an iteration statement)
            bool switch_ = false;
            std::uint32_t completionLocal = 0u; ///< (Finally: the kind of completion)
            std::uint32_t valueLocal = 0u;      ///< (Finally: its value)
            std::vector<std::function<void()>> jumps;   ///< (Finally: the jumps (by id - 3) to complete after the finally block)
            std::uint32_t iteratorLocal = 0u;   ///< (Cleanup: the iterator record)
            bool asyncIterator = false;
            std::optional<std::uint32_t> environment;   ///< (the environment local in effect where it begins)
        };
        std::size_t depth_of(std::size_t aControl) const { return iControls.size() - 1u - aControl; }
        void begin(control_kind aKind, std::optional<type> aResult = {}, std::vector<std::string> aLabels = {});
        void end_control();
        std::size_t handler() const;
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
        std::uint32_t local(type aType = type::I64);
        std::uint32_t temporary(type aType = type::I64);
        void release(std::uint32_t aLocal);
        void call(std::uint32_t aFunction);
        void runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult);
        void check();               ///< (the i64 on the stack: an exception branches to the innermost handler)
        void check_void();          ///< (a call that returned Exception or Undefined (dropped))
        void function_index(function_info& aFunction, bool aBody = false);
        void string_value(std::string_view aText);  ///< (a string literal (its JavaScript value, UTF-16 as WTF-8))
        void key(std::string_view aName);           ///< (an identifier name as a property key)
        void undefined() { constant(values::Undefined); }
        // environments
        std::optional<std::uint32_t> current_environment() const;
        void environment_value();                   ///< (the innermost environment (or undefined))
        void enter_scope(scope* aScope);
        void leave_scope(scope* aScope);
        void initialize_scope(scope* aScope, node* aStatements = nullptr);
        // bindings
        struct reference
        {
            enum kind_t { Local, Environment, Global, Dynamic } kind = Global;   ///< (Dynamic: found by name at run time (with, eval))
            binding* target = nullptr;
            std::uint32_t local = 0u;
            std::uint32_t hops = 0u;
            std::uint32_t environment = 0u;
            std::string name;
            std::vector<std::uint32_t> withObjects;    ///< (with statements' objects whose bindings are looked up first)
            bool globalLexical = false;                 ///< (a global: a binding of the global lexical environment (let, const, class))
        };
        reference resolve(std::string const& aName, node* aSite);
        void load(reference const& aReference, bool aTypeof = false, node* aSite = nullptr);
        void store(reference const& aReference, bool aInitialize = false, node* aSite = nullptr);   ///< (the value on the stack: kept)
        void load_binding(binding* aBinding);
        void initialize_binding(binding* aBinding);  ///< (the value on the stack: consumed)
        // statements
        void statement(node* aNode);
        void statements(node* aNode, std::size_t aFirst = 0u);
        void declarations(node* aNode, bool aInitialize);
        void hoist_functions(node* aStatements, scope* aScope);
        void variable_declaration(node* aNode);
        void if_statement(node* aNode);
        void loop_statement(node* aNode, std::vector<std::string> const& aLabels);
        void for_statement(node* aNode, std::vector<std::string> const& aLabels);
        void for_in_of_statement(node* aNode, std::vector<std::string> const& aLabels);
        void switch_statement(node* aNode, std::vector<std::string> const& aLabels);
        void try_statement(node* aNode);
        void jump(node* aNode, bool aContinue);
        void return_statement(node* aNode);
        void complete_return();      ///< (the value on the stack: the function returns it (through finally blocks))
        void with_statement(node* aNode);
        // expressions
        void expression(node* aNode);
        void expression_list(node* aNode);
        void condition(node* aNode);    ///< (an i32: its truth)
        void literal(node* aNode);
        void identifier(node* aNode, bool aTypeof = false);
        void member(node* aNode);
        void call_expression(node* aNode);
        void arguments_values(std::vector<node*> const& aArguments, std::optional<std::uint32_t>& aSpread);
        void call_value(std::uint32_t aCallee, std::uint32_t aThis, std::vector<node*> const& aArguments, bool aTail = false);
        bool tail_call(node* aCall) const;
        void tail_positions(node* aExpression);
        void check_call();
        void new_expression(node* aNode);
        void assignment(node* aNode);
        void update(node* aNode);
        void unary(node* aNode);
        void binary(node* aNode);
        void logical(node* aNode);
        void conditional(node* aNode);
        void array_literal(node* aNode);
        void object_literal(node* aNode);
        void template_literal(node* aNode);
        void tagged_template(node* aNode);
        void function_expression(node* aNode);
        void class_expression(node* aNode, std::optional<std::string> aName = {});
        void optional_chain(node* aNode);
        void delete_expression(node* aNode);
        void yield_expression(node* aNode);
        void await_value();
        void resumption();
        // numeric locals: local bindings whose values are always numbers are f64 (or i64) locals
        void infer_numeric();
        numeric_rep rep_of(binding const* aBinding) const;
        numeric_rep num_type(node* aNode);
        void numeric(node* aNode);                  ///< (its value as an f64: num_type(aNode) is not None)
        void box(numeric_rep aRep);                 ///< (an f64 or i64: as a value)
        void unbox(numeric_rep aRep);               ///< (a value (a number): as an f64 or i64)
        bool numeric_binary(node* aNode);           ///< (an operation of numbers: emitted (else false))
        bool numeric_condition(node* aNode);        ///< (an i32)
        bool numeric_update(reference const& aReference, bool aIncrement, bool aPostfix);
        bool numeric_assignment(reference const& aReference, node* aValue, std::string const& aOperator);
        void f64_remainder();
        bool has_using(node* aStatements) const;
        void export_statement(node* aNode);
        void for_loop(node* init, node* test, node* update, node* body, scope* s, bool perIteration, std::vector<std::string> const& aLabels);
        std::string names_descriptor(scope const* aScope) const;
        void disposal_scope(std::function<void()> const& aBody);
        void integer(node* aNode);                  ///< (an integer (num_type I64) as an i64)
        void counter_guard(std::uint32_t aSlot, std::uint32_t aOld, std::int64_t aStep);
        void super_property(node* aNode);
        void super_call(node* aNode);
        void property_key(node* aNode);     ///< (a property name node: its key)
        void named_evaluation(node* aNode, std::string_view aName);  ///< (an anonymous function or class gets the name)
        bool is_anonymous_function(node* aNode) const;
        // patterns
        void bind_pattern(node* aTarget, bool aInitialize, binding_kind aKind);  ///< (the value on the stack: consumed)
        void assign_target(node* aTarget);  ///< (the value on the stack: consumed; a target that is an expression or a pattern)
        void destructure_object(node* aPattern, bool aBinding, bool aInitialize);
        void destructure_array(node* aPattern, bool aBinding, bool aInitialize);
        void bind_element(node* aTarget, bool aBinding, bool aInitialize);
        // functions
        void prologue();
        void instantiate_parameters();
        void closure(function_info& aFunction);
        void set_home_object(std::uint32_t aFunction, std::uint32_t aHome);
        bool this_lexical() const;
        bool derived_eval_code() const;
        void this_value(node* aSite);
        void new_target_value();
        void home_object_value();
        void callee_value();
    private:
        compiler& iCompiler;
        function_info& iFunction;
        bool iBody;                     ///< (a generator's or async function's body (run as a coroutine))
        std::vector<std::uint8_t> iCode;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> iRelocations;
        std::vector<type> iLocals;
        std::unordered_map<binding const*, numeric_rep> iNumeric;
        bool iDiscarded = false;
        std::set<node*> iTailCalls;                ///< (the calls in tail position of return statements)
        std::vector<std::uint32_t> iDisposals;     ///< (the dispose capabilities of the blocks with using declarations being generated)
        bool iDiscardedNow = false;    ///< (the expression being generated is an expression statement's: its value is not used)
        std::unordered_map<node const*, binding*> iNumericNames;   ///< (the references (identifiers) to bindings that may be numeric)
        std::vector<std::uint32_t> iFreeTemporaries;
        std::vector<control> iControls;
        std::vector<scope*> iScopes;    ///< (the scopes whose code is being generated, innermost last)
        std::uint32_t iScratch = 0u;    ///< (an i64 local for checks)
        std::uint32_t iArgumentsBase = 0u;  ///< (an i32 local: the index of the first extra argument (if it has more than four))
        std::uint32_t iThisLocal = ThisLocal;
        std::optional<std::uint32_t> iCompletion;   ///< (eval code: its completion value)
        std::vector<std::uint32_t> iWithObjects;
        std::optional<std::uint32_t> iFunctionEnvironment;  ///< (the function's closure's environment)
    };

    class compiler : public language_compiler
    {
        friend class function_generator;
    public:
        compiler();
        // i_language_compiler
    public:
        void begin_compilation() override;
        void begin_pass() override;
        void end_pass() override;
        void begin_fragment(i_source_fragment& aFragment) override;
        void end_fragment() override;
        void emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource) override;
        bool frame_name(std::uint32_t aFunction, neolib::i_string& aName) override;
        void set_options(neolib::i_string const& aOptions) override;
        void diagnose(i_source_fragment const& aFragment, std::uint32_t& aLine, std::uint32_t& aColumn, std::uint32_t& aEndLine, std::uint32_t& aEndColumn,
            neolib::i_string& aType, neolib::i_string& aMessage, neolib::i_string& aReport) override;
    private:
        // the syntax tree
        node* make_node(construct aKind, std::string_view aSource);
        void await_operators(node* aNode, bool aAwait);
        void yield_identifiers(node* aNode, bool aGenerator);
        void array_commas(node* aNode);
        void eval_context_errors(node* aNode);
        void build(construct aKind, std::string_view aSource);
        void dump(node* aNode, int aDepth = 0) const;
        // analysis
        void analyse(node* aScript);
        void check_early_errors(node* aScript);     ///< (early_errors.cpp: the static semantics' early errors (a syntax_error))
        void analyse_function(function_info& aFunction);
        function_info* new_function(node* aDeclaration, function_info* aParent);
        scope* new_scope(scope_kind aKind, scope* aParent, node* aOwner, function_info* aFunction);
        binding* declare(scope* aScope, std::string const& aName, binding_kind aKind, node* aDeclaration);
        void declare_pattern(scope* aScope, node* aTarget, binding_kind aKind);
        void collect(node* aNode, scope* aScope, function_info* aFunction);
        void hoist_var(node* aNode, scope* aVarScope, function_info* aFunction);
        void resolve_uses(node* aNode, scope* aScope, function_info* aFunction);
        void use(std::string const& aName, scope* aScope, function_info* aFunction, node* aSite);
        bool directive_strict(node* aStatements) const;
        // code generation
        void generate(node* aScript);
        std::uint32_t literal_string(std::string const& aText);     ///< (the global (an i64) that holds the string's value)
        std::uint32_t global_index(std::string const& aName, type aType);
        std::uint32_t source_line_of(std::string_view aSource) const;
    public:
        std::string_view fragment_source() const { return iSource; }
        [[noreturn]] void error(std::string_view aSource, std::string const& aMessage) const;
    private:
        std::string_view iSource;
        std::deque<node> iNodes;
        std::vector<node*> iStack;
        std::deque<function_info> iFunctions;
        std::deque<scope> iScopes;
        std::vector<std::tuple<node*, scope*, function_info*>> iAnnexBFunctions;
        std::map<std::string, std::uint32_t> iLiterals;     ///< (string literals -> their globals)
        std::vector<std::pair<std::string, std::uint32_t>> iLiteralOrder;
        std::uint32_t iDispatcher = 0u;
        std::uint32_t iTemplateSites = 0u;
        std::uint32_t iErrorGlobal = 0u;
        std::map<std::uint32_t, std::string> iFrameNames;
        bool iModule = false;
        int iDepth = 0;                 ///< (fragments being compiled)
    public:
        // (the kind of code being compiled (see set_options))
        enum class code_kind { Script, DirectEval, IndirectEval, EvaluateScript, Function, Module };
        enum : std::uint32_t
        {
            EvalStrict = 0x01,          ///< (a direct eval in strict code)
            EvalFunction = 0x02,        ///< (in a function (not an arrow function's): new.target is allowed)
            EvalMethod = 0x04,          ///< (in a method: super.x is allowed)
            EvalDerivedConstructor = 0x08, ///< (in a derived class's constructor: super() is allowed)
            EvalFieldInitializer = 0x10, ///< (in a class field's initializer: arguments is not allowed)
            EvalTailCall = 0x20,        ///< (the call is in tail position: if it is not eval, a tail call)
            EvalParameters = 0x40,      ///< (in a function's parameters: it may not declare a var named as a parameter)
            EvalArgumentsObject = 0x80  ///< (in a (non-arrow) function's parameters: it may not declare a var named arguments)
        };
        code_kind iKind = code_kind::Script;
        bool iModuleMain = false;       ///< (a module that is the program (not one that the runtime loaded))
        bool iTopLevelAwait = false;    ///< (a module whose code awaits)
        std::string module_descriptor(node* aModule);
        std::uint32_t iEvalFlags = 0u;
        std::size_t iFunctionParametersEnd = 0u;    ///< (Function: the end of its parameters' source; and where its body begins)
        std::size_t iFunctionBodyBegin = 0u;
        bool eval_code() const { return iKind == code_kind::DirectEval || iKind == code_kind::IndirectEval || iKind == code_kind::Function; }
        bool sloppy_eval_var_scope(scope const* aScope) const;   ///< (a scope that a sloppy direct eval in it can add var bindings to)
    };
}
