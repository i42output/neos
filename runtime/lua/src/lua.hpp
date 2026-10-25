/*
  lua.hpp

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

// The Lua runtime plugin (neos.runtime.lua): Lua's values (NaN-boxed i64s: see neos/language/lua.hpp), the heap's cells
// (strings, tables, functions, upvalue boxes, varargs, userdata, threads and large integers), calls, errors and the standard
// library. Code compiled from Lua calls its functions as imports named "lua::f" (see api.cpp).
//
// This is an implementation of the Lua 5.4 language and standard library as the Lua 5.4 Reference Manual describes them; it is
// not a copy of the reference implementation's source code, though some of its algorithms follow the reference implementation's (see
// runtime/lua/NOTICE).

#pragma once

#include <neos/runtime/plugin.h>
#include <neos/language/lua.hpp>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <functional>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#if defined(_MSC_VER)
#include <csetjmp>
#endif

namespace neos::lua_runtime
{
    // --- values

    using value = std::uint64_t;
    namespace tags = neos::language::lua::values;
    namespace layout = neos::language::lua::layout;

    inline constexpr value TagMask = tags::TagMask;
    inline constexpr value TagSpecial = tags::TagSpecial;
    inline constexpr value TagInteger = tags::TagInteger;
    inline constexpr value TagString = tags::TagString;
    inline constexpr value TagTable = tags::TagTable;
    inline constexpr value TagFunction = tags::TagFunction;
    inline constexpr value TagOther = tags::TagOther;
    inline constexpr value Nil = tags::Nil;
    inline constexpr value False = tags::False;
    inline constexpr value True = tags::True;
    inline constexpr value Empty = tags::Empty;
    inline constexpr value Exception = tags::Exception;
    inline constexpr value Dispatch = tags::Dispatch;
    inline constexpr value PayloadMask = tags::PayloadMask;

    inline value tag_of(value aValue) { return aValue & TagMask; }
    inline bool is_float(value aValue) { return aValue < TagSpecial; }
    inline bool is_nil(value aValue) { return aValue == Nil; }
    inline bool is_boolean(value aValue) { return aValue == True || aValue == False; }
    inline bool truthy(value aValue) { return (aValue | 1u) != (False | 1u); }
    inline value boolean(bool aValue) { return aValue ? True : False; }
    inline bool is_inline_integer(value aValue) { return tag_of(aValue) == TagInteger; }
    inline bool is_string(value aValue) { return tag_of(aValue) == TagString; }
    inline bool is_table(value aValue) { return tag_of(aValue) == TagTable; }
    inline bool is_function(value aValue) { return tag_of(aValue) == TagFunction; }
    inline bool is_cell(value aValue) { return aValue >= TagString && aValue <= (TagOther | PayloadMask); }
    inline std::uint64_t handle_of(value aValue) { return aValue & PayloadMask; }     ///< (a cell's offset in the heap's region)
    inline std::int64_t inline_integer(value aValue) { return static_cast<std::int64_t>(aValue << 16) >> 16; }
    inline bool fits_inline(std::int64_t aValue) { return aValue >= tags::MinInline && aValue <= tags::MaxInline; }
    inline double float_of(value aValue) { double d; std::memcpy(&d, &aValue, sizeof(d)); return d; }
    inline value make_float(double aValue)
    {
        value v;
        std::memcpy(&v, &aValue, sizeof(v));
        // (a NaN with a payload (that could be a tag): the canonical NaN of its sign)
        if (v >= TagSpecial)
            v = 0xFFF8000000000000ull;
        else if ((v & 0x7FF8000000000000ull) == 0x7FF8000000000000ull && (v & 0x0007FFFFFFFFFFFFull) != 0u)
            v &= 0xFFF8000000000000ull;
        return v;
    }

    // --- the heap's cells

    enum class cell_type : std::uint8_t
    {
        Free,       ///< (not a cell: a free slot of the heap's memory)
        String,
        Table,
        Function,
        Box,        ///< (an upvalue)
        Varargs,
        Integer,    ///< (an integer that does not fit inline)
        Userdata,
        Thread
    };

    class tracer;

    // (a cell: its type is at offset 8 (after its vtable pointer); its fields begin at offset 16 (see neos::language::lua::layout))
    struct cell
    {
        cell_type type;
        std::uint8_t marked = 0u;
        explicit cell(cell_type aType) : type{ aType } {}
        virtual ~cell() = default;
        virtual void trace(tracer&) {}
    };

    // (the heap's region: the cells, and the storage that generated code reads (see region_vector); its base)
    inline std::byte* gCellBase = nullptr;

    class heap
    {
        friend class collector;
    public:
        heap();
        ~heap();
    public:
        template <typename T, typename... Args>
        T& make(Args&&... aArguments)
        {
            if (++iAllocations >= iAllocationTrigger || iAllocatedBytes >= iByteTrigger)
                allocating();
            iAllocatedBytes += sizeof(T);
            void* memory = allocate(sizeof(T));
            T* result;
            try
            {
                result = new (memory) T(std::forward<Args>(aArguments)...);
            }
            catch (...)
            {
                deallocate(memory);
                throw;
            }
            add(*result, memory);
            return *result;
        }
        void account(std::size_t aBytes) { iAllocatedBytes += aBytes; }
        cell* find(std::uint64_t aOffset) const;    ///< (the live cell at the offset, if there is one)
        void collect();
        std::size_t live_bytes() const;
        // (storage in the region that is not a cell (see region_vector))
        void* allocate_raw(std::size_t aSize);
        void release_raw(void* aMemory) noexcept;
    private:
        void* allocate(std::size_t aSize);
        void deallocate(void* aMemory) noexcept;
        void allocating();
        void add(cell& aCell, void* aMemory);
    private:
        std::uint64_t iAllocations = 0u;
        std::uint64_t iAllocationTrigger = 4096u;
        std::uint64_t iAllocatedBytes = 0u;
        std::uint64_t iByteTrigger = 8u * 1024u * 1024u;
    };

    // (the heap: created when first used)
    heap& create_heap();
    inline heap* gHeap = nullptr;
    inline heap& the_heap()
    {
        if (gHeap == nullptr) [[unlikely]]
            return create_heap();
        return *gHeap;
    }

    inline value tag_for(cell_type aType)
    {
        switch (aType)
        {
        case cell_type::String: return TagString;
        case cell_type::Table: return TagTable;
        case cell_type::Function: return TagFunction;
        default: return TagOther;
        }
    }

    inline std::uint64_t offset_of(void const* aAddress) { return static_cast<std::uint64_t>(static_cast<std::byte const*>(aAddress) - gCellBase); }
    inline value value_of(cell const& aCell) { return tag_for(aCell.type) | offset_of(&aCell); }

    template <typename T>
    inline T& cell_of(value aValue)
    {
        return *reinterpret_cast<T*>(gCellBase + handle_of(aValue));
    }

    inline cell* cell_at(value aValue)
    {
        return is_cell(aValue) ? &cell_of<cell>(aValue) : nullptr;
    }

    inline bool is_type(value aValue, cell_type aType)
    {
        return tag_of(aValue) == TagOther && cell_of<cell>(aValue).type == aType;
    }

    // (a vector of trivially copyable elements in the heap's region, that generated code can read: its storage's offset (in the region),
    // its size and its capacity (in elements))
    template <typename T>
    class region_vector
    {
    public:
        region_vector() = default;
        region_vector(region_vector const&) = delete;
        region_vector& operator=(region_vector const&) = delete;
        ~region_vector() { release(); }
    public:
        std::size_t size() const { return static_cast<std::size_t>(iSize); }
        std::size_t capacity() const { return static_cast<std::size_t>(iCapacity); }
        bool empty() const { return iSize == 0u; }
        T* data() { return reinterpret_cast<T*>(gCellBase + iOffset); }
        T const* data() const { return reinterpret_cast<T const*>(gCellBase + iOffset); }
        T& operator[](std::size_t aIndex) { return data()[aIndex]; }
        T const& operator[](std::size_t aIndex) const { return data()[aIndex]; }
        T* begin() { return data(); }
        T* end() { return data() + iSize; }
        T const* begin() const { return data(); }
        T const* end() const { return data() + iSize; }
        operator std::span<T const>() const { return std::span<T const>{ data(), size() }; }
        void reserve(std::size_t aCapacity)
        {
            if (aCapacity <= iCapacity)
                return;
            auto* const storage = static_cast<T*>(the_heap().allocate_raw(aCapacity * sizeof(T)));
            if (iSize != 0u)
                std::memcpy(storage, data(), static_cast<std::size_t>(iSize) * sizeof(T));
            release_storage();
            iOffset = offset_of(storage);
            iCapacity = aCapacity;
        }
        void push_back(T const& aValue)
        {
            if (iSize == iCapacity)
                reserve(iCapacity < 4u ? 4u : static_cast<std::size_t>(iCapacity) * 2u);
            data()[iSize++] = aValue;
        }
        void resize(std::size_t aSize, T const& aValue = T{})
        {
            reserve(aSize);
            for (auto i = iSize; i < aSize; ++i)
                data()[i] = aValue;
            iSize = aSize;
        }
        template <typename Iterator>
        void assign(Iterator aBegin, Iterator aEnd)
        {
            iSize = 0u;
            reserve(static_cast<std::size_t>(std::distance(aBegin, aEnd)));
            for (auto i = aBegin; i != aEnd; ++i)
                data()[iSize++] = *i;
        }
        void clear() { iSize = 0u; }
    private:
        void release_storage()
        {
            if (iCapacity != 0u)
                the_heap().release_raw(data());
        }
        void release()
        {
            release_storage();
            iOffset = 0u;
            iSize = 0u;
            iCapacity = 0u;
        }
    private:
        std::uint64_t iOffset = 0u;
        std::uint64_t iSize = 0u;
        std::uint64_t iCapacity = 0u;
    };

    // --- rooted containers: a value_list is a root while it exists (a C++ local, temporary or static)

    class value_list
    {
    public:
        value_list();
        value_list(std::initializer_list<value> aValues);
        explicit value_list(std::span<value const> aValues);
        value_list(value_list const& aOther);
        value_list& operator=(value_list const& aOther);
        ~value_list();
    public:
        std::vector<value>& operator*() { return iValues; }
        std::vector<value> const& operator*() const { return iValues; }
        std::vector<value>* operator->() { return &iValues; }
        std::vector<value> const* operator->() const { return &iValues; }
        value& operator[](std::size_t aIndex) { return iValues[aIndex]; }
        value operator[](std::size_t aIndex) const { return iValues[aIndex]; }
        std::size_t size() const { return iValues.size(); }
        bool empty() const { return iValues.empty(); }
        void push_back(value aValue) { iValues.push_back(aValue); }
        void clear() { iValues.clear(); }
        std::span<value const> span() const { return std::span<value const>{ iValues }; }
        auto begin() { return iValues.begin(); }
        auto end() { return iValues.end(); }
        auto begin() const { return iValues.begin(); }
        auto end() const { return iValues.end(); }
    private:
        friend class collector;
        friend struct thread_stacks;
        std::vector<value> iValues;
        value_list** iHead = nullptr;       ///< (the list it is in: its thread's (a coroutine's are abandoned when it is deleted))
        value_list* iPrevious = nullptr;
        value_list* iNext = nullptr;
    };

    // (values that the runtime keeps in statics: roots for the program's life)
    class static_values
    {
    public:
        static_values();
        std::vector<value>& operator*() { return iValues; }
        std::vector<value>* operator->() { return &iValues; }
        value& operator[](std::size_t aIndex) { return iValues[aIndex]; }
        std::size_t size() const { return iValues.size(); }
        bool empty() const { return iValues.empty(); }
        void push_back(value aValue) { iValues.push_back(aValue); }
        void clear() { iValues.clear(); }
    private:
        friend class collector;
        std::vector<value> iValues;
        static_values* iNext = nullptr;
    };

    // --- marking

    class collector;

    class tracer
    {
    public:
        explicit tracer(collector& aCollector) : iCollector{ aCollector } {}
        tracer(tracer const&) = delete;
        tracer& operator=(tracer const&) = delete;
    public:
        void mark(value aValue);
        void mark(cell const* aCell);
        void mark(std::span<value const> aValues) { for (auto v : aValues) mark(v); }
        void mark(std::vector<value> const& aValues) { for (auto v : aValues) mark(v); }
        void mark(region_vector<value> const& aValues) { for (auto v : aValues) mark(v); }
        void conservative(void const* aBegin, void const* aEnd);
        bool alive(value aValue) const;
        void after_marking(cell& aCell, void (*aFunction)(cell& aCell, tracer& aTracer));
    private:
        collector& iCollector;
    };

    // (the runtime's own roots (its statics): a root_tracer marks them at each collection)
    class root_tracer
    {
    public:
        using function = void (*)(tracer& aTracer);
        explicit root_tracer(function aFunction, function aAfterMarking = nullptr);
    };

    // (stacks: the segments of native stacks that are not the running stack's live part (a coroutine's resumer's, a suspended
    // coroutine's, and on Windows, the stack of code that called generated code (which runs on a fiber of its own)))
    struct stack_segment
    {
        void const* low = nullptr;
        void const* high = nullptr;
        stack_segment* previous = nullptr;
    };
    stack_segment*& stack_segments();
    void const* stack_base();
    void set_stack_base(void const* aBase);
    void const* stack_pointer();
#if defined(_MSC_VER)
#define NEOS_LUA_SPILL_REGISTERS() std::jmp_buf neosSpilledRegisters; (void)setjmp(neosSpilledRegisters)
#else
#define NEOS_LUA_SPILL_REGISTERS() __builtin_unwind_init()
#endif
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
    void suspended_stack(tracer& aTracer, stack_segment const* aTop, stack_segment const* aBottom, neos_rt_coroutine* aCoroutine);

    class gc_pause
    {
    public:
        gc_pause();
        ~gc_pause();
    };

    // --- strings

    struct string_cell : cell
    {
        explicit string_cell(std::string aText) : cell{ cell_type::String }, text{ std::move(aText) } {}
        std::uint64_t hashValue = 0u;   ///< (layout::StringHash)
        std::string text;
        bool hashed = false;
        bool interned = false;
        bool pinned = false;            ///< (an interned string that lives as long as the program, e.g. a literal)
        std::size_t hash()
        {
            if (!hashed)
            {
                hashValue = std::hash<std::string_view>{}(text);
                hashed = true;
            }
            return hashValue;
        }
    };

    inline constexpr std::size_t MaxInternedLength = 40u;

    value make_string(std::string_view aText);
    value make_string(std::string&& aText);
    inline value make_string(char const* aText) { return make_string(std::string_view{ aText }); }
    inline value make_string(std::string const& aText) { return make_string(std::string_view{ aText }); }
    value intern(std::string_view aText, bool aPinned = false);
    inline std::string const& str(value aString) { return cell_of<string_cell>(aString).text; }
    inline std::string_view str_view(value aString) { return cell_of<string_cell>(aString).text; }

    // --- tables

    struct table : cell
    {
        table() : cell{ cell_type::Table } {}
        struct node
        {
            value key = Empty;      ///< (Empty: a free slot)
            value v = Nil;          ///< (Nil: its key's entry was removed (the key is kept until the table is rehashed))
        };
        // (the hash part's slots: a small table's (up to InlineCount) in the table itself)
        class node_storage
        {
        public:
            static constexpr std::size_t InlineCount = 4u;
        public:
            node_storage() = default;
            node_storage(node_storage const&) = delete;
            node_storage& operator=(node_storage const&) = delete;
            ~node_storage() { release(); }
        public:
            std::size_t size() const { return iSize; }
            std::size_t capacity() const { return iSize; }
            bool empty() const { return iSize == 0u; }
            node& operator[](std::size_t aIndex) { return iData[aIndex]; }
            node const& operator[](std::size_t aIndex) const { return iData[aIndex]; }
            node* begin() { return iData; }
            node* end() { return iData + iSize; }
            node const* begin() const { return iData; }
            node const* end() const { return iData + iSize; }
            void reset(std::size_t aSize)   ///< (aSize free slots (the slots there were are discarded))
            {
                release();
                iData = aSize <= InlineCount ? iInline : static_cast<node*>(the_heap().allocate_raw(aSize * sizeof(node)));
                iOffset = offset_of(iData);
                iSize = aSize;
                for (std::size_t i = 0u; i < aSize; ++i)
                    iData[i] = node{};
            }
        private:
            void release()
            {
                if (iData != iInline && iData != nullptr)
                    the_heap().release_raw(iData);
                iData = nullptr;
                iOffset = 0u;
                iSize = 0u;
            }
        private:
            // (layout::TableNodes, layout::TableNodesSize)
            std::uint64_t iOffset = 0u;
            std::uint64_t iSize = 0u;
            node* iData = nullptr;
            node iInline[InlineCount];
        };
        region_vector<value> array; ///< (the values of the keys 1 to n: layout::TableArray, layout::TableArraySize)
        value metatable = Nil;      ///< (layout::TableMetatable)
        node_storage nodes;         ///< (the others: open addressing (a power of 2 slots, or none))
        std::uint64_t used = 0u;    ///< (the slots whose keys are not Empty: layout::TableUsed)
        std::uint8_t weakKeys = 0u;
        std::uint8_t weakValues = 0u;
        value get(value aKey) const;
        value get_integer(std::int64_t aKey) const;
        value get_string(value aKey) const;
        void set(value aKey, value aValue);     ///< (raw: the key is valid (not nil nor NaN))
        void set_integer(std::int64_t aKey, value aValue);
        std::int64_t length() const;
        bool next(value& aKey, value& aValue) const;    ///< (false: the end (an invalid key throws))
        void trace(tracer& aTracer) override;
    private:
        std::size_t find(value aKey) const;     ///< (its slot, or nodes.size())
        void insert(value aKey, value aValue);
        void rehash(std::size_t aCount);
        void migrate();                         ///< (the keys that follow the array part: moved into it)
    };

    value make_table(std::size_t aArray = 0u, std::size_t aHash = 0u);
    inline table& tab(value aTable) { return cell_of<table>(aTable); }
    value normalize_key(value aKey);            ///< (a float key with an integer value: the integer)
    std::size_t hash_key(value aKey);
    bool key_equal(value aLeft, value aRight);

    // --- functions

    struct native_call;
    using native_function = void (*)(native_call& aCall);
    // (a native function's fast version, for the common case: its first result (Empty: it declines; its native function is called), 
    // and its second (Empty: none); it raises no errors)
    using fast_native = value (*)(value const* aArguments, std::size_t aCount, value& aSecond);

    struct function_cell : cell
    {
        function_cell() : cell{ cell_type::Function } {}
        // (a Lua function: its upvalues (boxes: layout::FunctionUpvalues), its chunk's environment (a box: layout::FunctionEnvironment),
        // its number (the dispatcher's), its WebAssembly function and its program)
        region_vector<value> upvalues;
        value environment = Nil;
        bool native = false;
        std::uint32_t number = 0u;
        std::uint32_t index = 0u;
        void* program = nullptr;
        // (a native function: its C++ function (and its fast version) and its name (for error messages))
        native_function function = nullptr;
        fast_native fast = nullptr;
        char const* name = "?";
        char const* qualified = "?";        ///< (its name with its library's, e.g. "string.rep")
        void trace(tracer& aTracer) override
        {
            aTracer.mark(upvalues);
            aTracer.mark(environment);
        }
    };

    inline function_cell& fun(value aFunction) { return cell_of<function_cell>(aFunction); }
    value make_native(native_function aFunction, char const* aName, std::initializer_list<value> aUpvalues = {});

    struct box_cell : cell
    {
        explicit box_cell(value aValue) : cell{ cell_type::Box }, v{ aValue } {}
        value v;
        void trace(tracer& aTracer) override { aTracer.mark(v); }
    };

    value make_box(value aValue);

    struct varargs_cell : cell
    {
        varargs_cell() : cell{ cell_type::Varargs } {}
        std::vector<value> values;
        void trace(tracer& aTracer) override { aTracer.mark(values); }
    };

    struct integer_cell : cell
    {
        explicit integer_cell(std::int64_t aValue) : cell{ cell_type::Integer }, i{ aValue } {}
        std::int64_t i;
    };

    struct userdata_cell : cell
    {
        userdata_cell() : cell{ cell_type::Userdata } {}
        value metatable = Nil;
        value user = Nil;                       ///< (its user value)
        void* data = nullptr;                   ///< (a native object, e.g. a file)
        void (*destroy)(void*) = nullptr;
        ~userdata_cell() override { if (destroy != nullptr) destroy(data); }
        void trace(tracer& aTracer) override { aTracer.mark(metatable); aTracer.mark(user); }
    };

    // --- numbers

    value make_integer(std::int64_t aValue);
    inline bool is_integer(value aValue) { return is_inline_integer(aValue) || is_type(aValue, cell_type::Integer); }
    inline bool is_number(value aValue) { return is_float(aValue) || is_integer(aValue); }
    inline std::int64_t integer_of(value aValue) { return is_inline_integer(aValue) ? inline_integer(aValue) : cell_of<integer_cell>(aValue).i; }
    inline double number_of(value aValue) { return is_float(aValue) ? float_of(aValue) : static_cast<double>(integer_of(aValue)); }
    bool float_to_integer(double aValue, std::int64_t& aResult);       ///< (exactly)
    bool to_integer(value aValue, std::int64_t& aResult, bool aStrings = false);   ///< (a number (or a string, if aStrings) with an exact integer value)
    bool to_number(value aValue, value& aResult);                      ///< (a number, or a string that is a numeral)
    bool string_to_number(std::string_view aText, value& aResult);
    std::string number_text(value aNumber);
    std::string integer_text(std::int64_t aValue);
    std::string float_text(double aValue);

    // --- the state

    struct thread_cell;

    // (the stacks of a thread (the main thread or a coroutine): the extra arguments of calls (beyond their first four), the argument
    // stack (for calls with any number of arguments, multiple returns ...), the to-be-closed variables and the results of the last
    // call (beyond its first))
    struct thread_stacks
    {
        std::vector<value> extra;
        std::vector<value> arguments;
        std::vector<value> tbc;
        std::vector<value> results;
        std::vector<struct protector*> protectors;  ///< (its protected calls being made: the innermost handles an error)
        std::vector<unsigned long long> boundaries; ///< (the frames (their ids) of the Lua functions whose native calls are calling Lua functions)
        value_list* roots = nullptr;                ///< (its rooted containers (on its native stack))
        void trace(tracer& aTracer) const
        {
            aTracer.mark(extra);
            aTracer.mark(arguments);
            aTracer.mark(tbc);
            aTracer.mark(results);
            for (auto* l = roots; l != nullptr; l = l->iNext)
                aTracer.mark(l->iValues);
        }
    };

    struct state
    {
        value globals = Nil;
        value registry = Nil;
        value loaded = Nil;         ///< (package.loaded)
        value stringMetatable = Nil;
        value typeMetatables[8] = { Nil, Nil, Nil, Nil, Nil, Nil, Nil, Nil };   ///< (nil, boolean, number, function, thread: debug.setmetatable)
        value pending = Nil;        ///< (the error of a call that returned Exception)
        value names[32] = {};       ///< (interned names of metamethods etc. (see name))
        value mainThread = Nil;
        thread_stacks mainStacks;
        thread_stacks* stacks = &mainStacks;    ///< (the running thread's)
        thread_cell* current = nullptr;         ///< (the running coroutine (null: the main thread))
        bool initialized = false;
        std::uint64_t randomState[4] = {};
        std::string warnBuffer;
        bool warnOn = false;
        bool exitRequested = false;     ///< (os.exit: the program ends (an error that nothing handles))
        int exitCode = 0;
        std::vector<std::vector<value>> spareLists;     ///< (the storage of value lists that have ended, for those that begin)
    };

    // (the state: created when first used)
    state& create_state();
    // (a thread local that is used often: the initial exec model where it is supported (no call to look it up))
#if defined(__GNUC__) && !defined(_WIN32)
#define NEOS_LUA_FAST_TLS [[gnu::tls_model("initial-exec")]]
#else
#define NEOS_LUA_FAST_TLS
#endif
    inline state* gState = nullptr;
    inline state& the_state()
    {
        if (gState == nullptr) [[unlikely]]
            return create_state();
        return *gState;
    }
    inline thread_stacks& stacks() { return *the_state().stacks; }

    enum name_index : std::size_t
    {
        NameIndex, NameNewIndex, NameGc, NameMode, NameLen, NameEq, NameAdd, NameSub, NameMul, NameMod, NamePow, NameDiv, NameIDiv,
        NameBAnd, NameBOr, NameBXor, NameShl, NameShr, NameUnm, NameBNot, NameLt, NameLe, NameConcat, NameCall, NameClose,
        NameToString, NameName, NameMetatable, NamePairs, NameCount
    };
    inline value name(name_index aName) { return the_state().names[aName]; }

    // --- errors

    struct lua_error
    {
        value v;
        explicit lua_error(value aValue);
        lua_error(lua_error const& aOther);
        lua_error& operator=(lua_error const& aOther);
        ~lua_error();
        lua_error* previous = nullptr;
        lua_error* next = nullptr;
    };

    [[noreturn]] void throw_value(value aValue);
    // (while it exists: a native function that a native function called (aIndirect) is running (its errors have no position), or Lua code)
    class indirect_scope
    {
    public:
        explicit indirect_scope(bool aIndirect);
        ~indirect_scope();
        indirect_scope(indirect_scope const&) = delete;
        indirect_scope& operator=(indirect_scope const&) = delete;
    private:
        bool iPrevious;
    };
    [[noreturn]] void runtime_error(std::string const& aMessage);   ///< (with the position of the Lua code that is running)
    std::string where(int aLevel);                                  ///< ("chunk:line: " of the function at the level (1: the running Lua function))
    std::string type_name(value aValue);
    std::string object_type_name(value aValue);                     ///< (its metatable's __name, else its type's name)

    // --- metatables and operations

    value metatable_of(value aValue);
    value metamethod(value aValue, name_index aEvent);              ///< (nil if none)
    value index(value aObject, value aKey, value aDescription = Nil);
    value index(value aObject, value aKey, value aDescription, bool aMissed);   ///< (aMissed: the object is a table without the key)
    void set_index(value aObject, value aKey, value aValue, value aDescription = Nil);
    value raw_get(value aTable, value aKey);
    void raw_set(value aTable, value aKey, value aValue);
    bool raw_equal(value aLeft, value aRight);
    bool equal(value aLeft, value aRight);
    bool less_than(value aLeft, value aRight);
    bool less_equal(value aLeft, value aRight);
    value arith(int aOperation, value aLeft, value aRight, value aLeftDescription = Nil, value aRightDescription = Nil);
    value unary_minus(value aValue, value aDescription = Nil);
    value bitwise_not(value aValue, value aDescription = Nil);
    value length(value aValue, value aDescription = Nil);
    value concat(value aLeft, value aRight);
    value concat(std::span<value const> aValues);
    value tostring(value aValue);                                   ///< (with __tostring and __name)
    std::string tostring_text(value aValue);
    std::string raw_tostring(value aValue);                         ///< (without metamethods)

    // --- calls

    // (a native function's call: its arguments and its results)
    struct native_call
    {
        std::span<value const> args;
        value_list& results;
        function_cell& self;
        bool direct = false;                ///< (called by Lua code (not by a native function): errors have its position)
        bool method = false;                ///< (called as a method (o:f(...)): its arguments are numbered from the first after self)
        std::size_t count() const { return args.size(); }
        value arg(std::size_t aIndex) const { return aIndex < args.size() ? args[aIndex] : Nil; }
        bool none(std::size_t aIndex) const { return aIndex >= args.size(); }
        void ret(value aValue) { results.push_back(aValue); }
    };

    void call(value aFunction, std::span<value const> aArguments, value_list& aResults, value aDescription = Nil);
    value call1(value aFunction, std::span<value const> aArguments);
    value call1(value aFunction, std::initializer_list<value> aArguments);
    value call_compiled(function_cell& aFunction, std::span<value const> aArguments);   ///< (its first result; the others in the thread's results (see results_count))
    std::size_t results_count();                                    ///< (the number of results of the compiled call that returned last)
    void set_results_count(std::size_t aCount);
    void set_results_count(std::size_t aCount, void* aProgram);
    neos_rt_host const* host();
    void* current_program();

    // --- native functions' arguments

    [[noreturn]] void argument_error(native_call const& aCall, std::size_t aIndex, std::string const& aMessage);
    [[noreturn]] void type_error(native_call const& aCall, std::size_t aIndex, char const* aExpected);
    std::int64_t check_integer(native_call const& aCall, std::size_t aIndex);
    std::int64_t optional_integer(native_call const& aCall, std::size_t aIndex, std::int64_t aDefault);
    double check_number(native_call const& aCall, std::size_t aIndex);
    value check_number_value(native_call const& aCall, std::size_t aIndex);
    std::string_view check_string(native_call const& aCall, std::size_t aIndex);  ///< (numbers are converted)
    value check_string_value(native_call const& aCall, std::size_t aIndex);
    std::string_view optional_string(native_call const& aCall, std::size_t aIndex, std::string_view aDefault);
    table& check_table(native_call const& aCall, std::size_t aIndex);
    void check_any(native_call const& aCall, std::size_t aIndex);

    // --- standard output (the host's streams)

    void write_standard(int aStream, std::string_view aText);
    void flush_standard(int aStream);

    // --- the libraries

    void open_libraries();
    void set_function(value aTable, char const* aName, native_function aFunction);
    value library_table(char const* aName);
    void register_library(value aTable, std::initializer_list<std::pair<char const*, native_function>> aFunctions);
    void register_library(value aTable, char const* aLibrary, std::initializer_list<std::pair<char const*, native_function>> aFunctions);
    void set_fast(value aTable, char const* aName, fast_native aFast);     ///< (a library function's fast version)
    std::string chunk_display_name(std::string const& aChunkName);     ///< (a chunk's name as Lua shows it, e.g. [string "..."])
    std::string read_source_file(std::string const& aPath, bool& aOk);

    void open_base();
    void open_string();
    void open_table();
    void open_math();
    void open_io();
    void open_os();
    void open_utf8();
    void open_coroutine();
    void open_debug();
    void open_package();

    // (coroutines)
    struct thread_cell : cell
    {
        thread_cell() : cell{ cell_type::Thread } {}
        ~thread_cell() override;
        value function = Nil;
        neos_rt_coroutine* coroutine = nullptr;
        enum class status_t { Suspended, Running, Normal, Dead } status = status_t::Suspended;
        bool started = false;
        bool threw = false;
        value_list* transferRoot = nullptr;
        std::vector<value> transfer;            ///< (the values passed by resume and yield)
        thread_stacks stacks;
        thread_cell* resumer = nullptr;
        void const* stackBase = nullptr;
        stack_segment* suspended = nullptr;
        stack_segment link;
        void* program = nullptr;
        void trace(tracer& aTracer) override;
    };
    std::size_t yield_values(std::span<value const> aValues, value_list& aResults);    ///< (coroutine.yield)
    bool yieldable();

    // (load: a chunk's function (or an error message))
    value load_chunk(std::string_view aSource, std::string const& aChunkName, std::string const& aMode, value aEnvironment, std::string& aError);
    // (the stack traceback of the running code (debug.traceback))
    std::string traceback(std::string const& aMessage, int aLevel, bool aNative = false);  ///< (aNative: the native function running (that raised an error) is the first)
    // (the script's arguments (the arg table))
    void set_script_arguments();
}
