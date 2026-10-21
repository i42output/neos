/*
  api.hpp

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

// The interface between the runtime and compiled code (api.cpp) and the runtime's functions that it uses

#pragma once

#include "js.hpp"

namespace neos::javascript_runtime
{
    // compiled JavaScript functions have one signature: (callee, this, new.target, argument count, the first four arguments) ->
    // result (i64s but the count, an i32); the arguments that follow the first four are on the extra arguments stack (pushed by the
    // caller, which pops them when the call returns)
    inline constexpr std::size_t RegisterArguments = 4u;

    value call_compiled(function_object& aFunction, value aThis, arguments aArguments, value aNewTarget);
    // (a compiled function by its index (of the program aProgram: null, the running one), e.g. a generator's body)
    value call_compiled_index(void* aProgram, std::uint32_t aIndex, value aCallee, value aThis, arguments aArguments, value aNewTarget);

    // (the extra arguments stack)
    std::vector<value>& extra_arguments();

    // (a JavaScript exception that compiled code has not caught yet)
    value& pending_exception();

    void set_host(neos_rt_host const* aHost);

    // (the global lexical environment's bindings (let, const and class declarations of scripts): the current realm's)
    using global_binding = realm::global_binding;
    std::unordered_map<value, global_binding>& global_lexicals();
    std::span<neos_rt_function const> api_functions();

    // (functions.cpp)
    value make_closure(std::uint32_t aNumber, std::uint32_t aIndex, value aEnvironment, std::int32_t aFlags, value aName, std::uint32_t aLength, value aSource);
    value make_arguments_object(value aCallee, std::span<value const> aArguments, bool aMapped);
    value map_arguments(value aArguments, value aEnvironment, std::uint32_t aParameters, std::span<std::uint32_t const> aSlots);

    // (operators.cpp)
    value binary_operation(int aOperation, value aLeft, value aRight);
    value unary_operation(int aOperation, value aValue);
    void put_value(value aBase, value aKey, value aValue, bool aStrict);   ///< (PutValue: aKey a property key; aBase any value but null or undefined)
    std::string describe_key(value aKey);
    value make_iterator_record(iterator_record const& aRecord);
    iterator_record& iterator_record_of(value aRecord);
    value make_for_in_iterator(value aObject);
    value for_in_step(value aIterator);                 ///< (the next key, or Empty)

    // (console.cpp)
    void report_uncaught(value aException, bool aInPromise = false);
    std::string inspect(value aValue, bool aStyled, int aDepth = 2);

    // (generators.cpp: the generators' and async functions' side of the interface)
    value start_function(value aCallee, value aThis, value aEnvironment, std::uint32_t aBody, std::int32_t aFlags);
    value start_function_of(void* aProgram, value aEnvironment, std::uint32_t aBody);
    // (modules.cpp)
    value import_dynamic(value aSpecifier, value aOptions);
    namespace api
    {
        value module_define(std::int32_t aFunction, value aDescriptor);
        std::int32_t module_main();
        value import_get(value aEnvironment, std::int32_t aHops, std::int32_t aSlot, value aName);
        value import_meta(value aEnvironment);
        value import_dynamic_(value aSpecifier, value aOptions);
    }
    value yield_value(value aValue);        ///< (Return: resumed to return (see resume_value))
    value yield_delegate(value aIterable);
    value resume_value();

    // (classes.cpp)
    value make_private_name(value aDescription);
    value class_prototype(value aHeritage);     ///< (aHeritage Empty: none)
    value class_constructor(value aConstructor, value aPrototype, value aHeritage, value aName, value aSource);   ///< (aConstructor undefined: the default)
    void class_private_method(value aConstructor, value aHome, value aName, value aFunction, std::int32_t aKind, bool aStatic);
    void class_field(value aConstructor, value aHome, value aName, value aInitializer, bool aStatic);
    void class_static_block(value aConstructor, value aFunction);
    value class_finish(value aConstructor);
    value super_construct(value aConstructor, value aNewTarget, value aArguments);
    value private_get(value aObject, value aName);
    void private_set(value aObject, value aName, value aValue);
    bool private_in(value aName, value aObject);

    // (eval.cpp: the interface's functions for code whose names are found at run time, and for eval code)
    namespace api
    {
        value env_names(value aEnvironment, value aDescriptor);
        value env_with(value aParent, value aObject);
        value dyn_get(value aEnvironment, value aName, std::int32_t aTypeof);
        value dyn_set(value aEnvironment, value aName, value aValue, std::int32_t aFlags);
        value dyn_delete(value aEnvironment, value aName);
        value dyn_this(value aEnvironment, value aName);
        value eval_declare(value aEnvironment, value aName, std::int32_t aFlags);
        value eval_function(value aEnvironment, value aName, value aFunction);
        value eval_direct(value aCallee, value aSource, std::int32_t aCount, std::int32_t aFlags, value aEnvironment, value aThis, value aNewTarget, value aHome);
        value tail_call(value aCallee, value aThis, std::int32_t aCount, value a0, value a1, value a2, value a3);
        value eval_this();
        value eval_this_derived(value aEnvironment);
        value eval_bind_this(value aEnvironment, value aThis);
        value eval_internal(value aEnvironment, value aName);
        value eval_new_target();
        value eval_home();
        value eval_environment();
        value eval_complete(value aResult);
    }

    // (builtins_disposable.cpp: using and await using declarations: a DisposeCapability (an internal cell), AddDisposableResource
    // (aAsync: the async-dispose hint) and DisposeResources (with the block's completion: aThrew, a throw completion of aCompletion;
    // its result's a normal completion (undefined) or it throws))
    value dispose_new();
    void dispose_add(value aCapability, value aValue, bool aAsync);
    value dispose_all(value aCapability, value aCompletion, bool aThrew);

    // (regexp.cpp)
    value make_regexp(value aPattern, value aFlags);
}
