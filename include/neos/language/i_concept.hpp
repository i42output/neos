/*
  i_concept.hpp

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
#include <neolib/i_reference_counted.hpp>
#include <neolib/i_string.hpp>

namespace neos::language
{
    enum class compiler_pass
    {
        Probe,
        Emit
    };

    class i_concept : public neolib::i_reference_counted
    {
    public:
        typedef const char* source_iterator;
    public:
        virtual const neolib::i_string& name() const = 0;
    public:
        virtual source_iterator consume_token(compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd) const = 0;
    public:
        template <typename SourceIterator>
        SourceIterator consume_token(compiler_pass aPass, SourceIterator aSource, SourceIterator aSourceEnd) const
        {
            auto result = consume_token(aPass, &*aSource, std::next(&*aSource, std::distance(aSource, aSourceEnd)));
            return std::next(aSource, std::distance(&*aSource, result));
        }
    };
}