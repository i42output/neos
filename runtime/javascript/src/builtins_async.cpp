/*
  builtins_async.cpp

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

// Built-ins that are async functions (they await): Array.fromAsync; and Error.isError

#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        constexpr double MaxSafeInteger = 9007199254740991.0;

        value key_of(double aIndex)
        {
            return to_property_key(number(aIndex));
        }

        value array_from_async(value aThis, arguments aArguments, value, function_object&)
        {
            auto const c = aThis;
            auto const asyncItems = argument(aArguments, 0u);
            auto const mapfn = argument(aArguments, 1u);
            auto const thisArg = argument(aArguments, 2u);
            return async_native([=]() -> value
            {
                bool const mapping = mapfn != Undefined;
                if (mapping && !is_callable(mapfn))
                    throw_type_error("Array.fromAsync: " + describe(mapfn) + " is not a function");
                auto const& r = the_realm();
                auto const usingAsyncIterator = get_method(asyncItems, r.symAsyncIterator);
                value usingSyncIterator = Undefined;
                if (usingAsyncIterator == Undefined)
                    usingSyncIterator = get_method(asyncItems, r.symIterator);
                std::optional<iterator_record> record;
                if (usingAsyncIterator != Undefined)
                {
                    record = get_iterator_from_method(asyncItems, usingAsyncIterator);
                    record->async = true;
                }
                else if (usingSyncIterator != Undefined)
                {
                    record = create_async_from_sync_iterator(get_iterator_from_method(asyncItems, usingSyncIterator));
                    record->async = true;
                }
                if (record)
                {
                    auto const a = is_constructor(c) ? construct(c) : create_array_from_list({});
                    double k = 0.0;
                    for (;;)
                    {
                        if (k >= MaxSafeInteger)
                        {
                            auto const error = make_error(r.typeErrorPrototype, ascii("Array.fromAsync: too many elements"));
                            iterator_close_on_throw(*record);
                            throw js_exception{ error };
                        }
                        auto const pk = key_of(k);
                        auto nextResult = call(record->nextMethod, record->iterator);
                        nextResult = await_value(nextResult);
                        if (!is_object(nextResult))
                            throw_type_error("Iterator result " + describe(nextResult) + " is not an object");
                        if (iterator_complete(nextResult))
                        {
                            set(a, the_names().length, number(k), true);
                            return a;
                        }
                        auto const nextValue = iterator_value(nextResult);
                        value mapped = nextValue;
                        try
                        {
                            if (mapping)
                            {
                                value const args[] = { nextValue, number(k) };
                                mapped = call(mapfn, thisArg, args);
                                mapped = await_value(mapped);
                            }
                            create_data_property_or_throw(a, pk, mapped);
                        }
                        catch (js_exception const&)
                        {
                            iterator_close_on_throw(*record);
                            throw;
                        }
                        k += 1.0;
                    }
                }
                // (an array-like)
                auto const arrayLike = to_object(asyncItems);
                auto const len = length_of_array_like(arrayLike);
                value const lengthArgument[] = { number(len) };
                auto const a = is_constructor(c) ? construct(c, lengthArgument) : create_array_from_list({});
                if (!is_constructor(c))
                    set(a, the_names().length, number(len), true);
                for (double k = 0.0; k < len; k += 1.0)
                {
                    auto const pk = key_of(k);
                    auto kValue = get(arrayLike, pk);
                    kValue = await_value(kValue);
                    value mapped = kValue;
                    if (mapping)
                    {
                        value const args[] = { kValue, number(k) };
                        mapped = await_value(call(mapfn, thisArg, args));
                    }
                    create_data_property_or_throw(a, pk, mapped);
                }
                set(a, the_names().length, number(len), true);
                return a;
            });
        }

        value error_is_error(value, arguments aArguments, value, function_object&)
        {
            auto const v = argument(aArguments, 0u);
            return boolean(is_object(v) && obj(v).cls == object_class::Error);
        }
    }

    void init_async_builtins(realm& aRealm)
    {
        method(aRealm.array, "fromAsync", 1u, array_from_async);
        method(aRealm.error, "isError", 1u, error_is_error);
    }
}
