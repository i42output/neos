/*
  lib_table.cpp

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

// The table library

#include <algorithm>
#include <limits>
#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    namespace
    {
        // (a table's elements, as the library accesses them: with metamethods unless it has none)
        bool plain(value aTable)
        {
            return is_table(aTable) && !is_table(tab(aTable).metatable);
        }

        value get_element(value aTable, std::int64_t aIndex)
        {
            if (plain(aTable))
                return tab(aTable).get_integer(aIndex);
            return index(aTable, make_integer(aIndex));
        }

        void set_element(value aTable, std::int64_t aIndex, value aValue)
        {
            if (plain(aTable))
                tab(aTable).set_integer(aIndex, aValue);
            else
                set_index(aTable, make_integer(aIndex), aValue);
        }

        std::int64_t length_of(value aTable)
        {
            if (plain(aTable))
                return tab(aTable).length();
            auto const n = length(aTable);
            std::int64_t result;
            if (!to_integer(n, result))
                runtime_error("object length is not an integer");
            return result;
        }

        // (a table, or a value with the metamethods the function needs)
        void check_table_like(native_call& c, std::size_t aIndex, bool aRead, bool aWrite, bool aLength)
        {
            auto const v = c.arg(aIndex);
            if (is_table(v))
                return;
            auto const mt = metatable_of(v);
            if (is_table(mt) && (!aRead || !is_nil(tab(mt).get_string(name(NameIndex)))) &&
                (!aWrite || !is_nil(tab(mt).get_string(name(NameNewIndex)))) && (!aLength || !is_nil(tab(mt).get_string(name(NameLen)))))
                return;
            type_error(c, aIndex, "table");
        }

        void table_concat(native_call& c)
        {
            check_table_like(c, 0u, true, false, true);
            auto const t = c.arg(0u);
            auto const separator = optional_string(c, 1u, "");
            auto const first = optional_integer(c, 2u, 1);
            auto const last = c.none(3u) || is_nil(c.arg(3u)) ? length_of(t) : check_integer(c, 3u);
            std::string result;
            for (auto i = first; i <= last; ++i)
            {
                auto const v = get_element(t, i);
                if (is_string(v))
                    result += str(v);
                else if (is_number(v))
                    result += number_text(v);
                else
                    runtime_error("invalid value (" + type_name(v) + ") at index " + std::to_string(i) + " in table for 'concat'");
                if (i != last)
                    result.append(separator);
                if (i == std::numeric_limits<std::int64_t>::max())
                    break;
            }
            c.ret(make_string(std::move(result)));
        }

        void table_insert(native_call& c)
        {
            check_table_like(c, 0u, true, true, true);
            auto const t = c.arg(0u);
            auto const size = length_of(t) + 1;
            switch (c.count())
            {
            case 2u:
                set_element(t, size, c.arg(1u));
                break;
            case 3u:
                {
                    auto const position = check_integer(c, 1u);
                    if (static_cast<std::uint64_t>(position) - 1u >= static_cast<std::uint64_t>(size))
                        argument_error(c, 1u, "position out of bounds");
                    for (auto i = size; i > position; --i)
                        set_element(t, i, get_element(t, i - 1));
                    set_element(t, position, c.arg(2u));
                }
                break;
            default:
                runtime_error("wrong number of arguments to 'insert'");
            }
        }

        void table_remove(native_call& c)
        {
            check_table_like(c, 0u, true, true, true);
            auto const t = c.arg(0u);
            auto const size = length_of(t);
            auto position = optional_integer(c, 1u, size);
            if (position != size && static_cast<std::uint64_t>(position) - 1u > static_cast<std::uint64_t>(size))
                argument_error(c, 1u, "position out of bounds");
            auto const removed = get_element(t, position);
            value_list keep{ removed };
            for (; position < size; ++position)
                set_element(t, position, get_element(t, position + 1));
            set_element(t, position, Nil);
            c.ret(removed);
        }

        void table_move(native_call& c)
        {
            check_table_like(c, 0u, true, false, false);
            auto const a1 = c.arg(0u);
            auto const f = check_integer(c, 1u);
            auto const e = check_integer(c, 2u);
            auto const t = check_integer(c, 3u);
            auto const a2 = c.none(4u) || is_nil(c.arg(4u)) ? a1 : c.arg(4u);
            if (!c.none(4u) && !is_nil(c.arg(4u)))
                check_table_like(c, 4u, false, true, false);
            if (e >= f)
            {
                if (!(f > 0 || e < std::numeric_limits<std::int64_t>::max() + f))
                    argument_error(c, 2u, "too many elements to move");
                if (t > std::numeric_limits<std::int64_t>::max() - (e - f))
                    argument_error(c, 3u, "destination wrap around");
                if (t > e || t <= f || (a1 != a2))
                {
                    for (std::int64_t i = 0; i <= e - f; ++i)
                        set_element(a2, t + i, get_element(a1, f + i));
                }
                else
                {
                    for (std::int64_t i = e - f; i >= 0; --i)
                        set_element(a2, t + i, get_element(a1, f + i));
                }
            }
            c.ret(a2);
        }

        void table_pack(native_call& c)
        {
            auto const t = make_table(c.count(), 1u);
            value_list keep{ t };
            auto& tt = tab(t);
            for (std::size_t i = 0u; i < c.count(); ++i)
                tt.set_integer(static_cast<std::int64_t>(i) + 1, c.args[i]);
            tt.set(intern("n"), make_integer(static_cast<std::int64_t>(c.count())));
            c.ret(t);
        }

        void table_unpack(native_call& c)
        {
            auto const t = c.arg(0u);
            auto const first = optional_integer(c, 1u, 1);
            auto const last = c.none(2u) || is_nil(c.arg(2u)) ? length_of(t) : check_integer(c, 2u);
            if (first > last)
                return;
            auto const count = static_cast<std::uint64_t>(last) - static_cast<std::uint64_t>(first);
            if (count >= 1000000u)
                runtime_error("too many results to unpack");
            for (auto i = first; ; ++i)
            {
                c.ret(get_element(t, i));
                if (i == last)
                    break;
            }
        }

        // (sort: a quicksort that checks its bounds (an inconsistent comparison raises an error rather than going beyond them))
        class sorter
        {
        public:
            sorter(native_call& c, std::vector<value>& aValues, value aComparison) : iCall{ c }, iValues{ aValues }, iComparison{ aComparison } {}
            void sort(std::ptrdiff_t aLow, std::ptrdiff_t aHigh)
            {
                while (aHigh - aLow > 12)
                {
                    // (median of three)
                    auto const middle = aLow + (aHigh - aLow) / 2;
                    if (less(iValues[middle], iValues[aLow]))
                        std::swap(iValues[middle], iValues[aLow]);
                    if (less(iValues[aHigh], iValues[middle]))
                    {
                        std::swap(iValues[aHigh], iValues[middle]);
                        if (less(iValues[middle], iValues[aLow]))
                            std::swap(iValues[middle], iValues[aLow]);
                    }
                    auto const pivot = iValues[middle];
                    std::swap(iValues[middle], iValues[aHigh - 1]);
                    auto i = aLow;
                    auto j = aHigh - 1;
                    for (;;)
                    {
                        for (;;)
                        {
                            if (++i > aHigh)
                                invalid();
                            if (!less(iValues[i], pivot))
                                break;
                        }
                        for (;;)
                        {
                            if (--j < aLow)
                                invalid();
                            if (!less(pivot, iValues[j]))
                                break;
                        }
                        if (j < i)
                            break;
                        std::swap(iValues[i], iValues[j]);
                    }
                    std::swap(iValues[aHigh - 1], iValues[i]);
                    // (the smaller part recursively, the larger iteratively)
                    if (i - aLow < aHigh - i)
                    {
                        sort(aLow, i - 1);
                        aLow = i + 1;
                    }
                    else
                    {
                        sort(i + 1, aHigh);
                        aHigh = i - 1;
                    }
                }
                // (insertion sort)
                for (auto i = aLow + 1; i <= aHigh; ++i)
                {
                    auto const v = iValues[i];
                    auto j = i - 1;
                    while (j >= aLow && less(v, iValues[j]))
                    {
                        iValues[j + 1] = iValues[j];
                        --j;
                    }
                    iValues[j + 1] = v;
                }
            }
        private:
            bool less(value a, value b)
            {
                if (is_nil(iComparison))
                {
                    if (is_inline_integer(a) && is_inline_integer(b))
                        return inline_integer(a) < inline_integer(b);
                    if (is_float(a) && is_float(b))
                        return float_of(a) < float_of(b);
                    return less_than(a, b);
                }
                value const arguments[] = { a, b };
                return truthy(call1(iComparison, arguments));
            }
            [[noreturn]] void invalid()
            {
                runtime_error("invalid order function for sorting");
            }
        private:
            native_call& iCall;
            std::vector<value>& iValues;
            value iComparison;
        };

        void table_sort(native_call& c)
        {
            check_table_like(c, 0u, true, true, true);
            auto const t = c.arg(0u);
            auto const n = length_of(t);
            if (n > 1)
            {
                if (n >= std::numeric_limits<std::int32_t>::max())
                    argument_error(c, 0u, "array too big");
                auto const comparison = c.arg(1u);
                if (!is_nil(comparison) && !is_function(comparison))
                    type_error(c, 1u, "function");
                value_list values;
                values->reserve(static_cast<std::size_t>(n));
                for (std::int64_t i = 1; i <= n; ++i)
                    values.push_back(get_element(t, i));
                sorter{ c, *values, comparison }.sort(0, static_cast<std::ptrdiff_t>(n) - 1);
                for (std::int64_t i = 1; i <= n; ++i)
                    set_element(t, i, values[static_cast<std::size_t>(i - 1)]);
            }
        }
    }

    void open_table()
    {
        auto const t = library_table("table");
        register_library(t, "table", {
            { "concat", &table_concat },
            { "insert", &table_insert },
            { "remove", &table_remove },
            { "move", &table_move },
            { "pack", &table_pack },
            { "unpack", &table_unpack },
            { "sort", &table_sort } });
    }
}
