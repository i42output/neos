/*
  functions.cpp

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

// Functions: the program's functions (closures), arguments objects and the Function built-ins

#include <algorithm>
#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        // (the closure flags (see the compiler's closure))
        enum : std::int32_t
        {
            ClosureStrict = 0x1,
            ClosureConstructor = 0x2,
            ClosureArrow = 0x4,
            ClosureMethod = 0x8,
            ClosureGenerator = 0x10,
            ClosureAsync = 0x20,
            ClosureClassConstructor = 0x40,
            ClosureDerived = 0x80,
            ClosureGetter = 0x100,
            ClosureSetter = 0x200
        };
    }

    value make_closure(std::uint32_t aNumber, std::uint32_t aIndex, value aEnvironment, std::int32_t aFlags, value aName, std::uint32_t aLength, value aSource)
    {
        auto const& r = the_realm();
        bool const generator = (aFlags & ClosureGenerator) != 0;
        bool const async = (aFlags & ClosureAsync) != 0;
        value prototype = r.functionPrototype;
        if (generator && async)
            prototype = r.asyncGeneratorFunctionPrototype;
        else if (generator)
            prototype = r.generatorFunctionPrototype;
        else if (async)
            prototype = r.asyncFunctionPrototype;
        auto& f = the_heap().make<function_object>(prototype, function_kind::Compiled);
        f.functionRealm = &the_realm();
        f.program = current_program();
        f.index = aIndex;
        f.nativeId = aNumber;
        f.environment = aEnvironment;
        f.strict = (aFlags & ClosureStrict) != 0;
        f.isConstructor = (aFlags & ClosureConstructor) != 0;
        f.derived = (aFlags & ClosureDerived) != 0;
        f.sourceText = aSource;
        if ((aFlags & ClosureClassConstructor) != 0)
            f.flavour = function_flavour::ClassConstructor;
        else if (generator && async)
            f.flavour = function_flavour::AsyncGenerator;
        else if (generator)
            f.flavour = function_flavour::Generator;
        else if (async)
            f.flavour = (aFlags & ClosureArrow) != 0 ? function_flavour::AsyncArrow : function_flavour::Async;
        else if ((aFlags & ClosureArrow) != 0)
            f.flavour = function_flavour::Arrow;
        else if ((aFlags & (ClosureGetter | ClosureSetter)) != 0)
            f.flavour = function_flavour::Accessor;
        else if ((aFlags & ClosureMethod) != 0)
            f.flavour = function_flavour::Method;
        auto const& n = the_names();
        f.properties.add(n.length, number(aLength), Undefined, AttributeConfigurable);
        f.properties.add(n.name, aName, Undefined, AttributeConfigurable);
        if (!f.strict && f.flavour == function_flavour::Normal)
        {
            // (a sloppy function's caller and arguments (the implementation-defined extension other engines have): its caller is
            // undefined (its caller isn't tracked: test262's "extension not supported"), its arguments null)
            f.properties.add(n.caller, Undefined, Undefined, 0u);
            f.properties.add(n.arguments, Null, Undefined, 0u);
        }
        auto const result = value_of(f);
        if (generator)
        {
            auto const p = make_object(async ? r.asyncGeneratorPrototype : r.generatorPrototype);
            f.properties.add(n.prototype, p, Undefined, AttributeWritable);
        }
        else if (f.isConstructor && f.flavour != function_flavour::ClassConstructor)
        {
            auto const p = make_object();
            obj(p).properties.add(n.constructor, result, Undefined, AttributesHidden);
            f.properties.add(n.prototype, p, Undefined, AttributeWritable);
        }
        return result;
    }

    // --- arguments objects

    namespace
    {
        // (a mapped arguments object: its indices below the number of parameters are the parameters' bindings (slots of the
        // function's environment) until they are deleted or redefined)
        class arguments_object : public object
        {
        public:
            arguments_object(value aPrototype) : object{ aPrototype, object_class::Arguments } {}
        public:
            void trace(tracer& aTracer) override
            {
                object::trace(aTracer);
                aTracer.mark(env);
            }
        public:
            bool mapped(value aKey, std::uint32_t& aSlot) const
            {
                auto const index = array_index(aKey);
                if (!index || *index >= map.size() || !map[*index])
                    return false;
                aSlot = *map[*index];
                return true;
            }
            value read(std::uint32_t aSlot) const { return cell_of<environment>(env).slots[aSlot]; }
            void write(std::uint32_t aSlot, value aValue) { cell_of<environment>(env).slots[aSlot] = aValue; }
            std::optional<property_descriptor> get_own_property(value aKey) override
            {
                auto desc = ordinary_get_own_property(aKey);
                if (!desc)
                    return desc;
                std::uint32_t slot;
                if (mapped(aKey, slot))
                    desc->valueField = read(slot);
                return desc;
            }
            bool define_own_property(value aKey, property_descriptor const& aDescriptor) override
            {
                std::uint32_t slot;
                bool const isMapped = mapped(aKey, slot);
                auto newDescriptor = aDescriptor;
                if (isMapped && aDescriptor.is_data() && !aDescriptor.valueField && aDescriptor.writable && !*aDescriptor.writable)
                    newDescriptor.valueField = read(slot);
                if (!ordinary_define_own_property(aKey, newDescriptor))
                    return false;
                if (isMapped)
                {
                    if (aDescriptor.is_accessor())
                        unmap(aKey);
                    else
                    {
                        if (aDescriptor.valueField)
                            write(slot, *aDescriptor.valueField);
                        if (aDescriptor.writable && !*aDescriptor.writable)
                            unmap(aKey);
                    }
                }
                return true;
            }
            value get(value aKey, value aReceiver) override
            {
                std::uint32_t slot;
                if (mapped(aKey, slot))
                    return read(slot);
                return ordinary_get(aKey, aReceiver);
            }
            bool set(value aKey, value aValue, value aReceiver) override
            {
                std::uint32_t slot;
                bool isMapped = aReceiver == value_of(*this) && mapped(aKey, slot);
                if (isMapped)
                    write(slot, aValue);
                return ordinary_set(aKey, aValue, aReceiver);
            }
            bool delete_property(value aKey) override
            {
                std::uint32_t slot;
                bool const isMapped = mapped(aKey, slot);
                bool const result = ordinary_delete(aKey);
                if (result && isMapped)
                    unmap(aKey);
                return result;
            }
            bool has_property(value aKey) override
            {
                if (properties.find(aKey) != nullptr)
                    return true;
                auto const parent = get_prototype_of();
                return parent != Null && obj(parent).has_property(aKey);
            }
        private:
            void unmap(value aKey)
            {
                if (auto const index = array_index(aKey); index && *index < map.size())
                    map[*index].reset();
            }
        public:
            value env = Undefined;
            std::vector<std::optional<std::uint32_t>> map;
            std::uint32_t argumentCount = 0u;  ///< (the number of arguments passed)
        };
    }

    value make_arguments_object(value aCallee, std::span<value const> aArguments, bool aMapped)
    {
        (void)aMapped;
        auto const& r = the_realm();
        auto const& n = the_names();
        auto& a = the_heap().make<arguments_object>(r.objectPrototype);
        a.argumentCount = static_cast<std::uint32_t>(aArguments.size());
        for (std::size_t i = 0u; i < aArguments.size(); ++i)
            a.properties.add(index_key(i), aArguments[i], Undefined, AttributesDefault);
        a.properties.add(n.length, number(static_cast<double>(aArguments.size())), Undefined, AttributesHidden);
        a.properties.add(r.symIterator, r.arrayProtoValues, Undefined, AttributesHidden);
        if (!aMapped)
            a.properties.add(n.callee, r.throwTypeError, r.throwTypeError, AttributeAccessor);
        else
            a.properties.add(n.callee, aCallee, Undefined, AttributesHidden);
        return value_of(a);
    }

    value map_arguments(value aArguments, value aEnvironment, std::uint32_t aIndex, std::span<std::uint32_t const> aSlots)
    {
        // (the elements from aIndex (those below the number of arguments passed) are the slots aSlots of the environment)
        auto& a = static_cast<arguments_object&>(obj(aArguments));
        a.env = aEnvironment;
        auto const length = a.argumentCount;
        for (std::uint32_t i = 0u; i < aSlots.size() && aIndex + i < length; ++i)
        {
            if (a.map.size() <= aIndex + i)
                a.map.resize(aIndex + i + 1u);
            a.map[aIndex + i] = aSlots[i];
        }
        return aArguments;
    }

    // --- the Function built-ins

    namespace
    {
        value function_prototype_call(value aThis, arguments aArguments, value, function_object&)
        {
            if (!is_callable(aThis))
                throw_type_error("Function.prototype.call called on a value that is not a function");
            return call(aThis, argument(aArguments, 0u), aArguments.empty() ? aArguments : aArguments.subspan(1u));
        }

        value function_prototype_apply(value aThis, arguments aArguments, value, function_object&)
        {
            if (!is_callable(aThis))
                throw_type_error("Function.prototype.apply was called on a value that is not a function");
            auto const list = argument(aArguments, 1u);
            if (is_nullish(list))
                return call(aThis, argument(aArguments, 0u));
            auto const values = create_list_from_array_like(list);
            return call(aThis, argument(aArguments, 0u), values);
        }

        value function_prototype_bind(value aThis, arguments aArguments, value, function_object&)
        {
            if (!is_callable(aThis))
                throw_type_error("Bind must be called on a function");
            auto& target = obj(aThis);
            auto& f = the_heap().make<function_object>(target.get_prototype_of(), function_kind::Bound);
            f.boundTarget = aThis;
            f.boundThis = argument(aArguments, 0u);
            if (aArguments.size() > 1u)
                f.boundArguments.assign(aArguments.begin() + 1, aArguments.end());
            f.isConstructor = target.constructor();
            f.strict = true;
            auto const result = value_of(f);
            // (its length: the target's length less the bound arguments; its name: "bound " and the target's name)
            double length = 0.0;
            if (has_own_property(aThis, the_names().length))
            {
                auto const targetLength = get(aThis, the_names().length);
                if (is_number(targetLength))
                {
                    auto const l = as_number(targetLength);
                    if (l == std::numeric_limits<double>::infinity())
                        length = l;
                    else if (l != -std::numeric_limits<double>::infinity())
                    {
                        auto const integer = std::isnan(l) ? 0.0 : std::trunc(l);
                        length = std::max(0.0, integer - static_cast<double>(f.boundArguments.size()));
                    }
                }
            }
            f.properties.add(the_names().length, number(length), Undefined, AttributeConfigurable);
            auto targetName = get(aThis, the_names().name);
            if (!is_string(targetName))
                targetName = the_names().empty;
            f.properties.add(the_names().name, concat(ascii("bound "), targetName), Undefined, AttributeConfigurable);
            return result;
        }

        value function_prototype_to_string(value aThis, arguments, value, function_object&)
        {
            auto* f = as_function(aThis);
            if (f == nullptr)
            {
                if (is_callable(aThis))
                    return ascii("function () { [native code] }");
                throw_type_error("Function.prototype.toString requires that 'this' be a Function");
            }
            if (f->kind == function_kind::Compiled && is_string(f->sourceText))
                return f->sourceText;
            // (NativeFunction syntax: function [get|set] [PropertyName] (): a name that is not a property name (e.g. "bound f", "$&") is
            // left out)
            auto const* name = f->own(the_names().name);
            std::u16string text = u"function ";
            if (name != nullptr && !name->accessor() && is_string(name->data))
            {
                std::u16string n{ str(name->data) };
                if (n.starts_with(u"get ") || n.starts_with(u"set "))
                {
                    text += n.substr(0u, 4u);
                    n = n.substr(4u);
                }
                bool valid = !n.empty();
                if (valid && n.front() == u'[' && n.back() == u']')
                    ;
                else
                    for (std::size_t i = 0u; i < n.size() && valid; ++i)
                    {
                        auto const c = n[i];
                        valid = c == u'$' || c == u'_' || (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c >= 0x80u ||
                            (i != 0u && c >= u'0' && c <= u'9');
                    }
                if (valid)
                    text += n;
            }
            text += u"() { [native code] }";
            return make_string(text);
        }

        value function_prototype_has_instance(value aThis, arguments aArguments, value, function_object&)
        {
            return boolean(ordinary_has_instance(aThis, argument(aArguments, 0u)));
        }

        value function_prototype(value, arguments, value, function_object&)
        {
            return Undefined;
        }

        value throw_type_error_function(value, arguments, value, function_object&)
        {
            throw_type_error("'caller', 'callee', and 'arguments' properties may not be accessed on strict mode functions or the arguments objects for calls to them");
        }

        value function_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            return create_dynamic_function(aArguments, aNewTarget, 0);
        }
    }

    void init_function(realm& aRealm)
    {
        auto& fp = obj(aRealm.functionPrototype);
        auto& f = static_cast<function_object&>(fp);
        f.native = function_prototype;
        f.put(the_names().length, number(0.0), AttributeConfigurable);
        f.put(the_names().name, the_names().empty, AttributeConfigurable);
        auto const p = aRealm.functionPrototype;
        method(p, "apply", 2u, function_prototype_apply);
        method(p, "bind", 1u, function_prototype_bind);
        method(p, "call", 1u, function_prototype_call);
        method(p, "toString", 0u, function_prototype_to_string);
        method(p, aRealm.symHasInstance, 1u, function_prototype_has_instance, 0u);
        // (%ThrowTypeError%: frozen, nameless)
        aRealm.throwTypeError = make_native(the_names().empty, 0u, throw_type_error_function);
        auto& t = obj(aRealm.throwTypeError);
        t.own(the_names().length)->attributes = 0u;
        t.own(the_names().name)->attributes = 0u;
        t.extensible = false;
        fp.put_accessor(the_names().caller, aRealm.throwTypeError, aRealm.throwTypeError, AttributeConfigurable);
        fp.put_accessor(the_names().arguments, aRealm.throwTypeError, aRealm.throwTypeError, AttributeConfigurable);
        // (the Function constructor)
        aRealm.function = make_native("Function", 1u, function_constructor, true);
        obj(aRealm.function).put(the_names().prototype, p, 0u);
        fp.put(the_names().constructor, aRealm.function);
        obj(aRealm.global).put(ascii("Function"), aRealm.function);
    }
}
