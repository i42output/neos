/*
  module.cpp

  Copyright (c) 2024 Leigh Johnston.  All Rights Reserved.

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

#include <neos/language/semantic_concept.hpp>
#include <neos/language/compiler.hpp>
#include <neos/i_context.hpp>
#include "module.hpp"

namespace neos::concepts::core
{   
    class source_package_name : public semantic_concept<source_package_name>
    {
        // types
    public:
        using data_type = neolib::string;
        // construction
    public:
        source_package_name() :
            semantic_concept{ "source.package.name", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            return aRhs.name() == "string.utf8";
        }
        // emit
    protected:
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            data<neolib::string>() = aRhs.data<neolib::i_string>();
            aResult = instance();
        }
    };

    class source_package_import : public semantic_concept<source_package_import>
    {
        // types
    public:
        using data_type = neolib::string;
        // construction
    public:
        source_package_import() :
            semantic_concept{ "source.package.import", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold() const override
        {
            return holds_data();
        }
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            return aRhs.name() == "source.package.name";
        }
        // emit
    protected:
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            language::source_fragment file{ data<neolib::string>() };
            file.set_imported();
            aContext.load_fragment(file);
            if (!aContext.compiler().compile(std::move(file)))
                throw std::runtime_error("failed to compile imported source fragment.");
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            data<neolib::string>() = aRhs.data<neolib::i_string>();
            aResult = instance();
        }
    };

    class module_package_name : public semantic_concept<module_package_name>
    {
        // types
    public:
        using data_type = neolib::string;
        // construction
    public:
        module_package_name() :
            semantic_concept{ "module.package.name", neos::language::emit_type::Infix }
        {
        }
        // emit
    public:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            return aRhs.name() == "string.utf8";
        }
        // emit
    protected:
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            data<neolib::string>() = aRhs.data<neolib::i_string>();
            aResult = instance();
        }
    };

    class module_package_import : public semantic_concept<module_package_import>
    {
        // types
    public:
        using data_type = neolib::string;
        // construction
    public:
        module_package_import() :
            semantic_concept{ "module.package.import", neos::language::emit_type::Infix }
        {
        }
        // emit
    protected:
        bool can_fold() const override
        {
            return holds_data();
        }
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            return aRhs.name() == "module.package.name";
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            data<neolib::string>() = aRhs.data<neolib::i_string>();
            aResult = instance();
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
            "Copyright (c) 2025 Leigh Johnston.  All Rights Reserved."
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

