/*
  xml_dom_xmlbuilder.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/dom/xmlbuilder.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.dom.xmlbuilder (CPython 3.13's Lib/xml/dom/xmlbuilder.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_dom_xmlbuilder_docs.hpp"

namespace neos::python_runtime::detail::xml_dom_xmlbuilder_module
{
    namespace
    {
        using pyclass::S;
        using pyclass::I;
        using pyclass::BigI;
        using pyclass::F;
        using pyclass::B;
        using pyclass::Mod;
        using pyclass::none;
        using pyclass::boolean;
        using pyclass::M;
        using pyclass::has_attr;
        using pyclass::get_attr;
        using pyclass::callable;
        using pyclass::module_writer;
        using pyclass::namespace_type;
        using pyclass::try_except;
        using pyclass::try_finally;
        using pyclass::with_;
        using pyclass::unpack2;
        using pyclass::format;
        using pyclass::handler_scope;
        using pyclass::matches;
        using pyclass::iterate;
        using pyclass::unpack;
        using pyclass::call_ex;
        using pyclass::dict_of;
        using pyclass::set_of;
        using pyclass::inplace;
        using pyclass::del_attr;
        using pyclass::del_item;
        using pyclass::formatted;
        using pyclass::yield_from;
        using pyclass::truth_of;
        using pyclass::module_of;
        using pyclass::handled_exception;
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.dom.xmlbuilder.name"))
            bool made = false;
        };
        state& the_state()
        {
            static state* sState = new state{};
            return *sState;
        }

        // (its entry: the object, as it is defined (or assigned) now)
        py const& object_entry(char const* aQualifiedName)
        {
            auto& s = the_state();
            auto const o = s.objects.find(aQualifiedName);
            if (o == s.objects.end())
                py_raise("SystemError", std::string{ "xml.dom.xmlbuilder: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.dom.xmlbuilder.name">()))
        template <pyclass::fixed_string Name>
        py const& G() { static py const* const sEntry = &object_entry(Name.value); return *sEntry; }
        // (an object made: one of the module's attributes)
        void define(char const* aModule, char const* aName, py const& aValue)
        {
            auto& s = the_state();
            s.objects[std::string{ aModule } + "." + aName] = aValue;
            for (auto& a : s.attributes)
                if (a.first == aName)
                {
                    a.second = aValue;
                    return;
                }
            s.attributes.emplace_back(aName, aValue);
        }
        // (del name: no longer one of the module's attributes (its object remains, for the module's functions))
        [[maybe_unused]] void undefine(char const* aModule, char const* aName)
        {
            auto& s = the_state();
            (void)aModule;
            std::erase_if(s.attributes, [&](auto const& a) { return a.first == aName; });
        }
        // (a module's global assigned (global name; name = value): its object, and the module's attribute (if it has been imported))
        void set_global(char const* aModule, char const* aName, py const& aValue)
        {
            define(aModule, aName, aValue);
            if (modules_imported(aModule))
                py{ fn_import(aModule) }.set_attr(aName, aValue);
        }
        // (globals(): the module's namespace (its __dict__ once it has been imported; while it is being made, its attributes so far))
        [[maybe_unused]] py module_globals()
        {
            if (modules_imported("xml.dom.xmlbuilder"))
                return py{ fn_import("xml.dom.xmlbuilder") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.dom.xmlbuilder" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.dom.xmlbuilder", xml_dom_xmlbuilder_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.dom.xmlbuilder (Lib/xml/dom/xmlbuilder.py)

        void make_xml_dom_xmlbuilder()
        {
            static module_writer const& w = writer("xml.dom.xmlbuilder");
            define("xml.dom.xmlbuilder", "__all__", py_list({ S<"DOMBuilder">(), S<"DOMEntityResolver">(), S<"DOMInputSource">() }));
                // Note that the DOMBuilder class in LoadSave constrains which of these
                // values can be set using the DOM Level 3 LoadSave feature.
            define("xml.dom.xmlbuilder", "Options", w.make_class("Options", {  }, {
                { "namespaces", I<1>() },
                { "namespace_declarations", boolean(true) },
                { "validation", boolean(false) },
                { "external_parameter_entities", boolean(true) },
                { "external_general_entities", boolean(true) },
                { "external_dtd_subset", boolean(true) },
                { "validate_if_schema", boolean(false) },
                { "validate", boolean(false) },
                { "datatype_normalization", boolean(false) },
                { "create_entity_ref_nodes", boolean(true) },
                { "entities", boolean(true) },
                { "whitespace_in_element_content", boolean(true) },
                { "cdata_sections", boolean(true) },
                { "comments", boolean(true) },
                { "charset_overrides_xml_encoding", boolean(true) },
                { "infoset", boolean(false) },
                { "supported_mediatypes_only", boolean(false) },
                { "errorHandler", none() },
                { "filter", none() }, }));

                // This dictionary maps from (feature,value) to a list of
                // (option,value) pairs that should be set on the Options object.
                // If a (feature,value) setting is not in this dictionary, it is
                // not supported by the DOMBuilder.
                // 
            define("xml.dom.xmlbuilder", "DOMBuilder", w.make_class("DOMBuilder", {  }, {
                { "entityResolver", none() },
                { "errorHandler", none() },
                { "filter", none() },
                { "ACTION_REPLACE", I<1>() },
                { "ACTION_APPEND_AS_CHILDREN", I<2>() },
                { "ACTION_INSERT_AFTER", I<3>() },
                { "ACTION_INSERT_BEFORE", I<4>() },
                { "_legal_actions", py_tuple({ (I<1>()), (I<2>()), (I<3>()), (I<4>()) }) },
                { "__init__", w.method("DOMBuilder", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_options", (G<"xml.dom.xmlbuilder.Options">())());
                        return none();
                    }) },
                { "_get_entityResolver", w.method("DOMBuilder", "_get_entityResolver", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("entityResolver");
                    }) },
                { "_set_entityResolver", w.method("DOMBuilder", "_set_entityResolver", { py_positional("entityResolver") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py entityResolver = a[1];
                        self.set_attr("entityResolver", entityResolver);
                        return none();
                    }) },
                { "_get_errorHandler", w.method("DOMBuilder", "_get_errorHandler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("errorHandler");
                    }) },
                { "_set_errorHandler", w.method("DOMBuilder", "_set_errorHandler", { py_positional("errorHandler") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py errorHandler = a[1];
                        self.set_attr("errorHandler", errorHandler);
                        return none();
                    }) },
                { "_get_filter", w.method("DOMBuilder", "_get_filter", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("filter");
                    }) },
                { "_set_filter", w.method("DOMBuilder", "_set_filter", { py_positional("filter") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filter = a[1];
                        self.set_attr("filter", filter);
                        return none();
                    }) },
                { "setFeature", w.method("DOMBuilder", "setFeature", { py_positional("name"), py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py state = a[2];
                        py settings;
                        py value;
                        if (self.method("supportsFeature", { name }).truthy())
                        {
                            state = [&]() -> py { py const t = [&]() -> py { py const t = state; return !t.truthy() ? t : I<1>(); }(); return t.truthy() ? t : I<0>(); }();
                            {
                                bool otherwise1 = false;
                                try
                                {
                                    settings = self.attr("_settings")[py_tuple({ (G<"xml.dom.xmlbuilder._name_xform">())(name), state })];
                                    otherwise1 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler1;
                                    if (matches(handler1.exception, B<"KeyError">()))
                                    {
                                        fn_raise_from((Mod<"xml.dom">().attr("NotSupportedErr"))((S<"unsupported feature: %r">()) % py_tuple({ name })).get(), none().get());
                                        throw raised{};
                                    }
                                    else
                                        py_reraise(handler1.exception);
                                }
                                if (otherwise1)
                                {
                                    for (py const& item : iterate{ settings })
                                    {
                                        auto const items3 = unpack(item, 2);
                                        name = items3[0];
                                        value = items3[1];
                                        (B<"setattr">())(self.attr("_options"), name, value);
                                    }
                                }
                            }
                        }
                        else
                        {
                            py_raise((Mod<"xml.dom">().attr("NotFoundErr"))((S<"unknown feature: ">()) + S(name.repr())));
                        }
                        return none();
                    }) },
                { "supportsFeature", w.method("DOMBuilder", "supportsFeature", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return (B<"hasattr">())(self.attr("_options"), (G<"xml.dom.xmlbuilder._name_xform">())(name));
                    }) },
                { "canSetFeature", w.method("DOMBuilder", "canSetFeature", { py_positional("name"), py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py state = a[2];
                        py key;
                        key = py_tuple({ (G<"xml.dom.xmlbuilder._name_xform">())(name), [&]() -> py { py const t = [&]() -> py { py const t = state; return !t.truthy() ? t : I<1>(); }(); return t.truthy() ? t : I<0>(); }() });
                        return boolean(self.attr("_settings").contains(key));
                    }) },
                { "_settings", dict_of({ { py_tuple({ S<"namespace_declarations">(), I<0>() }), py_list({ py_tuple({ S<"namespace_declarations">(), I<0>() }) }) }, { py_tuple({ S<"namespace_declarations">(), I<1>() }), py_list({ py_tuple({ S<"namespace_declarations">(), I<1>() }) }) }, { py_tuple({ S<"validation">(), I<0>() }), py_list({ py_tuple({ S<"validation">(), I<0>() }) }) }, { py_tuple({ S<"external_general_entities">(), I<0>() }), py_list({ py_tuple({ S<"external_general_entities">(), I<0>() }) }) }, { py_tuple({ S<"external_general_entities">(), I<1>() }), py_list({ py_tuple({ S<"external_general_entities">(), I<1>() }) }) }, { py_tuple({ S<"external_parameter_entities">(), I<0>() }), py_list({ py_tuple({ S<"external_parameter_entities">(), I<0>() }) }) }, { py_tuple({ S<"external_parameter_entities">(), I<1>() }), py_list({ py_tuple({ S<"external_parameter_entities">(), I<1>() }) }) }, { py_tuple({ S<"validate_if_schema">(), I<0>() }), py_list({ py_tuple({ S<"validate_if_schema">(), I<0>() }) }) }, { py_tuple({ S<"create_entity_ref_nodes">(), I<0>() }), py_list({ py_tuple({ S<"create_entity_ref_nodes">(), I<0>() }) }) }, { py_tuple({ S<"create_entity_ref_nodes">(), I<1>() }), py_list({ py_tuple({ S<"create_entity_ref_nodes">(), I<1>() }) }) }, { py_tuple({ S<"entities">(), I<0>() }), py_list({ py_tuple({ S<"create_entity_ref_nodes">(), I<0>() }), py_tuple({ S<"entities">(), I<0>() }) }) }, { py_tuple({ S<"entities">(), I<1>() }), py_list({ py_tuple({ S<"entities">(), I<1>() }) }) }, { py_tuple({ S<"whitespace_in_element_content">(), I<0>() }), py_list({ py_tuple({ S<"whitespace_in_element_content">(), I<0>() }) }) }, { py_tuple({ S<"whitespace_in_element_content">(), I<1>() }), py_list({ py_tuple({ S<"whitespace_in_element_content">(), I<1>() }) }) }, { py_tuple({ S<"cdata_sections">(), I<0>() }), py_list({ py_tuple({ S<"cdata_sections">(), I<0>() }) }) }, { py_tuple({ S<"cdata_sections">(), I<1>() }), py_list({ py_tuple({ S<"cdata_sections">(), I<1>() }) }) }, { py_tuple({ S<"comments">(), I<0>() }), py_list({ py_tuple({ S<"comments">(), I<0>() }) }) }, { py_tuple({ S<"comments">(), I<1>() }), py_list({ py_tuple({ S<"comments">(), I<1>() }) }) }, { py_tuple({ S<"charset_overrides_xml_encoding">(), I<0>() }), py_list({ py_tuple({ S<"charset_overrides_xml_encoding">(), I<0>() }) }) }, { py_tuple({ S<"charset_overrides_xml_encoding">(), I<1>() }), py_list({ py_tuple({ S<"charset_overrides_xml_encoding">(), I<1>() }) }) }, { py_tuple({ S<"infoset">(), I<0>() }), py_list() }, { py_tuple({ S<"infoset">(), I<1>() }), py_list({ py_tuple({ S<"namespace_declarations">(), I<0>() }), py_tuple({ S<"validate_if_schema">(), I<0>() }), py_tuple({ S<"create_entity_ref_nodes">(), I<0>() }), py_tuple({ S<"entities">(), I<0>() }), py_tuple({ S<"cdata_sections">(), I<0>() }), py_tuple({ S<"datatype_normalization">(), I<1>() }), py_tuple({ S<"whitespace_in_element_content">(), I<1>() }), py_tuple({ S<"comments">(), I<1>() }), py_tuple({ S<"charset_overrides_xml_encoding">(), I<1>() }) }) }, { py_tuple({ S<"supported_mediatypes_only">(), I<0>() }), py_list({ py_tuple({ S<"supported_mediatypes_only">(), I<0>() }) }) }, { py_tuple({ S<"namespaces">(), I<0>() }), py_list({ py_tuple({ S<"namespaces">(), I<0>() }) }) }, { py_tuple({ S<"namespaces">(), I<1>() }), py_list({ py_tuple({ S<"namespaces">(), I<1>() }) }) } }) },
                { "getFeature", w.method("DOMBuilder", "getFeature", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py options;
                        py xname;
                        xname = (G<"xml.dom.xmlbuilder._name_xform">())(name);
                        try
                        {
                            return (B<"getattr">())(self.attr("_options"), xname);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler4;
                            if (matches(handler4.exception, B<"AttributeError">()))
                            {
                                if (name == (S<"infoset">()))
                                {
                                    options = self.attr("_options");
                                    return [&]() -> py { py const t = options.attr("datatype_normalization"); return !t.truthy() ? t : [&]() -> py { py const t = options.attr("whitespace_in_element_content"); return !t.truthy() ? t : [&]() -> py { py const t = options.attr("comments"); return !t.truthy() ? t : [&]() -> py { py const t = options.attr("charset_overrides_xml_encoding"); return !t.truthy() ? t : boolean(!([&]() -> py { py const t = options.attr("namespace_declarations"); return t.truthy() ? t : [&]() -> py { py const t = options.attr("validate_if_schema"); return t.truthy() ? t : [&]() -> py { py const t = options.attr("create_entity_ref_nodes"); return t.truthy() ? t : [&]() -> py { py const t = options.attr("entities"); return t.truthy() ? t : options.attr("cdata_sections"); }(); }(); }(); }()).truthy()); }(); }(); }(); }();
                                }
                                py_raise((Mod<"xml.dom">().attr("NotFoundErr"))((S<"feature %s not known">()) % S(name.repr())));
                            }
                            else
                                py_reraise(handler4.exception);
                        }
                        return none();
                    }) },
                { "parseURI", w.method("DOMBuilder", "parseURI", { py_positional("uri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py uri = a[1];
                        py input;
                        if (self.attr("entityResolver").truthy())
                        {
                            input = self.attr("entityResolver").method("resolveEntity", { none(), uri });
                        }
                        else
                        {
                            input = (G<"xml.dom.xmlbuilder.DOMEntityResolver">())().method("resolveEntity", { none(), uri });
                        }
                        return self.method("parse", { input });
                    }) },
                { "parse", w.method("DOMBuilder", "parse", { py_positional("input") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py input = a[1];
                        py fp;
                        py options;
                        py urllib;
                        options = (Mod<"copy">().attr("copy"))(self.attr("_options"));
                        options.set_attr("filter", self.attr("filter"));
                        options.set_attr("errorHandler", self.attr("errorHandler"));
                        fp = input.attr("byteStream");
                        if ((fp.is_none() && input.attr("systemId").truthy()))
                        {
                            urllib = module_of("urllib");
                            fp = urllib.attr("request").method("urlopen", { input.attr("systemId") });
                        }
                        return self.method("_parse_bytestream", { fp, options });
                    }) },
                { "parseWithContext", w.method("DOMBuilder", "parseWithContext", { py_positional("input"), py_positional("cnode"), py_positional("action") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py input = a[1];
                        py cnode = a[2];
                        py action = a[3];
                        if (!self.attr("_legal_actions").contains(action))
                        {
                            py_raise((B<"ValueError">())(S<"not a legal action">()));
                        }
                        py_raise((B<"NotImplementedError">())(S<"Haven't written this yet...">()));
                    }) },
                { "_parse_bytestream", w.method("DOMBuilder", "_parse_bytestream", { py_positional("stream"), py_positional("options") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stream = a[1];
                        py options = a[2];
                        py builder;
                        py xml;
                        xml = module_of("xml");
                        builder = xml.attr("dom").attr("expatbuilder").method("makeBuilder", { options });
                        return builder.method("parseFile", { stream });
                    }) }, }));

            define("xml.dom.xmlbuilder", "_name_xform", w.function("_name_xform", "_name_xform", { py_positional("name") }, [](arguments a) -> py
                {
                    py name = a[0];
                    return name.method("lower").method("replace", { S<"-">(), S<"_">() });
                }));

            define("xml.dom.xmlbuilder", "DOMEntityResolver", w.make_class("DOMEntityResolver", { B<"object">() }, {
                { "__slots__", py_tuple({ S<"_opener">() }) },
                { "resolveEntity", w.method("DOMEntityResolver", "resolveEntity", { py_positional("publicId"), py_positional("systemId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py publicId = a[1];
                        py systemId = a[2];
                        py fragment;
                        py netloc;
                        py params;
                        py parts;
                        py path;
                        py posixpath;
                        py query;
                        py scheme;
                        py source;
                        py urllib;
                        if (systemId.is_none())
                            py_raise(B<"AssertionError">()());
                        source = (G<"xml.dom.xmlbuilder.DOMInputSource">())();
                        source.set_attr("publicId", publicId);
                        source.set_attr("systemId", systemId);
                        source.set_attr("byteStream", self.method("_get_opener").method("open", { systemId }));
                        // determine the encoding if the transport provided it
                        source.set_attr("encoding", self.method("_guess_media_encoding", { source }));
                        // determine the base URI is we can
                        posixpath = module_of("posixpath");
                        urllib = module_of("urllib");
                        parts = urllib.attr("parse").method("urlparse", { systemId });
                        auto const items5 = unpack(parts, 6);
                        scheme = items5[0];
                        netloc = items5[1];
                        path = items5[2];
                        params = items5[3];
                        query = items5[4];
                        fragment = items5[5];
                        // XXX should we check the scheme here as well?
                        if ((path.truthy() && (!path.method("endswith", { S<"/">() }).truthy())))
                        {
                            path = posixpath.method("dirname", { path }) + (S<"/">());
                            parts = py_tuple({ scheme, netloc, path, params, query, fragment });
                            source.set_attr("baseURI", urllib.attr("parse").method("urlunparse", { parts }));
                        }
                        return source;
                    }) },
                { "_get_opener", w.method("DOMEntityResolver", "_get_opener", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        try
                        {
                            return self.attr("_opener");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler6;
                            if (matches(handler6.exception, B<"AttributeError">()))
                            {
                                self.set_attr("_opener", self.method("_create_opener"));
                                return self.attr("_opener");
                            }
                            else
                                py_reraise(handler6.exception);
                        }
                        return none();
                    }) },
                { "_create_opener", w.method("DOMEntityResolver", "_create_opener", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py urllib;
                        urllib = module_of("urllib");
                        return urllib.attr("request").method("build_opener");
                    }) },
                { "_guess_media_encoding", w.method("DOMEntityResolver", "_guess_media_encoding", { py_positional("source") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        py charset;
                        py info;
                        info = source.attr("byteStream").method("info");
                        // import email.message
                        // assert isinstance(info, email.message.Message)
                        charset = info.method("get_param", { S<"charset">() });
                        if (!charset.is_none())
                        {
                            return charset.method("lower");
                        }
                        return none();
                    }) }, }));

            define("xml.dom.xmlbuilder", "DOMInputSource", w.make_class("DOMInputSource", { B<"object">() }, {
                { "__slots__", py_tuple({ S<"byteStream">(), S<"characterStream">(), S<"stringData">(), S<"encoding">(), S<"publicId">(), S<"systemId">(), S<"baseURI">() }) },
                { "__init__", w.method("DOMInputSource", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("byteStream", none());
                        self.set_attr("characterStream", none());
                        self.set_attr("stringData", none());
                        self.set_attr("encoding", none());
                        self.set_attr("publicId", none());
                        self.set_attr("systemId", none());
                        self.set_attr("baseURI", none());
                        return none();
                    }) },
                { "_get_byteStream", w.method("DOMInputSource", "_get_byteStream", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("byteStream");
                    }) },
                { "_set_byteStream", w.method("DOMInputSource", "_set_byteStream", { py_positional("byteStream") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py byteStream = a[1];
                        self.set_attr("byteStream", byteStream);
                        return none();
                    }) },
                { "_get_characterStream", w.method("DOMInputSource", "_get_characterStream", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("characterStream");
                    }) },
                { "_set_characterStream", w.method("DOMInputSource", "_set_characterStream", { py_positional("characterStream") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py characterStream = a[1];
                        self.set_attr("characterStream", characterStream);
                        return none();
                    }) },
                { "_get_stringData", w.method("DOMInputSource", "_get_stringData", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("stringData");
                    }) },
                { "_set_stringData", w.method("DOMInputSource", "_set_stringData", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("stringData", data);
                        return none();
                    }) },
                { "_get_encoding", w.method("DOMInputSource", "_get_encoding", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("encoding");
                    }) },
                { "_set_encoding", w.method("DOMInputSource", "_set_encoding", { py_positional("encoding") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py encoding = a[1];
                        self.set_attr("encoding", encoding);
                        return none();
                    }) },
                { "_get_publicId", w.method("DOMInputSource", "_get_publicId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("publicId");
                    }) },
                { "_set_publicId", w.method("DOMInputSource", "_set_publicId", { py_positional("publicId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py publicId = a[1];
                        self.set_attr("publicId", publicId);
                        return none();
                    }) },
                { "_get_systemId", w.method("DOMInputSource", "_get_systemId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("systemId");
                    }) },
                { "_set_systemId", w.method("DOMInputSource", "_set_systemId", { py_positional("systemId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py systemId = a[1];
                        self.set_attr("systemId", systemId);
                        return none();
                    }) },
                { "_get_baseURI", w.method("DOMInputSource", "_get_baseURI", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("baseURI");
                    }) },
                { "_set_baseURI", w.method("DOMInputSource", "_set_baseURI", { py_positional("uri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py uri = a[1];
                        self.set_attr("baseURI", uri);
                        return none();
                    }) }, }));

                // There's really no need for this class; concrete implementations
                // should just implement the endElement() and startElement()
                // methods as appropriate.  Using this makes it easy to only
                // implement one of them.
            define("xml.dom.xmlbuilder", "DOMBuilderFilter", w.make_class("DOMBuilderFilter", {  }, {
                { "FILTER_ACCEPT", I<1>() },
                { "FILTER_REJECT", I<2>() },
                { "FILTER_SKIP", I<3>() },
                { "FILTER_INTERRUPT", I<4>() },
                { "whatToShow", (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_ALL") },
                { "_get_whatToShow", w.method("DOMBuilderFilter", "_get_whatToShow", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("whatToShow");
                    }) },
                { "acceptNode", w.method("DOMBuilderFilter", "acceptNode", { py_positional("element") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py element = a[1];
                        return self.attr("FILTER_ACCEPT");
                    }) },
                { "startContainer", w.method("DOMBuilderFilter", "startContainer", { py_positional("element") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py element = a[1];
                        return self.attr("FILTER_ACCEPT");
                    }) }, }));

            undefine("xml.dom.xmlbuilder", "NodeFilter");
            define("xml.dom.xmlbuilder", "DocumentLS", w.make_class("DocumentLS", {  }, {
                { "async_", boolean(false) },
                { "_get_async", w.method("DocumentLS", "_get_async", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(false);
                    }) },
                { "_set_async", w.method("DocumentLS", "_set_async", { py_positional("flag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py flag = a[1];
                        if (flag.truthy())
                        {
                            py_raise((Mod<"xml.dom">().attr("NotSupportedErr"))(S<"asynchronous document loading is not supported">()));
                        }
                        return none();
                    }) },
                { "abort", w.method("DocumentLS", "abort", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())(S<"haven't figured out what this means yet">()));
                    }) },
                { "load", w.method("DocumentLS", "load", { py_positional("uri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py uri = a[1];
                        py_raise((B<"NotImplementedError">())(S<"haven't written this yet">()));
                    }) },
                { "loadXML", w.method("DocumentLS", "loadXML", { py_positional("source") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        py_raise((B<"NotImplementedError">())(S<"haven't written this yet">()));
                    }) },
                { "saveXML", w.method("DocumentLS", "saveXML", { py_positional("snode") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py snode = a[1];
                        if (snode.is_none())
                        {
                            snode = self;
                        }
                        else if (!snode.attr("ownerDocument").is(self))
                        {
                            py_raise((Mod<"xml.dom">().attr("WrongDocumentErr"))());
                        }
                        return snode.method("toxml");
                    }) }, }));

            define("xml.dom.xmlbuilder", "DOMImplementationLS", w.make_class("DOMImplementationLS", {  }, {
                { "MODE_SYNCHRONOUS", I<1>() },
                { "MODE_ASYNCHRONOUS", I<2>() },
                { "createDOMBuilder", w.method("DOMImplementationLS", "createDOMBuilder", { py_positional("mode"), py_positional("schemaType") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mode = a[1];
                        py schemaType = a[2];
                        if (!schemaType.is_none())
                        {
                            py_raise((Mod<"xml.dom">().attr("NotSupportedErr"))(S<"schemaType not yet supported">()));
                        }
                        if (mode == self.attr("MODE_SYNCHRONOUS"))
                        {
                            return (G<"xml.dom.xmlbuilder.DOMBuilder">())();
                        }
                        if (mode == self.attr("MODE_ASYNCHRONOUS"))
                        {
                            py_raise((Mod<"xml.dom">().attr("NotSupportedErr"))(S<"asynchronous builders are not supported">()));
                        }
                        py_raise((B<"ValueError">())(S<"unknown value for mode">()));
                    }) },
                { "createDOMWriter", w.method("DOMImplementationLS", "createDOMWriter", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())(S<"the writer interface hasn't been written yet!">()));
                    }) },
                { "createDOMInputSource", w.method("DOMImplementationLS", "createDOMInputSource", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"xml.dom.xmlbuilder.DOMInputSource">())();
                    }) }, }));
        }

        bool xml_dom_xmlbuilder_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_dom_xmlbuilder();
                }
                if (auto const doc = xml_dom_xmlbuilder_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("copy", py{ fn_import("copy") }.release());
                aAttributes.emplace_back("xml", py{ fn_import("xml") }.release());
                aAttributes.emplace_back("NodeFilter", import_attribute("xml.dom.NodeFilter", "NodeFilter").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.dom.xmlbuilder", xml_dom_xmlbuilder_module);
    }
}
