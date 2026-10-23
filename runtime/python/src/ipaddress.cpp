/*
  ipaddress.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/ipaddress.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/ipaddress.py's copyright: Copyright 2007 Google Inc. Licensed to PSF
  under a Contributor Agreement.
*/

// The module ipaddress (CPython 3.13's Lib/ipaddress.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "ipaddress_docs.hpp"

namespace neos::python_runtime::detail::ipaddress_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "ipaddress.name"))
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
                py_raise("SystemError", std::string{ "ipaddress: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"ipaddress.name">()))
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
            static module_writer const sWriter{ "ipaddress", ipaddress_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- ipaddress (Lib/ipaddress.py)

        void make_ipaddress()
        {
            static module_writer const& w = writer("ipaddress");
            // Copyright 2007 Google Inc.
            // Licensed to PSF under a Contributor Agreement.
            define("ipaddress", "__version__", S<"1.0">());
            define("ipaddress", "IPV4LENGTH", I<32>());
            define("ipaddress", "IPV6LENGTH", I<128>());
            define("ipaddress", "AddressValueError", w.make_class("AddressValueError", { B<"ValueError">() }, {
 }));

            define("ipaddress", "NetmaskValueError", w.make_class("NetmaskValueError", { B<"ValueError">() }, {
 }));

            define("ipaddress", "ip_address", w.function("ip_address", "ip_address", { py_positional("address") }, [](arguments a) -> py
                {
                    py address = a[0];
                    try
                    {
                        return (G<"ipaddress.IPv4Address">())(address);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler1;
                        if (matches(handler1.exception, py_tuple({ G<"ipaddress.AddressValueError">(), G<"ipaddress.NetmaskValueError">() })))
                        {
                        }
                        else
                            py_reraise(handler1.exception);
                    }
                    try
                    {
                        return (G<"ipaddress.IPv6Address">())(address);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler2;
                        if (matches(handler2.exception, py_tuple({ G<"ipaddress.AddressValueError">(), G<"ipaddress.NetmaskValueError">() })))
                        {
                        }
                        else
                            py_reraise(handler2.exception);
                    }
                    py_raise((B<"ValueError">())(S(address.repr() + " does not appear to be an IPv4 or IPv6 address")));
                }));

            define("ipaddress", "ip_network", w.function("ip_network", "ip_network", { py_positional("address"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                {
                    py address = a[0];
                    py strict = a[1];
                    try
                    {
                        return (G<"ipaddress.IPv4Network">())(address, strict);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler3;
                        if (matches(handler3.exception, py_tuple({ G<"ipaddress.AddressValueError">(), G<"ipaddress.NetmaskValueError">() })))
                        {
                        }
                        else
                            py_reraise(handler3.exception);
                    }
                    try
                    {
                        return (G<"ipaddress.IPv6Network">())(address, strict);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler4;
                        if (matches(handler4.exception, py_tuple({ G<"ipaddress.AddressValueError">(), G<"ipaddress.NetmaskValueError">() })))
                        {
                        }
                        else
                            py_reraise(handler4.exception);
                    }
                    py_raise((B<"ValueError">())(S(address.repr() + " does not appear to be an IPv4 or IPv6 network")));
                }));

            define("ipaddress", "ip_interface", w.function("ip_interface", "ip_interface", { py_positional("address") }, [](arguments a) -> py
                {
                    py address = a[0];
                    try
                    {
                        return (G<"ipaddress.IPv4Interface">())(address);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler5;
                        if (matches(handler5.exception, py_tuple({ G<"ipaddress.AddressValueError">(), G<"ipaddress.NetmaskValueError">() })))
                        {
                        }
                        else
                            py_reraise(handler5.exception);
                    }
                    try
                    {
                        return (G<"ipaddress.IPv6Interface">())(address);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler6;
                        if (matches(handler6.exception, py_tuple({ G<"ipaddress.AddressValueError">(), G<"ipaddress.NetmaskValueError">() })))
                        {
                        }
                        else
                            py_reraise(handler6.exception);
                    }
                    py_raise((B<"ValueError">())(S(address.repr() + " does not appear to be an IPv4 or IPv6 interface")));
                }));

            define("ipaddress", "v4_int_to_packed", w.function("v4_int_to_packed", "v4_int_to_packed", { py_positional("address") }, [](arguments a) -> py
                {
                    py address = a[0];
                    try
                    {
                        return address.method("to_bytes", { I<4>() });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler7;
                        if (matches(handler7.exception, B<"OverflowError">()))
                        {
                            py_raise((B<"ValueError">())(S<"Address negative or too large for IPv4">()));
                        }
                        else
                            py_reraise(handler7.exception);
                    }
                    return none();
                }));

            define("ipaddress", "v6_int_to_packed", w.function("v6_int_to_packed", "v6_int_to_packed", { py_positional("address") }, [](arguments a) -> py
                {
                    py address = a[0];
                    try
                    {
                        return address.method("to_bytes", { I<16>() });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler8;
                        if (matches(handler8.exception, B<"OverflowError">()))
                        {
                            py_raise((B<"ValueError">())(S<"Address negative or too large for IPv6">()));
                        }
                        else
                            py_reraise(handler8.exception);
                    }
                    return none();
                }));

            define("ipaddress", "_split_optional_netmask", w.function("_split_optional_netmask", "_split_optional_netmask", { py_positional("address") }, [](arguments a) -> py
                {
                    py address = a[0];
                    py addr;
                    addr = (B<"str">())(address).method("split", { S<"/">() });
                    if (py{ addr.size() } > (I<2>()))
                    {
                        py_raise((G<"ipaddress.AddressValueError">())(S(std::string{ "Only one '/' permitted in " } + address.repr())));
                    }
                    return addr;
                }));

            define("ipaddress", "_find_address_range", w.function("_find_address_range", "_find_address_range", { py_positional("addresses") }, [](arguments a) -> py
                {
                    return make_native_generator("_find_address_range", "_find_address_range", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py addresses = a[0];
                        py first;
                        py ip;
                        py it;
                        py last;
                        it = (B<"iter">())(addresses);
                        py const value9 = (B<"next">())(it);
                        first = value9;
                        last = value9;
                        for (py const& item : iterate{ it })
                        {
                            ip = item;
                            if (ip.attr("_ip") != (last.attr("_ip") + (I<1>())))
                            {
                                py_yield(py_tuple({ first, last }));
                                first = ip;
                            }
                            last = ip;
                        }
                        py_yield(py_tuple({ first, last }));
                        return none();
                    });
                }));

            define("ipaddress", "_count_righthand_zero_bits", w.function("_count_righthand_zero_bits", "_count_righthand_zero_bits", { py_positional("number"), py_positional("bits") }, [](arguments a) -> py
                {
                    py number = a[0];
                    py bits = a[1];
                    if (number == (I<0>()))
                    {
                        return bits;
                    }
                    return (B<"min">())(bits, (py{ unary(NEOS_PY_INVERT, number.get()) } & (number - (I<1>()))).method("bit_length"));
                }));

            define("ipaddress", "summarize_address_range", w.function("summarize_address_range", "summarize_address_range", { py_positional("first"), py_positional("last") }, [](arguments a) -> py
                {
                    return make_native_generator("summarize_address_range", "summarize_address_range", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py first = a[0];
                        py last = a[1];
                        py first_int;
                        py ip;
                        py ip_bits;
                        py last_int;
                        py nbits;
                        py net;
                        if (!([&]() -> py { py const t = boolean(isinstance(first, G<"ipaddress._BaseAddress">())); return !t.truthy() ? t : boolean(isinstance(last, G<"ipaddress._BaseAddress">())); }()).truthy())
                        {
                            py_raise((B<"TypeError">())(S<"first and last must be IP addresses, not networks">()));
                        }
                        if (first.attr("version") != last.attr("version"))
                        {
                            py_raise((B<"TypeError">())((S<"%s and %s are not of the same version">()) % py_tuple({ first, last })));
                        }
                        if (first > last)
                        {
                            py_raise((B<"ValueError">())(S<"last IP address must be greater than first">()));
                        }
                        if (first.attr("version") == (I<4>()))
                        {
                            ip = G<"ipaddress.IPv4Network">();
                        }
                        else if (first.attr("version") == (I<6>()))
                        {
                            ip = G<"ipaddress.IPv6Network">();
                        }
                        else
                        {
                            py_raise((B<"ValueError">())(S<"unknown IP version">()));
                        }
                        ip_bits = first.attr("_max_prefixlen");
                        first_int = first.attr("_ip");
                        last_int = last.attr("_ip");
                        while (first_int <= last_int)
                        {
                            nbits = (B<"min">())((G<"ipaddress._count_righthand_zero_bits">())(first_int, ip_bits), ((last_int - first_int) + (I<1>())).method("bit_length") - (I<1>()));
                            net = ip(py_tuple({ first_int, ip_bits - nbits }));
                            py_yield(net);
                            first_int = inplace(NEOS_PY_ADD, first_int, (I<1>()) << nbits);
                            if ((first_int - (I<1>())) == ip.attr("_ALL_ONES"))
                            {
                                break;
                            }
                        }
                        return none();
                    });
                }));

            define("ipaddress", "_collapse_addresses_internal", w.function("_collapse_addresses_internal", "_collapse_addresses_internal", { py_positional("addresses") }, [](arguments a) -> py
                {
                    return make_native_generator("_collapse_addresses_internal", "_collapse_addresses_internal", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py addresses = a[0];
                        py existing;
                        py last;
                        py net;
                        py subnets;
                        py supernet;
                        py to_merge;
                        // First merge
                        to_merge = (B<"list">())(addresses);
                        subnets = py_dict();
                        while (to_merge.truthy())
                        {
                            net = to_merge.method("pop");
                            supernet = net.method("supernet");
                            existing = subnets.method("get", { supernet });
                            if (existing.is_none())
                            {
                                subnets.set_item(supernet, net);
                            }
                            else if (existing != net)
                            {
                                // Merge consecutive subnets
                                del_item(subnets, supernet);
                                to_merge.method("append", { supernet });
                            }
                        }
                        // Then iterate over resulting networks, skipping subsumed subnets
                        last = none();
                        for (py const& item : iterate{ (B<"sorted">())(subnets.method("values")) })
                        {
                            net = item;
                            if (!last.is_none())
                            {
                                // Since they are sorted, last.network_address <= net.network_address
                                // is a given.
                                if (last.attr("broadcast_address") >= net.attr("broadcast_address"))
                                {
                                    continue;
                                }
                            }
                            py_yield(net);
                            last = net;
                        }
                        return none();
                    });
                }));

            define("ipaddress", "collapse_addresses", w.function("collapse_addresses", "collapse_addresses", { py_positional("addresses") }, [](arguments a) -> py
                {
                    py addresses = a[0];
                    py addrs;
                    py first;
                    py ip;
                    py ips;
                    py last;
                    py nets;
                    addrs = py_list();
                    ips = py_list();
                    nets = py_list();
                    // split IP addresses and networks
                    for (py const& item : iterate{ addresses })
                    {
                        ip = item;
                        if (isinstance(ip, G<"ipaddress._BaseAddress">()))
                        {
                            if ((ips.truthy() && (ips[I<-1>()].attr("_version") != ip.attr("_version"))))
                            {
                                py_raise((B<"TypeError">())((S<"%s and %s are not of the same version">()) % py_tuple({ ip, ips[I<-1>()] })));
                            }
                            ips.method("append", { ip });
                        }
                        else if (ip.attr("_prefixlen") == ip.attr("_max_prefixlen"))
                        {
                            if ((ips.truthy() && (ips[I<-1>()].attr("_version") != ip.attr("_version"))))
                            {
                                py_raise((B<"TypeError">())((S<"%s and %s are not of the same version">()) % py_tuple({ ip, ips[I<-1>()] })));
                            }
                            try
                            {
                                ips.method("append", { ip.attr("ip") });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler15;
                                if (matches(handler15.exception, B<"AttributeError">()))
                                {
                                    ips.method("append", { ip.attr("network_address") });
                                }
                                else
                                    py_reraise(handler15.exception);
                            }
                        }
                        else
                        {
                            if ((nets.truthy() && (nets[I<-1>()].attr("_version") != ip.attr("_version"))))
                            {
                                py_raise((B<"TypeError">())((S<"%s and %s are not of the same version">()) % py_tuple({ ip, nets[I<-1>()] })));
                            }
                            nets.method("append", { ip });
                        }
                    }
                    // sort and dedup
                    ips = (B<"sorted">())((B<"set">())(ips));
                    // find consecutive address ranges in the sorted sequence and summarize them
                    if (ips.truthy())
                    {
                        for (py const& item : iterate{ (G<"ipaddress._find_address_range">())(ips) })
                        {
                            auto const items17 = unpack(item, 2);
                            first = items17[0];
                            last = items17[1];
                            addrs.method("extend", { (G<"ipaddress.summarize_address_range">())(first, last) });
                        }
                    }
                    return (G<"ipaddress._collapse_addresses_internal">())(addrs + nets);
                }));

            define("ipaddress", "get_mixed_type_key", w.function("get_mixed_type_key", "get_mixed_type_key", { py_positional("obj") }, [](arguments a) -> py
                {
                    py obj = a[0];
                    if (isinstance(obj, G<"ipaddress._BaseNetwork">()))
                    {
                        return obj.method("_get_networks_key");
                    }
                    else if (isinstance(obj, G<"ipaddress._BaseAddress">()))
                    {
                        return obj.method("_get_address_key");
                    }
                    return py::borrowed(not_implemented());
                }));

            define("ipaddress", "_IPAddressBase", w.make_class("_IPAddressBase", {  }, {
                { "__slots__", py_tuple() },
                { "exploded", B<"property">()(w.method("_IPAddressBase", "exploded", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_explode_shorthand_ip_string");
                    })) },
                { "compressed", B<"property">()(w.method("_IPAddressBase", "compressed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"str">())(self);
                    })) },
                { "reverse_pointer", B<"property">()(w.method("_IPAddressBase", "reverse_pointer", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_reverse_pointer");
                    })) },
                { "version", B<"property">()(w.method("_IPAddressBase", "version", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg;
                        msg = (S<"%200s has no version specified">()) % py_tuple({ (B<"type">())(self) });
                        py_raise((B<"NotImplementedError">())(msg));
                    })) },
                { "_check_int_address", w.method("_IPAddressBase", "_check_int_address", { py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py address = a[1];
                        py msg;
                        if (address < (I<0>()))
                        {
                            msg = S<"%d (< 0) is not permitted as an IPv%d address">();
                            py_raise((G<"ipaddress.AddressValueError">())(msg % py_tuple({ address, self.attr("_version") })));
                        }
                        if (address > self.attr("_ALL_ONES"))
                        {
                            msg = S<"%d (>= 2**%d) is not permitted as an IPv%d address">();
                            py_raise((G<"ipaddress.AddressValueError">())(msg % py_tuple({ address, self.attr("_max_prefixlen"), self.attr("_version") })));
                        }
                        return none();
                    }) },
                { "_check_packed_address", w.method("_IPAddressBase", "_check_packed_address", { py_positional("address"), py_positional("expected_len") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py address = a[1];
                        py expected_len = a[2];
                        py address_len;
                        py msg;
                        address_len = py{ address.size() };
                        if (address_len != expected_len)
                        {
                            msg = S<"%r (len %d != %d) is not permitted as an IPv%d address">();
                            py_raise((G<"ipaddress.AddressValueError">())(msg % py_tuple({ address, address_len, expected_len, self.attr("_version") })));
                        }
                        return none();
                    }) },
                { "_ip_int_from_prefix", w.class_method("_IPAddressBase", "_ip_int_from_prefix", { py_positional("prefixlen") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py prefixlen = a[1];
                        return cls.attr("_ALL_ONES") ^ (cls.attr("_ALL_ONES") >> prefixlen);
                    }) },
                { "_prefix_from_ip_int", w.class_method("_IPAddressBase", "_prefix_from_ip_int", { py_positional("ip_int") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py ip_int = a[1];
                        py all_ones;
                        py byteslen;
                        py details;
                        py leading_ones;
                        py msg;
                        py prefixlen;
                        py trailing_zeroes;
                        trailing_zeroes = (G<"ipaddress._count_righthand_zero_bits">())(ip_int, cls.attr("_max_prefixlen"));
                        prefixlen = cls.attr("_max_prefixlen") - trailing_zeroes;
                        leading_ones = ip_int >> trailing_zeroes;
                        all_ones = ((I<1>()) << prefixlen) - (I<1>());
                        if (leading_ones != all_ones)
                        {
                            byteslen = floordiv(cls.attr("_max_prefixlen"), (I<8>()));
                            details = ip_int.method("to_bytes", { byteslen, S<"big">() });
                            msg = S<"Netmask pattern %r mixes zeroes & ones">();
                            py_raise((B<"ValueError">())(msg % details));
                        }
                        return prefixlen;
                    }) },
                { "_report_invalid_netmask", w.class_method("_IPAddressBase", "_report_invalid_netmask", { py_positional("netmask_str") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py netmask_str = a[1];
                        py msg;
                        msg = (S<"%r is not a valid netmask">()) % netmask_str;
                        fn_raise_from((G<"ipaddress.NetmaskValueError">())(msg).get(), none().get());
                        throw raised{};
                    }) },
                { "_prefix_from_prefix_string", w.class_method("_IPAddressBase", "_prefix_from_prefix_string", { py_positional("prefixlen_str") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py prefixlen_str = a[1];
                        py prefixlen;
                        // int allows a leading +/- as well as surrounding whitespace,
                        // so we ensure that isn't the case
                        if (!([&]() -> py { py const t = prefixlen_str.method("isascii"); return !t.truthy() ? t : prefixlen_str.method("isdigit"); }()).truthy())
                        {
                            cls.method("_report_invalid_netmask", { prefixlen_str });
                        }
                        try
                        {
                            prefixlen = (B<"int">())(prefixlen_str);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler18;
                            if (matches(handler18.exception, B<"ValueError">()))
                            {
                                cls.method("_report_invalid_netmask", { prefixlen_str });
                            }
                            else
                                py_reraise(handler18.exception);
                        }
                        if (!(((I<0>()) <= prefixlen) && (prefixlen <= cls.attr("_max_prefixlen"))))
                        {
                            cls.method("_report_invalid_netmask", { prefixlen_str });
                        }
                        return prefixlen;
                    }) },
                { "_prefix_from_ip_string", w.class_method("_IPAddressBase", "_prefix_from_ip_string", { py_positional("ip_str") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py ip_str = a[1];
                        py ip_int;
                        // Parse the netmask/hostmask like an IP address.
                        try
                        {
                            ip_int = cls.method("_ip_int_from_string", { ip_str });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler19;
                            if (matches(handler19.exception, G<"ipaddress.AddressValueError">()))
                            {
                                cls.method("_report_invalid_netmask", { ip_str });
                            }
                            else
                                py_reraise(handler19.exception);
                        }
                        // Try matching a netmask (this would be /1*0*/ as a bitwise regexp).
                        // Note that the two ambiguous cases (all-ones and all-zeroes) are
                        // treated as netmasks.
                        try
                        {
                            return cls.method("_prefix_from_ip_int", { ip_int });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler20;
                            if (matches(handler20.exception, B<"ValueError">()))
                            {
                            }
                            else
                                py_reraise(handler20.exception);
                        }
                        // Invert the bits, and try matching a /0+1+/ hostmask instead.
                        ip_int = inplace(NEOS_PY_XOR, ip_int, cls.attr("_ALL_ONES"));
                        try
                        {
                            return cls.method("_prefix_from_ip_int", { ip_int });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler21;
                            if (matches(handler21.exception, B<"ValueError">()))
                            {
                                cls.method("_report_invalid_netmask", { ip_str });
                            }
                            else
                                py_reraise(handler21.exception);
                        }
                        return none();
                    }) },
                { "_split_addr_prefix", w.class_method("_IPAddressBase", "_split_addr_prefix", { py_positional("address") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py address = a[1];
                        // a packed address or integer
                        if (isinstance(address, py_tuple({ B<"bytes">(), B<"int">() })))
                        {
                            return py_tuple({ address, cls.attr("_max_prefixlen") });
                        }
                        if (!isinstance(address, B<"tuple">()))
                        {
                            // Assume input argument to be string or any object representation
                            // which converts into a formatted IP prefix string.
                            address = (G<"ipaddress._split_optional_netmask">())(address);
                        }
                        // Constructing from a tuple (addr, [mask])
                        if (py{ address.size() } > (I<1>()))
                        {
                            return address;
                        }
                        return py_tuple({ address[I<0>()], cls.attr("_max_prefixlen") });
                    }) },
                { "__reduce__", w.method("_IPAddressBase", "__reduce__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ self.attr("__class__"), py_tuple({ (B<"str">())(self) }) });
                    }) }, }));

            define("ipaddress", "_address_fmt_re", none());
                // Shorthand for Integer addition and subtraction. This is not
                // meant to ever support addition/subtraction of addresses.
            define("ipaddress", "_BaseAddress", (Mod<"functools">().attr("total_ordering"))(w.make_class("_BaseAddress", { G<"ipaddress._IPAddressBase">() }, {
                { "__slots__", py_tuple() },
                { "__int__", w.method("_BaseAddress", "__int__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_ip");
                    }) },
                { "__eq__", w.method("_BaseAddress", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        try
                        {
                            return [&]() -> py { py const t = boolean(self.attr("_ip") == other.attr("_ip")); return !t.truthy() ? t : boolean(self.attr("_version") == other.attr("_version")); }();
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler22;
                            if (matches(handler22.exception, B<"AttributeError">()))
                            {
                                return py::borrowed(not_implemented());
                            }
                            else
                                py_reraise(handler22.exception);
                        }
                        return none();
                    }) },
                { "__lt__", w.method("_BaseAddress", "__lt__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (!isinstance(other, G<"ipaddress._BaseAddress">()))
                        {
                            return py::borrowed(not_implemented());
                        }
                        if (self.attr("_version") != other.attr("_version"))
                        {
                            py_raise((B<"TypeError">())((S<"%s and %s are not of the same version">()) % py_tuple({ self, other })));
                        }
                        if (self.attr("_ip") != other.attr("_ip"))
                        {
                            return boolean(self.attr("_ip") < other.attr("_ip"));
                        }
                        return boolean(false);
                    }) },
                { "__add__", w.method("_BaseAddress", "__add__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (!isinstance(other, B<"int">()))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return self.method("__class__", { (B<"int">())(self) + other });
                    }) },
                { "__sub__", w.method("_BaseAddress", "__sub__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (!isinstance(other, B<"int">()))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return self.method("__class__", { (B<"int">())(self) - other });
                    }) },
                { "__repr__", w.method("_BaseAddress", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s(%r)">()) % py_tuple({ self.attr("__class__").attr("__name__"), (B<"str">())(self) });
                    }) },
                { "__str__", w.method("_BaseAddress", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"str">())(self.method("_string_from_ip_int", { self.attr("_ip") }));
                    }) },
                { "__hash__", w.method("_BaseAddress", "__hash__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"hash">())((B<"hex">())((B<"int">())(self.attr("_ip"))));
                    }) },
                { "_get_address_key", w.method("_BaseAddress", "_get_address_key", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ self.attr("_version"), self });
                    }) },
                { "__reduce__", w.method("_BaseAddress", "__reduce__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ self.attr("__class__"), py_tuple({ self.attr("_ip") }) });
                    }) },
                { "__format__", w.method("_BaseAddress", "__format__", { py_positional("fmt") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fmt = a[1];
                        py alternate;
                        py fmt_base;
                        py grouping;
                        py m;
                        py padlen;
                        py re;
                        // Support string formatting
                        if (((!fmt.truthy()) || (fmt[I<-1>()] == (S<"s">()))))
                        {
                            return (B<"format">())((B<"str">())(self), fmt);
                        }
                        // From here on down, support for 'bnXx'
                        if ((G<"ipaddress._address_fmt_re">()).is_none())
                        {
                            re = module_of("re");
                            set_global("ipaddress", "_address_fmt_re", re.method("compile", { S<"(#?)(_?)([xbnX])">() }));
                        }
                        m = (G<"ipaddress._address_fmt_re">()).method("fullmatch", { fmt });
                        if (!m.truthy())
                        {
                            return (B<"super">()(G<"ipaddress._BaseAddress">(), self)).method("__format__", { fmt });
                        }
                        auto const items23 = unpack(m.method("groups"), 3);
                        alternate = items23[0];
                        grouping = items23[1];
                        fmt_base = items23[2];
                        // Set some defaults
                        if (fmt_base == (S<"n">()))
                        {
                            if (self.attr("_version") == (I<4>()))
                            {
                                fmt_base = S<"b">();
                            }
                            else
                            {
                                fmt_base = S<"x">();
                            }
                        }
                        if (fmt_base == (S<"b">()))
                        {
                            padlen = self.attr("_max_prefixlen");
                        }
                        else
                        {
                            padlen = floordiv(self.attr("_max_prefixlen"), (I<4>()));
                        }
                        if (grouping.truthy())
                        {
                            padlen = inplace(NEOS_PY_ADD, padlen, floordiv(padlen, (I<4>())) - (I<1>()));
                        }
                        if (alternate.truthy())
                        {
                            padlen = inplace(NEOS_PY_ADD, padlen, I<2>());
                        }
                        return (B<"format">())((B<"int">())(self), S(alternate.str() + "0" + padlen.str() + grouping.str() + fmt_base.str()));
                    }) }, })));

            define("ipaddress", "_BaseNetwork", (Mod<"functools">().attr("total_ordering"))(w.make_class("_BaseNetwork", { G<"ipaddress._IPAddressBase">() }, {
                { "__repr__", w.method("_BaseNetwork", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s(%r)">()) % py_tuple({ self.attr("__class__").attr("__name__"), (B<"str">())(self) });
                    }) },
                { "__str__", w.method("_BaseNetwork", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s/%d">()) % py_tuple({ self.attr("network_address"), self.attr("prefixlen") });
                    }) },
                { "hosts", w.method("_BaseNetwork", "hosts", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("hosts", "_BaseNetwork.hosts", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py broadcast;
                            py network;
                            py x;
                            network = (B<"int">())(self.attr("network_address"));
                            broadcast = (B<"int">())(self.attr("broadcast_address"));
                            for (py const& item : iterate{ (B<"range">())(network + (I<1>()), broadcast) })
                            {
                                x = item;
                                py_yield(self.method("_address_class", { x }));
                            }
                            return none();
                        });
                    }) },
                { "__iter__", w.method("_BaseNetwork", "__iter__", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("__iter__", "_BaseNetwork.__iter__", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py broadcast;
                            py network;
                            py x;
                            network = (B<"int">())(self.attr("network_address"));
                            broadcast = (B<"int">())(self.attr("broadcast_address"));
                            for (py const& item : iterate{ (B<"range">())(network, broadcast + (I<1>())) })
                            {
                                x = item;
                                py_yield(self.method("_address_class", { x }));
                            }
                            return none();
                        });
                    }) },
                { "__getitem__", w.method("_BaseNetwork", "__getitem__", { py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py broadcast;
                        py network;
                        network = (B<"int">())(self.attr("network_address"));
                        broadcast = (B<"int">())(self.attr("broadcast_address"));
                        if (n >= (I<0>()))
                        {
                            if ((network + n) > broadcast)
                            {
                                py_raise((B<"IndexError">())(S<"address out of range">()));
                            }
                            return self.method("_address_class", { network + n });
                        }
                        else
                        {
                            n = inplace(NEOS_PY_ADD, n, I<1>());
                            if ((broadcast + n) < network)
                            {
                                py_raise((B<"IndexError">())(S<"address out of range">()));
                            }
                            return self.method("_address_class", { broadcast + n });
                        }
                        return none();
                    }) },
                { "__lt__", w.method("_BaseNetwork", "__lt__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (!isinstance(other, G<"ipaddress._BaseNetwork">()))
                        {
                            return py::borrowed(not_implemented());
                        }
                        if (self.attr("_version") != other.attr("_version"))
                        {
                            py_raise((B<"TypeError">())((S<"%s and %s are not of the same version">()) % py_tuple({ self, other })));
                        }
                        if (self.attr("network_address") != other.attr("network_address"))
                        {
                            return boolean(self.attr("network_address") < other.attr("network_address"));
                        }
                        if (self.attr("netmask") != other.attr("netmask"))
                        {
                            return boolean(self.attr("netmask") < other.attr("netmask"));
                        }
                        return boolean(false);
                    }) },
                { "__eq__", w.method("_BaseNetwork", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        try
                        {
                            return [&]() -> py { py const t = boolean(self.attr("_version") == other.attr("_version")); return !t.truthy() ? t : [&]() -> py { py const t = boolean(self.attr("network_address") == other.attr("network_address")); return !t.truthy() ? t : boolean((B<"int">())(self.attr("netmask")) == (B<"int">())(other.attr("netmask"))); }(); }();
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler26;
                            if (matches(handler26.exception, B<"AttributeError">()))
                            {
                                return py::borrowed(not_implemented());
                            }
                            else
                                py_reraise(handler26.exception);
                        }
                        return none();
                    }) },
                { "__hash__", w.method("_BaseNetwork", "__hash__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"hash">())(py_tuple({ (B<"int">())(self.attr("network_address")), (B<"int">())(self.attr("netmask")) }));
                    }) },
                { "__contains__", w.method("_BaseNetwork", "__contains__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (self.attr("_version") != other.attr("_version"))
                        {
                            return boolean(false);
                        }
                        // dealing with another network.
                        if (isinstance(other, G<"ipaddress._BaseNetwork">()))
                        {
                            return boolean(false);
                        }
                        else
                        {
                            // dealing with another address
                            // address
                            return boolean((other.attr("_ip") & self.attr("netmask").attr("_ip")) == self.attr("network_address").attr("_ip"));
                        }
                        return none();
                    }) },
                { "overlaps", w.method("_BaseNetwork", "overlaps", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return [&]() -> py { py const t = boolean(other.contains(self.attr("network_address"))); return t.truthy() ? t : [&]() -> py { py const t = boolean(other.contains(self.attr("broadcast_address"))); return t.truthy() ? t : [&]() -> py { py const t = boolean(self.contains(other.attr("network_address"))); return t.truthy() ? t : boolean(self.contains(other.attr("broadcast_address"))); }(); }(); }();
                    }) },
                { "broadcast_address", (Mod<"functools">().attr("cached_property"))(w.method("_BaseNetwork", "broadcast_address", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_address_class", { (B<"int">())(self.attr("network_address")) | (B<"int">())(self.attr("hostmask")) });
                    })) },
                { "hostmask", (Mod<"functools">().attr("cached_property"))(w.method("_BaseNetwork", "hostmask", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_address_class", { (B<"int">())(self.attr("netmask")) ^ self.attr("_ALL_ONES") });
                    })) },
                { "with_prefixlen", B<"property">()(w.method("_BaseNetwork", "with_prefixlen", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s/%d">()) % py_tuple({ self.attr("network_address"), self.attr("_prefixlen") });
                    })) },
                { "with_netmask", B<"property">()(w.method("_BaseNetwork", "with_netmask", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s/%s">()) % py_tuple({ self.attr("network_address"), self.attr("netmask") });
                    })) },
                { "with_hostmask", B<"property">()(w.method("_BaseNetwork", "with_hostmask", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s/%s">()) % py_tuple({ self.attr("network_address"), self.attr("hostmask") });
                    })) },
                { "num_addresses", B<"property">()(w.method("_BaseNetwork", "num_addresses", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return ((B<"int">())(self.attr("broadcast_address")) - (B<"int">())(self.attr("network_address"))) + (I<1>());
                    })) },
                { "_address_class", B<"property">()(w.method("_BaseNetwork", "_address_class", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg;
                        msg = (S<"%200s has no associated address class">()) % py_tuple({ (B<"type">())(self) });
                        py_raise((B<"NotImplementedError">())(msg));
                    })) },
                { "prefixlen", B<"property">()(w.method("_BaseNetwork", "prefixlen", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_prefixlen");
                    })) },
                { "address_exclude", w.method("_BaseNetwork", "address_exclude", { py_positional("other") }, [](arguments a) -> py
                    {
                        return make_native_generator("address_exclude", "_BaseNetwork.address_exclude", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py other = a[1];
                            py s1;
                            py s2;
                            if (!(self.attr("_version") == other.attr("_version")))
                            {
                                py_raise((B<"TypeError">())((S<"%s and %s are not of the same version">()) % py_tuple({ self, other })));
                            }
                            if (!isinstance(other, G<"ipaddress._BaseNetwork">()))
                            {
                                py_raise((B<"TypeError">())((S<"%s is not a network object">()) % other));
                            }
                            if (!other.method("subnet_of", { self }).truthy())
                            {
                                py_raise((B<"ValueError">())((S<"%s not contained in %s">()) % py_tuple({ other, self })));
                            }
                            if (other == self)
                            {
                                return none();
                            }
                            // Make sure we're comparing the network of other.
                            other = other.method("__class__", { (S<"%s/%s">()) % py_tuple({ other.attr("network_address"), other.attr("prefixlen") }) });
                            auto const items27 = unpack(self.method("subnets"), 2);
                            s1 = items27[0];
                            s2 = items27[1];
                            while (((s1 != other) && (s2 != other)))
                            {
                                if (other.method("subnet_of", { s1 }).truthy())
                                {
                                    py_yield(s2);
                                    auto const items29 = unpack(s1.method("subnets"), 2);
                                    s1 = items29[0];
                                    s2 = items29[1];
                                }
                                else if (other.method("subnet_of", { s2 }).truthy())
                                {
                                    py_yield(s1);
                                    auto const items30 = unpack(s2.method("subnets"), 2);
                                    s1 = items30[0];
                                    s2 = items30[1];
                                }
                                else
                                {
                                    // If we got here, there's a bug somewhere.
                                    py_raise((B<"AssertionError">())((S<"Error performing exclusion: s1: %s s2: %s other: %s">()) % py_tuple({ s1, s2, other })));
                                }
                            }
                            if (s1 == other)
                            {
                                py_yield(s2);
                            }
                            else if (s2 == other)
                            {
                                py_yield(s1);
                            }
                            else
                            {
                                // If we got here, there's a bug somewhere.
                                py_raise((B<"AssertionError">())((S<"Error performing exclusion: s1: %s s2: %s other: %s">()) % py_tuple({ s1, s2, other })));
                            }
                            return none();
                        });
                    }) },
                { "compare_networks", w.method("_BaseNetwork", "compare_networks", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        // does this need to raise a ValueError?
                        if (self.attr("_version") != other.attr("_version"))
                        {
                            py_raise((B<"TypeError">())((S<"%s and %s are not of the same type">()) % py_tuple({ self, other })));
                        }
                        // self._version == other._version below here:
                        if (self.attr("network_address") < other.attr("network_address"))
                        {
                            return I<-1>();
                        }
                        if (self.attr("network_address") > other.attr("network_address"))
                        {
                            return I<1>();
                        }
                        // self.network_address == other.network_address below here:
                        if (self.attr("netmask") < other.attr("netmask"))
                        {
                            return I<-1>();
                        }
                        if (self.attr("netmask") > other.attr("netmask"))
                        {
                            return I<1>();
                        }
                        return I<0>();
                    }) },
                { "_get_networks_key", w.method("_BaseNetwork", "_get_networks_key", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ self.attr("_version"), self.attr("network_address"), self.attr("netmask") });
                    }) },
                { "subnets", w.method("_BaseNetwork", "subnets", { py_positional("prefixlen_diff", I<1>()), py_positional("new_prefix", none()) }, [](arguments a) -> py
                    {
                        return make_native_generator("subnets", "_BaseNetwork.subnets", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py prefixlen_diff = a[1];
                            py new_prefix = a[2];
                            py current;
                            py end;
                            py new_addr;
                            py new_prefixlen;
                            py start;
                            py step;
                            if (self.attr("_prefixlen") == self.attr("_max_prefixlen"))
                            {
                                py_yield(self);
                                return none();
                            }
                            if (!new_prefix.is_none())
                            {
                                if (new_prefix < self.attr("_prefixlen"))
                                {
                                    py_raise((B<"ValueError">())(S<"new prefix must be longer">()));
                                }
                                if (prefixlen_diff != (I<1>()))
                                {
                                    py_raise((B<"ValueError">())(S<"cannot set prefixlen_diff and new_prefix">()));
                                }
                                prefixlen_diff = new_prefix - self.attr("_prefixlen");
                            }
                            if (prefixlen_diff < (I<0>()))
                            {
                                py_raise((B<"ValueError">())(S<"prefix length diff must be > 0">()));
                            }
                            new_prefixlen = self.attr("_prefixlen") + prefixlen_diff;
                            if (new_prefixlen > self.attr("_max_prefixlen"))
                            {
                                py_raise((B<"ValueError">())((S<"prefix length diff %d is invalid for netblock %s">()) % py_tuple({ new_prefixlen, self })));
                            }
                            start = (B<"int">())(self.attr("network_address"));
                            end = (B<"int">())(self.attr("broadcast_address")) + (I<1>());
                            step = ((B<"int">())(self.attr("hostmask")) + (I<1>())) >> prefixlen_diff;
                            for (py const& item : iterate{ (B<"range">())(start, end, step) })
                            {
                                new_addr = item;
                                current = self.method("__class__", { py_tuple({ new_addr, new_prefixlen }) });
                                py_yield(current);
                            }
                            return none();
                        });
                    }) },
                { "supernet", w.method("_BaseNetwork", "supernet", { py_positional("prefixlen_diff", I<1>()), py_positional("new_prefix", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefixlen_diff = a[1];
                        py new_prefix = a[2];
                        py new_prefixlen;
                        if (self.attr("_prefixlen") == (I<0>()))
                        {
                            return self;
                        }
                        if (!new_prefix.is_none())
                        {
                            if (new_prefix > self.attr("_prefixlen"))
                            {
                                py_raise((B<"ValueError">())(S<"new prefix must be shorter">()));
                            }
                            if (prefixlen_diff != (I<1>()))
                            {
                                py_raise((B<"ValueError">())(S<"cannot set prefixlen_diff and new_prefix">()));
                            }
                            prefixlen_diff = self.attr("_prefixlen") - new_prefix;
                        }
                        new_prefixlen = self.attr("prefixlen") - prefixlen_diff;
                        if (new_prefixlen < (I<0>()))
                        {
                            py_raise((B<"ValueError">())((S<"current prefixlen is %d, cannot have a prefixlen_diff of %d">()) % py_tuple({ self.attr("prefixlen"), prefixlen_diff })));
                        }
                        return self.method("__class__", { py_tuple({ (B<"int">())(self.attr("network_address")) & ((B<"int">())(self.attr("netmask")) << prefixlen_diff), new_prefixlen }) });
                    }) },
                { "is_multicast", B<"property">()(w.method("_BaseNetwork", "is_multicast", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = self.attr("network_address").attr("is_multicast"); return !t.truthy() ? t : self.attr("broadcast_address").attr("is_multicast"); }();
                    })) },
                { "_is_subnet_of", w.static_method("_BaseNetwork", "_is_subnet_of", { py_positional("a"), py_positional("b") }, [](arguments a) -> py
                    {
                        py a_ = a[0];
                        py b = a[1];
                        try
                        {
                            // Always false if one is v4 and the other is v6.
                            if (a_.attr("_version") != b.attr("_version"))
                            {
                                py_raise((B<"TypeError">())(S(a_.str() + " and " + b.str() + " are not of the same version")));
                            }
                            return [&]() -> py { py const t = boolean(b.attr("network_address") <= a_.attr("network_address")); return !t.truthy() ? t : boolean(b.attr("broadcast_address") >= a_.attr("broadcast_address")); }();
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler32;
                            if (matches(handler32.exception, B<"AttributeError">()))
                            {
                                py_raise((B<"TypeError">())(S(std::string{ "Unable to test subnet containment between " } + a_.str() + " and " + b.str())));
                            }
                            else
                                py_reraise(handler32.exception);
                        }
                        return none();
                    }) },
                { "subnet_of", w.method("_BaseNetwork", "subnet_of", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return self.method("_is_subnet_of", { self, other });
                    }) },
                { "supernet_of", w.method("_BaseNetwork", "supernet_of", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return self.method("_is_subnet_of", { other, self });
                    }) },
                { "is_reserved", B<"property">()(w.method("_BaseNetwork", "is_reserved", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = self.attr("network_address").attr("is_reserved"); return !t.truthy() ? t : self.attr("broadcast_address").attr("is_reserved"); }();
                    })) },
                { "is_link_local", B<"property">()(w.method("_BaseNetwork", "is_link_local", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = self.attr("network_address").attr("is_link_local"); return !t.truthy() ? t : self.attr("broadcast_address").attr("is_link_local"); }();
                    })) },
                { "is_private", B<"property">()(w.method("_BaseNetwork", "is_private", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = (B<"any">())([&]() -> py { py const comprehension33 = py_list(); py priv_network; for (py const& item : iterate{ self.attr("_constants").attr("_private_networks") }) { priv_network = item; comprehension33.method("append", { [&]() -> py { py const t = boolean(priv_network.contains(self.attr("network_address"))); return !t.truthy() ? t : boolean(priv_network.contains(self.attr("broadcast_address"))); }() }); } return comprehension33; }()); return !t.truthy() ? t : (B<"all">())([&]() -> py { py const comprehension34 = py_list(); py network; for (py const& item : iterate{ self.attr("_constants").attr("_private_networks_exceptions") }) { network = item; comprehension34.method("append", { [&]() -> py { py const t = boolean(!network.contains(self.attr("network_address"))); return !t.truthy() ? t : boolean(!network.contains(self.attr("broadcast_address"))); }() }); } return comprehension34; }()); }();
                    })) },
                { "is_global", B<"property">()(w.method("_BaseNetwork", "is_global", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(!self.attr("is_private").truthy());
                    })) },
                { "is_unspecified", B<"property">()(w.method("_BaseNetwork", "is_unspecified", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = self.attr("network_address").attr("is_unspecified"); return !t.truthy() ? t : self.attr("broadcast_address").attr("is_unspecified"); }();
                    })) },
                { "is_loopback", B<"property">()(w.method("_BaseNetwork", "is_loopback", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = self.attr("network_address").attr("is_loopback"); return !t.truthy() ? t : self.attr("broadcast_address").attr("is_loopback"); }();
                    })) }, })));

            define("ipaddress", "_BaseConstants", w.make_class("_BaseConstants", {  }, {
                { "_private_networks", py_list() }, }));

            (G<"ipaddress._BaseNetwork">()).set_attr("_constants", G<"ipaddress._BaseConstants">());
                // Equivalent to 255.255.255.255 or 32 bits of 1's.
                // There are only a handful of valid v4 netmasks, so we cache them all
                // when constructed (see _make_netmask()).
            define("ipaddress", "_BaseV4", w.make_class("_BaseV4", {  }, {
                { "__slots__", py_tuple() },
                { "_version", I<4>() },
                { "_ALL_ONES", power((I<2>()), (G<"ipaddress.IPV4LENGTH">())) - (I<1>()) },
                { "_max_prefixlen", G<"ipaddress.IPV4LENGTH">() },
                { "_netmask_cache", py_dict() },
                { "_explode_shorthand_ip_string", w.method("_BaseV4", "_explode_shorthand_ip_string", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"str">())(self);
                    }) },
                { "_make_netmask", w.class_method("_BaseV4", "_make_netmask", { py_positional("arg") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py arg = a[1];
                        py netmask;
                        py prefixlen;
                        if (!cls.attr("_netmask_cache").contains(arg))
                        {
                            if (isinstance(arg, B<"int">()))
                            {
                                prefixlen = arg;
                                if (!(((I<0>()) <= prefixlen) && (prefixlen <= cls.attr("_max_prefixlen"))))
                                {
                                    cls.method("_report_invalid_netmask", { prefixlen });
                                }
                            }
                            else
                            {
                                try
                                {
                                    // Check for a netmask in prefix length form
                                    prefixlen = cls.method("_prefix_from_prefix_string", { arg });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler35;
                                    if (matches(handler35.exception, G<"ipaddress.NetmaskValueError">()))
                                    {
                                        // Check for a netmask or hostmask in dotted-quad form.
                                        // This may raise NetmaskValueError.
                                        prefixlen = cls.method("_prefix_from_ip_string", { arg });
                                    }
                                    else
                                        py_reraise(handler35.exception);
                                }
                            }
                            netmask = (G<"ipaddress.IPv4Address">())(cls.method("_ip_int_from_prefix", { prefixlen }));
                            cls.attr("_netmask_cache").set_item(arg, py_tuple({ netmask, prefixlen }));
                        }
                        return cls.attr("_netmask_cache")[arg];
                    }) },
                { "_ip_int_from_string", w.class_method("_BaseV4", "_ip_int_from_string", { py_positional("ip_str") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py ip_str = a[1];
                        py exc;
                        py octets;
                        if (!ip_str.truthy())
                        {
                            py_raise((G<"ipaddress.AddressValueError">())(S<"Address cannot be empty">()));
                        }
                        octets = ip_str.method("split", { S<".">() });
                        if (py{ octets.size() } != (I<4>()))
                        {
                            py_raise((G<"ipaddress.AddressValueError">())((S<"Expected 4 octets in %r">()) % ip_str));
                        }
                        try
                        {
                            return (B<"int">()).method("from_bytes", { (B<"map">())(cls.attr("_parse_octet"), octets), S<"big">() });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler36;
                            if (matches(handler36.exception, B<"ValueError">()))
                            {
                                exc = handler36.exception;
                                fn_raise_from((G<"ipaddress.AddressValueError">())((S<"%s in %r">()) % py_tuple({ exc, ip_str })).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler36.exception);
                        }
                        return none();
                    }) },
                { "_parse_octet", w.class_method("_BaseV4", "_parse_octet", { py_positional("octet_str") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py octet_str = a[1];
                        py msg;
                        py octet_int;
                        if (!octet_str.truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Empty octet not permitted">()));
                        }
                        // Reject non-ASCII digits.
                        if (!([&]() -> py { py const t = octet_str.method("isascii"); return !t.truthy() ? t : octet_str.method("isdigit"); }()).truthy())
                        {
                            msg = S<"Only decimal digits permitted in %r">();
                            py_raise((B<"ValueError">())(msg % octet_str));
                        }
                        // We do the length check second, since the invalid character error
                        // is likely to be more informative for the user
                        if (py{ octet_str.size() } > (I<3>()))
                        {
                            msg = S<"At most 3 characters permitted in %r">();
                            py_raise((B<"ValueError">())(msg % octet_str));
                        }
                        // Handle leading zeros as strict as glibc's inet_pton()
                        // See security bug bpo-36384
                        if (((octet_str != (S<"0">())) && (octet_str[I<0>()] == (S<"0">()))))
                        {
                            msg = S<"Leading zeros are not permitted in %r">();
                            py_raise((B<"ValueError">())(msg % octet_str));
                        }
                        // Convert to integer (we know digits are legal)
                        octet_int = (B<"int">())(octet_str, I<10>());
                        if (octet_int > (I<255>()))
                        {
                            py_raise((B<"ValueError">())((S<"Octet %d (> 255) not permitted">()) % octet_int));
                        }
                        return octet_int;
                    }) },
                { "_string_from_ip_int", w.class_method("_BaseV4", "_string_from_ip_int", { py_positional("ip_int") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py ip_int = a[1];
                        return (S<".">()).method("join", { (B<"map">())(B<"str">(), ip_int.method("to_bytes", { I<4>(), S<"big">() })) });
                    }) },
                { "_reverse_pointer", w.method("_BaseV4", "_reverse_pointer", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py reverse_octets;
                        reverse_octets = (B<"str">())(self).method("split", { S<".">() })[B<"slice">()(none(), none(), I<-1>())];
                        return (S<".">()).method("join", { reverse_octets }) + (S<".in-addr.arpa">());
                    }) },
                { "max_prefixlen", B<"property">()(w.method("_BaseV4", "max_prefixlen", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_max_prefixlen");
                    })) },
                { "version", B<"property">()(w.method("_BaseV4", "version", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_version");
                    })) }, }));

            define("ipaddress", "IPv4Address", w.make_class("IPv4Address", { G<"ipaddress._BaseV4">(), G<"ipaddress._BaseAddress">() }, {
                { "__slots__", py_tuple({ S<"_ip">(), S<"__weakref__">() }) },
                { "__init__", w.method("IPv4Address", "__init__", { py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py address = a[1];
                        py addr_str;
                        // Efficient constructor from integer.
                        if (isinstance(address, B<"int">()))
                        {
                            self.method("_check_int_address", { address });
                            self.set_attr("_ip", address);
                            return none();
                        }
                        // Constructing from a packed address
                        if (isinstance(address, B<"bytes">()))
                        {
                            self.method("_check_packed_address", { address, I<4>() });
                            self.set_attr("_ip", (B<"int">()).method("from_bytes", { address }));
                            return none();
                        }
                        // Assume input argument to be string or any object representation
                        // which converts into a formatted IP string.
                        addr_str = (B<"str">())(address);
                        if (addr_str.contains((S<"/">())))
                        {
                            py_raise((G<"ipaddress.AddressValueError">())(S(std::string{ "Unexpected '/' in " } + address.repr())));
                        }
                        self.set_attr("_ip", self.method("_ip_int_from_string", { addr_str }));
                        return none();
                    }) },
                { "packed", B<"property">()(w.method("IPv4Address", "packed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"ipaddress.v4_int_to_packed">())(self.attr("_ip"));
                    })) },
                { "is_reserved", B<"property">()(w.method("IPv4Address", "is_reserved", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("_constants").attr("_reserved_network").contains(self));
                    })) },
                { "is_private", B<"property">()((Mod<"functools">()).method("lru_cache")(w.method("IPv4Address", "is_private", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = (B<"any">())([&]() -> py { py const comprehension37 = py_list(); py net; for (py const& item : iterate{ self.attr("_constants").attr("_private_networks") }) { net = item; comprehension37.method("append", { boolean(net.contains(self)) }); } return comprehension37; }()); return !t.truthy() ? t : (B<"all">())([&]() -> py { py const comprehension38 = py_list(); py net; for (py const& item : iterate{ self.attr("_constants").attr("_private_networks_exceptions") }) { net = item; comprehension38.method("append", { boolean(!net.contains(self)) }); } return comprehension38; }()); }();
                    }))) },
                { "is_global", B<"property">()((Mod<"functools">()).method("lru_cache")(w.method("IPv4Address", "is_global", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = boolean(!self.attr("_constants").attr("_public_network").contains(self)); return !t.truthy() ? t : boolean(!self.attr("is_private").truthy()); }();
                    }))) },
                { "is_multicast", B<"property">()(w.method("IPv4Address", "is_multicast", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("_constants").attr("_multicast_network").contains(self));
                    })) },
                { "is_unspecified", B<"property">()(w.method("IPv4Address", "is_unspecified", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self == self.attr("_constants").attr("_unspecified_address"));
                    })) },
                { "is_loopback", B<"property">()(w.method("IPv4Address", "is_loopback", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("_constants").attr("_loopback_network").contains(self));
                    })) },
                { "is_link_local", B<"property">()(w.method("IPv4Address", "is_link_local", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("_constants").attr("_linklocal_network").contains(self));
                    })) },
                { "ipv6_mapped", B<"property">()(w.method("IPv4Address", "ipv6_mapped", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"ipaddress.IPv6Address">())(S(std::string{ "::ffff:" } + self.str()));
                    })) }, }));

            define("ipaddress", "IPv4Interface", w.make_class("IPv4Interface", { G<"ipaddress.IPv4Address">() }, {
                { "__init__", w.method("IPv4Interface", "__init__", { py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py address = a[1];
                        py addr;
                        py mask;
                        auto const items39 = unpack(self.method("_split_addr_prefix", { address }), 2);
                        addr = items39[0];
                        mask = items39[1];
                        (G<"ipaddress.IPv4Address">()).method("__init__", { self, addr });
                        self.set_attr("network", (G<"ipaddress.IPv4Network">()).call({ py_tuple({ addr, mask }) }, { { "strict", boolean(false) } }));
                        self.set_attr("netmask", self.attr("network").attr("netmask"));
                        self.set_attr("_prefixlen", self.attr("network").attr("_prefixlen"));
                        return none();
                    }) },
                { "hostmask", (Mod<"functools">().attr("cached_property"))(w.method("IPv4Interface", "hostmask", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("network").attr("hostmask");
                    })) },
                { "__str__", w.method("IPv4Interface", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s/%d">()) % py_tuple({ self.method("_string_from_ip_int", { self.attr("_ip") }), self.attr("_prefixlen") });
                    }) },
                { "__eq__", w.method("IPv4Interface", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py address_equal;
                        address_equal = (G<"ipaddress.IPv4Address">()).method("__eq__", { self, other });
                        if ((address_equal.is(py::borrowed(not_implemented())) || (!address_equal.truthy())))
                        {
                            return address_equal;
                        }
                        try
                        {
                            return boolean(self.attr("network") == other.attr("network"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler40;
                            if (matches(handler40.exception, B<"AttributeError">()))
                            {
                                // An interface with an associated network is NOT the
                                // same as an unassociated address. That's why the hash
                                // takes the extra info into account.
                                return boolean(false);
                            }
                            else
                                py_reraise(handler40.exception);
                        }
                        return none();
                    }) },
                { "__lt__", w.method("IPv4Interface", "__lt__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py address_less;
                        address_less = (G<"ipaddress.IPv4Address">()).method("__lt__", { self, other });
                        if (address_less.is(py::borrowed(not_implemented())))
                        {
                            return py::borrowed(not_implemented());
                        }
                        try
                        {
                            return [&]() -> py { py const t = boolean(self.attr("network") < other.attr("network")); return t.truthy() ? t : [&]() -> py { py const t = boolean(self.attr("network") == other.attr("network")); return !t.truthy() ? t : address_less; }(); }();
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler41;
                            if (matches(handler41.exception, B<"AttributeError">()))
                            {
                                // We *do* allow addresses and interfaces to be sorted. The
                                // unassociated address is considered less than all interfaces.
                                return boolean(false);
                            }
                            else
                                py_reraise(handler41.exception);
                        }
                        return none();
                    }) },
                { "__hash__", w.method("IPv4Interface", "__hash__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"hash">())(py_tuple({ self.attr("_ip"), self.attr("_prefixlen"), (B<"int">())(self.attr("network").attr("network_address")) }));
                    }) },
                { "__reduce__", (G<"ipaddress._IPAddressBase">()).attr("__reduce__") },
                { "ip", B<"property">()(w.method("IPv4Interface", "ip", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"ipaddress.IPv4Address">())(self.attr("_ip"));
                    })) },
                { "with_prefixlen", B<"property">()(w.method("IPv4Interface", "with_prefixlen", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s/%s">()) % py_tuple({ self.method("_string_from_ip_int", { self.attr("_ip") }), self.attr("_prefixlen") });
                    })) },
                { "with_netmask", B<"property">()(w.method("IPv4Interface", "with_netmask", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s/%s">()) % py_tuple({ self.method("_string_from_ip_int", { self.attr("_ip") }), self.attr("netmask") });
                    })) },
                { "with_hostmask", B<"property">()(w.method("IPv4Interface", "with_hostmask", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s/%s">()) % py_tuple({ self.method("_string_from_ip_int", { self.attr("_ip") }), self.attr("hostmask") });
                    })) }, }));

                // Class to use when creating address objects
            define("ipaddress", "IPv4Network", w.make_class("IPv4Network", { G<"ipaddress._BaseV4">(), G<"ipaddress._BaseNetwork">() }, {
                { "_address_class", G<"ipaddress.IPv4Address">() },
                { "__init__", w.method("IPv4Network", "__init__", { py_positional("address"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py address = a[1];
                        py strict = a[2];
                        auto const addr = std::make_shared<py>();
                        py mask;
                        py packed;
                        auto const items42 = unpack(self.method("_split_addr_prefix", { address }), 2);
                        *addr = items42[0];
                        mask = items42[1];
                        self.set_attr("network_address", (G<"ipaddress.IPv4Address">())((*addr)));
                        auto const items43 = unpack(self.method("_make_netmask", { mask }), 2);
                        self.set_attr("netmask", items43[0]);
                        self.set_attr("_prefixlen", items43[1]);
                        packed = (B<"int">())(self.attr("network_address"));
                        if ((packed & (B<"int">())(self.attr("netmask"))) != packed)
                        {
                            if (strict.truthy())
                            {
                                py_raise((B<"ValueError">())((S<"%s has host bits set">()) % self));
                            }
                            else
                            {
                                self.set_attr("network_address", (G<"ipaddress.IPv4Address">())(packed & (B<"int">())(self.attr("netmask"))));
                            }
                        }
                        if (self.attr("_prefixlen") == (self.attr("_max_prefixlen") - (I<1>())))
                        {
                            self.set_attr("hosts", self.attr("__iter__"));
                        }
                        else if (self.attr("_prefixlen") == self.attr("_max_prefixlen"))
                        {
                            self.set_attr("hosts", w.function("<lambda>", "IPv4Network.__init__.<locals>.<lambda>", {  }, [addr](arguments a) -> py
        {
            return (B<"iter">())(py_tuple({ (G<"ipaddress.IPv4Address">())((*addr)) }));
        }));
                        }
                        return none();
                    }) },
                { "is_global", B<"property">()((Mod<"functools">()).method("lru_cache")(w.method("IPv4Network", "is_global", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = boolean(!([&]() -> py { py const t = boolean((G<"ipaddress.IPv4Network">())(S<"100.64.0.0/10">()).contains(self.attr("network_address"))); return !t.truthy() ? t : boolean((G<"ipaddress.IPv4Network">())(S<"100.64.0.0/10">()).contains(self.attr("broadcast_address"))); }()).truthy()); return !t.truthy() ? t : boolean(!self.attr("is_private").truthy()); }();
                    }))) }, }));

                // Not globally reachable address blocks listed on
                // https://www.iana.org/assignments/iana-ipv4-special-registry/iana-ipv4-special-registry.xhtml
            define("ipaddress", "_IPv4Constants", w.make_class("_IPv4Constants", {  }, {
                { "_linklocal_network", (G<"ipaddress.IPv4Network">())(S<"169.254.0.0/16">()) },
                { "_loopback_network", (G<"ipaddress.IPv4Network">())(S<"127.0.0.0/8">()) },
                { "_multicast_network", (G<"ipaddress.IPv4Network">())(S<"224.0.0.0/4">()) },
                { "_public_network", (G<"ipaddress.IPv4Network">())(S<"100.64.0.0/10">()) },
                { "_private_networks", py_list({ (G<"ipaddress.IPv4Network">())(S<"0.0.0.0/8">()), (G<"ipaddress.IPv4Network">())(S<"10.0.0.0/8">()), (G<"ipaddress.IPv4Network">())(S<"127.0.0.0/8">()), (G<"ipaddress.IPv4Network">())(S<"169.254.0.0/16">()), (G<"ipaddress.IPv4Network">())(S<"172.16.0.0/12">()), (G<"ipaddress.IPv4Network">())(S<"192.0.0.0/24">()), (G<"ipaddress.IPv4Network">())(S<"192.0.0.170/31">()), (G<"ipaddress.IPv4Network">())(S<"192.0.2.0/24">()), (G<"ipaddress.IPv4Network">())(S<"192.168.0.0/16">()), (G<"ipaddress.IPv4Network">())(S<"198.18.0.0/15">()), (G<"ipaddress.IPv4Network">())(S<"198.51.100.0/24">()), (G<"ipaddress.IPv4Network">())(S<"203.0.113.0/24">()), (G<"ipaddress.IPv4Network">())(S<"240.0.0.0/4">()), (G<"ipaddress.IPv4Network">())(S<"255.255.255.255/32">()) }) },
                { "_private_networks_exceptions", py_list({ (G<"ipaddress.IPv4Network">())(S<"192.0.0.9/32">()), (G<"ipaddress.IPv4Network">())(S<"192.0.0.10/32">()) }) },
                { "_reserved_network", (G<"ipaddress.IPv4Network">())(S<"240.0.0.0/4">()) },
                { "_unspecified_address", (G<"ipaddress.IPv4Address">())(S<"0.0.0.0">()) }, }));

            (G<"ipaddress.IPv4Address">()).set_attr("_constants", G<"ipaddress._IPv4Constants">());
            (G<"ipaddress.IPv4Network">()).set_attr("_constants", G<"ipaddress._IPv4Constants">());
                // There are only a bunch of valid v6 netmasks, so we cache them all
                // when constructed (see _make_netmask()).
            define("ipaddress", "_BaseV6", w.make_class("_BaseV6", {  }, {
                { "__slots__", py_tuple() },
                { "_version", I<6>() },
                { "_ALL_ONES", power((I<2>()), (G<"ipaddress.IPV6LENGTH">())) - (I<1>()) },
                { "_HEXTET_COUNT", I<8>() },
                { "_HEX_DIGITS", (B<"frozenset">())(S<"0123456789ABCDEFabcdef">()) },
                { "_max_prefixlen", G<"ipaddress.IPV6LENGTH">() },
                { "_netmask_cache", py_dict() },
                { "_make_netmask", w.class_method("_BaseV6", "_make_netmask", { py_positional("arg") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py arg = a[1];
                        py netmask;
                        py prefixlen;
                        if (!cls.attr("_netmask_cache").contains(arg))
                        {
                            if (isinstance(arg, B<"int">()))
                            {
                                prefixlen = arg;
                                if (!(((I<0>()) <= prefixlen) && (prefixlen <= cls.attr("_max_prefixlen"))))
                                {
                                    cls.method("_report_invalid_netmask", { prefixlen });
                                }
                            }
                            else
                            {
                                prefixlen = cls.method("_prefix_from_prefix_string", { arg });
                            }
                            netmask = (G<"ipaddress.IPv6Address">())(cls.method("_ip_int_from_prefix", { prefixlen }));
                            cls.attr("_netmask_cache").set_item(arg, py_tuple({ netmask, prefixlen }));
                        }
                        return cls.attr("_netmask_cache")[arg];
                    }) },
                { "_ip_int_from_string", w.class_method("_BaseV6", "_ip_int_from_string", { py_positional("ip_str") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py ip_str = a[1];
                        py _max_parts;
                        py _min_parts;
                        py exc;
                        py i;
                        py ip_int;
                        py ipv4_int;
                        py msg;
                        py parts;
                        py parts_hi;
                        py parts_lo;
                        py parts_skipped;
                        py shorten;
                        py skip_index;
                        if (!ip_str.truthy())
                        {
                            py_raise((G<"ipaddress.AddressValueError">())(S<"Address cannot be empty">()));
                        }
                        if (py{ ip_str.size() } > (I<45>()))
                        {
                            shorten = ip_str;
                            if (py{ shorten.size() } > (I<100>()))
                            {
                                shorten = S(ip_str[B<"slice">()(none(), I<45>(), none())].str() + "(" + (py{ ip_str.size() } - (I<90>())).str() + " chars elided)" + ip_str[B<"slice">()(I<-45>(), none(), none())].str());
                            }
                            py_raise((G<"ipaddress.AddressValueError">())(S(std::string{ "At most 45 characters expected in " } + shorten.repr())));
                        }
                        // We want to allow more parts than the max to be 'split'
                        // to preserve the correct error message when there are
                        // too many parts combined with '::'
                        _max_parts = cls.attr("_HEXTET_COUNT") + (I<1>());
                        parts = ip_str.method("split", { S<":">() }, { { "maxsplit", _max_parts } });
                        // An IPv6 address needs at least 2 colons (3 parts).
                        _min_parts = I<3>();
                        if (py{ parts.size() } < _min_parts)
                        {
                            msg = (S<"At least %d parts expected in %r">()) % py_tuple({ _min_parts, ip_str });
                            py_raise((G<"ipaddress.AddressValueError">())(msg));
                        }
                        // If the address has an IPv4-style suffix, convert it to hexadecimal.
                        if (parts[I<-1>()].contains((S<".">())))
                        {
                            try
                            {
                                ipv4_int = (G<"ipaddress.IPv4Address">())(parts.method("pop")).attr("_ip");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler44;
                                if (matches(handler44.exception, G<"ipaddress.AddressValueError">()))
                                {
                                    exc = handler44.exception;
                                    fn_raise_from((G<"ipaddress.AddressValueError">())((S<"%s in %r">()) % py_tuple({ exc, ip_str })).get(), none().get());
                                    throw raised{};
                                }
                                else
                                    py_reraise(handler44.exception);
                            }
                            parts.method("append", { (S<"%x">()) % ((ipv4_int >> (I<16>())) & (I<65535>())) });
                            parts.method("append", { (S<"%x">()) % (ipv4_int & (I<65535>())) });
                        }
                        // An IPv6 address can't have more than 8 colons (9 parts).
                        // The extra colon comes from using the "::" notation for a single
                        // leading or trailing zero part.
                        if (py{ parts.size() } > _max_parts)
                        {
                            msg = (S<"At most %d colons permitted in %r">()) % py_tuple({ _max_parts - (I<1>()), ip_str });
                            py_raise((G<"ipaddress.AddressValueError">())(msg));
                        }
                        // Disregarding the endpoints, find '::' with nothing in between.
                        // This indicates that a run of zeroes has been skipped.
                        skip_index = none();
                        for (py const& item : iterate{ (B<"range">())(I<1>(), py{ parts.size() } - (I<1>())) })
                        {
                            i = item;
                            if (!parts[i].truthy())
                            {
                                if (!skip_index.is_none())
                                {
                                    // Can't have more than one '::'
                                    msg = (S<"At most one '::' permitted in %r">()) % ip_str;
                                    py_raise((G<"ipaddress.AddressValueError">())(msg));
                                }
                                skip_index = i;
                            }
                        }
                        // parts_hi is the number of parts to copy from above/before the '::'
                        // parts_lo is the number of parts to copy from below/after the '::'
                        if (!skip_index.is_none())
                        {
                            // If we found a '::', then check if it also covers the endpoints.
                            parts_hi = skip_index;
                            parts_lo = (py{ parts.size() } - skip_index) - (I<1>());
                            if (!parts[I<0>()].truthy())
                            {
                                parts_hi = inplace(NEOS_PY_SUBTRACT, parts_hi, I<1>());
                                if (parts_hi.truthy())
                                {
                                    msg = S<"Leading ':' only permitted as part of '::' in %r">();
                                    py_raise((G<"ipaddress.AddressValueError">())(msg % ip_str));
                                }
                            }
                            if (!parts[I<-1>()].truthy())
                            {
                                parts_lo = inplace(NEOS_PY_SUBTRACT, parts_lo, I<1>());
                                if (parts_lo.truthy())
                                {
                                    msg = S<"Trailing ':' only permitted as part of '::' in %r">();
                                    py_raise((G<"ipaddress.AddressValueError">())(msg % ip_str));
                                }
                            }
                            parts_skipped = cls.attr("_HEXTET_COUNT") - (parts_hi + parts_lo);
                            if (parts_skipped < (I<1>()))
                            {
                                msg = S<"Expected at most %d other parts with '::' in %r">();
                                py_raise((G<"ipaddress.AddressValueError">())(msg % py_tuple({ cls.attr("_HEXTET_COUNT") - (I<1>()), ip_str })));
                            }
                        }
                        else
                        {
                            // Otherwise, allocate the entire address to parts_hi.  The
                            // endpoints could still be empty, but _parse_hextet() will check
                            // for that.
                            if (py{ parts.size() } != cls.attr("_HEXTET_COUNT"))
                            {
                                msg = S<"Exactly %d parts expected without '::' in %r">();
                                py_raise((G<"ipaddress.AddressValueError">())(msg % py_tuple({ cls.attr("_HEXTET_COUNT"), ip_str })));
                            }
                            if (!parts[I<0>()].truthy())
                            {
                                msg = S<"Leading ':' only permitted as part of '::' in %r">();
                                py_raise((G<"ipaddress.AddressValueError">())(msg % ip_str));
                            }
                            if (!parts[I<-1>()].truthy())
                            {
                                msg = S<"Trailing ':' only permitted as part of '::' in %r">();
                                py_raise((G<"ipaddress.AddressValueError">())(msg % ip_str));
                            }
                            parts_hi = py{ parts.size() };
                            parts_lo = I<0>();
                            parts_skipped = I<0>();
                        }
                        try
                        {
                            // Now, parse the hextets into a 128-bit integer.
                            ip_int = I<0>();
                            for (py const& item : iterate{ (B<"range">())(parts_hi) })
                            {
                                i = item;
                                ip_int = inplace(NEOS_PY_LSHIFT, ip_int, I<16>());
                                ip_int = inplace(NEOS_PY_OR, ip_int, cls.method("_parse_hextet", { parts[i] }));
                            }
                            ip_int = inplace(NEOS_PY_LSHIFT, ip_int, (I<16>()) * parts_skipped);
                            for (py const& item : iterate{ (B<"range">())(-parts_lo, I<0>()) })
                            {
                                i = item;
                                ip_int = inplace(NEOS_PY_LSHIFT, ip_int, I<16>());
                                ip_int = inplace(NEOS_PY_OR, ip_int, cls.method("_parse_hextet", { parts[i] }));
                            }
                            return ip_int;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler46;
                            if (matches(handler46.exception, B<"ValueError">()))
                            {
                                exc = handler46.exception;
                                fn_raise_from((G<"ipaddress.AddressValueError">())((S<"%s in %r">()) % py_tuple({ exc, ip_str })).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler46.exception);
                        }
                        return none();
                    }) },
                { "_parse_hextet", w.class_method("_BaseV6", "_parse_hextet", { py_positional("hextet_str") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py hextet_str = a[1];
                        py msg;
                        // Reject non-ASCII digits.
                        if (!cls.attr("_HEX_DIGITS").method("issuperset", { hextet_str }).truthy())
                        {
                            py_raise((B<"ValueError">())((S<"Only hex digits permitted in %r">()) % hextet_str));
                        }
                        // We do the length check second, since the invalid character error
                        // is likely to be more informative for the user
                        if (py{ hextet_str.size() } > (I<4>()))
                        {
                            msg = S<"At most 4 characters permitted in %r">();
                            py_raise((B<"ValueError">())(msg % hextet_str));
                        }
                        // Length check means we can skip checking the integer value
                        return (B<"int">())(hextet_str, I<16>());
                    }) },
                { "_compress_hextets", w.class_method("_BaseV6", "_compress_hextets", { py_positional("hextets") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py hextets = a[1];
                        py best_doublecolon_end;
                        py best_doublecolon_len;
                        py best_doublecolon_start;
                        py doublecolon_len;
                        py doublecolon_start;
                        py hextet;
                        py index;
                        best_doublecolon_start = I<-1>();
                        best_doublecolon_len = I<0>();
                        doublecolon_start = I<-1>();
                        doublecolon_len = I<0>();
                        for (py const& item : iterate{ (B<"enumerate">())(hextets) })
                        {
                            auto const items50 = unpack(item, 2);
                            index = items50[0];
                            hextet = items50[1];
                            if (hextet == (S<"0">()))
                            {
                                doublecolon_len = inplace(NEOS_PY_ADD, doublecolon_len, I<1>());
                                if (doublecolon_start == (I<-1>()))
                                {
                                    // Start of a sequence of zeros.
                                    doublecolon_start = index;
                                }
                                if (doublecolon_len > best_doublecolon_len)
                                {
                                    // This is the longest sequence of zeros so far.
                                    best_doublecolon_len = doublecolon_len;
                                    best_doublecolon_start = doublecolon_start;
                                }
                            }
                            else
                            {
                                doublecolon_len = I<0>();
                                doublecolon_start = I<-1>();
                            }
                        }
                        if (best_doublecolon_len > (I<1>()))
                        {
                            best_doublecolon_end = best_doublecolon_start + best_doublecolon_len;
                            // For zeros at the end of the address.
                            if (best_doublecolon_end == py{ hextets.size() })
                            {
                                hextets = inplace(NEOS_PY_ADD, hextets, py_list({ S<"">() }));
                            }
                            hextets.set_item(B<"slice">()(best_doublecolon_start, best_doublecolon_end, none()), py_list({ S<"">() }));
                            // For zeros at the beginning of the address.
                            if (best_doublecolon_start == (I<0>()))
                            {
                                hextets = py_list({ S<"">() }) + hextets;
                            }
                        }
                        return hextets;
                    }) },
                { "_string_from_ip_int", w.class_method("_BaseV6", "_string_from_ip_int", { py_positional("ip_int", none()) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py ip_int = a[1];
                        py hex_str;
                        py hextets;
                        if (ip_int.is_none())
                        {
                            ip_int = (B<"int">())(cls.attr("_ip"));
                        }
                        if (ip_int > cls.attr("_ALL_ONES"))
                        {
                            py_raise((B<"ValueError">())(S<"IPv6 address is too large">()));
                        }
                        hex_str = (S<"%032x">()) % ip_int;
                        hextets = [&]() -> py { py const comprehension51 = py_list(); py x; for (py const& item : iterate{ (B<"range">())(I<0>(), I<32>(), I<4>()) }) { x = item; comprehension51.method("append", { (S<"%x">()) % (B<"int">())(hex_str[B<"slice">()(x, x + (I<4>()), none())], I<16>()) }); } return comprehension51; }();
                        hextets = cls.method("_compress_hextets", { hextets });
                        return (S<":">()).method("join", { hextets });
                    }) },
                { "_explode_shorthand_ip_string", w.method("_BaseV6", "_explode_shorthand_ip_string", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exploded;
                        py hex_str;
                        py ip_int;
                        py ip_str;
                        if (isinstance(self, G<"ipaddress.IPv6Network">()))
                        {
                            ip_str = (B<"str">())(self.attr("network_address"));
                        }
                        else if (isinstance(self, G<"ipaddress.IPv6Interface">()))
                        {
                            ip_str = (B<"str">())(self.attr("ip"));
                        }
                        else
                        {
                            ip_str = self.method("_string_from_ip_int", { self.attr("_ip") });
                        }
                        ip_int = self.method("_ip_int_from_string", { ip_str });
                        hex_str = (S<"%032x">()) % ip_int;
                        exploded = (S<":">()).method("join", { [&]() -> py { py const comprehension52 = py_list(); py x; for (py const& item : iterate{ (B<"range">())(I<0>(), I<32>(), I<4>()) }) { x = item; comprehension52.method("append", { hex_str[B<"slice">()(x, x + (I<4>()), none())] }); } return comprehension52; }() });
                        if (isinstance(self, G<"ipaddress._BaseNetwork">()))
                        {
                            return (S<"%s/%d">()) % py_tuple({ exploded, self.attr("_prefixlen") });
                        }
                        if (self.attr("_scope_id").truthy())
                        {
                            exploded = (S<"%s%%%s">()) % py_tuple({ exploded, self.attr("_scope_id") });
                        }
                        if (isinstance(self, G<"ipaddress.IPv6Interface">()))
                        {
                            return (S<"%s/%d">()) % py_tuple({ exploded, self.attr("_prefixlen") });
                        }
                        return exploded;
                    }) },
                { "_reverse_pointer", w.method("_BaseV6", "_reverse_pointer", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py reverse_chars;
                        reverse_chars = self.attr("exploded")[B<"slice">()(none(), none(), I<-1>())].method("replace", { S<":">(), S<"">() });
                        return (S<".">()).method("join", { reverse_chars }) + (S<".ip6.arpa">());
                    }) },
                { "_split_scope_id", w.static_method("_BaseV6", "_split_scope_id", { py_positional("ip_str") }, [](arguments a) -> py
                    {
                        py ip_str = a[0];
                        py addr;
                        py scope_id;
                        py sep;
                        auto const items53 = unpack(ip_str.method("partition", { S<"%">() }), 3);
                        addr = items53[0];
                        sep = items53[1];
                        scope_id = items53[2];
                        if (!sep.truthy())
                        {
                            scope_id = none();
                        }
                        else if (((!scope_id.truthy()) || scope_id.contains((S<"%">()))))
                        {
                            py_raise((G<"ipaddress.AddressValueError">())((S<"Invalid IPv6 address: \"%r\"">()) % ip_str));
                        }
                        return py_tuple({ addr, scope_id });
                    }) },
                { "max_prefixlen", B<"property">()(w.method("_BaseV6", "max_prefixlen", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_max_prefixlen");
                    })) },
                { "version", B<"property">()(w.method("_BaseV6", "version", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_version");
                    })) }, }));

            define("ipaddress", "IPv6Address", w.make_class("IPv6Address", { G<"ipaddress._BaseV6">(), G<"ipaddress._BaseAddress">() }, {
                { "__slots__", py_tuple({ S<"_ip">(), S<"_scope_id">(), S<"__weakref__">() }) },
                { "__init__", w.method("IPv6Address", "__init__", { py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py address = a[1];
                        py addr_str;
                        // Efficient constructor from integer.
                        if (isinstance(address, B<"int">()))
                        {
                            self.method("_check_int_address", { address });
                            self.set_attr("_ip", address);
                            self.set_attr("_scope_id", none());
                            return none();
                        }
                        // Constructing from a packed address
                        if (isinstance(address, B<"bytes">()))
                        {
                            self.method("_check_packed_address", { address, I<16>() });
                            self.set_attr("_ip", (B<"int">()).method("from_bytes", { address, S<"big">() }));
                            self.set_attr("_scope_id", none());
                            return none();
                        }
                        // Assume input argument to be string or any object representation
                        // which converts into a formatted IP string.
                        addr_str = (B<"str">())(address);
                        if (addr_str.contains((S<"/">())))
                        {
                            py_raise((G<"ipaddress.AddressValueError">())(S(std::string{ "Unexpected '/' in " } + address.repr())));
                        }
                        auto const items54 = unpack(self.method("_split_scope_id", { addr_str }), 2);
                        addr_str = items54[0];
                        self.set_attr("_scope_id", items54[1]);
                        self.set_attr("_ip", self.method("_ip_int_from_string", { addr_str }));
                        return none();
                    }) },
                { "_explode_shorthand_ip_string", w.method("IPv6Address", "_explode_shorthand_ip_string", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ipv4_mapped;
                        py prefix_len;
                        py raw_exploded_str;
                        ipv4_mapped = self.attr("ipv4_mapped");
                        if (ipv4_mapped.is_none())
                        {
                            return (B<"super">()(G<"ipaddress.IPv6Address">(), self)).method("_explode_shorthand_ip_string");
                        }
                        prefix_len = I<30>();
                        raw_exploded_str = (B<"super">()(G<"ipaddress.IPv6Address">(), self)).method("_explode_shorthand_ip_string");
                        return S(raw_exploded_str[B<"slice">()(none(), prefix_len, none())].str() + ipv4_mapped.str());
                    }) },
                { "_reverse_pointer", w.method("IPv6Address", "_reverse_pointer", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ipv4_int;
                        py ipv4_mapped;
                        py prefix_len;
                        py raw_exploded_str;
                        py reverse_chars;
                        ipv4_mapped = self.attr("ipv4_mapped");
                        if (ipv4_mapped.is_none())
                        {
                            return (B<"super">()(G<"ipaddress.IPv6Address">(), self)).method("_reverse_pointer");
                        }
                        prefix_len = I<30>();
                        raw_exploded_str = (B<"super">()(G<"ipaddress.IPv6Address">(), self)).method("_explode_shorthand_ip_string")[B<"slice">()(none(), prefix_len, none())];
                        // ipv4 encoded using hexadecimal nibbles instead of decimals
                        ipv4_int = ipv4_mapped.attr("_ip");
                        reverse_chars = S(raw_exploded_str.str() + formatted(ipv4_int, "008x"))[B<"slice">()(none(), none(), I<-1>())].method("replace", { S<":">(), S<"">() });
                        return (S<".">()).method("join", { reverse_chars }) + (S<".ip6.arpa">());
                    }) },
                { "_ipv4_mapped_ipv6_to_str", w.method("IPv6Address", "_ipv4_mapped_ipv6_to_str", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py high_order_bits;
                        py ipv4_mapped;
                        ipv4_mapped = self.attr("ipv4_mapped");
                        if (ipv4_mapped.is_none())
                        {
                            py_raise((G<"ipaddress.AddressValueError">())((S<"Can not apply to non-IPv4-mapped IPv6 address %s">()) % (B<"str">())(self)));
                        }
                        high_order_bits = self.attr("_ip") >> (I<32>());
                        return (S<"%s:%s">()) % py_tuple({ self.method("_string_from_ip_int", { high_order_bits }), (B<"str">())(ipv4_mapped) });
                    }) },
                { "__str__", w.method("IPv6Address", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ip_str;
                        py ipv4_mapped;
                        ipv4_mapped = self.attr("ipv4_mapped");
                        if (ipv4_mapped.is_none())
                        {
                            ip_str = (B<"super">()(G<"ipaddress.IPv6Address">(), self)).method("__str__");
                        }
                        else
                        {
                            ip_str = self.method("_ipv4_mapped_ipv6_to_str");
                        }
                        return (self.attr("_scope_id").truthy() ? (ip_str + (S<"%">())) + self.attr("_scope_id") : ip_str);
                    }) },
                { "__hash__", w.method("IPv6Address", "__hash__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"hash">())(py_tuple({ self.attr("_ip"), self.attr("_scope_id") }));
                    }) },
                { "__eq__", w.method("IPv6Address", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py address_equal;
                        address_equal = (B<"super">()(G<"ipaddress.IPv6Address">(), self)).method("__eq__", { other });
                        if (address_equal.is(py::borrowed(not_implemented())))
                        {
                            return py::borrowed(not_implemented());
                        }
                        if (!address_equal.truthy())
                        {
                            return boolean(false);
                        }
                        return boolean(self.attr("_scope_id") == (B<"getattr">())(other, S<"_scope_id">(), none()));
                    }) },
                { "__reduce__", w.method("IPv6Address", "__reduce__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ self.attr("__class__"), py_tuple({ (B<"str">())(self) }) });
                    }) },
                { "scope_id", B<"property">()(w.method("IPv6Address", "scope_id", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_scope_id");
                    })) },
                { "packed", B<"property">()(w.method("IPv6Address", "packed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"ipaddress.v6_int_to_packed">())(self.attr("_ip"));
                    })) },
                { "is_multicast", B<"property">()(w.method("IPv6Address", "is_multicast", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ipv4_mapped;
                        ipv4_mapped = self.attr("ipv4_mapped");
                        if (!ipv4_mapped.is_none())
                        {
                            return ipv4_mapped.attr("is_multicast");
                        }
                        return boolean(self.attr("_constants").attr("_multicast_network").contains(self));
                    })) },
                { "is_reserved", B<"property">()(w.method("IPv6Address", "is_reserved", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ipv4_mapped;
                        ipv4_mapped = self.attr("ipv4_mapped");
                        if (!ipv4_mapped.is_none())
                        {
                            return ipv4_mapped.attr("is_reserved");
                        }
                        return (B<"any">())([&]() -> py { py const comprehension55 = py_list(); py x; for (py const& item : iterate{ self.attr("_constants").attr("_reserved_networks") }) { x = item; comprehension55.method("append", { boolean(x.contains(self)) }); } return comprehension55; }());
                    })) },
                { "is_link_local", B<"property">()(w.method("IPv6Address", "is_link_local", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ipv4_mapped;
                        ipv4_mapped = self.attr("ipv4_mapped");
                        if (!ipv4_mapped.is_none())
                        {
                            return ipv4_mapped.attr("is_link_local");
                        }
                        return boolean(self.attr("_constants").attr("_linklocal_network").contains(self));
                    })) },
                { "is_site_local", B<"property">()(w.method("IPv6Address", "is_site_local", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("_constants").attr("_sitelocal_network").contains(self));
                    })) },
                { "is_private", B<"property">()((Mod<"functools">()).method("lru_cache")(w.method("IPv6Address", "is_private", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ipv4_mapped;
                        ipv4_mapped = self.attr("ipv4_mapped");
                        if (!ipv4_mapped.is_none())
                        {
                            return ipv4_mapped.attr("is_private");
                        }
                        return [&]() -> py { py const t = (B<"any">())([&]() -> py { py const comprehension56 = py_list(); py net; for (py const& item : iterate{ self.attr("_constants").attr("_private_networks") }) { net = item; comprehension56.method("append", { boolean(net.contains(self)) }); } return comprehension56; }()); return !t.truthy() ? t : (B<"all">())([&]() -> py { py const comprehension57 = py_list(); py net; for (py const& item : iterate{ self.attr("_constants").attr("_private_networks_exceptions") }) { net = item; comprehension57.method("append", { boolean(!net.contains(self)) }); } return comprehension57; }()); }();
                    }))) },
                { "is_global", B<"property">()(w.method("IPv6Address", "is_global", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ipv4_mapped;
                        ipv4_mapped = self.attr("ipv4_mapped");
                        if (!ipv4_mapped.is_none())
                        {
                            return ipv4_mapped.attr("is_global");
                        }
                        return boolean(!self.attr("is_private").truthy());
                    })) },
                { "is_unspecified", B<"property">()(w.method("IPv6Address", "is_unspecified", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ipv4_mapped;
                        ipv4_mapped = self.attr("ipv4_mapped");
                        if (!ipv4_mapped.is_none())
                        {
                            return ipv4_mapped.attr("is_unspecified");
                        }
                        return boolean(self.attr("_ip") == (I<0>()));
                    })) },
                { "is_loopback", B<"property">()(w.method("IPv6Address", "is_loopback", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ipv4_mapped;
                        ipv4_mapped = self.attr("ipv4_mapped");
                        if (!ipv4_mapped.is_none())
                        {
                            return ipv4_mapped.attr("is_loopback");
                        }
                        return boolean(self.attr("_ip") == (I<1>()));
                    })) },
                { "ipv4_mapped", B<"property">()(w.method("IPv6Address", "ipv4_mapped", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((self.attr("_ip") >> (I<32>())) != (I<65535>()))
                        {
                            return none();
                        }
                        return (G<"ipaddress.IPv4Address">())(self.attr("_ip") & (I<4294967295ll>()));
                    })) },
                { "teredo", B<"property">()(w.method("IPv6Address", "teredo", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((self.attr("_ip") >> (I<96>())) != (I<536936448>()))
                        {
                            return none();
                        }
                        return py_tuple({ (G<"ipaddress.IPv4Address">())((self.attr("_ip") >> (I<64>())) & (I<4294967295ll>())), (G<"ipaddress.IPv4Address">())(py{ unary(NEOS_PY_INVERT, self.attr("_ip").get()) } & (I<4294967295ll>())) });
                    })) },
                { "sixtofour", B<"property">()(w.method("IPv6Address", "sixtofour", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((self.attr("_ip") >> (I<112>())) != (I<8194>()))
                        {
                            return none();
                        }
                        return (G<"ipaddress.IPv4Address">())((self.attr("_ip") >> (I<80>())) & (I<4294967295ll>()));
                    })) }, }));

            define("ipaddress", "IPv6Interface", w.make_class("IPv6Interface", { G<"ipaddress.IPv6Address">() }, {
                { "__init__", w.method("IPv6Interface", "__init__", { py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py address = a[1];
                        py addr;
                        py mask;
                        auto const items58 = unpack(self.method("_split_addr_prefix", { address }), 2);
                        addr = items58[0];
                        mask = items58[1];
                        (G<"ipaddress.IPv6Address">()).method("__init__", { self, addr });
                        self.set_attr("network", (G<"ipaddress.IPv6Network">()).call({ py_tuple({ addr, mask }) }, { { "strict", boolean(false) } }));
                        self.set_attr("netmask", self.attr("network").attr("netmask"));
                        self.set_attr("_prefixlen", self.attr("network").attr("_prefixlen"));
                        return none();
                    }) },
                { "hostmask", (Mod<"functools">().attr("cached_property"))(w.method("IPv6Interface", "hostmask", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("network").attr("hostmask");
                    })) },
                { "__str__", w.method("IPv6Interface", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s/%d">()) % py_tuple({ (B<"super">()(G<"ipaddress.IPv6Interface">(), self)).method("__str__"), self.attr("_prefixlen") });
                    }) },
                { "__eq__", w.method("IPv6Interface", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py address_equal;
                        address_equal = (G<"ipaddress.IPv6Address">()).method("__eq__", { self, other });
                        if ((address_equal.is(py::borrowed(not_implemented())) || (!address_equal.truthy())))
                        {
                            return address_equal;
                        }
                        try
                        {
                            return boolean(self.attr("network") == other.attr("network"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler59;
                            if (matches(handler59.exception, B<"AttributeError">()))
                            {
                                // An interface with an associated network is NOT the
                                // same as an unassociated address. That's why the hash
                                // takes the extra info into account.
                                return boolean(false);
                            }
                            else
                                py_reraise(handler59.exception);
                        }
                        return none();
                    }) },
                { "__lt__", w.method("IPv6Interface", "__lt__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py address_less;
                        address_less = (G<"ipaddress.IPv6Address">()).method("__lt__", { self, other });
                        if (address_less.is(py::borrowed(not_implemented())))
                        {
                            return address_less;
                        }
                        try
                        {
                            return [&]() -> py { py const t = boolean(self.attr("network") < other.attr("network")); return t.truthy() ? t : [&]() -> py { py const t = boolean(self.attr("network") == other.attr("network")); return !t.truthy() ? t : address_less; }(); }();
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler60;
                            if (matches(handler60.exception, B<"AttributeError">()))
                            {
                                // We *do* allow addresses and interfaces to be sorted. The
                                // unassociated address is considered less than all interfaces.
                                return boolean(false);
                            }
                            else
                                py_reraise(handler60.exception);
                        }
                        return none();
                    }) },
                { "__hash__", w.method("IPv6Interface", "__hash__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"hash">())(py_tuple({ self.attr("_ip"), self.attr("_prefixlen"), (B<"int">())(self.attr("network").attr("network_address")) }));
                    }) },
                { "__reduce__", (G<"ipaddress._IPAddressBase">()).attr("__reduce__") },
                { "ip", B<"property">()(w.method("IPv6Interface", "ip", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"ipaddress.IPv6Address">())(self.attr("_ip"));
                    })) },
                { "with_prefixlen", B<"property">()(w.method("IPv6Interface", "with_prefixlen", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s/%s">()) % py_tuple({ self.method("_string_from_ip_int", { self.attr("_ip") }), self.attr("_prefixlen") });
                    })) },
                { "with_netmask", B<"property">()(w.method("IPv6Interface", "with_netmask", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s/%s">()) % py_tuple({ self.method("_string_from_ip_int", { self.attr("_ip") }), self.attr("netmask") });
                    })) },
                { "with_hostmask", B<"property">()(w.method("IPv6Interface", "with_hostmask", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s/%s">()) % py_tuple({ self.method("_string_from_ip_int", { self.attr("_ip") }), self.attr("hostmask") });
                    })) },
                { "is_unspecified", B<"property">()(w.method("IPv6Interface", "is_unspecified", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = boolean(self.attr("_ip") == (I<0>())); return !t.truthy() ? t : self.attr("network").attr("is_unspecified"); }();
                    })) },
                { "is_loopback", B<"property">()(w.method("IPv6Interface", "is_loopback", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = (B<"super">()(G<"ipaddress.IPv6Interface">(), self)).attr("is_loopback"); return !t.truthy() ? t : self.attr("network").attr("is_loopback"); }();
                    })) }, }));

                // Class to use when creating address objects
            define("ipaddress", "IPv6Network", w.make_class("IPv6Network", { G<"ipaddress._BaseV6">(), G<"ipaddress._BaseNetwork">() }, {
                { "_address_class", G<"ipaddress.IPv6Address">() },
                { "__init__", w.method("IPv6Network", "__init__", { py_positional("address"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py address = a[1];
                        py strict = a[2];
                        auto const addr = std::make_shared<py>();
                        py mask;
                        py packed;
                        auto const items61 = unpack(self.method("_split_addr_prefix", { address }), 2);
                        *addr = items61[0];
                        mask = items61[1];
                        self.set_attr("network_address", (G<"ipaddress.IPv6Address">())((*addr)));
                        auto const items62 = unpack(self.method("_make_netmask", { mask }), 2);
                        self.set_attr("netmask", items62[0]);
                        self.set_attr("_prefixlen", items62[1]);
                        packed = (B<"int">())(self.attr("network_address"));
                        if ((packed & (B<"int">())(self.attr("netmask"))) != packed)
                        {
                            if (strict.truthy())
                            {
                                py_raise((B<"ValueError">())((S<"%s has host bits set">()) % self));
                            }
                            else
                            {
                                self.set_attr("network_address", (G<"ipaddress.IPv6Address">())(packed & (B<"int">())(self.attr("netmask"))));
                            }
                        }
                        if (self.attr("_prefixlen") == (self.attr("_max_prefixlen") - (I<1>())))
                        {
                            self.set_attr("hosts", self.attr("__iter__"));
                        }
                        else if (self.attr("_prefixlen") == self.attr("_max_prefixlen"))
                        {
                            self.set_attr("hosts", w.function("<lambda>", "IPv6Network.__init__.<locals>.<lambda>", {  }, [addr](arguments a) -> py
        {
            return (B<"iter">())(py_tuple({ (G<"ipaddress.IPv6Address">())((*addr)) }));
        }));
                        }
                        return none();
                    }) },
                { "hosts", w.method("IPv6Network", "hosts", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("hosts", "IPv6Network.hosts", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py broadcast;
                            py network;
                            py x;
                            network = (B<"int">())(self.attr("network_address"));
                            broadcast = (B<"int">())(self.attr("broadcast_address"));
                            for (py const& item : iterate{ (B<"range">())(network + (I<1>()), broadcast + (I<1>())) })
                            {
                                x = item;
                                py_yield(self.method("_address_class", { x }));
                            }
                            return none();
                        });
                    }) },
                { "is_site_local", B<"property">()(w.method("IPv6Network", "is_site_local", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = self.attr("network_address").attr("is_site_local"); return !t.truthy() ? t : self.attr("broadcast_address").attr("is_site_local"); }();
                    })) }, }));

                // Not globally reachable address blocks listed on
                // https://www.iana.org/assignments/iana-ipv6-special-registry/iana-ipv6-special-registry.xhtml
                // IANA says N/A, let's consider it not globally reachable to be safe
                // RFC 9637: https://www.rfc-editor.org/rfc/rfc9637.html#section-6-2.2
            define("ipaddress", "_IPv6Constants", w.make_class("_IPv6Constants", {  }, {
                { "_linklocal_network", (G<"ipaddress.IPv6Network">())(S<"fe80::/10">()) },
                { "_multicast_network", (G<"ipaddress.IPv6Network">())(S<"ff00::/8">()) },
                { "_private_networks", py_list({ (G<"ipaddress.IPv6Network">())(S<"::1/128">()), (G<"ipaddress.IPv6Network">())(S<"::/128">()), (G<"ipaddress.IPv6Network">())(S<"::ffff:0:0/96">()), (G<"ipaddress.IPv6Network">())(S<"64:ff9b:1::/48">()), (G<"ipaddress.IPv6Network">())(S<"100::/64">()), (G<"ipaddress.IPv6Network">())(S<"2001::/23">()), (G<"ipaddress.IPv6Network">())(S<"2001:db8::/32">()), (G<"ipaddress.IPv6Network">())(S<"2002::/16">()), (G<"ipaddress.IPv6Network">())(S<"3fff::/20">()), (G<"ipaddress.IPv6Network">())(S<"fc00::/7">()), (G<"ipaddress.IPv6Network">())(S<"fe80::/10">()) }) },
                { "_private_networks_exceptions", py_list({ (G<"ipaddress.IPv6Network">())(S<"2001:1::1/128">()), (G<"ipaddress.IPv6Network">())(S<"2001:1::2/128">()), (G<"ipaddress.IPv6Network">())(S<"2001:3::/32">()), (G<"ipaddress.IPv6Network">())(S<"2001:4:112::/48">()), (G<"ipaddress.IPv6Network">())(S<"2001:20::/28">()), (G<"ipaddress.IPv6Network">())(S<"2001:30::/28">()) }) },
                { "_reserved_networks", py_list({ (G<"ipaddress.IPv6Network">())(S<"::/8">()), (G<"ipaddress.IPv6Network">())(S<"100::/8">()), (G<"ipaddress.IPv6Network">())(S<"200::/7">()), (G<"ipaddress.IPv6Network">())(S<"400::/6">()), (G<"ipaddress.IPv6Network">())(S<"800::/5">()), (G<"ipaddress.IPv6Network">())(S<"1000::/4">()), (G<"ipaddress.IPv6Network">())(S<"4000::/3">()), (G<"ipaddress.IPv6Network">())(S<"6000::/3">()), (G<"ipaddress.IPv6Network">())(S<"8000::/3">()), (G<"ipaddress.IPv6Network">())(S<"A000::/3">()), (G<"ipaddress.IPv6Network">())(S<"C000::/3">()), (G<"ipaddress.IPv6Network">())(S<"E000::/4">()), (G<"ipaddress.IPv6Network">())(S<"F000::/5">()), (G<"ipaddress.IPv6Network">())(S<"F800::/6">()), (G<"ipaddress.IPv6Network">())(S<"FE00::/9">()) }) },
                { "_sitelocal_network", (G<"ipaddress.IPv6Network">())(S<"fec0::/10">()) }, }));

            (G<"ipaddress.IPv6Address">()).set_attr("_constants", G<"ipaddress._IPv6Constants">());
            (G<"ipaddress.IPv6Network">()).set_attr("_constants", G<"ipaddress._IPv6Constants">());
        }

        bool ipaddress_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_ipaddress();
                }
                if (auto const doc = ipaddress_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("functools", py{ fn_import("functools") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("ipaddress", ipaddress_module);
    }
}
