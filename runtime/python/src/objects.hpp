/*
  objects.hpp

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

// The Python runtime plugin's core objects (objects.cpp) as its other parts (e.g. the modules of Python's standard library) use them

#pragma once

#include <algorithm>
#include <atomic>
#include <map>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <neonumeric/integer.hpp>
#include <neos/runtime/python/object.h>
#include <neos/runtime/string.hpp>
#include "python.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        using object = neos_py_object;

        // --- object storage

        struct header
        {
            neos_rt_object base;
            std::atomic<long> references{ 1 };
        };

        // (references are counted by the thread that holds the GIL (see threads.cpp): one thread at a time runs the program (as 
        // CPython's GIL build: plain increments and decrements, not atomic ones (which are many times slower)); the GIL's mutex 
        // makes them visible to the next thread that holds it)
        inline void add_reference(header& aHeader)
        {
            aHeader.references.store(aHeader.references.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        }
        // (the number of references that remain)
        inline long remove_reference(header& aHeader)
        {
            auto const remaining = aHeader.references.load(std::memory_order_relaxed) - 1;
            aHeader.references.store(remaining, std::memory_order_relaxed);
            return remaining;
        }

        inline void retain_object(neos_rt_object* aObject)
        {
            add_reference(*reinterpret_cast<header*>(aObject));
        }

        // (weak references (weakref.cpp): the number of objects that have weak references, and an object's weak references cleared (their 
        // callbacks called) as it is destroyed)
        extern std::atomic<std::size_t> gWeaklyReferenced;
        void clear_weak_references(neos_py_object* aObject);

        template <typename T>
        inline void release_object(neos_rt_object* aObject)
        {
            if (remove_reference(*reinterpret_cast<header*>(aObject)) == 0)
            {
                if (gWeaklyReferenced.load(std::memory_order_relaxed) != 0u)
                    clear_weak_references(reinterpret_cast<neos_py_object*>(aObject));
                delete reinterpret_cast<T*>(aObject);
            }
        }

        inline void retain_static(neos_rt_object*) {}
        inline void release_static(neos_rt_object*) {}

        inline void retain(object* aObject) { if (aObject != nullptr) aObject->type->retain(aObject); }
        inline void release(object* aObject) { if (aObject != nullptr) aObject->type->release(aObject); }

        // an owned reference
        class ref
        {
        public:
            ref() = default;
            explicit ref(object* aObject) : iObject{ aObject } {}
            ref(ref const& aOther) : iObject{ aOther.iObject } { retain(iObject); }
            ref(ref&& aOther) noexcept : iObject{ aOther.iObject } { aOther.iObject = nullptr; }
            ~ref() { release(iObject); }
            ref& operator=(ref aOther) { std::swap(iObject, aOther.iObject); return *this; }
            static ref borrowed(object* aObject) { retain(aObject); return ref{ aObject }; }
        public:
            object* get() const { return iObject; }
            object* release_ownership() { auto const result = iObject; iObject = nullptr; return result; }
            explicit operator bool() const { return iObject != nullptr; }
        private:
            object* iObject = nullptr;
        };

        inline neos_py_type const* python_type(object* aObject)
        {
            if (aObject == nullptr)
                return nullptr;
            auto const* type = reinterpret_cast<neos_py_type const*>(aObject->type);
            return type->magic == NEOS_PY_TYPE_MAGIC ? type : nullptr;
        }

        neos_py_context* context();

        // --- the core types

        inline neos_py_type make_type(char const* aQualifiedName, char const* aName, void (*aRetain)(neos_rt_object*), void (*aRelease)(neos_rt_object*))
        {
            neos_py_type type{};
            type.base = neos_rt_object_type{ aQualifiedName, aRetain, aRelease };
            type.magic = NEOS_PY_TYPE_MAGIC;
            type.name = aName;
            return type;
        }

        using big_integer = neonumeric::integer<0u>;
        using big_detail = neonumeric::xinteger_detail<big_integer>;
        struct int_object 
        { 
            header h; 
            long long value; 
            std::unique_ptr<big_integer> big; ///< (the value if it is not a 64-bit integer (a 128-bit integer's, if needed, made from it))
            long long high = 0;                 ///< (a 128-bit integer (wide): its high 64 bits; value is its low 64 bits (unsigned))
            bool wide = false;
            // (ints are made and released at a high rate (e.g. by iteration over a range): their storage is a pool's (see objects.cpp))
            static void* operator new(std::size_t aSize);
            static void operator delete(void* aObject, std::size_t aSize) noexcept;
        };
        struct float_object 
        { 
            header h; 
            double value; 
            // (as int's)
            static void* operator new(std::size_t aSize);
            static void operator delete(void* aObject, std::size_t aSize) noexcept;
        };
        struct complex_object { header h; double real; double imag; };
        // (a str: its text (UTF-8; immutable once it is made, so what is found about it is kept: its length and the last character that at()
        // found (its offset)); len() and at() are its characters (code points), ascii_len() and ascii_at() are its bytes (its encoding is
        // ignored: e.g. for finding ASCII characters (a line's \r and \n)); its text is ASCII if len() == ascii_len())
        struct str_object
        {
            header h;
            neos::runtime::string const text;           ///< (immutable: see neos/runtime/string.hpp)
            std::string const& value = text.utf8();     ///< (its UTF-8: text's (which does not move))
            mutable bool hashed = false;                ///< (its hash: found once)
            mutable long long hash = 0;

            long long len() const { return static_cast<long long>(text.len()); }
            // (the code point at aIndex (0 <= aIndex < len()))
            char32_t at(long long aIndex) const { return text.at(static_cast<std::size_t>(aIndex)); }
            char32_t operator[](long long aIndex) const { return at(aIndex); }
            bool is_ascii() const { return text.is_ascii(); }
            std::size_t ascii_len() const { return value.size(); }
            char ascii_at(std::size_t aIndex) const { return value[aIndex]; }
            // (as int's: a pool's (see objects.cpp))
            static void* operator new(std::size_t aSize);
            static void operator delete(void* aObject, std::size_t aSize) noexcept;
        };
        struct list_object { header h; std::vector<object*> items; ~list_object() { for (auto i : items) release(i); } };
        struct tuple_object { header h; std::vector<object*> items; ~tuple_object() { for (auto i : items) release(i); } };
        struct dict_object 
        { 
            header h; 
            std::vector<std::pair<object*, object*>> items;     ///< (in insertion order)
            std::vector<long long> hashes;                      ///< (the keys' hashes)
            std::vector<std::int32_t> index;                    ///< (the items' positions by their keys' hashes (open addressing, -1: none); 
                                                                ///< empty: none (a small dict), or to be rebuilt)
            object* binder = nullptr;                           ///< (a module's namespace (globals()): the getter of the module's 
                                                                ///< names, which binds those that are set)
            ~dict_object() { for (auto& i : items) { release(i.first); release(i.second); } release(binder); } 
        };
        struct slice_object { header h; object* start; object* stop; object* step; ~slice_object() { release(start); release(stop); release(step); } };
        struct range_object { header h; long long start; long long stop; long long step; };
        struct iterator_object 
        { 
            header h; 
            object* sequence; 
            std::size_t index = 0u; 
            long long count = -1; ///< (a range's length (a range does not change): found by the first next())
            ~iterator_object() { release(sequence); } 
        };
        using native_function = neos_py_object* (*)(neos_py_context*, void*, neos_py_object*, neos_py_object*);
        struct function_object { header h; std::string name; native_function function; void* data; object* self; bool binds = true; ~function_object() { release(self); } };  // (binds: a class's attribute is a method (CPython's builtins are not))
        struct singleton { neos_rt_object base; };

        inline neos_py_type sNoneType = make_type("python.NoneType", "NoneType", retain_static, release_static);
        inline neos_py_type sNotImplementedType = make_type("python.NotImplementedType", "NotImplementedType", retain_static, release_static);
        inline neos_py_type sEllipsisType = make_type("python.ellipsis", "ellipsis", retain_static, release_static);
        inline neos_py_type sBoolType = make_type("python.bool", "bool", retain_static, release_static);
        inline neos_py_type sIntType = make_type("python.int", "int", retain_object, release_object<int_object>);
        inline neos_py_type sFloatType = make_type("python.float", "float", retain_object, release_object<float_object>);
        inline neos_py_type sComplexType = make_type("python.complex", "complex", retain_object, release_object<complex_object>);
        inline neos_py_type sStrType = make_type("python.str", "str", retain_object, release_object<str_object>);
        inline neos_py_type sListType = make_type("python.list", "list", retain_object, release_object<list_object>);
        inline neos_py_type sTupleType = make_type("python.tuple", "tuple", retain_object, release_object<tuple_object>);
        inline neos_py_type sDictType = make_type("python.dict", "dict", retain_object, release_object<dict_object>);
        inline neos_py_type sSliceType = make_type("python.slice", "slice", retain_object, release_object<slice_object>);
        inline neos_py_type sRangeType = make_type("python.range", "range", retain_object, release_object<range_object>);
        inline neos_py_type sIteratorType = make_type("python.iterator", "iterator", retain_object, release_object<iterator_object>);
        inline neos_py_type sFunctionType = make_type("python.builtin_function_or_method", "builtin_function_or_method", retain_object, release_object<function_object>);

        inline singleton sNone{ &sNoneType.base };
        inline singleton sNotImplemented{ &sNotImplementedType.base };
        inline singleton sEllipsis{ &sEllipsisType.base };
        inline singleton sTrue{ &sBoolType.base };
        inline singleton sFalse{ &sBoolType.base };

        inline int kind_of_other(object* aObject);
        // (the commonest kinds here (inline: always); the rest: kind_of_other)
#if defined(_MSC_VER)
        __forceinline int kind_of(object* aObject)
#else
        inline __attribute__((always_inline)) int kind_of(object* aObject)
#endif
        {
            if (aObject == nullptr)
                return NEOS_PY_OTHER;
            auto const type = aObject->type;
            if (type == &sStrType.base) return NEOS_PY_STR;
            if (type == &sIntType.base) return NEOS_PY_INT;
            if (type == &sListType.base) return NEOS_PY_LIST;
            if (type == &sTupleType.base) return NEOS_PY_TUPLE;
            if (type == &sDictType.base) return NEOS_PY_DICT;
            return kind_of_other(aObject);
        }
        inline int kind_of_other(object* aObject)
        {
            auto const type = aObject->type;
            if (type == &sBoolType.base) return NEOS_PY_BOOL;
            if (type == &sNoneType.base) return NEOS_PY_NONE;
            if (type == &sFloatType.base) return NEOS_PY_FLOAT;
            if (type == &sNotImplementedType.base) return NEOS_PY_NOT_IMPLEMENTED;
            if (type == &sEllipsisType.base) return NEOS_PY_ELLIPSIS;
            if (type == &sComplexType.base) return NEOS_PY_COMPLEX;
            if (type == &sSliceType.base) return NEOS_PY_SLICE;
            if (type == &sRangeType.base) return NEOS_PY_RANGE;
            if (type == &sIteratorType.base) return NEOS_PY_ITERATOR;
            if (type == &sFunctionType.base) return NEOS_PY_FUNCTION;
            return NEOS_PY_OTHER;
        }

        template <typename T> T* as(object* aObject) { return reinterpret_cast<T*>(aObject); }
        // (an int of 64 bits (not a bool, nor an instance of a subclass))
        inline bool is_small_int(object* aObject) { return aObject != nullptr && aObject->type == &sIntType.base && as<int_object>(aObject)->big == nullptr && !as<int_object>(aObject)->wide; }

        inline object* none() { return &sNone.base; }
        inline object* not_implemented() { return &sNotImplemented.base; }
        inline object* make_bool(bool aValue) { return aValue ? &sTrue.base : &sFalse.base; }
        object* small_int(long long aValue);
        inline object* make_int(long long aValue) 
        { 
            if (aValue >= -5 && aValue <= 256)
                return small_int(aValue);
            return &(new int_object{ { { &sIntType.base } }, aValue })->h.base; 
        }
        inline object* make_float(double aValue) { return &(new float_object{ { { &sFloatType.base } }, aValue })->h.base; }
        inline object* make_complex(double aReal, double aImag) { return &(new complex_object{ { { &sComplexType.base } }, aReal, aImag })->h.base; }
        // (aLength: its code points, if known (its size: it is ASCII))
        inline object* make_str(std::string aValue, std::size_t aLength = neos::runtime::string::npos) 
        { 
            return &(new str_object{ { { &sStrType.base } }, neos::runtime::string{ std::move(aValue), aLength } })->h.base; 
        }
        inline object* make_list(std::vector<object*> aItems = {}) { return &(new list_object{ { { &sListType.base } }, std::move(aItems) })->h.base; }
        inline object* make_tuple(std::vector<object*> aItems = {}) { return &(new tuple_object{ { { &sTupleType.base } }, std::move(aItems) })->h.base; }
        inline object* make_dict() { return &(new dict_object{ { { &sDictType.base } }, {} })->h.base; }
        inline object* make_slice(object* aStart, object* aStop, object* aStep)
        {
            auto const value = [](object* aPart) { auto const result = aPart != nullptr ? aPart : none(); retain(result); return result; };
            return &(new slice_object{ { { &sSliceType.base } }, value(aStart), value(aStop), value(aStep) })->h.base;
        }
        inline object* make_range(long long aStart, long long aStop, long long aStep) { return &(new range_object{ { { &sRangeType.base } }, aStart, aStop, aStep })->h.base; }
        inline object* make_iterator(object* aSequence) { retain(aSequence); return &(new iterator_object{ { { &sIteratorType.base } }, aSequence })->h.base; }
        inline object* make_function(std::string aName, native_function aFunction, void* aData, object* aSelf = nullptr)
        {
            retain(aSelf);
            return &(new function_object{ { { &sFunctionType.base } }, std::move(aName), aFunction, aData, aSelf })->h.base;
        }

        // --- operations (objects.cpp)

        // (a builtin function: its name and its function (positional arguments and a dict of keyword arguments (or null)))
        struct builtin_entry
        {
            char const* name;
            object* (*function)(std::vector<object*> const& aArgs, object* aKwargs);
        };
        object* call_builtin(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs);
        // (a function object of a builtin function (aEntry is static))
        inline object* make_builtin(builtin_entry const& aEntry) { return make_function(aEntry.name, call_builtin, const_cast<builtin_entry*>(&aEntry)); }

        // (modules of the standard library that other source files implement: a module's initializer adds its attributes 
        // (new references); a static "inline bool sRegistered = register_module(name, initializer);" registers it)
        using module_initializer = bool (*)(std::vector<std::pair<std::string, object*>>& aAttributes);
        std::map<std::string, module_initializer, std::less<>>& modules();
        bool register_module(char const* aName, module_initializer aInitializer);
        // (a class of a module (e.g. threading.Thread): its namespace's functions (builtin functions) are its methods)
        object* make_class(std::string const& aModule, std::string const& aName, std::vector<object*> const& aBases,
            std::vector<std::pair<std::string, object*>> const& aNamespace);
        // (a class's isinstance test (e.g. collections.abc.Iterable's): an object is an instance if it is an instance of a subclass, or the test is 1)
        void set_class_instancecheck(object* aClass, int (*aCheck)(object*));
        bool is_class(object* aObject);
        bool is_subclass(object* aClass, object* aBase);   // (user classes: aBase is aClass or in its MRO)
        std::vector<object*> class_subclasses(object* aClass);  // (a user class's subclasses (borrowed))
        int class_setattr_raw(object* aClass, char const* aName, object* aValue);   // (type.__setattr__ (aValue null: type.__delattr__))
        object* make_bound_method(object* aFunction, object* aSelf);
        object* make_empty_module(std::string const& aName);   // (types.ModuleType(name))
        object* fn_type_new(neos_py_context*, void*, object* aArgs, object* aKwargs);   // (type.__new__(metaclass, name, bases, namespace))
        bool is_type_object(object* aObject);
        std::string const& type_qualified_name(object* aType);  // (a type object's qualified name (e.g. "collections.deque"))
        bool is_instance(object* aObject);
        object* user_class_of(object* aObject);
        object* getattr(object* aObject, char const* aName);
        int setattr(object* aObject, char const* aName, object* aValue);
        object* compare(int aOperation, object* aLhs, object* aRhs);
        bool repr_of(object* aObject, std::string& aResult);
        bool str_of(object* aObject, std::string& aResult);
        bool to_double(object* aObject, double& aResult);
        long long length(object* aObject);
        object* exception_class(std::string const& aName);
        bool is_exception_class(std::string_view aName);
        std::string object_address(void const* aObject);

        std::nullptr_t error(std::string const& aType, std::string const& aMessage);
        char const* type_name(object* aObject);                  // (as Python shows it)
        bool join_repr(std::vector<object*> const& aItems, std::string& aResult);  // (the items' reprs separated by ", ")
        bool hash_of(object* aObject, long long& aResult);
        int equal(object* aLhs, object* aRhs);                  // (identity, then ==: 1, 0 or -1 (an exception))
        int contains(object* aContainer, object* aItem);         // (1, 0 or -1)
        object* iter(object* aObject);
        object* next(object* aIterator);                        // (null: exhausted, or an exception (failed()))
        void raise_key_error(object* aKey);
        // (the position of a dict's key: -1 if absent; -2 if an exception was raised (e.g. the key is unhashable))
        long long dict_find(object* aDict, object* aKey);
        int dict_set(object* aDict, object* aKey, object* aValue);
        // (a module's getter (see the Python compiler's rewrite_module_names) called: getter(index) (-1: the names), or 
        // getter(index, value, True) (the name bound))
        object* call_module_getter(object* aGetter, long long aIndex, object* aValue = nullptr);
        // (a module's namespace's item set: the module's name of that key bound to the value (false: an exception))
        bool namespace_bind(object* aGetter, object* aKey, object* aValue);
        bool no_keywords(object* aKwargs, char const* aFunction);

        bool is_int_like(object* aObject);                       // (int or bool)
        bool to_index(object* aObject, long long& aResult);       // (__index__)
        bool collect(object* aIterable, std::vector<object*>& aItems); // (new references)
        int truth(object* aObject);
        bool slice_indices(object* aSlice, long long aLength, long long& aStart, long long& aStop, long long& aStep);
        long long bytes_hash(std::string_view aBytes);
        // (the arguments of a native function as CPython's argument clinic checks them: aNames are its parameters, of which the first 
        // aRequired are required and the first aPositionalOnly cannot be keywords; aFunction is its qualified name (e.g. "bytes.split"); 
        // aResult: the arguments (null: not given) (arguments.cpp))
        // (a function that can change the host environment (e.g. os.remove): false (RuntimeError) if the program is running in restricted mode)
        bool host_unrestricted(char const* aFunction);
        bool parse_arguments(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames,
            std::size_t aRequired, std::size_t aPositionalOnly, std::vector<object*>& aResult);

        // --- bytes and bytearray, and the codecs (bytes.cpp)

        object* make_bytes(std::string aData);
        object* make_bytearray(std::string aData);
        bool bytes_like(object* aObject, std::string_view& aData);   // (bytes, bytearray, memoryview or a buffer provider's object: its data)
        // (the buffer protocol: an object's buffer (its data, whether it is read-only, and its items' format (a struct format, e.g. "i") and size))
        struct buffer_info
        {
            char* data = nullptr;
            std::size_t size = 0u;      ///< (in bytes)
            bool readonly = true;
            char const* format = "B";
            std::size_t itemsize = 1u;
        };
        // (the buffers of the types of other modules (e.g. array.array): get (false: none), and exports (+1 or -1: a memoryview of the object
        // was made or released))
        struct buffer_provider
        {
            bool (*get)(object* aObject, buffer_info& aResult);
            void (*exports)(object* aObject, int aDelta);
        };
        std::map<neos_rt_object_type const*, buffer_provider>& buffer_providers();
        bool buffer_of(object* aObject, buffer_info& aResult);  // (bytes, bytearray, a memoryview (contiguous) or a provider's: false if none)
        // (an integer (int, bool or __index__) as a C integer: 0: aSigned (and aUnsigned) is its value; 1: aUnsigned is (it is beyond aSigned's
        // range); 2: it is greater than 2**64 - 1; 3: it is less than -2**63; -1: it is not an integer (TypeError))
        int integer_value(object* aValue, long long& aSigned, std::uint64_t& aUnsigned);
        object* make_unsigned(std::uint64_t aValue);
        std::uint16_t to_half(double aValue);           // (IEEE 754 binary16 (textmodules.cpp))
        double from_half(std::uint16_t aValue);
        bool encode(std::string_view aText, std::string_view aEncoding, std::string_view aErrors, std::string& aResult);
        bool decode(std::string_view aData, std::string_view aEncoding, std::string_view aErrors, std::string& aResult);
        object* b_bytes(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_bytearray(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_memoryview(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_bytes_literal(std::vector<object*> const& aArgs, object* aKwargs);
        object* bytes_fromhex(object* aString, bool aMutable);
        object* str_encode(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);

        object* call(object* aCallable, object* aArgs, object* aKwargs);
        object* fn_builtin(char const* aName);
        void fn_raise(object* aException);              // (raise exception: an exception or its class)
        void raise_exception(object* aException);
        void raise_exception(object* aException, bool aContext);    // (aContext: the exception being handled is its context)
        void reraise_exception(object* aException);     // (as a bare raise: an exception that was taken (its context is unchanged))

        object* b_property(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_staticmethod(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_classmethod(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_issubclass(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_callable(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_dir(std::vector<object*> const& aArgs, object* aKwargs);
        object* class_function(std::string const& aClass, char const* aName);   // (a builtin type's function (e.g. object.__new__): a new reference, or null)
        inline bool is_callable(object* aObject) { ref const r{ b_callable({ aObject }, nullptr) }; return r && truth(r.get()) == 1; }
        object* b_isinstance(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_dict(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_list(std::vector<object*> const& aArgs, object* aKwargs);
        // (a new instance of a user class (object.__new__): of a subclass of a builtin type: its value from aArgs (immutable types))
        object* new_instance(object* aClass, object* aArgs, object* aKwargs);
        object* itertools_chain_from_iterable();        // (itertools.cpp)
        object* fraction_class_function(char const* aName);  // (fractions.cpp: Fraction.from_float etc.)
        object* decimal_class_function(char const* aName);   // (decimal.cpp: Decimal.from_float)
        object* datetime_class_function(std::string const& aClass, char const* aName);    // (datetime.cpp: e.g. datetime.max, date.today)
        // (big integers (objects.cpp))
        object* make_int(big_integer aValue);
        big_integer big_value(object* aObject);
        bool big_negative(big_integer const& aValue);
        bool big_zero(big_integer const& aValue);
        big_integer big_abs(big_integer const& aValue);
        std::uint64_t big_bit_length(big_integer const& aValue);
        void big_divmod(big_integer const& a, big_integer const& b, big_integer& q, big_integer& r);   // (floor)
        std::string big_digits(big_integer const& aValue, int aBase);
        object* b_cached_property(std::vector<object*> const& aArgs, object* aKwargs);
        // (the types of objects that are bound to an instance as methods when they are a class's attributes (e.g. functools.lru_cache's wrapper))
        std::set<neos_rt_object_type const*>& method_types();
        // (the builtin functions that are abstract methods (their __isabstractmethod__ is True): e.g. numbers.Complex.__add__)
        std::set<object*>& abstract_builtins();
        bool unhashable_builtin_type(std::string_view aQualifiedName);
        std::map<std::string, neos_py_type const*, std::less<>>& observed_types();   // (the types of plugins' objects that class_of has seen)    // (list, dict, set, ...: their __hash__ is None)
        // (the types of modules that classes derive from (e.g. class D(collections.deque)), by their qualified names: an instance's value of the type 
        // (construct: of the arguments; aOwner: the instance, which the value does not own (default: the type called with the arguments)), and the 
        // value's release of its owner (disown) when the instance is destroyed)
        struct native_base_hooks
        {
            object* (*construct)(object* aArgs, object* aKwargs, object* aOwner) = nullptr;
            void (*disown)(object* aValue) = nullptr;
        };
        std::map<std::string, native_base_hooks, std::less<>>& native_bases();
        void write_unraisable(object* aObject);         // (the pending exception: "Exception ignored in: <aObject>" and its report)
        object* fn_tuple_of(object* aIterable);
        object* class_of(object* aObject);
        object* type_object_for(std::string const& aName, std::string const& aQualifiedName);
        std::map<std::string, std::vector<std::string>, std::less<>>& user_exception_bases();

        object* getitem(object* aObject, object* aKey);
        int setitem(object* aObject, object* aKey, object* aValue);
        object* binary(int aOperation, object* aLhs, object* aRhs, bool aInplace);
        object* unary(int aOperation, object* aObject);

        // --- the Unicode database (unicodedata.cpp): the properties of characters (UCD 15.1.0, as CPython 3.13's str has them)

        namespace unicode_flag
        {
            inline constexpr std::uint16_t ALPHA = 0x1u, DECIMAL = 0x2u, DIGIT = 0x4u, NUMERIC = 0x8u, LOWER = 0x10u, UPPER = 0x20u, TITLE = 0x40u,
                SPACE = 0x80u, LINEBREAK = 0x100u, PRINTABLE = 0x200u, CASED = 0x400u, CASE_IGNORABLE = 0x800u, XID_START = 0x1000u, XID_CONTINUE = 0x2000u,
                MIRRORED = 0x8000u;
        }
        enum class unicode_case { Upper, Lower, Title, Fold };
        std::uint16_t unicode_flags(char32_t aCharacter);
        inline bool unicode_is(char32_t aCharacter, std::uint16_t aFlag) { return (unicode_flags(aCharacter) & aFlag) != 0u; }
        void append_unicode_case(std::u32string& aResult, char32_t aCharacter, unicode_case aCase);   // (its full case mapping)
        char32_t unicode_simple_case(char32_t aCharacter, unicode_case aCase);                         // (CPython's: its full mapping's first)
        int unicode_decimal(char32_t aCharacter);       // (-1: none)
        int unicode_digit(char32_t aCharacter);
        bool unicode_name(char32_t aCharacter, std::string& aResult);
        bool unicode_lookup(std::string_view aName, char32_t& aResult);   // (a name or an alias (\N{...}))

        // --- str's methods (str.cpp)

        object* str_method(object* aSelf, char const* aName);    // (a bound method (aSelf null: unbound), or null if str has no method aName)
        object* str_maketrans(std::vector<object*> const& aArgs, object* aKwargs);
        std::u32string to_code_points(std::string_view aText);
        std::size_t code_point_count(std::string const& aText);
        std::string from_code_points(std::u32string_view aText);
        void append_code_point(std::string& aText, char32_t aCodePoint);
        char32_t char_lower(char32_t aCharacter);
        char32_t char_upper(char32_t aCharacter);
        bool char_is_space(char32_t aCharacter);
        bool char_is_alnum(char32_t aCharacter);
        bool char_is_decimal(char32_t aCharacter);
        bool char_is_linebreak(char32_t aCharacter);

        // --- list's, tuple's and dict's other methods, and dict's views (containers.cpp)

        object* container_method(object* aSelf, int aKind, char const* aName);  // (as str_method)
        // (a special method of a builtin type (e.g. __getitem__): aKind: the kind of aSelf (-1: any, unbound (aSelf null)); null if none)
        object* protocol_method(object* aSelf, int aKind, char const* aName);
        object* builtin_value(object* aObject);     // (an instance of a subclass of a builtin type (e.g. dict): its value (borrowed); else aObject)
        object* special_method(object* aObject, char const* aName);  // (a user class's method, bound (null if none: no exception))
        bool has_special_method(object* aObject, char const* aName);  // (whether a user class's instance has the method (special_method's))
        bool dict_update(object* aDict, object* aOther);
        object* dict_iterator(object* aDict, int aWhich);   // (0: keys, 1: values, 2: items)     // (a mapping or an iterable of pairs)
        void dict_erase(dict_object& aDict, std::size_t aPosition);
        object* b_sorted(std::vector<object*> const& aArgs, object* aKwargs);

        // --- struct sequences (structseq.cpp): e.g. time.struct_time (aVisible: the number of fields that are items)
        struct structseq_descriptor
        {
            std::string qualifiedName;
            std::string name;
            std::vector<std::string> fields;
            std::size_t visible = 0u;
            neos_py_type type{};
        };
        structseq_descriptor const& structseq_type(std::string const& aQualifiedName, std::vector<std::string> const& aFields, std::size_t aVisible);
        object* make_structseq(structseq_descriptor const& aDescriptor, std::vector<object*> aValues);  // (steals the values)
        bool structseq_items(object* aObject, std::vector<object*>& aItems);                          // (borrowed)

        object* make_exception(std::string aType, std::vector<object*> aArgs);    // (steals the arguments)
        char const* exception_type_name(object* aObject);   // (an exception's class's name: null if it is not an exception)
        std::vector<object*> const& tuple_items(object* aTuple);
        object* fn_caught(char const* aText);           // (the exception being handled, from its text (a new reference): fn_handled ends its handling)
        void fn_handled();
        object* b_with_items(std::vector<object*> const& aArgs, object* aKwargs);    // (support.cpp)
        object* b_match(std::vector<object*> const& aArgs, object* aKwargs);
        object* make_callable_iterator(object* aCallable, object* aSentinel); // (iter(callable, sentinel))
        object* make_plain_object();                    // (object())
        object* b_annotate(std::vector<object*> const& aArgs, object* aKwargs);   // (__annotate(function, annotations, sources))
        bool set_function_annotations(object* aFunction, object* aMaker, object* aSources);
        object* b_type_param(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_type_alias(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_generic_base(std::vector<object*> const& aArgs, object* aKwargs);
        object* types_generic_alias_subs(object* aAlias, object* aItem);
        bool is_type_alias_type(object* aObject);    // (the arguments of a types.GenericAlias alias[item] (see typing.cpp))
        object* make_string_formatter();                 // (string.Formatter (string_formatter.cpp): a new reference)
        object* b_generic_function(std::vector<object*> const& aArgs, object* aKwargs);
        // (exec.cpp)
        object* fn_module_namespace(char const* aName, object* aGetter);
        object* fn_global_lookup(char const* aModule, char const* aName);
        void fn_import_star(char const* aModule, object* aFrom);
        void fn_register_namespace(char const* aName, object* aGetter);
        object* fn_exec_lookup(char const* aName);
        object* fn_exec_globals();
        object* fn_exec_read(char const* aName, object* aValue);
        void fn_exec_store(char const* aName, object* aValue);
        object* fn_sorted_keys(object* aMapping);
        object* b_exec_result(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_compile(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_exec(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_import(std::vector<object*> const& aArgs, object* aKwargs);   // (__import__ (exec.cpp))
        object* b_eval(std::vector<object*> const& aArgs, object* aKwargs);
        void* current_program();                        // (the program that runs (exec's): null for the program)
        void* switch_program(void* aProgram);           // (the previous one)
        void* program_context(void* aProgram);          // (a function's program (exec's, or null: the program) as the host's context (null: running))
        object* exec_current_globals();                 // (borrowed: null if none)
        object* exec_globals_of(void* aProgram);        // (the globals of the innermost exec running a program (borrowed): null if none)
        void exec_enter(object* aGlobals);              // (a function of code that exec ran: its globals)
        void exec_leave();
        void fn_set_update(object* aSet, object* aIterable);
        void fn_dict_merge(object* aDict, object* aMapping);
        void fn_ns_set(object* aDict, char const* aName, object* aValue);     // (a class body's namespace: a bound name (aValue null: unbound))
        void fn_ns_cell(object* aDict, char const* aName, object* aCell);     // (a cell's value, if it has one)
        void fn_ns_classcell(object* aDict, object* aCell);                   // (__classcell__)
        void fn_unbound(char const* aName, std::int32_t aKind);
        object* make_generic_alias(object* aOrigin, object* aArgs);   // (types.GenericAlias: e.g. list[int])
        bool is_generic_alias(object* aObject);
        object* make_mapping_proxy(object* aMapping);   // (types.MappingProxyType: a read-only view)
        object* proxied_mapping(object* aObject);       // (a mapping proxy's mapping (borrowed); else the object)
        char const* wrapper_type_name(object* aObject);   // (staticmethod, classmethod, property or cached_property: null if not one)
        bool is_union(object* aObject);                 // (types.UnionType (X | Y))
        object* union_args(object* aUnion);             // (its types: a tuple (borrowed))
        bool is_union_operand(object* aObject);
        object* make_union(object* aLhs, object* aRhs);
        // (as an unhandled exception's report shows it (traceback.format_exception's; aColorize: with ANSI colors))
        std::string exception_report(object* aException, std::set<object*>& aSeen, bool aColorize = false);
        bool traceback_colorize();                      // (whether reports of unhandled exceptions are colored (traceback.cpp))
        // (the program's frames (the host's: see neos_rt_host::frames), innermost first (support.cpp))
        using frame_stack = std::shared_ptr<std::vector<neos_rt_frame> const>;
        frame_stack capture_frames();                   // (the calling thread's: null if the host has none)
        object* make_traceback();                       // (an exception's __traceback__ before it is caught (support.cpp): it has no frame)
        object* make_traceback(frame_stack const& aStack, std::size_t aIndex, object* aNext); // (a frame's (steals aNext))
        neos_rt_frame const* traceback_frame(object* aTraceback); // (a traceback's frame: null if it has none)
        object* make_frame(frame_stack const& aStack, std::size_t aIndex); // (a frame object)
        void frames_reported(std::string const& aText);  // (an exception (its text) that the program raised: the frames it is raised in)
        object* frames_caught(object* aTraceback, std::string const& aText, bool aPropagates = false); // (an exception (its text) that is 
                                                        // caught: its traceback, from the traceback it has (aTraceback: borrowed); null: unchanged; 
                                                        // aPropagates: it propagates once it is handled (e.g. by a with statement's __exit__): the 
                                                        // frames it was raised in are kept for the code that catches it)
        void explicit_raise();                          // (the exception raised next is raised by a raise statement (not re-raised))
        void entering_frame();                          // (the exception raised next is raised as the innermost frame is entered (it is not in its traceback))
        long long recursion_limit();                    // (sys.getrecursionlimit() (sys.cpp))
        void fn_depth_check(std::int32_t aDepth);       // (the depth of calls: RecursionError beyond the limit)
        object* call_method_named(object* aObject, char const* aName, std::vector<object*> aArgs);  // (steals the arguments)
        object* fn_import(char const* aName);
        bool write_out(std::string const& aText);      // (false: an exception (e.g. BrokenPipeError) has been raised)

        object* imported_modules();                     // (sys.modules: a new reference)
        bool modules_imported(char const* aName);
        object* handled_exception();                    // (the exception being handled (borrowed), or null)
        void extend_sys(std::vector<std::pair<std::string, object*>>& aAttributes);  // (sys.cpp)

        // --- helpers of the modules of the standard library

        inline object* new_reference(object* aObject) { retain(aObject); return aObject; }
        // (a call: steals the arguments)
        inline object* invoke(object* aCallable, std::vector<object*> aArgs, object* aKwargs = nullptr)
        {
            ref const args{ make_tuple(std::move(aArgs)) };
            return call(aCallable, args.get(), aKwargs);
        }
        // (an attribute of a module (importing it): a new reference)
        inline object* module_attribute(char const* aModule, char const* aName)
        {
            ref const module{ fn_import(aModule) };
            return module ? getattr(module.get(), aName) : nullptr;
        }
        // (a class whose methods are builtin functions (aMethods), with other attributes (aExtra: new references, which it steals))
        template <std::size_t N>
        inline object* make_class_of(std::string const& aModule, std::string const& aName, std::vector<object*> const& aBases,
            builtin_entry const (&aMethods)[N], std::vector<std::pair<std::string, object*>> aExtra = {})
        {
            for (auto const& m : aMethods)
                aExtra.emplace_back(m.name, make_builtin(m));
            auto const result = make_class(aModule, aName, aBases, aExtra);
            for (auto& e : aExtra)
                release(e.second);
            return result;
        }
        // (the method's self and its other arguments, as parse_arguments checks them)
        inline bool self_arguments(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames,
            std::size_t aRequired, std::size_t aPositionalOnly, std::vector<object*>& aResult)
        {
            if (aArgs.empty())
                return (error("TypeError", std::string{ aFunction } + "() needs an argument"), false);
            return parse_arguments(aFunction, std::vector<object*>{ aArgs.begin() + 1, aArgs.end() }, aKwargs, aNames, aRequired, aPositionalOnly, aResult);
        }
        inline std::string const* str_of_value(object* aObject) { return kind_of(aObject) == NEOS_PY_STR ? &as<str_object>(aObject)->value : nullptr; }
        // (a dict of keyword arguments (the values are borrowed; null values are left out))
        inline ref keywords(std::initializer_list<std::pair<char const*, object*>> aItems)
        {
            ref result{ make_dict() };
            for (auto const& i : aItems)
                if (i.second != nullptr)
                {
                    ref const key{ make_str(i.first) };
                    dict_set(result.get(), key.get(), i.second);
                }
            return result;
        }
        // (the exception that was raised: taken (it is no longer raised))
        inline ref take_exception()
        {
            std::string const text = error_text();
            clear_error();
            ref e{ fn_caught(text.c_str()) };
            fn_handled();
            clear_error();
            return e;
        }
        inline bool exception_is(object* aException, char const* aClass)
        {
            ref const cls{ exception_class(aClass) };
            ref const r{ b_isinstance({ aException, cls.get() }, nullptr) };
            clear_error();
            return r && truth(r.get()) == 1;
        }
        inline std::string text_of(object* aObject)        // (str(); "" if it raises)
        {
            std::string text;
            if (!str_of(aObject, text))
                clear_error();
            return text;
        }
        inline std::string repr_text(object* aObject)      // (repr(); "" if it raises)
        {
            std::string text;
            if (!repr_of(aObject, text))
                clear_error();
            return text;
        }
        // (a module's exception class (e.g. "shutil.Error"), made when it is first needed: a new reference)
        inline object* module_exception(char const* aQualifiedName, std::vector<std::string> aBases)
        {
            if (!is_exception_class(aQualifiedName))
                user_exception_bases()[aQualifiedName] = std::move(aBases);
            return exception_class(aQualifiedName);
        }
        inline int attribute_truth(object* aObject, char const* aName)  // (-1: an exception)
        {
            ref const value{ getattr(aObject, aName) };
            return value ? truth(value.get()) : -1;
        }
        inline bool attribute_int(object* aObject, char const* aName, long long& aResult)
        {
            ref const value{ getattr(aObject, aName) };
            return value && to_index(value.get(), aResult);
        }
        void run_exit_functions();                      // (atexit's: the program ends (services.cpp))
        bool signals_check();                           // (the Python handlers of signals that have arrived are called: false if one raised)
        void signals_start();                           // (the signals ignored from the start (SIGPIPE: BrokenPipeError instead))
        void fn_finish();

        // --- files (io.cpp)

        object* sys_stdout();                           // (sys.stdout (a new reference), or null if sys has not been imported)
        bool is_standard_output(object* aFile);         // (the process's standard output (print writes it directly))
        object* standard_stream(int aStream);           // (sys.stdin, stdout and stderr: 0, 1 and 2 (new references))
        object* b_open(std::vector<object*> const& aArgs, object* aKwargs);
        // (OSError (FileNotFoundError, ...) for an errno value, with a filename)
        std::nullptr_t raise_os_error(int aErrno, object* aFilename = nullptr, object* aFilename2 = nullptr);
        char const* errno_exception_type(int aErrno);   // (the subclass of OSError of an errno value (io.cpp))

        // --- threads (threads.cpp): the global interpreter lock (once a thread has been started): released around blocking 
        // operations; gil_check lets a thread that has waited for it run
        void gil_acquire();
        void gil_release();
        void gil_check();
        void threads_end();

        // --- generators (generators.cpp)

        object* b_generator(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_yield(std::vector<object*> const& aArgs, object* aKwargs);
        object* fn_yield(object* aValue);
        object* b_yield_from(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_coroutine(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_async_generator(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_await(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_async_iter(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_async_with(std::vector<object*> const& aArgs, object* aKwargs);
        object* fn_awaitable_iterator(object* aAwaitable);   // (an awaitable's iterator (a new reference))
        int fn_await_step(object* aIterator, object* aSent, object* aThrown, object*& aValue);  // (1: yielded, 0: finished (its result), -1: an exception)
        bool fn_is_coroutine(object* aObject);
        void generators_end();
        bool generator_stop(object* aIterator);

        // --- set and frozenset (set.cpp)

        object* b_set(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_frozenset(std::vector<object*> const& aArgs, object* aKwargs);
        object* fn_set();
        void fn_set_add(object* aSet, object* aItem);
        void fn_set_of_constants(object* aSet);
    }
}
