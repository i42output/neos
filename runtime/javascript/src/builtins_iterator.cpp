/*
  builtins_iterator.cpp

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

// Iterator: the Iterator constructor, Iterator.from, Iterator.concat, Iterator.zip and Iterator.zipKeyed, and the iterator
// helpers of %Iterator.prototype% (map, filter, take, drop, flatMap, chunks, windows, reduce, toArray, forEach, some, every,
// find, includes, join, [Symbol.dispose]). The helpers that return iterators return Iterator Helper objects: the abstract closures of the
// specification (generator-like) are state machines here (each helper's state is kept in its object between calls of next).

#include <algorithm>
#include <deque>
#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        // (the kinds of Iterator Helper objects (the closure each runs))
        enum class helper_kind : std::uint8_t
        {
            Map,
            Filter,
            Take,
            Drop,
            FlatMap,
            Concat,
            Zip,
            Chunks,
            Windows
        };

        // (a generator's states, as an Iterator Helper has them)
        enum class helper_state : std::uint8_t
        {
            SuspendedStart,
            SuspendedYield,
            Executing,
            Completed
        };

        enum class zip_mode : std::uint8_t
        {
            Shortest,
            Longest,
            Strict
        };

        class iterator_helper : public object
        {
        public:
            iterator_helper(value aPrototype, helper_kind aKind) : object{ aPrototype, object_class::IteratorHelper }, kind{ aKind } {}
        public:
            void trace(tracer& aTracer) override
            {
                object::trace(aTracer);
                aTracer.mark(iterated);
                aTracer.mark(function);
                aTracer.mark(inner);
                for (auto const& [method, iterable] : iterables)
                {
                    aTracer.mark(method);
                    aTracer.mark(iterable);
                }
                for (auto const& i : iterators)
                    aTracer.mark(i);
                aTracer.mark(padding);
                aTracer.mark(keys);
                for (auto const v : buffer)
                    aTracer.mark(v);
            }
        public:
            helper_kind kind;
            helper_state state = helper_state::SuspendedStart;
            iterator_record iterated;               ///< (map ... flatMap: the underlying iterator)
            value function = Undefined;             ///< (the mapper or predicate)
            double counter = 0.0;
            double remaining = 0.0;                 ///< (take and drop: the limit)
            // flatMap and concat: the inner iterator
            iterator_record inner;
            bool innerAlive = false;
            // concat: the iterables' methods and iterables
            std::vector<std::pair<value, value>> iterables;
            std::size_t nextIterable = 0u;
            // zip: the iterators, which are open (not exhausted), their padding, the mode and zipKeyed's keys
            std::vector<iterator_record> iterators;
            std::vector<bool> exhausted;            ///< (an iterator that is done (longest): its padding is used)
            std::vector<std::size_t> openIterators; ///< (indices of iterators)
            std::vector<value> padding;
            zip_mode mode = zip_mode::Shortest;
            bool keyed = false;
            std::vector<value> keys;
            // chunks and windows: the size, the buffer, whether a partial window is allowed and whether the closure has
            // yielded its last (partial) result (after which it returns, whatever the completion of that yield)
            double size = 0.0;
            std::deque<value> buffer;
            bool allowPartial = false;
            bool finished = false;
        };

        class wrap_for_valid_iterator : public object
        {
        public:
            wrap_for_valid_iterator(value aPrototype) : object{ aPrototype, object_class::WrapForValidIterator } {}
        public:
            void trace(tracer& aTracer) override
            {
                object::trace(aTracer);
                aTracer.mark(iterated);
            }
        public:
            iterator_record iterated;
        };

        // --- iterator operations

        // (IteratorStep: false if done)
        bool iterator_step(iterator_record& aRecord)
        {
            value result;
            try
            {
                result = iterator_next(aRecord);
            }
            catch (...)
            {
                aRecord.done = true;
                throw;
            }
            bool done;
            try
            {
                done = iterator_complete(result);
            }
            catch (...)
            {
                aRecord.done = true;
                throw;
            }
            if (done)
                aRecord.done = true;
            return !done;
        }

        // (IteratorCloseAll (in reverse order) with a normal completion or, if aThrown, a throw completion)
        void iterator_close_all(std::span<iterator_record const> aIterators, std::optional<value> aThrown = {})
        {
            for (auto i = aIterators.rbegin(); i != aIterators.rend(); ++i)
            {
                if (aThrown)
                    iterator_close_on_throw(*i);
                else
                    try
                    {
                        iterator_close(*i);
                    }
                    catch (js_exception const& e)
                    {
                        aThrown = e.thrown;
                    }
            }
            if (aThrown)
                throw_value(*aThrown);
        }

        // (IteratorClose with a throw completion: the iterator is closed (its errors ignored) and the exception rethrown)
        template <typename F>
        auto close_if_abrupt(iterator_record const& aRecord, F&& aFunction) -> decltype(aFunction())
        {
            try
            {
                return aFunction();
            }
            catch (js_exception const&)
            {
                iterator_close_on_throw(aRecord);
                throw;
            }
        }

        [[noreturn]] void close_and_throw(iterator_record const& aRecord, value aErrorPrototype, std::string const& aMessage)
        {
            auto const error = make_error(aErrorPrototype, make_string_utf8(aMessage));
            iterator_close_on_throw(aRecord);
            throw_value(error);
        }

        // (GetIteratorFlattenable)
        iterator_record get_iterator_flattenable(value aObject, bool aIterateStrings)
        {
            if (!is_object(aObject) && !(aIterateStrings && is_string(aObject)))
                throw_type_error(describe(aObject) + " is not an object");
            auto const method = get_method(aObject, the_realm().symIterator);
            value iterator = aObject;
            if (method != Undefined)
                iterator = call(method, aObject);
            if (!is_object(iterator))
                throw_type_error(describe(iterator) + " is not an object");
            return get_iterator_direct(iterator);
        }

        value counter_value(double& aCounter)
        {
            auto const result = number(aCounter);
            if (aCounter < 9007199254740992.0)
                aCounter += 1.0;
            return result;
        }

        // --- Iterator Helper objects

        iterator_helper& make_helper(helper_kind aKind)
        {
            return the_heap().make<iterator_helper>(the_realm().iteratorHelperPrototype, aKind);
        }

        // ([[UnderlyingIterators]])
        rooted_vector<iterator_record> underlying_iterators(iterator_helper const& aHelper)
        {
            switch (aHelper.kind)
            {
            case helper_kind::Concat:
                return {};
            case helper_kind::Zip:
                {
                    rooted_vector<iterator_record> result;
                    for (auto const i : aHelper.openIterators)
                        result.push_back(aHelper.iterators[i]);
                    return result;
                }
            default:
                return { aHelper.iterated };
            }
        }

        void remove_open(iterator_helper& aHelper, std::size_t aIndex)
        {
            std::erase(aHelper.openIterators, aIndex);
        }

        // (a zip's next result (none: it is done))
        std::optional<value> zip_resume(iterator_helper& h)
        {
            auto const count = h.iterators.size();
            if (count == 0u)
                return {};
            value_list results;
            results.reserve(count);
            for (std::size_t i = 0u; i < count; ++i)
            {
                if (h.exhausted[i])
                {
                    results.push_back(h.padding[i]);
                    continue;
                }
                std::optional<value> result;
                try
                {
                    result = iterator_step_value(h.iterators[i]);
                }
                catch (js_exception const& e)
                {
                    remove_open(h, i);
                    iterator_close_all(underlying_iterators(h), e.thrown);
                }
                if (!result)
                {
                    remove_open(h, i);
                    switch (h.mode)
                    {
                    case zip_mode::Shortest:
                        iterator_close_all(underlying_iterators(h));
                        return {};
                    case zip_mode::Strict:
                        if (i != 0u)
                            iterator_close_all(underlying_iterators(h), make_error(the_realm().typeErrorPrototype,
                                ascii("Iterator.zip: iterators of different lengths (strict)")));
                        for (std::size_t k = 1u; k < count; ++k)
                        {
                            bool open;
                            try
                            {
                                open = iterator_step(h.iterators[k]);
                            }
                            catch (js_exception const& e)
                            {
                                remove_open(h, k);
                                iterator_close_all(underlying_iterators(h), e.thrown);
                            }
                            if (!open)
                                remove_open(h, k);
                            else
                                iterator_close_all(underlying_iterators(h), make_error(the_realm().typeErrorPrototype,
                                    ascii("Iterator.zip: iterators of different lengths (strict)")));
                        }
                        return {};
                    case zip_mode::Longest:
                        if (h.openIterators.empty())
                            return {};
                        h.exhausted[i] = true;
                        result = h.padding[i];
                        break;
                    }
                }
                results.push_back(*result);
            }
            if (!h.keyed)
                return create_array_from_list(results);
            auto const o = make_object(Null);
            for (std::size_t i = 0u; i < count; ++i)
                create_data_property_or_throw(o, h.keys[i], results[i]);
            return o;
        }

        // (resumes a helper's closure: its next yielded value (none: it returned))
        std::optional<value> helper_resume(iterator_helper& h)
        {
            switch (h.kind)
            {
            case helper_kind::Map:
                {
                    auto const v = iterator_step_value(h.iterated);
                    if (!v)
                        return {};
                    value const arguments_[] = { *v, counter_value(h.counter) };
                    return close_if_abrupt(h.iterated, [&]() { return call(h.function, Undefined, arguments_); });
                }
            case helper_kind::Filter:
                for (;;)
                {
                    auto const v = iterator_step_value(h.iterated);
                    if (!v)
                        return {};
                    value const arguments_[] = { *v, counter_value(h.counter) };
                    auto const selected = close_if_abrupt(h.iterated, [&]() { return call(h.function, Undefined, arguments_); });
                    if (to_boolean(selected))
                        return v;
                }
            case helper_kind::Take:
                {
                    if (h.remaining == 0.0)
                    {
                        iterator_close(h.iterated);
                        return {};
                    }
                    if (h.remaining != std::numeric_limits<double>::infinity())
                        h.remaining -= 1.0;
                    return iterator_step_value(h.iterated);
                }
            case helper_kind::Drop:
                {
                    while (h.remaining > 0.0)
                    {
                        if (h.remaining != std::numeric_limits<double>::infinity())
                            h.remaining -= 1.0;
                        if (!iterator_step(h.iterated))
                            return {};
                    }
                    return iterator_step_value(h.iterated);
                }
            case helper_kind::FlatMap:
                for (;;)
                {
                    if (!h.innerAlive)
                    {
                        auto const v = iterator_step_value(h.iterated);
                        if (!v)
                            return {};
                        value const arguments_[] = { *v, counter_value(h.counter) };
                        auto const mapped = close_if_abrupt(h.iterated, [&]() { return call(h.function, Undefined, arguments_); });
                        h.inner = close_if_abrupt(h.iterated, [&]() { return get_iterator_flattenable(mapped, false); });
                        h.innerAlive = true;
                    }
                    auto const innerValue = close_if_abrupt(h.iterated, [&]() { return iterator_step_value(h.inner); });
                    if (innerValue)
                        return innerValue;
                    h.innerAlive = false;
                }
            case helper_kind::Concat:
                for (;;)
                {
                    if (!h.innerAlive)
                    {
                        if (h.nextIterable >= h.iterables.size())
                            return {};
                        auto const [method, iterable] = h.iterables[h.nextIterable++];
                        auto const iterator = call(method, iterable);
                        if (!is_object(iterator))
                            throw_type_error(describe(iterator) + " is not an object");
                        h.inner = get_iterator_direct(iterator);
                        h.innerAlive = true;
                    }
                    auto const innerValue = iterator_step_value(h.inner);
                    if (innerValue)
                        return innerValue;
                    h.innerAlive = false;
                }
            case helper_kind::Zip:
                return zip_resume(h);
            case helper_kind::Chunks:
                if (h.finished)
                    return {};
                for (;;)
                {
                    auto const v = iterator_step_value(h.iterated);
                    if (!v)
                    {
                        if (h.buffer.empty())
                            return {};
                        h.finished = true;
                        return make_array(value_list{ h.buffer.begin(), h.buffer.end() });
                    }
                    h.buffer.push_back(*v);
                    if (static_cast<double>(h.buffer.size()) == h.size)
                    {
                        auto result = make_array(value_list{ h.buffer.begin(), h.buffer.end() });
                        h.buffer.clear();
                        return result;
                    }
                }
            case helper_kind::Windows:
                if (h.finished)
                    return {};
                for (;;)
                {
                    auto const v = iterator_step_value(h.iterated);
                    if (!v)
                    {
                        if (h.allowPartial && !h.buffer.empty() && static_cast<double>(h.buffer.size()) < h.size)
                        {
                            h.finished = true;
                            return make_array(value_list{ h.buffer.begin(), h.buffer.end() });
                        }
                        return {};
                    }
                    if (static_cast<double>(h.buffer.size()) == h.size)
                        h.buffer.pop_front();
                    h.buffer.push_back(*v);
                    if (static_cast<double>(h.buffer.size()) == h.size)
                        return make_array(value_list{ h.buffer.begin(), h.buffer.end() });
                }
            }
            return {};
        }

        // (the closure is resumed with a return completion at its Yield)
        void helper_resume_return(iterator_helper& h)
        {
            switch (h.kind)
            {
            case helper_kind::FlatMap:
                try
                {
                    iterator_close(h.inner);
                }
                catch (js_exception const&)
                {
                    iterator_close_on_throw(h.iterated);
                    throw;
                }
                iterator_close(h.iterated);
                break;
            case helper_kind::Concat:
                iterator_close(h.inner);
                break;
            case helper_kind::Zip:
                iterator_close_all(underlying_iterators(h));
                break;
            case helper_kind::Chunks:
            case helper_kind::Windows:
                // (the last yield's completion is ignored: the closure returns (its iterator is exhausted))
                if (!h.finished)
                    iterator_close(h.iterated);
                break;
            default:
                iterator_close(h.iterated);
                break;
            }
        }

        iterator_helper& this_helper(value aThis)
        {
            if (!is_object(aThis) || obj(aThis).cls != object_class::IteratorHelper)
                throw_type_error("Iterator Helper method called on incompatible receiver " + describe(aThis));
            return static_cast<iterator_helper&>(obj(aThis));
        }

        value iterator_helper_next(value aThis, arguments, value, function_object&)
        {
            auto& h = this_helper(aThis);
            if (h.state == helper_state::Executing)
                throw_type_error("Iterator Helper is already running");
            if (h.state == helper_state::Completed)
                return create_iter_result_object(Undefined, true);
            h.state = helper_state::Executing;
            std::optional<value> result;
            try
            {
                result = helper_resume(h);
            }
            catch (...)
            {
                h.state = helper_state::Completed;
                throw;
            }
            if (!result)
            {
                h.state = helper_state::Completed;
                return create_iter_result_object(Undefined, true);
            }
            h.state = helper_state::SuspendedYield;
            return create_iter_result_object(*result, false);
        }

        value iterator_helper_return(value aThis, arguments, value, function_object&)
        {
            auto& h = this_helper(aThis);
            if (h.state == helper_state::SuspendedStart)
            {
                h.state = helper_state::Completed;
                iterator_close_all(underlying_iterators(h));
                return create_iter_result_object(Undefined, true);
            }
            if (h.state == helper_state::Executing)
                throw_type_error("Iterator Helper is already running");
            if (h.state == helper_state::Completed)
                return create_iter_result_object(Undefined, true);
            h.state = helper_state::Executing;
            try
            {
                helper_resume_return(h);
            }
            catch (...)
            {
                h.state = helper_state::Completed;
                throw;
            }
            h.state = helper_state::Completed;
            return create_iter_result_object(Undefined, true);
        }

        // --- %WrapForValidIteratorPrototype%

        wrap_for_valid_iterator& this_wrapper(value aThis)
        {
            if (!is_object(aThis) || obj(aThis).cls != object_class::WrapForValidIterator)
                throw_type_error("method called on incompatible receiver " + describe(aThis));
            return static_cast<wrap_for_valid_iterator&>(obj(aThis));
        }

        value wrap_for_valid_iterator_next(value aThis, arguments, value, function_object&)
        {
            auto& w = this_wrapper(aThis);
            return call(w.iterated.nextMethod, w.iterated.iterator);
        }

        value wrap_for_valid_iterator_return(value aThis, arguments, value, function_object&)
        {
            auto& w = this_wrapper(aThis);
            auto const iterator = w.iterated.iterator;
            auto const returnMethod = get_method(iterator, the_names().return_);
            if (returnMethod == Undefined)
                return create_iter_result_object(Undefined, true);
            return call(returnMethod, iterator);
        }

        // --- the Iterator constructor

        value iterator_constructor(value, arguments, value aNewTarget, function_object& aSelf)
        {
            if (aNewTarget == Undefined || aNewTarget == value_of(aSelf))
                throw_type_error("Abstract class Iterator not directly constructable");
            return ordinary_create_from_constructor(aNewTarget, &realm::iteratorPrototype);
        }

        value iterator_from(value, arguments aArguments, value, function_object&)
        {
            auto const record = get_iterator_flattenable(argument(aArguments, 0u), true);
            if (ordinary_has_instance(the_realm().iterator, record.iterator))
                return record.iterator;
            auto& w = the_heap().make<wrap_for_valid_iterator>(the_realm().wrapForValidIteratorPrototype);
            w.iterated = record;
            return value_of(w);
        }

        value iterator_concat(value, arguments aArguments, value, function_object&)
        {
            rooted_vector<std::pair<value, value>> iterables;
            for (auto const item : aArguments)
            {
                if (!is_object(item))
                    throw_type_error("Iterator.concat: " + describe(item) + " is not an object");
                auto const method = get_method(item, the_realm().symIterator);
                if (method == Undefined)
                    throw_type_error("Iterator.concat: " + describe(item) + " is not iterable");
                iterables.emplace_back(method, item);
            }
            auto& h = make_helper(helper_kind::Concat);
            h.iterables.assign(iterables.begin(), iterables.end());
            return value_of(h);
        }

        // (Iterator.zip and Iterator.zipKeyed: the options)
        zip_mode zip_options(value aOptions, value& aPaddingOption)
        {
            if (aOptions == Undefined)
                aOptions = make_object(Null);
            else if (!is_object(aOptions))
                throw_type_error("options must be an object");
            auto const modeValue = get(aOptions, ascii("mode"));
            zip_mode mode = zip_mode::Shortest;
            if (modeValue != Undefined)
            {
                if (!is_string(modeValue))
                    throw_type_error("mode must be \"shortest\", \"longest\" or \"strict\"");
                auto const& text = str(modeValue);
                if (text == u"shortest")
                    mode = zip_mode::Shortest;
                else if (text == u"longest")
                    mode = zip_mode::Longest;
                else if (text == u"strict")
                    mode = zip_mode::Strict;
                else
                    throw_type_error("mode must be \"shortest\", \"longest\" or \"strict\"");
            }
            aPaddingOption = Undefined;
            if (mode == zip_mode::Longest)
            {
                aPaddingOption = get(aOptions, ascii("padding"));
                if (aPaddingOption != Undefined && !is_object(aPaddingOption))
                    throw_type_error("padding must be an object");
            }
            return mode;
        }

        value make_zip(rooted_vector<iterator_record> aIterators, zip_mode aMode, value_list aPadding, bool aKeyed, value_list aKeys)
        {
            auto& h = make_helper(helper_kind::Zip);
            h.mode = aMode;
            h.keyed = aKeyed;
            h.keys.assign(aKeys.begin(), aKeys.end());
            h.padding.assign(aPadding.begin(), aPadding.end());
            h.exhausted.assign(aIterators.size(), false);
            for (std::size_t i = 0u; i < aIterators.size(); ++i)
                h.openIterators.push_back(i);
            h.iterators.assign(aIterators.begin(), aIterators.end());
            return value_of(h);
        }

        // (IfAbruptCloseIterators)
        template <typename F>
        auto close_all_if_abrupt(std::span<iterator_record const> aIterators, F&& aFunction) -> decltype(aFunction())
        {
            try
            {
                return aFunction();
            }
            catch (js_exception const& e)
            {
                iterator_close_all(aIterators, e.thrown);
                throw;
            }
        }

        value iterator_zip(value, arguments aArguments, value, function_object&)
        {
            auto const iterables = argument(aArguments, 0u);
            if (!is_object(iterables))
                throw_type_error("Iterator.zip: " + describe(iterables) + " is not an object");
            value paddingOption;
            auto const mode = zip_options(argument(aArguments, 1u), paddingOption);
            rooted_vector<iterator_record> iterators;
            value_list padding;
            auto inputIterator = get_iterator(iterables);
            for (;;)
            {
                auto const next = close_all_if_abrupt(iterators, [&]() { return iterator_step_value(inputIterator); });
                if (!next)
                    break;
                try
                {
                    iterators.push_back(get_iterator_flattenable(*next, false));
                }
                catch (js_exception const& e)
                {
                    rooted_vector<iterator_record> needClosing{ inputIterator };
                    needClosing.insert(needClosing.end(), iterators.begin(), iterators.end());
                    iterator_close_all(needClosing, e.thrown);
                }
            }
            auto const count = iterators.size();
            if (mode == zip_mode::Longest)
            {
                if (paddingOption == Undefined)
                    padding.assign(count, Undefined);
                else
                {
                    auto paddingIterator = close_all_if_abrupt(iterators, [&]() { return get_iterator(paddingOption); });
                    bool usingIterator = true;
                    for (std::size_t i = 0u; i < count; ++i)
                    {
                        if (usingIterator)
                        {
                            auto const next = close_all_if_abrupt(iterators, [&]() { return iterator_step_value(paddingIterator); });
                            if (!next)
                                usingIterator = false;
                            else
                                padding.push_back(*next);
                        }
                        if (!usingIterator)
                            padding.push_back(Undefined);
                    }
                    if (usingIterator)
                        close_all_if_abrupt(iterators, [&]() { iterator_close(paddingIterator); });
                }
            }
            return make_zip(std::move(iterators), mode, std::move(padding), false, {});
        }

        value iterator_zip_keyed(value, arguments aArguments, value, function_object&)
        {
            auto const iterables = argument(aArguments, 0u);
            if (!is_object(iterables))
                throw_type_error("Iterator.zipKeyed: " + describe(iterables) + " is not an object");
            value paddingOption;
            auto const mode = zip_options(argument(aArguments, 1u), paddingOption);
            rooted_vector<iterator_record> iterators;
            value_list padding;
            auto const allKeys = obj(iterables).own_property_keys();
            value_list keys;
            for (auto const key : allKeys)
            {
                auto const desc = close_all_if_abrupt(iterators, [&]() { return obj(iterables).get_own_property(key); });
                if (desc && desc->enumerable.value_or(false))
                {
                    auto const v = close_all_if_abrupt(iterators, [&]() { return get(iterables, key); });
                    if (v != Undefined)
                    {
                        keys.push_back(key);
                        auto const record = close_all_if_abrupt(iterators, [&]() { return get_iterator_flattenable(v, false); });
                        iterators.push_back(record);
                    }
                }
            }
            auto const count = iterators.size();
            if (mode == zip_mode::Longest)
            {
                if (paddingOption == Undefined)
                    padding.assign(count, Undefined);
                else
                    for (auto const key : keys)
                        padding.push_back(close_all_if_abrupt(iterators, [&]() { return get(paddingOption, key); }));
            }
            return make_zip(std::move(iterators), mode, std::move(padding), true, std::move(keys));
        }

        // --- %Iterator.prototype%

        value this_iterator(value aThis)
        {
            if (!is_object(aThis))
                throw_type_error("Iterator.prototype method called on non-object " + describe(aThis));
            return aThis;
        }

        // (validates a helper's function argument: the iterator is closed if it is not callable)
        void require_callable(value aIterator, value aFunction)
        {
            if (!is_callable(aFunction))
                close_and_throw(iterator_record{ aIterator, Undefined, false }, the_realm().typeErrorPrototype, describe(aFunction) + " is not a function");
        }

        value make_function_helper(value aThis, arguments aArguments, helper_kind aKind)
        {
            auto const o = this_iterator(aThis);
            auto const f = argument(aArguments, 0u);
            require_callable(o, f);
            auto const record = get_iterator_direct(o);
            auto& h = make_helper(aKind);
            h.iterated = record;
            h.function = f;
            return value_of(h);
        }

        value iterator_prototype_map(value aThis, arguments aArguments, value, function_object&)
        {
            return make_function_helper(aThis, aArguments, helper_kind::Map);
        }

        value iterator_prototype_filter(value aThis, arguments aArguments, value, function_object&)
        {
            return make_function_helper(aThis, aArguments, helper_kind::Filter);
        }

        value iterator_prototype_flat_map(value aThis, arguments aArguments, value, function_object&)
        {
            return make_function_helper(aThis, aArguments, helper_kind::FlatMap);
        }

        value make_limit_helper(value aThis, arguments aArguments, helper_kind aKind)
        {
            auto const o = this_iterator(aThis);
            iterator_record const iterated{ o, Undefined, false };
            auto const numberLimit = close_if_abrupt(iterated, [&]() { return to_number(argument(aArguments, 0u)); });
            if (std::isnan(numberLimit))
                close_and_throw(iterated, the_realm().rangeErrorPrototype, "limit must be a number");
            if (std::isfinite(numberLimit) && numberLimit > 9007199254740991.0)
                close_and_throw(iterated, the_realm().rangeErrorPrototype, "limit is too large");
            auto const intLimit = to_integer_or_infinity(number(numberLimit));
            if (intLimit < 0.0)
                close_and_throw(iterated, the_realm().rangeErrorPrototype, "limit must be positive");
            auto const record = get_iterator_direct(o);
            auto& h = make_helper(aKind);
            h.iterated = record;
            h.remaining = intLimit;
            return value_of(h);
        }

        value iterator_prototype_take(value aThis, arguments aArguments, value, function_object&)
        {
            return make_limit_helper(aThis, aArguments, helper_kind::Take);
        }

        value iterator_prototype_drop(value aThis, arguments aArguments, value, function_object&)
        {
            return make_limit_helper(aThis, aArguments, helper_kind::Drop);
        }

        value iterator_prototype_reduce(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = this_iterator(aThis);
            auto const reducer = argument(aArguments, 0u);
            require_callable(o, reducer);
            auto iterated = get_iterator_direct(o);
            value accumulator;
            double counter;
            if (aArguments.size() < 2u)
            {
                auto const first = iterator_step_value(iterated);
                if (!first)
                    throw_type_error("Reduce of empty iterator with no initial value");
                accumulator = *first;
                counter = 1.0;
            }
            else
            {
                accumulator = aArguments[1];
                counter = 0.0;
            }
            for (;;)
            {
                auto const v = iterator_step_value(iterated);
                if (!v)
                    return accumulator;
                value const arguments_[] = { accumulator, *v, counter_value(counter) };
                accumulator = close_if_abrupt(iterated, [&]() { return call(reducer, Undefined, arguments_); });
            }
        }

        value iterator_prototype_to_array(value aThis, arguments, value, function_object&)
        {
            auto const o = this_iterator(aThis);
            auto iterated = get_iterator_direct(o);
            value_list items;
            while (auto const v = iterator_step_value(iterated))
                items.push_back(*v);
            return make_array(std::move(items));
        }

        value iterator_prototype_for_each(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = this_iterator(aThis);
            auto const procedure = argument(aArguments, 0u);
            require_callable(o, procedure);
            auto iterated = get_iterator_direct(o);
            double counter = 0.0;
            while (auto const v = iterator_step_value(iterated))
            {
                value const arguments_[] = { *v, counter_value(counter) };
                close_if_abrupt(iterated, [&]() { return call(procedure, Undefined, arguments_); });
            }
            return Undefined;
        }

        // (some (0), every (1) and find (2))
        value iterator_search(value aThis, arguments aArguments, int aKind)
        {
            auto const o = this_iterator(aThis);
            auto const predicate = argument(aArguments, 0u);
            require_callable(o, predicate);
            auto iterated = get_iterator_direct(o);
            double counter = 0.0;
            for (;;)
            {
                auto const v = iterator_step_value(iterated);
                if (!v)
                    return aKind == 0 ? False : aKind == 1 ? True : Undefined;
                value const arguments_[] = { *v, counter_value(counter) };
                auto const result = to_boolean(close_if_abrupt(iterated, [&]() { return call(predicate, Undefined, arguments_); }));
                if (aKind == 1 ? !result : result)
                {
                    iterator_close(iterated);
                    return aKind == 0 ? True : aKind == 1 ? False : *v;
                }
            }
        }

        value iterator_prototype_some(value aThis, arguments aArguments, value, function_object&)
        {
            return iterator_search(aThis, aArguments, 0);
        }

        value iterator_prototype_every(value aThis, arguments aArguments, value, function_object&)
        {
            return iterator_search(aThis, aArguments, 1);
        }

        value iterator_prototype_find(value aThis, arguments aArguments, value, function_object&)
        {
            return iterator_search(aThis, aArguments, 2);
        }

        value iterator_prototype_join(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = this_iterator(aThis);
            iterator_record const unopened{ o, Undefined, false };
            auto const separator = argument(aArguments, 0u);
            std::u16string separatorString = u",";
            if (separator != Undefined)
                separatorString = str(close_if_abrupt(unopened, [&]() { return to_string(separator); }));
            auto iterated = get_iterator_direct(o);
            std::u16string result;
            bool first = true;
            for (;;)
            {
                auto const v = iterator_step_value(iterated);
                if (!v)
                    return make_string(std::move(result));
                if (first)
                    first = false;
                else
                    result += separatorString;
                if (!is_nullish(*v))
                    result += str(close_if_abrupt(iterated, [&]() { return to_string(*v); }));
                if (result.size() > MaxStringLength)
                    close_and_throw(iterated, the_realm().rangeErrorPrototype, "Invalid string length");
            }
        }

        // (chunks and windows: the size (an integral Number from 1 to 2^32 - 1))
        double chunk_size(iterator_record const& aIterated, value aSize)
        {
            if (!is_integral_number(aSize))
                close_and_throw(aIterated, the_realm().typeErrorPrototype, describe(aSize) + " is not an integral number");
            auto const size = as_number(aSize);
            if (size < 1.0 || size > 4294967295.0)
                close_and_throw(aIterated, the_realm().rangeErrorPrototype, "size must be between 1 and 2^32 - 1");
            return size;
        }

        value iterator_prototype_chunks(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = this_iterator(aThis);
            iterator_record const unopened{ o, Undefined, false };
            auto const size = chunk_size(unopened, argument(aArguments, 0u));
            auto const record = get_iterator_direct(o);
            auto& h = make_helper(helper_kind::Chunks);
            h.iterated = record;
            h.size = size;
            return value_of(h);
        }

        value iterator_prototype_windows(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = this_iterator(aThis);
            iterator_record const unopened{ o, Undefined, false };
            auto const size = chunk_size(unopened, argument(aArguments, 0u));
            auto const undersized = argument(aArguments, 1u);
            bool allowPartial = false;
            if (undersized != Undefined)
            {
                if (is_string(undersized) && str(undersized) == u"allow-partial")
                    allowPartial = true;
                else if (!is_string(undersized) || str(undersized) != u"only-full")
                    close_and_throw(unopened, the_realm().typeErrorPrototype, "undersized must be \"only-full\" or \"allow-partial\"");
            }
            auto const record = get_iterator_direct(o);
            auto& h = make_helper(helper_kind::Windows);
            h.iterated = record;
            h.size = size;
            h.allowPartial = allowPartial;
            return value_of(h);
        }

        value iterator_prototype_includes(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = this_iterator(aThis);
            iterator_record const unopened{ o, Undefined, false };
            auto const searchElement = argument(aArguments, 0u);
            auto const skippedElements = argument(aArguments, 1u);
            double toSkip = 0.0;
            if (skippedElements != Undefined)
            {
                if (!is_number(skippedElements) || !(std::isinf(as_number(skippedElements)) || is_integral_number(skippedElements)))
                    close_and_throw(unopened, the_realm().typeErrorPrototype, describe(skippedElements) + " is not an integral number");
                toSkip = as_number(skippedElements);
            }
            if (toSkip < 0.0)
                close_and_throw(unopened, the_realm().rangeErrorPrototype, "skippedElements must not be negative");
            if (std::isfinite(toSkip) && toSkip > 9007199254740991.0)
                close_and_throw(unopened, the_realm().rangeErrorPrototype, "skippedElements is too large");
            double skipped = 0.0;
            auto iterated = get_iterator_direct(o);
            for (;;)
            {
                auto const v = iterator_step_value(iterated);
                if (!v)
                    return False;
                if (skipped < toSkip)
                    skipped += 1.0;
                else if (same_value_zero(*v, searchElement))
                {
                    iterator_close(iterated);
                    return True;
                }
            }
        }

        value iterator_prototype_dispose(value aThis, arguments, value, function_object&)
        {
            auto const returnMethod = get_method(aThis, the_names().return_);
            if (returnMethod != Undefined)
                call(returnMethod, aThis);
            return Undefined;
        }

        // (SetterThatIgnoresPrototypeProperties)
        void setter_that_ignores_prototype_properties(value aThis, value aHome, value aKey, value aValue)
        {
            if (!is_object(aThis))
                throw_type_error("setter called on non-object " + describe(aThis));
            if (aThis == aHome)
                throw_type_error("Cannot assign to read only property '" + describe(aKey) + "' of object");
            if (!obj(aThis).get_own_property(aKey))
                create_data_property_or_throw(aThis, aKey, aValue);
            else
                set(aThis, aKey, aValue, true);
        }

        value iterator_prototype_get_to_string_tag(value, arguments, value, function_object&)
        {
            return ascii("Iterator");
        }

        value iterator_prototype_set_to_string_tag(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& r = the_realm();
            setter_that_ignores_prototype_properties(aThis, r.iteratorPrototype, r.symToStringTag, argument(aArguments, 0u));
            return Undefined;
        }

        value iterator_prototype_get_constructor(value, arguments, value, function_object&)
        {
            return the_realm().iterator;
        }

        value iterator_prototype_set_constructor(value aThis, arguments aArguments, value, function_object&)
        {
            setter_that_ignores_prototype_properties(aThis, the_realm().iteratorPrototype, the_names().constructor, argument(aArguments, 0u));
            return Undefined;
        }
    }

    void init_iterator_helpers(realm& aRealm)
    {
        auto const ip = aRealm.iteratorPrototype;
        // (the Iterator constructor)
        auto const c = make_native("Iterator", 0u, iterator_constructor, true);
        aRealm.iterator = c;
        obj(c).put(the_names().prototype, ip, 0u);
        method(c, "concat", 0u, iterator_concat);
        method(c, "from", 1u, iterator_from);
        method(c, "zip", 1u, iterator_zip);
        method(c, "zipKeyed", 1u, iterator_zip_keyed);
        // (%Iterator.prototype%)
        getter(ip, the_names().constructor, iterator_prototype_get_constructor, iterator_prototype_set_constructor);
        method(ip, "drop", 1u, iterator_prototype_drop);
        method(ip, "chunks", 1u, iterator_prototype_chunks);
        method(ip, "every", 1u, iterator_prototype_every);
        method(ip, "filter", 1u, iterator_prototype_filter);
        method(ip, "find", 1u, iterator_prototype_find);
        method(ip, "flatMap", 1u, iterator_prototype_flat_map);
        method(ip, "forEach", 1u, iterator_prototype_for_each);
        method(ip, "includes", 1u, iterator_prototype_includes);
        method(ip, "join", 1u, iterator_prototype_join);
        method(ip, "map", 1u, iterator_prototype_map);
        method(ip, "reduce", 1u, iterator_prototype_reduce);
        method(ip, "some", 1u, iterator_prototype_some);
        method(ip, "take", 1u, iterator_prototype_take);
        method(ip, "toArray", 0u, iterator_prototype_to_array);
        method(ip, "windows", 1u, iterator_prototype_windows);
        method(ip, aRealm.symDispose, 0u, iterator_prototype_dispose);
        getter(ip, aRealm.symToStringTag, iterator_prototype_get_to_string_tag, iterator_prototype_set_to_string_tag);
        // (%IteratorHelperPrototype%)
        aRealm.iteratorHelperPrototype = make_object(ip);
        method(aRealm.iteratorHelperPrototype, "next", 0u, iterator_helper_next);
        method(aRealm.iteratorHelperPrototype, "return", 0u, iterator_helper_return);
        obj(aRealm.iteratorHelperPrototype).put(aRealm.symToStringTag, ascii("Iterator Helper"), AttributeConfigurable);
        // (%WrapForValidIteratorPrototype%)
        aRealm.wrapForValidIteratorPrototype = make_object(ip);
        method(aRealm.wrapForValidIteratorPrototype, "next", 0u, wrap_for_valid_iterator_next);
        method(aRealm.wrapForValidIteratorPrototype, "return", 0u, wrap_for_valid_iterator_return);
        obj(aRealm.global).put(ascii("Iterator"), c);
    }
}
