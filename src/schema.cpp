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
            iConceptLibraries{ aConceptLibraries },
            iRoot{ neolib::make_ref<schema_atom>() }
        {
            parse(aSchema.root(), *iRoot);
            resolve_references();
            if (!atom_references().empty())
            {
                std::vector<atom_reference_key_t> references;
                for (auto const& r : atom_references())
                    references.push_back(r.first);
                throw unresolved_references(std::move(references));
            }
        }

        meta const& schema::meta() const
        {
            return iMeta;
        }

        schema_keyword schema::keyword(const neolib::rjson_string& aSymbol)
        {
            static std::map<std::string, schema_keyword> sKeywords = 
            {
                { "meta", schema_keyword::Meta},
                { "libraries", schema_keyword::Libraries},
                { "is", schema_keyword::Is},
                { "done", schema_keyword::Done},
                { "next", schema_keyword::Next},
                { "default", schema_keyword::Default},
                { "expect", schema_keyword::Expect},
                { "tokens", schema_keyword::Tokens},
            };
            auto result = sKeywords.find(aSymbol);
            if (result != sKeywords.end())
                return result->second;
            return schema_keyword::Invalid;
        }

        void schema::parse(neolib::rjson_value const& aNode, i_atom& aAtom)
        {
            for (auto const& childNode : aNode)
            {
                auto default_method = [this](neolib::rjson_value const& aChildNode, i_atom& aParentAtom)
                {
                    auto newChild = aParentAtom.as_schema_atom().children().insert(
                        atom_ptr{ neolib::make_ref<schema_atom>(aParentAtom.as_schema_atom(), aChildNode.name()) }, atom_ptr{});
                    parse(aChildNode, *newChild->first());
                };
                static std::map<schema_keyword, std::function<void(neolib::rjson_value const&, i_atom&)>> const sMethods =
                {
                    { schema_keyword::Meta, [this, &default_method](neolib::rjson_value const& aChildNode, i_atom& aParentAtom)
                        { 
                            if (aChildNode.parent().is_root()) 
                                parse_meta(aChildNode); 
                            else 
                                default_method(aChildNode, aParentAtom); 
                        } },
                    { schema_keyword::Libraries, [this, &default_method](neolib::rjson_value const& aChildNode, i_atom& aParentAtom)
                        { 
                            if (aChildNode.parent().is_root()) 
                                parse_meta(aChildNode); 
                            else default_method(aChildNode, aParentAtom); 
                        } },
                    { schema_keyword::Is, [this](neolib::rjson_value const& aChildNode, i_atom& aParentAtom)
                        {
                            /* todo */
                        } },
                    { schema_keyword::Done, [this](neolib::rjson_value const& aChildNode, i_atom& aParentAtom)
                        {
                            /* todo */
                        } },
                    { schema_keyword::Next, [this](neolib::rjson_value const& aChildNode, i_atom& aParentAtom)
                        {
                            /* todo */
                        } },
                    { schema_keyword::Default, [this](neolib::rjson_value const& aChildNode, i_atom& aParentAtom)
                        {
                            /* todo */
                        } },
                    { schema_keyword::Expect, [this](neolib::rjson_value const& aChildNode, i_atom& aParentAtom)
                        { 
                            aParentAtom.as_schema_atom().expects().push_back(atom_ptr{});
                            add_rhs_atom_reference(aChildNode, aParentAtom, aParentAtom.as_schema_atom().expects().back());
                        } },
                    { schema_keyword::Tokens, [this](neolib::rjson_value const& aChildNode, i_atom& aParentAtom)
                        { 
                            parse_tokens(aChildNode, aParentAtom);
                        } }
                };
                auto method = (childNode.name_is_keyword() ? sMethods.find(keyword(childNode.name())) : sMethods.end());
                if (method != sMethods.end())
                    method->second(childNode, aAtom);
                else
                    default_method(childNode, aAtom);
            }
        }

        void schema::parse_meta(neolib::rjson_value const& aNode)
        {
            if (keyword(aNode.name()) == schema_keyword::Meta)
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
            else if (keyword(aNode.name()) == schema_keyword::Libraries)
            {
                for (auto const& library : aNode)
                    if (iConceptLibraries.find(neolib::string{ library.as<neolib::rjson_keyword>().text }) == iConceptLibraries.end())
                        throw std::runtime_error("concept library '" + library.as<neolib::rjson_keyword>().text + "' not found");
            }
        }

        void schema::parse_tokens(neolib::rjson_value const& aNode, i_atom& aAtom)
        {
            for (auto const& token : aNode)
            {
                switch (keyword(token.name()))
                {
                case schema_keyword::Expect:
                    aAtom.as_schema_atom().expects().push_back(atom_ptr{});
                    add_rhs_atom_reference(token, aAtom, aAtom.as_schema_atom().expects().back());
                    break;
                case schema_keyword::Default:
                    break;
                case schema_keyword::Invalid:
                    aAtom.as_schema_atom().tokens().push_back(schema_atom::tokens_t::concrete_value_type{});
                    add_lhs_atom_reference(token, aAtom, aAtom.as_schema_atom().tokens().back().first());
                    token.visit([this, &token, &aAtom](auto&& aNodeValue)
                    {
                        typedef typename std::remove_cv<typename std::remove_reference<decltype(aNodeValue)>::type>::type type_t;
                        if constexpr (std::is_same_v<type_t, neolib::rjson_keyword> || std::is_same_v<type_t, neolib::rjson_string>)
                            add_rhs_atom_reference(token, aAtom, aAtom.as_schema_atom().tokens().back().second());
                        else if constexpr (std::is_same_v<type_t, neolib::rjson_object>)
                            parse_tokens(token, *(aAtom.as_schema_atom().tokens().back().second() = neolib::make_ref<schema_atom>(aAtom.as_schema_atom(), token.name())));
                    }, false);
                    break;
                default:
                    throw std::runtime_error("unexpected keyword '" + token.name() + "' in token specification");
                    break;
                }
            }
        }

        std::string schema::fully_qualified_name(neolib::rjson_value const& aNode, const std::string& aRhs) const
        {
            for (auto n = &aNode; n->has_parent(); n = &n->parent())
                if (n->name_is_keyword() && keyword(n->name()) == schema_keyword::Tokens)
                    return fully_qualified_name(n->parent(), aRhs);
            if (aNode.is_root())
                return aRhs;
            return fully_qualified_name(aNode.parent(), aNode.name()) + (!aRhs.empty() && keyword(aRhs) == schema_keyword::Invalid ? "." + aRhs : std::string{});
        }

        const schema::atom_references_t& schema::atom_references() const
        {
            return iAtomReferences;
        }

        schema::atom_references_t& schema::atom_references()
        {
            return iAtomReferences;
        }

        void schema::add_lhs_atom_reference(neolib::rjson_value const& aNode, i_atom& aParentAtom, abstract_atom_ptr& aAtom)
        {
            
            if (aNode.name_is_keyword())
            {
                if (keyword(aNode.name()) == schema_keyword::Invalid)
                    atom_references()[atom_reference_key_t{ aNode.name(), fully_qualified_name(aNode) }].push_back(&aAtom);
                else
                    throw std::runtime_error("unexpected keyword '" + aNode.name() + "'");
            }
            else
                aAtom = neolib::make_ref<schema_atom>(aParentAtom.as_schema_atom(), aNode.name());
        }

        void schema::add_rhs_atom_reference(neolib::rjson_value const& aNode, i_atom& aParentAtom, abstract_atom_ptr& aAtom)
        {
            aNode.visit([this, &aNode, &aParentAtom, &aAtom](auto&& aNodeValue)
            {
                typedef typename std::remove_cv<typename std::remove_reference<decltype(aNodeValue)>::type>::type type_t;
                if constexpr (std::is_same_v<type_t, neolib::rjson_keyword>)
                {
                    switch (keyword(aNodeValue.text))
                    {
                    case schema_keyword::Invalid:
                        atom_references()[atom_reference_key_t{ aNodeValue.text, fully_qualified_name(aNode.parent(), aNodeValue.text) }].push_back(&aAtom);
                        break;
                    case schema_keyword::Done:
                        /* todo */
                        break;
                    case schema_keyword::Next:
                        /* todo */
                        break;
                    default:
                        throw std::runtime_error("unexpected keyword '" + aNodeValue.text + "'");
                    }
                }
                else if constexpr (std::is_same_v<type_t, neolib::rjson_string>)
                    aAtom = neolib::make_ref<schema_atom>(aParentAtom.as_schema_atom(), aNode.name());
            }, false);
        }

        void schema::resolve_references()
        {
            bool foundSome = false;
            do
            {
                foundSome = false;
                for (auto entry = atom_references().begin(); entry != atom_references().end();)
                {
                    auto result = find_concept(entry->first.first);
                    if (result != nullptr)
                    {
                        auto conceptAtom = neolib::make_ref<concept_atom>(result);
                        for (auto& r : entry->second)
                            *r = conceptAtom;
                        entry = atom_references().erase(entry);
                        foundSome = true;
                        continue;
                    }
                    ++entry;
                }
            } while (foundSome);
        }

        std::string schema::fully_qualified_name(const i_atom& aAtom) const
        {
            if (!aAtom.has_parent())
                return aAtom.symbol();
            return fully_qualified_name(aAtom.parent()) + aAtom.symbol();
        }

        neolib::ref_ptr<i_concept> schema::find_concept(const neolib::rjson_string& aSymbol) const
        {
            neolib::ref_ptr<i_concept> concept;
            for (auto const& cl : iConceptLibraries)
            {
                if (cl.second()->find_concept(neolib::string{ aSymbol }, concept))
                    return concept;
            }
            return nullptr;
        }
    }
}