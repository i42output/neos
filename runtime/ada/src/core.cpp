/*
  core.cpp

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

// The Ada runtime's core: the host, the function tables, the standard streams and the memory (see ada.hpp)

#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "core.hpp"
#include <neos/language/ada.hpp>

namespace neos::ada_runtime
{
    namespace
    {
        neos_rt_host const* gHost = nullptr;

        // the region: address space reserved once (pages committed as they are used): the range of the shared memory (see 
        // neos_rt_host::shared_memory) from Origin (see neos::language::ada::layout); offsets below FirstAddress are not used (0 is null)
        constexpr std::uint64_t Origin = language::ada::layout::Origin;
        constexpr std::uint64_t RegionSize = language::ada::layout::Size;
        constexpr std::uint64_t Granule = std::uint64_t{ 1u } << 16u;
        constexpr std::uint64_t FirstAddress = language::ada::layout::FirstAddress;

        class region
        {
        public:
            region()
            {
                // (the shared memory's range, so that the code of other languages that share it (e.g. C's: Annex B) has the same 
                // addresses for Ada's objects)
                if (gHost != nullptr && gHost->version >= 18u && gHost->shared_memory != nullptr)
                    iBase = static_cast<std::uint8_t*>(gHost->shared_memory(Origin, RegionSize));
                if (iBase != nullptr)
                    iSize = NEOS_RT_SHARED_MEMORY_SIZE;
                else
                {
                    // (else a reservation of its own: the range's offsets are its)
#if defined(_WIN32)
                    auto* const p = static_cast<std::uint8_t*>(::VirtualAlloc(nullptr, RegionSize, MEM_RESERVE, PAGE_NOACCESS));
#else
                    // (reserved inaccessible: blocks are made accessible as they are committed (so that the reservation does not count 
                    // as the process's data))
                    auto const m = ::mmap(nullptr, RegionSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                    auto* const p = m == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(m);
#endif
                    if (p == nullptr)
                    {
                        std::fputs("Ada runtime: cannot reserve the memory's address space\n", stderr);
                        std::abort();
                    }
                    iBase = reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uintptr_t>(p) - Origin);
                    iSize = Origin + RegionSize;
                }
                iTop = FirstAddress;
            }
        public:
            std::uint8_t* base() const { return iBase; }
            std::uint64_t size() const { return iSize; }     ///< (the memory's: its offsets are below it)
            // (a block of whole granules, committed)
            address take(std::uint64_t aSize)
            {
                std::lock_guard lock{ iMutex };
                aSize = (aSize + Granule - 1u) & ~(Granule - 1u);
                // (a free block that fits (first fit))
                for (auto i = iFree.begin(); i != iFree.end(); ++i)
                    if (i->second >= aSize)
                    {
                        auto const result = i->first;
                        auto const rest = i->second - aSize;
                        iFree.erase(i);
                        if (rest != 0u)
                            iFree.emplace(result + aSize, rest);
                        return result;
                    }
                if (iTop + aSize > Origin + RegionSize)
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
                // (coalesced with its neighbours)
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
        private:
            void commit(std::uint64_t aOffset, std::uint64_t aSize)
            {
#if defined(_WIN32)
                if (::VirtualAlloc(iBase + aOffset, aSize, MEM_COMMIT, PAGE_READWRITE) == nullptr)
                {
                    std::fputs("Ada runtime: cannot commit memory\n", stderr);
                    std::abort();
                }
#else
                if (::mprotect(iBase + aOffset, aSize, PROT_READ | PROT_WRITE) != 0)
                {
                    std::fputs("Ada runtime: cannot commit memory\n", stderr);
                    std::abort();
                }
#endif
            }
        private:
            std::mutex iMutex;
            std::uint8_t* iBase = nullptr;
            std::uint64_t iSize = 0u;
            std::uint64_t iTop = 0u;
            std::map<address, std::uint64_t> iFree;
        };

        region& the_region()
        {
            static region sRegion;
            return sRegion;
        }

        // the heap: size classes of small blocks (free lists), larger blocks from the region; a block's size is in the 16 bytes 
        // before it
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
                    std::memset(at<std::uint8_t>(block), 0, size);
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
                std::lock_guard lock{ iMutex };
                auto const size = *at<std::uint64_t>(aBlock - 16);
                if (size <= MaxSmall)
                    iFree[size / 16u].push_back(aBlock);
                else
                    the_region().give(aBlock - 16, size + 16u);
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
        // (the memory: the external memory that generated code uses (fixed: its address and size never change))
        if (gHost != nullptr && gHost->version >= 14u && gHost->external_memory_at != nullptr)
            gHost->external_memory_at(language::ada::layout::Memory, memory(), the_region().size(), NEOS_RT_EXTERNAL_MEMORY_FIXED);
    }

    function_table::function_table(std::initializer_list<neos_rt_function> aFunctions)
    {
        functions().insert(functions().end(), aFunctions.begin(), aFunctions.end());
    }

    std::vector<neos_rt_function>& functions()
    {
        static std::vector<neos_rt_function> sFunctions;
        return sFunctions;
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

    address allocate(std::uint64_t aSize)
    {
        return the_heap().allocate(aSize == 0u ? 1u : aSize);
    }

    void deallocate(address aAddress)
    {
        the_heap().deallocate(aAddress);
    }

    address allocate_segment(std::uint64_t aSize)
    {
        return the_region().take(aSize);
    }

    void deallocate_segment(address aAddress, std::uint64_t aSize)
    {
        the_region().give(aAddress, aSize);
    }

    void program_ended()
    {
        std_flush(1);
    }
}
