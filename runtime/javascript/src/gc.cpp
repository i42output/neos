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
#include <cstdlib>
#include <cstdio>
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
    namespace
    {
        // --- the cells' memory: pages (of one size of slot each, or a large cell's) of one reserved region of address space, so that
        // whether a word points into a cell is known from the page it points into

        constexpr std::size_t PageShift = 16u;
        constexpr std::size_t PageSize = std::size_t{ 1u } << PageShift;
        constexpr std::size_t RegionSize = std::size_t{ 64u } << 30u;      // (address space only: pages are committed when used)
        constexpr std::size_t MaxSmall = 4096u;
        constexpr std::size_t HandlePages = 65536u;     // (the handle table's (4 GB: 512M handles): after the first page)
        static_assert(neos::language::javascript::layout::RegionHandles == PageSize);

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
            bool raw = false;               ///< (storage that is not cells (see region_vector))
        };

        // (the region (all of it: its address and size never change; compiled code reads only the pages that are committed) is the
        // external memory that compiled code reads (see neos::language::javascript::layout))
        void publish_region(std::byte* aBase, std::size_t aSize)
        {
            auto const* h = host();
            if (h != nullptr && h->version >= 14u && h->external_memory_at != nullptr)
                h->external_memory_at(neos::language::javascript::layout::Memory, aBase, aSize, NEOS_RT_EXTERNAL_MEMORY_FIXED);
        }

        class cell_memory
        {
        public:
            cell_memory()
            {
                for (std::size_t c = 0u, size = 0u; size <= MaxSmall; size += 16u)
                {
                    while (SizeClasses[c] < size)
                        ++c;
                    iClassOf[size / 16u] = static_cast<std::uint8_t>(c);
                }
#if defined(_WIN32)
                iBase = static_cast<std::byte*>(::VirtualAlloc(nullptr, RegionSize, MEM_RESERVE, PAGE_NOACCESS));
#else
                auto const address = ::mmap(nullptr, RegionSize + PageSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                if (address != MAP_FAILED)
                    iBase = reinterpret_cast<std::byte*>((reinterpret_cast<std::uintptr_t>(address) + PageSize - 1u) & ~(PageSize - 1u));
#endif
                if (iBase == nullptr)
                {
                    std::fputs("JavaScript runtime: cannot reserve the heap's address space\n", stderr);
                    std::abort();
                }
                gRegionBase = iBase;
                iPages.reserve(4096u);
                // (the first page is reserved (offset 0 is no cell); the handle table's pages follow it (layout::RegionHandles: 
                // committed as it grows))
                iPages.resize(1u + HandlePages);
                for (auto& p : iPages)
                    p.raw = true;
            }
        public:
            // (a cell's memory, or (aRaw) storage that is not a cell)
            void* allocate(std::size_t aSize, bool aRaw = false)
            {
                if (aSize > MaxSmall)
                    return allocate_large(aSize, aRaw);
                auto* const classes = aRaw ? iRawClasses : iClasses;
                auto& c = classes[iClassOf[(aSize + 15u) / 16u]];
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
                    auto const page = new_pages(1u);
                    iPages[page].slotSize = slotSize;
                    iPages[page].raw = aRaw;
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
                    for (std::size_t p = page; p < page + count; ++p)
                        iPages[p] = page_info{};
                    decommit(page_address(page), count * PageSize);
                    if (aReuse)
                        iFreeRuns.emplace_back(page, count);
                    return;
                }
                if (!aReuse)
                    return;
                auto& c = (info.raw ? iRawClasses : iClasses)[iClassOf[info.slotSize / 16u]];
                *static_cast<void**>(aMemory) = c.freeList;
                c.freeList = aMemory;
            }
            void publish() const { publish_region(iBase, RegionSize); }
            // (the handle table (with at least aEntries entries committed))
            std::uint64_t* handles(std::size_t aEntries)
            {
                auto const needed = (aEntries * sizeof(std::uint64_t) + PageSize - 1u) / PageSize;
                if (needed > HandlePages)
                    throw std::bad_alloc{};
                for (; iHandlePages < needed; ++iHandlePages)
                    commit(page_address(1u + iHandlePages), PageSize);     // (zeroed)
                return reinterpret_cast<std::uint64_t*>(iBase + neos::language::javascript::layout::RegionHandles);
            }
            std::size_t handle_capacity() const { return iHandlePages * PageSize / sizeof(std::uint64_t); }
            // (each slot of cells (live, free or not used yet), in descending address order (aVisit(slot)))
            template <typename Visit>
            void for_each_slot_descending(Visit aVisit)
            {
                for (auto page = iPages.size(); page-- > 0u;)
                {
                    auto const& info = iPages[page];
                    if (info.slotSize == 0u || info.raw)
                        continue;
                    if (info.largeSize != 0u)
                    {
                        if (info.first == page)
                            aVisit(page_address(page));
                        continue;
                    }
                    auto* const start = page_address(page);
                    for (auto i = PageSize / info.slotSize; i-- > 0u;)
                        aVisit(start + i * info.slotSize);
                }
            }
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
                if (page >= iPages.size())
                    return nullptr;
                auto const& info = iPages[page];
                if (info.slotSize == 0u || info.raw)
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
            std::uintptr_t end() const { return reinterpret_cast<std::uintptr_t>(iBase) + iPages.size() * PageSize; }
        private:
            std::byte* page_address(std::size_t aPage) const { return iBase + aPage * PageSize; }
            std::size_t page_of(void const* aMemory) const
            {
                return static_cast<std::size_t>((static_cast<std::byte const*>(aMemory) - iBase) >> PageShift);
            }
            std::size_t new_pages(std::size_t aCount)
            {
                for (auto r = iFreeRuns.begin(); r != iFreeRuns.end(); ++r)
                    if (r->second == aCount)
                    {
                        auto const page = r->first;
                        iFreeRuns.erase(r);
                        commit(page_address(page), aCount * PageSize);
                        return page;
                    }
                auto const page = iPages.size();
                if ((page + aCount) * PageSize > RegionSize)
                    throw std::bad_alloc{};
                commit(page_address(page), aCount * PageSize);
                iPages.resize(page + aCount);
                publish();
                return page;
            }
            void* allocate_large(std::size_t aSize, bool aRaw)
            {
                auto const count = (aSize + PageSize - 1u) / PageSize;
                auto const page = new_pages(count);
                for (std::size_t p = page; p < page + count; ++p)
                    iPages[p] = page_info{ static_cast<std::uint32_t>(PageSize), static_cast<std::uint32_t>(page), aSize, aRaw };
                return page_address(page);
            }
            static void commit(void* aAddress, std::size_t aSize)
            {
#if defined(_WIN32)
                if (::VirtualAlloc(aAddress, aSize, MEM_COMMIT, PAGE_READWRITE) == nullptr)
                    throw std::bad_alloc{};
#else
                if (::mprotect(aAddress, aSize, PROT_READ | PROT_WRITE) != 0)
                    throw std::bad_alloc{};
#endif
            }
            static void decommit(void* aAddress, std::size_t aSize) noexcept
            {
#if defined(_WIN32)
                ::VirtualFree(aAddress, aSize, MEM_DECOMMIT);
#else
                ::madvise(aAddress, aSize, MADV_DONTNEED);
#endif
            }
        private:
            struct size_class
            {
                void* freeList = nullptr;
                std::byte* bump = nullptr;
                std::byte* bumpEnd = nullptr;
            };
            std::byte* iBase = nullptr;
            std::vector<page_info> iPages;
            std::vector<std::pair<std::size_t, std::size_t>> iFreeRuns;  ///< (large cells' pages that were freed: first, count)
            size_class iRawClasses[ClassCount];
            std::size_t iHandlePages = 0u;      ///< (the handle table's committed pages)
            size_class iClasses[ClassCount];
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

        root_block sRootBlocks{ &sRootBlocks, &sRootBlocks, 0u, 0u };

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
            triggers();
        }
    public:
        cell_memory& memory() { return iMemory; }
        bool quarantine() const { return iQuarantine; }
        bool collecting() const { return iCollecting; }
        void pause() { ++iPauses; }
        void resume() { --iPauses; }
        void triggers()
        {
            iHeap.iAllocations = 0u;
            iHeap.iAllocationTrigger = iStress != 0u ? iStress : ~std::uint64_t{};
            iHeap.iAllocatedBytes = 0u;
            iHeap.iByteTrigger = iStress != 0u ? ~std::uint64_t{} : std::max(MinimumTrigger, iLiveBytes);
        }
        void allocating()
        {
            if (!ready())
            {
                // (later)
                iHeap.iAllocations = 0u;
                iHeap.iAllocationTrigger = iStress != 0u ? iStress : 4096u;
                iHeap.iAllocatedBytes = 0u;
                iHeap.iByteTrigger = iStress != 0u ? ~std::uint64_t{} : MinimumTrigger;
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
            if (auto* const c = iHeap.find(aHandle); c != nullptr && c->marked == 0u)
                push(*c);
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
        std::vector<std::pair<root_tracer::function, root_tracer::function>>& roots()
        {
            static std::vector<std::pair<root_tracer::function, root_tracer::function>> sRoots;
            return sRoots;
        }
    private:
        void mark_and_sweep();
        void scan_stacks();
        void sweep();
    private:
        heap& iHeap;
        cell_memory iMemory;
        std::vector<cell*> iWork;
        std::size_t iHandleOffset = 0u;
        std::uint64_t iStress = 0u;
        bool iQuarantine = false;
        std::uint64_t iVerbose = 0u;
        int iPauses = 0;
        bool iCollecting = false;
        std::uint64_t iLiveBytes = 0u;
        std::uint64_t iCollections = 0u;
    };

    namespace
    {
        collector& the_collector()
        {
            static collector* sCollector = new collector{ the_heap() };
            return *sCollector;
        }
    }

    // --- the heap

    heap::heap()
    {
        iCells.reserve(1u << 16);
        iCells.push_back(nullptr);  // (handle 0 refers to nothing)
    }

    heap::~heap()
    {
        // (the process is ending: cells are not destroyed (their destructors could refer to others))
    }

    heap& the_heap()
    {
        static heap* sHeap = new heap{};
        return *sHeap;
    }

    void* heap::allocate(std::size_t aSize)
    {
        return the_collector().memory().allocate(aSize);
    }

    void heap::deallocate(void* aMemory) noexcept
    {
        the_collector().memory().release(aMemory, true);
    }

    void heap::allocating()
    {
        the_collector().allocating();
    }

    void heap::add(cell& aCell, void* aMemory)
    {
        // (a cell is the start of its memory (its type has single inheritance))
        if (static_cast<void*>(&aCell) != aMemory)
            std::abort();
        auto& c = the_collector();
        c.record_handle_offset(aCell);
        if (c.collecting())
            aCell.marked = 1u;     // (a cell made while dead cells are freed (e.g. by a weak reference's clearing) lives)
        if (!iFreeHandles.empty())
        {
            aCell.handle = iFreeHandles.back();
            iFreeHandles.pop_back();
            iCells[aCell.handle] = &aCell;
            iHandleTable[aCell.handle] = region_offset(&aCell);
            return;
        }
        if (iCells.size() >= 0xFFFFFFFFu)
            throw std::bad_alloc{};
        aCell.handle = static_cast<std::uint32_t>(iCells.size());
        iCells.push_back(&aCell);
        // (the handle table (compiled code's): at a fixed offset (its pages are committed as it grows))
        if (aCell.handle >= iHandleCapacity)
        {
            iHandleTable = handle_table(static_cast<std::size_t>(aCell.handle) + 1u);
            iHandleCapacity = the_collector().memory().handle_capacity();
        }
        iHandleTable[aCell.handle] = region_offset(&aCell);
    }

    std::uint64_t* handle_table(std::size_t aEntries)
    {
        return the_collector().memory().handles(aEntries);
    }

    void* region_allocate(std::size_t aSize)
    {
        // (no collection: the cell whose storage it is may be changing)
        return the_collector().memory().allocate(aSize, true);
    }

    void region_release(void* aMemory) noexcept
    {
        the_collector().memory().release(aMemory, true);
    }

    void publish_heap()
    {
        the_collector().memory().publish();
    }

    void heap::collect()
    {
        the_collector().collect();
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
        the_collector().roots().emplace_back(aFunction, aAfterMarking);
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
        iCollecting = true;
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
        for (auto* b = sRootBlocks.next; b != &sRootBlocks; b = b->next)
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
        iHeap.iCells[aHandle] = nullptr;
        iHeap.iHandleTable[aHandle] = 0u;
        auto const size = iMemory.size_of(aCell);
        aCell->~cell();
        auto* const memory = reinterpret_cast<std::byte*>(aCell);
        if (iQuarantine)
            std::memset(memory, 0xDB, size);
        std::uint32_t const none = 0u;
        std::memcpy(memory + iHandleOffset, &none, sizeof(none));
        iMemory.release(memory, !iQuarantine);
        if (!iQuarantine)
            iHeap.iFreeHandles.push_back(aHandle);
    }

    void collector::sweep()
    {
        std::uint64_t liveBytes = 0u;
        std::size_t live = 0u;
        std::size_t freed = 0u;
        auto& cells = iHeap.iCells;
        // (by address, descending: the free lists' slots are then in ascending order (a new cell's slot is next to the last's))
        iMemory.for_each_slot_descending([&](std::byte* aSlot)
        {
            std::uint32_t h;
            std::memcpy(&h, aSlot + iHandleOffset, sizeof(h));
            if (h == 0u || h >= cells.size() || cells[h] != reinterpret_cast<cell*>(aSlot))
                return;     // (free, or not used yet)
            auto* const c = cells[h];
            if (c->marked != 0u)
            {
                c->marked = 0u;
                ++live;
                liveBytes += iMemory.size_of(c);
                if (c->type == cell_type::String)
                    liveBytes += static_cast<string*>(c)->text.capacity() * sizeof(char16_t);
                return;
            }
            free_cell(c, h);
            ++freed;
        });
        iLiveBytes = liveBytes;
        if (iVerbose != 0u)
            std::fprintf(stderr, "[gc %llu: %zu live (%llu KB), %zu freed]\n", static_cast<unsigned long long>(iCollections), live,
                static_cast<unsigned long long>(liveBytes / 1024u), freed);
    }

    // --- rooted memory

    void* root_allocate(std::size_t aBytes)
    {
        auto* const b = static_cast<root_block*>(std::malloc(sizeof(root_block) + aBytes));
        if (b == nullptr)
            throw std::bad_alloc{};
        b->size = aBytes;
        b->previous = &sRootBlocks;
        b->next = sRootBlocks.next;
        sRootBlocks.next->previous = b;
        sRootBlocks.next = b;
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
        js_exception* sExceptions = nullptr;

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
            static value_list sKept;
            return sKept;
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
    }

    void symbol::trace(tracer& aTracer)
    {
        aTracer.mark(description);
    }

    void object::trace(tracer& aTracer)
    {
        aTracer.mark(proto);
        for (auto const& p : properties.entries())
            if (p.key != 0u)
                aTracer.mark(p);
        if (cls == object_class::WeakRef)
        {
            // (a WeakRef: its target is weak (cleared when it dies))
            if (internal != Undefined)
                aTracer.after_marking(*this, [](cell& aCell, tracer& aTracer)
                {
                    auto& o = static_cast<object&>(aCell);
                    if (!aTracer.alive(o.internal))
                        o.internal = Undefined;
                });
        }
        else
            aTracer.mark(internal);
        for (auto const& [name, element] : privateElements)
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
