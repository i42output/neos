/*
  symbols.hpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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
#include <neolib/core/string.hpp>
#include <neolib/core/map.hpp>
#include <neolib/core/unordered_map.hpp>
#include <neos/language/type.hpp>
#include <neos/language/symbol.hpp>

namespace neos
{
    namespace language
    {
        class i_scope;

        class i_symbol_table_entry
        {
        public:
            using abstract_type = i_symbol_table_entry;
        public:
            virtual ~i_symbol_table_entry() = default;
        public:
            virtual symbol_type type() const = 0;
            virtual std::uint32_t index() const = 0;
        };

        class symbol_table_entry : public i_symbol_table_entry
        {
        public:
            symbol_table_entry(symbol_type aType, std::uint32_t aIndex = 0u) : 
                iType{ aType },
                iIndex{ aIndex }
            {}
            symbol_table_entry(i_symbol_table_entry const& aOther) :
                iType{ aOther.type() },
                iIndex{ aOther.index() }
            {}
            symbol_table_entry& operator=(i_symbol_table_entry const& aOther)
            { 
                iType = aOther.type();
                iIndex = aOther.index();
                return *this; 
            }
        public:
            symbol_type type() const final
            {
                return iType;
            }
            std::uint32_t index() const final
            {
                return iIndex;
            }
        private:
            symbol_type iType;
            std::uint32_t iIndex;
        };

        using symbol_table = neolib::unordered_map<i_scope const*, neolib::unordered_multimap<symbol_name, symbol_table_entry>>;
        using i_symbol_table = neolib::maybe_abstract_t<symbol_table>;
    }
}