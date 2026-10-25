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

// The garbage collector: a non-moving mark-sweep collector of the heap's cells (as the JavaScript runtime's is)
//
// Roots: the native stacks (scanned conservatively: any word that is a value that refers to a live cell, or that points into a live
// cell, keeps that cell alive) — the running stack, the stacks of the code that the running code was resumed (or called) from and
// the stacks of suspended coroutines —, the interpreter's values (neos_rt_host::values), the rooted containers (value_list), the
// pending errors and the runtime's own state (root_tracer). Weak tables' entries are cleared when their keys or values die.
//
// A collection happens at an allocation (heap::make) when enough has been allocated since the last one (NEOS_LUA_GC_STRESS=N: every
// N allocations, for testing; NEOS_LUA_GC=0: never; NEOS_LUA_GC_VERBOSE=1: a line for each collection), or when collectgarbage is
// called.

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include "lua.hpp"

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

namespace neos::lua_runtime
{
    namespace
    {
        // --- the cells' memory: pages (of one size of slot each, or a large cell's) of one reserved region of address space, so that
        // whether a word points into a cell is known from the page it points into

        constexpr std::size_t PageShift = 16u;
        constexpr std::size_t PageSize = std::size_t{ 1u } << PageShift;
        constexpr std::size_t RegionSize = std::size_t{ 64u } << 30u;      // (address space only: pages are committed when used)
        constexpr std::size_t MaxSmall = 4096u;

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

        // (a slot's cell type (a free slot's: cell_type::Free): the byte after its cell's vtable pointer)
        constexpr std::size_t CellTypeOffset = 8u;
        inline cell_type slot_type(std::byte const* aSlot) { return static_cast<cell_type>(aSlot[CellTypeOffset]); }

        // (the host's external memory: the region (its committed pages), which generated code reads as memory 1)
        void publish_region(std::byte* aBase, std::size_t aSize)
        {
            auto const* h = host();
            if (h != nullptr && h->version >= 13u && h->external_memory != nullptr)
                h->external_memory(aBase, aSize);
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
                    std::fputs("Lua runtime: cannot reserve the heap's address space\n", stderr);
                    std::abort();
                }
                gCellBase = iBase;
                iPages.reserve(4096u);
                // (offset 0 is no cell: the first page is reserved)
                iPages.resize(1u);
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
                    auto* const result = c.freeList;
                    c.freeList = *static_cast<void**>(result);
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
            void release(void* aMemory) noexcept
            {
                auto const page = page_of(aMemory);
                auto& info = iPages[page];
                if (!info.raw)
                    static_cast<std::byte*>(aMemory)[CellTypeOffset] = static_cast<std::byte>(cell_type::Free);
                if (info.largeSize != 0u)
                {
                    auto const count = (info.largeSize + PageSize - 1u) / PageSize;
                    for (std::size_t p = page; p < page + count; ++p)
                        iPages[p] = page_info{};
                    decommit(page_address(page), count * PageSize);
                    iFreeRuns.emplace_back(page, count);
                    return;
                }
                auto& c = (info.raw ? iRawClasses : iClasses)[iClassOf[info.slotSize / 16u]];
                *static_cast<void**>(aMemory) = c.freeList;
                c.freeList = aMemory;
            }
            // (each cell's slot, live or free (aVisit(slot))
            template <typename Visit>
            void for_each_slot(Visit aVisit)
            {
                for (std::size_t page = 0u; page < iPages.size(); ++page)
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
                    auto const count = PageSize / info.slotSize;
                    for (std::size_t i = 0u; i < count; ++i)
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
            std::uint64_t committed() const { return iPages.size() * PageSize; }
            void publish() const { publish_region(iBase, iPages.size() * PageSize); }
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
            std::vector<std::pair<std::size_t, std::size_t>> iFreeRuns;
            size_class iClasses[ClassCount];
            size_class iRawClasses[ClassCount];
            std::uint8_t iClassOf[MaxSmall / 16u + 1u] = {};
        };

        // --- the calling thread's stacks

        NEOS_LUA_FAST_TLS thread_local stack_segment* tSegments = nullptr;
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

        lua_error* sErrors = nullptr;
    }

    // --- the collector

    class collector
    {
    public:
        static constexpr std::uint64_t MinimumTrigger = 8u * 1024u * 1024u;
    public:
        collector(heap& aHeap) : iHeap{ aHeap }
        {
            environment_flag("NEOS_LUA_GC_STRESS", iStress);
            std::uint64_t flag = 0u;
            if (environment_flag("NEOS_LUA_GC", flag) && flag == 0u)
                iPauses = 1;
            environment_flag("NEOS_LUA_GC_VERBOSE", iVerbose);
            triggers();
        }
    public:
        cell_memory& memory() { return iMemory; }
        bool collecting() const { return iCollecting; }
        void pause() { ++iPauses; }
        void resume() { --iPauses; }
        bool stopped() const { return iStopped; }
        void set_stopped(bool aStopped) { iStopped = aStopped; }
        std::uint64_t live_bytes() const { return iLiveBytes + iHeap.iAllocatedBytes; }
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
            if (iCollecting || iPauses != 0 || iStopped || !the_state().initialized)
                return false;
            auto const* h = host();
            return h != nullptr && h->version >= 8u && h->values != nullptr && stack_base() != nullptr;
        }
        void collect();
        // marking
        void push(cell& aCell)
        {
            aCell.marked = 1u;
            iWork.push_back(&aCell);
        }
        void mark_value(value aValue)
        {
            if (is_cell(aValue))
            {
                auto& c = cell_of<cell>(aValue);
                if (c.marked == 0u)
                    push(c);
            }
        }
        // (the live cell at an offset in the region, if there is one)
        cell* find(std::uint64_t aOffset) const
        {
            if (aOffset >= iMemory.committed())
                return nullptr;
            auto const address = iMemory.begin() + aOffset;
            auto const* const slot = iMemory.slot_at(address);
            if (slot == nullptr || reinterpret_cast<std::uintptr_t>(slot) != address || slot_type(slot) == cell_type::Free)
                return nullptr;
            return reinterpret_cast<cell*>(const_cast<std::byte*>(slot));
        }
        void mark_word(std::uint64_t aWord)
        {
            // (a value (of a live cell), or an address in a live cell)
            auto const tag = aWord & TagMask;
            if (tag >= TagString && tag <= TagOther)
            {
                auto* const c = find(handle_of(aWord));
                if (c == nullptr || c->marked != 0u || value_of(*c) != aWord)
                    return;
                push(*c);
                return;
            }
            if (aWord >= iMemory.begin() && aWord < iMemory.end())
                if (auto const* const slot = iMemory.slot_at(static_cast<std::uintptr_t>(aWord)); slot != nullptr && slot_type(slot) != cell_type::Free)
                {
                    auto* const c = reinterpret_cast<cell*>(const_cast<std::byte*>(slot));
                    if (c->marked == 0u)
                        push(*c);
                }
        }
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
        bool marked(value aValue) const
        {
            if (!is_cell(aValue))
                return true;
            return cell_of<cell>(aValue).marked != 0u;
        }
        void drain(tracer& aTracer)
        {
            while (!iWork.empty())
            {
                auto* const c = iWork.back();
                iWork.pop_back();
                c->trace(aTracer);
            }
        }
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
        void free_cell(cell* aCell);
    private:
        heap& iHeap;
        cell_memory iMemory;
        std::vector<cell*> iWork;
        std::uint64_t iStress = 0u;
        std::uint64_t iVerbose = 0u;
        int iPauses = 0;
        bool iStopped = false;
        bool iCollecting = false;
        std::uint64_t iLiveBytes = 0u;
        std::uint64_t iCollections = 0u;
    };

    namespace
    {
        collector* sTheCollector = nullptr;

        collector& create_collector()
        {
            static collector* sCollector = new collector{ the_heap() };
            sTheCollector = sCollector;
            return *sCollector;
        }

        inline collector& the_collector()
        {
            if (sTheCollector == nullptr) [[unlikely]]
                return create_collector();
            return *sTheCollector;
        }
    }

    // --- the heap

    heap::heap()
    {
    }

    heap::~heap()
    {
        // (the process is ending: cells are not destroyed)
    }

    heap& create_heap()
    {
        static heap* sHeap = new heap{};
        gHeap = sHeap;
        return *sHeap;
    }

    void* heap::allocate(std::size_t aSize)
    {
        return the_collector().memory().allocate(aSize);
    }

    void heap::deallocate(void* aMemory) noexcept
    {
        the_collector().memory().release(aMemory);
    }

    void heap::allocating()
    {
        the_collector().allocating();
    }

    void heap::add(cell& aCell, void* aMemory)
    {
        // (a cell is the start of its memory (its type has single inheritance), and its type follows its vtable pointer)
        if (static_cast<void*>(&aCell) != aMemory ||
            reinterpret_cast<std::byte const*>(&aCell.type) - reinterpret_cast<std::byte const*>(&aCell) != static_cast<std::ptrdiff_t>(CellTypeOffset))
            std::abort();
        if (the_collector().collecting())
            aCell.marked = 1u;
    }

    cell* heap::find(std::uint64_t aOffset) const
    {
        return the_collector().find(aOffset);
    }

    void* heap::allocate_raw(std::size_t aSize)
    {
        // (no collection: the cell whose storage it is may be changing)
        iAllocatedBytes += aSize;
        return the_collector().memory().allocate(aSize, true);
    }

    void heap::release_raw(void* aMemory) noexcept
    {
        the_collector().memory().release(aMemory);
    }

    void heap::collect()
    {
        the_collector().collect();
    }

    std::size_t heap::live_bytes() const
    {
        return static_cast<std::size_t>(the_collector().live_bytes());
    }

    void collect_garbage()
    {
        the_heap().collect();
    }

    bool gc_stopped()
    {
        return the_collector().stopped();
    }

    void gc_stop(bool aStop)
    {
        the_collector().set_stopped(aStop);
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

    // --- rooted containers

    value_list::value_list() : iHead{ &stacks().roots }
    {
        // (the storage of a value list that has ended (value lists are made and ended often, e.g. for each call of a native function))
        auto& spare = the_state().spareLists;
        if (!spare.empty())
        {
            iValues.swap(spare.back());
            spare.pop_back();
        }
        iNext = *iHead;
        if (iNext != nullptr)
            iNext->iPrevious = this;
        *iHead = this;
    }

    value_list::value_list(std::initializer_list<value> aValues) : value_list{}
    {
        iValues.assign(aValues.begin(), aValues.end());
    }

    value_list::value_list(std::span<value const> aValues) : value_list{}
    {
        iValues.assign(aValues.begin(), aValues.end());
    }

    value_list::value_list(value_list const& aOther) : value_list{}
    {
        iValues = aOther.iValues;
    }

    value_list& value_list::operator=(value_list const& aOther)
    {
        iValues = aOther.iValues;
        return *this;
    }

    value_list::~value_list()
    {
        if (iPrevious != nullptr)
            iPrevious->iNext = iNext;
        else if (*iHead == this)
            *iHead = iNext;
        if (iNext != nullptr)
            iNext->iPrevious = iPrevious;
        auto& spare = the_state().spareLists;
        if (iValues.capacity() != 0u && iValues.capacity() <= 256u && spare.size() < 64u)
        {
            iValues.clear();
            spare.push_back(std::move(iValues));
        }
    }

    namespace
    {
        static_values* sStatics = nullptr;
    }

    static_values::static_values() : iNext{ sStatics }
    {
        sStatics = this;
    }

    // --- errors (their values are roots while they exist)

    lua_error::lua_error(value aValue) : v{ aValue }, previous{ nullptr }, next{ sErrors }
    {
        if (sErrors != nullptr)
            sErrors->previous = this;
        sErrors = this;
    }

    lua_error::lua_error(lua_error const& aOther) : lua_error{ aOther.v }
    {
    }

    lua_error& lua_error::operator=(lua_error const& aOther)
    {
        v = aOther.v;
        return *this;
    }

    lua_error::~lua_error()
    {
        if (previous != nullptr)
            previous->next = next;
        else
            sErrors = next;
        if (next != nullptr)
            next->previous = previous;
    }

    // --- marking

    void tracer::mark(value aValue)
    {
        iCollector.mark_value(aValue);
    }

    void tracer::mark(cell const* aCell)
    {
        if (aCell != nullptr && aCell->marked == 0u)
            iCollector.push(const_cast<cell&>(*aCell));
    }

    void tracer::conservative(void const* aBegin, void const* aEnd)
    {
        iCollector.conservative(aBegin, aEnd);
    }

    bool tracer::alive(value aValue) const
    {
        return iCollector.marked(aValue);
    }

    void tracer::after_marking(cell& aCell, void (*aFunction)(cell& aCell, tracer& aTracer))
    {
        iCollector.iAfterMarking.emplace_back(&aCell, aFunction);
    }

    void suspended_stack(tracer& aTracer, stack_segment const* aTop, stack_segment const* aBottom, neos_rt_coroutine* aCoroutine)
    {
        for (auto const* s = aTop; s != nullptr && s != aBottom; s = s->previous)
            if (s->high != nullptr)
                aTracer.conservative(s->low, s->high);
        if (aCoroutine != nullptr)
            if (auto const* h = host(); h != nullptr && h->version >= 8u && h->values != nullptr)
                h->values(aCoroutine, [](void* aData, unsigned long long const* aValues, size_t aCount)
                {
                    static_cast<tracer*>(aData)->conservative(aValues, aValues + aCount);
                }, &aTracer);
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
            std::fputs("Lua runtime: out of memory while collecting garbage\n", stderr);
            std::abort();
        }
    }

    void collector::mark_and_sweep()
    {
        iCollecting = true;
        NEOS_LUA_SPILL_REGISTERS();
        tracer t{ *this };
        scan_stacks();
        if (auto const* h = host(); h != nullptr)
            h->values(nullptr, [](void* aData, unsigned long long const* aValues, size_t aCount)
            {
                static_cast<collector*>(aData)->conservative(aValues, aValues + aCount);
            }, this);
        for (auto* s = sStatics; s != nullptr; s = s->iNext)
            for (auto v : s->iValues)
                mark_value(v);
        for (auto* e = sErrors; e != nullptr; e = e->next)
            mark_value(e->v);
        for (auto const& r : roots())
            r.first(t);
        drain(t);
        for (std::size_t i = 0u; i < iAfterMarking.size(); ++i)
            iAfterMarking[i].second(*iAfterMarking[i].first, t);
        for (auto const& r : roots())
            if (r.second != nullptr)
                r.second(t);
        drain(t);
        iAfterMarking.clear();
        sweep();
        ++iCollections;
        triggers();
        iCollecting = false;
    }

    void collector::scan_stacks()
    {
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

    void collector::free_cell(cell* aCell)
    {
        aCell->~cell();
        iMemory.release(aCell);
    }

    void collector::sweep()
    {
        std::uint64_t liveBytes = 0u;
        std::size_t live = 0u;
        std::size_t freed = 0u;
        iMemory.for_each_slot([&](std::byte* aSlot)
        {
            if (slot_type(aSlot) == cell_type::Free)
                return;
            auto* const c = reinterpret_cast<cell*>(aSlot);
            if (c->marked != 0u)
            {
                c->marked = 0u;
                ++live;
                liveBytes += iMemory.size_of(c);
                if (c->type == cell_type::String)
                    liveBytes += static_cast<string_cell*>(c)->text.capacity();
                else if (c->type == cell_type::Table)
                    liveBytes += static_cast<table*>(c)->array.capacity() * sizeof(value) + static_cast<table*>(c)->nodes.capacity() * sizeof(table::node);
                return;
            }
            free_cell(c);
            ++freed;
        });
        iLiveBytes = liveBytes;
        if (iVerbose != 0u)
            std::fprintf(stderr, "[lua gc %llu: %zu live (%llu KB), %zu freed]\n", static_cast<unsigned long long>(iCollections), live,
                static_cast<unsigned long long>(liveBytes / 1024u), freed);
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

    // --- the traces of the cells

    void table::trace(tracer& aTracer)
    {
        aTracer.mark(metatable);
        if (weakKeys == 0u && weakValues == 0u)
        {
            aTracer.mark(array);
            for (auto const& n : nodes)
                if (n.key != Empty)
                {
                    aTracer.mark(n.key);
                    aTracer.mark(n.v);
                }
            return;
        }
        // (a weak table: its weak keys or values are not marked (entries whose keys or values die are removed after marking); a
        // weak key's value is marked (not an ephemeron table))
        if (weakValues == 0u)
            aTracer.mark(array);
        for (auto const& n : nodes)
            if (n.key != Empty)
            {
                if (weakKeys == 0u)
                    aTracer.mark(n.key);
                if (weakValues == 0u)
                    aTracer.mark(n.v);
            }
        aTracer.after_marking(*this, [](cell& aCell, tracer& aTracer)
        {
            auto& t = static_cast<table&>(aCell);
            auto const collectable = [](value v) { return is_table(v) || is_function(v) || tag_of(v) == TagOther; };
            if (t.weakValues != 0u)
                for (auto& v : t.array)
                    if (collectable(v) && !aTracer.alive(v))
                        v = Nil;
            for (auto& n : t.nodes)
                if (n.key != Empty && n.v != Nil)
                    if ((t.weakKeys != 0u && collectable(n.key) && !aTracer.alive(n.key)) || (t.weakValues != 0u && collectable(n.v) && !aTracer.alive(n.v)))
                        n.v = Nil;
            // (the dead keys are kept (as removed entries) until the table is rehashed: they are not traced (see insert))
            for (auto& n : t.nodes)
                if (n.key != Empty && n.v == Nil && collectable(n.key) && !aTracer.alive(n.key))
                    n.key = Nil;    // (a removed entry whose key is dead: a tombstone that matches no key)
        });
    }
}
