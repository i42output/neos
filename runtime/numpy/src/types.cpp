/*
  types.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// the Python types of the NumPy plugin's objects: ndarray, the scalar types, dtype, ufunc and the classes (np.float64, ...)

#include <algorithm>
#include <cmath>
#include <map>
#include "numpy.hpp"
#include "cast.hpp"
#include "ufunc.hpp"
#include "py.hpp"

namespace neos::numpy
{
    int array_set_flat(object* aArray, object* aValue);
    extern void (*const sRetainArray)(neos_rt_object*);
    extern void (*const sReleaseArray)(neos_rt_object*);

    namespace
    {
        neos_py_type make_type(char const* aQualifiedName, char const* aName, void (*aRetain)(neos_rt_object*), void (*aRelease)(neos_rt_object*))
        {
            neos_py_type type{};
            type.base = neos_rt_object_type{ aQualifiedName, aRetain, aRelease };
            type.magic = NEOS_PY_TYPE_MAGIC;
            type.name = aName;
            return type;
        }

        void retain_static(neos_rt_object*) {}
        void release_static(neos_rt_object*) {}

        // --- ndarray and scalars

        object* array_repr_slot(neos_py_context*, object* aSelf)
        {
            auto const* a = as_array(aSelf);
            return make_str(a->scalar ? scalar_repr(*a) : array_repr(*a));
        }

        object* array_str_slot(neos_py_context*, object* aSelf)
        {
            auto const* a = as_array(aSelf);
            return make_str(a->scalar ? scalar_str(*a) : array_str(*a));
        }

        object* array_getattr_slot(neos_py_context*, object* aSelf, char const* aName)
        {
            return array_getattr(aSelf, aName);
        }

        int array_setattr_slot(neos_py_context*, object* aSelf, char const* aName, object* aValue)
        {
            auto* const a = as_array(aSelf);
            std::string_view const name{ aName };
            if (name == "shape" && !a->scalar)
            {
                shape_t shape;
                if (!to_shape(aValue, shape))
                    return -1;
                ref const args{ make_tuple({ (ctx->retain(aValue), aValue) }) };
                ref const reshaped{ method_call("reshape", aSelf, args.get(), nullptr) };
                if (!reshaped)
                    return -1;
                auto* const r = as_array(reshaped.get());
                if (r->data != a->data)
                {
                    error("AttributeError", "Incompatible shape for in-place modification. Use `.reshape()` to make a copy with the desired shape.");
                    return -1;
                }
                a->shape = r->shape;
                a->strides = r->strides;
                return 0;
            }
            if (name == "flat" && !a->scalar)
                return array_set_flat(aSelf, aValue);
            error("AttributeError", std::string{ "'numpy." } + (a->scalar ? info(a->type).scalarName : "ndarray") + "' object attribute '" + aName + "' is read-only");
            return -1;
        }

        object* array_getitem_slot(neos_py_context*, object* aSelf, object* aKey)
        {
            return array_getitem(aSelf, aKey);
        }

        int array_setitem_slot(neos_py_context*, object* aSelf, object* aKey, object* aValue)
        {
            return array_setitem(aSelf, aKey, aValue);
        }

        object* array_binary_slot(neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace)
        {
            return binary_operator(aOperation, aLhs, aRhs, aInplace != 0);
        }

        object* array_unary_slot(neos_py_context*, int aOperation, object* aSelf)
        {
            return unary_operator(aOperation, aSelf);
        }

        object* array_compare_slot(neos_py_context*, int aOperation, object* aLhs, object* aRhs)
        {
            return compare_operator(aOperation, aLhs, aRhs);
        }

        int array_truth_slot(neos_py_context*, object* aSelf)
        {
            auto const* a = as_array(aSelf);
            if (a->size() == 0)
            {
                error("ValueError", "The truth value of an empty array is ambiguous. Use `array.size > 0` to check that an array is not empty.");
                return -1;
            }
            if (a->size() > 1)
            {
                error("ValueError", "The truth value of an array with more than one element is ambiguous. Use a.any() or a.all()");
                return -1;
            }
            // (the single element)
            std::int64_t offset = 0;
            (void)offset;
            return read_as<bool>(a->pointer(), a->type) ? 1 : 0;
        }

        long long array_length_slot(neos_py_context*, object* aSelf)
        {
            auto const* a = as_array(aSelf);
            if (a->ndim() == 0)
            {
                error("TypeError", a->scalar ? std::string{ "object of type 'numpy." } + info(a->type).scalarName + "' has no len()" : std::string{ "len() of unsized object" });
                return -1;
            }
            return a->shape[0];
        }

        int array_contains_slot(neos_py_context*, object* aSelf, object* aItem)
        {
            ref const equal{ compare_operator(NEOS_PY_EQ, aSelf, aItem) };
            if (!equal)
                return -1;
            if (!is_array(equal.get()))
                return ctx->truth(ctx, equal.get());
            auto const* e = as_array(equal.get());
            bool any = false;
            iterate(e->shape, { e->pointer() }, { e->strides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                auto p = aPointers[0];
                for (std::int64_t i = 0; i < aCount; ++i, p += aStrides[0])
                    any = any || read_as<bool>(p, e->type);
            });
            return any ? 1 : 0;
        }

        int array_index_slot(neos_py_context*, object* aSelf, long long* aResult)
        {
            auto const* a = as_array(aSelf);
            if (a->ndim() != 0 || !(is_integer(a->type) || is_bool(a->type)))
            {
                error("TypeError", "only integer scalar arrays can be converted to a scalar index");
                return -1;
            }
            *aResult = read_as<std::int64_t>(a->pointer(), a->type);
            return 0;
        }

        int array_to_float_slot(neos_py_context*, object* aSelf, double* aResult)
        {
            auto const* a = as_array(aSelf);
            if (a->ndim() != 0)
            {
                error("TypeError", "only 0-dimensional arrays can be converted to Python scalars");
                return -1;
            }
            *aResult = read_as<double>(a->pointer(), a->type);
            return 0;
        }

        int array_to_int_slot(neos_py_context*, object* aSelf, long long* aResult)
        {
            auto const* a = as_array(aSelf);
            if (a->ndim() != 0)
            {
                error("TypeError", "only 0-dimensional arrays can be converted to Python scalars");
                return -1;
            }
            *aResult = read_as<std::int64_t>(a->pointer(), a->type);
            return 0;
        }

        // (iterating an ndarray: its first axis)
        object* array_iter_slot(neos_py_context*, object* aSelf)
        {
            auto const* a = as_array(aSelf);
            if (a->ndim() == 0)
                return error("TypeError", a->scalar ? std::string{ "'numpy." } + info(a->type).scalarName + "' object is not iterable" : std::string{ "iteration over a 0-d array" });
            std::vector<object*> items;
            for (std::int64_t i = 0; i < a->shape[0]; ++i)
            {
                ref const index{ make_int(i) };
                auto const item = array_getitem(aSelf, index.get());
                if (item == nullptr)
                {
                    for (auto x : items)
                        ctx->release(x);
                    return nullptr;
                }
                items.push_back(item);
            }
            ref const list{ make_list(std::move(items)) };
            return ctx->iter(ctx, list.get());
        }

        neos_py_type make_array_type(char const* aQualifiedName, char const* aName)
        {
            auto type = make_type(aQualifiedName, aName, sRetainArray, sReleaseArray);
            type.repr = array_repr_slot;
            type.str = array_str_slot;
            type.getattr = array_getattr_slot;
            type.setattr = array_setattr_slot;
            type.getitem = array_getitem_slot;
            type.setitem = array_setitem_slot;
            type.binary = array_binary_slot;
            type.unary = array_unary_slot;
            type.compare = array_compare_slot;
            type.truth = array_truth_slot;
            type.length = array_length_slot;
            type.iter = array_iter_slot;
            type.contains = array_contains_slot;
            type.index = array_index_slot;
            type.to_float = array_to_float_slot;
            type.to_int = array_to_int_slot;
            return type;
        }

        neos_py_type sArrayType = make_array_type("numpy.ndarray", "ndarray");
        neos_py_type sScalarTypes[] =
        {
            make_array_type("numpy.bool", "bool"),
            make_array_type("numpy.int8", "int8"),
            make_array_type("numpy.uint8", "uint8"),
            make_array_type("numpy.int16", "int16"),
            make_array_type("numpy.uint16", "uint16"),
            make_array_type("numpy.int32", "int32"),
            make_array_type("numpy.uint32", "uint32"),
            make_array_type("numpy.int64", "int64"),
            make_array_type("numpy.uint64", "uint64"),
            make_array_type("numpy.float16", "float16"),
            make_array_type("numpy.float32", "float32"),
            make_array_type("numpy.float64", "float64"),
            make_array_type("numpy.complex64", "complex64"),
            make_array_type("numpy.complex128", "complex128")
        };
    }

    neos_py_type const* array_type() { return &sArrayType; }
    neos_py_type const* scalar_type(dtype aType) { return &sScalarTypes[static_cast<int>(aType)]; }

    // --- dtype objects

    namespace
    {
        struct dtype_object
        {
            neos_rt_object base;
            dtype type;
        };

        object* dtype_repr_slot(neos_py_context*, object* aSelf);
        object* dtype_str_slot(neos_py_context*, object* aSelf);
        object* dtype_getattr_slot(neos_py_context*, object* aSelf, char const* aName);
        object* dtype_compare_slot(neos_py_context*, int aOperation, object* aLhs, object* aRhs);

        neos_py_type make_dtype_type()
        {
            auto type = make_type("numpy.dtype", "dtype", retain_static, release_static);
            type.repr = dtype_repr_slot;
            type.str = dtype_str_slot;
            type.getattr = dtype_getattr_slot;
            type.compare = dtype_compare_slot;
            return type;
        }

        neos_py_type sDtypeType = make_dtype_type();

        dtype_object sDtypes[] =
        {
            { { &sDtypeType.base }, dtype::Bool }, { { &sDtypeType.base }, dtype::Int8 }, { { &sDtypeType.base }, dtype::UInt8 },
            { { &sDtypeType.base }, dtype::Int16 }, { { &sDtypeType.base }, dtype::UInt16 }, { { &sDtypeType.base }, dtype::Int32 },
            { { &sDtypeType.base }, dtype::UInt32 }, { { &sDtypeType.base }, dtype::Int64 }, { { &sDtypeType.base }, dtype::UInt64 },
            { { &sDtypeType.base }, dtype::Float16 }, { { &sDtypeType.base }, dtype::Float32 }, { { &sDtypeType.base }, dtype::Float64 },
            { { &sDtypeType.base }, dtype::Complex64 }, { { &sDtypeType.base }, dtype::Complex128 }
        };

        dtype type_of_dtype_object(object* aObject)
        {
            return reinterpret_cast<dtype_object*>(aObject)->type;
        }

        object* dtype_repr_slot(neos_py_context*, object* aSelf)
        {
            return make_str(std::string{ "dtype('" } + info(type_of_dtype_object(aSelf)).name + "')");
        }

        object* dtype_str_slot(neos_py_context*, object* aSelf)
        {
            return make_str(info(type_of_dtype_object(aSelf)).name);
        }
    }

    object* dtype_object(dtype aType)
    {
        return &sDtypes[static_cast<int>(aType)].base;
    }

    // --- classes (np.float64, np.integer, np.ndarray, ...)

    namespace
    {
        // a class: a concrete scalar type, or an abstract one (whose types are those of a category)
        struct class_info
        {
            char const* name;
            std::optional<dtype> type;
            bool (*member)(dtype);
        };

        bool any_type(dtype) { return true; }
        bool number_type(dtype t) { return !is_bool(t); }
        bool integer_type(dtype t) { return is_integer(t); }
        bool signed_type(dtype t) { return is_signed(t); }
        bool unsigned_type(dtype t) { return is_unsigned(t); }
        bool inexact_type(dtype t) { return is_inexact(t); }
        bool floating_type(dtype t) { return is_float(t); }
        bool complex_type(dtype t) { return is_complex(t); }

        class_info const sClasses[] =
        {
            { "bool", dtype::Bool, nullptr }, { "int8", dtype::Int8, nullptr }, { "uint8", dtype::UInt8, nullptr }, { "int16", dtype::Int16, nullptr },
            { "uint16", dtype::UInt16, nullptr }, { "int32", dtype::Int32, nullptr }, { "uint32", dtype::UInt32, nullptr }, { "int64", dtype::Int64, nullptr },
            { "uint64", dtype::UInt64, nullptr }, { "float16", dtype::Float16, nullptr }, { "float32", dtype::Float32, nullptr },
            { "float64", dtype::Float64, nullptr }, { "complex64", dtype::Complex64, nullptr }, { "complex128", dtype::Complex128, nullptr },
            { "generic", std::nullopt, any_type }, { "number", std::nullopt, number_type }, { "integer", std::nullopt, integer_type },
            { "signedinteger", std::nullopt, signed_type }, { "unsignedinteger", std::nullopt, unsigned_type }, { "inexact", std::nullopt, inexact_type },
            { "floating", std::nullopt, floating_type }, { "complexfloating", std::nullopt, complex_type }, { "ndarray", std::nullopt, nullptr }
        };

        // (aliases of the classes)
        std::map<std::string, std::string, std::less<>> const sClassAliases =
        {
            { "bool_", "bool" }, { "byte", "int8" }, { "ubyte", "uint8" }, { "short", "int16" }, { "ushort", "uint16" }, { "intc", "int32" },
            { "uintc", "uint32" }, { "int_", "int64" }, { "intp", "int64" }, { "long", "int64" }, { "longlong", "int64" }, { "uint", "uint64" },
            { "uintp", "uint64" }, { "ulong", "uint64" }, { "ulonglong", "uint64" }, { "half", "float16" }, { "single", "float32" },
            { "double", "float64" }, { "float_", "float64" }, { "csingle", "complex64" }, { "cdouble", "complex128" }, { "complex_", "complex128" }
        };

        object* construct_scalar(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
        {
            auto const& c = *static_cast<class_info const*>(aData);
            if (!c.type)
            {
                if (std::string_view{ c.name } == "ndarray")
                {
                    arguments args{ "ndarray", aArgs, aKwargs, { "shape", "dtype" } };
                    if (!args.ok())
                        return nullptr;
                    shape_t shape;
                    if (!to_shape(args[0], shape))
                        return nullptr;
                    auto const t = args.has(1) ? parse_dtype(args[1]) : std::optional<dtype>{ dtype::Float64 };
                    if (!t)
                        return nullptr;
                    return as_object(new_array(*t, shape));
                }
                return error("TypeError", std::string{ "cannot create 'numpy." } + c.name + "' instances");
            }
            auto const items = sequence_items(aArgs);
            if (items.size() > 1u)
                return error("TypeError", std::string{ c.name } + "() takes at most 1 argument (" + std::to_string(items.size()) + " given)");
            if (items.empty())
            {
                scalar_value zero;
                zero.type = *c.type;
                return as_object(new_scalar(*c.type, zero));
            }
            auto* const array = to_array(items[0], { *c.type });
            if (array == nullptr)
                return nullptr;
            return result_object(array);
        }

        int class_instancecheck(neos_py_context*, void* aData, object* aObject)
        {
            auto const& c = *static_cast<class_info const*>(aData);
            if (std::string_view{ c.name } == "ndarray")
                return is_ndarray(aObject) ? 1 : 0;
            if (!is_numpy_scalar(aObject))
                return 0;
            auto const t = as_array(aObject)->type;
            if (c.type)
                return *c.type == t ? 1 : 0;
            return c.member(t) ? 1 : 0;
        }

        std::map<std::string, object*>& class_objects()
        {
            static std::map<std::string, object*> sObjects;
            return sObjects;
        }

        object* class_object(class_info const& aClass)
        {
            auto& objects = class_objects();
            auto const existing = objects.find(aClass.name);
            if (existing != objects.end())
            {
                ctx->retain(existing->second);
                return existing->second;
            }
            auto const qualified = std::string{ "numpy." } + aClass.name;
            auto const result = ctx->type_new(ctx, aClass.name, qualified.c_str(), construct_scalar, const_cast<class_info*>(&aClass), class_instancecheck);
            objects[aClass.name] = result;
            ctx->retain(result);
            return result;
        }
    }

    object* find_class(std::string_view aName)
    {
        std::string name{ aName };
        if (auto const alias = sClassAliases.find(name); alias != sClassAliases.end())
            name = alias->second;
        for (auto const& c : sClasses)
            if (name == c.name)
                return class_object(c);
        return nullptr;
    }

    std::optional<dtype> dtype_of_object(object* aObject)
    {
        if (aObject == nullptr)
            return {};
        if (aObject->type == &sDtypeType.base)
            return type_of_dtype_object(aObject);
        // (a class of ours: a Python type whose module is numpy)
        if (type_name(aObject) == "type")
        {
            ref const module{ ctx->getattr(ctx, aObject, "__module__") };
            ref const name{ ctx->getattr(ctx, aObject, "__name__") };
            if (module && name && as_str(module.get()) == std::optional<std::string>{ "numpy" })
            {
                auto const n = as_str(name.get()).value_or("");
                for (auto const& c : sClasses)
                    if (n == c.name && c.type)
                        return *c.type;
            }
        }
        return {};
    }

    namespace
    {
        object* dtype_getattr_slot(neos_py_context*, object* aSelf, char const* aName)
        {
            auto const t = type_of_dtype_object(aSelf);
            auto const& i = info(t);
            std::string_view const name{ aName };
            if (name == "name") return make_str(i.name);
            if (name == "kind") return make_str(std::string(1u, i.kind));
            if (name == "char") return make_str(std::string(1u, i.character));
            if (name == "itemsize") return make_int(static_cast<long long>(i.itemsize));
            if (name == "str") return make_str(i.str);
            if (name == "byteorder") return make_str(i.itemsize == 1u ? "|" : "=");
            if (name == "type") return find_class(i.scalarName);
            if (name == "ndim") return make_int(0);
            if (name == "shape") return make_tuple({});
            if (name == "isnative") return make_bool(true);
            if (name == "names" || name == "fields") return new_none();
            if (name == "alignment") return make_int(static_cast<long long>(i.itemsize > 8u ? 8u : i.itemsize));
            if (name == "num")
            {
                static int const sNumbers[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 23, 11, 12, 14, 15 };
                return make_int(sNumbers[static_cast<int>(t)]);
            }
            return error("AttributeError", std::string{ "'numpy.dtype' object has no attribute '" } + aName + "'");
        }

        object* dtype_compare_slot(neos_py_context*, int aOperation, object* aLhs, object* aRhs)
        {
            if (aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE)
                return ctx->not_implemented(ctx);
            auto const other = aLhs->type == &sDtypeType.base ? aRhs : aLhs;
            auto const self = aLhs->type == &sDtypeType.base ? aLhs : aRhs;
            std::optional<dtype> t;
            if (other->type == &sDtypeType.base)
                t = type_of_dtype_object(other);
            else if (kind(other) == NEOS_PY_NONE)
                return make_bool(aOperation == NEOS_PY_NE);
            else if (auto const text = as_str(other))
                t = dtype_from_name(*text);
            else
                t = dtype_of_object(other);
            bool const equal = t && *t == type_of_dtype_object(self);
            return make_bool(aOperation == NEOS_PY_EQ ? equal : !equal);
        }
    }

    // --- ufunc objects

    namespace
    {
        struct ufunc_object
        {
            neos_rt_object base;
            ufunc const* function;
        };

        object* ufunc_repr_slot(neos_py_context*, object* aSelf)
        {
            return make_str(std::string{ "<ufunc '" } + reinterpret_cast<ufunc_object*>(aSelf)->function->name + "'>");
        }

        object* ufunc_call(ufunc const& aUfunc, object* aArgs, object* aKwargs)
        {
            if (aUfunc.nin == 2)
            {
                arguments args{ aUfunc.name, aArgs, aKwargs, { "x1", "x2", "out", "*dtype", "*where", "*casting", "*order", "*subok", "*signature" } };
                if (!args.ok())
                    return nullptr;
                if (args[0] == nullptr || args[1] == nullptr)
                    return error("TypeError", std::string{ aUfunc.name } + "() takes from 2 to 3 positional arguments but " + std::to_string(args.positional()) + " were given");
                ndarray* out = nullptr;
                if (args.has(2))
                {
                    object* o = args[2];
                    if (kind(o) == NEOS_PY_TUPLE)
                        o = sequence_items(o).at(0);
                    if (!is_ndarray(o))
                        return error("TypeError", "return arrays must be of ArrayType");
                    out = as_array(o);
                }
                std::optional<dtype> type;
                if (args.has(3) && !(type = parse_dtype(args[3])))
                    return nullptr;
                return apply_binary(aUfunc, args[0], args[1], out, type);
            }
            arguments args{ aUfunc.name, aArgs, aKwargs, { "x", "out", "*dtype", "*where", "*casting", "*order", "*subok", "*signature" } };
            if (!args.ok())
                return nullptr;
            if (args[0] == nullptr)
                return error("TypeError", std::string{ aUfunc.name } + "() takes from 1 to 2 positional arguments but 0 were given");
            ndarray* out = nullptr;
            if (args.has(1))
            {
                object* o = args[1];
                if (kind(o) == NEOS_PY_TUPLE)
                    o = sequence_items(o).at(0);
                if (!is_ndarray(o))
                    return error("TypeError", "return arrays must be of ArrayType");
                out = as_array(o);
            }
            std::optional<dtype> type;
            if (args.has(2) && !(type = parse_dtype(args[2])))
                return nullptr;
            return apply_unary(aUfunc, args[0], out, type);
        }

        object* ufunc_call_slot(neos_py_context*, object* aSelf, object* aArgs, object* aKwargs)
        {
            return ufunc_call(*reinterpret_cast<ufunc_object*>(aSelf)->function, aArgs, aKwargs);
        }

        object* ufunc_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs);

        struct ufunc_method_entry { char const* name; };
        ufunc_method_entry const sUfuncMethods[] = { { "reduce" }, { "accumulate" }, { "outer" }, { "reduceat" }, { "at" } };

        object* ufunc_getattr_slot(neos_py_context*, object* aSelf, char const* aName)
        {
            auto const& u = *reinterpret_cast<ufunc_object*>(aSelf)->function;
            std::string_view const name{ aName };
            if (name == "__name__") return make_str(u.name);
            if (name == "nin") return make_int(u.nin);
            if (name == "nout") return make_int(1);
            if (name == "nargs") return make_int(u.nin + 1);
            if (name == "signature") return new_none();
            if (name == "__doc__") return make_str("");
            if (name == "identity")
            {
                if (!u.identity) return new_none();
                if (*u.identity == std::floor(*u.identity)) return make_int(static_cast<long long>(*u.identity));
                return make_float(*u.identity);
            }
            for (auto const& m : sUfuncMethods)
                if (name == m.name)
                    return ctx->method_new(ctx, m.name, ufunc_method, const_cast<ufunc_method_entry*>(&m), aSelf);
            return error("AttributeError", std::string{ "'numpy.ufunc' object has no attribute '" } + aName + "'");
        }

        neos_py_type make_ufunc_type()
        {
            auto type = make_type("numpy.ufunc", "ufunc", retain_static, release_static);
            type.repr = ufunc_repr_slot;
            type.call = ufunc_call_slot;
            type.getattr = ufunc_getattr_slot;
            return type;
        }

        neos_py_type sUfuncType = make_ufunc_type();

        // (ufunc.reduceat: reductions over the slices that indices delimit)
        object* ufunc_reduceat(ufunc const& u, object* aArgs, object* aKwargs)
        {
            using namespace py;
            try
            {
                arguments a{ "reduceat", aArgs, aKwargs, { "array", "indices", "axis", "dtype", "out" } };
                if (!a.ok())
                    return nullptr;
                auto const array = np("asarray")(value::borrow(a[0]));
                auto const indices = np("asarray")(value::borrow(a[1]), kw{ "dtype", np("intp") });
                if (indices.ndim() != 1)
                    raise("ValueError", "index array must be 1-dimensional");
                auto const ndim = array.ndim();
                auto axis = a.has(2) ? value::borrow(a[2]).to_int() : 0;
                if (axis < -ndim || axis >= ndim)
                    raise("AxisError", "axis " + std::to_string(axis) + " is out of bounds for array of dimension " + std::to_string(ndim));
                if (axis < 0)
                    axis += ndim;
                auto const n = array.array()->shape[static_cast<std::size_t>(axis)];
                auto const ufuncObject = value::borrow(make_ufunc_object(u));
                std::vector<value> parts;
                auto const count = indices.len();
                for (long long i = 0; i < count; ++i)
                {
                    auto const start = indices[i].to_int();
                    if (start < 0 || start >= n)
                        raise("IndexError", "index " + std::to_string(start) + " out-of-bounds in " + u.name + ".reduceat [0, " + std::to_string(n) + ")");
                    auto const stop = i + 1 < count ? indices[i + 1].to_int() : n;
                    std::vector<value> key(static_cast<std::size_t>(axis), all());
                    if (stop > start)
                    {
                        key.push_back(slice(start, stop));
                        parts.push_back(ufuncObject.method("reduce", array[tuple(key)], kw{ "axis", value{ axis } }, kw{ "keepdims", value{ true } },
                            kw{ "dtype", a.has(3) ? value::borrow(a[3]) : dtype_of(array) }));
                    }
                    else
                    {
                        key.push_back(slice(start, start + 1));
                        parts.push_back(array[tuple(key)]);
                    }
                }
                if (parts.empty())
                    return np("asarray")(array)[slice(0, 0)].release();
                auto result = np("concatenate")(tuple(parts), kw{ "axis", value{ axis } });
                if (a.has(3))
                    result = result.method("astype", value::borrow(a[3]));
                else
                    result = result.method("astype", dtype_of(array));
                return result.release();
            }
            catch (raised const&)
            {
                return nullptr;
            }
        }

        // (ufunc.at: unbuffered in place operation on the elements that indices select)
        object* ufunc_at(ufunc const& u, object* aArgs, object* aKwargs)
        {
            using namespace py;
            try
            {
                arguments a{ "at", aArgs, aKwargs, { "a", "indices", "b" } };
                if (!a.ok())
                    return nullptr;
                auto const target = value::borrow(a[0]);
                if (!is_ndarray(target.get()))
                    raise("TypeError", "first operand must be array");
                auto* const array = target.array();
                auto const positions = np("arange")(array->size()).method("reshape", target.shape())[value::borrow(a[1])];
                auto const flatPositions = np("ravel")(positions);
                value values;
                if (u.nin == 2)
                {
                    if (!a.has(2))
                        raise("ValueError", "second operand needed for ufunc");
                    values = np("ravel")(np("broadcast_to")(value::borrow(a[2]), positions.shape()));
                }
                auto const* p = flatPositions.array();
                for (std::int64_t i = 0; i < p->size(); ++i)
                {
                    auto flat = *reinterpret_cast<std::int64_t const*>(p->pointer() + i * p->strides[0]);
                    std::int64_t offset = 0;
                    for (std::size_t d = array->ndim(); d-- > 0u;)
                    {
                        offset += (flat % array->shape[d]) * array->strides[d];
                        flat /= array->shape[d];
                    }
                    ref const element{ as_object(new_view(array, array->type, static_cast<std::size_t>(static_cast<std::int64_t>(array->offset) + offset), {}, {})) };
                    ref result;
                    if (u.nin == 2)
                        result = ref{ apply_binary(u, element.get(), values[value{ static_cast<long long>(i) }].get(), as_array(element.get())) };
                    else
                        result = ref{ apply_unary(u, element.get(), as_array(element.get())) };
                    if (!result)
                        return nullptr;
                }
                return new_none();
            }
            catch (raised const&)
            {
                return nullptr;
            }
        }

        object* ufunc_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
        {
            auto const& m = *static_cast<ufunc_method_entry*>(aData);
            auto const items = sequence_items(aArgs);
            auto const& u = *reinterpret_cast<ufunc_object*>(items.at(0))->function;
            std::vector<object*> rest;
            for (std::size_t i = 1u; i < items.size(); ++i)
            {
                ctx->retain(items[i]);
                rest.push_back(items[i]);
            }
            ref const args{ make_tuple(std::move(rest)) };
            std::string_view const name{ m.name };
            if (name == "reduce")
            {
                arguments a{ "reduce", args.get(), aKwargs, { "array", "axis", "dtype", "out", "keepdims", "initial", "where" } };
                if (!a.ok())
                    return nullptr;
                ref const array{ as_object(to_array(a[0], { {}, false })) };
                if (!array)
                    return nullptr;
                auto* const arr = as_array(array.get());
                std::vector<std::size_t> axes;
                if (a[1] == nullptr)
                {
                    if (arr->ndim() == 0)
                        return error("ValueError", "zero-dimensional arrays cannot be reduced");
                    axes.push_back(0u);
                }
                else if (!to_axes(a[1], arr->ndim(), axes))
                    return nullptr;
                std::optional<dtype> type;
                if (a.has(2) && !(type = parse_dtype(a[2])))
                    return nullptr;
                bool keepdims = false;
                if (a.has(4) && !to_bool(a[4], keepdims))
                    return nullptr;
                ndarray* out = a.has(3) && is_ndarray(a[3]) ? as_array(a[3]) : nullptr;
                return reduce_array(u, arr, axes, type, keepdims, a[5], out);
            }
            if (name == "accumulate")
            {
                arguments a{ "accumulate", args.get(), aKwargs, { "array", "axis", "dtype", "out" } };
                if (!a.ok())
                    return nullptr;
                ref const array{ as_object(to_array(a[0], { {}, false })) };
                if (!array)
                    return nullptr;
                auto* const arr = as_array(array.get());
                std::int64_t axis = 0;
                if (a.has(1) && !to_int(a[1], axis))
                    return nullptr;
                std::size_t normalized = 0u;
                if (!normalize_axis(axis, arr->ndim(), normalized))
                    return nullptr;
                std::optional<dtype> type;
                if (a.has(2) && !(type = parse_dtype(a[2])))
                    return nullptr;
                return accumulate_array(u, arr, normalized, type);
            }
            if (name == "outer")
            {
                arguments a{ "outer", args.get(), aKwargs, { "A", "B" } };
                if (!a.ok())
                    return nullptr;
                ref const x{ as_object(to_array(a[0], { {}, false })) };
                ref const y{ as_object(to_array(a[1], { {}, false })) };
                if (!x || !y)
                    return nullptr;
                auto* const ax = as_array(x.get());
                auto* const ay = as_array(y.get());
                // (A[..., None, None, ...] op B)
                shape_t shape = ax->shape;
                shape_t strides = ax->strides;
                for (std::size_t i = 0u; i < ay->ndim(); ++i)
                {
                    shape.push_back(1);
                    strides.push_back(0);
                }
                ref const expanded{ as_object(new_view(ax, ax->type, ax->offset, shape, strides)) };
                return apply_binary(u, expanded.get(), y.get());
            }
            if (name == "reduceat")
                return ufunc_reduceat(u, args.get(), aKwargs);
            if (name == "at")
                return ufunc_at(u, args.get(), aKwargs);
            return error("NotImplementedError", std::string{ "ufunc." } + m.name + " is not supported");
        }
    }

    object* make_ufunc_object(ufunc const& aUfunc)
    {
        static std::map<ufunc const*, ufunc_object*> sObjects;
        auto& o = sObjects[&aUfunc];
        if (o == nullptr)
            o = new ufunc_object{ { &sUfuncType.base }, &aUfunc };
        return &o->base;
    }

    object* call_ufunc(ufunc const& aUfunc, object* aArgs, object* aKwargs)
    {
        return ufunc_call(aUfunc, aArgs, aKwargs);
    }
}
