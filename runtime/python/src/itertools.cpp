/*
  itertools.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/itertoolsmodule.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// The module itertools (CPython 3.13's itertoolsmodule.c: lazy iterators, their arguments and messages)

#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            // an iterator's state: its next item (null: exhausted (or an exception))
            struct iterator_state
            {
                virtual ~iterator_state() = default;
                virtual object* next() = 0;
                virtual object* repr(object*) { return nullptr; }
            };

            struct itertools_object
            {
                header h;
                std::unique_ptr<iterator_state> state;
            };

            struct kind_entry
            {
                char const* name;
                neos_py_type type;
            };

            neos_py_type make_itertools_type(char const* aQualifiedName, char const* aName)
            {
                auto type = make_type(aQualifiedName, aName, retain_object, release_object<itertools_object>);
                type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
                type.next = [](neos_py_context*, object* aSelf) -> object* { return reinterpret_cast<itertools_object*>(aSelf)->state->next(); };
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    if (auto const r = reinterpret_cast<itertools_object*>(aSelf)->state->repr(aSelf))
                        return r;
                    if (failed())
                        return nullptr;
                    return make_str(std::string{ "<" } + aSelf->type->name + " object at " + object_address(aSelf) + ">");
                };
                return type;
            }

            neos_py_type sCountType = make_itertools_type("itertools.count", "count");
            neos_py_type sCycleType = make_itertools_type("itertools.cycle", "cycle");
            neos_py_type sRepeatType = make_itertools_type("itertools.repeat", "repeat");
            neos_py_type sAccumulateType = make_itertools_type("itertools.accumulate", "accumulate");
            neos_py_type sBatchedType = make_itertools_type("itertools.batched", "batched");
            neos_py_type sChainType = make_itertools_type("itertools.chain", "chain");
            neos_py_type sCompressType = make_itertools_type("itertools.compress", "compress");
            neos_py_type sDropwhileType = make_itertools_type("itertools.dropwhile", "dropwhile");
            neos_py_type sTakewhileType = make_itertools_type("itertools.takewhile", "takewhile");
            neos_py_type sFilterfalseType = make_itertools_type("itertools.filterfalse", "filterfalse");
            neos_py_type sGroupbyType = make_itertools_type("itertools.groupby", "groupby");
            neos_py_type sGrouperType = make_itertools_type("itertools._grouper", "_grouper");
            neos_py_type sIsliceType = make_itertools_type("itertools.islice", "islice");
            neos_py_type sPairwiseType = make_itertools_type("itertools.pairwise", "pairwise");
            neos_py_type sStarmapType = make_itertools_type("itertools.starmap", "starmap");
            neos_py_type sTeeType = make_itertools_type("itertools._tee", "_tee");
            neos_py_type sZipLongestType = make_itertools_type("itertools.zip_longest", "zip_longest");
            neos_py_type sProductType = make_itertools_type("itertools.product", "product");
            neos_py_type sPermutationsType = make_itertools_type("itertools.permutations", "permutations");
            neos_py_type sCombinationsType = make_itertools_type("itertools.combinations", "combinations");
            neos_py_type sCombinationsWRType = make_itertools_type("itertools.combinations_with_replacement", "combinations_with_replacement");

            object* make_iterator_of(neos_py_type& aType, std::unique_ptr<iterator_state> aState)
            {
                auto const result = new itertools_object{ { { &aType.base } } };
                result->state = std::move(aState);
                return &result->h.base;
            }

            object* call_one(object* aFunction, object* aArgument)
            {
                retain(aArgument);
                ref const args{ make_tuple({ aArgument }) };
                return call(aFunction, args.get(), nullptr);
            }

            object* call_two(object* aFunction, object* aFirst, object* aSecond)
            {
                retain(aFirst);
                retain(aSecond);
                ref const args{ make_tuple({ aFirst, aSecond }) };
                return call(aFunction, args.get(), nullptr);
            }

            bool no_kwargs(char const* aName, object* aKwargs)
            {
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                {
                    error("TypeError", std::string{ aName } + "() takes no keyword arguments");
                    return false;
                }
                return true;
            }

            // --- count, cycle and repeat

            struct count_state : iterator_state
            {
                ref current;
                ref step;
                object* next() override
                {
                    auto const result = current.get();
                    retain(result);
                    ref advanced{ binary(NEOS_PY_ADD, current.get(), step.get(), false) };
                    if (!advanced)
                    {
                        release(result);
                        return nullptr;
                    }
                    current = advanced;
                    return result;
                }
                object* repr(object*) override
                {
                    std::string c, s;
                    if (!repr_of(current.get(), c))
                        return nullptr;
                    bool const unitStep = kind_of(step.get()) == NEOS_PY_INT && as<int_object>(step.get())->value == 1 && is_small_int(step.get());
                    if (unitStep)
                        return make_str("count(" + c + ")");
                    if (!repr_of(step.get(), s))
                        return nullptr;
                    return make_str("count(" + c + ", " + s + ")");
                }
            };

            struct cycle_state : iterator_state
            {
                ref iterator;
                std::vector<ref> saved;
                std::size_t index = 0u;
                bool exhausted = false;
                object* next() override
                {
                    if (!exhausted)
                    {
                        auto const item = detail::next(iterator.get());
                        if (item != nullptr)
                        {
                            saved.push_back(ref::borrowed(item));
                            return item;
                        }
                        if (failed())
                            return nullptr;
                        exhausted = true;
                    }
                    if (saved.empty())
                        return nullptr;
                    auto const item = saved[index++ % saved.size()].get();
                    return (retain(item), item);
                }
            };

            struct repeat_state : iterator_state
            {
                ref item;
                long long times = -1;
                object* next() override
                {
                    if (times == 0)
                        return nullptr;
                    if (times > 0)
                        --times;
                    return (retain(item.get()), item.get());
                }
                object* repr(object*) override
                {
                    std::string text;
                    if (!repr_of(item.get(), text))
                        return nullptr;
                    return make_str(times < 0 ? "repeat(" + text + ")" : "repeat(" + text + ", " + std::to_string(times) + ")");
                }
            };

            // --- accumulate, batched, chain, compress, dropwhile, takewhile and filterfalse

            struct accumulate_state : iterator_state
            {
                ref iterator;
                ref function;
                ref total;
                ref initial;
                object* next() override
                {
                    if (initial)
                    {
                        total = initial;
                        initial = ref{};
                        return (retain(total.get()), total.get());
                    }
                    ref const item{ detail::next(iterator.get()) };
                    if (!item)
                        return nullptr;
                    if (!total)
                        total = item;
                    else
                    {
                        ref sum{ function ? call_two(function.get(), total.get(), item.get()) : binary(NEOS_PY_ADD, total.get(), item.get(), false) };
                        if (!sum)
                            return nullptr;
                        total = sum;
                    }
                    return (retain(total.get()), total.get());
                }
            };

            struct batched_state : iterator_state
            {
                ref iterator;
                long long n = 1;
                bool strict = false;
                object* next() override
                {
                    std::vector<object*> items;
                    while (static_cast<long long>(items.size()) < n)
                    {
                        auto const item = detail::next(iterator.get());
                        if (item == nullptr)
                        {
                            if (failed())
                            {
                                for (auto i : items)
                                    release(i);
                                return nullptr;
                            }
                            break;
                        }
                        items.push_back(item);
                    }
                    if (items.empty())
                        return nullptr;
                    if (strict && static_cast<long long>(items.size()) != n)
                    {
                        for (auto i : items)
                            release(i);
                        return error("ValueError", "batched(): incomplete batch");
                    }
                    return make_tuple(std::move(items));
                }
            };

            struct chain_state : iterator_state
            {
                ref sources;            ///< (an iterator of iterables)
                ref current;
                object* next() override
                {
                    for (;;)
                    {
                        if (!current)
                        {
                            ref const source{ detail::next(sources.get()) };
                            if (!source)
                                return nullptr;
                            current = ref{ iter(source.get()) };
                            if (!current)
                                return nullptr;
                        }
                        auto const item = detail::next(current.get());
                        if (item != nullptr || failed())
                            return item;
                        current = ref{};
                    }
                }
            };

            struct compress_state : iterator_state
            {
                ref data;
                ref selectors;
                object* next() override
                {
                    for (;;)
                    {
                        ref item{ detail::next(data.get()) };
                        if (!item)
                            return nullptr;
                        ref const selector{ detail::next(selectors.get()) };
                        if (!selector)
                            return nullptr;
                        auto const t = truth(selector.get());
                        if (t < 0)
                            return nullptr;
                        if (t == 1)
                            return item.release_ownership();
                    }
                }
            };

            struct predicate_state : iterator_state
            {
                enum kind_t { Dropwhile, Takewhile, Filterfalse } kind;
                ref predicate;
                ref iterator;
                bool done = false;      ///< (dropwhile: dropping is over; takewhile: stopped)
                object* next() override
                {
                    if (kind == Takewhile && done)
                        return nullptr;
                    for (;;)
                    {
                        ref item{ detail::next(iterator.get()) };
                        if (!item)
                            return nullptr;
                        if (kind == Dropwhile && done)
                            return item.release_ownership();
                        int t = 0;
                        if (kind == Filterfalse && (!predicate || kind_of(predicate.get()) == NEOS_PY_NONE))
                            t = truth(item.get());
                        else
                        {
                            ref const r{ call_one(predicate.get(), item.get()) };
                            if (!r)
                                return nullptr;
                            t = truth(r.get());
                        }
                        if (t < 0)
                            return nullptr;
                        switch (kind)
                        {
                        case Dropwhile:
                            if (t == 0)
                            {
                                done = true;
                                return item.release_ownership();
                            }
                            break;
                        case Takewhile:
                            if (t == 1)
                                return item.release_ownership();
                            done = true;
                            return nullptr;
                        case Filterfalse:
                            if (t == 0)
                                return item.release_ownership();
                            break;
                        }
                    }
                }
            };

            // --- groupby

            struct groupby_state : iterator_state
            {
                ref iterator;
                ref keyFunction;
                ref currentKey;
                ref currentValue;
                ref targetKey;
                std::size_t generation = 0u;     ///< (changes when the groupby moves to the next group: its groupers are then exhausted)
                bool exhausted = false;

                // (the next item and its key: false if exhausted (or an exception))
                bool step()
                {
                    ref item{ detail::next(iterator.get()) };
                    if (!item)
                    {
                        exhausted = true;
                        return false;
                    }
                    ref key{ keyFunction && kind_of(keyFunction.get()) != NEOS_PY_NONE ? call_one(keyFunction.get(), item.get()) : (retain(item.get()), item.get()) };
                    if (!key)
                        return false;
                    currentValue = item;
                    currentKey = key;
                    return true;
                }
                object* next() override;
            };

            struct grouper_state : iterator_state
            {
                ref parent;     ///< (the groupby)
                ref key;
                std::size_t generation;
                groupby_state& owner() const { return *static_cast<groupby_state*>(reinterpret_cast<itertools_object*>(parent.get())->state.get()); }
                object* next() override
                {
                    auto& g = owner();
                    if (g.generation != generation)
                        return nullptr;
                    if (!g.currentValue)
                    {
                        if (g.exhausted || !g.step())
                            return nullptr;
                    }
                    auto const e = equal(key.get(), g.currentKey.get());
                    if (e < 0)
                        return nullptr;
                    if (e == 0)
                        return nullptr;
                    auto const result = g.currentValue.release_ownership();
                    g.currentValue = ref{};
                    return result;
                }
            };

            object* groupby_state::next()
            {
                ++generation;
                // (skip the rest of the current group)
                for (;;)
                {
                    if (!currentValue)
                    {
                        if (exhausted || !step())
                            return nullptr;
                    }
                    if (!targetKey)
                        break;
                    auto const e = equal(targetKey.get(), currentKey.get());
                    if (e < 0)
                        return nullptr;
                    if (e == 0)
                        break;
                    currentValue = ref{};
                }
                targetKey = currentKey;
                return nullptr;
            }

            // --- islice, pairwise, starmap, tee and zip_longest

            struct islice_state : iterator_state
            {
                ref iterator;
                long long next_index = 0;   ///< (of the next item to return)
                long long stop = -1;        ///< (-1: none)
                long long step = 1;
                long long position = 0;     ///< (of the next item of the iterator)
                object* next() override
                {
                    // (CPython's islice_next)
                    if (!iterator)
                        return nullptr;
                    while (position < next_index)
                    {
                        ref const skipped{ detail::next(iterator.get()) };
                        if (!skipped)
                        {
                            iterator = ref{};
                            return nullptr;
                        }
                        ++position;
                    }
                    if (stop >= 0 && position >= stop)
                    {
                        iterator = ref{};
                        return nullptr;
                    }
                    auto const item = detail::next(iterator.get());
                    if (item == nullptr)
                    {
                        iterator = ref{};
                        return nullptr;
                    }
                    ++position;
                    auto const old = next_index;
                    next_index += step;
                    if (next_index < old || (stop >= 0 && next_index > stop))
                        next_index = stop;
                    return item;
                }
            };

            struct pairwise_state : iterator_state
            {
                ref iterator;
                ref previous;
                bool started = false;
                object* next() override
                {
                    if (!iterator)
                        return nullptr;
                    if (!started)
                    {
                        started = true;
                        previous = ref{ detail::next(iterator.get()) };
                        if (!previous)
                        {
                            iterator = ref{};
                            return nullptr;
                        }
                    }
                    ref item{ detail::next(iterator.get()) };
                    if (!item)
                    {
                        iterator = ref{};
                        previous = ref{};
                        return nullptr;
                    }
                    auto const first = previous.release_ownership();
                    previous = item;
                    return make_tuple({ first, (retain(item.get()), item.get()) });
                }
            };

            struct starmap_state : iterator_state
            {
                ref function;
                ref iterator;
                object* next() override
                {
                    ref const item{ detail::next(iterator.get()) };
                    if (!item)
                        return nullptr;
                    ref const args{ fn_tuple_of(item.get()) };
                    if (!args)
                        return nullptr;
                    return call(function.get(), args.get(), nullptr);
                }
            };

            struct tee_buffer
            {
                ref iterator;
                std::deque<ref> items;
                std::size_t base = 0u;      ///< (the position of items[0])
                std::vector<std::size_t*> positions;
                bool exhausted = false;
            };

            struct tee_state : iterator_state
            {
                std::shared_ptr<tee_buffer> buffer;
                std::size_t position = 0u;
                tee_state(std::shared_ptr<tee_buffer> aBuffer, std::size_t aPosition) : buffer{ std::move(aBuffer) }, position{ aPosition }
                {
                    buffer->positions.push_back(&position);
                }
                ~tee_state() override
                {
                    std::erase(buffer->positions, &position);
                }
                object* next() override
                {
                    auto& b = *buffer;
                    if (position >= b.base + b.items.size())
                    {
                        if (b.exhausted)
                            return nullptr;
                        auto const item = detail::next(b.iterator.get());
                        if (item == nullptr)
                        {
                            if (!failed())
                                b.exhausted = true;
                            return nullptr;
                        }
                        b.items.push_back(ref{ item });
                    }
                    auto const result = b.items[position - b.base].get();
                    retain(result);
                    ++position;
                    // (the items that every tee has passed)
                    std::size_t minimum = position;
                    for (auto p : b.positions)
                        minimum = std::min(minimum, *p);
                    while (b.base < minimum && !b.items.empty())
                    {
                        b.items.pop_front();
                        ++b.base;
                    }
                    return result;
                }
            };

            struct zip_longest_state : iterator_state
            {
                std::vector<ref> iterators;     ///< (null: exhausted)
                ref fill;
                std::size_t active = 0u;
                object* next() override
                {
                    if (active == 0u)
                        return nullptr;
                    std::vector<object*> items;
                    for (auto& it : iterators)
                    {
                        object* item = nullptr;
                        if (it)
                        {
                            item = detail::next(it.get());
                            if (item == nullptr)
                            {
                                if (failed())
                                {
                                    for (auto i : items)
                                        release(i);
                                    return nullptr;
                                }
                                it = ref{};
                                if (--active == 0u)
                                {
                                    for (auto i : items)
                                        release(i);
                                    return nullptr;
                                }
                            }
                        }
                        if (item == nullptr)
                            item = (retain(fill.get()), fill.get());
                        items.push_back(item);
                    }
                    return make_tuple(std::move(items));
                }
            };

            // --- product, permutations and combinations (the pools are lists of the items)

            struct product_state : iterator_state
            {
                std::vector<std::vector<ref>> pools;
                std::vector<std::size_t> indices;
                bool first = true;
                bool done = false;
                object* next() override
                {
                    if (done)
                        return nullptr;
                    if (first)
                    {
                        first = false;
                        for (auto const& p : pools)
                            if (p.empty())
                            {
                                done = true;
                                return nullptr;
                            }
                        indices.assign(pools.size(), 0u);
                    }
                    else
                    {
                        std::size_t i = pools.size();
                        for (;;)
                        {
                            if (i == 0u)
                            {
                                done = true;
                                return nullptr;
                            }
                            --i;
                            if (++indices[i] < pools[i].size())
                                break;
                            indices[i] = 0u;
                        }
                    }
                    std::vector<object*> items;
                    for (std::size_t i = 0u; i < pools.size(); ++i)
                        items.push_back((retain(pools[i][indices[i]].get()), pools[i][indices[i]].get()));
                    return make_tuple(std::move(items));
                }
            };

            struct permutations_state : iterator_state
            {
                std::vector<ref> pool;
                std::size_t r = 0u;
                std::vector<std::size_t> indices;
                std::vector<std::size_t> cycles;
                bool first = true;
                bool done = false;
                object* result() const
                {
                    std::vector<object*> items;
                    for (std::size_t i = 0u; i < r; ++i)
                        items.push_back((retain(pool[indices[i]].get()), pool[indices[i]].get()));
                    return make_tuple(std::move(items));
                }
                object* next() override
                {
                    auto const n = pool.size();
                    if (done)
                        return nullptr;
                    if (first)
                    {
                        first = false;
                        if (r > n)
                        {
                            done = true;
                            return nullptr;
                        }
                        for (std::size_t i = 0u; i < n; ++i)
                            indices.push_back(i);
                        for (std::size_t i = 0u; i < r; ++i)
                            cycles.push_back(n - i);
                        return result();
                    }
                    if (n == 0u)
                    {
                        done = true;
                        return nullptr;
                    }
                    for (std::size_t i = r; i-- > 0u;)
                    {
                        if (--cycles[i] == 0u)
                        {
                            std::rotate(indices.begin() + static_cast<std::ptrdiff_t>(i), indices.begin() + static_cast<std::ptrdiff_t>(i) + 1, indices.end());
                            cycles[i] = n - i;
                        }
                        else
                        {
                            std::swap(indices[i], indices[n - cycles[i]]);
                            return result();
                        }
                    }
                    done = true;
                    return nullptr;
                }
            };

            struct combinations_state : iterator_state
            {
                std::vector<ref> pool;
                std::size_t r = 0u;
                bool replacement = false;
                std::vector<std::size_t> indices;
                bool first = true;
                bool done = false;
                object* result() const
                {
                    std::vector<object*> items;
                    for (auto i : indices)
                        items.push_back((retain(pool[i].get()), pool[i].get()));
                    return make_tuple(std::move(items));
                }
                object* next() override
                {
                    auto const n = pool.size();
                    if (done)
                        return nullptr;
                    if (first)
                    {
                        first = false;
                        if ((!replacement && r > n) || (replacement && n == 0u && r > 0u))
                        {
                            done = true;
                            return nullptr;
                        }
                        for (std::size_t i = 0u; i < r; ++i)
                            indices.push_back(replacement ? 0u : i);
                        return result();
                    }
                    std::size_t i = r;
                    for (;;)
                    {
                        if (i == 0u)
                        {
                            done = true;
                            return nullptr;
                        }
                        --i;
                        if (replacement ? indices[i] != n - 1u : indices[i] != i + n - r)
                            break;
                    }
                    if (replacement)
                    {
                        auto const value = indices[i] + 1u;
                        for (auto j = i; j < r; ++j)
                            indices[j] = value;
                    }
                    else
                    {
                        ++indices[i];
                        for (auto j = i + 1u; j < r; ++j)
                            indices[j] = indices[j - 1u] + 1u;
                    }
                    return result();
                }
            };

            bool pool_of(object* aIterable, std::vector<ref>& aPool)
            {
                std::vector<object*> items;
                if (!collect(aIterable, items))
                    return false;
                for (auto i : items)
                    aPool.emplace_back(i);
                return true;
            }

            bool non_negative(object* aValue, char const* aMessage, long long& aResult)
            {
                if (!to_index(aValue, aResult))
                    return false;
                if (aResult < 0)
                {
                    error("ValueError", aMessage);
                    return false;
                }
                return true;
            }

            // --- the module's functions (the classes' constructors)

            object* iterator_of(object* aIterable)
            {
                return iter(aIterable);
            }

            builtin_entry const sItertoolsFunctions[] =
            {
                { "count", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("count", aArgs, aKwargs, { "start", "step" }, 0u, 0u, a))
                            return nullptr;
                        for (auto v : { a[0], a[1] })
                            if (v != nullptr && kind_of(v) != NEOS_PY_INT && kind_of(v) != NEOS_PY_BOOL && kind_of(v) != NEOS_PY_FLOAT && kind_of(v) != NEOS_PY_COMPLEX &&
                                !is_instance(v) && kind_of(v) != NEOS_PY_OTHER)
                                return error("TypeError", "a number is required");
                        auto state = std::make_unique<count_state>();
                        state->current = a[0] != nullptr ? ref::borrowed(a[0]) : ref{ make_int(0) };
                        state->step = a[1] != nullptr ? ref::borrowed(a[1]) : ref{ make_int(1) };
                        return make_iterator_of(sCountType, std::move(state));
                    } },
                { "cycle", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("cycle", aArgs, aKwargs, { "iterable" }, 1u, 1u, a))
                            return nullptr;
                        auto state = std::make_unique<cycle_state>();
                        state->iterator = ref{ iterator_of(a[0]) };
                        if (!state->iterator)
                            return nullptr;
                        return make_iterator_of(sCycleType, std::move(state));
                    } },
                { "repeat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("repeat", aArgs, aKwargs, { "object", "times" }, 1u, 0u, a))
                            return nullptr;
                        auto state = std::make_unique<repeat_state>();
                        state->item = ref::borrowed(a[0]);
                        if (a[1] != nullptr)
                        {
                            if (!to_index(a[1], state->times))
                                return nullptr;
                            state->times = std::max(0ll, state->times);
                        }
                        return make_iterator_of(sRepeatType, std::move(state));
                    } },
                { "accumulate", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("accumulate", aArgs, aKwargs, { "iterable", "func", "initial" }, 1u, 0u, a))
                            return nullptr;
                        auto state = std::make_unique<accumulate_state>();
                        state->iterator = ref{ iterator_of(a[0]) };
                        if (!state->iterator)
                            return nullptr;
                        if (a[1] != nullptr && kind_of(a[1]) != NEOS_PY_NONE)
                            state->function = ref::borrowed(a[1]);
                        if (a[2] != nullptr && kind_of(a[2]) != NEOS_PY_NONE)
                            state->initial = ref::borrowed(a[2]);
                        return make_iterator_of(sAccumulateType, std::move(state));
                    } },
                { "batched", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("batched", aArgs, aKwargs, { "iterable", "n", "strict" }, 2u, 0u, a))
                            return nullptr;
                        auto state = std::make_unique<batched_state>();
                        if (!to_index(a[1], state->n))
                            return nullptr;
                        if (state->n < 1)
                            return error("ValueError", "n must be at least one");
                        state->strict = a[2] != nullptr && truth(a[2]) == 1;
                        state->iterator = ref{ iterator_of(a[0]) };
                        if (!state->iterator)
                            return nullptr;
                        return make_iterator_of(sBatchedType, std::move(state));
                    } },
                { "chain", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_kwargs("chain", aKwargs))
                            return nullptr;
                        auto items = aArgs;
                        for (auto i : items)
                            retain(i);
                        ref const tuple{ make_tuple(std::move(items)) };
                        auto state = std::make_unique<chain_state>();
                        state->sources = ref{ iter(tuple.get()) };
                        return make_iterator_of(sChainType, std::move(state));
                    } },
                { "compress", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("compress", aArgs, aKwargs, { "data", "selectors" }, 2u, 0u, a))
                            return nullptr;
                        auto state = std::make_unique<compress_state>();
                        state->data = ref{ iterator_of(a[0]) };
                        if (!state->data)
                            return nullptr;
                        state->selectors = ref{ iterator_of(a[1]) };
                        if (!state->selectors)
                            return nullptr;
                        return make_iterator_of(sCompressType, std::move(state));
                    } },
                { "dropwhile", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_kwargs("dropwhile", aKwargs))
                            return nullptr;
                        if (aArgs.size() != 2u)
                            return error("TypeError", "dropwhile expected 2 arguments, got " + std::to_string(aArgs.size()));
                        auto state = std::make_unique<predicate_state>();
                        state->kind = predicate_state::Dropwhile;
                        state->predicate = ref::borrowed(aArgs[0]);
                        state->iterator = ref{ iterator_of(aArgs[1]) };
                        if (!state->iterator)
                            return nullptr;
                        return make_iterator_of(sDropwhileType, std::move(state));
                    } },
                { "takewhile", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_kwargs("takewhile", aKwargs))
                            return nullptr;
                        if (aArgs.size() != 2u)
                            return error("TypeError", "takewhile expected 2 arguments, got " + std::to_string(aArgs.size()));
                        auto state = std::make_unique<predicate_state>();
                        state->kind = predicate_state::Takewhile;
                        state->predicate = ref::borrowed(aArgs[0]);
                        state->iterator = ref{ iterator_of(aArgs[1]) };
                        if (!state->iterator)
                            return nullptr;
                        return make_iterator_of(sTakewhileType, std::move(state));
                    } },
                { "filterfalse", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_kwargs("filterfalse", aKwargs))
                            return nullptr;
                        if (aArgs.size() != 2u)
                            return error("TypeError", "filterfalse expected 2 arguments, got " + std::to_string(aArgs.size()));
                        auto state = std::make_unique<predicate_state>();
                        state->kind = predicate_state::Filterfalse;
                        state->predicate = ref::borrowed(aArgs[0]);
                        state->iterator = ref{ iterator_of(aArgs[1]) };
                        if (!state->iterator)
                            return nullptr;
                        return make_iterator_of(sFilterfalseType, std::move(state));
                    } },
                { "groupby", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("groupby", aArgs, aKwargs, { "iterable", "key" }, 1u, 0u, a))
                            return nullptr;
                        auto state = std::make_unique<groupby_state>();
                        state->iterator = ref{ iterator_of(a[0]) };
                        if (!state->iterator)
                            return nullptr;
                        if (a[1] != nullptr)
                            state->keyFunction = ref::borrowed(a[1]);
                        return make_iterator_of(sGroupbyType, std::move(state));
                    } },
                { "islice", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_kwargs("islice", aKwargs))
                            return nullptr;
                        if (aArgs.size() < 2u || aArgs.size() > 4u)
                            return error("TypeError", "islice expected at most 4 arguments, got " + std::to_string(aArgs.size()));
                        auto state = std::make_unique<islice_state>();
                        auto const value = [](object* aValue, long long aDefault, long long& aResult) -> bool
                        {
                            if (kind_of(aValue) == NEOS_PY_NONE)
                            {
                                aResult = aDefault;
                                return true;
                            }
                            if (!to_index(aValue, aResult))
                            {
                                clear_error();
                                return false;
                            }
                            return aResult >= 0;
                        };
                        long long start = 0, stop = -1, step = 1;
                        if (aArgs.size() == 2u)
                        {
                            if (!value(aArgs[1], -1, stop))
                                return error("ValueError", "Stop argument for islice() must be None or an integer: 0 <= x <= sys.maxsize.");
                        }
                        else
                        {
                            if (!value(aArgs[1], 0, start) || !value(aArgs[2], -1, stop))
                                return error("ValueError", "Indices for islice() must be None or an integer: 0 <= x <= sys.maxsize.");
                            if (aArgs.size() == 4u)
                            {
                                if (!value(aArgs[3], 1, step) || step == 0)
                                    return error("ValueError", "Step for islice() must be a positive integer or None.");
                            }
                        }
                        state->iterator = ref{ iterator_of(aArgs[0]) };
                        if (!state->iterator)
                            return nullptr;
                        state->next_index = start;
                        state->stop = stop;
                        state->step = step;
                        return make_iterator_of(sIsliceType, std::move(state));
                    } },
                { "pairwise", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("pairwise", aArgs, aKwargs, { "iterable" }, 1u, 1u, a))
                            return nullptr;
                        auto state = std::make_unique<pairwise_state>();
                        state->iterator = ref{ iterator_of(a[0]) };
                        if (!state->iterator)
                            return nullptr;
                        return make_iterator_of(sPairwiseType, std::move(state));
                    } },
                { "starmap", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_kwargs("starmap", aKwargs))
                            return nullptr;
                        if (aArgs.size() != 2u)
                            return error("TypeError", "starmap expected 2 arguments, got " + std::to_string(aArgs.size()));
                        auto state = std::make_unique<starmap_state>();
                        state->function = ref::borrowed(aArgs[0]);
                        state->iterator = ref{ iterator_of(aArgs[1]) };
                        if (!state->iterator)
                            return nullptr;
                        return make_iterator_of(sStarmapType, std::move(state));
                    } },
                { "tee", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("tee", aArgs, aKwargs, { "iterable", "n" }, 1u, 2u, a))
                            return nullptr;
                        long long n = 2;
                        if (a[1] != nullptr && !to_index(a[1], n))
                            return nullptr;
                        if (n < 0)
                            return error("ValueError", "n must be >= 0");
                        auto buffer = std::make_shared<tee_buffer>();
                        buffer->iterator = ref{ iterator_of(a[0]) };
                        if (!buffer->iterator)
                            return nullptr;
                        std::vector<object*> tees;
                        for (long long i = 0; i < n; ++i)
                            tees.push_back(make_iterator_of(sTeeType, std::make_unique<tee_state>(buffer, 0u)));
                        return make_tuple(std::move(tees));
                    } },
                { "zip_longest", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto state = std::make_unique<zip_longest_state>();
                        state->fill = ref::borrowed(none());
                        if (aKwargs != nullptr)
                            for (auto const& item : as<dict_object>(aKwargs)->items)
                            {
                                if (as<str_object>(item.first)->value != "fillvalue")
                                    return error("TypeError", "zip_longest() got an unexpected keyword argument '" + as<str_object>(item.first)->value + "'");
                                state->fill = ref::borrowed(item.second);
                            }
                        for (std::size_t i = 0u; i < aArgs.size(); ++i)
                        {
                            ref it{ iter(aArgs[i]) };
                            if (!it)
                            {
                                clear_error();
                                return error("TypeError", "zip_longest argument #" + std::to_string(i + 1u) + " must support iteration");
                            }
                            state->iterators.push_back(it);
                        }
                        state->active = state->iterators.size();
                        return make_iterator_of(sZipLongestType, std::move(state));
                    } },
                { "product", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        long long repeat = 1;
                        if (aKwargs != nullptr)
                            for (auto const& item : as<dict_object>(aKwargs)->items)
                            {
                                if (as<str_object>(item.first)->value != "repeat")
                                    return error("TypeError", "product() got an unexpected keyword argument '" + as<str_object>(item.first)->value + "'");
                                if (!to_index(item.second, repeat))
                                    return nullptr;
                                if (repeat < 0)
                                    return error("ValueError", "repeat argument cannot be negative");
                            }
                        auto state = std::make_unique<product_state>();
                        std::vector<std::vector<ref>> pools;
                        for (auto a : aArgs)
                        {
                            std::vector<ref> pool;
                            if (!pool_of(a, pool))
                                return nullptr;
                            pools.push_back(std::move(pool));
                        }
                        for (long long r = 0; r < repeat; ++r)
                            state->pools.insert(state->pools.end(), pools.begin(), pools.end());
                        return make_iterator_of(sProductType, std::move(state));
                    } },
                { "permutations", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("permutations", aArgs, aKwargs, { "iterable", "r" }, 1u, 0u, a))
                            return nullptr;
                        auto state = std::make_unique<permutations_state>();
                        if (!pool_of(a[0], state->pool))
                            return nullptr;
                        long long r = static_cast<long long>(state->pool.size());
                        if (a[1] != nullptr && kind_of(a[1]) != NEOS_PY_NONE)
                        {
                            if (!is_int_like(a[1]))
                                return error("TypeError", "Expected int as r");
                            if (!non_negative(a[1], "r must be non-negative", r))
                                return nullptr;
                        }
                        state->r = static_cast<std::size_t>(r);
                        return make_iterator_of(sPermutationsType, std::move(state));
                    } },
                { "combinations", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("combinations", aArgs, aKwargs, { "iterable", "r" }, 2u, 0u, a))
                            return nullptr;
                        auto state = std::make_unique<combinations_state>();
                        if (!pool_of(a[0], state->pool))
                            return nullptr;
                        long long r = 0;
                        if (!non_negative(a[1], "r must be non-negative", r))
                            return nullptr;
                        state->r = static_cast<std::size_t>(r);
                        return make_iterator_of(sCombinationsType, std::move(state));
                    } },
                { "combinations_with_replacement", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("combinations_with_replacement", aArgs, aKwargs, { "iterable", "r" }, 2u, 0u, a))
                            return nullptr;
                        auto state = std::make_unique<combinations_state>();
                        state->replacement = true;
                        if (!pool_of(a[0], state->pool))
                            return nullptr;
                        long long r = 0;
                        if (!non_negative(a[1], "r must be non-negative", r))
                            return nullptr;
                        state->r = static_cast<std::size_t>(r);
                        return make_iterator_of(sCombinationsWRType, std::move(state));
                    } }
            };

            // (groupby's items: (key, grouper))
            neos_py_type make_groupby_type()
            {
                auto type = make_itertools_type("itertools.groupby", "groupby");
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& g = *static_cast<groupby_state*>(reinterpret_cast<itertools_object*>(aSelf)->state.get());
                    g.next();
                    if (failed() || !g.currentValue)
                        return nullptr;
                    auto grouper = std::make_unique<grouper_state>();
                    grouper->parent = ref::borrowed(aSelf);
                    grouper->key = g.currentKey;
                    grouper->generation = g.generation;
                    return make_tuple({ (retain(g.currentKey.get()), g.currentKey.get()), make_iterator_of(sGrouperType, std::move(grouper)) });
                };
                return type;
            }

            // (chain.from_iterable)
            object* chain_from_iterable(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                if (!no_kwargs("from_iterable", aKwargs))
                    return nullptr;
                if (args.size() != 1u)
                    return error("TypeError", "chain.from_iterable() takes exactly one argument (" + std::to_string(args.size()) + " given)");
                auto state = std::make_unique<chain_state>();
                state->sources = ref{ iter(args[0]) };
                if (!state->sources)
                    return nullptr;
                return make_iterator_of(sChainType, std::move(state));
            }

            int instance_of(neos_py_context*, void* aData, object* aObject)
            {
                auto const& entry = *static_cast<builtin_entry const*>(aData);
                return std::string{ "itertools." } + entry.name == aObject->type->name ? 1 : 0;
            }

            object* construct(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& entry = *static_cast<builtin_entry const*>(aData);
                return entry.function(as<tuple_object>(aArgs)->items, aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr);
            }

            bool itertools_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                sGroupbyType = make_groupby_type();
                struct type_entry { char const* name; neos_py_type* type; };
                static type_entry const sTypes[] =
                {
                    { "count", &sCountType }, { "cycle", &sCycleType }, { "repeat", &sRepeatType }, { "accumulate", &sAccumulateType },
                    { "batched", &sBatchedType }, { "chain", &sChainType }, { "compress", &sCompressType }, { "dropwhile", &sDropwhileType },
                    { "takewhile", &sTakewhileType }, { "filterfalse", &sFilterfalseType }, { "groupby", &sGroupbyType }, { "islice", &sIsliceType },
                    { "pairwise", &sPairwiseType }, { "starmap", &sStarmapType }, { "zip_longest", &sZipLongestType }, { "product", &sProductType },
                    { "permutations", &sPermutationsType }, { "combinations", &sCombinationsType },
                    { "combinations_with_replacement", &sCombinationsWRType }
                };
                for (auto const& f : sItertoolsFunctions)
                {
                    neos_py_type* type = nullptr;
                    for (auto const& t : sTypes)
                        if (std::strcmp(t.name, f.name) == 0)
                            type = t.type;
                    if (type == nullptr)
                    {
                        aAttributes.emplace_back(f.name, make_builtin(f));
                        continue;
                    }
                    // (a class: callable; isinstance's target)
                    auto const typeObject = context()->type_new(context(), f.name, (std::string{ "itertools." } + f.name).c_str(), construct,
                        const_cast<builtin_entry*>(&f), instance_of);
                    // (instancecheck's data is the type: the call's is the entry)
                    aAttributes.emplace_back(f.name, typeObject);
                }
                return true;
            }

            inline bool const sItertools = register_module("itertools", itertools_module);
        }

        // (chain.from_iterable: the type object's class function)
        object* itertools_chain_from_iterable()
        {
            return make_function("from_iterable", chain_from_iterable, nullptr);
        }
    }
}
