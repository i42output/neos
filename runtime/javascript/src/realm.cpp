/*
  realm.cpp

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

// The realm: its intrinsics (the built-ins' objects) and global object

#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        realm* sRealm = nullptr;            // (the first realm: the program's)
        thread_local realm* tCurrent = nullptr;
        names* sNames = nullptr;

        std::unordered_map<std::u16string, value>& symbol_registry()
        {
            static std::unordered_map<std::u16string, value> sRegistry;
            return sRegistry;
        }

        void initialize(realm& r);
    }

    realm& the_realm()
    {
        return tCurrent != nullptr ? *tCurrent : *sRealm;
    }

    bool realm_initialized()
    {
        return sRealm != nullptr;
    }

    realm_scope::realm_scope(realm* aRealm) : iPrevious{ tCurrent }
    {
        if (aRealm != nullptr)
            tCurrent = aRealm;
    }

    realm_scope::~realm_scope()
    {
        tCurrent = iPrevious;
    }

    realm* exchange_realm(realm* aRealm)
    {
        return std::exchange(tCurrent, aRealm);
    }

    realm* create_realm()
    {
        auto* r = new realm{ .registry = symbol_registry() };
        realm_scope scope{ r };
        initialize(*r);
        return r;
    }

    realm& function_realm(value aFunction)
    {
        // (GetFunctionRealm: a function's realm (a bound function's: its target's; a proxy's: its target's (a revoked proxy throws)))
        for (auto p = aFunction; is_object(p);)
        {
            auto& o = obj(p);
            if (o.cls == object_class::Proxy)
            {
                proxy_function_realm(p);
                break;
            }
            auto* f = as_function(p);
            if (f == nullptr)
                break;
            if (f->kind == function_kind::Bound)
            {
                p = f->boundTarget;
                continue;
            }
            if (f->functionRealm != nullptr)
                return *f->functionRealm;
            break;
        }
        return the_realm();
    }

    names const& the_names()
    {
        return *sNames;
    }

    void initialize_realm()
    {
        sNames = new names{};
        auto& n = *sNames;
        n.length = ascii("length");
        n.prototype = ascii("prototype");
        n.constructor = ascii("constructor");
        n.name = ascii("name");
        n.message = ascii("message");
        n.cause = ascii("cause");
        n.value_ = ascii("value");
        n.writable = ascii("writable");
        n.enumerable = ascii("enumerable");
        n.configurable = ascii("configurable");
        n.get = ascii("get");
        n.set = ascii("set");
        n.toString = ascii("toString");
        n.valueOf = ascii("valueOf");
        n.done = ascii("done");
        n.next = ascii("next");
        n.return_ = ascii("return");
        n.throw_ = ascii("throw");
        n.callee = ascii("callee");
        n.caller = ascii("caller");
        n.arguments = ascii("arguments");
        n.lastIndex = ascii("lastIndex");
        n.index = ascii("index");
        n.input = ascii("input");
        n.groups = ascii("groups");
        n.indices = ascii("indices");
        n.then = ascii("then");
        n.errors = ascii("errors");
        n.error = ascii("error");
        n.suppressed = ascii("suppressed");
        n.stack = ascii("stack");
        n.empty = intern(std::u16string_view{});
        n.undefined = ascii("undefined");
        n.null = ascii("null");
        n.true_ = ascii("true");
        n.false_ = ascii("false");
        n.object = ascii("object");
        n.function = ascii("function");
        n.string = ascii("string");
        n.number = ascii("number");
        n.boolean = ascii("boolean");
        n.symbol = ascii("symbol");
        n.bigint = ascii("bigint");
        n.default_ = ascii("default");
        n.join = ascii("join");
        n.raw = ascii("raw");
        n.size = ascii("size");
        n.has = ascii("has");
        n.add = ascii("add");
        n.keys = ascii("keys");
        n.values = ascii("values");
        n.entries = ascii("entries");

        sRealm = new realm{ .registry = symbol_registry() };
        initialize(*sRealm);
    }

    namespace
    {
    void initialize(realm& r)
    {
        // (well-known symbols: shared by the realms)
        auto const symbol = [](char const* aDescription) { return make_symbol(ascii(aDescription)); };
        if (&r != sRealm)
        {
            auto const& m = *sRealm;
            r.symAsyncIterator = m.symAsyncIterator, r.symHasInstance = m.symHasInstance, r.symIsConcatSpreadable = m.symIsConcatSpreadable,
                r.symIterator = m.symIterator, r.symMatch = m.symMatch, r.symMatchAll = m.symMatchAll, r.symReplace = m.symReplace,
                r.symSearch = m.symSearch, r.symSpecies = m.symSpecies, r.symSplit = m.symSplit, r.symToPrimitive = m.symToPrimitive,
                r.symToStringTag = m.symToStringTag, r.symUnscopables = m.symUnscopables, r.symDispose = m.symDispose,
                r.symAsyncDispose = m.symAsyncDispose;
        }
        else
        {
        r.symAsyncIterator = symbol("Symbol.asyncIterator");
        r.symHasInstance = symbol("Symbol.hasInstance");
        r.symIsConcatSpreadable = symbol("Symbol.isConcatSpreadable");
        r.symIterator = symbol("Symbol.iterator");
        r.symMatch = symbol("Symbol.match");
        r.symMatchAll = symbol("Symbol.matchAll");
        r.symReplace = symbol("Symbol.replace");
        r.symSearch = symbol("Symbol.search");
        r.symSpecies = symbol("Symbol.species");
        r.symSplit = symbol("Symbol.split");
        r.symToPrimitive = symbol("Symbol.toPrimitive");
        r.symToStringTag = symbol("Symbol.toStringTag");
        r.symUnscopables = symbol("Symbol.unscopables");
        r.symDispose = symbol("Symbol.dispose");
        r.symAsyncDispose = symbol("Symbol.asyncDispose");
        }
        // (the fundamental objects first: %Object.prototype%, %Function.prototype% and the global object)
        r.objectPrototype = make_object(Null);
        r.functionPrototype = value_of(the_heap().make<function_object>(r.objectPrototype, function_kind::Native));
        r.global = make_object(r.objectPrototype);
        auto& g = obj(r.global);
        g.put(ascii("globalThis"), r.global, AttributesHidden);
        g.put(ascii("NaN"), CanonicalNaN, 0u);
        g.put(ascii("Infinity"), number(std::numeric_limits<double>::infinity()), 0u);
        g.put(ascii("undefined"), Undefined, 0u);
        init_object(r);
        init_function(r);
        init_symbol(r);
        init_error(r);
        init_iterator(r);
        init_boolean(r);
        init_number(r);
        init_bigint(r);
        init_math(r);
        init_string(r);
        init_array(r);
        init_global(r);
        init_eval(r);
        init_json(r);
        init_reflect(r);
        init_proxy(r);
        init_collections(r);
        init_promise(r);
        init_disposable(r);
        init_generator(r);
        init_async_builtins(r);
        init_regexp(r);
        init_date(r);
        init_temporal(r);
        init_typed_arrays(r);
        init_console(r);
        init_host262(r);
    }
    }
}
