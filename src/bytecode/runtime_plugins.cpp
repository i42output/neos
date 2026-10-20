/*
  runtime_plugins.cpp

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

#include <neos/neos.hpp>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <memory>
#include <map>
#include <stdexcept>
#include <filesystem>
#include <optional>
#define NEOS_RUNTIME_PLUGIN_HOST
#include <neos/runtime/plugin.h>
#include <neos/bytecode/vm/runtime_plugins.hpp>

#if __has_include(<ffi.h>) && !defined(NEOS_NO_LIBFFI)
#if defined(NEOS_LIBFFI_STATIC) // (linking libffi statically: its declarations must not be dllimport on Windows)
#ifndef FFI_BUILDING
#define FFI_BUILDING
#endif
#ifndef FFI_STATIC_BUILD
#define FFI_STATIC_BUILD
#endif
#endif
#include <ffi.h>
#define NEOS_HAVE_LIBFFI 1
#if defined(_MSC_VER)
#ifndef NEOS_LIBFFI_LIB
#define NEOS_LIBFFI_LIB "libffi.lib"
#endif
#pragma comment(lib, NEOS_LIBFFI_LIB)
#endif
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            namespace runtime
            {
                struct function
                {
                    std::string name;
                    neos_rt_function const* description = nullptr;
                    neos_rt_plugin_error error = nullptr;   ///< (the plugin's, if it can raise errors)
                    long const* errorPending = nullptr;     ///< (the plugin's, if it says when error can return one)
#if defined(NEOS_HAVE_LIBFFI)
                    std::vector<ffi_type*> parameterTypes;
                    ffi_cif cif = {};
#endif
                };

                namespace
                {
                    constexpr std::uint8_t I32 = 0x7Fu;
                    constexpr std::uint8_t I64 = 0x7Eu;
                    constexpr std::uint8_t F32 = 0x7Du;
                    constexpr std::uint8_t F64 = 0x7Cu;

                    // the WebAssembly value type of a plugin type (0 for void)
                    std::uint8_t value_type(neos_rt_type aType)
                    {
                        switch (aType)
                        {
                        case NEOS_RT_VOID: return 0u;
                        case NEOS_RT_BOOL:
                        case NEOS_RT_CHAR:
                        case NEOS_RT_I32:
                        case NEOS_RT_U32:
                        case NEOS_RT_STRING:
                        case NEOS_RT_OBJECT: return I32;
                        case NEOS_RT_I64:
                        case NEOS_RT_U64:
                        case NEOS_RT_INTEGER: return I64;
                        case NEOS_RT_SIZE: return sizeof(std::size_t) == 8u ? I64 : I32;
                        case NEOS_RT_F32: return F32;
                        case NEOS_RT_F64: return F64;
                        }
                        throw std::runtime_error("invalid runtime plugin type");
                    }

                    std::string type_name(std::uint8_t aValueType)
                    {
                        switch (aValueType)
                        {
                        case I32: return "i32";
                        case I64: return "i64";
                        case F32: return "f32";
                        case F64: return "f64";
                        default: return "void";
                        }
                    }

#if defined(NEOS_HAVE_LIBFFI)
                    ffi_type* ffi_type_of(neos_rt_type aType)
                    {
                        switch (aType)
                        {
                        case NEOS_RT_VOID: return &ffi_type_void;
                        case NEOS_RT_BOOL: return sizeof(bool) == 1u ? &ffi_type_uint8 : &ffi_type_uint32;
                        case NEOS_RT_CHAR: return &ffi_type_schar;
                        case NEOS_RT_I32: return &ffi_type_sint32;
                        case NEOS_RT_U32: return &ffi_type_uint32;
                        case NEOS_RT_I64:
                        case NEOS_RT_INTEGER: return &ffi_type_sint64;
                        case NEOS_RT_U64: return &ffi_type_uint64;
                        case NEOS_RT_SIZE: return sizeof(std::size_t) == 8u ? &ffi_type_uint64 : &ffi_type_uint32;
                        case NEOS_RT_F32: return &ffi_type_float;
                        case NEOS_RT_F64: return &ffi_type_double;
                        case NEOS_RT_STRING:
                        case NEOS_RT_OBJECT: return &ffi_type_pointer;
                        }
                        throw std::runtime_error("invalid runtime plugin type");
                    }
#endif

                    std::optional<std::string> environment_variable(char const* aName)
                    {
#if defined(_MSC_VER)
                        char* buffer = nullptr;
                        std::size_t size = 0u;
                        if (::_dupenv_s(&buffer, &size, aName) != 0 || buffer == nullptr)
                            return {};
                        std::string const result{ buffer };
                        std::free(buffer);
                        return result;
#else
                        if (auto const value = std::getenv(aName))
                            return std::string{ value };
                        return {};
#endif
                    }

                    std::optional<std::filesystem::path> executable_directory()
                    {
                        try
                        {
#if defined(_WIN32)
                            std::wstring path(32768u, L'\0');
                            auto const length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
                            if (length == 0u || length >= path.size())
                                return {};
                            path.resize(length);
                            return std::filesystem::path{ path }.parent_path();
#elif defined(__linux__)
                            return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#else
                            return {};
#endif
                        }
                        catch (...)
                        {
                            return {};
                        }
                    }

                    std::vector<std::filesystem::path> search_path()
                    {
                        std::vector<std::filesystem::path> result;
                        if (auto const list = environment_variable("NEOS_RUNTIME_PLUGIN_PATH"))
                        {
#if defined(_WIN32)
                            char const separator = ';';
#else
                            char const separator = ':';
#endif
                            std::size_t start = 0u;
                            for (;;)
                            {
                                auto const end = list->find(separator, start);
                                auto const entry = list->substr(start, end == std::string::npos ? std::string::npos : end - start);
                                if (!entry.empty())
                                    result.push_back(std::filesystem::path{ entry });
                                if (end == std::string::npos)
                                    break;
                                start = end + 1u;
                            }
                        }
                        if (auto const directory = executable_directory())
                        {
                            result.push_back(*directory);
                            result.push_back(*directory / "plugins");
                        }
                        return result;
                    }

                    class library
                    {
                    public:
                        explicit library(std::filesystem::path const& aPath)
                        {
#if defined(_WIN32)
                            iHandle = ::LoadLibraryExW(aPath.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
                            iHandle = ::dlopen(aPath.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
                        }
                        ~library()
                        {
                            // (plugins stay loaded: functions may still be in use by other threads)
                        }
                    public:
                        bool loaded() const
                        {
                            return iHandle != nullptr;
                        }
                        void* symbol(char const* aName) const
                        {
#if defined(_WIN32)
                            return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(iHandle), aName));
#else
                            return ::dlsym(iHandle, aName);
#endif
                        }
                    private:
#if defined(_WIN32)
                        HMODULE iHandle = nullptr;
#else
                        void* iHandle = nullptr;
#endif
                    };

                    struct plugin
                    {
                        std::unique_ptr<library> lib;
                        neos_rt_plugin const* description = nullptr;
                        std::map<std::string, function> functions;
                        neos_rt_plugin_end end = nullptr;
                    };

                    class registry
                    {
                    public:
                        static registry& instance()
                        {
                            static registry sRegistry;
                            return sRegistry;
                        }
                    public:
                        // (a program has ended: the plugins that it used finish their work)
                        void program_end()
                        {
                            std::vector<neos_rt_plugin_end> ends;
                            {
                                std::lock_guard<std::recursive_mutex> lock{ iMutex };
                                for (auto const& p : iPlugins)
                                    if (p.second.end != nullptr)
                                        ends.push_back(p.second.end);
                            }
                            for (auto end : ends)
                                end();
                        }
                        function const& resolve(std::string const& aImport)
                        {
                            std::lock_guard<std::recursive_mutex> lock{ iMutex };
                            auto const separator = aImport.rfind("::");
                            if (separator == std::string::npos)
                                throw std::runtime_error("unresolved import '" + aImport + "'");
                            std::string ns = aImport.substr(0u, separator);
                            std::string const name = aImport.substr(separator + 2u);
                            auto& p = load(ns);
                            auto const f = p.functions.find(name);
                            if (f == p.functions.end())
                                throw std::runtime_error("unresolved import '" + aImport + "': runtime plugin '" + file_stem(ns) + "' has no function '" + name + "'");
                            return f->second;
                        }
                    private:
                        static std::string file_stem(std::string const& aNamespace)
                        {
                            std::string stem = "neos.runtime.";
                            for (std::size_t i = 0u; i < aNamespace.size(); ++i)
                            {
                                if (aNamespace.compare(i, 2u, "::") == 0)
                                {
                                    stem += '.';
                                    ++i;
                                }
                                else
                                    stem += aNamespace[i];
                            }
                            return stem;
                        }
                    public:
                        bool exists(std::string const& aNamespace)
                        {
                            {
                                std::scoped_lock lock{ iMutex };
                                if (iPlugins.find(aNamespace) != iPlugins.end())
                                    return true;
                            }
                            auto const stem = file_stem(aNamespace);
#if defined(_WIN32)
                            auto const fileName = stem + ".dll";
#elif defined(__APPLE__)
                            auto const fileName = stem + ".dylib";
#else
                            auto const fileName = stem + ".so";
#endif
                            for (auto const& directory : search_path())
                            {
                                std::error_code ec;
                                if (std::filesystem::is_regular_file(directory / fileName, ec))
                                    return true;
                            }
                            return false;
                        }
                    private:
                        plugin& load(std::string const& aNamespace)
                        {
                            auto existing = iPlugins.find(aNamespace);
                            if (existing != iPlugins.end())
                                return existing->second;
#if !defined(NEOS_HAVE_LIBFFI)
                            throw std::runtime_error("cannot load runtime plugin '" + file_stem(aNamespace) + "': neos was built without libffi");
#else
                            auto const stem = file_stem(aNamespace);
#if defined(_WIN32)
                            auto const fileName = stem + ".dll";
#elif defined(__APPLE__)
                            auto const fileName = stem + ".dylib";
#else
                            auto const fileName = stem + ".so";
#endif
                            for (auto const& directory : search_path())
                            {
                                auto const path = directory / fileName;
                                std::error_code ec;
                                if (!std::filesystem::is_regular_file(path, ec))
                                    continue;
                                auto lib = std::make_unique<library>(path);
                                if (!lib->loaded())
                                    throw std::runtime_error("cannot load runtime plugin '" + path.string() + "'");
                                auto const entry = reinterpret_cast<neos_rt_plugin_entry>(lib->symbol(NEOS_RUNTIME_PLUGIN_ENTRY_NAME));
                                if (entry == nullptr)
                                    throw std::runtime_error("'" + path.string() + "' is not a runtime plugin (no " NEOS_RUNTIME_PLUGIN_ENTRY_NAME " export)");
                                auto const description = entry();
                                if (description == nullptr || description->version != NEOS_RUNTIME_PLUGIN_VERSION)
                                    throw std::runtime_error("runtime plugin '" + path.string() + "' has an unsupported version (" + 
                                        (description != nullptr ? std::to_string(description->version) : std::string{ "?" }) + "; neos supports " + 
                                        std::to_string(NEOS_RUNTIME_PLUGIN_VERSION) + ")");
                                if (description->name == nullptr || aNamespace != description->name)
                                    throw std::runtime_error("runtime plugin '" + path.string() + "' is not for namespace '" + aNamespace + "'");
                                plugin p;
                                p.description = description;
                                if (auto const setHost = reinterpret_cast<neos_rt_plugin_set_host>(lib->symbol(NEOS_RUNTIME_PLUGIN_SET_HOST_NAME)))
                                    setHost(static_cast<neos_rt_host const*>(host()));
                                auto const error = reinterpret_cast<neos_rt_plugin_error>(lib->symbol(NEOS_RUNTIME_PLUGIN_ERROR_NAME));
                                auto const errorPending = reinterpret_cast<neos_rt_plugin_error_pending>(lib->symbol(NEOS_RUNTIME_PLUGIN_ERROR_PENDING_NAME));
                                long const* const pending = error != nullptr && errorPending != nullptr ? errorPending() : nullptr;
                                p.end = reinterpret_cast<neos_rt_plugin_end>(lib->symbol(NEOS_RUNTIME_PLUGIN_END_NAME));
                                for (unsigned i = 0u; i < description->functionCount; ++i)
                                {
                                    auto const& d = description->functions[i];
                                    if (d.name == nullptr || d.function == nullptr || d.parameterCount > NEOS_RT_MAX_PARAMETERS)
                                        throw std::runtime_error("runtime plugin '" + path.string() + "' has an invalid function description");
                                    function f;
                                    f.name = aNamespace + "::" + d.name;
                                    f.description = &d;
                                    f.error = error;
                                    f.errorPending = pending;
                                    for (unsigned a = 0u; a < d.parameterCount; ++a)
                                    {
                                        if (d.parameters[a] == NEOS_RT_VOID)
                                            throw std::runtime_error("runtime plugin function '" + f.name + "' has a void parameter");
                                        f.parameterTypes.push_back(ffi_type_of(d.parameters[a]));
                                    }
                                    (void)value_type(d.result); // (validates)
                                    auto& added = p.functions.emplace(d.name, std::move(f)).first->second;
                                    if (::ffi_prep_cif(&added.cif, FFI_DEFAULT_ABI, d.parameterCount, ffi_type_of(d.result),
                                        added.parameterTypes.empty() ? nullptr : added.parameterTypes.data()) != FFI_OK)
                                        throw std::runtime_error("runtime plugin function '" + added.name + "': unsupported signature");
                                }
                                p.lib = std::move(lib);
                                return iPlugins.emplace(aNamespace, std::move(p)).first->second;
                            }
                            throw std::runtime_error("runtime plugin '" + fileName + "' not found (searched NEOS_RUNTIME_PLUGIN_PATH and the executable's directory)");
#endif
                        }
                    private:
                        std::recursive_mutex iMutex;
                        std::map<std::string, plugin> iPlugins;
                    };
                }

                void program_end()
                {
                    registry::instance().program_end();
                }

                bool available()
                {
#if defined(NEOS_HAVE_LIBFFI)
                    return true;
#else
                    return false;
#endif
                }

                bool plugin_available(std::string const& aNamespace)
                {
                    return registry::instance().exists(aNamespace);
                }

                function const& resolve(std::string const& aImport, std::vector<std::uint8_t> const& aParameters, std::vector<std::uint8_t> const& aResults)
                {
                    auto const& f = registry::instance().resolve(aImport);
                    auto const& d = *f.description;
                    auto signature = [](std::vector<std::uint8_t> const& aParameters, std::vector<std::uint8_t> const& aResults)
                    {
                        std::string result = "(";
                        for (std::size_t i = 0u; i < aParameters.size(); ++i)
                            result += (i == 0u ? "" : ", ") + type_name(aParameters[i]);
                        result += ") -> ";
                        result += aResults.empty() ? "void" : type_name(aResults[0]);
                        return result;
                    };
                    std::vector<std::uint8_t> parameters;
                    for (unsigned i = 0u; i < d.parameterCount; ++i)
                        parameters.push_back(value_type(d.parameters[i]));
                    std::vector<std::uint8_t> results;
                    if (d.result != NEOS_RT_VOID)
                        results.push_back(value_type(d.result));
                    if (parameters != aParameters || results != aResults)
                        throw std::runtime_error("import '" + aImport + "' " + signature(aParameters, aResults) +
                            " does not match the runtime plugin function " + signature(parameters, results));
                    return f;
                }

                bool restricted(function const& aFunction)
                {
                    return (aFunction.description->flags & NEOS_RT_RESTRICTED) != 0u;
                }

                void const* c_function(function const& aFunction, std::vector<std::uint8_t>& aParameters, std::uint8_t& aResult, char const* (*&aError)())
                {
                    auto const& d = *aFunction.description;
                    aParameters.assign(d.parameters, d.parameters + d.parameterCount);
                    aResult = static_cast<std::uint8_t>(d.result);
                    aError = aFunction.error;
                    return reinterpret_cast<void const*>(d.function);
                }

                long const* error_pending(function const& aFunction)
                {
                    return aFunction.errorPending;
                }

                void call(function const& aFunction, value* aBuffer, marshaller const& aMarshaller)
                {
#if !defined(NEOS_HAVE_LIBFFI)
                    (void)aFunction; (void)aBuffer; (void)aMarshaller;
                    throw std::runtime_error("neos was built without libffi");
#else
                    auto const& d = *aFunction.description;
                    union argument
                    {
                        bool b;
                        signed char c;
                        std::int32_t i32;
                        std::uint32_t u32;
                        std::int64_t i64;
                        std::uint64_t u64;
                        std::size_t size;
                        float f32;
                        double f64;
                        char const* string;
                        void* object;
                    };
                    argument arguments[NEOS_RT_MAX_PARAMETERS];
                    void* pointers[NEOS_RT_MAX_PARAMETERS];
                    std::vector<std::string> strings; // (copies of strings that cannot be used in place)
                    for (unsigned i = 0u; i < d.parameterCount; ++i)
                    {
                        auto& a = arguments[i];
                        auto const v = aBuffer[i];
                        switch (d.parameters[i])
                        {
                        case NEOS_RT_BOOL: a.b = static_cast<std::uint32_t>(v) != 0u; break;
                        case NEOS_RT_CHAR: a.c = static_cast<signed char>(static_cast<std::uint8_t>(v)); break;
                        case NEOS_RT_I32: a.i32 = static_cast<std::int32_t>(static_cast<std::uint32_t>(v)); break;
                        case NEOS_RT_U32: a.u32 = static_cast<std::uint32_t>(v); break;
                        case NEOS_RT_I64: a.i64 = static_cast<std::int64_t>(v); break;
                        case NEOS_RT_U64: a.u64 = v; break;
                        case NEOS_RT_SIZE: a.size = static_cast<std::size_t>(v); break;
                        case NEOS_RT_F32: { auto const bits = static_cast<std::uint32_t>(v); std::memcpy(&a.f32, &bits, sizeof(a.f32)); } break;
                        case NEOS_RT_F64: std::memcpy(&a.f64, &v, sizeof(a.f64)); break;
                        case NEOS_RT_STRING:
                            // (in place if possible)
                            a.string = aMarshaller.stringPointer(static_cast<std::uint32_t>(v));
                            if (a.string == nullptr)
                            {
                                strings.reserve(NEOS_RT_MAX_PARAMETERS);
                                strings.push_back(aMarshaller.readString(static_cast<std::uint32_t>(v)));
                                a.string = strings.back().c_str();
                            }
                            break;
                        case NEOS_RT_OBJECT:
                            a.object = aMarshaller.objectPointer(static_cast<std::uint32_t>(v));
                            break;
                        case NEOS_RT_INTEGER:
                            // (an object: its handle (shifted) becomes its address, with the low bit set)
                            if ((v & 1u) != 0u)
                                a.i64 = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(aMarshaller.objectPointer(static_cast<std::uint32_t>(v >> 1u))) | 1u);
                            else
                                a.i64 = static_cast<std::int64_t>(v);
                            break;
                        default:
                            break;
                        }
                        pointers[i] = &a;
                    }
                    union result
                    {
                        ffi_arg integral;
                        ffi_sarg signedIntegral;
                        std::int64_t i64;
                        std::uint64_t u64;
                        float f32;
                        double f64;
                        char const* string;
                        void* object;
                        unsigned char storage[16];
                    } r;
                    std::memset(&r, 0, sizeof(r));
                    ::ffi_call(const_cast<ffi_cif*>(&aFunction.cif), reinterpret_cast<void(*)(void)>(d.function), &r, pointers);
                    if (aFunction.error != nullptr)
                        if (auto const error = aFunction.error())
                            throw plugin_error(error);
                    switch (d.result)
                    {
                    case NEOS_RT_VOID: break;
                    case NEOS_RT_BOOL: aBuffer[0] = static_cast<value>((r.integral & 0xFFu) != 0u ? 1u : 0u); break;
                    case NEOS_RT_CHAR: aBuffer[0] = static_cast<value>(static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<signed char>(r.signedIntegral)))); break;
                    case NEOS_RT_I32: aBuffer[0] = static_cast<value>(static_cast<std::uint32_t>(static_cast<std::int32_t>(r.signedIntegral))); break;
                    case NEOS_RT_U32: aBuffer[0] = static_cast<value>(static_cast<std::uint32_t>(r.integral)); break;
                    case NEOS_RT_I64: aBuffer[0] = static_cast<value>(sizeof(ffi_arg) >= 8u ? static_cast<std::uint64_t>(r.signedIntegral) : static_cast<std::uint64_t>(r.i64)); break;
                    case NEOS_RT_U64: aBuffer[0] = sizeof(ffi_arg) >= 8u ? static_cast<value>(r.integral) : r.u64; break;
                    case NEOS_RT_SIZE: aBuffer[0] = static_cast<value>(static_cast<std::size_t>(r.integral)); break;
                    case NEOS_RT_F32: { std::uint32_t bits; std::memcpy(&bits, &r.f32, sizeof(bits)); aBuffer[0] = static_cast<value>(bits); break; }
                    case NEOS_RT_F64: { value bits; std::memcpy(&bits, &r.f64, sizeof(bits)); aBuffer[0] = bits; break; }
                    case NEOS_RT_STRING: aBuffer[0] = static_cast<value>(aMarshaller.allocateString(r.string != nullptr ? std::string_view{ r.string } : std::string_view{})); break;
                    case NEOS_RT_OBJECT: aBuffer[0] = static_cast<value>(aMarshaller.objectHandle(r.object)); break;
                    case NEOS_RT_INTEGER:
                        {
                            auto const v = sizeof(ffi_arg) >= 8u ? static_cast<std::uint64_t>(r.signedIntegral) : static_cast<std::uint64_t>(r.i64);
                            // (an object: a handle for the new reference (shifted), with the low bit set)
                            if ((v & 1u) != 0u)
                                aBuffer[0] = (static_cast<value>(aMarshaller.objectHandle(reinterpret_cast<void*>(static_cast<std::uintptr_t>(v & ~std::uint64_t{ 1u })))) << 1u) | 1u;
                            else
                                aBuffer[0] = static_cast<value>(v);
                        }
                        break;
                    }
#endif
                }
            }
        }
    }
}
