/*
  set.cpp

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

  Portions of this file are derived from CPython 3.13 (Objects/setobject.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// Python's set and frozenset: a port of CPython 3.13's Objects/setobject.c (the same hash table: open addressing with
// linear probes and then perturbed probing, the same resizing and the same algorithms for the set operations) so that
// sets iterate (and print) in CPython's order

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            constexpr long long kSetMinSize = 8;
            constexpr int kLinearProbes = 9;
            constexpr int kPerturbShift = 5;
            constexpr int kDiscardNotFound = 0;
            constexpr int kDiscardFound = 1;

            struct set_entry
            {
                object* key;
                long long hash;
            };

            // (the key of a deleted entry: its hash is -1)
            neos_rt_object sDummy{};
            object* const dummy = &sDummy;

            struct set_object
            {
                header h;
                long long fill = 0;     ///< (active and dummy entries)
                long long used = 0;     ///< (active entries)
                long long mask = kSetMinSize - 1;
                set_entry* table = smalltable;
                long long hash = -1;    ///< (a frozenset's hash; -1: not computed)
                long long finger = 0;   ///< (where pop() looks first)
                set_entry smalltable[kSetMinSize] = {};

                explicit set_object(neos_py_type const& aType) { h.base.type = &aType.base; }
                set_object(set_object const&) = delete;
                ~set_object()
                {
                    for (long long i = 0; i <= mask; ++i)
                        if (table[i].key != nullptr && table[i].key != dummy)
                            release(table[i].key);
                    if (table != smalltable)
                        delete[] table;
                }
            };

            struct set_iterator_object
            {
                header h;
                set_object* set;        ///< (null when the iterator is exhausted)
                long long used;
                long long pos = 0;
                long long len;
                ~set_iterator_object() { release(set != nullptr ? &set->h.base : nullptr); }
            };

            neos_py_type make_set_type(bool aFrozen);
            neos_py_type make_set_iterator_type();
            neos_py_type sSetType = make_set_type(false);
            neos_py_type sFrozenSetType = make_set_type(true);
            neos_py_type sSetIteratorType = make_set_iterator_type();

            bool is_set(object* aObject) { return aObject != nullptr && aObject->type == &sSetType.base; }
            bool is_frozenset(object* aObject) { return aObject != nullptr && aObject->type == &sFrozenSetType.base; }
            bool is_any_set(object* aObject) { return is_set(aObject) || is_frozenset(aObject); }
            set_object* as_set(object* aObject) { return reinterpret_cast<set_object*>(aObject); }
            object* as_object(set_object* aSet) { return &aSet->h.base; }
            neos_py_type const& type_of(set_object* aSet) { return aSet->h.base.type == &sSetType.base ? sSetType : sFrozenSetType; }

            int table_resize(set_object* so, long long minused);

            set_entry* lookkey(set_object* so, object* key, long long hash)
            {
                auto perturb = static_cast<std::uint64_t>(hash);
                auto mask = static_cast<std::uint64_t>(so->mask);
                auto i = static_cast<std::uint64_t>(hash) & mask;
                for (;;)
                {
                    auto entry = &so->table[i];
                    int probes = (i + kLinearProbes <= mask) ? kLinearProbes : 0;
                    do
                    {
                        if (entry->hash == 0 && entry->key == nullptr)
                            return entry;
                        if (entry->hash == hash)
                        {
                            auto const startkey = entry->key;
                            if (startkey == key)
                                return entry;
                            auto const table = so->table;
                            retain(startkey);
                            auto const cmp = equal(startkey, key);
                            release(startkey);
                            if (cmp < 0)
                                return nullptr;
                            if (table != so->table || entry->key != startkey)
                                return lookkey(so, key, hash);
                            if (cmp > 0)
                                return entry;
                            mask = static_cast<std::uint64_t>(so->mask);
                        }
                        ++entry;
                    } while (probes--);
                    perturb >>= kPerturbShift;
                    i = (i * 5u + 1u + perturb) & mask;
                }
            }

            int add_entry(set_object* so, object* key, long long hash)
            {
                set_entry* table;
                set_entry* freeslot;
                set_entry* entry;
                std::uint64_t perturb;
                std::uint64_t mask;
                std::uint64_t i;
                int probes;
                int cmp;
                retain(key);
            restart:
                mask = static_cast<std::uint64_t>(so->mask);
                i = static_cast<std::uint64_t>(hash) & mask;
                freeslot = nullptr;
                perturb = static_cast<std::uint64_t>(hash);
                for (;;)
                {
                    entry = &so->table[i];
                    probes = (i + kLinearProbes <= mask) ? kLinearProbes : 0;
                    do
                    {
                        if (entry->hash == 0 && entry->key == nullptr)
                            goto found_unused_or_dummy;
                        if (entry->hash == hash)
                        {
                            object* startkey = entry->key;
                            if (startkey == key)
                                goto found_active;
                            table = so->table;
                            retain(startkey);
                            cmp = equal(startkey, key);
                            release(startkey);
                            if (cmp > 0)
                                goto found_active;
                            if (cmp < 0)
                                goto comparison_error;
                            if (table != so->table || entry->key != startkey)
                                goto restart;
                            mask = static_cast<std::uint64_t>(so->mask);
                        }
                        else if (entry->hash == -1)
                            freeslot = entry;
                        ++entry;
                    } while (probes--);
                    perturb >>= kPerturbShift;
                    i = (i * 5u + 1u + perturb) & mask;
                }
            found_unused_or_dummy:
                if (freeslot == nullptr)
                    goto found_unused;
                if (freeslot->hash != -1)
                    goto restart;
                ++so->used;
                freeslot->key = key;
                freeslot->hash = hash;
                return 0;
            found_unused:
                ++so->fill;
                ++so->used;
                entry->key = key;
                entry->hash = hash;
                if (static_cast<std::uint64_t>(so->fill) * 5u < mask * 3u)
                    return 0;
                return table_resize(so, so->used > 50000 ? so->used * 2 : so->used * 4);
            found_active:
                release(key);
                return 0;
            comparison_error:
                release(key);
                return -1;
            }

            // (a key known to be absent; the caller updates fill and used)
            void insert_clean(set_entry* table, std::uint64_t mask, object* key, long long hash)
            {
                auto perturb = static_cast<std::uint64_t>(hash);
                auto i = static_cast<std::uint64_t>(hash) & mask;
                set_entry* entry;
                for (;;)
                {
                    entry = &table[i];
                    if (entry->key == nullptr)
                        goto found_null;
                    if (i + kLinearProbes <= mask)
                    {
                        for (int j = 0; j < kLinearProbes; ++j)
                        {
                            ++entry;
                            if (entry->key == nullptr)
                                goto found_null;
                        }
                    }
                    perturb >>= kPerturbShift;
                    i = (i * 5u + 1u + perturb) & mask;
                }
            found_null:
                entry->key = key;
                entry->hash = hash;
            }

            int table_resize(set_object* so, long long minused)
            {
                auto const oldmask = so->mask;
                set_entry small_copy[kSetMinSize];
                std::uint64_t newsize = kSetMinSize;
                while (newsize <= static_cast<std::uint64_t>(minused))
                    newsize <<= 1;
                auto oldtable = so->table;
                bool const is_oldtable_malloced = oldtable != so->smalltable;
                set_entry* newtable;
                if (newsize == kSetMinSize)
                {
                    newtable = so->smalltable;
                    if (newtable == oldtable)
                    {
                        if (so->fill == so->used)
                            return 0;
                        std::memcpy(small_copy, oldtable, sizeof(small_copy));
                        oldtable = small_copy;
                    }
                }
                else
                    newtable = new set_entry[newsize];
                std::memset(newtable, 0, sizeof(set_entry) * newsize);
                so->mask = static_cast<long long>(newsize - 1u);
                so->table = newtable;
                auto const newmask = static_cast<std::uint64_t>(so->mask);
                if (so->fill == so->used)
                {
                    for (auto entry = oldtable; entry <= oldtable + oldmask; ++entry)
                        if (entry->key != nullptr)
                            insert_clean(newtable, newmask, entry->key, entry->hash);
                }
                else
                {
                    so->fill = so->used;
                    for (auto entry = oldtable; entry <= oldtable + oldmask; ++entry)
                        if (entry->key != nullptr && entry->key != dummy)
                            insert_clean(newtable, newmask, entry->key, entry->hash);
                }
                if (is_oldtable_malloced)
                    delete[] oldtable;
                return 0;
            }

            int contains_entry(set_object* so, object* key, long long hash)
            {
                auto const entry = lookkey(so, key, hash);
                if (entry != nullptr)
                    return entry->key != nullptr ? 1 : 0;
                return -1;
            }

            int discard_entry(set_object* so, object* key, long long hash)
            {
                auto const entry = lookkey(so, key, hash);
                if (entry == nullptr)
                    return -1;
                if (entry->key == nullptr)
                    return kDiscardNotFound;
                auto const old_key = entry->key;
                entry->key = dummy;
                entry->hash = -1;
                --so->used;
                release(old_key);
                return kDiscardFound;
            }

            int add_key(set_object* so, object* key)
            {
                long long hash = 0;
                if (!hash_of(key, hash))
                    return -1;
                return add_entry(so, key, hash);
            }

            int contains_key(set_object* so, object* key)
            {
                long long hash = 0;
                if (!hash_of(key, hash))
                    return -1;
                return contains_entry(so, key, hash);
            }

            int discard_key(set_object* so, object* key)
            {
                long long hash = 0;
                if (!hash_of(key, hash))
                    return -1;
                return discard_entry(so, key, hash);
            }

            void empty_to_minsize(set_object* so)
            {
                std::memset(so->smalltable, 0, sizeof(so->smalltable));
                so->fill = 0;
                so->used = 0;
                so->mask = kSetMinSize - 1;
                so->table = so->smalltable;
                so->hash = -1;
            }

            int clear_internal(set_object* so)
            {
                auto table = so->table;
                auto const fill = so->fill;
                auto used = so->used;
                bool const table_is_malloced = table != so->smalltable;
                set_entry small_copy[kSetMinSize];
                if (table_is_malloced)
                    empty_to_minsize(so);
                else if (fill > 0)
                {
                    std::memcpy(small_copy, table, sizeof(small_copy));
                    table = small_copy;
                    empty_to_minsize(so);
                }
                for (auto entry = table; used > 0; ++entry)
                    if (entry->key != nullptr && entry->key != dummy)
                    {
                        --used;
                        release(entry->key);
                    }
                if (table_is_malloced)
                    delete[] table;
                return 0;
            }

            bool set_next(set_object* so, long long& pos, set_entry*& entry_ptr)
            {
                auto i = pos;
                auto const mask = so->mask;
                auto entry = &so->table[i];
                while (i <= mask && (entry->key == nullptr || entry->key == dummy))
                {
                    ++i;
                    ++entry;
                }
                pos = i + 1;
                if (i > mask)
                    return false;
                entry_ptr = entry;
                return true;
            }

            int merge(set_object* so, set_object* other)
            {
                if (other == so || other->used == 0)
                    return 0;
                if ((so->fill + other->used) * 5 >= so->mask * 3)
                    if (table_resize(so, (so->used + other->used) * 2) != 0)
                        return -1;
                auto so_entry = so->table;
                auto other_entry = other->table;
                // (if the table is empty, and the other's is the same size with no dummies, the entries are copied)
                if (so->fill == 0 && so->mask == other->mask && other->fill == other->used)
                {
                    for (long long i = 0; i <= other->mask; ++i, ++so_entry, ++other_entry)
                    {
                        auto const key = other_entry->key;
                        if (key != nullptr)
                        {
                            retain(key);
                            so_entry->key = key;
                            so_entry->hash = other_entry->hash;
                        }
                    }
                    so->fill = other->fill;
                    so->used = other->used;
                    return 0;
                }
                // (if the table is empty, there is no need to check for duplicates)
                if (so->fill == 0)
                {
                    auto const newtable = so->table;
                    auto const newmask = static_cast<std::uint64_t>(so->mask);
                    so->fill = other->used;
                    so->used = other->used;
                    for (auto i = other->mask + 1; i > 0; --i, ++other_entry)
                    {
                        auto const key = other_entry->key;
                        if (key != nullptr && key != dummy)
                        {
                            retain(key);
                            insert_clean(newtable, newmask, key, other_entry->hash);
                        }
                    }
                    return 0;
                }
                for (long long i = 0; i <= other->mask; ++i)
                {
                    other_entry = &other->table[i];
                    auto const key = other_entry->key;
                    if (key != nullptr && key != dummy)
                        if (add_entry(so, key, other_entry->hash) != 0)
                            return -1;
                }
                return 0;
            }

            int update_dict(set_object* so, object* other)
            {
                auto const& items = as<dict_object>(other)->items;
                auto const dictsize = static_cast<long long>(items.size());
                if ((so->fill + dictsize) * 5 >= so->mask * 3)
                    if (table_resize(so, (so->used + dictsize) * 2) != 0)
                        return -1;
                for (std::size_t i = 0u; i < items.size(); ++i)
                {
                    ref const key = ref::borrowed(items[i].first);
                    if (add_entry(so, key.get(), as<dict_object>(other)->hashes[i]) != 0)
                        return -1;
                }
                return 0;
            }

            int update_iterable(set_object* so, object* other)
            {
                ref const it{ iter(other) };
                if (!it)
                    return -1;
                for (;;)
                {
                    ref const key{ next(it.get()) };
                    if (!key)
                        return failed() ? -1 : 0;
                    if (add_key(so, key.get()) != 0)
                        return -1;
                }
            }

            int update_local(set_object* so, object* other)
            {
                if (is_any_set(other))
                    return merge(so, as_set(other));
                if (kind_of(other) == NEOS_PY_DICT)
                    return update_dict(so, other);
                return update_iterable(so, other);
            }

            int update_internal(set_object* so, object* other)
            {
                if (is_any_set(other))
                {
                    if (as_object(so) == other)
                        return 0;
                    return merge(so, as_set(other));
                }
                if (kind_of(other) == NEOS_PY_DICT)
                    return update_dict(so, other);
                return update_iterable(so, other);
            }

            set_object* make_new_set(neos_py_type const& aType, object* aIterable)
            {
                auto const so = new set_object{ aType };
                if (aIterable != nullptr && update_local(so, aIterable) != 0)
                {
                    release(as_object(so));
                    return nullptr;
                }
                return so;
            }

            object* new_set(neos_py_type const& aType, object* aIterable)
            {
                auto const so = make_new_set(aType, aIterable);
                return so != nullptr ? as_object(so) : nullptr;
            }

            void swap_bodies(set_object* a, set_object* b)
            {
                std::swap(a->fill, b->fill);
                std::swap(a->used, b->used);
                std::swap(a->mask, b->mask);
                auto u = a->table;
                if (a->table == a->smalltable)
                    u = b->smalltable;
                a->table = b->table;
                if (b->table == b->smalltable)
                    a->table = a->smalltable;
                b->table = u;
                if (a->table == a->smalltable || b->table == b->smalltable)
                {
                    set_entry tab[kSetMinSize];
                    std::memcpy(tab, a->smalltable, sizeof(tab));
                    std::memcpy(a->smalltable, b->smalltable, sizeof(tab));
                    std::memcpy(b->smalltable, tab, sizeof(tab));
                }
                if (is_frozenset(as_object(a)) && is_frozenset(as_object(b)))
                    std::swap(a->hash, b->hash);
                else
                {
                    a->hash = -1;
                    b->hash = -1;
                }
            }

            object* copy(set_object* so)
            {
                return new_set(type_of(so), as_object(so));
            }

            object* intersection(set_object* so, object* other)
            {
                if (as_object(so) == other)
                    return copy(so);
                ref result{ new_set(type_of(so), nullptr) };
                auto const r = as_set(result.get());
                if (is_any_set(other))
                {
                    auto o = as_set(other);
                    if (o->used > so->used)
                        std::swap(so, o);
                    long long pos = 0;
                    set_entry* entry;
                    while (set_next(o, pos, entry))
                    {
                        ref const key = ref::borrowed(entry->key);
                        auto const hash = entry->hash;
                        auto const rv = contains_entry(so, key.get(), hash);
                        if (rv < 0)
                            return nullptr;
                        if (rv != 0 && add_entry(r, key.get(), hash) != 0)
                            return nullptr;
                    }
                    return result.release_ownership();
                }
                ref const it{ iter(other) };
                if (!it)
                    return nullptr;
                for (;;)
                {
                    ref const key{ next(it.get()) };
                    if (!key)
                        break;
                    long long hash = 0;
                    if (!hash_of(key.get(), hash))
                        return nullptr;
                    auto const rv = contains_entry(so, key.get(), hash);
                    if (rv < 0)
                        return nullptr;
                    if (rv != 0)
                    {
                        if (add_entry(r, key.get(), hash) != 0)
                            return nullptr;
                        if (r->used >= so->used)
                            break;
                    }
                }
                if (failed())
                    return nullptr;
                return result.release_ownership();
            }

            object* intersection_multi(set_object* so, std::vector<object*> const& aOthers)
            {
                if (aOthers.empty())
                    return copy(so);
                ref result = ref::borrowed(as_object(so));
                for (auto other : aOthers)
                {
                    ref newresult{ intersection(as_set(result.get()), other) };
                    if (!newresult)
                        return nullptr;
                    result = std::move(newresult);
                }
                return result.release_ownership();
            }

            int intersection_update(set_object* so, object* aTmp)
            {
                ref const tmp{ aTmp };
                if (!tmp)
                    return -1;
                swap_bodies(so, as_set(tmp.get()));
                return 0;
            }

            object* isdisjoint(set_object* so, object* other)
            {
                if (as_object(so) == other)
                    return make_bool(so->used == 0);
                if (is_any_set(other))
                {
                    auto o = as_set(other);
                    if (o->used > so->used)
                        std::swap(so, o);
                    long long pos = 0;
                    set_entry* entry;
                    while (set_next(o, pos, entry))
                    {
                        ref const key = ref::borrowed(entry->key);
                        auto const rv = contains_entry(so, key.get(), entry->hash);
                        if (rv < 0)
                            return nullptr;
                        if (rv != 0)
                            return make_bool(false);
                    }
                    return make_bool(true);
                }
                ref const it{ iter(other) };
                if (!it)
                    return nullptr;
                for (;;)
                {
                    ref const key{ next(it.get()) };
                    if (!key)
                        break;
                    auto const rv = contains_key(so, key.get());
                    if (rv < 0)
                        return nullptr;
                    if (rv != 0)
                        return make_bool(false);
                }
                if (failed())
                    return nullptr;
                return make_bool(true);
            }

            int difference_update_internal(set_object* so, object* other)
            {
                if (as_object(so) == other)
                    return clear_internal(so);
                if (is_any_set(other))
                {
                    // (if the other set is more than 8 times larger, its intersection with this one)
                    ref o;
                    if ((as_set(other)->used >> 3) > so->used)
                    {
                        o = ref{ intersection(so, other) };
                        if (!o)
                            return -1;
                    }
                    else
                        o = ref::borrowed(other);
                    long long pos = 0;
                    set_entry* entry;
                    while (set_next(as_set(o.get()), pos, entry))
                    {
                        ref const key = ref::borrowed(entry->key);
                        if (discard_entry(so, key.get(), entry->hash) < 0)
                            return -1;
                    }
                }
                else
                {
                    ref const it{ iter(other) };
                    if (!it)
                        return -1;
                    for (;;)
                    {
                        ref const key{ next(it.get()) };
                        if (!key)
                            break;
                        if (discard_key(so, key.get()) < 0)
                            return -1;
                    }
                    if (failed())
                        return -1;
                }
                // (if more than 1/4 of the entries are dummies, they are resized away)
                if (static_cast<std::uint64_t>(so->fill - so->used) <= static_cast<std::uint64_t>(so->mask) / 4u)
                    return 0;
                return table_resize(so, so->used > 50000 ? so->used * 2 : so->used * 4);
            }

            object* copy_and_difference(set_object* so, object* other)
            {
                ref result{ copy(so) };
                if (!result)
                    return nullptr;
                if (difference_update_internal(as_set(result.get()), other) == 0)
                    return result.release_ownership();
                return nullptr;
            }

            object* difference(set_object* so, object* other)
            {
                long long other_size = 0;
                if (is_any_set(other))
                    other_size = as_set(other)->used;
                else if (kind_of(other) == NEOS_PY_DICT)
                    other_size = static_cast<long long>(as<dict_object>(other)->items.size());
                else
                    return copy_and_difference(so, other);
                // (if this set is much larger than the other: a copy with the other's elements removed)
                if ((so->used >> 2) > other_size)
                    return copy_and_difference(so, other);
                ref result{ new_set(type_of(so), nullptr) };
                auto const r = as_set(result.get());
                long long pos = 0;
                set_entry* entry;
                if (kind_of(other) == NEOS_PY_DICT)
                {
                    while (set_next(so, pos, entry))
                    {
                        ref const key = ref::borrowed(entry->key);
                        auto const hash = entry->hash;
                        auto const rv = contains(other, key.get());
                        if (rv < 0)
                            return nullptr;
                        if (rv == 0 && add_entry(r, key.get(), hash) != 0)
                            return nullptr;
                    }
                    return result.release_ownership();
                }
                while (set_next(so, pos, entry))
                {
                    ref const key = ref::borrowed(entry->key);
                    auto const hash = entry->hash;
                    auto const rv = contains_entry(as_set(other), key.get(), hash);
                    if (rv < 0)
                        return nullptr;
                    if (rv == 0 && add_entry(r, key.get(), hash) != 0)
                        return nullptr;
                }
                return result.release_ownership();
            }

            object* difference_multi(set_object* so, std::vector<object*> const& aOthers)
            {
                if (aOthers.empty())
                    return copy(so);
                ref result{ difference(so, aOthers[0]) };
                if (!result)
                    return nullptr;
                for (std::size_t i = 1u; i < aOthers.size(); ++i)
                    if (difference_update_internal(as_set(result.get()), aOthers[i]) != 0)
                        return nullptr;
                return result.release_ownership();
            }

            int symmetric_difference_update_dict(set_object* so, object* other)
            {
                auto const& items = as<dict_object>(other)->items;
                for (std::size_t i = 0u; i < items.size(); ++i)
                {
                    ref const key = ref::borrowed(items[i].first);
                    auto const hash = as<dict_object>(other)->hashes[i];
                    auto const rv = discard_entry(so, key.get(), hash);
                    if (rv < 0)
                        return -1;
                    if (rv == kDiscardNotFound && add_entry(so, key.get(), hash) != 0)
                        return -1;
                }
                return 0;
            }

            int symmetric_difference_update_set(set_object* so, set_object* other)
            {
                long long pos = 0;
                set_entry* entry;
                while (set_next(other, pos, entry))
                {
                    ref const key = ref::borrowed(entry->key);
                    auto const hash = entry->hash;
                    auto const rv = discard_entry(so, key.get(), hash);
                    if (rv < 0)
                        return -1;
                    if (rv == kDiscardNotFound && add_entry(so, key.get(), hash) != 0)
                        return -1;
                }
                return 0;
            }

            int symmetric_difference_update(set_object* so, object* other)
            {
                if (as_object(so) == other)
                    return clear_internal(so);
                if (kind_of(other) == NEOS_PY_DICT)
                    return symmetric_difference_update_dict(so, other);
                if (is_any_set(other))
                    return symmetric_difference_update_set(so, as_set(other));
                ref const otherset{ new_set(type_of(so), other) };
                if (!otherset)
                    return -1;
                return symmetric_difference_update_set(so, as_set(otherset.get()));
            }

            object* symmetric_difference(set_object* so, object* other)
            {
                ref result{ new_set(type_of(so), nullptr) };
                if (update_local(as_set(result.get()), other) < 0)
                    return nullptr;
                if (symmetric_difference_update_set(as_set(result.get()), so) < 0)
                    return nullptr;
                return result.release_ownership();
            }

            object* issubset(set_object* so, object* other)
            {
                if (!is_any_set(other))
                {
                    ref const tmp{ intersection(so, other) };
                    if (!tmp)
                        return nullptr;
                    return make_bool(as_set(tmp.get())->used == so->used);
                }
                if (so->used > as_set(other)->used)
                    return make_bool(false);
                long long pos = 0;
                set_entry* entry;
                while (set_next(so, pos, entry))
                {
                    ref const key = ref::borrowed(entry->key);
                    auto const rv = contains_entry(as_set(other), key.get(), entry->hash);
                    if (rv < 0)
                        return nullptr;
                    if (rv == 0)
                        return make_bool(false);
                }
                return make_bool(true);
            }

            object* issuperset(set_object* so, object* other)
            {
                if (is_any_set(other))
                    return issubset(as_set(other), as_object(so));
                ref const it{ iter(other) };
                if (!it)
                    return nullptr;
                for (;;)
                {
                    ref const key{ next(it.get()) };
                    if (!key)
                        break;
                    auto const rv = contains_key(so, key.get());
                    if (rv < 0)
                        return nullptr;
                    if (rv == 0)
                        return make_bool(false);
                }
                if (failed())
                    return nullptr;
                return make_bool(true);
            }

            // (a set as the key of contains, remove and discard: as a frozenset)
            template <typename Operation>
            int with_set_key(set_object* so, object* key, Operation aOperation)
            {
                auto rv = aOperation(so, key);
                if (rv < 0)
                {
                    if (!is_set(key) || !std::string_view{ error_text() }.starts_with("TypeError"))
                        return -1;
                    clear_error();
                    ref const tmpkey{ new_set(sFrozenSetType, key) };
                    if (!tmpkey)
                        return -1;
                    rv = aOperation(so, tmpkey.get());
                }
                return rv;
            }

            long long frozenset_hash_impl(set_object* so)
            {
                auto const shuffle_bits = [](std::uint64_t h) { return ((h ^ 89869747ull) ^ (h << 16)) * 3644798167ull; };
                std::uint64_t hash = 0u;
                for (auto entry = so->table; entry <= &so->table[so->mask]; ++entry)
                    hash ^= shuffle_bits(static_cast<std::uint64_t>(entry->hash));
                if (((so->mask + 1 - so->fill) & 1) != 0)
                    hash ^= shuffle_bits(0u);
                if (((so->fill - so->used) & 1) != 0)
                    hash ^= shuffle_bits(static_cast<std::uint64_t>(-1));
                hash ^= (static_cast<std::uint64_t>(so->used) + 1u) * 1927868237ull;
                hash ^= (hash >> 11) ^ (hash >> 25);
                hash = hash * 69069u + 907133923ull;
                if (hash == static_cast<std::uint64_t>(-1))
                    hash = 590923713ull;
                return static_cast<long long>(hash);
            }

            object* pop(set_object* so)
            {
                auto entry = so->table + (so->finger & so->mask);
                auto const limit = so->table + so->mask;
                if (so->used == 0)
                {
                    ref const message{ make_str("pop from an empty set") };
                    raise_key_error(message.get());
                    return nullptr;
                }
                while (entry->key == nullptr || entry->key == dummy)
                {
                    ++entry;
                    if (entry > limit)
                        entry = so->table;
                }
                auto const key = entry->key;
                entry->key = dummy;
                entry->hash = -1;
                --so->used;
                so->finger = entry - so->table + 1;
                return key;
            }

            // --- the methods

            enum class arguments { None, One, Any };

            struct set_method
            {
                char const* name;
                bool mutating;      ///< (a set's, not a frozenset's)
                arguments args;
                object* (*function)(set_object* aSelf, std::vector<object*> const& aArgs);
            };

            object* none_result(int aResult) { return aResult < 0 ? nullptr : none(); }

            set_method const sSetMethods[] =
            {
                { "add", true, arguments::One, [](set_object* s, std::vector<object*> const& a) { return none_result(add_key(s, a[0])); } },
                { "clear", true, arguments::None, [](set_object* s, std::vector<object*> const&) { return none_result(clear_internal(s)); } },
                { "__contains__", false, arguments::One, [](set_object* s, std::vector<object*> const& a) -> object*
                    { auto const rv = with_set_key(s, a[0], contains_key); return rv < 0 ? nullptr : make_bool(rv != 0); } },
                { "copy", false, arguments::None, [](set_object* s, std::vector<object*> const&) -> object*
                    { if (is_frozenset(as_object(s))) { retain(as_object(s)); return as_object(s); } return copy(s); } },
                { "discard", true, arguments::One, [](set_object* s, std::vector<object*> const& a) { return none_result(with_set_key(s, a[0], discard_key)); } },
                { "difference", false, arguments::Any, difference_multi },
                { "difference_update", true, arguments::Any, [](set_object* s, std::vector<object*> const& a) -> object*
                    { for (auto o : a) if (difference_update_internal(s, o) != 0) return nullptr; return none(); } },
                { "intersection", false, arguments::Any, intersection_multi },
                { "intersection_update", true, arguments::Any, [](set_object* s, std::vector<object*> const& a)
                    { return none_result(intersection_update(s, intersection_multi(s, a))); } },
                { "isdisjoint", false, arguments::One, [](set_object* s, std::vector<object*> const& a) { return isdisjoint(s, a[0]); } },
                { "issubset", false, arguments::One, [](set_object* s, std::vector<object*> const& a) { return issubset(s, a[0]); } },
                { "issuperset", false, arguments::One, [](set_object* s, std::vector<object*> const& a) { return issuperset(s, a[0]); } },
                { "pop", true, arguments::None, [](set_object* s, std::vector<object*> const&) { return pop(s); } },
                { "remove", true, arguments::One, [](set_object* s, std::vector<object*> const& a) -> object*
                    {
                        auto const rv = with_set_key(s, a[0], discard_key);
                        if (rv < 0)
                            return nullptr;
                        if (rv == kDiscardNotFound)
                        {
                            raise_key_error(a[0]);
                            return nullptr;
                        }
                        return none();
                    } },
                { "symmetric_difference", false, arguments::One, [](set_object* s, std::vector<object*> const& a) { return symmetric_difference(s, a[0]); } },
                { "symmetric_difference_update", true, arguments::One, [](set_object* s, std::vector<object*> const& a)
                    { return none_result(symmetric_difference_update(s, a[0])); } },
                { "union", false, arguments::Any, [](set_object* s, std::vector<object*> const& a) -> object*
                    {
                        ref result{ copy(s) };
                        if (!result)
                            return nullptr;
                        for (auto o : a)
                            if (o != as_object(s) && update_local(as_set(result.get()), o) != 0)
                                return nullptr;
                        return result.release_ownership();
                    } },
                { "update", true, arguments::Any, [](set_object* s, std::vector<object*> const& a) -> object*
                    { for (auto o : a) if (update_internal(s, o) != 0) return nullptr; return none(); } }
            };

            object* call_set_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& method = *static_cast<set_method const*>(aData);
                auto const& items = as<tuple_object>(aArgs)->items;
                auto const self = as_set(items[0]);
                std::vector<object*> const args{ items.begin() + 1, items.end() };
                std::string const name = std::string{ type_of(self).name } + "." + method.name;
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                    return error("TypeError", name + "() takes no keyword arguments");
                if (method.args == arguments::One && args.size() != 1u)
                    return error("TypeError", name + "() takes exactly one argument (" + std::to_string(args.size()) + " given)");
                if (method.args == arguments::None && !args.empty())
                    return error("TypeError", name + "() takes no arguments (" + std::to_string(args.size()) + " given)");
                return method.function(self, args);
            }

            // --- the types

            neos_py_type make_set_type(bool aFrozen)
            {
                auto type = aFrozen ?
                    make_type("frozenset", "frozenset", retain_object, release_object<set_object>) :
                    make_type("set", "set", retain_object, release_object<set_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const so = as_set(aSelf);
                    std::string const name = type_of(so).name;
                    if (so->used == 0)
                        return make_str(name + "()");
                    std::vector<object*> keys;
                    long long pos = 0;
                    set_entry* entry;
                    while (set_next(so, pos, entry))
                        keys.push_back(entry->key);
                    std::string text;
                    if (!join_repr(keys, text))
                        return nullptr;
                    return make_str(is_set(aSelf) ? "{" + text + "}" : name + "({" + text + "})");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    for (auto const& method : sSetMethods)
                        if (std::strcmp(method.name, aName) == 0 && (!method.mutating || is_set(aSelf)))
                            return make_function(method.name, call_set_method, const_cast<set_method*>(&method), aSelf);
                    return error("AttributeError", std::string{ "'" } + type_of(as_set(aSelf)).name + "' object has no attribute '" + aName + "'");
                };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace) -> object*
                {
                    if (aInplace != 0)
                    {
                        // (a set's augmented assignments update it: |=, &=, -= and ^=; a frozenset's are its binary operations)
                        if (!is_set(aLhs) || !is_any_set(aRhs))
                            return not_implemented();
                        auto const so = as_set(aLhs);
                        int result = 0;
                        switch (aOperation)
                        {
                        case NEOS_PY_OR: result = update_internal(so, aRhs); break;
                        case NEOS_PY_AND: result = intersection_update(so, intersection(so, aRhs)); break;
                        case NEOS_PY_SUBTRACT: result = difference_update_internal(so, aRhs); break;
                        case NEOS_PY_XOR: result = symmetric_difference_update(so, aRhs); break;
                        default: return not_implemented();
                        }
                        if (result < 0)
                            return nullptr;
                        retain(aLhs);
                        return aLhs;
                    }
                    if (!is_any_set(aLhs) || !is_any_set(aRhs))
                        return not_implemented();
                    auto const so = as_set(aLhs);
                    switch (aOperation)
                    {
                    case NEOS_PY_OR:
                        {
                            ref result{ copy(so) };
                            if (!result)
                                return nullptr;
                            if (aLhs != aRhs && update_local(as_set(result.get()), aRhs) != 0)
                                return nullptr;
                            return result.release_ownership();
                        }
                    case NEOS_PY_AND: return intersection(so, aRhs);
                    case NEOS_PY_SUBTRACT: return difference(so, aRhs);
                    case NEOS_PY_XOR: return symmetric_difference(so, aRhs);
                    default: return not_implemented();
                    }
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (!is_any_set(aLhs) || !is_any_set(aRhs))
                        return not_implemented();
                    auto const v = as_set(aLhs);
                    auto const w = as_set(aRhs);
                    switch (aOperation)
                    {
                    case NEOS_PY_EQ:
                    case NEOS_PY_NE:
                        {
                            bool equal = false;
                            if (v->used == w->used && (v->hash == -1 || w->hash == -1 || v->hash == w->hash))
                            {
                                ref const subset{ issubset(v, aRhs) };
                                if (!subset)
                                    return nullptr;
                                equal = subset.get() == make_bool(true);
                            }
                            return make_bool(aOperation == NEOS_PY_EQ ? equal : !equal);
                        }
                    case NEOS_PY_LE: return issubset(v, aRhs);
                    case NEOS_PY_GE: return issuperset(v, aRhs);
                    case NEOS_PY_LT: return v->used >= w->used ? make_bool(false) : issubset(v, aRhs);
                    case NEOS_PY_GT: return v->used <= w->used ? make_bool(false) : issuperset(v, aRhs);
                    default: return not_implemented();
                    }
                };
                type.length = [](neos_py_context*, object* aSelf) -> long long { return as_set(aSelf)->used; };
                type.iter = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const so = as_set(aSelf);
                    retain(aSelf);
                    return &(new set_iterator_object{ { { &sSetIteratorType.base } }, so, so->used, 0, so->used })->h.base;
                };
                type.contains = [](neos_py_context*, object* aSelf, object* aItem) -> int
                {
                    return with_set_key(as_set(aSelf), aItem, contains_key);
                };
                if (aFrozen)
                    type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                    {
                        auto const so = as_set(aSelf);
                        if (so->hash == -1)
                            so->hash = frozenset_hash_impl(so);
                        *aResult = so->hash;
                        return 0;
                    };
                return type;
            }

            neos_py_type make_set_iterator_type()
            {
                auto type = make_type("set_iterator", "set_iterator", retain_object, release_object<set_iterator_object>);
                type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& si = *reinterpret_cast<set_iterator_object*>(aSelf);
                    auto const so = si.set;
                    if (so == nullptr)
                        return nullptr;
                    if (si.used != so->used)
                    {
                        si.used = -1;
                        return error("RuntimeError", "Set changed size during iteration");
                    }
                    auto i = si.pos;
                    auto const entry = so->table;
                    auto const mask = so->mask;
                    while (i <= mask && (entry[i].key == nullptr || entry[i].key == dummy))
                        ++i;
                    si.pos = i + 1;
                    if (i > mask)
                    {
                        si.set = nullptr;
                        release(as_object(so));
                        return nullptr;
                    }
                    --si.len;
                    retain(entry[i].key);
                    return entry[i].key;
                };
                return type;
            }

            bool constructor_arguments(std::vector<object*> const& aArgs, object* aKwargs, char const* aName)
            {
                if (!no_keywords(aKwargs, aName))
                    return false;
                if (aArgs.size() > 1u)
                {
                    error("TypeError", std::string{ aName } + " expected at most 1 argument, got " + std::to_string(aArgs.size()));
                    return false;
                }
                return true;
            }
        }

        // set() and frozenset()
        object* b_set(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!constructor_arguments(aArgs, aKwargs, "set"))
                return nullptr;
            return new_set(sSetType, aArgs.empty() ? nullptr : aArgs[0]);
        }

        object* b_frozenset(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!constructor_arguments(aArgs, aKwargs, "frozenset"))
                return nullptr;
            // (frozenset(f) is f)
            if (!aArgs.empty() && is_frozenset(aArgs[0]))
            {
                retain(aArgs[0]);
                return aArgs[0];
            }
            return new_set(sFrozenSetType, aArgs.empty() ? nullptr : aArgs[0]);
        }

        // (set displays and comprehensions: {a, b, ...}, {x for ...})
        object* fn_set()
        {
            return new_set(sSetType, nullptr);
        }

        void fn_set_add(object* aSet, object* aItem)
        {
            add_key(as_set(aSet), aItem);
        }

        // (a display of three or more constants: CPython builds a frozenset constant and a set from it (BUILD_SET 0, SET_UPDATE),
        // so the set's order is that of an empty set merged with the elements' table)
        void fn_set_of_constants(object* aSet)
        {
            // (the elements as a frozenset constant (made again from its elements when the compiler merges its constants) and 
            // an empty set updated with it)
            ref const constant{ new_set(sFrozenSetType, nullptr) };
            long long pos = 0;
            set_entry* entry;
            while (set_next(as_set(aSet), pos, entry))
                add_entry(as_set(constant.get()), entry->key, entry->hash);
            ref const result{ new_set(sSetType, nullptr) };
            merge(as_set(result.get()), as_set(constant.get()));
            swap_bodies(as_set(aSet), as_set(result.get()));
        }
    }
}
