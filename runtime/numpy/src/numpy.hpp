/*
  numpy.hpp

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

// The NumPy runtime plugin (neos.runtime.numpy): its parts

#pragma once

#include <atomic>
#include <complex>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <neos/runtime/python/object.h>

namespace neos::numpy
{
    using object = neos_py_object;

    // the Python runtime plugin's context (with which objects are created and operated on)
    extern neos_py_context* ctx;

    // --- Python objects

    // an owned reference to a Python object
    class ref
    {
    public:
        ref() = default;
        explicit ref(object* aObject) : iObject{ aObject } {}
        ref(ref const& aOther) : iObject{ aOther.iObject } { if (iObject != nullptr) ctx->retain(iObject); }
        ref(ref&& aOther) noexcept : iObject{ aOther.iObject } { aOther.iObject = nullptr; }
        ~ref() { if (iObject != nullptr) ctx->release(iObject); }
        ref& operator=(ref aOther) { std::swap(iObject, aOther.iObject); return *this; }
        static ref borrowed(object* aObject) { if (aObject != nullptr) ctx->retain(aObject); return ref{ aObject }; }
    public:
        object* get() const { return iObject; }
        object* release() { auto const result = iObject; iObject = nullptr; return result; }
        explicit operator bool() const { return iObject != nullptr; }
    private:
        object* iObject = nullptr;
    };

    // raise a Python exception (returns null, for "return error(...)")
    std::nullptr_t error(char const* aType, std::string const& aMessage);
    bool failed();
    int kind(object* aObject);
    std::string type_name(object* aObject);
    object* none();
    object* new_none();
    object* make_bool(bool aValue);
    object* make_int(long long aValue);
    object* make_float(double aValue);
    object* make_complex(std::complex<double> aValue);
    object* make_str(std::string const& aValue);
    object* make_tuple(std::vector<object*> aItems);  // (steals the items)
    object* make_list(std::vector<object*> aItems);   // (steals the items)
    std::optional<std::string> as_str(object* aObject);
    std::vector<object*> sequence_items(object* aObject); // (borrowed: a list or tuple)
    bool is_sequence(object* aObject);
    bool is_python_scalar(object* aObject);

    // --- dtypes

    enum class dtype : int
    {
        Bool, Int8, UInt8, Int16, UInt16, Int32, UInt32, Int64, UInt64, Float16, Float32, Float64, Complex64, Complex128, Count
    };

    struct dtype_info
    {
        char const* name;
        char kind;              // b, i, u, f, c
        char character;         // ?, b, B, h, H, i, I, l, L, e, f, d, F, D
        std::size_t itemsize;
        char const* str;        // e.g. "<f8"
        char const* scalarName; // e.g. "float64" (the scalar type's name)
    };

    dtype_info const& info(dtype aType);
    inline bool is_bool(dtype t) { return t == dtype::Bool; }
    inline bool is_signed(dtype t) { return t == dtype::Int8 || t == dtype::Int16 || t == dtype::Int32 || t == dtype::Int64; }
    inline bool is_unsigned(dtype t) { return t == dtype::UInt8 || t == dtype::UInt16 || t == dtype::UInt32 || t == dtype::UInt64; }
    inline bool is_integer(dtype t) { return is_signed(t) || is_unsigned(t); }
    inline bool is_float(dtype t) { return t == dtype::Float16 || t == dtype::Float32 || t == dtype::Float64; }
    inline bool is_complex(dtype t) { return t == dtype::Complex64 || t == dtype::Complex128; }
    inline bool is_inexact(dtype t) { return is_float(t) || is_complex(t); }
    std::optional<dtype> parse_dtype(object* aObject); // (null: not a dtype; raises TypeError)
    std::optional<dtype> dtype_from_name(std::string_view aName);
    object* find_class(std::string_view aName);
    dtype promote(dtype a, dtype b);
    dtype smallest_float_for(dtype aType);            // the float type that safely holds an integer (or bool) type
    dtype complex_for(dtype aFloat);
    dtype real_for(dtype aComplex);
    bool can_cast(dtype aFrom, dtype aTo, std::string_view aCasting);

    // IEEE 754 binary16
    struct half
    {
        std::uint16_t bits = 0;
        half() = default;
        explicit half(float aValue);
        explicit half(double aValue);
        operator float() const;
    };

    // --- element values (loaded from and stored to memory with NumPy's casting)

    template <dtype D> struct ctype_of;
    template <> struct ctype_of<dtype::Bool> { using type = bool; };
    template <> struct ctype_of<dtype::Int8> { using type = std::int8_t; };
    template <> struct ctype_of<dtype::UInt8> { using type = std::uint8_t; };
    template <> struct ctype_of<dtype::Int16> { using type = std::int16_t; };
    template <> struct ctype_of<dtype::UInt16> { using type = std::uint16_t; };
    template <> struct ctype_of<dtype::Int32> { using type = std::int32_t; };
    template <> struct ctype_of<dtype::UInt32> { using type = std::uint32_t; };
    template <> struct ctype_of<dtype::Int64> { using type = std::int64_t; };
    template <> struct ctype_of<dtype::UInt64> { using type = std::uint64_t; };
    template <> struct ctype_of<dtype::Float16> { using type = half; };
    template <> struct ctype_of<dtype::Float32> { using type = float; };
    template <> struct ctype_of<dtype::Float64> { using type = double; };
    template <> struct ctype_of<dtype::Complex64> { using type = std::complex<float>; };
    template <> struct ctype_of<dtype::Complex128> { using type = std::complex<double>; };
    template <dtype D> using ctype = typename ctype_of<D>::type;

    // a value of any type (as its widest form)
    struct scalar_value
    {
        dtype type = dtype::Float64;
        bool b = false;
        std::int64_t i = 0;
        std::uint64_t u = 0;
        double f = 0.0;
        std::complex<double> c;
    };

    scalar_value load(unsigned char const* aPointer, dtype aType);
    void store(unsigned char* aPointer, dtype aType, scalar_value const& aValue); // (cast from aValue.type)
    void cast_element(unsigned char const* aFrom, dtype aFromType, unsigned char* aTo, dtype aToType);
    // (aCount elements, as cast_element; strides in bytes)
    void cast_elements(unsigned char const* aFrom, std::int64_t aFromStride, dtype aFromType, unsigned char* aTo, std::int64_t aToStride, dtype aToType, std::int64_t aCount);

    // --- arrays

    void advise_large_allocation(void* aData, std::size_t aBytes); // (as NumPy: huge pages for a large array (Linux))

    struct storage
    {
        std::unique_ptr<std::uint64_t[]> words;
        std::size_t bytes = 0;
        explicit storage(std::size_t aBytes) : storage{ aBytes, true } {}
        storage(std::size_t aBytes, bool aZero) : words{ new std::uint64_t[(aBytes + 15u) / 8u + 1u] }, bytes{ aBytes } 
        {
            advise_large_allocation(words.get(), aBytes);
            if (aZero)
                std::memset(words.get(), 0, ((aBytes + 15u) / 8u + 1u) * sizeof(std::uint64_t));
        }
        unsigned char* data() { return reinterpret_cast<unsigned char*>(words.get()); }
    };

    using shape_t = std::vector<std::int64_t>;

    struct ndarray
    {
        neos_rt_object base;
        std::atomic<long> references{ 1 };
        dtype type = dtype::Float64;
        std::shared_ptr<storage> data;
        std::size_t offset = 0u;    ///< (bytes)
        shape_t shape;
        shape_t strides;            ///< (bytes)
        bool scalar = false;        ///< a NumPy scalar (e.g. np.float64): a 0-d array that behaves as a scalar
        bool writeable = true;
        bool view = false;          ///< (its data is another array's)

        unsigned char* pointer() const { return data->data() + offset; }
        std::int64_t size() const;
        std::size_t ndim() const { return shape.size(); }
        std::size_t itemsize() const { return info(type).itemsize; }
        bool c_contiguous() const;
        bool f_contiguous() const;
    };

    bool is_array(object* aObject);                   // (an ndarray or a NumPy scalar)
    bool is_ndarray(object* aObject);                 // (not a scalar)
    bool is_numpy_scalar(object* aObject);
    ndarray* as_array(object* aObject);
    object* as_object(ndarray* aArray);
    shape_t contiguous_strides(shape_t const& aShape, std::size_t aItemsize, bool aFortran = false);
    ndarray* new_array(dtype aType, shape_t const& aShape, bool aFortran = false); // (zero filled)
    ndarray* new_unfilled_array(dtype aType, shape_t const& aShape, bool aFortran = false); // (not filled: for a result whose every element is written)
    ndarray* new_view(ndarray* aBase, dtype aType, std::size_t aOffset, shape_t aShape, shape_t aStrides);
    ndarray* new_scalar(dtype aType, scalar_value const& aValue);
    ndarray* copy_array(ndarray const& aArray, std::optional<dtype> aType = {}, bool aFortran = false);
    object* to_python(ndarray const& aArray); // (a 0-d array or scalar: the value as a Python object; .item())
    object* scalar_from(ndarray const& aArray, unsigned char const* aElement); // a NumPy scalar of the array's type
    object* result_object(ndarray* aResult);  // (a 0-d result is a scalar)
    std::int64_t element_count(shape_t const& aShape);

    // conversion of a Python object to an array (np.array / np.asarray)
    struct array_options
    {
        std::optional<dtype> type;
        bool copy = true;
        int ndmin = 0;
    };
    ndarray* to_array(object* aObject, array_options const& aOptions = {});
    // an operand of an operation (an array, or a Python scalar: weakly typed as NEP 50)
    struct operand
    {
        ref holder;
        ndarray* array = nullptr;
        bool weak = false;          ///< a Python scalar
        int weakKind = 0;           ///< (bool, int, float or complex: NEOS_PY_BOOL etc.)
    };
    bool to_operand(object* aObject, operand& aResult);

    // iteration (with broadcasting)
    bool broadcast_shapes(std::vector<shape_t const*> const& aShapes, shape_t& aResult);
    shape_t broadcast_strides(ndarray const& aArray, shape_t const& aShape);
    // calls aInner(count, pointers, strides) for each inner run (the last dimension) of the elements of the operands
    void iterate(shape_t const& aShape, std::vector<unsigned char*> aPointers, std::vector<shape_t> const& aStrides,
        std::function<void(std::int64_t, unsigned char**, std::int64_t const*)> const& aInner);

    // --- formatting (NumPy's arrayprint)

    std::string array_repr(ndarray const& aArray);
    std::string array_str(ndarray const& aArray);
    std::string scalar_repr(ndarray const& aScalar);
    std::string scalar_str(ndarray const& aScalar);
    std::string format_double_repr(double aValue);    // (Python's repr of a float)
    std::string shape_repr(shape_t const& aShape);
    struct print_options
    {
        int precision = 8;
        std::int64_t threshold = 1000;
        int edgeitems = 3;
        int linewidth = 75;
        bool suppress = false;
        std::string nanstr = "nan";
        std::string infstr = "inf";
        std::string sign = "-";
        std::string floatmode = "maxprec";
    };
    print_options& options();

    // --- arguments of a call (args tuple and kwargs dict)

    class arguments
    {
    public:
        // aNames: the parameters ("name", or "*name" for keyword only)
        arguments(char const* aFunction, object* aArgs, object* aKwargs, std::initializer_list<char const*> aNames);
    public:
        bool ok() const { return iOk; }
        object* operator[](std::size_t aIndex) const { return iValues[aIndex]; }
        object* get(std::size_t aIndex) const { return iValues[aIndex]; }
        bool has(std::size_t aIndex) const { return iValues[aIndex] != nullptr && kind(iValues[aIndex]) != NEOS_PY_NONE; }
        std::size_t positional() const { return iPositional; }
        std::vector<object*> const& extra() const { return iExtra; } // (positional arguments beyond the parameters, if "*args")
    private:
        bool iOk = true;
        std::vector<object*> iValues;
        std::vector<object*> iExtra;
        std::size_t iPositional = 0u;
    };

    bool to_int(object* aObject, std::int64_t& aResult);
    bool to_double(object* aObject, double& aResult);
    bool to_bool(object* aObject, bool& aResult);
    bool to_shape(object* aObject, shape_t& aResult);      // (an int or a sequence of ints)
    bool to_axes(object* aObject, std::size_t aNdim, std::vector<std::size_t>& aResult, bool aAllowDuplicates = false); // (None: all)
    bool normalize_axis(std::int64_t aAxis, std::size_t aNdim, std::size_t& aResult);

    // --- ufuncs

    struct ufunc;
    ufunc const* find_ufunc(std::string_view aName);
    object* make_ufunc_object(ufunc const& aUfunc);
    object* apply_binary(ufunc const& aUfunc, object* aLhs, object* aRhs, ndarray* aOut = nullptr, std::optional<dtype> aType = {});
    object* apply_unary(ufunc const& aUfunc, object* aOperand, ndarray* aOut = nullptr, std::optional<dtype> aType = {});
    object* binary_operator(int aOperation, object* aLhs, object* aRhs, bool aInplace);
    object* unary_operator(int aOperation, object* aOperand);
    object* compare_operator(int aOperation, object* aLhs, object* aRhs);

    // --- the module

    object* module_getattr(char const* aName);
    object* array_getattr(object* aSelf, char const* aName);
    object* array_getitem(object* aSelf, object* aKey);
    int array_setitem(object* aSelf, object* aKey, object* aValue);
    object* call_function(std::string_view aName, object* aArgs, object* aKwargs);
    object* make_function_object(char const* aName);
    object* dtype_object(dtype aType);
    std::optional<dtype> dtype_of_object(object* aObject); // (a dtype object or a scalar type object)
    neos_py_type const* scalar_type(dtype aType);
    neos_py_type const* array_type();

    // reductions and other functions used by methods
    object* reduce(std::string_view aName, ndarray* aArray, object* aArgs, object* aKwargs);
    object* method_call(std::string_view aName, object* aSelf, object* aArgs, object* aKwargs);
}
