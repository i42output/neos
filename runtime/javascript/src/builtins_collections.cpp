/*
  builtins_collections.cpp

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

// Keyed collections: Map, Set, WeakMap, WeakSet (and the Map and Set iterators), WeakRef and FinalizationRegistry
//
// A Map's or Set's entries are kept in insertion order in a vector (a deleted entry is a tombstone until the vector is
// compacted) with a hash index (SameValueZero: keys are canonicalized (-0 is +0), strings and BigInts compare by
// content). The positions of the iterations in progress over a collection (its iterators', forEach's, the Set
// methods') are registered with it (cursors) so that compaction and clear() move them: an iteration visits the entries
// added while it is in progress and skips those deleted before it reaches them, as the specification's list of entries
// (with ~empty~ for a deleted one) has it.
//
// There is no garbage collector yet: a weak collection holds its keys strongly, WeakRef.prototype.deref always returns
// the target and a FinalizationRegistry never calls its cleanup callback.

#include <algorithm>
#include <unordered_set>
#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        // --- keys (SameValueZero)

        value canonicalize_key(value aKey)
        {
            if (is_number(aKey))
            {
                auto const n = as_number(aKey);
                if (n == 0.0)
                    return number(0.0);
                if (std::isnan(n))
                    return CanonicalNaN;
            }
            return aKey;
        }

        struct key_hash
        {
            std::size_t operator()(value aKey) const
            {
                if (is_string(aKey))
                    return cell_of<string>(aKey).hash();
                if (is_bigint(aKey))
                {
                    auto const& b = cell_of<bigint>(aKey);
                    std::size_t result = b.negative ? 0x9E3779B97F4A7C15ull : 0u;
                    for (auto const digit : b.magnitude)
                        result = (result ^ digit) * 0x100000001B3ull;
                    return result;
                }
                return std::hash<value>{}(aKey);
            }
        };

        struct key_equal
        {
            bool operator()(value aLhs, value aRhs) const
            {
                // (both canonical: numbers by their bits)
                if (aLhs == aRhs)
                    return true;
                if (is_string(aLhs) && is_string(aRhs))
                    return string_equal(aLhs, aRhs);
                if (is_bigint(aLhs) && is_bigint(aRhs))
                    return bigint_equal(aLhs, aRhs);
                return false;
            }
        };

        // --- a Map's or Set's entries

        class keyed_data
        {
        public:
            struct entry
            {
                value key;      ///< (Empty: deleted)
                value data;
            };
        public:
            keyed_data() = default;
            keyed_data(keyed_data const&) = delete;
            keyed_data& operator=(keyed_data const&) = delete;
        public:
            std::size_t size() const { return iIndex.size(); }
            std::vector<entry> const& entries() const { return iEntries; }
            entry* find(value aKey)
            {
                auto const existing = iIndex.find(canonicalize_key(aKey));
                return existing != iIndex.end() ? &iEntries[existing->second] : nullptr;
            }
            bool has(value aKey)
            {
                return find(aKey) != nullptr;
            }
            void set(value aKey, value aData)
            {
                aKey = canonicalize_key(aKey);
                if (auto* e = find(aKey))
                {
                    e->data = aData;
                    return;
                }
                iIndex.emplace(aKey, static_cast<std::uint32_t>(iEntries.size()));
                iEntries.push_back(entry{ aKey, aData });
            }
            bool remove(value aKey)
            {
                auto const existing = iIndex.find(canonicalize_key(aKey));
                if (existing == iIndex.end())
                    return false;
                auto& e = iEntries[existing->second];
                e.key = Empty;
                e.data = Undefined;
                iIndex.erase(existing);
                ++iDeleted;
                if (iDeleted > 16u && iDeleted > iEntries.size() / 2u)
                    compact();
                return true;
            }
            void clear()
            {
                iEntries.clear();
                iIndex.clear();
                iDeleted = 0u;
                for (auto* c : iCursors)
                    *c = 0u;
            }
            void copy_from(keyed_data const& aOther)
            {
                for (auto const& e : aOther.iEntries)
                    if (e.key != Empty)
                        set(e.key, e.data);
            }
            void add_cursor(std::uint32_t& aCursor)
            {
                iCursors.push_back(&aCursor);
            }
            void remove_cursor(std::uint32_t& aCursor)
            {
                auto const existing = std::find(iCursors.begin(), iCursors.end(), &aCursor);
                if (existing != iCursors.end())
                    iCursors.erase(existing);
            }
        private:
            void compact()
            {
                // (a cursor at an old position moves to the number of live entries before it)
                std::vector<std::uint32_t> before(iEntries.size() + 1u);
                std::uint32_t live = 0u;
                for (std::size_t i = 0u; i < iEntries.size(); ++i)
                {
                    before[i] = live;
                    if (iEntries[i].key != Empty)
                        ++live;
                }
                before[iEntries.size()] = live;
                for (auto* c : iCursors)
                    *c = before[std::min<std::size_t>(*c, iEntries.size())];
                std::erase_if(iEntries, [](entry const& e) { return e.key == Empty; });
                iDeleted = 0u;
                iIndex.clear();
                for (std::uint32_t i = 0u; i < iEntries.size(); ++i)
                    iIndex.emplace(iEntries[i].key, i);
            }
        private:
            std::vector<entry> iEntries;
            std::unordered_map<value, std::uint32_t, key_hash, key_equal> iIndex;
            std::vector<std::uint32_t*> iCursors;
            std::size_t iDeleted = 0u;
        };

        // (an iteration in progress over a collection's entries: its position, registered with the collection while it lasts)
        class cursor
        {
        public:
            explicit cursor(keyed_data& aData) : iData{ aData }
            {
                iData.add_cursor(position);
            }
            ~cursor()
            {
                iData.remove_cursor(position);
            }
            cursor(cursor const&) = delete;
            cursor& operator=(cursor const&) = delete;
        public:
            // (the next entry (none: the end))
            keyed_data::entry const* next()
            {
                while (position < iData.entries().size())
                {
                    auto const& e = iData.entries()[position++];
                    if (e.key != Empty)
                        return &e;
                }
                return nullptr;
            }
        public:
            std::uint32_t position = 0u;
        private:
            keyed_data& iData;
        };

        class collection_object : public object
        {
        public:
            collection_object(value aPrototype, object_class aClass) : object{ aPrototype, aClass } {}
        public:
            keyed_data data;
        };

        class collection_iterator : public object
        {
        public:
            collection_iterator(value aPrototype, object_class aClass) : object{ aPrototype, aClass } {}
        public:
            value collection = Undefined;   ///< (undefined: done)
            std::uint32_t position = 0u;
            int kind = 0;                   ///< (0 keys, 1 values, 2 entries)
        };

        class weak_collection : public object
        {
        public:
            weak_collection(value aPrototype, object_class aClass) : object{ aPrototype, aClass } {}
        public:
            std::unordered_map<value, value> data;  ///< (objects and symbols: by identity)
        };

        class finalization_registry : public object
        {
        public:
            explicit finalization_registry(value aPrototype) : object{ aPrototype, object_class::FinalizationRegistry } {}
        public:
            struct registration
            {
                value target;
                value heldValue;
                value unregisterToken;      ///< (Empty: none)
            };
            value cleanup = Undefined;
            std::vector<registration> cells;
        };

        bool can_be_held_weakly(value aValue)
        {
            if (is_object(aValue))
                return true;
            return is_symbol(aValue) && !cell_of<symbol>(aValue).registered;
        }

        collection_object& this_collection(value aThis, object_class aClass, char const* aMethod)
        {
            if (!is_object(aThis) || obj(aThis).cls != aClass)
                throw_type_error(std::string{ "Method " } + (aClass == object_class::Map ? "Map" : "Set") + ".prototype." + aMethod +
                    " called on incompatible receiver " + describe(aThis));
            return static_cast<collection_object&>(obj(aThis));
        }

        weak_collection& this_weak_collection(value aThis, object_class aClass, char const* aMethod)
        {
            if (!is_object(aThis) || obj(aThis).cls != aClass)
                throw_type_error(std::string{ "Method " } + (aClass == object_class::WeakMap ? "WeakMap" : "WeakSet") + ".prototype." + aMethod +
                    " called on incompatible receiver " + describe(aThis));
            return static_cast<weak_collection&>(obj(aThis));
        }

        value make_collection(value aPrototype, object_class aClass)
        {
            return value_of(the_heap().make<collection_object>(aPrototype, aClass));
        }

        value make_collection_iterator(value aCollection, int aKind)
        {
            auto& c = static_cast<collection_object&>(obj(aCollection));
            bool const isMap = c.cls == object_class::Map;
            auto& it = the_heap().make<collection_iterator>(isMap ? the_realm().mapIteratorPrototype : the_realm().setIteratorPrototype,
                isMap ? object_class::MapIterator : object_class::SetIterator);
            it.collection = aCollection;
            it.kind = aKind;
            c.data.add_cursor(it.position);
            return value_of(it);
        }

        value collection_iterator_next(value aThis, object_class aClass)
        {
            if (!is_object(aThis) || obj(aThis).cls != aClass)
                throw_type_error(std::string{ "next method called on incompatible receiver " } + describe(aThis));
            auto& it = static_cast<collection_iterator&>(obj(aThis));
            if (it.collection == Undefined)
                return create_iter_result_object(Undefined, true);
            auto& data = static_cast<collection_object&>(obj(it.collection)).data;
            while (it.position < data.entries().size())
            {
                auto const e = data.entries()[it.position++];
                if (e.key == Empty)
                    continue;
                if (aClass == object_class::SetIterator)
                {
                    if (it.kind == 2)
                    {
                        value const pair[] = { e.key, e.key };
                        return create_iter_result_object(create_array_from_list(pair), false);
                    }
                    return create_iter_result_object(e.key, false);
                }
                if (it.kind == 0)
                    return create_iter_result_object(e.key, false);
                if (it.kind == 1)
                    return create_iter_result_object(e.data, false);
                value const pair[] = { e.key, e.data };
                return create_iter_result_object(create_array_from_list(pair), false);
            }
            data.remove_cursor(it.position);
            it.collection = Undefined;
            return create_iter_result_object(Undefined, true);
        }

        value map_iterator_next(value aThis, arguments, value, function_object&)
        {
            return collection_iterator_next(aThis, object_class::MapIterator);
        }

        value set_iterator_next(value aThis, arguments, value, function_object&)
        {
            return collection_iterator_next(aThis, object_class::SetIterator);
        }

        value species_getter(value aThis, arguments, value, function_object&)
        {
            return aThis;
        }

        // (the constructors' iterable: each value (Set, WeakSet) or entry (Map, WeakMap) added with the adder)
        void add_from_iterable(value aTarget, value aIterable, value aAdderName, bool aEntries)
        {
            auto const adder = get(aTarget, aAdderName);
            if (!is_callable(adder))
                throw_type_error("'" + utf8(aAdderName) + "' returned for property '" + utf8(aAdderName) + "' of object " + describe(aTarget) + " is not a function");
            auto record = get_iterator(aIterable);
            while (auto const next = iterator_step_value(record))
            {
                try
                {
                    if (aEntries)
                    {
                        if (!is_object(*next))
                            throw_type_error("Iterator value " + describe(*next) + " is not an entry object");
                        auto const k = get(*next, index_key(0u));
                        auto const v = get(*next, index_key(1u));
                        value const args[] = { k, v };
                        call(adder, aTarget, args);
                    }
                    else
                    {
                        value const args[] = { *next };
                        call(adder, aTarget, args);
                    }
                }
                catch (...)
                {
                    iterator_close_on_throw(record);
                    throw;
                }
            }
        }

        // --- Map

        value map_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Constructor Map requires 'new'");
            auto const map = make_collection(get_prototype_from_constructor(aNewTarget, &realm::mapPrototype), object_class::Map);
            auto const iterable = argument(aArguments, 0u);
            if (!is_nullish(iterable))
                add_from_iterable(map, iterable, the_names().set, true);
            return map;
        }

        value map_group_by(value, arguments aArguments, value, function_object&)
        {
            auto const items = argument(aArguments, 0u);
            auto const callback = argument(aArguments, 1u);
            if (is_nullish(items))
                throw_type_error("Map.groupBy called on null or undefined");
            if (!is_callable(callback))
                throw_type_error(describe(callback) + " is not a function");
            auto const result = make_collection(the_realm().mapPrototype, object_class::Map);
            auto& groups = static_cast<collection_object&>(obj(result)).data;
            auto record = get_iterator(items);
            double k = 0.0;
            while (auto const next = iterator_step_value(record))
            {
                value key;
                try
                {
                    value const args[] = { *next, number(k) };
                    key = call(callback, Undefined, args);
                }
                catch (...)
                {
                    iterator_close_on_throw(record);
                    throw;
                }
                if (auto* existing = groups.find(key))
                    static_cast<array_object&>(obj(existing->data)).push(*next);
                else
                {
                    value const element[] = { *next };
                    groups.set(key, create_array_from_list(element));
                }
                ++k;
            }
            return result;
        }

        value map_clear(value aThis, arguments, value, function_object&)
        {
            this_collection(aThis, object_class::Map, "clear").data.clear();
            return Undefined;
        }

        value map_delete(value aThis, arguments aArguments, value, function_object&)
        {
            return boolean(this_collection(aThis, object_class::Map, "delete").data.remove(argument(aArguments, 0u)));
        }

        value map_entries(value aThis, arguments, value, function_object&)
        {
            this_collection(aThis, object_class::Map, "entries");
            return make_collection_iterator(aThis, 2);
        }

        value map_keys(value aThis, arguments, value, function_object&)
        {
            this_collection(aThis, object_class::Map, "keys");
            return make_collection_iterator(aThis, 0);
        }

        value map_values(value aThis, arguments, value, function_object&)
        {
            this_collection(aThis, object_class::Map, "values");
            return make_collection_iterator(aThis, 1);
        }

        value map_for_each(value aThis, arguments aArguments, value, function_object&)
        {
            auto& map = this_collection(aThis, object_class::Map, "forEach");
            auto const callback = argument(aArguments, 0u);
            if (!is_callable(callback))
                throw_type_error(describe(callback) + " is not a function");
            auto const thisArg = argument(aArguments, 1u);
            cursor c{ map.data };
            while (auto const* e = c.next())
            {
                value const args[] = { e->data, e->key, aThis };
                call(callback, thisArg, args);
            }
            return Undefined;
        }

        value map_get(value aThis, arguments aArguments, value, function_object&)
        {
            auto* e = this_collection(aThis, object_class::Map, "get").data.find(argument(aArguments, 0u));
            return e != nullptr ? e->data : Undefined;
        }

        value map_has(value aThis, arguments aArguments, value, function_object&)
        {
            return boolean(this_collection(aThis, object_class::Map, "has").data.has(argument(aArguments, 0u)));
        }

        value map_set(value aThis, arguments aArguments, value, function_object&)
        {
            this_collection(aThis, object_class::Map, "set").data.set(argument(aArguments, 0u), argument(aArguments, 1u));
            return aThis;
        }

        value map_get_or_insert(value aThis, arguments aArguments, value, function_object&)
        {
            auto& map = this_collection(aThis, object_class::Map, "getOrInsert");
            auto const key = argument(aArguments, 0u);
            if (auto* e = map.data.find(key))
                return e->data;
            auto const v = argument(aArguments, 1u);
            map.data.set(key, v);
            return v;
        }

        value map_get_or_insert_computed(value aThis, arguments aArguments, value, function_object&)
        {
            auto& map = this_collection(aThis, object_class::Map, "getOrInsertComputed");
            auto const callback = argument(aArguments, 1u);
            if (!is_callable(callback))
                throw_type_error(describe(callback) + " is not a function");
            auto const key = canonicalize_key(argument(aArguments, 0u));
            if (auto* e = map.data.find(key))
                return e->data;
            value const args[] = { key };
            auto const v = call(callback, Undefined, args);
            map.data.set(key, v);
            return v;
        }

        value map_size(value aThis, arguments, value, function_object&)
        {
            return number(static_cast<double>(this_collection(aThis, object_class::Map, "size").data.size()));
        }

        // --- Set

        value set_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Constructor Set requires 'new'");
            auto const set = make_collection(get_prototype_from_constructor(aNewTarget, &realm::setPrototype), object_class::Set);
            auto const iterable = argument(aArguments, 0u);
            if (!is_nullish(iterable))
                add_from_iterable(set, iterable, the_names().add, false);
            return set;
        }

        value set_add(value aThis, arguments aArguments, value, function_object&)
        {
            auto& set = this_collection(aThis, object_class::Set, "add");
            auto const v = argument(aArguments, 0u);
            if (!set.data.has(v))
                set.data.set(v, Undefined);
            return aThis;
        }

        value set_clear(value aThis, arguments, value, function_object&)
        {
            this_collection(aThis, object_class::Set, "clear").data.clear();
            return Undefined;
        }

        value set_delete(value aThis, arguments aArguments, value, function_object&)
        {
            return boolean(this_collection(aThis, object_class::Set, "delete").data.remove(argument(aArguments, 0u)));
        }

        value set_entries(value aThis, arguments, value, function_object&)
        {
            this_collection(aThis, object_class::Set, "entries");
            return make_collection_iterator(aThis, 2);
        }

        value set_values(value aThis, arguments, value, function_object&)
        {
            this_collection(aThis, object_class::Set, "values");
            return make_collection_iterator(aThis, 1);
        }

        value set_for_each(value aThis, arguments aArguments, value, function_object&)
        {
            auto& set = this_collection(aThis, object_class::Set, "forEach");
            auto const callback = argument(aArguments, 0u);
            if (!is_callable(callback))
                throw_type_error(describe(callback) + " is not a function");
            auto const thisArg = argument(aArguments, 1u);
            cursor c{ set.data };
            while (auto const* e = c.next())
            {
                value const args[] = { e->key, e->key, aThis };
                call(callback, thisArg, args);
            }
            return Undefined;
        }

        value set_has(value aThis, arguments aArguments, value, function_object&)
        {
            return boolean(this_collection(aThis, object_class::Set, "has").data.has(argument(aArguments, 0u)));
        }

        value set_size(value aThis, arguments, value, function_object&)
        {
            return number(static_cast<double>(this_collection(aThis, object_class::Set, "size").data.size()));
        }

        // (a Set Record: GetSetRecord)
        struct set_record
        {
            value set;
            double size;
            value has;
            value keys;
        };

        set_record get_set_record(value aObject)
        {
            if (!is_object(aObject))
                throw_type_error(describe(aObject) + " is not an object");
            auto const rawSize = get(aObject, the_names().size);
            auto const numSize = to_number(rawSize);
            if (std::isnan(numSize))
                throw_type_error("The 'size' property must be a number");
            auto const intSize = to_integer_or_infinity(number(numSize));
            if (intSize < 0.0)
                throw_range_error("The 'size' property must not be negative");
            auto const has = get(aObject, the_names().has);
            if (!is_callable(has))
                throw_type_error("The 'has' property must be a function");
            auto const keys = get(aObject, the_names().keys);
            if (!is_callable(keys))
                throw_type_error("The 'keys' property must be a function");
            return set_record{ aObject, intSize, has, keys };
        }

        bool other_has(set_record const& aOther, value aValue)
        {
            value const args[] = { aValue };
            return to_boolean(call(aOther.has, aOther.set, args));
        }

        value make_result_set()
        {
            return make_collection(the_realm().setPrototype, object_class::Set);
        }

        keyed_data& data_of(value aCollection)
        {
            return static_cast<collection_object&>(obj(aCollection)).data;
        }

        value set_union(value aThis, arguments aArguments, value, function_object&)
        {
            auto& set = this_collection(aThis, object_class::Set, "union");
            auto const other = get_set_record(argument(aArguments, 0u));
            auto keys = get_iterator_from_method(other.set, other.keys);
            auto const result = make_result_set();
            auto& resultData = data_of(result);
            resultData.copy_from(set.data);
            while (auto const next = iterator_step_value(keys))
                if (!resultData.has(*next))
                    resultData.set(*next, Undefined);
            return result;
        }

        value set_intersection(value aThis, arguments aArguments, value, function_object&)
        {
            auto& set = this_collection(aThis, object_class::Set, "intersection");
            auto const other = get_set_record(argument(aArguments, 0u));
            auto const result = make_result_set();
            auto& resultData = data_of(result);
            if (static_cast<double>(set.data.size()) <= other.size)
            {
                cursor c{ set.data };
                while (auto const* e = c.next())
                {
                    auto const v = e->key;
                    if (other_has(other, v) && !resultData.has(v))
                        resultData.set(v, Undefined);
                }
            }
            else
            {
                auto keys = get_iterator_from_method(other.set, other.keys);
                while (auto const next = iterator_step_value(keys))
                    if (set.data.has(*next) && !resultData.has(*next))
                        resultData.set(*next, Undefined);
            }
            return result;
        }

        value set_difference(value aThis, arguments aArguments, value, function_object&)
        {
            auto& set = this_collection(aThis, object_class::Set, "difference");
            auto const other = get_set_record(argument(aArguments, 0u));
            auto const result = make_result_set();
            auto& resultData = data_of(result);
            resultData.copy_from(set.data);
            if (static_cast<double>(set.data.size()) <= other.size)
            {
                // (the result's elements as they are now: only this method changes the result)
                std::vector<value> elements;
                for (auto const& e : resultData.entries())
                    if (e.key != Empty)
                        elements.push_back(e.key);
                for (auto const v : elements)
                    if (other_has(other, v))
                        resultData.remove(v);
            }
            else
            {
                auto keys = get_iterator_from_method(other.set, other.keys);
                while (auto const next = iterator_step_value(keys))
                    resultData.remove(*next);
            }
            return result;
        }

        value set_symmetric_difference(value aThis, arguments aArguments, value, function_object&)
        {
            auto& set = this_collection(aThis, object_class::Set, "symmetricDifference");
            auto const other = get_set_record(argument(aArguments, 0u));
            auto keys = get_iterator_from_method(other.set, other.keys);
            auto const result = make_result_set();
            auto& resultData = data_of(result);
            resultData.copy_from(set.data);
            while (auto const next = iterator_step_value(keys))
            {
                bool const alreadyInResult = resultData.has(*next);
                if (set.data.has(*next))
                {
                    if (alreadyInResult)
                        resultData.remove(*next);
                }
                else if (!alreadyInResult)
                    resultData.set(*next, Undefined);
            }
            return result;
        }

        value set_is_subset_of(value aThis, arguments aArguments, value, function_object&)
        {
            auto& set = this_collection(aThis, object_class::Set, "isSubsetOf");
            auto const other = get_set_record(argument(aArguments, 0u));
            if (static_cast<double>(set.data.size()) > other.size)
                return False;
            cursor c{ set.data };
            while (auto const* e = c.next())
                if (!other_has(other, e->key))
                    return False;
            return True;
        }

        value set_is_superset_of(value aThis, arguments aArguments, value, function_object&)
        {
            auto& set = this_collection(aThis, object_class::Set, "isSupersetOf");
            auto const other = get_set_record(argument(aArguments, 0u));
            if (static_cast<double>(set.data.size()) < other.size)
                return False;
            auto keys = get_iterator_from_method(other.set, other.keys);
            while (auto const next = iterator_step_value(keys))
                if (!set.data.has(*next))
                {
                    iterator_close(keys);
                    return False;
                }
            return True;
        }

        value set_is_disjoint_from(value aThis, arguments aArguments, value, function_object&)
        {
            auto& set = this_collection(aThis, object_class::Set, "isDisjointFrom");
            auto const other = get_set_record(argument(aArguments, 0u));
            if (static_cast<double>(set.data.size()) <= other.size)
            {
                cursor c{ set.data };
                while (auto const* e = c.next())
                    if (other_has(other, e->key))
                        return False;
            }
            else
            {
                auto keys = get_iterator_from_method(other.set, other.keys);
                while (auto const next = iterator_step_value(keys))
                    if (set.data.has(*next))
                    {
                        iterator_close(keys);
                        return False;
                    }
            }
            return True;
        }

        // --- WeakMap and WeakSet

        value weak_map_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Constructor WeakMap requires 'new'");
            auto const map = value_of(the_heap().make<weak_collection>(get_prototype_from_constructor(aNewTarget, &realm::weakMapPrototype), object_class::WeakMap));
            auto const iterable = argument(aArguments, 0u);
            if (!is_nullish(iterable))
                add_from_iterable(map, iterable, the_names().set, true);
            return map;
        }

        value weak_map_delete(value aThis, arguments aArguments, value, function_object&)
        {
            auto& map = this_weak_collection(aThis, object_class::WeakMap, "delete");
            return boolean(map.data.erase(argument(aArguments, 0u)) != 0u);
        }

        value weak_map_get(value aThis, arguments aArguments, value, function_object&)
        {
            auto& map = this_weak_collection(aThis, object_class::WeakMap, "get");
            auto const existing = map.data.find(argument(aArguments, 0u));
            return existing != map.data.end() ? existing->second : Undefined;
        }

        value weak_map_has(value aThis, arguments aArguments, value, function_object&)
        {
            auto& map = this_weak_collection(aThis, object_class::WeakMap, "has");
            return boolean(map.data.contains(argument(aArguments, 0u)));
        }

        value weak_map_set(value aThis, arguments aArguments, value, function_object&)
        {
            auto& map = this_weak_collection(aThis, object_class::WeakMap, "set");
            auto const key = argument(aArguments, 0u);
            if (!can_be_held_weakly(key))
                throw_type_error("Invalid value used as weak map key: " + describe(key));
            map.data[key] = argument(aArguments, 1u);
            return aThis;
        }

        value weak_map_get_or_insert(value aThis, arguments aArguments, value, function_object&)
        {
            auto& map = this_weak_collection(aThis, object_class::WeakMap, "getOrInsert");
            auto const key = argument(aArguments, 0u);
            if (!can_be_held_weakly(key))
                throw_type_error("Invalid value used as weak map key: " + describe(key));
            return map.data.try_emplace(key, argument(aArguments, 1u)).first->second;
        }

        value weak_map_get_or_insert_computed(value aThis, arguments aArguments, value, function_object&)
        {
            auto& map = this_weak_collection(aThis, object_class::WeakMap, "getOrInsertComputed");
            auto const key = argument(aArguments, 0u);
            if (!can_be_held_weakly(key))
                throw_type_error("Invalid value used as weak map key: " + describe(key));
            auto const callback = argument(aArguments, 1u);
            if (!is_callable(callback))
                throw_type_error(describe(callback) + " is not a function");
            if (auto const existing = map.data.find(key); existing != map.data.end())
                return existing->second;
            value const args[] = { key };
            auto const v = call(callback, Undefined, args);
            map.data[key] = v;
            return v;
        }

        value weak_set_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Constructor WeakSet requires 'new'");
            auto const set = value_of(the_heap().make<weak_collection>(get_prototype_from_constructor(aNewTarget, &realm::weakSetPrototype), object_class::WeakSet));
            auto const iterable = argument(aArguments, 0u);
            if (!is_nullish(iterable))
                add_from_iterable(set, iterable, the_names().add, false);
            return set;
        }

        value weak_set_add(value aThis, arguments aArguments, value, function_object&)
        {
            auto& set = this_weak_collection(aThis, object_class::WeakSet, "add");
            auto const v = argument(aArguments, 0u);
            if (!can_be_held_weakly(v))
                throw_type_error("Invalid value used in weak set: " + describe(v));
            set.data.try_emplace(v, Undefined);
            return aThis;
        }

        value weak_set_delete(value aThis, arguments aArguments, value, function_object&)
        {
            auto& set = this_weak_collection(aThis, object_class::WeakSet, "delete");
            return boolean(set.data.erase(argument(aArguments, 0u)) != 0u);
        }

        value weak_set_has(value aThis, arguments aArguments, value, function_object&)
        {
            auto& set = this_weak_collection(aThis, object_class::WeakSet, "has");
            return boolean(set.data.contains(argument(aArguments, 0u)));
        }

        // --- WeakRef

        value weak_ref_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Constructor WeakRef requires 'new'");
            auto const target = argument(aArguments, 0u);
            if (!can_be_held_weakly(target))
                throw_type_error("WeakRef: invalid target " + describe(target));
            auto const result = ordinary_create_from_constructor(aNewTarget, &realm::weakRefPrototype, object_class::WeakRef);
            obj(result).internal = target;
            return result;
        }

        value weak_ref_deref(value aThis, arguments, value, function_object&)
        {
            if (!is_object(aThis) || obj(aThis).cls != object_class::WeakRef)
                throw_type_error("Method WeakRef.prototype.deref called on incompatible receiver " + describe(aThis));
            return obj(aThis).internal;
        }

        // --- FinalizationRegistry

        value finalization_registry_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Constructor FinalizationRegistry requires 'new'");
            auto const cleanup = argument(aArguments, 0u);
            if (!is_callable(cleanup))
                throw_type_error("FinalizationRegistry: cleanup must be callable");
            auto& registry = the_heap().make<finalization_registry>(get_prototype_from_constructor(aNewTarget, &realm::finalizationRegistryPrototype));
            registry.cleanup = cleanup;
            return value_of(registry);
        }

        finalization_registry& this_registry(value aThis, char const* aMethod)
        {
            if (!is_object(aThis) || obj(aThis).cls != object_class::FinalizationRegistry)
                throw_type_error(std::string{ "Method FinalizationRegistry.prototype." } + aMethod + " called on incompatible receiver " + describe(aThis));
            return static_cast<finalization_registry&>(obj(aThis));
        }

        value finalization_registry_register(value aThis, arguments aArguments, value, function_object&)
        {
            auto& registry = this_registry(aThis, "register");
            auto const target = argument(aArguments, 0u);
            auto const heldValue = argument(aArguments, 1u);
            auto unregisterToken = argument(aArguments, 2u);
            if (!can_be_held_weakly(target))
                throw_type_error("FinalizationRegistry.prototype.register: invalid target");
            if (same_value(target, heldValue))
                throw_type_error("FinalizationRegistry.prototype.register: target and holdings must not be same");
            if (!can_be_held_weakly(unregisterToken))
            {
                if (unregisterToken != Undefined)
                    throw_type_error("FinalizationRegistry.prototype.register: invalid unregister token " + describe(unregisterToken));
                unregisterToken = Empty;
            }
            registry.cells.push_back({ target, heldValue, unregisterToken });
            return Undefined;
        }

        value finalization_registry_unregister(value aThis, arguments aArguments, value, function_object&)
        {
            auto& registry = this_registry(aThis, "unregister");
            auto const unregisterToken = argument(aArguments, 0u);
            if (!can_be_held_weakly(unregisterToken))
                throw_type_error("Invalid unregisterToken ('" + describe(unregisterToken) + "')");
            auto const removed = std::erase_if(registry.cells, [&](auto const& c) { return c.unregisterToken != Empty && c.unregisterToken == unregisterToken; });
            return boolean(removed != 0u);
        }

        // --- initialization helpers

        value make_constructor(realm& aRealm, char const* aName, std::uint32_t aLength, native_function aFunction, value aPrototype, char const* aTag)
        {
            auto const c = make_native(aName, aLength, aFunction, true);
            obj(c).put(the_names().prototype, aPrototype, 0u);
            obj(aPrototype).put(the_names().constructor, c);
            obj(aPrototype).put(aRealm.symToStringTag, ascii(aTag), AttributeConfigurable);
            obj(aRealm.global).put(ascii(aName), c);
            return c;
        }
    }

    std::vector<value> collection_entries(value aCollection)
    {
        std::vector<value> result;
        auto& o = obj(aCollection);
        if (o.cls != object_class::Map && o.cls != object_class::Set)
            return result;
        for (auto const& e : static_cast<collection_object&>(o).data.entries())
            if (e.key != Empty)
            {
                result.push_back(e.key);
                if (o.cls == object_class::Map)
                    result.push_back(e.data);
            }
        return result;
    }

    void init_collections(realm& aRealm)
    {
        // Map
        aRealm.mapPrototype = make_object();
        auto const mp = aRealm.mapPrototype;
        auto const m = make_constructor(aRealm, "Map", 0u, map_constructor, mp, "Map");
        aRealm.map = m;
        method(m, "groupBy", 2u, map_group_by);
        getter(m, aRealm.symSpecies, species_getter);
        method(mp, "clear", 0u, map_clear);
        method(mp, "delete", 1u, map_delete);
        auto const mapEntries = method(mp, "entries", 0u, map_entries);
        method(mp, "forEach", 1u, map_for_each);
        method(mp, "get", 1u, map_get);
        method(mp, "getOrInsert", 2u, map_get_or_insert);
        method(mp, "getOrInsertComputed", 2u, map_get_or_insert_computed);
        method(mp, "has", 1u, map_has);
        method(mp, "keys", 0u, map_keys);
        method(mp, "set", 2u, map_set);
        getter(mp, the_names().size, map_size);
        method(mp, "values", 0u, map_values);
        obj(mp).put(aRealm.symIterator, mapEntries);
        aRealm.mapIteratorPrototype = make_object(aRealm.iteratorPrototype);
        method(aRealm.mapIteratorPrototype, "next", 0u, map_iterator_next);
        obj(aRealm.mapIteratorPrototype).put(aRealm.symToStringTag, ascii("Map Iterator"), AttributeConfigurable);
        // Set
        aRealm.setPrototype = make_object();
        auto const sp = aRealm.setPrototype;
        auto const s = make_constructor(aRealm, "Set", 0u, set_constructor, sp, "Set");
        aRealm.set = s;
        getter(s, aRealm.symSpecies, species_getter);
        method(sp, "add", 1u, set_add);
        method(sp, "clear", 0u, set_clear);
        method(sp, "delete", 1u, set_delete);
        method(sp, "difference", 1u, set_difference);
        method(sp, "entries", 0u, set_entries);
        method(sp, "forEach", 1u, set_for_each);
        method(sp, "has", 1u, set_has);
        method(sp, "intersection", 1u, set_intersection);
        method(sp, "isDisjointFrom", 1u, set_is_disjoint_from);
        method(sp, "isSubsetOf", 1u, set_is_subset_of);
        method(sp, "isSupersetOf", 1u, set_is_superset_of);
        getter(sp, the_names().size, set_size);
        method(sp, "symmetricDifference", 1u, set_symmetric_difference);
        method(sp, "union", 1u, set_union);
        auto const setValues = method(sp, "values", 0u, set_values);
        obj(sp).put(the_names().keys, setValues);
        obj(sp).put(aRealm.symIterator, setValues);
        aRealm.setIteratorPrototype = make_object(aRealm.iteratorPrototype);
        method(aRealm.setIteratorPrototype, "next", 0u, set_iterator_next);
        obj(aRealm.setIteratorPrototype).put(aRealm.symToStringTag, ascii("Set Iterator"), AttributeConfigurable);
        // WeakMap
        aRealm.weakMapPrototype = make_object();
        auto const wmp = aRealm.weakMapPrototype;
        make_constructor(aRealm, "WeakMap", 0u, weak_map_constructor, wmp, "WeakMap");
        method(wmp, "delete", 1u, weak_map_delete);
        method(wmp, "get", 1u, weak_map_get);
        method(wmp, "getOrInsert", 2u, weak_map_get_or_insert);
        method(wmp, "getOrInsertComputed", 2u, weak_map_get_or_insert_computed);
        method(wmp, "has", 1u, weak_map_has);
        method(wmp, "set", 2u, weak_map_set);
        // WeakSet
        aRealm.weakSetPrototype = make_object();
        auto const wsp = aRealm.weakSetPrototype;
        make_constructor(aRealm, "WeakSet", 0u, weak_set_constructor, wsp, "WeakSet");
        method(wsp, "add", 1u, weak_set_add);
        method(wsp, "delete", 1u, weak_set_delete);
        method(wsp, "has", 1u, weak_set_has);
        // WeakRef
        aRealm.weakRefPrototype = make_object();
        auto const wrp = aRealm.weakRefPrototype;
        make_constructor(aRealm, "WeakRef", 1u, weak_ref_constructor, wrp, "WeakRef");
        method(wrp, "deref", 0u, weak_ref_deref);
        // FinalizationRegistry
        aRealm.finalizationRegistryPrototype = make_object();
        auto const frp = aRealm.finalizationRegistryPrototype;
        make_constructor(aRealm, "FinalizationRegistry", 1u, finalization_registry_constructor, frp, "FinalizationRegistry");
        method(frp, "register", 2u, finalization_registry_register);
        method(frp, "unregister", 1u, finalization_registry_unregister);
    }
}
