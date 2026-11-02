/*
  exec.cpp

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

// The Ada runtime: what generated code calls (as "ada::f") for the program's execution: initialization and static data, the 
// stacks (primary: generated code's frames; secondary: composite results and temporaries), the heap, exceptions (occurrences
// and the pending flag that generated code tests), images and values of scalars, and the program's end.

#include <cinttypes>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <execinfo.h>
#include <csignal>
#include <unistd.h>
#endif
#include "core.hpp"
#include "exec.hpp"
#include "int128.hpp"
#include <cstdio>
#include <cstdlib>

namespace neos::ada_runtime
{
    namespace
    {
        constexpr std::uint64_t StaticBase = std::uint64_t{ 1u } << 16u;    ///< (static data: its address (the region's first block))
        constexpr std::uint64_t EnvironmentStack = std::uint64_t{ 64u } << 20u;
        constexpr std::uint64_t SecondaryStack = std::uint64_t{ 16u } << 20u;

        struct exception_info
        {
            std::string name;           ///< (its full name, upper case)
        };

        std::mutex gMutex;
        std::map<std::int64_t, exception_info> gExceptions;
        std::int64_t gIdConstraintError = 0;
        std::int64_t gIdProgramError = 0;
        std::int64_t gIdStorageError = 0;
        std::int64_t gIdTaskingError = 0;
        std::int64_t gExitStatus = 0;
    }

    thread_local execution_state* tState = nullptr;

    void set_state(execution_state* aState)
    {
        tState = aState;
    }

    execution_state& state()
    {
        if (tState == nullptr)
            tState = new execution_state{};
        return *tState;
    }

    void set_pending(std::int64_t aId)
    {
        auto& s = state();
        if (s.excGlobal != nullptr)
            *s.excGlobal = static_cast<unsigned long long>(aId);
    }

    std::string exception_name(std::int64_t aId)
    {
        std::lock_guard lock{ gMutex };
        auto e = gExceptions.find(aId);
        return e != gExceptions.end() ? e->second.name : std::string{ "UNKNOWN" };
    }

    void raise(std::int64_t aId, std::string const& aMessage)
    {
        auto& s = state();
        s.current.id = aId;
        s.current.message = aMessage;
        set_pending(aId);
    }

    std::int64_t predefined_id(int aKind)
    {
        return aKind == 0 ? gIdConstraintError : aKind == 1 ? gIdProgramError : aKind == 2 ? gIdStorageError : gIdTaskingError;
    }

    void raise_predefined(int aKind, std::string const& aMessage)
    {
        raise(predefined_id(aKind), aMessage);
    }

    std::int64_t exit_status_value()
    {
        return gExitStatus;
    }

    // (an Ada string's characters: its descriptor (data, first, last) at an address)
    std::string string_at(address aDescriptor)
    {
        if (aDescriptor == 0)
            return {};
        auto const data = aDescriptor + *at<std::int64_t>(aDescriptor);
        auto const first = *at<std::int64_t>(aDescriptor + 8);
        auto const last = *at<std::int64_t>(aDescriptor + 16);
        if (last < first)
            return {};
        return std::string{ chars(data), static_cast<std::size_t>(last - first + 1) };
    }

    // (a new string on the secondary stack: its descriptor (1 .. n) followed by its characters)
    address new_string(std::string const& aText)
    {
        auto const descriptor = ss_allocate(24u + aText.size());
        *at<std::int64_t>(descriptor) = 24;
        *at<std::int64_t>(descriptor + 8) = 1;
        *at<std::int64_t>(descriptor + 16) = static_cast<std::int64_t>(aText.size());
        std::memcpy(chars(descriptor + 24), aText.data(), aText.size());
        return descriptor;
    }

    void finalize_to(std::size_t aMark)
    {
        // (each object's Finalize (registered with its static link), the last registered first; the pending
        // exception (if any) kept: an exception that Finalize raises is lost (Program_Error, by the RM, is not raised))
        auto& s = state();
        if (s.finalization.size() <= aMark)
            return;
        auto const* h = host();
        auto const pending = s.excGlobal != nullptr ? *s.excGlobal : 0u;
        auto const current = s.current;
        // (the master's dependent tasks: awaited first (their entries removed))
        std::vector<std::int64_t> tasks;
        std::vector<std::int64_t> protectedObjects;
        for (auto i = s.finalization.size(); i-- > aMark;)
            if (s.finalization[i].function == ProtectedEntry)
            {
                protectedObjects.push_back(s.finalization[i].object);
                s.finalization.erase(s.finalization.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else if (s.finalization[i].function == TaskEntry || s.finalization[i].function == CollectionEntry)
            {
                if (s.finalization[i].function == TaskEntry)
                    tasks.push_back(s.finalization[i].object);
                else
                    for (auto t : collection_tasks(s.finalization[i].object))
                        tasks.push_back(t);
                s.finalization.erase(s.finalization.begin() + static_cast<std::ptrdiff_t>(i));
            }
        if (!tasks.empty())
        {
            if (s.excGlobal != nullptr)
                *s.excGlobal = 0u;
            await_tasks(tasks);
        }
        // (the protected objects: first, their queued calls fail (9.4(20)))
        for (auto p : protectedObjects)
            protected_finalize(p);
        bool failed = false;
        while (s.finalization.size() > aMark)
        {
            auto const entry = s.finalization.back();
            s.finalization.pop_back();
            if (entry.function == ObjectsEntry)
            {
                // (an access type's collection: its objects, the last allocated first)
                for (auto const& o : collection_objects(entry.object))
                    s.finalization.push_back(o);
                continue;
            }
            if (h == nullptr || h->call == nullptr || entry.object == 0)
                continue;
            if (s.excGlobal != nullptr)
                *s.excGlobal = 0u;
            neos_rt_type const parameters[2] = { NEOS_RT_I64, NEOS_RT_I64 };
            neos_rt_value arguments[2];
            arguments[0].i64 = entry.link;
            arguments[1].i64 = entry.object;
            neos_rt_value result;
            // (a Finalize: abort-deferred (9.8(9)))
            abort_defer();
            h->call(static_cast<unsigned>(entry.function), 2u, parameters, arguments, NEOS_RT_VOID, &result);
            abort_undefer(false);
            // (an exception propagated by a Finalize: the other finalizations performed, then Program_Error (7.6.1(14-18)))
            if (s.excGlobal != nullptr && *s.excGlobal != 0u && *s.excGlobal < static_cast<std::uint64_t>(AtcSignalBase))
                failed = true;
        }
        s.current = current;
        if (s.excGlobal != nullptr)
            *s.excGlobal = pending;
        // (unless the master is left by an abort (then it is ignored))
        if (failed && (pending == 0u || pending < static_cast<std::uint64_t>(AtcSignalBase)))
            raise_predefined(1, "finalize raised an exception");
    }

    address ss_allocate(std::uint64_t aSize)
    {
        auto& s = state();
        aSize = (aSize + 15u) & ~std::uint64_t{ 15u };
        if (s.ssTop + aSize > s.ssLimit)
        {
            raise_predefined(2, "secondary stack overflow");
            return s.ssBase;
        }
        auto const result = static_cast<address>(s.ssTop);
        s.ssTop += aSize;
        return result;
    }
}

using namespace neos::ada_runtime;

namespace
{
    // --- initialization

#if !defined(_WIN32)
    // (an invalid memory access (e.g. beyond the memory committed): the program's output flushed, then reported as Storage_Error
    // (its exit status: 1))
    void memory_fault(int)
    {
        if (std::getenv("NEOS_ADA_BACKTRACE") != nullptr)
        {
            void* frames[64];
            auto const n = ::backtrace(frames, 64);
            ::backtrace_symbols_fd(frames, n, 2);
        }
        std_flush(1);
        static char const sReport[] = "\nraised STORAGE_ERROR : invalid memory access\n";
        auto const written = ::write(2, sReport, sizeof(sReport) - 1u);
        (void)written;
        ::_exit(1);
    }

    void handle_memory_faults()
    {
        static bool sInstalled = false;
        if (sInstalled)
            return;
        sInstalled = true;
        static std::vector<char> sStack(1u << 16u);
        stack_t alternate{};
        alternate.ss_sp = sStack.data();
        alternate.ss_size = sStack.size();
        ::sigaltstack(&alternate, nullptr);
        struct sigaction action{};
        action.sa_handler = &memory_fault;
        action.sa_flags = SA_ONSTACK;
        sigemptyset(&action.sa_mask);
        ::sigaction(SIGSEGV, &action, nullptr);
        ::sigaction(SIGBUS, &action, nullptr);
    }
#else
    void handle_memory_faults() {}
#endif

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_init(std::int64_t aStaticSize, std::int32_t aSpGlobal, std::int32_t aLimitGlobal, std::int32_t aExcGlobal)
    {
        handle_memory_faults();
        auto& s = state();
        auto const* h = host();
        // (static data first: at StaticBase)
        auto const staticBlock = allocate_segment(static_cast<std::uint64_t>(aStaticSize) + 16u);
        (void)staticBlock;
        std::memset(memory() + StaticBase, 0, static_cast<std::size_t>(aStaticSize));
        auto const stack = allocate_segment(EnvironmentStack);
        auto const secondary = allocate_segment(SecondaryStack);
        s.ssBase = static_cast<std::uint64_t>(secondary);
        s.ssTop = s.ssBase;
        s.ssLimit = s.ssBase + SecondaryStack;
        if (h != nullptr && h->global != nullptr)
        {
            s.spGlobal = h->global(static_cast<unsigned>(aSpGlobal));
            s.limitGlobal = h->global(static_cast<unsigned>(aLimitGlobal));
            s.excGlobal = h->global(static_cast<unsigned>(aExcGlobal));
        }
        if (s.spGlobal != nullptr)
            *s.spGlobal = static_cast<unsigned long long>(stack);
        if (s.limitGlobal != nullptr)
            *s.limitGlobal = static_cast<unsigned long long>(stack) + EnvironmentStack - 65536u;
        if (s.excGlobal != nullptr)
            *s.excGlobal = 0u;
        s.stackBase = static_cast<std::uint64_t>(stack);
        return static_cast<std::int64_t>(StaticBase);
    }

    int hex_digit(char c)
    {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0;
    }

    NEOS_ADA_PRIMITIVE void neos_ada_init_data(std::int64_t aAddress, char const* aHex)
    {
        auto* p = at<std::uint8_t>(aAddress);
        for (std::size_t i = 0u; aHex[i] != '\0' && aHex[i + 1u] != '\0'; i += 2u)
            *p++ = static_cast<std::uint8_t>(hex_digit(aHex[i]) * 16 + hex_digit(aHex[i + 1u]));
    }

    NEOS_ADA_PRIMITIVE void neos_ada_register_exception(std::int64_t aId, char const* aName)
    {
        std::lock_guard lock{ gMutex };
        std::string name{ aName };
        gExceptions[aId] = exception_info{ name };
        if (name == "CONSTRAINT_ERROR")
            gIdConstraintError = aId;
        else if (name == "PROGRAM_ERROR")
            gIdProgramError = aId;
        else if (name == "STORAGE_ERROR")
            gIdStorageError = aId;
        else if (name == "TASKING_ERROR")
            gIdTaskingError = aId;
    }

    // --- exceptions

    NEOS_ADA_PRIMITIVE void neos_ada_raise(std::int64_t aId, std::int64_t aMessage)
    {
        raise(aId, aMessage != 0 ? string_at(aMessage) : std::string{});
    }

    NEOS_ADA_PRIMITIVE void neos_ada_raise_check(std::int32_t aKind, char const* aFile, std::int32_t aLine, char const* aWhat)
    {
        std::string message = std::string{ aFile } + ":" + std::to_string(aLine) + " " + aWhat;
        raise_predefined(aKind, message);
    }

    NEOS_ADA_PRIMITIVE void neos_ada_raise_at(std::int64_t aId, char const* aFile, std::int32_t aLine)
    {
        raise(aId, std::string{ aFile } + ":" + std::to_string(aLine));
    }

    // (occurrences that handlers (and Ada.Exceptions.Save_Occurrence) keep: by handle (0: Null_Occurrence))
    std::mutex gOccurrencesMutex;
    std::map<std::int64_t, occurrence> gOccurrences;
    std::int64_t gNextOccurrence = 1;

    std::int64_t keep_occurrence(occurrence const& aOccurrence)
    {
        std::lock_guard lock{ gOccurrencesMutex };
        auto const handle = gNextOccurrence++;
        gOccurrences[handle] = aOccurrence;
        return handle;
    }

    std::optional<occurrence> occurrence_of(std::int64_t aHandle)
    {
        std::lock_guard lock{ gOccurrencesMutex };
        auto o = gOccurrences.find(aHandle);
        if (o == gOccurrences.end())
            return {};
        return o->second;
    }

    // (the current occurrence: kept for a handler)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_save_occurrence()
    {
        return keep_occurrence(state().current);
    }

    // (a copy of an occurrence (Save_Occurrence): kept until released)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_copy_occurrence(std::int64_t aHandle)
    {
        auto const o = occurrence_of(aHandle);
        return o ? keep_occurrence(*o) : 0;
    }

    NEOS_ADA_PRIMITIVE void neos_ada_reraise(std::int64_t aHandle)
    {
        auto& s = state();
        if (auto const o = occurrence_of(aHandle))
            s.current = *o;
        set_pending(s.current.id);
    }

    // (Reraise_Occurrence: a null occurrence raises nothing)
    NEOS_ADA_PRIMITIVE void neos_ada_reraise_occurrence(std::int64_t aHandle)
    {
        if (auto const o = occurrence_of(aHandle); o && o->id != 0)
        {
            state().current = *o;
            set_pending(o->id);
        }
    }

    NEOS_ADA_PRIMITIVE void neos_ada_release_occurrence(std::int64_t aHandle)
    {
        std::lock_guard lock{ gOccurrencesMutex };
        gOccurrences.erase(aHandle);
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_occurrence_id(std::int64_t aHandle)
    {
        auto const o = occurrence_of(aHandle);
        return o ? o->id : 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_occurrence_message(std::int64_t aHandle)
    {
        auto const o = occurrence_of(aHandle);
        return new_string(o ? o->message : std::string{});
    }

    // (Exception_Information: the name and the message)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_occurrence_information(std::int64_t aHandle)
    {
        auto const o = occurrence_of(aHandle);
        if (!o || o->id == 0)
            return new_string({});
        std::string text = "raised " + exception_name(o->id);
        if (!o->message.empty())
            text += " : " + o->message;
        return new_string(text + "\n");
    }

    // (Raise_Exception: Null_Id raises Constraint_Error)
    NEOS_ADA_PRIMITIVE void neos_ada_raise_exception(std::int64_t aId, std::int64_t aMessage)
    {
        if (aId == 0)
        {
            raise_predefined(0, "null exception id");
            return;
        }
        raise(aId, aMessage != 0 ? string_at(aMessage) : std::string{});
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_exception_name(std::int64_t aId)
    {
        return new_string(aId == 0 ? std::string{} : exception_name(aId));
    }

    // (an exception that the main subprogram did not handle: reported, and the program's exit status is 1)
    NEOS_ADA_PRIMITIVE std::int32_t neos_ada_unhandled()
    {
        auto& s = state();
        std_flush(1);
        std::string report = "\nraised " + exception_name(s.current.id);
        if (!s.current.message.empty())
            report += " : " + s.current.message;
        report += "\n";
        std_write(2, report.data(), report.size());
        std_flush(2);
        return 1;
    }

    NEOS_ADA_PRIMITIVE std::int32_t neos_ada_exit_status()
    {
        return static_cast<std::int32_t>(gExitStatus);
    }

    NEOS_ADA_PRIMITIVE void neos_ada_set_status(std::int64_t aStatus)
    {
        gExitStatus = aStatus;
    }

    // --- the secondary stack and the heap

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_ss_mark()
    {
        return static_cast<std::int64_t>(state().ssTop);
    }

    NEOS_ADA_PRIMITIVE void neos_ada_ss_release(std::int64_t aMark)
    {
        auto& s = state();
        if (static_cast<std::uint64_t>(aMark) >= s.ssBase && static_cast<std::uint64_t>(aMark) <= s.ssTop)
            s.ssTop = static_cast<std::uint64_t>(aMark);
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_ss_allocate(std::int64_t aSize)
    {
        return ss_allocate(static_cast<std::uint64_t>(aSize < 0 ? 0 : aSize));
    }

    // (a function's result on the secondary stack: moved down to the mark (the function's temporaries above it are released))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_ss_return(std::int64_t aMark, std::int64_t aResult, std::int64_t aSize)
    {
        auto& s = state();
        if (aResult <= aMark || static_cast<std::uint64_t>(aMark) < s.ssBase)
            return aResult;
        std::memmove(memory() + aMark, memory() + aResult, static_cast<std::size_t>(aSize));
        s.ssTop = static_cast<std::uint64_t>(aMark) + ((static_cast<std::uint64_t>(aSize) + 15u) & ~std::uint64_t{ 15u });
        return aMark;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_allocate(std::int64_t aSize)
    {
        auto const result = allocate(static_cast<std::uint64_t>(aSize < 0 ? 0 : aSize));
        if (result == 0)
            raise_predefined(2, "heap exhausted");
        return result;
    }

    // (an allocation for an access type whose Storage_Size is specified (aLimit): its total (in aCell) within it, else Storage_Error
    // (13.11(18)))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_allocate_limited(std::int64_t aCell, std::int64_t aSize, std::int64_t aLimit)
    {
        auto* used = at<std::int64_t>(aCell);
        auto const size = aSize < 0 ? 0 : aSize;
        if (*used + size > aLimit)
        {
            raise_predefined(2, "the access type's storage size is exhausted");
            return 0;
        }
        *used += size;
        return neos_ada_allocate(size);
    }

    NEOS_ADA_PRIMITIVE void neos_ada_deallocate(std::int64_t aAddress)
    {
        deallocate(aAddress);
    }

    // (access-to-protected-subprogram values (their descriptors' addresses: procedure, static link, object): whether they are equal)
    NEOS_ADA_PRIMITIVE std::int32_t neos_ada_protected_access_equal(std::int64_t aLeft, std::int64_t aRight)
    {
        if (aLeft == aRight)
            return 1;
        if (aLeft == 0 || aRight == 0)
            return 0;
        return *at<std::int64_t>(aLeft) == *at<std::int64_t>(aRight) && *at<std::int64_t>(aLeft + 16) == *at<std::int64_t>(aRight + 16) ? 1 : 0;
    }

    NEOS_ADA_PRIMITIVE void neos_ada_copy(std::int64_t aTarget, std::int64_t aSource, std::int64_t aCount)
    {
        if (aCount > 0 && aTarget != aSource)
            std::memmove(memory() + aTarget, memory() + aSource, static_cast<std::size_t>(aCount));
    }

    NEOS_ADA_PRIMITIVE void neos_ada_fill(std::int64_t aTarget, std::int64_t aByte, std::int64_t aCount)
    {
        if (aCount > 0)
            std::memset(memory() + aTarget, static_cast<int>(aByte), static_cast<std::size_t>(aCount));
    }

    NEOS_ADA_PRIMITIVE std::int32_t neos_ada_compare(std::int64_t aLeft, std::int64_t aRight, std::int64_t aCount)
    {
        if (aCount <= 0)
            return 0;
        auto const r = std::memcmp(memory() + aLeft, memory() + aRight, static_cast<std::size_t>(aCount));
        return r < 0 ? -1 : r > 0 ? 1 : 0;
    }

    // (two one dimensional arrays' elements compared as unsigned integers of aSize bytes, lexicographically: -1, 0 or 1)
    NEOS_ADA_PRIMITIVE std::int32_t neos_ada_compare_arrays(std::int64_t aLeft, std::int64_t aRight, std::int64_t aSize)
    {
        // (their elements (of aSize bytes; a negative size: signed values of -aSize bytes) compared in order, then their lengths)
        bool const isSigned = aSize < 0;
        if (isSigned)
            aSize = -aSize;
        auto const length = [](std::int64_t d) { auto const f = *at<std::int64_t>(d + 8); auto const l = *at<std::int64_t>(d + 16); return l < f ? std::int64_t{ 0 } : l - f + 1; };
        auto const ln = length(aLeft);
        auto const rn = length(aRight);
        auto const* lp = at<std::uint8_t>(aLeft + *at<std::int64_t>(aLeft));
        auto const* rp = at<std::uint8_t>(aRight + *at<std::int64_t>(aRight));
        for (std::int64_t i = 0; i < ln && i < rn; ++i)
        {
            std::uint64_t a = 0u, b = 0u;
            std::memcpy(&a, lp + i * aSize, static_cast<std::size_t>(aSize));
            std::memcpy(&b, rp + i * aSize, static_cast<std::size_t>(aSize));
            if (a != b)
            {
                if (isSigned && aSize < 8)
                {
                    auto const shift = static_cast<unsigned>(64 - 8 * aSize);
                    auto const sa = static_cast<std::int64_t>(a << shift) >> shift;
                    auto const sb = static_cast<std::int64_t>(b << shift) >> shift;
                    return sa < sb ? -1 : 1;
                }
                if (isSigned)
                    return static_cast<std::int64_t>(a) < static_cast<std::int64_t>(b) ? -1 : 1;
                return a < b ? -1 : 1;
            }
        }
        return ln < rn ? -1 : ln > rn ? 1 : 0;
    }

    // (... of elements aStride bytes apart whose values are aSize bytes (a negative size: signed))
    NEOS_ADA_PRIMITIVE std::int32_t neos_ada_compare_arrays_padded(std::int64_t aLeft, std::int64_t aRight, std::int64_t aStride, std::int64_t aSize)
    {
        bool const isSigned = aSize < 0;
        auto const size = isSigned ? -aSize : aSize;
        auto const length = [](std::int64_t d) { auto const f = *at<std::int64_t>(d + 8); auto const l = *at<std::int64_t>(d + 16); return l < f ? std::int64_t{ 0 } : l - f + 1; };
        auto const ln = length(aLeft);
        auto const rn = length(aRight);
        auto const* lp = at<std::uint8_t>(aLeft + *at<std::int64_t>(aLeft));
        auto const* rp = at<std::uint8_t>(aRight + *at<std::int64_t>(aRight));
        for (std::int64_t i = 0; i < ln && i < rn; ++i)
        {
            std::uint64_t a = 0u, b = 0u;
            std::memcpy(&a, lp + i * aStride, static_cast<std::size_t>(size));
            std::memcpy(&b, rp + i * aStride, static_cast<std::size_t>(size));
            if (a == b)
                continue;
            if (isSigned)
            {
                auto const shift = static_cast<unsigned>(64 - 8 * size);
                auto const sa = shift < 64u ? static_cast<std::int64_t>(a << shift) >> shift : 0;
                auto const sb = shift < 64u ? static_cast<std::int64_t>(b << shift) >> shift : 0;
                return sa < sb ? -1 : 1;
            }
            return a < b ? -1 : 1;
        }
        return ln < rn ? -1 : ln > rn ? 1 : 0;
    }

    // (the stack overflowed (a frame would pass the limit))
    NEOS_ADA_PRIMITIVE void neos_ada_stack_overflow()
    {
        raise_predefined(2, "stack overflow");
    }

    // --- images and values

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_image_integer(std::int64_t aValue)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), aValue < 0 ? "%" PRId64 : " %" PRId64, aValue);
        return new_string(buffer);
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_image_unsigned(std::int64_t aValue)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), " %" PRIu64, static_cast<std::uint64_t>(aValue));
        return new_string(buffer);
    }

    // (a floating point value's image: sign or space, one digit, '.', digits - 1 digits, 'E', sign, at least two digits)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_image_float(double aValue, std::int64_t aDigits)
    {
        if (std::isnan(aValue))
            return new_string("NaN");
        if (std::isinf(aValue))
            return new_string(aValue < 0 ? "-Inf" : "+Inf");
        char buffer[64];
        auto const digits = static_cast<int>(aDigits < 1 ? 1 : aDigits);
        std::snprintf(buffer, sizeof(buffer), "%.*E", digits - 1, aValue);
        std::string text{ buffer };
        // (C's exponent: at least two digits, as Ada's)
        if (text[0] != '-')
            text = " " + text;
        return new_string(text);
    }

    // (a fixed point value's image: its value (count of smalls * small, as numerator/denominator) with aAft digits after the point)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_image_fixed(std::int64_t aCount, double aSmall, std::int64_t aAft)
    {
        char buffer[96];
        auto const value = static_cast<long double>(aCount) * static_cast<long double>(aSmall);
        std::snprintf(buffer, sizeof(buffer), "%.*Lf", static_cast<int>(aAft < 1 ? 1 : aAft), value);
        std::string text{ buffer };
        if (text[0] != '-')
            text = " " + text;
        return new_string(text);
    }

    // (a fixed point value's image, exactly: aCount * aNumerator / aDenominator with aAft digits after the point (rounded, half away
    // from zero))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_image_fixed_exact(std::int64_t aCount, std::int64_t aNumerator, std::int64_t aDenominator, std::int64_t aAft)
    {
        auto const aft = static_cast<int>(aAft < 1 ? 1 : aAft);
        uint128 scale = 1u;
        for (int i = 0; i < aft; ++i)
            scale *= 10u;
        bool const negative = (aCount < 0) != (aNumerator < 0);
        auto const magnitude = [](std::int64_t v) { return v < 0 ? static_cast<uint128>(-(v + 1)) + 1u : static_cast<uint128>(v); };
        auto const count = magnitude(aCount);
        auto const numerator = magnitude(aNumerator);
        auto const denominator = magnitude(aDenominator);
        // (count * numerator * scale / denominator: in 128 bits if it fits, else approximately)
        uint128 const limit = ~static_cast<uint128>(0);
        if (denominator == 0u || (count != 0u && numerator > limit / count) || (count * numerator != 0u && scale > limit / (count * numerator)))
            return neos_ada_image_fixed(aCount, static_cast<double>(aNumerator) / static_cast<double>(aDenominator), aAft);
        auto const product = count * numerator * scale;
        auto quotient = product / denominator;
        if ((product % denominator) * 2u >= denominator)
            ++quotient;
        auto const whole = quotient / scale;
        auto const fraction = quotient % scale;
        auto const digits = [](uint128 v, int aWidth) -> std::string
        {
            std::string text;
            do
            {
                text.insert(text.begin(), static_cast<char>('0' + static_cast<int>(v % 10u)));
                v /= 10u;
            } while (v != 0u);
            while (static_cast<int>(text.size()) < aWidth)
                text.insert(text.begin(), '0');
            return text;
        };
        std::string text = (negative && quotient != 0u ? "-" : " ") + digits(whole, 1) + "." + digits(fraction, aft);
        return new_string(text);
    }

    // (an enumeration literal's image: the aIndex'th of the names (separated by NUL) of a static table)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_image_enumeration(std::int64_t aNames, std::int64_t aIndex)
    {
        char const* p = chars(aNames);
        for (std::int64_t i = 0; i < aIndex; ++i)
            p += std::strlen(p) + 1u;
        return new_string(p);
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_image_character(std::int64_t aCode)
    {
        static char const* const sControl[] = { "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL", "BS", "HT", "LF", "VT", "FF", "CR", "SO", "SI",
            "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB", "CAN", "EM", "SUB", "ESC", "FS", "GS", "RS", "US" };
        if (aCode < 32)
            return new_string(sControl[aCode]);
        if (aCode == 127)
            return new_string("DEL");
        if (aCode >= 128 && aCode < 160)
        {
            // (the language-defined names, in upper case (3.5(32.6/5)))
            static char const* const sHigh[] = { "RESERVED_128", "RESERVED_129", "BPH", "NBH", "RESERVED_132", "NEL", "SSA", "ESA", "HTS", "HTJ",
                "VTS", "PLD", "PLU", "RI", "SS2", "SS3", "DCS", "PU1", "PU2", "STS", "CCH", "MW", "SPA", "EPA", "SOS", "RESERVED_153", "SCI", "CSI",
                "ST", "OSC", "PM", "APC" };
            return new_string(sHigh[aCode - 128]);
        }
        if (aCode > 255)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "Hex_%08X", static_cast<unsigned>(aCode));
            return new_string(buffer);
        }
        std::string text = "'";
        text += static_cast<char>(aCode);
        text += "'";
        return new_string(text);
    }

    std::string trimmed(std::string const& aText)
    {
        auto const first = aText.find_first_not_of(" ");
        auto const last = aText.find_last_not_of(" ");
        return first == std::string::npos ? std::string{} : aText.substr(first, last - first + 1u);
    }

    // (a numeric literal's underscores: each between two digits (extended digits within a based literal's #s) (2.4.1))
    bool valid_underscores(std::string const& aText)
    {
        bool based = false;
        auto const digit = [&](char c)
        {
            return based ? std::isxdigit(static_cast<unsigned char>(c)) != 0 : std::isdigit(static_cast<unsigned char>(c)) != 0;
        };
        for (std::size_t k = 0u; k < aText.size(); ++k)
        {
            auto const c = aText[k];
            if (c == '#' || c == ':')
                based = !based;
            else if (c == '_' && (k == 0u || k + 1u >= aText.size() || !digit(aText[k - 1u]) || !digit(aText[k + 1u])))
                return false;
        }
        return true;
    }

    // (an integer literal's value (Ada syntax, with an optional sign, based literals and exponents); a bad one raises Constraint_Error)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_value_integer(std::int64_t aString)
    {
        auto const text = trimmed(string_at(aString));
        if (!valid_underscores(text))
        {
            raise_predefined(0, "bad input for 'Value: \"" + text + "\"");
            return 0;
        }
        std::string digits;
        for (auto c : text)
            if (c != '_')
                digits += c;
        bool negative = false;
        std::size_t i = 0u;
        if (i < digits.size() && (digits[i] == '-' || digits[i] == '+'))
            negative = digits[i++] == '-';
        auto const bad = [&]() { raise_predefined(0, "bad input for 'Value: \"" + text + "\""); return std::int64_t{ 0 }; };
        if (i >= digits.size() || !std::isdigit(static_cast<unsigned char>(digits[i])))
            return bad();
        unsigned long long base = 10u;
        unsigned long long value = 0u;
        std::size_t j = i;
        while (j < digits.size() && std::isdigit(static_cast<unsigned char>(digits[j])))
            value = value * 10u + static_cast<unsigned>(digits[j++] - '0');
        if (j < digits.size() && (digits[j] == '#' || digits[j] == ':'))
        {
            base = value;
            if (base < 2u || base > 16u)
                return bad();
            char const close = digits[j++];
            value = 0u;
            bool any = false;
            while (j < digits.size() && digits[j] != close)
            {
                auto const d = std::isdigit(static_cast<unsigned char>(digits[j])) ? digits[j] - '0' : std::isxdigit(static_cast<unsigned char>(digits[j])) ? std::tolower(digits[j]) - 'a' + 10 : 99;
                if (static_cast<unsigned>(d) >= base)
                    return bad();
                value = value * base + static_cast<unsigned>(d);
                ++j;
                any = true;
            }
            if (j >= digits.size() || !any)
                return bad();
            ++j;
        }
        if (j < digits.size() && (digits[j] == 'e' || digits[j] == 'E'))
        {
            ++j;
            if (j < digits.size() && digits[j] == '+')
                ++j;
            if (j >= digits.size())
                return bad();
            unsigned e = 0u;
            while (j < digits.size() && std::isdigit(static_cast<unsigned char>(digits[j])))
                e = e * 10u + static_cast<unsigned>(digits[j++] - '0');
            for (unsigned k = 0u; k < e; ++k)
                value *= base;
        }
        if (j != digits.size())
            return bad();
        return negative ? -static_cast<std::int64_t>(value) : static_cast<std::int64_t>(value);
    }

    NEOS_ADA_PRIMITIVE double neos_ada_value_float(std::int64_t aString)
    {
        auto const text = trimmed(string_at(aString));
        if (!valid_underscores(text))
        {
            raise_predefined(0, "bad input for 'Value: \"" + text + "\"");
            return 0.0;
        }
        std::string digits;
        for (auto c : text)
            if (c != '_')
                digits += c;
        char* end = nullptr;
        auto const value = std::strtod(digits.c_str(), &end);
        if (digits.empty() || end == nullptr || *end != '\0' || digits.find('.') == std::string::npos)
        {
            // (an integer literal is a valid real value in 'Value (RM 3.5)
            if (!digits.empty() && end != nullptr && *end == '\0')
                return value;
            raise_predefined(0, "bad input for 'Value: \"" + text + "\"");
            return 0.0;
        }
        return value;
    }

    // (the position of an enumeration literal named by a string (case insensitive, trimmed) in a static table of names; -1: none)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_value_enumeration(std::int64_t aNames, std::int64_t aCount, std::int64_t aString)
    {
        auto text = trimmed(string_at(aString));
        bool const character = text.size() == 3u && text.front() == '\'' && text.back() == '\'';
        if (!character)
            for (auto& c : text)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        char const* p = chars(aNames);
        for (std::int64_t i = 0; i < aCount; ++i)
        {
            if (text == p)
                return i;
            p += std::strlen(p) + 1u;
        }
        raise_predefined(0, "bad input for 'Value: \"" + text + "\"");
        return 0;
    }

    // --- output for tests (the standard library's Text_IO is the program's own)

    NEOS_ADA_PRIMITIVE void neos_ada_put_string(std::int64_t aString)
    {
        auto const text = string_at(aString);
        std_write(1, text.data(), text.size());
    }

    NEOS_ADA_PRIMITIVE void neos_ada_put_line(std::int64_t aString)
    {
        auto const text = string_at(aString) + "\n";
        std_write(1, text.data(), text.size());
    }

    NEOS_ADA_PRIMITIVE void neos_ada_put_int(std::int64_t aValue)
    {
        auto const text = std::to_string(aValue) + "\n";
        std_write(1, text.data(), text.size());
    }

    constexpr auto I64 = NEOS_RT_I64;
    constexpr auto I32 = NEOS_RT_I32;
    constexpr auto F64 = NEOS_RT_F64;
    constexpr auto STR = NEOS_RT_STRING;
    constexpr auto VOID = NEOS_RT_VOID;

    function_table const sExecFunctions{
        { "init", NEOS_RT_FUNCTION(neos_ada_init), I64, 4u, { I64, I32, I32, I32 }, 0u },
        { "init_data", NEOS_RT_FUNCTION(neos_ada_init_data), VOID, 2u, { I64, STR }, 0u },
        { "register_exception", NEOS_RT_FUNCTION(neos_ada_register_exception), VOID, 2u, { I64, STR }, 0u },
        { "raise", NEOS_RT_FUNCTION(neos_ada_raise), VOID, 2u, { I64, I64 }, 0u },
        { "raise_check", NEOS_RT_FUNCTION(neos_ada_raise_check), VOID, 4u, { I32, STR, I32, STR }, 0u },
        { "raise_at", NEOS_RT_FUNCTION(neos_ada_raise_at), VOID, 3u, { I64, STR, I32 }, 0u },
        { "save_occurrence", NEOS_RT_FUNCTION(neos_ada_save_occurrence), I64, 0u, {}, 0u },
        { "protected_access_equal", NEOS_RT_FUNCTION(neos_ada_protected_access_equal), I32, 2u, { I64, I64 }, 0u },
        { "reraise", NEOS_RT_FUNCTION(neos_ada_reraise), VOID, 1u, { I64 }, 0u },
        { "release_occurrence", NEOS_RT_FUNCTION(neos_ada_release_occurrence), VOID, 1u, { I64 }, 0u },
        { "copy_occurrence", NEOS_RT_FUNCTION(neos_ada_copy_occurrence), I64, 1u, { I64 }, 0u },
        { "reraise_occurrence", NEOS_RT_FUNCTION(neos_ada_reraise_occurrence), VOID, 1u, { I64 }, 0u },
        { "occurrence_information", NEOS_RT_FUNCTION(neos_ada_occurrence_information), I64, 1u, { I64 }, 0u },
        { "raise_exception", NEOS_RT_FUNCTION(neos_ada_raise_exception), VOID, 2u, { I64, I64 }, 0u },
        { "occurrence_id", NEOS_RT_FUNCTION(neos_ada_occurrence_id), I64, 1u, { I64 }, 0u },
        { "occurrence_message", NEOS_RT_FUNCTION(neos_ada_occurrence_message), I64, 1u, { I64 }, 0u },
        { "exception_name", NEOS_RT_FUNCTION(neos_ada_exception_name), I64, 1u, { I64 }, 0u },
        { "unhandled", NEOS_RT_FUNCTION(neos_ada_unhandled), I32, 0u, {}, 0u },
        { "exit_status", NEOS_RT_FUNCTION(neos_ada_exit_status), I32, 0u, {}, 0u },
        { "set_status", NEOS_RT_FUNCTION(neos_ada_set_status), VOID, 1u, { I64 }, 0u },
        { "ss_mark", NEOS_RT_FUNCTION(neos_ada_ss_mark), I64, 0u, {}, 0u },
        { "ss_release", NEOS_RT_FUNCTION(neos_ada_ss_release), VOID, 1u, { I64 }, 0u },
        { "ss_allocate", NEOS_RT_FUNCTION(neos_ada_ss_allocate), I64, 1u, { I64 }, 0u },
        { "ss_return", NEOS_RT_FUNCTION(neos_ada_ss_return), I64, 3u, { I64, I64, I64 }, 0u },
        { "allocate", NEOS_RT_FUNCTION(neos_ada_allocate), I64, 1u, { I64 }, 0u },
        { "deallocate", NEOS_RT_FUNCTION(neos_ada_deallocate), VOID, 1u, { I64 }, 0u },
        { "allocate_limited", NEOS_RT_FUNCTION(neos_ada_allocate_limited), I64, 3u, { I64, I64, I64 }, 0u },
        { "copy", NEOS_RT_FUNCTION(neos_ada_copy), VOID, 3u, { I64, I64, I64 }, 0u },
        { "fill", NEOS_RT_FUNCTION(neos_ada_fill), VOID, 3u, { I64, I64, I64 }, 0u },
        { "compare", NEOS_RT_FUNCTION(neos_ada_compare), I32, 3u, { I64, I64, I64 }, 0u },
        { "compare_arrays", NEOS_RT_FUNCTION(neos_ada_compare_arrays), I32, 3u, { I64, I64, I64 }, 0u },
        { "compare_arrays_padded", NEOS_RT_FUNCTION(neos_ada_compare_arrays_padded), I32, 4u, { I64, I64, I64, I64 }, 0u },
        { "stack_overflow", NEOS_RT_FUNCTION(neos_ada_stack_overflow), VOID, 0u, {}, 0u },
        { "image_integer", NEOS_RT_FUNCTION(neos_ada_image_integer), I64, 1u, { I64 }, 0u },
        { "image_unsigned", NEOS_RT_FUNCTION(neos_ada_image_unsigned), I64, 1u, { I64 }, 0u },
        { "image_float", NEOS_RT_FUNCTION(neos_ada_image_float), I64, 2u, { F64, I64 }, 0u },
        { "image_fixed", NEOS_RT_FUNCTION(neos_ada_image_fixed), I64, 3u, { I64, F64, I64 }, 0u },
        { "image_fixed_exact", NEOS_RT_FUNCTION(neos_ada_image_fixed_exact), I64, 4u, { I64, I64, I64, I64 }, 0u },
        { "image_enumeration", NEOS_RT_FUNCTION(neos_ada_image_enumeration), I64, 2u, { I64, I64 }, 0u },
        { "image_character", NEOS_RT_FUNCTION(neos_ada_image_character), I64, 1u, { I64 }, 0u },
        { "value_integer", NEOS_RT_FUNCTION(neos_ada_value_integer), I64, 1u, { I64 }, 0u },
        { "value_float", NEOS_RT_FUNCTION(neos_ada_value_float), F64, 1u, { I64 }, 0u },
        { "value_enumeration", NEOS_RT_FUNCTION(neos_ada_value_enumeration), I64, 3u, { I64, I64, I64 }, 0u },
        { "put_string", NEOS_RT_FUNCTION(neos_ada_put_string), VOID, 1u, { I64 }, 0u },
        { "put_line", NEOS_RT_FUNCTION(neos_ada_put_line), VOID, 1u, { I64 }, 0u },
        { "put_int", NEOS_RT_FUNCTION(neos_ada_put_int), VOID, 1u, { I64 }, 0u },
    };
}

namespace neos::ada_runtime
{
    std::int64_t occurrence_keep(occurrence const& aOccurrence)
    {
        return keep_occurrence(aOccurrence);
    }

    void occurrence_release(std::int64_t aHandle)
    {
        neos_ada_release_occurrence(aHandle);
    }
}
