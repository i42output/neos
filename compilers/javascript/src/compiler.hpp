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
#include <memory_resource>
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
        inline constexpr std::uint64_t TagBigInt = 0xFFFC000000000000ull;
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
        std::pmr::vector<node*> children;   ///< (in the compiler's node memory (see compiler::iNodeMemory): freed with its nodes)
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
        I64,            ///< (an integer (a counter): an i64)
        I32             ///< (a 32-bit integer (never -0), e.g. the result of a bitwise operator: an i32)
    };

    enum class intrinsic : std::uint8_t
    {
        None,
        Imul,           ///< (Math.imul(a, b))
        Floor,          ///< (Math.floor(x))
        Ceil,
        Trunc,
        Sqrt,
        Abs
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
        // (the program's property cache stubs: functions that the sites call (their code shared): get (object, key, site -> value),
        // set (object, key, value, strict, site -> result), callee numbers (callee, site -> number))
        // (the alias classes of the runtime's heap's words (see the VM's optimizer::MemargClassShift): a store of one class does not 
        // change a word of another; Any: any word (the default))
        enum class alias : std::uint32_t { Any, Shape, Values, Slot, Proto, Elements, Element, Env, EnvSlot, String, Site, Alloc, Function, Dictionary, Header, Validity };
        static alias alias_of_word(std::uint32_t aOffset, bool aArray);     ///< (a word (its offset) of an object's cell (an array's))
        enum class stub : std::uint32_t { GetNamed, GetComputed, SetNamed, SetComputed, CalleeNumber, MethodNumber, GetLength, CharCodeAt, ToBoolean, ConstructNumber, ConstructThis, ArrayLiteral, ObjectLiteral, ArrayPush, Count };
        void generate_stub(stub aStub);
        void generate_slow_function(std::uint32_t aFunction, std::optional<std::uint32_t> aRuntime, std::size_t aParameters);
        void call_stub(stub aStub);         ///< (a call of the stub (it is used))
        // (the built-ins that a method call x.m(...) calls directly if its callee is the realm's (the runtime's function: its callee, this
        // and arguments (undefined: those not passed)), else the callee is called: their methods' names, argument counts and the 
        // runtime's functions (by number: the runtime's direct_builtin_function's))
        struct direct_builtin { std::string_view method; std::uint32_t minimumArguments; std::uint32_t maximumArguments; char const* runtime; };
        // (a null runtime function: the built-in itself is called (the runtime's native_direct with as many arguments as are passed))
        static constexpr direct_builtin DirectBuiltins[] = { { "slice", 1u, 2u, "slice_direct" }, { "test", 1u, 1u, "test_direct" },
            { "pop", 0u, 0u, nullptr }, { "indexOf", 1u, 2u, nullptr }, { "hasOwnProperty", 1u, 1u, nullptr }, { "exec", 1u, 1u, nullptr },
            { "assign", 2u, 3u, nullptr } };
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
        void slow_runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult);   ///< (a slow path's: see compiler::slow_function)
        std::uint32_t cached_get_named();   ///< (object, key -> its value: a get of a named property with a property cache site of its
                                            ///< own (its number): inline if the site fits, else the runtime's get_named_cached)
        void cached_set_named();            ///< (object, key, value, strict -> the result: as cached_get_named, a set)
        std::uint32_t cached_get(bool aComputed);   ///< (as cached_get_named; aComputed: object, a computed key -> its value (its site's
                                                    ///< entries also have their keys))
        void site_entries(std::uint32_t aSite, std::uint32_t aShape, std::uint32_t aEntry, std::optional<std::uint32_t> aKey, std::size_t aSlow,
            std::function<void()> const& aBody);
        void cached_get_stub(bool aComputed);
        void cached_set_stub();
        void set_computed();                ///< (object, a computed key, value, strict -> the result: as runtime's set (a call of its stub))
        void set_computed_stub();
        void get_length_stub();
        void char_code_at_stub();
        void array_push_stub();
        void to_boolean_stub();
        void construct_this_stub();
        void array_literal_stub();
        void object_literal_stub();
        bool simple_object_literal(node* aNode);
        void array_element(std::uint32_t aObject, std::uint32_t aKey, std::uint32_t aAddress, std::size_t aMiss); ///< (an array's dense
                                                    ///< element (the key a number): its address (in aAddress), else to aMiss)
        void callee_number_stub(stub aStub);
        void mem_load(opcode aOpcode, std::uint32_t aOffset, alias aAlias = alias::Any);   ///< (a load from the runtime's heap (its external memory: layout::Memory))
        void mem_store(opcode aOpcode, std::uint32_t aOffset, alias aAlias = alias::Any);
        void property_site(std::uint32_t aSite);    ///< (the address of a property cache site's entry (aSite: the local (i32) that has its number))
        void volatile_load(opcode aOpcode, std::uint32_t aOffset, bool aHeapOffset, alias aAlias);
        void offset_load(std::uint32_t aOffset, alias aAlias);
        void site_load(opcode aOpcode, std::uint32_t aOffset);  ///< (as mem_load, a site's entry's field (a cached load))
        void cached_load(opcode aOpcode, std::uint32_t aOffset, alias aAlias, bool aHeapOffset = false);  ///< (a cached load of a word that
                                                                ///< the code has checked does not change (e.g. a watched holder's property))
        void cell_of(std::uint32_t aValue);         ///< (the address of an object's cell (aValue: the local that has it))
        void environment_slot(std::uint32_t aHops, std::uint32_t aSlot);  ///< (environment -> the address of its (or an outer one's) slot)
        void callee_number_of(std::uint32_t aCallee, std::optional<std::uint32_t> aSite);    ///< (-> the callee's number for the dispatcher (else -1))
        void callee_number_at(std::uint32_t aCallee, std::uint32_t aSite, bool aCallSite);
        void site_dispatch_matches(std::uint32_t aCallee, std::uint32_t aSite);
        void property_site_at(std::uint32_t aSite);
        void string_lengths_differ();
        void strict_equality(std::uint32_t aLhs, std::uint32_t aRhs);
        void strict_equality_of_others(std::uint32_t aLhs, std::uint32_t aRhs);
        static bool number_literal_operand(node const* aNode);   ///< (a binary expression one of whose operands is a number literal)
        void loose_equality(std::uint32_t aLhs, std::uint32_t aRhs, bool aNotEqual, std::string const& aRuntime);
        void dispatch_number(std::uint32_t aNumber);  ///< (a call of the program's function (see dispatch): its number, before its arguments)
        void dispatch(std::uint32_t aNumber);         ///< (... its arguments -> its result: the call)
        static constexpr std::int64_t TailCallDepth = 1024;  ///< (tail calls made as calls (nested) before they are made by the caller)
        void check();               ///< (the i64 on the stack: an exception branches to the innermost handler)
        void assume_not_exception();    ///< (the i64 on the stack is not an exception (neos::assume): optimized code knows it)
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
            bool globalConstant = false;                ///< (a global lexical binding that is const)
        };
        reference resolve(std::string const& aName, node* aSite);
        void load(reference const& aReference, bool aTypeof = false, node* aSite = nullptr);
        bool initialized_at(binding const* aBinding, node const* aSite) const;
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
        void truth();                   ///< (the value's (on the stack) ToBoolean: an i32)
        void literal(node* aNode);
        void identifier(node* aNode, bool aTypeof = false);
        void member(node* aNode);
        void call_expression(node* aNode);
        void arguments_values(std::vector<node*> const& aArguments, std::optional<std::uint32_t>& aSpread);
        void call_value(std::uint32_t aCallee, std::uint32_t aThis, std::vector<node*> const& aArguments, bool aTail = false,
            function_info const* aKnown = nullptr, std::optional<std::uint32_t> aSite = {});   ///< (aKnown: the function that the callee probably is (it is called directly if it is))
        void call_locals(std::uint32_t aCallee, std::uint32_t aThis, std::vector<std::uint32_t> const& aValues, bool aTail = false,
            function_info const* aKnown = nullptr, std::optional<std::uint32_t> aSite = {});
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
        void optional_chain(node* aNode, std::optional<std::uint32_t>* aThis = nullptr, bool aDelete = false);
        bool is_call_reference(node* aNode) const;
        void delete_expression(node* aNode);
        void yield_expression(node* aNode);
        void await_value();
        void resumption();
        // numeric locals: local bindings whose values are always numbers are f64 (or i64 or i32) locals
        void infer_numeric();
        numeric_rep rep_of(binding const* aBinding) const;
        numeric_rep num_type(node* aNode);
        void numeric(node* aNode);                  ///< (its value as an f64: num_type(aNode) is not None)
        void int32(node* aNode);                    ///< (its value's ToInt32 as an i32: num_type(aNode) is not None)
        void typed(node* aNode, numeric_rep aRep);  ///< (its value as aRep (an integer rep: num_type(aNode) is one))
        void to_int32();                            ///< (the f64 on the stack: its ToInt32 (an i32))
        void box(numeric_rep aRep);                 ///< (an f64 or i64 or i32: as a value)
        void unbox(numeric_rep aRep);               ///< (a value (a number): as an f64 or i64 or i32)
        bool numeric_value(node* aNode);            ///< (a unary, conditional or intrinsic call expression of numbers: its value emitted (else false))
        bool numeric_binary(node* aNode);           ///< (an operation of numbers: emitted (else false))
        bool relational_condition(node* aNode);    ///< (a comparison as a condition: an i32)
        bool numeric_condition(node* aNode);        ///< (an i32)
        bool numeric_update(reference const& aReference, bool aIncrement, bool aPostfix);
        bool numeric_assignment(reference const& aReference, node* aValue, std::string const& aOperator);
        binding* integer_binding(node* aNode);
        void integer_remainder(node* aDividend, binding& aDivisor, bool aZero = false);    ///< (a % the binding (an integer); aZero: whether it is 0 (an i32))
        bool remainder_condition(node* aRemainder);     ///< (whether a remainder of an integer binding is 0 (an i32); false: not one)
        void f64_remainder(node* aDividend, node* aDivisor);  ///< (the two f64s on the stack: the remainder (as % computes it))
        intrinsic intrinsic_of(node* aNode) const;  ///< (a call that is compiled inline (see iIntrinsics))
        // numeric versions: a function whose parameters are used as numbers is compiled twice: a version (chosen when it is called with
        // numbers) whose parameters are numeric locals, and the general one
        bool version_eligible() const;
        bool pure(node* aNode);                     ///< (code that calls no user code and changes nothing but locals)
        void version_check();                       ///< (an i32: whether the numeric version applies (its parameters' arguments are numbers ...))
        bool has_using(node* aStatements) const;
        void export_statement(node* aNode);
        void for_loop(node* init, node* test, node* update, node* body, scope* s, bool perIteration, std::vector<std::string> const& aLabels);
        std::string names_descriptor(scope const* aScope) const;
        void disposal_scope(std::function<void()> const& aBody);
        void integer(node* aNode);                  ///< (an integer (num_type I64) as an i64)
        void counter_guard(std::uint32_t aSlot, std::int64_t aStep, binding const* aBinding = nullptr);
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
        struct prepared_reference
        {
            bool prepared = false;          ///< (a property reference: its object (or this, for super) and key were evaluated)
            node* target = nullptr;
            std::uint32_t object = 0u;
            std::uint32_t keyValue = 0u;
            std::optional<reference> dynamic;   ///< (a name found by name: its reference was resolved (its base is object))
            std::uint32_t superBase = 0u;       ///< (super.x / super[x]: the super base (object is this))
        };
        std::uint32_t super_reference(node* aTarget, std::uint32_t& aThis, std::uint32_t& aKey);
        prepared_reference prepare_reference(node* aElement, bool aBinding = false);
        void assign_prepared(prepared_reference const& aReference, node* aElement);
        void bound_shadows(node* aTest, std::vector<std::pair<binding const*, int>>& aAdded);
        bool bound_comparison(node* aComparison);
        void body_vars_from_parameters();
        std::uint32_t dynamic_reference(reference const& aReference);
        void dynamic_reference_get(std::uint32_t aBase, reference const& aReference);
        void dynamic_reference_set(std::uint32_t aBase, reference const& aReference, std::uint32_t aValue);
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
        std::uint32_t iParameterCount = ParameterCount;    ///< (its parameters (a stub's are its own: see generate_stub))
        std::unordered_map<binding const*, numeric_rep> iNumeric;
        bool iDiscarded = false;
        std::set<node*> iTailCalls;                ///< (the calls in tail position of return statements)
        std::vector<std::uint32_t> iDisposals;     ///< (the dispose capabilities of the blocks with using declarations being generated)
        bool iDiscardedNow = false;    ///< (the expression being generated is an expression statement's: its value is not used)
        std::unordered_map<node const*, binding*> iNumericNames;   ///< (the references (identifiers) to bindings that may be numeric)
        std::set<binding const*> iUnitCounters;        ///< (i64 locals whose only changes are steps of one from a safe integer)
        std::map<binding const*, int> iMonotoneCounters;    ///< (unit counters whose steps are all +1 (1) or all -1 (-1): no guards (see counter_value))
        std::set<binding const*> iPositiveCounters;     ///< (unit counters that start at a positive integer and only step up: never 0 or negative)
        std::map<binding const*, std::vector<node*>> iCounterSteps;    ///< (unit counters' steps (updates and assignments))
        std::set<binding const*> iBoundedCounters;      ///< (unit counters within a loop whose test bounds them within the safe integers)
        binding const* bounded_counter(node* aTest, node* aUpdate);
        int monotone(binding const* aBinding) const;
        void counter_value(binding const* aBinding);
        bool raw_counter(node* aNode, node* aOther);
        std::set<binding const*> iInvariantNumerics;   ///< (f64 locals that are never assigned (e.g. a numeric version's parameters))
        std::map<std::pair<binding const*, int>, std::uint32_t> iBoundShadows;    ///< (a loop's integral bounds of such locals (see bound_shadows))
        std::map<binding const*, std::pair<std::uint32_t, std::uint32_t>> iIntegralShadows;  ///< (a loop's integer values of such locals (see integral_shadows))
        void integral_shadows(node* aNode, std::vector<binding const*>& aAdded);
        bool iNumericParameters = false;            ///< (the numeric version is being inferred or generated: its parameters may be numeric)
        std::vector<std::pair<std::uint32_t, binding*>> iNumericArguments;  ///< (the numeric version's numeric parameters (by index))
        std::unordered_map<node const*, intrinsic> iIntrinsics;    ///< (the numeric version's (pure) intrinsic calls: inline (its entry checks them))
        std::uint32_t iIntrinsicMask = 0u;          ///< (the intrinsics they use (bits: 1 << intrinsic))
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
        void clear_nodes();
        void await_operators(node* aNode, bool aAwait);
        void if_clause_functions(node* aNode);
        bool script_may_run_code() const;
    public:
        std::set<std::string> iEvalPrivateNames;   ///< (direct eval code: the private names of the classes that enclose the call)
    private:
        void yield_identifiers(node* aNode, bool aGenerator);
        void yield_identifiers_child(node* aNode, std::size_t aIndex, bool aGenerator);
        bool keyword_identifier_operand(node* aKeyword, node* aLeftmost, node* aBoundary);
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
        std::optional<std::uint32_t> global_mirror(std::string const& aName);   ///< (the global (an i64) that mirrors a global binding's value)
        function_info* script_function(std::string const& aName);   ///< (the script's function declaration that defines a global function)
        std::uint32_t function_closure(function_info const& aFunction);   ///< (the global (an i64) that holds its function object)
        std::uint32_t property_sites_global();  ///< (the global (an i64) that holds the offset of the program's property cache sites)
        std::uint32_t char_code_at_global();    ///< (the global that holds %String.prototype.charCodeAt% (see function_generator::stub))
        std::uint32_t function_call_global();   ///< (the global that holds %Function.prototype.call% (f.call(t, ...) calls f if the callee is it))
        std::uint32_t array_push_global();      ///< (the global that holds %Array.prototype.push% (see function_generator::stub))
        std::uint32_t array_prototype_global(); ///< (the global that holds %Array.prototype%)
        std::uint32_t direct_builtin_global(std::uint32_t aBuiltin);    ///< (the global that holds the realm's built-in (see function_generator::direct_builtins))
        std::uint32_t allocation_state_global();    ///< (the global (an i64): the region offset of the isolate's allocation state)
        // (a function of the program's that calls the runtime's function (its index) with its arguments: slow paths call it rather than
        // the runtime's, so that an optimizing tier that inlines it can leave for the function's own code there (see the VM's deopt)
        // rather than make the call, and so know more about the code after it)
        std::uint32_t slow_function(std::uint32_t aRuntime, std::string const& aName, std::vector<type> const& aParameters, type aResult);
        void define_slow_functions();
        std::uint32_t tail_depth_global();      ///< (the global (an i64): the tail calls made as calls that are running (see call_value))
        std::uint32_t source_line_of(std::string_view aSource) const;
        mutable std::vector<std::size_t> iNewlines;     ///< (see source_line_of)
        mutable char const* iNewlinesOf = nullptr;
        mutable std::size_t iNewlinesSize = 0u;
    public:
        std::string_view fragment_source() const { return iSource; }
        [[noreturn]] void error(std::string_view aSource, std::string const& aMessage) const;
    private:
        std::string_view iSource;
        std::deque<node> iNodes;
        std::pmr::monotonic_buffer_resource iNodeMemory;    ///< (the nodes' children: released with the nodes (at once))
        std::vector<node*> iStack;
        std::deque<function_info> iFunctions;
        std::deque<scope> iScopes;
        std::vector<std::tuple<node*, scope*, function_info*>> iAnnexBFunctions;
        std::map<std::string, std::uint32_t> iLiterals;     ///< (string literals -> their globals)
        std::vector<std::pair<std::string, std::uint32_t>> iLiteralOrder;
        std::unordered_map<std::string, std::uint32_t> iGlobalIndex;   ///< (the generator's globals (global_index's), by name)
        std::size_t iGlobalIndexed = 0u;                                ///< (how many of the generator's globals iGlobalIndex has)
        std::map<std::string, std::uint32_t> iMirrors;      ///< (global binding names -> the globals that mirror their values)
        std::vector<std::pair<std::string, std::uint32_t>> iMirrorOrder;
        std::map<std::string, function_info*> iScriptFunctions;     ///< (the script's function declarations (those that define its functions))
        std::map<function_info const*, std::uint32_t> iClosures;  ///< (function declarations -> the globals that hold their function objects)
        std::uint32_t iDispatcher = 0u;
        bool iFunctionCallUsed = false;         ///< (function_call_global is used: the program's start sets it)
        std::uint32_t iDirectBuiltinsUsed = 0u; ///< (by bit: the direct built-ins whose globals are used (the program's start sets them))
        bool iDirectDispatch = false;           ///< (the program's functions are called directly (call_indirect): their indices are consecutive)
        std::uint32_t iDispatchSignature = 0u;  ///< (their signature (an indirect signature))
        std::uint32_t iStubs[static_cast<std::size_t>(function_generator::stub::Count)] = {};   ///< (see function_generator::stub)
        bool iStubsUsed[static_cast<std::size_t>(function_generator::stub::Count)] = {};
        struct slow_info { std::uint32_t function; std::optional<std::uint32_t> runtime; std::vector<type> parameters; };
        std::map<std::string, slow_info> iSlowFunctions;    ///< (by the runtime's function's name: see slow_function)
        std::uint32_t iTemplateSites = 0u;
        std::uint32_t iPropertySites = 0u;      ///< (the program's property cache sites (see cached_get_named): the next one's number)
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
