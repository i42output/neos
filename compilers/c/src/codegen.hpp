/*
  codegen.hpp

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

// C: the code generation of one WebAssembly function (a C function's body)

#pragma once

#include <functional>
#include <map>
#include <set>
#include "program.hpp"

namespace neos::compilers::c
{
    class function_generator
    {
    public:
        // (the dispatch method's plan: each dispatch loop's blocks in the order of their code, and those that a jump back (or a 
        // longjmp) enters, as the planning generation (a first generation of the function) found them)
        struct dispatch_plan
        {
            struct loop
            {
                std::vector<std::int32_t> layout;
                std::set<std::int32_t> entered;     ///< (the blocks that the loop's dispatch enters)
            };
            std::map<node const*, loop> loops;      ///< (by owner: the function's body (nullptr), or a statement expression)
        };
        function_generator(program& aProgram, entity* aFunction, bool aDispatch = false, dispatch_plan* aPlan = nullptr, bool aPlanning = false);
    public:
        void generate();
        bool validate(std::string const& aName);           ///< (a diagnostic: the code's types are valid (NEOS_C_VALIDATE))
        // --- code
    public:
        void op(opcode aOpcode);
        void u32(std::uint32_t aValue);
        void i32(std::int32_t aValue);
        void i64(std::int64_t aValue);
        void f32(float aValue);
        void f64(double aValue);
        void get(std::uint32_t aLocal);
        void set(std::uint32_t aLocal);
        void tee(std::uint32_t aLocal);
        void drop();
        std::uint32_t local(type aType);
        std::uint32_t temporary(type aType);
        void release(std::uint32_t aLocal);
        void call(std::uint32_t aFunction);
        void call_indirect(std::vector<type> const& aParameters, type aResult);
        void runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult);
        void libc(std::string const& aName);        ///< (a C library function, by its signature in the library's table)
        void function_index(std::uint32_t aFunction);
        void load(qtype aType, std::uint32_t aOffset = 0u);     ///< ([address] -> value (an aggregate: its address))
        void store(qtype aType, std::uint32_t aOffset = 0u);    ///< ([address, value] -> ())
        void copy(std::uint64_t aSize);                         ///< ([destination, source] -> ())
        void zero(std::uint64_t aSize);                         ///< ([destination] -> ())
        type wasm_of(qtype aType) const { return iProgram.wasm(iProgram.kind_of(aType)); }
        vkind kind_of(qtype aType) const { return iProgram.kind_of(aType); }
        // --- control
    public:
        enum class control_kind { Block, Loop, If, Exit };
        struct control
        {
            control_kind kind;
            node const* breakOf = nullptr;      ///< (a loop's or switch's: the block that break leaves)
            node const* continueOf = nullptr;   ///< (a loop's: the block that continue leaves)
            node const* label = nullptr;        ///< (a labeled region: goto's target (forward: the block's end; backward: the loop's start))
            std::optional<std::uint32_t> stackMark;  ///< (a block with variable length arrays: the local holding the stack pointer at its start)
        };
        void begin(control_kind aKind, std::optional<type> aResult = {});
        void begin_if(std::optional<type> aResult = {});
        void else_();
        void end();
        void branch_to(std::size_t aControl, bool aConditional = false);
        void branch_table(std::vector<std::size_t> const& aControls, std::size_t aDefault);   ///< ([i32 index] -> ())
        std::size_t find_break(node const* aStatement) const;
        std::size_t find_continue(node const* aStatement) const;
        std::size_t find_label(node const* aLabel) const;
        void restore_stack_to(std::size_t aControl);            ///< (leaving blocks with variable length arrays: the stack pointer restored)
        // --- values
    public:
        void rvalue(node* aExpression);                         ///< (its value: of value_type(aExpression))
        void value_as(node* aExpression, qtype aType);          ///< (its value converted to a type, as by assignment)
        void convert(qtype aFrom, qtype aTo);
        void normalize(qtype aType);                            ///< (an integer value made its type's (truncated, sign or zero extended))
        void truth(node* aExpression);                          ///< (an i32: 1 if it compares unequal to 0)
        void to_truth(qtype aType);                             ///< ([value] -> i32)
        void discard(node* aExpression);                        ///< (evaluated for its side effects)
        void address(node* aExpression);                        ///< (an i64: the address of an lvalue (or of an aggregate value))
        bool in_local(node const* aExpression, std::uint32_t& aLocal) const;    ///< (an lvalue that is a WebAssembly local)
        void constant_value(constant const& aValue, qtype aType);
        void binary(node* aExpression);
        void arithmetic(construct aOperator, qtype aType);      ///< ([a, b] -> result, both of the type)
        void compare(construct aOperator, qtype aType);         ///< ([a, b] -> i32)
        void pointer_arithmetic(node* aPointer, node* aInteger, bool aSubtract, bool aIntegerFirst = false);
        void call_expression(node* aCall, std::optional<std::uint64_t> aResultAddress = {});
        void builtin_call(node* aCall);
        void assignment(node* aAssignment, bool aValueUsed);
        void increment(node* aExpression, bool aValueUsed);
        void store_to(node* aTarget, std::function<void()> const& aValue, bool aValueUsed);   ///< (the value is of the target's type)
        void bit_field_load(member_info const& aMember, vkind aKind = vkind::None);        ///< ([unit address] -> value (of aKind (None: 
                                                                                            ///< the member type's)))
        void bit_field_store(member_info const& aMember, bool aValueUsed, vkind aKind = vkind::None);  ///< ([unit address, value] -> (value))
        void dynamic_size(qtype aType);                         ///< (an i64: the size of a type (a variable length array's, evaluated))
        void conditional(node* aExpression, bool aDiscard);
        void compound_literal(node* aLiteral);
        enum class use { Value, Address, Discard };
        void statement_expression(node* aExpression, use aUse);
        // --- objects
    public:
        std::int64_t frame_object(std::uint64_t aSize, std::uint64_t aAlignment);    ///< (a fixed object's offset in the frame)
        std::int64_t frame_temporary(std::uint64_t aSize, std::uint64_t aAlignment); ///< (a temporary's (until the end of the statement))
        void object_address(entity* aObject);
        void assign_storage(entity* aObject);
        void declare_object(entity* aObject, node* aDeclarator);    ///< (an automatic object's declaration reached: its initialization)
        void initialize(qtype aType, node* aInitializer, std::function<void()> const& aAddress, std::uint64_t aOffset, bool aZeroed);
        // --- statements
    public:
        void statement(node* aStatement);
        void compound(node* aCompound);
        void computed_goto(node* aGoto);
        void declaration_cleanups(node* aDeclaration);
        void cleanups(node* aCompound, std::size_t aFirst, std::size_t aEnd);   ///< (gnu::cleanup: the functions of the block's objects 
                                                                                ///< declared by its items [aFirst, aEnd), in reverse order)
        void jump_cleanups(node* aJump, node* aTarget);     ///< (gnu::cleanup: those of the blocks that a jump to aTarget (a label's 
                                                            ///< statement, a loop or switch; nullptr: return) leaves)
        void reset_vla_marks(std::vector<node*> const& aItems);
        void block_items(std::vector<node*> const& aItems);
        void switch_statement(node* aSwitch);
        void return_statement(node* aReturn);
        void declaration(node* aDeclaration);
        void statement_end();                                   ///< (a statement's temporaries released)
        // --- the dispatch method (a function whose control flow cannot be structured (gotos into blocks or into overlapping regions, 
        // case labels in nested statements (Duff's device)) or that calls setjmp: its statements that contain labels (or calls of 
        // setjmp) as numbered blocks, in a loop that dispatches on the number of the block to execute; jumps set the number, and branch 
        // to the loop's start)
    public:
        void dispatch_body();
        bool contains_target(node* aNode);                      ///< (a label, a call of setjmp, or a case label of a dispatched switch)
        bool dispatched_switch(node* aSwitch);
        std::int32_t block_number(node const* aTarget);
        std::int32_t new_block_number();
        void open_block(std::int32_t aNumber);
        void next_block(std::int32_t aNumber);                  ///< (the current block falls through to the next)
        void jump(std::int32_t aNumber);
        void dispatched_statement(node* aStatement);
        void dispatched_items(node* aOwner, std::vector<node*> const& aItems);
        void begin_dispatch(node const* aOwner);
        void end_dispatch();
        void dispatched_switch_statement(node* aSwitch);
        void prepare_setjmp(node* aExpression);                 ///< (an expression with a call of setjmp: the call's environment saved; 
                                                                ///< the expression evaluated in a block that longjmp resumes)
        void unwind_return();                                   ///< (a jump pending: this function returns (its result zero))
        void jump_check();                                      ///< (after a call: a jump pending (longjmp)...)
    private:
        program& iProgram;
        entity* iFunction;
        function_info& iInfo;
        c_type const* iType;
        std::vector<std::uint8_t> iCode;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> iRelocations;
        std::vector<type> iParameters;
        std::vector<type> iLocals;                  ///< (the locals after the parameters)
        std::multimap<type, std::uint32_t> iFreeTemporaries;
        std::vector<control> iControls;
        std::uint32_t iFramePointer = 0u;           ///< (the frame's address: the stack pointer's value after the prologue)
        std::uint32_t iEntryStack = 0u;             ///< (the stack pointer's value on entry)
        std::uint32_t iResult = 0u;                 ///< (the result's local)
        std::uint32_t iVarargs = 0u;                ///< (the variable arguments' address)
        std::uint32_t iReturnAddress = 0u;          ///< (a function returning a structure or union: where to put it)
        bool iReturnsRecord = false;
        std::uint64_t iFrameFixed = 0u;
        std::uint64_t iTemporaries = 0u;
        std::uint64_t iTemporaryBase = 0u;          ///< (a statement expression's statements: the enclosing statement's temporaries kept)
        std::uint64_t iFrameSize = 0u;
        std::uint64_t iFrameAlignment = 16u;    ///< (the fixed objects' largest alignment (the stack's: 16))
        bool iFrameUsed = false;
        bool iDynamicStack = false;                 ///< (the stack pointer moves in the body (variable length arrays, alloca))
        std::map<node const*, std::uint32_t> iJumpTargets;  ///< (a goto's label: forward or backward region)
        // (the dispatch method)
        bool iDispatch = false;
        struct dispatch_context
        {
            node const* owner = nullptr;            ///< (the function's body (nullptr), or a statement expression (its own loop))
            std::uint32_t state = 0u;               ///< (the number of the block to execute)
            std::size_t loop = 0u;                  ///< (its loop's control)
            std::size_t block = 0u;                 ///< (the current block's control (planning: its if))
            dispatch_plan::loop* plan = nullptr;
            std::size_t position = 0u;              ///< (the current block's in the layout)
            std::map<std::int32_t, std::size_t> positions;  ///< (the blocks' in the layout)
            std::vector<std::size_t> blocks;        ///< (the controls whose ends begin the blocks (by position))
            std::set<std::int32_t> opened;          ///< (planning: the blocks begun so far)
        };
        dispatch_plan* iPlan = nullptr;
        bool iPlanning = false;
        std::vector<dispatch_context> iContexts;
        std::map<std::int32_t, node const*> iNumberOwners;  ///< (a block's context's owner)
        node const* owner_of(node const* aTarget) const;
        dispatch_context& context_of(std::int32_t aNumber);
        std::int32_t iNextNumber = 0;
        std::map<node const*, std::int32_t> iBlockNumbers;
        std::map<node const*, std::int32_t> iDispatchedBreaks;
        std::map<node const*, std::int32_t> iDispatchedContinues;
        std::map<node const*, bool> iContainsTarget;
        std::map<node const*, bool> iDispatchedSwitches;
        std::set<node const*> iPreparedSetjmps;
        std::uint32_t iSetjmpValue = 0u;            ///< (setjmp's value: 0, or longjmp's)
        node const* iStructured = nullptr;          ///< (a statement with a target generated by the structured method (its setjmp prepared))
    public:
        // (labels: the regions that gotos to them branch out of (forward) or back to (backward))
        struct label_region
        {
            node const* label = nullptr;
            std::size_t first = 0u;       ///< (the first item of the region)
            std::size_t last = 0u;        ///< (after the region's last item)
            bool backward = false;
        };
        bool plan_labels(std::vector<node*> const& aItems, std::vector<label_region>& aRegions);
    };
}
