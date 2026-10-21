/*
  gc.hpp

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

// The garbage collector (gc.cpp): a non-moving mark-sweep collector of the heap's cells
//
// Roots: the native stacks (scanned conservatively: any word that is a value that refers to a live cell, or that points into a live
// cell, keeps that cell alive) — the running stack, the stacks of the code that the running code was resumed from (stack segments)
// and the stacks of suspended generators and async functions (their own segments, scanned when they are traced) —, the interpreter's
// values (neos_rt_host::values), the memory of rooted containers (root_allocator: e.g. value_list) and the runtime's own state, which
// each part of the runtime adds with a root_tracer (realms, interned strings, the job queue, modules ...).
//
// Tracing: each cell type marks the cells it refers to (cell::trace). A cell type with values (or pointers to cells) of its own must
// override trace (calling its base's): a value it does not mark is freed while the cell still refers to it. Weak references (WeakMap
// and WeakSet entries (ephemerons), WeakRef targets, FinalizationRegistry targets) are not marked; they are cleared when their
// referents die (tracer::ephemerons, tracer::after_marking).
//
// A collection happens at an allocation (heap::make), when enough has been allocated since the last one (NEOS_JS_GC_STRESS=N: every
// N allocations, for testing (freed cells' memory and handles are then not reused: NEOS_JS_GC_QUARANTINE=0 reuses them); NEOS_JS_GC=0:
// never; NEOS_JS_GC_VERBOSE=1: a line for each collection), or when $262.gc() is called.
//
// The rules for the runtime's code (and a new cell type):
// - a cell type's values (and pointers to cells) are marked by its trace (an override of cell::trace that calls its base's: e.g.
//   object::trace marks an object's prototype, properties, internal value and private elements); a weak one is not marked, and is
//   cleared (tracer::after_marking) if it is not tracer::alive;
// - a cell's type has single inheritance from cell (the cell is at the start of its memory); its destructor does not use other cells
//   (they may have been freed);
// - a container of values (or of structures with values) that is not a cell's member (a C++ local or temporary, a function's
//   result, a member of a structure on the stack, a static) is rooted: value_list, rooted_vector<T> or another container with
//   root_allocator (a static can instead be marked by a root_tracer); a pointer to a value in a container that a cell owns is not a
//   root: the cell must be alive otherwise (a local value, or a reference to the cell, is);
// - a std::function does not capture values (the collector does not see them): enqueue_job and async_native take the values;
// - a C++ object that refers to a cell's memory (e.g. a Map's cursors, the positions of its iterators) forgets it when the cell
//   dies (tracer::alive(void const*)).

#pragma once

#include <unordered_map>
#include "js.hpp"

#if defined(_MSC_VER)
#include <csetjmp>
#endif

namespace neos::javascript_runtime
{
    // --- marking

    class collector;
    struct stack_segment;

    class tracer
    {
    public:
        explicit tracer(collector& aCollector) : iCollector{ aCollector } {}
        tracer(tracer const&) = delete;
        tracer& operator=(tracer const&) = delete;
    public:
        void mark(value aValue)
        {
            if (aValue >= TagString && aValue < (TagInternal | 0xFFFFFFFFFFFFull))
                mark_handle(handle_of(aValue));
        }
        void mark(cell const* aCell)
        {
            if (aCell != nullptr && aCell->marked == 0u)
                push(const_cast<cell&>(*aCell));
        }
        void mark(cell const& aCell) { mark(&aCell); }
        void mark(std::span<value const> aValues)
        {
            for (auto const v : aValues)
                mark(v);
        }
        void mark(property const& aProperty)
        {
            mark(aProperty.key);
            mark(aProperty.data);
            mark(aProperty.setter);
        }
        void mark(property_descriptor const& aDescriptor)
        {
            if (aDescriptor.valueField)
                mark(*aDescriptor.valueField);
            if (aDescriptor.getter)
                mark(*aDescriptor.getter);
            if (aDescriptor.setter)
                mark(*aDescriptor.setter);
        }
        void mark(iterator_record const& aRecord)
        {
            mark(aRecord.iterator);
            mark(aRecord.nextMethod);
        }
        // (words that may be values or pointers into cells (e.g. a stack's): [aBegin, aEnd) (aligned to 8 bytes))
        void conservative(void const* aBegin, void const* aEnd);
        // (a suspended coroutine's stack: its segments, from the innermost (aTop) to (not including) aBottom; and the interpreter's values
        // of its frames)
        void suspended_stack(stack_segment const* aTop, stack_segment const* aBottom, neos_rt_coroutine* aCoroutine);
        // (weak references)
        bool alive(value aValue) const;     ///< (after marking: a value that does not refer to a cell, or refers to a live one)
        bool alive(void const* aAddress) const; ///< (after marking: an address that is not in a cell, or is in a live one)
        bool marked(value aValue) const;    ///< (during marking: as alive, as far as marking has got)
        void ephemerons(std::unordered_map<value, value> const& aTable);    ///< (a WeakMap's or WeakSet's: a value is marked if its key is)
        // (aFunction(aCell, *this) is called when marking has finished (before the dead cells are freed): to clear its weak references to
        // the dead (see alive))
        void after_marking(cell& aCell, void (*aFunction)(cell& aCell, tracer& aTracer));
    private:
        void mark_handle(std::uint32_t aHandle);
        void push(cell& aCell);
    private:
        collector& iCollector;
    };

    // (the runtime's own roots: a root_tracer (a static object) marks those of a part of the runtime (its statics) at each collection;
    // its optional second function is called when marking has finished (e.g. to forget the dead that a table refers to weakly))
    class root_tracer
    {
    public:
        using function = void (*)(tracer& aTracer);
        explicit root_tracer(function aFunction, function aAfterMarking = nullptr);
    };

    // --- stacks: segments of native stacks that are not the running stack's live part (e.g. the stack of a coroutine's resumer, while
    // the coroutine runs; a suspended coroutine's stack; on Windows, the stack of code that called generated code, which runs on a
    // fiber of its own)

    struct stack_segment
    {
        void const* low = nullptr;          ///< (its live part: [low, high); none if high is null)
        void const* high = nullptr;
        stack_segment* previous = nullptr;
    };

    stack_segment*& stack_segments();       ///< (the calling thread's: innermost first)
    void const* stack_base();               ///< (the base (highest address) of the stack that the calling thread is running on)
    void set_stack_base(void const* aBase); ///< (the running coroutine's (not Windows: its fibers' are known))
    void const* stack_pointer();            ///< (an address below the caller's frame)

    // (the callee-saved registers are saved in the calling function's frame (call before recording a segment of its stack: the values
    // that its callers keep in registers are then in the segment))
#if defined(_MSC_VER)
#define NEOS_JS_SPILL_REGISTERS() std::jmp_buf neosSpilledRegisters; (void)setjmp(neosSpilledRegisters)
#else
#define NEOS_JS_SPILL_REGISTERS() __builtin_unwind_init()
#endif

    // (the running stack's segment while the code that it calls runs on another stack (Windows: generated code runs on fibers))
    class stack_switch
    {
    public:
        explicit stack_switch(void const* aLow) : iSegment{ aLow, stack_base(), stack_segments() } { stack_segments() = &iSegment; }
        ~stack_switch() { stack_segments() = iSegment.previous; }
        stack_switch(stack_switch const&) = delete;
        stack_switch& operator=(stack_switch const&) = delete;
    private:
        stack_segment iSegment;
    };

    // --- control

    void collect_garbage();                 ///< (a full collection now ($262.gc))
    // (no collection happens while one exists (e.g. while a realm is initialized))
    class gc_pause
    {
    public:
        gc_pause();
        ~gc_pause();
        gc_pause(gc_pause const&) = delete;
        gc_pause& operator=(gc_pause const&) = delete;
    };

    // (WeakRef: AddToKeptObjects (a WeakRef's target is kept alive until the job ends) and ClearKeptObjects (at the end of a job))
    void keep_during_job(value aValue);
    void clear_kept_objects();
}
