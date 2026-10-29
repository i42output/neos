/*
  jit_ssa.hpp

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

// The optimizing tier's intermediate representation (jit_ssa.cpp): a function (pre-decoded WebAssembly, as the optimizer
// (jit_optimizer.hpp) leaves it) in static single assignment form: basic blocks of instructions, each of which defines (at most)
// one value; locals and the operand stack become values (phis where control flow merges). Its optimizations (constant folding,
// global value numbering, redundant load elimination, the bounds of values (whose accesses of a fixed external memory need no
// check)) are the instructions' semantics only. The JIT (jit.cpp) allocates its registers and emits its code.

#pragma once

#include <neos/neos.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <optional>
#include <stdexcept>
#include <vector>
#include <neos/bytecode/vm/jit.hpp>
#include "jit_optimizer.hpp"

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            namespace jit
            {
                namespace ssa
                {
                    using value = std::uint32_t;
                    constexpr value None = ~value{};

                    constexpr std::uint8_t I32 = 0x7Fu;
                    constexpr std::uint8_t I64 = 0x7Eu;
                    constexpr std::uint8_t F32 = 0x7Du;
                    constexpr std::uint8_t F64 = 0x7Cu;
                    constexpr std::uint8_t Void = 0x40u;

                    struct unsupported : std::runtime_error { using std::runtime_error::runtime_error; };

                    // (an instruction's operation: a WebAssembly opcode (a constant (0x41-0x44), a numeric instruction, a load or
                    // store, select, global.get or global.set, call or call_indirect), or one of these)
                    enum operation : std::uint16_t
                    {
                        Param = 0x100,      ///< (a parameter (imm0: its index))
                        Phi,                ///< (operands: by predecessor)
                        CallResult,         ///< (a call's result after its first (operand: the call; imm0: its index): just after the call)
                        Jump,               ///< (to the block's successor)
                        Branch,             ///< (operand: the condition (i32); successors: if true, if false)
                        Switch,             ///< (operand: the index (i32); successors: the targets, then the default)
                        Return,             ///< (operands: the results)
                        Trap,               ///< (imm0: the trap: unreachable)
                        Deopt,              ///< (leaves for the function's own code (see optimizer::deopt; imm0: its number): operands: the
                                            ///< function's locals, the operand stack below the call's arguments, the arguments)
                        Assume              ///< (neos::assume (see import_call::Assume): operand: a condition that is not 0 here (no code))
                    };

                    enum flag : std::uint8_t
                    {
                        Unchecked = 0x01u,  ///< (a memory access that is within its memory: not checked)
                        Volatile = 0x02u,   ///< (not to be removed (e.g. a load that may trap))
                        Scaled = 0x0Cu,     ///< (an unchecked access's address: its operand shifted left by these bits (>> 2: 1 to 3), as an
                                            ///< address's index is scaled)
                        Top16 = 0x10u       ///< (an i64 eq or ne: of its first operand's top 16 bits (>> 48) and its second's (a constant's)
                                            ///< low 16 bits)
                    };
                    constexpr unsigned ScaledShift = 2u;

                    // (a map of 64 bit keys (not all ones) to trivially copyable values: open addressing (linear probing), one allocation
                    // for all its entries (not one each, as std::unordered_map's))
                    template <typename V>
                    class flat_map64
                    {
                        static_assert(std::is_trivially_copyable_v<V>);
                    public:
                        static constexpr std::uint64_t Empty = ~std::uint64_t{ 0u };
                    public:
                        V* find(std::uint64_t aKey) noexcept
                        {
                            if (iCount == 0u)
                                return nullptr;
                            for (auto i = slot(aKey);; i = (i + 1u) & iMask)
                            {
                                if (iKeys[i] == aKey)
                                    return &iValues[i];
                                if (iKeys[i] == Empty)
                                    return nullptr;
                            }
                        }
                        V const* find(std::uint64_t aKey) const noexcept { return const_cast<flat_map64&>(*this).find(aKey); }
                        V& operator[](std::uint64_t aKey)
                        {
                            if ((iCount + 1u) * 4u > iKeys.size() * 3u)
                                grow();
                            for (auto i = slot(aKey);; i = (i + 1u) & iMask)
                            {
                                if (iKeys[i] == aKey)
                                    return iValues[i];
                                if (iKeys[i] == Empty)
                                {
                                    iKeys[i] = aKey;
                                    iValues[i] = V{};
                                    ++iCount;
                                    return iValues[i];
                                }
                            }
                        }
                        std::size_t size() const noexcept { return iCount; }
                        void reserve(std::size_t aCount)
                        {
                            while (aCount * 4u > iKeys.size() * 3u)
                                grow();
                        }
                    private:
                        std::size_t slot(std::uint64_t aKey) const noexcept { return static_cast<std::size_t>((aKey * 0x9E3779B97F4A7C15ull) >> 32u) & iMask; }
                        void grow()
                        {
                            auto const size = iKeys.empty() ? std::size_t{ 64u } : iKeys.size() * 2u;
                            std::vector<std::uint64_t> keys(size, Empty);
                            std::vector<V> values(size);
                            std::swap(keys, iKeys);
                            std::swap(values, iValues);
                            iMask = size - 1u;
                            for (std::size_t k = 0u; k < keys.size(); ++k)
                                if (keys[k] != Empty)
                                    for (auto i = slot(keys[k]);; i = (i + 1u) & iMask)
                                        if (iKeys[i] == Empty)
                                        {
                                            iKeys[i] = keys[k];
                                            iValues[i] = values[k];
                                            break;
                                        }
                        }
                    private:
                        std::vector<std::uint64_t> iKeys;
                        std::vector<V> iValues;
                        std::size_t iMask = 0u;
                        std::size_t iCount = 0u;
                    };

                    // (a vector of trivially copyable elements whose first N are in the object itself (an instruction's operands: most have
                    // few; not an allocation each))
                    template <typename T, std::size_t N>
                    class small_vector
                    {
                        static_assert(std::is_trivially_copyable_v<T>);
                    public:
                        using value_type = T;
                        using size_type = std::size_t;
                        using iterator = T*;
                        using const_iterator = T const*;
                    public:
                        small_vector() noexcept = default;
                        small_vector(std::initializer_list<T> aValues) { assign(aValues.begin(), aValues.end()); }
                        small_vector(std::size_t aCount, T const& aValue) { assign(aCount, aValue); }
                        template <typename It, typename = std::enable_if_t<!std::is_integral_v<It>>>
                        small_vector(It aFirst, It aLast) { assign(aFirst, aLast); }
                        small_vector(small_vector const& aOther) { assign(aOther.begin(), aOther.end()); }
                        small_vector(small_vector&& aOther) noexcept { take(aOther); }
                        ~small_vector() { release(); }
                        small_vector& operator=(small_vector const& aOther) { if (this != &aOther) assign(aOther.begin(), aOther.end()); return *this; }
                        small_vector& operator=(small_vector&& aOther) noexcept { if (this != &aOther) { release(); take(aOther); } return *this; }
                        small_vector& operator=(std::initializer_list<T> aValues) { assign(aValues.begin(), aValues.end()); return *this; }
                        small_vector(std::vector<T> const& aValues) { assign(aValues.begin(), aValues.end()); }
                        small_vector& operator=(std::vector<T> const& aValues) { assign(aValues.begin(), aValues.end()); return *this; }
                    public:
                        std::size_t size() const noexcept { return iSize; }
                        bool empty() const noexcept { return iSize == 0u; }
                        T* data() noexcept { return iHeap != nullptr ? iHeap : iInline; }
                        T const* data() const noexcept { return iHeap != nullptr ? iHeap : iInline; }
                        T* begin() noexcept { return data(); }
                        T* end() noexcept { return data() + iSize; }
                        T const* begin() const noexcept { return data(); }
                        T const* end() const noexcept { return data() + iSize; }
                        T& operator[](std::size_t aIndex) noexcept { return data()[aIndex]; }
                        T const& operator[](std::size_t aIndex) const noexcept { return data()[aIndex]; }
                        T& front() noexcept { return data()[0]; }
                        T const& front() const noexcept { return data()[0]; }
                        T& back() noexcept { return data()[iSize - 1u]; }
                        T const& back() const noexcept { return data()[iSize - 1u]; }
                        void clear() noexcept { iSize = 0u; }
                        void reserve(std::size_t aCapacity) { if (aCapacity > capacity()) grow(aCapacity); }
                        void push_back(T const& aValue) { if (iSize == capacity()) { auto const v = aValue; grow(iSize * 2u); data()[iSize++] = v; return; } data()[iSize++] = aValue; }
                        void pop_back() noexcept { --iSize; }
                        void resize(std::size_t aSize, T const& aValue = T{}) { reserve(aSize); for (auto i = iSize; i < aSize; ++i) data()[i] = aValue; iSize = aSize; }
                        void assign(std::size_t aCount, T const& aValue) { auto const v = aValue; iSize = 0u; resize(aCount, v); }
                        template <typename It, typename = std::enable_if_t<!std::is_integral_v<It>>>
                        void assign(It aFirst, It aLast)
                        {
                            auto const count = static_cast<std::size_t>(std::distance(aFirst, aLast));
                            if (count > capacity())
                            {
                                // (the source may be this vector's own elements: copied before its storage is released)
                                small_vector copy;
                                copy.grow(count);
                                std::copy(aFirst, aLast, copy.data());
                                copy.iSize = count;
                                *this = std::move(copy);
                                return;
                            }
                            std::copy(aFirst, aLast, data());
                            iSize = count;
                        }
                        void assign(std::initializer_list<T> aValues) { assign(aValues.begin(), aValues.end()); }
                        T* insert(T const* aPosition, T const& aValue)
                        {
                            auto const at = static_cast<std::size_t>(aPosition - data());
                            auto const v = aValue;
                            reserve(iSize + 1u);
                            std::memmove(data() + at + 1u, data() + at, (iSize - at) * sizeof(T));
                            data()[at] = v;
                            ++iSize;
                            return data() + at;
                        }
                        T* insert(T const* aPosition, std::size_t aCount, T const& aValue)
                        {
                            auto const at = static_cast<std::size_t>(aPosition - data());
                            auto const v = aValue;
                            reserve(iSize + aCount);
                            std::memmove(data() + at + aCount, data() + at, (iSize - at) * sizeof(T));
                            std::fill(data() + at, data() + at + aCount, v);
                            iSize += static_cast<std::uint32_t>(aCount);
                            return data() + at;
                        }
                        template <typename It, typename = std::enable_if_t<!std::is_integral_v<It>>>
                        T* insert(T const* aPosition, It aFirst, It aLast)
                        {
                            auto const at = static_cast<std::size_t>(aPosition - data());
                            small_vector const values(aFirst, aLast);
                            auto const count = values.size();
                            reserve(iSize + count);
                            std::memmove(data() + at + count, data() + at, (iSize - at) * sizeof(T));
                            std::copy(values.begin(), values.end(), data() + at);
                            iSize += count;
                            return data() + at;
                        }
                        T* erase(T const* aPosition) { return erase(aPosition, aPosition + 1); }
                        T* erase(T const* aFirst, T const* aLast)
                        {
                            auto const at = static_cast<std::size_t>(aFirst - data());
                            auto const count = static_cast<std::size_t>(aLast - aFirst);
                            std::memmove(data() + at, data() + at + count, (iSize - at - count) * sizeof(T));
                            iSize -= count;
                            return data() + at;
                        }
                        bool operator==(small_vector const& aOther) const { return iSize == aOther.iSize && std::equal(begin(), end(), aOther.begin()); }
                        operator std::vector<T>() const { return std::vector<T>(begin(), end()); }
                    private:
                        std::size_t capacity() const noexcept { return iHeap != nullptr ? iCapacity : N; }
                        void grow(std::size_t aCapacity)
                        {
                            aCapacity = std::max<std::size_t>(aCapacity, N * 2u);
                            auto* const heap = static_cast<T*>(::operator new(aCapacity * sizeof(T)));
                            std::memcpy(heap, data(), iSize * sizeof(T));
                            release();
                            iHeap = heap;
                            iCapacity = static_cast<std::uint32_t>(aCapacity);
                        }
                        void release() noexcept
                        {
                            if (iHeap != nullptr)
                                ::operator delete(iHeap);
                            iHeap = nullptr;
                        }
                        void take(small_vector& aOther) noexcept
                        {
                            iSize = aOther.iSize;
                            if (aOther.iHeap != nullptr)
                            {
                                iHeap = aOther.iHeap;
                                iCapacity = aOther.iCapacity;
                                aOther.iHeap = nullptr;
                            }
                            else
                                std::memcpy(iInline, aOther.iInline, iSize * sizeof(T));
                            aOther.iSize = 0u;
                        }
                    private:
                        T* iHeap = nullptr;
                        std::uint32_t iSize = 0u;
                        std::uint32_t iCapacity = 0u;
                        T iInline[N];
                    };

                    using operand_list = small_vector<value, 3>;

                    struct instruction
                    {
                        std::uint16_t op = 0u;
                        std::uint8_t type = Void;   ///< (its value's type (Void: none))
                        std::uint8_t flags = 0u;
                        operand_list operands;
                        std::uint64_t imm0 = 0u;    ///< (a constant's bits; a memory access's memarg; the function, global, type or parameter)
                        std::uint64_t imm1 = 0u;    ///< (a memory access's offset)
                        std::size_t origin = 0u;    ///< (the position that its call is reported at)
                        std::uint32_t block = 0u;
                        bool dead = false;
                        bool constant() const { return op >= 0x41u && op <= 0x44u; }
                        bool terminator() const { return op >= Jump && op <= Deopt; }
                        bool load() const { return op >= 0x28u && op <= 0x35u; }
                        bool store() const { return op >= 0x36u && op <= 0x3Eu; }
                        bool call() const { return op == 0x10u || op == 0x11u; }
                    };

                    struct block
                    {
                        std::vector<value> code;                ///< (its instructions: phis first; its terminator last)
                        small_vector<std::uint32_t, 2> predecessors;
                        small_vector<std::uint32_t, 2> successors;
                        std::uint32_t loopDepth = 0u;
                        bool loopHeader = false;
                        bool reachable = false;
                    };

                    struct function
                    {
                        std::uint32_t index = 0u;
                        std::vector<instruction> values;        ///< (an instruction's value is its index)
                        std::vector<block> blocks;              ///< ([0]: the entry)
                        std::vector<std::uint8_t> parameters;
                        std::vector<std::uint8_t> results;
                        std::vector<std::uint32_t> order;       ///< (the blocks, as the code is laid out (reverse postorder))
                        std::vector<std::uint32_t> uses;        ///< (each value's uses)
                        bool usesCurrent = false;               ///< (uses: counted since the code last changed (see optimize))
                        std::uint64_t cfgHash = 0u;             ///< (the blocks' edges when loop depths were last found (see finish))
                    };

                    // (an entry at a loop (on-stack replacement): the code that a call running in the baseline's code continues in, from
                    // the loop's head: its parameters are the function's parameters and its own locals (their values there), and the code
                    // before the loop is not its)
                    struct osr_entry
                    {
                        std::size_t loop = 0u;                  ///< (the loop's token's origin: the function's own loop (not within another loop))
                        std::vector<std::uint8_t> parameters;   ///< (the function's parameters' and its own locals' types)
                    };

                    // (a function's code (aTokens, its locals aLocals (after its parameters), and aOrigins: its tokens' origins); throws
                    // unsupported if it uses an instruction that the representation does not have; aOsr: its code from a loop (see osr_entry))
                    function build(module_description const& aModule, std::uint32_t aFunction, std::vector<std::uint64_t> const& aTokens,
                        std::vector<std::uint8_t> const& aLocals, std::vector<std::size_t> const& aOrigins, std::vector<optimizer::deopt> const& aDeopts = {},
                        osr_entry const* aOsr = nullptr);
                    // (aAccessible: the size of a fixed external memory (0: not fixed): its accesses that are within it are not checked; 
                    // aMemoryWord: a word of a fixed external memory (its value now, if it can be read): cached loads (optimizer::MemargCached)
                    // at constant addresses are constants)
                    void optimize(function& aFunction, module_description const& aModule, std::function<std::uint64_t(std::uint32_t aMemory)> const& aAccessible = {},
                        std::function<std::optional<std::uint64_t>(std::uint32_t aMemory, std::uint64_t aAddress, std::uint32_t aWidth)> const& aMemoryWord = {});
                    // (the blocks' order (reverse postorder) and the values' uses, after the code changes)
                    void finish(function& aFunction);
                }
            }
        }
    }
}
