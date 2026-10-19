/*
  py.cpp

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

#include "py.hpp"

namespace neos::numpy::py
{
    namespace
    {
        value checked(object* aObject)
        {
            if (aObject == nullptr)
                throw raised{};
            return value::steal(aObject);
        }
    }

    value::value(ref aRef) : iRef{ std::move(aRef) }
    {
        if (!iRef)
            throw raised{};
    }

    value::value(int aValue) : iRef{ make_int(aValue) } {}
    value::value(long aValue) : iRef{ make_int(aValue) } {}
    value::value(long long aValue) : iRef{ make_int(aValue) } {}
    value::value(unsigned long aValue) : iRef{ make_int(static_cast<long long>(aValue)) } {}
    value::value(unsigned long long aValue) : iRef{ make_int(static_cast<long long>(aValue)) } {}
    value::value(double aValue) : iRef{ make_float(aValue) } {}
    value::value(bool aValue) : iRef{ make_bool(aValue) } {}
    value::value(char const* aValue) : iRef{ make_str(aValue) } {}
    value::value(std::string const& aValue) : iRef{ make_str(aValue) } {}

    value value::steal(object* aObject)
    {
        if (aObject == nullptr)
            throw raised{};
        value result;
        result.iRef = ref{ aObject };
        return result;
    }

    value value::borrow(object* aObject)
    {
        value result;
        result.iRef = ref::borrowed(aObject);
        return result;
    }

    bool value::is_none() const
    {
        return iRef && kind(iRef.get()) == NEOS_PY_NONE;
    }

    ndarray* value::array() const
    {
        if (!iRef || !is_array(iRef.get()))
            raise("TypeError", "expected an array");
        return as_array(iRef.get());
    }

    value value::attr(char const* aName) const
    {
        return checked(ctx->getattr(ctx, get(), aName));
    }

    void value::set_attr(char const* aName, value const& aValue) const
    {
        if (ctx->setattr(ctx, get(), aName, aValue.get()) != 0)
            throw raised{};
    }

    value value::operator[](value const& aKey) const
    {
        return checked(ctx->getitem(ctx, get(), aKey.get()));
    }

    void value::set(value const& aKey, value const& aValue) const
    {
        if (ctx->setitem(ctx, get(), aKey.get(), aValue.get()) != 0)
            throw raised{};
    }

    value value::call(std::vector<value> const& aArgs, std::vector<kw> const& aKwargs) const
    {
        std::vector<object*> items;
        for (auto const& a : aArgs)
            items.push_back(a.empty() ? new_none() : (ctx->retain(a.get()), a.get()));
        ref const args{ make_tuple(std::move(items)) };
        ref kwargs;
        if (!aKwargs.empty())
        {
            kwargs = ref{ ctx->dict_new(ctx) };
            for (auto const& k : aKwargs)
            {
                if (k.v->empty())
                    continue;
                ref const key{ make_str(k.name) };
                ctx->dict_set(ctx, kwargs.get(), key.get(), k.v->get());
            }
        }
        return checked(ctx->call(ctx, get(), args.get(), kwargs.get()));
    }

    bool value::truth() const
    {
        auto const t = ctx->truth(ctx, get());
        if (t < 0)
            throw raised{};
        return t == 1;
    }

    long long value::to_int() const
    {
        // (int(x): a float is truncated)
        if (kind(get()) == NEOS_PY_FLOAT || (is_array(get()) && as_array(get())->ndim() == 0 && numpy::is_float(as_array(get())->type)))
            return static_cast<long long>(to_double());
        std::int64_t result = 0;
        if (!numpy::to_int(get(), result))
            throw raised{};
        return result;
    }

    double value::to_double() const
    {
        double result = 0.0;
        if (!numpy::to_double(get(), result))
            throw raised{};
        return result;
    }

    std::string value::to_str() const
    {
        auto const s = as_str(get());
        if (!s)
            raise("TypeError", "expected a str");
        return *s;
    }

    std::string value::str() const
    {
        return checked(ctx->str(ctx, get())).to_str();
    }

    std::string value::repr() const
    {
        return checked(ctx->repr(ctx, get())).to_str();
    }

    long long value::len() const
    {
        auto const n = ctx->length(ctx, get());
        if (n < 0)
            throw raised{};
        return n;
    }

    std::vector<value> value::items() const
    {
        if (numpy::is_sequence(get()))
        {
            std::vector<value> result;
            for (auto x : sequence_items(get()))
                result.push_back(borrow(x));
            return result;
        }
        auto const iterator = checked(ctx->iter(ctx, get()));
        std::vector<value> result;
        for (;;)
        {
            auto const next = ctx->next(ctx, iterator.get());
            if (next == nullptr)
            {
                if (failed())
                    throw raised{};
                break;
            }
            result.push_back(steal(next));
        }
        return result;
    }

    long long value::ndim() const
    {
        return static_cast<long long>(array()->ndim());
    }

    long long value::size() const
    {
        return array()->size();
    }

    value none()
    {
        return value::steal(new_none());
    }

    value np(char const* aName)
    {
        return checked(module_getattr(aName));
    }

    value tuple(std::vector<value> const& aItems)
    {
        std::vector<object*> items;
        for (auto const& a : aItems)
            items.push_back((ctx->retain(a.get()), a.get()));
        return checked(make_tuple(std::move(items)));
    }

    value list(std::vector<value> const& aItems)
    {
        std::vector<object*> items;
        for (auto const& a : aItems)
            items.push_back((ctx->retain(a.get()), a.get()));
        return checked(make_list(std::move(items)));
    }

    value dict(std::vector<std::pair<char const*, value>> const& aItems)
    {
        auto const d = checked(ctx->dict_new(ctx));
        for (auto const& item : aItems)
        {
            value const key{ item.first };
            ctx->dict_set(ctx, d.get(), key.get(), item.second.get());
        }
        return d;
    }

    value slice(value const& aStart, value const& aStop, value const& aStep)
    {
        auto const n = none();
        return checked(ctx->slice_new(ctx, aStart.empty() ? n.get() : aStart.get(), aStop.empty() ? n.get() : aStop.get(), aStep.empty() ? n.get() : aStep.get()));
    }

    value all()
    {
        return slice({}, {}, {});
    }

    value ellipsis()
    {
        return checked(ctx->ellipsis(ctx));
    }

    value array_value(ndarray* aArray)
    {
        return checked(as_object(aArray));
    }

    bool isinstance_array(value const& aValue)
    {
        return is_ndarray(aValue.get());
    }

    bool is_str(value const& aValue) { return kind(aValue.get()) == NEOS_PY_STR; }
    bool is_int(value const& aValue) { return kind(aValue.get()) == NEOS_PY_INT; }
    bool is_float(value const& aValue) { return kind(aValue.get()) == NEOS_PY_FLOAT; }
    bool is_tuple(value const& aValue) { return kind(aValue.get()) == NEOS_PY_TUPLE; }
    bool is_list(value const& aValue) { return kind(aValue.get()) == NEOS_PY_LIST; }
    bool is_sequence(value const& aValue) { return numpy::is_sequence(aValue.get()); }

    bool is_callable(value const& aValue)
    {
        auto const k = kind(aValue.get());
        if (k == NEOS_PY_FUNCTION)
            return true;
        return type_name(aValue.get()) == "type" || type_name(aValue.get()) == "ufunc" || type_name(aValue.get()) == "function";
    }

    value dtype_of(value const& aValue)
    {
        return aValue.attr("dtype");
    }

    void raise(char const* aType, std::string const& aMessage)
    {
        error(aType, aMessage);
        throw raised{};
    }

    std::string caught()
    {
        auto const text = ctx->exception(ctx);
        std::string result = text != nullptr ? text : "";
        ctx->clear(ctx);
        return result;
    }

    void check()
    {
        if (failed())
            throw raised{};
    }

    namespace
    {
        value binop(int aOperation, value const& a, value const& b)
        {
            return checked(ctx->binary(ctx, aOperation, a.get(), b.get()));
        }

        value compare(int aOperation, value const& a, value const& b)
        {
            return checked(ctx->compare(ctx, aOperation, a.get(), b.get()));
        }
    }

    value operator+(value const& a, value const& b) { return binop(NEOS_PY_ADD, a, b); }
    value operator-(value const& a, value const& b) { return binop(NEOS_PY_SUBTRACT, a, b); }
    value operator*(value const& a, value const& b) { return binop(NEOS_PY_MULTIPLY, a, b); }
    value operator/(value const& a, value const& b) { return binop(NEOS_PY_TRUE_DIVIDE, a, b); }
    value operator%(value const& a, value const& b) { return binop(NEOS_PY_REMAINDER, a, b); }
    value operator&(value const& a, value const& b) { return binop(NEOS_PY_AND, a, b); }
    value operator|(value const& a, value const& b) { return binop(NEOS_PY_OR, a, b); }
    value operator^(value const& a, value const& b) { return binop(NEOS_PY_XOR, a, b); }
    value operator<<(value const& a, value const& b) { return binop(NEOS_PY_LSHIFT, a, b); }
    value operator>>(value const& a, value const& b) { return binop(NEOS_PY_RSHIFT, a, b); }
    value floordiv(value const& a, value const& b) { return binop(NEOS_PY_FLOOR_DIVIDE, a, b); }
    value pow(value const& a, value const& b) { return binop(NEOS_PY_POWER, a, b); }
    value matmul(value const& a, value const& b) { return binop(NEOS_PY_MATMUL, a, b); }
    value operator-(value const& a) { return checked(ctx->unary(ctx, NEOS_PY_NEGATIVE, a.get())); }
    value operator~(value const& a) { return checked(ctx->unary(ctx, NEOS_PY_INVERT, a.get())); }
    value abs(value const& a) { return checked(ctx->unary(ctx, NEOS_PY_ABSOLUTE, a.get())); }
    value operator==(value const& a, value const& b) { return compare(NEOS_PY_EQ, a, b); }
    value operator!=(value const& a, value const& b) { return compare(NEOS_PY_NE, a, b); }
    value operator<(value const& a, value const& b) { return compare(NEOS_PY_LT, a, b); }
    value operator<=(value const& a, value const& b) { return compare(NEOS_PY_LE, a, b); }
    value operator>(value const& a, value const& b) { return compare(NEOS_PY_GT, a, b); }
    value operator>=(value const& a, value const& b) { return compare(NEOS_PY_GE, a, b); }
    bool is(value const& a, value const& b) { return a.get() == b.get(); }

    object* guard(object* (*aFunction)(object*, object*), object* aArgs, object* aKwargs)
    {
        try
        {
            auto const result = aFunction(aArgs, aKwargs);
            if (result != nullptr && failed())
            {
                ctx->release(result);
                return nullptr;
            }
            return result;
        }
        catch (raised const&)
        {
            if (!failed())
                error("SystemError", "error return without exception set");
            return nullptr;
        }
        catch (std::exception const& e)
        {
            return error("SystemError", e.what());
        }
    }
}
