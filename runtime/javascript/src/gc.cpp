/*
  gc.cpp

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

// The garbage collector: the heap's memory (pages of cells), marking (from the roots: see gc.hpp), sweeping, and the traces of the
// cells that js.hpp defines

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <mutex>
#include <neos/language/javascript.hpp>
#include "js.hpp"
#include "gc.hpp"
#include "api.hpp"

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
#include <pthread.h>
#endif

namespace neos::javascript_runtime
{
    std::byte* gRegionBase = nullptr;
    thread_local heap* tHeap = nullptr;
    sole_isolate gSoleIsolate;

    namespace
    {
        // --- the cells' memory: pages (of one size of slot each, or a large cell's) of one reserved region of address space, so that
        // whether a word points into a cell is known from the page it points into

        constexpr std::size_t PageShift = 16u;
        constexpr std::size_t PageSize = std::size_t{ 1u } << PageShift;
        constexpr std::size_t RegionSize = std::size_t{ 32u } << 30u;      // (address space only: pages are committed when used)
        constexpr std::size_t PageCount = RegionSize / PageSize;
        // (address space after the region, never committed: compiled code's accesses of a cell (a handle's offset and a field's) are 
        // within the region and this, so that an optimizing tier can prove them within the memory (and not check them))
        constexpr std::size_t GuardSize = std::size_t{ 1u } << 20u;
        constexpr std::size_t MaxSmall = 4096u;
        static_assert(RegionSize >> heap::HandleShift <= std::size_t{ 1u } << 32u);     // (a handle is 32 bits)
        static_assert(neos::language::javascript::layout::HandleShift == heap::HandleShift);

        constexpr std::uint32_t SizeClasses[] =
        {
            16u, 32u, 48u, 64u, 80u, 96u, 112u, 128u, 144u, 160u, 176u, 192u, 208u, 224u, 240u, 256u,
            320u, 384u, 448u, 512u, 640u, 768u, 896u, 1024u, 1280u, 1536u, 1792u, 2048u, 2560u, 3072u, 4096u
        };
        constexpr std::size_t ClassCount = std::size(SizeClasses);

        struct page_info
        {
            std::uint32_t slotSize = 0u;    ///< (0: not in use)
            std::uint32_t first = 0u;       ///< (a large cell's: its first page)
            std::size_t largeSize = 0u;     ///< (a large cell's: its size (0: the page has slots))
            std::uint32_t owner = 0u;       ///< (the isolate whose page it is (see cell_memory::owner): 0: none)
            bool raw = false;               ///< (storage that is not cells (see region_vector))
            bool empty = false;             ///< (a page of small cells that has none (its slots are not in a free list): see cell_memory::empty)
        };

        // (the region (all of it: its address and size never change; compiled code reads only the pages that are committed) is the
        // external memory that compiled code reads (see neos::language::javascript::layout))
        void publish_region(std::byte* aBase, std::size_t aSize)
        {
            auto const* h = host();
            if (h != nullptr && h->version >= 14u && h->external_memory_at != nullptr)
                h->external_memory_at(neos::language::javascript::layout::Memory, aBase, aSize, NEOS_RT_EXTERNAL_MEMORY_FIXED);
        }

        void commit(void* aAddress, std::size_t aSize)
        {
#if defined(_WIN32)
            if (::VirtualAlloc(aAddress, aSize, MEM_COMMIT, PAGE_READWRITE) == nullptr)
                throw std::bad_alloc{};
#else
            if (::mprotect(aAddress, aSize, PROT_READ | PROT_WRITE) != 0)
                throw std::bad_alloc{};
#endif
        }
        void decommit(void* aAddress, std::size_t aSize) noexcept
        {
#if defined(_WIN32)
            ::VirtualFree(aAddress, aSize, MEM_DECOMMIT);
#else
            ::madvise(aAddress, aSize, MADV_DONTNEED);
#endif
        }
        std::byte* reserve(std::size_t aSize, std::size_t aAlignment)
        {
            std::byte* result = nullptr;
#if defined(_WIN32)
            (void)aAlignment;   // (VirtualAlloc's are aligned to 64 KB)
            result = static_cast<std::byte*>(::VirtualAlloc(nullptr, aSize, MEM_RESERVE, PAGE_NOACCESS));
#else
            auto const address = ::mmap(nullptr, aSize + aAlignment, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (address != MAP_FAILED)
                result = reinterpret_cast<std::byte*>((reinterpret_cast<std::uintptr_t>(address) + aAlignment - 1u) & ~(aAlignment - 1u));
#endif
            if (result == nullptr)
            {
                std::fputs("JavaScript runtime: cannot reserve the heap's address space\n", stderr);
                std::abort();
            }
            return result;
        }

        // (the region: reserved once, for the process (its address and size never change); its pages are the isolates' (each page's
        // information says whose it is: an isolate's collector uses only its own); pages are taken from it (the ones that were given
        // back first) and given back (an isolate's large cell that is freed, an isolate's pages when it ends))
        class region
        {
        public:
            static region& instance()
            {
                static region sRegion;
                return sRegion;
            }
            region()
            {
                iBase = reserve(RegionSize + GuardSize, PageSize);
                gRegionBase = iBase;
                iPages = reinterpret_cast<page_info*>(reserve(PageCount * sizeof(page_info), PageSize));
                commit_information(1u);
                // (the first page is reserved (offset 0 (handle 0) is no cell))
                iPages[0].raw = true;
            }
        public:
            std::byte* base() const { return iBase; }
            page_info* pages() const { return iPages; }
            std::size_t high() const { return iHigh.load(std::memory_order_acquire); }  ///< (the pages that have been used: [0, high))
            std::size_t take(std::size_t aCount)
            {
                std::lock_guard<std::mutex> lock{ iMutex };
                for (auto r = iFree.begin(); r != iFree.end(); ++r)
                    if (r->second >= aCount)
                    {
                        auto const page = r->first;
                        auto const rest = r->second - aCount;
                        iFree.erase(r);
                        if (rest != 0u)
                            iFree.emplace(page + aCount, rest);
                        commit(iBase + page * PageSize, aCount * PageSize);
                        return page;
                    }
                auto const page = iHigh.load(std::memory_order_relaxed);
                if ((page + aCount) * PageSize > RegionSize)
                    throw std::bad_alloc{};
                commit(iBase + page * PageSize, aCount * PageSize);
                commit_information(page + aCount);
                iHigh.store(page + aCount, std::memory_order_release);
                return page;
            }
            void give_back(std::size_t aPage, std::size_t aCount) noexcept
            {
                for (auto p = aPage; p < aPage + aCount; ++p)
                    iPages[p] = page_info{};
                decommit(iBase + aPage * PageSize, aCount * PageSize);
                std::lock_guard<std::mutex> lock{ iMutex };
                try
                {
                    // (merged with the runs next to it)
                    auto page = aPage;
                    auto count = aCount;
                    if (auto const next = iFree.find(page + count); next != iFree.end())
                    {
                        count += next->second;
                        iFree.erase(next);
                    }
                    if (auto previous = iFree.lower_bound(page); previous != iFree.begin())
                    {
                        --previous;
                        if (previous->first + previous->second == page)
                        {
                            page = previous->first;
                            count += previous->second;
                            iFree.erase(previous);
                        }
                    }
                    iFree.emplace(page, count);
                }
                catch (...)
                {
                    // (not reused)
                }
            }
            void publish() const { publish_region(iBase, RegionSize + GuardSize); }
        private:
            void commit_information(std::size_t aPages)
            {
                // (the pages' information (its address space is reserved for all of them): committed as pages are used)
                constexpr std::size_t chunk = 32768u;   // (pages' information committed at a time: a multiple of the pages' size, in bytes)
                static_assert(chunk * sizeof(page_info) % PageSize == 0u && PageCount % chunk == 0u);
                auto const wanted = (aPages + chunk - 1u) / chunk * chunk;
                if (wanted <= iInformation)
                    return;
                commit(reinterpret_cast<std::byte*>(iPages + iInformation), (wanted - iInformation) * sizeof(page_info));
                iInformation = wanted;
            }
        private:
            std::byte* iBase = nullptr;
            page_info* iPages = nullptr;
            std::size_t iInformation = 0u;
            std::atomic<std::size_t> iHigh = 1u;
            std::mutex iMutex;
            std::map<std::size_t, std::size_t> iFree;   ///< (pages given back: first, count)
        };

        std::uint32_t new_owner()
        {
            static std::atomic<std::uint32_t> sOwners = 0u;
            for (;;)
                if (auto const owner = ++sOwners; owner != 0u)
                    return owner;
        }

        class cell_memory
        {
        public:
            cell_memory() : iRegion{ region::instance() }, iBase{ iRegion.base() }, iPages{ iRegion.pages() }, iOwner{ new_owner() }
            {
                static_assert(ClassCount <= allocation_state::Classes);
                for (std::size_t c = 0u, size = 0u; size <= MaxSmall; size += 16u)
                {
                    while (SizeClasses[c] < size)
                        ++c;
                    iClassOf[size / 16u] = static_cast<std::uint8_t>(c);
                }
                // (the allocation state (that compiled code uses): in the region)
                iState = ::new (allocate(sizeof(allocation_state), true)) allocation_state{};
                iState->plainArrays[0] = ~std::uint64_t{};
                auto* const table = allocate(MegamorphicEntries * sizeof(megamorphic_entry), true);
                std::memset(table, 0, MegamorphicEntries * sizeof(megamorphic_entry));
                iState->megamorphic = static_cast<std::uint64_t>(static_cast<std::byte*>(table) - iBase);
            }
            ~cell_memory()
            {
                // (the isolate has ended: its pages are given back)
                for (std::size_t page = 1u, high = iRegion.high(); page < high;)
                {
                    if (iPages[page].owner != iOwner)
                    {
                        ++page;
                        continue;
                    }
                    auto const first = page;
                    while (page < high && iPages[page].owner == iOwner)
                        ++page;
                    iRegion.give_back(first, page - first);
                }
            }
            cell_memory(cell_memory const&) = delete;
            cell_memory& operator=(cell_memory const&) = delete;
        public:
            // (a cell's memory, or (aRaw) storage that is not a cell)
            void* allocate(std::size_t aSize, bool aRaw = false)
            {
                if (aSize > MaxSmall)
                    return allocate_large(aSize, aRaw);
                auto const ci = iClassOf[(aSize + 15u) / 16u];
                if (!aRaw)
                {
                    // (cells: a slot that was freed (the free list's first: see allocation_state); else the next of the size class's run 
                    // of free slots (bump: compiled code allocates from it too); else the next run: of a page that the last collection 
                    // swept, else one that it has not swept yet (swept now: its runs), else a page that has no cells (its size class's, 
                    // else a new one: its slots are a run))
                    auto& head = iState->freeLists[ci];
                    if (head != 0u)
                    {
                        auto* const result = iBase + head;
                        head = *reinterpret_cast<std::uint64_t const*>(result);
                        return result;
                    }
                    auto const slotSize = SizeClasses[ci];
                    for (;;)
                    {
                        auto& bump = iState->bump[ci];
                        if (bump != 0u && bump + slotSize <= iState->bumpLimit[ci])
                        {
                            auto* const result = iBase + bump;
                            bump += slotSize;
                            return result;
                        }
                        if (!iRuns[ci].empty())
                        {
                            auto const run = iRuns[ci].back();
                            iRuns[ci].pop_back();
                            iState->bump[ci] = run.first;
                            iState->bumpLimit[ci] = run.second;
                            continue;
                        }
                        if (!iUnswept[ci].empty())
                        {
                            auto const page = iUnswept[ci].back();
                            iUnswept[ci].pop_back();
                            iSweeper(iSweeperData, page);
                            continue;
                        }
                        std::size_t page;
                        if (!iEmpty[ci].empty())
                        {
                            page = iEmpty[ci].back();
                            iEmpty[ci].pop_back();
                            iPages[page].empty = false;
                        }
                        else
                        {
                            page = iRegion.take(1u);
                            iPages[page] = page_info{ slotSize, 0u, 0u, iOwner, false };
                        }
                        auto const start = static_cast<std::uint64_t>(page_address(page) - iBase);
                        iState->bump[ci] = start;
                        iState->bumpLimit[ci] = start + (PageSize / slotSize) * slotSize;
                    }
                }
                auto* const classes = iRawClasses;
                auto& c = classes[ci];
                if (c.freeList != nullptr)
                {
                    // (the next one is fetched now: it is the next allocation's (a free slot is usually not in the cache))
                    auto* const result = c.freeList;
                    c.freeList = *static_cast<void**>(result);
#if defined(__GNUC__) || defined(__clang__)
                    __builtin_prefetch(c.freeList, 1);
#endif
                    return result;
                }
                auto const slotSize = SizeClasses[&c - classes];
                if (c.bump == c.bumpEnd)
                {
                    auto const page = iRegion.take(1u);
                    iPages[page] = page_info{ slotSize, 0u, 0u, iOwner, aRaw };
                    c.bump = page_address(page);
                    c.bumpEnd = c.bump + (PageSize / slotSize) * slotSize;
                }
                auto* const result = c.bump;
                c.bump += slotSize;
                return result;
            }
            void release(void* aMemory, bool aReuse) noexcept
            {
                auto const page = page_of(aMemory);
                auto& info = iPages[page];
                if (info.largeSize != 0u)
                {
                    auto const count = (info.largeSize + PageSize - 1u) / PageSize;
                    if (aReuse)
                        iRegion.give_back(page, count);
                    else
                    {
                        for (std::size_t p = page; p < page + count; ++p)
                            iPages[p] = page_info{};
                        decommit(page_address(page), count * PageSize);
                    }
                    return;
                }
                if (!aReuse)
                    return;
                if (!info.raw)
                {
                    free_slot(aMemory);
                    return;
                }
                auto& c = iRawClasses[iClassOf[info.slotSize / 16u]];
                *static_cast<void**>(aMemory) = c.freeList;
                c.freeList = aMemory;
            }
            // (lazy sweeping: a collection's pages of small cells, swept later (aSweeper(page)) as their size classes' cells are
            // allocated (or all of them by finish_sweeping))
            void set_sweeper(void (*aSweeper)(void*, std::size_t), void* aData)
            {
                iSweeper = aSweeper;
                iSweeperData = aData;
            }
            void begin_sweeping()
            {
                for (std::size_t ci = 0u; ci < ClassCount; ++ci)
                {
                    // (the runs' slots (free) are found again when their pages are swept)
                    iState->freeLists[ci] = 0u;
                    iState->bump[ci] = 0u;
                    iState->bumpLimit[ci] = 0u;
                    iRuns[ci].clear();
                    iUnswept[ci].clear();
                }
                for (auto page = iRegion.high(); page-- > 0u;)
                {
                    auto const& info = iPages[page];
                    if (info.owner == iOwner && info.slotSize != 0u && !info.raw && info.largeSize == 0u && !info.empty)
                        iUnswept[iClassOf[info.slotSize / 16u]].push_back(page);
                }
            }
            void finish_sweeping()
            {
                for (std::size_t ci = 0u; ci < ClassCount; ++ci)
                    while (!iUnswept[ci].empty())
                    {
                        auto const page = iUnswept[ci].back();
                        iUnswept[ci].pop_back();
                        iSweeper(iSweeperData, page);
                    }
            }
            // (a page's slots (of cells (live, free or not used yet)), in descending address order (aVisit(slot)))
            template <typename Visit>
            void for_each_slot_of(std::size_t aPage, Visit aVisit)
            {
                auto const& info = iPages[aPage];
                auto* const start = page_address(aPage);
                for (auto i = PageSize / info.slotSize; i-- > 0u;)
                    aVisit(start + i * info.slotSize);
            }
            // (a page of small cells: its first slot and its slots' size)
            void page_slots(std::size_t aPage, std::byte*& aStart, std::uint32_t& aSlotSize)
            {
                auto const& info = iPages[aPage];
                aStart = page_address(aPage);
                aSlotSize = info.slotSize;
            }
            // (a swept page's runs of free slots (their region offsets: [first, end)), in ascending order: they are allocated from
            // (the first first))
            void add_runs(std::uint32_t aSlotSize, std::span<std::pair<std::uint64_t, std::uint64_t> const> aRuns)
            {
                auto& runs = iRuns[iClassOf[aSlotSize / 16u]];
                for (auto r = aRuns.rbegin(); r != aRuns.rend(); ++r)
                    runs.push_back(*r);
            }
            // (a free slot of a cells' page: its size class's)
            void free_slot(void* aSlot)
            {
                auto& head = iState->freeLists[iClassOf[iPages[page_of(aSlot)].slotSize / 16u]];
                *static_cast<std::uint64_t*>(aSlot) = head;
                head = static_cast<std::uint64_t>(static_cast<std::byte*>(aSlot) - iBase);
            }
            allocation_state& state() const { return *iState; }
            // (a page of small cells that its sweep found has none: not swept again (until it has cells: see allocate))
            void empty(std::size_t aPage)
            {
                auto& info = iPages[aPage];
                info.empty = true;
                iEmpty[iClassOf[info.slotSize / 16u]].push_back(aPage);
            }
            std::uint32_t size_class(std::size_t aSize) const { return iClassOf[(aSize + 15u) / 16u]; }
            static std::uint32_t class_size(std::uint32_t aClass) { return SizeClasses[aClass]; }
            // (large cells' pages (their first), in descending address order (aVisit(cell)))
            template <typename Visit>
            void for_each_large_descending(Visit aVisit)
            {
                for (auto page = iRegion.high(); page-- > 0u;)
                {
                    auto const& info = iPages[page];
                    if (info.owner == iOwner && info.slotSize != 0u && !info.raw && info.largeSize != 0u && info.first == page)
                        aVisit(page_address(page));
                }
            }
            // (the slot that begins at an offset in the region (null if none: it is not the start of a slot of a page of cells))
            std::byte* slot_starting_at(std::size_t aOffset) const
            {
                auto const page = aOffset >> PageShift;
                if (page >= iRegion.high())
                    return nullptr;
                auto const& info = iPages[page];
                if (info.owner != iOwner || info.slotSize == 0u || info.raw)
                    return nullptr;
                auto const offset = aOffset & (PageSize - 1u);
                if (info.largeSize != 0u)
                    return info.first == page && offset == 0u ? iBase + aOffset : nullptr;
                return offset % info.slotSize == 0u && offset + info.slotSize <= PageSize ? iBase + aOffset : nullptr;
            }
            bool raw(void const* aMemory) const { return iPages[page_of(aMemory)].raw; }   ///< (storage that is not cells')
            std::size_t size_of(void const* aMemory) const
            {
                auto const& info = iPages[page_of(aMemory)];
                return info.largeSize != 0u ? info.largeSize : info.slotSize;
            }
            // (the slot that an address is in (null if it is not in a page of cells))
            std::byte const* slot_at(std::uintptr_t aAddress) const
            {
                auto const base = reinterpret_cast<std::uintptr_t>(iBase);
                if (aAddress < base)
                    return nullptr;
                auto const page = (aAddress - base) >> PageShift;
                if (page >= iRegion.high())
                    return nullptr;
                auto const& info = iPages[page];
                if (info.owner != iOwner || info.slotSize == 0u || info.raw)
                    return nullptr;
                if (info.largeSize != 0u)
                {
                    auto const* const start = page_address(info.first);
                    return aAddress < reinterpret_cast<std::uintptr_t>(start) + info.largeSize ? start : nullptr;
                }
                auto const offset = (aAddress - base) & (PageSize - 1u);
                auto const index = offset / info.slotSize;
                if ((index + 1u) * info.slotSize > PageSize)
                    return nullptr;
                return page_address(page) + index * info.slotSize;
            }
            std::uintptr_t begin() const { return reinterpret_cast<std::uintptr_t>(iBase); }
            std::uintptr_t end() const { return reinterpret_cast<std::uintptr_t>(iBase) + iRegion.high() * PageSize; }
        private:
            std::byte* page_address(std::size_t aPage) const { return iBase + aPage * PageSize; }
            std::size_t page_of(void const* aMemory) const
            {
                return static_cast<std::size_t>((static_cast<std::byte const*>(aMemory) - iBase) >> PageShift);
            }
            void* allocate_large(std::size_t aSize, bool aRaw)
            {
                auto const count = (aSize + PageSize - 1u) / PageSize;
                auto const page = iRegion.take(count);
                for (std::size_t p = page; p < page + count; ++p)
                    iPages[p] = page_info{ static_cast<std::uint32_t>(PageSize), static_cast<std::uint32_t>(page), aSize, iOwner, aRaw };
                return page_address(page);
            }
        private:
            struct raw_class
            {
                void* freeList = nullptr;
                std::byte* bump = nullptr;
                std::byte* bumpEnd = nullptr;
            };
            region& iRegion;
            std::byte* iBase;
            page_info* iPages;
            std::uint32_t iOwner;           ///< (its isolate's number: its pages are those whose owner it is)
            raw_class iRawClasses[ClassCount];
            allocation_state* iState = nullptr;   ///< (cells' free lists: see allocation_state)
            std::vector<std::pair<std::uint64_t, std::uint64_t>> iRuns[ClassCount];   ///< (swept pages' runs of free slots (the last first))
            std::vector<std::size_t> iUnswept[ClassCount];     ///< (pages of small cells that are not swept yet (the last first))
            std::vector<std::size_t> iEmpty[ClassCount];       ///< (pages of small cells that have none (see empty))
            void (*iSweeper)(void*, std::size_t) = nullptr;
            void* iSweeperData = nullptr;
            std::uint8_t iClassOf[MaxSmall / 16u + 1u] = {};
        };

        // --- rooted memory (root_allocate): a list of blocks

        struct root_block
        {
            root_block* previous;
            root_block* next;
            std::size_t size;
            std::size_t padding;    // (the memory that follows is 16 byte aligned)
        };
        static_assert(sizeof(root_block) % 16u == 0u);

        // (rooted memory (root_allocate): the thread's (the isolate that it runs scans it; a thread_local container's outlives the
        // isolates that the thread runs, one after another))
        root_block& root_blocks()
        {
            thread_local root_block tBlocks{ &tBlocks, &tBlocks, 0u, 0u };
            return tBlocks;
        }

        // (the runtime's own roots (see root_tracer): the same for every isolate (each marks its isolate's state))
        std::vector<std::pair<root_tracer::function, root_tracer::function>>& root_list()
        {
            static std::vector<std::pair<root_tracer::function, root_tracer::function>> sRoots;
            return sRoots;
        }

        // --- the calling thread's stacks

        thread_local stack_segment* tSegments = nullptr;
#if !defined(_WIN32)
        thread_local void const* tStackBase = nullptr;
#endif

        bool environment_flag(char const* aName, std::uint64_t& aValue)
        {
            if (auto const text = std::getenv(aName); text != nullptr && *text != '\0')
            {
                aValue = std::strtoull(text, nullptr, 10);
                return true;
            }
            return false;
        }
    }

    // --- the collector

    class collector
    {
    public:
        static constexpr std::uint64_t MinimumTrigger = 8u * 1024u * 1024u;   ///< (bytes allocated (as estimated) between collections)
    public:
        collector(heap& aHeap) : iHeap{ aHeap }
        {
            environment_flag("NEOS_JS_GC_STRESS", iStress);
            std::uint64_t flag = 0u;
            if (environment_flag("NEOS_JS_GC_QUARANTINE", flag))
                iQuarantine = flag != 0u;
            else
                iQuarantine = iStress != 0u;
            if (environment_flag("NEOS_JS_GC", flag) && flag == 0u)
                iPauses = 1;
            environment_flag("NEOS_JS_GC_VERBOSE", iVerbose);
            iMemory.set_sweeper([](void* aCollector, std::size_t aPage) { static_cast<collector*>(aCollector)->sweep_page(aPage); }, this);
            iHeap.iState = &iMemory.state();
            triggers();
        }
    public:
        cell_memory& memory() { return iMemory; }
        std::uint64_t epoch() const { return iCollections + 1u; }   ///< (the collection's number (shapes' epochs start at 0: none))
        bool quarantine() const { return iQuarantine; }
        bool collecting() const { return iCollecting; }
        void pause() { ++iPauses; }
        void resume() { --iPauses; }
        void triggers()
        {
            iHeap.iAllocations = 0u;
            iHeap.iAllocationTrigger = iStress != 0u ? iStress : ~std::uint64_t{};
            iHeap.iState->allocatedBytes = 0u;
            iHeap.iByteTrigger = iStress != 0u ? ~std::uint64_t{} : std::max<std::uint64_t>(MinimumTrigger, iLiveBytes * 2u);
            limit();
        }
        // (compiled code's allocations: the runtime's once the bytes allocated reach the trigger (always, if collections are by count))
        void limit()
        {
            iHeap.iState->compiledLimit = iStress != 0u ? 0u : iHeap.iByteTrigger;
        }
        void allocating()
        {
            if (!ready())
            {
                // (later)
                iHeap.iAllocations = 0u;
                iHeap.iAllocationTrigger = iStress != 0u ? iStress : 4096u;
                iHeap.iState->allocatedBytes = 0u;
                iHeap.iByteTrigger = iStress != 0u ? ~std::uint64_t{} : MinimumTrigger;
                limit();
                return;
            }
            collect();
        }
        bool ready() const
        {
            if (iCollecting || iPauses != 0 || !realm_initialized())
                return false;
            auto const* h = host();
            return h != nullptr && h->version >= 8u && h->values != nullptr && stack_base() != nullptr;
        }
        // (the live cell whose handle it is (its slot's handle field is the handle: a free slot's is 0), else null)
        cell* live_cell(std::uint32_t aHandle) const
        {
            if (aHandle == 0u || iHandleOffset == 0u)
                return nullptr;
            auto* const slot = iMemory.slot_starting_at(static_cast<std::size_t>(aHandle) << heap::HandleShift);
            if (slot == nullptr)
                return nullptr;
            std::uint32_t h;
            std::memcpy(&h, slot + iHandleOffset, sizeof(h));
            return h == aHandle ? reinterpret_cast<cell*>(slot) : nullptr;
        }
        void record_handle_offset(cell& aCell)
        {
            if (iHandleOffset == 0u)
                iHandleOffset = static_cast<std::size_t>(reinterpret_cast<std::byte const*>(&aCell.handle) - reinterpret_cast<std::byte const*>(&aCell));
        }
        void free_cell(cell* aCell, std::uint32_t aHandle);
        void collect();
        // marking
        void push(cell& aCell)
        {
            aCell.marked = 1u;
            iWork.push_back(&aCell);
            // (the values of the ephemerons whose key it is: marked now)
            if (!iPendingEphemerons.empty())
                if (auto const pending = iPendingEphemerons.find(&aCell); pending != iPendingEphemerons.end())
                {
                    iEphemeronValues.insert(iEphemeronValues.end(), pending->second.begin(), pending->second.end());
                    iPendingEphemerons.erase(pending);
                }
        }
        void mark_value(value aValue)
        {
            if (aValue >= TagString && aValue < (TagInternal | 0xFFFFFFFFFFFFull))
                mark_handle(handle_of(aValue));
        }
        // (a WeakMap's or WeakSet's entries: a value is marked if its key is (now, or when the key is marked))
        void ephemerons(std::unordered_map<value, value> const& aTable)
        {
            for (auto const& [key, v] : aTable)
            {
                if (marked(key))
                    mark_value(v);
                else if (auto* const k = iHeap.find(handle_of(key)); k != nullptr && marked(v) == false)
                    iPendingEphemerons[k].push_back(v);
            }
        }
        void mark_handle(std::uint32_t aHandle)
        {
            // (a value's handle (a value that a cell, or a root, has refers to a cell that lives: it is not checked (a word that may 
            // be a value is: see mark_word)))
            if (aHandle == 0u)
                return;
            auto& c = iHeap.at(aHandle);
            if (c.marked == 0u)
                push(c);
        }
        void mark_word(std::uint64_t aWord)
        {
            auto const tag = aWord & TagMask;
            if (tag >= TagString && tag <= TagInternal)
            {
                if ((aWord & 0x0000FFFF00000000ull) != 0u)
                    return;
                auto* const c = iHeap.find(handle_of(aWord));
                if (c == nullptr || c->marked != 0u || value_of(*c) != aWord)
                    return;
                push(*c);
                return;
            }
            if (aWord >= iMemory.begin() && aWord < iMemory.end())
                if (auto const* const slot = iMemory.slot_at(static_cast<std::uintptr_t>(aWord)))
                {
                    std::uint32_t handle;
                    std::memcpy(&handle, slot + iHandleOffset, sizeof(handle));
                    if (auto* const c = iHeap.find(handle); c != nullptr && reinterpret_cast<std::byte const*>(c) == slot && c->marked == 0u)
                        push(*c);
                }
        }
        // (the words of a stack are read whatever they are (with AddressSanitizer: its redzones too))
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((no_sanitize("address")))
#endif
        void conservative(void const* aBegin, void const* aEnd)
        {
            auto begin = (reinterpret_cast<std::uintptr_t>(aBegin) + 7u) & ~std::uintptr_t{ 7u };
            auto const end = reinterpret_cast<std::uintptr_t>(aEnd) & ~std::uintptr_t{ 7u };
            for (; begin < end; begin += 8u)
                mark_word(*reinterpret_cast<std::uint64_t const*>(begin));
        }
        bool alive_at(void const* aAddress) const
        {
            auto const address = reinterpret_cast<std::uintptr_t>(aAddress);
            if (address < iMemory.begin() || address >= iMemory.end())
                return true;
            auto const* const slot = iMemory.slot_at(address);
            if (slot == nullptr)
                return true;
            std::uint32_t handle;
            std::memcpy(&handle, slot + iHandleOffset, sizeof(handle));
            auto const* const c = iHeap.find(handle);
            return c == nullptr || reinterpret_cast<std::byte const*>(c) != slot || c->marked != 0u;
        }
        bool marked(value aValue) const
        {
            if (aValue < TagString || aValue >= (TagInternal | 0xFFFFFFFFFFFFull))
                return true;
            auto const* const c = iHeap.find(handle_of(aValue));
            return c != nullptr && c->marked != 0u;
        }
        void drain(tracer& aTracer)
        {
            for (;;)
            {
                if (!iWork.empty())
                {
                    auto* const c = iWork.back();
                    iWork.pop_back();
                    ++iMarked;
                    iMarkedBytes += iMemory.size_of(c);
                    if (c->type == cell_type::String)
                        iMarkedBytes += static_cast<string*>(c)->text.capacity() * sizeof(char16_t);
                    c->trace(aTracer);
                }
                else if (!iEphemeronValues.empty())
                {
                    auto const v = iEphemeronValues.back();
                    iEphemeronValues.pop_back();
                    mark_value(v);
                }
                else
                    break;
            }
        }
        std::unordered_map<cell*, std::vector<value>> iPendingEphemerons;    ///< (unmarked keys -> their ephemerons' values)
        std::vector<value> iEphemeronValues;                                ///< (the values of ephemerons whose keys have been marked)
        std::vector<std::pair<cell*, void (*)(cell&, tracer&)>> iAfterMarking;
        std::vector<std::pair<root_tracer::function, root_tracer::function>>& roots() { return root_list(); }
        // (the isolate has ended: its cells are destroyed (the objects that they own are freed))
        void destroy_cells() noexcept
        {
            ++iPauses;
            iMemory.finish_sweeping();
            // (no cell is marked: all are freed)
            sweep();
            iMemory.finish_sweeping();
        }
    private:
        void mark_and_sweep();
        void scan_stacks();
        void sweep();
        void sweep_page(std::size_t aPage);
    private:
        heap& iHeap;
        cell_memory iMemory;
        std::vector<cell*> iWork;
        std::size_t iHandleOffset = 0u;
        std::uint64_t iStress = 0u;
        bool iQuarantine = false;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> iSweptRuns;     ///< (a page's free runs, as sweep_page finds them)
        std::uint64_t iVerbose = 0u;
        int iPauses = 0;
        bool iCollecting = false;
        std::uint64_t iLiveBytes = 0u;
        std::uint64_t iMarkedBytes = 0u;    ///< (the collection's marked cells' bytes (and their count))
        std::size_t iMarked = 0u;
        std::uint64_t iCollections = 0u;
    };

    namespace
    {
        collector& the_collector()
        {
            return the_heap().the_collector();
        }
    }

    // --- the heap (an isolate's)

    heap::heap() : iRootShape{ new shape{} }
    {
    }

    heap::~heap()
    {
    }

    namespace
    {
        std::mutex sIsolatesMutex;
        std::size_t sIsolates = 0u;     ///< (alive)
    }

    heap& new_isolate()
    {
        auto* const h = new heap{};
        tHeap = h;
        h->iCollector = new collector{ *h };
        std::lock_guard<std::mutex> lock{ sIsolatesMutex };
        if (++sIsolates == 1u)
        {
#if !defined(_WIN32)
            gSoleIsolate.thread = ::pthread_self();
#endif
            gSoleIsolate.threadPointer = thread_pointer();
            gSoleIsolate.isolate.store(h, std::memory_order_release);
        }
        else
            gSoleIsolate.isolate.store(nullptr, std::memory_order_release);
        return *h;
    }

    void end_isolate()
    {
        auto* const h = tHeap;
        if (h == nullptr)
            return;
        {
            std::lock_guard<std::mutex> lock{ sIsolatesMutex };
            --sIsolates;
            if (gSoleIsolate.isolate.load(std::memory_order_relaxed) == h)
                gSoleIsolate.isolate.store(nullptr, std::memory_order_release);
        }
        auto* const c = h->iCollector;
        // (its cells (their destructors use the isolate's state), then its state, then its memory)
        c->destroy_cells();
        h->destroy_locals();
        delete h->iRootShape;
        for (auto* s : h->iTypeRootShapes)
            delete s;
        for (auto* s : h->watchedRootShapes)
            delete s;
        delete h->commonNames;
        delete c;
        tHeap = nullptr;
        delete h;
    }

    std::size_t new_isolate_local()
    {
        static std::atomic<std::size_t> sLocals = 0u;
        return sLocals++;
    }

    void* heap::new_local(std::size_t aIndex, void* (*aMake)(), void (*aDestroy)(void*))
    {
        auto* const object = aMake();   // (it may use others, which are made first)
        if (aIndex >= iLocals.size())
            iLocals.resize(aIndex + 1u);
        if (iLocals[aIndex].object != nullptr)
        {
            aDestroy(object);
            return iLocals[aIndex].object;
        }
        iLocalOrder.reserve(iLocalOrder.size() + 1u);
        iLocals[aIndex] = local_object{ object, aDestroy };
        iLocalOrder.push_back(aIndex);
        return object;
    }

    void heap::destroy_locals() noexcept
    {
        // (the last made first (one that is used again while others are destroyed is made again, and destroyed later))
        while (!iLocalOrder.empty())
        {
            auto const index = iLocalOrder.back();
            iLocalOrder.pop_back();
            auto const l = iLocals[index];
            iLocals[index] = local_object{};
            l.destroy(l.object);
        }
    }

    std::size_t new_shape_type()
    {
        static std::atomic<std::size_t> sTypes = 0u;
        return sTypes++;
    }

    shape* heap::new_type_root_shape(std::size_t aType)
    {
        if (aType >= iTypeRootShapes.size())
            iTypeRootShapes.resize(aType + 1u, nullptr);
        return iTypeRootShapes[aType] = new shape{};
    }

    std::uint32_t heap::size_class(std::size_t aSize) const
    {
        return iCollector->memory().size_class(aSize);
    }

    std::uint32_t heap::class_size(std::uint32_t aClass) const
    {
        return cell_memory::class_size(aClass);
    }

    std::uint64_t heap::object_template(std::uint32_t aClass, std::size_t aRoom)
    {
        // (an ordinary object (no prototype, no properties) with room for aRoom values (its values' offset is its copies'): storage that 
        // is not a cell's, never destroyed (it owns nothing))
        if (iTemplates[aClass] == 0u)
        {
            constexpr std::size_t base = (sizeof(object) + 7u) & ~std::size_t{ 7u };
            auto* const memory = static_cast<std::byte*>(iCollector->memory().allocate(base + sizeof(value), true));
            auto* const o = ::new (memory) object{ Undefined };
            o->properties.use_storage(reinterpret_cast<value*>(memory + base), aRoom);
            iTemplates[aClass] = region_offset(memory);
        }
        return iTemplates[aClass];
    }

    std::uint64_t heap::array_template(std::uint32_t aClass, std::size_t aRoom)
    {
        // (an array (no prototype, no elements) with room for aRoom elements in its cell (their offset is its copies'), as make_with makes 
        // arrays (its own type's shapes; its [[Get]] not the ordinary one))
        if (iArrayTemplates[aClass] == 0u)
        {
            constexpr std::size_t base = (sizeof(array_object) + 7u) & ~std::size_t{ 7u };
            auto* const memory = static_cast<std::byte*>(iCollector->memory().allocate(base + sizeof(value), true));
            auto* const a = ::new (memory) array_object{ Undefined };
            a->ordinaryGet = false;
            a->properties.set_root(type_root_shape<array_object>());
            a->elements.use_storage(reinterpret_cast<value*>(memory + base), aRoom);
            iArrayTemplates[aClass] = region_offset(memory);
        }
        return iArrayTemplates[aClass];
    }

    void* heap::allocate(std::size_t aSize)
    {
        return iCollector->memory().allocate(aSize);
    }

    std::size_t heap::slot_size(void const* aMemory)
    {
        return iCollector->memory().size_of(aMemory);
    }

    void heap::deallocate(void* aMemory) noexcept
    {
        iCollector->memory().release(aMemory, true);
    }

    void heap::allocating()
    {
        iCollector->allocating();
    }

    void heap::add(cell& aCell, void* aMemory)
    {
        // (a cell is the start of its memory (its type has single inheritance))
        if (static_cast<void*>(&aCell) != aMemory)
            std::abort();
        auto& c = *iCollector;
        c.record_handle_offset(aCell);
        if (c.collecting())
            aCell.marked = 1u;     // (a cell made while dead cells are freed (e.g. by a weak reference's clearing) lives)
        // (its handle: its offset in the region (its slot's: a multiple of 16) >> HandleShift)
        auto const offset = region_offset(&aCell);
        if ((offset & ((std::uint64_t{ 1u } << HandleShift) - 1u)) != 0u)
            std::abort();
        aCell.handle = static_cast<std::uint32_t>(offset >> HandleShift);
    }

    cell* heap::find(std::uint32_t aHandle) const
    {
        return iCollector->live_cell(aHandle);
    }

    void* region_allocate(std::size_t aSize)
    {
        // (no collection: the cell whose storage it is may be changing)
        return the_collector().memory().allocate(aSize, true);
    }

    bool region_raw(void const* aMemory)
    {
        return the_collector().memory().raw(aMemory);
    }

    void region_release(void* aMemory) noexcept
    {
        // (storage in a cell (e.g. a table's values in its object's) is the cell's)
        auto& memory = the_collector().memory();
        if (memory.raw(aMemory))
            memory.release(aMemory, true);
    }

    void publish_heap()
    {
        region::instance().publish();
    }

    void heap::collect()
    {
        iCollector->collect();
    }

    void collect_garbage()
    {
        the_heap().collect();
    }

    gc_pause::gc_pause()
    {
        the_collector().pause();
    }

    gc_pause::~gc_pause()
    {
        the_collector().resume();
    }

    root_tracer::root_tracer(function aFunction, function aAfterMarking)
    {
        root_list().emplace_back(aFunction, aAfterMarking);
    }

    // --- marking

    void tracer::mark_handle(std::uint32_t aHandle)
    {
        iCollector.mark_handle(aHandle);
    }

    void tracer::push(cell& aCell)
    {
        iCollector.push(aCell);
    }

    void tracer::conservative(void const* aBegin, void const* aEnd)
    {
        iCollector.conservative(aBegin, aEnd);
    }

    void tracer::suspended_stack(stack_segment const* aTop, stack_segment const* aBottom, neos_rt_coroutine* aCoroutine)
    {
        for (auto const* s = aTop; s != nullptr && s != aBottom; s = s->previous)
            if (s->high != nullptr)
                conservative(s->low, s->high);
        if (aCoroutine != nullptr)
            if (auto const* h = host(); h != nullptr && h->version >= 8u && h->values != nullptr)
                h->values(aCoroutine, [](void* aData, unsigned long long const* aValues, size_t aCount)
                {
                    static_cast<tracer*>(aData)->conservative(aValues, aValues + aCount);
                }, this);
    }

    std::uint64_t tracer::epoch() const
    {
        return iCollector.epoch();
    }

    bool tracer::alive(value aValue) const
    {
        return iCollector.marked(aValue);
    }

    bool tracer::alive(void const* aAddress) const
    {
        return iCollector.alive_at(aAddress);
    }

    bool tracer::marked(value aValue) const
    {
        return iCollector.marked(aValue);
    }

    void tracer::ephemerons(std::unordered_map<value, value> const& aTable)
    {
        iCollector.ephemerons(aTable);
    }

    void tracer::after_marking(cell& aCell, void (*aFunction)(cell& aCell, tracer& aTracer))
    {
        iCollector.iAfterMarking.emplace_back(&aCell, aFunction);
    }

    // --- collection

    void collector::collect()
    {
        if (!ready())
            return;
        try
        {
            mark_and_sweep();
        }
        catch (...)
        {
            // (the cells' marks are inconsistent: the program cannot go on)
            std::fputs("JavaScript runtime: out of memory while collecting garbage\n", stderr);
            std::abort();
        }
    }

    void collector::mark_and_sweep()
    {
        // (the last collection's pages that are not swept yet: swept (the cells' marks are clear))
        iMemory.finish_sweeping();
        iCollecting = true;
        iMarked = 0u;
        iMarkedBytes = 0u;
        // (the registers' values (the callers' values that are in registers) are in this frame: scan_stacks scans from below it)
        NEOS_JS_SPILL_REGISTERS();
        tracer t{ *this };
        scan_stacks();
        // (the interpreter's values: the running code's)
        if (auto const* h = host(); h != nullptr)
            h->values(nullptr, [](void* aData, unsigned long long const* aValues, size_t aCount)
            {
                static_cast<collector*>(aData)->conservative(aValues, aValues + aCount);
            }, this);
        // (rooted memory)
        auto& blocks = root_blocks();
        for (auto* b = blocks.next; b != &blocks; b = b->next)
            conservative(b + 1, reinterpret_cast<std::byte const*>(b + 1) + b->size);
        // (the runtime's own)
        for (auto const& r : roots())
            r.first(t);
        // (marking (ephemerons' values are marked when their keys are: see push))
        drain(t);
        // (weak references to the dead are cleared)
        for (std::size_t i = 0u; i < iAfterMarking.size(); ++i)
            iAfterMarking[i].second(*iAfterMarking[i].first, t);
        for (auto const& r : roots())
            if (r.second != nullptr)
                r.second(t);
        drain(t);
        iPendingEphemerons.clear();
        iAfterMarking.clear();
        sweep();
        ++iCollections;
        triggers();
        iCollecting = false;
    }

    void collector::scan_stacks()
    {
        // (the running stack's live part, and the segments of the stacks that the running code was resumed (or called) from: a stack's
        // live part (from its innermost segment) is scanned once)
        struct range
        {
            void const* low;
            void const* high;
        };
        std::vector<range> ranges;
        ranges.push_back(range{ stack_pointer(), stack_base() });
        for (auto const* s = tSegments; s != nullptr; s = s->previous)
            if (s->high != nullptr)
            {
                auto existing = std::find_if(ranges.begin(), ranges.end(), [&](range const& r) { return r.high == s->high; });
                if (existing == ranges.end())
                    ranges.push_back(range{ s->low, s->high });
                else if (s->low < existing->low)
                    existing->low = s->low;
            }
        for (auto const& r : ranges)
            if (r.low < r.high)
                conservative(r.low, r.high);
    }

    void collector::free_cell(cell* aCell, std::uint32_t aHandle)
    {
        (void)aHandle;
        auto const size = iMemory.size_of(aCell);
        // (a plain object that owns nothing outside its cell (its properties' values in it): nothing to destroy)
        if (!(&typeid(*aCell) == &typeid(object) && static_cast<object*>(aCell)->properties.owns_nothing() &&
            static_cast<object*>(aCell)->extra_if() == nullptr))
            aCell->~cell();
        auto* const memory = reinterpret_cast<std::byte*>(aCell);
        if (iQuarantine)
            std::memset(memory, 0xDB, size);
        std::uint32_t const none = 0u;
        std::memcpy(memory + iHandleOffset, &none, sizeof(none));
        iMemory.release(memory, !iQuarantine);
    }

    void collector::sweep()
    {
        // (large cells now; the pages of small cells as their cells are allocated (see cell_memory::begin_sweeping))
        std::size_t freed = 0u;
        iMemory.for_each_large_descending([&](std::byte* aCell)
        {
            std::uint32_t h;
            std::memcpy(&h, aCell + iHandleOffset, sizeof(h));
            if (h == 0u || h != static_cast<std::uint32_t>(region_offset(aCell) >> heap::HandleShift))
                return;
            auto* const c = reinterpret_cast<cell*>(aCell);
            if (c->marked != 0u)
                c->marked = 0u;
            else
            {
                free_cell(c, h);
                ++freed;
            }
        });
        iMemory.begin_sweeping();
        iLiveBytes = iMarkedBytes;
        if (iVerbose != 0u)
            std::fprintf(stderr, "[gc %llu: %zu live (%llu KB), %zu large freed]\n", static_cast<unsigned long long>(iCollections), iMarked,
                static_cast<unsigned long long>(iMarkedBytes / 1024u), freed);
    }

    void collector::sweep_page(std::size_t aPage)
    {
        // (its live cells unmarked, its dead ones freed; its free slots (free, or not used yet) in runs, from which cells are allocated
        // (see cell_memory::allocate))
        std::byte* start;
        std::uint32_t slotSize;
        iMemory.page_slots(aPage, start, slotSize);
        auto const startOffset = static_cast<std::uint64_t>(region_offset(start));
        bool live = false;
        auto const first = static_cast<std::uint32_t>(region_offset(start) >> heap::HandleShift);
        auto const step = slotSize >> heap::HandleShift;
        auto const count = PageSize / slotSize;
        auto& tRuns = iSweptRuns;   // (the collector's: a thread_local's access is not cheap (the runtime is a module loaded at run time))
        tRuns.clear();
        std::uint64_t runStart = 0u;
        bool inRun = false;
        constexpr std::size_t Ahead = 8u;   // (slots: their words are fetched before they are read (the loop is bound by memory's latency))
        // (the collector's state that the loop reads: locals (the runs' growth would otherwise make them read again each time))
        auto const handleOffset = iHandleOffset;
        bool const quarantine = iQuarantine;
        for (std::size_t i = 0u; i < count; ++i)
        {
            auto* const slot = start + i * slotSize;
#if defined(__GNUC__) || defined(__clang__)
            if (i + Ahead < count)
                __builtin_prefetch(slot + Ahead * slotSize + handleOffset, 1);
#endif
            auto const handle = first + static_cast<std::uint32_t>(i) * step;
            std::uint32_t h;
            std::memcpy(&h, slot + handleOffset, sizeof(h));
            bool free = true;
            if (h == handle)
            {
                auto* const c = reinterpret_cast<cell*>(slot);
                if (c->marked != 0u)
                {
                    c->marked = 0u;
                    live = true;
                    free = false;
                }
                else if (quarantine)
                {
                    free_cell(c, h);
                    free = false;
                }
                else
                {
                    // (a dead cell: destroyed (unless it is a plain object that owns nothing outside its cell), then free)
                    if (!(&typeid(*c) == &typeid(object) && static_cast<object*>(c)->properties.owns_nothing() &&
                        static_cast<object*>(c)->extra_if() == nullptr))
                        c->~cell();
                    std::uint32_t const none = 0u;
                    std::memcpy(slot + handleOffset, &none, sizeof(none));
                }
            }
            else if (quarantine)
                free = false;
            if (free)
            {
                if (!inRun)
                {
                    runStart = startOffset + i * slotSize;
                    inRun = true;
                }
            }
            else if (inRun)
            {
                tRuns.emplace_back(runStart, startOffset + i * slotSize);
                inRun = false;
            }
        }
        if (inRun)
            tRuns.emplace_back(runStart, startOffset + count * slotSize);
        // (a page that has no live cells: not swept again until it has cells (its slots are not runs: it is an empty page))
        if (!live && !iQuarantine)
            iMemory.empty(aPage);
        else
            iMemory.add_runs(slotSize, tRuns);
    }

    // --- rooted memory

    void* root_allocate(std::size_t aBytes)
    {
        auto& blocks = root_blocks();
        auto* const b = static_cast<root_block*>(std::malloc(sizeof(root_block) + aBytes));
        if (b == nullptr)
            throw std::bad_alloc{};
        b->size = aBytes;
        b->previous = &blocks;
        b->next = blocks.next;
        blocks.next->previous = b;
        blocks.next = b;
        return b + 1;
    }

    void root_deallocate(void* aMemory) noexcept
    {
        if (aMemory == nullptr)
            return;
        auto* const b = static_cast<root_block*>(aMemory) - 1;
        b->previous->next = b->next;
        b->next->previous = b->previous;
        std::free(b);
    }

    // --- stacks

    stack_segment*& stack_segments()
    {
        return tSegments;
    }

    void const* stack_base()
    {
#if defined(_WIN32)
        return reinterpret_cast<NT_TIB const*>(::NtCurrentTeb())->StackBase;
#elif defined(__linux__)
        if (tStackBase == nullptr)
        {
            pthread_attr_t attributes;
            if (::pthread_getattr_np(::pthread_self(), &attributes) == 0)
            {
                void* address = nullptr;
                std::size_t size = 0u;
                if (::pthread_attr_getstack(&attributes, &address, &size) == 0)
                    tStackBase = static_cast<std::byte const*>(address) + size;
                ::pthread_attr_destroy(&attributes);
            }
        }
        return tStackBase;
#else
        return tStackBase;  // (unknown: no collection)
#endif
    }

    void set_stack_base(void const* aBase)
    {
#if defined(_WIN32)
        (void)aBase;
#else
        tStackBase = aBase;
#endif
    }

#if defined(_MSC_VER)
    __declspec(noinline)
#else
    [[gnu::noinline]]
#endif
    void const* stack_pointer()
    {
        char volatile marker = 0;
        void const* volatile here = const_cast<char const*>(&marker);
        return here;
    }

    // --- the JavaScript exceptions that exist (their values are roots)

    namespace
    {
        thread_local js_exception* sExceptions = nullptr;     // (the thread's: its isolate's)

        root_tracer const sExceptionRoots{ [](tracer& aTracer)
        {
            for (auto const* e = sExceptions; e != nullptr; e = e->next)
                aTracer.mark(e->thrown);
        } };
    }

    js_exception::js_exception(value aThrown) : thrown{ aThrown }, previous{ nullptr }, next{ sExceptions }
    {
        if (sExceptions != nullptr)
            sExceptions->previous = this;
        sExceptions = this;
    }

    js_exception::js_exception(js_exception const& aOther) : js_exception{ aOther.thrown }
    {
    }

    js_exception& js_exception::operator=(js_exception const& aOther)
    {
        thrown = aOther.thrown;
        return *this;
    }

    js_exception::~js_exception()
    {
        if (previous != nullptr)
            previous->next = next;
        else
            sExceptions = next;
        if (next != nullptr)
            next->previous = previous;
    }

    // --- WeakRef's kept objects

    namespace
    {
        value_list& kept_objects()
        {
            static isolate_local<value_list> sKept;
            return *sKept;
        }
    }

    void keep_during_job(value aValue)
    {
        kept_objects().push_back(aValue);
    }

    void clear_kept_objects()
    {
        // (its memory is freed: a rooted block's words are roots, beyond the vector's size too)
        if (!kept_objects().empty())
            value_list{}.swap(kept_objects());
    }

    // --- the traces of the cells of js.hpp

    void string::trace(tracer& aTracer)
    {
        aTracer.mark(left);
        aTracer.mark(right);
        aTracer.mark(base);
    }

    void symbol::trace(tracer& aTracer)
    {
        aTracer.mark(description);
    }

    void object::trace(tracer& aTracer)
    {
        aTracer.mark(proto);
        properties.trace(aTracer);
        auto const* const extra = extra_if();
        if (extra == nullptr)
            return;
        if (cls == object_class::WeakRef)
        {
            // (a WeakRef: its target is weak (cleared when it dies))
            if (extra->internal != Undefined)
                aTracer.after_marking(*this, [](cell& aCell, tracer& aTracer)
                {
                    auto& o = static_cast<object&>(aCell);
                    if (!aTracer.alive(o.internal()))
                        o.internal() = Undefined;
                });
        }
        else
            aTracer.mark(extra->internal);
        for (auto const& [name, element] : extra->privateElements)
        {
            aTracer.mark(name);
            aTracer.mark(element);
        }
    }

    void array_object::trace(tracer& aTracer)
    {
        object::trace(aTracer);
        aTracer.mark(elements);
    }

    void function_object::trace(tracer& aTracer)
    {
        object::trace(aTracer);
        aTracer.mark(realmGlobal);
        aTracer.mark(slots);
        aTracer.mark(environment);
        aTracer.mark(homeObject);
        aTracer.mark(fields);
        aTracer.mark(sourceText);
        aTracer.mark(lexicalThis);
        aTracer.mark(lexicalNewTarget);
        aTracer.mark(lexicalHome);
        aTracer.mark(lexicalCallee);
        aTracer.mark(lastConstructed);
        aTracer.mark(boundTarget);
        aTracer.mark(boundThis);
        aTracer.mark(boundArguments);
    }

    void environment::trace(tracer& aTracer)
    {
        aTracer.mark(parent);
        aTracer.mark(slots);
        aTracer.mark(withObject);
        aTracer.mark(names);
    }

    // --- the roots of api.cpp (the pending exception and the extra arguments stack)

    namespace
    {
        root_tracer const sApiRoots{ [](tracer& aTracer)
        {
            aTracer.mark(pending_exception());
            aTracer.mark(extra_arguments());
        } };
    }
}
