/*
  scope.hpp

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
#include <unordered_map>
#include <neolib/core/reference_counted.hpp>
#include <neolib/core/vector.hpp>
#include <neolib/core/string.hpp>
#include <neos/language/function.hpp>

namespace neos
{
    namespace language
    {
        using i_scope_name = neolib::i_string;
        using scope_name = neolib::string;

        enum class scope_type : std::uint32_t
        {
            Program,
            Namespace,
            Class,
            Function,
            Block
        };

        class i_scope : public neolib::i_reference_counted
        {
        public:
            using abstract_type = i_scope;
            using i_child_list = neolib::i_vector<neolib::i_ref_ptr<i_scope>>;
        public:
            virtual ~i_scope() = default;
        public:
            virtual bool has_parent() const = 0;
            virtual i_scope const& parent() const = 0;
            virtual i_child_list const& children() const = 0;
            virtual i_child_list& children() = 0;
            virtual i_scope_name const& name() const = 0;
            virtual i_scope_name const& qualified_name() const = 0;
            virtual scope_type type() const = 0;
        public:
            virtual i_scope& create_child(i_scope_name const& aName, scope_type aType) = 0;
        };

        class i_function_scope : public i_scope
        {
        public:
            using abstract_type = i_function_scope;
        public:
            virtual i_function_signature const& function_signature() const = 0;
            virtual void set_function_signature(i_function_signature const& aSignature) = 0;
        };

        template <typename Base = i_scope>
        class scope : public neolib::reference_counted<Base>
        {
        public:
            using child_list = neolib::vector<neolib::ref_ptr<i_scope>>;
        private:
            using index = std::unordered_map<std::string_view, i_scope*>;
        public:
            scope() :
                iParent{ nullptr },
                iName{},
                iType{ scope_type::Program }
            {}
            scope(i_scope_name const& aName, scope_type aType) :
                iParent{ nullptr },
                iName{ aName },
                iType{ aType }
            {}
            scope(i_scope& aParent, i_scope_name const& aName, scope_type aType) :
                iParent{ &aParent },
                iName{ aName },
                iType{ aType }
            {}
        public:
            bool has_parent() const final
            {
                return iParent != nullptr;
            }
            i_scope const& parent() const final
            {
                if (iParent)
                    return *iParent;
                throw std::logic_error("neos::scope: no parent!");
            }
            child_list const& children() const final
            {
                return iChildren;
            }
            child_list& children() final
            {
                return iChildren;
            }
            scope_name const& name() const final
            {
                return iName;
            }
            scope_name const& qualified_name() const final
            {
                if (iQualifiedName.has_value())
                    return iQualifiedName.value();
                if (has_parent())
                    iQualifiedName = parent().qualified_name() + "::" + name();
                else
                    iQualifiedName = name();
                return iQualifiedName.value();
            }
            scope_type type() const final
            {
                return iType;
            }
        public:
            i_scope& create_child(i_scope_name const& aName, scope_type aType) final;
            void clear()
            {
                iChildIndex.clear();
                iChildren.clear();
            }
        private:
            i_scope* iParent;
            neolib::string iName;
            mutable std::optional<neolib::string> iQualifiedName;
            scope_type iType;
            child_list iChildren;
            index iChildIndex;
        };

        class function_scope : public scope<i_function_scope>
        {
        public:
            using scope::scope;
        public:
            i_function_signature const& function_signature() const final
            {
                return iFunctionSignature;
            }
            void set_function_signature(i_function_signature const& aSignature) final
            {
                iFunctionSignature = aSignature;
            }
        private:
            language::function_signature iFunctionSignature;
        };

        template <typename Base>
        inline i_scope& scope<Base>::create_child(i_scope_name const& aName, scope_type aType)
        {
            if (aType == scope_type::Namespace)
            {
                auto const indexed = iChildIndex.find(aName.to_std_string_view());
                if (indexed != iChildIndex.end())
                    return *indexed->second;
            }
            switch (aType)
            {
            case scope_type::Namespace:
                children().push_back(neolib::make_ref<scope<>>(*this, aName, aType));
                iChildIndex[children().back()->name().to_std_string_view()] = &*children().back();
                break;
            case scope_type::Class:
                children().push_back(neolib::make_ref<scope<>>(*this, aName, aType));
                break;
            case scope_type::Function:
                children().push_back(neolib::make_ref<function_scope, i_scope>(*this, aName, aType));
                break;
            case scope_type::Block:
                children().push_back(neolib::make_ref<scope<>>(*this, aName, aType));
                break;
            }
            return *children().back();
        }

    }
}