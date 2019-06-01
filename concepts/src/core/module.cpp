/*
  module.cpp

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

#include <neos/language/concept.hpp>
#include <neos/language/compiler.hpp>
#include <neos/i_context.hpp>
#include "module.hpp"

namespace neos::concept::core
{   
    class source_package_name : public neos_concept<source_package_name>
    {
        // types
    public:
        typedef neolib::string representation_type;
        // construction
    public:
        source_package_name() :
            neos_concept{ "source.package.name", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return std::next(aSource);
        }
        // emit
    protected:
        bool can_fold(const i_concept& aRhs) const override
        {
            return aRhs.name() == "string.utf8";
        }
        i_concept* do_fold(i_context& aContext, const i_concept& aRhs) override
        {
            data<neolib::i_string>() = aRhs.data<neolib::i_string>();
            return this;
        }
    };

    class source_package_import : public neos_concept<source_package_import>
    {
        // types
    public:
        typedef neolib::string representation_type;
        // construction
    public:
        source_package_import() :
            neos_concept{ "source.package.import", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
        // emit
    protected:
        bool can_fold() const override
        {
            return !as_instance().data<representation_type>().empty();
        }
        i_concept* do_fold(i_context& aContext) override
        {
            language::source_fragment file{ neolib::string{data<neolib::string>()} };
            file.set_imported();
            aContext.load_fragment(file);
            aContext.compiler().compile(std::move(file));
            return nullptr;
        }
        bool can_fold(const i_concept& aRhs) const override
        {
            return aRhs.name() == "source.package.name";
        }
        i_concept* do_fold(i_context& aContext, const i_concept& aRhs) override
        {
            data<neolib::i_string>() = aRhs.data<neolib::i_string>();
            return this;
        }
    };

    class module_package_name : public neos_concept<module_package_name>
    {
        // types
    public:
        typedef neolib::string representation_type;
        // construction
    public:
        module_package_name() :
            neos_concept{ "module.package.name", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return std::next(aSource);
        }
        // emit
    protected:
        bool can_fold(const i_concept& aRhs) const override
        {
            return aRhs.name() == "string.utf8";
        }
        i_concept* do_fold(i_context& aContext, const i_concept& aRhs) override
        {
            data<neolib::i_string>() = aRhs.data<neolib::i_string>();
            return this;
        }
    };

    class module_package_import : public neos_concept<module_package_import>
    {
        // types
    public:
        typedef neolib::string representation_type;
        // construction
    public:
        module_package_import() :
            neos_concept{ "module.package.import", neos::language::emit_type::Infix }
        {
        }
        // parse
    public:
        source_iterator consume_token(neos::language::compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
        // emit
    protected:
        bool can_fold() const override
        {
            return !as_instance().data<representation_type>().empty();
        }
        i_concept* do_fold(i_context& aContext) override
        {
            // todo
            return nullptr;
        }
        bool can_fold(const i_concept& aRhs) const override
        {
            return aRhs.name() == "module.package.name";
        }
        i_concept* do_fold(i_context& aContext, const i_concept& aRhs) override
        {
            data<neolib::i_string>() = aRhs.data<neolib::i_string>();
            return this;
        }
    };

    module::module(neos::language::i_concept_library& aParent) :
        neos::language::concept_library
        { 
            aParent,
            library_id(), 
            aParent.uri().to_std_string(),
            library_name(), 
            "Core module concepts", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2019 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        concepts()[neolib::string{ "source.package.name" }] = neolib::make_ref<source_package_name>();
        concepts()[neolib::string{ "source.package.import" }] = neolib::make_ref<source_package_import>();
        concepts()[neolib::string{ "module.package.name" }] = neolib::make_ref<module_package_name>();
        concepts()[neolib::string{ "module.package.import" }] = neolib::make_ref<module_package_import>();
    }

    const std::string& module::library_name()
    {
        static const std::string sName = "neos.module";
        return sName;
    }

    const neolib::uuid& module::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("EBE0A8D9-AE7E-4379-8A5A-9A5CACD6FFE4");
        return sId;
    }
}

