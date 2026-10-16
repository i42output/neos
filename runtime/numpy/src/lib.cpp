/*
  lib.cpp

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

// functions that NumPy implements in Python (numpy/lib, numpy/_core): index tricks, set operations, shape functions, ...

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <unordered_set>
#include "numpy.hpp"
#include "cast.hpp"
#include "functions.hpp"
#include "py.hpp"

namespace neos::numpy
{
    namespace
    {
        using namespace py;

        // (an argument: borrowed; empty if not given)
        value arg(arguments const& aArgs, std::size_t aIndex)
        {
            return value::borrow(aArgs.get(aIndex));
        }

        // (an argument, or its default if not given or None)
        value arg(arguments const& aArgs, std::size_t aIndex, value const& aDefault)
        {
            return aArgs.has(aIndex) ? value::borrow(aArgs.get(aIndex)) : aDefault;
        }

        value asarray(value const& aValue)
        {
            return np("asarray")(aValue);
        }

        value int_tuple(std::vector<long long> const& aValues)
        {
            std::vector<value> items;
            for (auto v : aValues)
                items.emplace_back(v);
            return tuple(items);
        }

        std::vector<long long> int_items(value const& aValue)
        {
            std::vector<long long> result;
            for (auto const& x : aValue.items())
                result.push_back(x.to_int());
            return result;
        }

        long long normalize_axis_index(long long aAxis, long long aNdim)
        {
            if (aAxis < -aNdim || aAxis >= aNdim)
                raise("AxisError", "axis " + std::to_string(aAxis) + " is out of bounds for array of dimension " + std::to_string(aNdim));
            return aAxis < 0 ? aAxis + aNdim : aAxis;
        }

        std::vector<value> result_items(value const& aTuple)
        {
            return aTuple.items();
        }

        bool integer_dtype(value const& aArray)
        {
            auto const t = aArray.array()->type;
            return is_integer(t);
        }

        // --- index tricks

        object* f_diag_indices(object* aArgs, object* aKwargs)
        {
            arguments args{ "diag_indices", aArgs, aKwargs, { "n", "ndim" } };
            if (!args.ok())
                return nullptr;
            auto const idx = np("arange")(arg(args, 0));
            auto const ndim = arg(args, 1, 2).to_int();
            return tuple(std::vector<value>(static_cast<std::size_t>(ndim), idx)).release();
        }

        object* f_diag_indices_from(object* aArgs, object* aKwargs)
        {
            arguments args{ "diag_indices_from", aArgs, aKwargs, { "arr" } };
            if (!args.ok())
                return nullptr;
            auto const arr = arg(args, 0);
            auto const* a = arr.array();
            if (a->ndim() < 2u)
                raise("ValueError", "input array must be at least 2-d");
            for (auto d : a->shape)
                if (d != a->shape[0])
                    raise("ValueError", "All dimensions of input must be of equal length");
            return np("diag_indices")(a->shape[0], static_cast<long long>(a->ndim())).release();
        }

        value tri_indices(value const& n, value const& k, value const& m, bool aUpper)
        {
            auto const tri_ = aUpper ? ~np("tri")(n, m, kw{ "k", k - 1 }, kw{ "dtype", np("bool") }) : np("tri")(n, m, kw{ "k", k }, kw{ "dtype", np("bool") });
            std::vector<value> result;
            for (auto const& inds : np("indices")(tri_.shape(), kw{ "sparse", true }).items())
                result.push_back(np("broadcast_to")(inds, tri_.shape())[tri_]);
            return tuple(result);
        }

        object* f_tril_indices(object* aArgs, object* aKwargs)
        {
            arguments args{ "tril_indices", aArgs, aKwargs, { "n", "k", "m" } };
            if (!args.ok())
                return nullptr;
            return tri_indices(arg(args, 0), arg(args, 1, 0), arg(args, 2, py::none()), false).release();
        }

        object* f_triu_indices(object* aArgs, object* aKwargs)
        {
            arguments args{ "triu_indices", aArgs, aKwargs, { "n", "k", "m" } };
            if (!args.ok())
                return nullptr;
            return tri_indices(arg(args, 0), arg(args, 1, 0), arg(args, 2, py::none()), true).release();
        }

        object* tri_indices_from(char const* aName, object* aArgs, object* aKwargs, bool aUpper)
        {
            arguments args{ aName, aArgs, aKwargs, { "arr", "k" } };
            if (!args.ok())
                return nullptr;
            auto const* a = arg(args, 0).array();
            if (a->ndim() != 2u)
                raise("ValueError", "input array must be 2-d");
            return tri_indices(a->shape[0], arg(args, 1, 0), a->shape[1], aUpper).release();
        }

        object* f_tril_indices_from(object* aArgs, object* aKwargs) { return tri_indices_from("tril_indices_from", aArgs, aKwargs, false); }
        object* f_triu_indices_from(object* aArgs, object* aKwargs) { return tri_indices_from("triu_indices_from", aArgs, aKwargs, true); }

        object* f_diagflat(object* aArgs, object* aKwargs)
        {
            arguments args{ "diagflat", aArgs, aKwargs, { "v", "k" } };
            if (!args.ok())
                return nullptr;
            auto const v = asarray(arg(args, 0)).method("ravel");
            auto const k = arg(args, 1, 0).to_int();
            auto const s = v.len();
            auto const n = s + std::llabs(k);
            auto const res = np("zeros")(tuple({ n, n }), dtype_of(v));
            value fi;
            if (k >= 0)
            {
                auto const i = np("arange")(0, n - k, kw{ "dtype", np("intp") });
                fi = i + k + i * n;
            }
            else
            {
                auto const i = np("arange")(0, n + k, kw{ "dtype", np("intp") });
                fi = i + (i - k) * n;
            }
            res.attr("flat").set(fi, v);
            return res.release();
        }

        object* f_fill_diagonal(object* aArgs, object* aKwargs)
        {
            arguments args{ "fill_diagonal", aArgs, aKwargs, { "a", "val", "wrap" } };
            if (!args.ok())
                return nullptr;
            auto const a = arg(args, 0);
            auto* const array = a.array();
            if (array->ndim() < 2u)
                raise("ValueError", "array must be at least 2-d");
            bool const wrap = arg(args, 2, false).truth();
            std::int64_t step;
            std::int64_t end;
            auto const size = array->size();
            if (array->ndim() == 2u)
            {
                step = array->shape[1] + 1;
                end = wrap ? size : std::min(size, array->shape[1] * array->shape[1]);
            }
            else
            {
                for (auto d : array->shape)
                    if (d != array->shape[0])
                        raise("ValueError", "All dimensions of input must be of equal length");
                step = 1;
                std::int64_t product = 1;
                for (std::size_t i = 0u; i + 1u < array->ndim(); ++i)
                {
                    product *= array->shape[i];
                    step += product;
                }
                end = size;
            }
            // (a.flat[:end:step] = val: the values repeat)
            auto const values = np("asarray")(arg(args, 1), dtype_of(a)).method("ravel");
            auto* const v = values.array();
            if (v->size() == 0)
                return new_none();
            std::int64_t k = 0;
            for (std::int64_t flat = 0; flat < end; flat += step, ++k)
            {
                std::int64_t offset = 0, rest = flat;
                for (std::size_t d = array->ndim(); d-- > 0u;)
                {
                    offset += (rest % array->shape[d]) * array->strides[d];
                    rest /= array->shape[d];
                }
                cast_element(v->pointer() + (k % v->size()) * v->strides[0], v->type, array->pointer() + offset, array->type);
            }
            return new_none();
        }

        object* f_ix_(object* aArgs, object* aKwargs)
        {
            arguments args{ "ix_", aArgs, aKwargs, { "..." } };
            if (!args.ok())
                return nullptr;
            auto const& items = args.extra();
            auto const nd = static_cast<long long>(items.size());
            std::vector<value> out;
            for (long long k = 0; k < nd; ++k)
            {
                auto nv = value::borrow(items[static_cast<std::size_t>(k)]);
                if (!isinstance_array(nv))
                {
                    nv = asarray(nv);
                    if (nv.size() == 0)
                        nv = nv.method("astype", np("intp"));
                }
                if (nv.ndim() != 1)
                    raise("ValueError", "Cross index must be 1 dimensional");
                if (is_bool(nv.array()->type))
                    nv = nv.method("nonzero")[0];
                std::vector<long long> shape(static_cast<std::size_t>(nd), 1);
                shape[static_cast<std::size_t>(k)] = nv.size();
                out.push_back(nv.method("reshape", int_tuple(shape)));
            }
            return tuple(out).release();
        }

        object* f_unravel_index(object* aArgs, object* aKwargs)
        {
            arguments args{ "unravel_index", aArgs, aKwargs, { "indices", "shape", "order" } };
            if (!args.ok())
                return nullptr;
            auto const indices = asarray(arg(args, 0));
            auto* const ia = indices.array();
            if (!is_integer(ia->type) && !(ia->size() == 0))
                raise("TypeError", std::string{ "only int indices permitted" });
            shape_t dims;
            if (!to_shape(args[1], dims))
                throw raised{};
            bool const fortran = args.has(2) && arg(args, 2).to_str() == "F";
            std::int64_t size = 1;
            for (auto d : dims)
                size *= d;
            std::vector<ndarray*> results;
            for (std::size_t i = 0u; i < dims.size(); ++i)
                results.push_back(new_array(dtype::Int64, ia->shape));
            std::vector<value> holders;
            for (auto* r : results)
                holders.push_back(array_value(r));
            std::int64_t k = 0;
            for_each(*ia, [&](unsigned char* p)
            {
                if (failed())
                    return;
                auto index = read_as<std::int64_t>(p, ia->type);
                if (index < 0 || index >= size)
                {
                    error("ValueError", "index " + std::to_string(index) + " is out of bounds for array with size " + std::to_string(size));
                    return;
                }
                if (!fortran)
                    for (std::size_t d = dims.size(); d-- > 0u;)
                    {
                        reinterpret_cast<std::int64_t*>(results[d]->pointer())[k] = index % dims[d];
                        index /= dims[d];
                    }
                else
                    for (std::size_t d = 0u; d < dims.size(); ++d)
                    {
                        reinterpret_cast<std::int64_t*>(results[d]->pointer())[k] = index % dims[d];
                        index /= dims[d];
                    }
                ++k;
            });
            check();
            std::vector<value> out;
            for (auto const& h : holders)
                out.push_back(ia->ndim() == 0 ? value::steal(result_object((ctx->retain(h.get()), h.array()))) : h);
            return tuple(out).release();
        }

        object* f_ravel_multi_index(object* aArgs, object* aKwargs)
        {
            arguments args{ "ravel_multi_index", aArgs, aKwargs, { "multi_index", "dims", "mode", "order" } };
            if (!args.ok())
                return nullptr;
            shape_t dims;
            if (!to_shape(args[1], dims))
                throw raised{};
            auto const coordinates = arg(args, 0).items();
            if (coordinates.size() != dims.size())
                raise("ValueError", "parameter multi_index must be a sequence of length " + std::to_string(dims.size()));
            std::vector<std::string> modes;
            if (args.has(2))
            {
                auto const m = arg(args, 2);
                if (is_str(m))
                    modes.assign(dims.size(), m.to_str());
                else
                    for (auto const& x : m.items())
                        modes.push_back(x.to_str());
            }
            else
                modes.assign(dims.size(), "raise");
            bool const fortran = args.has(3) && arg(args, 3).to_str() == "F";
            std::vector<value> arrays;
            for (auto const& c : coordinates)
                arrays.push_back(asarray(c));
            auto const broadcast = np("broadcast_arrays").call(arrays).items();
            auto const shape = broadcast.empty() ? shape_t{} : broadcast[0].array()->shape;
            auto* const r = new_array(dtype::Int64, shape);
            auto const result = array_value(r);
            std::int64_t n = 1;
            for (auto d : shape)
                n *= d;
            std::vector<std::int64_t> strides(dims.size());
            std::int64_t s = 1;
            if (!fortran)
                for (std::size_t d = dims.size(); d-- > 0u;) { strides[d] = s; s *= dims[d]; }
            else
                for (std::size_t d = 0u; d < dims.size(); ++d) { strides[d] = s; s *= dims[d]; }
            std::vector<std::vector<std::int64_t>> values(dims.size());
            for (std::size_t d = 0u; d < dims.size(); ++d)
            {
                auto* const a = broadcast[d].array();
                if (!is_integer(a->type) && a->size() > 0)
                    raise("TypeError", "only int indices permitted");
                for_each(*a, [&](unsigned char* p) { values[d].push_back(read_as<std::int64_t>(p, a->type)); });
            }
            for (std::int64_t i = 0; i < n; ++i)
            {
                std::int64_t flat = 0;
                for (std::size_t d = 0u; d < dims.size(); ++d)
                {
                    auto x = values[d][static_cast<std::size_t>(i)];
                    if (modes[d] == "wrap")
                        x = ((x % dims[d]) + dims[d]) % dims[d];
                    else if (modes[d] == "clip")
                        x = std::clamp<std::int64_t>(x, 0, dims[d] - 1);
                    else if (x < 0 || x >= dims[d])
                        raise("ValueError", "invalid entry in coordinates array");
                    flat += x * strides[d];
                }
                reinterpret_cast<std::int64_t*>(r->pointer())[i] = flat;
            }
            if (shape.empty())
                return result_object((ctx->retain(result.get()), r));
            return value{ result }.release();
        }

        value make_along_axis_idx(shape_t const& aShape, value const& aIndices, long long aAxis)
        {
            if (!integer_dtype(aIndices))
                raise("IndexError", "`indices` must be an integer array");
            auto const ndim = aIndices.ndim();
            if (static_cast<long long>(aShape.size()) != ndim)
                raise("ValueError", "`indices` and `arr` must have the same number of dimensions");
            std::vector<value> fancy;
            for (long long dim = 0; dim < ndim; ++dim)
            {
                if (dim == aAxis)
                    fancy.push_back(aIndices);
                else
                {
                    std::vector<long long> shape(static_cast<std::size_t>(ndim), 1);
                    shape[static_cast<std::size_t>(dim)] = -1;
                    fancy.push_back(np("arange")(aShape[static_cast<std::size_t>(dim)]).method("reshape", int_tuple(shape)));
                }
            }
            return tuple(fancy);
        }

        object* f_take_along_axis(object* aArgs, object* aKwargs)
        {
            arguments args{ "take_along_axis", aArgs, aKwargs, { "arr", "indices", "axis" } };
            if (!args.ok())
                return nullptr;
            auto arr = arg(args, 0);
            auto const indices = arg(args, 1);
            long long axis;
            if (args.get(2) != nullptr && kind(args[2]) == NEOS_PY_NONE)
            {
                if (indices.ndim() != 1)
                    raise("ValueError", "when axis=None, `indices` must have a single dimension.");
                arr = np("array")(arr.method("ravel"));
                axis = 0;
            }
            else
                axis = normalize_axis_index(arg(args, 2, -1).to_int(), arr.ndim());
            return arr[make_along_axis_idx(arr.array()->shape, indices, axis)].release();
        }

        object* f_put_along_axis(object* aArgs, object* aKwargs)
        {
            arguments args{ "put_along_axis", aArgs, aKwargs, { "arr", "indices", "values", "axis" } };
            if (!args.ok())
                return nullptr;
            auto arr = arg(args, 0);
            auto const indices = arg(args, 1);
            long long axis;
            if (!args.has(3))
            {
                if (indices.ndim() != 1)
                    raise("ValueError", "when axis=None, `indices` must have a single dimension.");
                arr = arr.attr("flat");
                axis = 0;
            }
            else
                axis = normalize_axis_index(arg(args, 3).to_int(), arr.ndim());
            arr.set(make_along_axis_idx(arr.array()->shape, indices, axis), arg(args, 2));
            return new_none();
        }

        // (the flat offset of element aFlat of an array, C order)
        std::int64_t flat_offset(ndarray const& aArray, std::int64_t aFlat)
        {
            std::int64_t offset = 0;
            for (std::size_t d = aArray.ndim(); d-- > 0u;)
            {
                offset += (aFlat % aArray.shape[d]) * aArray.strides[d];
                aFlat /= aArray.shape[d];
            }
            return offset;
        }

        object* f_put(object* aArgs, object* aKwargs)
        {
            arguments args{ "put", aArgs, aKwargs, { "a", "ind", "v", "mode" } };
            if (!args.ok())
                return nullptr;
            auto const a = arg(args, 0);
            if (!isinstance_array(a))
                raise("TypeError", "argument 1 must be numpy.ndarray, not " + type_name(a.get()));
            auto* const array = a.array();
            auto const mode = arg(args, 3, "raise").to_str();
            auto const indices = np("asarray")(arg(args, 1), kw{ "dtype", np("intp") }).method("ravel");
            auto const values = np("asarray")(arg(args, 2), dtype_of(a)).method("ravel");
            auto* const ia = indices.array();
            auto* const va = values.array();
            auto const n = array->size();
            if (va->size() == 0 || ia->size() == 0)
                return new_none();
            for (std::int64_t i = 0; i < ia->size(); ++i)
            {
                auto index = *reinterpret_cast<std::int64_t const*>(ia->pointer() + i * ia->strides[0]);
                if (mode == "wrap")
                    index = ((index % n) + n) % n;
                else if (mode == "clip")
                    index = std::clamp<std::int64_t>(index, 0, n - 1);
                else
                {
                    if (index < -n || index >= n)
                        raise("IndexError", "index " + std::to_string(index) + " is out of bounds for axis 0 with size " + std::to_string(n));
                    if (index < 0)
                        index += n;
                }
                cast_element(va->pointer() + (i % va->size()) * va->strides[0], va->type, array->pointer() + flat_offset(*array, index), array->type);
            }
            return new_none();
        }

        object* f_putmask(object* aArgs, object* aKwargs)
        {
            arguments args{ "putmask", aArgs, aKwargs, { "a", "mask", "values" } };
            if (!args.ok())
                return nullptr;
            auto const a = arg(args, 0);
            if (!isinstance_array(a))
                raise("TypeError", "argument a of putmask must be a numpy array");
            auto* const array = a.array();
            auto const mask = np("asarray")(arg(args, 1), kw{ "dtype", np("bool") });
            if (mask.array()->size() != array->size())
                raise("ValueError", "putmask: mask and data must be the same size");
            auto const flatMask = mask.method("ravel");
            auto const values = np("asarray")(arg(args, 2), dtype_of(a)).method("ravel");
            auto* const va = values.array();
            if (va->size() == 0)
                return new_none();
            auto* const m = flatMask.array();
            for (std::int64_t i = 0; i < array->size(); ++i)
                if (read_as<bool>(m->pointer() + i * m->strides[0], dtype::Bool))
                    cast_element(va->pointer() + (i % va->size()) * va->strides[0], va->type, array->pointer() + flat_offset(*array, i), array->type);
            return new_none();
        }

        object* f_place(object* aArgs, object* aKwargs)
        {
            arguments args{ "place", aArgs, aKwargs, { "arr", "mask", "vals" } };
            if (!args.ok())
                return nullptr;
            auto const a = arg(args, 0);
            if (!isinstance_array(a))
                raise("TypeError", "argument 1 must be numpy.ndarray, not " + type_name(a.get()));
            auto* const array = a.array();
            auto const mask = np("asarray")(arg(args, 1), kw{ "dtype", np("bool") }).method("ravel");
            auto const values = np("asarray")(arg(args, 2), dtype_of(a)).method("ravel");
            auto* const va = values.array();
            auto* const m = mask.array();
            std::int64_t k = 0;
            for (std::int64_t i = 0; i < std::min(array->size(), m->size()); ++i)
                if (read_as<bool>(m->pointer() + i * m->strides[0], dtype::Bool))
                {
                    if (va->size() == 0)
                        raise("ValueError", "Cannot insert from an empty array!");
                    cast_element(va->pointer() + (k++ % va->size()) * va->strides[0], va->type, array->pointer() + flat_offset(*array, i), array->type);
                }
            return new_none();
        }

        object* f_extract(object* aArgs, object* aKwargs)
        {
            arguments args{ "extract", aArgs, aKwargs, { "condition", "arr" } };
            if (!args.ok())
                return nullptr;
            auto const condition = np("ravel")(arg(args, 0));
            return np("take")(np("ravel")(arg(args, 1)), np("nonzero")(condition)[0]).release();
        }

        object* f_compress(object* aArgs, object* aKwargs)
        {
            arguments args{ "compress", aArgs, aKwargs, { "condition", "a", "axis", "out" } };
            if (!args.ok())
                return nullptr;
            auto const condition = asarray(arg(args, 0));
            if (condition.ndim() != 1)
                raise("ValueError", "condition must be a 1-d array");
            auto a = asarray(arg(args, 1));
            long long axis = 0;
            if (!args.has(2))
                a = a.method("ravel");
            else
                axis = normalize_axis_index(arg(args, 2).to_int(), a.ndim());
            auto const indices = np("nonzero")(condition)[0];
            auto const n = a.array()->shape[static_cast<std::size_t>(axis)];
            auto* const ind = indices.array();
            if (ind->size() > 0 && reinterpret_cast<std::int64_t const*>(ind->pointer())[ind->size() - 1] >= n)
                raise("IndexError", "index " + std::to_string(reinterpret_cast<std::int64_t const*>(ind->pointer())[ind->size() - 1]) +
                    " is out of bounds for axis " + std::to_string(axis) + " with size " + std::to_string(n));
            return np("take")(a, indices, kw{ "axis", value{ axis } }).release();
        }

        object* f_choose(object* aArgs, object* aKwargs)
        {
            arguments args{ "choose", aArgs, aKwargs, { "a", "choices", "out", "mode" } };
            if (!args.ok())
                return nullptr;
            auto const a = np("asarray")(arg(args, 0));
            auto const mode = arg(args, 3, "raise").to_str();
            std::vector<value> choices;
            for (auto const& c : arg(args, 1).items())
                choices.push_back(asarray(c));
            if (choices.empty())
                raise("ValueError", "0-length sequence.");
            std::vector<value> all_{ a };
            all_.insert(all_.end(), choices.begin(), choices.end());
            auto const broadcast = np("broadcast_arrays").call(all_).items();
            std::vector<value> typesOf(choices.begin(), choices.end());
            auto const t = np("result_type").call(typesOf);
            auto const* ia = broadcast[0].array();
            auto* const r = new_array(*dtype_of_object(t.get()), ia->shape);
            auto const result = array_value(r);
            auto const n = static_cast<std::int64_t>(choices.size());
            std::int64_t i = 0;
            for_each(*ia, [&](unsigned char* p)
            {
                if (failed())
                    return;
                auto index = read_as<std::int64_t>(p, ia->type);
                if (mode == "wrap")
                    index = ((index % n) + n) % n;
                else if (mode == "clip")
                    index = std::clamp<std::int64_t>(index, 0, n - 1);
                else if (index < 0 || index >= n)
                {
                    error("ValueError", "invalid entry in choice array");
                    return;
                }
                auto const* c = broadcast[static_cast<std::size_t>(index) + 1u].array();
                cast_element(c->pointer() + flat_offset(*c, i), c->type, r->pointer() + i * static_cast<std::int64_t>(r->itemsize()), r->type);
                ++i;
            });
            check();
            return result_object((ctx->retain(result.get()), r));
        }

        object* f_copyto(object* aArgs, object* aKwargs)
        {
            arguments args{ "copyto", aArgs, aKwargs, { "dst", "src", "casting", "where" } };
            if (!args.ok())
                return nullptr;
            auto const dst = arg(args, 0);
            if (!isinstance_array(dst))
                raise("TypeError", "copyto() argument 1 must be numpy.ndarray, not " + type_name(dst.get()));
            auto* const d = dst.array();
            auto const casting = arg(args, 2, "same_kind").to_str();
            auto const src = arg(args, 1);
            bool const weak = is_python_scalar(src.get());
            auto const s = asarray(src);
            auto* const sa = s.array();
            if (!weak && !can_cast(sa->type, d->type, casting))
                raise("TypeError", std::string{ "Cannot cast " } + (sa->ndim() == 0 ? "scalar" : "array data") + " from dtype('" + info(sa->type).name + "') to dtype('" +
                    info(d->type).name + "') according to the rule '" + casting + "'");
            if (weak && !can_cast(sa->type, d->type, casting) && !(kind(src.get()) == NEOS_PY_INT && is_integer(d->type)) &&
                !(kind(src.get()) == NEOS_PY_FLOAT && is_inexact(d->type)) && !(kind(src.get()) == NEOS_PY_BOOL))
                raise("TypeError", std::string{ "Cannot cast scalar from dtype('" } + info(sa->type).name + "') to dtype('" + info(d->type).name +
                    "') according to the rule '" + casting + "'");
            if (weak && kind(src.get()) == NEOS_PY_INT && is_integer(d->type))
            {
                // (a Python integer must fit)
                ref const check{ as_object(to_array(src.get(), { d->type, true })) };
                if (!check)
                    throw raised{};
            }
            if (!args.has(3))
            {
                dst.set(ellipsis(), s);
                return new_none();
            }
            auto const where = np("asarray")(arg(args, 3), kw{ "dtype", np("bool") });
            auto const sb = np("broadcast_to")(s, dst.shape());
            auto const wb = np("broadcast_to")(where, dst.shape());
            auto* const sv = sb.array();
            auto* const wv = wb.array();
            iterate(d->shape, { sv->pointer(), wv->pointer(), d->pointer() }, { sv->strides, wv->strides, d->strides },
                [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    auto ps = aPointers[0], pw = aPointers[1], pd = aPointers[2];
                    for (std::int64_t i = 0; i < aCount; ++i, ps += aStrides[0], pw += aStrides[1], pd += aStrides[2])
                        if (*pw != 0)
                            cast_element(ps, sv->type, pd, d->type);
                });
            return new_none();
        }

        object* f_select(object* aArgs, object* aKwargs)
        {
            arguments args{ "select", aArgs, aKwargs, { "condlist", "choicelist", "default" } };
            if (!args.ok())
                return nullptr;
            auto condlist = arg(args, 0).items();
            auto choicelist = arg(args, 1).items();
            if (condlist.size() != choicelist.size())
                raise("ValueError", "list of cases must be same length as list of conditions");
            if (condlist.empty())
                raise("ValueError", "select with an empty condition list is not possible");
            for (auto& c : choicelist)
                if (!is_python_scalar(c.get()))
                    c = asarray(c);
            auto const d = arg(args, 2, 0);
            choicelist.push_back(is_python_scalar(d.get()) ? d : asarray(d));
            auto const dt = np("result_type").call(choicelist);
            condlist = np("broadcast_arrays").call(condlist).items();
            choicelist = np("broadcast_arrays").call(choicelist).items();
            for (std::size_t i = 0u; i < condlist.size(); ++i)
                if (!is_bool(condlist[i].array()->type))
                    raise("TypeError", "invalid entry " + std::to_string(i) + " in condlist: should be boolean ndarray");
            value resultShape = choicelist[0].ndim() == 0 ? condlist[0].shape() : np("broadcast_arrays")(condlist[0], choicelist[0]).items()[0].shape();
            auto const result = np("full")(resultShape, choicelist.back(), dt);
            for (std::size_t i = condlist.size(); i-- > 0u;)
                np("copyto")(result, choicelist[i], kw{ "where", condlist[i] });
            return value{ result }.release();
        }

        object* f_piecewise(object* aArgs, object* aKwargs)
        {
            arguments args{ "piecewise", aArgs, aKwargs, { "x", "condlist", "funclist", "..." } };
            if (!args.ok())
                return nullptr;
            auto const x = asarray(arg(args, 0));
            auto const funclist = arg(args, 2).items();
            auto const n2 = static_cast<long long>(funclist.size());
            auto condlist = arg(args, 1);
            if (np("isscalar")(condlist).truth() || (!is_list(condlist[0]) && !isinstance_array(condlist[0]) && x.ndim() != 0))
                condlist = list({ condlist });
            condlist = np("asarray")(condlist, kw{ "dtype", np("bool") });
            auto n = condlist.len();
            if (n == n2 - 1)
            {
                auto const condelse = ~np("any")(condlist, kw{ "axis", value{ 0 } }, kw{ "keepdims", value{ true } });
                condlist = np("concatenate")(list({ condlist, condelse }), kw{ "axis", value{ 0 } });
                ++n;
            }
            else if (n != n2)
                raise("ValueError", "with " + std::to_string(n) + " condition(s), either " + std::to_string(n) + " or " + std::to_string(n + 1) + " functions are expected");
            auto const y = np("zeros_like")(x);
            std::vector<value> extra;
            for (auto e : args.extra())
                extra.push_back(value::borrow(e));
            auto const conds = condlist.items();
            for (std::size_t i = 0u; i < conds.size(); ++i)
            {
                auto const& func = funclist[i];
                if (!is_callable(func))
                    y.set(conds[i], func);
                else
                {
                    auto const vals = x[conds[i]];
                    if (vals.size() > 0)
                    {
                        std::vector<value> callArgs{ vals };
                        callArgs.insert(callArgs.end(), extra.begin(), extra.end());
                        y.set(conds[i], func.call(callArgs));
                    }
                }
            }
            return value{ y }.release();
        }

        // --- shape functions

        object* f_resize(object* aArgs, object* aKwargs)
        {
            arguments args{ "resize", aArgs, aKwargs, { "a", "new_shape" } };
            if (!args.ok())
                return nullptr;
            shape_t shape;
            if (!to_shape(args[1], shape))
                throw raised{};
            auto const a = np("ravel")(arg(args, 0));
            std::int64_t newSize = 1;
            for (auto d : shape)
            {
                newSize *= d;
                if (d < 0)
                    raise("ValueError", "all elements of `new_shape` must be non-negative");
            }
            std::vector<long long> s(shape.begin(), shape.end());
            if (a.size() == 0 || newSize == 0)
                return np("zeros_like")(a, kw{ "shape", int_tuple(s) }).release();
            auto const repeats = (newSize + a.size() - 1) / a.size();
            auto const joined = np("concatenate")(tuple(std::vector<value>(static_cast<std::size_t>(repeats), a)))[slice({}, newSize)];
            return np("reshape")(joined, int_tuple(s)).release();
        }

        object* f_rollaxis(object* aArgs, object* aKwargs)
        {
            arguments args{ "rollaxis", aArgs, aKwargs, { "a", "axis", "start" } };
            if (!args.ok())
                return nullptr;
            auto const a = arg(args, 0);
            auto const n = a.ndim();
            auto const axis = normalize_axis_index(arg(args, 1).to_int(), n);
            auto start = arg(args, 2, 0).to_int();
            auto const originalStart = start;
            if (start < 0)
                start += n;
            if (!(0 <= start && start < n + 1))
                raise("AxisError", "'start' arg requires " + std::to_string(-n) + " <= start < " + std::to_string(n + 1) + ", but " + std::to_string(originalStart) + " was passed in");
            if (axis < start)
                --start;
            if (axis == start)
                return a[ellipsis()].release();
            std::vector<long long> axes;
            for (long long i = 0; i < n; ++i)
                if (i != axis)
                    axes.push_back(i);
            axes.insert(axes.begin() + start, axis);
            return a.method("transpose", int_tuple(axes)).release();
        }

        object* f_unstack(object* aArgs, object* aKwargs)
        {
            arguments args{ "unstack", aArgs, aKwargs, { "x", "*axis" } };
            if (!args.ok())
                return nullptr;
            auto const x = arg(args, 0);
            if (x.ndim() == 0)
                raise("ValueError", "Input array must be at least 1-d.");
            return tuple(np("moveaxis")(x, arg(args, 1, 0), 0).items()).release();
        }

        object* f_matrix_transpose(object* aArgs, object* aKwargs)
        {
            arguments args{ "matrix_transpose", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            auto const x = asarray(arg(args, 0));
            if (x.ndim() < 2)
                raise("ValueError", "Input array must be at least 2-dimensional, but it is " + std::to_string(x.ndim()));
            return np("swapaxes")(x, -1, -2).release();
        }

        // (np.block: the nesting depth of the lists and the arrays' dimensions)
        void block_check(value const& aArrays, std::vector<long long> const& aIndex, std::optional<long long>& aListDepth, long long& aMaxNdim, long long& aSize)
        {
            if (is_tuple(aArrays))
            {
                std::string where;
                for (auto i : aIndex)
                    where += "[" + std::to_string(i) + "]";
                raise("TypeError", "arrays" + where + " is a tuple. Only lists can be used to arrange blocks, and np.block does not allow implicit conversion from tuple to ndarray.");
            }
            if (is_list(aArrays))
            {
                auto const items = aArrays.items();
                if (items.empty())
                {
                    std::string where;
                    for (auto i : aIndex)
                        where += "[" + std::to_string(i) + "]";
                    raise("ValueError", "List at arrays" + where + " cannot be empty");
                }
                for (std::size_t i = 0u; i < items.size(); ++i)
                {
                    auto index = aIndex;
                    index.push_back(static_cast<long long>(i));
                    block_check(items[i], index, aListDepth, aMaxNdim, aSize);
                }
                return;
            }
            auto const depth = static_cast<long long>(aIndex.size());
            if (aListDepth && *aListDepth != depth)
                raise("ValueError", "List depths are mismatched. First element was at depth " + std::to_string(*aListDepth) + ", but there is an element at depth " +
                    std::to_string(depth) + " (arrays" + [&]() { std::string w; for (auto i : aIndex) w += "[" + std::to_string(i) + "]"; return w; }() + ")");
            aListDepth = depth;
            auto const a = asarray(aArrays);
            aMaxNdim = std::max(aMaxNdim, a.ndim());
            aSize += a.size();
        }

        value block_concatenate(value const& aArrays, long long aDepth, long long aListNdim, long long aResultNdim)
        {
            if (aDepth < aListNdim)
            {
                std::vector<value> parts;
                for (auto const& x : aArrays.items())
                    parts.push_back(block_concatenate(x, aDepth + 1, aListNdim, aResultNdim));
                return np("concatenate")(list(parts), kw{ "axis", value{ -(aListNdim - aDepth) } });
            }
            return np("array")(aArrays, kw{ "ndmin", value{ aResultNdim } }, kw{ "copy", value{ false } });
        }

        object* f_block(object* aArgs, object* aKwargs)
        {
            arguments args{ "block", aArgs, aKwargs, { "arrays" } };
            if (!args.ok())
                return nullptr;
            std::optional<long long> listDepth;
            long long maxNdim = 0, size = 0;
            block_check(arg(args, 0), {}, listDepth, maxNdim, size);
            auto const listNdim = listDepth.value_or(0);
            auto const resultNdim = std::max(maxNdim, listNdim);
            auto const result = block_concatenate(arg(args, 0), 0, listNdim, resultNdim);
            if (listNdim == 0)
                return np("array")(result).release();
            return value{ result }.release();
        }

        object* f_trim_zeros(object* aArgs, object* aKwargs)
        {
            arguments args{ "trim_zeros", aArgs, aKwargs, { "filt", "trim", "axis" } };
            if (!args.ok())
                return nullptr;
            auto const filt = arg(args, 0);
            auto const f = asarray(filt);
            auto trim = arg(args, 1, "fb").to_str();
            for (auto& c : trim)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (trim != "fb" && trim != "bf" && trim != "f" && trim != "b")
                raise("ValueError", "unexpected character(s) in `trim`: '" + trim + "'");
            auto const ndim = f.ndim();
            std::vector<long long> axes;
            if (!args.has(2))
                for (long long i = 0; i < ndim; ++i)
                    axes.push_back(i);
            else if (is_int(arg(args, 2)) || is_array(args[2]))
                axes.push_back(normalize_axis_index(arg(args, 2).to_int(), ndim));
            else
                for (auto const& x : arg(args, 2).items())
                    axes.push_back(normalize_axis_index(x.to_int(), ndim));
            if (axes.empty())
                return value{ filt }.release();
            auto const nonzero = np("argwhere")(f);
            std::vector<value> sl;
            if (nonzero.size() == 0)
            {
                for (long long ax = 0; ax < ndim; ++ax)
                    sl.push_back(std::find(axes.begin(), axes.end(), ax) != axes.end() ? slice(0, 0) : all());
            }
            else
            {
                auto const start = nonzero.method("min", kw{ "axis", value{ 0 } });
                auto const stop = nonzero.method("max", kw{ "axis", value{ 0 } }) + 1;
                for (long long ax = 0; ax < ndim; ++ax)
                {
                    if (std::find(axes.begin(), axes.end(), ax) == axes.end())
                        sl.push_back(all());
                    else
                        sl.push_back(slice(trim.find('f') != std::string::npos ? start[ax] : value{}, trim.find('b') != std::string::npos ? stop[ax] : value{}));
                }
            }
            if (sl.size() == 1u)
                return filt[sl[0]].release();
            return filt[tuple(sl)].release();
        }

        object* f_ediff1d(object* aArgs, object* aKwargs)
        {
            arguments args{ "ediff1d", aArgs, aKwargs, { "ary", "to_end", "to_begin" } };
            if (!args.ok())
                return nullptr;
            auto const ary = asarray(arg(args, 0)).method("ravel");
            auto const dtypeReq = dtype_of(ary);
            if (!args.has(1) && !args.has(2))
                return (ary[slice(1, {})] - ary[slice({}, -1)]).release();
            auto const prepare = [&](std::size_t aIndex, char const* aName) -> value
            {
                if (!args.has(aIndex))
                    return {};
                auto const x = asarray(arg(args, aIndex));
                if (!np("can_cast")(x, dtypeReq, kw{ "casting", value{ "same_kind" } }).truth())
                    raise("TypeError", std::string{ "dtype of `" } + aName + "` must be compatible with input `ary` under the `same_kind` rule.");
                return x.method("ravel");
            };
            auto const toBegin = prepare(2, "to_begin");
            auto const toEnd = prepare(1, "to_end");
            auto const lBegin = toBegin.empty() ? 0 : toBegin.len();
            auto const lEnd = toEnd.empty() ? 0 : toEnd.len();
            auto const lDiff = std::max(ary.len() - 1, 0ll);
            auto const result = np("empty")(lDiff + lBegin + lEnd, dtypeReq);
            if (lBegin > 0)
                result.set(slice({}, lBegin), toBegin);
            if (lEnd > 0)
                result.set(slice(lBegin + lDiff, {}), toEnd);
            result.set(slice(lBegin, lBegin + lDiff), ary[slice(1, {})] - ary[slice({}, -1)]);
            return value{ result }.release();
        }

        // --- set operations

        // (membership of the elements of ar1 in ar2)
        value isin_mask(value const& aAr1, value const& aAr2, bool aInvert)
        {
            auto const a1 = asarray(aAr1).method("ravel");
            auto const a2 = asarray(aAr2).method("ravel");
            auto* const x = a1.array();
            auto* const y = a2.array();
            auto const t = promote(x->type, y->type);
            auto* const r = new_array(dtype::Bool, { x->size() });
            auto const result = array_value(r);
            dispatch(t, [&](auto tag)
            {
                using T = decltype(tag);
                using C = std::conditional_t<is_half_v<T>, float, T>;
                std::vector<C> values;
                for (std::int64_t i = 0; i < y->size(); ++i)
                    values.push_back(cast<C>(read_as<T>(y->pointer() + i * y->strides[0], y->type)));
                if constexpr (is_complex_v<C>)
                {
                    for (std::int64_t i = 0; i < x->size(); ++i)
                    {
                        auto const v = cast<C>(read_as<T>(x->pointer() + i * x->strides[0], x->type));
                        bool const found = std::find(values.begin(), values.end(), v) != values.end();
                        r->pointer()[i] = found != aInvert;
                    }
                }
                else
                {
                    if constexpr (std::is_floating_point_v<C>)
                        values.erase(std::remove_if(values.begin(), values.end(), [](C x) { return std::isnan(x); }), values.end());
                    std::sort(values.begin(), values.end(), [](C a, C b) { return a < b; });
                    for (std::int64_t i = 0; i < x->size(); ++i)
                    {
                        auto const v = cast<C>(read_as<T>(x->pointer() + i * x->strides[0], x->type));
                        bool found = std::binary_search(values.begin(), values.end(), v, [](C a, C b) { return a < b; });
                        if constexpr (std::is_floating_point_v<C>)
                            found = found && !std::isnan(v);
                        r->pointer()[i] = found != aInvert;
                    }
                }
            });
            return result;
        }

        object* f_isin(object* aArgs, object* aKwargs)
        {
            arguments args{ "isin", aArgs, aKwargs, { "element", "test_elements", "assume_unique", "invert", "*kind" } };
            if (!args.ok())
                return nullptr;
            auto const element = asarray(arg(args, 0));
            if (args.has(4))
            {
                auto const k = arg(args, 4).to_str();
                if (k != "sort" && k != "table")
                    raise("ValueError", "Invalid kind: '" + k + "'. Please use None, 'sort' or 'table'.");
            }
            return isin_mask(element, arg(args, 1), arg(args, 3, false).truth()).method("reshape", element.shape()).release();
        }

        object* f_in1d(object* aArgs, object* aKwargs)
        {
            arguments args{ "in1d", aArgs, aKwargs, { "ar1", "ar2", "assume_unique", "invert", "*kind" } };
            if (!args.ok())
                return nullptr;
            return isin_mask(arg(args, 0), arg(args, 1), arg(args, 3, false).truth()).release();
        }

        object* f_intersect1d(object* aArgs, object* aKwargs)
        {
            arguments args{ "intersect1d", aArgs, aKwargs, { "ar1", "ar2", "assume_unique", "return_indices" } };
            if (!args.ok())
                return nullptr;
            auto ar1 = asarray(arg(args, 0));
            auto ar2 = asarray(arg(args, 1));
            bool const assumeUnique = arg(args, 2, false).truth();
            bool const returnIndices = arg(args, 3, false).truth();
            value ind1, ind2;
            if (!assumeUnique)
            {
                if (returnIndices)
                {
                    auto const u1 = np("unique")(ar1, kw{ "return_index", value{ true } }).items();
                    auto const u2 = np("unique")(ar2, kw{ "return_index", value{ true } }).items();
                    ar1 = u1[0]; ind1 = u1[1];
                    ar2 = u2[0]; ind2 = u2[1];
                }
                else
                {
                    ar1 = np("unique")(ar1);
                    ar2 = np("unique")(ar2);
                }
            }
            else
            {
                ar1 = ar1.method("ravel");
                ar2 = ar2.method("ravel");
            }
            auto aux = np("concatenate")(tuple({ ar1, ar2 }));
            value auxSortIndices;
            if (returnIndices)
            {
                auxSortIndices = np("argsort")(aux, kw{ "kind", value{ "mergesort" } });
                aux = aux[auxSortIndices];
            }
            else
                aux = np("sort")(aux);
            auto const mask = aux[slice(1, {})] == aux[slice({}, -1)];
            auto const int1d = aux[slice({}, -1)][mask];
            if (!returnIndices)
                return value{ int1d }.release();
            auto ar1Indices = auxSortIndices[slice({}, -1)][mask];
            auto ar2Indices = auxSortIndices[slice(1, {})][mask] - ar1.size();
            if (!assumeUnique)
            {
                ar1Indices = ind1[ar1Indices];
                ar2Indices = ind2[ar2Indices];
            }
            return tuple({ int1d, ar1Indices, ar2Indices }).release();
        }

        object* f_setdiff1d(object* aArgs, object* aKwargs)
        {
            arguments args{ "setdiff1d", aArgs, aKwargs, { "ar1", "ar2", "assume_unique" } };
            if (!args.ok())
                return nullptr;
            value ar1, ar2 = arg(args, 1);
            if (arg(args, 2, false).truth())
                ar1 = asarray(arg(args, 0)).method("ravel");
            else
            {
                ar1 = np("unique")(arg(args, 0));
                ar2 = np("unique")(ar2);
            }
            return ar1[isin_mask(ar1, ar2, true)].release();
        }

        object* f_union1d(object* aArgs, object* aKwargs)
        {
            arguments args{ "union1d", aArgs, aKwargs, { "ar1", "ar2" } };
            if (!args.ok())
                return nullptr;
            return np("unique")(np("concatenate")(tuple({ arg(args, 0), arg(args, 1) }), kw{ "axis", py::none() })).release();
        }

        object* f_setxor1d(object* aArgs, object* aKwargs)
        {
            arguments args{ "setxor1d", aArgs, aKwargs, { "ar1", "ar2", "assume_unique" } };
            if (!args.ok())
                return nullptr;
            auto ar1 = arg(args, 0), ar2 = arg(args, 1);
            if (!arg(args, 2, false).truth())
            {
                ar1 = np("unique")(ar1);
                ar2 = np("unique")(ar2);
            }
            auto aux = np("concatenate")(tuple({ ar1, ar2 }), kw{ "axis", py::none() });
            if (aux.size() == 0)
                return value{ aux }.release();
            aux = np("sort")(aux);
            auto const flag = np("concatenate")(tuple({ list({ value{ true } }), aux[slice(1, {})] != aux[slice({}, -1)], list({ value{ true } }) }));
            return aux[flag[slice(1, {})] & flag[slice({}, -1)]].release();
        }

        // (a named tuple result: e.g. UniqueAllResult(values=..., ...))
        struct named_tuple
        {
            neos_rt_object base;
            std::atomic<long> references{ 1 };
            std::string name;
            std::vector<std::string> fields;
            std::vector<object*> values;
        };

        void named_tuple_retain(neos_rt_object* aObject) { ++reinterpret_cast<named_tuple*>(aObject)->references; }
        void named_tuple_release(neos_rt_object* aObject)
        {
            auto* const t = reinterpret_cast<named_tuple*>(aObject);
            if (--t->references == 0)
            {
                for (auto v : t->values)
                    ctx->release(v);
                delete t;
            }
        }

        object* named_tuple_repr(neos_py_context*, object* aSelf)
        {
            auto const* t = reinterpret_cast<named_tuple const*>(aSelf);
            std::string text = t->name + "(";
            for (std::size_t i = 0u; i < t->values.size(); ++i)
            {
                ref const r{ ctx->repr(ctx, t->values[i]) };
                if (!r)
                    return nullptr;
                text += (i != 0u ? ", " : "") + t->fields[i] + "=" + *as_str(r.get());
            }
            return make_str(text + ")");
        }

        object* named_tuple_getattr(neos_py_context*, object* aSelf, char const* aName)
        {
            auto const* t = reinterpret_cast<named_tuple const*>(aSelf);
            for (std::size_t i = 0u; i < t->fields.size(); ++i)
                if (t->fields[i] == aName)
                {
                    ctx->retain(t->values[i]);
                    return t->values[i];
                }
            return error("AttributeError", "'" + t->name + "' object has no attribute '" + aName + "'");
        }

        object* named_tuple_tuple(object* aSelf)
        {
            auto const* t = reinterpret_cast<named_tuple const*>(aSelf);
            std::vector<object*> items;
            for (auto v : t->values)
                items.push_back((ctx->retain(v), v));
            return make_tuple(std::move(items));
        }

        object* named_tuple_getitem(neos_py_context*, object* aSelf, object* aKey)
        {
            ref const t{ named_tuple_tuple(aSelf) };
            return ctx->getitem(ctx, t.get(), aKey);
        }

        long long named_tuple_length(neos_py_context*, object* aSelf)
        {
            return static_cast<long long>(reinterpret_cast<named_tuple const*>(aSelf)->values.size());
        }

        object* named_tuple_iter(neos_py_context*, object* aSelf)
        {
            ref const t{ named_tuple_tuple(aSelf) };
            return ctx->iter(ctx, t.get());
        }

        neos_py_type make_named_tuple_type()
        {
            neos_py_type type{};
            type.base = neos_rt_object_type{ "numpy.namedtuple", named_tuple_retain, named_tuple_release };
            type.magic = NEOS_PY_TYPE_MAGIC;
            type.name = "tuple";
            type.repr = named_tuple_repr;
            type.getattr = named_tuple_getattr;
            type.getitem = named_tuple_getitem;
            type.length = named_tuple_length;
            type.iter = named_tuple_iter;
            return type;
        }

        neos_py_type const sNamedTupleType = make_named_tuple_type();

        value named(std::string const& aName, std::vector<std::string> const& aFields, std::vector<value> const& aValues)
        {
            auto* const t = new named_tuple{ { &sNamedTupleType.base }, 1, aName, aFields, {} };
            for (auto const& v : aValues)
                t->values.push_back((ctx->retain(v.get()), v.get()));
            return value::steal(&t->base);
        }

        object* f_unique_all(object* aArgs, object* aKwargs)
        {
            arguments args{ "unique_all", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            auto const r = np("unique")(arg(args, 0), kw{ "return_index", value{ true } }, kw{ "return_inverse", value{ true } }, kw{ "return_counts", value{ true } },
                kw{ "equal_nan", value{ false } }).items();
            return named("UniqueAllResult", { "values", "indices", "inverse_indices", "counts" }, r).release();
        }

        object* f_unique_counts(object* aArgs, object* aKwargs)
        {
            arguments args{ "unique_counts", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            auto const r = np("unique")(arg(args, 0), kw{ "return_counts", value{ true } }, kw{ "equal_nan", value{ false } }).items();
            return named("UniqueCountsResult", { "values", "counts" }, r).release();
        }

        object* f_unique_inverse(object* aArgs, object* aKwargs)
        {
            arguments args{ "unique_inverse", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            auto const r = np("unique")(arg(args, 0), kw{ "return_inverse", value{ true } }, kw{ "equal_nan", value{ false } }).items();
            return named("UniqueInverseResult", { "values", "inverse_indices" }, r).release();
        }

        // (the unique values of integers, in the order of NumPy's hash set (std::unordered_set))
        value unique_hash(value const& aArray)
        {
            auto const flat = aArray.method("ravel");
            auto* const a = flat.array();
            return dispatch(a->type, [&](auto tag) -> value
            {
                using T = decltype(tag);
                if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
                {
                    // (as NumPy: a set of pointers to the elements, hashed with FNV-1a over their bytes)
                    struct hash
                    {
                        std::size_t operator()(T const* aValue) const
                        {
                            std::uint64_t h = 0xcbf29ce484222325ull;
                            auto const* p = reinterpret_cast<unsigned char const*>(aValue);
                            for (std::size_t i = 0u; i < sizeof(T); ++i)
                            {
                                h ^= p[i];
                                h *= 0x100000001b3ull;
                            }
                            return static_cast<std::size_t>(h);
                        }
                    };
                    struct equal
                    {
                        bool operator()(T const* a, T const* b) const { return *a == *b; }
                    };
                    auto const contiguous = np("ascontiguousarray")(flat);
                    auto* const c = contiguous.array();
                    std::unordered_set<T const*, hash, equal> set(static_cast<std::size_t>(std::min<std::int64_t>(c->size(), 1024)));
                    for (std::int64_t i = 0; i < c->size(); ++i)
                        set.insert(reinterpret_cast<T const*>(c->pointer()) + i);
                    auto* const r = new_array(a->type, { static_cast<std::int64_t>(set.size()) });
                    auto* out = reinterpret_cast<T*>(r->pointer());
                    for (auto x : set)
                        *out++ = *x;
                    return array_value(r);
                }
                else
                    return np("unique")(flat);
            });
        }

        object* f_unique_values(object* aArgs, object* aKwargs)
        {
            arguments args{ "unique_values", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            return unique_hash(asarray(arg(args, 0))).release();
        }

        // --- sorting

        object* f_lexsort(object* aArgs, object* aKwargs)
        {
            arguments args{ "lexsort", aArgs, aKwargs, { "keys", "axis" } };
            if (!args.ok())
                return nullptr;
            auto keys = arg(args, 0);
            std::vector<value> keyList;
            if (isinstance_array(keys) && keys.ndim() >= 1)
                for (auto const& k : keys.items())
                    keyList.push_back(k);
            else
                for (auto const& k : keys.items())
                    keyList.push_back(asarray(k));
            if (keyList.empty())
                raise("TypeError", "need sequence of keys with len > 0 in lexsort");
            keyList = np("broadcast_arrays").call(keyList).items();
            auto const ndim = keyList[0].ndim();
            if (ndim == 0)
                return np("asarray")(0, kw{ "dtype", np("intp") }).release();
            auto const axis = normalize_axis_index(arg(args, 1, -1).to_int(), ndim);
            // (stable sorts by each key, the last key last)
            auto order = np("argsort")(keyList[0], kw{ "axis", value{ axis } }, kw{ "kind", value{ "stable" } });
            for (std::size_t k = 1u; k < keyList.size(); ++k)
            {
                auto const sortedKey = np("take_along_axis")(keyList[k], order, kw{ "axis", value{ axis } });
                auto const next = np("argsort")(sortedKey, kw{ "axis", value{ axis } }, kw{ "kind", value{ "stable" } });
                order = np("take_along_axis")(order, next, kw{ "axis", value{ axis } });
            }
            return value{ order }.release();
        }

        object* f_sort_complex(object* aArgs, object* aKwargs)
        {
            arguments args{ "sort_complex", aArgs, aKwargs, { "a" } };
            if (!args.ok())
                return nullptr;
            auto b = np("array")(arg(args, 0), kw{ "copy", value{ true } });
            b = np("sort")(b);
            auto const t = b.array()->type;
            if (!is_complex(t))
            {
                if (t == dtype::Int8 || t == dtype::UInt8 || t == dtype::Int16 || t == dtype::UInt16 || t == dtype::Bool)
                    return b.method("astype", "F").release();
                return b.method("astype", "D").release();
            }
            return value{ b }.release();
        }

        // --- products

        object* f_kron(object* aArgs, object* aKwargs)
        {
            arguments args{ "kron", aArgs, aKwargs, { "a", "b" } };
            if (!args.ok())
                return nullptr;
            auto const b = asarray(arg(args, 1));
            auto const a = np("array")(arg(args, 0), kw{ "copy", py::none() }, kw{ "ndmin", value{ b.ndim() } });
            auto const ndb = b.ndim(), nda = a.ndim();
            auto const nd = std::max(ndb, nda);
            if (nda == 0 || ndb == 0)
                return np("multiply")(a, b).release();
            auto as_ = int_items(a.shape());
            auto bs = int_items(b.shape());
            as_.insert(as_.begin(), static_cast<std::size_t>(std::max(0ll, ndb - nda)), 1);
            bs.insert(bs.begin(), static_cast<std::size_t>(std::max(0ll, nda - ndb)), 1);
            auto range = [](long long aStart, long long aStop, long long aStep)
            {
                std::vector<long long> r;
                for (auto i = aStart; i < aStop; i += aStep)
                    r.push_back(i);
                return r;
            };
            auto aArr = np("expand_dims")(a, kw{ "axis", int_tuple(range(0, ndb - nda, 1)) });
            auto bArr = np("expand_dims")(b, kw{ "axis", int_tuple(range(0, nda - ndb, 1)) });
            aArr = np("expand_dims")(aArr, kw{ "axis", int_tuple(range(1, nd * 2, 2)) });
            bArr = np("expand_dims")(bArr, kw{ "axis", int_tuple(range(0, nd * 2, 2)) });
            auto const result = np("multiply")(aArr, bArr);
            std::vector<long long> shape;
            for (std::size_t i = 0u; i < as_.size(); ++i)
                shape.push_back(as_[i] * bs[i]);
            return result.method("reshape", int_tuple(shape)).release();
        }

        object* f_tensordot(object* aArgs, object* aKwargs)
        {
            arguments args{ "tensordot", aArgs, aKwargs, { "a", "b", "axes" } };
            if (!args.ok())
                return nullptr;
            auto const axes = arg(args, 2, 2);
            std::vector<long long> axesA, axesB;
            if (is_int(axes) || is_array(axes.get()))
            {
                auto const n = axes.to_int();
                for (long long i = -n; i < 0; ++i)
                    axesA.push_back(i);
                for (long long i = 0; i < n; ++i)
                    axesB.push_back(i);
            }
            else
            {
                auto const pair = axes.items();
                if (pair.size() != 2u)
                    raise("ValueError", "too many values to unpack (expected 2)");
                auto const list_of = [](value const& v) { std::vector<long long> r; if (is_int(v)) r.push_back(v.to_int()); else for (auto const& x : v.items()) r.push_back(x.to_int()); return r; };
                axesA = list_of(pair[0]);
                axesB = list_of(pair[1]);
            }
            if (std::set<long long>(axesA.begin(), axesA.end()).size() != axesA.size() || std::set<long long>(axesB.begin(), axesB.end()).size() != axesB.size())
                raise("ValueError", "duplicate axes are not allowed in tensordot");
            auto const a = asarray(arg(args, 0));
            auto const b = asarray(arg(args, 1));
            auto const as_ = a.array()->shape;
            auto const bs = b.array()->shape;
            auto const nda = static_cast<long long>(as_.size()), ndb = static_cast<long long>(bs.size());
            bool equal = axesA.size() == axesB.size();
            if (equal)
                for (std::size_t k = 0u; k < axesA.size(); ++k)
                {
                    auto const ia = axesA[k] < 0 ? axesA[k] + nda : axesA[k];
                    auto const ib = axesB[k] < 0 ? axesB[k] + ndb : axesB[k];
                    if (ia < 0 || ia >= nda || ib < 0 || ib >= ndb)
                        raise("IndexError", "tuple index out of range");
                    if (as_[static_cast<std::size_t>(ia)] != bs[static_cast<std::size_t>(ib)])
                    {
                        equal = false;
                        break;
                    }
                    axesA[k] = ia;
                    axesB[k] = ib;
                }
            if (!equal)
                raise("ValueError", "shape-mismatch for sum");
            std::vector<long long> notinA, notinB, olda, oldb;
            for (long long k = 0; k < nda; ++k)
                if (std::find(axesA.begin(), axesA.end(), k) == axesA.end())
                    notinA.push_back(k);
            for (long long k = 0; k < ndb; ++k)
                if (std::find(axesB.begin(), axesB.end(), k) == axesB.end())
                    notinB.push_back(k);
            long long n2 = 1, na = 1, nb = 1;
            for (auto x : axesA) n2 *= as_[static_cast<std::size_t>(x)];
            for (auto x : notinA) { na *= as_[static_cast<std::size_t>(x)]; olda.push_back(as_[static_cast<std::size_t>(x)]); }
            for (auto x : notinB) { nb *= bs[static_cast<std::size_t>(x)]; oldb.push_back(bs[static_cast<std::size_t>(x)]); }
            auto newaxesA = notinA;
            newaxesA.insert(newaxesA.end(), axesA.begin(), axesA.end());
            auto newaxesB = axesB;
            newaxesB.insert(newaxesB.end(), notinB.begin(), notinB.end());
            auto const at = a.method("transpose", int_tuple(newaxesA)).method("reshape", int_tuple({ na, n2 }));
            auto const bt = b.method("transpose", int_tuple(newaxesB)).method("reshape", int_tuple({ n2, nb }));
            auto const res = np("dot")(at, bt);
            auto shape = olda;
            shape.insert(shape.end(), oldb.begin(), oldb.end());
            return res.method("reshape", int_tuple(shape)).release();
        }

        object* f_vecdot(object* aArgs, object* aKwargs)
        {
            arguments args{ "vecdot", aArgs, aKwargs, { "x1", "x2", "*out", "*casting", "*order", "*dtype", "*subok", "*signature", "*axes", "*axis" } };
            if (!args.ok())
                return nullptr;
            auto x1 = asarray(arg(args, 0));
            auto const x2 = asarray(arg(args, 1));
            auto const axis = arg(args, 9, -1).to_int();
            if (is_complex(x1.array()->type))
                x1 = np("conjugate")(x1);
            auto const product = x1 * x2;
            if (x1.array()->shape.empty() || x2.array()->shape.empty())
                raise("ValueError", "vecdot: Input operand 0 does not have enough dimensions (has 0, gufunc core with signature (n),(n)->() requires 1)");
            auto const n1 = x1.array()->shape.back(), n2 = x2.array()->shape.back();
            if (n1 != n2)
                raise("ValueError", "vecdot: Input operand 1 has a mismatch in its core dimension 0, with gufunc signature (n),(n)->() (size " + std::to_string(n2) +
                    " is different from " + std::to_string(n1) + ")");
            return np("sum")(product, kw{ "axis", value{ axis } }).release();
        }

        object* f_matvec(object* aArgs, object* aKwargs)
        {
            arguments args{ "matvec", aArgs, aKwargs, { "x1", "x2", "*out", "*casting", "*order", "*dtype", "*subok", "*signature", "*axes", "*axis" } };
            if (!args.ok())
                return nullptr;
            auto const x1 = asarray(arg(args, 0));
            auto const x2 = asarray(arg(args, 1));
            return np("matmul")(x1, np("expand_dims")(x2, -1))[tuple({ ellipsis(), 0 })].release();
        }

        object* f_vecmat(object* aArgs, object* aKwargs)
        {
            arguments args{ "vecmat", aArgs, aKwargs, { "x1", "x2", "*out", "*casting", "*order", "*dtype", "*subok", "*signature", "*axes", "*axis" } };
            if (!args.ok())
                return nullptr;
            auto x1 = asarray(arg(args, 0));
            auto const x2 = asarray(arg(args, 1));
            if (is_complex(x1.array()->type))
                x1 = np("conjugate")(x1);
            return np("matmul")(np("expand_dims")(x1, -2), x2)[tuple({ ellipsis(), 0, all() })].release();
        }

        object* f_broadcast_shapes(object* aArgs, object* aKwargs)
        {
            arguments args{ "broadcast_shapes", aArgs, aKwargs, { "..." } };
            if (!args.ok())
                return nullptr;
            std::vector<shape_t> shapes;
            for (auto x : args.extra())
            {
                shape_t s;
                if (!to_shape(x, s))
                    throw raised{};
                shapes.push_back(s);
            }
            std::vector<shape_t const*> pointers;
            for (auto const& s : shapes)
                pointers.push_back(&s);
            shape_t result;
            if (!broadcast_shapes(pointers, result))
                throw raised{};
            return int_tuple(std::vector<long long>(result.begin(), result.end())).release();
        }
    }

    void register_lib()
    {
        register_functions({
            { "diag_indices", f_diag_indices }, { "diag_indices_from", f_diag_indices_from }, { "tril_indices", f_tril_indices },
            { "triu_indices", f_triu_indices }, { "tril_indices_from", f_tril_indices_from }, { "triu_indices_from", f_triu_indices_from },
            { "diagflat", f_diagflat }, { "fill_diagonal", f_fill_diagonal }, { "ix_", f_ix_ }, { "unravel_index", f_unravel_index },
            { "ravel_multi_index", f_ravel_multi_index }, { "take_along_axis", f_take_along_axis }, { "put_along_axis", f_put_along_axis },
            { "put", f_put }, { "putmask", f_putmask }, { "place", f_place }, { "extract", f_extract }, { "compress", f_compress },
            { "choose", f_choose }, { "copyto", f_copyto }, { "select", f_select }, { "piecewise", f_piecewise }, { "resize", f_resize },
            { "rollaxis", f_rollaxis }, { "unstack", f_unstack }, { "matrix_transpose", f_matrix_transpose }, { "block", f_block },
            { "trim_zeros", f_trim_zeros }, { "ediff1d", f_ediff1d }, { "isin", f_isin }, { "in1d", f_in1d }, { "intersect1d", f_intersect1d },
            { "setdiff1d", f_setdiff1d }, { "union1d", f_union1d }, { "setxor1d", f_setxor1d }, { "unique_all", f_unique_all },
            { "unique_counts", f_unique_counts }, { "unique_inverse", f_unique_inverse }, { "unique_values", f_unique_values },
            { "lexsort", f_lexsort }, { "sort_complex", f_sort_complex }, { "kron", f_kron }, { "tensordot", f_tensordot }, { "vecdot", f_vecdot },
            { "matvec", f_matvec }, { "vecmat", f_vecmat }, { "broadcast_shapes", f_broadcast_shapes }
        });
    }
}
