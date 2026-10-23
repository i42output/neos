/*
  pickle.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/_pickle.c, Modules/clinic/_pickle.c.h, Objects/picklebufobject.c,
  Lib/pickle.py and Lib/_compat_pickle.py), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used
  under the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C and Python to
  C++ for the neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The modules _pickle (CPython 3.13's Modules/_pickle.c: Pickler, Unpickler, their memos' proxies, the exceptions, dump, dumps, load and
// loads; PickleBuffer (Objects/picklebufobject.c)), pickle (Lib/pickle.py: its constants and helpers; its classes and functions are
// _pickle's, as CPython's pickle imports them from _pickle; _Pickler and _Unpickler, pickle.py's own implementations, are _pickle's
// classes too) and _compat_pickle (Lib/_compat_pickle.py's mappings). The pickler's output buffer and frames, its memo (keyed by object
// identity), and the unpickler's stack, marks and memo are native, as _pickle.c's are; what the pickler writes for each object (and in
// what order it tries an object's reductions: reducer_override, the dispatch tables, __reduce_ex__ and __reduce__) and what the unpickler
// does for each opcode, and their messages, are _pickle.c's. Pickler and Unpickler are classes (made by type) of closures, so a program's
// subclasses override persistent_id, reducer_override, dispatch_table, find_class and persistent_load; an instance's native state is
// kept (by its identity) until it is destroyed (a weak reference's callback).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pickle_docs.hpp"
#include "compat_pickle_tables.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        // --- the opcodes (_pickle.c's)

        enum opcode : unsigned char
        {
            MARK = '(', STOP = '.', POP = '0', POP_MARK = '1', DUP = '2', FLOAT = 'F', INT = 'I', BININT = 'J', BININT1 = 'K', LONG = 'L',
            BININT2 = 'M', NONE = 'N', PERSID = 'P', BINPERSID = 'Q', REDUCE = 'R', STRING = 'S', BINSTRING = 'T', SHORT_BINSTRING = 'U',
            UNICODE_ = 'V', BINUNICODE = 'X', APPEND = 'a', BUILD = 'b', GLOBAL = 'c', DICT = 'd', EMPTY_DICT = '}', APPENDS = 'e', GET = 'g',
            BINGET = 'h', INST = 'i', LONG_BINGET = 'j', LIST = 'l', EMPTY_LIST = ']', OBJ = 'o', PUT = 'p', BINPUT = 'q', LONG_BINPUT = 'r',
            SETITEM = 's', TUPLE = 't', EMPTY_TUPLE = ')', SETITEMS = 'u', BINFLOAT = 'G',
            // (protocol 2)
            PROTO = 0x80, NEWOBJ = 0x81, EXT1 = 0x82, EXT2 = 0x83, EXT4 = 0x84, TUPLE1 = 0x85, TUPLE2 = 0x86, TUPLE3 = 0x87, NEWTRUE = 0x88,
            NEWFALSE = 0x89, LONG1 = 0x8a, LONG4 = 0x8b,
            // (protocol 3)
            BINBYTES = 'B', SHORT_BINBYTES = 'C',
            // (protocol 4)
            SHORT_BINUNICODE = 0x8c, BINUNICODE8 = 0x8d, BINBYTES8 = 0x8e, EMPTY_SET = 0x8f, ADDITEMS = 0x90, FROZENSET = 0x91, NEWOBJ_EX = 0x92,
            STACK_GLOBAL = 0x93, MEMOIZE = 0x94, FRAME = 0x95,
            // (protocol 5)
            BYTEARRAY8 = 0x96, NEXT_BUFFER = 0x97, READONLY_BUFFER = 0x98
        };

        constexpr int kHighestProtocol = 5;
        constexpr int kDefaultProtocol = 4;
        constexpr std::size_t kBatchSize = 1000;        ///< (the items that APPENDS, SETITEMS and ADDITEMS add at a time)
        constexpr int kFastNestingLimit = 50;           ///< (fast mode: the nesting beyond which cyclic objects are looked for)
        constexpr std::size_t kWriteBufSize = 4096;
        constexpr long long kPrefetch = 8192 * 16;      ///< (the data that the unpickler peeks at, if the file can peek)
        constexpr std::size_t kFrameSizeMin = 4;
        constexpr std::size_t kFrameSizeTarget = 64 * 1024;
        constexpr std::size_t kFrameHeaderSize = 9;
        constexpr int kRecursionLimit = 10000;          ///< (CPython 3.13's limit of the recursion of its C code: the pickler's)
        constexpr long long kReadWholeLine = -1;

        char const* const sModule = "_pickle";

        // --- the module's state (_pickle.c's PickleState): made as _pickle is first imported

        struct module_state
        {
            bool made = false;
            ref pickleError, picklingError, unpicklingError;
            ref dispatchTable;          ///< (copyreg.dispatch_table)
            ref extensionRegistry;      ///< (copyreg._extension_registry: {(module_name, name): code})
            ref invertedRegistry;       ///< (copyreg._inverted_registry: {code: (module_name, name)})
            ref extensionCache;         ///< (copyreg._extension_cache: {code: object})
            ref nameMapping2to3, importMapping2to3, nameMapping3to2, importMapping3to2;   ///< (_compat_pickle's)
            ref codecsEncode;           ///< (codecs.encode)
            ref getattrFunction;        ///< (builtins.getattr)
            ref partial;                ///< (functools.partial)
            ref typeType, functionType, noneType, ellipsisType, notImplementedType, bytesClass, bytearrayClass, setClass, frozensetClass;
            ref picklerClass, unpicklerClass, pickleBufferClass;
            object* persistentIdFunction = nullptr;     ///< (Pickler.persistent_id, Unpickler.persistent_load and Unpickler.find_class: the
            object* persistentLoadFunction = nullptr;   ///< classes' own (a subclass may override them))
            object* findClassFunction = nullptr;
            neos_rt_object_type const* bytesType = nullptr;      ///< (the types of bytes, bytearray, set and frozenset (bytes.cpp's and
            neos_rt_object_type const* bytearrayType = nullptr;  ///< set.cpp's): an object of one is that type's own (not a subclass's))
            neos_rt_object_type const* setType = nullptr;
            neos_rt_object_type const* frozensetType = nullptr;
        };
        module_state& state()
        {
            static module_state* sState = new module_state{};
            return *sState;
        }

        [[noreturn]] void raise_error(char const* aType, std::string const& aMessage)
        {
            error(aType, aMessage);
            throw raised{};
        }
        [[noreturn]] void raise_instance(object* aClass, std::string const& aMessage)
        {
            ref const e{ invoke(aClass, { make_str(aMessage) }) };
            if (e)
                fn_raise(e.get());
            throw raised{};
        }
        [[noreturn]] void pickling_error(std::string const& aMessage) { raise_instance(state().picklingError.get(), aMessage); }
        [[noreturn]] void unpickling_error(std::string const& aMessage) { raise_instance(state().unpicklingError.get(), aMessage); }
        object* checked(object* aObject) { return py_checked(aObject); }

        std::string repr_string(object* aObject)
        {
            std::string text;
            py_checked(repr_of(aObject, text));
            return text;
        }
        std::string str_string(object* aObject)
        {
            std::string text;
            py_checked(str_of(aObject, text));
            return text;
        }
        // (the name of an object's type (CPython's tp_name, as its messages have it: at most 200 characters))
        std::string type_text(object* aObject)
        {
            std::string name = type_name(aObject);
            if (name.size() > 200u)
                name.resize(200u);
            return name;
        }
        bool is_str(object* aObject) { return kind_of(builtin_value(aObject)) == NEOS_PY_STR; }   // (PyUnicode_Check: str or a subclass's)
        std::string const& str_value(object* aObject) { return as<str_object>(builtin_value(aObject))->value; }
        // (PyTuple_Check: a tuple or a subclass's instance: its items (null if not one))
        std::vector<object*> const* tuple_view(object* aObject)
        {
            auto const value = builtin_value(aObject);
            return kind_of(value) == NEOS_PY_TUPLE ? &as<tuple_object>(value)->items : nullptr;
        }
        bool is_dict(object* aObject) { return kind_of(builtin_value(aObject)) == NEOS_PY_DICT; }  // (PyDict_Check)
        bool is_type(object* aObject)   // (PyType_Check)
        {
            if (is_class(aObject) || is_type_object(aObject))
                return true;
            ref const cls{ checked(class_of(aObject)) };
            return cls.get() == state().typeType.get() || is_subclass(cls.get(), state().typeType.get());
        }
        // (getattr(aObject, aName), or null if it has no such attribute (other exceptions are raised): PyObject_GetOptionalAttr)
        ref optional_attr(object* aObject, char const* aName)
        {
            ref value{ optional_attribute(aObject, aName) };
            if (!value && failed())
            {
                ref const e{ take_exception() };
                if (!exception_is(e.get(), "AttributeError"))
                {
                    reraise_exception(e.get());
                    throw raised{};
                }
            }
            return value;
        }
        ref call1(object* aCallable, object* aArgument) { return ref{ checked(invoke(aCallable, { new_reference(aArgument) })) }; }

        // --- argument clinic's binding of arguments (its messages)

        struct signature
        {
            char const* function;                   ///< (its name, as the messages have it)
            std::vector<char const*> names;
            std::size_t positionalOnly;             ///< (the first parameters that are positional-only)
            std::size_t positional;                 ///< (the parameters that can be positional: the others are keyword-only)
            std::size_t required;                   ///< (the first parameters that are required)
        };
        // (the arguments of the parameters (null: not given))
        using bound_arguments = std::array<object*, 6>;
        bound_arguments bind(signature const& aSignature, std::vector<object*> const& aArgs, object* aKwargs)
        {
            auto const& names = aSignature.names;
            std::size_t const nargs = aArgs.size();
            std::size_t const nkw = aKwargs != nullptr ? as<dict_object>(aKwargs)->items.size() : 0u;
            std::size_t const maxargs = names.size();
            std::size_t const maxpos = aSignature.positional;
            std::size_t const minpos = std::min(aSignature.required, maxpos);
            std::size_t const minposonly = std::min(aSignature.required, aSignature.positionalOnly);
            auto const fname = [&aSignature]() { return std::string{ aSignature.function } + "()"; };
            auto const plural = [](std::size_t n) { return n == 1u ? "" : "s"; };
            if (nargs + nkw > maxargs)
                raise_error("TypeError", fname() + " takes at most " + std::to_string(maxargs) + " " + (nargs == 0u ? "keyword " : "") + "argument" +
                    plural(maxargs) + " (" + std::to_string(nargs + nkw) + " given)");
            if (nargs > maxpos)
            {
                if (maxpos == 0u)
                    raise_error("TypeError", fname() + " takes no positional arguments");
                raise_error("TypeError", fname() + " takes " + (minpos < maxpos ? "at most " : "exactly ") + std::to_string(maxpos) + " positional argument" +
                    plural(maxpos) + " (" + std::to_string(nargs) + " given)");
            }
            if (nargs < minposonly)
                raise_error("TypeError", fname() + " takes " + (minposonly < maxpos ? "at least " : "exactly ") + std::to_string(minposonly) +
                    " positional argument" + plural(minposonly) + " (" + std::to_string(nargs) + " given)");
            bound_arguments result{};
            for (std::size_t i = 0u; i < nargs; ++i)
                result[i] = aArgs[i];
            std::size_t used = 0u;
            if (nkw != 0u)
                for (auto const& item : as<dict_object>(aKwargs)->items)
                {
                    auto const& key = as<str_object>(item.first)->value;
                    std::size_t i = aSignature.positionalOnly;
                    while (i < maxargs && key != names[i])
                        ++i;
                    if (i == maxargs)
                        raise_error("TypeError", fname() + " got an unexpected keyword argument '" + key + "'");
                    if (i < nargs)
                        raise_error("TypeError", "argument for " + fname() + " given by name ('" + key + "') and position (" + std::to_string(i + 1u) + ")");
                    result[i] = item.second;
                    ++used;
                }
            for (std::size_t i = nargs; i < aSignature.required; ++i)
                if (result[i] == nullptr)
                    raise_error("TypeError", fname() + " missing required argument '" + names[i] + "' (pos " + std::to_string(i + 1u) + ")");
            return result;
        }
        // (a str argument (argument clinic's str converter): its text)
        std::string str_argument(char const* aFunction, char const* aName, object* aValue, char const* aDefault)
        {
            if (aValue == nullptr)
                return aDefault;
            if (kind_of(aValue) != NEOS_PY_STR && !is_str(aValue))
                raise_error("TypeError", std::string{ aFunction } + "() argument '" + aName + "' must be str, not " + type_text(aValue));
            auto const& text = str_value(aValue);
            if (text.find('\0') != std::string::npos)
                raise_error("ValueError", "embedded null character");
            return text;
        }
        bool bool_argument(object* aValue, bool aDefault)
        {
            if (aValue == nullptr)
                return aDefault;
            auto const t = truth(aValue);
            py_checked(t >= 0);
            return t == 1;
        }

        // --- the pickler's memo (_pickle.c's PyMemoTable: an object's identity -> its index; it holds a reference to each object)

        class memo_table
        {
        public:
            struct entry
            {
                object* key = nullptr;
                std::size_t value = 0u;
            };
        public:
            memo_table() : iTable(kMinSize) {}
            memo_table(memo_table const& aOther) : iTable{ aOther.iTable }, iUsed{ aOther.iUsed }
            {
                for (auto const& e : iTable)
                    retain(e.key);
            }
            memo_table& operator=(memo_table const& aOther)
            {
                if (this != &aOther)
                {
                    memo_table copy{ aOther };
                    std::swap(iTable, copy.iTable);
                    std::swap(iUsed, copy.iUsed);
                }
                return *this;
            }
            ~memo_table() { release_keys(); }
        public:
            std::size_t size() const { return iUsed; }
            std::size_t allocated() const { return iTable.size(); }
            std::size_t const* find(object* aKey) const
            {
                auto const& e = iTable[slot(aKey)];
                return e.key != nullptr ? &e.value : nullptr;
            }
            void set(object* aKey, std::size_t aValue)
            {
                auto& e = iTable[slot(aKey)];
                if (e.key != nullptr)
                {
                    e.value = aValue;
                    return;
                }
                retain(aKey);
                e.key = aKey;
                e.value = aValue;
                ++iUsed;
                // (as PyMemoTable: kept at most 2/3 full; grown by 4 (2 if large))
                if (iUsed * 3u >= iTable.size() * 2u)
                    resize(iUsed > 50000u ? iUsed * 2u : iUsed * 4u);
            }
            void clear()
            {
                release_keys();
                iTable.assign(kMinSize, entry{});
                iUsed = 0u;
            }
            std::vector<entry> const& entries() const { return iTable; }
        private:
            static constexpr std::size_t kMinSize = 8u;
            static std::size_t hash(object* aKey)
            {
                auto x = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(aKey));
                x ^= x >> 33;
                x *= 0xff51afd7ed558ccdull;
                x ^= x >> 33;
                return static_cast<std::size_t>(x);
            }
            std::size_t slot(object* aKey) const
            {
                std::size_t const mask = iTable.size() - 1u;
                std::size_t i = hash(aKey) & mask;
                while (iTable[i].key != nullptr && iTable[i].key != aKey)
                    i = (i + 1u) & mask;
                return i;
            }
            void resize(std::size_t aMinimum)
            {
                std::size_t size = kMinSize;
                while (size <= aMinimum)
                    size <<= 1u;
                std::vector<entry> old{ size };
                std::swap(old, iTable);
                for (auto const& e : old)
                    if (e.key != nullptr)
                        iTable[slot(e.key)] = e;
            }
            void release_keys()
            {
                for (auto& e : iTable)
                {
                    release(e.key);
                    e.key = nullptr;
                }
            }
        private:
            std::vector<entry> iTable;
            std::size_t iUsed = 0u;
        };

        // (a limit of the depth of the pickler's recursion (_Py_EnterRecursiveCall's))
        struct recursion_guard
        {
            explicit recursion_guard(int& aDepth) : depth{ aDepth }
            {
                if (++depth > kRecursionLimit)
                {
                    --depth;
                    raise_error("RecursionError", "maximum recursion depth exceeded while pickling an object");
                }
            }
            ~recursion_guard() { --depth; }
            int& depth;
        };

        void write_size64(char* aOut, std::uint64_t aValue)
        {
            for (int i = 0; i < 8; ++i)
                aOut[i] = static_cast<char>((aValue >> (8 * i)) & 0xffu);
        }
        void write_size32(char* aOut, std::uint64_t aValue)
        {
            for (int i = 0; i < 4; ++i)
                aOut[i] = static_cast<char>((aValue >> (8 * i)) & 0xffu);
        }

        // (a UTF-8 text's code points (a lone surrogate is a 3-byte sequence, as "surrogatepass" encodes it))
        template <typename F>
        void for_each_code_point(std::string_view aText, F&& aFunction)
        {
            std::size_t i = 0u;
            while (i < aText.size())
            {
                auto const c = static_cast<unsigned char>(aText[i]);
                char32_t cp = c;
                std::size_t n = 1u;
                if (c >= 0xf0u && i + 3u < aText.size())
                {
                    cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(aText[i + 1u]) & 0x3fu) << 12) | ((static_cast<unsigned char>(aText[i + 2u]) & 0x3fu) << 6) |
                        (static_cast<unsigned char>(aText[i + 3u]) & 0x3fu);
                    n = 4u;
                }
                else if (c >= 0xe0u && i + 2u < aText.size())
                {
                    cp = ((c & 0x0fu) << 12) | ((static_cast<unsigned char>(aText[i + 1u]) & 0x3fu) << 6) | (static_cast<unsigned char>(aText[i + 2u]) & 0x3fu);
                    n = 3u;
                }
                else if (c >= 0xc0u && i + 1u < aText.size())
                {
                    cp = ((c & 0x1fu) << 6) | (static_cast<unsigned char>(aText[i + 1u]) & 0x3fu);
                    n = 2u;
                }
                aFunction(cp);
                i += n;
            }
        }
        // (a str of UTF-8 data (as PyUnicode_DecodeUTF8(data, size, aErrors)): ASCII is made directly)
        object* decode_utf8(char const* aData, std::size_t aSize, char const* aErrors)
        {
            bool ascii = true;
            for (std::size_t i = 0u; i < aSize && ascii; ++i)
                ascii = static_cast<unsigned char>(aData[i]) < 0x80u;
            if (ascii)
                return make_str(std::string{ aData, aSize }, aSize);
            std::string text;
            py_checked(decode(std::string_view{ aData, aSize }, "utf-8", aErrors, text));
            return make_str(std::move(text));
        }

        // (a str that CPython interns or shares: an identifier-like constant (all its characters are name characters; a short one), '' or 
        // a Latin-1 character)
        bool internable(std::string const& aText)
        {
            if (aText.size() > 40u)
                return false;
            if (aText.empty() || aText.size() == 1u || (aText.size() == 2u && static_cast<unsigned char>(aText[0]) <= 0xc3u && static_cast<unsigned char>(aText[0]) >= 0xc2u))
                return true;
            static auto const sNameCharacters = []()
            {
                std::array<bool, 256> result{};
                for (int c = 0; c < 256; ++c)
                    result[static_cast<std::size_t>(c)] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
                return result;
            }();
            for (auto c : aText)
                if (!sNameCharacters[static_cast<unsigned char>(c)])
                    return false;
            return true;
        }

        // (the memo's strs that CPython would intern, by value (an open-addressing table of their positions in the memo, by their hashes))
        class interned_table
        {
        public:
            std::size_t const* find(object* aText) const
            {
                if (iUsed == 0u)
                    return nullptr;
                auto const h = hash(aText);
                auto const& text = as<str_object>(aText)->value;
                std::size_t const mask = iTable.size() - 1u;
                for (std::size_t i = h & mask;; i = (i + 1u) & mask)
                {
                    auto const& e = iTable[i];
                    if (e.text == nullptr)
                        return nullptr;
                    if (e.hash == h && *e.text == text)
                        return &e.value;
                }
            }
            void insert(object* aText, std::size_t aValue)
            {
                if ((iUsed + 1u) * 2u > iTable.size())
                    grow();
                insert(&as<str_object>(aText)->value, hash(aText), aValue);
            }
            std::pair<std::size_t, bool> find_or_insert(object* aText, std::size_t aValue)    // (its value, and whether it was found (else aValue: inserted))
            {
                if ((iUsed + 1u) * 2u > iTable.size())
                    grow();
                auto const h = hash(aText);
                auto const& text = as<str_object>(aText)->value;
                std::size_t const mask = iTable.size() - 1u;
                for (std::size_t i = h & mask;; i = (i + 1u) & mask)
                {
                    auto& e = iTable[i];
                    if (e.text == nullptr)
                    {
                        e = entry{ &text, h, aValue };
                        ++iUsed;
                        return { aValue, false };
                    }
                    if (e.hash == h && *e.text == text)
                        return { e.value, true };
                }
            }
            void clear()
            {
                iTable.clear();
                iUsed = 0u;
            }
        private:
            struct entry
            {
                std::string const* text = nullptr;  ///< (its memoized str's text)
                std::size_t hash = 0u;
                std::size_t value = 0u;
            };
            static std::size_t hash(object* aText)
            {
                long long h = 0;
                hash_of(aText, h);      // (a str's: found once)
                auto x = static_cast<std::uint64_t>(h);
                return static_cast<std::size_t>(x ^ (x >> 31));
            }
            void insert(std::string const* aText, std::size_t aHash, std::size_t aValue)
            {
                std::size_t const mask = iTable.size() - 1u;
                for (std::size_t i = aHash & mask;; i = (i + 1u) & mask)
                {
                    auto& e = iTable[i];
                    if (e.text == nullptr)
                    {
                        e = entry{ aText, aHash, aValue };
                        ++iUsed;
                        return;
                    }
                    if (e.hash == aHash && *e.text == *aText)
                        return;
                }
            }
            void grow()
            {
                std::vector<entry> old{ std::max<std::size_t>(iTable.size() * 2u, 8u) };
                std::swap(old, iTable);
                iUsed = 0u;
                for (auto const& e : old)
                    if (e.text != nullptr)
                        insert(e.text, e.hash, e.value);
            }
        private:
            std::vector<entry> iTable;
            std::size_t iUsed = 0u;
        };

        // --- the pickler (_pickle.c's PicklerObject, and its functions)

        struct pickler
        {
            memo_table memo;
            std::string output;                     ///< (the output buffer: the pickle, or its part not yet written to the file)
            long long frameStart = -1;              ///< (the position of the current frame's header (-1: none))
            int proto = 0;
            int bin = 0;
            bool framing = false;
            int fast = 0;
            int fastNesting = 0;
            bool fixImports = false;
            bool running = false;
            int depth = 0;
            ref writeMethod;                        ///< (the file's write method (null: dumps))
            ref persistentId;                       ///< (persistent_id, while dump runs (null: the class's own))
            ref reducerOverride;                    ///< (reducer_override, while dump runs)
            ref dispatchTable;                      ///< (null: copyreg.dispatch_table)
            ref bufferCallback;
            std::unordered_set<object*> fastMemo;   ///< (fast mode: the containers being pickled, beyond kFastNestingLimit)
            // (the memo's strs that CPython would intern (identifiers, '' and the Latin-1 characters: CPython's are shared objects, so
            // equal ones are found in its memo), by value)
            interned_table interned;
            bool internSuspended = false;           ///< (a builtin type's names are not CPython's interned strs)

            pickler() { output.reserve(kWriteBufSize); }

            // (output)
            void new_frame()
            {
                frameStart = static_cast<long long>(output.size());
                output.append(kFrameHeaderSize, '\xfe');
            }
            void write(char const* aData, std::size_t aSize)
            {
                if (framing && frameStart == -1)
                    new_frame();
                output.append(aData, aSize);
            }
            void write(std::string_view aData) { write(aData.data(), aData.size()); }
            void write1(char aOpcode)
            {
                if (framing && frameStart == -1)
                    new_frame();
                output.push_back(aOpcode);
            }
            void write1(opcode aOpcode) { write1(static_cast<char>(aOpcode)); }
            void commit_frame()
            {
                if (!framing || frameStart == -1)
                    return;
                auto const start = static_cast<std::size_t>(frameStart);
                std::size_t const frameLength = output.size() - start - kFrameHeaderSize;
                if (frameLength >= kFrameSizeMin)
                {
                    output[start] = static_cast<char>(FRAME);
                    write_size64(&output[start + 1u], frameLength);
                }
                else
                    output.erase(start, kFrameHeaderSize);
                frameStart = -1;
            }
            object* take_output()      // (the pickle: a bytes object)
            {
                commit_frame();
                // (the bytes object keeps its data: its buffer's excess is released (as _Pickler_GetString resizes it))
                if (output.capacity() > output.size() + output.size() / 8u + 32u)
                    output.shrink_to_fit();
                auto const result = make_bytes(std::move(output));
                output = std::string{};
                return result;
            }
            void clear_buffer()
            {
                output.clear();
                frameStart = -1;
            }
            void flush_to_file()
            {
                ref const data{ take_output() };
                call1(writeMethod.get(), data.get());
            }
            void opcode_boundary()
            {
                if (!framing || frameStart == -1)
                    return;
                if (output.size() - static_cast<std::size_t>(frameStart) - kFrameHeaderSize >= kFrameSizeTarget)
                {
                    commit_frame();
                    // (the frame is written to the file, so that the buffer does not grow (dumps: kept))
                    if (writeMethod)
                    {
                        flush_to_file();
                        clear_buffer();
                    }
                }
            }
            // (a header and its data (large data is written directly to the file, outside a frame); aPayload: the data's object (null: made
            // if it is needed))
            void write_bytes(char const* aHeader, std::size_t aHeaderSize, char const* aData, std::size_t aSize, object* aPayload)
            {
                bool const bypass = aSize >= kFrameSizeTarget;
                bool const wasFraming = framing;
                if (bypass)
                {
                    commit_frame();
                    framing = false;
                }
                write(aHeader, aHeaderSize);
                if (bypass && writeMethod)
                {
                    flush_to_file();
                    ref made;
                    if (aPayload == nullptr)
                    {
                        made = ref{ make_bytes(std::string{ aData, aSize }) };
                        aPayload = made.get();
                    }
                    call1(writeMethod.get(), aPayload);
                    clear_buffer();
                }
                else
                    write(aData, aSize);
                framing = wasFraming;
            }

            // (the memo)
            void memo_get(std::size_t aIndex)
            {
                if (bin == 0)
                {
                    write1(GET);
                    write(std::to_string(aIndex));
                    write1('\n');
                }
                else if (aIndex < 256u)
                {
                    char const data[2] = { static_cast<char>(BINGET), static_cast<char>(aIndex) };
                    write(data, 2u);
                }
                else if (aIndex <= 0xffffffffu)
                {
                    char data[5] = { static_cast<char>(LONG_BINGET) };
                    write_size32(data + 1, aIndex);
                    write(data, 5u);
                }
                else
                    pickling_error("memo id too large for LONG_BINGET");
            }
            void memo_get(object* aObject)
            {
                auto const index = memo.find(aObject);
                if (index == nullptr)
                {
                    raise_key_error(aObject);
                    throw raised{};
                }
                memo_get(*index);
            }
            void memo_put(object* aObject, bool aInterned = false)
            {
                if (fast != 0)
                    return;
                auto const index = memo.size();
                memo.set(aObject, index);
                if (aInterned)
                    interned.insert(aObject, index);
                if (proto >= 4)
                    write1(MEMOIZE);
                else if (bin == 0)
                {
                    write1(PUT);
                    write(std::to_string(index));
                    write1('\n');
                }
                else if (index < 256u)
                {
                    char const data[2] = { static_cast<char>(BINPUT), static_cast<char>(index) };
                    write(data, 2u);
                }
                else if (index <= 0xffffffffu)
                {
                    char data[5] = { static_cast<char>(LONG_BINPUT) };
                    write_size32(data + 1, index);
                    write(data, 5u);
                }
                else
                    pickling_error("memo id too large for LONG_BINPUT");
            }

            // (fast mode: a container entered (cyclic objects are found beyond kFastNestingLimit) and left)
            void fast_save_enter(object* aObject)
            {
                if (++fastNesting >= kFastNestingLimit)
                {
                    if (fastMemo.contains(aObject))
                    {
                        auto const address = object_address(aObject);
                        raise_error("ValueError", "fast mode: can't pickle cyclic objects including object type " + type_text(aObject) + " at " + address);
                    }
                    fastMemo.insert(aObject);
                }
            }
            void fast_save_leave(object* aObject)
            {
                if (fastNesting-- >= kFastNestingLimit)
                    fastMemo.erase(aObject);
            }
            struct fast_guard
            {
                fast_guard(pickler& aPickler, neos_py_object* aObject) : self{ aPickler }, target{ aObject }
                {
                    if (self.fast != 0)
                    {
                        self.fast_save_enter(target);
                        entered = true;
                    }
                }
                ~fast_guard()
                {
                    if (entered)
                        self.fast_save_leave(target);
                }
                pickler& self;
                neos_py_object* target;
                bool entered = false;
            };

            void save(object* aObject, bool aPersistent = false);
            void save_bool(object* aObject)
            {
                bool const value = aObject == make_bool(true);
                if (proto >= 2)
                    write1(value ? NEWTRUE : NEWFALSE);
                else
                    write(value ? "I01\n" : "I00\n", 4u);
            }
            void save_long(object* aObject)
            {
                long long value = 0;
                std::uint64_t unsignedValue = 0u;
                auto const range = small_int(aObject, value) ? 0 : integer_value(aObject, value, unsignedValue);
                py_checked(range >= 0);
                if (range == 0 && value >= -0x80000000ll && value <= 0x7fffffffll)
                {
                    // (a 4-byte integer)
                    if (bin != 0)
                    {
                        auto const u = static_cast<std::uint32_t>(value);
                        char data[5] = { 0, static_cast<char>(u & 0xffu), static_cast<char>((u >> 8) & 0xffu), static_cast<char>((u >> 16) & 0xffu),
                            static_cast<char>((u >> 24) & 0xffu) };
                        if (data[4] != 0 || data[3] != 0)
                        {
                            data[0] = static_cast<char>(BININT);
                            write(data, 5u);
                        }
                        else if (data[2] != 0)
                        {
                            data[0] = static_cast<char>(BININT2);
                            write(data, 3u);
                        }
                        else
                        {
                            data[0] = static_cast<char>(BININT1);
                            write(data, 2u);
                        }
                    }
                    else
                    {
                        write1(INT);
                        write(std::to_string(value));
                        write1('\n');
                    }
                    return;
                }
                if (proto >= 2)
                {
                    // (linear-time pickling: the int's little-endian two's complement bytes)
                    std::string data;
                    bool negative = false;
                    if (range == 0)
                    {
                        negative = value < 0;
                        auto const magnitude = negative ? std::uint64_t{ 0u } - static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(value);
                        int bits = 0;
                        for (auto m = magnitude; m != 0u; m >>= 1)
                            ++bits;
                        std::size_t const n = static_cast<std::size_t>(bits >> 3) + 1u;
                        for (std::size_t i = 0u; i < n; ++i)
                            data.push_back(i < 8u ? static_cast<char>((static_cast<std::uint64_t>(value) >> (8u * i)) & 0xffu) : (negative ? '\xff' : '\0'));
                    }
                    else
                    {
                        py const x = py::borrowed(aObject);
                        negative = range == 3 || (range == 2 && x < py{ 0 });
                        auto const bits = static_cast<std::uint64_t>(x.method("bit_length").as_index());
                        std::uint64_t const n = (bits >> 3) + 1u;
                        if (n > 0x7fffffffu)
                            raise_error("OverflowError", "int too large to pickle");
                        py const bytes = x.method("to_bytes", { py{ static_cast<long long>(n) }, py{ "little" } }, { { "signed", py::boolean(true) } });
                        std::string_view view;
                        bytes_like(bytes.get(), view);
                        data.assign(view.data(), view.size());
                    }
                    auto n = data.size();
                    // (a negative int's most significant byte may be redundant)
                    if (negative && n > 1u && static_cast<unsigned char>(data[n - 1u]) == 0xffu && (static_cast<unsigned char>(data[n - 2u]) & 0x80u) != 0u)
                        --n;
                    if (n < 256u)
                    {
                        char const header[2] = { static_cast<char>(LONG1), static_cast<char>(n) };
                        write(header, 2u);
                    }
                    else
                    {
                        char header[5] = { static_cast<char>(LONG4) };
                        write_size32(header + 1, n);
                        write(header, 5u);
                    }
                    write(data.data(), n);
                    return;
                }
                // (protocols 0 and 1: its repr and "L", for Python 2)
                write1(LONG);
                write(repr_string(aObject));
                write("L\n", 2u);
            }
            void save_float(object* aObject)
            {
                double const x = as<float_object>(aObject)->value;
                if (bin != 0)
                {
                    std::uint64_t u = 0u;
                    std::memcpy(&u, &x, sizeof(u));
                    char data[9] = { static_cast<char>(BINFLOAT) };
                    for (int i = 0; i < 8; ++i)
                        data[1 + i] = static_cast<char>((u >> (56 - 8 * i)) & 0xffu);
                    write(data, 9u);
                }
                else
                {
                    write1(FLOAT);
                    write(python_float(x));
                    write1('\n');
                }
            }
            void save_bytes_data(object* aObject, char const* aData, std::size_t aSize)
            {
                char header[9];
                std::size_t length = 0u;
                if (aSize <= 0xffu)
                {
                    header[0] = static_cast<char>(SHORT_BINBYTES);
                    header[1] = static_cast<char>(aSize);
                    length = 2u;
                }
                else if (aSize <= 0xffffffffu)
                {
                    header[0] = static_cast<char>(BINBYTES);
                    write_size32(header + 1, aSize);
                    length = 5u;
                }
                else if (proto >= 4)
                {
                    header[0] = static_cast<char>(BINBYTES8);
                    write_size64(header + 1, aSize);
                    length = 9u;
                }
                else
                    raise_error("OverflowError", "serializing a bytes object larger than 4 GiB requires pickle protocol 4 or higher");
                write_bytes(header, length, aData, aSize, aObject);
                memo_put(aObject);
            }
            void save_reduce(object* aArgs, object* aObject);
            void save_bytes(object* aObject)
            {
                std::string_view data;
                bytes_like(aObject, data);
                if (proto < 3)
                {
                    // (as bytes for Python 3 and as str for Python 2: codecs.encode(its latin-1 text, "latin1"))
                    auto& s = state();
                    ref reduceValue;
                    if (data.empty())
                        reduceValue = ref{ make_tuple({ new_reference(s.bytesClass.get()), make_tuple() }) };
                    else
                    {
                        std::string text;
                        text.reserve(data.size());
                        for (auto c : data)
                            append_code_point(text, static_cast<unsigned char>(c));
                        reduceValue = ref{ make_tuple({ new_reference(s.codecsEncode.get()), make_tuple({ make_str(std::move(text), data.size()), make_str("latin1") }) }) };
                    }
                    save_reduce(reduceValue.get(), aObject);
                    return;
                }
                save_bytes_data(aObject, data.data(), data.size());
            }
            void save_bytearray_data(object* aObject, char const* aData, std::size_t aSize)
            {
                char header[9] = { static_cast<char>(BYTEARRAY8) };
                write_size64(header + 1, aSize);
                write_bytes(header, 9u, aData, aSize, aObject);
                memo_put(aObject);
            }
            void save_bytearray(object* aObject)
            {
                std::string_view data;
                bytes_like(aObject, data);
                if (proto < 5)
                {
                    auto& s = state();
                    ref reduceValue;
                    if (data.empty())
                        reduceValue = ref{ make_tuple({ new_reference(s.bytearrayClass.get()), make_tuple() }) };
                    else
                        reduceValue = ref{ make_tuple({ new_reference(s.bytearrayClass.get()), make_tuple({ make_bytes(std::string{ data }) }) }) };
                    save_reduce(reduceValue.get(), aObject);
                    return;
                }
                save_bytearray_data(aObject, data.data(), data.size());
            }
            void save_picklebuffer(object* aObject);
            void save_unicode(object* aObject, bool aInterned = false)
            {
                auto const& text = as<str_object>(aObject)->value;
                if (bin != 0)
                {
                    auto const size = text.size();
                    char header[9];
                    std::size_t length = 0u;
                    if (size <= 0xffu && proto >= 4)
                    {
                        header[0] = static_cast<char>(SHORT_BINUNICODE);
                        header[1] = static_cast<char>(size);
                        length = 2u;
                    }
                    else if (size <= 0xffffffffu)
                    {
                        header[0] = static_cast<char>(BINUNICODE);
                        write_size32(header + 1, size);
                        length = 5u;
                    }
                    else if (proto >= 4)
                    {
                        header[0] = static_cast<char>(BINUNICODE8);
                        write_size64(header + 1, size);
                        length = 9u;
                    }
                    else
                        raise_error("OverflowError", "serializing a string larger than 4 GiB requires pickle protocol 4 or higher");
                    write_bytes(header, length, text.data(), size, nullptr);
                }
                else
                {
                    // (raw-unicode-escape, with '\\', '\0', '\n', '\r' and '\x1a' escaped too)
                    static char const sHex[] = "0123456789abcdef";
                    std::string encoded;
                    encoded.reserve(text.size());
                    for_each_code_point(text, [&](char32_t c)
                    {
                        if (c >= 0x10000u)
                        {
                            encoded += "\\U";
                            for (int shift = 28; shift >= 0; shift -= 4)
                                encoded.push_back(sHex[(c >> shift) & 0xfu]);
                        }
                        else if (c >= 256u || c == '\\' || c == 0u || c == '\n' || c == '\r' || c == 0x1au)
                        {
                            encoded += "\\u";
                            for (int shift = 12; shift >= 0; shift -= 4)
                                encoded.push_back(sHex[(c >> shift) & 0xfu]);
                        }
                        else
                            encoded.push_back(static_cast<char>(c));
                    });
                    write1(UNICODE_);
                    write(encoded);
                    write1('\n');
                }
                memo_put(aObject, aInterned);
            }
            void save_tuple(object* aObject)
            {
                auto const& items = as<tuple_object>(aObject)->items;
                auto const length = items.size();
                if (length == 0u)
                {
                    if (proto != 0)
                        write1(EMPTY_TUPLE);
                    else
                    {
                        char const data[2] = { static_cast<char>(MARK), static_cast<char>(TUPLE) };
                        write(data, 2u);
                    }
                    return;
                }
                // (the tuple is not in the memo: if it is once its items are pickled, it is recursive: what was pushed is popped, and the
                // memo's is pushed)
                if (length <= 3u && proto >= 2)
                {
                    for (std::size_t i = 0u; i < length; ++i)
                        save(items[i]);
                    if (memo.find(aObject) != nullptr)
                    {
                        for (std::size_t i = 0u; i < length; ++i)
                            write1(POP);
                        memo_get(aObject);
                        return;
                    }
                    static opcode const sOpcodes[] = { EMPTY_TUPLE, TUPLE1, TUPLE2, TUPLE3 };
                    write1(sOpcodes[length]);
                }
                else
                {
                    write1(MARK);
                    for (std::size_t i = 0u; i < length; ++i)
                        save(items[i]);
                    if (memo.find(aObject) != nullptr)
                    {
                        if (bin != 0)
                            write1(POP_MARK);
                        else
                            for (std::size_t i = 0u; i <= length; ++i)
                                write1(POP);
                        memo_get(aObject);
                        return;
                    }
                    write1(TUPLE);
                }
                memo_put(aObject);
            }
            // (an iterator's items: MARK item ... APPENDS, in batches)
            void batch_list(object* aIterator)
            {
                auto const next_item = [aIterator]() -> ref
                {
                    ref item{ next(aIterator) };
                    if (!item && failed())
                        throw raised{};
                    return item;
                };
                if (proto == 0)
                {
                    for (;;)
                    {
                        ref const item{ next_item() };
                        if (!item)
                            break;
                        save(item.get());
                        write1(APPEND);
                    }
                    return;
                }
                std::size_t n = 0u;
                do
                {
                    ref first{ next_item() };
                    if (!first)
                        break;
                    ref item{ next_item() };
                    if (!item)
                    {
                        save(first.get());
                        write1(APPEND);
                        break;
                    }
                    write1(MARK);
                    save(first.get());
                    n = 1u;
                    while (item)
                    {
                        save(item.get());
                        item = ref{};
                        if (++n == kBatchSize)
                            break;
                        item = next_item();
                    }
                    write1(APPENDS);
                } while (n == kBatchSize);
            }
            void batch_list_exact(object* aList)
            {
                auto const& items = as<list_object>(aList)->items;
                if (items.size() == 1u)
                {
                    ref const item{ ref::borrowed(items[0]) };
                    save(item.get());
                    write1(APPEND);
                    return;
                }
                std::size_t total = 0u;
                do
                {
                    std::size_t batch = 0u;
                    write1(MARK);
                    while (total < items.size())
                    {
                        ref const item{ ref::borrowed(items[total]) };
                        save(item.get());
                        ++total;
                        if (++batch == kBatchSize)
                            break;
                    }
                    write1(APPENDS);
                } while (total < items.size());
            }
            void save_list(object* aObject)
            {
                fast_guard const guard{ *this, aObject };
                if (bin != 0)
                    write1(EMPTY_LIST);
                else
                {
                    char const data[2] = { static_cast<char>(MARK), static_cast<char>(LIST) };
                    write(data, 2u);
                }
                auto const length = as<list_object>(aObject)->items.size();
                memo_put(aObject);
                if (length != 0u)
                {
                    if (proto > 0)
                    {
                        recursion_guard const recursion{ depth };
                        batch_list_exact(aObject);
                    }
                    else
                    {
                        ref const it{ checked(iter(aObject)) };
                        recursion_guard const recursion{ depth };
                        batch_list(it.get());
                    }
                }
            }
            // (an iterator's (key, value) pairs: MARK key value ... SETITEMS, in batches)
            void batch_dict(object* aIterator)
            {
                auto const next_item = [aIterator]() -> ref
                {
                    ref item{ next(aIterator) };
                    if (!item && failed())
                        throw raised{};
                    if (item)
                    {
                        auto const pair = tuple_view(item.get());
                        if (pair == nullptr || pair->size() != 2u)
                            raise_error("TypeError", "dict items iterator must return 2-tuples");
                    }
                    return item;
                };
                auto const save_pair = [this](object* aPair)
                {
                    auto const& pair = *tuple_view(aPair);
                    save(pair[0]);
                    save(pair[1]);
                };
                if (proto == 0)
                {
                    for (;;)
                    {
                        ref const item{ next_item() };
                        if (!item)
                            break;
                        save_pair(item.get());
                        write1(SETITEM);
                    }
                    return;
                }
                std::size_t n = 0u;
                do
                {
                    ref first{ next_item() };
                    if (!first)
                        break;
                    ref item{ next_item() };
                    if (!item)
                    {
                        save_pair(first.get());
                        write1(SETITEM);
                        break;
                    }
                    write1(MARK);
                    save_pair(first.get());
                    n = 1u;
                    while (item)
                    {
                        save_pair(item.get());
                        item = ref{};
                        if (++n == kBatchSize)
                            break;
                        item = next_item();
                    }
                    write1(SETITEMS);
                } while (n == kBatchSize);
            }
            void batch_dict_exact(object* aDict)
            {
                auto const& items = as<dict_object>(aDict)->items;
                auto const size = items.size();
                if (size == 1u)
                {
                    ref const key{ ref::borrowed(items[0].first) };
                    ref const value{ ref::borrowed(items[0].second) };
                    save(key.get());
                    save(value.get());
                    write1(SETITEM);
                    return;
                }
                std::size_t position = 0u;
                std::size_t n = 0u;
                do
                {
                    n = 0u;
                    write1(MARK);
                    while (position < items.size())
                    {
                        ref const key{ ref::borrowed(items[position].first) };
                        ref const value{ ref::borrowed(items[position].second) };
                        ++position;
                        save(key.get());
                        save(value.get());
                        if (++n == kBatchSize)
                            break;
                    }
                    write1(SETITEMS);
                    if (items.size() != size)
                        raise_error("RuntimeError", "dictionary changed size during iteration");
                } while (n == kBatchSize);
            }
            void save_dict(object* aObject)
            {
                fast_guard const guard{ *this, aObject };
                if (bin != 0)
                    write1(EMPTY_DICT);
                else
                {
                    char const data[2] = { static_cast<char>(MARK), static_cast<char>(DICT) };
                    write(data, 2u);
                }
                memo_put(aObject);
                if (!as<dict_object>(aObject)->items.empty())
                {
                    if (proto > 0)
                    {
                        recursion_guard const recursion{ depth };
                        batch_dict_exact(aObject);
                    }
                    else
                    {
                        ref const items{ checked(call_method_named(aObject, "items", {})) };
                        ref const it{ checked(iter(items.get())) };
                        recursion_guard const recursion{ depth };
                        batch_dict(it.get());
                    }
                }
            }
            void save_set(object* aObject)
            {
                auto& s = state();
                if (proto < 4)
                {
                    ref const items{ checked(invoke(ref{ checked(fn_builtin("list")) }.get(), { new_reference(aObject) })) };
                    ref const reduceValue{ make_tuple({ new_reference(s.setClass.get()), make_tuple({ new_reference(items.get()) }) }) };
                    save_reduce(reduceValue.get(), aObject);
                    return;
                }
                write1(EMPTY_SET);
                memo_put(aObject);
                auto const size = length(aObject);
                py_checked(size >= 0);
                if (size == 0)
                    return;
                ref const it{ checked(iter(aObject)) };
                std::size_t n = 0u;
                do
                {
                    n = 0u;
                    write1(MARK);
                    for (;;)
                    {
                        ref const item{ next(it.get()) };
                        if (!item)
                        {
                            if (failed())
                                throw raised{};
                            break;
                        }
                        save(item.get());
                        if (++n == kBatchSize)
                            break;
                    }
                    write1(ADDITEMS);
                    if (length(aObject) != size)
                        raise_error("RuntimeError", "set changed size during iteration");
                } while (n == kBatchSize);
            }
            void save_frozenset(object* aObject)
            {
                fast_guard const guard{ *this, aObject };
                auto& s = state();
                if (proto < 4)
                {
                    ref const items{ checked(invoke(ref{ checked(fn_builtin("list")) }.get(), { new_reference(aObject) })) };
                    ref const reduceValue{ make_tuple({ new_reference(s.frozensetClass.get()), make_tuple({ new_reference(items.get()) }) }) };
                    save_reduce(reduceValue.get(), aObject);
                    return;
                }
                write1(MARK);
                ref const it{ checked(iter(aObject)) };
                for (;;)
                {
                    ref const item{ next(it.get()) };
                    if (!item)
                    {
                        if (failed())
                            throw raised{};
                        break;
                    }
                    save(item.get());
                }
                // (recursive: what was pushed is popped, and the memo's is pushed)
                if (memo.find(aObject) != nullptr)
                {
                    write1(POP_MARK);
                    memo_get(aObject);
                    return;
                }
                write1(FROZENSET);
                memo_put(aObject);
            }
            void save_global(object* aObject, object* aName);
            void save_type(object* aObject)
            {
                auto& s = state();
                object* singleton = nullptr;
                if (aObject == s.noneType.get())
                    singleton = none();
                else if (aObject == s.ellipsisType.get())
                    singleton = &sEllipsis.base;
                else if (aObject == s.notImplementedType.get())
                    singleton = not_implemented();
                if (singleton != nullptr)
                {
                    ref const reduceValue{ make_tuple({ new_reference(s.typeType.get()), make_tuple({ new_reference(singleton) }) }) };
                    save_reduce(reduceValue.get(), aObject);
                    return;
                }
                save_global(aObject, nullptr);
            }
            // (a persistent id: true if persistent_id gave one (it is saved))
            bool save_pers(object* aObject)
            {
                ref const pid{ call1(persistentId.get(), aObject) };
                if (pid.get() == none())
                    return false;
                if (bin != 0)
                {
                    save(pid.get(), true);
                    write1(BINPERSID);
                }
                else
                {
                    auto const text = str_string(pid.get());
                    if (std::any_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80u; }))
                        pickling_error("persistent IDs in protocol 0 must be ASCII strings");
                    write1(PERSID);
                    write(text);
                    write1('\n');
                }
                return true;
            }
            void save_other(object* aObject);
            void dump(object* aSelf, object* aObject);
        };

        // --- PickleBuffer (Objects/picklebufobject.c: a wrapper of a buffer that may be pickled out-of-band)

        struct picklebuffer_object
        {
            header h;
            object* exporter = nullptr;     ///< (the object whose buffer it is (null: released))
            ~picklebuffer_object() { release(exporter); }
        };
        neos_py_type make_picklebuffer_type();
        neos_py_type sPickleBufferType = make_picklebuffer_type();
        bool is_picklebuffer(object* aObject) { return aObject != nullptr && aObject->type == &sPickleBufferType.base; }
        picklebuffer_object& as_picklebuffer(object* aObject) { return *reinterpret_cast<picklebuffer_object*>(aObject); }

        bool picklebuffer_buffer(object* aObject, buffer_info& aResult)
        {
            auto const exporter = as_picklebuffer(aObject).exporter;
            if (exporter == nullptr)
            {
                error("ValueError", "operation forbidden on released PickleBuffer object");
                return false;
            }
            if (!buffer_of(exporter, aResult))
            {
                error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(exporter) + "'");
                return false;
            }
            return true;
        }
        void picklebuffer_exports(object* aObject, int aDelta)
        {
            if (auto const exporter = as_picklebuffer(aObject).exporter)
                if (auto const p = buffer_providers().find(exporter->type); p != buffer_providers().end() && p->second.exports != nullptr)
                    p->second.exports(exporter, aDelta);
        }

        object* picklebuffer_method(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs)
        {
            auto const& args = tuple_items(aArgs);
            std::string_view const name{ static_cast<char const*>(aData) };
            auto& self = as_picklebuffer(args[0]);
            if (!no_keywords(aKwargs, ("PickleBuffer." + std::string{ name }).c_str()))
                return nullptr;
            if (args.size() != 1u)
                return error("TypeError", "PickleBuffer." + std::string{ name } + "() takes no arguments (" + std::to_string(args.size() - 1u) + " given)");
            if (name == "release")
            {
                if (self.exporter != nullptr)
                {
                    auto const exporter = self.exporter;
                    self.exporter = nullptr;
                    release(exporter);
                }
                return new_reference(none());
            }
            // (raw: a memoryview of its bytes (format 'B'))
            if (self.exporter == nullptr)
                return error("ValueError", "operation forbidden on released PickleBuffer object");
            ref const view{ b_memoryview({ args[0] }, nullptr) };
            if (!view)
                return nullptr;
            ref const format{ getattr(view.get(), "format") };
            if (!format)
                return nullptr;
            if (kind_of(format.get()) == NEOS_PY_STR && as<str_object>(format.get())->value == "B")
                return view.get() != nullptr ? new_reference(view.get()) : nullptr;
            return call_method_named(view.get(), "cast", { make_str("B") });
        }

        neos_py_type make_picklebuffer_type()
        {
            auto type = make_type("pickle.PickleBuffer", "PickleBuffer", retain_object, release_object<picklebuffer_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object* { return make_str("<pickle.PickleBuffer object at " + object_address(aSelf) + ">"); };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                static char const* const sMethods[] = { "raw", "release" };
                for (auto m : sMethods)
                    if (std::strcmp(m, aName) == 0)
                        return make_function(m, picklebuffer_method, const_cast<char*>(m), aSelf);
                if (std::strcmp(aName, "__class__") == 0)
                    return new_reference(state().pickleBufferClass.get());
                if (std::strcmp(aName, "__doc__") == 0)
                    return make_str(pickle_docs::doc("PickleBuffer"));
                return nullptr;
            };
            return type;
        }

        object* picklebuffer_new(neos_py_context*, void*, neos_py_object* aArgs, neos_py_object* aKwargs)
        {
            try
            {
                static signature const sSignature{ "PickleBuffer", { "" }, 1u, 1u, 1u };
                auto const a = bind(sSignature, tuple_items(aArgs), aKwargs != nullptr && as<dict_object>(aKwargs)->items.empty() ? nullptr : aKwargs);
                buffer_info info;
                if (!buffer_of(a[0], info))
                    return error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(a[0]) + "'");
                retain(a[0]);
                return &(new picklebuffer_object{ { { &sPickleBufferType.base } }, a[0] })->h.base;
            }
            catch (raised const&)
            {
                return nullptr;
            }
        }
        int picklebuffer_instancecheck(neos_py_context*, void*, object* aObject) { return is_picklebuffer(aObject) ? 1 : 0; }

        // --- the pickler's functions that pickle other objects

        // (the parts of a dotted name (a qualified name): AttributeError if it is a local object's)
        std::vector<py> dotted_path(object* aObject, py const& aName)
        {
            py const parts = aName.method("split", { py{ "." } });
            std::vector<py> result = parts.items();
            for (auto const& part : result)
                if (kind_of(part.get()) == NEOS_PY_STR && as<str_object>(part.get())->value == "<locals>")
                {
                    if (aObject == nullptr)
                        raise_error("AttributeError", "Can't get local object " + aName.repr());
                    raise_error("AttributeError", "Can't get local attribute " + aName.repr() + " on " + repr_string(aObject));
                }
            return result;
        }
        // (an attribute of an attribute ... of an object (aParent: the object of which the last is an attribute): null if one has no such
        // attribute)
        ref deep_attribute(object* aObject, std::vector<py> const& aNames, ref* aParent)
        {
            ref current = ref::borrowed(aObject);
            ref parent;
            for (auto const& name : aNames)
            {
                parent = current;
                if (kind_of(name.get()) != NEOS_PY_STR)
                    raise_error("TypeError", "attribute name must be string, not '" + type_text(name.get()) + "'");
                current = optional_attr(parent.get(), as<str_object>(name.get())->value.c_str());
                if (!current)
                    return ref{};
            }
            if (aParent != nullptr)
                *aParent = parent;
            return current;
        }
        // (the module of a global (its __module__, else the module in sys.modules of which it is the attribute, else __main__))
        py whichmodule(py const& aGlobal, std::vector<py> const& aDottedPath)
        {
            if (ref name{ optional_attr(aGlobal.get(), "__module__") }; name && name.get() != none())
                return py{ std::move(name) };
            ref const modules{ checked(imported_modules()) };
            std::vector<std::pair<object*, object*>> const items = as<dict_object>(builtin_value(modules.get()))->items;
            for (auto const& [moduleName, module] : items)
            {
                if (module == none() || (kind_of(moduleName) == NEOS_PY_STR && as<str_object>(moduleName)->value == "__main__"))
                    continue;
                ref const candidate{ deep_attribute(module, aDottedPath, nullptr) };
                if (candidate.get() == aGlobal.get())
                    return py::borrowed(moduleName);
            }
            return py{ "__main__" };
        }
        // (PyImport_Import: a module, importing it)
        py import_module(py const& aName)
        {
            if (kind_of(aName.get()) == NEOS_PY_STR)
            {
                ref const modules{ checked(imported_modules()) };
                auto const position = dict_find(modules.get(), aName.get());
                if (position == -2)
                    throw raised{};
                if (position >= 0)
                    if (auto const module = as<dict_object>(modules.get())->items[static_cast<std::size_t>(position)].second; module != none())
                        return py::borrowed(module);
                return py{ fn_import(as<str_object>(aName.get())->value.c_str()) };
            }
            return builtin("__import__")(aName);
        }
        // (getattr(module, name), for a dotted name if aQualname (find_class's))
        py get_attribute(py const& aObject, py const& aName, bool aQualname)
        {
            ref result;
            if (aQualname)
            {
                auto const path = dotted_path(aObject.get(), aName);
                result = deep_attribute(aObject.get(), path, nullptr);
            }
            else
            {
                if (kind_of(aName.get()) != NEOS_PY_STR && !is_str(aName.get()))
                    raise_error("TypeError", "attribute name must be string, not '" + type_text(aName.get()) + "'");
                result = optional_attr(aObject.get(), str_value(aName.get()).c_str());
            }
            if (!result)
                raise_error("AttributeError", "Can't get attribute " + aName.repr() + " on " + aObject.repr());
            return py{ std::move(result) };
        }

        // (protocols 0 to 2: Python 3's names mapped to Python 2's (_compat_pickle's REVERSE_NAME_MAPPING and REVERSE_IMPORT_MAPPING))
        void fix_imports(py& aModuleName, py& aGlobalName)
        {
            auto& s = state();
            ref const key{ make_tuple({ aModuleName.reference(), aGlobalName.reference() }) };
            auto const position = dict_find(s.nameMapping3to2.get(), key.get());
            if (position == -2)
                throw raised{};
            if (position >= 0)
            {
                auto const item = as<dict_object>(s.nameMapping3to2.get())->items[static_cast<std::size_t>(position)].second;
                auto const pair = tuple_view(item);
                if (pair == nullptr || pair->size() != 2u)
                    raise_error("RuntimeError", "_compat_pickle.REVERSE_NAME_MAPPING values should be 2-tuples, not " + type_text(item));
                if (!is_str((*pair)[0]) || !is_str((*pair)[1]))
                    raise_error("RuntimeError", "_compat_pickle.REVERSE_NAME_MAPPING values should be pairs of str, not (" + type_text((*pair)[0]) + ", " +
                        type_text((*pair)[1]) + ")");
                aModuleName = py::borrowed((*pair)[0]);
                aGlobalName = py::borrowed((*pair)[1]);
                return;
            }
            auto const p = dict_find(s.importMapping3to2.get(), aModuleName.get());
            if (p == -2)
                throw raised{};
            if (p >= 0)
            {
                auto const item = as<dict_object>(s.importMapping3to2.get())->items[static_cast<std::size_t>(p)].second;
                if (!is_str(item))
                    raise_error("RuntimeError", "_compat_pickle.REVERSE_IMPORT_MAPPING values should be strings, not " + type_text(item));
                aModuleName = py::borrowed(item);
            }
        }

        void pickler::save_global(object* aObject, object* aName)
        {
            auto& s = state();
            py const obj = py::borrowed(aObject);
            py globalName;
            if (aName != nullptr)
                globalName = py::borrowed(aName);
            else if (ref qualname{ optional_attr(aObject, "__qualname__") })
                globalName = py{ std::move(qualname) };
            else
                globalName = obj.attr("__name__");
            auto dotted = dotted_path(nullptr, globalName);
            py moduleName = whichmodule(obj, dotted);
            // (the global must be its module's attribute of its name)
            py module;
            try
            {
                module = import_module(moduleName);
            }
            catch (raised const&)
            {
                clear_error();
                pickling_error("Can't pickle " + obj.repr() + ": import of module " + moduleName.repr() + " failed");
            }
            ref parent;
            ref found;
            try
            {
                found = deep_attribute(module.get(), dotted, &parent);
            }
            catch (raised const&)
            {
                clear_error();
            }
            if (!found)
                pickling_error("Can't pickle " + obj.repr() + ": attribute lookup " + globalName.str() + " on " + moduleName.str() + " failed");
            if (found.get() != aObject)
                pickling_error("Can't pickle " + obj.repr() + ": it's not the same object as " + moduleName.str() + "." + globalName.str());
            if (proto >= 2)
            {
                // (in the extension registry: an EXT opcode)
                ref const key{ make_tuple({ moduleName.reference(), globalName.reference() }) };
                auto const position = dict_find(s.extensionRegistry.get(), key.get());
                if (position == -2)
                    throw raised{};
                if (position >= 0)
                {
                    auto const codeObject = as<dict_object>(s.extensionRegistry.get())->items[static_cast<std::size_t>(position)].second;
                    long long code = 0;
                    py_checked(to_index(codeObject, code));
                    if (code <= 0 || code > 0x7fffffffll)
                        raise_error("RuntimeError", "extension code " + std::to_string(code) + " is out of range");
                    char data[5];
                    std::size_t n = 0u;
                    if (code <= 0xff)
                    {
                        data[0] = static_cast<char>(EXT1);
                        data[1] = static_cast<char>(code);
                        n = 2u;
                    }
                    else if (code <= 0xffff)
                    {
                        data[0] = static_cast<char>(EXT2);
                        data[1] = static_cast<char>(code & 0xff);
                        data[2] = static_cast<char>((code >> 8) & 0xff);
                        n = 3u;
                    }
                    else
                    {
                        data[0] = static_cast<char>(EXT4);
                        write_size32(data + 1, static_cast<std::uint64_t>(code));
                        n = 5u;
                    }
                    write(data, n);
                    return;
                }
            }
            if (parent.get() == module.get())
            {
                globalName = dotted.back();
                dotted.clear();
            }
            if (proto >= 4)
            {
                // (a builtin type's (CPython's C type's) qualified name is made as it is read: not interned (its module's name is))
                save(moduleName.get());
                bool const wasSuspended = internSuspended;
                struct restore { bool& flag; bool value; ~restore() { flag = value; } } const restoring{ internSuspended, wasSuspended };
                internSuspended = wasSuspended || is_type_object(aObject);
                save(globalName.get());
                write1(STACK_GLOBAL);
            }
            else
            {
                // (a multi-part qualified name: getattr(getattr(..., name1), name2))
                if (!dotted.empty())
                {
                    if (dotted.size() > 1u)
                        globalName = dotted[0];
                    for (std::size_t i = 1u; i < dotted.size(); ++i)
                    {
                        save(s.getattrFunction.get());
                        if (proto < 2)
                            write1(MARK);
                    }
                }
                write1(GLOBAL);
                if (proto < 3 && fixImports)
                    fix_imports(moduleName, globalName);
                // (UTF-8 for protocol 3; ASCII (Python 2's) for the others)
                auto const identifier = [this](py const& aName, char const* aWhat) -> std::string
                {
                    if (!is_str(aName.get()))
                        raise_error("TypeError", "bad argument type for built-in operation");
                    auto const& text = str_value(aName.get());
                    if (proto != 3 && std::any_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80u; }))
                        pickling_error(std::string{ "can't pickle " } + aWhat + " identifier '" + aName.str() + "' using pickle protocol " + std::to_string(proto));
                    return text;
                };
                write(identifier(moduleName, "module"));
                write1('\n');
                write(identifier(globalName, "global"));
                write1('\n');
                for (std::size_t i = 1u; i < dotted.size(); ++i)
                {
                    save(dotted[i].get());
                    write1(proto < 2 ? TUPLE : TUPLE2);
                    write1(REDUCE);
                }
            }
            memo_put(aObject);
        }

        bool is_iterator(object* aObject)   // (PyIter_Check)
        {
            if (kind_of(aObject) == NEOS_PY_ITERATOR)
                return true;
            if (auto const type = python_type(aObject); type != nullptr && type->next != nullptr && !is_instance(aObject))
                return true;
            return static_cast<bool>(optional_attr(aObject, "__next__"));
        }

        // (an object reduced (__reduce__'s tuple: aArgs): the callable and its arguments, and its state and items)
        void pickler::save_reduce(object* aArgs, object* aObject)
        {
            auto& s = state();
            auto const* tuple = tuple_view(aArgs);
            auto const size = tuple->size();
            if (size < 2u || size > 6u)
                pickling_error("tuple returned by __reduce__ must contain 2 through 6 elements");
            ref const keep{ ref::borrowed(aArgs) };
            std::vector<object*> const items = *tuple;
            object* callable = items[0];
            object* argtup = items[1];
            object* stateValue = size > 2u ? items[2] : none();
            object* listItems = size > 3u ? items[3] : none();
            object* dictItems = size > 4u ? items[4] : none();
            object* stateSetter = size > 5u ? items[5] : none();
            if (!is_callable(callable))
                pickling_error("first item of the tuple returned by __reduce__ must be callable");
            if (tuple_view(argtup) == nullptr)
                pickling_error("second item of the tuple returned by __reduce__ must be a tuple");
            if (stateValue == none())
                stateValue = nullptr;
            if (listItems == none())
                listItems = nullptr;
            else if (!is_iterator(listItems))
                pickling_error("fourth element of the tuple returned by __reduce__ must be an iterator, not " + std::string{ type_name(listItems) });
            if (dictItems == none())
                dictItems = nullptr;
            else if (!is_iterator(dictItems))
                pickling_error("fifth element of the tuple returned by __reduce__ must be an iterator, not " + std::string{ type_name(dictItems) });
            if (stateSetter == none())
                stateSetter = nullptr;
            else if (!is_callable(stateSetter))
                pickling_error("sixth element of the tuple returned by __reduce__ must be a function, not " + std::string{ type_name(stateSetter) });
            bool useNewobj = false;
            bool useNewobjEx = false;
            if (proto >= 2)
                if (ref const name{ optional_attr(callable, "__name__") }; name && kind_of(name.get()) == NEOS_PY_STR)
                {
                    useNewobjEx = as<str_object>(name.get())->value == "__newobj_ex__";
                    useNewobj = !useNewobjEx && as<str_object>(name.get())->value == "__newobj__";
                }
            auto const& arguments = *tuple_view(argtup);
            if (useNewobjEx)
            {
                if (arguments.size() != 3u)
                    pickling_error("length of the NEWOBJ_EX argument tuple must be exactly 3, not " + std::to_string(arguments.size()));
                object* const cls = arguments[0];
                if (!is_type(cls))
                    pickling_error("first item from NEWOBJ_EX argument tuple must be a class, not " + type_text(cls));
                object* const args = arguments[1];
                if (tuple_view(args) == nullptr)
                    pickling_error("second item from NEWOBJ_EX argument tuple must be a tuple, not " + type_text(args));
                object* const kwargs = arguments[2];
                if (!is_dict(kwargs))
                    pickling_error("third item from NEWOBJ_EX argument tuple must be a dict, not " + type_text(kwargs));
                if (proto >= 4)
                {
                    save(cls);
                    save(args);
                    save(kwargs);
                    write1(NEWOBJ_EX);
                }
                else
                {
                    // (partial(cls.__new__, cls, *args, **kwargs)())
                    std::vector<object*> newargs{ checked(getattr(cls, "__new__")), new_reference(cls) };
                    for (auto a : *tuple_view(args))
                        newargs.push_back(new_reference(a));
                    ref const callArgs{ make_tuple(std::move(newargs)) };
                    ref const kw{ is_dict(kwargs) && kind_of(kwargs) != NEOS_PY_DICT ? checked(invoke(ref{ checked(fn_builtin("dict")) }.get(), { new_reference(kwargs) })) : 
                        new_reference(kwargs) };
                    ref const partialCallable{ checked(call(s.partial.get(), callArgs.get(), kw.get())) };
                    ref const empty{ make_tuple() };
                    save(partialCallable.get());
                    save(empty.get());
                    write1(REDUCE);
                }
            }
            else if (useNewobj)
            {
                if (arguments.empty())
                    pickling_error("__newobj__ arglist is empty");
                object* const cls = arguments[0];
                if (!is_type(cls))
                    pickling_error("args[0] from __newobj__ args is not a type");
                if (aObject != nullptr)
                {
                    ref objectClass{ optional_attr(aObject, "__class__") };
                    if (!objectClass)
                        objectClass = ref{ checked(class_of(aObject)) };
                    if (objectClass.get() != cls)
                        pickling_error("args[0] from __newobj__ args has the wrong class");
                }
                save(cls);
                std::vector<object*> rest;
                for (std::size_t i = 1u; i < arguments.size(); ++i)
                    rest.push_back(new_reference(arguments[i]));
                ref const newargtup{ make_tuple(std::move(rest)) };
                save(newargtup.get());
                write1(NEWOBJ);
            }
            else
            {
                save(callable);
                save(argtup);
                write1(REDUCE);
            }
            // (recursive: what was pushed is popped, and the memo's is pushed)
            if (aObject != nullptr)
            {
                if (memo.find(aObject) != nullptr)
                {
                    write1(POP);
                    memo_get(aObject);
                    return;
                }
                memo_put(aObject);
            }
            if (listItems != nullptr)
                batch_list(listItems);
            if (dictItems != nullptr)
                batch_dict(dictItems);
            if (stateValue != nullptr)
            {
                if (stateSetter == nullptr)
                {
                    save(stateValue);
                    write1(BUILD);
                }
                else
                {
                    // (state_setter(obj, state), its result popped)
                    save(stateSetter);
                    save(aObject);
                    save(stateValue);
                    write1(TUPLE2);
                    write1(REDUCE);
                    write1(POP);
                }
            }
        }

        void pickler::save_picklebuffer(object* aObject)
        {
            if (proto < 5)
                pickling_error("PickleBuffer can only be pickled with protocol >= 5");
            buffer_info info;
            py_checked(picklebuffer_buffer(aObject, info));
            bool inBand = true;
            if (bufferCallback)
            {
                ref const result{ call1(bufferCallback.get(), aObject) };
                auto const t = truth(result.get());
                py_checked(t >= 0);
                inBand = t == 1;
            }
            if (inBand)
            {
                // (in-band: its data, as bytes or bytearray)
                if (info.readonly)
                    save_bytes_data(aObject, info.data, info.size);
                else
                    save_bytearray_data(aObject, info.data, info.size);
            }
            else
            {
                // (out-of-band)
                write1(NEXT_BUFFER);
                if (info.readonly)
                    write1(READONLY_BUFFER);
            }
        }

        void pickler::save(object* aObject, bool aPersistent)
        {
            opcode_boundary();
            // (aPersistent: a persistent id, which is not given to persistent_id)
            if (!aPersistent && persistentId && save_pers(aObject))
                return;
            // (atoms: not memoized)
            switch (kind_of(aObject))
            {
            case NEOS_PY_NONE:
                write1(NONE);
                return;
            case NEOS_PY_BOOL:
                save_bool(aObject);
                return;
            case NEOS_PY_INT:
                save_long(aObject);
                return;
            case NEOS_PY_FLOAT:
                save_float(aObject);
                return;
            case NEOS_PY_STR:
                if (auto const index = memo.find(aObject))
                    memo_get(*index);
                else if (auto const& text = as<str_object>(aObject)->value; fast == 0 && !internSuspended && internable(text))
                {
                    // (its memo position (it is to be the memo's next), if it is not there)
                    if (auto const [i, found] = interned.find_or_insert(aObject, memo.size()); found)
                        memo_get(i);
                    else
                    {
                        try
                        {
                            save_unicode(aObject);
                        }
                        catch (...)
                        {
                            interned.clear();   // (not memoized: forgotten)
                            throw;
                        }
                    }
                }
                else
                    save_unicode(aObject);
                return;
            default:
                break;
            }
            if (auto const index = memo.find(aObject))
            {
                memo_get(*index);
                return;
            }
            auto& s = state();
            if (aObject->type == s.bytesType)
            {
                save_bytes(aObject);
                return;
            }
            recursion_guard const recursion{ depth };
            switch (kind_of(aObject))
            {
            case NEOS_PY_DICT:
                save_dict(aObject);
                return;
            case NEOS_PY_LIST:
                save_list(aObject);
                return;
            case NEOS_PY_TUPLE:
                save_tuple(aObject);
                return;
            default:
                break;
            }
            if (aObject->type == s.setType)
                save_set(aObject);
            else if (aObject->type == s.frozensetType)
                save_frozenset(aObject);
            else if (aObject->type == s.bytearrayType)
                save_bytearray(aObject);
            else if (is_picklebuffer(aObject))
                save_picklebuffer(aObject);
            else
                save_other(aObject);
        }

        // (another object: reducer_override's reduction, a type or a function (a global), or its reduction (the dispatch tables',
        // __reduce_ex__'s or __reduce__'s))
        void pickler::save_other(object* aObject)
        {
            auto& s = state();
            ref reduceValue;
            if (reducerOverride)
            {
                reduceValue = call1(reducerOverride.get(), aObject);
                if (reduceValue.get() == not_implemented())
                    reduceValue = ref{};
            }
            if (!reduceValue)
            {
                ref const type{ checked(class_of(aObject)) };
                if (type.get() == s.typeType.get())
                {
                    save_type(aObject);
                    return;
                }
                if (type.get() == s.functionType.get())
                {
                    save_global(aObject, nullptr);
                    return;
                }
                ref reduceFunction;
                if (!dispatchTable)
                {
                    auto const position = dict_find(s.dispatchTable.get(), type.get());
                    if (position == -2)
                        throw raised{};
                    if (position >= 0)
                        reduceFunction = ref::borrowed(as<dict_object>(s.dispatchTable.get())->items[static_cast<std::size_t>(position)].second);
                }
                else
                {
                    reduceFunction = ref{ getitem(dispatchTable.get(), type.get()) };
                    if (!reduceFunction)
                    {
                        ref const e{ take_exception() };
                        if (!exception_is(e.get(), "KeyError"))
                        {
                            reraise_exception(e.get());
                            throw raised{};
                        }
                    }
                }
                object* state = nullptr;
                if (reduceFunction)
                    reduceValue = call1(reduceFunction.get(), aObject);
                else if (proto >= 2 && default_reducible(aObject, state))
                {
                    // (object.__reduce_ex__'s reduction (copyreg.__newobj__, (cls,), state): NEWOBJ, and its state)
                    static object* const sEmpty = make_tuple();
                    ref const keep{ state != nullptr ? new_reference(state) : nullptr };
                    save(type.get());
                    save(sEmpty);
                    write1(NEWOBJ);
                    if (memo.find(aObject) != nullptr)
                    {
                        write1(POP);
                        memo_get(aObject);
                        return;
                    }
                    memo_put(aObject);
                    if (state != nullptr)
                    {
                        save(state);
                        write1(BUILD);
                    }
                    return;
                }
                else if (issubclass(py::borrowed(type.get()), py::borrowed(s.typeType.get())))
                {
                    save_global(aObject, nullptr);
                    return;
                }
                else if (ref const reduceEx{ optional_attr(aObject, "__reduce_ex__") })
                    reduceValue = ref{ checked(invoke(reduceEx.get(), { make_int(proto) })) };
                else if (ref const reduce{ optional_attr(aObject, "__reduce__") })
                    reduceValue = ref{ checked(invoke(reduce.get(), {})) };
                else
                    pickling_error("can't pickle '" + type_text(aObject) + "' object: " + repr_string(aObject));
            }
            if (is_str(reduceValue.get()))
            {
                save_global(aObject, reduceValue.get());
                return;
            }
            if (tuple_view(reduceValue.get()) == nullptr)
                pickling_error("__reduce__ must return a string or tuple");
            save_reduce(reduceValue.get(), aObject);
        }

        void pickler::dump(object* aSelf, object* aObject)
        {
            auto& s = state();
            if (aSelf != nullptr)
            {
                // (persistent_id: the class's own is none)
                ref method{ checked(getattr(aSelf, "persistent_id")) };
                if (ref const function{ optional_attr(method.get(), "__func__") }; function && function.get() == s.persistentIdFunction)
                    if (ref const self{ optional_attr(method.get(), "__self__") }; self.get() == aSelf)
                        method = ref{};
                persistentId = method;
                reducerOverride = optional_attr(aSelf, "reducer_override");
            }
            struct finish
            {
                ~finish()
                {
                    self.framing = false;
                    self.persistentId = ref{};
                    self.reducerOverride = ref{};
                }
                pickler& self;
            } const finishing{ *this };
            if (proto >= 2)
            {
                char const header[2] = { static_cast<char>(PROTO), static_cast<char>(proto) };
                write(header, 2u);
                if (proto >= 4)
                    framing = true;
            }
            save(aObject);
            write1(STOP);
            commit_frame();
        }

        // (a pickler's protocol, file and buffer callback (_Pickler_SetProtocol, _Pickler_SetOutputStream and _Pickler_SetBufferCallback))
        void set_protocol(pickler& aPickler, object* aProtocol, bool aFixImports)
        {
            long long proto = kDefaultProtocol;
            if (aProtocol != nullptr && aProtocol != none())
            {
                py_checked(to_index(aProtocol, proto));
                if (proto < 0)
                    proto = kHighestProtocol;
                else if (proto > kHighestProtocol)
                    raise_error("ValueError", "pickle protocol must be <= " + std::to_string(kHighestProtocol));
            }
            aPickler.proto = static_cast<int>(proto);
            aPickler.bin = proto > 0 ? 1 : 0;
            aPickler.fixImports = aFixImports && proto < 3;
        }
        void set_output(pickler& aPickler, object* aFile)
        {
            aPickler.writeMethod = optional_attr(aFile, "write");
            if (!aPickler.writeMethod)
                raise_error("TypeError", "file must have a 'write' attribute");
        }
        void set_buffer_callback(pickler& aPickler, object* aCallback)
        {
            if (aCallback == none())
                aCallback = nullptr;
            if (aCallback != nullptr && aPickler.proto < 5)
                raise_error("ValueError", "buffer_callback needs protocol >= 5");
            aPickler.bufferCallback = aCallback != nullptr ? ref::borrowed(aCallback) : ref{};
        }

        // --- the unpickler (_pickle.c's UnpicklerObject, and its functions)

        struct unpickler
        {
            // (the input: a buffer of the data (loads), or of the data that was read from the file)
            ref inputObject;
            char const* inputBuffer = nullptr;
            long long inputLength = 0;
            long long nextReadIndex = 0;
            long long prefetchedIndex = 0;          ///< (the data before which was only peeked at)
            long long frameEnd = -1;                ///< (the current frame's end (-1: none): the input is capped at it)
            long long savedInputLength = 0;
            std::string inputLine;
            ref readMethod, readintoMethod, readlineMethod, peekMethod;
            ref buffers;                            ///< (an iterator of out-of-band buffers)
            std::string encoding = "ASCII";
            std::string errors = "strict";
            // (the stack, and its marks (fence: the position of the last mark))
            std::vector<object*> stack;
            long long fence = 0;
            bool markSet = false;
            std::vector<long long> marks;
            // (the memo)
            std::vector<object*> memo = std::vector<object*>(32u, nullptr);
            std::size_t memoLength = 0u;
            int proto = 0;
            bool fixImports = true;
            bool running = false;
            object* self = nullptr;                 ///< (its Unpickler, while load runs (null: load and loads))
            ref persistentLoad;                     ///< (while load runs: null: the class's own)
            ref findClass;                          ///< (while load runs: null: the class's own)

            ~unpickler()
            {
                clear_stack(0);
                memo_cleanup();
            }

            // (the stack)
            [[noreturn]] void stack_underflow() { unpickling_error(markSet ? "unexpected MARK found" : "unpickling stack underflow"); }
            void push(object* aObject) { stack.push_back(aObject); }    // (steals the reference)
            void append(object* aObject) { retain(aObject); stack.push_back(aObject); }
            ref pop()
            {
                if (static_cast<long long>(stack.size()) <= fence)
                    stack_underflow();
                ref result{ stack.back() };
                stack.pop_back();
                return result;
            }
            void clear_stack(long long aStart)
            {
                while (static_cast<long long>(stack.size()) > aStart)
                {
                    auto const o = stack.back();
                    stack.pop_back();
                    release(o);
                }
            }
            object* pop_tuple(long long aStart)
            {
                if (aStart < fence)
                    stack_underflow();
                std::vector<object*> items{ stack.begin() + aStart, stack.end() };
                stack.resize(static_cast<std::size_t>(aStart));
                return make_tuple(std::move(items));
            }
            object* pop_list(long long aStart)
            {
                std::vector<object*> items{ stack.begin() + aStart, stack.end() };
                stack.resize(static_cast<std::size_t>(aStart));
                return make_list(std::move(items));
            }
            long long size() const { return static_cast<long long>(stack.size()); }
            long long marker()
            {
                if (marks.empty())
                    unpickling_error("could not find MARK");
                auto const mark = marks.back();
                marks.pop_back();
                markSet = !marks.empty();
                fence = marks.empty() ? 0 : marks.back();
                return mark;
            }

            // (the memo)
            void memo_put(std::size_t aIndex, object* aValue)
            {
                if (aIndex >= memo.size())
                    memo.resize(std::max(aIndex * 2u, aIndex + 1u), nullptr);
                auto const old = memo[aIndex];
                retain(aValue);
                memo[aIndex] = aValue;
                if (old != nullptr)
                    release(old);
                else
                    ++memoLength;
            }
            object* memo_get(std::size_t aIndex) const { return aIndex < memo.size() ? memo[aIndex] : nullptr; }
            void memo_cleanup()
            {
                for (auto& m : memo)
                {
                    auto const o = m;
                    m = nullptr;
                    release(o);
                }
                memoLength = 0u;
            }

            // (input)
            [[noreturn]] void bad_readline() { unpickling_error("pickle data was truncated"); }
            long long set_string_input(object* aData)
            {
                std::string_view view;
                if (!bytes_like(aData, view))
                    raise_error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(aData) + "'");
                inputObject = ref::borrowed(aData);
                inputBuffer = view.data();
                inputLength = static_cast<long long>(view.size());
                nextReadIndex = 0;
                prefetchedIndex = inputLength;
                frameEnd = -1;
                return inputLength;
            }
            void end_frame()
            {
                inputLength = savedInputLength;
                frameEnd = -1;
            }
            void leave_frame(bool aStraddle)
            {
                if (aStraddle)
                    unpickling_error("pickle exhausted before end of frame");
                end_frame();
            }
            // (the data that was only peeked at, and has been consumed: read)
            void skip_consumed()
            {
                auto const consumed = nextReadIndex - prefetchedIndex;
                if (consumed <= 0)
                    return;
                ref const r{ checked(invoke(readMethod.get(), { make_int(consumed) })) };
                prefetchedIndex = nextReadIndex;
            }
            long long read_from_file(long long aCount)
            {
                skip_consumed();
                ref data;
                if (aCount == kReadWholeLine)
                    data = ref{ checked(invoke(readlineMethod.get(), {})) };
                else
                {
                    // (some data prefetched (without moving the file's position), if the file can peek)
                    if (peekMethod && aCount < kPrefetch)
                    {
                        ref peeked{ invoke(peekMethod.get(), { make_int(kPrefetch) }) };
                        if (!peeked)
                        {
                            ref const e{ take_exception() };
                            if (!exception_is(e.get(), "NotImplementedError"))
                            {
                                reraise_exception(e.get());
                                throw raised{};
                            }
                            peekMethod = ref{};
                        }
                        else
                        {
                            auto const readSize = set_string_input(peeked.get());
                            prefetchedIndex = 0;
                            if (aCount <= readSize)
                                return aCount;
                        }
                    }
                    data = ref{ checked(invoke(readMethod.get(), { make_int(aCount) })) };
                }
                return set_string_input(data.get());
            }
            char const* read_impl(long long aCount)
            {
                if (frameEnd >= 0)
                {
                    leave_frame(nextReadIndex < frameEnd);
                    // (the frame ended: the read may be the buffer's)
                    if (aCount <= inputLength - nextReadIndex)
                    {
                        auto const result = inputBuffer + nextReadIndex;
                        nextReadIndex += aCount;
                        return result;
                    }
                }
                if (!readMethod)
                    bad_readline();
                auto const n = read_from_file(aCount);
                if (n < aCount)
                    bad_readline();
                nextReadIndex = aCount;
                return inputBuffer;
            }
            char const* read(long long aCount)
            {
                if (aCount <= inputLength - nextReadIndex)
                {
                    auto const result = inputBuffer + nextReadIndex;
                    nextReadIndex += aCount;
                    return result;
                }
                return read_impl(aCount);
            }
            // (aCount bytes (large data, read from the file directly))
            std::string read_into(long long aCount)
            {
                std::string result;
                result.reserve(static_cast<std::size_t>(aCount));
                auto inBuffer = inputLength - nextReadIndex;
                if (inBuffer > 0)
                {
                    auto const n = std::min(inBuffer, aCount);
                    result.append(inputBuffer + nextReadIndex, static_cast<std::size_t>(n));
                    nextReadIndex += n;
                    aCount -= n;
                    if (aCount == 0)
                        return result;
                }
                if (frameEnd >= 0)
                {
                    leave_frame(inBuffer > 0);
                    inBuffer = inputLength - nextReadIndex;
                    if (inBuffer > 0)
                    {
                        auto const n = std::min(inBuffer, aCount);
                        result.append(inputBuffer + nextReadIndex, static_cast<std::size_t>(n));
                        nextReadIndex += n;
                        aCount -= n;
                        if (aCount == 0)
                            return result;
                    }
                }
                if (!readMethod)
                    bad_readline();
                skip_consumed();
                if (!readintoMethod)
                {
                    ref const data{ checked(invoke(readMethod.get(), { make_int(aCount) })) };
                    if (data.get()->type != state().bytesType)
                    {
                        ref const cls{ checked(class_of(data.get())) };
                        raise_error("ValueError", "read() returned non-bytes object (" + repr_string(cls.get()) + ")");
                    }
                    std::string_view view;
                    bytes_like(data.get(), view);
                    if (static_cast<long long>(view.size()) < aCount)
                        bad_readline();
                    result.append(view.data(), static_cast<std::size_t>(aCount));
                    return result;
                }
                // (readinto() of a writable buffer)
                ref const target{ make_bytearray(std::string(static_cast<std::size_t>(aCount), '\0')) };
                ref const view{ checked(b_memoryview({ target.get() }, nullptr)) };
                ref const sizeObject{ checked(invoke(readintoMethod.get(), { new_reference(view.get()) })) };
                long long readSize = 0;
                py_checked(to_index(sizeObject.get(), readSize));
                if (readSize < 0)
                    raise_error("ValueError", "readinto() returned negative size");
                if (readSize < aCount)
                    bad_readline();
                std::string_view data;
                bytes_like(target.get(), data);
                result.append(data.data(), static_cast<std::size_t>(aCount));
                return result;
            }
            // (a line, with its '\n')
            std::string_view readline()
            {
                for (;;)
                {
                    for (auto i = nextReadIndex; i < inputLength; ++i)
                        if (inputBuffer[i] == '\n')
                        {
                            auto const start = inputBuffer + nextReadIndex;
                            auto const n = i - nextReadIndex + 1;
                            nextReadIndex = i + 1;
                            inputLine.assign(start, static_cast<std::size_t>(n));
                            return inputLine;
                        }
                    if (frameEnd >= 0)
                    {
                        // (the frame ended: the line continues beyond it)
                        leave_frame(nextReadIndex < frameEnd);
                        continue;
                    }
                    break;
                }
                if (!readMethod)
                    bad_readline();
                auto const n = read_from_file(kReadWholeLine);
                if (n == 0 || inputBuffer[n - 1] != '\n')
                    bad_readline();
                nextReadIndex = n;
                inputLine.assign(inputBuffer, static_cast<std::size_t>(n));
                return inputLine;
            }

            object* find_class(object* aModuleName, object* aGlobalName);
            object* load();
            void load_one(unsigned char aOpcode);
            // (the opcodes' functions)
            void load_int();
            void load_long();
            void load_counted_long(int aSize);
            void load_float();
            void load_string();
            void load_counted_binstring(int aSize);
            void load_unicode();
            void load_readonly_buffer();
            void load_next_buffer();
            void load_dict();
            void load_frozenset();
            void load_obj();
            void load_inst();
            void load_newobj(bool aKwargs);
            void load_global();
            void load_stack_global();
            void load_persid();
            void load_binpersid();
            void load_pop();
            void load_get();
            void load_extension(int aSize);
            void load_put();
            void do_append(long long aStart);
            void do_setitems(long long aStart);
            void load_additems();
            void load_build();
            void load_mark();
            void load_reduce();
            void load_proto();
            void load_frame();
            object* call_find_class(object* aModuleName, object* aGlobalName);
        };

        // (s holds aSize bytes of an unsigned little-endian integer: its value (-1 if it is greater than the largest size))
        long long calc_binsize(char const* aBytes, int aSize)
        {
            auto const s = reinterpret_cast<unsigned char const*>(aBytes);
            std::uint64_t x = 0u;
            for (int i = 0; i < aSize; ++i)
                x |= static_cast<std::uint64_t>(s[i]) << (8 * i);
            if (x > static_cast<std::uint64_t>(std::numeric_limits<long long>::max()))
                return -1;
            return static_cast<long long>(x);
        }
        // (s holds aSize bytes of a little-endian integer: signed if aSize is 4, unsigned if it is 1 or 2)
        long long calc_binint(char const* aBytes, int aSize)
        {
            auto const s = reinterpret_cast<unsigned char const*>(aBytes);
            long long x = 0;
            for (int i = 0; i < aSize; ++i)
                x |= static_cast<long long>(s[i]) << (8 * i);
            if (aSize == 4)
                x = static_cast<std::int32_t>(static_cast<std::uint32_t>(x));
            return x;
        }
        // (int(text, base) (PyLong_FromString))
        object* long_from_string(std::string const& aText, int aBase)
        {
            ref const text{ make_str(aText) };
            ref const base{ make_int(aBase) };
            ref const intClass{ checked(fn_builtin("int")) };
            return invoke(intClass.get(), { new_reference(text.get()), new_reference(base.get()) });
        }

        void unpickler::load_int()
        {
            auto const line = readline();
            if (line.size() < 2u)
                bad_readline();
            std::string const text{ line.substr(0u, line.size() - 1u) };
            errno = 0;
            char* end = nullptr;
            long long const x = std::strtoll(text.c_str(), &end, 0);
            if (errno != 0 || *end != '\0' || x < std::numeric_limits<long>::min() || x > std::numeric_limits<long>::max() || text.empty())
            {
                // (an int beyond a C long)
                ref value{ long_from_string(text, 0) };
                if (!value)
                {
                    clear_error();
                    raise_error("ValueError", "could not convert string to int");
                }
                push(value.release_ownership());
                return;
            }
            if (line.size() == 3u && (x == 0 || x == 1))
                push(make_bool(x == 1));
            else
                push(make_int(x));
        }
        void unpickler::load_long()
        {
            auto const line = readline();
            if (line.size() < 2u)
                bad_readline();
            // (Python 2's "L" suffix, if any, is removed)
            std::string text{ line.substr(0u, line.size() - 1u) };
            if (!text.empty() && text.back() == 'L')
                text.pop_back();
            push(checked(long_from_string(text, 0)));
        }
        object* int_from_bytes(char const* aData, std::size_t aSize)
        {
            if (aSize == 0u)
                return make_int(0);
            if (aSize <= 8u)
            {
                std::uint64_t u = 0u;
                for (std::size_t i = 0u; i < aSize; ++i)
                    u |= static_cast<std::uint64_t>(static_cast<unsigned char>(aData[i])) << (8u * i);
                if (aSize < 8u && (static_cast<unsigned char>(aData[aSize - 1u]) & 0x80u) != 0u)
                    u |= ~std::uint64_t{ 0u } << (8u * aSize);
                return make_int(static_cast<long long>(u));
            }
            ref const intClass{ checked(fn_builtin("int")) };
            ref const fromBytes{ checked(getattr(intClass.get(), "from_bytes")) };
            ref const kwargs{ keywords({ { "signed", make_bool(true) } }) };
            ref const args{ make_tuple({ make_bytes(std::string{ aData, aSize }), make_str("little") }) };
            return call(fromBytes.get(), args.get(), kwargs.get());
        }
        void unpickler::load_counted_long(int aSize)
        {
            auto const n = calc_binint(read(aSize), aSize);
            if (n < 0)
                unpickling_error("LONG pickle has negative byte count");
            if (n == 0)
            {
                push(make_int(0));
                return;
            }
            auto const data = read(n);
            push(checked(int_from_bytes(data, static_cast<std::size_t>(n))));
        }
        void unpickler::load_float()
        {
            auto const line = readline();
            if (line.size() < 2u)
                bad_readline();
            // (as PyOS_string_to_double: no whitespace nor underscores)
            std::string const text{ line.substr(0u, line.size() - 1u) };
            bool const plain = !text.empty() && std::isspace(static_cast<unsigned char>(text.front())) == 0 && std::isspace(static_cast<unsigned char>(text.back())) == 0 &&
                text.find('_') == std::string::npos;
            ref result;
            if (plain)
            {
                ref const value{ make_str(text) };
                ref const floatClass{ checked(fn_builtin("float")) };
                result = ref{ invoke(floatClass.get(), { new_reference(value.get()) }) };
                if (!result)
                {
                    ref const e{ take_exception() };
                    if (!exception_is(e.get(), "ValueError"))
                    {
                        reraise_exception(e.get());
                        throw raised{};
                    }
                }
            }
            if (!result)
            {
                // (no number at its start: PyOS_string_to_double's message (of the line))
                char* end = nullptr;
                std::strtod(text.c_str(), &end);
                bool const prefix = end != text.c_str() && !std::isspace(static_cast<unsigned char>(text.front())) && !(text.size() > 1u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'));
                if (!prefix)
                    raise_error("ValueError", "could not convert string to float: '" + std::string{ line.substr(0u, 200u) } + "'");
                raise_error("ValueError", "could not convert string to float");
            }
            push(result.release_ownership());
        }
        // (PyBytes_DecodeEscape: a Python 2 str literal's escapes)
        std::string decode_escape(std::string_view aText)
        {
            std::string result;
            for (std::size_t i = 0u; i < aText.size(); ++i)
            {
                char c = aText[i];
                if (c != '\\')
                {
                    result.push_back(c);
                    continue;
                }
                if (++i == aText.size())
                    raise_error("ValueError", "Trailing \\ in string");
                c = aText[i];
                switch (c)
                {
                case '\n': break;
                case '\\': result.push_back('\\'); break;
                case '\'': result.push_back('\''); break;
                case '"': result.push_back('"'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 't': result.push_back('\t'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 'v': result.push_back('\v'); break;
                case 'a': result.push_back('\a'); break;
                case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
                    {
                        int value = c - '0';
                        for (int k = 0; k < 2 && i + 1u < aText.size() && aText[i + 1u] >= '0' && aText[i + 1u] <= '7'; ++k)
                            value = value * 8 + (aText[++i] - '0');
                        result.push_back(static_cast<char>(value & 0xff));
                        break;
                    }
                case 'x':
                    {
                        auto const hex = [](char h) { return h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1; };
                        if (i + 2u < aText.size() && hex(aText[i + 1u]) >= 0 && hex(aText[i + 2u]) >= 0)
                        {
                            result.push_back(static_cast<char>(hex(aText[i + 1u]) * 16 + hex(aText[i + 2u])));
                            i += 2u;
                            break;
                        }
                        raise_error("ValueError", "invalid \\x escape at position " + std::to_string(i - 1u));
                    }
                default:
                    result.push_back('\\');
                    result.push_back(c);
                    break;
                }
            }
            return result;
        }
        // (a Python 2 str: bytes if the encoding is "bytes", else its text, decoded)
        object* python2_string(unpickler const& aUnpickler, std::string_view aData)
        {
            if (aUnpickler.encoding == "bytes")
                return make_bytes(std::string{ aData });
            ref const data{ make_bytes(std::string{ aData }) };
            ref const decoded{ call_method_named(data.get(), "decode", { make_str(aUnpickler.encoding), make_str(aUnpickler.errors) }) };
            return decoded ? new_reference(decoded.get()) : nullptr;
        }
        void unpickler::load_string()
        {
            auto line = readline();
            auto length = line.size() - 1u;
            // (its outermost quotes)
            if (length >= 2u && line[0] == line[length - 1u] && (line[0] == '\'' || line[0] == '"'))
                length -= 2u;
            else
                unpickling_error("the STRING opcode argument must be quoted");
            auto const bytes = decode_escape(line.substr(1u, length));
            push(checked(python2_string(*this, bytes)));
        }
        void unpickler::load_counted_binstring(int aSize)
        {
            auto const n = calc_binint(read(aSize), aSize);
            if (n < 0)
                unpickling_error("BINSTRING pickle has negative byte count");
            auto const data = read(n);
            push(checked(python2_string(*this, std::string_view{ data, static_cast<std::size_t>(n) })));
        }
        void unpickler::load_unicode()
        {
            auto const line = readline();
            if (line.empty())
                bad_readline();
            ref const data{ make_bytes(std::string{ line.substr(0u, line.size() - 1u) }) };
            push(checked(call_method_named(data.get(), "decode", { make_str("raw_unicode_escape") })));
        }
        void unpickler::load_next_buffer()
        {
            if (!buffers)
                unpickling_error("pickle stream refers to out-of-band data but no *buffers* argument was given");
            ref buffer{ next(buffers.get()) };
            if (!buffer)
            {
                if (failed())
                    throw raised{};
                unpickling_error("not enough out-of-band buffers");
            }
            push(buffer.release_ownership());
        }
        void unpickler::load_readonly_buffer()
        {
            if (size() <= fence)
                stack_underflow();
            auto& top = stack.back();
            ref const view{ checked(b_memoryview({ top }, nullptr)) };
            ref const readonly{ checked(getattr(view.get(), "readonly")) };
            if (truth(readonly.get()) == 0)
            {
                // (the object is writable: a read-only view of it)
                ref const readonlyView{ checked(call_method_named(view.get(), "toreadonly", {})) };
                auto const old = top;
                top = readonlyView.get();
                retain(top);
                release(old);
            }
        }
        void unpickler::load_dict()
        {
            auto const i = marker();
            auto const j = size();
            ref const dict{ make_dict() };
            if ((j - i) % 2 != 0)
                unpickling_error("odd number of items for DICT");
            for (auto k = i + 1; k < j; k += 2)
                py_checked(dict_set(dict.get(), stack[static_cast<std::size_t>(k - 1)], stack[static_cast<std::size_t>(k)]) >= 0);
            clear_stack(i);
            append(dict.get());
        }
        void unpickler::load_frozenset()
        {
            auto const i = marker();
            ref const items{ checked(pop_tuple(i)) };
            push(checked(b_frozenset({ items.get() }, nullptr)));
        }
        // (an instance of a class (OBJ and INST): cls.__new__(cls) if there are no arguments and it has no __getinitargs__)
        object* instantiate(object* aClass, object* aArgs)
        {
            if (tuple_items(aArgs).empty() && is_type(aClass))
            {
                if (!optional_attr(aClass, "__getinitargs__"))
                {
                    ref const constructor{ checked(getattr(aClass, "__new__")) };
                    return invoke(constructor.get(), { new_reference(aClass) });
                }
            }
            return call(aClass, aArgs, nullptr);
        }
        void unpickler::load_obj()
        {
            auto const i = marker();
            if (size() - i < 1)
                stack_underflow();
            ref const args{ checked(pop_tuple(i + 1)) };
            ref const cls{ pop() };
            push(checked(instantiate(cls.get(), args.get())));
        }
        void unpickler::load_inst()
        {
            auto const i = marker();
            auto line = readline();
            if (line.size() < 2u)
                bad_readline();
            // (INST is Python 2's: its names are ASCII)
            auto const ascii = [](std::string_view aText) -> object*
            {
                std::string text;
                py_checked(decode(aText, "ascii", "strict", text));
                return make_str(std::move(text), aText.size());
            };
            ref const moduleName{ checked(ascii(line.substr(0u, line.size() - 1u))) };
            line = readline();
            if (line.size() < 2u)
                bad_readline();
            ref const className{ checked(ascii(line.substr(0u, line.size() - 1u))) };
            ref const cls{ checked(call_find_class(moduleName.get(), className.get())) };
            ref const args{ checked(pop_tuple(i)) };
            push(checked(instantiate(cls.get(), args.get())));
        }
        void unpickler::load_newobj(bool aKwargs)
        {
            // (cls args [kwargs]: cls.__new__(cls, *args, **kwargs))
            ref kwargs;
            if (aKwargs)
                kwargs = pop();
            ref const args{ pop() };
            ref const cls{ pop() };
            char const* const name = aKwargs ? "NEWOBJ_EX" : "NEWOBJ";
            if (!is_type(cls.get()))
                unpickling_error(std::string{ name } + " class argument must be a type, not " + type_text(cls.get()));
            if (tuple_view(args.get()) == nullptr)
                unpickling_error(std::string{ name } + " args argument must be a tuple, not " + type_text(args.get()));
            if (aKwargs && !is_dict(kwargs.get()))
                unpickling_error(std::string{ name } + " kwargs argument must be a dict, not " + type_text(kwargs.get()));
            if (object* instance = nullptr; !aKwargs && kind_of(args.get()) == NEOS_PY_TUPLE && default_new(cls.get(), args.get(), instance))
            {
                push(checked(instance));
                return;
            }
            ref const constructor{ checked(getattr(cls.get(), "__new__")) };
            std::vector<object*> items{ new_reference(cls.get()) };
            for (auto a : *tuple_view(args.get()))
                items.push_back(new_reference(a));
            ref const callArgs{ make_tuple(std::move(items)) };
            ref const kw{ aKwargs && kind_of(kwargs.get()) != NEOS_PY_DICT ? checked(invoke(ref{ checked(fn_builtin("dict")) }.get(), { new_reference(kwargs.get()) })) :
                (aKwargs ? new_reference(kwargs.get()) : nullptr) };
            push(checked(call(constructor.get(), callArgs.get(), kw.get())));
        }
        void unpickler::load_global()
        {
            auto line = readline();
            if (line.size() < 2u)
                bad_readline();
            ref const moduleName{ checked(decode_utf8(line.data(), line.size() - 1u, "strict")) };
            line = readline();
            if (line.size() < 2u)
                bad_readline();
            ref const globalName{ checked(decode_utf8(line.data(), line.size() - 1u, "strict")) };
            push(checked(call_find_class(moduleName.get(), globalName.get())));
        }
        void unpickler::load_stack_global()
        {
            ref const globalName{ pop() };
            ref const moduleName{ pop() };
            if (kind_of(moduleName.get()) != NEOS_PY_STR || kind_of(globalName.get()) != NEOS_PY_STR)
                unpickling_error("STACK_GLOBAL requires str");
            push(checked(call_find_class(moduleName.get(), globalName.get())));
        }
        void unpickler::load_persid()
        {
            auto const line = readline();
            if (line.empty())
                bad_readline();
            auto const text = line.substr(0u, line.size() - 1u);
            if (std::any_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80u; }))
                unpickling_error("persistent IDs in protocol 0 must be ASCII strings");
            ref const pid{ make_str(std::string{ text }, text.size()) };
            if (!persistentLoad)
                unpickling_error("A load persistent id instruction was encountered, but no persistent_load function was specified.");
            push(call1(persistentLoad.get(), pid.get()).release_ownership());
        }
        void unpickler::load_binpersid()
        {
            ref const pid{ pop() };
            if (!persistentLoad)
                unpickling_error("A load persistent id instruction was encountered, but no persistent_load function was specified.");
            push(call1(persistentLoad.get(), pid.get()).release_ownership());
        }
        void unpickler::load_pop()
        {
            // (the stack and the marks are separate: a mark at the top of the stack is popped)
            auto const length = size();
            if (!marks.empty() && marks.back() == length)
            {
                marks.pop_back();
                markSet = !marks.empty();
                fence = marks.empty() ? 0 : marks.back();
            }
            else if (length <= fence)
                stack_underflow();
            else
            {
                auto const o = stack.back();
                stack.pop_back();
                release(o);
            }
        }
        void unpickler::load_get()
        {
            auto const line = readline();
            if (line.size() < 2u)
                bad_readline();
            ref const key{ checked(long_from_string(std::string{ line.substr(0u, line.size() - 1u) }, 10)) };
            long long index = 0;
            py_checked(to_index(key.get(), index));
            auto const value = index >= 0 ? memo_get(static_cast<std::size_t>(index)) : nullptr;
            if (value == nullptr)
                unpickling_error("Memo value not found at index " + std::to_string(index));
            append(value);
        }
        void unpickler::load_extension(int aSize)
        {
            auto& s = state();
            auto const code = calc_binint(read(aSize), aSize);
            if (code <= 0)
                unpickling_error("EXT specifies code <= 0");
            ref const codeObject{ make_int(code) };
            // (the cache, else the registry's (module_name, class_name))
            auto const cached = dict_find(s.extensionCache.get(), codeObject.get());
            if (cached == -2)
                throw raised{};
            if (cached >= 0)
            {
                append(as<dict_object>(s.extensionCache.get())->items[static_cast<std::size_t>(cached)].second);
                return;
            }
            auto const position = dict_find(s.invertedRegistry.get(), codeObject.get());
            if (position == -2)
                throw raised{};
            if (position < 0)
                raise_error("ValueError", "unregistered extension code " + std::to_string(code));
            ref const pair{ ref::borrowed(as<dict_object>(s.invertedRegistry.get())->items[static_cast<std::size_t>(position)].second) };
            auto const items = tuple_view(pair.get());
            if (items == nullptr || items->size() != 2u || !is_str((*items)[0]) || !is_str((*items)[1]))
                raise_error("ValueError", "_inverted_registry[" + std::to_string(code) + "] isn't a 2-tuple of strings");
            ref const obj{ checked(call_find_class((*items)[0], (*items)[1])) };
            py_checked(dict_set(s.extensionCache.get(), codeObject.get(), obj.get()) >= 0);
            append(obj.get());
        }
        void unpickler::load_put()
        {
            auto const line = readline();
            if (line.size() < 2u)
                bad_readline();
            if (size() <= fence)
                stack_underflow();
            auto const value = stack.back();
            ref const key{ checked(long_from_string(std::string{ line.substr(0u, line.size() - 1u) }, 10)) };
            long long index = 0;
            py_checked(to_index(key.get(), index));
            if (index < 0)
                raise_error("ValueError", "negative PUT argument");
            memo_put(static_cast<std::size_t>(index), value);
        }
        void unpickler::do_append(long long aStart)
        {
            auto const length = size();
            if (aStart > length || aStart <= fence)
                stack_underflow();
            if (length == aStart)
                return;
            auto const list = stack[static_cast<std::size_t>(aStart - 1)];
            if (kind_of(list) == NEOS_PY_LIST)
            {
                auto& items = as<list_object>(list)->items;
                items.insert(items.end(), stack.begin() + aStart, stack.end());
                stack.resize(static_cast<std::size_t>(aStart));
                return;
            }
            if (ref const extend{ optional_attr(list, "extend") })
            {
                ref const slice{ pop_list(aStart) };
                call1(extend.get(), slice.get());
                return;
            }
            // (no extend(): append())
            ref const appendMethod{ checked(getattr(list, "append")) };
            for (auto i = aStart; i < length; ++i)
            {
                ref const r{ invoke(appendMethod.get(), { new_reference(stack[static_cast<std::size_t>(i)]) }) };
                if (!r)
                {
                    clear_stack(aStart);
                    throw raised{};
                }
            }
            clear_stack(aStart);
        }
        void unpickler::do_setitems(long long aStart)
        {
            auto const length = size();
            if (aStart > length || aStart <= fence)
                stack_underflow();
            if (length == aStart)
                return;
            if ((length - aStart) % 2 != 0)
                unpickling_error("odd number of items for SETITEMS");
            auto const dict = stack[static_cast<std::size_t>(aStart - 1)];
            bool const exact = kind_of(dict) == NEOS_PY_DICT;
            if (exact)
            {
                // (room for the items (growing geometrically, as batches of them are added))
                auto& d = *as<dict_object>(dict);
                auto const needed = d.items.size() + static_cast<std::size_t>((length - aStart) / 2);
                if (needed > d.items.capacity())
                {
                    d.items.reserve(std::max(needed, d.items.capacity() * 2u));
                    d.hashes.reserve(d.items.capacity());
                }
            }
            bool ok = true;
            for (auto i = aStart + 1; i < length && ok; i += 2)
                ok = (exact ? dict_set(dict, stack[static_cast<std::size_t>(i - 1)], stack[static_cast<std::size_t>(i)]) :
                    setitem(dict, stack[static_cast<std::size_t>(i - 1)], stack[static_cast<std::size_t>(i)])) >= 0;
            clear_stack(aStart);
            py_checked(ok);
        }
        void unpickler::load_additems()
        {
            auto const mark = marker();
            auto const length = size();
            if (mark > length || mark <= fence)
                stack_underflow();
            if (length == mark)
                return;
            auto const set = stack[static_cast<std::size_t>(mark - 1)];
            auto const value = builtin_value(set);
            if (value->type == state().setType)
            {
                for (auto i = mark; i < length; ++i)
                {
                    fn_set_add(value, stack[static_cast<std::size_t>(i)]);
                    if (failed())
                    {
                        clear_stack(mark);
                        throw raised{};
                    }
                }
                clear_stack(mark);
                return;
            }
            ref const add{ checked(getattr(set, "add")) };
            for (auto i = mark; i < length; ++i)
            {
                ref const r{ invoke(add.get(), { new_reference(stack[static_cast<std::size_t>(i)]) }) };
                if (!r)
                {
                    clear_stack(mark);
                    throw raised{};
                }
            }
            clear_stack(mark);
        }
        void unpickler::load_build()
        {
            // (instance state: instance.__setstate__(state), else its __dict__ (and its slots: setattr) updated)
            if (size() - 2 < fence)
                stack_underflow();
            ref stateValue{ pop() };
            auto const inst = stack.back();
            if (ref const setstate{ optional_attr(inst, "__setstate__") })
            {
                call1(setstate.get(), stateValue.get());
                return;
            }
            ref slotState;
            if (auto const pair = tuple_view(stateValue.get()); pair != nullptr && pair->size() == 2u)
            {
                ref const tuple{ stateValue };
                slotState = ref::borrowed((*pair)[1]);
                stateValue = ref::borrowed((*pair)[0]);
            }
            if (stateValue.get() != none())
            {
                if (!is_dict(stateValue.get()))
                    unpickling_error("state is not a dictionary");
                ref const dict{ checked(getattr(inst, "__dict__")) };
                // (an empty dict (e.g. a new instance's) given a dict's items: a copy of them (their hashes and index too))
                bool copied = false;
                if (kind_of(dict.get()) == NEOS_PY_DICT && kind_of(stateValue.get()) == NEOS_PY_DICT)
                {
                    auto& target = *as<dict_object>(dict.get());
                    auto const& source = *as<dict_object>(stateValue.get());
                    if (target.items.empty() && target.binder == nullptr && source.binder == nullptr && &target != &source)
                    {
                        for (auto const& [key, value] : source.items)
                        {
                            retain(key);
                            retain(value);
                        }
                        target.items = source.items;
                        target.hashes = source.hashes;
                        target.index = source.index;
                        copied = true;
                    }
                }
                if (!copied)
                {
                    std::vector<std::pair<object*, object*>> const items = as<dict_object>(builtin_value(stateValue.get()))->items;
                    for (auto const& [key, value] : items)
                        py_checked((kind_of(dict.get()) == NEOS_PY_DICT ? dict_set(dict.get(), key, value) : setitem(dict.get(), key, value)) >= 0);
                }
            }
            if (slotState)
            {
                if (!is_dict(slotState.get()))
                    unpickling_error("slot state is not a dictionary");
                std::vector<std::pair<object*, object*>> const items = as<dict_object>(builtin_value(slotState.get()))->items;
                for (auto const& [key, value] : items)
                {
                    if (!is_str(key))
                        raise_error("TypeError", "attribute name must be string, not '" + type_text(key) + "'");
                    py_checked(setattr(inst, str_value(key).c_str(), value) == 0);
                }
            }
        }
        void unpickler::load_mark()
        {
            markSet = true;
            fence = size();
            marks.push_back(fence);
        }
        void unpickler::load_reduce()
        {
            ref const argtup{ pop() };
            ref const callable{ pop() };
            if (tuple_view(argtup.get()) == nullptr)
                raise_error("TypeError", "argument list must be a tuple");
            ref const args{ kind_of(argtup.get()) == NEOS_PY_TUPLE ? new_reference(argtup.get()) : make_tuple([&]() 
                { 
                    std::vector<object*> v; 
                    for (auto a : *tuple_view(argtup.get())) 
                        v.push_back(new_reference(a)); 
                    return v; 
                }()) };
            push(checked(call(callable.get(), args.get(), nullptr)));
        }
        void unpickler::load_proto()
        {
            auto const i = static_cast<unsigned char>(*read(1));
            if (i > kHighestProtocol)
                raise_error("ValueError", "unsupported pickle protocol: " + std::to_string(i));
            proto = i;
        }
        void unpickler::load_frame()
        {
            auto const s = read(8);
            // (a frame must not begin before the current one ends (its header may be the current frame's last data))
            if (frameEnd >= 0)
            {
                if (nextReadIndex < frameEnd)
                    unpickling_error("beginning of a new frame before end of current frame");
                end_frame();
            }
            auto const frameLength = calc_binsize(s, 8);
            if (frameLength < 0)
                raise_error("OverflowError", "FRAME length exceeds system's maximum of " + std::to_string(std::numeric_limits<long long>::max()) + " bytes");
            read(frameLength);
            nextReadIndex -= frameLength;
            frameEnd = nextReadIndex + frameLength;
            savedInputLength = inputLength;
            inputLength = frameEnd;
        }

        // (find_class: the class's own, or a subclass's)
        object* unpickler::call_find_class(object* aModuleName, object* aGlobalName)
        {
            if (findClass)
                return invoke(findClass.get(), { new_reference(aModuleName), new_reference(aGlobalName) });
            return find_class(aModuleName, aGlobalName);
        }
        object* unpickler::find_class(object* aModuleName, object* aGlobalName)
        {
            try
            {
                auto& s = state();
                py moduleName = py::borrowed(aModuleName);
                py globalName = py::borrowed(aGlobalName);
                // (protocols 0 to 2: Python 2's names mapped to Python 3's (_compat_pickle's NAME_MAPPING and IMPORT_MAPPING))
                if (proto < 3 && fixImports)
                {
                    ref const key{ make_tuple({ moduleName.reference(), globalName.reference() }) };
                    auto const position = dict_find(s.nameMapping2to3.get(), key.get());
                    if (position == -2)
                        throw raised{};
                    if (position >= 0)
                    {
                        auto const item = as<dict_object>(s.nameMapping2to3.get())->items[static_cast<std::size_t>(position)].second;
                        auto const pair = tuple_view(item);
                        if (pair == nullptr || pair->size() != 2u)
                            raise_error("RuntimeError", "_compat_pickle.NAME_MAPPING values should be 2-tuples, not " + type_text(item));
                        if (!is_str((*pair)[0]) || !is_str((*pair)[1]))
                            raise_error("RuntimeError", "_compat_pickle.NAME_MAPPING values should be pairs of str, not (" + type_text((*pair)[0]) + ", " +
                                type_text((*pair)[1]) + ")");
                        moduleName = py::borrowed((*pair)[0]);
                        globalName = py::borrowed((*pair)[1]);
                    }
                    else
                    {
                        auto const p = dict_find(s.importMapping2to3.get(), moduleName.get());
                        if (p == -2)
                            throw raised{};
                        if (p >= 0)
                        {
                            auto const item = as<dict_object>(s.importMapping2to3.get())->items[static_cast<std::size_t>(p)].second;
                            if (!is_str(item))
                                raise_error("RuntimeError", "_compat_pickle.IMPORT_MAPPING values should be strings, not " + type_text(item));
                            moduleName = py::borrowed(item);
                        }
                    }
                }
                py const module = import_module(moduleName);
                return get_attribute(module, globalName, proto >= 4).release();
            }
            catch (raised const&)
            {
                return nullptr;
            }
        }

        void unpickler::load_one(unsigned char aOpcode)
        {
            switch (aOpcode)
            {
            case NONE: append(none()); break;
            case BININT: push(make_int(calc_binint(read(4), 4))); break;
            case BININT1: push(make_int(static_cast<unsigned char>(*read(1)))); break;
            case BININT2: push(make_int(calc_binint(read(2), 2))); break;
            case INT: load_int(); break;
            case LONG: load_long(); break;
            case LONG1: load_counted_long(1); break;
            case LONG4: load_counted_long(4); break;
            case FLOAT: load_float(); break;
            case BINFLOAT:
                {
                    auto const s = read(8);
                    std::uint64_t u = 0u;
                    for (int i = 0; i < 8; ++i)
                        u = (u << 8) | static_cast<unsigned char>(s[i]);
                    double x = 0.0;
                    std::memcpy(&x, &u, sizeof(x));
                    push(make_float(x));
                    break;
                }
            case SHORT_BINBYTES:
            case BINBYTES:
            case BINBYTES8:
                {
                    int const n = aOpcode == SHORT_BINBYTES ? 1 : aOpcode == BINBYTES ? 4 : 8;
                    auto const length = calc_binsize(read(n), n);
                    if (length < 0)
                        raise_error("OverflowError", "BINBYTES exceeds system's maximum size of " + std::to_string(std::numeric_limits<long long>::max()) + " bytes");
                    if (length <= inputLength - nextReadIndex)
                    {
                        push(make_bytes(std::string{ inputBuffer + nextReadIndex, static_cast<std::size_t>(length) }));
                        nextReadIndex += length;
                    }
                    else
                        push(make_bytes(read_into(length)));
                    break;
                }
            case BYTEARRAY8:
                {
                    auto const length = calc_binsize(read(8), 8);
                    if (length < 0)
                        raise_error("OverflowError", "BYTEARRAY8 exceeds system's maximum size of " + std::to_string(std::numeric_limits<long long>::max()) + " bytes");
                    push(make_bytearray(read_into(length)));
                    break;
                }
            case NEXT_BUFFER: load_next_buffer(); break;
            case READONLY_BUFFER: load_readonly_buffer(); break;
            case SHORT_BINSTRING: load_counted_binstring(1); break;
            case BINSTRING: load_counted_binstring(4); break;
            case STRING: load_string(); break;
            case UNICODE_: load_unicode(); break;
            case SHORT_BINUNICODE:
            case BINUNICODE:
            case BINUNICODE8:
                {
                    int const n = aOpcode == SHORT_BINUNICODE ? 1 : aOpcode == BINUNICODE ? 4 : 8;
                    auto const length = calc_binsize(read(n), n);
                    if (length < 0)
                        raise_error("OverflowError", "BINUNICODE exceeds system's maximum size of " + std::to_string(std::numeric_limits<long long>::max()) + " bytes");
                    auto const data = read(length);
                    push(checked(decode_utf8(data, static_cast<std::size_t>(length), "surrogatepass")));
                    break;
                }
            case EMPTY_TUPLE: push(make_tuple()); break;
            case TUPLE1:
            case TUPLE2:
            case TUPLE3:
                {
                    long long const n = aOpcode - TUPLE1 + 1;
                    if (size() < n)
                        stack_underflow();
                    push(pop_tuple(size() - n));
                    break;
                }
            case TUPLE: push(pop_tuple(marker())); break;
            case EMPTY_LIST: push(make_list()); break;
            case LIST: push(pop_list(marker())); break;
            case EMPTY_DICT: push(make_dict()); break;
            case DICT: load_dict(); break;
            case EMPTY_SET: push(checked(fn_set())); break;
            case ADDITEMS: load_additems(); break;
            case FROZENSET: load_frozenset(); break;
            case OBJ: load_obj(); break;
            case INST: load_inst(); break;
            case NEWOBJ: load_newobj(false); break;
            case NEWOBJ_EX: load_newobj(true); break;
            case GLOBAL: load_global(); break;
            case STACK_GLOBAL: load_stack_global(); break;
            case APPEND:
                if (size() - 1 <= fence)
                    stack_underflow();
                do_append(size() - 1);
                break;
            case APPENDS: do_append(marker()); break;
            case BUILD: load_build(); break;
            case DUP:
                if (size() <= fence)
                    stack_underflow();
                append(stack.back());
                break;
            case BINGET:
            case LONG_BINGET:
                {
                    auto const index = aOpcode == BINGET ? static_cast<long long>(static_cast<unsigned char>(*read(1))) : calc_binsize(read(4), 4);
                    auto const value = index >= 0 ? memo_get(static_cast<std::size_t>(index)) : nullptr;
                    if (value == nullptr)
                        unpickling_error("Memo value not found at index " + std::to_string(index));
                    append(value);
                    break;
                }
            case GET: load_get(); break;
            case MARK: load_mark(); break;
            case BINPUT:
            case LONG_BINPUT:
                {
                    auto const s = read(aOpcode == BINPUT ? 1 : 4);
                    if (size() <= fence)
                        stack_underflow();
                    auto const index = aOpcode == BINPUT ? static_cast<long long>(static_cast<unsigned char>(*s)) : calc_binsize(s, 4);
                    if (index < 0)
                        raise_error("ValueError", "negative LONG_BINPUT argument");
                    memo_put(static_cast<std::size_t>(index), stack.back());
                    break;
                }
            case PUT: load_put(); break;
            case MEMOIZE:
                if (size() <= fence)
                    stack_underflow();
                memo_put(memoLength, stack.back());
                break;
            case POP: load_pop(); break;
            case POP_MARK: clear_stack(marker()); break;
            case SETITEM: do_setitems(size() - 2); break;
            case SETITEMS: do_setitems(marker()); break;
            case PERSID: load_persid(); break;
            case BINPERSID: load_binpersid(); break;
            case REDUCE: load_reduce(); break;
            case PROTO: load_proto(); break;
            case FRAME: load_frame(); break;
            case EXT1: load_extension(1); break;
            case EXT2: load_extension(2); break;
            case EXT4: load_extension(4); break;
            case NEWTRUE: append(make_bool(true)); break;
            case NEWFALSE: append(make_bool(false)); break;
            default:
                {
                    char text[8];
                    if (aOpcode >= 0x20u && aOpcode <= 0x7eu && aOpcode != '\'' && aOpcode != '\\')
                        std::snprintf(text, sizeof(text), "'%c'", aOpcode);
                    else
                        std::snprintf(text, sizeof(text), "'\\x%02x'", aOpcode);
                    unpickling_error(std::string{ "invalid load key, " } + text + ".");
                }
            }
        }

        object* unpickler::load()
        {
            marks.clear();
            markSet = false;
            fence = 0;
            proto = 0;
            clear_stack(0);
            for (;;)
            {
                unsigned char op = 0u;
                if (nextReadIndex < inputLength)
                    op = static_cast<unsigned char>(inputBuffer[nextReadIndex++]);
                else
                {
                    try
                    {
                        op = static_cast<unsigned char>(*read_impl(1));
                    }
                    catch (raised const&)
                    {
                        // (the end of the input: EOFError)
                        ref const e{ take_exception() };
                        if (isinstance(py::borrowed(e.get()), py::borrowed(state().unpicklingError.get())))
                            raise_error("EOFError", "Ran out of input");
                        reraise_exception(e.get());
                        throw raised{};
                    }
                }
                if (op == STOP)
                    break;
                load_one(op);
            }
            skip_consumed();
            return pop().release_ownership();
        }

        // (an unpickler's file, encoding and buffers (_Unpickler_SetInputStream, _Unpickler_SetInputEncoding and _Unpickler_SetBuffers))
        void set_input(unpickler& aUnpickler, object* aFile)
        {
            aUnpickler.peekMethod = optional_attr(aFile, "peek");
            aUnpickler.readintoMethod = optional_attr(aFile, "readinto");
            aUnpickler.readMethod = optional_attr(aFile, "read");
            aUnpickler.readlineMethod = optional_attr(aFile, "readline");
            if (!aUnpickler.readlineMethod || !aUnpickler.readMethod)
            {
                aUnpickler.peekMethod = aUnpickler.readintoMethod = aUnpickler.readMethod = aUnpickler.readlineMethod = ref{};
                raise_error("TypeError", "file must have 'read' and 'readline' attributes");
            }
        }
        void set_buffers(unpickler& aUnpickler, object* aBuffers)
        {
            if (aBuffers == nullptr || aBuffers == none())
                aUnpickler.buffers = ref{};
            else
                aUnpickler.buffers = ref{ checked(iter(aBuffers)) };
        }

        // --- the native states of the Pickler and Unpickler instances (by identity, until an instance is destroyed: a weak reference's
        // callback forgets it; an instance that cannot be weakly referenced is kept)

        template <typename T>
        struct instance_entry
        {
            T core;
            ref weak;
            ref strong;
        };
        template <typename T>
        std::unordered_map<object*, std::unique_ptr<instance_entry<T>>>& instance_entries()
        {
            static auto* sEntries = new std::unordered_map<object*, std::unique_ptr<instance_entry<T>>>{};
            return *sEntries;
        }
        template <typename T>
        T& core_of(object* aSelf)
        {
            auto& entries = instance_entries<T>();
            if (auto const e = entries.find(aSelf); e != entries.end())
                return e->second->core;
            auto entry = std::make_unique<instance_entry<T>>();
            py const forget = make_native_function("_forget", "_forget", "", sModule, [aSelf](std::vector<py> const&, object*)
            {
                auto& entries = instance_entries<T>();
                if (auto const e = entries.find(aSelf); e != entries.end())
                {
                    auto const entry = std::move(e->second);
                    entries.erase(e);
                }
                return py::none();
            });
            ref const weakrefClass{ module_attribute("weakref", "ref") };
            entry->weak = ref{ weakrefClass ? invoke(weakrefClass.get(), { new_reference(aSelf), forget.reference() }) : nullptr };
            if (!entry->weak)
            {
                clear_error();
                entry->strong = ref::borrowed(aSelf);
            }
            auto& core = entry->core;
            entries.emplace(aSelf, std::move(entry));
            return core;
        }

        void reset(pickler& aPickler)
        {
            aPickler.memo.clear();
            aPickler.interned.clear();
            aPickler.clear_buffer();
            aPickler.writeMethod = aPickler.persistentId = aPickler.reducerOverride = aPickler.dispatchTable = aPickler.bufferCallback = ref{};
            aPickler.fastMemo.clear();
        }
        void reset(unpickler& aUnpickler)
        {
            aUnpickler.clear_stack(0);
            aUnpickler.memo_cleanup();
            aUnpickler.memo.assign(32u, nullptr);
            aUnpickler.marks.clear();
            aUnpickler.inputObject = aUnpickler.readMethod = aUnpickler.readintoMethod = aUnpickler.readlineMethod = aUnpickler.peekMethod = aUnpickler.buffers = ref{};
            aUnpickler.inputBuffer = nullptr;
            aUnpickler.inputLength = aUnpickler.nextReadIndex = aUnpickler.prefetchedIndex = aUnpickler.savedInputLength = 0;
            aUnpickler.frameEnd = -1;
            aUnpickler.inputLine.clear();
            aUnpickler.proto = 0;
        }
        // (a pickler or unpickler running: "already used" if it is running)
        struct running_guard
        {
            running_guard(bool& aRunning, char const* aWhat) : running{ aRunning }
            {
                if (running)
                    raise_error("RuntimeError", std::string{ aWhat } + " object is already used");
                running = true;
            }
            ~running_guard() { running = false; }
            bool& running;
        };

        // --- the memos' proxies (_pickle.PicklerMemoProxy and _pickle.UnpicklerMemoProxy)

        struct memoproxy_object
        {
            header h;
            object* owner;      ///< (its Pickler or Unpickler)
            ~memoproxy_object() { release(owner); }
        };
        neos_py_type make_memoproxy_type(bool aUnpickler);
        neos_py_type sPicklerMemoProxyType = make_memoproxy_type(false);
        neos_py_type sUnpicklerMemoProxyType = make_memoproxy_type(true);
        ref sPicklerMemoProxyClass;
        ref sUnpicklerMemoProxyClass;

        object* memoproxy_copy(object* aProxy)
        {
            ref const result{ make_dict() };
            auto const owner = reinterpret_cast<memoproxy_object*>(aProxy)->owner;
            if (aProxy->type == &sPicklerMemoProxyType.base)
            {
                // ({id(obj): (index, obj)})
                ref const id{ checked(fn_builtin("id")) };
                for (auto const& e : core_of<pickler>(owner).memo.entries())
                    if (e.key != nullptr)
                    {
                        ref const key{ checked(invoke(id.get(), { new_reference(e.key) })) };
                        ref const value{ make_tuple({ make_int(static_cast<long long>(e.value)), new_reference(e.key) }) };
                        py_checked(dict_set(result.get(), key.get(), value.get()) >= 0);
                    }
            }
            else
            {
                auto const& memo = core_of<unpickler>(owner).memo;
                for (std::size_t i = 0u; i < memo.size(); ++i)
                    if (memo[i] != nullptr)
                    {
                        ref const key{ make_int(static_cast<long long>(i)) };
                        py_checked(dict_set(result.get(), key.get(), memo[i]) >= 0);
                    }
            }
            return new_reference(result.get());
        }
        object* memoproxy_method(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs)
        {
            try
            {
                auto const& args = tuple_items(aArgs);
                std::string_view const name{ static_cast<char const*>(aData) };
                auto const self = args[0];
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                    return error("TypeError", std::string{ name } + "() takes no keyword arguments");
                if (args.size() != 1u)
                    return error("TypeError", std::string{ name } + "() takes no arguments (" + std::to_string(args.size() - 1u) + " given)");
                auto const owner = reinterpret_cast<memoproxy_object*>(self)->owner;
                bool const forUnpickler = self->type == &sUnpicklerMemoProxyType.base;
                if (name == "clear")
                {
                    if (forUnpickler)
                        core_of<unpickler>(owner).memo_cleanup();
                    else
                    {
                        core_of<pickler>(owner).memo.clear();
                        core_of<pickler>(owner).interned.clear();
                    }
                    return new_reference(none());
                }
                ref const contents{ checked(memoproxy_copy(self)) };
                if (name == "copy")
                    return new_reference(contents.get());
                // (__reduce__: (dict, (copy,)))
                ref const dictClass{ checked(fn_builtin("dict")) };
                return make_tuple({ new_reference(dictClass.get()), make_tuple({ new_reference(contents.get()) }) });
            }
            catch (raised const&)
            {
                return nullptr;
            }
        }
        neos_py_type make_memoproxy_type(bool aUnpickler)
        {
            auto type = make_type(aUnpickler ? "_pickle.UnpicklerMemoProxy" : "_pickle.PicklerMemoProxy", aUnpickler ? "UnpicklerMemoProxy" : "PicklerMemoProxy",
                retain_object, release_object<memoproxy_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                bool const u = aSelf->type == &sUnpicklerMemoProxyType.base;
                return make_str(std::string{ u ? "<_pickle.UnpicklerMemoProxy object at " : "<_pickle.PicklerMemoProxy object at " } + object_address(aSelf) + ">");
            };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                bool const u = aSelf->type == &sUnpicklerMemoProxyType.base;
                static char const* const sMethods[] = { "clear", "copy", "__reduce__" };
                for (auto m : sMethods)
                    if (std::strcmp(m, aName) == 0)
                        return make_function(m, memoproxy_method, const_cast<char*>(m), aSelf);
                if (std::strcmp(aName, "__class__") == 0)
                    return new_reference((u ? sUnpicklerMemoProxyClass : sPicklerMemoProxyClass).get());
                return nullptr;
            };
            type.hash = [](neos_py_context*, object* aSelf, long long*) -> int
            {
                bool const u = aSelf->type == &sUnpicklerMemoProxyType.base;
                error("TypeError", std::string{ "unhashable type: '" } + (u ? "_pickle.UnpicklerMemoProxy" : "_pickle.PicklerMemoProxy") + "'");
                return -1;
            };
            return type;
        }
        object* make_memoproxy(object* aOwner, bool aUnpickler)
        {
            retain(aOwner);
            return &(new memoproxy_object{ { { &(aUnpickler ? sUnpicklerMemoProxyType : sPicklerMemoProxyType).base } }, aOwner })->h.base;
        }
        object* memoproxy_new(neos_py_context*, void* aData, neos_py_object*, neos_py_object*)
        {
            return error("TypeError", std::string{ "cannot create '" } + (aData != nullptr ? "_pickle.UnpicklerMemoProxy" : "_pickle.PicklerMemoProxy") + "' instances");
        }
        int pickler_memoproxy_instancecheck(neos_py_context*, void*, object* aObject) { return aObject != nullptr && aObject->type == &sPicklerMemoProxyType.base ? 1 : 0; }
        int unpickler_memoproxy_instancecheck(neos_py_context*, void*, object* aObject) { return aObject != nullptr && aObject->type == &sUnpicklerMemoProxyType.base ? 1 : 0; }

        // --- Pickler and Unpickler (classes made by type, of closures)

        using method_body = std::function<py(py const& aSelf, std::vector<object*> const& aArgs, object* aKwargs)>;
        // (a method of Pickler or Unpickler (aClass): its self is an instance of the class)
        py class_method(char const* aClass, char const* aName, method_body aBody, char const* aDoc = nullptr)
        {
            std::string const qualname = std::string{ aClass } + "." + aName;
            auto const doc = pickle_docs::doc(qualname.c_str());
            std::string const className{ aClass };
            std::string const name{ aName };
            return make_native_function(aName, qualname, aDoc != nullptr ? aDoc : doc != nullptr ? doc : "", sModule,
                [qualname, className, name, aBody](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                if (aArgs.empty())
                    raise_error("TypeError", "unbound method " + qualname + "() needs an argument");
                auto const& cls = className == "Pickler" ? state().picklerClass : state().unpicklerClass;
                if (!isinstance(aArgs[0], py::borrowed(cls.get())))
                    raise_error("TypeError", "descriptor '" + name + "' for '_pickle." + className + "' objects doesn't apply to a '" + type_text(aArgs[0].get()) + "' object");
                std::vector<object*> args;
                args.reserve(aArgs.size() - 1u);
                for (std::size_t i = 1u; i < aArgs.size(); ++i)
                    args.push_back(aArgs[i].get());
                return aBody(aArgs[0], args, aKwargs != nullptr && as<dict_object>(aKwargs)->items.empty() ? nullptr : aKwargs);
            });
        }
        // (a property of Pickler or Unpickler: aSet null: read-only)
        py class_property(char const* aClass, char const* aName, method_body aGet, method_body aSet, method_body aDelete)
        {
            py const getter = class_method(aClass, aName, std::move(aGet));
            py const setter = aSet ? class_method(aClass, aName, std::move(aSet)) : py::none();
            py const deleter = aDelete ? class_method(aClass, aName, std::move(aDelete)) : py::none();
            return builtin("property")(getter, setter, deleter);
        }
        py make_class(char const* aName, std::vector<std::pair<char const*, py>> const& aNamespace)
        {
            py const ns = py_dict();
            ns.set_item(py{ "__module__" }, py{ sModule });
            ns.set_item(py{ "__qualname__" }, py{ aName });
            ns.set_item(py{ "__doc__" }, py{ pickle_docs::doc(aName) });
            for (auto const& [name, value] : aNamespace)
                ns.set_item(py{ name }, value);
            return builtin("type")(py{ aName }, py_tuple({ builtin("object") }), ns);
        }
        // (the name of an instance's class (its tp_name, as the messages have it))
        std::string instance_class_name(py const& aSelf)
        {
            return type_of(aSelf).attr("__name__").str();
        }
        // (an int attribute (Py_T_INT))
        int int_attribute(object* aValue)
        {
            if (aValue == nullptr)
                raise_error("TypeError", "can't delete numeric/char attribute");
            long long value = 0;
            py_checked(to_index(aValue, value));
            if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
                raise_error("OverflowError", "Python int too large to convert to C int");
            return static_cast<int>(value);
        }

        py make_pickler_class()
        {
            auto& s = state();
            static signature const sInit{ "Pickler", { "file", "protocol", "fix_imports", "buffer_callback" }, 0u, 4u, 1u };
            static signature const sDump{ "dump", { "obj" }, 1u, 1u, 1u };
            py const persistentId = class_method("Pickler", "persistent_id", [](py const&, std::vector<object*> const& a, object* aKwargs)
            {
                if (aKwargs != nullptr)
                    raise_error("TypeError", "Pickler.persistent_id() takes no keyword arguments");
                if (a.size() != 1u)
                    raise_error("TypeError", "Pickler.persistent_id() takes exactly one argument (" + std::to_string(a.size()) + " given)");
                return py::none();
            }, "");
            s.persistentIdFunction = persistentId.get();
            return make_class("Pickler", {
                { "__init__", class_method("Pickler", "__init__", [](py const& aSelf, std::vector<object*> const& aArgs, object* aKwargs)
                    {
                        auto const a = bind(sInit, aArgs, aKwargs);
                        auto& p = core_of<pickler>(aSelf.get());
                        running_guard const running{ p.running, "Pickler" };
                        if (p.writeMethod)
                            reset(p);
                        set_protocol(p, a[1], bool_argument(a[2], true));
                        set_output(p, a[0]);
                        set_buffer_callback(p, a[3]);
                        p.clear_buffer();
                        p.fast = 0;
                        p.fastNesting = 0;
                        p.fastMemo.clear();
                        if (!p.dispatchTable)
                            p.dispatchTable = optional_attr(aSelf.get(), "dispatch_table");
                        return py::none();
                    }) },
                { "dump", class_method("Pickler", "dump", [](py const& aSelf, std::vector<object*> const& aArgs, object* aKwargs)
                    {
                        auto const a = bind(sDump, aArgs, aKwargs);
                        auto& p = core_of<pickler>(aSelf.get());
                        // (its __init__ must have been called (a subclass's __init__ may not have called it))
                        if (!p.writeMethod)
                            pickling_error("Pickler.__init__() was not called by " + instance_class_name(aSelf) + ".__init__()");
                        running_guard const running{ p.running, "Pickler" };
                        p.clear_buffer();
                        p.dump(aSelf.get(), a[0]);
                        p.flush_to_file();
                        return py::none();
                    }) },
                { "clear_memo", class_method("Pickler", "clear_memo", [](py const& aSelf, std::vector<object*> const& aArgs, object* aKwargs)
                    {
                        static signature const sClear{ "clear_memo", {}, 0u, 0u, 0u };
                        bind(sClear, aArgs, aKwargs);
                        auto& p = core_of<pickler>(aSelf.get());
                        p.memo.clear();
                        p.interned.clear();
                        return py::none();
                    }) },
                { "persistent_id", persistentId },
                { "__sizeof__", class_method("Pickler", "__sizeof__", [](py const& aSelf, std::vector<object*> const&, object*)
                    {
                        auto const& p = core_of<pickler>(aSelf.get());
                        return py{ static_cast<long long>(sizeof(pickler) + p.memo.allocated() * sizeof(memo_table::entry) + p.output.capacity() + 33u) };
                    }) },
                { "memo", class_property("Pickler", "memo",
                    [](py const& aSelf, std::vector<object*> const&, object*) { return py{ make_memoproxy(aSelf.get(), false) }; },
                    [](py const& aSelf, std::vector<object*> const& a, object*)
                    {
                        auto& p = core_of<pickler>(aSelf.get());
                        auto const value = a[0];
                        memo_table memo;
                        if (value->type == &sPicklerMemoProxyType.base)
                            memo = core_of<pickler>(reinterpret_cast<memoproxy_object*>(value)->owner).memo;
                        else if (is_dict(value))
                        {
                            std::vector<std::pair<object*, object*>> const items = as<dict_object>(builtin_value(value))->items;
                            for (auto const& item : items)
                            {
                                auto const pair = tuple_view(item.second);
                                if (pair == nullptr || pair->size() != 2u)
                                    raise_error("TypeError", "'memo' values must be 2-item tuples");
                                long long index = 0;
                                py_checked(to_index((*pair)[0], index));
                                memo.set((*pair)[1], static_cast<std::size_t>(index));
                            }
                        }
                        else
                            raise_error("TypeError", "'memo' attribute must be a PicklerMemoProxy object or dict, not " + type_text(value));
                        p.memo = memo;
                        p.interned.clear();
                        for (auto const& e : p.memo.entries())
                            if (e.key != nullptr && kind_of(e.key) == NEOS_PY_STR && internable(as<str_object>(e.key)->value))
                                p.interned.insert(e.key, e.value);
                        return py::none();
                    },
                    [](py const&, std::vector<object*> const&, object*) -> py { raise_error("TypeError", "attribute deletion is not supported"); }) },
                { "bin", class_property("Pickler", "bin",
                    [](py const& aSelf, std::vector<object*> const&, object*) { return py{ core_of<pickler>(aSelf.get()).bin }; },
                    [](py const& aSelf, std::vector<object*> const& a, object*) { core_of<pickler>(aSelf.get()).bin = int_attribute(a[0]); return py::none(); },
                    [](py const&, std::vector<object*> const&, object*) -> py { int_attribute(nullptr); return py::none(); }) },
                { "fast", class_property("Pickler", "fast",
                    [](py const& aSelf, std::vector<object*> const&, object*) { return py{ core_of<pickler>(aSelf.get()).fast }; },
                    [](py const& aSelf, std::vector<object*> const& a, object*) { core_of<pickler>(aSelf.get()).fast = int_attribute(a[0]); return py::none(); },
                    [](py const&, std::vector<object*> const&, object*) -> py { int_attribute(nullptr); return py::none(); }) },
                { "dispatch_table", class_property("Pickler", "dispatch_table",
                    [](py const& aSelf, std::vector<object*> const&, object*)
                    {
                        auto const& p = core_of<pickler>(aSelf.get());
                        if (!p.dispatchTable)
                            raise_error("AttributeError", "'_pickle.Pickler' object has no attribute 'dispatch_table'");
                        return py::borrowed(p.dispatchTable.get());
                    },
                    [](py const& aSelf, std::vector<object*> const& a, object*) { core_of<pickler>(aSelf.get()).dispatchTable = ref::borrowed(a[0]); return py::none(); },
                    [](py const& aSelf, std::vector<object*> const&, object*)
                    {
                        auto& p = core_of<pickler>(aSelf.get());
                        if (!p.dispatchTable)
                            raise_error("AttributeError", "dispatch_table");
                        p.dispatchTable = ref{};
                        return py::none();
                    }) } });
        }

        py make_unpickler_class()
        {
            auto& s = state();
            static signature const sInit{ "Unpickler", { "file", "fix_imports", "encoding", "errors", "buffers" }, 0u, 1u, 1u };
            static signature const sFindClass{ "find_class", { "module_name", "global_name" }, 2u, 2u, 2u };
            static signature const sPersistentLoad{ "persistent_load", { "pid" }, 1u, 1u, 1u };
            py const persistentLoad = class_method("Unpickler", "persistent_load", [](py const&, std::vector<object*> const& aArgs, object* aKwargs) -> py
            {
                bind(sPersistentLoad, aArgs, aKwargs);
                unpickling_error("A load persistent id instruction was encountered, but no persistent_load function was specified.");
            }, "");
            py const findClass = class_method("Unpickler", "find_class", [](py const& aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                auto const a = bind(sFindClass, aArgs, aKwargs);
                return py{ core_of<unpickler>(aSelf.get()).find_class(a[0], a[1]) };
            });
            s.persistentLoadFunction = persistentLoad.get();
            s.findClassFunction = findClass.get();
            return make_class("Unpickler", {
                { "__init__", class_method("Unpickler", "__init__", [](py const& aSelf, std::vector<object*> const& aArgs, object* aKwargs)
                    {
                        auto const a = bind(sInit, aArgs, aKwargs);
                        auto& u = core_of<unpickler>(aSelf.get());
                        running_guard const running{ u.running, "Unpickler" };
                        if (u.readMethod)
                            reset(u);
                        set_input(u, a[0]);
                        u.encoding = str_argument("Unpickler", "encoding", a[2], "ASCII");
                        u.errors = str_argument("Unpickler", "errors", a[3], "strict");
                        set_buffers(u, a[4]);
                        u.fixImports = bool_argument(a[1], true);
                        u.clear_stack(0);
                        u.memo_cleanup();
                        u.memo.assign(32u, nullptr);
                        u.proto = 0;
                        return py::none();
                    }) },
                { "load", class_method("Unpickler", "load", [](py const& aSelf, std::vector<object*> const& aArgs, object* aKwargs)
                    {
                        static signature const sLoad{ "load", {}, 0u, 0u, 0u };
                        bind(sLoad, aArgs, aKwargs);
                        auto& u = core_of<unpickler>(aSelf.get());
                        if (!u.readMethod)
                            unpickling_error("Unpickler.__init__() was not called by " + instance_class_name(aSelf) + ".__init__()");
                        running_guard const running{ u.running, "Unpickler" };
                        // (persistent_load and find_class: the class's own are native)
                        auto const own = [&aSelf](char const* aName, object* aFunction) -> ref
                        {
                            ref method{ checked(getattr(aSelf.get(), aName)) };
                            if (ref const function{ optional_attr(method.get(), "__func__") }; function && function.get() == aFunction)
                                if (ref const self{ optional_attr(method.get(), "__self__") }; self.get() == aSelf.get())
                                    return ref{};
                            return method;
                        };
                        struct finish
                        {
                            ~finish()
                            {
                                self.persistentLoad = ref{};
                                self.findClass = ref{};
                                self.self = nullptr;
                            }
                            unpickler& self;
                        } const finishing{ u };
                        u.self = aSelf.get();
                        u.persistentLoad = own("persistent_load", state().persistentLoadFunction);
                        u.findClass = own("find_class", state().findClassFunction);
                        return py{ u.load() };
                    }) },
                { "find_class", findClass },
                { "persistent_load", persistentLoad },
                { "__sizeof__", class_method("Unpickler", "__sizeof__", [](py const& aSelf, std::vector<object*> const&, object*)
                    {
                        auto const& u = core_of<unpickler>(aSelf.get());
                        return py{ static_cast<long long>(sizeof(unpickler) + u.memo.size() * sizeof(object*) + u.marks.capacity() * sizeof(long long) + u.inputLine.size() +
                            u.encoding.size() + u.errors.size() + 3u) };
                    }) },
                { "memo", class_property("Unpickler", "memo",
                    [](py const& aSelf, std::vector<object*> const&, object*) { return py{ make_memoproxy(aSelf.get(), true) }; },
                    [](py const& aSelf, std::vector<object*> const& a, object*)
                    {
                        auto& u = core_of<unpickler>(aSelf.get());
                        auto const value = a[0];
                        std::vector<object*> memo;
                        if (value->type == &sUnpicklerMemoProxyType.base)
                        {
                            memo = core_of<unpickler>(reinterpret_cast<memoproxy_object*>(value)->owner).memo;
                            for (auto m : memo)
                                retain(m);
                        }
                        else if (is_dict(value))
                        {
                            // (as _pickle.c: the items are put in the memo that the new one replaces)
                            std::vector<std::pair<object*, object*>> const items = as<dict_object>(builtin_value(value))->items;
                            memo.assign(items.size(), nullptr);
                            for (auto const& item : items)
                            {
                                if (!is_int_like(builtin_value(item.first)))
                                    raise_error("TypeError", "memo key must be integers");
                                long long index = 0;
                                py_checked(to_index(item.first, index));
                                if (index < 0)
                                    raise_error("ValueError", "memo key must be positive integers.");
                                u.memo_put(static_cast<std::size_t>(index), item.second);
                            }
                        }
                        else
                            raise_error("TypeError", "'memo' attribute must be an UnpicklerMemoProxy object or dict, not " + type_text(value));
                        u.memo_cleanup();
                        u.memo = std::move(memo);
                        u.memoLength = static_cast<std::size_t>(std::count_if(u.memo.begin(), u.memo.end(), [](object* m) { return m != nullptr; }));
                        return py::none();
                    },
                    [](py const&, std::vector<object*> const&, object*) -> py { raise_error("TypeError", "attribute deletion is not supported"); }) } });
        }

        // --- the module's functions

        object* f_dump(std::vector<object*> const& aArgs, object* aKwargs)
        {
            try
            {
                static signature const sSignature{ "dump", { "obj", "file", "protocol", "fix_imports", "buffer_callback" }, 0u, 3u, 2u };
                auto const a = bind(sSignature, aArgs, aKwargs);
                pickler p;
                set_protocol(p, a[2], bool_argument(a[3], true));
                set_output(p, a[1]);
                set_buffer_callback(p, a[4]);
                p.dump(nullptr, a[0]);
                p.flush_to_file();
                return new_reference(none());
            }
            catch (raised const&)
            {
                return nullptr;
            }
        }
        object* f_dumps(std::vector<object*> const& aArgs, object* aKwargs)
        {
            try
            {
                static signature const sSignature{ "dumps", { "obj", "protocol", "fix_imports", "buffer_callback" }, 0u, 2u, 1u };
                auto const a = bind(sSignature, aArgs, aKwargs);
                pickler p;
                set_protocol(p, a[1], bool_argument(a[2], true));
                set_buffer_callback(p, a[3]);
                p.dump(nullptr, a[0]);
                return p.take_output();
            }
            catch (raised const&)
            {
                return nullptr;
            }
        }
        object* f_load(std::vector<object*> const& aArgs, object* aKwargs)
        {
            try
            {
                static signature const sSignature{ "load", { "file", "fix_imports", "encoding", "errors", "buffers" }, 0u, 1u, 1u };
                auto const a = bind(sSignature, aArgs, aKwargs);
                unpickler u;
                set_input(u, a[0]);
                u.encoding = str_argument("load", "encoding", a[2], "ASCII");
                u.errors = str_argument("load", "errors", a[3], "strict");
                set_buffers(u, a[4]);
                u.fixImports = bool_argument(a[1], true);
                return u.load();
            }
            catch (raised const&)
            {
                return nullptr;
            }
        }
        object* f_loads(std::vector<object*> const& aArgs, object* aKwargs)
        {
            try
            {
                static signature const sSignature{ "loads", { "data", "fix_imports", "encoding", "errors", "buffers" }, 1u, 1u, 1u };
                auto const a = bind(sSignature, aArgs, aKwargs);
                unpickler u;
                u.set_string_input(a[0]);
                if (a[2] != nullptr)
                    u.encoding = str_argument("loads", "encoding", a[2], "ASCII");
                if (a[3] != nullptr)
                    u.errors = str_argument("loads", "errors", a[3], "strict");
                set_buffers(u, a[4]);
                u.fixImports = bool_argument(a[1], true);
                return u.load();
            }
            catch (raised const&)
            {
                return nullptr;
            }
        }
        builtin_entry const sFunctions[] = { { "dump", f_dump }, { "dumps", f_dumps }, { "load", f_load }, { "loads", f_loads } };

        // (the module's state: its exceptions and classes, and the objects of other modules that it uses)
        void make_state()
        {
            auto& s = state();
            if (s.made)
                return;
            s.made = true;
            s.pickleError = ref{ checked(module_exception("_pickle.PickleError", { "Exception" })) };
            s.picklingError = ref{ checked(module_exception("_pickle.PicklingError", { "_pickle.PickleError" })) };
            s.unpicklingError = ref{ checked(module_exception("_pickle.UnpicklingError", { "_pickle.PickleError" })) };
            s.getattrFunction = ref{ checked(fn_builtin("getattr")) };
            py const copyreg{ fn_import("copyreg") };
            auto const table = [&copyreg](char const* aName, char const* aWhat) -> ref
            {
                py const value = copyreg.attr(aName);
                if (kind_of(value.get()) != NEOS_PY_DICT)
                    raise_error("RuntimeError", std::string{ "copyreg." } + aName + " should be a dict" + aWhat + type_text(value.get()));
                return ref::borrowed(value.get());
            };
            s.dispatchTable = table("dispatch_table", ", not ");
            s.extensionRegistry = table("_extension_registry", ", not ");
            s.invertedRegistry = table("_inverted_registry", ", not ");
            s.extensionCache = table("_extension_cache", ", not ");
            py const compat{ fn_import("_compat_pickle") };
            auto const mapping = [&compat](char const* aName) -> ref
            {
                py const value = compat.attr(aName);
                if (kind_of(value.get()) != NEOS_PY_DICT)
                    raise_error("RuntimeError", std::string{ "_compat_pickle." } + aName + " should be a dict, not " + type_text(value.get()));
                return ref::borrowed(value.get());
            };
            s.nameMapping2to3 = mapping("NAME_MAPPING");
            s.importMapping2to3 = mapping("IMPORT_MAPPING");
            s.nameMapping3to2 = mapping("REVERSE_NAME_MAPPING");
            s.importMapping3to2 = mapping("REVERSE_IMPORT_MAPPING");
            s.codecsEncode = ref{ checked(module_attribute("codecs", "encode")) };
            s.partial = ref{ checked(module_attribute("functools", "partial")) };
            s.typeType = ref{ checked(fn_builtin("type")) };
            s.functionType = ref{ checked(module_attribute("types", "FunctionType")) };
            s.noneType = ref{ checked(class_of(none())) };
            s.ellipsisType = ref{ checked(class_of(&sEllipsis.base)) };
            s.notImplementedType = ref{ checked(class_of(not_implemented())) };
            s.bytesClass = ref{ checked(fn_builtin("bytes")) };
            s.bytearrayClass = ref{ checked(fn_builtin("bytearray")) };
            s.setClass = ref{ checked(fn_builtin("set")) };
            s.frozensetClass = ref{ checked(fn_builtin("frozenset")) };
            {
                ref const b{ make_bytes({}) };
                ref const ba{ make_bytearray({}) };
                ref const st{ checked(fn_set()) };
                ref const fs{ checked(b_frozenset({}, nullptr)) };
                s.bytesType = b.get()->type;
                s.bytearrayType = ba.get()->type;
                s.setType = st.get()->type;
                s.frozensetType = fs.get()->type;
            }
            buffer_providers()[&sPickleBufferType.base] = buffer_provider{ picklebuffer_buffer, picklebuffer_exports };
            s.pickleBufferClass = ref{ context()->type_new(context(), "PickleBuffer", "pickle.PickleBuffer", picklebuffer_new, nullptr, picklebuffer_instancecheck) };
            sPicklerMemoProxyClass = ref{ context()->type_new(context(), "PicklerMemoProxy", "_pickle.PicklerMemoProxy", memoproxy_new, nullptr, 
                pickler_memoproxy_instancecheck) };
            sUnpicklerMemoProxyClass = ref{ context()->type_new(context(), "UnpicklerMemoProxy", "_pickle.UnpicklerMemoProxy", memoproxy_new, 
                reinterpret_cast<void*>(1), unpickler_memoproxy_instancecheck) };
            s.picklerClass = ref::borrowed(make_pickler_class().get());
            s.unpicklerClass = ref::borrowed(make_unpickler_class().get());
        }

        bool pickle_c_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                make_state();
                auto& s = state();
                auto const add = [&](char const* aName, object* aValue) { aAttributes.emplace_back(aName, new_reference(aValue)); };
                aAttributes.emplace_back("__doc__", make_str(pickle_docs::doc("_pickle")));
                add("PickleBuffer", s.pickleBufferClass.get());
                add("Pickler", s.picklerClass.get());
                add("Unpickler", s.unpicklerClass.get());
                add("PickleError", s.pickleError.get());
                add("PicklingError", s.picklingError.get());
                add("UnpicklingError", s.unpicklingError.get());
                for (auto const& f : sFunctions)
                {
                    ref const function{ make_builtin(f) };
                    as<function_object>(function.get())->doc = pickle_docs::doc((std::string{ "_pickle." } + f.name).c_str());
                    add(f.name, function.get());
                }
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        // --- the module pickle (Lib/pickle.py): its constants and helpers, and _pickle's classes and functions

        struct opcode_constant
        {
            char const* name;
            char const* value;
            std::size_t size;
        };
        opcode_constant const sOpcodeConstants[] =
        {
            { "MARK", "(", 1 }, { "STOP", ".", 1 }, { "POP", "0", 1 }, { "POP_MARK", "1", 1 }, { "DUP", "2", 1 }, { "FLOAT", "F", 1 }, { "INT", "I", 1 },
            { "BININT", "J", 1 }, { "BININT1", "K", 1 }, { "LONG", "L", 1 }, { "BININT2", "M", 1 }, { "NONE", "N", 1 }, { "PERSID", "P", 1 },
            { "BINPERSID", "Q", 1 }, { "REDUCE", "R", 1 }, { "STRING", "S", 1 }, { "BINSTRING", "T", 1 }, { "SHORT_BINSTRING", "U", 1 }, { "UNICODE", "V", 1 },
            { "BINUNICODE", "X", 1 }, { "APPEND", "a", 1 }, { "BUILD", "b", 1 }, { "GLOBAL", "c", 1 }, { "DICT", "d", 1 }, { "EMPTY_DICT", "}", 1 },
            { "APPENDS", "e", 1 }, { "GET", "g", 1 }, { "BINGET", "h", 1 }, { "INST", "i", 1 }, { "LONG_BINGET", "j", 1 }, { "LIST", "l", 1 },
            { "EMPTY_LIST", "]", 1 }, { "OBJ", "o", 1 }, { "PUT", "p", 1 }, { "BINPUT", "q", 1 }, { "LONG_BINPUT", "r", 1 }, { "SETITEM", "s", 1 },
            { "TUPLE", "t", 1 }, { "EMPTY_TUPLE", ")", 1 }, { "SETITEMS", "u", 1 }, { "BINFLOAT", "G", 1 }, { "TRUE", "I01\n", 4 }, { "FALSE", "I00\n", 4 },
            { "PROTO", "\x80", 1 }, { "NEWOBJ", "\x81", 1 }, { "EXT1", "\x82", 1 }, { "EXT2", "\x83", 1 }, { "EXT4", "\x84", 1 }, { "TUPLE1", "\x85", 1 },
            { "TUPLE2", "\x86", 1 }, { "TUPLE3", "\x87", 1 }, { "NEWTRUE", "\x88", 1 }, { "NEWFALSE", "\x89", 1 }, { "LONG1", "\x8a", 1 }, { "LONG4", "\x8b", 1 },
            { "BINBYTES", "B", 1 }, { "SHORT_BINBYTES", "C", 1 }, { "SHORT_BINUNICODE", "\x8c", 1 }, { "BINUNICODE8", "\x8d", 1 }, { "BINBYTES8", "\x8e", 1 },
            { "EMPTY_SET", "\x8f", 1 }, { "ADDITEMS", "\x90", 1 }, { "FROZENSET", "\x91", 1 }, { "NEWOBJ_EX", "\x92", 1 }, { "STACK_GLOBAL", "\x93", 1 },
            { "MEMOIZE", "\x94", 1 }, { "FRAME", "\x95", 1 }, { "BYTEARRAY8", "\x96", 1 }, { "NEXT_BUFFER", "\x97", 1 }, { "READONLY_BUFFER", "\x98", 1 }
        };

        // (pickle._getattribute(obj, name): (the attribute of a dotted name, its parent))
        py getattribute(py const& aObject, py const& aName)
        {
            py object = aObject;
            py parent;
            for (auto const& subpath : aName.method("split", { py{ "." } }).items())
            {
                if (subpath == py{ "<locals>" })
                    raise_error("AttributeError", "Can't get local attribute " + aName.repr() + " on " + aObject.repr());
                parent = object;
                ref value{ getattr(object.get(), str_value(subpath.get()).c_str()) };
                if (!value)
                {
                    ref const e{ take_exception() };
                    if (!exception_is(e.get(), "AttributeError"))
                    {
                        reraise_exception(e.get());
                        throw raised{};
                    }
                    raise_error("AttributeError", "Can't get attribute " + aName.repr() + " on " + aObject.repr());
                }
                object = py{ std::move(value) };
            }
            return py_tuple({ object, parent });
        }

        py pickle_function(char const* aName, std::vector<py_parameter> aParameters, std::function<py(std::vector<py> const&)> aBody)
        {
            auto const doc = pickle_docs::doc(aName);
            std::string const name{ aName };
            return make_native_function(aName, aName, doc != nullptr ? doc : "", "pickle", [name, aParameters, aBody](std::vector<py> const& aArgs, object* aKwargs)
            {
                return aBody(py_bind(name, aParameters, aArgs, aKwargs));
            });
        }

        bool pickle_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                py const c{ fn_import("_pickle") };
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", py{ pickle_docs::doc("") });
                py const copyreg{ fn_import("copyreg") };
                add("FunctionType", import_attribute("types", "FunctionType"));
                for (auto name : { "dispatch_table", "_extension_registry", "_inverted_registry", "_extension_cache" })
                    add(name, copyreg.attr(name));
                add("islice", import_attribute("itertools", "islice"));
                add("partial", import_attribute("functools", "partial"));
                add("sys", py{ fn_import("sys") });
                add("maxsize", import_attribute("sys", "maxsize"));
                add("pack", import_attribute("struct", "pack"));
                add("unpack", import_attribute("struct", "unpack"));
                for (auto name : { "re", "io", "codecs", "_compat_pickle" })
                    add(name, py{ fn_import(name) });
                add("__all__", py_list({ py{ "PickleError" }, py{ "PicklingError" }, py{ "UnpicklingError" }, py{ "Pickler" }, py{ "Unpickler" }, py{ "dump" },
                    py{ "dumps" }, py{ "load" }, py{ "loads" }, py{ "PickleBuffer" } }));
                add("PickleBuffer", c.attr("PickleBuffer"));
                add("_HAVE_PICKLE_BUFFER", py::boolean(true));
                add("bytes_types", py_tuple({ builtin("bytes"), builtin("bytearray") }));
                add("format_version", py{ "4.0" });
                add("compatible_formats", py_list({ py{ "1.0" }, py{ "1.1" }, py{ "1.2" }, py{ "1.3" }, py{ "2.0" }, py{ "3.0" }, py{ "4.0" }, py{ "5.0" } }));
                add("HIGHEST_PROTOCOL", py{ kHighestProtocol });
                add("DEFAULT_PROTOCOL", py{ kDefaultProtocol });
                for (auto const& k : sOpcodeConstants)
                    add(k.name, py{ make_bytes(std::string{ k.value, k.size }) });
                add("_tuplesize2code", py_list({ py{ make_bytes(")") }, py{ make_bytes("\x85") }, py{ make_bytes("\x86") }, py{ make_bytes("\x87") } }));
                add("_NoValue", builtin("object")());
                add("_getattribute", pickle_function("_getattribute", { py_positional("obj"), py_positional("name") }, [](std::vector<py> const& a)
                    {
                        return getattribute(a[0], a[1]);
                    }));
                add("whichmodule", pickle_function("whichmodule", { py_positional("obj"), py_positional("name") }, [](std::vector<py> const& a)
                    {
                        // (its __module__, else the module (in sys.modules) of which it is the attribute, else __main__)
                        if (ref const moduleName{ optional_attr(a[0].get(), "__module__") }; moduleName && moduleName.get() != none())
                            return py::borrowed(moduleName.get());
                        py const modules = import_attribute("sys", "modules").method("copy");
                        for (auto const& item : modules.method("items").items())
                        {
                            auto const pair = item.items();
                            if (pair[0] == py{ "__main__" } || pair[0] == py{ "__mp_main__" } || pair[1].is_none())
                                continue;
                            try
                            {
                                if (getattribute(pair[1], a[1])[py{ 0 }].is(a[0]))
                                    return pair[0];
                            }
                            catch (raised const&)
                            {
                                if (!caught({ "AttributeError" }))
                                    throw;
                            }
                        }
                        return py{ "__main__" };
                    }));
                add("encode_long", pickle_function("encode_long", { py_positional("x") }, [](std::vector<py> const& a)
                    {
                        auto const& x = a[0];
                        if (x == py{ 0 })
                            return py{ make_bytes({}) };
                        auto const n = (x.method("bit_length").as_index() >> 3) + 1;
                        py result = x.method("to_bytes", { py{ n }, py{ "little" } }, { { "signed", py::boolean(true) } });
                        std::string_view data;
                        bytes_like(result.get(), data);
                        if (x < py{ 0 } && n > 1 && static_cast<unsigned char>(data[data.size() - 1u]) == 0xffu && (static_cast<unsigned char>(data[data.size() - 2u]) & 0x80u) != 0u)
                            result = py{ make_bytes(std::string{ data.substr(0u, data.size() - 1u) }) };
                        return result;
                    }));
                add("decode_long", pickle_function("decode_long", { py_positional("data") }, [](std::vector<py> const& a)
                    {
                        return builtin("int").method("from_bytes", { a[0] }, { { "byteorder", py{ "little" } }, { "signed", py::boolean(true) } });
                    }));
                for (auto name : { "PickleError", "PicklingError", "UnpicklingError", "Pickler", "Unpickler", "dump", "dumps", "load", "loads" })
                    add(name, c.attr(name));
                add("_Pickler", c.attr("Pickler"));
                add("_Unpickler", c.attr("Unpickler"));
                // (pickle.py's _dump, _dumps, _load and _loads: its own Pickler's and Unpickler's (here _pickle's))
                add("_dump", pickle_function("_dump", { py_positional("obj"), py_positional("file"), py_positional("protocol", py::none()),
                    py_keyword("fix_imports", py::boolean(true)), py_keyword("buffer_callback", py::none()) }, [c](std::vector<py> const& a)
                    {
                        return c.attr("Pickler").call({ a[1], a[2] }, { { "fix_imports", a[3] }, { "buffer_callback", a[4] } }).method("dump", { a[0] });
                    }));
                add("_dumps", pickle_function("_dumps", { py_positional("obj"), py_positional("protocol", py::none()), py_keyword("fix_imports", py::boolean(true)),
                    py_keyword("buffer_callback", py::none()) }, [c](std::vector<py> const& a)
                    {
                        py const f = import_attribute("io", "BytesIO")();
                        c.attr("Pickler").call({ f, a[1] }, { { "fix_imports", a[2] }, { "buffer_callback", a[3] } }).method("dump", { a[0] });
                        return f.method("getvalue");
                    }));
                add("_load", pickle_function("_load", { py_positional("file"), py_keyword("fix_imports", py::boolean(true)), py_keyword("encoding", py{ "ASCII" }),
                    py_keyword("errors", py{ "strict" }), py_keyword("buffers", py::none()) }, [c](std::vector<py> const& a)
                    {
                        return c.attr("Unpickler").call({ a[0] }, { { "fix_imports", a[1] }, { "buffers", a[4] }, { "encoding", a[2] }, { "errors", a[3] } }).method("load");
                    }));
                add("_loads", pickle_function("_loads", { py_positional_only("s"), py_keyword("fix_imports", py::boolean(true)), py_keyword("encoding", py{ "ASCII" }),
                    py_keyword("errors", py{ "strict" }), py_keyword("buffers", py::none()) }, [c](std::vector<py> const& a)
                    {
                        if (isinstance(a[0], builtin("str")))
                            raise_error("TypeError", "Can't load pickle from unicode string");
                        py const file = import_attribute("io", "BytesIO")(a[0]);
                        return c.attr("Unpickler").call({ file }, { { "fix_imports", a[1] }, { "buffers", a[4] }, { "encoding", a[2] }, { "errors", a[3] } }).method("load");
                    }));
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        // --- the module _compat_pickle (Lib/_compat_pickle.py's mappings)

        bool compat_pickle_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            using namespace compat_pickle;
            auto const pairs = [](auto const& aTable) -> object*
            {
                auto const result = make_dict();
                for (auto const& [k, v] : aTable)
                {
                    ref const key{ make_str(k) };
                    ref const value{ make_str(v) };
                    dict_set(result, key.get(), value.get());
                }
                return result;
            };
            auto const quads = [](auto const& aTable) -> object*
            {
                auto const result = make_dict();
                for (auto const& q : aTable)
                {
                    ref const key{ make_tuple({ make_str(q[0]), make_str(q[1]) }) };
                    ref const value{ make_tuple({ make_str(q[2]), make_str(q[3]) }) };
                    dict_set(result, key.get(), value.get());
                }
                return result;
            };
            auto const names = [](auto const& aTable) -> object*
            {
                std::vector<object*> items;
                for (auto n : aTable)
                    items.push_back(make_str(n));
                return make_tuple(std::move(items));
            };
            aAttributes.emplace_back("IMPORT_MAPPING", pairs(sImportMapping));
            aAttributes.emplace_back("NAME_MAPPING", quads(sNameMapping));
            aAttributes.emplace_back("PYTHON2_EXCEPTIONS", names(sPython2Exceptions));
            aAttributes.emplace_back("MULTIPROCESSING_EXCEPTIONS", names(sMultiprocessingExceptions));
            aAttributes.emplace_back("REVERSE_IMPORT_MAPPING", pairs(sReverseImportMapping));
            aAttributes.emplace_back("REVERSE_NAME_MAPPING", quads(sReverseNameMapping));
            aAttributes.emplace_back("PYTHON3_OSERROR_EXCEPTIONS", names(sPython3OSErrorExceptions));
            aAttributes.emplace_back("PYTHON3_IMPORTERROR_EXCEPTIONS", names(sPython3ImportErrorExceptions));
            return true;
        }

        inline bool const sPickleC = register_module("_pickle", pickle_c_module);
        inline bool const sPickle = register_module("pickle", pickle_module);
        inline bool const sCompatPickle = register_module("_compat_pickle", compat_pickle_module);
    }
}
