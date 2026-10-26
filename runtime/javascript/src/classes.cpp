/*
  classes.cpp

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

// Classes: ClassDefinitionEvaluation's runtime side (the constructor, its fields and private methods, static elements),
// InitializeInstanceElements, super(...) and private names (#x: symbols that are never values the program sees)

#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        struct private_method
        {
            value key = Undefined;      ///< (a private name)
            value method = Undefined;   ///< (a method; or undefined if an accessor)
            value getter = Undefined;
            value setter = Undefined;
            bool accessor = false;
        };

        struct class_element
        {
            bool block = false;         ///< (a static block (else a field))
            value key = Undefined;      ///< (a field's name: a property key or a private name)
            value function = Undefined; ///< (a field's initializer (undefined if none) or a static block's function)
        };

        // (a class constructor's [[Fields]] and [[PrivateMethods]], and (while the class is being defined) its static elements)
        class class_definition : public cell
        {
        public:
            class_definition() : cell{ cell_type::Internal } {}
        public:
            void trace(tracer& aTracer) override
            {
                for (auto const* methods : { &instanceMethods, &staticMethods })
                    for (auto const& m : *methods)
                    {
                        aTracer.mark(m.key);
                        aTracer.mark(m.method);
                        aTracer.mark(m.getter);
                        aTracer.mark(m.setter);
                    }
                for (auto const* elements : { &instanceFields, &staticElements })
                    for (auto const& e : *elements)
                    {
                        aTracer.mark(e.key);
                        aTracer.mark(e.function);
                    }
            }
        public:
            std::vector<private_method> instanceMethods;
            std::vector<private_method> staticMethods;
            std::vector<class_element> instanceFields;
            std::vector<class_element> staticElements;
        };

        class_definition& definition_of(function_object& aConstructor)
        {
            return cell_of<class_definition>(aConstructor.fields);
        }

        function_object& function_of(value aFunction)
        {
            return static_cast<function_object&>(obj(aFunction));
        }

        bool is_private_name(value aKey)
        {
            return is_symbol(aKey) && cell_of<symbol>(aKey).privateName;
        }

        std::string private_description(value aName)
        {
            return utf8(cell_of<symbol>(aName).description);
        }

        std::pair<value, property>* private_element_find(object& aObject, value aName)
        {
            if (auto* const extra = aObject.extra_if())
                for (auto& e : extra->privateElements)
                    if (e.first == aName)
                        return &e;
            return nullptr;
        }

        void private_field_add(value aObject, value aName, value aValue)
        {
            auto& o = obj(aObject);
            if (private_element_find(o, aName) != nullptr)
                throw_type_error("Cannot initialize " + private_description(aName) + " twice on the same object");
            property p;
            p.key = aName;
            p.data = aValue;
            p.attributes = AttributeWritable;
            o.private_elements_of().emplace_back(aName, p);
        }

        void private_method_add(value aObject, private_method const& aMethod)
        {
            auto& o = obj(aObject);
            if (private_element_find(o, aMethod.key) != nullptr)
                throw_type_error("Cannot initialize private methods of class " + private_description(aMethod.key) + " twice on the same object");
            property p;
            p.key = aMethod.key;
            if (aMethod.accessor)
            {
                p.data = aMethod.getter;
                p.setter = aMethod.setter;
                p.attributes = AttributeAccessor;
            }
            else
                p.data = aMethod.method;
            o.private_elements_of().emplace_back(aMethod.key, p);
        }

        void define_field(value aReceiver, class_element const& aField)
        {
            value initial = Undefined;
            if (aField.function != Undefined)
            {
                // (the initializer is passed the field's name (for the names of anonymous functions))
                value const argument[] = { aField.key };
                initial = call(aField.function, aReceiver, argument);
            }
            if (is_private_name(aField.key))
                private_field_add(aReceiver, aField.key, initial);
            else
                create_data_property_or_throw(aReceiver, aField.key, initial);
        }

        // (a class without a constructor: its default constructor)
        value default_constructor(value, arguments aArguments, value aNewTarget, function_object& aSelf)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Class constructor " + utf8(to_string(get(value_of(aSelf), the_names().name))) + " cannot be invoked without 'new'");
            value result;
            if (aSelf.derived)
            {
                auto const parent = aSelf.get_prototype_of();
                if (!is_constructor(parent))
                    throw_type_error("Super constructor " + describe(parent) + " of anonymous class is not a constructor");
                result = construct(parent, aArguments, aNewTarget);
            }
            else
                result = ordinary_create_from_constructor(aNewTarget, &realm::objectPrototype);
            initialize_instance_elements(result, value_of(aSelf));
            return result;
        }
    }

    void initialize_instance_elements(value aObject, value aConstructor)
    {
        auto& f = function_of(aConstructor);
        if (f.fields == Undefined)
            return;
        auto& d = definition_of(f);
        for (auto const& m : d.instanceMethods)
            private_method_add(aObject, m);
        for (auto const& field : d.instanceFields)
            define_field(aObject, field);
    }

    value make_private_name(value aDescription)
    {
        auto const result = make_symbol(aDescription);
        cell_of<symbol>(result).privateName = true;
        return result;
    }

    value class_prototype(value aHeritage)
    {
        auto const& r = the_realm();
        if (aHeritage == Empty)
            return make_object(r.objectPrototype);
        if (aHeritage == Null)
            return make_object(Null);
        if (!is_constructor(aHeritage))
            throw_type_error("Class extends value " + describe(aHeritage) + " is not a constructor or null");
        auto const parent = get(aHeritage, the_names().prototype);
        if (!is_object(parent) && parent != Null)
            throw_type_error("Class extends value does not have valid prototype property " + describe(parent));
        return make_object(parent);
    }

    value class_constructor(value aConstructor, value aPrototype, value aHeritage, value aName, value aSource)
    {
        auto const& r = the_realm();
        bool const derived = aHeritage != Empty;
        auto const parent = (aHeritage == Empty || aHeritage == Null) ? r.functionPrototype : aHeritage;
        value f = aConstructor;
        if (f == Undefined)
        {
            f = make_native(aName, 0u, default_constructor, true, parent);
            function_of(f).flavour = function_flavour::ClassConstructor;
        }
        else
        {
            obj(f).proto = parent;
            set_function_name(f, aName);
        }
        auto& fn = function_of(f);
        fn.isConstructor = true;
        fn.derived = derived;
        fn.homeObject = aPrototype;
        fn.sourceText = aSource;
        fn.fields = value_of(the_heap().make<class_definition>());
        obj(f).put(the_names().prototype, aPrototype, 0u);
        obj(aPrototype).put(the_names().constructor, f, AttributeWritable | AttributeConfigurable);
        return f;
    }

    void class_private_method(value aConstructor, value aHome, value aName, value aFunction, std::int32_t aKind, bool aStatic)
    {
        // (aKind: 0 method, 1 getter, 2 setter)
        auto& d = definition_of(function_of(aConstructor));
        function_of(aFunction).homeObject = aHome;
        set_function_name(aFunction, cell_of<symbol>(aName).description, aKind == 1 ? "get" : aKind == 2 ? "set" : nullptr);
        auto& container = aStatic ? d.staticMethods : d.instanceMethods;
        for (auto& existing : container)
            if (existing.key == aName)
            {
                if (aKind == 1)
                    existing.getter = aFunction;
                else
                    existing.setter = aFunction;
                return;
            }
        private_method m;
        m.key = aName;
        if (aKind == 0)
            m.method = aFunction;
        else
        {
            m.accessor = true;
            (aKind == 1 ? m.getter : m.setter) = aFunction;
        }
        container.push_back(m);
    }

    void class_field(value aConstructor, value aHome, value aName, value aInitializer, bool aStatic)
    {
        auto& d = definition_of(function_of(aConstructor));
        if (aInitializer != Undefined)
            function_of(aInitializer).homeObject = aHome;
        (aStatic ? d.staticElements : d.instanceFields).push_back(class_element{ false, aName, aInitializer });
    }

    void class_static_block(value aConstructor, value aFunction)
    {
        auto& d = definition_of(function_of(aConstructor));
        function_of(aFunction).homeObject = aConstructor;
        d.staticElements.push_back(class_element{ true, Undefined, aFunction });
    }

    value class_finish(value aConstructor)
    {
        auto& d = definition_of(function_of(aConstructor));
        for (auto const& m : d.staticMethods)
            private_method_add(aConstructor, m);
        rooted_vector<class_element> const elements{ d.staticElements.begin(), d.staticElements.end() };
        d.staticElements.clear();
        for (auto const& e : elements)
        {
            if (e.block)
                call(e.function, aConstructor);
            else
                define_field(aConstructor, e);
        }
        return aConstructor;
    }

    value super_constructor(value aConstructor)
    {
        // (GetSuperConstructor: before the arguments are evaluated)
        return obj(aConstructor).get_prototype_of();
    }

    value super_construct(value aParent, value aNewTarget, value aArguments)
    {
        // (the parent (see super_constructor) is a constructor: checked after the arguments are evaluated)
        auto const parent = aParent;
        if (!is_constructor(parent))
            throw_type_error("Super constructor " + describe(parent) + " of anonymous class is not a constructor");
        auto const list = create_list_from_array_like(aArguments);
        return construct(parent, list, aNewTarget);
    }

    value private_get(value aObject, value aName)
    {
        if (!is_object(aObject))
            throw_type_error("Cannot read private member " + private_description(aName) + " from " + describe(aObject));
        auto* e = private_element_find(obj(aObject), aName);
        if (e == nullptr)
            throw_type_error("Cannot read private member " + private_description(aName) + " from an object whose class did not declare it");
        auto const& p = e->second;
        if (!p.accessor())
            return p.data;
        if (p.data == Undefined)
            throw_type_error("'" + private_description(aName) + "' was defined without a getter");
        return call(p.data, aObject);
    }

    void private_set(value aObject, value aName, value aValue)
    {
        if (!is_object(aObject))
            throw_type_error("Cannot write private member " + private_description(aName) + " to " + describe(aObject));
        auto* e = private_element_find(obj(aObject), aName);
        if (e == nullptr)
            throw_type_error("Cannot write private member " + private_description(aName) + " to an object whose class did not declare it");
        auto& p = e->second;
        if (p.accessor())
        {
            if (p.setter == Undefined)
                throw_type_error("'" + private_description(aName) + "' was defined without a setter");
            value const argument[] = { aValue };
            call(p.setter, aObject, argument);
            return;
        }
        if (!p.writable())
            throw_type_error("Private method " + private_description(aName) + " is not writable");
        p.data = aValue;
    }

    bool private_in(value aName, value aObject)
    {
        if (!is_object(aObject))
            throw_type_error("Cannot use 'in' operator to search for '" + private_description(aName) + "' in " + describe(aObject));
        return private_element_find(obj(aObject), aName) != nullptr;
    }
}
