/*
  urllib_parse.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/urllib/parse.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module urllib.parse (CPython 3.13's Lib/urllib/parse.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "urllib_parse_docs.hpp"

namespace neos::python_runtime::detail::urllib_parse_module
{
    namespace
    {
        using pyclass::S;
        using pyclass::I;
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
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "urllib.parse.name"))
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
                py_raise("SystemError", std::string{ "urllib.parse: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"urllib.parse.name">()))
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
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "urllib.parse", urllib_parse_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- urllib.parse (Lib/urllib/parse.py)

        void make_urllib_parse()
        {
            static module_writer const& w = writer("urllib.parse");
            define("urllib.parse", "__all__", py_list({ S<"urlparse">(), S<"urlunparse">(), S<"urljoin">(), S<"urldefrag">(), S<"urlsplit">(), S<"urlunsplit">(), S<"urlencode">(), S<"parse_qs">(), S<"parse_qsl">(), S<"quote">(), S<"quote_plus">(), S<"quote_from_bytes">(), S<"unquote">(), S<"unquote_plus">(), S<"unquote_to_bytes">(), S<"DefragResult">(), S<"ParseResult">(), S<"SplitResult">(), S<"DefragResultBytes">(), S<"ParseResultBytes">(), S<"SplitResultBytes">() }));
            // A classification of schemes.
            // The empty string classifies URLs with no scheme specified,
            // being the default value returned by “urlsplit” and “urlparse”.
            define("urllib.parse", "uses_relative", py_list({ S<"">(), S<"ftp">(), S<"http">(), S<"gopher">(), S<"nntp">(), S<"imap">(), S<"wais">(), S<"file">(), S<"https">(), S<"shttp">(), S<"mms">(), S<"prospero">(), S<"rtsp">(), S<"rtsps">(), S<"rtspu">(), S<"sftp">(), S<"svn">(), S<"svn+ssh">(), S<"ws">(), S<"wss">() }));
            define("urllib.parse", "uses_netloc", py_list({ S<"">(), S<"ftp">(), S<"http">(), S<"gopher">(), S<"nntp">(), S<"telnet">(), S<"imap">(), S<"wais">(), S<"file">(), S<"mms">(), S<"https">(), S<"shttp">(), S<"snews">(), S<"prospero">(), S<"rtsp">(), S<"rtsps">(), S<"rtspu">(), S<"rsync">(), S<"svn">(), S<"svn+ssh">(), S<"sftp">(), S<"nfs">(), S<"git">(), S<"git+ssh">(), S<"ws">(), S<"wss">(), S<"itms-services">() }));
            define("urllib.parse", "uses_params", py_list({ S<"">(), S<"ftp">(), S<"hdl">(), S<"prospero">(), S<"http">(), S<"imap">(), S<"https">(), S<"shttp">(), S<"rtsp">(), S<"rtsps">(), S<"rtspu">(), S<"sip">(), S<"sips">(), S<"mms">(), S<"sftp">(), S<"tel">() }));
            // These are not actually used anymore, but should stay for backwards
            // compatibility.  (They are undocumented, but have a public-looking name.)
            define("urllib.parse", "non_hierarchical", py_list({ S<"gopher">(), S<"hdl">(), S<"mailto">(), S<"news">(), S<"telnet">(), S<"wais">(), S<"imap">(), S<"snews">(), S<"sip">(), S<"sips">() }));
            define("urllib.parse", "uses_query", py_list({ S<"">(), S<"http">(), S<"wais">(), S<"imap">(), S<"https">(), S<"shttp">(), S<"mms">(), S<"gopher">(), S<"rtsp">(), S<"rtsps">(), S<"rtspu">(), S<"sip">(), S<"sips">() }));
            define("urllib.parse", "uses_fragment", py_list({ S<"">(), S<"ftp">(), S<"hdl">(), S<"http">(), S<"gopher">(), S<"news">(), S<"nntp">(), S<"wais">(), S<"https">(), S<"shttp">(), S<"snews">(), S<"file">(), S<"prospero">() }));
            // Characters valid in scheme names
            define("urllib.parse", "scheme_chars", S<"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-.">());
            // Leading and trailing C0 control and space to be stripped per WHATWG spec.
            // == "".join([chr(i) for i in range(0, 0x20 + 1)])
            define("urllib.parse", "_WHATWG_C0_CONTROL_OR_SPACE", S<"\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f ">());
            // Unsafe bytes to be removed per WHATWG spec
            define("urllib.parse", "_UNSAFE_URL_BYTES_TO_REMOVE", py_list({ S<"\t">(), S<"\r">(), S<"\n">() }));
            define("urllib.parse", "clear_cache", w.function("clear_cache", "clear_cache", {  }, [](arguments a) -> py
                {
                    (G<"urllib.parse.urlsplit">()).method("cache_clear");
                    (G<"urllib.parse._byte_quoter_factory">()).method("cache_clear");
                    return none();
                }));

            // Helpers for bytes handling
            // For 3.2, we deliberately require applications that
            // handle improperly quoted URLs to do their own
            // decoding and encoding. If valid use cases are
            // presented, we may relax this by using latin-1
            // decoding internally for 3.3
            define("urllib.parse", "_implicit_encoding", S<"ascii">());
            define("urllib.parse", "_implicit_errors", S<"strict">());
            define("urllib.parse", "_noop", w.function("_noop", "_noop", { py_positional("obj") }, [](arguments a) -> py
                {
                    py obj = a[0];
                    return obj;
                }));

            define("urllib.parse", "_encode_result", w.function("_encode_result", "_encode_result", { py_positional("obj"), py_positional("encoding", G<"urllib.parse._implicit_encoding">()), py_positional("errors", G<"urllib.parse._implicit_errors">()) }, [](arguments a) -> py
                {
                    py obj = a[0];
                    py encoding = a[1];
                    py errors = a[2];
                    return obj.method("encode", { encoding, errors });
                }));

            define("urllib.parse", "_decode_args", w.function("_decode_args", "_decode_args", { py_positional("args"), py_positional("encoding", G<"urllib.parse._implicit_encoding">()), py_positional("errors", G<"urllib.parse._implicit_errors">()) }, [](arguments a) -> py
                {
                    py args = a[0];
                    py encoding = a[1];
                    py errors = a[2];
                    return (B<"tuple">())([&]() -> py { py const comprehension1 = py_list(); py x; for (py const& item : iterate{ args }) { x = item; comprehension1.method("append", { (x.truthy() ? x.method("decode", { encoding, errors }) : S<"">()) }); } return comprehension1; }());
                }));

            define("urllib.parse", "_coerce_args", w.function("_coerce_args", "_coerce_args", {  }, [](arguments a) -> py
                {
                    py args = a[0];
                    py arg;
                    py str_input;
                    str_input = boolean(isinstance(args[I<0>()], B<"str">()));
                    for (py const& item : iterate{ args[B<"slice">()(I<1>(), none(), none())] })
                    {
                        arg = item;
                        // We special-case the empty string to support the
                        // "scheme=''" default argument to some functions
                        if ((arg.truthy() && (boolean(isinstance(arg, B<"str">())) != str_input)))
                        {
                            py_raise((B<"TypeError">())(S<"Cannot mix str and non-str arguments">()));
                        }
                    }
                    if (str_input.truthy())
                    {
                        return args + py_tuple({ G<"urllib.parse._noop">() });
                    }
                    return (G<"urllib.parse._decode_args">())(args) + py_tuple({ G<"urllib.parse._encode_result">() });
                }, true));

            // Result objects are more helpful than simple tuples
            define("urllib.parse", "_ResultMixinStr", w.make_class("_ResultMixinStr", { B<"object">() }, {
                { "__slots__", py_tuple() },
                { "encode", w.method("_ResultMixinStr", "encode", { py_positional("encoding", S<"ascii">()), py_positional("errors", S<"strict">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py encoding = a[1];
                        py errors = a[2];
                        return call_ex(self.attr("_encoded_counterpart"), {  }, [&]() -> py { py const comprehension3 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; comprehension3.method("append", { x.method("encode", { encoding, errors }) }); } return comprehension3; }(), py{});
                    }) }, }));

            define("urllib.parse", "_ResultMixinBytes", w.make_class("_ResultMixinBytes", { B<"object">() }, {
                { "__slots__", py_tuple() },
                { "decode", w.method("_ResultMixinBytes", "decode", { py_positional("encoding", S<"ascii">()), py_positional("errors", S<"strict">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py encoding = a[1];
                        py errors = a[2];
                        return call_ex(self.attr("_decoded_counterpart"), {  }, [&]() -> py { py const comprehension4 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; comprehension4.method("append", { x.method("decode", { encoding, errors }) }); } return comprehension4; }(), py{});
                    }) }, }));

            define("urllib.parse", "_NetlocResultMixinBase", w.make_class("_NetlocResultMixinBase", { B<"object">() }, {
                { "__slots__", py_tuple() },
                { "username", B<"property">()(w.method("_NetlocResultMixinBase", "username", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_userinfo")[I<0>()];
                    })) },
                { "password", B<"property">()(w.method("_NetlocResultMixinBase", "password", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_userinfo")[I<1>()];
                    })) },
                { "hostname", B<"property">()(w.method("_NetlocResultMixinBase", "hostname", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py hostname;
                        py percent;
                        py separator;
                        py zone;
                        hostname = self.attr("_hostinfo")[I<0>()];
                        if (!hostname.truthy())
                        {
                            return none();
                        }
                        // Scoped IPv6 address may have zone info, which must not be lowercased
                        // like http://[fe80::822a:a8ff:fe49:470c%tESt]:1234/keys
                        separator = (isinstance(hostname, B<"str">()) ? S<"%">() : py{ make_bytes(std::string{ "\x25", 1 }) });
                        auto const items5 = unpack(hostname.method("partition", { separator }), 3);
                        hostname = items5[0];
                        percent = items5[1];
                        zone = items5[2];
                        return (hostname.method("lower") + percent) + zone;
                    })) },
                { "port", B<"property">()(w.method("_NetlocResultMixinBase", "port", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py port;
                        port = self.attr("_hostinfo")[I<1>()];
                        if (!port.is_none())
                        {
                            if ((port.method("isdigit").truthy() && port.method("isascii").truthy()))
                            {
                                port = (B<"int">())(port);
                            }
                            else
                            {
                                py_raise((B<"ValueError">())(S(std::string{ "Port could not be cast to integer value as " } + port.repr())));
                            }
                            if (!(((I<0>()) <= port) && (port <= (I<65535>()))))
                            {
                                py_raise((B<"ValueError">())(S<"Port out of range 0-65535">()));
                            }
                        }
                        return port;
                    })) },
                { "__class_getitem__", (B<"classmethod">())(Mod<"types">().attr("GenericAlias")) }, }));

            define("urllib.parse", "_NetlocResultMixinStr", w.make_class("_NetlocResultMixinStr", { G<"urllib.parse._NetlocResultMixinBase">(), G<"urllib.parse._ResultMixinStr">() }, {
                { "__slots__", py_tuple() },
                { "_userinfo", B<"property">()(w.method("_NetlocResultMixinStr", "_userinfo", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py have_info;
                        py have_password;
                        py hostinfo;
                        py netloc;
                        py password;
                        py userinfo;
                        py username;
                        netloc = self.attr("netloc");
                        auto const items6 = unpack(netloc.method("rpartition", { S<"@">() }), 3);
                        userinfo = items6[0];
                        have_info = items6[1];
                        hostinfo = items6[2];
                        if (have_info.truthy())
                        {
                            auto const items7 = unpack(userinfo.method("partition", { S<":">() }), 3);
                            username = items7[0];
                            have_password = items7[1];
                            password = items7[2];
                            if (!have_password.truthy())
                            {
                                password = none();
                            }
                        }
                        else
                        {
                            py const value8 = none();
                            username = value8;
                            password = value8;
                        }
                        return py_tuple({ username, password });
                    })) },
                { "_hostinfo", B<"property">()(w.method("_NetlocResultMixinStr", "_hostinfo", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _;
                        py bracketed;
                        py have_open_br;
                        py hostinfo;
                        py hostname;
                        py netloc;
                        py port;
                        netloc = self.attr("netloc");
                        auto const items9 = unpack(netloc.method("rpartition", { S<"@">() }), 3);
                        _ = items9[0];
                        _ = items9[1];
                        hostinfo = items9[2];
                        auto const items10 = unpack(hostinfo.method("partition", { S<"[">() }), 3);
                        _ = items10[0];
                        have_open_br = items10[1];
                        bracketed = items10[2];
                        if (have_open_br.truthy())
                        {
                            auto const items11 = unpack(bracketed.method("partition", { S<"]">() }), 3);
                            hostname = items11[0];
                            _ = items11[1];
                            port = items11[2];
                            auto const items12 = unpack(port.method("partition", { S<":">() }), 3);
                            _ = items12[0];
                            _ = items12[1];
                            port = items12[2];
                        }
                        else
                        {
                            auto const items13 = unpack(hostinfo.method("partition", { S<":">() }), 3);
                            hostname = items13[0];
                            _ = items13[1];
                            port = items13[2];
                        }
                        if (!port.truthy())
                        {
                            port = none();
                        }
                        return py_tuple({ hostname, port });
                    })) }, }));

            define("urllib.parse", "_NetlocResultMixinBytes", w.make_class("_NetlocResultMixinBytes", { G<"urllib.parse._NetlocResultMixinBase">(), G<"urllib.parse._ResultMixinBytes">() }, {
                { "__slots__", py_tuple() },
                { "_userinfo", B<"property">()(w.method("_NetlocResultMixinBytes", "_userinfo", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py have_info;
                        py have_password;
                        py hostinfo;
                        py netloc;
                        py password;
                        py userinfo;
                        py username;
                        netloc = self.attr("netloc");
                        auto const items14 = unpack(netloc.method("rpartition", { py{ make_bytes(std::string{ "\x40", 1 }) } }), 3);
                        userinfo = items14[0];
                        have_info = items14[1];
                        hostinfo = items14[2];
                        if (have_info.truthy())
                        {
                            auto const items15 = unpack(userinfo.method("partition", { py{ make_bytes(std::string{ "\x3a", 1 }) } }), 3);
                            username = items15[0];
                            have_password = items15[1];
                            password = items15[2];
                            if (!have_password.truthy())
                            {
                                password = none();
                            }
                        }
                        else
                        {
                            py const value16 = none();
                            username = value16;
                            password = value16;
                        }
                        return py_tuple({ username, password });
                    })) },
                { "_hostinfo", B<"property">()(w.method("_NetlocResultMixinBytes", "_hostinfo", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _;
                        py bracketed;
                        py have_open_br;
                        py hostinfo;
                        py hostname;
                        py netloc;
                        py port;
                        netloc = self.attr("netloc");
                        auto const items17 = unpack(netloc.method("rpartition", { py{ make_bytes(std::string{ "\x40", 1 }) } }), 3);
                        _ = items17[0];
                        _ = items17[1];
                        hostinfo = items17[2];
                        auto const items18 = unpack(hostinfo.method("partition", { py{ make_bytes(std::string{ "\x5b", 1 }) } }), 3);
                        _ = items18[0];
                        have_open_br = items18[1];
                        bracketed = items18[2];
                        if (have_open_br.truthy())
                        {
                            auto const items19 = unpack(bracketed.method("partition", { py{ make_bytes(std::string{ "\x5d", 1 }) } }), 3);
                            hostname = items19[0];
                            _ = items19[1];
                            port = items19[2];
                            auto const items20 = unpack(port.method("partition", { py{ make_bytes(std::string{ "\x3a", 1 }) } }), 3);
                            _ = items20[0];
                            _ = items20[1];
                            port = items20[2];
                        }
                        else
                        {
                            auto const items21 = unpack(hostinfo.method("partition", { py{ make_bytes(std::string{ "\x3a", 1 }) } }), 3);
                            hostname = items21[0];
                            _ = items21[1];
                            port = items21[2];
                        }
                        if (!port.truthy())
                        {
                            port = none();
                        }
                        return py_tuple({ hostname, port });
                    })) }, }));

            define("urllib.parse", "_DefragResultBase", (Mod<"collections">().attr("namedtuple"))(S<"DefragResult">(), S<"url fragment">()));
            define("urllib.parse", "_SplitResultBase", (Mod<"collections">().attr("namedtuple"))(S<"SplitResult">(), S<"scheme netloc path query fragment">()));
            define("urllib.parse", "_ParseResultBase", (Mod<"collections">().attr("namedtuple"))(S<"ParseResult">(), S<"scheme netloc path params query fragment">()));
            (G<"urllib.parse._DefragResultBase">()).set_attr("__doc__", S<"\nDefragResult(url, fragment)\n\nA 2-tuple that contains the url without fragment identifier and the fragment\nidentifier as a separate argument.\n">());
            (G<"urllib.parse._DefragResultBase">()).attr("url").set_attr("__doc__", S<"The URL with no fragment identifier.">());
            (G<"urllib.parse._DefragResultBase">()).attr("fragment").set_attr("__doc__", S<"\nFragment identifier separated from URL, that allows indirect identification of a\nsecondary resource by reference to a primary resource and additional identifying\ninformation.\n">());
            (G<"urllib.parse._SplitResultBase">()).set_attr("__doc__", S<"\nSplitResult(scheme, netloc, path, query, fragment)\n\nA 5-tuple that contains the different components of a URL. Similar to\nParseResult, but does not split params.\n">());
            (G<"urllib.parse._SplitResultBase">()).attr("scheme").set_attr("__doc__", S<"Specifies URL scheme for the request.">());
            (G<"urllib.parse._SplitResultBase">()).attr("netloc").set_attr("__doc__", S<"\nNetwork location where the request is made to.\n">());
            (G<"urllib.parse._SplitResultBase">()).attr("path").set_attr("__doc__", S<"\nThe hierarchical path, such as the path to a file to download.\n">());
            (G<"urllib.parse._SplitResultBase">()).attr("query").set_attr("__doc__", S<"\nThe query component, that contains non-hierarchical data, that along with data\nin path component, identifies a resource in the scope of URI's scheme and\nnetwork location.\n">());
            (G<"urllib.parse._SplitResultBase">()).attr("fragment").set_attr("__doc__", S<"\nFragment identifier, that allows indirect identification of a secondary resource\nby reference to a primary resource and additional identifying information.\n">());
            (G<"urllib.parse._ParseResultBase">()).set_attr("__doc__", S<"\nParseResult(scheme, netloc, path, params, query, fragment)\n\nA 6-tuple that contains components of a parsed URL.\n">());
            (G<"urllib.parse._ParseResultBase">()).attr("scheme").set_attr("__doc__", (G<"urllib.parse._SplitResultBase">()).attr("scheme").attr("__doc__"));
            (G<"urllib.parse._ParseResultBase">()).attr("netloc").set_attr("__doc__", (G<"urllib.parse._SplitResultBase">()).attr("netloc").attr("__doc__"));
            (G<"urllib.parse._ParseResultBase">()).attr("path").set_attr("__doc__", (G<"urllib.parse._SplitResultBase">()).attr("path").attr("__doc__"));
            (G<"urllib.parse._ParseResultBase">()).attr("params").set_attr("__doc__", S<"\nParameters for last path element used to dereference the URI in order to provide\naccess to perform some operation on the resource.\n">());
            (G<"urllib.parse._ParseResultBase">()).attr("query").set_attr("__doc__", (G<"urllib.parse._SplitResultBase">()).attr("query").attr("__doc__"));
            (G<"urllib.parse._ParseResultBase">()).attr("fragment").set_attr("__doc__", (G<"urllib.parse._SplitResultBase">()).attr("fragment").attr("__doc__"));
            // For backwards compatibility, alias _NetlocResultMixinStr
            // ResultBase is no longer part of the documented API, but it is
            // retained since deprecating it isn't worth the hassle
            define("urllib.parse", "ResultBase", G<"urllib.parse._NetlocResultMixinStr">());
            // Structured result objects for string data
            define("urllib.parse", "DefragResult", w.make_class("DefragResult", { G<"urllib.parse._DefragResultBase">(), G<"urllib.parse._ResultMixinStr">() }, {
                { "__slots__", py_tuple() },
                { "geturl", w.method("DefragResult", "geturl", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("fragment").truthy())
                        {
                            return (self.attr("url") + (S<"#">())) + self.attr("fragment");
                        }
                        else
                        {
                            return self.attr("url");
                        }
                        return none();
                    }) }, }));

            define("urllib.parse", "SplitResult", w.make_class("SplitResult", { G<"urllib.parse._SplitResultBase">(), G<"urllib.parse._NetlocResultMixinStr">() }, {
                { "__slots__", py_tuple() },
                { "geturl", w.method("SplitResult", "geturl", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"urllib.parse.urlunsplit">())(self);
                    }) }, }));

            define("urllib.parse", "ParseResult", w.make_class("ParseResult", { G<"urllib.parse._ParseResultBase">(), G<"urllib.parse._NetlocResultMixinStr">() }, {
                { "__slots__", py_tuple() },
                { "geturl", w.method("ParseResult", "geturl", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"urllib.parse.urlunparse">())(self);
                    }) }, }));

            // Structured result objects for bytes data
            define("urllib.parse", "DefragResultBytes", w.make_class("DefragResultBytes", { G<"urllib.parse._DefragResultBase">(), G<"urllib.parse._ResultMixinBytes">() }, {
                { "__slots__", py_tuple() },
                { "geturl", w.method("DefragResultBytes", "geturl", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("fragment").truthy())
                        {
                            return (self.attr("url") + py{ make_bytes(std::string{ "\x23", 1 }) }) + self.attr("fragment");
                        }
                        else
                        {
                            return self.attr("url");
                        }
                        return none();
                    }) }, }));

            define("urllib.parse", "SplitResultBytes", w.make_class("SplitResultBytes", { G<"urllib.parse._SplitResultBase">(), G<"urllib.parse._NetlocResultMixinBytes">() }, {
                { "__slots__", py_tuple() },
                { "geturl", w.method("SplitResultBytes", "geturl", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"urllib.parse.urlunsplit">())(self);
                    }) }, }));

            define("urllib.parse", "ParseResultBytes", w.make_class("ParseResultBytes", { G<"urllib.parse._ParseResultBase">(), G<"urllib.parse._NetlocResultMixinBytes">() }, {
                { "__slots__", py_tuple() },
                { "geturl", w.method("ParseResultBytes", "geturl", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"urllib.parse.urlunparse">())(self);
                    }) }, }));

            // Set up the encode/decode result pairs
            define("urllib.parse", "_fix_result_transcoding", w.function("_fix_result_transcoding", "_fix_result_transcoding", {  }, [](arguments a) -> py
                {
                    py _decoded;
                    py _encoded;
                    py _result_pairs;
                    _result_pairs = py_tuple({ py_tuple({ G<"urllib.parse.DefragResult">(), G<"urllib.parse.DefragResultBytes">() }), py_tuple({ G<"urllib.parse.SplitResult">(), G<"urllib.parse.SplitResultBytes">() }), py_tuple({ G<"urllib.parse.ParseResult">(), G<"urllib.parse.ParseResultBytes">() }) });
                    for (py const& item : iterate{ _result_pairs })
                    {
                        auto const items23 = unpack(item, 2);
                        _decoded = items23[0];
                        _encoded = items23[1];
                        _decoded.set_attr("_encoded_counterpart", _encoded);
                        _encoded.set_attr("_decoded_counterpart", _decoded);
                    }
                    return none();
                }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (G<"urllib.parse._fix_result_transcoding">())();
                    return none();
                }))();
            undefine("urllib.parse", "_fix_result_transcoding");
            define("urllib.parse", "urlparse", w.function("urlparse", "urlparse", { py_positional("url"), py_positional("scheme", S<"">()), py_positional("allow_fragments", boolean(true)) }, [](arguments a) -> py
                {
                    py url = a[0];
                    py scheme = a[1];
                    py allow_fragments = a[2];
                    py _coerce_result;
                    py fragment;
                    py netloc;
                    py params;
                    py query;
                    py result;
                    py splitresult;
                    auto const items24 = unpack((G<"urllib.parse._coerce_args">())(url, scheme), 3);
                    url = items24[0];
                    scheme = items24[1];
                    _coerce_result = items24[2];
                    splitresult = (G<"urllib.parse.urlsplit">())(url, scheme, allow_fragments);
                    auto const items25 = unpack(splitresult, 5);
                    scheme = items25[0];
                    netloc = items25[1];
                    url = items25[2];
                    query = items25[3];
                    fragment = items25[4];
                    if (((G<"urllib.parse.uses_params">()).contains(scheme) && url.contains((S<";">()))))
                    {
                        auto const items26 = unpack((G<"urllib.parse._splitparams">())(url), 2);
                        url = items26[0];
                        params = items26[1];
                    }
                    else
                    {
                        params = S<"">();
                    }
                    result = (G<"urllib.parse.ParseResult">())(scheme, netloc, url, params, query, fragment);
                    return _coerce_result(result);
                }));

            define("urllib.parse", "_splitparams", w.function("_splitparams", "_splitparams", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    py i;
                    if (url.contains((S<"/">())))
                    {
                        i = url.method("find", { S<";">(), url.method("rfind", { S<"/">() }) });
                        if (i < (I<0>()))
                        {
                            return py_tuple({ url, S<"">() });
                        }
                    }
                    else
                    {
                        i = url.method("find", { S<";">() });
                    }
                    return py_tuple({ url[B<"slice">()(none(), i, none())], url[B<"slice">()(i + (I<1>()), none(), none())] });
                }));

            define("urllib.parse", "_splitnetloc", w.function("_splitnetloc", "_splitnetloc", { py_positional("url"), py_positional("start", I<0>()) }, [](arguments a) -> py
                {
                    py url = a[0];
                    py start = a[1];
                    py c;
                    py delim;
                    py wdelim;
                    delim = py{ url.size() };
                    for (py const& item : iterate{ S<"/?#">() })
                    {
                        c = item;
                        wdelim = url.method("find", { c, start });
                        if (wdelim >= (I<0>()))
                        {
                            delim = (B<"min">())(delim, wdelim);
                        }
                    }
                    return py_tuple({ url[B<"slice">()(start, delim, none())], url[B<"slice">()(delim, none(), none())] });
                }));

            define("urllib.parse", "_checknetloc", w.function("_checknetloc", "_checknetloc", { py_positional("netloc") }, [](arguments a) -> py
                {
                    py netloc = a[0];
                    py c;
                    py n;
                    py netloc2;
                    py unicodedata;
                    if (((!netloc.truthy()) || netloc.method("isascii").truthy()))
                    {
                        return none();
                    }
                    // looking for characters like \u2100 that expand to 'a/c'
                    // IDNA uses NFKC equivalence, so normalize for this check
                    unicodedata = module_of("unicodedata");
                    n = netloc.method("replace", { S<"@">(), S<"">() });
                    n = n.method("replace", { S<":">(), S<"">() });
                    n = n.method("replace", { S<"#">(), S<"">() });
                    n = n.method("replace", { S<"?">(), S<"">() });
                    netloc2 = unicodedata.method("normalize", { S<"NFKC">(), n });
                    if (n == netloc2)
                    {
                        return none();
                    }
                    for (py const& item : iterate{ S<"/?#@:">() })
                    {
                        c = item;
                        if (netloc2.contains(c))
                        {
                            py_raise((B<"ValueError">())((((S<"netloc '">()) + netloc) + (S<"' contains invalid ">())) + (S<"characters under NFKC normalization">())));
                        }
                    }
                    return none();
                }));

            define("urllib.parse", "_check_bracketed_netloc", w.function("_check_bracketed_netloc", "_check_bracketed_netloc", { py_positional("netloc") }, [](arguments a) -> py
                {
                    py netloc = a[0];
                    py _;
                    py before_bracket;
                    py bracketed;
                    py have_open_br;
                    py hostname;
                    py hostname_and_port;
                    py port;
                    hostname_and_port = netloc.method("rpartition", { S<"@">() })[I<2>()];
                    auto const items29 = unpack(hostname_and_port.method("partition", { S<"[">() }), 3);
                    before_bracket = items29[0];
                    have_open_br = items29[1];
                    bracketed = items29[2];
                    if (have_open_br.truthy())
                    {
                        // No data is allowed before a bracket.
                        if (before_bracket.truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Invalid IPv6 URL">()));
                        }
                        auto const items30 = unpack(bracketed.method("partition", { S<"]">() }), 3);
                        hostname = items30[0];
                        _ = items30[1];
                        port = items30[2];
                        // No data is allowed after the bracket but before the port delimiter.
                        if ((port.truthy() && (!port.method("startswith", { S<":">() }).truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"Invalid IPv6 URL">()));
                        }
                    }
                    else
                    {
                        auto const items31 = unpack(hostname_and_port.method("partition", { S<":">() }), 3);
                        hostname = items31[0];
                        _ = items31[1];
                        port = items31[2];
                    }
                    (G<"urllib.parse._check_bracketed_host">())(hostname);
                    return none();
                }));

            // Valid bracketed hosts are defined in
            // https://www.rfc-editor.org/rfc/rfc3986#page-49 and https://url.spec.whatwg.org/
            define("urllib.parse", "_check_bracketed_host", w.function("_check_bracketed_host", "_check_bracketed_host", { py_positional("hostname") }, [](arguments a) -> py
                {
                    py hostname = a[0];
                    py ip;
                    if (hostname.method("startswith", { py_tuple({ S<"v">(), S<"V">() }) }).truthy())
                    {
                        if (!(Mod<"re">().attr("match"))(S<"\\A[vV][a-fA-F0-9]+\\..+\\Z">(), hostname).truthy())
                        {
                            py_raise((B<"ValueError">())(S("IPvFuture address is invalid")));
                        }
                    }
                    else
                    {
                        ip = (Mod<"ipaddress">().attr("ip_address"))(hostname);
                        if (isinstance(ip, Mod<"ipaddress">().attr("IPv4Address")))
                        {
                            py_raise((B<"ValueError">())(S("An IPv4 address cannot be in brackets")));
                        }
                    }
                    return none();
                }));

            // typed=True avoids BytesWarnings being emitted during cache key
            // comparison since this API supports both bytes and str input.
            define("urllib.parse", "urlsplit", (Mod<"functools">()).method("lru_cache", {  }, { { "typed", boolean(true) } })(w.function("urlsplit", "urlsplit", { py_positional("url"), py_positional("scheme", S<"">()), py_positional("allow_fragments", boolean(true)) }, [](arguments a) -> py
                {
                    py url = a[0];
                    py scheme = a[1];
                    py allow_fragments = a[2];
                    py _coerce_result;
                    py b;
                    py c;
                    py fragment;
                    py i;
                    py netloc;
                    py query;
                    py v;
                    auto const items32 = unpack((G<"urllib.parse._coerce_args">())(url, scheme), 3);
                    url = items32[0];
                    scheme = items32[1];
                    _coerce_result = items32[2];
                    // Only lstrip url as some applications rely on preserving trailing space.
                    // (https://url.spec.whatwg.org/#concept-basic-url-parser would strip both)
                    url = url.method("lstrip", { G<"urllib.parse._WHATWG_C0_CONTROL_OR_SPACE">() });
                    scheme = scheme.method("strip", { G<"urllib.parse._WHATWG_C0_CONTROL_OR_SPACE">() });
                    for (py const& item : iterate{ G<"urllib.parse._UNSAFE_URL_BYTES_TO_REMOVE">() })
                    {
                        b = item;
                        url = url.method("replace", { b, S<"">() });
                        scheme = scheme.method("replace", { b, S<"">() });
                    }
                    allow_fragments = (B<"bool">())(allow_fragments);
                    py const value34 = S<"">();
                    netloc = value34;
                    query = value34;
                    fragment = value34;
                    i = url.method("find", { S<":">() });
                    if (((i > (I<0>())) && url[I<0>()].method("isascii").truthy() && url[I<0>()].method("isalpha").truthy()))
                    {
                        bool broke35 = false;
                        for (py const& item : iterate{ url[B<"slice">()(none(), i, none())] })
                        {
                            c = item;
                            if (!(G<"urllib.parse.scheme_chars">()).contains(c))
                            {
                                broke35 = true;
                                break;
                            }
                        }
                        if (!broke35)
                        {
                            auto const items36 = unpack(py_tuple({ url[B<"slice">()(none(), i, none())].method("lower"), url[B<"slice">()(i + (I<1>()), none(), none())] }), 2);
                            scheme = items36[0];
                            url = items36[1];
                        }
                    }
                    if (url[B<"slice">()(none(), I<2>(), none())] == (S<"//">()))
                    {
                        auto const items37 = unpack((G<"urllib.parse._splitnetloc">())(url, I<2>()), 2);
                        netloc = items37[0];
                        url = items37[1];
                        if ((([&]() -> py { py const t = boolean(netloc.contains((S<"[">()))); return !t.truthy() ? t : boolean(!netloc.contains((S<"]">()))); }()).truthy() || ([&]() -> py { py const t = boolean(netloc.contains((S<"]">()))); return !t.truthy() ? t : boolean(!netloc.contains((S<"[">()))); }()).truthy()))
                        {
                            py_raise((B<"ValueError">())(S<"Invalid IPv6 URL">()));
                        }
                        if ((netloc.contains((S<"[">())) && netloc.contains((S<"]">()))))
                        {
                            (G<"urllib.parse._check_bracketed_netloc">())(netloc);
                        }
                    }
                    if ((allow_fragments.truthy() && url.contains((S<"#">()))))
                    {
                        auto const items38 = unpack(url.method("split", { S<"#">(), I<1>() }), 2);
                        url = items38[0];
                        fragment = items38[1];
                    }
                    if (url.contains((S<"?">())))
                    {
                        auto const items39 = unpack(url.method("split", { S<"?">(), I<1>() }), 2);
                        url = items39[0];
                        query = items39[1];
                    }
                    (G<"urllib.parse._checknetloc">())(netloc);
                    v = (G<"urllib.parse.SplitResult">())(scheme, netloc, url, query, fragment);
                    return _coerce_result(v);
                })));

            define("urllib.parse", "urlunparse", w.function("urlunparse", "urlunparse", { py_positional("components") }, [](arguments a) -> py
                {
                    py components = a[0];
                    py _coerce_result;
                    py fragment;
                    py netloc;
                    py params;
                    py query;
                    py scheme;
                    py url;
                    auto const items40 = unpack(call_ex((G<"urllib.parse._coerce_args">()), {  }, components, py{}), 7);
                    scheme = items40[0];
                    netloc = items40[1];
                    url = items40[2];
                    params = items40[3];
                    query = items40[4];
                    fragment = items40[5];
                    _coerce_result = items40[6];
                    if (params.truthy())
                    {
                        url = (S<"%s;%s">()) % py_tuple({ url, params });
                    }
                    return _coerce_result((G<"urllib.parse.urlunsplit">())(py_tuple({ scheme, netloc, url, query, fragment })));
                }));

            define("urllib.parse", "urlunsplit", w.function("urlunsplit", "urlunsplit", { py_positional("components") }, [](arguments a) -> py
                {
                    py components = a[0];
                    py _coerce_result;
                    py fragment;
                    py netloc;
                    py query;
                    py scheme;
                    py url;
                    auto const items41 = unpack(call_ex((G<"urllib.parse._coerce_args">()), {  }, components, py{}), 6);
                    scheme = items41[0];
                    netloc = items41[1];
                    url = items41[2];
                    query = items41[3];
                    fragment = items41[4];
                    _coerce_result = items41[5];
                    if (netloc.truthy())
                    {
                        if ((url.truthy() && (url[B<"slice">()(none(), I<1>(), none())] != (S<"/">()))))
                        {
                            url = (S<"/">()) + url;
                        }
                        url = ((S<"//">()) + netloc) + url;
                    }
                    else if (url[B<"slice">()(none(), I<2>(), none())] == (S<"//">()))
                    {
                        url = (S<"//">()) + url;
                    }
                    else if ((scheme.truthy() && (G<"urllib.parse.uses_netloc">()).contains(scheme) && ([&]() -> py { py const t = boolean(!url.truthy()); return t.truthy() ? t : boolean(url[B<"slice">()(none(), I<1>(), none())] == (S<"/">())); }()).truthy()))
                    {
                        url = (S<"//">()) + url;
                    }
                    if (scheme.truthy())
                    {
                        url = (scheme + (S<":">())) + url;
                    }
                    if (query.truthy())
                    {
                        url = (url + (S<"?">())) + query;
                    }
                    if (fragment.truthy())
                    {
                        url = (url + (S<"#">())) + fragment;
                    }
                    return _coerce_result(url);
                }));

            define("urllib.parse", "urljoin", w.function("urljoin", "urljoin", { py_positional("base"), py_positional("url"), py_positional("allow_fragments", boolean(true)) }, [](arguments a) -> py
                {
                    py base = a[0];
                    py url = a[1];
                    py allow_fragments = a[2];
                    py _coerce_result;
                    py base_parts;
                    py bfragment;
                    py bnetloc;
                    py bparams;
                    py bpath;
                    py bquery;
                    py bscheme;
                    py fragment;
                    py netloc;
                    py params;
                    py path;
                    py query;
                    py resolved_path;
                    py scheme;
                    py seg;
                    py segments;
                    if (!base.truthy())
                    {
                        return url;
                    }
                    if (!url.truthy())
                    {
                        return base;
                    }
                    auto const items42 = unpack((G<"urllib.parse._coerce_args">())(base, url), 3);
                    base = items42[0];
                    url = items42[1];
                    _coerce_result = items42[2];
                    auto const items43 = unpack((G<"urllib.parse.urlparse">())(base, S<"">(), allow_fragments), 6);
                    bscheme = items43[0];
                    bnetloc = items43[1];
                    bpath = items43[2];
                    bparams = items43[3];
                    bquery = items43[4];
                    bfragment = items43[5];
                    auto const items44 = unpack((G<"urllib.parse.urlparse">())(url, bscheme, allow_fragments), 6);
                    scheme = items44[0];
                    netloc = items44[1];
                    path = items44[2];
                    params = items44[3];
                    query = items44[4];
                    fragment = items44[5];
                    if (((scheme != bscheme) || (!(G<"urllib.parse.uses_relative">()).contains(scheme))))
                    {
                        return _coerce_result(url);
                    }
                    if ((G<"urllib.parse.uses_netloc">()).contains(scheme))
                    {
                        if (netloc.truthy())
                        {
                            return _coerce_result((G<"urllib.parse.urlunparse">())(py_tuple({ scheme, netloc, path, params, query, fragment })));
                        }
                        netloc = bnetloc;
                    }
                    if (((!path.truthy()) && (!params.truthy())))
                    {
                        path = bpath;
                        params = bparams;
                        if (!query.truthy())
                        {
                            query = bquery;
                        }
                        return _coerce_result((G<"urllib.parse.urlunparse">())(py_tuple({ scheme, netloc, path, params, query, fragment })));
                    }
                    base_parts = bpath.method("split", { S<"/">() });
                    if (base_parts[I<-1>()] != (S<"">()))
                    {
                        // the last item is not a directory, so will not be taken into account
                        // in resolving the relative path
                        del_item(base_parts, I<-1>());
                    }
                    // for rfc3986, ignore all base path should the first character be root.
                    if (path[B<"slice">()(none(), I<1>(), none())] == (S<"/">()))
                    {
                        segments = path.method("split", { S<"/">() });
                    }
                    else
                    {
                        segments = base_parts + path.method("split", { S<"/">() });
                        // filter out elements that would cause redundant slashes on re-joining
                        // the resolved_path
                        segments.set_item(B<"slice">()(I<1>(), I<-1>(), none()), (B<"filter">())(none(), segments[B<"slice">()(I<1>(), I<-1>(), none())]));
                    }
                    resolved_path = py_list();
                    for (py const& item : iterate{ segments })
                    {
                        seg = item;
                        if (seg == (S<"..">()))
                        {
                            try
                            {
                                resolved_path.method("pop");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler46;
                                if (matches(handler46.exception, B<"IndexError">()))
                                {
                                    // ignore any .. segments that would otherwise cause an IndexError
                                    // when popped from resolved_path if resolving for rfc3986
                                }
                                else
                                    py_reraise(handler46.exception);
                            }
                        }
                        else if (seg == (S<".">()))
                        {
                            continue;
                        }
                        else
                        {
                            resolved_path.method("append", { seg });
                        }
                    }
                    if (py_tuple({ S<".">(), S<"..">() }).contains(segments[I<-1>()]))
                    {
                        // do some post-processing here. if the last segment was a relative dir,
                        // then we need to append the trailing '/'
                        resolved_path.method("append", { S<"">() });
                    }
                    return _coerce_result((G<"urllib.parse.urlunparse">())(py_tuple({ scheme, netloc, [&]() -> py { py const t = (S<"/">()).method("join", { resolved_path }); return t.truthy() ? t : S<"/">(); }(), params, query, fragment })));
                }));

            define("urllib.parse", "urldefrag", w.function("urldefrag", "urldefrag", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    py _coerce_result;
                    py a_;
                    py defrag;
                    py frag;
                    py n;
                    py p;
                    py q;
                    py s;
                    auto const items47 = unpack((G<"urllib.parse._coerce_args">())(url), 2);
                    url = items47[0];
                    _coerce_result = items47[1];
                    if (url.contains((S<"#">())))
                    {
                        auto const items48 = unpack((G<"urllib.parse.urlparse">())(url), 6);
                        s = items48[0];
                        n = items48[1];
                        p = items48[2];
                        a_ = items48[3];
                        q = items48[4];
                        frag = items48[5];
                        defrag = (G<"urllib.parse.urlunparse">())(py_tuple({ s, n, p, a_, q, S<"">() }));
                    }
                    else
                    {
                        frag = S<"">();
                        defrag = url;
                    }
                    return _coerce_result((G<"urllib.parse.DefragResult">())(defrag, frag));
                }));

            define("urllib.parse", "_hexdig", S<"0123456789ABCDEFabcdef">());
            define("urllib.parse", "_hextobyte", none());
            define("urllib.parse", "unquote_to_bytes", w.function("unquote_to_bytes", "unquote_to_bytes", { py_positional("string") }, [](arguments a) -> py
                {
                    py string = a[0];
                    return (B<"bytes">())((G<"urllib.parse._unquote_impl">())(string));
                }));

            define("urllib.parse", "_unquote_impl", w.function("_unquote_impl", "_unquote_impl", { py_positional("string") }, [](arguments a) -> py
                {
                    py string = a[0];
                    py append;
                    py bits;
                    py item_;
                    py res;
                    if (!string.truthy())
                    {
                        // Is it a string-like object?
                        string.attr("split");
                        return py{ make_bytes(std::string{ "", 0 }) };
                    }
                    if (isinstance(string, B<"str">()))
                    {
                        string = string.method("encode", { S<"utf-8">() });
                    }
                    bits = string.method("split", { py{ make_bytes(std::string{ "\x25", 1 }) } });
                    if (py{ bits.size() } == (I<1>()))
                    {
                        return string;
                    }
                    res = (B<"bytearray">())(bits[I<0>()]);
                    append = res.attr("extend");
                    // Delay the initialization of the table to not waste memory
                    // if the function is never called
                    if ((G<"urllib.parse._hextobyte">()).is_none())
                    {
                        set_global("urllib.parse", "_hextobyte", [&]() -> py { py const comprehension49 = py_dict(); py a_; py b; for (py const& item : iterate{ G<"urllib.parse._hexdig">() }) { a_ = item; for (py const& item : iterate{ G<"urllib.parse._hexdig">() }) { b = item; comprehension49.set_item((a_ + b).method("encode"), (B<"bytes">()).method("fromhex", { a_ + b })); } } return comprehension49; }());
                    }
                    for (py const& item : iterate{ bits[B<"slice">()(I<1>(), none(), none())] })
                    {
                        item_ = item;
                        try
                        {
                            append((G<"urllib.parse._hextobyte">())[item_[B<"slice">()(none(), I<2>(), none())]]);
                            append(item_[B<"slice">()(I<2>(), none(), none())]);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler51;
                            if (matches(handler51.exception, B<"KeyError">()))
                            {
                                append(py{ make_bytes(std::string{ "\x25", 1 }) });
                                append(item_);
                            }
                            else
                                py_reraise(handler51.exception);
                        }
                    }
                    return res;
                }));

            define("urllib.parse", "_asciire", (Mod<"re">()).method("compile", { S<"([\u0000-]+)">() }));
            define("urllib.parse", "_generate_unquoted_parts", w.function("_generate_unquoted_parts", "_generate_unquoted_parts", { py_positional("string"), py_positional("encoding"), py_positional("errors") }, [](arguments a) -> py
                {
                    return make_native_generator("_generate_unquoted_parts", "_generate_unquoted_parts", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py string = a[0];
                        py encoding = a[1];
                        py errors = a[2];
                        py ascii_match;
                        py end;
                        py previous_match_end;
                        py start;
                        previous_match_end = I<0>();
                        for (py const& item : iterate{ (G<"urllib.parse._asciire">()).method("finditer", { string }) })
                        {
                            ascii_match = item;
                            auto const items53 = unpack(ascii_match.method("span"), 2);
                            start = items53[0];
                            end = items53[1];
                            py_yield(string[B<"slice">()(previous_match_end, start, none())]);
                            // The ascii_match[1] group == string[start:end].
                            py_yield((G<"urllib.parse._unquote_impl">())(ascii_match[I<1>()]).method("decode", { encoding, errors }));
                            previous_match_end = end;
                        }
                        py_yield(string[B<"slice">()(previous_match_end, none(), none())]);
                        return none();
                    });
                }));

            define("urllib.parse", "unquote", w.function("unquote", "unquote", { py_positional("string"), py_positional("encoding", S<"utf-8">()), py_positional("errors", S<"replace">()) }, [](arguments a) -> py
                {
                    py string = a[0];
                    py encoding = a[1];
                    py errors = a[2];
                    if (isinstance(string, B<"bytes">()))
                    {
                        return (G<"urllib.parse._unquote_impl">())(string).method("decode", { encoding, errors });
                    }
                    if (!string.contains((S<"%">())))
                    {
                        // Is it a string-like object?
                        string.attr("split");
                        return string;
                    }
                    if (encoding.is_none())
                    {
                        encoding = S<"utf-8">();
                    }
                    if (errors.is_none())
                    {
                        errors = S<"replace">();
                    }
                    return (S<"">()).method("join", { (G<"urllib.parse._generate_unquoted_parts">())(string, encoding, errors) });
                }));

            define("urllib.parse", "parse_qs", w.function("parse_qs", "parse_qs", { py_positional("qs"), py_positional("keep_blank_values", boolean(false)), py_positional("strict_parsing", boolean(false)), py_positional("encoding", S<"utf-8">()), py_positional("errors", S<"replace">()), py_positional("max_num_fields", none()), py_positional("separator", S<"&">()) }, [](arguments a) -> py
                {
                    py qs = a[0];
                    py keep_blank_values = a[1];
                    py strict_parsing = a[2];
                    py encoding = a[3];
                    py errors = a[4];
                    py max_num_fields = a[5];
                    py separator = a[6];
                    py name;
                    py pairs;
                    py parsed_result;
                    py value;
                    parsed_result = py_dict();
                    pairs = (G<"urllib.parse.parse_qsl">()).call({ qs, keep_blank_values, strict_parsing }, { { "encoding", encoding }, { "errors", errors }, { "max_num_fields", max_num_fields }, { "separator", separator } });
                    for (py const& item : iterate{ pairs })
                    {
                        auto const items55 = unpack(item, 2);
                        name = items55[0];
                        value = items55[1];
                        if (parsed_result.contains(name))
                        {
                            parsed_result[name].method("append", { value });
                        }
                        else
                        {
                            parsed_result.set_item(name, py_list({ value }));
                        }
                    }
                    return parsed_result;
                }));

            define("urllib.parse", "parse_qsl", w.function("parse_qsl", "parse_qsl", { py_positional("qs"), py_positional("keep_blank_values", boolean(false)), py_positional("strict_parsing", boolean(false)), py_positional("encoding", S<"utf-8">()), py_positional("errors", S<"replace">()), py_positional("max_num_fields", none()), py_positional("separator", S<"&">()) }, [](arguments a) -> py
                {
                    py qs = a[0];
                    py keep_blank_values = a[1];
                    py strict_parsing = a[2];
                    auto const encoding = std::make_shared<py>(a[3]);
                    auto const errors = std::make_shared<py>(a[4]);
                    py max_num_fields = a[5];
                    py separator = a[6];
                    py _unquote;
                    py eq;
                    py has_eq;
                    py name;
                    py name_value;
                    py num_fields;
                    py r;
                    py value;
                    if (((!separator.truthy()) || (!isinstance(separator, py_tuple({ B<"str">(), B<"bytes">() })))))
                    {
                        py_raise((B<"ValueError">())(S<"Separator must be of type string or bytes.">()));
                    }
                    if (isinstance(qs, B<"str">()))
                    {
                        if (!isinstance(separator, B<"str">()))
                        {
                            separator = (B<"str">())(separator, S<"ascii">());
                        }
                        eq = S<"=">();
                        _unquote = w.function("_unquote", "parse_qsl.<locals>._unquote", { py_positional("s") }, [encoding, errors](arguments a) -> py
        {
            py s = a[0];
            return (G<"urllib.parse.unquote_plus">()).call({ s }, { { "encoding", (*encoding) }, { "errors", (*errors) } });
        });
                    }
                    else
                    {
                        if (!qs.truthy())
                        {
                            return py_list();
                        }
                        // Use memoryview() to reject integers and iterables,
                        // acceptable by the bytes constructor.
                        qs = (B<"bytes">())((B<"memoryview">())(qs));
                        if (isinstance(separator, B<"str">()))
                        {
                            separator = (B<"bytes">())(separator, S<"ascii">());
                        }
                        eq = py{ make_bytes(std::string{ "\x3d", 1 }) };
                        _unquote = w.function("_unquote", "parse_qsl.<locals>._unquote", { py_positional("s") }, [](arguments a) -> py
        {
            py s = a[0];
            return (G<"urllib.parse.unquote_to_bytes">())(s.method("replace", { py{ make_bytes(std::string{ "\x2b", 1 }) }, py{ make_bytes(std::string{ "\x20", 1 }) } }));
        });
                    }
                    if (!qs.truthy())
                    {
                        return py_list();
                    }
                    // If max_num_fields is defined then check that the number of fields
                    // is less than max_num_fields. This prevents a memory exhaustion DOS
                    // attack via post bodies with many fields.
                    if (!max_num_fields.is_none())
                    {
                        num_fields = (I<1>()) + qs.method("count", { separator });
                        if (max_num_fields < num_fields)
                        {
                            py_raise((B<"ValueError">())(S<"Max number of fields exceeded">()));
                        }
                    }
                    r = py_list();
                    for (py const& item : iterate{ qs.method("split", { separator }) })
                    {
                        name_value = item;
                        if ((name_value.truthy() || strict_parsing.truthy()))
                        {
                            auto const items57 = unpack(name_value.method("partition", { eq }), 3);
                            name = items57[0];
                            has_eq = items57[1];
                            value = items57[2];
                            if (((!has_eq.truthy()) && strict_parsing.truthy()))
                            {
                                py_raise((B<"ValueError">())((S<"bad query field: %r">()) % py_tuple({ name_value })));
                            }
                            if ((value.truthy() || keep_blank_values.truthy()))
                            {
                                name = _unquote(name);
                                value = _unquote(value);
                                r.method("append", { py_tuple({ name, value }) });
                            }
                        }
                    }
                    return r;
                }));

            define("urllib.parse", "unquote_plus", w.function("unquote_plus", "unquote_plus", { py_positional("string"), py_positional("encoding", S<"utf-8">()), py_positional("errors", S<"replace">()) }, [](arguments a) -> py
                {
                    py string = a[0];
                    py encoding = a[1];
                    py errors = a[2];
                    string = string.method("replace", { S<"+">(), S<" ">() });
                    return (G<"urllib.parse.unquote">())(string, encoding, errors);
                }));

            define("urllib.parse", "_ALWAYS_SAFE", (B<"frozenset">())(py{ make_bytes(std::string{ "\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5a\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6a\x6b\x6c\x6d\x6e\x6f\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7a\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x5f\x2e\x2d\x7e", 66 }) }));
            define("urllib.parse", "_ALWAYS_SAFE_BYTES", (B<"bytes">())(G<"urllib.parse._ALWAYS_SAFE">()));
            define("urllib.parse", "__getattr__", w.function("__getattr__", "__getattr__", { py_positional("name") }, [](arguments a) -> py
                {
                    py name = a[0];
                    if (name == (S<"Quoter">()))
                    {
                        (Mod<"warnings">().attr("warn")).call({ S<"Deprecated in 3.11. urllib.parse.Quoter will be removed in Python 3.14. It was not intended to be a public API.">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                        return G<"urllib.parse._Quoter">();
                    }
                    py_raise((B<"AttributeError">())(S(std::string{ "module " } + (S<"urllib.parse">()).repr() + " has no attribute " + name.repr())));
                }));

                // Keeps a cache internally, via __missing__, for efficiency (lookups
                // of cached keys don't call Python code at all).
            define("urllib.parse", "_Quoter", w.make_class("_Quoter", { B<"dict">() }, {
                { "__init__", w.method("_Quoter", "__init__", { py_positional("safe") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py safe = a[1];
                        self.set_attr("safe", (G<"urllib.parse._ALWAYS_SAFE">()).method("union", { safe }));
                        return none();
                    }) },
                { "__repr__", w.method("_Quoter", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S(std::string{ "<Quoter " } + (B<"dict">())(self).repr() + ">");
                    }) },
                { "__missing__", w.method("_Quoter", "__missing__", { py_positional("b") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py b = a[1];
                        py res;
                        res = (self.attr("safe").contains(b) ? (B<"chr">())(b) : (S<"%{:02X}">()).method("format", { b }));
                        self.set_item(b, res);
                        return res;
                    }) }, }));

            define("urllib.parse", "quote", w.function("quote", "quote", { py_positional("string"), py_positional("safe", S<"/">()), py_positional("encoding", none()), py_positional("errors", none()) }, [](arguments a) -> py
                {
                    py string = a[0];
                    py safe = a[1];
                    py encoding = a[2];
                    py errors = a[3];
                    if (isinstance(string, B<"str">()))
                    {
                        if (!string.truthy())
                        {
                            return string;
                        }
                        if (encoding.is_none())
                        {
                            encoding = S<"utf-8">();
                        }
                        if (errors.is_none())
                        {
                            errors = S<"strict">();
                        }
                        string = string.method("encode", { encoding, errors });
                    }
                    else
                    {
                        if (!encoding.is_none())
                        {
                            py_raise((B<"TypeError">())(S<"quote() doesn't support 'encoding' for bytes">()));
                        }
                        if (!errors.is_none())
                        {
                            py_raise((B<"TypeError">())(S<"quote() doesn't support 'errors' for bytes">()));
                        }
                    }
                    return (G<"urllib.parse.quote_from_bytes">())(string, safe);
                }));

            define("urllib.parse", "quote_plus", w.function("quote_plus", "quote_plus", { py_positional("string"), py_positional("safe", S<"">()), py_positional("encoding", none()), py_positional("errors", none()) }, [](arguments a) -> py
                {
                    py string = a[0];
                    py safe = a[1];
                    py encoding = a[2];
                    py errors = a[3];
                    py space;
                    // Check if ' ' in string, where string may either be a str or bytes.  If
                    // there are no spaces, the regular quote will produce the right answer.
                    if ((([&]() -> py { py const t = boolean(isinstance(string, B<"str">())); return !t.truthy() ? t : boolean(!string.contains((S<" ">()))); }()).truthy() || ([&]() -> py { py const t = boolean(isinstance(string, B<"bytes">())); return !t.truthy() ? t : boolean(!string.contains(py{ make_bytes(std::string{ "\x20", 1 }) })); }()).truthy()))
                    {
                        return (G<"urllib.parse.quote">())(string, safe, encoding, errors);
                    }
                    if (isinstance(safe, B<"str">()))
                    {
                        space = S<" ">();
                    }
                    else
                    {
                        space = py{ make_bytes(std::string{ "\x20", 1 }) };
                    }
                    string = (G<"urllib.parse.quote">())(string, safe + space, encoding, errors);
                    return string.method("replace", { S<" ">(), S<"+">() });
                }));

            // Expectation: A typical program is unlikely to create more than 5 of these.
            define("urllib.parse", "_byte_quoter_factory", (Mod<"functools">().attr("lru_cache"))(w.function("_byte_quoter_factory", "_byte_quoter_factory", { py_positional("safe") }, [](arguments a) -> py
                {
                    py safe = a[0];
                    return (G<"urllib.parse._Quoter">())(safe).attr("__getitem__");
                })));

            define("urllib.parse", "quote_from_bytes", w.function("quote_from_bytes", "quote_from_bytes", { py_positional("bs"), py_positional("safe", S<"/">()) }, [](arguments a) -> py
                {
                    py bs = a[0];
                    py safe = a[1];
                    py bs_len;
                    py chunk_size;
                    py chunks;
                    py quoter;
                    if (!isinstance(bs, py_tuple({ B<"bytes">(), B<"bytearray">() })))
                    {
                        py_raise((B<"TypeError">())(S<"quote_from_bytes() expected bytes">()));
                    }
                    if (!bs.truthy())
                    {
                        return S<"">();
                    }
                    if (isinstance(safe, B<"str">()))
                    {
                        // Normalize 'safe' by converting to bytes and removing non-ASCII chars
                        safe = safe.method("encode", { S<"ascii">(), S<"ignore">() });
                    }
                    else
                    {
                        // List comprehensions are faster than generator expressions.
                        safe = (B<"bytes">())([&]() -> py { py const comprehension58 = py_list(); py c; for (py const& item : iterate{ safe }) { c = item; if (!(c < (I<128>()))) continue; comprehension58.method("append", { c }); } return comprehension58; }());
                    }
                    if (!bs.method("rstrip", { (G<"urllib.parse._ALWAYS_SAFE_BYTES">()) + safe }).truthy())
                    {
                        return bs.method("decode");
                    }
                    quoter = (G<"urllib.parse._byte_quoter_factory">())(safe);
                    if (([&]() -> py { bs_len = py{ bs.size() }; return bs_len; }()) < (I<200000>()))
                    {
                        return (S<"">()).method("join", { (B<"map">())(quoter, bs) });
                    }
                    else
                    {
                        // This saves memory - https://github.com/python/cpython/issues/95865
                        chunk_size = (Mod<"math">().attr("isqrt"))(bs_len);
                        chunks = [&]() -> py { py const comprehension59 = py_list(); py i; for (py const& item : iterate{ (B<"range">())(I<0>(), bs_len, chunk_size) }) { i = item; comprehension59.method("append", { (S<"">()).method("join", { (B<"map">())(quoter, bs[B<"slice">()(i, i + chunk_size, none())]) }) }); } return comprehension59; }();
                        return (S<"">()).method("join", { chunks });
                    }
                    return none();
                }));

            define("urllib.parse", "urlencode", w.function("urlencode", "urlencode", { py_positional("query"), py_positional("doseq", boolean(false)), py_positional("safe", S<"">()), py_positional("encoding", none()), py_positional("errors", none()), py_positional("quote_via", G<"urllib.parse.quote_plus">()) }, [](arguments a) -> py
                {
                    py query = a[0];
                    py doseq = a[1];
                    py safe = a[2];
                    py encoding = a[3];
                    py errors = a[4];
                    py quote_via = a[5];
                    py elt;
                    py err;
                    py k;
                    py l;
                    py v;
                    py x;
                    if (has_attr(query, "items"))
                    {
                        query = query.method("items");
                    }
                    else
                    {
                        // It's a bother at times that strings and string-like objects are
                        // sequences.
                        try
                        {
                            // non-sequence items should not work with len()
                            // non-empty strings will fail this
                            if ((py{ query.size() }.truthy() && (!isinstance(query[I<0>()], B<"tuple">()))))
                            {
                                py_raise(B<"TypeError">());
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler60;
                            // Zero-length sequences of all types will get here and succeed,
                            // but that's a minor nit.  Since the original implementation
                            // allowed empty dicts that type of behavior probably should be
                            // preserved for consistency
                            if (matches(handler60.exception, B<"TypeError">()))
                            {
                                err = handler60.exception;
                                fn_raise_from((B<"TypeError">())(S<"not a valid non-string sequence or mapping object">()).get(), err.get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler60.exception);
                        }
                    }
                    l = py_list();
                    if (!doseq.truthy())
                    {
                        for (py const& item : iterate{ query })
                        {
                            auto const items62 = unpack(item, 2);
                            k = items62[0];
                            v = items62[1];
                            if (isinstance(k, B<"bytes">()))
                            {
                                k = quote_via(k, safe);
                            }
                            else
                            {
                                k = quote_via((B<"str">())(k), safe, encoding, errors);
                            }
                            if (isinstance(v, B<"bytes">()))
                            {
                                v = quote_via(v, safe);
                            }
                            else
                            {
                                v = quote_via((B<"str">())(v), safe, encoding, errors);
                            }
                            l.method("append", { (k + (S<"=">())) + v });
                        }
                    }
                    else
                    {
                        for (py const& item : iterate{ query })
                        {
                            auto const items64 = unpack(item, 2);
                            k = items64[0];
                            v = items64[1];
                            if (isinstance(k, B<"bytes">()))
                            {
                                k = quote_via(k, safe);
                            }
                            else
                            {
                                k = quote_via((B<"str">())(k), safe, encoding, errors);
                            }
                            if (isinstance(v, B<"bytes">()))
                            {
                                v = quote_via(v, safe);
                                l.method("append", { (k + (S<"=">())) + v });
                            }
                            else if (isinstance(v, B<"str">()))
                            {
                                v = quote_via(v, safe, encoding, errors);
                                l.method("append", { (k + (S<"=">())) + v });
                            }
                            else
                            {
                                {
                                    bool otherwise65 = false;
                                    try
                                    {
                                        // Is this a sufficient test for sequence-ness?
                                        x = py{ v.size() };
                                        otherwise65 = true;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler65;
                                        if (matches(handler65.exception, B<"TypeError">()))
                                        {
                                            // not a sequence
                                            v = quote_via((B<"str">())(v), safe, encoding, errors);
                                            l.method("append", { (k + (S<"=">())) + v });
                                        }
                                        else
                                            py_reraise(handler65.exception);
                                    }
                                    if (otherwise65)
                                    {
                                        // loop over the sequence
                                        for (py const& item : iterate{ v })
                                        {
                                            elt = item;
                                            if (isinstance(elt, B<"bytes">()))
                                            {
                                                elt = quote_via(elt, safe);
                                            }
                                            else
                                            {
                                                elt = quote_via((B<"str">())(elt), safe, encoding, errors);
                                            }
                                            l.method("append", { (k + (S<"=">())) + elt });
                                        }
                                    }
                                }
                            }
                        }
                    }
                    return (S<"&">()).method("join", { l });
                }));

            define("urllib.parse", "to_bytes", w.function("to_bytes", "to_bytes", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    (Mod<"warnings">().attr("warn")).call({ S<"urllib.parse.to_bytes() is deprecated as of 3.8">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    return (G<"urllib.parse._to_bytes">())(url);
                }));

            define("urllib.parse", "_to_bytes", w.function("_to_bytes", "_to_bytes", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    // Most URL schemes require ASCII. If that changes, the conversion
                    // can be relaxed.
                    // XXX get rid of to_bytes()
                    if (isinstance(url, B<"str">()))
                    {
                        try
                        {
                            url = url.method("encode", { S<"ASCII">() }).method("decode");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler67;
                            if (matches(handler67.exception, B<"UnicodeError">()))
                            {
                                py_raise((B<"UnicodeError">())(((S<"URL ">()) + S(url.repr())) + (S<" contains non-ASCII characters">())));
                            }
                            else
                                py_reraise(handler67.exception);
                        }
                    }
                    return url;
                }));

            define("urllib.parse", "unwrap", w.function("unwrap", "unwrap", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    url = (B<"str">())(url).method("strip");
                    if (((url[B<"slice">()(none(), I<1>(), none())] == (S<"<">())) && (url[B<"slice">()(I<-1>(), none(), none())] == (S<">">()))))
                    {
                        url = url[B<"slice">()(I<1>(), I<-1>(), none())].method("strip");
                    }
                    if (url[B<"slice">()(none(), I<4>(), none())] == (S<"URL:">()))
                    {
                        url = url[B<"slice">()(I<4>(), none(), none())].method("strip");
                    }
                    return url;
                }));

            define("urllib.parse", "splittype", w.function("splittype", "splittype", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    (Mod<"warnings">().attr("warn")).call({ S<"urllib.parse.splittype() is deprecated as of 3.8, use urllib.parse.urlparse() instead">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    return (G<"urllib.parse._splittype">())(url);
                }));

            define("urllib.parse", "_typeprog", none());
            define("urllib.parse", "_splittype", w.function("_splittype", "_splittype", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    py data;
                    py match;
                    py scheme;
                    if ((G<"urllib.parse._typeprog">()).is_none())
                    {
                        set_global("urllib.parse", "_typeprog", (Mod<"re">().attr("compile"))(S<"([^/:]+):(.*)">(), Mod<"re">().attr("DOTALL")));
                    }
                    match = (G<"urllib.parse._typeprog">()).method("match", { url });
                    if (match.truthy())
                    {
                        auto const items68 = unpack(match.method("groups"), 2);
                        scheme = items68[0];
                        data = items68[1];
                        return py_tuple({ scheme.method("lower"), data });
                    }
                    return py_tuple({ none(), url });
                }));

            define("urllib.parse", "splithost", w.function("splithost", "splithost", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    (Mod<"warnings">().attr("warn")).call({ S<"urllib.parse.splithost() is deprecated as of 3.8, use urllib.parse.urlparse() instead">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    return (G<"urllib.parse._splithost">())(url);
                }));

            define("urllib.parse", "_hostprog", none());
            define("urllib.parse", "_splithost", w.function("_splithost", "_splithost", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    py host_port;
                    py match;
                    py path;
                    if ((G<"urllib.parse._hostprog">()).is_none())
                    {
                        set_global("urllib.parse", "_hostprog", (Mod<"re">().attr("compile"))(S<"//([^/#?]*)(.*)">(), Mod<"re">().attr("DOTALL")));
                    }
                    match = (G<"urllib.parse._hostprog">()).method("match", { url });
                    if (match.truthy())
                    {
                        auto const items69 = unpack(match.method("groups"), 2);
                        host_port = items69[0];
                        path = items69[1];
                        if ((path.truthy() && (path[I<0>()] != (S<"/">()))))
                        {
                            path = (S<"/">()) + path;
                        }
                        return py_tuple({ host_port, path });
                    }
                    return py_tuple({ none(), url });
                }));

            define("urllib.parse", "splituser", w.function("splituser", "splituser", { py_positional("host") }, [](arguments a) -> py
                {
                    py host = a[0];
                    (Mod<"warnings">().attr("warn")).call({ S<"urllib.parse.splituser() is deprecated as of 3.8, use urllib.parse.urlparse() instead">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    return (G<"urllib.parse._splituser">())(host);
                }));

            define("urllib.parse", "_splituser", w.function("_splituser", "_splituser", { py_positional("host") }, [](arguments a) -> py
                {
                    py host = a[0];
                    py delim;
                    py user;
                    auto const items70 = unpack(host.method("rpartition", { S<"@">() }), 3);
                    user = items70[0];
                    delim = items70[1];
                    host = items70[2];
                    return py_tuple({ (delim.truthy() ? user : none()), host });
                }));

            define("urllib.parse", "splitpasswd", w.function("splitpasswd", "splitpasswd", { py_positional("user") }, [](arguments a) -> py
                {
                    py user = a[0];
                    (Mod<"warnings">().attr("warn")).call({ S<"urllib.parse.splitpasswd() is deprecated as of 3.8, use urllib.parse.urlparse() instead">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    return (G<"urllib.parse._splitpasswd">())(user);
                }));

            define("urllib.parse", "_splitpasswd", w.function("_splitpasswd", "_splitpasswd", { py_positional("user") }, [](arguments a) -> py
                {
                    py user = a[0];
                    py delim;
                    py passwd;
                    auto const items71 = unpack(user.method("partition", { S<":">() }), 3);
                    user = items71[0];
                    delim = items71[1];
                    passwd = items71[2];
                    return py_tuple({ user, (delim.truthy() ? passwd : none()) });
                }));

            define("urllib.parse", "splitport", w.function("splitport", "splitport", { py_positional("host") }, [](arguments a) -> py
                {
                    py host = a[0];
                    (Mod<"warnings">().attr("warn")).call({ S<"urllib.parse.splitport() is deprecated as of 3.8, use urllib.parse.urlparse() instead">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    return (G<"urllib.parse._splitport">())(host);
                }));

            // splittag('/path#tag') --> '/path', 'tag'
            define("urllib.parse", "_portprog", none());
            define("urllib.parse", "_splitport", w.function("_splitport", "_splitport", { py_positional("host") }, [](arguments a) -> py
                {
                    py host = a[0];
                    py match;
                    py port;
                    if ((G<"urllib.parse._portprog">()).is_none())
                    {
                        set_global("urllib.parse", "_portprog", (Mod<"re">().attr("compile"))(S<"(.*):([0-9]*)">(), Mod<"re">().attr("DOTALL")));
                    }
                    match = (G<"urllib.parse._portprog">()).method("fullmatch", { host });
                    if (match.truthy())
                    {
                        auto const items72 = unpack(match.method("groups"), 2);
                        host = items72[0];
                        port = items72[1];
                        if (port.truthy())
                        {
                            return py_tuple({ host, port });
                        }
                    }
                    return py_tuple({ host, none() });
                }));

            define("urllib.parse", "splitnport", w.function("splitnport", "splitnport", { py_positional("host"), py_positional("defport", I<-1>()) }, [](arguments a) -> py
                {
                    py host = a[0];
                    py defport = a[1];
                    (Mod<"warnings">().attr("warn")).call({ S<"urllib.parse.splitnport() is deprecated as of 3.8, use urllib.parse.urlparse() instead">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    return (G<"urllib.parse._splitnport">())(host, defport);
                }));

            define("urllib.parse", "_splitnport", w.function("_splitnport", "_splitnport", { py_positional("host"), py_positional("defport", I<-1>()) }, [](arguments a) -> py
                {
                    py host = a[0];
                    py defport = a[1];
                    py delim;
                    py nport;
                    py port;
                    auto const items73 = unpack(host.method("rpartition", { S<":">() }), 3);
                    host = items73[0];
                    delim = items73[1];
                    port = items73[2];
                    if (!delim.truthy())
                    {
                        host = port;
                    }
                    else if (port.truthy())
                    {
                        if ((port.method("isdigit").truthy() && port.method("isascii").truthy()))
                        {
                            nport = (B<"int">())(port);
                        }
                        else
                        {
                            nport = none();
                        }
                        return py_tuple({ host, nport });
                    }
                    return py_tuple({ host, defport });
                }));

            define("urllib.parse", "splitquery", w.function("splitquery", "splitquery", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    (Mod<"warnings">().attr("warn")).call({ S<"urllib.parse.splitquery() is deprecated as of 3.8, use urllib.parse.urlparse() instead">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    return (G<"urllib.parse._splitquery">())(url);
                }));

            define("urllib.parse", "_splitquery", w.function("_splitquery", "_splitquery", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    py delim;
                    py path;
                    py query;
                    auto const items74 = unpack(url.method("rpartition", { S<"?">() }), 3);
                    path = items74[0];
                    delim = items74[1];
                    query = items74[2];
                    if (delim.truthy())
                    {
                        return py_tuple({ path, query });
                    }
                    return py_tuple({ url, none() });
                }));

            define("urllib.parse", "splittag", w.function("splittag", "splittag", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    (Mod<"warnings">().attr("warn")).call({ S<"urllib.parse.splittag() is deprecated as of 3.8, use urllib.parse.urlparse() instead">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    return (G<"urllib.parse._splittag">())(url);
                }));

            define("urllib.parse", "_splittag", w.function("_splittag", "_splittag", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    py delim;
                    py path;
                    py tag;
                    auto const items75 = unpack(url.method("rpartition", { S<"#">() }), 3);
                    path = items75[0];
                    delim = items75[1];
                    tag = items75[2];
                    if (delim.truthy())
                    {
                        return py_tuple({ path, tag });
                    }
                    return py_tuple({ url, none() });
                }));

            define("urllib.parse", "splitattr", w.function("splitattr", "splitattr", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    (Mod<"warnings">().attr("warn")).call({ S<"urllib.parse.splitattr() is deprecated as of 3.8, use urllib.parse.urlparse() instead">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    return (G<"urllib.parse._splitattr">())(url);
                }));

            define("urllib.parse", "_splitattr", w.function("_splitattr", "_splitattr", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    py words;
                    words = url.method("split", { S<";">() });
                    return py_tuple({ words[I<0>()], words[B<"slice">()(I<1>(), none(), none())] });
                }));

            define("urllib.parse", "splitvalue", w.function("splitvalue", "splitvalue", { py_positional("attr") }, [](arguments a) -> py
                {
                    py attr = a[0];
                    (Mod<"warnings">().attr("warn")).call({ S<"urllib.parse.splitvalue() is deprecated as of 3.8, use urllib.parse.parse_qsl() instead">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    return (G<"urllib.parse._splitvalue">())(attr);
                }));

            define("urllib.parse", "_splitvalue", w.function("_splitvalue", "_splitvalue", { py_positional("attr") }, [](arguments a) -> py
                {
                    py attr = a[0];
                    py delim;
                    py value;
                    auto const items76 = unpack(attr.method("partition", { S<"=">() }), 3);
                    attr = items76[0];
                    delim = items76[1];
                    value = items76[2];
                    return py_tuple({ attr, (delim.truthy() ? value : none()) });
                }));
        }

        bool urllib_parse_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_urllib_parse();
                }
                if (auto const doc = urllib_parse_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("namedtuple", import_attribute("collections", "namedtuple").release());
                aAttributes.emplace_back("functools", py{ fn_import("functools") }.release());
                aAttributes.emplace_back("math", py{ fn_import("math") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("types", py{ fn_import("types") }.release());
                aAttributes.emplace_back("warnings", py{ fn_import("warnings") }.release());
                aAttributes.emplace_back("ipaddress", py{ fn_import("ipaddress") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("urllib.parse", urllib_parse_module);
    }
}
