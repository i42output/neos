/*
  uuid.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/uuid.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module uuid (CPython 3.13's behaviour and messages): UUID objects (128-bit values), uuid1, uuid3, uuid4, uuid5, getnode

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            using uuid_bytes = std::array<unsigned char, 16>;

            struct uuid_object
            {
                header h;
                uuid_bytes value{};         ///< (big endian)
                object* isSafe = nullptr;   ///< (a SafeUUID)
                ~uuid_object() { release(isSafe); }
            };

            object* sSafeUnknown = nullptr;
            object* sSafeEnum = nullptr;

            std::string hex_of(uuid_bytes const& aBytes)
            {
                static char const sDigits[] = "0123456789abcdef";
                std::string result;
                for (auto b : aBytes)
                {
                    result += sDigits[b >> 4];
                    result += sDigits[b & 0xF];
                }
                return result;
            }

            std::string text_of_uuid(uuid_bytes const& aBytes)
            {
                auto const h = hex_of(aBytes);
                return h.substr(0u, 8u) + "-" + h.substr(8u, 4u) + "-" + h.substr(12u, 4u) + "-" + h.substr(16u, 4u) + "-" + h.substr(20u);
            }

            // (the bits of the value from a bit (0: the least significant) for a count of bits: an int)
            unsigned long long bits_of(uuid_bytes const& aBytes, unsigned aFrom, unsigned aCount)
            {
                unsigned long long result = 0u;
                for (unsigned i = 0u; i < aCount; ++i)
                {
                    auto const bit = aFrom + i;
                    auto const byte = aBytes[15u - bit / 8u];
                    if ((byte >> (bit % 8u)) & 1u)
                        result |= 1ull << i;
                }
                return result;
            }

            void set_bits(uuid_bytes& aBytes, unsigned aFrom, unsigned aCount, unsigned long long aValue)
            {
                for (unsigned i = 0u; i < aCount; ++i)
                {
                    auto const bit = aFrom + i;
                    auto& byte = aBytes[15u - bit / 8u];
                    if ((aValue >> i) & 1u)
                        byte = static_cast<unsigned char>(byte | (1u << (bit % 8u)));
                    else
                        byte = static_cast<unsigned char>(byte & ~(1u << (bit % 8u)));
                }
            }

            object* int_of(uuid_bytes const& aBytes)
            {
                ref const intType{ fn_builtin("int") };
                return intType ? invoke(intType.get(), { make_str(hex_of(aBytes)), make_int(16) }) : nullptr;
            }

            // (an int (0 <= value < 2**128): its bytes)
            bool bytes_of_int(object* aValue, uuid_bytes& aResult)
            {
                ref const zero{ make_int(0) };
                ref const limit{ binary(NEOS_PY_LSHIFT, ref{ make_int(1) }.get(), ref{ make_int(128) }.get(), false) };
                ref const low{ compare(NEOS_PY_LE, zero.get(), aValue) };
                ref const high{ compare(NEOS_PY_LT, aValue, limit.get()) };
                if (!low || !high)
                    return false;
                if (truth(low.get()) != 1 || truth(high.get()) != 1)
                    return (error("ValueError", "int is out of range (need a 128-bit value)"), false);
                ref const data{ call_method_named(aValue, "to_bytes", { make_int(16), make_str("big") }) };
                std::string_view bytes;
                if (!data || !bytes_like(data.get(), bytes) || bytes.size() != 16u)
                    return false;
                std::memcpy(aResult.data(), bytes.data(), 16u);
                return true;
            }

            neos_py_type make_uuid_type();
            neos_py_type sUuidType = make_uuid_type();

            uuid_object& as_uuid(object* aObject) { return *reinterpret_cast<uuid_object*>(aObject); }
            bool is_uuid(object* aObject) { return aObject != nullptr && aObject->type == &sUuidType.base; }

            object* make_uuid(uuid_bytes const& aBytes, object* aIsSafe = nullptr)
            {
                auto* const u = new uuid_object{ { { &sUuidType.base } }, aBytes };
                u->isSafe = new_reference(aIsSafe != nullptr ? aIsSafe : (sSafeUnknown != nullptr ? sSafeUnknown : none()));
                return &u->h.base;
            }

            // (UUID(hex=None, bytes=None, bytes_le=None, fields=None, int=None, version=None, *, is_safe=SafeUUID.unknown))
            object* uuid_new(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("UUID", as<tuple_object>(aArgs)->items, aKwargs, { "hex", "bytes", "bytes_le", "fields", "int", "version", "is_safe" }, 0u, 0u, a))
                    return nullptr;
                if (as<tuple_object>(aArgs)->items.size() > 6u)
                    return error("TypeError", "UUID.__init__() takes from 1 to 7 positional arguments but " + std::to_string(as<tuple_object>(aArgs)->items.size() + 1u) + " were given");
                int given = 0;
                for (std::size_t i = 0u; i < 5u; ++i)
                    if (a[i] != nullptr && kind_of(a[i]) != NEOS_PY_NONE)
                        ++given;
                if (given != 1)
                    return error("TypeError", "one of the hex, bytes, bytes_le, fields, or int arguments must be given");
                uuid_bytes value{};
                auto const given_ = [&](std::size_t i) { return a[i] != nullptr && kind_of(a[i]) != NEOS_PY_NONE; };
                if (given_(0u))
                {
                    auto const s = str_of_value(a[0]);
                    if (s == nullptr)
                        return error("AttributeError", std::string{ "'" } + type_name(a[0]) + "' object has no attribute 'replace'");
                    std::string hex = *s;
                    for (auto const* prefix : { "urn:", "uuid:" })
                        for (std::size_t at; (at = hex.find(prefix)) != std::string::npos;)
                            hex.erase(at, std::strlen(prefix));
                    while (!hex.empty() && (hex.front() == '{' || hex.front() == '}'))
                        hex.erase(0u, 1u);
                    while (!hex.empty() && (hex.back() == '{' || hex.back() == '}'))
                        hex.pop_back();
                    hex.erase(std::remove(hex.begin(), hex.end(), '-'), hex.end());
                    if (hex.size() != 32u)
                        return error("ValueError", "badly formed hexadecimal UUID string");
                    for (std::size_t i = 0u; i < 16u; ++i)
                    {
                        auto const digit = [](char c) -> int
                        {
                            if (c >= '0' && c <= '9') return c - '0';
                            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                            return -1;
                        };
                        auto const h = digit(hex[2u * i]);
                        auto const l = digit(hex[2u * i + 1u]);
                        if (h < 0 || l < 0)
                            return error("ValueError", "invalid literal for int() with base 16: " + repr_text(ref{ make_str(hex) }.get()));
                        value[i] = static_cast<unsigned char>((h << 4) | l);
                    }
                }
                if (given_(2u))
                {
                    std::string_view data;
                    if (!bytes_like(a[2], data))
                        return error("TypeError", std::string{ "object of type '" } + type_name(a[2]) + "' has no len()");
                    if (data.size() != 16u)
                        return error("ValueError", "bytes_le is not a 16-char string");
                    // (the first three fields are little endian)
                    unsigned char const order[16] = { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
                    for (std::size_t i = 0u; i < 16u; ++i)
                        value[i] = static_cast<unsigned char>(data[order[i]]);
                }
                if (given_(1u))
                {
                    std::string_view data;
                    if (!bytes_like(a[1], data))
                        return error("TypeError", std::string{ "object of type '" } + type_name(a[1]) + "' has no len()");
                    if (data.size() != 16u)
                        return error("ValueError", "bytes is not a 16-char string");
                    std::memcpy(value.data(), data.data(), 16u);
                }
                if (given_(3u))
                {
                    std::vector<object*> fields;
                    if (!collect(a[3], fields))
                        return nullptr;
                    std::vector<ref> owned;
                    for (auto f : fields)
                        owned.emplace_back(f);
                    if (fields.size() != 6u)
                        return error("ValueError", "fields is not a 6-tuple");
                    struct { unsigned bits; char const* message; } const ranges[] = { { 32u, "field 1 out of range (need a 32-bit value)" },
                        { 16u, "field 2 out of range (need a 16-bit value)" }, { 16u, "field 3 out of range (need a 16-bit value)" },
                        { 8u, "field 4 out of range (need an 8-bit value)" }, { 8u, "field 5 out of range (need an 8-bit value)" },
                        { 48u, "field 6 out of range (need a 48-bit value)" } };
                    unsigned long long values[6] = {};
                    for (std::size_t i = 0u; i < 6u; ++i)
                    {
                        long long v = 0;
                        if (!to_index(fields[i], v))
                        {
                            clear_error();
                            return error("ValueError", ranges[i].message);
                        }
                        if (v < 0 || (ranges[i].bits < 64u && static_cast<unsigned long long>(v) >= (1ull << ranges[i].bits)))
                            return error("ValueError", ranges[i].message);
                        values[i] = static_cast<unsigned long long>(v);
                    }
                    set_bits(value, 96u, 32u, values[0]);
                    set_bits(value, 80u, 16u, values[1]);
                    set_bits(value, 64u, 16u, values[2]);
                    set_bits(value, 56u, 8u, values[3]);
                    set_bits(value, 48u, 8u, values[4]);
                    set_bits(value, 0u, 48u, values[5]);
                }
                if (given_(4u) && !bytes_of_int(a[4], value))
                    return nullptr;
                if (a[5] != nullptr && kind_of(a[5]) != NEOS_PY_NONE)
                {
                    long long version = 0;
                    if (!to_index(a[5], version))
                        return nullptr;
                    if (version < 1 || version > 5)
                        return error("ValueError", "illegal version number");
                    // (the variant: RFC 4122's; the version)
                    set_bits(value, 62u, 2u, 2u);
                    set_bits(value, 76u, 4u, static_cast<unsigned long long>(version));
                }
                return make_uuid(value, a[6]);
            }

            object* field(uuid_object const& u, char const* aName)
            {
                std::string_view const name{ aName };
                if (name == "time_low") return make_int(static_cast<long long>(bits_of(u.value, 96u, 32u)));
                if (name == "time_mid") return make_int(static_cast<long long>(bits_of(u.value, 80u, 16u)));
                if (name == "time_hi_version") return make_int(static_cast<long long>(bits_of(u.value, 64u, 16u)));
                if (name == "clock_seq_hi_variant") return make_int(static_cast<long long>(bits_of(u.value, 56u, 8u)));
                if (name == "clock_seq_low") return make_int(static_cast<long long>(bits_of(u.value, 48u, 8u)));
                if (name == "node") return make_int(static_cast<long long>(bits_of(u.value, 0u, 48u)));
                if (name == "time") return make_int(static_cast<long long>((bits_of(u.value, 64u, 12u) << 48) | (bits_of(u.value, 80u, 16u) << 32) | bits_of(u.value, 96u, 32u)));
                if (name == "clock_seq") return make_int(static_cast<long long>(((bits_of(u.value, 56u, 8u) & 0x3fu) << 8) | bits_of(u.value, 48u, 8u)));
                return nullptr;
            }

            char const* variant_of(uuid_bytes const& aBytes)
            {
                if (!bits_of(aBytes, 63u, 1u))
                    return "reserved for NCS compatibility";
                if (!bits_of(aBytes, 62u, 1u))
                    return "specified in RFC 4122";
                if (!bits_of(aBytes, 61u, 1u))
                    return "reserved for Microsoft compatibility";
                return "reserved for future definition";
            }

            neos_py_type make_uuid_type()
            {
                auto type = make_type("uuid.UUID", "UUID", retain_object, release_object<uuid_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    return make_str("UUID('" + text_of_uuid(as_uuid(aSelf).value) + "')");
                };
                type.str = [](neos_py_context*, object* aSelf) -> object* { return make_str(text_of_uuid(as_uuid(aSelf).value)); };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& u = as_uuid(aSelf);
                    std::string_view const name{ aName };
                    if (name == "int") return int_of(u.value);
                    if (name == "is_safe") return new_reference(u.isSafe);
                    if (name == "hex") return make_str(hex_of(u.value));
                    if (name == "urn") return make_str("urn:uuid:" + text_of_uuid(u.value));
                    if (name == "bytes") return make_bytes(std::string{ u.value.begin(), u.value.end() });
                    if (name == "bytes_le")
                    {
                        unsigned char const order[16] = { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
                        std::string result;
                        for (auto i : order)
                            result += static_cast<char>(u.value[i]);
                        return make_bytes(result);
                    }
                    if (name == "fields")
                        return make_tuple({ field(u, "time_low"), field(u, "time_mid"), field(u, "time_hi_version"), field(u, "clock_seq_hi_variant"),
                            field(u, "clock_seq_low"), field(u, "node") });
                    if (auto const f = field(u, aName))
                        return f;
                    if (name == "variant") return make_str(variant_of(u.value));
                    if (name == "version")
                    {
                        if (std::strcmp(variant_of(u.value), "specified in RFC 4122") == 0)
                            return make_int(static_cast<long long>(bits_of(u.value, 76u, 4u)));
                        return new_reference(none());
                    }
                    if (name == "__int__" || name == "__copy__" || name == "__deepcopy__" || name == "__getstate__" || name == "__reduce__")
                        return make_function(aName, [](neos_py_context*, void* aData, object* aArgs, object*) -> object*
                        {
                            auto const& args = as<tuple_object>(aArgs)->items;
                            auto const self = args[0];
                            std::string_view const method{ static_cast<char const*>(aData) };
                            if (method == "__int__")
                                return int_of(as_uuid(self).value);
                            if (method == "__getstate__")
                            {
                                ref const d{ make_dict() };
                                ref const value{ int_of(as_uuid(self).value) };
                                dict_set(d.get(), ref{ make_str("int") }.get(), value.get());
                                if (as_uuid(self).isSafe != sSafeUnknown)
                                    if (ref const v{ getattr(as_uuid(self).isSafe, "value") })
                                        dict_set(d.get(), ref{ make_str("is_safe") }.get(), v.get());
                                clear_error();
                                return new_reference(d.get());
                            }
                            if (method == "__reduce__")
                            {
                                ref const cls{ module_attribute("uuid", "UUID") };
                                ref const value{ int_of(as_uuid(self).value) };
                                ref const kw{ keywords({ { "int", value.get() } }) };
                                // (UUID(int=value))
                                ref const functools{ module_attribute("functools", "partial") };
                                ref const maker{ functools ? call(functools.get(), ref{ make_tuple({ new_reference(cls.get()) }) }.get(), kw.get()) : nullptr };
                                return maker ? make_tuple({ new_reference(maker.get()), make_tuple() }) : nullptr;
                            }
                            return new_reference(self);
                        }, const_cast<char*>(std::string_view{ aName } == "__int__" ? "__int__" : std::string_view{ aName } == "__copy__" ? "__copy__" :
                            std::string_view{ aName } == "__deepcopy__" ? "__deepcopy__" : std::string_view{ aName } == "__getstate__" ? "__getstate__" : "__reduce__"), aSelf);
                    return error("AttributeError", std::string{ "'UUID' object has no attribute '" } + aName + "'");
                };
                type.setattr = [](neos_py_context*, object*, char const*, object*) -> int
                {
                    error("TypeError", "UUID objects are immutable");
                    return -1;
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (!is_uuid(aLhs) || !is_uuid(aRhs))
                        return not_implemented();
                    auto const c = std::memcmp(as_uuid(aLhs).value.data(), as_uuid(aRhs).value.data(), 16u);
                    switch (aOperation)
                    {
                    case NEOS_PY_LT: return make_bool(c < 0);
                    case NEOS_PY_LE: return make_bool(c <= 0);
                    case NEOS_PY_EQ: return make_bool(c == 0);
                    case NEOS_PY_NE: return make_bool(c != 0);
                    case NEOS_PY_GT: return make_bool(c > 0);
                    default: return make_bool(c >= 0);
                    }
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    ref const value{ int_of(as_uuid(aSelf).value) };
                    return value && hash_of(value.get(), *aResult) ? 0 : -1;
                };
                return type;
            }

            int uuid_instancecheck(neos_py_context*, void*, object* aObject) { return is_uuid(aObject) ? 1 : 0; }

            std::mt19937_64& engine()
            {
                static std::mt19937_64 sEngine{ std::random_device{}() };
                return sEngine;
            }

            bool random_bytes(unsigned char* aData, std::size_t aCount)
            {
                ref const urandom{ module_attribute("os", "urandom") };
                ref const data{ urandom ? invoke(urandom.get(), { make_int(static_cast<long long>(aCount)) }) : nullptr };
                std::string_view bytes;
                if (!data || !bytes_like(data.get(), bytes) || bytes.size() != aCount)
                    return false;
                std::memcpy(aData, bytes.data(), aCount);
                return true;
            }

            // (getnode(): a random 48-bit number with its multicast bit set (as CPython's _random_getnode))
            unsigned long long node()
            {
                static unsigned long long sNode = []()
                {
                    std::uniform_int_distribution<unsigned long long> d{ 0u, (1ull << 48) - 1u };
                    return d(engine()) | (1ull << 40);
                }();
                return sNode;
            }

            object* name_based(object* aNamespace, object* aName, char const* aHash, unsigned aVersion)
            {
                if (!is_uuid(aNamespace))
                    return error("AttributeError", std::string{ "'" } + type_name(aNamespace) + "' object has no attribute 'bytes'");
                std::string data{ as_uuid(aNamespace).value.begin(), as_uuid(aNamespace).value.end() };
                if (auto const s = str_of_value(aName))
                    data += *s;
                else
                {
                    std::string_view b;
                    if (!bytes_like(aName, b))
                        return error("TypeError", std::string{ "can't concat " } + type_name(aName) + " to bytes");
                    data += b;
                }
                ref const hash{ module_attribute("hashlib", aHash) };
                ref const digest{ hash ? invoke(hash.get(), { make_bytes(data) }, ref{ keywords({ { "usedforsecurity", ref{ make_bool(false) }.get() } }) }.get()) : nullptr };
                ref const bytes{ digest ? call_method_named(digest.get(), "digest", {}) : nullptr };
                std::string_view d;
                if (!bytes || !bytes_like(bytes.get(), d) || d.size() < 16u)
                    return nullptr;
                uuid_bytes value{};
                std::memcpy(value.data(), d.data(), 16u);
                set_bits(value, 62u, 2u, 2u);
                set_bits(value, 76u, 4u, aVersion);
                return make_uuid(value);
            }

            long long sLastTimestamp = -1;

            builtin_entry const sUuidFunctions[] =
            {
                { "getnode", [](std::vector<object*> const&, object*) -> object* { return make_int(static_cast<long long>(node())); } },
                { "uuid1", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("uuid1", aArgs, aKwargs, { "node", "clock_seq" }, 0u, 0u, a))
                            return nullptr;
                        auto const nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        long long timestamp = nanoseconds / 100 + 0x01b21dd213814000ll;
                        if (sLastTimestamp >= 0 && timestamp <= sLastTimestamp)
                            timestamp = sLastTimestamp + 1;
                        sLastTimestamp = timestamp;
                        long long clockSeq = 0;
                        if (a[1] != nullptr && kind_of(a[1]) != NEOS_PY_NONE)
                        {
                            if (!to_index(a[1], clockSeq))
                                return nullptr;
                        }
                        else
                            clockSeq = static_cast<long long>(engine()() & 0x3fffu);
                        long long theNode = static_cast<long long>(node());
                        if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE && !to_index(a[0], theNode))
                            return nullptr;
                        uuid_bytes value{};
                        set_bits(value, 96u, 32u, static_cast<unsigned long long>(timestamp) & 0xffffffffu);
                        set_bits(value, 80u, 16u, (static_cast<unsigned long long>(timestamp) >> 32) & 0xffffu);
                        set_bits(value, 64u, 12u, (static_cast<unsigned long long>(timestamp) >> 48) & 0x0fffu);
                        set_bits(value, 48u, 8u, static_cast<unsigned long long>(clockSeq) & 0xffu);
                        set_bits(value, 56u, 6u, (static_cast<unsigned long long>(clockSeq) >> 8) & 0x3fu);
                        set_bits(value, 0u, 48u, static_cast<unsigned long long>(theNode) & ((1ull << 48) - 1u));
                        set_bits(value, 62u, 2u, 2u);
                        set_bits(value, 76u, 4u, 1u);
                        return make_uuid(value);
                    } },
                { "uuid3", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("uuid3", aArgs, aKwargs, { "namespace", "name" }, 2u, 0u, a))
                            return nullptr;
                        return name_based(a[0], a[1], "md5", 3u);
                    } },
                { "uuid4", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (!aArgs.empty())
                            return error("TypeError", "uuid4() takes 0 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                        uuid_bytes value{};
                        if (!random_bytes(value.data(), 16u))
                            return nullptr;
                        set_bits(value, 62u, 2u, 2u);
                        set_bits(value, 76u, 4u, 4u);
                        return make_uuid(value);
                    } },
                { "uuid5", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("uuid5", aArgs, aKwargs, { "namespace", "name" }, 2u, 0u, a))
                            return nullptr;
                        return name_based(a[0], a[1], "sha1", 5u);
                    } }
            };

            object* constant_uuid(char const* aHex)
            {
                ref const hex{ make_str(aHex) };
                ref const args{ make_tuple({ new_reference(hex.get()) }) };
                return uuid_new(nullptr, nullptr, args.get(), nullptr);
            }

            bool uuid_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                if (sSafeEnum == nullptr)
                {
                    ref const enumClass{ module_attribute("enum", "Enum") };
                    if (!enumClass)
                        return false;
                    ref const members{ make_list({ make_tuple({ make_str("safe"), make_int(0) }), make_tuple({ make_str("unsafe"), make_int(-1) }),
                        make_tuple({ make_str("unknown"), new_reference(none()) }) }) };
                    ref const kw{ keywords({ { "module", ref{ make_str("uuid") }.get() } }) };
                    sSafeEnum = call(enumClass.get(), ref{ make_tuple({ make_str("SafeUUID"), new_reference(members.get()) }) }.get(), kw.get());
                    if (sSafeEnum == nullptr)
                        return false;
                    sSafeUnknown = getattr(sSafeEnum, "unknown");
                }
                aAttributes.emplace_back("SafeUUID", new_reference(sSafeEnum));
                aAttributes.emplace_back("UUID", context()->type_new(context(), "UUID", "uuid.UUID", uuid_new, nullptr, uuid_instancecheck));
                for (auto const& f : sUuidFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("NAMESPACE_DNS", constant_uuid("6ba7b810-9dad-11d1-80b4-00c04fd430c8"));
                aAttributes.emplace_back("NAMESPACE_URL", constant_uuid("6ba7b811-9dad-11d1-80b4-00c04fd430c8"));
                aAttributes.emplace_back("NAMESPACE_OID", constant_uuid("6ba7b812-9dad-11d1-80b4-00c04fd430c8"));
                aAttributes.emplace_back("NAMESPACE_X500", constant_uuid("6ba7b814-9dad-11d1-80b4-00c04fd430c8"));
                aAttributes.emplace_back("RESERVED_NCS", make_str("reserved for NCS compatibility"));
                aAttributes.emplace_back("RFC_4122", make_str("specified in RFC 4122"));
                aAttributes.emplace_back("RESERVED_MICROSOFT", make_str("reserved for Microsoft compatibility"));
                aAttributes.emplace_back("RESERVED_FUTURE", make_str("reserved for future definition"));
                return !failed();
            }

            inline bool const sUuid = register_module("uuid", uuid_module);
        }
    }
}
