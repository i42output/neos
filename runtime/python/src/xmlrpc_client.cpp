/*
  xmlrpc_client.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xmlrpc/client.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/xmlrpc/client.py's copyright: Copyright (c) 1999-2002 by Secret Labs
  AB; Copyright (c) 1999-2002 by Fredrik Lundh. Used under its permission notice (see runtime/python/NOTICE).
*/

// The module xmlrpc.client (CPython 3.13's Lib/xmlrpc/client.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xmlrpc_client_docs.hpp"

namespace neos::python_runtime::detail::xmlrpc_client_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xmlrpc.client.name"))
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
                py_raise("SystemError", std::string{ "xmlrpc.client: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xmlrpc.client.name">()))
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
            if (modules_imported("xmlrpc.client"))
                return py{ fn_import("xmlrpc.client") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xmlrpc.client" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xmlrpc.client", xmlrpc_client_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xmlrpc.client (Lib/xmlrpc/client.py)

        void make_xmlrpc_client()
        {
            static module_writer const& w = writer("xmlrpc.client");
            // 
            // XML-RPC CLIENT LIBRARY
            // $Id$
            // 
            // an XML-RPC client interface for Python.
            // 
            // the marshalling and response parser code can also be used to
            // implement XML-RPC servers.
            // 
            // Notes:
            // this version is designed to work with Python 2.1 or newer.
            // 
            // History:
            // 1999-01-14 fl  Created
            // 1999-01-15 fl  Changed dateTime to use localtime
            // 1999-01-16 fl  Added Binary/base64 element, default to RPC2 service
            // 1999-01-19 fl  Fixed array data element (from Skip Montanaro)
            // 1999-01-21 fl  Fixed dateTime constructor, etc.
            // 1999-02-02 fl  Added fault handling, handle empty sequences, etc.
            // 1999-02-10 fl  Fixed problem with empty responses (from Skip Montanaro)
            // 1999-06-20 fl  Speed improvements, pluggable parsers/transports (0.9.8)
            // 2000-11-28 fl  Changed boolean to check the truth value of its argument
            // 2001-02-24 fl  Added encoding/Unicode/SafeTransport patches
            // 2001-02-26 fl  Added compare support to wrappers (0.9.9/1.0b1)
            // 2001-03-28 fl  Make sure response tuple is a singleton
            // 2001-03-29 fl  Don't require empty params element (from Nicholas Riley)
            // 2001-06-10 fl  Folded in _xmlrpclib accelerator support (1.0b2)
            // 2001-08-20 fl  Base xmlrpclib.Error on built-in Exception (from Paul Prescod)
            // 2001-09-03 fl  Allow Transport subclass to override getparser
            // 2001-09-10 fl  Lazy import of urllib, cgi, xmllib (20x import speedup)
            // 2001-10-01 fl  Remove containers from memo cache when done with them
            // 2001-10-01 fl  Use faster escape method (80% dumps speedup)
            // 2001-10-02 fl  More dumps microtuning
            // 2001-10-04 fl  Make sure import expat gets a parser (from Guido van Rossum)
            // 2001-10-10 sm  Allow long ints to be passed as ints if they don't overflow
            // 2001-10-17 sm  Test for int and long overflow (allows use on 64-bit systems)
            // 2001-11-12 fl  Use repr() to marshal doubles (from Paul Felix)
            // 2002-03-17 fl  Avoid buffered read when possible (from James Rucker)
            // 2002-04-07 fl  Added pythondoc comments
            // 2002-04-16 fl  Added __str__ methods to datetime/binary wrappers
            // 2002-05-15 fl  Added error constants (from Andrew Kuchling)
            // 2002-06-27 fl  Merged with Python CVS version
            // 2002-10-22 fl  Added basic authentication (based on code from Phillip Eby)
            // 2003-01-22 sm  Add support for the bool type
            // 2003-02-27 gvr Remove apply calls
            // 2003-04-24 sm  Use cStringIO if available
            // 2003-04-25 ak  Add support for nil
            // 2003-06-15 gn  Add support for time.struct_time
            // 2003-07-12 gp  Correct marshalling of Faults
            // 2003-10-31 mvl Add multicall support
            // 2004-08-20 mvl Bump minimum supported Python version to 2.1
            // 2014-12-02 ch/doko  Add workaround for gzip bomb vulnerability
            // 
            // Copyright (c) 1999-2002 by Secret Labs AB.
            // Copyright (c) 1999-2002 by Fredrik Lundh.
            // 
            // info@pythonware.com
            // http://www.pythonware.com
            // 
            // --------------------------------------------------------------------
            // The XML-RPC client interface is
            // 
            // Copyright (c) 1999-2002 by Secret Labs AB
            // Copyright (c) 1999-2002 by Fredrik Lundh
            // 
            // By obtaining, using, and/or copying this software and/or its
            // associated documentation, you agree that you have read, understood,
            // and will comply with the following terms and conditions:
            // 
            // Permission to use, copy, modify, and distribute this software and
            // its associated documentation for any purpose and without fee is
            // hereby granted, provided that the above copyright notice appears in
            // all copies, and that both that copyright notice and this permission
            // notice appear in supporting documentation, and that the name of
            // Secret Labs AB or the author not be used in advertising or publicity
            // pertaining to distribution of the software without specific, written
            // prior permission.
            // 
            // SECRET LABS AB AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
            // TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANT-
            // ABILITY AND FITNESS.  IN NO EVENT SHALL SECRET LABS AB OR THE AUTHOR
            // BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY
            // DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
            // WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
            // ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
            // OF THIS SOFTWARE.
            // --------------------------------------------------------------------
            // --------------------------------------------------------------------
            // Internal stuff
            define("xmlrpc.client", "escape", w.function("escape", "escape", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    s = s.method("replace", { S<"&">(), S<"&amp;">() });
                    s = s.method("replace", { S<"<">(), S<"&lt;">() });
                    return s.method("replace", { S<">">(), S<"&gt;">() });
                }));

            // used in User-Agent header sent
            define("xmlrpc.client", "__version__", (S<"%d.%d">()) % (Mod<"sys">().attr("version_info"))[B<"slice">()(none(), I<2>(), none())]);
            // xmlrpc integer limits
            define("xmlrpc.client", "MAXINT", power((I<2>()), (I<31>())) - (I<1>()));
            define("xmlrpc.client", "MININT", -power((I<2>()), (I<31>())));
            // --------------------------------------------------------------------
            // Error constants (from Dan Libby's specification at
            // http://xmlrpc-epi.sourceforge.net/specs/rfc.fault_codes.php)
            // Ranges of errors
            define("xmlrpc.client", "PARSE_ERROR", I<-32700>());
            define("xmlrpc.client", "SERVER_ERROR", I<-32600>());
            define("xmlrpc.client", "APPLICATION_ERROR", I<-32500>());
            define("xmlrpc.client", "SYSTEM_ERROR", I<-32400>());
            define("xmlrpc.client", "TRANSPORT_ERROR", I<-32300>());
            // Specific errors
            define("xmlrpc.client", "NOT_WELLFORMED_ERROR", I<-32700>());
            define("xmlrpc.client", "UNSUPPORTED_ENCODING", I<-32701>());
            define("xmlrpc.client", "INVALID_ENCODING_CHAR", I<-32702>());
            define("xmlrpc.client", "INVALID_XMLRPC", I<-32600>());
            define("xmlrpc.client", "METHOD_NOT_FOUND", I<-32601>());
            define("xmlrpc.client", "INVALID_METHOD_PARAMS", I<-32602>());
            define("xmlrpc.client", "INTERNAL_ERROR", I<-32603>());
            // --------------------------------------------------------------------
            // Exceptions
            // #
            // Base class for all kinds of client-side errors.
            define("xmlrpc.client", "Error", w.make_class("Error", { B<"Exception">() }, {
                { "__str__", (B<"object">()).attr("__str__") }, }));

            // #
            // Indicates an HTTP-level protocol error.  This is raised by the HTTP
            // transport layer, if the server returns an error code other than 200
            // (OK).
            // 
            // @param url The target URL.
            // @param errcode The HTTP error code.
            // @param errmsg The HTTP error message.
            // @param headers The HTTP header dictionary.
            define("xmlrpc.client", "ProtocolError", w.make_class("ProtocolError", { G<"xmlrpc.client.Error">() }, {
                { "__init__", w.method("ProtocolError", "__init__", { py_positional("url"), py_positional("errcode"), py_positional("errmsg"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py errcode = a[2];
                        py errmsg = a[3];
                        py headers = a[4];
                        (G<"xmlrpc.client.Error">()).method("__init__", { self });
                        self.set_attr("url", url);
                        self.set_attr("errcode", errcode);
                        self.set_attr("errmsg", errmsg);
                        self.set_attr("headers", headers);
                        return none();
                    }) },
                { "__repr__", w.method("ProtocolError", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s for %s: %s %s>">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.attr("url"), self.attr("errcode"), self.attr("errmsg") });
                    }) }, }));

            // #
            // Indicates a broken XML-RPC response package.  This exception is
            // raised by the unmarshalling layer, if the XML-RPC response is
            // malformed.
            define("xmlrpc.client", "ResponseError", w.make_class("ResponseError", { G<"xmlrpc.client.Error">() }, {
 }));

            // #
            // Indicates an XML-RPC fault response package.  This exception is
            // raised by the unmarshalling layer, if the XML-RPC response contains
            // a fault string.  This exception can also be used as a class, to
            // generate a fault XML-RPC message.
            // 
            // @param faultCode The XML-RPC fault code.
            // @param faultString The XML-RPC fault string.
            define("xmlrpc.client", "Fault", w.make_class("Fault", { G<"xmlrpc.client.Error">() }, {
                { "__init__", w.method("Fault", "__init__", { py_positional("faultCode"), py_positional("faultString") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py faultCode = a[1];
                        py faultString = a[2];
                        py extra = a[3];
                        (G<"xmlrpc.client.Error">()).method("__init__", { self });
                        self.set_attr("faultCode", faultCode);
                        self.set_attr("faultString", faultString);
                        return none();
                    }, false, true) },
                { "__repr__", w.method("Fault", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s %s: %r>">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.attr("faultCode"), self.attr("faultString") });
                    }) }, }));

            // --------------------------------------------------------------------
            // Special values
            // #
            // Backwards compatibility
            define("xmlrpc.client", "boolean", B<"bool">());
            define("xmlrpc.client", "Boolean", B<"bool">());
            define("xmlrpc.client", "_iso8601_format", w.function("_iso8601_format", "_iso8601_format", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    if (!value.attr("tzinfo").is_none())
                    {
                        // XML-RPC only uses the naive portion of the datetime
                        value = value.method("replace", {  }, { { "tzinfo", none() } });
                    }
                    // XML-RPC doesn't use '-' separator in the date part
                    return value.method("isoformat", {  }, { { "timespec", S<"seconds">() } }).method("replace", { S<"-">(), S<"">() });
                }));

            define("xmlrpc.client", "_strftime", w.function("_strftime", "_strftime", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    if (isinstance(value, Mod<"datetime">().attr("datetime")))
                    {
                        return (G<"xmlrpc.client._iso8601_format">())(value);
                    }
                    if (!isinstance(value, py_tuple({ B<"tuple">(), Mod<"time">().attr("struct_time") })))
                    {
                        if (value == (I<0>()))
                        {
                            value = (Mod<"time">().attr("time"))();
                        }
                        value = (Mod<"time">().attr("localtime"))(value);
                    }
                    return (S<"%04d%02d%02dT%02d:%02d:%02d">()) % value[B<"slice">()(none(), I<6>(), none())];
                }));

                // #
                // Get date/time value.
                // 
                // @return Date/time value, as an ISO 8601 string.
            define("xmlrpc.client", "DateTime", w.make_class("DateTime", {  }, {
                { "__init__", w.method("DateTime", "__init__", { py_positional("value", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        if (isinstance(value, B<"str">()))
                        {
                            self.set_attr("value", value);
                        }
                        else
                        {
                            self.set_attr("value", (G<"xmlrpc.client._strftime">())(value));
                        }
                        return none();
                    }) },
                { "make_comparable", w.method("DateTime", "make_comparable", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py o;
                        py s;
                        if (isinstance(other, G<"xmlrpc.client.DateTime">()))
                        {
                            s = self.attr("value");
                            o = other.attr("value");
                        }
                        else if (isinstance(other, Mod<"datetime">().attr("datetime")))
                        {
                            s = self.attr("value");
                            o = (G<"xmlrpc.client._iso8601_format">())(other);
                        }
                        else if (isinstance(other, B<"str">()))
                        {
                            s = self.attr("value");
                            o = other;
                        }
                        else if (has_attr(other, "timetuple"))
                        {
                            s = self.method("timetuple");
                            o = other.method("timetuple");
                        }
                        else
                        {
                            s = self;
                            o = py::borrowed(not_implemented());
                        }
                        return py_tuple({ s, o });
                    }) },
                { "__lt__", w.method("DateTime", "__lt__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py o;
                        py s;
                        auto const items1 = unpack(self.method("make_comparable", { other }), 2);
                        s = items1[0];
                        o = items1[1];
                        if (o.is(py::borrowed(not_implemented())))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return boolean(s < o);
                    }) },
                { "__le__", w.method("DateTime", "__le__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py o;
                        py s;
                        auto const items2 = unpack(self.method("make_comparable", { other }), 2);
                        s = items2[0];
                        o = items2[1];
                        if (o.is(py::borrowed(not_implemented())))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return boolean(s <= o);
                    }) },
                { "__gt__", w.method("DateTime", "__gt__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py o;
                        py s;
                        auto const items3 = unpack(self.method("make_comparable", { other }), 2);
                        s = items3[0];
                        o = items3[1];
                        if (o.is(py::borrowed(not_implemented())))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return boolean(s > o);
                    }) },
                { "__ge__", w.method("DateTime", "__ge__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py o;
                        py s;
                        auto const items4 = unpack(self.method("make_comparable", { other }), 2);
                        s = items4[0];
                        o = items4[1];
                        if (o.is(py::borrowed(not_implemented())))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return boolean(s >= o);
                    }) },
                { "__eq__", w.method("DateTime", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py o;
                        py s;
                        auto const items5 = unpack(self.method("make_comparable", { other }), 2);
                        s = items5[0];
                        o = items5[1];
                        if (o.is(py::borrowed(not_implemented())))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return boolean(s == o);
                    }) },
                { "timetuple", w.method("DateTime", "timetuple", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (Mod<"time">().attr("strptime"))(self.attr("value"), S<"%Y%m%dT%H:%M:%S">());
                    }) },
                { "__str__", w.method("DateTime", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("value");
                    }) },
                { "__repr__", w.method("DateTime", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s %r at %#x>">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.attr("value"), (B<"id">())(self) });
                    }) },
                { "decode", w.method("DateTime", "decode", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("value", (B<"str">())(data).method("strip"));
                        return none();
                    }) },
                { "encode", w.method("DateTime", "encode", { py_positional("out") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py out = a[1];
                        out.method("write", { S<"<value><dateTime.iso8601>">() });
                        out.method("write", { self.attr("value") });
                        out.method("write", { S<"</dateTime.iso8601></value>\n">() });
                        return none();
                    }) }, }));

            define("xmlrpc.client", "_datetime", w.function("_datetime", "_datetime", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py value;
                    value = (G<"xmlrpc.client.DateTime">())();
                    value.method("decode", { data });
                    return value;
                }));

            define("xmlrpc.client", "_datetime_type", w.function("_datetime_type", "_datetime_type", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    return (Mod<"datetime">().attr("datetime")).method("strptime", { data, S<"%Y%m%dT%H:%M:%S">() });
                }));

            // #
            // Wrapper for binary data.  This can be used to transport any kind
            // of binary data over XML-RPC, using BASE64 encoding.
            // 
            // @param data An 8-bit string containing arbitrary data.
                // #
                // Get buffer contents.
                // 
                // @return Buffer contents, as an 8-bit string.
            define("xmlrpc.client", "Binary", w.make_class("Binary", {  }, {
                { "__init__", w.method("Binary", "__init__", { py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data.is_none())
                        {
                            data = py{ make_bytes(std::string{ "", 0 }) };
                        }
                        else
                        {
                            if (!isinstance(data, py_tuple({ B<"bytes">(), B<"bytearray">() })))
                            {
                                py_raise((B<"TypeError">())((S<"expected bytes or bytearray, not %s">()) % data.attr("__class__").attr("__name__")));
                            }
                            data = (B<"bytes">())(data);
                        }
                        self.set_attr("data", data);
                        return none();
                    }) },
                { "__str__", w.method("Binary", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"str">())(self.attr("data"), S<"latin-1">());
                    }) },
                { "__eq__", w.method("Binary", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (isinstance(other, G<"xmlrpc.client.Binary">()))
                        {
                            other = other.attr("data");
                        }
                        return boolean(self.attr("data") == other);
                    }) },
                { "decode", w.method("Binary", "decode", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("data", (Mod<"base64">().attr("decodebytes"))(data));
                        return none();
                    }) },
                { "encode", w.method("Binary", "encode", { py_positional("out") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py out = a[1];
                        py encoded;
                        out.method("write", { S<"<value><base64>\n">() });
                        encoded = (Mod<"base64">().attr("encodebytes"))(self.attr("data"));
                        out.method("write", { encoded.method("decode", { S<"ascii">() }) });
                        out.method("write", { S<"</base64></value>\n">() });
                        return none();
                    }) }, }));

            define("xmlrpc.client", "_binary", w.function("_binary", "_binary", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py value;
                    value = (G<"xmlrpc.client.Binary">())();
                    value.method("decode", { data });
                    return value;
                }));

            define("xmlrpc.client", "WRAPPERS", py_tuple({ G<"xmlrpc.client.DateTime">(), G<"xmlrpc.client.Binary">() }));
            // --------------------------------------------------------------------
            // XML parsers
                // fast expat parser for Python 2.0 and later.
            define("xmlrpc.client", "ExpatParser", w.make_class("ExpatParser", {  }, {
                { "__init__", w.method("ExpatParser", "__init__", { py_positional("target") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py encoding;
                        py parser;
                        py const value6 = (Mod<"xml.parsers.expat">().attr("ParserCreate"))(none(), none());
                        self.set_attr("_parser", value6);
                        parser = value6;
                        self.set_attr("_target", target);
                        parser.set_attr("StartElementHandler", target.attr("start"));
                        parser.set_attr("EndElementHandler", target.attr("end"));
                        parser.set_attr("CharacterDataHandler", target.attr("data"));
                        encoding = none();
                        target.method("xml", { encoding, none() });
                        return none();
                    }) },
                { "feed", w.method("ExpatParser", "feed", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.attr("_parser").method("Parse", { data, boolean(false) });
                        return none();
                    }) },
                { "close", w.method("ExpatParser", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser;
                        {
                            bool otherwise7 = false;
                            try
                            {
                                parser = self.attr("_parser");
                                otherwise7 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler7;
                                if (matches(handler7.exception, B<"AttributeError">()))
                                {
                                }
                                else
                                    py_reraise(handler7.exception);
                            }
                            if (otherwise7)
                            {
                                del_attr(self, "_target");
                                del_attr(self, "_parser");
                                parser.method("Parse", { py{ make_bytes(std::string{ "", 0 }) }, boolean(true) });
                            }
                        }
                        return none();
                    }) }, }));

            // --------------------------------------------------------------------
            // XML-RPC marshalling and unmarshalling code
            // #
            // XML-RPC marshaller.
            // 
            // @param encoding Default encoding for 8-bit strings.  The default
            // value is None (interpreted as UTF-8).
            // @see dumps
                // by the way, if you don't understand what's going on in here,
                // that's perfectly ok.
                // backward compatible
                // XXX(twouters): using "_arbitrary_instance" as key as a quick-fix
                // for the p3yk merge, this should probably be fixed more neatly.
            define("xmlrpc.client", "Marshaller", w.make_class("Marshaller", {  }, {
                { "__init__", w.method("Marshaller", "__init__", { py_positional("encoding", none()), py_positional("allow_none", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py encoding = a[1];
                        py allow_none = a[2];
                        self.set_attr("memo", py_dict());
                        self.set_attr("data", none());
                        self.set_attr("encoding", encoding);
                        self.set_attr("allow_none", allow_none);
                        return none();
                    }) },
                { "dispatch", py_dict() },
                { "dumps", w.method("Marshaller", "dumps", { py_positional("values") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py values = a[1];
                        py dump;
                        py out;
                        py result;
                        py v;
                        py write;
                        out = py_list();
                        write = out.attr("append");
                        dump = self.attr("_Marshaller__dump");
                        if (isinstance(values, G<"xmlrpc.client.Fault">()))
                        {
                            // fault instance
                            write(S<"<fault>\n">());
                            dump(dict_of({ { S<"faultCode">(), values.attr("faultCode") }, { S<"faultString">(), values.attr("faultString") } }), write);
                            write(S<"</fault>\n">());
                        }
                        else
                        {
                            // parameter block
                            // FIXME: the xml-rpc specification allows us to leave out
                            // the entire <params> block if there are no parameters.
                            // however, changing this may break older code (including
                            // old versions of xmlrpclib.py), so this is better left as
                            // is for now.  See @XMLRPC3 for more information. /F
                            write(S<"<params>\n">());
                            for (py const& item : iterate{ values })
                            {
                                v = item;
                                write(S<"<param>\n">());
                                dump(v, write);
                                write(S<"</param>\n">());
                            }
                            write(S<"</params>\n">());
                        }
                        result = (S<"">()).method("join", { out });
                        return result;
                    }) },
                { "_Marshaller__dump", w.method("Marshaller", "__dump", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py f;
                        py type_;
                        try
                        {
                            f = self.attr("dispatch")[(B<"type">())(value)];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler9;
                            if (matches(handler9.exception, B<"KeyError">()))
                            {
                                // check if this object can be marshalled as a structure
                                if (!has_attr(value, "__dict__"))
                                {
                                    py_raise((B<"TypeError">())((S<"cannot marshal %s objects">()) % (B<"type">())(value)));
                                }
                                // check if this class is a sub-class of a basic type,
                                // because we don't know how to marshal these types
                                // (e.g. a string sub-class)
                                for (py const& item : iterate{ (B<"type">())(value).attr("__mro__") })
                                {
                                    type_ = item;
                                    if (self.attr("dispatch").method("keys").contains(type_))
                                    {
                                        py_raise((B<"TypeError">())((S<"cannot marshal %s objects">()) % (B<"type">())(value)));
                                    }
                                }
                                // XXX(twouters): using "_arbitrary_instance" as key as a quick-fix
                                // for the p3yk merge, this should probably be fixed more neatly.
                                f = self.attr("dispatch")[S<"_arbitrary_instance">()];
                            }
                            else
                                py_reraise(handler9.exception);
                        }
                        f(self, value, write);
                        return none();
                    }) },
                { "dump_nil", w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }() },
                { "dump_bool", w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }() },
                { "dump_long", w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"int">(), w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    })); return t; }() },
                { "dump_int", w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    }) },
                { "dump_double", w.method("Marshaller", "dump_double", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><double>">());
                        write(S(value.repr()));
                        write(S<"</double></value>\n">());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"int">(), w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"float">(), w.method("Marshaller", "dump_double", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><double>">());
                        write(S(value.repr()));
                        write(S<"</double></value>\n">());
                        return none();
                    })); return t; }() },
                { "dump_unicode", w.method("Marshaller", "dump_unicode", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        write(S<"<value><string>">());
                        write(escape(value));
                        write(S<"</string></value>\n">());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"int">(), w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"float">(), w.method("Marshaller", "dump_double", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><double>">());
                        write(S(value.repr()));
                        write(S<"</double></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"str">(), w.method("Marshaller", "dump_unicode", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        write(S<"<value><string>">());
                        write(escape(value));
                        write(S<"</string></value>\n">());
                        return none();
                    })); return t; }() },
                { "dump_bytes", w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"int">(), w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"float">(), w.method("Marshaller", "dump_double", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><double>">());
                        write(S(value.repr()));
                        write(S<"</double></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"str">(), w.method("Marshaller", "dump_unicode", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        write(S<"<value><string>">());
                        write(escape(value));
                        write(S<"</string></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytes">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }() },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"int">(), w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"float">(), w.method("Marshaller", "dump_double", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><double>">());
                        write(S(value.repr()));
                        write(S<"</double></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"str">(), w.method("Marshaller", "dump_unicode", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        write(S<"<value><string>">());
                        write(escape(value));
                        write(S<"</string></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytes">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytearray">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }() },
                { "dump_array", w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"int">(), w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"float">(), w.method("Marshaller", "dump_double", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><double>">());
                        write(S(value.repr()));
                        write(S<"</double></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"str">(), w.method("Marshaller", "dump_unicode", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        write(S<"<value><string>">());
                        write(escape(value));
                        write(S<"</string></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytes">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytearray">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"tuple">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }() },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"int">(), w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"float">(), w.method("Marshaller", "dump_double", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><double>">());
                        write(S(value.repr()));
                        write(S<"</double></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"str">(), w.method("Marshaller", "dump_unicode", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        write(S<"<value><string>">());
                        write(escape(value));
                        write(S<"</string></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytes">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytearray">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"tuple">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(B<"list">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }() },
                { "dump_struct", w.method("Marshaller", "dump_struct", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        py dump;
                        py i;
                        py k;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive dictionaries">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><struct>\n">());
                        for (py const& item : iterate{ value.method("items") })
                        {
                            auto const items13 = unpack(item, 2);
                            k = items13[0];
                            v = items13[1];
                            write(S<"<member>\n">());
                            if (!isinstance(k, B<"str">()))
                            {
                                py_raise((B<"TypeError">())(S<"dictionary key must be string">()));
                            }
                            write((S<"<name>%s</name>\n">()) % escape(k));
                            dump(v, write);
                            write(S<"</member>\n">());
                        }
                        write(S<"</struct></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"int">(), w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"float">(), w.method("Marshaller", "dump_double", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><double>">());
                        write(S(value.repr()));
                        write(S<"</double></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"str">(), w.method("Marshaller", "dump_unicode", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        write(S<"<value><string>">());
                        write(escape(value));
                        write(S<"</string></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytes">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytearray">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"tuple">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(B<"list">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(B<"dict">(), w.method("Marshaller", "dump_struct", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        py dump;
                        py i;
                        py k;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive dictionaries">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><struct>\n">());
                        for (py const& item : iterate{ value.method("items") })
                        {
                            auto const items13 = unpack(item, 2);
                            k = items13[0];
                            v = items13[1];
                            write(S<"<member>\n">());
                            if (!isinstance(k, B<"str">()))
                            {
                                py_raise((B<"TypeError">())(S<"dictionary key must be string">()));
                            }
                            write((S<"<name>%s</name>\n">()) % escape(k));
                            dump(v, write);
                            write(S<"</member>\n">());
                        }
                        write(S<"</struct></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }() },
                { "dump_datetime", w.method("Marshaller", "dump_datetime", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><dateTime.iso8601>">());
                        write((G<"xmlrpc.client._strftime">())(value));
                        write(S<"</dateTime.iso8601></value>\n">());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"int">(), w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"float">(), w.method("Marshaller", "dump_double", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><double>">());
                        write(S(value.repr()));
                        write(S<"</double></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"str">(), w.method("Marshaller", "dump_unicode", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        write(S<"<value><string>">());
                        write(escape(value));
                        write(S<"</string></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytes">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytearray">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"tuple">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(B<"list">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(B<"dict">(), w.method("Marshaller", "dump_struct", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        py dump;
                        py i;
                        py k;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive dictionaries">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><struct>\n">());
                        for (py const& item : iterate{ value.method("items") })
                        {
                            auto const items13 = unpack(item, 2);
                            k = items13[0];
                            v = items13[1];
                            write(S<"<member>\n">());
                            if (!isinstance(k, B<"str">()))
                            {
                                py_raise((B<"TypeError">())(S<"dictionary key must be string">()));
                            }
                            write((S<"<name>%s</name>\n">()) % escape(k));
                            dump(v, write);
                            write(S<"</member>\n">());
                        }
                        write(S<"</struct></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(Mod<"datetime">().attr("datetime"), w.method("Marshaller", "dump_datetime", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><dateTime.iso8601>">());
                        write((G<"xmlrpc.client._strftime">())(value));
                        write(S<"</dateTime.iso8601></value>\n">());
                        return none();
                    })); return t; }() },
                { "dump_instance", w.method("Marshaller", "dump_instance", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if ((G<"xmlrpc.client.WRAPPERS">()).contains(value.attr("__class__")))
                        {
                            self.set_attr("write", write);
                            value.method("encode", { self });
                            del_attr(self, "write");
                        }
                        else
                        {
                            // store instance attributes as a struct (really?)
                            self.method("dump_struct", { value.attr("__dict__"), write });
                        }
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"int">(), w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"float">(), w.method("Marshaller", "dump_double", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><double>">());
                        write(S(value.repr()));
                        write(S<"</double></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"str">(), w.method("Marshaller", "dump_unicode", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        write(S<"<value><string>">());
                        write(escape(value));
                        write(S<"</string></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytes">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytearray">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"tuple">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(B<"list">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(B<"dict">(), w.method("Marshaller", "dump_struct", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        py dump;
                        py i;
                        py k;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive dictionaries">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><struct>\n">());
                        for (py const& item : iterate{ value.method("items") })
                        {
                            auto const items13 = unpack(item, 2);
                            k = items13[0];
                            v = items13[1];
                            write(S<"<member>\n">());
                            if (!isinstance(k, B<"str">()))
                            {
                                py_raise((B<"TypeError">())(S<"dictionary key must be string">()));
                            }
                            write((S<"<name>%s</name>\n">()) % escape(k));
                            dump(v, write);
                            write(S<"</member>\n">());
                        }
                        write(S<"</struct></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(Mod<"datetime">().attr("datetime"), w.method("Marshaller", "dump_datetime", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><dateTime.iso8601>">());
                        write((G<"xmlrpc.client._strftime">())(value));
                        write(S<"</dateTime.iso8601></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(G<"xmlrpc.client.DateTime">(), w.method("Marshaller", "dump_instance", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if ((G<"xmlrpc.client.WRAPPERS">()).contains(value.attr("__class__")))
                        {
                            self.set_attr("write", write);
                            value.method("encode", { self });
                            del_attr(self, "write");
                        }
                        else
                        {
                            // store instance attributes as a struct (really?)
                            self.method("dump_struct", { value.attr("__dict__"), write });
                        }
                        return none();
                    })); return t; }() },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"int">(), w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"float">(), w.method("Marshaller", "dump_double", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><double>">());
                        write(S(value.repr()));
                        write(S<"</double></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"str">(), w.method("Marshaller", "dump_unicode", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        write(S<"<value><string>">());
                        write(escape(value));
                        write(S<"</string></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytes">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytearray">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"tuple">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(B<"list">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(B<"dict">(), w.method("Marshaller", "dump_struct", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        py dump;
                        py i;
                        py k;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive dictionaries">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><struct>\n">());
                        for (py const& item : iterate{ value.method("items") })
                        {
                            auto const items13 = unpack(item, 2);
                            k = items13[0];
                            v = items13[1];
                            write(S<"<member>\n">());
                            if (!isinstance(k, B<"str">()))
                            {
                                py_raise((B<"TypeError">())(S<"dictionary key must be string">()));
                            }
                            write((S<"<name>%s</name>\n">()) % escape(k));
                            dump(v, write);
                            write(S<"</member>\n">());
                        }
                        write(S<"</struct></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(Mod<"datetime">().attr("datetime"), w.method("Marshaller", "dump_datetime", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><dateTime.iso8601>">());
                        write((G<"xmlrpc.client._strftime">())(value));
                        write(S<"</dateTime.iso8601></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(G<"xmlrpc.client.DateTime">(), w.method("Marshaller", "dump_instance", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if ((G<"xmlrpc.client.WRAPPERS">()).contains(value.attr("__class__")))
                        {
                            self.set_attr("write", write);
                            value.method("encode", { self });
                            del_attr(self, "write");
                        }
                        else
                        {
                            // store instance attributes as a struct (really?)
                            self.method("dump_struct", { value.attr("__dict__"), write });
                        }
                        return none();
                    })); return t; }(); t.set_item(G<"xmlrpc.client.Binary">(), w.method("Marshaller", "dump_instance", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if ((G<"xmlrpc.client.WRAPPERS">()).contains(value.attr("__class__")))
                        {
                            self.set_attr("write", write);
                            value.method("encode", { self });
                            del_attr(self, "write");
                        }
                        else
                        {
                            // store instance attributes as a struct (really?)
                            self.method("dump_struct", { value.attr("__dict__"), write });
                        }
                        return none();
                    })); return t; }() },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item((B<"type">())(none()), w.method("Marshaller", "dump_nil", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (!self.attr("allow_none").truthy())
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal None unless allow_none is enabled">()));
                        }
                        write(S<"<value><nil/></value>">());
                        return none();
                    })); return t; }(); t.set_item(B<"bool">(), w.method("Marshaller", "dump_bool", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><boolean>">());
                        write([&]() -> py { py const t = [&]() -> py { py const t = value; return !t.truthy() ? t : S<"1">(); }(); return t.truthy() ? t : S<"0">(); }());
                        write(S<"</boolean></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"int">(), w.method("Marshaller", "dump_long", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if (((value > (G<"xmlrpc.client.MAXINT">())) || (value < (G<"xmlrpc.client.MININT">()))))
                        {
                            py_raise((B<"OverflowError">())(S<"int exceeds XML-RPC limits">()));
                        }
                        write(S<"<value><int>">());
                        write((B<"str">())((B<"int">())(value)));
                        write(S<"</int></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"float">(), w.method("Marshaller", "dump_double", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><double>">());
                        write(S(value.repr()));
                        write(S<"</double></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"str">(), w.method("Marshaller", "dump_unicode", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        write(S<"<value><string>">());
                        write(escape(value));
                        write(S<"</string></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytes">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"bytearray">(), w.method("Marshaller", "dump_bytes", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py encoded;
                        write(S<"<value><base64>\n">());
                        encoded = (Mod<"base64">().attr("encodebytes"))(value);
                        write(encoded.method("decode", { S<"ascii">() }));
                        write(S<"</base64></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(B<"tuple">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(B<"list">(), w.method("Marshaller", "dump_array", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py dump;
                        py i;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive sequences">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><array><data>\n">());
                        for (py const& item : iterate{ value })
                        {
                            v = item;
                            dump(v, write);
                        }
                        write(S<"</data></array></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(B<"dict">(), w.method("Marshaller", "dump_struct", { py_positional("value"), py_positional("write"), py_positional("escape", G<"xmlrpc.client.escape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        py escape = a[3];
                        py dump;
                        py i;
                        py k;
                        py v;
                        i = (B<"id">())(value);
                        if (self.attr("memo").contains(i))
                        {
                            py_raise((B<"TypeError">())(S<"cannot marshal recursive dictionaries">()));
                        }
                        self.attr("memo").set_item(i, none());
                        dump = self.attr("_Marshaller__dump");
                        write(S<"<value><struct>\n">());
                        for (py const& item : iterate{ value.method("items") })
                        {
                            auto const items13 = unpack(item, 2);
                            k = items13[0];
                            v = items13[1];
                            write(S<"<member>\n">());
                            if (!isinstance(k, B<"str">()))
                            {
                                py_raise((B<"TypeError">())(S<"dictionary key must be string">()));
                            }
                            write((S<"<name>%s</name>\n">()) % escape(k));
                            dump(v, write);
                            write(S<"</member>\n">());
                        }
                        write(S<"</struct></value>\n">());
                        del_item(self.attr("memo"), i);
                        return none();
                    })); return t; }(); t.set_item(Mod<"datetime">().attr("datetime"), w.method("Marshaller", "dump_datetime", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        write(S<"<value><dateTime.iso8601>">());
                        write((G<"xmlrpc.client._strftime">())(value));
                        write(S<"</dateTime.iso8601></value>\n">());
                        return none();
                    })); return t; }(); t.set_item(G<"xmlrpc.client.DateTime">(), w.method("Marshaller", "dump_instance", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if ((G<"xmlrpc.client.WRAPPERS">()).contains(value.attr("__class__")))
                        {
                            self.set_attr("write", write);
                            value.method("encode", { self });
                            del_attr(self, "write");
                        }
                        else
                        {
                            // store instance attributes as a struct (really?)
                            self.method("dump_struct", { value.attr("__dict__"), write });
                        }
                        return none();
                    })); return t; }(); t.set_item(G<"xmlrpc.client.Binary">(), w.method("Marshaller", "dump_instance", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if ((G<"xmlrpc.client.WRAPPERS">()).contains(value.attr("__class__")))
                        {
                            self.set_attr("write", write);
                            value.method("encode", { self });
                            del_attr(self, "write");
                        }
                        else
                        {
                            // store instance attributes as a struct (really?)
                            self.method("dump_struct", { value.attr("__dict__"), write });
                        }
                        return none();
                    })); return t; }(); t.set_item(S<"_arbitrary_instance">(), w.method("Marshaller", "dump_instance", { py_positional("value"), py_positional("write") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py write = a[2];
                        if ((G<"xmlrpc.client.WRAPPERS">()).contains(value.attr("__class__")))
                        {
                            self.set_attr("write", write);
                            value.method("encode", { self });
                            del_attr(self, "write");
                        }
                        else
                        {
                            // store instance attributes as a struct (really?)
                            self.method("dump_struct", { value.attr("__dict__"), write });
                        }
                        return none();
                    })); return t; }() }, }));

            // #
            // XML-RPC unmarshaller.
            // 
            // @see loads
                // and again, if you don't understand what's going on in here,
                // that's perfectly ok.
                // 
                // event handlers
                // FIXME: assert standalone == 1 ???
                // 
                // accelerator support
                // 
                // element decoders
            define("xmlrpc.client", "Unmarshaller", w.make_class("Unmarshaller", {  }, {
                { "__init__", w.method("Unmarshaller", "__init__", { py_positional("use_datetime", boolean(false)), py_positional("use_builtin_types", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py use_datetime = a[1];
                        py use_builtin_types = a[2];
                        self.set_attr("_type", none());
                        self.set_attr("_stack", py_list());
                        self.set_attr("_marks", py_list());
                        self.set_attr("_data", py_list());
                        self.set_attr("_value", boolean(false));
                        self.set_attr("_methodname", none());
                        self.set_attr("_encoding", S<"utf-8">());
                        self.set_attr("append", self.attr("_stack").attr("append"));
                        self.set_attr("_use_datetime", [&]() -> py { py const t = use_builtin_types; return t.truthy() ? t : use_datetime; }());
                        self.set_attr("_use_bytes", use_builtin_types);
                        return none();
                    }) },
                { "close", w.method("Unmarshaller", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((self.attr("_type").is_none() || self.attr("_marks").truthy()))
                        {
                            py_raise((G<"xmlrpc.client.ResponseError">())());
                        }
                        if (self.attr("_type") == (S<"fault">()))
                        {
                            py_raise(call_ex((G<"xmlrpc.client.Fault">()), {  }, py{}, self.attr("_stack")[I<0>()]));
                        }
                        return (B<"tuple">())(self.attr("_stack"));
                    }) },
                { "getmethodname", w.method("Unmarshaller", "getmethodname", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_methodname");
                    }) },
                { "xml", w.method("Unmarshaller", "xml", { py_positional("encoding"), py_positional("standalone") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py encoding = a[1];
                        py standalone = a[2];
                        self.set_attr("_encoding", encoding);
                        return none();
                    }) },
                { "start", w.method("Unmarshaller", "start", { py_positional("tag"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        py attrs = a[2];
                        if (tag.contains((S<":">())))
                        {
                            tag = tag.method("split", { S<":">() })[I<-1>()];
                        }
                        if (((tag == (S<"array">())) || (tag == (S<"struct">()))))
                        {
                            self.attr("_marks").method("append", { py{ self.attr("_stack").size() } });
                        }
                        self.set_attr("_data", py_list());
                        if ((self.attr("_value").truthy() && (!self.attr("dispatch").contains(tag))))
                        {
                            py_raise((G<"xmlrpc.client.ResponseError">())((S<"unknown tag %r">()) % tag));
                        }
                        self.set_attr("_value", boolean(tag == (S<"value">())));
                        return none();
                    }) },
                { "data", w.method("Unmarshaller", "data", { py_positional("text") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        self.attr("_data").method("append", { text });
                        return none();
                    }) },
                { "end", w.method("Unmarshaller", "end", { py_positional("tag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        py f;
                        try
                        {
                            f = self.attr("dispatch")[tag];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler14;
                            if (matches(handler14.exception, B<"KeyError">()))
                            {
                                if (!tag.contains((S<":">())))
                                {
                                    return none();
                                }
                                try
                                {
                                    f = self.attr("dispatch")[tag.method("split", { S<":">() })[I<-1>()]];
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler15;
                                    if (matches(handler15.exception, B<"KeyError">()))
                                    {
                                        return none();
                                    }
                                    else
                                        py_reraise(handler15.exception);
                                }
                            }
                            else
                                py_reraise(handler14.exception);
                        }
                        return f(self, (S<"">()).method("join", { self.attr("_data") }));
                    }) },
                { "end_dispatch", w.method("Unmarshaller", "end_dispatch", { py_positional("tag"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        py data = a[2];
                        py f;
                        try
                        {
                            f = self.attr("dispatch")[tag];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler16;
                            if (matches(handler16.exception, B<"KeyError">()))
                            {
                                if (!tag.contains((S<":">())))
                                {
                                    return none();
                                }
                                try
                                {
                                    f = self.attr("dispatch")[tag.method("split", { S<":">() })[I<-1>()]];
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler17;
                                    if (matches(handler17.exception, B<"KeyError">()))
                                    {
                                        return none();
                                    }
                                    else
                                        py_reraise(handler17.exception);
                                }
                            }
                            else
                                py_reraise(handler16.exception);
                        }
                        return f(self, data);
                    }) },
                { "dispatch", py_dict() },
                { "end_nil", w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "end_boolean", w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "end_int", w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "end_double", w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"float">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "end_bigdecimal", w.method("Unmarshaller", "end_bigdecimal", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (Mod<"decimal">().attr("Decimal"))(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"float">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"bigdecimal">(), w.method("Unmarshaller", "end_bigdecimal", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (Mod<"decimal">().attr("Decimal"))(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "end_string", w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"float">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"bigdecimal">(), w.method("Unmarshaller", "end_bigdecimal", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (Mod<"decimal">().attr("Decimal"))(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"string">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"float">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"bigdecimal">(), w.method("Unmarshaller", "end_bigdecimal", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (Mod<"decimal">().attr("Decimal"))(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"string">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"name">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "end_array", w.method("Unmarshaller", "end_array", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map arrays to Python lists
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ self.attr("_stack")[B<"slice">()(mark, none(), none())] }));
                        self.set_attr("_value", I<0>());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"float">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"bigdecimal">(), w.method("Unmarshaller", "end_bigdecimal", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (Mod<"decimal">().attr("Decimal"))(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"string">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"name">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"array">(), w.method("Unmarshaller", "end_array", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map arrays to Python lists
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ self.attr("_stack")[B<"slice">()(mark, none(), none())] }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "end_struct", w.method("Unmarshaller", "end_struct", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py dict;
                        py i;
                        py items;
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map structs to Python dictionaries
                        dict = py_dict();
                        items = self.attr("_stack")[B<"slice">()(mark, none(), none())];
                        for (py const& item : iterate{ (B<"range">())(I<0>(), py{ items.size() }, I<2>()) })
                        {
                            i = item;
                            dict.set_item(items[i], items[i + (I<1>())]);
                        }
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ dict }));
                        self.set_attr("_value", I<0>());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"float">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"bigdecimal">(), w.method("Unmarshaller", "end_bigdecimal", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (Mod<"decimal">().attr("Decimal"))(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"string">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"name">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"array">(), w.method("Unmarshaller", "end_array", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map arrays to Python lists
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ self.attr("_stack")[B<"slice">()(mark, none(), none())] }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"struct">(), w.method("Unmarshaller", "end_struct", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py dict;
                        py i;
                        py items;
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map structs to Python dictionaries
                        dict = py_dict();
                        items = self.attr("_stack")[B<"slice">()(mark, none(), none())];
                        for (py const& item : iterate{ (B<"range">())(I<0>(), py{ items.size() }, I<2>()) })
                        {
                            i = item;
                            dict.set_item(items[i], items[i + (I<1>())]);
                        }
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ dict }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "end_base64", w.method("Unmarshaller", "end_base64", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.Binary">())();
                        value.method("decode", { data.method("encode", { S<"ascii">() }) });
                        if (self.attr("_use_bytes").truthy())
                        {
                            value = value.attr("data");
                        }
                        self.method("append", { value });
                        self.set_attr("_value", I<0>());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"float">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"bigdecimal">(), w.method("Unmarshaller", "end_bigdecimal", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (Mod<"decimal">().attr("Decimal"))(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"string">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"name">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"array">(), w.method("Unmarshaller", "end_array", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map arrays to Python lists
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ self.attr("_stack")[B<"slice">()(mark, none(), none())] }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"struct">(), w.method("Unmarshaller", "end_struct", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py dict;
                        py i;
                        py items;
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map structs to Python dictionaries
                        dict = py_dict();
                        items = self.attr("_stack")[B<"slice">()(mark, none(), none())];
                        for (py const& item : iterate{ (B<"range">())(I<0>(), py{ items.size() }, I<2>()) })
                        {
                            i = item;
                            dict.set_item(items[i], items[i + (I<1>())]);
                        }
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ dict }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"base64">(), w.method("Unmarshaller", "end_base64", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.Binary">())();
                        value.method("decode", { data.method("encode", { S<"ascii">() }) });
                        if (self.attr("_use_bytes").truthy())
                        {
                            value = value.attr("data");
                        }
                        self.method("append", { value });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }() },
                { "end_dateTime", w.method("Unmarshaller", "end_dateTime", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.DateTime">())();
                        value.method("decode", { data });
                        if (self.attr("_use_datetime").truthy())
                        {
                            value = (G<"xmlrpc.client._datetime_type">())(data);
                        }
                        self.method("append", { value });
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"float">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"bigdecimal">(), w.method("Unmarshaller", "end_bigdecimal", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (Mod<"decimal">().attr("Decimal"))(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"string">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"name">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"array">(), w.method("Unmarshaller", "end_array", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map arrays to Python lists
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ self.attr("_stack")[B<"slice">()(mark, none(), none())] }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"struct">(), w.method("Unmarshaller", "end_struct", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py dict;
                        py i;
                        py items;
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map structs to Python dictionaries
                        dict = py_dict();
                        items = self.attr("_stack")[B<"slice">()(mark, none(), none())];
                        for (py const& item : iterate{ (B<"range">())(I<0>(), py{ items.size() }, I<2>()) })
                        {
                            i = item;
                            dict.set_item(items[i], items[i + (I<1>())]);
                        }
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ dict }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"base64">(), w.method("Unmarshaller", "end_base64", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.Binary">())();
                        value.method("decode", { data.method("encode", { S<"ascii">() }) });
                        if (self.attr("_use_bytes").truthy())
                        {
                            value = value.attr("data");
                        }
                        self.method("append", { value });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"dateTime.iso8601">(), w.method("Unmarshaller", "end_dateTime", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.DateTime">())();
                        value.method("decode", { data });
                        if (self.attr("_use_datetime").truthy())
                        {
                            value = (G<"xmlrpc.client._datetime_type">())(data);
                        }
                        self.method("append", { value });
                        return none();
                    })); return t; }() },
                { "end_value", w.method("Unmarshaller", "end_value", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_value").truthy())
                        {
                            self.method("end_string", { data });
                        }
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"float">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"bigdecimal">(), w.method("Unmarshaller", "end_bigdecimal", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (Mod<"decimal">().attr("Decimal"))(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"string">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"name">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"array">(), w.method("Unmarshaller", "end_array", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map arrays to Python lists
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ self.attr("_stack")[B<"slice">()(mark, none(), none())] }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"struct">(), w.method("Unmarshaller", "end_struct", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py dict;
                        py i;
                        py items;
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map structs to Python dictionaries
                        dict = py_dict();
                        items = self.attr("_stack")[B<"slice">()(mark, none(), none())];
                        for (py const& item : iterate{ (B<"range">())(I<0>(), py{ items.size() }, I<2>()) })
                        {
                            i = item;
                            dict.set_item(items[i], items[i + (I<1>())]);
                        }
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ dict }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"base64">(), w.method("Unmarshaller", "end_base64", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.Binary">())();
                        value.method("decode", { data.method("encode", { S<"ascii">() }) });
                        if (self.attr("_use_bytes").truthy())
                        {
                            value = value.attr("data");
                        }
                        self.method("append", { value });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"dateTime.iso8601">(), w.method("Unmarshaller", "end_dateTime", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.DateTime">())();
                        value.method("decode", { data });
                        if (self.attr("_use_datetime").truthy())
                        {
                            value = (G<"xmlrpc.client._datetime_type">())(data);
                        }
                        self.method("append", { value });
                        return none();
                    })); return t; }(); t.set_item(S<"value">(), w.method("Unmarshaller", "end_value", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_value").truthy())
                        {
                            self.method("end_string", { data });
                        }
                        return none();
                    })); return t; }() },
                { "end_params", w.method("Unmarshaller", "end_params", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("_type", S<"params">());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"float">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"bigdecimal">(), w.method("Unmarshaller", "end_bigdecimal", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (Mod<"decimal">().attr("Decimal"))(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"string">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"name">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"array">(), w.method("Unmarshaller", "end_array", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map arrays to Python lists
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ self.attr("_stack")[B<"slice">()(mark, none(), none())] }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"struct">(), w.method("Unmarshaller", "end_struct", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py dict;
                        py i;
                        py items;
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map structs to Python dictionaries
                        dict = py_dict();
                        items = self.attr("_stack")[B<"slice">()(mark, none(), none())];
                        for (py const& item : iterate{ (B<"range">())(I<0>(), py{ items.size() }, I<2>()) })
                        {
                            i = item;
                            dict.set_item(items[i], items[i + (I<1>())]);
                        }
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ dict }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"base64">(), w.method("Unmarshaller", "end_base64", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.Binary">())();
                        value.method("decode", { data.method("encode", { S<"ascii">() }) });
                        if (self.attr("_use_bytes").truthy())
                        {
                            value = value.attr("data");
                        }
                        self.method("append", { value });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"dateTime.iso8601">(), w.method("Unmarshaller", "end_dateTime", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.DateTime">())();
                        value.method("decode", { data });
                        if (self.attr("_use_datetime").truthy())
                        {
                            value = (G<"xmlrpc.client._datetime_type">())(data);
                        }
                        self.method("append", { value });
                        return none();
                    })); return t; }(); t.set_item(S<"value">(), w.method("Unmarshaller", "end_value", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_value").truthy())
                        {
                            self.method("end_string", { data });
                        }
                        return none();
                    })); return t; }(); t.set_item(S<"params">(), w.method("Unmarshaller", "end_params", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("_type", S<"params">());
                        return none();
                    })); return t; }() },
                { "end_fault", w.method("Unmarshaller", "end_fault", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("_type", S<"fault">());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"float">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"bigdecimal">(), w.method("Unmarshaller", "end_bigdecimal", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (Mod<"decimal">().attr("Decimal"))(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"string">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"name">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"array">(), w.method("Unmarshaller", "end_array", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map arrays to Python lists
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ self.attr("_stack")[B<"slice">()(mark, none(), none())] }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"struct">(), w.method("Unmarshaller", "end_struct", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py dict;
                        py i;
                        py items;
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map structs to Python dictionaries
                        dict = py_dict();
                        items = self.attr("_stack")[B<"slice">()(mark, none(), none())];
                        for (py const& item : iterate{ (B<"range">())(I<0>(), py{ items.size() }, I<2>()) })
                        {
                            i = item;
                            dict.set_item(items[i], items[i + (I<1>())]);
                        }
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ dict }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"base64">(), w.method("Unmarshaller", "end_base64", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.Binary">())();
                        value.method("decode", { data.method("encode", { S<"ascii">() }) });
                        if (self.attr("_use_bytes").truthy())
                        {
                            value = value.attr("data");
                        }
                        self.method("append", { value });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"dateTime.iso8601">(), w.method("Unmarshaller", "end_dateTime", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.DateTime">())();
                        value.method("decode", { data });
                        if (self.attr("_use_datetime").truthy())
                        {
                            value = (G<"xmlrpc.client._datetime_type">())(data);
                        }
                        self.method("append", { value });
                        return none();
                    })); return t; }(); t.set_item(S<"value">(), w.method("Unmarshaller", "end_value", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_value").truthy())
                        {
                            self.method("end_string", { data });
                        }
                        return none();
                    })); return t; }(); t.set_item(S<"params">(), w.method("Unmarshaller", "end_params", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("_type", S<"params">());
                        return none();
                    })); return t; }(); t.set_item(S<"fault">(), w.method("Unmarshaller", "end_fault", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("_type", S<"fault">());
                        return none();
                    })); return t; }() },
                { "end_methodName", w.method("Unmarshaller", "end_methodName", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.set_attr("_methodname", data);
                        self.set_attr("_type", S<"methodName">());
                        return none();
                    }) },
                { "dispatch", [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = [&]() -> py { py const t = py_dict(); t.set_item(S<"nil">(), w.method("Unmarshaller", "end_nil", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { none() });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"boolean">(), w.method("Unmarshaller", "end_boolean", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data == (S<"0">()))
                        {
                            self.method("append", { boolean(false) });
                        }
                        else if (data == (S<"1">()))
                        {
                            self.method("append", { boolean(true) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"bad boolean value">()));
                        }
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i1">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i2">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i4">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"i8">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"int">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"biginteger">(), w.method("Unmarshaller", "end_int", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"int">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"double">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"float">(), w.method("Unmarshaller", "end_double", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (B<"float">())(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"bigdecimal">(), w.method("Unmarshaller", "end_bigdecimal", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.method("append", { (Mod<"decimal">().attr("Decimal"))(data) });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"string">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"name">(), w.method("Unmarshaller", "end_string", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.method("append", { data });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"array">(), w.method("Unmarshaller", "end_array", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map arrays to Python lists
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ self.attr("_stack")[B<"slice">()(mark, none(), none())] }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"struct">(), w.method("Unmarshaller", "end_struct", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py dict;
                        py i;
                        py items;
                        py mark;
                        mark = self.attr("_marks").method("pop");
                        // map structs to Python dictionaries
                        dict = py_dict();
                        items = self.attr("_stack")[B<"slice">()(mark, none(), none())];
                        for (py const& item : iterate{ (B<"range">())(I<0>(), py{ items.size() }, I<2>()) })
                        {
                            i = item;
                            dict.set_item(items[i], items[i + (I<1>())]);
                        }
                        self.attr("_stack").set_item(B<"slice">()(mark, none(), none()), py_list({ dict }));
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"base64">(), w.method("Unmarshaller", "end_base64", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.Binary">())();
                        value.method("decode", { data.method("encode", { S<"ascii">() }) });
                        if (self.attr("_use_bytes").truthy())
                        {
                            value = value.attr("data");
                        }
                        self.method("append", { value });
                        self.set_attr("_value", I<0>());
                        return none();
                    })); return t; }(); t.set_item(S<"dateTime.iso8601">(), w.method("Unmarshaller", "end_dateTime", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py value;
                        value = (G<"xmlrpc.client.DateTime">())();
                        value.method("decode", { data });
                        if (self.attr("_use_datetime").truthy())
                        {
                            value = (G<"xmlrpc.client._datetime_type">())(data);
                        }
                        self.method("append", { value });
                        return none();
                    })); return t; }(); t.set_item(S<"value">(), w.method("Unmarshaller", "end_value", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_value").truthy())
                        {
                            self.method("end_string", { data });
                        }
                        return none();
                    })); return t; }(); t.set_item(S<"params">(), w.method("Unmarshaller", "end_params", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("_type", S<"params">());
                        return none();
                    })); return t; }(); t.set_item(S<"fault">(), w.method("Unmarshaller", "end_fault", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("_type", S<"fault">());
                        return none();
                    })); return t; }(); t.set_item(S<"methodName">(), w.method("Unmarshaller", "end_methodName", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_encoding").truthy())
                        {
                            data = data.method("decode", { self.attr("_encoding") });
                        }
                        self.set_attr("_methodname", data);
                        self.set_attr("_type", S<"methodName">());
                        return none();
                    })); return t; }() }, }));

            // # Multicall support
            // 
                // some lesser magic to store calls made to a MultiCall object
                // for batch execution
            define("xmlrpc.client", "_MultiCallMethod", w.make_class("_MultiCallMethod", {  }, {
                { "__init__", w.method("_MultiCallMethod", "__init__", { py_positional("call_list"), py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py call_list = a[1];
                        py name = a[2];
                        self.set_attr("_MultiCallMethod__call_list", call_list);
                        self.set_attr("_MultiCallMethod__name", name);
                        return none();
                    }) },
                { "__getattr__", w.method("_MultiCallMethod", "__getattr__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return (G<"xmlrpc.client._MultiCallMethod">())(self.attr("_MultiCallMethod__call_list"), (S<"%s.%s">()) % py_tuple({ self.attr("_MultiCallMethod__name"), name }));
                    }) },
                { "__call__", w.method("_MultiCallMethod", "__call__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        self.attr("_MultiCallMethod__call_list").method("append", { py_tuple({ self.attr("_MultiCallMethod__name"), args }) });
                        return none();
                    }, true) }, }));

            define("xmlrpc.client", "MultiCallIterator", w.make_class("MultiCallIterator", {  }, {
                { "__init__", w.method("MultiCallIterator", "__init__", { py_positional("results") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py results = a[1];
                        self.set_attr("results", results);
                        return none();
                    }) },
                { "__getitem__", w.method("MultiCallIterator", "__getitem__", { py_positional("i") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py item_;
                        item_ = self.attr("results")[i];
                        if (isinstance(item_, B<"dict">()))
                        {
                            py_raise((G<"xmlrpc.client.Fault">())(item_[S<"faultCode">()], item_[S<"faultString">()]));
                        }
                        else if (isinstance(item_, B<"list">()))
                        {
                            return item_[I<0>()];
                        }
                        else
                        {
                            py_raise((B<"ValueError">())(S<"unexpected type in multicall result">()));
                        }
                        return none();
                    }) }, }));

            define("xmlrpc.client", "MultiCall", w.make_class("MultiCall", {  }, {
                { "__init__", w.method("MultiCall", "__init__", { py_positional("server") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py server = a[1];
                        self.set_attr("_MultiCall__server", server);
                        self.set_attr("_MultiCall__call_list", py_list());
                        return none();
                    }) },
                { "__repr__", w.method("MultiCall", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s at %#x>">()) % py_tuple({ self.attr("__class__").attr("__name__"), (B<"id">())(self) });
                    }) },
                { "__getattr__", w.method("MultiCall", "__getattr__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return (G<"xmlrpc.client._MultiCallMethod">())(self.attr("_MultiCall__call_list"), name);
                    }) },
                { "__call__", w.method("MultiCall", "__call__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args;
                        py marshalled_list;
                        py name;
                        marshalled_list = py_list();
                        for (py const& item : iterate{ self.attr("_MultiCall__call_list") })
                        {
                            auto const items20 = unpack(item, 2);
                            name = items20[0];
                            args = items20[1];
                            marshalled_list.method("append", { dict_of({ { S<"methodName">(), name }, { S<"params">(), args } }) });
                        }
                        return (G<"xmlrpc.client.MultiCallIterator">())(self.attr("_MultiCall__server").attr("system").method("multicall", { marshalled_list }));
                    }) }, }));

            // --------------------------------------------------------------------
            // convenience functions
            define("xmlrpc.client", "FastMarshaller", none());
            define("xmlrpc.client", "FastParser", none());
            define("xmlrpc.client", "FastUnmarshaller", none());
            // #
            // Create a parser object, and connect it to an unmarshalling instance.
            // This function picks the fastest available XML parser.
            // 
            // return A (parser, unmarshaller) tuple.
            define("xmlrpc.client", "getparser", w.function("getparser", "getparser", { py_positional("use_datetime", boolean(false)), py_positional("use_builtin_types", boolean(false)) }, [](arguments a) -> py
                {
                    py use_datetime = a[0];
                    py use_builtin_types = a[1];
                    py mkbytes;
                    py mkdatetime;
                    py parser;
                    py target;
                    if (((G<"xmlrpc.client.FastParser">()).truthy() && (G<"xmlrpc.client.FastUnmarshaller">()).truthy()))
                    {
                        if (use_builtin_types.truthy())
                        {
                            mkdatetime = G<"xmlrpc.client._datetime_type">();
                            mkbytes = Mod<"base64">().attr("decodebytes");
                        }
                        else if (use_datetime.truthy())
                        {
                            mkdatetime = G<"xmlrpc.client._datetime_type">();
                            mkbytes = G<"xmlrpc.client._binary">();
                        }
                        else
                        {
                            mkdatetime = G<"xmlrpc.client._datetime">();
                            mkbytes = G<"xmlrpc.client._binary">();
                        }
                        target = (G<"xmlrpc.client.FastUnmarshaller">())(boolean(true), boolean(false), mkbytes, mkdatetime, G<"xmlrpc.client.Fault">());
                        parser = (G<"xmlrpc.client.FastParser">())(target);
                    }
                    else
                    {
                        target = (G<"xmlrpc.client.Unmarshaller">()).call({  }, { { "use_datetime", use_datetime }, { "use_builtin_types", use_builtin_types } });
                        if ((G<"xmlrpc.client.FastParser">()).truthy())
                        {
                            parser = (G<"xmlrpc.client.FastParser">())(target);
                        }
                        else
                        {
                            parser = (G<"xmlrpc.client.ExpatParser">())(target);
                        }
                    }
                    return py_tuple({ parser, target });
                }));

            // #
            // Convert a Python tuple or a Fault instance to an XML-RPC packet.
            // 
            // @def dumps(params, **options)
            // @param params A tuple or Fault instance.
            // @keyparam methodname If given, create a methodCall request for
            // this method name.
            // @keyparam methodresponse If given, create a methodResponse packet.
            // If used with a tuple, the tuple must be a singleton (that is,
            // it must contain exactly one element).
            // @keyparam encoding The packet encoding.
            // @return A string containing marshalled data.
            define("xmlrpc.client", "dumps", w.function("dumps", "dumps", { py_positional("params"), py_positional("methodname", none()), py_positional("methodresponse", none()), py_positional("encoding", none()), py_positional("allow_none", boolean(false)) }, [](arguments a) -> py
                {
                    py params = a[0];
                    py methodname = a[1];
                    py methodresponse = a[2];
                    py encoding = a[3];
                    py allow_none = a[4];
                    py data;
                    py m;
                    py xmlheader;
                    if (!isinstance(params, py_tuple({ B<"tuple">(), G<"xmlrpc.client.Fault">() })))
                        py_raise(B<"AssertionError">()(S<"argument must be tuple or Fault instance">()));
                    if (isinstance(params, G<"xmlrpc.client.Fault">()))
                    {
                        methodresponse = I<1>();
                    }
                    else if ((methodresponse.truthy() && isinstance(params, B<"tuple">())))
                    {
                        if (!(py{ params.size() } == (I<1>())))
                            py_raise(B<"AssertionError">()(S<"response tuple must be a singleton">()));
                    }
                    if (!encoding.truthy())
                    {
                        encoding = S<"utf-8">();
                    }
                    if ((G<"xmlrpc.client.FastMarshaller">()).truthy())
                    {
                        m = (G<"xmlrpc.client.FastMarshaller">())(encoding);
                    }
                    else
                    {
                        m = (G<"xmlrpc.client.Marshaller">())(encoding, allow_none);
                    }
                    data = m.method("dumps", { params });
                    if (encoding != (S<"utf-8">()))
                    {
                        xmlheader = (S<"<?xml version='1.0' encoding='%s'?>\n">()) % (B<"str">())(encoding);
                    }
                    else
                    {
                        xmlheader = S<"<?xml version='1.0'?>\n">();
                    }
                    // standard XML-RPC wrappings
                    if (methodname.truthy())
                    {
                        // a method call
                        data = py_tuple({ xmlheader, S<"<methodCall>\n<methodName>">(), (G<"xmlrpc.client.escape">())(methodname), S<"</methodName>\n">(), data, S<"</methodCall>\n">() });
                    }
                    else if (methodresponse.truthy())
                    {
                        // a method response, or a fault structure
                        data = py_tuple({ xmlheader, S<"<methodResponse>\n">(), data, S<"</methodResponse>\n">() });
                    }
                    else
                    {
                        return data;
                    }
                    return (S<"">()).method("join", { data });
                }));

            // #
            // Convert an XML-RPC packet to a Python object.  If the XML-RPC packet
            // represents a fault condition, this function raises a Fault exception.
            // 
            // @param data An XML-RPC packet, given as an 8-bit string.
            // @return A tuple containing the unpacked data, and the method name
            // (None if not present).
            // @see Fault
            define("xmlrpc.client", "loads", w.function("loads", "loads", { py_positional("data"), py_positional("use_datetime", boolean(false)), py_positional("use_builtin_types", boolean(false)) }, [](arguments a) -> py
                {
                    py data = a[0];
                    py use_datetime = a[1];
                    py use_builtin_types = a[2];
                    py p;
                    py u;
                    auto const items21 = unpack((G<"xmlrpc.client.getparser">()).call({  }, { { "use_datetime", use_datetime }, { "use_builtin_types", use_builtin_types } }), 2);
                    p = items21[0];
                    u = items21[1];
                    p.method("feed", { data });
                    p.method("close");
                    return py_tuple({ u.method("close"), u.method("getmethodname") });
                }));

            // #
            // Encode a string using the gzip content encoding such as specified by the
            // Content-Encoding: gzip
            // in the HTTP header, as described in RFC 1952
            // 
            // @param data the unencoded data
            // @return the encoded data
            define("xmlrpc.client", "gzip_encode", w.function("gzip_encode", "gzip_encode", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py f;
                    py gzf;
                    if (!(Mod<"gzip">()).truthy())
                    {
                        py_raise(B<"NotImplementedError">());
                    }
                    f = (Mod<"io">().attr("BytesIO"))();
                    {
                        py const manager22 = (Mod<"gzip">().attr("GzipFile")).call({  }, { { "mode", S<"wb">() }, { "fileobj", f }, { "compresslevel", I<1>() } });
                        py const exit22 = pyclass::special_method(manager22, "__exit__");
                        py const value22 = pyclass::special_method(manager22, "__enter__")(manager22);
                        gzf = value22;
                        [[maybe_unused]] int flow22 = 0;
                        [[maybe_unused]] py result22;
                        bool exited22 = false;
                        try
                        {
                            gzf.method("write", { data });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler22;
                            exited22 = true;
                            if (!exit22(manager22, type_of(handler22.exception), handler22.exception, handler22.exception.attr("__traceback__")).truthy())
                                py_reraise(handler22.exception);
                        }
                        finally22:
                        if (!exited22)
                            exit22(manager22, none(), none(), none());
                        if (flow22 == 1)
                            return result22;
                    }
                    return f.method("getvalue");
                }));

            // #
            // Decode a string using the gzip content encoding such as specified by the
            // Content-Encoding: gzip
            // in the HTTP header, as described in RFC 1952
            // 
            // @param data The encoded data
            // @keyparam max_decode Maximum bytes to decode (20 MiB default), use negative
            // values for unlimited decoding
            // @return the unencoded data
            // @raises ValueError if data is not correctly coded.
            // @raises ValueError if max gzipped payload length exceeded
            define("xmlrpc.client", "gzip_decode", w.function("gzip_decode", "gzip_decode", { py_positional("data"), py_positional("max_decode", I<20971520>()) }, [](arguments a) -> py
                {
                    py data = a[0];
                    py max_decode = a[1];
                    py decoded;
                    py gzf;
                    if (!(Mod<"gzip">()).truthy())
                    {
                        py_raise(B<"NotImplementedError">());
                    }
                    {
                        py const manager23 = (Mod<"gzip">().attr("GzipFile")).call({  }, { { "mode", S<"rb">() }, { "fileobj", (Mod<"io">().attr("BytesIO"))(data) } });
                        py const exit23 = pyclass::special_method(manager23, "__exit__");
                        py const value23 = pyclass::special_method(manager23, "__enter__")(manager23);
                        gzf = value23;
                        [[maybe_unused]] int flow23 = 0;
                        [[maybe_unused]] py result23;
                        bool exited23 = false;
                        try
                        {
                            try
                            {
                                if (max_decode < (I<0>()))
                                {
                                    decoded = gzf.method("read");
                                }
                                else
                                {
                                    decoded = gzf.method("read", { max_decode + (I<1>()) });
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler24;
                                if (matches(handler24.exception, B<"OSError">()))
                                {
                                    py_raise((B<"ValueError">())(S<"invalid data">()));
                                }
                                else
                                    py_reraise(handler24.exception);
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler23;
                            exited23 = true;
                            if (!exit23(manager23, type_of(handler23.exception), handler23.exception, handler23.exception.attr("__traceback__")).truthy())
                                py_reraise(handler23.exception);
                        }
                        finally23:
                        if (!exited23)
                            exit23(manager23, none(), none(), none());
                        if (flow23 == 1)
                            return result23;
                    }
                    if (((max_decode >= (I<0>())) && (py{ decoded.size() } > max_decode)))
                    {
                        py_raise((B<"ValueError">())(S<"max gzipped payload length exceeded">()));
                    }
                    return decoded;
                }));

            // #
            // Return a decoded file-like object for the gzip encoding
            // as described in RFC 1952.
            // 
            // @param response A stream supporting a read() method
            // @return a file-like object that the decoded data can be read() from
            define("xmlrpc.client", "GzipDecodedResponse", w.make_class("GzipDecodedResponse", { ((Mod<"gzip">()).truthy() ? Mod<"gzip">().attr("GzipFile") : B<"object">()) }, {
                { "__init__", w.method("GzipDecodedResponse", "__init__", { py_positional("response") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py response = a[1];
                        if (!(Mod<"gzip">()).truthy())
                        {
                            py_raise(B<"NotImplementedError">());
                        }
                        self.set_attr("io", (Mod<"io">().attr("BytesIO"))(response.method("read")));
                        (Mod<"gzip">().attr("GzipFile")).method("__init__", { self }, { { "mode", S<"rb">() }, { "fileobj", self.attr("io") } });
                        return none();
                    }) },
                { "close", w.method("GzipDecodedResponse", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            std::optional<py> pending25;
                            [[maybe_unused]] int flow25 = 0;
                            [[maybe_unused]] py result25;
                            try
                            {
                                (Mod<"gzip">().attr("GzipFile")).method("close", { self });
                            }
                            catch (raised const&)
                            {
                                pending25 = py_take_exception();
                            }
                            finally25:
                            self.attr("io").method("close");
                            if (pending25)
                                py_reraise(*pending25);
                            if (flow25 == 1)
                                return result25;
                        }
                        return none();
                    }) }, }));

            // --------------------------------------------------------------------
            // request dispatcher
                // some magic to bind an XML-RPC method to an RPC server.
                // supports "nested" methods (e.g. examples.getStateName)
            define("xmlrpc.client", "_Method", w.make_class("_Method", {  }, {
                { "__init__", w.method("_Method", "__init__", { py_positional("send"), py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py send = a[1];
                        py name = a[2];
                        self.set_attr("_Method__send", send);
                        self.set_attr("_Method__name", name);
                        return none();
                    }) },
                { "__getattr__", w.method("_Method", "__getattr__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return (G<"xmlrpc.client._Method">())(self.attr("_Method__send"), (S<"%s.%s">()) % py_tuple({ self.attr("_Method__name"), name }));
                    }) },
                { "__call__", w.method("_Method", "__call__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        return self.method("_Method__send", { self.attr("_Method__name"), args });
                    }, true) }, }));

            // #
            // Standard transport class for XML-RPC over HTTP.
            // <p>
            // You can create custom transports by subclassing this method, and
            // overriding selected methods.
                // client identifier (may be overridden)
                // if true, we'll request gzip encoding
                // if positive, encode request using gzip if it exceeds this threshold
                // note that many servers will get confused, so only use it if you know
                // that they can decode such a request
                // #
                // Send a complete request, and parse the response.
                // Retry request if a cached connection has disconnected.
                // 
                // @param host Target host.
                // @param handler Target PRC handler.
                // @param request_body XML-RPC request body.
                // @param verbose Debugging flag.
                // @return Parsed response.
                // #
                // Create parser.
                // 
                // @return A 2-tuple containing a parser and an unmarshaller.
                // #
                // Get authorization info from host parameter
                // Host may be a string, or a (host, x509-dict) tuple; if a string,
                // it is checked for a "user:pw@host" format, and a "Basic
                // Authentication" header is added if appropriate.
                // 
                // @param host Host descriptor (URL or (URL, x509 info) tuple).
                // @return A 3-tuple containing (actual host, extra headers,
                // x509 info).  The header and x509 fields may be None.
                // #
                // Connect to server.
                // 
                // @param host Target host.
                // @return An HTTPConnection object
                // #
                // Clear any cached connection object.
                // Used in the event of socket errors.
                // 
                // #
                // Send HTTP request.
                // 
                // @param host Host descriptor (URL or (URL, x509 info) tuple).
                // @param handler Target RPC handler (a path relative to host)
                // @param request_body The XML-RPC request body
                // @param debug Enable debugging if debug is true.
                // @return An HTTPConnection.
                // #
                // Send request headers.
                // This function provides a useful hook for subclassing
                // 
                // @param connection httpConnection.
                // @param headers list of key,value pairs for HTTP headers
                // #
                // Send request body.
                // This function provides a useful hook for subclassing
                // 
                // @param connection httpConnection.
                // @param request_body XML-RPC request body.
                // #
                // Parse response.
                // 
                // @param file Stream.
                // @return Response tuple and target method.
            define("xmlrpc.client", "Transport", w.make_class("Transport", {  }, {
                { "user_agent", (S<"Python-xmlrpc/%s">()) % (G<"xmlrpc.client.__version__">()) },
                { "accept_gzip_encoding", boolean(true) },
                { "encode_threshold", none() },
                { "__init__", w.method("Transport", "__init__", { py_positional("use_datetime", boolean(false)), py_positional("use_builtin_types", boolean(false)), py_keyword("headers", py_tuple()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py use_datetime = a[1];
                        py use_builtin_types = a[2];
                        py headers = a[3];
                        self.set_attr("_use_datetime", use_datetime);
                        self.set_attr("_use_builtin_types", use_builtin_types);
                        self.set_attr("_connection", py_tuple({ none(), none() }));
                        self.set_attr("_headers", (B<"list">())(headers));
                        self.set_attr("_extra_headers", py_list());
                        return none();
                    }) },
                { "request", w.method("Transport", "request", { py_positional("host"), py_positional("handler"), py_positional("request_body"), py_positional("verbose", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py handler = a[2];
                        py request_body = a[3];
                        py verbose = a[4];
                        py e;
                        py i;
                        for (py const& item : iterate{ py_tuple({ I<0>(), I<1>() }) })
                        {
                            i = item;
                            try
                            {
                                return self.method("single_request", { host, handler, request_body, verbose });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler27;
                                if (matches(handler27.exception, Mod<"http.client">().attr("RemoteDisconnected")))
                                {
                                    if (i.truthy())
                                    {
                                        py_reraise(handler27.exception);
                                    }
                                }
                                else if (matches(handler27.exception, B<"OSError">()))
                                {
                                    e = handler27.exception;
                                    if ((i.truthy() || (!py_tuple({ Mod<"errno">().attr("ECONNRESET"), Mod<"errno">().attr("ECONNABORTED"), Mod<"errno">().attr("EPIPE") }).contains(e.attr("errno")))))
                                    {
                                        py_reraise(handler27.exception);
                                    }
                                }
                                else
                                    py_reraise(handler27.exception);
                            }
                        }
                        return none();
                    }) },
                { "single_request", w.method("Transport", "single_request", { py_positional("host"), py_positional("handler"), py_positional("request_body"), py_positional("verbose", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py handler = a[2];
                        py request_body = a[3];
                        py verbose = a[4];
                        py http_conn;
                        py resp;
                        try
                        {
                            http_conn = self.method("send_request", { host, handler, request_body, verbose });
                            resp = http_conn.method("getresponse");
                            if (resp.attr("status") == (I<200>()))
                            {
                                self.set_attr("verbose", verbose);
                                return self.method("parse_response", { resp });
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler28;
                            if (matches(handler28.exception, G<"xmlrpc.client.Fault">()))
                            {
                                py_reraise(handler28.exception);
                            }
                            else if (matches(handler28.exception, B<"Exception">()))
                            {
                                // All unexpected errors leave connection in
                                // a strange state, so we clear it.
                                self.method("close");
                                py_reraise(handler28.exception);
                            }
                            else
                                py_reraise(handler28.exception);
                        }
                        // We got an error response.
                        // Discard any response data and raise exception
                        if (resp.method("getheader", { S<"content-length">(), S<"">() }).truthy())
                        {
                            resp.method("read");
                        }
                        py_raise((G<"xmlrpc.client.ProtocolError">())(host + handler, resp.attr("status"), resp.attr("reason"), (B<"dict">())(resp.method("getheaders"))));
                    }) },
                { "getparser", w.method("Transport", "getparser", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"xmlrpc.client.getparser">()).call({  }, { { "use_datetime", self.attr("_use_datetime") }, { "use_builtin_types", self.attr("_use_builtin_types") } });
                    }) },
                { "get_host_info", w.method("Transport", "get_host_info", { py_positional("host") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py auth;
                        py extra_headers;
                        py x509;
                        x509 = py_dict();
                        if (isinstance(host, B<"tuple">()))
                        {
                            auto const items29 = unpack(host, 2);
                            host = items29[0];
                            x509 = items29[1];
                        }
                        auto const items30 = unpack((Mod<"urllib.parse">().attr("_splituser"))(host), 2);
                        auth = items30[0];
                        host = items30[1];
                        if (auth.truthy())
                        {
                            auth = (Mod<"urllib.parse">().attr("unquote_to_bytes"))(auth);
                            auth = (Mod<"base64">().attr("encodebytes"))(auth).method("decode", { S<"utf-8">() });
                            auth = (S<"">()).method("join", { auth.method("split") });
                            extra_headers = py_list({ py_tuple({ S<"Authorization">(), (S<"Basic ">()) + auth }) });
                        }
                        else
                        {
                            extra_headers = py_list();
                        }
                        return py_tuple({ host, extra_headers, x509 });
                    }) },
                { "make_connection", w.method("Transport", "make_connection", { py_positional("host") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py chost;
                        py x509;
                        if ((self.attr("_connection").truthy() && (host == self.attr("_connection")[I<0>()])))
                        {
                            return self.attr("_connection")[I<1>()];
                        }
                        // create a HTTP connection object from a host descriptor
                        auto const items31 = unpack(self.method("get_host_info", { host }), 3);
                        chost = items31[0];
                        self.set_attr("_extra_headers", items31[1]);
                        x509 = items31[2];
                        self.set_attr("_connection", py_tuple({ host, (Mod<"http.client">().attr("HTTPConnection"))(chost) }));
                        return self.attr("_connection")[I<1>()];
                    }) },
                { "close", w.method("Transport", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py connection;
                        py host;
                        auto const items32 = unpack(self.attr("_connection"), 2);
                        host = items32[0];
                        connection = items32[1];
                        if (connection.truthy())
                        {
                            self.set_attr("_connection", py_tuple({ none(), none() }));
                            connection.method("close");
                        }
                        return none();
                    }) },
                { "send_request", w.method("Transport", "send_request", { py_positional("host"), py_positional("handler"), py_positional("request_body"), py_positional("debug") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py handler = a[2];
                        py request_body = a[3];
                        py debug = a[4];
                        py connection;
                        py headers;
                        connection = self.method("make_connection", { host });
                        headers = self.attr("_headers") + self.attr("_extra_headers");
                        if (debug.truthy())
                        {
                            connection.method("set_debuglevel", { I<1>() });
                        }
                        if ((self.attr("accept_gzip_encoding").truthy() && (Mod<"gzip">()).truthy()))
                        {
                            connection.method("putrequest", { S<"POST">(), handler }, { { "skip_accept_encoding", boolean(true) } });
                            headers.method("append", { py_tuple({ S<"Accept-Encoding">(), S<"gzip">() }) });
                        }
                        else
                        {
                            connection.method("putrequest", { S<"POST">(), handler });
                        }
                        headers.method("append", { py_tuple({ S<"Content-Type">(), S<"text/xml">() }) });
                        headers.method("append", { py_tuple({ S<"User-Agent">(), self.attr("user_agent") }) });
                        self.method("send_headers", { connection, headers });
                        self.method("send_content", { connection, request_body });
                        return connection;
                    }) },
                { "send_headers", w.method("Transport", "send_headers", { py_positional("connection"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py connection = a[1];
                        py headers = a[2];
                        py key;
                        py val;
                        for (py const& item : iterate{ headers })
                        {
                            auto const items34 = unpack(item, 2);
                            key = items34[0];
                            val = items34[1];
                            connection.method("putheader", { key, val });
                        }
                        return none();
                    }) },
                { "send_content", w.method("Transport", "send_content", { py_positional("connection"), py_positional("request_body") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py connection = a[1];
                        py request_body = a[2];
                        if (((!self.attr("encode_threshold").is_none()) && (self.attr("encode_threshold") < py{ request_body.size() }) && (Mod<"gzip">()).truthy()))
                        {
                            connection.method("putheader", { S<"Content-Encoding">(), S<"gzip">() });
                            request_body = (G<"xmlrpc.client.gzip_encode">())(request_body);
                        }
                        connection.method("putheader", { S<"Content-Length">(), (B<"str">())(py{ request_body.size() }) });
                        connection.method("endheaders", { request_body });
                        return none();
                    }) },
                { "parse_response", w.method("Transport", "parse_response", { py_positional("response") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py response = a[1];
                        py data;
                        py p;
                        py stream;
                        py u;
                        if (has_attr(response, "getheader"))
                        {
                            if (response.method("getheader", { S<"Content-Encoding">(), S<"">() }) == (S<"gzip">()))
                            {
                                stream = (G<"xmlrpc.client.GzipDecodedResponse">())(response);
                            }
                            else
                            {
                                stream = response;
                            }
                        }
                        else
                        {
                            stream = response;
                        }
                        auto const items35 = unpack(self.method("getparser"), 2);
                        p = items35[0];
                        u = items35[1];
                        while (([&]() -> py { data = stream.method("read", { I<1024>() }); return data; }()).truthy())
                        {
                            if (self.attr("verbose").truthy())
                            {
                                (B<"print">())(S<"body:">(), S(data.repr()));
                            }
                            p.method("feed", { data });
                        }
                        if (!stream.is(response))
                        {
                            stream.method("close");
                        }
                        p.method("close");
                        return u.method("close");
                    }) }, }));

            // #
            // Standard transport class for XML-RPC over HTTPS.
                // FIXME: mostly untested
            define("xmlrpc.client", "SafeTransport", w.make_class("SafeTransport", { G<"xmlrpc.client.Transport">() }, {
                { "__init__", w.method("SafeTransport", "__init__", { py_positional("use_datetime", boolean(false)), py_positional("use_builtin_types", boolean(false)), py_keyword("headers", py_tuple()), py_keyword("context", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py use_datetime = a[1];
                        py use_builtin_types = a[2];
                        py headers = a[3];
                        py context = a[4];
                        (B<"super">()(G<"xmlrpc.client.SafeTransport">(), self)).method("__init__", {  }, { { "use_datetime", use_datetime }, { "use_builtin_types", use_builtin_types }, { "headers", headers } });
                        self.set_attr("context", context);
                        return none();
                    }) },
                { "make_connection", w.method("SafeTransport", "make_connection", { py_positional("host") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py chost;
                        py x509;
                        if ((self.attr("_connection").truthy() && (host == self.attr("_connection")[I<0>()])))
                        {
                            return self.attr("_connection")[I<1>()];
                        }
                        if (!has_attr(Mod<"http.client">(), "HTTPSConnection"))
                        {
                            py_raise((B<"NotImplementedError">())(S<"your version of http.client doesn't support HTTPS">()));
                        }
                        // create a HTTPS connection object from a host descriptor
                        // host may be a string, or a (host, x509-dict) tuple
                        auto const items37 = unpack(self.method("get_host_info", { host }), 3);
                        chost = items37[0];
                        self.set_attr("_extra_headers", items37[1]);
                        x509 = items37[2];
                        self.set_attr("_connection", py_tuple({ host, call_ex((Mod<"http.client">().attr("HTTPSConnection")), { chost, none() }, py{}, [&]() -> py { py const t = x509; return t.truthy() ? t : py_dict(); }(), { { "context", self.attr("context") } }) }));
                        return self.attr("_connection")[I<1>()];
                    }) }, }));

            // #
            // Standard server proxy.  This class establishes a virtual connection
            // to an XML-RPC server.
            // <p>
            // This class is available as ServerProxy and Server.  New code should
            // use ServerProxy, to avoid confusion.
            // 
            // @def ServerProxy(uri, **options)
            // @param uri The connection point on the server.
            // @keyparam transport A transport factory, compatible with the
            // standard transport class.
            // @keyparam encoding The default encoding used for 8-bit strings
            // (default is UTF-8).
            // @keyparam verbose Use a true value to enable debugging output.
            // (printed to standard output).
            // @see Transport
                // note: to call a remote object with a non-standard name, use
                // result getattr(server, "strange-python-name")(args)
            define("xmlrpc.client", "ServerProxy", w.make_class("ServerProxy", {  }, {
                { "__init__", w.method("ServerProxy", "__init__", { py_positional("uri"), py_positional("transport", none()), py_positional("encoding", none()), py_positional("verbose", boolean(false)), py_positional("allow_none", boolean(false)), py_positional("use_datetime", boolean(false)), py_positional("use_builtin_types", boolean(false)), py_keyword("headers", py_tuple()), py_keyword("context", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py uri = a[1];
                        py transport = a[2];
                        py encoding = a[3];
                        py verbose = a[4];
                        py allow_none = a[5];
                        py use_datetime = a[6];
                        py use_builtin_types = a[7];
                        py headers = a[8];
                        py context = a[9];
                        py extra_kwargs;
                        py handler;
                        py p;
                        p = (Mod<"urllib.parse">().attr("urlsplit"))(uri);
                        if (!py_tuple({ S<"http">(), S<"https">() }).contains(p.attr("scheme")))
                        {
                            py_raise((B<"OSError">())(S<"unsupported XML-RPC protocol">()));
                        }
                        self.set_attr("_ServerProxy__host", p.attr("netloc"));
                        self.set_attr("_ServerProxy__handler", (Mod<"urllib.parse">().attr("urlunsplit"))((((py_list() + py_list({ S<"">() })) + py_list({ S<"">() })) + B<"list">()(p[B<"slice">()(I<2>(), none(), none())]))));
                        if (!self.attr("_ServerProxy__handler").truthy())
                        {
                            self.set_attr("_ServerProxy__handler", S<"/RPC2">());
                        }
                        if (transport.is_none())
                        {
                            if (p.attr("scheme") == (S<"https">()))
                            {
                                handler = G<"xmlrpc.client.SafeTransport">();
                                extra_kwargs = dict_of({ { S<"context">(), context } });
                            }
                            else
                            {
                                handler = G<"xmlrpc.client.Transport">();
                                extra_kwargs = py_dict();
                            }
                            transport = call_ex(handler, {  }, py{}, extra_kwargs, { { "use_datetime", use_datetime }, { "use_builtin_types", use_builtin_types }, { "headers", headers } });
                        }
                        self.set_attr("_ServerProxy__transport", transport);
                        self.set_attr("_ServerProxy__encoding", [&]() -> py { py const t = encoding; return t.truthy() ? t : S<"utf-8">(); }());
                        self.set_attr("_ServerProxy__verbose", verbose);
                        self.set_attr("_ServerProxy__allow_none", allow_none);
                        return none();
                    }) },
                { "_ServerProxy__close", w.method("ServerProxy", "__close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_ServerProxy__transport").method("close");
                        return none();
                    }) },
                { "_ServerProxy__request", w.method("ServerProxy", "__request", { py_positional("methodname"), py_positional("params") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py methodname = a[1];
                        py params = a[2];
                        py request;
                        py response;
                        request = (G<"xmlrpc.client.dumps">()).call({ params, methodname }, { { "encoding", self.attr("_ServerProxy__encoding") }, { "allow_none", self.attr("_ServerProxy__allow_none") } }).method("encode", { self.attr("_ServerProxy__encoding"), S<"xmlcharrefreplace">() });
                        response = self.attr("_ServerProxy__transport").method("request", { self.attr("_ServerProxy__host"), self.attr("_ServerProxy__handler"), request }, { { "verbose", self.attr("_ServerProxy__verbose") } });
                        if (py{ response.size() } == (I<1>()))
                        {
                            response = response[I<0>()];
                        }
                        return response;
                    }) },
                { "__repr__", w.method("ServerProxy", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s for %s%s>">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.attr("_ServerProxy__host"), self.attr("_ServerProxy__handler") });
                    }) },
                { "__getattr__", w.method("ServerProxy", "__getattr__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return (G<"xmlrpc.client._Method">())(self.attr("_ServerProxy__request"), name);
                    }) },
                { "__call__", w.method("ServerProxy", "__call__", { py_positional("attr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attr = a[1];
                        if (attr == (S<"close">()))
                        {
                            return self.attr("_ServerProxy__close");
                        }
                        else if (attr == (S<"transport">()))
                        {
                            return self.attr("_ServerProxy__transport");
                        }
                        py_raise((B<"AttributeError">())((S<"Attribute %r not found">()) % py_tuple({ attr })));
                    }) },
                { "__enter__", w.method("ServerProxy", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("ServerProxy", "__exit__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        self.method("_ServerProxy__close");
                        return none();
                    }, true) }, }));

            // compatibility
            define("xmlrpc.client", "Server", G<"xmlrpc.client.ServerProxy">());
            // --------------------------------------------------------------------
            // test code
            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool xmlrpc_client_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xmlrpc_client();
                }
                if (auto const doc = xmlrpc_client_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("base64", py{ fn_import("base64") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("datetime", import_attribute("datetime", "datetime").release());
                aAttributes.emplace_back("Decimal", import_attribute("decimal", "Decimal").release());
                aAttributes.emplace_back("http", py{ fn_import("http") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                aAttributes.emplace_back("expat", import_attribute("xml.parsers", "expat").release());
                aAttributes.emplace_back("errno", py{ fn_import("errno") }.release());
                aAttributes.emplace_back("BytesIO", import_attribute("io", "BytesIO").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xmlrpc.client", xmlrpc_client_module);
    }
}
