/*
  schema.cpp

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

#include <neos/neos.hpp>
#include <neos/language/schema.hpp>

namespace neos
{
    namespace language
    {
        schema::schema(neolib::rjson const& aSchema, const concept_libraries_t& aConceptLibraries) :
            iMeta{ aSchema.root().as<neolib::rjson_object>().at("meta").as<neolib::rjson_object>().at("language").as<neolib::rjson_string>() },
            iConceptLibraries{ aConceptLibraries }
        {
            parse(aSchema.root(), neolib::make_ref<schema_atom>());
            if (!iAtomReferences.empty())
            {
                std::vector<std::string> references;
                for (auto const& r : iAtomReferences)
                    references.push_back(r.first);
                throw unresolved_references(std::move(references));
            }
        }

        meta const& schema::meta() const
        {
            return iMeta;
        }

        void schema::parse(neolib::rjson_value const& aNode, atom_ptr aAtom)
        {
            for (auto const& childNode : aNode)
            {
                auto default_method = [this](neolib::rjson_value const& aChildNode, atom_ptr aParentAtom)
                {
                    parse(aChildNode, neolib::make_ref<schema_atom>(*aParentAtom, aChildNode.name()));
                };
                static std::map<neolib::json_string, std::function<void(neolib::rjson_value const&, atom_ptr)>> const sMethods = 
                {
                    { "meta", [this, &default_method](neolib::rjson_value const& aChildNode, atom_ptr aParentAtom) 
                        { 
                            if (aChildNode.parent().is_root()) 
                                parse_meta(aChildNode); 
                            else 
                                default_method(aChildNode, aParentAtom); 
                        } },
                    { "libraries", [this, &default_method](neolib::rjson_value const& aChildNode, atom_ptr aParentAtom) 
                        { 
                            if (aChildNode.parent().is_root()) 
                                parse_meta(aChildNode); 
                            else default_method(aChildNode, aParentAtom); 
                        } },
                    { "expect" , [this](neolib::rjson_value const& aChildNode, atom_ptr aParentAtom) 
                        { 
                            aParentAtom->expects().push_back(atom_ptr{}); 
                            add_rhs_atom_reference(aChildNode, aParentAtom, aParentAtom->expects().back());
                        } },
                    { "tokens" , [this](neolib::rjson_value const& aChildNode, atom_ptr aParentAtom) 
                        { 
                            /* todo */ 
                        } }
                };
                auto method = (childNode.name_is_keyword() ? sMethods.find(childNode.name()) : sMethods.end());
                if (method != sMethods.end())
                    method->second(childNode, aAtom);
                else
                    default_method(childNode, aAtom);
            }
        }

        void schema::parse_meta(neolib::rjson_value const& aNode)
        {
            if (aNode.name() == "meta")
            {
                for (auto const& meta : aNode)
                {
                    if (meta.name() == "description")
                        iMeta.description = meta.as<neolib::rjson_string>();
                    else if (meta.name() == "copyright")
                        iMeta.copyright = meta.as<neolib::rjson_string>();
                    else if (meta.name() == "version")
                        iMeta.version = meta.as<neolib::rjson_string>();
                    else if (meta.name() == "sourcecode.file.extension")
                    {
                        if (meta.type() == neolib::json_type::String)
                            iMeta.sourcecodeFileExtension.push_back(meta.as<neolib::rjson_string>());
                        else
                            for (auto const& ext : meta)
                                iMeta.sourcecodeFileExtension.push_back(ext.as<neolib::rjson_string>());
                    }
                    else if (meta.name() == "sourcecode.module.package.specification.file.extension")
                    {
                        if (meta.type() == neolib::json_type::String)
                            iMeta.sourcecodeModulePackageSpecificationFileExtension.push_back(meta.as<neolib::rjson_string>());
                        else
                            for (auto const& ext : meta)
                                iMeta.sourcecodeModulePackageSpecificationFileExtension.push_back(ext.as<neolib::rjson_string>());
                    }
                    else if (meta.name() == "sourcecode.module.package.implementation.file.extension")
                    {
                        if (meta.type() == neolib::json_type::String)
                            iMeta.sourcecodeModulePackageImplementationFileExtension.push_back(meta.as<neolib::rjson_string>());
                        else
                            for (auto const& ext : meta)
                                iMeta.sourcecodeModulePackageImplementationFileExtension.push_back(ext.as<neolib::rjson_string>());
                    }
                }
            }
            else if (aNode.name() == "libraries")
            {
                for (auto const& library : aNode)
                    if (iConceptLibraries.find(neolib::string{ library.as<neolib::rjson_keyword>().text.as_string() }) == iConceptLibraries.end())
                        throw std::runtime_error("Concept library '" + library.as<neolib::rjson_keyword>().text.as_string() + "' not found");
            }
        }

        std::string schema::fully_qualified_name(neolib::rjson_value const& aNode, const std::string& aRhs) const
        {
            if (aNode.is_root())
                return aRhs;
            neolib::rjson_value const* n = &aNode;
            std::string fullyQualifiedName = n->name();
            while (n->has_parent() && !n->parent().is_root())
            {
                fullyQualifiedName = n->parent().name().as_string() + "." + fullyQualifiedName;
                n = &n->parent();
            }
            if (!aRhs.empty())
                return fullyQualifiedName + "." + aRhs;
            else
                return fullyQualifiedName;
        }

        const schema::atom_references_t& schema::atom_references() const
        {
            return iAtomReferences;
        }

        void schema::add_lhs_atom_reference(neolib::rjson_value const& aNode, atom_ptr aParentAtom, std::remove_pointer<atom_reference_t>::type& aAtom)
        {
            if (aNode.name_is_keyword())
                iAtomReferences[fully_qualified_name(aNode)].push_back(&aAtom);
            else
                aAtom = neolib::make_ref<schema_atom>(*aParentAtom, aNode.name());
        }

        void schema::add_rhs_atom_reference(neolib::rjson_value const& aNode, atom_ptr aParentAtom, std::remove_pointer<atom_reference_t>::type& aAtom)
        {
            aNode.visit([this, &aNode, &aAtom](auto&& aNodeValue)
            {
                typedef typename std::remove_cv<typename std::remove_reference<decltype(aNodeValue)>::type>::type type_t;
                if constexpr (std::is_same_v<type_t, neolib::rjson_keyword>)
                    iAtomReferences[fully_qualified_name(aNode.parent(), aNodeValue.text.as_string())].push_back(&aAtom);
                else if constexpr (std::is_same_v<type_t, neolib::rjson_string>)
                    iAtomReferences[fully_qualified_name(aNode.parent(), aNodeValue.as_string())].push_back(&aAtom);
            }, false);
        }
    }
}