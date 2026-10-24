/*
  pyexpat.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/pyexpat.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// The module pyexpat (CPython 3.13's pyexpat.c: ParserCreate, ErrorString, the xmlparser objects (their handlers, methods and
// attributes), ExpatError, the submodules errors and model, their behaviour and messages) with the bundled Expat
// (3rdparty/expat-2.8.5)

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include "expat_config.h"     // (XML_DTD, XML_GE and XML_NS: as Expat is built)
#include <expat.h>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            // (the handlers, in CPython's order (handler_info))
            enum handler_index
            {
                StartElement, EndElement, ProcessingInstruction, CharacterData, UnparsedEntityDecl, NotationDecl, StartNamespaceDecl, EndNamespaceDecl,
                Comment, StartCdataSection, EndCdataSection, Default, DefaultHandlerExpand, NotStandalone, ExternalEntityRef, StartDoctypeDecl,
                EndDoctypeDecl, EntityDecl, XmlDecl, ElementDecl, AttlistDecl, SkippedEntity, HandlerCount
            };

            constexpr int kCharacterDataBufferSize = 8192;
            constexpr int kMaxChunkSize = 1 << 20;
            constexpr int kReadSize = 2048;

            struct parser_object
            {
                header h;
                XML_Parser itself = nullptr;
                object* parent = nullptr;           ///< (a child parser's (ExternalEntityParserCreate): it uses its parent's XML_Parser)
                bool orderedAttributes = false;
                bool specifiedAttributes = false;
                bool inCallback = false;
                bool nsPrefixes = false;
                bool buffering = false;             ///< (buffer_text)
                std::vector<char> buffer;
                int bufferSize = kCharacterDataBufferSize;
                int bufferUsed = 0;
                bool reparseDeferralEnabled = true;
                object* intern = nullptr;
                std::array<object*, HandlerCount> handlers{};
                ~parser_object()
                {
                    for (auto& h : handlers)
                    {
                        release(h);
                        h = nullptr;
                    }
                    release(intern);
                    // (the parser before its parent: a child parser uses its parent's)
                    if (itself != nullptr)
                        XML_ParserFree(itself);
                    release(parent);
                }
            };

            neos_py_type make_parser_type();
            neos_py_type sParserType = make_parser_type();

            parser_object& as_parser(object* aObject) { return *reinterpret_cast<parser_object*>(aObject); }
            bool is_parser(object* aObject) { return aObject != nullptr && aObject->type == &sParserType.base; }

            object* expat_error() { return module_exception("xml.parsers.expat.ExpatError", { "Exception" }); }

            // --- errors

            // (ExpatError(message), with its code, lineno and offset: raised; null)
            std::nullptr_t set_xml_error(XML_Error aCode, XML_Size aLine, XML_Size aColumn, char const* aMessage)
            {
                std::string const text = aMessage != nullptr ? std::string{ aMessage } :
                    std::string{ XML_ErrorString(aCode) } + ": line " + std::to_string(aLine) + ", column " + std::to_string(aColumn);
                ref const cls{ expat_error() };
                ref const exception{ cls ? invoke(cls.get(), { make_str(text) }) : nullptr };
                if (!exception)
                    return nullptr;
                ref const code{ make_int(static_cast<long long>(aCode)) };
                ref const line{ make_int(static_cast<long long>(aLine)) };
                ref const column{ make_int(static_cast<long long>(aColumn)) };
                if (setattr(exception.get(), "code", code.get()) < 0 || setattr(exception.get(), "lineno", line.get()) < 0 ||
                    setattr(exception.get(), "offset", column.get()) < 0)
                    return nullptr;
                fn_raise(exception.get());
                return nullptr;
            }

            std::nullptr_t set_error(parser_object& self, XML_Error aCode, char const* aMessage = nullptr)
            {
                return set_xml_error(aCode, XML_GetCurrentLineNumber(self.itself), XML_GetCurrentColumnNumber(self.itself), aMessage);
            }

            // --- conversions (Expat's XML_Char is UTF-8)

            object* conv_string(XML_Char const* aText)
            {
                if (aText == nullptr)
                    return new_reference(none());
                return make_str(std::string{ aText });
            }

            object* conv_string(XML_Char const* aText, int aLength)
            {
                if (aText == nullptr)
                    return new_reference(none());
                return make_str(std::string{ aText, static_cast<std::size_t>(aLength) });
            }

            // (a name: the parser's intern dict's (the same str object for the same name))
            object* string_intern(parser_object& self, XML_Char const* aText)
            {
                ref result{ conv_string(aText) };
                if (!result || self.intern == nullptr || aText == nullptr)
                    return result.release_ownership();
                auto const position = dict_find(self.intern, result.get());
                if (position == -2)
                    return nullptr;
                if (position >= 0)
                    return new_reference(as<dict_object>(self.intern)->items[static_cast<std::size_t>(position)].second);
                if (dict_set(self.intern, result.get(), result.get()) < 0)
                    return nullptr;
                return result.release_ownership();
            }

            // --- the handlers' trampolines

            void set_handler(parser_object& self, int aIndex, bool aOn);
            void clear_handlers(parser_object& self);

            int error_external_entity_ref_handler(XML_Parser, XML_Char const*, XML_Char const*, XML_Char const*, XML_Char const*) { return 0; }
            void noop_character_data_handler(void*, XML_Char const*, int) {}

            // (an exception in a handler: no more of them (the parse ends as soon as it can))
            void flag_error(parser_object& self)
            {
                clear_handlers(self);
                XML_SetExternalEntityRefHandler(self.itself, error_external_entity_ref_handler);
            }

            // (a handler called with its arguments (which it steals): its result (null: an exception, and the parser stopped))
            object* call_handler(parser_object& self, int aIndex, std::vector<object*> aArgs)
            {
                for (auto a : aArgs)
                    if (a == nullptr)
                    {
                        for (auto b : aArgs)
                            release(b);
                        return nullptr;
                    }
                ref const handler{ new_reference(self.handlers[aIndex]) };
                self.inCallback = true;
                ref result{ invoke(handler.get(), std::move(aArgs)) };
                self.inCallback = false;
                if (!result)
                    XML_StopParser(self.itself, XML_FALSE);
                return result.release_ownership();
            }

            // (0, or -1: an exception (handled: flag_error))
            int call_character_handler(parser_object& self, XML_Char const* aText, int aLength)
            {
                if (self.handlers[CharacterData] == nullptr)
                    return -1;
                ref const result{ call_handler(self, CharacterData, { conv_string(aText, aLength) }) };
                if (!result)
                {
                    flag_error(self);
                    XML_SetCharacterDataHandler(self.itself, noop_character_data_handler);
                    return -1;
                }
                return 0;
            }

            int flush_character_buffer(parser_object& self)
            {
                if (!self.buffering || self.bufferUsed == 0)
                    return 0;
                auto const rc = call_character_handler(self, self.buffer.data(), self.bufferUsed);
                self.bufferUsed = 0;
                return rc;
            }

            void character_data_handler(void* aUserData, XML_Char const* aData, int aLength)
            {
                auto& self = *static_cast<parser_object*>(aUserData);
                if (failed())
                    return;
                if (!self.buffering)
                {
                    call_character_handler(self, aData, aLength);
                    return;
                }
                if (aLength > self.bufferSize - self.bufferUsed)
                {
                    if (flush_character_buffer(self) < 0)
                        return;
                    // (the handler might have changed: the rest dropped if there is none)
                    if (self.handlers[CharacterData] == nullptr)
                        return;
                }
                if (aLength > self.bufferSize)
                {
                    call_character_handler(self, aData, aLength);
                    self.bufferUsed = 0;
                }
                else
                {
                    std::memcpy(self.buffer.data() + self.bufferUsed, aData, static_cast<std::size_t>(aLength));
                    self.bufferUsed += aLength;
                }
            }

            // (a handler's call (a void handler's): if it has one; the character data buffered so far first)
            template <typename F>
            void void_handler(void* aUserData, int aIndex, F const& aArguments)
            {
                auto& self = *static_cast<parser_object*>(aUserData);
                if (self.handlers[aIndex] == nullptr || failed())
                    return;
                if (flush_character_buffer(self) < 0)
                    return;
                ref const result{ call_handler(self, aIndex, aArguments(self)) };
                if (!result)
                    flag_error(self);
            }

            // (an int handler's: its result's value)
            template <typename F>
            int int_handler(parser_object& self, int aIndex, F const& aArguments)
            {
                if (self.handlers[aIndex] == nullptr || failed())
                    return 0;
                if (flush_character_buffer(self) < 0)
                    return 0;
                ref const result{ call_handler(self, aIndex, aArguments(self)) };
                if (!result)
                {
                    flag_error(self);
                    return 0;
                }
                long long value = 0;
                if (!to_index(result.get(), value))
                    return -1;
                return static_cast<int>(value);
            }

            void start_element_handler(void* aUserData, XML_Char const* aName, XML_Char const** aAttributes)
            {
                auto& self = *static_cast<parser_object*>(aUserData);
                if (self.handlers[StartElement] == nullptr || failed())
                    return;
                if (flush_character_buffer(self) < 0)
                    return;
                // (the attributes (names and values): the specified ones, or all of them)
                int max = 0;
                if (self.specifiedAttributes)
                    max = XML_GetSpecifiedAttributeCount(self.itself);
                else
                    while (aAttributes[max] != nullptr)
                        max += 2;
                ref container{ self.orderedAttributes ? make_list() : make_dict() };
                for (int i = 0; i < max; i += 2)
                {
                    ref const n{ string_intern(self, aAttributes[i]) };
                    ref const v{ n ? conv_string(aAttributes[i + 1]) : nullptr };
                    if (!v)
                    {
                        flag_error(self);
                        return;
                    }
                    if (self.orderedAttributes)
                    {
                        as<list_object>(container.get())->items.push_back(new_reference(n.get()));
                        as<list_object>(container.get())->items.push_back(new_reference(v.get()));
                    }
                    else if (dict_set(container.get(), n.get(), v.get()) < 0)
                    {
                        flag_error(self);
                        return;
                    }
                }
                ref const result{ call_handler(self, StartElement, { string_intern(self, aName), container.release_ownership() }) };
                if (!result)
                    flag_error(self);
            }

            void end_element_handler(void* aUserData, XML_Char const* aName)
            {
                void_handler(aUserData, EndElement, [&](parser_object& self) { return std::vector<object*>{ string_intern(self, aName) }; });
            }

            void processing_instruction_handler(void* aUserData, XML_Char const* aTarget, XML_Char const* aData)
            {
                void_handler(aUserData, ProcessingInstruction, [&](parser_object& self) { return std::vector<object*>{ string_intern(self, aTarget), conv_string(aData) }; });
            }

            void unparsed_entity_decl_handler(void* aUserData, XML_Char const* aEntityName, XML_Char const* aBase, XML_Char const* aSystemId,
                XML_Char const* aPublicId, XML_Char const* aNotationName)
            {
                void_handler(aUserData, UnparsedEntityDecl, [&](parser_object& self)
                    {
                        return std::vector<object*>{ string_intern(self, aEntityName), string_intern(self, aBase), string_intern(self, aSystemId),
                            string_intern(self, aPublicId), string_intern(self, aNotationName) };
                    });
            }

            void entity_decl_handler(void* aUserData, XML_Char const* aEntityName, int aIsParameterEntity, XML_Char const* aValue, int aValueLength,
                XML_Char const* aBase, XML_Char const* aSystemId, XML_Char const* aPublicId, XML_Char const* aNotationName)
            {
                void_handler(aUserData, EntityDecl, [&](parser_object& self)
                    {
                        return std::vector<object*>{ string_intern(self, aEntityName), make_int(aIsParameterEntity), conv_string(aValue, aValueLength),
                            string_intern(self, aBase), string_intern(self, aSystemId), string_intern(self, aPublicId), string_intern(self, aNotationName) };
                    });
            }

            void xml_decl_handler(void* aUserData, XML_Char const* aVersion, XML_Char const* aEncoding, int aStandalone)
            {
                void_handler(aUserData, XmlDecl, [&](parser_object&) { return std::vector<object*>{ conv_string(aVersion), conv_string(aEncoding), make_int(aStandalone) }; });
            }

            // (a content model: (type, quant, name, children))
            object* conv_content_model(XML_Content const& aModel, int aDepth)
            {
                if (aDepth > 1000)
                    return error("RecursionError", "maximum recursion depth exceeded in conv_content_model");
                std::vector<object*> children;
                for (unsigned i = 0u; i < aModel.numchildren; ++i)
                {
                    auto const child = conv_content_model(aModel.children[i], aDepth + 1);
                    if (child == nullptr)
                    {
                        for (auto c : children)
                            release(c);
                        return nullptr;
                    }
                    children.push_back(child);
                }
                return make_tuple({ make_int(static_cast<long long>(aModel.type)), make_int(static_cast<long long>(aModel.quant)), conv_string(aModel.name),
                    make_tuple(std::move(children)) });
            }

            void element_decl_handler(void* aUserData, XML_Char const* aName, XML_Content* aModel)
            {
                auto& self = *static_cast<parser_object*>(aUserData);
                if (self.handlers[ElementDecl] != nullptr && !failed() && flush_character_buffer(self) >= 0)
                {
                    ref model{ conv_content_model(*aModel, 0) };
                    if (!model)
                        flag_error(self);
                    else
                    {
                        ref const result{ call_handler(self, ElementDecl, { string_intern(self, aName), model.release_ownership() }) };
                        if (!result)
                            flag_error(self);
                    }
                }
                XML_FreeContentModel(self.itself, aModel);
            }

            void attlist_decl_handler(void* aUserData, XML_Char const* aElementName, XML_Char const* aAttributeName, XML_Char const* aAttributeType,
                XML_Char const* aDefault, int aIsRequired)
            {
                void_handler(aUserData, AttlistDecl, [&](parser_object& self)
                    {
                        return std::vector<object*>{ string_intern(self, aElementName), string_intern(self, aAttributeName), conv_string(aAttributeType),
                            conv_string(aDefault), make_int(aIsRequired) };
                    });
            }

            void skipped_entity_handler(void* aUserData, XML_Char const* aEntityName, int aIsParameterEntity)
            {
                void_handler(aUserData, SkippedEntity, [&](parser_object& self) { return std::vector<object*>{ string_intern(self, aEntityName), make_int(aIsParameterEntity) }; });
            }

            void notation_decl_handler(void* aUserData, XML_Char const* aNotationName, XML_Char const* aBase, XML_Char const* aSystemId, XML_Char const* aPublicId)
            {
                void_handler(aUserData, NotationDecl, [&](parser_object& self)
                    {
                        return std::vector<object*>{ string_intern(self, aNotationName), string_intern(self, aBase), string_intern(self, aSystemId),
                            string_intern(self, aPublicId) };
                    });
            }

            void start_namespace_decl_handler(void* aUserData, XML_Char const* aPrefix, XML_Char const* aUri)
            {
                void_handler(aUserData, StartNamespaceDecl, [&](parser_object& self) { return std::vector<object*>{ string_intern(self, aPrefix), string_intern(self, aUri) }; });
            }

            void end_namespace_decl_handler(void* aUserData, XML_Char const* aPrefix)
            {
                void_handler(aUserData, EndNamespaceDecl, [&](parser_object& self) { return std::vector<object*>{ string_intern(self, aPrefix) }; });
            }

            void comment_handler(void* aUserData, XML_Char const* aData)
            {
                void_handler(aUserData, Comment, [&](parser_object&) { return std::vector<object*>{ conv_string(aData) }; });
            }

            void start_cdata_section_handler(void* aUserData)
            {
                void_handler(aUserData, StartCdataSection, [](parser_object&) { return std::vector<object*>{}; });
            }

            void end_cdata_section_handler(void* aUserData)
            {
                void_handler(aUserData, EndCdataSection, [](parser_object&) { return std::vector<object*>{}; });
            }

            void default_handler(void* aUserData, XML_Char const* aText, int aLength)
            {
                void_handler(aUserData, Default, [&](parser_object&) { return std::vector<object*>{ conv_string(aText, aLength) }; });
            }

            void default_handler_expand(void* aUserData, XML_Char const* aText, int aLength)
            {
                void_handler(aUserData, DefaultHandlerExpand, [&](parser_object&) { return std::vector<object*>{ conv_string(aText, aLength) }; });
            }

            int not_standalone_handler(void* aUserData)
            {
                return int_handler(*static_cast<parser_object*>(aUserData), NotStandalone, [](parser_object&) { return std::vector<object*>{}; });
            }

            int external_entity_ref_handler(XML_Parser aParser, XML_Char const* aContext, XML_Char const* aBase, XML_Char const* aSystemId,
                XML_Char const* aPublicId)
            {
                return int_handler(*static_cast<parser_object*>(XML_GetUserData(aParser)), ExternalEntityRef, [&](parser_object& self)
                    {
                        return std::vector<object*>{ conv_string(aContext), string_intern(self, aBase), string_intern(self, aSystemId),
                            string_intern(self, aPublicId) };
                    });
            }

            void start_doctype_decl_handler(void* aUserData, XML_Char const* aDoctypeName, XML_Char const* aSystemId, XML_Char const* aPublicId,
                int aHasInternalSubset)
            {
                void_handler(aUserData, StartDoctypeDecl, [&](parser_object& self)
                    {
                        return std::vector<object*>{ string_intern(self, aDoctypeName), string_intern(self, aSystemId), string_intern(self, aPublicId),
                            make_int(aHasInternalSubset) };
                    });
            }

            void end_doctype_decl_handler(void* aUserData)
            {
                void_handler(aUserData, EndDoctypeDecl, [](parser_object&) { return std::vector<object*>{}; });
            }

            // (the handlers' names, and their setters (the trampoline, or none))
            struct handler_info
            {
                char const* name;
                void (*set)(XML_Parser aParser, bool aOn);
            };

            handler_info const sHandlers[HandlerCount] =
            {
                { "StartElementHandler", [](XML_Parser p, bool on) { XML_SetStartElementHandler(p, on ? start_element_handler : nullptr); } },
                { "EndElementHandler", [](XML_Parser p, bool on) { XML_SetEndElementHandler(p, on ? end_element_handler : nullptr); } },
                { "ProcessingInstructionHandler", [](XML_Parser p, bool on) { XML_SetProcessingInstructionHandler(p, on ? processing_instruction_handler : nullptr); } },
                { "CharacterDataHandler", [](XML_Parser p, bool on) { XML_SetCharacterDataHandler(p, on ? character_data_handler : nullptr); } },
                { "UnparsedEntityDeclHandler", [](XML_Parser p, bool on) { XML_SetUnparsedEntityDeclHandler(p, on ? unparsed_entity_decl_handler : nullptr); } },
                { "NotationDeclHandler", [](XML_Parser p, bool on) { XML_SetNotationDeclHandler(p, on ? notation_decl_handler : nullptr); } },
                { "StartNamespaceDeclHandler", [](XML_Parser p, bool on) { XML_SetStartNamespaceDeclHandler(p, on ? start_namespace_decl_handler : nullptr); } },
                { "EndNamespaceDeclHandler", [](XML_Parser p, bool on) { XML_SetEndNamespaceDeclHandler(p, on ? end_namespace_decl_handler : nullptr); } },
                { "CommentHandler", [](XML_Parser p, bool on) { XML_SetCommentHandler(p, on ? comment_handler : nullptr); } },
                { "StartCdataSectionHandler", [](XML_Parser p, bool on) { XML_SetStartCdataSectionHandler(p, on ? start_cdata_section_handler : nullptr); } },
                { "EndCdataSectionHandler", [](XML_Parser p, bool on) { XML_SetEndCdataSectionHandler(p, on ? end_cdata_section_handler : nullptr); } },
                { "DefaultHandler", [](XML_Parser p, bool on) { XML_SetDefaultHandler(p, on ? default_handler : nullptr); } },
                { "DefaultHandlerExpand", [](XML_Parser p, bool on) { XML_SetDefaultHandlerExpand(p, on ? default_handler_expand : nullptr); } },
                { "NotStandaloneHandler", [](XML_Parser p, bool on) { XML_SetNotStandaloneHandler(p, on ? not_standalone_handler : nullptr); } },
                { "ExternalEntityRefHandler", [](XML_Parser p, bool on) { XML_SetExternalEntityRefHandler(p, on ? external_entity_ref_handler : nullptr); } },
                { "StartDoctypeDeclHandler", [](XML_Parser p, bool on) { XML_SetStartDoctypeDeclHandler(p, on ? start_doctype_decl_handler : nullptr); } },
                { "EndDoctypeDeclHandler", [](XML_Parser p, bool on) { XML_SetEndDoctypeDeclHandler(p, on ? end_doctype_decl_handler : nullptr); } },
                { "EntityDeclHandler", [](XML_Parser p, bool on) { XML_SetEntityDeclHandler(p, on ? entity_decl_handler : nullptr); } },
                { "XmlDeclHandler", [](XML_Parser p, bool on) { XML_SetXmlDeclHandler(p, on ? xml_decl_handler : nullptr); } },
                { "ElementDeclHandler", [](XML_Parser p, bool on) { XML_SetElementDeclHandler(p, on ? element_decl_handler : nullptr); } },
                { "AttlistDeclHandler", [](XML_Parser p, bool on) { XML_SetAttlistDeclHandler(p, on ? attlist_decl_handler : nullptr); } },
                { "SkippedEntityHandler", [](XML_Parser p, bool on) { XML_SetSkippedEntityHandler(p, on ? skipped_entity_handler : nullptr); } }
            };

            void set_handler(parser_object& self, int aIndex, bool aOn)
            {
                sHandlers[aIndex].set(self.itself, aOn);
            }

            void clear_handlers(parser_object& self)
            {
                for (int i = 0; i < HandlerCount; ++i)
                {
                    release(self.handlers[i]);
                    self.handlers[i] = nullptr;
                    set_handler(self, i, false);
                }
            }

            int handler_index_of(char const* aName)
            {
                for (int i = 0; i < HandlerCount; ++i)
                    if (std::strcmp(sHandlers[i].name, aName) == 0)
                        return i;
                return -1;
            }

            // --- unknown encodings (a single byte encoding that a codec has: its 256 characters)

            int unknown_encoding_handler(void*, XML_Char const* aName, XML_Encoding* aInfo)
            {
                if (failed())
                    return XML_STATUS_ERROR;
                std::string bytes(256u, '\0');
                for (int i = 0; i < 256; ++i)
                    bytes[static_cast<std::size_t>(i)] = static_cast<char>(i);
                ref const data{ make_bytes(bytes) };
                ref const decoded{ call_method_named(data.get(), "decode", { make_str(aName), make_str("replace") }) };
                if (!decoded)
                    return XML_STATUS_ERROR;
                auto const characters = to_code_points(as<str_object>(decoded.get())->value);
                if (characters.size() != 256u)
                {
                    error("ValueError", "multi-byte encodings are not supported");
                    return XML_STATUS_ERROR;
                }
                for (std::size_t i = 0u; i < 256u; ++i)
                    aInfo->map[i] = characters[i] != U'\xFFFD' ? static_cast<int>(characters[i]) : -1;
                aInfo->data = nullptr;
                aInfo->convert = nullptr;
                aInfo->release = nullptr;
                return XML_STATUS_OK;
            }

            // --- xmlparser objects

            object* new_parser(char const* aEncoding, char const* aNamespaceSeparator, object* aIntern)
            {
                auto* const p = new parser_object{ { { &sParserType.base } } };
                ref result{ &p->h.base };
                p->intern = aIntern != nullptr ? new_reference(aIntern) : nullptr;
                p->itself = XML_ParserCreate_MM(aEncoding, nullptr, aNamespaceSeparator);
                if (p->itself == nullptr)
                    return error("RuntimeError", "XML_ParserCreate failed");
                // (the hash salt: random)
                {
                    static std::random_device sDevice;
                    std::array<std::uint8_t, 16> salt{};
                    for (auto& b : salt)
                        b = static_cast<std::uint8_t>(sDevice());
                    XML_SetHashSalt16Bytes(p->itself, salt.data());
                }
                XML_SetUserData(p->itself, p);
                XML_SetUnknownEncodingHandler(p->itself, unknown_encoding_handler, nullptr);
                return result.release_ownership();
            }

            object* get_parse_result(parser_object& self, int aResult)
            {
                if (failed())
                    return nullptr;
                if (aResult == 0)
                    return set_error(self, XML_GetErrorCode(self.itself));
                if (flush_character_buffer(self) < 0)
                    return nullptr;
                return make_int(aResult);
            }

            object* parser_parse(parser_object& self, object* aData, object* aIsFinal)
            {
                int isFinal = 0;
                if (aIsFinal != nullptr && (isFinal = truth(aIsFinal)) < 0)
                    return nullptr;
                std::string_view data;
                std::string text;
                if (kind_of(aData) == NEOS_PY_STR || kind_of(builtin_value(aData)) == NEOS_PY_STR)
                {
                    text = as<str_object>(kind_of(aData) == NEOS_PY_STR ? aData : builtin_value(aData))->value;
                    data = text;
                    // (str: UTF-8 (the document's encoding declaration ignored))
                    (void)XML_SetEncoding(self.itself, "utf-8");
                }
                else if (!bytes_like(aData, data))
                    return error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(aData) + "'");
                int rc = 1;
                while (data.size() > static_cast<std::size_t>(kMaxChunkSize))
                {
                    rc = XML_Parse(self.itself, data.data(), kMaxChunkSize, 0);
                    if (rc == 0)
                        return get_parse_result(self, rc);
                    data.remove_prefix(static_cast<std::size_t>(kMaxChunkSize));
                }
                rc = XML_Parse(self.itself, data.data(), static_cast<int>(data.size()), isFinal);
                return get_parse_result(self, rc);
            }

            object* parser_parse_file(parser_object& self, object* aFile)
            {
                ref const read{ getattr(aFile, "read") };
                if (!read)
                {
                    clear_error();
                    return error("TypeError", "argument must have 'read' attribute");
                }
                int rv = 1;
                for (;;)
                {
                    auto const buffer = XML_GetBuffer(self.itself, kReadSize);
                    if (buffer == nullptr)
                        return get_parse_result(self, 0);
                    ref const chunk{ invoke(read.get(), { make_int(kReadSize) }) };
                    if (!chunk)
                        return nullptr;
                    std::string_view data;
                    // (bytes or bytearray (as CPython's readinst))
                    auto const type = std::string_view{ type_name(chunk.get()) };
                    if ((type != "bytes" && type != "bytearray") || !bytes_like(chunk.get(), data))
                        return error("TypeError", std::string{ "read() did not return a bytes object (type=" } + type_name(chunk.get()) + ")");
                    if (data.size() > static_cast<std::size_t>(kReadSize))
                        return error("ValueError", "read() returned too much data: " + std::to_string(kReadSize) + " bytes requested, " +
                            std::to_string(data.size()) + " returned");
                    std::memcpy(buffer, data.data(), data.size());
                    auto const bytesRead = static_cast<int>(data.size());
                    rv = XML_ParseBuffer(self.itself, bytesRead, bytesRead == 0);
                    if (failed())
                        return nullptr;
                    if (rv == 0 || bytesRead == 0)
                        break;
                }
                return get_parse_result(self, rv);
            }

            object* parser_external_entity_parser_create(object* aSelf, object* aContext, object* aEncoding)
            {
                auto& self = as_parser(aSelf);
                std::string context;
                std::string encoding;
                if (kind_of(aContext) != NEOS_PY_NONE)
                {
                    if (kind_of(aContext) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "ExternalEntityParserCreate() argument 1 must be str or None, not " } + type_name(aContext));
                    context = as<str_object>(aContext)->value;
                }
                if (aEncoding != nullptr)
                {
                    if (kind_of(aEncoding) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "ExternalEntityParserCreate() argument 2 must be str, not " } + type_name(aEncoding));
                    encoding = as<str_object>(aEncoding)->value;
                }
                auto* const p = new parser_object{ { { &sParserType.base } } };
                ref result{ &p->h.base };
                p->bufferSize = self.bufferSize;
                p->orderedAttributes = self.orderedAttributes;
                p->specifiedAttributes = self.specifiedAttributes;
                p->nsPrefixes = self.nsPrefixes;
                p->reparseDeferralEnabled = self.reparseDeferralEnabled;
                p->itself = XML_ExternalEntityParserCreate(self.itself, kind_of(aContext) == NEOS_PY_NONE ? nullptr : context.c_str(),
                    aEncoding != nullptr ? encoding.c_str() : nullptr);
                // (the child uses its parent's XML_Parser: it keeps its parent alive)
                p->parent = new_reference(aSelf);
                p->intern = self.intern != nullptr ? new_reference(self.intern) : nullptr;
                if (self.buffering)
                {
                    p->buffering = true;
                    p->buffer.resize(static_cast<std::size_t>(p->bufferSize));
                }
                if (p->itself == nullptr)
                    return error("MemoryError", "");
                XML_SetUserData(p->itself, p);
                for (int i = 0; i < HandlerCount; ++i)
                    if (self.handlers[i] != nullptr)
                    {
                        p->handlers[i] = new_reference(self.handlers[i]);
                        set_handler(*p, i, true);
                    }
                return result.release_ownership();
            }

            object* parser_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::string_view const name{ static_cast<char const*>(aData) };
                auto const& args = as<tuple_object>(aArgs)->items;
                auto const selfObject = args[0];
                auto& self = as_parser(selfObject);
                std::vector<object*> const rest{ args.begin() + 1, args.end() };
                std::vector<object*> a;
                auto const fn = std::string{ name };
                if (name == "Parse")
                    return parse_arguments("Parse", rest, aKwargs, { "data", "isfinal" }, 1u, 2u, a) ? parser_parse(self, a[0], a[1]) : nullptr;
                if (name == "ParseFile")
                    return parse_arguments("ParseFile", rest, aKwargs, { "file" }, 1u, 1u, a) ? parser_parse_file(self, a[0]) : nullptr;
                if (name == "SetBase")
                {
                    if (!parse_arguments("SetBase", rest, aKwargs, { "base" }, 1u, 1u, a))
                        return nullptr;
                    if (kind_of(a[0]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "SetBase() argument must be str, not " } + type_name(a[0]));
                    if (!XML_SetBase(self.itself, as<str_object>(a[0])->value.c_str()))
                        return error("MemoryError", "");
                    return new_reference(none());
                }
                if (name == "GetBase")
                    return parse_arguments("GetBase", rest, aKwargs, {}, 0u, 0u, a) ? conv_string(XML_GetBase(self.itself)) : nullptr;
                if (name == "GetInputContext")
                {
                    if (!parse_arguments("GetInputContext", rest, aKwargs, {}, 0u, 0u, a))
                        return nullptr;
                    if (self.inCallback)
                    {
                        int offset = 0, size = 0;
                        if (auto const buffer = XML_GetInputContext(self.itself, &offset, &size); buffer != nullptr)
                            return make_bytes(std::string{ buffer + offset, static_cast<std::size_t>(size - offset) });
                    }
                    return new_reference(none());
                }
                if (name == "ExternalEntityParserCreate")
                    return parse_arguments("ExternalEntityParserCreate", rest, aKwargs, { "context", "encoding" }, 1u, 2u, a) ?
                        parser_external_entity_parser_create(selfObject, a[0], a[1]) : nullptr;
                if (name == "SetParamEntityParsing")
                {
                    long long flag = 0;
                    if (!parse_arguments("SetParamEntityParsing", rest, aKwargs, { "flag" }, 1u, 1u, a) || !to_index(a[0], flag))
                        return nullptr;
                    return make_int(XML_SetParamEntityParsing(self.itself, static_cast<XML_ParamEntityParsing>(flag)));
                }
                if (name == "UseForeignDTD")
                {
                    if (!parse_arguments("UseForeignDTD", rest, aKwargs, { "flag" }, 0u, 1u, a))
                        return nullptr;
                    int flag = 1;
                    if (a[0] != nullptr && (flag = truth(a[0])) < 0)
                        return nullptr;
                    auto const rc = XML_UseForeignDTD(self.itself, flag ? XML_TRUE : XML_FALSE);
                    if (rc != XML_ERROR_NONE)
                        return set_error(self, rc);
                    return new_reference(none());
                }
                if (name == "SetReparseDeferralEnabled")
                {
                    if (!parse_arguments("SetReparseDeferralEnabled", rest, aKwargs, { "enabled" }, 1u, 1u, a))
                        return nullptr;
                    auto const enabled = truth(a[0]);
                    if (enabled < 0)
                        return nullptr;
                    XML_SetReparseDeferralEnabled(self.itself, enabled ? XML_TRUE : XML_FALSE);
                    self.reparseDeferralEnabled = enabled != 0;
                    return new_reference(none());
                }
                if (name == "GetReparseDeferralEnabled")
                    return parse_arguments("GetReparseDeferralEnabled", rest, aKwargs, {}, 0u, 0u, a) ? make_bool(self.reparseDeferralEnabled) : nullptr;
                // (the protections' thresholds and amplification factors (a root parser's))
                bool const threshold = name == "SetBillionLaughsAttackProtectionActivationThreshold" || name == "SetAllocTrackerActivationThreshold";
                if (threshold)
                {
                    long long value = 0;
                    if (!parse_arguments(fn.c_str(), rest, aKwargs, { "threshold" }, 1u, 1u, a) || !to_index(a[0], value))
                        return nullptr;
                    if (value < 0)
                        return error("OverflowError", "can't convert negative int to unsigned");
                    auto const ok = name == "SetAllocTrackerActivationThreshold" ?
                        XML_SetAllocTrackerActivationThreshold(self.itself, static_cast<unsigned long long>(value)) :
                        XML_SetBillionLaughsAttackProtectionActivationThreshold(self.itself, static_cast<unsigned long long>(value));
                    if (ok == XML_TRUE)
                        return new_reference(none());
                    return set_error(self, XML_ERROR_INVALID_ARGUMENT, "parser must be a root parser");
                }
                double factor = 0.0;
                if (!parse_arguments(fn.c_str(), rest, aKwargs, { "max_factor" }, 1u, 1u, a) || !to_double(a[0], factor))
                    return nullptr;
                auto const maxFactor = static_cast<float>(factor);
                auto const ok = name == "SetAllocTrackerMaximumAmplification" ? XML_SetAllocTrackerMaximumAmplification(self.itself, maxFactor) :
                    XML_SetBillionLaughsAttackProtectionMaximumAmplification(self.itself, maxFactor);
                if (ok == XML_TRUE)
                    return new_reference(none());
                return set_error(self, XML_ERROR_INVALID_ARGUMENT, (std::isnan(maxFactor) || maxFactor < 1.0f) ? "'max_factor' must be at least 1.0" :
                    "parser must be a root parser");
            }

            char const* const sParserMethods[] = { "Parse", "ParseFile", "SetBase", "GetBase", "GetInputContext", "ExternalEntityParserCreate",
                "SetParamEntityParsing", "UseForeignDTD", "SetBillionLaughsAttackProtectionActivationThreshold",
                "SetBillionLaughsAttackProtectionMaximumAmplification", "SetAllocTrackerActivationThreshold", "SetAllocTrackerMaximumAmplification",
                "SetReparseDeferralEnabled", "GetReparseDeferralEnabled" };

            int parser_setattr(neos_py_context*, object* aSelf, char const* aName, object* aValue)
            {
                auto& self = as_parser(aSelf);
                std::string_view const name{ aName };
                auto const index = handler_index_of(aName);
                bool const settable = index >= 0 || name == "buffer_text" || name == "buffer_size" || name == "namespace_prefixes" ||
                    name == "ordered_attributes" || name == "specified_attributes";
                if (!settable)
                {
                    static char const* const sReadOnly[] = { "ErrorCode", "ErrorLineNumber", "ErrorColumnNumber", "ErrorByteIndex", "CurrentLineNumber",
                        "CurrentColumnNumber", "CurrentByteIndex", "buffer_used", "intern" };
                    for (auto r : sReadOnly)
                        if (name == r)
                        {
                            error("AttributeError", std::string{ "attribute '" } + aName + "' of 'pyexpat.xmlparser' objects is not writable");
                            return -1;
                        }
                    error("AttributeError", std::string{ "'pyexpat.xmlparser' object has no attribute '" } + aName + "' and no __dict__ for setting new attributes");
                    return -1;
                }
                if (aValue == nullptr)
                {
                    error("RuntimeError", "Cannot delete attribute");
                    return -1;
                }
                if (index >= 0)
                {
                    // (the character data handler changed: the buffered data the old one's)
                    if (index == CharacterData && flush_character_buffer(self) < 0)
                        return -1;
                    if (kind_of(aValue) == NEOS_PY_NONE)
                    {
                        release(self.handlers[index]);
                        self.handlers[index] = nullptr;
                        if (index == CharacterData && self.inCallback)
                            XML_SetCharacterDataHandler(self.itself, noop_character_data_handler);
                        else
                            set_handler(self, index, false);
                    }
                    else
                    {
                        retain(aValue);
                        release(self.handlers[index]);
                        self.handlers[index] = aValue;
                        set_handler(self, index, true);
                    }
                    return 0;
                }
                if (name == "buffer_size")
                {
                    if (!is_int_like(aValue) || kind_of(aValue) == NEOS_PY_BOOL)
                    {
                        error("TypeError", "buffer_size must be an integer");
                        return -1;
                    }
                    long long size = 0;
                    if (!to_index(aValue, size))
                        return -1;
                    if (size <= 0)
                    {
                        error("ValueError", "buffer_size must be greater than zero");
                        return -1;
                    }
                    if (size == self.bufferSize)
                        return 0;
                    if (size > 0x7FFFFFFFll)
                    {
                        error("ValueError", "buffer_size must not be greater than 2147483647");
                        return -1;
                    }
                    if (self.buffering && self.bufferUsed != 0 && flush_character_buffer(self) < 0)
                        return -1;
                    self.bufferSize = static_cast<int>(size);
                    self.buffering = true;
                    self.buffer.assign(static_cast<std::size_t>(size), '\0');
                    return 0;
                }
                auto const b = truth(aValue);
                if (b < 0)
                    return -1;
                if (name == "buffer_text")
                {
                    if (b != 0)
                    {
                        if (!self.buffering)
                        {
                            self.buffering = true;
                            self.buffer.assign(static_cast<std::size_t>(self.bufferSize), '\0');
                            self.bufferUsed = 0;
                        }
                    }
                    else if (self.buffering)
                    {
                        if (flush_character_buffer(self) < 0)
                            return -1;
                        self.buffering = false;
                        self.buffer.clear();
                    }
                }
                else if (name == "namespace_prefixes")
                {
                    self.nsPrefixes = b != 0;
                    XML_SetReturnNSTriplet(self.itself, self.nsPrefixes ? 1 : 0);
                }
                else if (name == "ordered_attributes")
                    self.orderedAttributes = b != 0;
                else
                    self.specifiedAttributes = b != 0;
                return 0;
            }

            neos_py_type make_parser_type()
            {
                auto type = make_type("pyexpat.xmlparser", "xmlparser", retain_object, release_object<parser_object>);
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& self = as_parser(aSelf);
                    std::string_view const name{ aName };
                    for (auto m : sParserMethods)
                        if (name == m)
                            return make_function(m, parser_method, const_cast<char*>(m), aSelf);
                    if (auto const index = handler_index_of(aName); index >= 0)
                        return new_reference(self.handlers[index] != nullptr ? self.handlers[index] : none());
                    if (name == "ErrorCode")
                        return make_int(static_cast<long long>(XML_GetErrorCode(self.itself)));
                    if (name == "ErrorLineNumber")
                        return make_int(static_cast<long long>(XML_GetErrorLineNumber(self.itself)));
                    if (name == "ErrorColumnNumber")
                        return make_int(static_cast<long long>(XML_GetErrorColumnNumber(self.itself)));
                    if (name == "ErrorByteIndex")
                        return make_int(static_cast<long long>(XML_GetErrorByteIndex(self.itself)));
                    if (name == "CurrentLineNumber")
                        return make_int(static_cast<long long>(XML_GetCurrentLineNumber(self.itself)));
                    if (name == "CurrentColumnNumber")
                        return make_int(static_cast<long long>(XML_GetCurrentColumnNumber(self.itself)));
                    if (name == "CurrentByteIndex")
                        return make_int(static_cast<long long>(XML_GetCurrentByteIndex(self.itself)));
                    if (name == "buffer_size")
                        return make_int(self.bufferSize);
                    if (name == "buffer_text")
                        return make_bool(self.buffering);
                    if (name == "buffer_used")
                        return make_int(self.bufferUsed);
                    if (name == "namespace_prefixes")
                        return make_bool(self.nsPrefixes);
                    if (name == "ordered_attributes")
                        return make_bool(self.orderedAttributes);
                    if (name == "specified_attributes")
                        return make_bool(self.specifiedAttributes);
                    if (name == "intern")
                        return new_reference(self.intern != nullptr ? self.intern : none());
                    if (name == "__doc__")
                        return make_str("XML parser");
                    return nullptr;
                };
                type.setattr = parser_setattr;
                return type;
            }

            object* parser_type_new(neos_py_context*, void*, object*, object*)
            {
                return error("TypeError", "cannot create 'pyexpat.xmlparser' instances");
            }

            int parser_instancecheck(neos_py_context*, void*, object* aObject) { return is_parser(aObject) ? 1 : 0; }

            // --- the module

            builtin_entry const sPyexpatFunctions[] =
            {
                { "ParserCreate", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("ParserCreate", aArgs, aKwargs, { "encoding", "namespace_separator", "intern" }, 0u, 0u, a))
                            return nullptr;
                        auto const text = [&](object* aValue, char const* aArgument, std::string& aResult, bool& aGiven) -> bool
                        {
                            aGiven = aValue != nullptr && kind_of(aValue) != NEOS_PY_NONE;
                            if (!aGiven)
                                return true;
                            if (kind_of(aValue) != NEOS_PY_STR)
                                return (error("TypeError", std::string{ "ParserCreate() argument '" } + aArgument + "' must be str or None, not " + type_name(aValue)), false);
                            aResult = as<str_object>(aValue)->value;
                            return true;
                        };
                        std::string encoding, separator;
                        bool hasEncoding = false, hasSeparator = false;
                        if (!text(a[0], "encoding", encoding, hasEncoding) || !text(a[1], "namespace_separator", separator, hasSeparator))
                            return nullptr;
                        if (hasSeparator && separator.size() > 1u)
                            return error("ValueError", "namespace_separator must be at most one character, omitted, or None");
                        // (intern: None, no interning; not given, a new dict)
                        ref intern;
                        if (a[2] == nullptr)
                            intern = ref{ make_dict() };
                        else if (kind_of(a[2]) != NEOS_PY_NONE)
                        {
                            if (kind_of(a[2]) != NEOS_PY_DICT)
                                return error("TypeError", "intern must be a dictionary");
                            intern = ref::borrowed(a[2]);
                        }
                        return new_parser(hasEncoding ? encoding.c_str() : nullptr, hasSeparator ? separator.c_str() : nullptr, intern.get());
                    } },
                { "ErrorString", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long code = 0;
                        if (!parse_arguments("ErrorString", aArgs, aKwargs, { "code" }, 1u, 1u, a) || !to_index(a[0], code))
                            return nullptr;
                        return conv_string(XML_ErrorString(static_cast<XML_Error>(code)));
                    } }
            };

            // (the errors' constants (XML_ERROR_NONE is not one of them))
            char const* const sErrorNames[] =
            {
                nullptr, "XML_ERROR_NO_MEMORY", "XML_ERROR_SYNTAX", "XML_ERROR_NO_ELEMENTS", "XML_ERROR_INVALID_TOKEN", "XML_ERROR_UNCLOSED_TOKEN",
                "XML_ERROR_PARTIAL_CHAR", "XML_ERROR_TAG_MISMATCH", "XML_ERROR_DUPLICATE_ATTRIBUTE", "XML_ERROR_JUNK_AFTER_DOC_ELEMENT",
                "XML_ERROR_PARAM_ENTITY_REF", "XML_ERROR_UNDEFINED_ENTITY", "XML_ERROR_RECURSIVE_ENTITY_REF", "XML_ERROR_ASYNC_ENTITY",
                "XML_ERROR_BAD_CHAR_REF", "XML_ERROR_BINARY_ENTITY_REF", "XML_ERROR_ATTRIBUTE_EXTERNAL_ENTITY_REF", "XML_ERROR_MISPLACED_XML_PI",
                "XML_ERROR_UNKNOWN_ENCODING", "XML_ERROR_INCORRECT_ENCODING", "XML_ERROR_UNCLOSED_CDATA_SECTION", "XML_ERROR_EXTERNAL_ENTITY_HANDLING",
                "XML_ERROR_NOT_STANDALONE", "XML_ERROR_UNEXPECTED_STATE", "XML_ERROR_ENTITY_DECLARED_IN_PE", "XML_ERROR_FEATURE_REQUIRES_XML_DTD",
                "XML_ERROR_CANT_CHANGE_FEATURE_ONCE_PARSING", "XML_ERROR_UNBOUND_PREFIX", "XML_ERROR_UNDECLARING_PREFIX", "XML_ERROR_INCOMPLETE_PE",
                "XML_ERROR_XML_DECL", "XML_ERROR_TEXT_DECL", "XML_ERROR_PUBLICID", "XML_ERROR_SUSPENDED", "XML_ERROR_NOT_SUSPENDED",
                "XML_ERROR_ABORTED", "XML_ERROR_FINISHED", "XML_ERROR_SUSPEND_PE", "XML_ERROR_RESERVED_PREFIX_XML", "XML_ERROR_RESERVED_PREFIX_XMLNS",
                "XML_ERROR_RESERVED_NAMESPACE_URI", "XML_ERROR_INVALID_ARGUMENT", "XML_ERROR_NO_BUFFER", "XML_ERROR_AMPLIFICATION_LIMIT_BREACH"
            };

            bool errors_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                ref const codes{ make_dict() };
                ref const messages{ make_dict() };
                for (std::size_t i = 1u; i < std::size(sErrorNames); ++i)
                {
                    auto const message = XML_ErrorString(static_cast<XML_Error>(i));
                    ref const text{ make_str(message != nullptr ? message : "") };
                    ref const number{ make_int(static_cast<long long>(i)) };
                    aAttributes.emplace_back(sErrorNames[i], new_reference(text.get()));
                    if (dict_set(codes.get(), text.get(), number.get()) < 0 || dict_set(messages.get(), number.get(), text.get()) < 0)
                        return false;
                }
                aAttributes.emplace_back("__doc__", make_str("Constants used to describe error conditions."));
                aAttributes.emplace_back("codes", new_reference(codes.get()));
                aAttributes.emplace_back("messages", new_reference(messages.get()));
                return true;
            }

            bool model_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                aAttributes.emplace_back("__doc__", make_str("Constants used to interpret content model information."));
                for (auto const& [name, value] : { std::pair{ "XML_CTYPE_EMPTY", static_cast<int>(XML_CTYPE_EMPTY) }, std::pair{ "XML_CTYPE_ANY", static_cast<int>(XML_CTYPE_ANY) },
                    std::pair{ "XML_CTYPE_MIXED", static_cast<int>(XML_CTYPE_MIXED) }, std::pair{ "XML_CTYPE_NAME", static_cast<int>(XML_CTYPE_NAME) },
                    std::pair{ "XML_CTYPE_CHOICE", static_cast<int>(XML_CTYPE_CHOICE) }, std::pair{ "XML_CTYPE_SEQ", static_cast<int>(XML_CTYPE_SEQ) },
                    std::pair{ "XML_CQUANT_NONE", static_cast<int>(XML_CQUANT_NONE) }, std::pair{ "XML_CQUANT_OPT", static_cast<int>(XML_CQUANT_OPT) },
                    std::pair{ "XML_CQUANT_REP", static_cast<int>(XML_CQUANT_REP) }, std::pair{ "XML_CQUANT_PLUS", static_cast<int>(XML_CQUANT_PLUS) } })
                    aAttributes.emplace_back(name, make_int(value));
                return true;
            }

            bool pyexpat_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sPyexpatFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("__doc__", make_str("Python wrapper for Expat parser."));
                ref const error{ expat_error() };
                if (!error)
                    return false;
                aAttributes.emplace_back("error", new_reference(error.get()));
                aAttributes.emplace_back("ExpatError", new_reference(error.get()));
                aAttributes.emplace_back("XMLParserType", context()->type_new(context(), "xmlparser", "pyexpat.xmlparser", parser_type_new, nullptr,
                    parser_instancecheck));
                aAttributes.emplace_back("EXPAT_VERSION", make_str(XML_ExpatVersion()));
                auto const info = XML_ExpatVersionInfo();
                aAttributes.emplace_back("version_info", make_tuple({ make_int(info.major), make_int(info.minor), make_int(info.micro) }));
                aAttributes.emplace_back("native_encoding", make_str("UTF-8"));
                // (the submodules errors and model (sys.modules has them as pyexpat.errors and pyexpat.model))
                for (auto const name : { "errors", "model" })
                {
                    ref const module{ fn_import((std::string{ "pyexpat." } + name).c_str()) };
                    if (!module)
                        return false;
                    aAttributes.emplace_back(name, new_reference(module.get()));
                }
                std::vector<object*> features;
                for (auto f = XML_GetFeatureList(); f->feature != XML_FEATURE_END; ++f)
                    features.push_back(make_tuple({ make_str(f->name), make_int(f->value) }));
                aAttributes.emplace_back("features", make_list(std::move(features)));
                aAttributes.emplace_back("XML_PARAM_ENTITY_PARSING_NEVER", make_int(XML_PARAM_ENTITY_PARSING_NEVER));
                aAttributes.emplace_back("XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE", make_int(XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE));
                aAttributes.emplace_back("XML_PARAM_ENTITY_PARSING_ALWAYS", make_int(XML_PARAM_ENTITY_PARSING_ALWAYS));
                return true;
            }

            inline bool const sPyexpat = register_module("pyexpat", pyexpat_module) && register_module("pyexpat.errors", errors_module) &&
                register_module("pyexpat.model", model_module);
        }
    }
}
