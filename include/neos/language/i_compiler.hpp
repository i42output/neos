/*
  i_compiler.hpp

  Copyright (c) 2019 Leigh Johnston.  All Rights Reserved.

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

#pragma once

#include <neos/neos.hpp>
#include <neolib/string.hpp>
#include <neolib/optional.hpp>

namespace neos::language
{
    typedef neolib::i_string i_source_file_path_t;
    typedef neolib::i_optional<i_source_file_path_t> i_optional_source_file_path_t;
    typedef neolib::i_string i_source_t;

    enum class compilation_status
    {
        Pending,
        Compiling,
        Compiled
    };

    class i_source_fragment
    {
    public:
        typedef const char* const_fast_iterator;
        typedef char* fast_iterator;
    public:
        virtual ~i_source_fragment() {}
    public:
        virtual const i_optional_source_file_path_t& source_file_path() const = 0;
        virtual i_optional_source_file_path_t& source_file_path() = 0;
        virtual const i_source_t& source() const = 0;
        virtual i_source_t& source() = 0;
        virtual compilation_status status() const = 0;
    public:
        virtual const_fast_iterator cbegin() const = 0;
        virtual const_fast_iterator cend() const = 0;
        virtual const_fast_iterator begin() const = 0;
        virtual const_fast_iterator end() const = 0;
        virtual fast_iterator begin() = 0;
        virtual fast_iterator end() = 0;
    };

    class i_compiler
    {
    public:
        virtual ~i_compiler() {}
    public:
        virtual void compile(const i_source_fragment& aFragment) = 0;
    };
}