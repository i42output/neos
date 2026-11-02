/*
  lib_os.cpp

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

// The Ada runtime: the native primitives of the standard library (os): see ada.hpp
//
// For Ada.Calendar, Ada.Real_Time, Ada.Command_Line, Ada.Environment_Variables, Ada.Directories and Ada.Locales. Strings 
// are passed as (address, length); a primitive that gives a string writes it into a buffer that the caller supplies 
// (address, capacity) and returns its whole length (it writes no more than the capacity: the caller calls again with a 
// larger buffer if the length is larger), or a negative value if there is none. A primitive that does an operation returns
// a status: 0 (done), 1 (the name does not identify a suitable file: Name_Error), 2 (the operation is not possible: 
// Use_Error).
//
// The program's command line arguments: set_arguments gives them (the command name first); until it is called they are the
// host's (neos_rt_host::arguments), or none if there is no host.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdlib.h>
#elif defined(__APPLE__)
#include <unistd.h>
#include <crt_externs.h>
#else
#include <unistd.h>
extern char** environ;
#endif
#include "ada.hpp"

namespace neos::ada_runtime
{
    void set_arguments(std::vector<std::string> const& aArguments);
}

namespace
{
    using namespace neos::ada_runtime;
    namespace fs = std::filesystem;

    // --- strings

    std::string to_string(address aString, std::int64_t aLength)
    {
        return aLength <= 0 ? std::string{} : std::string{ chars(aString), static_cast<std::size_t>(aLength) };
    }

    std::int64_t give(std::string const& aValue, address aBuffer, std::int64_t aCapacity)
    {
        if (aCapacity > 0 && !aValue.empty())
            std::memcpy(chars(aBuffer), aValue.data(), std::min<std::size_t>(aValue.size(), static_cast<std::size_t>(aCapacity)));
        return static_cast<std::int64_t>(aValue.size());
    }

    fs::path to_path(address aName, std::int64_t aLength)
    {
        auto const name = to_string(aName, aLength);
        return fs::path{ std::u8string{ name.begin(), name.end() } };
    }

    std::string from_path(fs::path const& aPath)
    {
        auto const name = aPath.u8string();
        return std::string{ name.begin(), name.end() };
    }

    // --- the command line

    struct arguments_state
    {
        std::mutex mutex;
        std::optional<std::vector<std::string>> arguments;
    };

    arguments_state& arguments_state_()
    {
        static arguments_state sState;
        return sState;
    }

    std::vector<std::string> arguments()
    {
        auto& state = arguments_state_();
        std::scoped_lock lock{ state.mutex };
        if (state.arguments)
            return *state.arguments;
        std::vector<std::string> result;
        auto const h = host();
        if (h != nullptr && h->arguments != nullptr)
        {
            unsigned const count = h->arguments(nullptr, 0u);
            std::vector<char const*> pointers(count, nullptr);
            unsigned const got = count == 0u ? 0u : h->arguments(pointers.data(), count);
            for (unsigned i = 0u; i < got && i < count; ++i)
                result.emplace_back(pointers[i] != nullptr ? pointers[i] : "");
        }
        return result;
    }

    // the number of arguments (not counting the command name)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_argument_count()
    {
        auto const all = arguments();
        return all.empty() ? 0 : static_cast<std::int64_t>(all.size() - 1u);
    }

    // argument aNumber (0: the command name); -1 if there is none
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_argument(std::int64_t aNumber, address aBuffer, std::int64_t aCapacity)
    {
        auto const all = arguments();
        if (aNumber < 0 || static_cast<std::uint64_t>(aNumber) >= all.size())
            return -1;
        return give(all[static_cast<std::size_t>(aNumber)], aBuffer, aCapacity);
    }

    // --- clocks and the time zone

    // the real time clock: nanoseconds since 1970-01-01 00:00:00 UTC (leap seconds not counted)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_clock_realtime()
    {
        using namespace std::chrono;
        return static_cast<std::int64_t>(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
    }

    // the monotonic clock: nanoseconds since an unspecified origin
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_clock_monotonic()
    {
        using namespace std::chrono;
        return static_cast<std::int64_t>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
    }

    std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d)
    {
        y -= m <= 2u ? 1 : 0;
        std::int64_t const era = (y >= 0 ? y : y - 399) / 400;
        unsigned const yoe = static_cast<unsigned>(y - era * 400);
        unsigned const doy = (153u * (m + (m > 2u ? -3 : 9)) + 2u) / 5u + d - 1u;
        unsigned const doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
        return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
    }

    std::optional<std::int64_t> local_offset(std::int64_t aSeconds)
    {
        std::time_t const t = static_cast<std::time_t>(aSeconds);
        std::tm local{};
#ifdef _WIN32
        if (localtime_s(&local, &t) != 0)
            return {};
#else
        if (localtime_r(&t, &local) == nullptr)
            return {};
#endif
        std::int64_t const localSeconds = days_from_civil(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1), 
            static_cast<unsigned>(local.tm_mday)) * 86400 + local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
        return (localSeconds - aSeconds) / 60;
    }

    // the local time zone's offset from UTC (minutes) at the time aSeconds (seconds since 1970-01-01 00:00:00 UTC); 
    // 100000 if it is unknown. (A time that the C library cannot convert uses the offset of the nearest time it can.)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_utc_offset(std::int64_t aSeconds)
    {
        auto offset = local_offset(aSeconds);
        if (!offset)
            offset = local_offset(std::clamp<std::int64_t>(aSeconds, 0, 32503593600)); // (1970 .. 2999)
        if (!offset || *offset < -28 * 60 || *offset > 28 * 60)
            return 100000;
        return *offset;
    }

    // --- environment variables

    std::mutex& environment_mutex()
    {
        static std::mutex sMutex;
        return sMutex;
    }

    bool valid_name(std::string const& aName)
    {
        return !aName.empty() && aName.find('=') == std::string::npos && aName.find('\0') == std::string::npos;
    }

    // the value of the variable; -1 if there is none
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_env_value(address aName, std::int64_t aLength, address aBuffer, std::int64_t aCapacity)
    {
        auto const name = to_string(aName, aLength);
        if (!valid_name(name))
            return -1;
        std::scoped_lock lock{ environment_mutex() };
        char const* const value = std::getenv(name.c_str());
        if (value == nullptr)
            return -1;
        return give(value, aBuffer, aCapacity);
    }

    // set a variable: 0, else -1 (the name or value is not allowed)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_env_set(address aName, std::int64_t aLength, address aValue, std::int64_t aValueLength)
    {
        auto const name = to_string(aName, aLength);
        auto const value = to_string(aValue, aValueLength);
        if (!valid_name(name) || value.find('\0') != std::string::npos)
            return -1;
        std::scoped_lock lock{ environment_mutex() };
#ifdef _WIN32
        return _putenv_s(name.c_str(), value.c_str()) == 0 ? 0 : -1;
#else
        return ::setenv(name.c_str(), value.c_str(), 1) == 0 ? 0 : -1;
#endif
    }

    // remove a variable: 0, else -1
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_env_clear(address aName, std::int64_t aLength)
    {
        auto const name = to_string(aName, aLength);
        if (!valid_name(name))
            return 0;
        std::scoped_lock lock{ environment_mutex() };
#ifdef _WIN32
        return _putenv_s(name.c_str(), "") == 0 ? 0 : -1;
#else
        return ::unsetenv(name.c_str()) == 0 ? 0 : -1;
#endif
    }

    std::vector<std::string> environment()
    {
        std::vector<std::string> result;
#if defined(_WIN32)
        char** const all = _environ;
#elif defined(__APPLE__)
        char** const all = *_NSGetEnviron();
#else
        char** const all = environ;
#endif
        for (char** e = all; e != nullptr && *e != nullptr; ++e)
            if (**e != '=') // (Windows's hidden "=C:=C:\..." entries are not variables)
                result.emplace_back(*e);
        return result;
    }

    // remove all variables
    NEOS_ADA_PRIMITIVE void neos_ada_env_clear_all()
    {
        std::scoped_lock lock{ environment_mutex() };
        for (auto const& entry : environment())
        {
            auto const name = entry.substr(0u, entry.find('='));
#ifdef _WIN32
            _putenv_s(name.c_str(), "");
#else
            ::unsetenv(name.c_str());
#endif
        }
    }

    // the number of variables
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_env_count()
    {
        std::scoped_lock lock{ environment_mutex() };
        return static_cast<std::int64_t>(environment().size());
    }

    // variable aIndex (0 ..): "name=value"; -1 if there is none
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_env_entry(std::int64_t aIndex, address aBuffer, std::int64_t aCapacity)
    {
        std::scoped_lock lock{ environment_mutex() };
        auto const all = environment();
        if (aIndex < 0 || static_cast<std::uint64_t>(aIndex) >= all.size())
            return -1;
        return give(all[static_cast<std::size_t>(aIndex)], aBuffer, aCapacity);
    }

    // --- files and directories

    // 1 on Windows (file names: '\' and '/' are separators, and a name can start with a drive), else 0
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_os_windows()
    {
#ifdef _WIN32
        return 1;
#else
        return 0;
#endif
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_current_directory(address aBuffer, std::int64_t aCapacity)
    {
        std::error_code ec;
        auto const path = fs::current_path(ec);
        if (ec)
            return -1;
        return give(from_path(path), aBuffer, aCapacity);
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_set_directory(address aName, std::int64_t aLength)
    {
        auto const path = to_path(aName, aLength);
        std::error_code ec;
        if (path.empty() || !fs::is_directory(path, ec))
            return 1;
        fs::current_path(path, ec);
        return ec ? 2 : 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_create_directory(address aName, std::int64_t aLength)
    {
        auto const path = to_path(aName, aLength);
        std::error_code ec;
        if (path.empty())
            return 1;
        if (fs::exists(path, ec))
            return 2;
        if (!fs::create_directory(path, ec) || ec)
            return 2;
        return 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_create_path(address aName, std::int64_t aLength)
    {
        auto const path = to_path(aName, aLength);
        std::error_code ec;
        if (path.empty())
            return 1;
        if (fs::is_directory(path, ec))
            return 0;
        fs::create_directories(path, ec);
        return ec || !fs::is_directory(path, ec) ? 2 : 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_delete_directory(address aName, std::int64_t aLength)
    {
        auto const path = to_path(aName, aLength);
        std::error_code ec;
        if (path.empty() || !fs::is_directory(fs::symlink_status(path, ec)))
            return 1;
        if (!fs::is_empty(path, ec) || ec)
            return 2;
        fs::remove(path, ec);
        return ec ? 2 : 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_delete_tree(address aName, std::int64_t aLength)
    {
        auto const path = to_path(aName, aLength);
        std::error_code ec;
        if (path.empty() || !fs::is_directory(fs::symlink_status(path, ec)))
            return 1;
        fs::remove_all(path, ec);
        return ec ? 2 : 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_delete_file(address aName, std::int64_t aLength)
    {
        auto const path = to_path(aName, aLength);
        std::error_code ec;
        auto const status = fs::symlink_status(path, ec);
        if (path.empty() || ec || !fs::exists(status) || fs::is_directory(status))
            return 1;
        fs::remove(path, ec);
        return ec ? 2 : 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_rename(address aOldName, std::int64_t aOldLength, address aNewName, std::int64_t aNewLength)
    {
        auto const from = to_path(aOldName, aOldLength);
        auto const to = to_path(aNewName, aNewLength);
        std::error_code ec;
        if (from.empty() || to.empty() || !fs::exists(fs::symlink_status(from, ec)))
            return 1;
        if (fs::exists(fs::symlink_status(to, ec)))
            return 2;
        fs::rename(from, to, ec);
        return ec ? 2 : 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_copy_file(address aSource, std::int64_t aSourceLength, address aTarget, std::int64_t aTargetLength)
    {
        auto const from = to_path(aSource, aSourceLength);
        auto const to = to_path(aTarget, aTargetLength);
        std::error_code ec;
        if (from.empty() || to.empty() || !fs::exists(from, ec) || fs::is_directory(from, ec))
            return 1;
        if (fs::is_directory(to, ec))
            return 2;
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        return ec ? 2 : 0;
    }

    // the kind of a file: 0 (directory), 1 (ordinary file), 2 (special file), -1 (none)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_file_kind(address aName, std::int64_t aLength)
    {
        auto const path = to_path(aName, aLength);
        std::error_code ec;
        if (path.empty())
            return -1;
        auto const status = fs::status(path, ec);
        if (ec || !fs::exists(status))
            return -1;
        if (fs::is_directory(status))
            return 0;
        if (fs::is_regular_file(status))
            return 1;
        return 2;
    }

    // the size of a file (bytes); -1 if there is no such file (a directory's or special file's size is 0)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_file_size(address aName, std::int64_t aLength)
    {
        auto const path = to_path(aName, aLength);
        std::error_code ec;
        auto const status = fs::status(path, ec);
        if (path.empty() || ec || !fs::exists(status))
            return -1;
        if (!fs::is_regular_file(status))
            return 0;
        auto const size = fs::file_size(path, ec);
        return ec ? -1 : static_cast<std::int64_t>(size);
    }

    // the modification time of a file (nanoseconds since 1970-01-01 00:00:00 UTC); INT64_MIN if there is no such file,
    // INT64_MIN + 1 if it is not known
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_file_time(address aName, std::int64_t aLength)
    {
        auto const path = to_path(aName, aLength);
        std::error_code ec;
        if (path.empty() || !fs::exists(path, ec))
            return INT64_MIN;
        auto const time = fs::last_write_time(path, ec);
        if (ec)
            return INT64_MIN + 1;
        using namespace std::chrono;
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L && !defined(_LIBCPP_VERSION)
        auto const system = time_point_cast<nanoseconds>(clock_cast<system_clock>(time));
#else
        auto const system = time_point_cast<nanoseconds>(system_clock::now()) + 
            duration_cast<nanoseconds>(time - fs::file_time_type::clock::now());
#endif
        return static_cast<std::int64_t>(system.time_since_epoch().count());
    }

    // the full name of a file (an absolute name, with "." and ".." parts resolved; the file need not exist)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_full_name(address aName, std::int64_t aLength, address aBuffer, std::int64_t aCapacity)
    {
        auto const path = to_path(aName, aLength);
        std::error_code ec;
        if (path.empty())
            return -1;
        auto full = fs::absolute(path, ec);
        if (ec)
            return -1;
        full = full.lexically_normal();
        if (full.has_relative_path() && !full.has_filename()) // (a trailing separator)
            full = full.parent_path();
        auto result = from_path(full);
#ifndef _WIN32
        while (result.size() >= 2u && result[0] == '/' && result[1] == '/') // ("//" as "/")
            result.erase(0u, 1u);
#endif
        return give(result, aBuffer, aCapacity);
    }

    // how the directory of a file compares names: 1 (case sensitive), 3 (case preserving)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_name_case(address, std::int64_t)
    {
#if defined(_WIN32) || defined(__APPLE__)
        return 3;
#else
        return 1;
#endif
    }

    // directory searches (a handle is a native object's number: never an address)
    struct search
    {
        int dots = 0; // (the entries "." and ".." come first)
        fs::directory_iterator next;
        std::optional<std::string> pending;
    };

    struct searches
    {
        std::mutex mutex;
        std::map<std::int64_t, std::unique_ptr<search>> open;
        std::int64_t nextHandle = 1;
    };

    searches& searches_()
    {
        static searches sSearches;
        return sSearches;
    }

    // start reading a directory: a handle (> 0), else -1 (not a directory) or -2 (it cannot be read)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_dir_open(address aName, std::int64_t aLength)
    {
        auto const path = to_path(aName, aLength);
        std::error_code ec;
        if (path.empty() || !fs::is_directory(path, ec))
            return -1;
        auto s = std::make_unique<search>();
        s->next = fs::directory_iterator{ path, ec };
        if (ec)
            return -2;
        auto& all = searches_();
        std::scoped_lock lock{ all.mutex };
        auto const handle = all.nextHandle++;
        all.open.emplace(handle, std::move(s));
        return handle;
    }

    // the simple name of the directory's next entry ("." and ".." first); -1 at the end. (If the name is longer than the 
    // buffer, the entry remains the next one.)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_dir_next(std::int64_t aHandle, address aBuffer, std::int64_t aCapacity)
    {
        auto& all = searches_();
        std::scoped_lock lock{ all.mutex };
        auto const existing = all.open.find(aHandle);
        if (existing == all.open.end())
            return -1;
        auto& s = *existing->second;
        if (!s.pending && s.dots < 2)
            s.pending = s.dots++ == 0 ? "." : "..";
        if (!s.pending)
        {
            std::error_code ec;
            if (s.next == fs::directory_iterator{})
                return -1;
            s.pending = from_path(s.next->path().filename());
            s.next.increment(ec);
            if (ec)
                s.next = fs::directory_iterator{};
        }
        auto const length = give(*s.pending, aBuffer, aCapacity);
        if (length <= aCapacity)
            s.pending.reset();
        return length;
    }

    NEOS_ADA_PRIMITIVE void neos_ada_dir_close(std::int64_t aHandle)
    {
        auto& all = searches_();
        std::scoped_lock lock{ all.mutex };
        all.open.erase(aHandle);
    }

    // --- the locale

    // ISO 639-1 (two letter) language codes and their ISO 639-3 (three letter) codes
    struct language_code { char const* two; char const* three; };
    language_code const sLanguageCodes[] = {
        { "aa", "aar" }, { "ab", "abk" }, { "ae", "ave" }, { "af", "afr" }, { "ak", "aka" }, { "am", "amh" }, { "an", "arg" },
        { "ar", "ara" }, { "as", "asm" }, { "av", "ava" }, { "ay", "aym" }, { "az", "aze" }, { "ba", "bak" }, { "be", "bel" },
        { "bg", "bul" }, { "bi", "bis" }, { "bm", "bam" }, { "bn", "ben" }, { "bo", "bod" }, { "br", "bre" }, { "bs", "bos" },
        { "ca", "cat" }, { "ce", "che" }, { "ch", "cha" }, { "co", "cos" }, { "cr", "cre" }, { "cs", "ces" }, { "cu", "chu" },
        { "cv", "chv" }, { "cy", "cym" }, { "da", "dan" }, { "de", "deu" }, { "dv", "div" }, { "dz", "dzo" }, { "ee", "ewe" },
        { "el", "ell" }, { "en", "eng" }, { "eo", "epo" }, { "es", "spa" }, { "et", "est" }, { "eu", "eus" }, { "fa", "fas" },
        { "ff", "ful" }, { "fi", "fin" }, { "fj", "fij" }, { "fo", "fao" }, { "fr", "fra" }, { "fy", "fry" }, { "ga", "gle" },
        { "gd", "gla" }, { "gl", "glg" }, { "gn", "grn" }, { "gu", "guj" }, { "gv", "glv" }, { "ha", "hau" }, { "he", "heb" },
        { "hi", "hin" }, { "ho", "hmo" }, { "hr", "hrv" }, { "ht", "hat" }, { "hu", "hun" }, { "hy", "hye" }, { "hz", "her" },
        { "ia", "ina" }, { "id", "ind" }, { "ie", "ile" }, { "ig", "ibo" }, { "ii", "iii" }, { "ik", "ipk" }, { "in", "ind" },
        { "io", "ido" }, { "is", "isl" }, { "it", "ita" }, { "iu", "iku" }, { "iw", "heb" }, { "ja", "jpn" }, { "ji", "yid" },
        { "jv", "jav" }, { "ka", "kat" }, { "kg", "kon" }, { "ki", "kik" }, { "kj", "kua" }, { "kk", "kaz" }, { "kl", "kal" },
        { "km", "khm" }, { "kn", "kan" }, { "ko", "kor" }, { "kr", "kau" }, { "ks", "kas" }, { "ku", "kur" }, { "kv", "kom" },
        { "kw", "cor" }, { "ky", "kir" }, { "la", "lat" }, { "lb", "ltz" }, { "lg", "lug" }, { "li", "lim" }, { "ln", "lin" },
        { "lo", "lao" }, { "lt", "lit" }, { "lu", "lub" }, { "lv", "lav" }, { "mg", "mlg" }, { "mh", "mah" }, { "mi", "mri" },
        { "mk", "mkd" }, { "ml", "mal" }, { "mn", "mon" }, { "mr", "mar" }, { "ms", "msa" }, { "mt", "mlt" }, { "my", "mya" },
        { "na", "nau" }, { "nb", "nob" }, { "nd", "nde" }, { "ne", "nep" }, { "ng", "ndo" }, { "nl", "nld" }, { "nn", "nno" },
        { "no", "nor" }, { "nr", "nbl" }, { "nv", "nav" }, { "ny", "nya" }, { "oc", "oci" }, { "oj", "oji" }, { "om", "orm" },
        { "or", "ori" }, { "os", "oss" }, { "pa", "pan" }, { "pi", "pli" }, { "pl", "pol" }, { "ps", "pus" }, { "pt", "por" },
        { "qu", "que" }, { "rm", "roh" }, { "rn", "run" }, { "ro", "ron" }, { "ru", "rus" }, { "rw", "kin" }, { "sa", "san" },
        { "sc", "srd" }, { "sd", "snd" }, { "se", "sme" }, { "sg", "sag" }, { "si", "sin" }, { "sk", "slk" }, { "sl", "slv" },
        { "sm", "smo" }, { "sn", "sna" }, { "so", "som" }, { "sq", "sqi" }, { "sr", "srp" }, { "ss", "ssw" }, { "st", "sot" },
        { "su", "sun" }, { "sv", "swe" }, { "sw", "swa" }, { "ta", "tam" }, { "te", "tel" }, { "tg", "tgk" }, { "th", "tha" },
        { "ti", "tir" }, { "tk", "tuk" }, { "tl", "tgl" }, { "tn", "tsn" }, { "to", "ton" }, { "tr", "tur" }, { "ts", "tso" },
        { "tt", "tat" }, { "tw", "twi" }, { "ty", "tah" }, { "ug", "uig" }, { "uk", "ukr" }, { "ur", "urd" }, { "uz", "uzb" },
        { "ve", "ven" }, { "vi", "vie" }, { "vo", "vol" }, { "wa", "wln" }, { "wo", "wol" }, { "xh", "xho" }, { "yi", "yid" },
        { "yo", "yor" }, { "za", "zha" }, { "zh", "zho" }, { "zu", "zul" }
    };

    // the active locale's name: e.g. "en_GB.UTF-8" (POSIX) or "en-GB" (Windows)
    std::string locale_name()
    {
#ifdef _WIN32
        wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) == 0)
            return {};
        std::string result;
        for (wchar_t const* c = name; *c != L'\0'; ++c)
            result += *c < 128 ? static_cast<char>(*c) : '?';
        return result;
#else
        std::scoped_lock lock{ environment_mutex() };
        for (char const* variable : { "LC_ALL", "LC_MESSAGES", "LANG" })
        {
            char const* const value = std::getenv(variable);
            if (value != nullptr && *value != '\0')
                return value;
        }
        return {};
#endif
    }

    // the active locale: its language (ISO 639-3, three lower case letters) then its country (ISO 3166-1 alpha-2, two 
    // upper case letters): "und" and "ZZ" if they are not known (writes 5 characters)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_locale(address aBuffer, std::int64_t aCapacity)
    {
        std::string language = "und";
        std::string country = "ZZ";
        auto const name = locale_name();
        std::size_t letters = 0u;
        while (letters < name.size() && std::isalpha(static_cast<unsigned char>(name[letters])))
            ++letters;
        std::string code = name.substr(0u, letters);
        for (auto& c : code)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (code.size() == 2u)
        {
            for (auto const& entry : sLanguageCodes)
                if (code == entry.two)
                    language = entry.three;
        }
        else if (code.size() == 3u)
            language = code;
        if (letters < name.size() && (name[letters] == '_' || name[letters] == '-'))
        {
            std::size_t const start = letters + 1u;
            std::size_t end = start;
            while (end < name.size() && std::isalpha(static_cast<unsigned char>(name[end])))
                ++end;
            if (end - start == 4u) // (a script, e.g. "zh-Hans-CN")
            {
                if (end < name.size() && (name[end] == '_' || name[end] == '-'))
                {
                    std::size_t const start2 = end + 1u;
                    std::size_t end2 = start2;
                    while (end2 < name.size() && std::isalpha(static_cast<unsigned char>(name[end2])))
                        ++end2;
                    if (end2 - start2 == 2u)
                        country = name.substr(start2, 2u);
                }
            }
            else if (end - start == 2u)
                country = name.substr(start, 2u);
            for (auto& c : country)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return give(language + country, aBuffer, aCapacity);
    }

    [[maybe_unused]] constexpr auto I64 = NEOS_RT_I64;
    [[maybe_unused]] constexpr auto F64 = NEOS_RT_F64;
    [[maybe_unused]] constexpr auto NO_RESULT = NEOS_RT_VOID;    // (not VOID: a macro of windows.h)
    constexpr unsigned RESTRICTED = NEOS_RT_RESTRICTED;

    function_table const sosFunctions{
        { "argument_count", NEOS_RT_FUNCTION(neos_ada_argument_count), I64, 0u, {}, 0u },
        { "argument", NEOS_RT_FUNCTION(neos_ada_argument), I64, 3u, { I64, I64, I64 }, 0u },
        { "clock_realtime", NEOS_RT_FUNCTION(neos_ada_clock_realtime), I64, 0u, {}, 0u },
        { "clock_monotonic", NEOS_RT_FUNCTION(neos_ada_clock_monotonic), I64, 0u, {}, 0u },
        { "utc_offset", NEOS_RT_FUNCTION(neos_ada_utc_offset), I64, 1u, { I64 }, 0u },
        { "env_value", NEOS_RT_FUNCTION(neos_ada_env_value), I64, 4u, { I64, I64, I64, I64 }, 0u },
        { "env_set", NEOS_RT_FUNCTION(neos_ada_env_set), I64, 4u, { I64, I64, I64, I64 }, RESTRICTED },
        { "env_clear", NEOS_RT_FUNCTION(neos_ada_env_clear), I64, 2u, { I64, I64 }, RESTRICTED },
        { "env_clear_all", NEOS_RT_FUNCTION(neos_ada_env_clear_all), NO_RESULT, 0u, {}, RESTRICTED },
        { "env_count", NEOS_RT_FUNCTION(neos_ada_env_count), I64, 0u, {}, 0u },
        { "env_entry", NEOS_RT_FUNCTION(neos_ada_env_entry), I64, 3u, { I64, I64, I64 }, 0u },
        { "os_windows", NEOS_RT_FUNCTION(neos_ada_os_windows), I64, 0u, {}, 0u },
        { "current_directory", NEOS_RT_FUNCTION(neos_ada_current_directory), I64, 2u, { I64, I64 }, 0u },
        { "set_directory", NEOS_RT_FUNCTION(neos_ada_set_directory), I64, 2u, { I64, I64 }, RESTRICTED },
        { "create_directory", NEOS_RT_FUNCTION(neos_ada_create_directory), I64, 2u, { I64, I64 }, RESTRICTED },
        { "create_path", NEOS_RT_FUNCTION(neos_ada_create_path), I64, 2u, { I64, I64 }, RESTRICTED },
        { "delete_directory", NEOS_RT_FUNCTION(neos_ada_delete_directory), I64, 2u, { I64, I64 }, RESTRICTED },
        { "delete_tree", NEOS_RT_FUNCTION(neos_ada_delete_tree), I64, 2u, { I64, I64 }, RESTRICTED },
        { "delete_file", NEOS_RT_FUNCTION(neos_ada_delete_file), I64, 2u, { I64, I64 }, RESTRICTED },
        { "rename", NEOS_RT_FUNCTION(neos_ada_rename), I64, 4u, { I64, I64, I64, I64 }, RESTRICTED },
        { "copy_file", NEOS_RT_FUNCTION(neos_ada_copy_file), I64, 4u, { I64, I64, I64, I64 }, RESTRICTED },
        { "file_kind", NEOS_RT_FUNCTION(neos_ada_file_kind), I64, 2u, { I64, I64 }, 0u },
        { "file_size", NEOS_RT_FUNCTION(neos_ada_file_size), I64, 2u, { I64, I64 }, 0u },
        { "file_time", NEOS_RT_FUNCTION(neos_ada_file_time), I64, 2u, { I64, I64 }, 0u },
        { "full_name", NEOS_RT_FUNCTION(neos_ada_full_name), I64, 4u, { I64, I64, I64, I64 }, 0u },
        { "name_case", NEOS_RT_FUNCTION(neos_ada_name_case), I64, 2u, { I64, I64 }, 0u },
        { "dir_open", NEOS_RT_FUNCTION(neos_ada_dir_open), I64, 2u, { I64, I64 }, 0u },
        { "dir_next", NEOS_RT_FUNCTION(neos_ada_dir_next), I64, 3u, { I64, I64, I64 }, 0u },
        { "dir_close", NEOS_RT_FUNCTION(neos_ada_dir_close), NO_RESULT, 1u, { I64 }, 0u },
        { "locale", NEOS_RT_FUNCTION(neos_ada_locale), I64, 2u, { I64, I64 }, 0u },
    };
}

namespace neos::ada_runtime
{
    // the program's command line arguments, the command name first (for a host that does not give them through 
    // neos_rt_host::arguments, or to replace them)
    void set_arguments(std::vector<std::string> const& aArguments)
    {
        auto& state = arguments_state_();
        std::scoped_lock lock{ state.mutex };
        state.arguments = aArguments;
    }
}
