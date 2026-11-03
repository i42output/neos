/*
  memory.cpp

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

// The C language's runtime: the C memory (a region of address space reserved once, committed as it is used: the runtime's objects,
// the program's static data, the stack, then the heap), the program's initialization, arguments and exit

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <map>
#include <mutex>
#include <string>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
extern char** environ;
#endif
#include <neos/language/c_library.hpp>
#include "core.hpp"

namespace neos::c_runtime
{
    namespace
    {
        neos_rt_host const* gHost = nullptr;

        constexpr std::uint64_t RegionSize = std::uint64_t{ 64u } << 30u;
        constexpr std::uint64_t Granule = std::uint64_t{ 1u } << 16u;

        class region
        {
        public:
            region()
            {
#if defined(_WIN32)
                iBase = static_cast<std::uint8_t*>(::VirtualAlloc(nullptr, RegionSize, MEM_RESERVE, PAGE_NOACCESS));
#else
                auto const p = ::mmap(nullptr, RegionSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                iBase = p == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(p);
#endif
                if (iBase == nullptr)
                {
                    std::fputs("C runtime: cannot reserve the memory's address space\n", stderr);
                    std::abort();
                }
                // (the runtime's objects: committed now (the program's static data, from StaticBase, when the program starts))
                iTop = language::c::layout::RuntimeBase;
                commit(iTop, language::c::layout::StaticBase - language::c::layout::RuntimeBase);
                iTop = language::c::layout::StaticBase;
            }
        public:
            std::uint8_t* base() const { return iBase; }
            // (a block of whole granules, committed (zeroed))
            address take(std::uint64_t aSize)
            {
                std::lock_guard lock{ iMutex };
                aSize = (aSize + Granule - 1u) & ~(Granule - 1u);
                for (auto i = iFree.begin(); i != iFree.end(); ++i)
                    if (i->second >= aSize)
                    {
                        auto const result = i->first;
                        auto const rest = i->second - aSize;
                        iFree.erase(i);
                        if (rest != 0u)
                            iFree.emplace(result + aSize, rest);
                        std::memset(iBase + result, 0, aSize);
                        return result;
                    }
                if (iTop + aSize > RegionSize)
                    return 0;
                auto const result = static_cast<address>(iTop);
                commit(iTop, aSize);
                iTop += aSize;
                return result;
            }
            void give(address aAddress, std::uint64_t aSize)
            {
                std::lock_guard lock{ iMutex };
                aSize = (aSize + Granule - 1u) & ~(Granule - 1u);
                auto [i, inserted] = iFree.emplace(aAddress, aSize);
                auto next = std::next(i);
                if (next != iFree.end() && i->first + static_cast<address>(i->second) == next->first)
                {
                    i->second += next->second;
                    iFree.erase(next);
                }
                if (i != iFree.begin())
                {
                    auto previous = std::prev(i);
                    if (previous->first + static_cast<address>(previous->second) == i->first)
                    {
                        previous->second += i->second;
                        iFree.erase(i);
                    }
                }
            }
            // (the static data: from StaticBase to aEnd)
            void static_data(std::uint64_t aEnd)
            {
                std::lock_guard lock{ iMutex };
                auto const end = (aEnd + Granule - 1u) & ~(Granule - 1u);
                if (end > iTop)
                {
                    commit(iTop, end - iTop);
                    iTop = end;
                }
            }
        private:
            void commit(std::uint64_t aOffset, std::uint64_t aSize)
            {
                if (aSize == 0u)
                    return;
#if defined(_WIN32)
                if (::VirtualAlloc(iBase + aOffset, aSize, MEM_COMMIT, PAGE_READWRITE) == nullptr)
                {
                    std::fputs("C runtime: cannot commit memory\n", stderr);
                    std::abort();
                }
#else
                if (::mprotect(iBase + aOffset, aSize, PROT_READ | PROT_WRITE) != 0)
                {
                    std::fputs("C runtime: cannot commit memory\n", stderr);
                    std::abort();
                }
#endif
            }
        private:
            std::mutex iMutex;
            std::uint8_t* iBase = nullptr;
            std::uint64_t iTop = 0u;
            std::map<address, std::uint64_t> iFree;
        };

        region& the_region()
        {
            static region sRegion;
            return sRegion;
        }

        // the heap: size classes of small blocks (free lists), larger blocks from the region; a block's size is in the 16 bytes before 
        // it (an aligned block (aligned_alloc): the size's top bit set, and the block it is within at 8 bytes before it)
        constexpr std::uint64_t AlignedFlag = std::uint64_t{ 1 } << 63u;

        class heap
        {
        public:
            address allocate(std::uint64_t aSize)
            {
                std::lock_guard lock{ iMutex };
                auto const size = (aSize + 15u) & ~std::uint64_t{ 15u };
                if (size <= MaxSmall)
                {
                    auto& list = iFree[size / 16u];
                    address block;
                    if (!list.empty())
                    {
                        block = list.back();
                        list.pop_back();
                        std::memset(at<std::uint8_t>(block), 0, size);
                    }
                    else
                    {
                        if (iChunkUsed + size + 16u > iChunkSize)
                        {
                            iChunk = the_region().take(ChunkSize);
                            if (iChunk == 0)
                                return 0;
                            iChunkSize = ChunkSize;
                            iChunkUsed = 0u;
                        }
                        block = iChunk + static_cast<address>(iChunkUsed) + 16;
                        iChunkUsed += size + 16u;
                    }
                    *at<std::uint64_t>(block - 16) = size;
                    return block;
                }
                auto const block = the_region().take(size + 16u);
                if (block == 0)
                    return 0;
                *at<std::uint64_t>(block) = size;
                return block + 16;
            }
            void deallocate(address aBlock)
            {
                if (aBlock == 0)
                    return;
                auto const header = *at<std::uint64_t>(aBlock - 16);
                if ((header & AlignedFlag) != 0u)
                {
                    deallocate(*at<std::uint64_t>(aBlock - 8));
                    return;
                }
                std::lock_guard lock{ iMutex };
                auto const size = header;
                if (size <= MaxSmall)
                    iFree[size / 16u].push_back(aBlock);
                else
                    the_region().give(aBlock - 16, size + 16u);
            }
            std::uint64_t size_of(address aBlock)
            {
                auto const header = *at<std::uint64_t>(aBlock - 16);
                if ((header & AlignedFlag) != 0u)
                    return header & ~AlignedFlag;
                return header;
            }
        private:
            static constexpr std::uint64_t MaxSmall = 4096u;
            static constexpr std::uint64_t ChunkSize = std::uint64_t{ 1u } << 20u;
            std::mutex iMutex;
            std::vector<address> iFree[MaxSmall / 16u + 1u];
            address iChunk = 0;
            std::uint64_t iChunkSize = 0u;
            std::uint64_t iChunkUsed = 0u;
        };

        heap& the_heap()
        {
            static heap sHeap;
            return sHeap;
        }

        // the program's arguments (argv and envp: arrays of pointers to strings, in the C memory)
        address gArgv = 0u;
        int gArgc = 0;
        address gEnvp = 0u;
        bool gEnded = false;
    }

    neos_rt_host const* host()
    {
        return gHost;
    }

    std::uint8_t* memory()
    {
        static std::uint8_t* const sBase = the_region().base();
        return sBase;
    }

    void set_host(neos_rt_host const* aHost)
    {
        gHost = aHost;
        // (the memory: the external memory that C programs' code uses (fixed: its address and size never change))
        if (gHost != nullptr && gHost->version >= 14u && gHost->external_memory_at != nullptr)
            gHost->external_memory_at(language::c::layout::Memory, memory(), RegionSize, NEOS_RT_EXTERNAL_MEMORY_FIXED);
    }

    std::vector<neos_rt_function>& functions()
    {
        static std::vector<neos_rt_function> sFunctions;
        return sFunctions;
    }

    function_table::function_table(std::initializer_list<neos_rt_function> aFunctions)
    {
        functions().insert(functions().end(), aFunctions.begin(), aFunctions.end());
    }

    neos_rt_function library(char const* aName, neos_rt_function_pointer aFunction)
    {
        // (its description from the library's table: "libc." and its name; its signature's types)
        neos_rt_function result{};
        for (auto const& f : language::c::LibraryFunctions)
            if (std::strcmp(f.name, aName) == 0)
            {
                static std::vector<std::string> sNames;
                sNames.reserve(512u);
                sNames.push_back(std::string{ "libc." } + aName);
                result.name = sNames.back().c_str();
                result.function = aFunction;
                auto const type_of = [](char c)
                {
                    switch (c)
                    {
                    case 'I': return NEOS_RT_I32;
                    case 'L': return NEOS_RT_I64;
                    case 'F': return NEOS_RT_F32;
                    case 'D': return NEOS_RT_F64;
                    default: return NEOS_RT_VOID;
                    }
                };
                std::string const signature = f.signature;
                result.result = type_of(signature[0]);
                result.parameterCount = 0u;
                for (std::size_t i = signature.find(':') + 1u; i < signature.size(); ++i)
                    result.parameters[result.parameterCount++] = type_of(signature[i]);
                result.flags = f.restricted ? NEOS_RT_RESTRICTED : 0u;
                return result;
            }
        std::fprintf(stderr, "C runtime: %s is not in the C library's table\n", aName);
        std::abort();
    }

    address allocate(std::uint64_t aSize)
    {
        return the_heap().allocate(aSize == 0u ? 1u : aSize);
    }

    address allocate_aligned(std::uint64_t aAlignment, std::uint64_t aSize)
    {
        if (aAlignment <= 16u)
            return allocate(aSize);
        auto const block = allocate(aSize + aAlignment + 16u);
        if (block == 0u)
            return 0u;
        auto const aligned = (block + 16u + aAlignment - 1u) & ~(aAlignment - 1u);
        *at<std::uint64_t>(aligned - 16u) = aSize | AlignedFlag;
        *at<std::uint64_t>(aligned - 8u) = block;
        return aligned;
    }

    void deallocate(address aAddress)
    {
        the_heap().deallocate(aAddress);
    }

    std::uint64_t allocated_size(address aAddress)
    {
        return aAddress == 0u ? 0u : the_heap().size_of(aAddress);
    }

    address reallocate(address aAddress, std::uint64_t aSize)
    {
        if (aAddress == 0u)
            return allocate(aSize);
        auto const old = allocated_size(aAddress);
        if (aSize <= old && aSize + 4096u > old)
            return aAddress;
        auto const block = allocate(aSize);
        if (block == 0u)
            return 0u;
        std::memcpy(at<std::uint8_t>(block), at<std::uint8_t>(aAddress), std::min(old, aSize));
        deallocate(aAddress);
        return block;
    }

    address copy_string(std::string const& aText)
    {
        auto const block = allocate(aText.size() + 1u);
        if (block != 0u)
            std::memcpy(at<char>(block), aText.c_str(), aText.size() + 1u);
        return block;
    }

    std::size_t std_write(int aStream, char const* aData, std::size_t aCount)
    {
        if (gHost != nullptr && gHost->version >= 9u)
            return gHost->std_write(aStream, aData, aCount);
        return std::fwrite(aData, 1u, aCount, aStream == 2 ? stderr : stdout);
    }

    std::size_t std_read(int aStream, char* aBuffer, std::size_t aCount)
    {
        if (gHost != nullptr && gHost->version >= 9u)
            return gHost->std_read(aStream, aBuffer, aCount);
        return std::fread(aBuffer, 1u, aCount, stdin);
    }

    int std_getc(int aStream)
    {
        if (gHost != nullptr && gHost->version >= 9u)
            return gHost->std_getc(aStream);
        return std::fgetc(stdin);
    }

    int std_ungetc(int aCharacter, int aStream)
    {
        if (gHost != nullptr && gHost->version >= 9u)
            return gHost->std_ungetc(aCharacter, aStream);
        return std::ungetc(aCharacter, stdin);
    }

    int std_flush(int aStream)
    {
        if (gHost != nullptr && gHost->version >= 9u)
            return gHost->std_flush(aStream);
        return std::fflush(aStream == 2 ? stderr : stdout);
    }

    std::uint32_t function_of(address aDescriptor)
    {
        return static_cast<std::uint32_t>(*at<std::uint64_t>(aDescriptor));
    }

    std::int64_t call_function(address aDescriptor, std::initializer_list<std::int64_t> aArguments, neos_rt_type aResult, std::initializer_list<neos_rt_type> aTypes)
    {
        if (gHost == nullptr || aDescriptor == 0u)
            return 0;
        neos_rt_value arguments[NEOS_RT_MAX_PARAMETERS]{};
        neos_rt_type types[NEOS_RT_MAX_PARAMETERS]{};
        unsigned count = 0u;
        auto type = aTypes.begin();
        for (auto a : aArguments)
        {
            types[count] = type != aTypes.end() ? *type++ : NEOS_RT_I64;
            if (types[count] == NEOS_RT_I32)
                arguments[count].i32 = static_cast<int>(a);
            else
                arguments[count].i64 = a;
            ++count;
        }
        neos_rt_value result{};
        auto const error = gHost->call(function_of(aDescriptor), count, types, arguments, aResult, &result);
        if (error != nullptr)
        {
            trap(error);
            return 0;
        }
        return aResult == NEOS_RT_I32 ? result.i32 : aResult == NEOS_RT_VOID ? 0 : result.i64;
    }

    void trap(char const* aMessage)
    {
        if (gHost != nullptr && gHost->trap != nullptr)
            gHost->trap(aMessage);
    }

    namespace
    {
        // --- the runtime's own functions (c::rt.*)

        void rt_init(std::int64_t aStaticEnd, std::int32_t aSpGlobal, std::int32_t aLimitGlobal)
        {
            // (the static data committed; the stack (NEOS_C_STACK bytes, by default 8 MB) after it, the heap after that)
            gEnded = false;
            the_region().static_data(static_cast<std::uint64_t>(aStaticEnd));
            std::uint64_t stackSize = std::uint64_t{ 8u } << 20u;
            if (auto const s = std::getenv("NEOS_C_STACK"))
                stackSize = std::max<std::uint64_t>(std::strtoull(s, nullptr, 0), std::uint64_t{ 256u } << 10u);
            auto const stack = the_region().take(stackSize);
            if (gHost != nullptr && gHost->version >= 10u)
            {
                if (auto* sp = gHost->global(static_cast<unsigned>(aSpGlobal)))
                    *sp = stack + stackSize;
                if (auto* limit = gHost->global(static_cast<unsigned>(aLimitGlobal)))
                    *limit = stack + 4096u;
            }
            errno_value() = 0;
            streams_init();
        }

        void rt_init_data(std::int64_t aAddress, char const* aHex)
        {
            auto* p = at<std::uint8_t>(static_cast<address>(aAddress));
            auto const digit = [](char c) -> std::uint8_t { return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
            for (; aHex[0] != '\0' && aHex[1] != '\0'; aHex += 2)
                *p++ = static_cast<std::uint8_t>((digit(aHex[0]) << 4u) | digit(aHex[1]));
        }

        std::int32_t rt_argc()
        {
            if (gHost == nullptr || gHost->arguments == nullptr)
                return 1;
            return static_cast<std::int32_t>(gHost->arguments(nullptr, 0u));
        }

        std::int64_t rt_argv()
        {
            // (argv: the program's arguments (its path first), then a null pointer)
            std::vector<char const*> arguments;
            if (gHost != nullptr && gHost->arguments != nullptr)
            {
                arguments.resize(gHost->arguments(nullptr, 0u));
                gHost->arguments(arguments.data(), static_cast<unsigned>(arguments.size()));
            }
            if (arguments.empty())
                arguments.push_back("a.out");
            gArgc = static_cast<int>(arguments.size());
            gArgv = allocate((arguments.size() + 1u) * 8u);
            for (std::size_t i = 0u; i < arguments.size(); ++i)
                at<std::uint64_t>(gArgv)[i] = copy_string(arguments[i]);
            at<std::uint64_t>(gArgv)[arguments.size()] = 0u;
            return static_cast<std::int64_t>(gArgv);
        }

        std::int64_t rt_envp()
        {
            std::vector<std::string> environment;
#if defined(_WIN32)
            // (the environment's strings as UTF-8 (the execution character set); those that begin with '=' are the shell's
            // per-drive current directories)
            if (auto* block = ::GetEnvironmentStringsW())
            {
                for (wchar_t const* p = block; *p != L'\0'; p += std::wcslen(p) + 1u)
                {
                    if (*p == L'=')
                        continue;
                    int const length = static_cast<int>(std::wcslen(p));
                    int const size = ::WideCharToMultiByte(CP_UTF8, 0, p, length, nullptr, 0, nullptr, nullptr);
                    std::string utf8(static_cast<std::size_t>(size), '\0');
                    ::WideCharToMultiByte(CP_UTF8, 0, p, length, utf8.data(), size, nullptr, nullptr);
                    environment.push_back(std::move(utf8));
                }
                ::FreeEnvironmentStringsW(block);
            }
#else
            for (char** p = environ; p != nullptr && *p != nullptr; ++p)
                environment.emplace_back(*p);
#endif
            gEnvp = allocate((environment.size() + 1u) * 8u);
            for (std::size_t i = 0u; i < environment.size(); ++i)
                at<std::uint64_t>(gEnvp)[i] = copy_string(environment[i]);
            return static_cast<std::int64_t>(gEnvp);
        }

        std::int32_t rt_exit_prepare(std::int32_t aStatus)
        {
            // (exit: the functions that atexit registered (in the reverse order), then the streams flushed and closed)
            if (!gEnded)
            {
                gEnded = true;
                exit_handlers(false);
                streams_end();
            }
            return aStatus;
        }

        std::int32_t rt_quick_exit_prepare(std::int32_t aStatus)
        {
            if (!gEnded)
            {
                gEnded = true;
                exit_handlers(true);
                streams_end();
            }
            return aStatus;
        }

        std::int32_t rt_exit_flush(std::int32_t aStatus)
        {
            // (_Exit: the streams flushed (as most implementations do), no handlers)
            if (!gEnded)
            {
                gEnded = true;
                streams_end();
            }
            return aStatus;
        }

        void rt_stack_overflow()
        {
            trap("C runtime: stack overflow");
        }

        // (128-bit integers, two's complement (portable: no __int128))
        struct wide
        {
            std::uint64_t low = 0u;
            std::uint64_t high = 0u;
            static wide of(std::int64_t aValue, bool aSigned)
            {
                return wide{ static_cast<std::uint64_t>(aValue), aSigned && aValue < 0 ? ~std::uint64_t{} : 0u };
            }
            bool negative() const { return (high >> 63u) != 0u; }
            wide operator+(wide const& b) const { wide r{ low + b.low, high + b.high }; if (r.low < low) ++r.high; return r; }
            wide operator-() const { wide r{ ~low, ~high }; return r + wide{ 1u, 0u }; }
            wide operator-(wide const& b) const { return *this + -b; }
            bool operator<(wide const& b) const
            {
                if (negative() != b.negative())
                    return negative();
                return high != b.high ? high < b.high : low < b.low;
            }
        };

        wide multiply(wide a, wide b, bool& aOverflow)
        {
            // (the product of operands that are 64-bit values (sign or zero extended): its magnitude's 128 bits, then its sign)
            bool const negative = a.negative() != b.negative();
            if (a.negative())
                a = -a;
            if (b.negative())
                b = -b;
            auto const x = a.low, y = b.low;
            if (a.high != 0u || b.high != 0u)
                aOverflow = true;
            std::uint64_t const x0 = x & 0xFFFFFFFFu, x1 = x >> 32u, y0 = y & 0xFFFFFFFFu, y1 = y >> 32u;
            std::uint64_t const p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
            std::uint64_t const middle = (p00 >> 32u) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
            wide r{ (p00 & 0xFFFFFFFFu) | (middle << 32u), p11 + (p01 >> 32u) + (p10 >> 32u) + (middle >> 32u) };
            if (r.negative())
                aOverflow = true;
            return negative ? -r : r;
        }

        std::int32_t rt_ckd(std::int32_t aOperation, std::int32_t aTypes, std::int64_t aA, std::int64_t aB, std::int64_t aResult)
        {
            // (ckd_add, ckd_sub, ckd_mul: the exact result, stored as the result's type wraps it; 1 if it overflowed)
            auto const width = [&](int aShift) { return (aTypes >> aShift) & 0xFF; };
            auto const isSigned = [&](int aShift) { return ((aTypes >> aShift) & 0x100) != 0; };
            auto const a = wide::of(aA, isSigned(0));
            auto const b = wide::of(aB, isSigned(9));
            auto const wr = width(18);
            bool const sr = isSigned(18);
            bool overflow = false;
            wide r;
            switch (aOperation)
            {
            case 0: r = a + b; break;
            case 1: r = a - b; break;
            default: r = multiply(a, b, overflow); break;
            }
            wide low, high;
            if (sr)
            {
                low = -wide::of(static_cast<std::int64_t>(std::uint64_t{ 1 } << (wr - 1)), false);
                high = wide::of(static_cast<std::int64_t>(std::uint64_t{ 1 } << (wr - 1)), false) - wide{ 1u, 0u };
                if (wr == 64)
                {
                    low = wide{ std::uint64_t{ 1 } << 63u, ~std::uint64_t{} };
                    high = wide{ ~(std::uint64_t{ 1 } << 63u), 0u };
                }
            }
            else
            {
                low = wide{};
                high = wr == 64 ? wide{ ~std::uint64_t{}, 0u } : wide{ (std::uint64_t{ 1 } << wr) - 1u, 0u };
            }
            if (r < low || high < r)
                overflow = true;
            auto const bits = r.low;
            auto* p = at<std::uint8_t>(static_cast<address>(aResult));
            for (int i = 0; i < (wr + 7) / 8 && i < 8; ++i)
                p[i] = static_cast<std::uint8_t>(bits >> (8 * i));
            return overflow ? 1 : 0;
        }

        function_table const sRuntimeFunctions
        {
            { "rt.init", NEOS_RT_FUNCTION(rt_init), NEOS_RT_VOID, 3u, { NEOS_RT_I64, NEOS_RT_I32, NEOS_RT_I32 }, 0u },
            { "rt.init_data", NEOS_RT_FUNCTION(rt_init_data), NEOS_RT_VOID, 2u, { NEOS_RT_I64, NEOS_RT_STRING }, 0u },
            { "rt.argc", NEOS_RT_FUNCTION(rt_argc), NEOS_RT_I32, 0u, {}, 0u },
            { "rt.argv", NEOS_RT_FUNCTION(rt_argv), NEOS_RT_I64, 0u, {}, 0u },
            { "rt.envp", NEOS_RT_FUNCTION(rt_envp), NEOS_RT_I64, 0u, {}, 0u },
            { "rt.exit_prepare", NEOS_RT_FUNCTION(rt_exit_prepare), NEOS_RT_I32, 1u, { NEOS_RT_I32 }, 0u },
            { "rt.quick_exit_prepare", NEOS_RT_FUNCTION(rt_quick_exit_prepare), NEOS_RT_I32, 1u, { NEOS_RT_I32 }, 0u },
            { "rt.exit_flush", NEOS_RT_FUNCTION(rt_exit_flush), NEOS_RT_I32, 1u, { NEOS_RT_I32 }, 0u },
            { "rt.stack_overflow", NEOS_RT_FUNCTION(rt_stack_overflow), NEOS_RT_VOID, 0u, {}, 0u },
            { "rt.ckd", NEOS_RT_FUNCTION(rt_ckd), NEOS_RT_I32, 5u, { NEOS_RT_I32, NEOS_RT_I32, NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I64 }, 0u },
            library("__neos_exit_prepare", NEOS_RT_FUNCTION(rt_exit_prepare)),
            library("__neos_quick_exit_prepare", NEOS_RT_FUNCTION(rt_quick_exit_prepare)),
            library("__neos_exit_flush", NEOS_RT_FUNCTION(rt_exit_flush))
        };
    }
}
