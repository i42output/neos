/*
  exec.hpp

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

// The Ada runtime: the state of a thread of control (an Ada task: the environment task's is the program's thread's) and the
// services of its execution (exceptions, the secondary stack) that other parts of the runtime use

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "ada.hpp"

namespace neos::ada_runtime
{
    struct occurrence
    {
        std::int64_t id = 0;
        std::string message;
    };

    struct execution_state
    {
        unsigned long long* spGlobal = nullptr;     ///< (the program's globals: the primary stack's pointer and limit, the pending exception)
        unsigned long long* limitGlobal = nullptr;
        unsigned long long* excGlobal = nullptr;
        std::uint64_t stackBase = 0u;
        std::uint64_t ssBase = 0u;
        std::uint64_t ssTop = 0u;
        std::uint64_t ssLimit = 0u;
        occurrence current;
        struct finalizable { std::int64_t object; std::int64_t function; std::int64_t link; };
        std::vector<finalizable> finalization;     ///< (controlled objects to finalize: address, its Finalize (function index), that's static link)
    };

    execution_state& state();
    void set_state(execution_state* aState);   ///< (the running task's (tasking switches it))
    void set_pending(std::int64_t aId);
    void await_tasks(std::vector<std::int64_t> const& aTasks);     ///< (a master's dependent tasks: their termination awaited (tasking.cpp))
    constexpr std::int64_t AbortSignal = 0x7FFFFFFFFFFFFFFF;       ///< (the exception that aborts a task: no handler handles it)
    constexpr std::int64_t AtcSignalBase = AbortSignal - 1024;      ///< (+ depth: the exception that aborts an asynchronous select's abortable part)
    constexpr std::int64_t TaskEntry = -1;                         ///< (a finalization chain entry that is a dependent task)
    constexpr std::int64_t CollectionEntry = -2;                   ///< (... a collection: the tasks that an access type's allocators created)
    std::vector<std::int64_t> collection_tasks(std::int64_t aCollection);   ///< (tasking.cpp)
    constexpr std::int64_t ProtectedEntry = -4;                    ///< (... a protected object: its queued calls fail (Program_Error) when it is finalized (9.4(20)))
    void protected_finalize(std::int64_t aObject);  ///< (tasking.cpp)
    std::int64_t predefined_id(int aKind);         ///< (the id of a predefined exception: see raise_predefined)
    constexpr std::int64_t ObjectsEntry = -3;                      ///< (... an access type's collection: the controlled objects its allocators created (7.6.1(11/3)))
    std::vector<execution_state::finalizable> collection_objects(std::int64_t aCollection);    ///< (ops.cpp: removed)
    void raise(std::int64_t aId, std::string const& aMessage);
    void raise_predefined(int aKind, std::string const& aMessage);  ///< (0: Constraint_Error, 1: Program_Error, 2: Storage_Error, 3: Tasking_Error)
    std::string exception_name(std::int64_t aId);
    std::string string_at(address aDescriptor);
    address new_string(std::string const& aText);
    address ss_allocate(std::uint64_t aSize);
    void finalize_to(std::size_t aMark);       ///< (the task's controlled objects registered after the mark finalized)
    void abort_defer();                        ///< (an abort-deferred operation begun (9.8(6-11)))
    void abort_undefer(bool aSignal);          ///< (... ended (aSignal: then the task's pending abort signalled, if no exception is propagating))
    std::int64_t occurrence_keep(occurrence const& aOccurrence);    ///< (an occurrence kept (an Exception_Occurrence's handle))
    void occurrence_release(std::int64_t aHandle);                  ///< (... no longer kept)
}
