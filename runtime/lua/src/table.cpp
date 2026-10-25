/*
  table.cpp

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

// Tables: an array part (the keys 1 to n) and a hash part (open addressing with linear probing). A key whose entry is removed
// (assigned nil) keeps its slot until the table is rehashed (when a new key needs a slot): next can continue from it.

#include <algorithm>
#include <bit>
#include "lua.hpp"

namespace neos::lua_runtime
{
    namespace
    {
        std::uint64_t mix(std::uint64_t aValue)
        {
            aValue += 0x9E3779B97F4A7C15ull;
            aValue = (aValue ^ (aValue >> 30)) * 0xBF58476D1CE4E5B9ull;
            aValue = (aValue ^ (aValue >> 27)) * 0x94D049BB133111EBull;
            return aValue ^ (aValue >> 31);
        }

        std::size_t hash_integer(std::int64_t aValue)
        {
            return static_cast<std::size_t>(mix(static_cast<std::uint64_t>(aValue)));
        }
    }

    value make_table(std::size_t aArray, std::size_t aHash)
    {
        auto& t = the_heap().make<table>();
        auto const v = TagTable | t.handle;
        if (aArray > 0u)
            t.array.reserve(aArray);
        if (aHash > 0u)
        {
            std::size_t size = 4u;
            while (size * 3u < aHash * 4u)
                size *= 2u;
            t.nodes.reset(size);
        }
        the_heap().account((aArray + aHash) * sizeof(value));
        return v;
    }

    value normalize_key(value aKey)
    {
        if (is_float(aKey))
        {
            std::int64_t i;
            if (float_to_integer(float_of(aKey), i))
                return make_integer(i);
        }
        return aKey;
    }

    std::size_t hash_key(value aKey)
    {
        if (is_inline_integer(aKey))
            return hash_integer(inline_integer(aKey));
        if (is_string(aKey))
            return cell_of<string_cell>(aKey).hash();
        if (tag_of(aKey) == TagOther)
        {
            if (auto* c = cell_at(aKey); c != nullptr && c->type == cell_type::Integer)
                return hash_integer(static_cast<integer_cell*>(c)->i);
        }
        return static_cast<std::size_t>(mix(aKey));
    }

    bool key_equal(value aLeft, value aRight)
    {
        if (aLeft == aRight)
            return true;
        auto const tag = tag_of(aLeft);
        if (tag == TagString && tag_of(aRight) == TagString)
        {
            auto& l = cell_of<string_cell>(aLeft);
            auto& r = cell_of<string_cell>(aRight);
            if (l.interned && r.interned)
                return false;
            return l.text == r.text;
        }
        if ((tag == TagOther || tag == TagInteger) && (tag_of(aRight) == TagOther || tag_of(aRight) == TagInteger))
            return is_integer(aLeft) && is_integer(aRight) && integer_of(aLeft) == integer_of(aRight);
        return false;
    }

    std::size_t table::find(value aKey) const
    {
        if (nodes.empty())
            return 0u;
        auto const mask = nodes.size() - 1u;
        for (auto i = hash_key(aKey) & mask;; i = (i + 1u) & mask)
        {
            auto const& n = nodes[i];
            if (n.key == Empty)
                return nodes.size();
            if (key_equal(n.key, aKey))
                return i;
        }
    }

    value table::get(value aKey) const
    {
        if (is_inline_integer(aKey))
            return get_integer(inline_integer(aKey));
        if (is_float(aKey))
        {
            std::int64_t i;
            if (float_to_integer(float_of(aKey), i))
                return get_integer(i);
        }
        else if (is_string(aKey))
            return get_string(aKey);
        else if (is_nil(aKey))
            return Nil;
        auto const slot = find(aKey);
        return slot < nodes.size() ? nodes[slot].v : Nil;
    }

    value table::get_integer(std::int64_t aKey) const
    {
        if (aKey >= 1 && static_cast<std::uint64_t>(aKey) <= array.size())
            return array[static_cast<std::size_t>(aKey - 1)];
        if (nodes.empty())
            return Nil;
        auto const mask = nodes.size() - 1u;
        for (auto i = hash_integer(aKey) & mask;; i = (i + 1u) & mask)
        {
            auto const& n = nodes[i];
            if (n.key == Empty)
                return Nil;
            if (is_integer(n.key) && integer_of(n.key) == aKey)
                return n.v;
        }
    }

    value table::get_string(value aKey) const
    {
        if (nodes.empty())
            return Nil;
        auto& s = cell_of<string_cell>(aKey);
        auto const mask = nodes.size() - 1u;
        for (auto i = s.hash() & mask;; i = (i + 1u) & mask)
        {
            auto const& n = nodes[i];
            if (n.key == Empty)
                return Nil;
            if (n.key == aKey)
                return n.v;
            if (!s.interned && is_string(n.key) && !cell_of<string_cell>(n.key).interned && str(n.key) == s.text)
                return n.v;
        }
    }

    void table::rehash(std::size_t aCount)
    {
        std::size_t size = 4u;
        while (size * 3u < aCount * 4u)
            size *= 2u;
        std::vector<node> const old(nodes.begin(), nodes.end());
        nodes.reset(size);
        used = 0u;
        the_heap().account(size * sizeof(node));
        auto const mask = size - 1u;
        for (auto const& n : old)
            if (n.key != Empty && n.key != Nil && n.v != Nil)
            {
                auto i = hash_key(n.key) & mask;
                while (nodes[i].key != Empty)
                    i = (i + 1u) & mask;
                nodes[i] = n;
                ++used;
            }
    }

    void table::insert(value aKey, value aValue)
    {
        if ((used + 1u) * 4u > nodes.size() * 3u)
        {
            std::size_t live = 0u;
            for (auto const& n : nodes)
                if (n.key != Empty && n.key != Nil && n.v != Nil)
                    ++live;
            rehash(std::max<std::size_t>(live + 1u, live * 2u));
        }
        auto const mask = nodes.size() - 1u;
        auto i = hash_key(aKey) & mask;
        while (nodes[i].key != Empty)
            i = (i + 1u) & mask;
        nodes[i] = node{ aKey, aValue };
        ++used;
    }

    void table::migrate()
    {
        // (the keys that follow the array part (from the hash part))
        while (!nodes.empty())
        {
            auto const k = static_cast<std::int64_t>(array.size()) + 1;
            auto const mask = nodes.size() - 1u;
            bool found = false;
            for (auto i = hash_integer(k) & mask;; i = (i + 1u) & mask)
            {
                auto& n = nodes[i];
                if (n.key == Empty)
                    break;
                if (is_integer(n.key) && integer_of(n.key) == k)
                {
                    if (n.v != Nil)
                    {
                        array.push_back(n.v);
                        n.v = Nil;
                        found = true;
                    }
                    break;
                }
            }
            if (!found)
                break;
        }
    }

    void table::set_integer(std::int64_t aKey, value aValue)
    {
        if (aKey >= 1 && static_cast<std::uint64_t>(aKey) <= array.size())
        {
            array[static_cast<std::size_t>(aKey - 1)] = aValue;
            return;
        }
        if (aKey >= 1 && static_cast<std::uint64_t>(aKey) == array.size() + 1u && aValue != Nil)
        {
            // (it extends the array part (its entry in the hash part, if it has one, is removed))
            if (!nodes.empty())
            {
                auto const mask = nodes.size() - 1u;
                for (auto i = hash_integer(aKey) & mask;; i = (i + 1u) & mask)
                {
                    auto& n = nodes[i];
                    if (n.key == Empty)
                        break;
                    if (is_integer(n.key) && integer_of(n.key) == aKey)
                    {
                        n.v = Nil;
                        break;
                    }
                }
            }
            array.push_back(aValue);
            the_heap().account(sizeof(value));
            migrate();
            return;
        }
        if (!nodes.empty())
        {
            auto const mask = nodes.size() - 1u;
            for (auto i = hash_integer(aKey) & mask;; i = (i + 1u) & mask)
            {
                auto& n = nodes[i];
                if (n.key == Empty)
                    break;
                if (is_integer(n.key) && integer_of(n.key) == aKey)
                {
                    n.v = aValue;
                    return;
                }
            }
        }
        if (aValue == Nil)
            return;
        insert(make_integer(aKey), aValue);
    }

    void table::set(value aKey, value aValue)
    {
        if (is_inline_integer(aKey))
        {
            set_integer(inline_integer(aKey), aValue);
            return;
        }
        if (is_float(aKey))
        {
            std::int64_t i;
            if (float_to_integer(float_of(aKey), i))
            {
                set_integer(i, aValue);
                return;
            }
        }
        else if (is_integer(aKey))
        {
            set_integer(integer_of(aKey), aValue);
            return;
        }
        auto const slot = find(aKey);
        if (slot < nodes.size())
        {
            nodes[slot].v = aValue;
            return;
        }
        if (aValue == Nil)
            return;
        insert(aKey, aValue);
    }

    std::int64_t table::length() const
    {
        // (a border: a non-nil value whose successor is nil (or 0 if t[1] is nil))
        auto const n = array.size();
        if (n > 0u && array[n - 1u] == Nil)
        {
            std::size_t i = 0u;
            std::size_t j = n;
            while (j - i > 1u)
            {
                auto const m = (i + j) / 2u;
                if (array[m - 1u] == Nil)
                    j = m;
                else
                    i = m;
            }
            return static_cast<std::int64_t>(i);
        }
        if (nodes.empty() || get_integer(static_cast<std::int64_t>(n) + 1) == Nil)
            return static_cast<std::int64_t>(n);
        // (the hash part continues it: an unbound search)
        std::uint64_t i = n + 1u;
        std::uint64_t j = i * 2u;
        while (get_integer(static_cast<std::int64_t>(j)) != Nil)
        {
            i = j;
            if (j > 0x3FFFFFFFFFFFFFFFull)
            {
                // (a pathological table: a linear search)
                std::uint64_t k = 1u;
                while (get_integer(static_cast<std::int64_t>(k)) != Nil)
                    ++k;
                return static_cast<std::int64_t>(k - 1u);
            }
            j *= 2u;
        }
        while (j - i > 1u)
        {
            auto const m = (i + j) / 2u;
            if (get_integer(static_cast<std::int64_t>(m)) == Nil)
                j = m;
            else
                i = m;
        }
        return static_cast<std::int64_t>(i);
    }

    bool table::next(value& aKey, value& aValue) const
    {
        std::size_t position = 0u;
        if (!is_nil(aKey))
        {
            std::int64_t k;
            bool found = false;
            if (to_integer(aKey, k) && k >= 1 && static_cast<std::uint64_t>(k) <= array.size())
            {
                position = static_cast<std::size_t>(k);
                found = true;
            }
            else
            {
                auto const key = normalize_key(aKey);
                auto const slot = find(key);
                if (slot < nodes.size())
                {
                    position = array.size() + slot + 1u;
                    found = true;
                }
            }
            if (!found)
                runtime_error("invalid key to 'next'");
        }
        for (; position < array.size(); ++position)
            if (array[position] != Nil)
            {
                aKey = make_integer(static_cast<std::int64_t>(position) + 1);
                aValue = array[position];
                return true;
            }
        for (auto slot = position - array.size(); slot < nodes.size(); ++slot)
        {
            auto const& n = nodes[slot];
            if (n.key != Empty && n.key != Nil && n.v != Nil)
            {
                aKey = n.key;
                aValue = n.v;
                return true;
            }
        }
        return false;
    }
}
