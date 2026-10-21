/*
  builtins_error.cpp

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

// Error and the native errors (EvalError, RangeError, ReferenceError, SyntaxError, TypeError, URIError), AggregateError and
// SuppressedError

#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        // (the error constructors: nativeId is the realm's prototype (an index of the table below))
        value realm::* const sPrototypes[] = { &realm::errorPrototype, &realm::evalErrorPrototype, &realm::rangeErrorPrototype,
            &realm::referenceErrorPrototype, &realm::syntaxErrorPrototype, &realm::typeErrorPrototype, &realm::uriErrorPrototype,
            &realm::aggregateErrorPrototype, &realm::suppressedErrorPrototype };

        void install_error_cause(value aError, value aOptions)
        {
            if (is_object(aOptions) && has_property(aOptions, the_names().cause))
                obj(aError).put(the_names().cause, get(aOptions, the_names().cause), AttributesHidden);
        }

        value create_error(value aNewTarget, function_object& aCallee)
        {
            auto const newTarget = aNewTarget == Undefined ? value_of(aCallee) : aNewTarget;
            auto const prototype = get_prototype_from_constructor(newTarget, sPrototypes[aCallee.nativeId]);
            return value_of(the_heap().make<object>(prototype, object_class::Error));
        }

        value error_constructor(value, arguments aArguments, value aNewTarget, function_object& aCallee)
        {
            auto const o = create_error(aNewTarget, aCallee);
            auto const message = argument(aArguments, 0u);
            if (message != Undefined)
                obj(o).put(the_names().message, to_string(message), AttributesHidden);
            install_error_cause(o, argument(aArguments, 1u));
            return o;
        }

        value aggregate_error_constructor(value, arguments aArguments, value aNewTarget, function_object& aCallee)
        {
            auto const o = create_error(aNewTarget, aCallee);
            auto const message = argument(aArguments, 1u);
            if (message != Undefined)
                obj(o).put(the_names().message, to_string(message), AttributesHidden);
            install_error_cause(o, argument(aArguments, 2u));
            auto const errors = iterate_to_list(argument(aArguments, 0u));
            obj(o).put(the_names().errors, create_array_from_list(errors), AttributesHidden);
            return o;
        }

        value suppressed_error_constructor(value, arguments aArguments, value aNewTarget, function_object& aCallee)
        {
            auto const o = create_error(aNewTarget, aCallee);
            auto const message = argument(aArguments, 2u);
            if (message != Undefined)
                obj(o).put(the_names().message, to_string(message), AttributesHidden);
            obj(o).put(the_names().error, argument(aArguments, 0u), AttributesHidden);
            obj(o).put(the_names().suppressed, argument(aArguments, 1u), AttributesHidden);
            return o;
        }

        value error_prototype_to_string(value aThis, arguments, value, function_object&)
        {
            if (!is_object(aThis))
                throw_type_error("Error.prototype.toString called on non-object");
            auto name = get(aThis, the_names().name);
            name = name == Undefined ? ascii("Error") : to_string(name);
            auto message = get(aThis, the_names().message);
            message = message == Undefined ? the_names().empty : to_string(message);
            if (string_length(name) == 0u)
                return message;
            if (string_length(message) == 0u)
                return name;
            return concat(concat(name, ascii(": ")), message);
        }
    }

    void init_error(realm& aRealm)
    {
        auto const& n = the_names();
        auto const make = [&](char const* aName, std::uint32_t aIndex, value aParentPrototype, value aParentConstructor, native_function aConstructor,
            std::uint32_t aLength)
        {
            auto const prototype = make_object(aParentPrototype);
            aRealm.*sPrototypes[aIndex] = prototype;
            auto const c = make_native(ascii(aName), aLength, aConstructor, true, aParentConstructor);
            static_cast<function_object&>(obj(c)).nativeId = aIndex;
            obj(c).put(n.prototype, prototype, 0u);
            obj(prototype).put(n.constructor, c);
            obj(prototype).put(n.name, ascii(aName));
            obj(prototype).put(n.message, n.empty);
            obj(aRealm.global).put(ascii(aName), c);
            return c;
        };
        aRealm.error = make("Error", 0u, aRealm.objectPrototype, aRealm.functionPrototype, error_constructor, 1u);
        method(aRealm.errorPrototype, "toString", 0u, error_prototype_to_string);
        make("EvalError", 1u, aRealm.errorPrototype, aRealm.error, error_constructor, 1u);
        make("RangeError", 2u, aRealm.errorPrototype, aRealm.error, error_constructor, 1u);
        make("ReferenceError", 3u, aRealm.errorPrototype, aRealm.error, error_constructor, 1u);
        make("SyntaxError", 4u, aRealm.errorPrototype, aRealm.error, error_constructor, 1u);
        make("TypeError", 5u, aRealm.errorPrototype, aRealm.error, error_constructor, 1u);
        make("URIError", 6u, aRealm.errorPrototype, aRealm.error, error_constructor, 1u);
        make("AggregateError", 7u, aRealm.errorPrototype, aRealm.error, aggregate_error_constructor, 2u);
        make("SuppressedError", 8u, aRealm.errorPrototype, aRealm.error, suppressed_error_constructor, 3u);
        // (the native errors' prototypes have no message of their own but their own name)
    }
}
