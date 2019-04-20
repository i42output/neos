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
#include <neolib/recursion.hpp>
#include <neos/language/schema.hpp>

namespace neos
{
    namespace language
    {
        schema::schema(neolib::rjson const& aSource, const concept_libraries_t& aConceptLibraries) :
            iSource{ aSource },
            iMeta{ aSource.root().as<neolib::rjson_object>().at("meta").as<neolib::rjson_object>().at("language").as<neolib::rjson_string>() },
            iConceptLibraries{ aConceptLibraries },
            iRoot{ neolib::make_ref<schema_node_atom>() }
        {
            parse(aSource.root(), iRoot->as_schema_atom().as_schema_node_atom());
            resolve_references();
            if (!atom_references().empty())
            {
                std::vector<atom_reference_key_t> references;
                for (auto const& r : atom_references())
                    references.push_back(r.first);
                throw unresolved_references(std::move(references));
            }
        }

        i_schema_node_atom& schema::root() const
        {
            return iRoot->as_schema_atom().as_schema_node_atom();
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
                { "drain", schema_keyword::Drain},
                { "next", schema_keyword::Next},
                { "continue", schema_keyword::Continue},
                { "ignore", schema_keyword::Ignore},
                { "error", schema_keyword::Error},
                { "default", schema_keyword::Default},
                { "expect", schema_keyword::Expect},
                { "tokens", schema_keyword::Tokens},
            };
            auto result = sKeywords.find(aSymbol);
            if (result != sKeywords.end())
                return result->second;
            return schema_keyword::Invalid;
        }

        void schema::default_atom_handler(neolib::rjson_value const& aChildNode, i_schema_node_atom& aParentAtom)
        {
            if (aChildNode.name_is_keyword() && keyword(aChildNode.name()) != schema_keyword::Invalid)
                throw_error(aChildNode, "unexpected keyword '" + aChildNode.name() + "'");
            auto newChild = aParentAtom.children().insert(
                atom_ptr{ neolib::make_ref<schema_node_atom>(aParentAtom, aChildNode.name()) }, atom_ptr{});
            parse(aChildNode, newChild->first()->as_schema_atom().as_schema_node_atom());
        }

        schema::atom_handlers_t& schema::atom_handlers()
        {
            if (iAtomHandlers == std::nullopt)
            {
                iAtomHandlers = atom_handlers_t
                {
                    { schema_keyword::Meta, [this](neolib::rjson_value const& aChildNode, i_schema_node_atom& aParentAtom)
                    {
                        if (aChildNode.parent().is_root())
                            parse_meta(aChildNode);
                        else
                            default_atom_handler(aChildNode, aParentAtom);
                    } },
                    { schema_keyword::Meta, [this](neolib::rjson_value const& aChildNode, i_schema_node_atom& aParentAtom)
                    {
                        if (aChildNode.parent().is_root())
                            parse_meta(aChildNode);
                        else
                            default_atom_handler(aChildNode, aParentAtom);
                    } },
                    { schema_keyword::Libraries, [this](neolib::rjson_value const& aChildNode, i_schema_node_atom& aParentAtom)
                    {
                        if (aChildNode.parent().is_root())
                            parse_meta(aChildNode);
                        else
                            default_atom_handler(aChildNode, aParentAtom);
                    } },
                    { schema_keyword::Is, [this](neolib::rjson_value const& aChildNode, i_schema_node_atom& aParentAtom)
                    {
                        auto const& conceptName = aChildNode.as<neolib::rjson_keyword>().text;
                        auto concept = find_concept(conceptName);
                        if (concept != nullptr)
                            aParentAtom.is_a().push_back(concept);
                        else
                            throw_error(aChildNode, "concept '" + conceptName + "' not found");
                    } },
                    { schema_keyword::Default, [this](neolib::rjson_value const& aChildNode, i_schema_node_atom& aParentAtom)
                    {
                        parse_tokens(aChildNode, aParentAtom);
                    } },
                    { schema_keyword::Expect, [this](neolib::rjson_value const& aChildNode, i_schema_node_atom& aParentAtom)
                    {
                        aParentAtom.expects().push_back(atom_ptr{});
                        add_rhs_atom_reference(aChildNode, aParentAtom, aParentAtom.expects().back());
                    } },
                    { schema_keyword::Tokens, [this](neolib::rjson_value const& aChildNode, i_schema_node_atom& aParentAtom)
                    {
                        parse_tokens(aChildNode, aParentAtom);
                    } }
                };
            }
            return *iAtomHandlers;
        }

        void schema::parse(neolib::rjson_value const& aNode, i_schema_node_atom& aAtom)
        {
            _limit_recursion_(schema);
            for (auto const& childNode : aNode)
            {
                auto method = (childNode.name_is_keyword() ? atom_handlers().find(keyword(childNode.name())) : atom_handlers().end());
                if (method != atom_handlers().end())
                    method->second(childNode, aAtom);
                else
                    default_atom_handler(childNode, aAtom);
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
                        throw_error(aNode, "concept library '" + library.as<neolib::rjson_keyword>().text + "' not found");
            }
        }

        void schema::parse_tokens(neolib::rjson_value const& aNode, i_schema_node_atom& aAtom)
        {
            _limit_recursion_(schema);
            auto& result = aAtom.tokens();
            auto parse_token_value = [&aAtom, &result](schema& aSchema, auto&& aToken)
            {
                aToken.visit([&aSchema, &aToken, &aAtom, &result](auto&& aNodeValue)
                {
                    typedef typename std::remove_cv<typename std::remove_reference<decltype(aNodeValue)>::type>::type type_t;
                    if constexpr (std::is_same_v<type_t, neolib::rjson_keyword> || std::is_same_v<type_t, neolib::rjson_string>)
                        aSchema.add_rhs_atom_reference(aToken, aAtom, result.back().second());
                    else if constexpr (std::is_same_v<type_t, neolib::rjson_object>)
                    {
                        auto newNode = neolib::make_ref<schema_node_atom>(aAtom, aToken.name() + ".*", true);
                        result.back().second() = newNode;
                        aSchema.parse_tokens(aToken, *newNode);
                    }
                }, false);
            };
            std::map<const char*, neolib::rjson_value::const_iterator> tokensInDocumentOrder;
            for (auto iterToken = aNode.cbegin(); iterToken != aNode.cend(); ++iterToken)
            {
                auto const& token = *iterToken;
                tokensInDocumentOrder[&token.name()[0]] = iterToken;
            }
            for (auto iterToken = tokensInDocumentOrder.cbegin(); iterToken != tokensInDocumentOrder.cend(); ++iterToken)
            {
                auto const& token = *iterToken->second;
                switch (keyword(token.name()))
                {
                case schema_keyword::Expect:
                    aAtom.expects().push_back(atom_ptr{});
                    add_rhs_atom_reference(token, aAtom, aAtom.expects().back());
                    break;
                case schema_keyword::Default:
                    if (iterToken != std::prev(tokensInDocumentOrder.cend()))
                        throw_error(token, "default specifier must appear last in token specification block");
                    result.push_back(schema_node_atom::tokens_t::concrete_value_type{});
                    result.back().first() = neolib::make_ref<schema_terminal_atom>(aAtom, schema_terminal::Default);
                    parse_token_value(*this, token);
                    break;
                case schema_keyword::Invalid:
                    result.push_back(schema_node_atom::tokens_t::concrete_value_type{});
                    add_lhs_atom_reference(token, aAtom, result.back().first());
                    parse_token_value(*this, token);
                    break;
                default:
                    throw_error(aNode, "unexpected keyword '" + token.name() + "' in token specification");
                    break;
                }
            }
        }

        std::string schema::fully_qualified_name(neolib::rjson_value const& aNode) const
        {
            _limit_recursion_(schema);
            auto next_node = [&aNode]() -> neolib::rjson_value const&
            {
                for (auto n = &aNode; n->has_parent(); n = &n->parent())
                    if (n->name_is_keyword() && (keyword(n->name()) == schema_keyword::Tokens || keyword(n->name()) == schema_keyword::Default))
                        return n->parent();
                return aNode;
            };
            auto const& nextNode = next_node();
            if (nextNode.is_root())
                return std::string{};
            auto lhs = fully_qualified_name(nextNode.parent());
            return lhs + (!lhs.empty() ? "." : std::string{}) + nextNode.name();
        }

        std::string schema::fully_qualified_name(neolib::rjson_value const& aNode, const neolib::rjson_string& aLeafName) const
        {
            auto lhs = fully_qualified_name(aNode);
            return lhs + (!lhs.empty() ? "." : std::string{}) + aLeafName;
        }

        const schema::atom_references_t& schema::atom_references() const
        {
            return iAtomReferences;
        }

        schema::atom_references_t& schema::atom_references()
        {
            return iAtomReferences;
        }

        void schema::add_lhs_atom_reference(neolib::rjson_value const& aNode, i_schema_node_atom& aParentAtom, abstract_atom_ptr& aAtom)
        {
            if (aNode.name_is_keyword())
            {
                if (keyword(aNode.name()) == schema_keyword::Invalid)
                {
                    auto const key = atom_reference_key_t{ aNode.name(), fully_qualified_name(aNode.parent(), aNode.name()) };
                    atom_references()[key].push_back(&aAtom);
                }
                else
                    throw_error(aNode, "unexpected keyword '" + aNode.name() + "'");
            }
            else
                aAtom = neolib::make_ref<schema_terminal_atom>(aParentAtom, schema_terminal::String, aNode.name());
        }

        void schema::add_rhs_atom_reference(neolib::rjson_value const& aNode, i_schema_node_atom& aParentAtom, abstract_atom_ptr& aAtom)
        {
            aNode.visit([this, &aNode, &aParentAtom, &aAtom](auto&& aNodeValue)
            {
                typedef typename std::remove_cv<typename std::remove_reference<decltype(aNodeValue)>::type>::type type_t;
                if constexpr (std::is_same_v<type_t, neolib::rjson_keyword>)
                {
                    switch (keyword(aNodeValue.text))
                    {
                    case schema_keyword::Invalid:
                        {
                            auto const key = atom_reference_key_t{ aNodeValue.text, fully_qualified_name(aNode.parent(), aNodeValue.text) };
                            atom_references()[key].push_back(&aAtom);
                        }
                        break;
                    case schema_keyword::Done:
                        aAtom = neolib::make_ref<schema_terminal_atom>(aParentAtom, schema_terminal::Done);
                        break;
                    case schema_keyword::Drain:
                        aAtom = neolib::make_ref<schema_terminal_atom>(aParentAtom, schema_terminal::Drain);
                        break;
                    case schema_keyword::Next:
                        aAtom = neolib::make_ref<schema_terminal_atom>(aParentAtom, schema_terminal::Next);
                        break;
                    case schema_keyword::Continue:
                        aAtom = neolib::make_ref<schema_terminal_atom>(aParentAtom, schema_terminal::Continue);
                        break;
                    case schema_keyword::Ignore:
                        aAtom = neolib::make_ref<schema_terminal_atom>(aParentAtom, schema_terminal::Ignore);
                        break;
                    case schema_keyword::Error:
                        aAtom = neolib::make_ref<schema_terminal_atom>(aParentAtom, schema_terminal::Error);
                        break;
                    default:
                        throw_error(aNode, "unexpected keyword '" + aNodeValue.text + "'");
                    }
                }
                else if constexpr (std::is_same_v<type_t, neolib::rjson_string>)
                    aAtom = neolib::make_ref<schema_terminal_atom>(aParentAtom, schema_terminal::String, aNodeValue);
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
                    auto atom = leaf(entry->first.second, entry->first.first);
                    if (atom != nullptr)
                    {
                        for (auto& r : entry->second)
                            *r = atom;
                        entry = atom_references().erase(entry);
                        foundSome = true;
                        continue;
                    }
                    auto concept = find_concept(entry->first.first);
                    if (concept != nullptr)
                    {
                        auto conceptAtom = create_concept_atom(concept);
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
        
        schema::atom_ptr schema::leaf(const std::string& aStem, const neolib::rjson_string& aLeafName)
        {
            return leaf(iRoot->as_schema_atom().as_schema_node_atom(), aStem, aLeafName);
        }

        schema::atom_ptr schema::leaf(i_schema_node_atom& aNode, const std::string& aStem, const neolib::rjson_string& aLeafName)
        {
            _limit_recursion_(schema);
            if (aStem == aNode.symbol())
            {
                if (aLeafName == aStem)
                    return &aNode;
            }
            for (auto& concept : aNode.is_a())
            {
                if (concept->name() == neolib::string{ aLeafName })
                    return neolib::make_ref<concept_atom>(concept);
                auto const base = concept->name() + ".";
                auto matchingConcept = find_concept(base + aLeafName);
                if (matchingConcept == nullptr && aLeafName.find(base.to_std_string()) == 0)
                    matchingConcept = find_concept(aLeafName);
                if (matchingConcept != nullptr)
                    return create_concept_atom(matchingConcept);
            }
            auto delim = std::find(aStem.begin(), aStem.end(), '.');
            schema_node_atom key{ aNode, std::string{ aStem.begin(), delim } };
            auto child = aNode.children().find(neolib::ref_ptr<i_atom>{ key });
            if (child != aNode.children().end())
            {
                if (delim != aStem.end())
                {
                    auto newStem = std::string{ std::next(delim), aStem.end() };
                    return leaf(child->first()->as_schema_atom().as_schema_node_atom(), newStem, aLeafName != aStem ? aLeafName : neolib::rjson_string{ newStem });
                }
                else
                    return child->first();
            }
            if (aNode.has_parent())
                return leaf(aNode.parent().as_schema_atom().as_schema_node_atom(), aStem, aLeafName);
            return atom_ptr{};
        }

        schema::atom_ptr schema::create_concept_atom(const neolib::i_ref_ptr<i_concept>& aConcept)
        {
            auto iterConceptAtom = iConceptAtoms.find(&*aConcept);
            if (iterConceptAtom == iConceptAtoms.end())
                iterConceptAtom = iConceptAtoms.emplace(&*aConcept, neolib::make_ref<concept_atom>(aConcept)).first;
            return iterConceptAtom->second;
        }

        neolib::ref_ptr<i_concept> schema::find_concept(const std::string& aSymbol) const
        {
            neolib::ref_ptr<i_concept> concept;
            for (auto const& cl : iConceptLibraries)
            {
                if (cl.second()->find_concept(neolib::string{ aSymbol }, concept))
                    return concept;
            }
            return nullptr;
        }

        void schema::throw_error(neolib::rjson_value const& aNode, const std::string aErrorText)
        {
            if (aNode.has_name())
                throw std::runtime_error(iSource.to_error_text(&aNode.name()[0], aErrorText));
            throw std::runtime_error(aErrorText);
        }
   }
}