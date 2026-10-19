/*
  structseq.cpp

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

  Portions of this file are derived from CPython 3.13 (Objects/structseq.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// Struct sequences (CPython's structseq: e.g. time.struct_time, os.stat_result, sys.version_info): tuples whose items
// are also named attributes (and which can have named attributes that are not items)

#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            struct structseq_object
            {
                header h;
                structseq_descriptor const* descriptor;
                std::vector<object*> values;    ///< (the items, then the other attributes)
                ~structseq_object() { for (auto v : values) release(v); }
            };

            structseq_object& as_structseq(object* aObject) { return *reinterpret_cast<structseq_object*>(aObject); }

            // (the items as a tuple: a new reference)
            object* items_of(object* aSelf)
            {
                auto const& s = as_structseq(aSelf);
                std::vector<object*> items{ s.values.begin(), s.values.begin() + static_cast<std::ptrdiff_t>(s.descriptor->visible) };
                for (auto i : items)
                    retain(i);
                return make_tuple(std::move(items));
            }

            bool is_structseq(object* aObject);
        }

        structseq_descriptor const& structseq_type(std::string const& aQualifiedName, std::vector<std::string> const& aFields, std::size_t aVisible)
        {
            static std::mutex sMutex;
            static std::deque<structseq_descriptor> sDescriptors;
            std::lock_guard<std::mutex> lock{ sMutex };
            for (auto const& d : sDescriptors)
                if (d.qualifiedName == aQualifiedName)
                    return d;
            auto& d = sDescriptors.emplace_back();
            d.qualifiedName = aQualifiedName;
            auto const dot = aQualifiedName.rfind('.');
            d.name = dot == std::string::npos ? aQualifiedName : aQualifiedName.substr(dot + 1u);
            d.fields = aFields;
            d.visible = aVisible;
            d.type = make_type(d.qualifiedName.c_str(), d.name.c_str(), retain_object, release_object<structseq_object>);
            d.type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                auto const& s = as_structseq(aSelf);
                std::string text = s.descriptor->qualifiedName + "(";
                for (std::size_t i = 0u; i < s.descriptor->visible; ++i)
                {
                    std::string value;
                    if (!repr_of(s.values[i], value))
                        return nullptr;
                    text += (i != 0u ? ", " : "") + s.descriptor->fields[i] + "=" + value;
                }
                return make_str(text + ")");
            };
            d.type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                auto const& s = as_structseq(aSelf);
                // (the last field of the name: an item can have the name of a later attribute (e.g. os.stat_result's st_atime))
                for (std::size_t i = std::min(s.descriptor->fields.size(), s.values.size()); i-- > 0u;)
                    if (s.descriptor->fields[i] == aName)
                    {
                        retain(s.values[i]);
                        return s.values[i];
                    }
                if (std::strcmp(aName, "n_sequence_fields") == 0)
                    return make_int(static_cast<long long>(s.descriptor->visible));
                if (std::strcmp(aName, "n_fields") == 0)
                    return make_int(static_cast<long long>(s.descriptor->fields.size()));
                // (a tuple's methods)
                ref const tuple{ items_of(aSelf) };
                return getattr(tuple.get(), aName);
            };
            d.type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object*
            {
                ref const tuple{ items_of(aSelf) };
                return getitem(tuple.get(), aKey);
            };
            d.type.length = [](neos_py_context*, object* aSelf) -> long long { return static_cast<long long>(as_structseq(aSelf).descriptor->visible); };
            d.type.iter = [](neos_py_context*, object* aSelf) -> object*
            {
                ref const tuple{ items_of(aSelf) };
                return iter(tuple.get());
            };
            d.type.contains = [](neos_py_context*, object* aSelf, object* aItem) -> int
            {
                ref const tuple{ items_of(aSelf) };
                return contains(tuple.get(), aItem);
            };
            d.type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
            {
                ref const a{ is_structseq(aLhs) ? items_of(aLhs) : (retain(aLhs), aLhs) };
                ref const b{ is_structseq(aRhs) ? items_of(aRhs) : (retain(aRhs), aRhs) };
                if (kind_of(a.get()) != NEOS_PY_TUPLE || kind_of(b.get()) != NEOS_PY_TUPLE)
                    return not_implemented();
                return compare(aOperation, a.get(), b.get());
            };
            d.type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int) -> object*
            {
                ref const a{ is_structseq(aLhs) ? items_of(aLhs) : (retain(aLhs), aLhs) };
                ref const b{ is_structseq(aRhs) ? items_of(aRhs) : (retain(aRhs), aRhs) };
                return binary(aOperation, a.get(), b.get(), false);
            };
            d.type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
            {
                ref const tuple{ items_of(aSelf) };
                return hash_of(tuple.get(), *aResult) ? 0 : -1;
            };
            return d;
        }

        namespace
        {
            bool is_structseq(object* aObject)
            {
                auto const type = python_type(aObject);
                return type != nullptr && type->repr != nullptr && aObject->type->release == &release_object<structseq_object>;
            }
        }

        object* make_structseq(structseq_descriptor const& aDescriptor, std::vector<object*> aValues)
        {
            while (aValues.size() < aDescriptor.fields.size())
                aValues.push_back((retain(none()), none()));
            return &(new structseq_object{ { { &aDescriptor.type.base } }, &aDescriptor, std::move(aValues) })->h.base;
        }

        bool structseq_items(object* aObject, std::vector<object*>& aItems)
        {
            if (!is_structseq(aObject))
                return false;
            auto const& s = as_structseq(aObject);
            aItems.assign(s.values.begin(), s.values.begin() + static_cast<std::ptrdiff_t>(s.descriptor->visible));
            return true;
        }
    }
}
