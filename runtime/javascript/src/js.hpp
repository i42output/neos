/*
  js.hpp

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

// The JavaScript runtime plugin's core: values, the heap, strings, symbols, objects (with the internal methods of
// ECMAScript's object model), functions, environments, exceptions and the realm's intrinsics; the abstract operations
// of ECMAScript are declared here and defined in operations.cpp.
//
// Values are NaN-boxed (64 bits): a number is its IEEE 754 double (NaN canonical); any other value is a tag in the
// upper 16 bits (bit patterns that no canonical double has) and, for a value that refers to a cell of the heap, the
// cell's handle (the index of the slot of the heap's table that refers to it) in the lower 32 bits. Compiled code
// holds values as i64s. Handles (rather than addresses) are validated when a value comes from compiled code.
//
// Exceptions: the runtime's own code throws a js_exception (a C++ exception) for a JavaScript exception; the functions
// that compiled code calls (api.cpp) catch it, keep the exception value (pending) and return the value Exception,
// which compiled code tests for after each call that can throw.

#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>
#include <neos/runtime/plugin.h>

namespace neos::javascript_runtime
{
    // --- values

    using value = std::uint64_t;

    inline constexpr value TagMask = 0xFFFF000000000000ull;
    inline constexpr value TagSpecial = 0xFFF9000000000000ull;
    inline constexpr value TagString = 0xFFFA000000000000ull;
    inline constexpr value TagSymbol = 0xFFFB000000000000ull;
    inline constexpr value TagBigInt = 0xFFFC000000000000ull;
    inline constexpr value TagObject = 0xFFFD000000000000ull;
    inline constexpr value TagInternal = 0xFFFE000000000000ull;    ///< (cells that are not JavaScript values, e.g. environments)

    inline constexpr value Undefined = TagSpecial | 0u;
    inline constexpr value Null = TagSpecial | 1u;
    inline constexpr value False = TagSpecial | 2u;
    inline constexpr value True = TagSpecial | 3u;
    inline constexpr value Empty = TagSpecial | 4u;                 ///< (an array's hole; an uninitialized binding (TDZ))
    inline constexpr value Exception = TagSpecial | 5u;             ///< (a call threw: the exception is pending)
    inline constexpr value ReturnCompletion = TagSpecial | 6u;      ///< (a generator was resumed to return (from a yield))
    inline constexpr value CanonicalNaN = 0x7FF8000000000000ull;

    inline bool is_number(value aValue) { return aValue < TagSpecial; }
    inline double as_number(value aValue) { return std::bit_cast<double>(aValue); }
    inline value number(double aNumber)
    {
        auto const bits = std::bit_cast<value>(aNumber);
        return bits >= TagSpecial ? CanonicalNaN : bits;
    }
    inline value tag_of(value aValue) { return aValue & TagMask; }
    inline std::uint32_t handle_of(value aValue) { return static_cast<std::uint32_t>(aValue); }
    inline bool is_undefined(value aValue) { return aValue == Undefined; }
    inline bool is_null(value aValue) { return aValue == Null; }
    inline bool is_nullish(value aValue) { return aValue == Undefined || aValue == Null; }
    inline bool is_boolean(value aValue) { return aValue == True || aValue == False; }
    inline bool is_string(value aValue) { return tag_of(aValue) == TagString; }
    inline bool is_symbol(value aValue) { return tag_of(aValue) == TagSymbol; }
    inline bool is_bigint(value aValue) { return tag_of(aValue) == TagBigInt; }
    inline bool is_object(value aValue) { return tag_of(aValue) == TagObject; }
    inline bool is_internal(value aValue) { return tag_of(aValue) == TagInternal; }
    inline value boolean(bool aValue) { return aValue ? True : False; }
    inline bool is_property_key(value aValue) { return is_string(aValue) || is_symbol(aValue); }

    // --- the heap

    enum class cell_type : std::uint8_t
    {
        String,
        Symbol,
        BigInt,
        Object,
        Internal
    };

    class tracer;

    class cell
    {
    public:
        explicit cell(cell_type aType) : type{ aType } {}
        cell(cell const&) = delete;
        cell& operator=(cell const&) = delete;
        virtual ~cell() = default;
    public:
        // (the garbage collector (gc.hpp): a cell type that refers to other cells (values, or pointers to cells) overrides it, marking 
        // them (and calling its base's); the default refers to none)
        virtual void trace(tracer& aTracer) { (void)aTracer; }
    public:
        cell_type const type;
        std::uint32_t handle = 0u;
        std::uint8_t marked = 0u;
    };

    // --- the region: the heap's address space (gc.cpp), which compiled code reads as an external memory (see
    // neos::language::javascript::layout): its cells, and storage that is not a cell's but that compiled code reads (region_vector)

    inline std::byte* gRegionBase = nullptr;
    void* region_allocate(std::size_t aSize);           ///< (gc.cpp: storage in the region that is not a cell)
    void region_release(void* aMemory) noexcept;
    void publish_heap();                                ///< (the region, as the host's external memory (see layout::Memory))
    inline std::uint64_t region_offset(void const* aAddress)
    {
        return static_cast<std::uint64_t>(static_cast<std::byte const*>(aAddress) - gRegionBase);
    }

    // (a vector of trivially copyable elements in the region, that compiled code can read: its storage's offset (in the region), its
    // size and its capacity (in elements))
    template <typename T>
    class region_vector
    {
        static_assert(std::is_trivially_copyable_v<T>);
    public:
        region_vector() = default;
        region_vector(std::size_t aSize, T const& aValue) { resize(aSize, aValue); }
        region_vector(region_vector const&) = delete;
        region_vector& operator=(region_vector const&) = delete;
        region_vector(region_vector&& aOther) noexcept : iOffset{ aOther.iOffset }, iSize{ aOther.iSize }, iCapacity{ aOther.iCapacity }
        {
            aOther.iOffset = 0u;
            aOther.iSize = 0u;
            aOther.iCapacity = 0u;
        }
        region_vector& operator=(region_vector&& aOther) noexcept
        {
            if (&aOther != this)
            {
                release();
                std::swap(iOffset, aOther.iOffset);
                std::swap(iSize, aOther.iSize);
                std::swap(iCapacity, aOther.iCapacity);
            }
            return *this;
        }
        ~region_vector() { release(); }
    public:
        std::size_t size() const { return static_cast<std::size_t>(iSize); }
        std::size_t capacity() const { return static_cast<std::size_t>(iCapacity); }
        void const* offset_field() const { return &iOffset; }   ///< (compiled code reads its offset (u64), then its size (u64))
        bool empty() const { return iSize == 0u; }
        T* data() { return iCapacity != 0u ? reinterpret_cast<T*>(gRegionBase + iOffset) : nullptr; }
        T const* data() const { return iCapacity != 0u ? reinterpret_cast<T const*>(gRegionBase + iOffset) : nullptr; }
        T& operator[](std::size_t aIndex) { return data()[aIndex]; }
        T const& operator[](std::size_t aIndex) const { return data()[aIndex]; }
        T& back() { return data()[iSize - 1u]; }
        T const& back() const { return data()[iSize - 1u]; }
        T* begin() { return data(); }
        T* end() { return data() + iSize; }
        T const* begin() const { return data(); }
        T const* end() const { return data() + iSize; }
        void reserve(std::size_t aCapacity)
        {
            if (aCapacity <= iCapacity)
                return;
            auto* const storage = static_cast<T*>(region_allocate(aCapacity * sizeof(T)));
            if (iSize != 0u)
                std::memcpy(static_cast<void*>(storage), data(), static_cast<std::size_t>(iSize) * sizeof(T));
            release_storage();
            iOffset = region_offset(storage);
            iCapacity = aCapacity;
        }
        void push_back(T const& aValue)
        {
            if (iSize == iCapacity)
                reserve(iCapacity < 4u ? 4u : static_cast<std::size_t>(iCapacity) * 2u);
            data()[iSize++] = aValue;
        }
        void pop_back() { --iSize; }
        void emplace_back(T const& aValue) { push_back(aValue); }
        T& front() { return data()[0]; }
        T const& front() const { return data()[0]; }
        T* insert(T* aPosition, T const& aValue)
        {
            auto const index = aPosition - begin();
            if (iSize == iCapacity)
                reserve(iCapacity < 4u ? 4u : static_cast<std::size_t>(iCapacity) * 2u);
            std::memmove(static_cast<void*>(data() + index + 1), data() + index, (static_cast<std::size_t>(iSize) - index) * sizeof(T));
            data()[index] = aValue;
            ++iSize;
            return data() + index;
        }
        template <typename Iterator>
        T* insert(T* aPosition, Iterator aFirst, Iterator aLast)
        {
            auto const index = aPosition - begin();
            auto const count = static_cast<std::size_t>(std::distance(aFirst, aLast));
            if (iSize + count > iCapacity)
                reserve(std::max<std::size_t>(iSize + count, static_cast<std::size_t>(iCapacity) * 2u));
            std::memmove(static_cast<void*>(data() + index + count), data() + index, (static_cast<std::size_t>(iSize) - index) * sizeof(T));
            std::copy(aFirst, aLast, data() + index);
            iSize += count;
            return data() + index;
        }
        T* erase(T* aFirst, T* aLast)
        {
            std::memmove(static_cast<void*>(aFirst), aLast, static_cast<std::size_t>(end() - aLast) * sizeof(T));
            iSize -= static_cast<std::uint64_t>(aLast - aFirst);
            return aFirst;
        }
        T* erase(T* aPosition) { return erase(aPosition, aPosition + 1); }
        template <typename Iterator>
        void assign(Iterator aFirst, Iterator aLast)
        {
            auto const count = static_cast<std::size_t>(std::distance(aFirst, aLast));
            if (count > iCapacity)
            {
                // (no copy of the old elements)
                iSize = 0u;
                reserve(count);
            }
            std::copy(aFirst, aLast, data());
            iSize = count;
        }
        void assign(std::size_t aCount, T const& aValue)
        {
            iSize = 0u;
            resize(aCount, aValue);
        }
        void shrink_to_fit()
        {
            if (iSize == iCapacity)
                return;
            if (iSize == 0u)
            {
                release();
                return;
            }
            region_vector smaller;
            smaller.reserve(static_cast<std::size_t>(iSize));
            std::memcpy(static_cast<void*>(smaller.data()), data(), static_cast<std::size_t>(iSize) * sizeof(T));
            smaller.iSize = iSize;
            *this = std::move(smaller);
        }
        void resize(std::size_t aSize, T const& aValue = T{})
        {
            // (growing as push_back does (an element at a time: amortized constant))
            if (aSize > iCapacity)
                reserve(std::max<std::size_t>(aSize, static_cast<std::size_t>(iCapacity) * 2u));
            for (auto i = iSize; i < aSize; ++i)
                data()[i] = aValue;
            iSize = aSize;
        }
        template <typename Predicate>
        void erase_if(Predicate aPredicate)
        {
            std::uint64_t kept = 0u;
            for (std::uint64_t i = 0u; i < iSize; ++i)
                if (!aPredicate(data()[i]))
                    data()[kept++] = data()[i];
            iSize = kept;
        }
        void clear() { iSize = 0u; }
        operator std::span<T const>() const { return std::span<T const>{ data(), size() }; }
    private:
        void release_storage()
        {
            if (iCapacity != 0u)
                region_release(data());
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

    class object;
    struct function_object;

    // (the heap: cells, by handle; a mark-sweep garbage collector (gc.cpp) frees the cells that cannot be reached (cells do not move:
    // a cell's handle is its offset in the region, divided by 16 (the region's size is 64 GB: it is 32 bits); a cell's memory (and so
    // its handle) is reused once it is freed). Compiled code finds a handle's cell from the handle (layout::HandleShift))
    class array_object;
    struct shape;
    struct property_descriptor;
    template <typename T>
    shape* type_root_shape();   ///< (the shape of no properties of objects of type T (other than object and function_object))

    class heap
    {
    public:
        heap();
        ~heap();
    public:
        template <typename T, typename... Args>
        T& make(Args&&... aArguments)
        {
            iAllocatedBytes += sizeof(T);
            if (++iAllocations >= iAllocationTrigger || iAllocatedBytes >= iByteTrigger)
                allocating();
            void* const memory = allocate(sizeof(T));
            T* result;
            try
            {
                result = ::new (memory) T{ std::forward<Args>(aArguments)... };
            }
            catch (...)
            {
                deallocate(memory);
                throw;
            }
            add(*result, memory);
            // (an object of another type than object and function_object has shapes of its own type's (see shape); one whose [[Get]]
            // is not the ordinary one has none (the property caches do not use it), except an array (its get of a key that is not
            // "length" or an index is the ordinary one: the get caches use it for such keys))
            if constexpr (requires { result->properties.make_dictionary(); })
                if constexpr (!std::is_same_v<T, object> && !std::is_same_v<T, function_object>)
                {
                    constexpr bool ordinaryGet =
                        std::is_same_v<decltype(&T::get), value (object::*)(value, value)> &&
                        std::is_same_v<decltype(&T::get_own_property), std::optional<property_descriptor> (object::*)(value)> &&
                        std::is_same_v<decltype(&T::get_prototype_of), value (object::*)()>;
                    if constexpr (!ordinaryGet)
                        result->ordinaryGet = false;
                    if constexpr (ordinaryGet || std::is_same_v<T, array_object>)
                        result->properties.set_root(type_root_shape<T>());
                    else
                        result->properties.make_dictionary();
                }
            return *result;
        }
        static constexpr unsigned HandleShift = 4u;     ///< (a handle: its cell's offset in the region >> HandleShift)
        cell* find(std::uint32_t aHandle) const;        ///< (gc.cpp: the live cell whose handle it is (null if none))
        cell& at(std::uint32_t aHandle) const
        {
            return *reinterpret_cast<cell*>(gRegionBase + (static_cast<std::size_t>(aHandle) << HandleShift));
        }
        void account(std::size_t aBytes) { iAllocatedBytes += aBytes; }    ///< (memory that a new cell owns (e.g. a string's text))
        void collect();                                                     ///< (a full collection now (unless collection is disabled))
    private:
        void* allocate(std::size_t aSize);
        void deallocate(void* aMemory) noexcept;
        void allocating();
        void add(cell& aCell, void* aMemory);
    private:
        friend class collector;
        std::uint64_t iAllocations = 0u;
        std::uint64_t iAllocationTrigger = ~std::uint64_t{};
        std::uint64_t iAllocatedBytes = 0u;
        std::uint64_t iByteTrigger = ~std::uint64_t{};
    };

    heap& the_heap();

    // --- rooted containers: the values in a container that is not a cell's (e.g. a C++ local, or a static) are kept alive by the 
    // garbage collector only if its memory is rooted (the collector scans the native stacks, not the C++ heap): a container of values 
    // (or of structures with values) that outlives an allocation (or a call) uses root_allocator (e.g. value_list), unless it is a 
    // member of a cell (whose trace marks them)

    void* root_allocate(std::size_t aBytes);            ///< (gc.cpp: memory whose words are roots until it is freed)
    void root_deallocate(void* aMemory) noexcept;

    template <typename T>
    struct root_allocator
    {
        using value_type = T;
        root_allocator() noexcept = default;
        template <typename U>
        root_allocator(root_allocator<U> const&) noexcept {}
        T* allocate(std::size_t aCount)
        {
            if (aCount > std::numeric_limits<std::size_t>::max() / sizeof(T))
                throw std::bad_alloc{};
            return static_cast<T*>(root_allocate(aCount * sizeof(T)));
        }
        void deallocate(T* aMemory, std::size_t) noexcept { root_deallocate(aMemory); }
        template <typename U>
        bool operator==(root_allocator<U> const&) const noexcept { return true; }
    };

    template <typename T>
    using rooted_vector = std::vector<T, root_allocator<T>>;
    using value_list = rooted_vector<value>;           ///< (a list of values that are kept alive while it lives)

    inline value value_of(cell const& aCell)
    {
        switch (aCell.type)
        {
        case cell_type::String: return TagString | aCell.handle;
        case cell_type::Symbol: return TagSymbol | aCell.handle;
        case cell_type::BigInt: return TagBigInt | aCell.handle;
        case cell_type::Object: return TagObject | aCell.handle;
        default: return TagInternal | aCell.handle;
        }
    }

    template <typename T>
    inline T& cell_of(value aValue)
    {
        return static_cast<T&>(the_heap().at(handle_of(aValue)));
    }

    bool valid(value aValue);   ///< (a value from compiled code refers to a cell of the heap of its type (if it refers to one))

    // --- strings (sequences of UTF-16 code units)

    class string : public cell
    {
    public:
        string() : cell{ cell_type::String } {}
        explicit string(std::u16string aText) : cell{ cell_type::String }, text{ std::move(aText) }, length{ static_cast<std::uint32_t>(text.size()) } {}
        string(value aLeft, value aRight, std::uint32_t aLength) : cell{ cell_type::String }, left{ aLeft }, right{ aRight }, length{ aLength } {}
        struct small_tag {};
        string(small_tag, std::u16string_view aText) : cell{ cell_type::String }, length{ static_cast<std::uint32_t>(aText.size()) }, smallText{ true }
        {
            std::copy(aText.begin(), aText.end(), small);
        }
    public:
        void trace(tracer& aTracer) override;
    public:
        std::u16string const& flat();
        std::u16string_view chars() { return smallText ? std::u16string_view{ small, length } : std::u16string_view{ flat() }; }  ///< (its code units (flat))
        std::size_t hash();
        std::int64_t array_index();     ///< (-1: not an array index)
    public:
        std::u16string text;            ///< (none while it is a rope: its parts (left and right) are concatenated when it is flattened)
        value left = 0u;
        value right = 0u;
        std::uint32_t length = 0u;
        std::size_t hashValue = 0u;
        bool hashed = false;
        bool interned = false;
        bool pinned = false;            ///< (interned for as long as the program runs (e.g. compiled code's literals); else an interned
                                        ///< string lives while it is referred to)
        std::int64_t arrayIndex = -2;   ///< (-2: not computed yet)
        // (a short string's code units, in place (no memory of their own) until it is flattened (text is then made from them))
        static constexpr std::size_t SmallCapacity = 16u;
        bool smallText = false;
        char16_t small[SmallCapacity];
    };

    inline constexpr std::uint32_t MaxStringLength = (1u << 30) - 25u;

    value make_string(std::u16string aText);
    value make_string(std::u16string_view aText);
    value make_small_string(std::u16string_view aText);        ///< (at most string::SmallCapacity code units)
    value make_string_utf8(std::string_view aText);             ///< (WTF-8: surrogates are allowed; C0 80 is NUL)
    value make_string(char16_t aCharacter);
    value ascii(char const* aText);                             ///< (an interned string)
    value intern(value aString);                                ///< (a string (that lives while it is referred to) as an interned string)
    value intern(std::u16string_view aText);                    ///< (an interned string that lives as long as the program (pinned))
    value concat(value aLeft, value aRight);                    ///< (strings)
    std::u16string const& str(value aString);                   ///< (a string's code units (flattened))
    std::string utf8(value aString);                            ///< (lone surrogates: U+FFFD)
    std::string wtf8(std::u16string_view aText);
    std::u16string utf16(std::string_view aUtf8);               ///< (WTF-8 as make_string_utf8)
    std::u16string utf16_ascii(std::string_view aText);
    inline std::uint32_t string_length(value aString) { return cell_of<string>(aString).length; }
    bool string_equal(value aLhs, value aRhs);
    std::optional<std::uint32_t> array_index(value aKey);       ///< (a key that is an array index: its index)
    value index_key(std::uint64_t aIndex);                      ///< (the key of an index (a canonical numeric string))

    // --- symbols

    class symbol : public cell
    {
    public:
        symbol() : cell{ cell_type::Symbol } {}
        explicit symbol(value aDescription) : cell{ cell_type::Symbol }, description{ aDescription } {}
    public:
        void trace(tracer& aTracer) override;
    public:
        value description = Undefined;
        bool registered = false;        ///< (Symbol.for)
        bool privateName = false;       ///< (a class's private name (#x): never visible to the program as a value)
    };

    value make_symbol(value aDescription);

    // --- BigInts (bigint.cpp)

    class bigint : public cell
    {
    public:
        bigint() : cell{ cell_type::BigInt } {}
    public:
        bool negative = false;
        std::vector<std::uint32_t> magnitude;   ///< (little endian; no leading zeros: zero has none)
    };

    value make_bigint(std::int64_t aValue);
    value make_bigint_u64(std::uint64_t aValue);
    value number_to_bigint(double aValue);                      ///< (NumberToBigInt: a RangeError if it is not an integer)
    std::optional<value> string_to_bigint(std::u16string_view aText);  ///< (StringToBigInt: none if it is not one)
    value parse_bigint_literal(std::string_view aText);        ///< (a BigInt literal's digits (with its 0x, 0o or 0b prefix, without its n))
    std::u16string bigint_to_string(value aBigInt, int aRadix = 10);
    double bigint_to_number(value aBigInt);
    int bigint_compare(value aLhs, value aRhs);                 ///< (-1, 0 or 1)
    int bigint_compare_number(value aLhs, double aRhs);         ///< (-1, 0 or 1; 2 if aRhs is NaN)
    bool bigint_equal(value aLhs, value aRhs);
    bool bigint_is_zero(value aBigInt);
    value bigint_add(value aLhs, value aRhs);
    value bigint_subtract(value aLhs, value aRhs);
    value bigint_multiply(value aLhs, value aRhs);
    value bigint_divide(value aLhs, value aRhs);
    value bigint_remainder(value aLhs, value aRhs);
    value bigint_power(value aLhs, value aRhs);
    value bigint_negate(value aBigInt);
    value bigint_bitwise(int aOperation, value aLhs, value aRhs);  ///< (0 and, 1 or, 2 xor)
    value bigint_not(value aBigInt);
    value bigint_shift_left(value aLhs, value aRhs);            ///< (a negative shift is a right shift)
    value bigint_as_int_n(std::uint64_t aBits, value aBigInt);
    value bigint_as_uint_n(std::uint64_t aBits, value aBigInt);
    std::int64_t bigint_to_int64(value aBigInt);                ///< (modulo 2^64)
    std::uint64_t bigint_to_uint64(value aBigInt);
    value to_bigint(value aValue);                              ///< (ToBigInt)

    // --- property keys, descriptors and storage

    enum : std::uint8_t
    {
        AttributeWritable = 0x01u,
        AttributeEnumerable = 0x02u,
        AttributeConfigurable = 0x04u,
        AttributeAccessor = 0x08u,
        AttributesDefault = AttributeWritable | AttributeEnumerable | AttributeConfigurable,
        AttributesHidden = AttributeWritable | AttributeConfigurable    ///< (the built-ins' methods)
    };

    struct property_descriptor
    {
        std::optional<value> valueField;
        std::optional<value> getter;
        std::optional<value> setter;
        std::optional<bool> writable;
        std::optional<bool> enumerable;
        std::optional<bool> configurable;

        bool is_accessor() const { return getter.has_value() || setter.has_value(); }
        bool is_data() const { return valueField.has_value() || writable.has_value(); }
        bool is_generic() const { return !is_accessor() && !is_data(); }
        static property_descriptor data(value aValue, std::uint8_t aAttributes)
        {
            property_descriptor result;
            result.valueField = aValue;
            result.writable = (aAttributes & AttributeWritable) != 0u;
            result.enumerable = (aAttributes & AttributeEnumerable) != 0u;
            result.configurable = (aAttributes & AttributeConfigurable) != 0u;
            return result;
        }
        static property_descriptor accessor(value aGetter, value aSetter, std::uint8_t aAttributes)
        {
            property_descriptor result;
            result.getter = aGetter;
            result.setter = aSetter;
            result.enumerable = (aAttributes & AttributeEnumerable) != 0u;
            result.configurable = (aAttributes & AttributeConfigurable) != 0u;
            return result;
        }
    };

    struct property
    {
        value key = 0u;                 ///< (0: deleted)
        value data = Undefined;         ///< (a data property's value; an accessor's getter)
        value setter = Undefined;
        std::uint8_t attributes = 0u;

        bool accessor() const { return (attributes & AttributeAccessor) != 0u; }
        bool writable() const { return (attributes & AttributeWritable) != 0u; }
        bool enumerable() const { return (attributes & AttributeEnumerable) != 0u; }
        bool configurable() const { return (attributes & AttributeConfigurable) != 0u; }
        property_descriptor descriptor() const
        {
            return accessor() ? property_descriptor::accessor(data, setter, attributes) : property_descriptor::data(data, attributes);
        }
    };

    // (the layout of objects' own properties: their keys, in the order they were added (their values and attributes are the objects'
    // own); objects whose properties were added in the same order share one (the shapes are a tree: a shape and a key added to it
    // make its transition). A shape is immutable: an object that adds a property moves to the shape's transition; one whose layout
    // changes otherwise (a property deleted) has no shape (its table is its own: "dictionary" mode). The property caches (api.cpp)
    // compare objects' shapes. Shapes are never freed (their number is limited); their keys are not kept alive: a shape's keys are
    // those of the objects that have it (they keep them alive), so a key that is freed (and its handle reused) cannot be confused)
    struct shape
    {
        shape* parent = nullptr;
        value key = 0u;                 ///< (the key that its parent's transition to it added)
        std::uint32_t count = 0u;       ///< (its keys)
        shape* transition(value aKey);  ///< (the shape with aKey added; null if there are no more shapes (the table has none))
        std::optional<std::uint32_t> position(value aKey);  ///< (a key's position)
    private:
        std::vector<std::pair<value, shape*>> iTransitions;
        std::unique_ptr<std::unordered_map<value, shape*>> iManyTransitions;
        std::unique_ptr<std::unordered_map<value, std::uint32_t>> iIndex;   ///< (built when a key is found in a shape of many keys)
    };
    shape* root_shape();    ///< (the shape of no properties)

    // (an object's own properties, in the order they were created: in the region (compiled code reads them: layout::ObjectShape,
    // layout::ObjectProperties and layout::ObjectPropertyCount are its first members'))
    class property_table
    {
    public:
        property_table() = default;
        property_table(property_table&&) noexcept = default;
        property_table& operator=(property_table&&) noexcept = default;
    public:
        property* find(value aKey);
        property const* find(value aKey) const { return const_cast<property_table*>(this)->find(aKey); }
        property& add(value aKey, value aData, value aSetter, std::uint8_t aAttributes);
        property& add(value aKey, value aData, std::uint8_t aAttributes, shape* aShape);  ///< (aShape: its shape's transition for aKey)
        void remove(value aKey);
        void reserve(std::size_t aCapacity) { iEntries.reserve(aCapacity); }
        void make_dictionary();                     ///< (no shape (its layout its own) from now on)
        void set_root(shape* aRoot) { iShape = aRoot; }    ///< (its shape (it has no properties): a root shape)
        std::size_t size() const { return iEntries.size() - iDeleted; }
        template <typename F>
        void for_each(F&& aFunction) const
        {
            for (auto const& e : iEntries)
                if (e.key != 0u)
                    aFunction(e);
        }
        std::span<property const> entries() const { return std::span<property const>{ iEntries.data(), iEntries.size() }; }
        property& at(std::uint32_t aPosition) { return iEntries[aPosition]; }   ///< (a property by its position (its shape's))
        shape* layout() const { return iShape; }       ///< (its shape (null: none))
        // (the members that compiled code reads (the runtime checks their offsets: see layout::ObjectShape))
        void const* shape_field() const { return &iShape; }
        void const* entries_field() const { return &iEntries; }   ///< (its offset (u64), then its size (u64))
    private:
        void reindex();
    private:
        shape* iShape = root_shape();
        region_vector<property> iEntries;
        std::unique_ptr<std::unordered_map<value, std::uint32_t>> iIndex;   ///< (no shape: its keys' positions (if it has many))
        std::uint32_t iDeleted = 0u;
    };

    // --- objects

    enum class object_class : std::uint8_t
    {
        Ordinary,
        Array,
        Function,
        Error,
        Boolean,
        Number,
        String,
        Symbol,
        BigInt,
        Date,
        RegExp,
        Arguments,
        Map,
        Set,
        WeakMap,
        WeakSet,
        WeakRef,
        FinalizationRegistry,
        Promise,
        Proxy,
        ArrayBuffer,
        SharedArrayBuffer,
        DataView,
        TypedArray,
        Generator,
        AsyncGenerator,
        AsyncFromSyncIterator,
        Iterator,               ///< (the built-ins' other iterators)
        ArrayIterator,
        MapIterator,
        SetIterator,
        StringIterator,
        RegExpStringIterator,
        IteratorHelper,
        WrapForValidIterator,
        ForIn,
        ModuleNamespace,
        DisposableStack,
        AsyncDisposableStack,
        RawJSON,                ///< (JSON.rawJSON's objects ([[IsRawJSON]]))
        HTMLDDA,                ///< (the test262 host's $262.IsHTMLDDA ([[IsHTMLDDA]], Annex B.3.6))
        TemporalDuration,       ///< (temporal_*.cpp: the Temporal objects (temporal::temporal_object))
        TemporalInstant,
        TemporalPlainDate,
        TemporalPlainTime,
        TemporalPlainDateTime,
        TemporalPlainYearMonth,
        TemporalPlainMonthDay,
        TemporalZonedDateTime,
        IntlCollator,           ///< (intl*.cpp: the Intl objects (intl::intl_object))
        IntlDateTimeFormat,
        IntlDisplayNames,
        IntlDurationFormat,
        IntlListFormat,
        IntlLocale,
        IntlNumberFormat,
        IntlPluralRules,
        IntlRelativeTimeFormat,
        IntlSegmenter,
        IntlSegments,
        IntlSegmentIterator
    };

    using arguments = std::span<value const>;

    class object : public cell
    {
    public:
        explicit object(value aPrototype, object_class aClass = object_class::Ordinary) : cell{ cell_type::Object }, proto{ aPrototype }, cls{ aClass } {}
    public:
        void trace(tracer& aTracer) override;   ///< (its prototype, properties, internal value (a WeakRef's target is weak) and private elements)
    public:
        // ECMAScript's internal methods
        virtual value get_prototype_of();
        virtual bool set_prototype_of(value aPrototype);
        virtual bool is_extensible();
        virtual bool prevent_extensions();
        virtual std::optional<property_descriptor> get_own_property(value aKey);
        virtual bool define_own_property(value aKey, property_descriptor const& aDescriptor);
        virtual bool has_property(value aKey);
        virtual value get(value aKey, value aReceiver);
        virtual bool set(value aKey, value aValue, value aReceiver);
        virtual bool delete_property(value aKey);
        virtual value_list own_property_keys();
        virtual bool callable() const { return false; }
        virtual bool constructor() const { return false; }
        virtual value call(value aThis, arguments aArguments);
        virtual value construct(arguments aArguments, value aNewTarget);
    public:
        // (ordinary versions (OrdinaryGetOwnProperty, ...) that exotic objects use)
        std::optional<property_descriptor> ordinary_get_own_property(value aKey);
        bool ordinary_define_own_property(value aKey, property_descriptor const& aDescriptor);
        value ordinary_get(value aKey, value aReceiver);
        bool ordinary_set(value aKey, value aValue, value aReceiver);
        bool ordinary_delete(value aKey);
        value_list ordinary_own_property_keys();
        bool ordinary_set_prototype_of(value aPrototype);
        // (a property that the object is created with (or one known not to exist yet): no checks)
        void put(value aKey, value aValue, std::uint8_t aAttributes = AttributesHidden);
        void put_accessor(value aKey, value aGetter, value aSetter, std::uint8_t aAttributes = AttributeConfigurable);
        property* own(value aKey) { return properties.find(aKey); }
    public:
        // (compiled code reads the prototype and the properties (see neos::language::javascript::layout): their offsets are checked 
        // when the runtime starts)
        value proto;
        property_table properties;
        object_class cls;
        bool extensible = true;
        bool mirrored = false;          ///< (a global object whose properties have mirrors: changes to them call global_object_changed;
                                        ///< it has no shape (the property caches do not use it))
        bool ordinaryGet = true;        ///< (its [[Get]] (and [[GetOwnProperty]] and [[GetPrototypeOf]]) are the ordinary ones)
        value internal = Undefined;     ///< ([[BooleanData]], [[NumberData]], [[StringData]], [[SymbolData]], [[BigIntData]], [[DateValue]],
                                        ///< [[ErrorData]] (its stack) ... as the class has)
        std::uint32_t privateBrand = 0u;
        std::vector<std::pair<value, property>> privateElements;    ///< (private names (symbols) -> fields, methods and accessors)
    };

    value make_object(value aPrototype);                ///< (an ordinary object)
    value make_object();                                ///< (with %Object.prototype%)
    inline object& obj(value aObject) { return cell_of<object>(aObject); }

    // (an object whose index properties are elements: dense (a vector) while its index properties are writable, enumerable,
    // configurable data properties within reach of its length; otherwise they are ordinary properties)
    class array_object : public object
    {
    public:
        explicit array_object(value aPrototype) : object{ aPrototype, object_class::Array } {}
    public:
        void trace(tracer& aTracer) override;
    public:
        std::optional<property_descriptor> get_own_property(value aKey) override;
        bool define_own_property(value aKey, property_descriptor const& aDescriptor) override;
        bool has_property(value aKey) override;
        value get(value aKey, value aReceiver) override;
        bool set(value aKey, value aValue, value aReceiver) override;
        bool delete_property(value aKey) override;
        value_list own_property_keys() override;
    public:
        bool set_length(property_descriptor const& aDescriptor);
        void make_sparse();
        bool dense_get(std::uint32_t aIndex, value& aResult) const
        {
            if (!sparse && aIndex < elements.size() && elements[aIndex] != Empty)
            {
                aResult = elements[aIndex];
                return true;
            }
            return false;
        }
        void push(value aValue);        ///< (a new element at the end; the array is dense, extensible and its length writable)
    public:
        region_vector<value> elements;          ///< (in the region)
        std::uint32_t length = 0u;
        bool lengthWritable = true;
        bool sparse = false;
    };

    value make_array(std::size_t aLength = 0u);
    value make_array(value_list aElements);
    inline array_object* as_array(value aValue)
    {
        if (!is_object(aValue))
            return nullptr;
        auto& o = obj(aValue);
        return o.cls == object_class::Array ? static_cast<array_object*>(&o) : nullptr;
    }

    // --- functions

    struct function_object;
    using native_function = value (*)(value aThis, arguments aArguments, value aNewTarget, function_object& aCallee);

    enum class function_kind : std::uint8_t
    {
        Native,
        Compiled,
        Bound
    };

    enum class function_flavour : std::uint8_t
    {
        Normal,
        Arrow,
        Method,
        ClassConstructor,
        Generator,
        Async,
        AsyncArrow,
        AsyncGenerator,
        Accessor
    };

    struct function_object : object
    {
        function_object(value aPrototype, function_kind aKind) : object{ aPrototype, object_class::Function }, kind{ aKind } {}
    public:
        void trace(tracer& aTracer) override;
    public:
        bool callable() const override { return true; }
        bool constructor() const override { return isConstructor; }
        value call(value aThis, arguments aArguments) override;
        value construct(arguments aArguments, value aNewTarget) override;
    public:
        function_kind kind;
        function_flavour flavour = function_flavour::Normal;
        bool isConstructor = false;
        bool strict = false;
        bool derived = false;               ///< (a derived class's constructor)
        value realmGlobal = Undefined;
        // native
        native_function native = nullptr;
        std::uint32_t nativeId = 0u;        ///< (the built-in's own identification, e.g. which of a family of functions it is)
        value slots[3] = { Undefined, Undefined, Undefined };   ///< (a built-in's own state, e.g. a promise's resolving functions')
        // compiled
        void* program = nullptr;            ///< (the program (as neos_rt_host::thread_context) whose function it is)
        struct realm* functionRealm = nullptr;  ///< ([[Realm]])
        std::uint32_t index = 0u;           ///< (the function's index in its program)
        value environment = Undefined;      ///< (its closure: the environment that encloses it)
        value homeObject = Undefined;
        value fields = Undefined;           ///< (a class constructor: its fields' initializer (a function) or undefined)
        value sourceText = Undefined;       ///< (its source text (a string), for Function.prototype.toString)
        value lexicalThis = Undefined;      ///< (an arrow function: the this of its enclosing function)
        value lexicalNewTarget = Undefined;
        value lexicalHome = Undefined;
        value lexicalCallee = Undefined;    ///< (an arrow function: the function that encloses it (for its arguments and super))
        // (a compiled constructor: the object that it made last, and the properties that those it made had (the storage that a new
        // object's properties are given))
        value lastConstructed = Undefined;
        std::uint32_t constructedProperties = 0u;
        // bound
        value boundTarget = Undefined;
        value boundThis = Undefined;
        std::vector<value> boundArguments;
    };

    inline function_object* as_function(value aValue)
    {
        if (!is_object(aValue))
            return nullptr;
        auto& o = obj(aValue);
        return o.cls == object_class::Function ? static_cast<function_object*>(&o) : nullptr;
    }

    // (an object whose internal methods are the ordinary ones (its type is object or function_object, which do not override them):
    // its own properties are its table's; the type_infos are compared by address (the objects are all made by this runtime))
    inline bool ordinary_type(object const& aObject)
    {
        auto const* const type = &typeid(aObject);
        return type == &typeid(object) || type == &typeid(function_object);
    }
    inline bool array_type(object const& aObject)
    {
        return &typeid(aObject) == &typeid(array_object);
    }

    template <typename T>
    shape* type_root_shape()
    {
        static shape* sRoot = new shape{};
        return sRoot;
    }

    value make_native(value aName, std::uint32_t aLength, native_function aFunction, bool aConstructor = false, value aPrototype = 0u);
    value make_native(char const* aName, std::uint32_t aLength, native_function aFunction, bool aConstructor = false);
    void set_function_name(value aFunction, value aName, char const* aPrefix = nullptr);  ///< (SetFunctionName: aName a property key)
    void set_function_length(value aFunction, double aLength);

    // --- environments (closures): a declarative environment's bindings are slots (an Empty slot is uninitialized)

    class environment : public cell
    {
    public:
        environment(value aParent, std::uint32_t aSize) : cell{ cell_type::Internal }, parent{ aParent }, slots(aSize, Undefined) {}
    public:
        void trace(tracer& aTracer) override;
    public:
        // (compiled code reads the parent and the slots (in the region): see neos::language::javascript::layout)
        value parent;
        region_vector<value> slots;
        value withObject = Undefined;   ///< (a with statement's object environment: its object)
        value names = Undefined;        ///< (an environment whose bindings can be found by name (direct eval): an array of their names)
    };

    // --- exceptions

    struct js_exception
    {
        // (the exceptions that exist (being thrown, or caught) are a list: their values are roots (gc.cpp), e.g. while a handler that
        // rethrows calls JavaScript code (an iterator's return method))
        js_exception(value aThrown);
        js_exception(js_exception const& aOther);
        js_exception& operator=(js_exception const& aOther);
        ~js_exception();

        value thrown;
        js_exception* previous;
        js_exception* next;
    };

    [[noreturn]] void throw_value(value aValue);
    [[noreturn]] void throw_type_error(std::string const& aMessage);
    [[noreturn]] void throw_range_error(std::string const& aMessage);
    [[noreturn]] void throw_reference_error(std::string const& aMessage);
    [[noreturn]] void throw_syntax_error(std::string const& aMessage);
    [[noreturn]] void throw_error(value aPrototype, std::string const& aMessage);
    value make_error(value aPrototype, value aMessage);

    // --- the realm and its intrinsics

    struct realm
    {
        value global = Undefined;               ///< (the global object)
        value globalEnvironment = Undefined;    ///< (the global lexical declarations: names -> slots)
        // prototypes and constructors
        value objectPrototype = Undefined, object = Undefined;
        value functionPrototype = Undefined, function = Undefined;
        value arrayPrototype = Undefined, array = Undefined;
        value stringPrototype = Undefined, string = Undefined;
        value numberPrototype = Undefined, number = Undefined;
        value booleanPrototype = Undefined, boolean = Undefined;
        value symbolPrototype = Undefined, symbol = Undefined;
        value bigintPrototype = Undefined, bigint = Undefined;
        value errorPrototype = Undefined, error = Undefined;
        value evalErrorPrototype = Undefined, rangeErrorPrototype = Undefined, referenceErrorPrototype = Undefined,
            syntaxErrorPrototype = Undefined, typeErrorPrototype = Undefined, uriErrorPrototype = Undefined, aggregateErrorPrototype = Undefined,
            suppressedErrorPrototype = Undefined;
        value iteratorPrototype = Undefined, asyncIteratorPrototype = Undefined, iterator = Undefined;
        value arrayIteratorPrototype = Undefined, mapIteratorPrototype = Undefined, setIteratorPrototype = Undefined,
            stringIteratorPrototype = Undefined, regexpStringIteratorPrototype = Undefined, iteratorHelperPrototype = Undefined,
            wrapForValidIteratorPrototype = Undefined, asyncFromSyncIteratorPrototype = Undefined, forInIteratorPrototype = Undefined;
        value generatorFunctionPrototype = Undefined, generatorPrototype = Undefined, generatorFunction = Undefined;
        value asyncFunctionPrototype = Undefined, asyncFunction = Undefined;
        value asyncGeneratorFunctionPrototype = Undefined, asyncGeneratorPrototype = Undefined, asyncGeneratorFunction = Undefined;
        value promisePrototype = Undefined, promise = Undefined;
        value regexpPrototype = Undefined, regexp = Undefined;
        value datePrototype = Undefined, date = Undefined;
        value mapPrototype = Undefined, map = Undefined;
        value setPrototype = Undefined, set = Undefined;
        value weakMapPrototype = Undefined, weakSetPrototype = Undefined, weakRefPrototype = Undefined, finalizationRegistryPrototype = Undefined;
        value arrayBufferPrototype = Undefined, arrayBuffer = Undefined, sharedArrayBufferPrototype = Undefined, sharedArrayBuffer = Undefined;
        value dataViewPrototype = Undefined;
        value typedArrayPrototype = Undefined, typedArray = Undefined;
        value typedArrayPrototypes[12] = {};
        value typedArrayConstructors[12] = {};
        value proxy = Undefined;
        value disposableStackPrototype = Undefined, asyncDisposableStackPrototype = Undefined;
        value temporalDurationPrototype = Undefined, temporalDuration = Undefined, temporalInstantPrototype = Undefined, temporalInstant = Undefined,
            temporalPlainDatePrototype = Undefined, temporalPlainDate = Undefined, temporalPlainTimePrototype = Undefined, temporalPlainTime = Undefined,
            temporalPlainDateTimePrototype = Undefined, temporalPlainDateTime = Undefined, temporalPlainYearMonthPrototype = Undefined,
            temporalPlainYearMonth = Undefined, temporalPlainMonthDayPrototype = Undefined, temporalPlainMonthDay = Undefined,
            temporalZonedDateTimePrototype = Undefined, temporalZonedDateTime = Undefined;
        value intl = Undefined, intlCollatorPrototype = Undefined, intlDateTimeFormatPrototype = Undefined, intlDisplayNamesPrototype = Undefined,
            intlDurationFormatPrototype = Undefined, intlListFormatPrototype = Undefined, intlLocalePrototype = Undefined, intlLocale = Undefined,
            intlNumberFormatPrototype = Undefined, intlPluralRulesPrototype = Undefined, intlRelativeTimeFormatPrototype = Undefined,
            intlSegmenterPrototype = Undefined, intlSegmentsPrototype = Undefined, intlSegmentIteratorPrototype = Undefined,
            intlCollator = Undefined, intlDateTimeFormat = Undefined, intlNumberFormat = Undefined, intlDurationFormat = Undefined,
            intlFallbackSymbol = Undefined;
        value throwTypeError = Undefined;       ///< (%ThrowTypeError%)
        value arrayProtoValues = Undefined;     ///< (%Array.prototype.values%)
        value eval = Undefined;
        // well-known symbols
        value symAsyncIterator = Undefined, symHasInstance = Undefined, symIsConcatSpreadable = Undefined, symIterator = Undefined,
            symMatch = Undefined, symMatchAll = Undefined, symReplace = Undefined, symSearch = Undefined, symSpecies = Undefined,
            symSplit = Undefined, symToPrimitive = Undefined, symToStringTag = Undefined, symUnscopables = Undefined,
            symDispose = Undefined, symAsyncDispose = Undefined;
        // the template objects of tagged templates (by site)
        std::unordered_map<std::uint64_t, value> templates;
        // the symbol registry (Symbol.for)
        std::unordered_map<std::u16string, value>& registry;     ///< (shared by the realms)
        // the global lexical environment's bindings (let, const and class declarations of scripts)
        struct global_binding
        {
            value stored = Empty;
            bool constant = false;
            unsigned long long* slot = nullptr;     ///< (the program's word that holds the value (see global_mirror), else stored)

            value get() const { return slot != nullptr ? static_cast<value>(*slot) : stored; }
            void set(value aValue) { if (slot != nullptr) *slot = aValue; else stored = aValue; }
        };
        std::unordered_map<value, global_binding> lexicals;
        // (the program's global value mirrors (see global_mirror): names -> the words (module globals) that hold the value that
        // reading the name finds (a global lexical binding's value; else an own data property's of the global object), or Empty)
        std::unordered_map<value, std::vector<unsigned long long*>> mirrors;
    };

    realm& the_realm();                     ///< (the current realm: the realm of the function running)
    void global_mirror_update(realm& aRealm, value aName);  ///< (a global binding changed (its mirrors, if it has any, are updated))
    void global_object_changed(object& aObject, value aKey);    ///< (a property of a global object with mirrors changed)
    bool is_object_prototype(object const& aObject);    ///< (a realm's %Object.prototype% (an immutable prototype exotic object))
    void initialize_realm();
    bool realm_initialized();
    realm* create_realm();                  ///< (a new realm ($262.createRealm))
    realm* exchange_realm(realm* aRealm);   ///< (the running realm is aRealm: the previous one (e.g. for a coroutine switch))
    realm* exchange_caller_realm(realm* aRealm);    ///< (likewise the realm of the running function's caller (see caller_realm))
    realm& caller_realm();                  ///< (the realm of the running function's caller: errors of [[Construct]] after the callee's context is gone are its)
    // (test262's agents (host262.cpp): an agent (or the main program) blocks until aDone or its deadline (milliseconds of monotonic_ms);
    // the other agents run meanwhile)
    double monotonic_ms();
    void agent_block(std::function<bool()> const& aDone, std::optional<double> aDeadline);
    bool agents_running();
    bool in_agent();                        ///< (host262.cpp: a test262 agent is running)
    void agent_yield();
    bool agent_enqueue_job(std::function<void()>& aJob);   ///< (host262.cpp: a job enqueued by a test262 agent goes to its queue)
    void const* running_generator();        ///< (generators.cpp: the generator (or async function) running, if any)
    realm& function_realm(value aFunction); ///< (GetFunctionRealm)
    // (the current realm while it exists)
    class realm_scope
    {
    public:
        explicit realm_scope(realm* aRealm);
        ~realm_scope();
        realm_scope(realm_scope const&) = delete;
        realm_scope& operator=(realm_scope const&) = delete;
    private:
        realm* iPrevious;
        realm* iPreviousCaller;
    };

    // (common interned strings)
    struct names
    {
        value length, prototype, constructor, name, message, cause, value_, writable, enumerable, configurable, get, set,
            toString, valueOf, done, next, return_, throw_, callee, caller, arguments, lastIndex, index, input, groups, indices,
            then, errors, error, suppressed, stack, empty, undefined, null, true_, false_, object, function, string, number, boolean,
            symbol, bigint, default_, join, raw, size, has, add, keys, values, entries;
    };
    names const& the_names();

    // --- the running program (see api.cpp)

    neos_rt_host const* host();
    void* current_program();
    void program_ended();                   ///< (the running program has ended: see neos_runtime_plugin_end)
    void program_ended(void* aProgram);     ///< (the program has ended (it no longer runs, e.g. eval code that is unloaded))
    void retain_program(void* aProgram);    ///< (something refers to the program's code (e.g. a function it made): it is not unloaded)
    bool program_retained(void* aProgram);
    // the standard streams (aStream: 1 (output) or 2 (error)): the running program's, as its host gives them (they may be redirected, 
    // e.g. to a window: see neos_rt_host::std_write), else the process's (stdout and stderr)
    void write_standard(int aStream, std::string_view aText);
    void flush_standard(int aStream);

    // --- abstract operations (operations.cpp)

    enum class hint { Default, Number, String };

    bool to_boolean(value aValue);
    value to_primitive(value aValue, hint aHint = hint::Default);
    double to_number(value aValue);
    value to_numeric(value aValue);                             ///< (a number or a BigInt)
    value to_string(value aValue);                              ///< (a string)
    std::u16string to_u16string(value aValue);
    value to_object(value aValue);
    value to_property_key(value aValue);
    double to_integer_or_infinity(value aValue);
    double to_length(value aValue);
    double to_index(value aValue);
    std::int32_t to_int32(value aValue);
    std::uint32_t to_uint32(value aValue);
    std::int32_t to_int32(double aValue);
    std::uint32_t to_uint32(double aValue);
    value canonical_numeric_index_string(value aKey);           ///< (a number or undefined)
    std::u16string number_to_u16string(double aValue, int aRadix = 10);
    value number_to_string(double aValue);
    std::size_t number_characters(double aValue, char16_t (&aBuffer)[32]);    ///< (ToString of a number, in aBuffer: its length)
    value concat_number(value aString, double aNumber, bool aNumberFirst);   ///< (a string and ToString of a number, concatenated)
    double string_to_number(std::u16string_view aText);
    std::string type_name(value aValue);                        ///< (for messages)
    std::string describe(value aValue);                         ///< (for messages: a short representation)

    bool same_value(value aLhs, value aRhs);
    bool same_value_zero(value aLhs, value aRhs);
    bool strictly_equal(value aLhs, value aRhs);
    bool loosely_equal(value aLhs, value aRhs);
    value type_of(value aValue);                                ///< (typeof: a string)

    bool is_callable(value aValue);
    bool is_constructor(value aValue);
    bool is_array(value aValue);
    bool is_regexp(value aValue);
    bool is_integral_number(value aValue);

    value get(value aObject, value aKey);                       ///< (aObject an object)
    value get_v(value aValue, value aKey);                      ///< (GetV: aValue any value)
    value get_method(value aValue, value aKey);                 ///< (undefined if none)
    void set(value aObject, value aKey, value aValue, bool aThrow);
    bool create_data_property(value aObject, value aKey, value aValue);
    void create_data_property_or_throw(value aObject, value aKey, value aValue);
    void define_property_or_throw(value aObject, value aKey, property_descriptor const& aDescriptor);
    void delete_property_or_throw(value aObject, value aKey);
    bool has_property(value aObject, value aKey);
    bool has_own_property(value aObject, value aKey);
    value call(value aFunction, value aThis, arguments aArguments = {});
    value construct(value aConstructor, arguments aArguments = {}, std::optional<value> aNewTarget = {});
    value invoke(value aValue, value aKey, arguments aArguments = {});
    bool set_integrity_level(value aObject, bool aFrozen);
    bool test_integrity_level(value aObject, bool aFrozen);
    value create_array_from_list(std::span<value const> aElements);
    double length_of_array_like(value aObject);
    value_list create_list_from_array_like(value aObject, bool aKeysOnly = false);
    bool ordinary_has_instance(value aConstructor, value aObject);
    bool instance_of(value aValue, value aTarget);
    value species_constructor(value aObject, value aDefault);
    value get_prototype_from_constructor(value aConstructor, value realm::*aDefault);
    value ordinary_create_from_constructor(value aConstructor, value realm::*aDefault, object_class aClass = object_class::Ordinary);
    value_list enumerable_own_properties(value aObject, int aKind);   ///< (0: keys, 1: values, 2: entries)
    void copy_data_properties(value aTarget, value aSource, std::span<value const> aExcluded = {});
    property_descriptor to_property_descriptor(value aObject);
    value from_property_descriptor(std::optional<property_descriptor> const& aDescriptor);
    void complete_property_descriptor(property_descriptor& aDescriptor);
    bool validate_and_apply_property_descriptor(object* aObject, value aKey, bool aExtensible, property_descriptor const& aDescriptor,
        std::optional<property_descriptor> const& aCurrent);
    bool is_compatible_property_descriptor(bool aExtensible, property_descriptor const& aDescriptor, std::optional<property_descriptor> const& aCurrent);

    // iterators
    struct iterator_record
    {
        value iterator = Undefined;
        value nextMethod = Undefined;
        bool done = false;
        bool async = false;     ///< (an async iterator (for await, yield* in an async generator): its results are awaited)
    };
    iterator_record get_iterator(value aValue, bool aAsync = false);
    iterator_record get_iterator_from_method(value aValue, value aMethod);
    iterator_record get_iterator_direct(value aObject);
    value iterator_next(iterator_record& aRecord, std::optional<value> aValue = {});
    bool iterator_complete(value aResult);
    value iterator_value(value aResult);
    std::optional<value> iterator_step_value(iterator_record& aRecord);   ///< (none: done)
    void iterator_close(iterator_record const& aRecord);                    ///< (normal completion)
    void iterator_close_on_throw(iterator_record const& aRecord);           ///< (throw completion: the iterator's errors are ignored)
    value create_iter_result_object(value aValue, bool aDone);
    value_list iterate_to_list(value aIterable);

    // number formatting helpers (numbers.cpp)
    std::string number_to_fixed(double aValue, int aDigits);
    std::string number_to_exponential(double aValue, std::optional<int> aDigits);
    std::string number_to_precision(double aValue, int aDigits);

    // --- helpers for the built-ins

    inline value argument(arguments aArguments, std::size_t aIndex)
    {
        return aIndex < aArguments.size() ? aArguments[aIndex] : Undefined;
    }
    value method(value aObject, char const* aName, std::uint32_t aLength, native_function aFunction, std::uint8_t aAttributes = AttributesHidden);
    value method(value aObject, value aKey, std::uint32_t aLength, native_function aFunction, std::uint8_t aAttributes = AttributesHidden);
    value getter(value aObject, value aKey, native_function aGetter, native_function aSetter = nullptr, std::uint8_t aAttributes = AttributeConfigurable);
    void constant(value aObject, char const* aName, value aValue);  ///< (not writable, enumerable or configurable)
    // (builtins_string.cpp: a call of the built-in String.prototype.charCodeAt of a string (with a number): its result (else false))
    bool char_code_at(value aCallee, value aThis, value aPosition, value& aResult);

    // --- the built-ins (builtins_*.cpp): each adds its intrinsics to the realm

    void init_object(realm& aRealm);
    void init_function(realm& aRealm);
    void init_error(realm& aRealm);
    void init_symbol(realm& aRealm);
    void init_boolean(realm& aRealm);
    void init_number(realm& aRealm);
    void init_math(realm& aRealm);
    bool math_intrinsic(value aFunction, std::uint32_t aIntrinsic);   ///< (builtins_basic.cpp: whether it is the Math function of a compiler intrinsic)
    void init_string(realm& aRealm);
    void init_array(realm& aRealm);
    void init_iterator(realm& aRealm);
    void init_global(realm& aRealm);
    void init_json(realm& aRealm);
    void init_reflect(realm& aRealm);
    void init_proxy(realm& aRealm);
    void init_collections(realm& aRealm);
    void init_promise(realm& aRealm);
    void init_generator(realm& aRealm);
    void init_bigint(realm& aRealm);
    void init_regexp(realm& aRealm);
    void init_date(realm& aRealm);
    void init_temporal(realm& aRealm);                           ///< (temporal_objects.cpp: Temporal (after init_date))
    void init_intl(realm& aRealm);                               ///< (intl.cpp: Intl (none without ICU))
    void init_typed_arrays(realm& aRealm);
    void init_console(realm& aRealm);
    void init_host262(realm& aRealm);
    void init_eval(realm& aRealm);
    void init_async_builtins(realm& aRealm);
    void init_disposable(realm& aRealm);
    void detach_array_buffer(value aBuffer, value aKey);         ///< (typed_arrays.cpp: DetachArrayBuffer)
    value evaluate_script(value aSource);                         ///< (eval.cpp: a script's evaluation (as indirect eval's))

    // promises and jobs (promise.cpp)
    value new_promise_capability(value aConstructor, value& aResolve, value& aReject);
    value promise_resolve(value aConstructor, value aValue);
    void create_resolving_functions(value aPromise, value& aResolve, value& aReject);
    value promise_rejected(value aReason);                      ///< (a new %Promise% rejected with aReason)
    int promise_state_of(value aPromise, value& aResult);       ///< (0 pending, 1 fulfilled, 2 rejected)
    void perform_promise_then(value aPromise, value aOnFulfilled, value aOnRejected, value aResultPromise = Undefined,
        value aResolve = Undefined, value aReject = Undefined);
    void enqueue_job(value aJob, value aArgument = Undefined);   ///< (a function called with an argument)
    void enqueue_job(std::function<void()> aJob);                ///< (a closure that captures no values (they would not be kept alive))
    void enqueue_job(void (*aJob)(value const* aValues), std::initializer_list<value> aValues); ///< (a function and (up to six) values (kept alive))
    bool run_jobs();                                             ///< (false: a job threw (it was reported))

    void initialize_instance_elements(value aObject, value aConstructor);  ///< (classes.cpp: InitializeInstanceElements)

    // generators and async functions (generators.cpp)
    value generator_resume(value aGenerator, value aValue, int aMode);    ///< (aMode: 0 next, 1 throw, 2 return)
    value await_value(value aValue);    ///< (Await: suspends the running async function (or async generator) until aValue settles)
    // (a built-in's body run as an async function's: its promise (aCaptured: the values that the body captures (they are kept alive
    // while it runs: the garbage collector does not see the values in a std::function)))
    value async_native(std::function<value()> aBody, std::initializer_list<value> aCaptured);
    iterator_record create_async_from_sync_iterator(iterator_record const& aSyncRecord);

    void object_define_properties(value aObject, value aProperties);          ///< (ObjectDefineProperties)
    value create_dynamic_function(arguments aArguments, value aNewTarget, int aKind);   ///< (0 normal, 1 generator, 2 async, 3 async generator)

    value make_array_iterator(value aObject, int aKind);                    ///< (aKind: 0 keys, 1 values, 2 entries)
    double typed_array_length_checked(value aTypedArray);                   ///< (typed_arrays.cpp: its length (a TypeError if it is out of bounds))
    void init_iterator_helpers(realm& aRealm);                              ///< (builtins_iterator.cpp: Iterator and its helpers)

    // strings (builtins_string.cpp, unicode.cpp)
    value make_string_iterator(value aString);
    std::u16string get_substitution_text(std::u16string const& aMatched, std::u16string const& aString, std::size_t aPosition,
        std::span<value const> aCaptures, value aNamedCaptures, std::u16string const& aReplacement);   ///< (GetSubstitution)
    std::u16string case_map(std::u16string const& aText, bool aUpper);      ///< (toUpperCase/toLowerCase: full case mapping)
    value normalize_string(value aString, std::u16string const& aForm);     ///< (NFC, NFD, NFKC or NFKD)

    // exotic objects
    value make_string_object(value aString, value aPrototype);      ///< (strings.cpp: a String exotic object)
    bool proxy_is_array(value aProxy);                              ///< (proxy.cpp: IsArray of a proxy's target)
    void proxy_function_realm(value aProxy);                        ///< (proxy.cpp: GetFunctionRealm of a proxy: throws if revoked)
    value proxy_target(value aProxy);                               ///< (proxy.cpp: a proxy's target (null if it is revoked))
    value_list collection_entries(value aCollection);       ///< (builtins_collections.cpp: a Map's keys and values (k0, v0, k1, v1 ...) or a Set's values, in order)
}
