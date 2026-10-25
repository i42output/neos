/*
  cache.cpp

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
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>
#include <tuple>
#include <functional>
#include <vector>
#include <neolib/file/file.hpp>
#include <neos/cache.hpp>

namespace neos
{
    namespace cache
    {
        namespace
        {
            // an entry's header: magic, the format's version and its kind
            constexpr char Magic[8] = { 'n', 'e', 'o', 's', '.', 'c', 'a', 'c' };
            constexpr std::uint64_t Version = 1u;

            std::mutex& folder_mutex()
            {
                static std::mutex sMutex;
                return sMutex;
            }

            std::optional<std::string>& folder_set()
            {
                static std::optional<std::string> sFolder;
                return sFolder;
            }

            std::optional<std::string> environment_variable(char const* aName)
            {
#if defined(_WIN32)
                char* value = nullptr;
                std::size_t length = 0u;
                if (::_dupenv_s(&value, &length, aName) != 0 || value == nullptr)
                    return {};
                std::string result{ value };
                std::free(value);
#else
                char const* const value = std::getenv(aName);
                if (value == nullptr)
                    return {};
                std::string result{ value };
#endif
                if (result.empty())
                    return {};
                return result;
            }

            std::int64_t time_of(std::filesystem::file_time_type const& aTime)
            {
                return static_cast<std::int64_t>(aTime.time_since_epoch().count());
            }

            std::optional<std::string> read_file(std::filesystem::path const& aPath)
            {
                std::ifstream file{ aPath, std::ios::binary };
                if (!file)
                    return {};
                std::ostringstream contents;
                contents << file.rdbuf();
                if (file.bad())
                    return {};
                return std::move(contents).str();
            }

            std::filesystem::path entry_path(std::string const& aFolder, std::string_view aKind, std::uint64_t aKey)
            {
                char name[17];
                static char const sDigits[] = "0123456789abcdef";
                for (int i = 0; i < 16; ++i)
                    name[i] = sDigits[(aKey >> ((15 - i) * 4)) & 0xFu];
                name[16] = '\0';
                return std::filesystem::path{ aFolder } / (std::string{ aKind } + "." + name);
            }

            std::string header(std::string_view aKind)
            {
                writer result;
                result.u64(Version).str(aKind);
                return std::string{ Magic, sizeof(Magic) } + result.data();
            }
        }

        std::string default_folder()
        {
            std::error_code ec;
#if defined(_WIN32)
            if (auto const localAppData = environment_variable("LOCALAPPDATA"))
                return (std::filesystem::path{ *localAppData } / "neos" / "cache").string();
#elif defined(__APPLE__)
            if (auto const home = environment_variable("HOME"))
                return (std::filesystem::path{ *home } / "Library" / "Caches" / "neos").string();
#else
            if (auto const cacheHome = environment_variable("XDG_CACHE_HOME"); cacheHome && std::filesystem::path{ *cacheHome }.is_absolute())
                return (std::filesystem::path{ *cacheHome } / "neos").string();
            if (auto const home = environment_variable("HOME"))
                return (std::filesystem::path{ *home } / ".cache" / "neos").string();
#endif
            return (std::filesystem::temp_directory_path(ec) / "neos-cache").string();
        }

        void enable(std::string const& aFolder)
        {
            std::lock_guard<std::mutex> lock{ folder_mutex() };
            folder_set() = aFolder;
        }

        std::optional<std::string> folder()
        {
            std::lock_guard<std::mutex> lock{ folder_mutex() };
            return folder_set();
        }

        hasher& hasher::add(std::string_view aData)
        {
            add(static_cast<std::uint64_t>(aData.size()));
            for (char c : aData)
            {
                iValue ^= static_cast<unsigned char>(c);
                iValue *= 0x100000001B3ull;
            }
            return *this;
        }

        hasher& hasher::add(std::uint64_t aValue)
        {
            for (int i = 0; i < 8; ++i)
            {
                iValue ^= (aValue >> (i * 8)) & 0xFFu;
                iValue *= 0x100000001B3ull;
            }
            return *this;
        }

        std::uint64_t hash(std::string_view aData)
        {
            return hasher{}.add(aData).value();
        }

        std::uint64_t installation()
        {
            hasher result;
            result.add(Version);
            std::error_code ec;
            std::filesystem::path const executable{ neolib::program_file() };
            result.add(executable.generic_string());
            result.add(static_cast<std::uint64_t>(std::filesystem::file_size(executable, ec)));
            result.add(static_cast<std::uint64_t>(time_of(std::filesystem::last_write_time(executable, ec))));
            // (the folders that neos loads concept libraries, compiler plugins, schemas, stubs and runtime plugins from (see
            // context::init, schema::installed and runtime_plugins.cpp's search_path))
            std::filesystem::path const home{ neos::home() };
            std::vector<std::filesystem::path> folders{ home, home / "plugins", home.parent_path() / "share" / "neos" / "languages",
                executable.parent_path(), executable.parent_path() / "plugins" };
            std::vector<std::string> seen;
            for (auto const& folder : folders)
            {
                auto const name = folder.lexically_normal().generic_string();
                if (std::find(seen.begin(), seen.end(), name) != seen.end())
                    continue;
                seen.push_back(name);
                if (!std::filesystem::is_directory(folder, ec))
                    continue;
                std::vector<std::tuple<std::string, std::uint64_t, std::int64_t>> files;
                for (auto const& entry : std::filesystem::directory_iterator{ folder, ec })
                {
                    auto const extension = entry.path().extension().string();
                    if (extension != ".ncl" && extension != ".ncp" && extension != ".neos" && extension != ".stub" &&
                        extension != ".so" && extension != ".dll" && extension != ".dylib")
                        continue;
                    if (!entry.is_regular_file(ec))
                        continue;
                    files.emplace_back(entry.path().filename().generic_string(), static_cast<std::uint64_t>(entry.file_size(ec)),
                        time_of(entry.last_write_time(ec)));
                }
                std::sort(files.begin(), files.end());
                result.add(name);
                for (auto const& [file, size, time] : files)
                    result.add(file).add(size).add(static_cast<std::uint64_t>(time));
            }
            return result.value();
        }

        std::optional<file_stamp> stamp(std::string const& aPath)
        {
            std::error_code ec;
            std::filesystem::path const path{ aPath };
            if (!std::filesystem::is_regular_file(path, ec))
                return {};
            auto const time = std::filesystem::last_write_time(path, ec);
            if (ec)
                return {};
            auto const contents = read_file(path);
            if (!contents)
                return {};
            return file_stamp{ aPath, static_cast<std::uint64_t>(contents->size()), time_of(time), hash(*contents) };
        }

        bool unchanged(file_stamp const& aStamp)
        {
            std::error_code ec;
            std::filesystem::path const path{ aStamp.path };
            auto const size = std::filesystem::file_size(path, ec);
            if (ec || size != aStamp.size)
                return false;
            auto const time = std::filesystem::last_write_time(path, ec);
            if (ec)
                return false;
            if (time_of(time) == aStamp.time)
                return true;
            auto const contents = read_file(path);
            return contents && hash(*contents) == aStamp.contents;
        }

        std::optional<std::string> load(std::string_view aKind, std::uint64_t aKey)
        {
            auto const cacheFolder = folder();
            if (!cacheFolder)
                return {};
            auto contents = read_file(entry_path(*cacheFolder, aKind, aKey));
            if (!contents)
                return {};
            auto const expected = header(aKind);
            if (contents->size() < expected.size() || std::string_view{ *contents }.substr(0u, expected.size()) != expected)
                return {};
            contents->erase(0u, expected.size());
            return contents;
        }

        void store(std::string_view aKind, std::uint64_t aKey, std::string_view aContents)
        {
            auto const cacheFolder = folder();
            if (!cacheFolder)
                return;
            std::error_code ec;
            std::filesystem::create_directories(*cacheFolder, ec);
            auto const path = entry_path(*cacheFolder, aKind, aKey);
            // (a temporary file of its own (another neos may be writing the same entry), renamed when it is complete)
            auto const unique = hasher{}.add(static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())))
                .add(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())).value();
            auto temporary = path;
            temporary += ".tmp" + std::to_string(unique);
            {
                std::ofstream file{ temporary, std::ios::binary | std::ios::trunc };
                if (!file)
                    return;
                auto const head = header(aKind);
                file.write(head.data(), static_cast<std::streamsize>(head.size()));
                file.write(aContents.data(), static_cast<std::streamsize>(aContents.size()));
                file.close();
                if (!file)
                {
                    std::filesystem::remove(temporary, ec);
                    return;
                }
            }
            std::filesystem::rename(temporary, path, ec);
            if (ec)
                std::filesystem::remove(temporary, ec);
        }

        writer& writer::u64(std::uint64_t aValue)
        {
            for (int i = 0; i < 8; ++i)
                iData.push_back(static_cast<char>((aValue >> (i * 8)) & 0xFFu));
            return *this;
        }

        writer& writer::str(std::string_view aValue)
        {
            u64(aValue.size());
            iData.append(aValue);
            return *this;
        }

        writer& writer::stamp(file_stamp const& aStamp)
        {
            return str(aStamp.path).u64(aStamp.size).u64(static_cast<std::uint64_t>(aStamp.time)).u64(aStamp.contents);
        }

        std::uint64_t reader::u64()
        {
            if (iData.size() - iPosition < 8u)
            {
                iOk = false;
                iPosition = iData.size();
                return 0u;
            }
            std::uint64_t result = 0u;
            for (int i = 0; i < 8; ++i)
                result |= static_cast<std::uint64_t>(static_cast<unsigned char>(iData[iPosition + i])) << (i * 8);
            iPosition += 8u;
            return result;
        }

        std::string_view reader::str()
        {
            auto const size = u64();
            if (iData.size() - iPosition < size)
            {
                iOk = false;
                iPosition = iData.size();
                return {};
            }
            auto const result = iData.substr(iPosition, static_cast<std::size_t>(size));
            iPosition += static_cast<std::size_t>(size);
            return result;
        }

        file_stamp reader::stamp()
        {
            file_stamp result;
            result.path = std::string{ str() };
            result.size = u64();
            result.time = static_cast<std::int64_t>(u64());
            result.contents = u64();
            return result;
        }
    }
}
