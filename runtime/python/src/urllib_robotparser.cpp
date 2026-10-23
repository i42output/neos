/*
  urllib_robotparser.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/urllib/robotparser.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module urllib.robotparser (CPython 3.13's Lib/urllib/robotparser.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "urllib_robotparser_docs.hpp"

namespace neos::python_runtime::detail::urllib_robotparser_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "urllib.robotparser.name"))
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
                py_raise("SystemError", std::string{ "urllib.robotparser: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"urllib.robotparser.name">()))
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
            if (modules_imported("urllib.robotparser"))
                return py{ fn_import("urllib.robotparser") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "urllib.robotparser" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "urllib.robotparser", urllib_robotparser_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- urllib.robotparser (Lib/urllib/robotparser.py)

        void make_urllib_robotparser()
        {
            static module_writer const& w = writer("urllib.robotparser");
            define("urllib.robotparser", "__all__", py_list({ S<"RobotFileParser">() }));
            define("urllib.robotparser", "RequestRate", (Mod<"collections">()).method("namedtuple", { S<"RequestRate">(), S<"requests seconds">() }));
            define("urllib.robotparser", "RobotFileParser", w.make_class("RobotFileParser", {  }, {
                { "__init__", w.method("RobotFileParser", "__init__", { py_positional("url", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        self.set_attr("entries", py_list());
                        self.set_attr("groups", py_dict());
                        self.set_attr("sitemaps", py_list());
                        self.set_attr("default_entry", none());
                        self.set_attr("disallow_all", boolean(false));
                        self.set_attr("allow_all", boolean(false));
                        self.method("set_url", { url });
                        self.set_attr("last_checked", I<0>());
                        return none();
                    }) },
                { "mtime", w.method("RobotFileParser", "mtime", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("last_checked");
                    }) },
                { "modified", w.method("RobotFileParser", "modified", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py time;
                        time = module_of("time");
                        self.set_attr("last_checked", time.method("time"));
                        return none();
                    }) },
                { "set_url", w.method("RobotFileParser", "set_url", { py_positional("url") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        self.set_attr("url", url);
                        auto const items1 = unpack((Mod<"urllib.parse">().attr("urlsplit"))(url)[B<"slice">()(I<1>(), I<3>(), none())], 2);
                        self.set_attr("host", items1[0]);
                        self.set_attr("path", items1[1]);
                        return none();
                    }) },
                { "read", w.method("RobotFileParser", "read", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py err;
                        py f;
                        py raw;
                        {
                            bool otherwise2 = false;
                            try
                            {
                                f = (Mod<"urllib.request">().attr("urlopen"))(self.attr("url"));
                                otherwise2 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler2;
                                if (matches(handler2.exception, Mod<"urllib.error">().attr("HTTPError")))
                                {
                                    err = handler2.exception;
                                    if (py_tuple({ I<401>(), I<403>() }).contains(err.attr("code")))
                                    {
                                        // If access to robot.txt has the status Unauthorized/Forbidden,
                                        // then most likely this applies to the entire site.
                                        self.set_attr("disallow_all", boolean(true));
                                    }
                                    else if (((I<400>()) <= err.attr("code")) && (err.attr("code") < (I<500>())))
                                    {
                                        // RFC 9309, Section 2.3.1.3: the crawler MAY access any
                                        // resources on the server.
                                        self.set_attr("allow_all", boolean(true));
                                    }
                                    else if (((I<500>()) <= err.attr("code")) && (err.attr("code") < (I<600>())))
                                    {
                                        // RFC 9309, Section 2.3.1.4: the crawler MUST assume
                                        // complete disallow.
                                        self.set_attr("disallow_all", boolean(true));
                                    }
                                    err.method("close");
                                }
                                else
                                    py_reraise(handler2.exception);
                            }
                            if (otherwise2)
                            {
                                raw = f.method("read");
                                self.method("parse", { raw.method("decode", { S<"utf-8">(), S<"surrogateescape">() }).method("splitlines") });
                            }
                        }
                        return none();
                    }) },
                { "_add_entry", w.method("RobotFileParser", "_add_entry", { py_positional("entry") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py entry = a[1];
                        py agent;
                        self.attr("entries").method("append", { entry });
                        for (py const& item : iterate{ entry.attr("useragents") })
                        {
                            agent = item;
                            agent = agent.method("lower");
                            if (!self.attr("groups").contains(agent))
                            {
                                self.attr("groups").set_item(agent, entry);
                            }
                            else
                            {
                                self.attr("groups").set_item(agent, (G<"urllib.robotparser.merge_entries">())(self.attr("groups")[agent], entry));
                            }
                        }
                        return none();
                    }) },
                { "parse", w.method("RobotFileParser", "parse", { py_positional("lines") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py lines = a[1];
                        py entries;
                        py entry;
                        py i;
                        py line;
                        py numbers;
                        py product_token;
                        py state;
                        entries = py_list();
                        // states:
                        // 0: start state
                        // 1: saw user-agent line
                        // 2: saw an allow or disallow line
                        state = I<0>();
                        entry = (G<"urllib.robotparser.Entry">())();
                        self.method("modified");
                        for (py const& item : iterate{ lines })
                        {
                            line = item;
                            // remove optional comment and strip line
                            i = line.method("find", { S<"#">() });
                            if (i >= (I<0>()))
                            {
                                line = line[B<"slice">()(none(), i, none())];
                            }
                            line = line.method("strip");
                            if (!line.truthy())
                            {
                                continue;
                            }
                            line = line.method("split", { S<":">(), I<1>() });
                            if (py{ line.size() } == (I<2>()))
                            {
                                line.set_item(I<0>(), line[I<0>()].method("strip").method("lower"));
                                line.set_item(I<1>(), line[I<1>()].method("strip"));
                                if (line[I<0>()] == (S<"user-agent">()))
                                {
                                    if (state == (I<2>()))
                                    {
                                        self.method("_add_entry", { entry });
                                        entry = (G<"urllib.robotparser.Entry">())();
                                    }
                                    product_token = line[I<1>()];
                                    entry.attr("useragents").method("append", { product_token });
                                    state = I<1>();
                                }
                                else if (line[I<0>()] == (S<"disallow">()))
                                {
                                    if (state != (I<0>()))
                                    {
                                        state = I<2>();
                                        try
                                        {
                                            entry.attr("rulelines").method("append", { (G<"urllib.robotparser.RuleLine">())(line[I<1>()], boolean(false)) });
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler5;
                                            if (matches(handler5.exception, B<"ValueError">()))
                                            {
                                            }
                                            else
                                                py_reraise(handler5.exception);
                                        }
                                    }
                                }
                                else if (line[I<0>()] == (S<"allow">()))
                                {
                                    if (state != (I<0>()))
                                    {
                                        state = I<2>();
                                        try
                                        {
                                            entry.attr("rulelines").method("append", { (G<"urllib.robotparser.RuleLine">())(line[I<1>()], boolean(true)) });
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler6;
                                            if (matches(handler6.exception, B<"ValueError">()))
                                            {
                                            }
                                            else
                                                py_reraise(handler6.exception);
                                        }
                                    }
                                }
                                else if (line[I<0>()] == (S<"crawl-delay">()))
                                {
                                    if (state != (I<0>()))
                                    {
                                        // before trying to convert to int we need to make
                                        // sure that robots.txt has valid syntax otherwise
                                        // it will crash
                                        if (line[I<1>()].method("strip").method("isdecimal").truthy())
                                        {
                                            entry.set_attr("delay", (B<"int">())(line[I<1>()]));
                                        }
                                        state = I<2>();
                                    }
                                }
                                else if (line[I<0>()] == (S<"request-rate">()))
                                {
                                    if (state != (I<0>()))
                                    {
                                        numbers = line[I<1>()].method("split", { S<"/">() });
                                        // check if all values are sane
                                        if (((py{ numbers.size() } == (I<2>())) && numbers[I<0>()].method("strip").method("isdecimal").truthy() && numbers[I<1>()].method("strip").method("isdecimal").truthy()))
                                        {
                                            entry.set_attr("req_rate", (G<"urllib.robotparser.RequestRate">())((B<"int">())(numbers[I<0>()]), (B<"int">())(numbers[I<1>()])));
                                        }
                                        state = I<2>();
                                    }
                                }
                                else if (line[I<0>()] == (S<"sitemap">()))
                                {
                                    // According to http://www.sitemaps.org/protocol.html
                                    // "This directive is independent of the user-agent line,
                                    // so it doesn't matter where you place it in your file."
                                    // Therefore we do not change the state of the parser.
                                    self.attr("sitemaps").method("append", { line[I<1>()] });
                                }
                            }
                        }
                        if (state != (I<0>()))
                        {
                            self.method("_add_entry", { entry });
                        }
                        return none();
                    }) },
                { "_find_entry", w.method("RobotFileParser", "_find_entry", { py_positional("useragent") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py useragent = a[1];
                        py entry;
                        entry = self.attr("groups").method("get", { useragent.method("lower") });
                        if (!entry.is_none())
                        {
                            return entry;
                        }
                        for (py const& item : iterate{ self.attr("groups").method("values") })
                        {
                            entry = item;
                            if (entry.method("applies_to", { useragent }).truthy())
                            {
                                return entry;
                            }
                        }
                        return self.attr("groups").method("get", { S<"*">() });
                    }) },
                { "can_fetch", w.method("RobotFileParser", "can_fetch", { py_positional("useragent"), py_positional("url") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py useragent = a[1];
                        py url = a[2];
                        py entry;
                        py parsed_url;
                        if (self.attr("disallow_all").truthy())
                        {
                            return boolean(false);
                        }
                        if (self.attr("allow_all").truthy())
                        {
                            return boolean(true);
                        }
                        // Until the robots.txt file has been read or found not
                        // to exist, we must assume that no url is allowable.
                        // This prevents false positives when a user erroneously
                        // calls can_fetch() before calling read().
                        if (!self.attr("last_checked").truthy())
                        {
                            return boolean(false);
                        }
                        // search for given user agent matches
                        // the first match counts
                        parsed_url = (Mod<"urllib.parse">().attr("urlsplit"))(url);
                        url = (Mod<"urllib.parse">().attr("urlunsplit"))(B<"tuple">()((((py_list() + py_list({ S<"">() })) + py_list({ S<"">() })) + B<"list">()(parsed_url[B<"slice">()(I<2>(), none(), none())]))));
                        url = (G<"urllib.robotparser.normalize_uri">())(url);
                        if (!url.truthy())
                        {
                            url = S<"/">();
                        }
                        if (url == (S<"/robots.txt">()))
                        {
                            // The /robots.txt URI is implicitly allowed.
                            return boolean(true);
                        }
                        entry = self.method("_find_entry", { useragent });
                        if (entry.is_none())
                        {
                            return boolean(true);
                        }
                        return entry.method("allowance", { url });
                    }) },
                { "crawl_delay", w.method("RobotFileParser", "crawl_delay", { py_positional("useragent") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py useragent = a[1];
                        py entry;
                        if (!self.method("mtime").truthy())
                        {
                            return none();
                        }
                        entry = self.method("_find_entry", { useragent });
                        if (entry.is_none())
                        {
                            return none();
                        }
                        return entry.attr("delay");
                    }) },
                { "request_rate", w.method("RobotFileParser", "request_rate", { py_positional("useragent") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py useragent = a[1];
                        py entry;
                        if (!self.method("mtime").truthy())
                        {
                            return none();
                        }
                        entry = self.method("_find_entry", { useragent });
                        if (entry.is_none())
                        {
                            return none();
                        }
                        return entry.attr("req_rate");
                    }) },
                { "site_maps", w.method("RobotFileParser", "site_maps", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("sitemaps").truthy())
                        {
                            return none();
                        }
                        return self.attr("sitemaps");
                    }) },
                { "__str__", w.method("RobotFileParser", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py entries;
                        entries = self.attr("entries");
                        if (!self.attr("default_entry").is_none())
                        {
                            entries = entries + py_list({ self.attr("default_entry") });
                        }
                        return (S<"\n\n">()).method("join", { (B<"filter">())(none(), (B<"map">())(B<"str">(), entries)) });
                    }) }, }));

            define("urllib.robotparser", "RuleLine", w.make_class("RuleLine", {  }, {
                { "__init__", w.method("RuleLine", "__init__", { py_positional("path"), py_positional("allowance") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py allowance = a[2];
                        py pattern;
                        if (((path == (S<"">())) && (!allowance.truthy())))
                        {
                            // an empty value means allow all
                            allowance = boolean(true);
                        }
                        path = (Mod<"re">().attr("sub"))(S<"[*]{2,}">(), S<"*">(), path);
                        path = (Mod<"re">().attr("sub"))(S<"[$][$*]+">(), S<"$">(), path);
                        path = (G<"urllib.robotparser.normalize_pattern">())(path);
                        self.set_attr("fullmatch", path.method("endswith", { S<"$">() }));
                        path = path.method("rstrip", { S<"$">() });
                        if (path.contains((S<"$">())))
                        {
                            py_raise((B<"ValueError">())(S<"$ not at the end of path">()));
                        }
                        self.set_attr("matcher", none());
                        if (path.contains((S<"*">())))
                        {
                            pattern = (Mod<"re">().attr("compile"))((G<"urllib.robotparser.translate_pattern">())(path), Mod<"re">().attr("DOTALL"));
                            if (self.attr("fullmatch").truthy())
                            {
                                self.set_attr("matcher", pattern.attr("fullmatch"));
                            }
                            else
                            {
                                self.set_attr("matcher", pattern.attr("match"));
                            }
                        }
                        self.set_attr("path", path);
                        self.set_attr("allowance", allowance);
                        return none();
                    }) },
                { "applies_to", w.method("RuleLine", "applies_to", { py_positional("filename") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py m;
                        if (!self.attr("matcher").is_none())
                        {
                            m = self.method("matcher", { filename });
                            if (m.truthy())
                            {
                                return m.method("end") + (I<1>());
                            }
                        }
                        else if (self.attr("fullmatch").truthy())
                        {
                            if (filename == self.attr("path"))
                            {
                                return py{ self.attr("path").size() } + (I<1>());
                            }
                        }
                        else if (filename.method("startswith", { self.attr("path") }).truthy())
                        {
                            return py{ self.attr("path").size() } + (I<1>());
                        }
                        return I<0>();
                    }) },
                { "__str__", w.method("RuleLine", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (((self.attr("allowance").truthy() ? S<"Allow">() : S<"Disallow">()) + (S<": ">())) + self.attr("path")) + (self.attr("fullmatch").truthy() ? S<"$">() : S<"">());
                    }) }, }));

            define("urllib.robotparser", "Entry", w.make_class("Entry", {  }, {
                { "__init__", w.method("Entry", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("useragents", py_list());
                        self.set_attr("rulelines", py_list());
                        self.set_attr("delay", none());
                        self.set_attr("req_rate", none());
                        return none();
                    }) },
                { "__str__", w.method("Entry", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py agent;
                        py rate;
                        py ret;
                        if (!self.attr("useragents").truthy())
                        {
                            return S<"">();
                        }
                        ret = py_list();
                        for (py const& item : iterate{ self.attr("useragents") })
                        {
                            agent = item;
                            ret.method("append", { S(std::string{ "User-agent: " } + agent.str()) });
                        }
                        if (!self.attr("delay").is_none())
                        {
                            ret.method("append", { S(std::string{ "Crawl-delay: " } + self.attr("delay").str()) });
                        }
                        if (!self.attr("req_rate").is_none())
                        {
                            rate = self.attr("req_rate");
                            ret.method("append", { S(std::string{ "Request-rate: " } + rate.attr("requests").str() + "/" + rate.attr("seconds").str()) });
                        }
                        if (self.attr("rulelines").truthy())
                        {
                            ret.method("extend", { (B<"map">())(B<"str">(), self.attr("rulelines")) });
                        }
                        else
                        {
                            ret.method("append", { S<"Allow:">() });
                        }
                        return (S<"\n">()).method("join", { ret });
                    }) },
                { "applies_to", w.method("Entry", "applies_to", { py_positional("useragent") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py useragent = a[1];
                        py agent;
                        if (useragent.is_none())
                        {
                            return boolean(self.attr("useragents").contains((S<"*">())));
                        }
                        // split the name token and make it lower case
                        useragent = useragent.method("split", { S<"/">() })[I<0>()].method("lower");
                        for (py const& item : iterate{ self.attr("useragents") })
                        {
                            agent = item;
                            if (agent != (S<"*">()))
                            {
                                agent = agent.method("lower");
                                if (useragent.contains(agent))
                                {
                                    return boolean(true);
                                }
                            }
                        }
                        return boolean(false);
                    }) },
                { "allowance", w.method("Entry", "allowance", { py_positional("filename") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py allowance;
                        py best_match;
                        py line;
                        py m;
                        best_match = I<-1>();
                        allowance = boolean(true);
                        for (py const& item : iterate{ self.attr("rulelines") })
                        {
                            line = item;
                            m = line.method("applies_to", { filename });
                            if (m.truthy())
                            {
                                if (m > best_match)
                                {
                                    best_match = m;
                                    allowance = line.attr("allowance");
                                }
                                else if (((m == best_match) && (!allowance.truthy())))
                                {
                                    allowance = line.attr("allowance");
                                }
                            }
                        }
                        return allowance;
                    }) }, }));

            define("urllib.robotparser", "normalize", w.function("normalize", "normalize", { py_positional("path") }, [](arguments a) -> py
                {
                    py path = a[0];
                    py unquoted;
                    unquoted = (Mod<"urllib.parse">().attr("unquote")).call({ path }, { { "errors", S<"surrogateescape">() } });
                    return (Mod<"urllib.parse">().attr("quote")).call({ unquoted }, { { "errors", S<"surrogateescape">() } });
                }));

            define("urllib.robotparser", "normalize_uri", w.function("normalize_uri", "normalize_uri", { py_positional("path") }, [](arguments a) -> py
                {
                    py path = a[0];
                    py query;
                    py sep;
                    auto const items11 = unpack(path.method("partition", { S<"?">() }), 3);
                    path = items11[0];
                    sep = items11[1];
                    query = items11[2];
                    path = (G<"urllib.robotparser.normalize">())(path);
                    if (sep.truthy())
                    {
                        query = (Mod<"re">().attr("sub"))(S<"[^=&]+">(), w.function("<lambda>", "normalize_uri.<locals>.<lambda>", { py_positional("m") }, [](arguments a) -> py
        {
            py m = a[0];
            return (G<"urllib.robotparser.normalize">())(m[I<0>()]);
        }), query);
                        path = inplace(NEOS_PY_ADD, path, (S<"?">()) + query);
                    }
                    return path;
                }));

            define("urllib.robotparser", "normalize_pattern", w.function("normalize_pattern", "normalize_pattern", { py_positional("path") }, [](arguments a) -> py
                {
                    py path = a[0];
                    py query;
                    py sep;
                    auto const items12 = unpack(path.method("partition", { S<"?">() }), 3);
                    path = items12[0];
                    sep = items12[1];
                    query = items12[2];
                    path = (Mod<"re">().attr("sub"))(S<"[^*$]+">(), w.function("<lambda>", "normalize_pattern.<locals>.<lambda>", { py_positional("m") }, [](arguments a) -> py
    {
        py m = a[0];
        return (G<"urllib.robotparser.normalize">())(m[I<0>()]);
    }), path);
                    if (sep.truthy())
                    {
                        query = (Mod<"re">().attr("sub"))(S<"[^=&*$]+">(), w.function("<lambda>", "normalize_pattern.<locals>.<lambda>", { py_positional("m") }, [](arguments a) -> py
        {
            py m = a[0];
            return (G<"urllib.robotparser.normalize">())(m[I<0>()]);
        }), query);
                        path = inplace(NEOS_PY_ADD, path, (S<"?">()) + query);
                    }
                    return path;
                }));

            define("urllib.robotparser", "translate_pattern", w.function("translate_pattern", "translate_pattern", { py_positional("path") }, [](arguments a) -> py
                {
                    py path = a[0];
                    py i;
                    py parts;
                    parts = (B<"list">())((B<"map">())(Mod<"re">().attr("escape"), path.method("split", { S<"*">() })));
                    for (py const& item : iterate{ (B<"range">())(I<1>(), py{ parts.size() } - (I<1>())) })
                    {
                        i = item;
                        parts.set_item(i, S(std::string{ "(?>.*?" } + parts[i].str() + ")"));
                    }
                    parts.set_item(I<-1>(), S(std::string{ ".*" } + parts[I<-1>()].str()));
                    return (S<"">()).method("join", { parts });
                }));

            define("urllib.robotparser", "merge_entries", w.function("merge_entries", "merge_entries", { py_positional("e1"), py_positional("e2") }, [](arguments a) -> py
                {
                    py e1 = a[0];
                    py e2 = a[1];
                    py entry;
                    entry = (G<"urllib.robotparser.Entry">())();
                    entry.set_attr("useragents", (B<"list">())((B<"filter">())((B<"set">())(e2.attr("useragents")).attr("__contains__"), e1.attr("useragents"))));
                    entry.set_attr("rulelines", e1.attr("rulelines") + e2.attr("rulelines"));
                    entry.set_attr("delay", (e2.attr("delay").is_none() ? e1.attr("delay") : e2.attr("delay")));
                    entry.set_attr("req_rate", (e2.attr("req_rate").is_none() ? e1.attr("req_rate") : e2.attr("req_rate")));
                    return entry;
                }));
        }

        bool urllib_robotparser_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_urllib_robotparser();
                }
                if (auto const doc = urllib_robotparser_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("collections", py{ fn_import("collections") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("urllib.robotparser", urllib_robotparser_module);
    }
}
