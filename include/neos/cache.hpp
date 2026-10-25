/*
  cache.hpp

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

#pragma once

#include <neos/neos.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace neos
{
    namespace cache
    {
        // The compilation cache (neos --cache): the results of compilations (a program's text, a schema's grammars as parsed)
        // kept in files, so that compiling the same thing again is not needed. It is off unless enabled (neos's --cache).
        // A file of the cache is named by a hash of everything that its result depends on; one that cannot be read (e.g. of
        // an older neos) is ignored, so deleting the cache's folder (or any of its files) is always safe.

        // the cache's folder if it is not given: the user's cache folder's "neos" ($XDG_CACHE_HOME or ~/.cache on Linux,
        // %LOCALAPPDATA%\neos\cache on Windows, ~/Library/Caches on macOS)
        std::string default_folder();
        // enables the cache (in the folder aFolder)
        void enable(std::string const& aFolder = default_folder());
        // the cache's folder (none: the cache is not enabled)
        std::optional<std::string> folder();

        // a hash (64-bit FNV-1a) of data added to it
        class hasher
        {
        public:
            hasher& add(std::string_view aData);
            hasher& add(std::uint64_t aValue);
            std::uint64_t value() const { return iValue; }
        private:
            std::uint64_t iValue = 0xCBF29CE484222325ull;
        };
        std::uint64_t hash(std::string_view aData);

        // a fingerprint of neos's installation: its executable, concept libraries, compiler plugins, schemas, stubs and
        // runtime plugins (their names, sizes and modification times); a result of a compilation depends on all of them
        std::uint64_t installation();

        // a file that a result depends on, as it was: its size, modification time and the hash of its contents
        struct file_stamp
        {
            std::string path;
            std::uint64_t size = 0u;
            std::int64_t time = 0;
            std::uint64_t contents = 0u;
        };
        std::optional<file_stamp> stamp(std::string const& aPath); ///< (none: not a file that can be read)
        bool unchanged(file_stamp const& aStamp); ///< (the same size and time, or else the same contents)

        // an entry of the cache (aKind: what it is a result of, e.g. "bytecode"); entries are written to a temporary file that
        // is then renamed, so that one being written is not read; failing to write one is not an error
        std::optional<std::string> load(std::string_view aKind, std::uint64_t aKey);
        void store(std::string_view aKind, std::uint64_t aKey, std::string_view aContents);

        // the encoding of an entry's contents
        class writer
        {
        public:
            writer& u64(std::uint64_t aValue);
            writer& str(std::string_view aValue);
            writer& stamp(file_stamp const& aStamp);
            std::string const& data() const { return iData; }
        private:
            std::string iData;
        };
        class reader
        {
        public:
            explicit reader(std::string_view aData) : iData{ aData } {}
        public:
            std::uint64_t u64();
            std::string_view str();
            file_stamp stamp();
            bool ok() const { return iOk; }     ///< (false: a value was read past the end of the data)
            bool at_end() const { return iPosition == iData.size(); }
        private:
            std::string_view iData;
            std::size_t iPosition = 0u;
            bool iOk = true;
        };
    }
}
