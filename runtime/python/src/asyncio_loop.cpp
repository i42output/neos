/*
  asyncio_loop.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/asyncio/protocols.py, Lib/asyncio/transports.py, Lib/asyncio/trsock.py,
  Lib/asyncio/base_events.py, Lib/asyncio/selector_events.py, Lib/asyncio/base_subprocess.py, Lib/asyncio/unix_events.py,
  Lib/asyncio/runners.py and Lib/asyncio/threads.py), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved.
  They are used under the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from
  Python to C++ for the neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// asyncio's modules protocols, transports, trsock, base_events, selector_events, base_subprocess, unix_events, runners and threads (translated from Python by py2neos, a Python-to-C++ translator).

#include "asyncio.hpp"

namespace neos::python_runtime::detail::asyncio
{
    namespace
    {
        // --- protocols (Lib/asyncio/protocols.py)

        void make_protocols()
        {
            static module_writer const& w = writer("protocols");
            define("protocols", "__all__", py_tuple({ S<"BaseProtocol">(), S<"Protocol">(), S<"DatagramProtocol">(), S<"SubprocessProtocol">(), S<"BufferedProtocol">() }));
            define("protocols", "BaseProtocol", w.make_class("BaseProtocol", {  }, {
                { "__slots__", py_tuple() },
                { "connection_made", w.method("BaseProtocol", "connection_made", { py_positional("transport") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport = a[1];
                        return none();
                    }) },
                { "connection_lost", w.method("BaseProtocol", "connection_lost", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        return none();
                    }) },
                { "pause_writing", w.method("BaseProtocol", "pause_writing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "resume_writing", w.method("BaseProtocol", "resume_writing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) }, }));

            define("protocols", "Protocol", w.make_class("Protocol", { G<"protocols.BaseProtocol">() }, {
                { "__slots__", py_tuple() },
                { "data_received", w.method("Protocol", "data_received", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        return none();
                    }) },
                { "eof_received", w.method("Protocol", "eof_received", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) }, }));

            define("protocols", "BufferedProtocol", w.make_class("BufferedProtocol", { G<"protocols.BaseProtocol">() }, {
                { "__slots__", py_tuple() },
                { "get_buffer", w.method("BufferedProtocol", "get_buffer", { py_positional("sizehint") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sizehint = a[1];
                        return none();
                    }) },
                { "buffer_updated", w.method("BufferedProtocol", "buffer_updated", { py_positional("nbytes") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py nbytes = a[1];
                        return none();
                    }) },
                { "eof_received", w.method("BufferedProtocol", "eof_received", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) }, }));

            define("protocols", "DatagramProtocol", w.make_class("DatagramProtocol", { G<"protocols.BaseProtocol">() }, {
                { "__slots__", py_tuple() },
                { "datagram_received", w.method("DatagramProtocol", "datagram_received", { py_positional("data"), py_positional("addr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py addr = a[2];
                        return none();
                    }) },
                { "error_received", w.method("DatagramProtocol", "error_received", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        return none();
                    }) }, }));

            define("protocols", "SubprocessProtocol", w.make_class("SubprocessProtocol", { G<"protocols.BaseProtocol">() }, {
                { "__slots__", py_tuple() },
                { "pipe_data_received", w.method("SubprocessProtocol", "pipe_data_received", { py_positional("fd"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py data = a[2];
                        return none();
                    }) },
                { "pipe_connection_lost", w.method("SubprocessProtocol", "pipe_connection_lost", { py_positional("fd"), py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py exc = a[2];
                        return none();
                    }) },
                { "process_exited", w.method("SubprocessProtocol", "process_exited", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) }, }));

            define("protocols", "_feed_data_to_buffered_proto", w.function("_feed_data_to_buffered_proto", "_feed_data_to_buffered_proto", { py_positional("proto"), py_positional("data") }, [](arguments a) -> py
                {
                    py proto = a[0];
                    py data = a[1];
                    py buf;
                    py buf_len;
                    py data_len;
                    data_len = py{ data.size() };
                    while (data_len.truthy())
                    {
                        buf = proto.method("get_buffer", { data_len });
                        buf_len = py{ buf.size() };
                        if (!buf_len.truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"get_buffer() returned an empty buffer">()));
                        }
                        if (buf_len >= data_len)
                        {
                            buf.set_item(B<"slice">()(none(), data_len, none()), data);
                            proto.method("buffer_updated", { data_len });
                            return none();
                        }
                        else
                        {
                            buf.set_item(B<"slice">()(none(), buf_len, none()), data[B<"slice">()(none(), buf_len, none())]);
                            proto.method("buffer_updated", { buf_len });
                            data = data[B<"slice">()(buf_len, none(), none())];
                            data_len = py{ data.size() };
                        }
                    }
                    return none();
                }));
        }

        // --- transports (Lib/asyncio/transports.py)

        void make_transports()
        {
            static module_writer const& w = writer("transports");
            define("transports", "__all__", py_tuple({ S<"BaseTransport">(), S<"ReadTransport">(), S<"WriteTransport">(), S<"Transport">(), S<"DatagramTransport">(), S<"SubprocessTransport">() }));
            define("transports", "BaseTransport", w.make_class("BaseTransport", {  }, {
                { "__slots__", py_tuple({ S<"_extra">() }) },
                { "__init__", w.method("BaseTransport", "__init__", { py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py extra = a[1];
                        if (extra.is_none())
                        {
                            extra = py_dict();
                        }
                        self.set_attr("_extra", extra);
                        return none();
                    }) },
                { "get_extra_info", w.method("BaseTransport", "get_extra_info", { py_positional("name"), py_positional("default", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py default_ = a[2];
                        return self.attr("_extra").method("get", { name, default_ });
                    }) },
                { "is_closing", w.method("BaseTransport", "is_closing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "close", w.method("BaseTransport", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "set_protocol", w.method("BaseTransport", "set_protocol", { py_positional("protocol") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol = a[1];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "get_protocol", w.method("BaseTransport", "get_protocol", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) }, }));

            define("transports", "ReadTransport", w.make_class("ReadTransport", { G<"transports.BaseTransport">() }, {
                { "__slots__", py_tuple() },
                { "is_reading", w.method("ReadTransport", "is_reading", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "pause_reading", w.method("ReadTransport", "pause_reading", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "resume_reading", w.method("ReadTransport", "resume_reading", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) }, }));

            define("transports", "WriteTransport", w.make_class("WriteTransport", { G<"transports.BaseTransport">() }, {
                { "__slots__", py_tuple() },
                { "set_write_buffer_limits", w.method("WriteTransport", "set_write_buffer_limits", { py_positional("high", none()), py_positional("low", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py high = a[1];
                        py low = a[2];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "get_write_buffer_size", w.method("WriteTransport", "get_write_buffer_size", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "get_write_buffer_limits", w.method("WriteTransport", "get_write_buffer_limits", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "write", w.method("WriteTransport", "write", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "writelines", w.method("WriteTransport", "writelines", { py_positional("list_of_data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py list_of_data = a[1];
                        py data;
                        data = py{ make_bytes(std::string{ "", 0 }) }.method("join", { list_of_data });
                        self.method("write", { data });
                        return none();
                    }) },
                { "write_eof", w.method("WriteTransport", "write_eof", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "can_write_eof", w.method("WriteTransport", "can_write_eof", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "abort", w.method("WriteTransport", "abort", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) }, }));

            define("transports", "Transport", w.make_class("Transport", { G<"transports.ReadTransport">(), G<"transports.WriteTransport">() }, {
                { "__slots__", py_tuple() }, }));

            define("transports", "DatagramTransport", w.make_class("DatagramTransport", { G<"transports.BaseTransport">() }, {
                { "__slots__", py_tuple() },
                { "sendto", w.method("DatagramTransport", "sendto", { py_positional("data"), py_positional("addr", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py addr = a[2];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "abort", w.method("DatagramTransport", "abort", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) }, }));

            define("transports", "SubprocessTransport", w.make_class("SubprocessTransport", { G<"transports.BaseTransport">() }, {
                { "__slots__", py_tuple() },
                { "get_pid", w.method("SubprocessTransport", "get_pid", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "get_returncode", w.method("SubprocessTransport", "get_returncode", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "get_pipe_transport", w.method("SubprocessTransport", "get_pipe_transport", { py_positional("fd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "send_signal", w.method("SubprocessTransport", "send_signal", { py_positional("signal") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py signal = a[1];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "terminate", w.method("SubprocessTransport", "terminate", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "kill", w.method("SubprocessTransport", "kill", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) }, }));

            define("transports", "_FlowControlMixin", w.make_class("_FlowControlMixin", { G<"transports.Transport">() }, {
                { "__slots__", py_tuple({ S<"_loop">(), S<"_protocol_paused">(), S<"_high_water">(), S<"_low_water">() }) },
                { "__init__", w.method("_FlowControlMixin", "__init__", { py_positional("extra", none()), py_positional("loop", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py extra = a[1];
                        py loop = a[2];
                        (B<"super">()(G<"transports._FlowControlMixin">(), self)).method("__init__", { extra });
                        if (loop.is_none())
                            py_raise(B<"AssertionError">()());
                        self.set_attr("_loop", loop);
                        self.set_attr("_protocol_paused", boolean(false));
                        self.method("_set_write_buffer_limits");
                        return none();
                    }) },
                { "_maybe_pause_protocol", w.method("_FlowControlMixin", "_maybe_pause_protocol", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc;
                        py size;
                        size = self.method("get_write_buffer_size");
                        if (size <= self.attr("_high_water"))
                        {
                            return none();
                        }
                        if (!self.attr("_protocol_paused").truthy())
                        {
                            self.set_attr("_protocol_paused", boolean(true));
                            try
                            {
                                self.attr("_protocol").method("pause_writing");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler1;
                                if (matches(handler1.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler1.exception);
                                }
                                else if (matches(handler1.exception, B<"BaseException">()))
                                {
                                    exc = handler1.exception;
                                    self.attr("_loop").method("call_exception_handler", { dict_of({ { S<"message">(), S<"protocol.pause_writing() failed">() }, { S<"exception">(), exc }, { S<"transport">(), self }, { S<"protocol">(), self.attr("_protocol") } }) });
                                }
                                else
                                    py_reraise(handler1.exception);
                            }
                        }
                        return none();
                    }) },
                { "_maybe_resume_protocol", w.method("_FlowControlMixin", "_maybe_resume_protocol", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc;
                        if ((self.attr("_protocol_paused").truthy() && (self.method("get_write_buffer_size") <= self.attr("_low_water"))))
                        {
                            self.set_attr("_protocol_paused", boolean(false));
                            try
                            {
                                self.attr("_protocol").method("resume_writing");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler2;
                                if (matches(handler2.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler2.exception);
                                }
                                else if (matches(handler2.exception, B<"BaseException">()))
                                {
                                    exc = handler2.exception;
                                    self.attr("_loop").method("call_exception_handler", { dict_of({ { S<"message">(), S<"protocol.resume_writing() failed">() }, { S<"exception">(), exc }, { S<"transport">(), self }, { S<"protocol">(), self.attr("_protocol") } }) });
                                }
                                else
                                    py_reraise(handler2.exception);
                            }
                        }
                        return none();
                    }) },
                { "get_write_buffer_limits", w.method("_FlowControlMixin", "get_write_buffer_limits", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ self.attr("_low_water"), self.attr("_high_water") });
                    }) },
                { "_set_write_buffer_limits", w.method("_FlowControlMixin", "_set_write_buffer_limits", { py_positional("high", none()), py_positional("low", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py high = a[1];
                        py low = a[2];
                        if (high.is_none())
                        {
                            if (low.is_none())
                            {
                                high = (I<64>()) * (I<1024>());
                            }
                            else
                            {
                                high = (I<4>()) * low;
                            }
                        }
                        if (low.is_none())
                        {
                            low = floordiv(high, (I<4>()));
                        }
                        if (!((high >= low) && (low >= (I<0>()))))
                        {
                            py_raise((B<"ValueError">())(S(std::string{ "high (" } + high.repr() + ") must be >= low (" + low.repr() + ") must be >= 0")));
                        }
                        self.set_attr("_high_water", high);
                        self.set_attr("_low_water", low);
                        return none();
                    }) },
                { "set_write_buffer_limits", w.method("_FlowControlMixin", "set_write_buffer_limits", { py_positional("high", none()), py_positional("low", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py high = a[1];
                        py low = a[2];
                        self.method("_set_write_buffer_limits", {  }, { { "high", high }, { "low", low } });
                        self.method("_maybe_pause_protocol");
                        return none();
                    }) },
                { "get_write_buffer_size", w.method("_FlowControlMixin", "get_write_buffer_size", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) }, }));
        }

        // --- trsock (Lib/asyncio/trsock.py)

        void make_trsock()
        {
            static module_writer const& w = writer("trsock");
            define("trsock", "TransportSocket", w.make_class("TransportSocket", {  }, {
                { "__slots__", py_tuple({ S<"_sock">() }) },
                { "__init__", w.method("TransportSocket", "__init__", { py_positional("sock") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        self.set_attr("_sock", sock);
                        return none();
                    }) },
                { "family", B<"property">()(w.method("TransportSocket", "family", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_sock").attr("family");
                    })) },
                { "type", B<"property">()(w.method("TransportSocket", "type", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_sock").attr("type");
                    })) },
                { "proto", B<"property">()(w.method("TransportSocket", "proto", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_sock").attr("proto");
                    })) },
                { "__repr__", w.method("TransportSocket", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py laddr;
                        py raddr;
                        py s;
                        s = S(std::string{ "<asyncio.TransportSocket fd=" } + self.method("fileno").str() + ", family=" + self.attr("family").str() + ", type=" + self.attr("type").str() + ", proto=" + self.attr("proto").str());
                        if (self.method("fileno") != (I<-1>()))
                        {
                            try
                            {
                                laddr = self.method("getsockname");
                                if (laddr.truthy())
                                {
                                    s = S(s.str() + ", laddr=" + laddr.str());
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler1;
                                if (matches(handler1.exception, Mod<"socket">().attr("error")))
                                {
                                }
                                else
                                    py_reraise(handler1.exception);
                            }
                            try
                            {
                                raddr = self.method("getpeername");
                                if (raddr.truthy())
                                {
                                    s = S(s.str() + ", raddr=" + raddr.str());
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler2;
                                if (matches(handler2.exception, Mod<"socket">().attr("error")))
                                {
                                }
                                else
                                    py_reraise(handler2.exception);
                            }
                        }
                        return S(s.str() + ">");
                    }) },
                { "__getstate__", w.method("TransportSocket", "__getstate__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"TypeError">())(S<"Cannot serialize asyncio.TransportSocket object">()));
                    }) },
                { "fileno", w.method("TransportSocket", "fileno", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_sock").method("fileno");
                    }) },
                { "dup", w.method("TransportSocket", "dup", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_sock").method("dup");
                    }) },
                { "get_inheritable", w.method("TransportSocket", "get_inheritable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_sock").method("get_inheritable");
                    }) },
                { "shutdown", w.method("TransportSocket", "shutdown", { py_positional("how") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py how = a[1];
                        self.attr("_sock").method("shutdown", { how });
                        return none();
                    }) },
                { "getsockopt", w.method("TransportSocket", "getsockopt", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        return call_ex(self.attr("_sock").attr("getsockopt"), {  }, args, kwargs);
                    }, true, true) },
                { "setsockopt", w.method("TransportSocket", "setsockopt", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        call_ex(self.attr("_sock").attr("setsockopt"), {  }, args, kwargs);
                        return none();
                    }, true, true) },
                { "getpeername", w.method("TransportSocket", "getpeername", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_sock").method("getpeername");
                    }) },
                { "getsockname", w.method("TransportSocket", "getsockname", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_sock").method("getsockname");
                    }) },
                { "getsockbyname", w.method("TransportSocket", "getsockbyname", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_sock").method("getsockbyname");
                    }) },
                { "settimeout", w.method("TransportSocket", "settimeout", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        if (value == (I<0>()))
                        {
                            return none();
                        }
                        py_raise((B<"ValueError">())(S<"settimeout(): only 0 timeout is allowed on transport sockets">()));
                    }) },
                { "gettimeout", w.method("TransportSocket", "gettimeout", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return I<0>();
                    }) },
                { "setblocking", w.method("TransportSocket", "setblocking", { py_positional("flag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py flag = a[1];
                        if (!flag.truthy())
                        {
                            return none();
                        }
                        py_raise((B<"ValueError">())(S<"setblocking(): transport sockets cannot be blocking">()));
                    }) }, }));
        }

        // --- base_events (Lib/asyncio/base_events.py)

        void make_base_events()
        {
            static module_writer const& w = writer("base_events");
            define("base_events", "__all__", py_tuple({ S<"BaseEventLoop">(), S<"Server">() }));
            // Minimum number of _scheduled timer handles before cleanup of
            // cancelled handles is performed.
            define("base_events", "_MIN_SCHEDULED_TIMER_HANDLES", I<100>());
            // Minimum fraction of _scheduled timer handles that are cancelled
            // before cleanup of cancelled handles is performed.
            define("base_events", "_MIN_CANCELLED_TIMER_HANDLES_FRACTION", F<0.5>());
            define("base_events", "_HAS_IPv6", (B<"hasattr">())(Mod<"socket">(), S<"AF_INET6">()));
            // Maximum timeout passed to select to avoid OS limitations
            define("base_events", "MAXIMUM_SELECT_TIMEOUT", (I<24>()) * (I<3600>()));
            define("base_events", "_format_handle", w.function("_format_handle", "_format_handle", { py_positional("handle") }, [](arguments a) -> py
                {
                    py handle = a[0];
                    py cb;
                    cb = handle.attr("_callback");
                    if (isinstance((B<"getattr">())(cb, S<"__self__">(), none()), G<"tasks.Task">()))
                    {
                        // format the task
                        return S(cb.attr("__self__").repr());
                    }
                    else
                    {
                        return (B<"str">())(handle);
                    }
                    return none();
                }));

            define("base_events", "_format_pipe", w.function("_format_pipe", "_format_pipe", { py_positional("fd") }, [](arguments a) -> py
                {
                    py fd = a[0];
                    if (fd == (py{ -1 } /* subprocess.PIPE */))
                    {
                        return S<"<pipe>">();
                    }
                    else if (fd == (py{ -2 } /* subprocess.STDOUT */))
                    {
                        return S<"<stdout>">();
                    }
                    else
                    {
                        return S(fd.repr());
                    }
                    return none();
                }));

            define("base_events", "_set_reuseport", w.function("_set_reuseport", "_set_reuseport", { py_positional("sock") }, [](arguments a) -> py
                {
                    py sock = a[0];
                    if (!has_attr(Mod<"socket">(), "SO_REUSEPORT"))
                    {
                        py_raise((B<"ValueError">())(S<"reuse_port not supported by socket module">()));
                    }
                    else
                    {
                        try
                        {
                            sock.method("setsockopt", { Mod<"socket">().attr("SOL_SOCKET"), Mod<"socket">().attr("SO_REUSEPORT"), I<1>() });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            if (matches(handler1.exception, B<"OSError">()))
                            {
                                py_raise((B<"ValueError">())(S<"reuse_port not supported by socket module, SO_REUSEPORT defined but not implemented.">()));
                            }
                            else
                                py_reraise(handler1.exception);
                        }
                    }
                    return none();
                }));

            define("base_events", "_ipaddr_info", w.function("_ipaddr_info", "_ipaddr_info", { py_positional("host"), py_positional("port"), py_positional("family"), py_positional("type"), py_positional("proto"), py_positional("flowinfo", I<0>()), py_positional("scopeid", I<0>()) }, [](arguments a) -> py
                {
                    py host = a[0];
                    py port = a[1];
                    py family = a[2];
                    py type = a[3];
                    py proto = a[4];
                    py flowinfo = a[5];
                    py scopeid = a[6];
                    py af;
                    py afs;
                    if (!has_attr(Mod<"socket">(), "inet_pton"))
                    {
                        return none();
                    }
                    if (((!set_of({ I<0>(), Mod<"socket">().attr("IPPROTO_TCP"), Mod<"socket">().attr("IPPROTO_UDP") }).contains(proto)) || host.is_none()))
                    {
                        return none();
                    }
                    if (type == (Mod<"socket">().attr("SOCK_STREAM")))
                    {
                        proto = Mod<"socket">().attr("IPPROTO_TCP");
                    }
                    else if (type == (Mod<"socket">().attr("SOCK_DGRAM")))
                    {
                        proto = Mod<"socket">().attr("IPPROTO_UDP");
                    }
                    else
                    {
                        return none();
                    }
                    if (port.is_none())
                    {
                        port = I<0>();
                    }
                    else if ((isinstance(port, B<"bytes">()) && (port == py{ make_bytes(std::string{ "", 0 }) })))
                    {
                        port = I<0>();
                    }
                    else if ((isinstance(port, B<"str">()) && (port == (S<"">()))))
                    {
                        port = I<0>();
                    }
                    else
                    {
                        // If port's a service name like "http", don't skip getaddrinfo.
                        try
                        {
                            port = (B<"int">())(port);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler2;
                            if (matches(handler2.exception, py_tuple({ B<"TypeError">(), B<"ValueError">() })))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler2.exception);
                        }
                    }
                    if (family == (Mod<"socket">().attr("AF_UNSPEC")))
                    {
                        afs = py_list({ Mod<"socket">().attr("AF_INET") });
                        if ((G<"base_events._HAS_IPv6">()).truthy())
                        {
                            afs.method("append", { Mod<"socket">().attr("AF_INET6") });
                        }
                    }
                    else
                    {
                        afs = py_list({ family });
                    }
                    if (isinstance(host, B<"bytes">()))
                    {
                        host = host.method("decode", { S<"idna">() });
                    }
                    if (host.contains((S<"%">())))
                    {
                        // Linux's inet_pton doesn't accept an IPv6 zone index after host,
                        // like '::1%lo0'.
                        return none();
                    }
                    for (py const& item : iterate{ afs })
                    {
                        af = item;
                        try
                        {
                            (Mod<"socket">().attr("inet_pton"))(af, host);
                            // The host has already been resolved.
                            if (((G<"base_events._HAS_IPv6">()).truthy() && (af == (Mod<"socket">().attr("AF_INET6")))))
                            {
                                return py_tuple({ af, type, proto, S<"">(), py_tuple({ host, port, flowinfo, scopeid }) });
                            }
                            else
                            {
                                return py_tuple({ af, type, proto, S<"">(), py_tuple({ host, port }) });
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler4;
                            if (matches(handler4.exception, B<"OSError">()))
                            {
                            }
                            else
                                py_reraise(handler4.exception);
                        }
                    }
                    // "host" is not an IP address.
                    return none();
                }));

            define("base_events", "_interleave_addrinfos", w.function("_interleave_addrinfos", "_interleave_addrinfos", { py_positional("addrinfos"), py_positional("first_address_family_count", I<1>()) }, [](arguments a) -> py
                {
                    py addrinfos = a[0];
                    py first_address_family_count = a[1];
                    py addr;
                    py addrinfos_by_family;
                    py addrinfos_lists;
                    py family;
                    py reordered;
                    // Group addresses by family
                    addrinfos_by_family = (Mod<"collections">().attr("OrderedDict"))();
                    for (py const& item : iterate{ addrinfos })
                    {
                        addr = item;
                        family = addr[I<0>()];
                        if (!addrinfos_by_family.contains(family))
                        {
                            addrinfos_by_family.set_item(family, py_list());
                        }
                        addrinfos_by_family[family].method("append", { addr });
                    }
                    addrinfos_lists = (B<"list">())(addrinfos_by_family.method("values"));
                    reordered = py_list();
                    if (first_address_family_count > (I<1>()))
                    {
                        reordered.method("extend", { addrinfos_lists[I<0>()][B<"slice">()(none(), first_address_family_count - (I<1>()), none())] });
                        del_item(addrinfos_lists[I<0>()], B<"slice">()(none(), first_address_family_count - (I<1>()), none()));
                    }
                    reordered.method("extend", { [&]() -> py { py const result = py_list(); py a_; for (py const& item : iterate{ (Mod<"itertools">().attr("chain")).method("from_iterable", { call_ex((Mod<"itertools">().attr("zip_longest")), {  }, addrinfos_lists, py{}) }) }) { a_ = item; if (a_.is_none()) continue; result.method("append", { a_ }); } return result; }() });
                    return reordered;
                }));

            define("base_events", "_run_until_complete_cb", w.function("_run_until_complete_cb", "_run_until_complete_cb", { py_positional("fut") }, [](arguments a) -> py
                {
                    py fut = a[0];
                    py exc;
                    if (!fut.method("cancelled").truthy())
                    {
                        exc = fut.method("exception");
                        if (isinstance(exc, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                        {
                            // Issue #22429: run_forever() already finished, no need to
                            // stop it.
                            return none();
                        }
                    }
                    (G<"futures._get_loop">())(fut).method("stop");
                    return none();
                }));

            if ((B<"hasattr">())(Mod<"socket">(), S<"TCP_NODELAY">()).truthy())
            {
                define("base_events", "_set_nodelay", w.function("_set_nodelay", "_set_nodelay", { py_positional("sock") }, [](arguments a) -> py
                    {
                        py sock = a[0];
                        if ((set_of({ Mod<"socket">().attr("AF_INET"), Mod<"socket">().attr("AF_INET6") }).contains(sock.attr("family")) && (sock.attr("type") == (Mod<"socket">().attr("SOCK_STREAM"))) && (sock.attr("proto") == (Mod<"socket">().attr("IPPROTO_TCP")))))
                        {
                            sock.method("setsockopt", { Mod<"socket">().attr("IPPROTO_TCP"), Mod<"socket">().attr("TCP_NODELAY"), I<1>() });
                        }
                        return none();
                    }));

            }
            else
            {
                define("base_events", "_set_nodelay", w.function("_set_nodelay", "_set_nodelay", { py_positional("sock") }, [](arguments a) -> py
                    {
                        py sock = a[0];
                        return none();
                    }));

            }
            define("base_events", "_check_ssl_socket", w.function("_check_ssl_socket", "_check_ssl_socket", { py_positional("sock") }, [](arguments a) -> py
                {
                    py sock = a[0];
                    if (((!none().is_none()) && isinstance(sock, none().attr("SSLSocket"))))
                    {
                        py_raise((B<"TypeError">())(S<"Socket cannot be of type SSLSocket">()));
                    }
                    return none();
                }));

            define("base_events", "_SendfileFallbackProtocol", w.make_class("_SendfileFallbackProtocol", { G<"protocols.Protocol">() }, {
                { "__init__", w.method("_SendfileFallbackProtocol", "__init__", { py_positional("transp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transp = a[1];
                        if (!isinstance(transp, G<"transports._FlowControlMixin">()))
                        {
                            py_raise((B<"TypeError">())(S<"transport should be _FlowControlMixin instance">()));
                        }
                        self.set_attr("_transport", transp);
                        self.set_attr("_proto", transp.method("get_protocol"));
                        self.set_attr("_should_resume_reading", transp.method("is_reading"));
                        self.set_attr("_should_resume_writing", transp.attr("_protocol_paused"));
                        transp.method("pause_reading");
                        transp.method("set_protocol", { self });
                        if (self.attr("_should_resume_writing").truthy())
                        {
                            self.set_attr("_write_ready_fut", self.attr("_transport").attr("_loop").method("create_future"));
                        }
                        else
                        {
                            self.set_attr("_write_ready_fut", none());
                        }
                        return none();
                    }) },
                { "drain", async_method(w, "_SendfileFallbackProtocol", "drain", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut;
                        if (self.attr("_transport").method("is_closing").truthy())
                        {
                            py_raise((B<"ConnectionError">())(S<"Connection closed by peer">()));
                        }
                        fut = self.attr("_write_ready_fut");
                        if (fut.is_none())
                        {
                            return none();
                        }
                        py_await(fut);
                        return none();
                    }) },
                { "connection_made", w.method("_SendfileFallbackProtocol", "connection_made", { py_positional("transport") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport = a[1];
                        py_raise((B<"RuntimeError">())(S<"Invalid state: connection should have been established already.">()));
                    }) },
                { "connection_lost", w.method("_SendfileFallbackProtocol", "connection_lost", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        if (!self.attr("_write_ready_fut").is_none())
                        {
                            // Never happens if peer disconnects after sending the whole content
                            // Thus disconnection is always an exception from user perspective
                            if (exc.is_none())
                            {
                                self.attr("_write_ready_fut").method("set_exception", { (B<"ConnectionError">())(S<"Connection is closed by peer">()) });
                            }
                            else
                            {
                                self.attr("_write_ready_fut").method("set_exception", { exc });
                            }
                        }
                        self.attr("_proto").method("connection_lost", { exc });
                        return none();
                    }) },
                { "pause_writing", w.method("_SendfileFallbackProtocol", "pause_writing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_write_ready_fut").is_none())
                        {
                            return none();
                        }
                        self.set_attr("_write_ready_fut", self.attr("_transport").attr("_loop").method("create_future"));
                        return none();
                    }) },
                { "resume_writing", w.method("_SendfileFallbackProtocol", "resume_writing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_write_ready_fut").is_none())
                        {
                            return none();
                        }
                        self.attr("_write_ready_fut").method("set_result", { boolean(false) });
                        self.set_attr("_write_ready_fut", none());
                        return none();
                    }) },
                { "data_received", w.method("_SendfileFallbackProtocol", "data_received", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py_raise((B<"RuntimeError">())(S<"Invalid state: reading should be paused">()));
                    }) },
                { "eof_received", w.method("_SendfileFallbackProtocol", "eof_received", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"RuntimeError">())(S<"Invalid state: reading should be paused">()));
                    }) },
                { "restore", async_method(w, "_SendfileFallbackProtocol", "restore", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_transport").method("set_protocol", { self.attr("_proto") });
                        if (self.attr("_should_resume_reading").truthy())
                        {
                            self.attr("_transport").method("resume_reading");
                        }
                        if (!self.attr("_write_ready_fut").is_none())
                        {
                            // Cancel the future.
                            // Basically it has no effect because protocol is switched back,
                            // no code should wait for it anymore.
                            self.attr("_write_ready_fut").method("cancel");
                        }
                        if (self.attr("_should_resume_writing").truthy())
                        {
                            self.attr("_proto").method("resume_writing");
                        }
                        return none();
                    }) }, }));

            define("base_events", "Server", w.make_class("Server", { G<"events.AbstractServer">() }, {
                { "__init__", w.method("Server", "__init__", { py_positional("loop"), py_positional("sockets"), py_positional("protocol_factory"), py_positional("ssl_context"), py_positional("backlog"), py_positional("ssl_handshake_timeout"), py_positional("ssl_shutdown_timeout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        py sockets = a[2];
                        py protocol_factory = a[3];
                        py ssl_context = a[4];
                        py backlog = a[5];
                        py ssl_handshake_timeout = a[6];
                        py ssl_shutdown_timeout = a[7];
                        self.set_attr("_loop", loop);
                        self.set_attr("_sockets", sockets);
                        // Weak references so we don't break Transport's ability to
                        // detect abandoned transports
                        self.set_attr("_clients", (Mod<"weakref">().attr("WeakSet"))());
                        self.set_attr("_waiters", py_list());
                        self.set_attr("_protocol_factory", protocol_factory);
                        self.set_attr("_backlog", backlog);
                        self.set_attr("_ssl_context", ssl_context);
                        self.set_attr("_ssl_handshake_timeout", ssl_handshake_timeout);
                        self.set_attr("_ssl_shutdown_timeout", ssl_shutdown_timeout);
                        self.set_attr("_serving", boolean(false));
                        self.set_attr("_serving_forever_fut", none());
                        return none();
                    }) },
                { "__repr__", w.method("Server", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S(std::string{ "<" } + self.attr("__class__").attr("__name__").str() + " sockets=" + self.attr("sockets").repr() + ">");
                    }) },
                { "_attach", w.method("Server", "_attach", { py_positional("transport") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport = a[1];
                        if (self.attr("_sockets").is_none())
                            py_raise(B<"AssertionError">()());
                        self.attr("_clients").method("add", { transport });
                        return none();
                    }) },
                { "_detach", w.method("Server", "_detach", { py_positional("transport") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport = a[1];
                        self.attr("_clients").method("discard", { transport });
                        if (((py{ self.attr("_clients").size() } == (I<0>())) && self.attr("_sockets").is_none()))
                        {
                            self.method("_wakeup");
                        }
                        return none();
                    }) },
                { "_wakeup", w.method("Server", "_wakeup", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py waiter;
                        py waiters;
                        waiters = self.attr("_waiters");
                        self.set_attr("_waiters", none());
                        for (py const& item : iterate{ waiters })
                        {
                            waiter = item;
                            if (!waiter.method("done").truthy())
                            {
                                waiter.method("set_result", { none() });
                            }
                        }
                        return none();
                    }) },
                { "_start_serving", w.method("Server", "_start_serving", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock;
                        if (self.attr("_serving").truthy())
                        {
                            return none();
                        }
                        self.set_attr("_serving", boolean(true));
                        for (py const& item : iterate{ self.attr("_sockets") })
                        {
                            sock = item;
                            sock.method("listen", { self.attr("_backlog") });
                            self.attr("_loop").method("_start_serving", { self.attr("_protocol_factory"), sock, self.attr("_ssl_context"), self, self.attr("_backlog"), self.attr("_ssl_handshake_timeout"), self.attr("_ssl_shutdown_timeout") });
                        }
                        return none();
                    }) },
                { "get_loop", w.method("Server", "get_loop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_loop");
                    }) },
                { "is_serving", w.method("Server", "is_serving", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_serving");
                    }) },
                { "sockets", B<"property">()(w.method("Server", "sockets", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_sockets").is_none())
                        {
                            return py_tuple();
                        }
                        return (B<"tuple">())([&]() -> py { py const result = py_list(); py s; for (py const& item : iterate{ self.attr("_sockets") }) { s = item; result.method("append", { (G<"trsock.TransportSocket">())(s) }); } return result; }());
                    })) },
                { "close", w.method("Server", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock;
                        py sockets;
                        sockets = self.attr("_sockets");
                        if (sockets.is_none())
                        {
                            return none();
                        }
                        self.set_attr("_sockets", none());
                        for (py const& item : iterate{ sockets })
                        {
                            sock = item;
                            self.attr("_loop").method("_stop_serving", { sock });
                        }
                        self.set_attr("_serving", boolean(false));
                        if (((!self.attr("_serving_forever_fut").is_none()) && (!self.attr("_serving_forever_fut").method("done").truthy())))
                        {
                            self.attr("_serving_forever_fut").method("cancel");
                            self.set_attr("_serving_forever_fut", none());
                        }
                        if (py{ self.attr("_clients").size() } == (I<0>()))
                        {
                            self.method("_wakeup");
                        }
                        return none();
                    }) },
                { "close_clients", w.method("Server", "close_clients", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport;
                        for (py const& item : iterate{ self.attr("_clients").method("copy") })
                        {
                            transport = item;
                            transport.method("close");
                        }
                        return none();
                    }) },
                { "abort_clients", w.method("Server", "abort_clients", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport;
                        for (py const& item : iterate{ self.attr("_clients").method("copy") })
                        {
                            transport = item;
                            transport.method("abort");
                        }
                        return none();
                    }) },
                { "start_serving", async_method(w, "Server", "start_serving", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_start_serving");
                        // Skip one loop iteration so that all 'loop.add_reader'
                        // go through.
                        py_await((G<"tasks.sleep">())(I<0>()));
                        return none();
                    }) },
                { "serve_forever", async_method(w, "Server", "serve_forever", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_serving_forever_fut").is_none())
                        {
                            py_raise((B<"RuntimeError">())(S(std::string{ "server " } + self.repr() + " is already being awaited on serve_forever()")));
                        }
                        if (self.attr("_sockets").is_none())
                        {
                            py_raise((B<"RuntimeError">())(S(std::string{ "server " } + self.repr() + " is closed")));
                        }
                        self.method("_start_serving");
                        self.set_attr("_serving_forever_fut", self.attr("_loop").method("create_future"));
                        {
                            std::optional<py> pending11;
                            [[maybe_unused]] int flow11 = 0;
                            [[maybe_unused]] py result11;
                            try
                            {
                                try
                                {
                                    py_await(self.attr("_serving_forever_fut"));
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler11;
                                    if (matches(handler11.exception, G<"exceptions.CancelledError">()))
                                    {
                                        {
                                            std::optional<py> pending12;
                                            [[maybe_unused]] int flow12 = 0;
                                            [[maybe_unused]] py result12;
                                            try
                                            {
                                                self.method("close");
                                                self.method("close_clients");
                                                py_await(self.method("wait_closed"));
                                            }
                                            catch (raised const&)
                                            {
                                                pending12 = py_take_exception();
                                            }
                                            py_reraise(handler11.exception);
                                            if (pending12)
                                                py_reraise(*pending12);
                                            if (flow12 == 1)
                                                { result11 = result12; flow11 = 1; goto finally11; }
                                        }
                                    }
                                    else
                                        py_reraise(handler11.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pending11 = py_take_exception();
                            }
                            finally11:
                            self.set_attr("_serving_forever_fut", none());
                            if (pending11)
                                py_reraise(*pending11);
                            if (flow11 == 1)
                                return result11;
                        }
                        return none();
                    }) },
                { "wait_closed", async_method(w, "Server", "wait_closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py waiter;
                        // Waiters are unblocked by self._wakeup(), which is called
                        // from two places: self.close() and self._detach(), but only
                        // when both conditions have become true. To signal that this
                        // has happened, self._wakeup() sets self._waiters to None.
                        if (self.attr("_waiters").is_none())
                        {
                            return none();
                        }
                        waiter = self.attr("_loop").method("create_future");
                        self.attr("_waiters").method("append", { waiter });
                        py_await(waiter);
                        return none();
                    }) }, }));

            define("base_events", "BaseEventLoop", w.make_class("BaseEventLoop", { G<"events.AbstractEventLoop">() }, {
                { "__init__", w.method("BaseEventLoop", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_timer_cancelled_count", I<0>());
                        self.set_attr("_closed", boolean(false));
                        self.set_attr("_stopping", boolean(false));
                        self.set_attr("_ready", (Mod<"collections">().attr("deque"))());
                        self.set_attr("_scheduled", py_list());
                        self.set_attr("_default_executor", none());
                        self.set_attr("_internal_fds", I<0>());
                        // Identifier of the thread running the event loop, or None if the
                        // event loop is not running
                        self.set_attr("_thread_id", none());
                        self.set_attr("_clock_resolution", (Mod<"time">().attr("get_clock_info"))(S<"monotonic">()).attr("resolution"));
                        self.set_attr("_exception_handler", none());
                        self.method("set_debug", { (G<"coroutines._is_debug_mode">())() });
                        // The preserved state of async generator hooks.
                        self.set_attr("_old_agen_hooks", none());
                        // In debug mode, if the execution of a callback or a step of a task
                        // exceed this duration in seconds, the slow callback/task is logged.
                        self.set_attr("slow_callback_duration", F<0.1>());
                        self.set_attr("_current_handle", none());
                        self.set_attr("_task_factory", none());
                        self.set_attr("_coroutine_origin_tracking_enabled", boolean(false));
                        self.set_attr("_coroutine_origin_tracking_saved_depth", none());
                        // A weak set of all asynchronous generators that are
                        // being iterated by the loop.
                        self.set_attr("_asyncgens", (Mod<"weakref">().attr("WeakSet"))());
                        // Set to True when `loop.shutdown_asyncgens` is called.
                        self.set_attr("_asyncgens_shutdown_called", boolean(false));
                        // Set to True when `loop.shutdown_default_executor` is called.
                        self.set_attr("_executor_shutdown_called", boolean(false));
                        return none();
                    }) },
                { "__repr__", w.method("BaseEventLoop", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S(std::string{ "<" } + self.attr("__class__").attr("__name__").str() + " running=" + self.method("is_running").str() + " closed=" + self.method("is_closed").str() + " debug=" + self.method("get_debug").str() + ">");
                    }) },
                { "create_future", w.method("BaseEventLoop", "create_future", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"futures.Future">()).call({  }, { { "loop", self } });
                    }) },
                { "create_task", w.method("BaseEventLoop", "create_task", { py_positional("coro"), py_keyword("name", none()), py_keyword("context", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py coro = a[1];
                        py name = a[2];
                        py context = a[3];
                        py kwargs = a[4];
                        py task;
                        self.method("_check_closed");
                        if (!self.attr("_task_factory").is_none())
                        {
                            if (!context.is_none())
                            {
                                kwargs.set_item(S<"context">(), context);
                            }
                            task = call_ex(self.attr("_task_factory"), { self, coro }, py{}, kwargs);
                            task.method("set_name", { name });
                        }
                        else
                        {
                            task = call_ex((G<"tasks.Task">()), { coro }, py{}, kwargs, { { "loop", self }, { "name", name }, { "context", context } });
                            // (del task._source_traceback[-1]: the translated function has no frame)
                        }
                        {
                            std::optional<py> pending13;
                            [[maybe_unused]] int flow13 = 0;
                            [[maybe_unused]] py result13;
                            try
                            {
                                result13 = task;
                                flow13 = 1;
                                goto finally13;
                            }
                            catch (raised const&)
                            {
                                pending13 = py_take_exception();
                            }
                            finally13:
                            // gh-128552: prevent a refcycle of
                            // task.exception().__traceback__->BaseEventLoop.create_task->task
                            task = none();
                            if (pending13)
                                py_reraise(*pending13);
                            if (flow13 == 1)
                                return result13;
                        }
                        return none();
                    }, false, true) },
                { "set_task_factory", w.method("BaseEventLoop", "set_task_factory", { py_positional("factory") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py factory = a[1];
                        if (((!factory.is_none()) && (!callable(factory))))
                        {
                            py_raise((B<"TypeError">())(S<"task factory must be a callable or None">()));
                        }
                        self.set_attr("_task_factory", factory);
                        return none();
                    }) },
                { "get_task_factory", w.method("BaseEventLoop", "get_task_factory", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_task_factory");
                    }) },
                { "_make_socket_transport", w.method("BaseEventLoop", "_make_socket_transport", { py_positional("sock"), py_positional("protocol"), py_positional("waiter", none()), py_keyword("extra", none()), py_keyword("server", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py protocol = a[2];
                        py waiter = a[3];
                        py extra = a[4];
                        py server = a[5];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "_make_ssl_transport", w.method("BaseEventLoop", "_make_ssl_transport", { py_positional("rawsock"), py_positional("protocol"), py_positional("sslcontext"), py_positional("waiter", none()), py_keyword("server_side", boolean(false)), py_keyword("server_hostname", none()), py_keyword("extra", none()), py_keyword("server", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()), py_keyword("call_connection_made", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py rawsock = a[1];
                        py protocol = a[2];
                        py sslcontext = a[3];
                        py waiter = a[4];
                        py server_side = a[5];
                        py server_hostname = a[6];
                        py extra = a[7];
                        py server = a[8];
                        py ssl_handshake_timeout = a[9];
                        py ssl_shutdown_timeout = a[10];
                        py call_connection_made = a[11];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "_make_datagram_transport", w.method("BaseEventLoop", "_make_datagram_transport", { py_positional("sock"), py_positional("protocol"), py_positional("address", none()), py_positional("waiter", none()), py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py protocol = a[2];
                        py address = a[3];
                        py waiter = a[4];
                        py extra = a[5];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "_make_read_pipe_transport", w.method("BaseEventLoop", "_make_read_pipe_transport", { py_positional("pipe"), py_positional("protocol"), py_positional("waiter", none()), py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pipe = a[1];
                        py protocol = a[2];
                        py waiter = a[3];
                        py extra = a[4];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "_make_write_pipe_transport", w.method("BaseEventLoop", "_make_write_pipe_transport", { py_positional("pipe"), py_positional("protocol"), py_positional("waiter", none()), py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pipe = a[1];
                        py protocol = a[2];
                        py waiter = a[3];
                        py extra = a[4];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "_make_subprocess_transport", async_method(w, "BaseEventLoop", "_make_subprocess_transport", { py_positional("protocol"), py_positional("args"), py_positional("shell"), py_positional("stdin"), py_positional("stdout"), py_positional("stderr"), py_positional("bufsize"), py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol = a[1];
                        py args = a[2];
                        py shell = a[3];
                        py stdin_ = a[4];
                        py stdout_ = a[5];
                        py stderr_ = a[6];
                        py bufsize = a[7];
                        py extra = a[8];
                        py kwargs = a[9];
                        py_raise(B<"NotImplementedError">());
                    }, false, true) },
                { "_write_to_self", w.method("BaseEventLoop", "_write_to_self", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "_process_events", w.method("BaseEventLoop", "_process_events", { py_positional("event_list") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py event_list = a[1];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "_check_closed", w.method("BaseEventLoop", "_check_closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_closed").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Event loop is closed">()));
                        }
                        return none();
                    }) },
                { "_check_default_executor", w.method("BaseEventLoop", "_check_default_executor", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_executor_shutdown_called").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Executor shutdown has been called">()));
                        }
                        return none();
                    }) },
                { "_asyncgen_finalizer_hook", w.method("BaseEventLoop", "_asyncgen_finalizer_hook", { py_positional("agen") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py agen = a[1];
                        self.attr("_asyncgens").method("discard", { agen });
                        if (!self.method("is_closed").truthy())
                        {
                            self.method("call_soon_threadsafe", { self.attr("create_task"), agen.method("aclose") });
                        }
                        return none();
                    }) },
                { "_asyncgen_firstiter_hook", w.method("BaseEventLoop", "_asyncgen_firstiter_hook", { py_positional("agen") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py agen = a[1];
                        if (self.attr("_asyncgens_shutdown_called").truthy())
                        {
                            (Mod<"warnings">().attr("warn")).call({ S(std::string{ "asynchronous generator " } + agen.repr() + " was scheduled after loop.shutdown_asyncgens() call"), B<"ResourceWarning">() }, { { "source", self } });
                        }
                        self.attr("_asyncgens").method("add", { agen });
                        return none();
                    }) },
                { "shutdown_asyncgens", async_method(w, "BaseEventLoop", "shutdown_asyncgens", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py agen;
                        py closing_agens;
                        py result;
                        py results;
                        self.set_attr("_asyncgens_shutdown_called", boolean(true));
                        if (!py{ self.attr("_asyncgens").size() }.truthy())
                        {
                            // If Python version is <3.6 or we don't have any asynchronous
                            // generators alive.
                            return none();
                        }
                        closing_agens = (B<"list">())(self.attr("_asyncgens"));
                        self.attr("_asyncgens").method("clear");
                        results = py_await(call_ex((G<"tasks.gather">()), {  }, [&]() -> py { py const result = py_list(); py ag; for (py const& item : iterate{ closing_agens }) { ag = item; result.method("append", { ag.method("aclose") }); } return result; }(), py{}, { { "return_exceptions", boolean(true) } }));
                        for (py const& item : iterate{ (B<"zip">())(results, closing_agens) })
                        {
                            auto const items15 = unpack(item, 2);
                            result = items15[0];
                            agen = items15[1];
                            if (isinstance(result, B<"Exception">()))
                            {
                                self.method("call_exception_handler", { dict_of({ { S<"message">(), S(std::string{ "an error occurred during closing of asynchronous generator " } + agen.repr()) }, { S<"exception">(), result }, { S<"asyncgen">(), agen } }) });
                            }
                        }
                        return none();
                    }) },
                { "shutdown_default_executor", async_method(w, "BaseEventLoop", "shutdown_default_executor", { py_positional("timeout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py timeout = a[1];
                        py future;
                        py thread;
                        self.set_attr("_executor_shutdown_called", boolean(true));
                        if (self.attr("_default_executor").is_none())
                        {
                            return none();
                        }
                        future = self.method("create_future");
                        thread = (Mod<"threading">().attr("Thread")).call({  }, { { "target", self.attr("_do_shutdown") }, { "args", py_tuple({ future }) } });
                        thread.method("start");
                        {
                            bool otherwise16 = false;
                            try
                            {
                                {
                                    py const manager17 = (G<"timeouts.timeout">())(timeout);
                                    py const exit17 = pyclass::special_method(manager17, "__aexit__");
                                    py const value17 = py_await(pyclass::special_method(manager17, "__aenter__")(manager17));
                                    [[maybe_unused]] int flow17 = 0;
                                    [[maybe_unused]] py result17;
                                    bool exited17 = false;
                                    try
                                    {
                                        py_await(future);
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler17;
                                        exited17 = true;
                                        if (!py_await(exit17(manager17, type_of(handler17.exception), handler17.exception, handler17.exception.attr("__traceback__"))).truthy())
                                            py_reraise(handler17.exception);
                                    }
                                    if (!exited17)
                                        py_await(exit17(manager17, none(), none(), none()));
                                    if (flow17 == 1)
                                        return result17;
                                }
                                otherwise16 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler16;
                                if (matches(handler16.exception, B<"TimeoutError">()))
                                {
                                    (Mod<"warnings">().attr("warn")).call({ S(std::string{ "The executor did not finishing joining its threads within " } + timeout.str() + " seconds."), B<"RuntimeWarning">() }, { { "stacklevel", I<1>() } });
                                    self.attr("_default_executor").method("shutdown", {  }, { { "wait", boolean(false) } });
                                }
                                else
                                    py_reraise(handler16.exception);
                            }
                            if (otherwise16)
                            {
                                thread.method("join");
                            }
                        }
                        return none();
                    }) },
                { "_do_shutdown", w.method("BaseEventLoop", "_do_shutdown", { py_positional("future") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py future = a[1];
                        py ex;
                        try
                        {
                            self.attr("_default_executor").method("shutdown", {  }, { { "wait", boolean(true) } });
                            if (!self.method("is_closed").truthy())
                            {
                                self.method("call_soon_threadsafe", { G<"futures._set_result_unless_cancelled">(), future, none() });
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler18;
                            if (matches(handler18.exception, B<"Exception">()))
                            {
                                ex = handler18.exception;
                                if (((!self.method("is_closed").truthy()) && (!future.method("cancelled").truthy())))
                                {
                                    self.method("call_soon_threadsafe", { future.attr("set_exception"), ex });
                                }
                            }
                            else
                                py_reraise(handler18.exception);
                        }
                        return none();
                    }) },
                { "_check_running", w.method("BaseEventLoop", "_check_running", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.method("is_running").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"This event loop is already running">()));
                        }
                        if (!(G<"events._get_running_loop">())().is_none())
                        {
                            py_raise((B<"RuntimeError">())(S<"Cannot run the event loop while another loop is running">()));
                        }
                        return none();
                    }) },
                { "_run_forever_setup", w.method("BaseEventLoop", "_run_forever_setup", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_check_closed");
                        self.method("_check_running");
                        self.method("_set_coroutine_origin_tracking", { self.attr("_debug") });
                        self.set_attr("_old_agen_hooks", (Mod<"sys">().attr("get_asyncgen_hooks"))());
                        self.set_attr("_thread_id", (Mod<"threading">().attr("get_ident"))());
                        (Mod<"sys">().attr("set_asyncgen_hooks")).call({  }, { { "firstiter", self.attr("_asyncgen_firstiter_hook") }, { "finalizer", self.attr("_asyncgen_finalizer_hook") } });
                        (G<"events._set_running_loop">())(self);
                        return none();
                    }) },
                { "_run_forever_cleanup", w.method("BaseEventLoop", "_run_forever_cleanup", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_stopping", boolean(false));
                        self.set_attr("_thread_id", none());
                        (G<"events._set_running_loop">())(none());
                        self.method("_set_coroutine_origin_tracking", { boolean(false) });
                        // Restore any pre-existing async generator hooks.
                        if (!self.attr("_old_agen_hooks").is_none())
                        {
                            call_ex((Mod<"sys">().attr("set_asyncgen_hooks")), {  }, self.attr("_old_agen_hooks"), py{});
                            self.set_attr("_old_agen_hooks", none());
                        }
                        return none();
                    }) },
                { "run_forever", w.method("BaseEventLoop", "run_forever", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_run_forever_setup");
                        {
                            std::optional<py> pending19;
                            [[maybe_unused]] int flow19 = 0;
                            [[maybe_unused]] py result19;
                            try
                            {
                                while (boolean(true).truthy())
                                {
                                    self.method("_run_once");
                                    if (self.attr("_stopping").truthy())
                                    {
                                        break;
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending19 = py_take_exception();
                            }
                            self.method("_run_forever_cleanup");
                            if (pending19)
                                py_reraise(*pending19);
                            if (flow19 == 1)
                                return result19;
                        }
                        return none();
                    }) },
                { "run_until_complete", w.method("BaseEventLoop", "run_until_complete", { py_positional("future") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py future = a[1];
                        py new_task;
                        self.method("_check_closed");
                        self.method("_check_running");
                        new_task = boolean(!(G<"futures.isfuture">())(future).truthy());
                        future = (G<"tasks.ensure_future">()).call({ future }, { { "loop", self } });
                        if (new_task.truthy())
                        {
                            // An exception is raised if the future didn't complete, so there
                            // is no need to log the "destroy pending task" message
                            future.set_attr("_log_destroy_pending", boolean(false));
                        }
                        future.method("add_done_callback", { G<"base_events._run_until_complete_cb">() });
                        {
                            std::optional<py> pending21;
                            [[maybe_unused]] int flow21 = 0;
                            [[maybe_unused]] py result21;
                            try
                            {
                                try
                                {
                                    self.method("run_forever");
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler21;
                                    {
                                        if ((new_task.truthy() && future.method("done").truthy() && (!future.method("cancelled").truthy())))
                                        {
                                            // The coroutine raised a BaseException. Consume the exception
                                            // to not log a warning, the caller doesn't have access to the
                                            // local task.
                                            future.method("exception");
                                        }
                                        py_reraise(handler21.exception);
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending21 = py_take_exception();
                            }
                            future.method("remove_done_callback", { G<"base_events._run_until_complete_cb">() });
                            if (pending21)
                                py_reraise(*pending21);
                            if (flow21 == 1)
                                return result21;
                        }
                        if (!future.method("done").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Event loop stopped before Future completed.">()));
                        }
                        return future.method("result");
                    }) },
                { "stop", w.method("BaseEventLoop", "stop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_stopping", boolean(true));
                        return none();
                    }) },
                { "close", w.method("BaseEventLoop", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py executor;
                        if (self.method("is_running").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Cannot close a running event loop">()));
                        }
                        if (self.attr("_closed").truthy())
                        {
                            return none();
                        }
                        if (self.attr("_debug").truthy())
                        {
                            (G<"log.logger">()).method("debug", { S<"Close %r">(), self });
                        }
                        self.set_attr("_closed", boolean(true));
                        self.attr("_ready").method("clear");
                        self.attr("_scheduled").method("clear");
                        self.set_attr("_executor_shutdown_called", boolean(true));
                        executor = self.attr("_default_executor");
                        if (!executor.is_none())
                        {
                            self.set_attr("_default_executor", none());
                            executor.method("shutdown", {  }, { { "wait", boolean(false) } });
                        }
                        return none();
                    }) },
                { "is_closed", w.method("BaseEventLoop", "is_closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_closed");
                    }) },
                { "__del__", w.method("BaseEventLoop", "__del__", { py_positional("_warn", Mod<"warnings">().attr("warn")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _warn = a[1];
                        if (!self.method("is_closed").truthy())
                        {
                            _warn.call({ S(std::string{ "unclosed event loop " } + self.repr()), B<"ResourceWarning">() }, { { "source", self } });
                            if (!self.method("is_running").truthy())
                            {
                                self.method("close");
                            }
                        }
                        return none();
                    }) },
                { "is_running", w.method("BaseEventLoop", "is_running", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(!self.attr("_thread_id").is_none());
                    }) },
                { "time", w.method("BaseEventLoop", "time", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (Mod<"time">().attr("monotonic"))();
                    }) },
                { "call_later", w.method("BaseEventLoop", "call_later", { py_positional("delay"), py_positional("callback"), py_keyword("context", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py delay = a[1];
                        py callback = a[2];
                        py context = a[3];
                        py args = a[4];
                        py timer;
                        if (delay.is_none())
                        {
                            py_raise((B<"TypeError">())(S<"delay must not be None">()));
                        }
                        timer = call_ex(self.attr("call_at"), { self.method("time") + delay, callback }, args, py{}, { { "context", context } });
                        // (del timer._source_traceback[-1]: the translated function has no frame)
                        return timer;
                    }, true) },
                { "call_at", w.method("BaseEventLoop", "call_at", { py_positional("when"), py_positional("callback"), py_keyword("context", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py when = a[1];
                        py callback = a[2];
                        py context = a[3];
                        py args = a[4];
                        py timer;
                        if (when.is_none())
                        {
                            py_raise((B<"TypeError">())(S<"when cannot be None">()));
                        }
                        self.method("_check_closed");
                        if (self.attr("_debug").truthy())
                        {
                            self.method("_check_thread");
                            self.method("_check_callback", { callback, S<"call_at">() });
                        }
                        timer = (G<"events.TimerHandle">())(when, callback, args, self, context);
                        // (del timer._source_traceback[-1]: the translated function has no frame)
                        (Mod<"heapq">().attr("heappush"))(self.attr("_scheduled"), timer);
                        timer.set_attr("_scheduled", boolean(true));
                        return timer;
                    }, true) },
                { "call_soon", w.method("BaseEventLoop", "call_soon", { py_positional("callback"), py_keyword("context", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py callback = a[1];
                        py context = a[2];
                        py args = a[3];
                        py handle;
                        self.method("_check_closed");
                        if (self.attr("_debug").truthy())
                        {
                            self.method("_check_thread");
                            self.method("_check_callback", { callback, S<"call_soon">() });
                        }
                        handle = self.method("_call_soon", { callback, args, context });
                        // (del handle._source_traceback[-1]: the translated function has no frame)
                        return handle;
                    }, true) },
                { "_check_callback", w.method("BaseEventLoop", "_check_callback", { py_positional("callback"), py_positional("method") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py callback = a[1];
                        py method = a[2];
                        if (((G<"coroutines.iscoroutine">())(callback).truthy() || (G<"coroutines.iscoroutinefunction">())(callback).truthy()))
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "coroutines cannot be used with " } + method.str() + "()")));
                        }
                        if (!callable(callback))
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "a callable object was expected by " } + method.str() + "(), got " + callback.repr())));
                        }
                        return none();
                    }) },
                { "_call_soon", w.method("BaseEventLoop", "_call_soon", { py_positional("callback"), py_positional("args"), py_positional("context") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py callback = a[1];
                        py args = a[2];
                        py context = a[3];
                        py handle;
                        handle = (G<"events.Handle">())(callback, args, self, context);
                        // (del handle._source_traceback[-1]: the translated function has no frame)
                        self.attr("_ready").method("append", { handle });
                        return handle;
                    }) },
                { "_check_thread", w.method("BaseEventLoop", "_check_thread", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py thread_id;
                        if (self.attr("_thread_id").is_none())
                        {
                            return none();
                        }
                        thread_id = (Mod<"threading">().attr("get_ident"))();
                        if (thread_id != self.attr("_thread_id"))
                        {
                            py_raise((B<"RuntimeError">())(S<"Non-thread-safe operation invoked on an event loop other than the current one">()));
                        }
                        return none();
                    }) },
                { "call_soon_threadsafe", w.method("BaseEventLoop", "call_soon_threadsafe", { py_positional("callback"), py_keyword("context", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py callback = a[1];
                        py context = a[2];
                        py args = a[3];
                        py handle;
                        self.method("_check_closed");
                        if (self.attr("_debug").truthy())
                        {
                            self.method("_check_callback", { callback, S<"call_soon_threadsafe">() });
                        }
                        handle = self.method("_call_soon", { callback, args, context });
                        // (del handle._source_traceback[-1]: the translated function has no frame)
                        self.method("_write_to_self");
                        return handle;
                    }, true) },
                { "run_in_executor", w.method("BaseEventLoop", "run_in_executor", { py_positional("executor"), py_positional("func") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py executor = a[1];
                        py func = a[2];
                        py args = a[3];
                        self.method("_check_closed");
                        if (self.attr("_debug").truthy())
                        {
                            self.method("_check_callback", { func, S<"run_in_executor">() });
                        }
                        if (executor.is_none())
                        {
                            executor = self.attr("_default_executor");
                            // Only check when the default executor is being used
                            self.method("_check_default_executor");
                            if (executor.is_none())
                            {
                                executor = (Mod<"concurrent.futures">().attr("ThreadPoolExecutor")).call({  }, { { "thread_name_prefix", S<"asyncio">() } });
                                self.set_attr("_default_executor", executor);
                            }
                        }
                        return (G<"futures.wrap_future">()).call({ call_ex(executor.attr("submit"), { func }, args, py{}) }, { { "loop", self } });
                    }, true) },
                { "set_default_executor", w.method("BaseEventLoop", "set_default_executor", { py_positional("executor") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py executor = a[1];
                        if (!isinstance(executor, Mod<"concurrent.futures">().attr("ThreadPoolExecutor")))
                        {
                            py_raise((B<"TypeError">())(S<"executor must be ThreadPoolExecutor instance">()));
                        }
                        self.set_attr("_default_executor", executor);
                        return none();
                    }) },
                { "_getaddrinfo_debug", w.method("BaseEventLoop", "_getaddrinfo_debug", { py_positional("host"), py_positional("port"), py_positional("family"), py_positional("type"), py_positional("proto"), py_positional("flags") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py family = a[3];
                        py type = a[4];
                        py proto = a[5];
                        py flags = a[6];
                        py addrinfo;
                        py dt;
                        py msg;
                        py t0;
                        msg = py_list({ S(host.str() + ":" + port.repr()) });
                        if (family.truthy())
                        {
                            msg.method("append", { S(std::string{ "family=" } + family.repr()) });
                        }
                        if (type.truthy())
                        {
                            msg.method("append", { S(std::string{ "type=" } + type.repr()) });
                        }
                        if (proto.truthy())
                        {
                            msg.method("append", { S(std::string{ "proto=" } + proto.repr()) });
                        }
                        if (flags.truthy())
                        {
                            msg.method("append", { S(std::string{ "flags=" } + flags.repr()) });
                        }
                        msg = (S<", ">()).method("join", { msg });
                        (G<"log.logger">()).method("debug", { S<"Get address info %s">(), msg });
                        t0 = self.method("time");
                        addrinfo = (Mod<"socket">().attr("getaddrinfo"))(host, port, family, type, proto, flags);
                        dt = self.method("time") - t0;
                        msg = S(std::string{ "Getting address info " } + msg.str() + " took " + formatted((dt * (F<1000.0>())), ".3f") + "ms: " + addrinfo.repr());
                        if (dt >= self.attr("slow_callback_duration"))
                        {
                            (G<"log.logger">()).method("info", { msg });
                        }
                        else
                        {
                            (G<"log.logger">()).method("debug", { msg });
                        }
                        return addrinfo;
                    }) },
                { "getaddrinfo", async_method(w, "BaseEventLoop", "getaddrinfo", { py_positional("host"), py_positional("port"), py_keyword("family", I<0>()), py_keyword("type", I<0>()), py_keyword("proto", I<0>()), py_keyword("flags", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py family = a[3];
                        py type = a[4];
                        py proto = a[5];
                        py flags = a[6];
                        py getaddr_func;
                        if (self.attr("_debug").truthy())
                        {
                            getaddr_func = self.attr("_getaddrinfo_debug");
                        }
                        else
                        {
                            getaddr_func = Mod<"socket">().attr("getaddrinfo");
                        }
                        return py_await(self.method("run_in_executor", { none(), getaddr_func, host, port, family, type, proto, flags }));
                    }) },
                { "getnameinfo", async_method(w, "BaseEventLoop", "getnameinfo", { py_positional("sockaddr"), py_positional("flags", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sockaddr = a[1];
                        py flags = a[2];
                        return py_await(self.method("run_in_executor", { none(), Mod<"socket">().attr("getnameinfo"), sockaddr, flags }));
                    }) },
                { "sock_sendfile", async_method(w, "BaseEventLoop", "sock_sendfile", { py_positional("sock"), py_positional("file"), py_positional("offset", I<0>()), py_positional("count", none()), py_keyword("fallback", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py file = a[2];
                        py offset = a[3];
                        py count = a[4];
                        py fallback = a[5];
                        py exc;
                        if ((self.attr("_debug").truthy() && (sock.method("gettimeout") != (I<0>()))))
                        {
                            py_raise((B<"ValueError">())(S<"the socket must be non-blocking">()));
                        }
                        (G<"base_events._check_ssl_socket">())(sock);
                        self.method("_check_sendfile_params", { sock, file, offset, count });
                        try
                        {
                            return py_await(self.method("_sock_sendfile_native", { sock, file, offset, count }));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler22;
                            if (matches(handler22.exception, G<"exceptions.SendfileNotAvailableError">()))
                            {
                                exc = handler22.exception;
                                if (!fallback.truthy())
                                {
                                    py_reraise(handler22.exception);
                                }
                            }
                            else
                                py_reraise(handler22.exception);
                        }
                        return py_await(self.method("_sock_sendfile_fallback", { sock, file, offset, count }));
                    }) },
                { "_sock_sendfile_native", async_method(w, "BaseEventLoop", "_sock_sendfile_native", { py_positional("sock"), py_positional("file"), py_positional("offset"), py_positional("count") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py file = a[2];
                        py offset = a[3];
                        py count = a[4];
                        py_raise((G<"exceptions.SendfileNotAvailableError">())(S(std::string{ "syscall sendfile is not available for socket " } + sock.repr() + " and file " + file.repr() + " combination")));
                    }) },
                { "_sock_sendfile_fallback", async_method(w, "BaseEventLoop", "_sock_sendfile_fallback", { py_positional("sock"), py_positional("file"), py_positional("offset"), py_positional("count") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py file = a[2];
                        py offset = a[3];
                        py count = a[4];
                        py blocksize;
                        py buf;
                        py read;
                        py total_sent;
                        py view;
                        if (has_attr(file, "seek"))
                        {
                            file.method("seek", { offset });
                        }
                        blocksize = (count.truthy() ? (B<"min">())(count, G<"constants.SENDFILE_FALLBACK_READBUFFER_SIZE">()) : G<"constants.SENDFILE_FALLBACK_READBUFFER_SIZE">());
                        buf = (B<"bytearray">())(blocksize);
                        total_sent = I<0>();
                        {
                            std::optional<py> pending23;
                            [[maybe_unused]] int flow23 = 0;
                            [[maybe_unused]] py result23;
                            try
                            {
                                while (boolean(true).truthy())
                                {
                                    if (count.truthy())
                                    {
                                        blocksize = (B<"min">())(count - total_sent, blocksize);
                                        if (blocksize <= (I<0>()))
                                        {
                                            break;
                                        }
                                    }
                                    view = (B<"memoryview">())(buf)[B<"slice">()(none(), blocksize, none())];
                                    read = py_await(self.method("run_in_executor", { none(), file.attr("readinto"), view }));
                                    if (!read.truthy())
                                    {
                                        break;
                                    }
                                    py_await(self.method("sock_sendall", { sock, view[B<"slice">()(none(), read, none())] }));
                                    total_sent = inplace(NEOS_PY_ADD, total_sent, read);
                                }
                                result23 = total_sent;
                                flow23 = 1;
                                goto finally23;
                            }
                            catch (raised const&)
                            {
                                pending23 = py_take_exception();
                            }
                            finally23:
                            if (((total_sent > (I<0>())) && has_attr(file, "seek")))
                            {
                                file.method("seek", { offset + total_sent });
                            }
                            if (pending23)
                                py_reraise(*pending23);
                            if (flow23 == 1)
                                return result23;
                        }
                        return none();
                    }) },
                { "_check_sendfile_params", w.method("BaseEventLoop", "_check_sendfile_params", { py_positional("sock"), py_positional("file"), py_positional("offset"), py_positional("count") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py file = a[2];
                        py offset = a[3];
                        py count = a[4];
                        if (!(B<"getattr">())(file, S<"mode">(), S<"b">()).contains((S<"b">())))
                        {
                            py_raise((B<"ValueError">())(S<"file should be opened in binary mode">()));
                        }
                        if (!(sock.attr("type") == (Mod<"socket">().attr("SOCK_STREAM"))))
                        {
                            py_raise((B<"ValueError">())(S<"only SOCK_STREAM type sockets are supported">()));
                        }
                        if (!count.is_none())
                        {
                            if (!isinstance(count, B<"int">()))
                            {
                                py_raise((B<"TypeError">())((S<"count must be a positive integer (got {!r})">()).method("format", { count })));
                            }
                            if (count <= (I<0>()))
                            {
                                py_raise((B<"ValueError">())((S<"count must be a positive integer (got {!r})">()).method("format", { count })));
                            }
                        }
                        if (!isinstance(offset, B<"int">()))
                        {
                            py_raise((B<"TypeError">())((S<"offset must be a non-negative integer (got {!r})">()).method("format", { offset })));
                        }
                        if (offset < (I<0>()))
                        {
                            py_raise((B<"ValueError">())((S<"offset must be a non-negative integer (got {!r})">()).method("format", { offset })));
                        }
                        return none();
                    }) },
                { "_connect_sock", async_method(w, "BaseEventLoop", "_connect_sock", { py_positional("exceptions"), py_positional("addr_info"), py_positional("local_addr_infos", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exceptions = a[1];
                        py addr_info = a[2];
                        py local_addr_infos = a[3];
                        py _;
                        py address;
                        py exc;
                        py family;
                        py laddr;
                        py lfamily;
                        py msg;
                        py my_exceptions;
                        py proto;
                        py sock;
                        py type_;
                        my_exceptions = py_list();
                        exceptions.method("append", { my_exceptions });
                        auto const items25 = unpack(addr_info, 5);
                        family = items25[0];
                        type_ = items25[1];
                        proto = items25[2];
                        _ = items25[3];
                        address = items25[4];
                        sock = none();
                        {
                            std::optional<py> pending26;
                            [[maybe_unused]] int flow26 = 0;
                            [[maybe_unused]] py result26;
                            try
                            {
                                try
                                {
                                    try
                                    {
                                        sock = (Mod<"socket">().attr("socket")).call({  }, { { "family", family }, { "type", type_ }, { "proto", proto } });
                                        sock.method("setblocking", { boolean(false) });
                                        if (!local_addr_infos.is_none())
                                        {
                                            bool broke28 = false;
                                            for (py const& item : iterate{ local_addr_infos })
                                            {
                                                auto const items29 = unpack(item, 5);
                                                lfamily = items29[0];
                                                _ = items29[1];
                                                _ = items29[2];
                                                _ = items29[3];
                                                laddr = items29[4];
                                                // skip local addresses of different family
                                                if (lfamily != family)
                                                {
                                                    continue;
                                                }
                                                try
                                                {
                                                    sock.method("bind", { laddr });
                                                    broke28 = true;
                                                    break;
                                                }
                                                catch (raised const&)
                                                {
                                                    pyclass::handler_scope handler30;
                                                    if (matches(handler30.exception, B<"OSError">()))
                                                    {
                                                        exc = handler30.exception;
                                                        msg = S(std::string{ "error while attempting to bind on address " } + laddr.repr() + ": " + (B<"str">())(exc).method("lower").str());
                                                        exc = (B<"OSError">())(exc.attr("errno"), msg);
                                                        my_exceptions.method("append", { exc });
                                                    }
                                                    else
                                                        py_reraise(handler30.exception);
                                                }
                                            }
                                            if (!broke28)
                                            {
                                                if (my_exceptions.truthy())
                                                {
                                                    py_raise(my_exceptions.method("pop"));
                                                }
                                                else
                                                {
                                                    py_raise((B<"OSError">())(S(std::string{ "no matching local address with family=" } + family.repr() + " found")));
                                                }
                                            }
                                        }
                                        py_await(self.method("sock_connect", { sock, address }));
                                        result26 = sock;
                                        flow26 = 1;
                                        goto finally26;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler27;
                                        if (matches(handler27.exception, B<"OSError">()))
                                        {
                                            exc = handler27.exception;
                                            my_exceptions.method("append", { exc });
                                            py_reraise(handler27.exception);
                                        }
                                        else
                                            py_reraise(handler27.exception);
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler26;
                                    {
                                        if (!sock.is_none())
                                        {
                                            try
                                            {
                                                sock.method("close");
                                            }
                                            catch (raised const&)
                                            {
                                                pyclass::handler_scope handler31;
                                                if (matches(handler31.exception, B<"OSError">()))
                                                {
                                                    // An error when closing a newly created socket is
                                                    // not important, but it can overwrite more important
                                                    // non-OSError error. So ignore it.
                                                }
                                                else
                                                    py_reraise(handler31.exception);
                                            }
                                        }
                                        py_reraise(handler26.exception);
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending26 = py_take_exception();
                            }
                            finally26:
                            py const value32 = none();
                            exceptions = value32;
                            my_exceptions = value32;
                            if (pending26)
                                py_reraise(*pending26);
                            if (flow26 == 1)
                                return result26;
                        }
                        return none();
                    }) },
                { "create_connection", async_method(w, "BaseEventLoop", "create_connection", { py_positional("protocol_factory"), py_positional("host", none()), py_positional("port", none()), py_keyword("ssl", none()), py_keyword("family", I<0>()), py_keyword("proto", I<0>()), py_keyword("flags", I<0>()), py_keyword("sock", none()), py_keyword("local_addr", none()), py_keyword("server_hostname", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()), py_keyword("happy_eyeballs_delay", none()), py_keyword("interleave", none()), py_keyword("all_errors", boolean(false)) }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py protocol_factory = a[1];
                        py host = a[2];
                        py port = a[3];
                        py ssl = a[4];
                        py family = a[5];
                        py proto = a[6];
                        py flags = a[7];
                        py sock = a[8];
                        py local_addr = a[9];
                        py server_hostname = a[10];
                        py ssl_handshake_timeout = a[11];
                        py ssl_shutdown_timeout = a[12];
                        py happy_eyeballs_delay = a[13];
                        py interleave = a[14];
                        py all_errors = a[15];
                        py addrinfo;
                        auto const exceptions = std::make_shared<py>();
                        py infos;
                        auto const laddr_infos = std::make_shared<py>();
                        py model;
                        py protocol;
                        py transport;
                        if (((!server_hostname.is_none()) && (!ssl.truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"server_hostname is only meaningful with ssl">()));
                        }
                        if ((server_hostname.is_none() && ssl.truthy()))
                        {
                            // Use host as default for server_hostname.  It is an error
                            // if host is empty or not set, e.g. when an
                            // already-connected socket was passed or when only a port
                            // is given.  To avoid this error, you can pass
                            // server_hostname='' -- this will bypass the hostname
                            // check.  (This also means that if host is a numeric
                            // IP/IPv6 address, we will attempt to verify that exact
                            // address; this will probably fail, but it is possible to
                            // create a certificate for a specific IP address, so we
                            // don't judge it here.)
                            if (!host.truthy())
                            {
                                py_raise((B<"ValueError">())(S<"You must set server_hostname when using ssl without a host">()));
                            }
                            server_hostname = host;
                        }
                        if (((!ssl_handshake_timeout.is_none()) && (!ssl.truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"ssl_handshake_timeout is only meaningful with ssl">()));
                        }
                        if (((!ssl_shutdown_timeout.is_none()) && (!ssl.truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"ssl_shutdown_timeout is only meaningful with ssl">()));
                        }
                        if (!sock.is_none())
                        {
                            (G<"base_events._check_ssl_socket">())(sock);
                        }
                        if (((!happy_eyeballs_delay.is_none()) && interleave.is_none()))
                        {
                            // If using happy eyeballs, default to interleave addresses by family
                            interleave = I<1>();
                        }
                        if (((!host.is_none()) || (!port.is_none())))
                        {
                            if (!sock.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"host/port and sock can not be specified at the same time">()));
                            }
                            infos = py_await((*self).method("_ensure_resolved", { py_tuple({ host, port }) }, { { "family", family }, { "type", Mod<"socket">().attr("SOCK_STREAM") }, { "proto", proto }, { "flags", flags }, { "loop", (*self) } }));
                            if (!infos.truthy())
                            {
                                py_raise((B<"OSError">())(S<"getaddrinfo() returned empty list">()));
                            }
                            if (!local_addr.is_none())
                            {
                                *laddr_infos = py_await((*self).method("_ensure_resolved", { local_addr }, { { "family", family }, { "type", Mod<"socket">().attr("SOCK_STREAM") }, { "proto", proto }, { "flags", flags }, { "loop", (*self) } }));
                                if (!(*laddr_infos).truthy())
                                {
                                    py_raise((B<"OSError">())(S<"getaddrinfo() returned empty list">()));
                                }
                            }
                            else
                            {
                                *laddr_infos = none();
                            }
                            if (interleave.truthy())
                            {
                                infos = (G<"base_events._interleave_addrinfos">())(infos, interleave);
                            }
                            *exceptions = py_list();
                            if (happy_eyeballs_delay.is_none())
                            {
                                // not using happy eyeballs
                                for (py const& item : iterate{ infos })
                                {
                                    addrinfo = item;
                                    try
                                    {
                                        sock = py_await((*self).method("_connect_sock", { (*exceptions), addrinfo, (*laddr_infos) }));
                                        break;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler34;
                                        if (matches(handler34.exception, B<"OSError">()))
                                        {
                                            continue;
                                        }
                                        else
                                            py_reraise(handler34.exception);
                                    }
                                }
                            }
                            else
                            {
                                sock = py_await((G<"staggered.staggered_race">()).call({ [&]() -> py { py const result = py_list(); py addrinfo; for (py const& item : iterate{ infos }) { addrinfo = item; result.method("append", { w.function("<lambda>", "BaseEventLoop.create_connection.<locals>.<lambda>", { py_positional("addrinfo", addrinfo) }, [exceptions, laddr_infos, self](arguments a) -> py
    {
        py addrinfo = a[0];
        return (*self).method("_connect_sock", { (*exceptions), addrinfo, (*laddr_infos) });
    }) }); } return result; }(), happy_eyeballs_delay }, { { "loop", (*self) } }))[I<0>()];
                            }
                            // can't use functools.partial as it keeps a reference
                            // to exceptions
                            if (sock.is_none())
                            {
                                *exceptions = [&]() -> py { py const result = py_list(); py exc; py sub; for (py const& item : iterate{ (*exceptions) }) { sub = item; for (py const& item : iterate{ sub }) { exc = item; result.method("append", { exc }); } } return result; }();
                                {
                                    std::optional<py> pending35;
                                    [[maybe_unused]] int flow35 = 0;
                                    [[maybe_unused]] py result35;
                                    try
                                    {
                                        if (all_errors.truthy())
                                        {
                                            py_raise((B<"ExceptionGroup">())(S<"create_connection failed">(), (*exceptions)));
                                        }
                                        if (py{ (*exceptions).size() } == (I<1>()))
                                        {
                                            py_raise((*exceptions)[I<0>()]);
                                        }
                                        else if ((*exceptions).truthy())
                                        {
                                            // If they all have the same str(), raise one.
                                            model = (B<"str">())((*exceptions)[I<0>()]);
                                            if ((B<"all">())([&]() -> py { py const result = py_list(); py exc; for (py const& item : iterate{ (*exceptions) }) { exc = item; result.method("append", { boolean((B<"str">())(exc) == model) }); } return result; }()).truthy())
                                            {
                                                py_raise((*exceptions)[I<0>()]);
                                            }
                                            // Raise a combined exception so the user can see all
                                            // the various error messages.
                                            py_raise((B<"OSError">())((S<"Multiple exceptions: {}">()).method("format", { (S<", ">()).method("join", { [&]() -> py { py const result = py_list(); py exc; for (py const& item : iterate{ (*exceptions) }) { exc = item; result.method("append", { (B<"str">())(exc) }); } return result; }() }) })));
                                        }
                                        else
                                        {
                                            // No exceptions were collected, raise a timeout error
                                            py_raise((B<"TimeoutError">())(S<"create_connection failed">()));
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pending35 = py_take_exception();
                                    }
                                    *exceptions = none();
                                    if (pending35)
                                        py_reraise(*pending35);
                                    if (flow35 == 1)
                                        return result35;
                                }
                            }
                        }
                        else
                        {
                            if (sock.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"host and port was not specified and no sock specified">()));
                            }
                            if (sock.attr("type") != (Mod<"socket">().attr("SOCK_STREAM")))
                            {
                                // We allow AF_INET, AF_INET6, AF_UNIX as long as they
                                // are SOCK_STREAM.
                                // We support passing AF_UNIX sockets even though we have
                                // a dedicated API for that: create_unix_connection.
                                // Disallowing AF_UNIX in this method, breaks backwards
                                // compatibility.
                                py_raise((B<"ValueError">())(S(std::string{ "A Stream Socket was expected, got " } + sock.repr())));
                            }
                        }
                        auto const items36 = unpack(py_await((*self).method("_create_connection_transport", { sock, protocol_factory, ssl, server_hostname }, { { "ssl_handshake_timeout", ssl_handshake_timeout }, { "ssl_shutdown_timeout", ssl_shutdown_timeout } })), 2);
                        transport = items36[0];
                        protocol = items36[1];
                        if ((*self).attr("_debug").truthy())
                        {
                            // Get the socket from the transport because SSL transport closes
                            // the old socket and creates a new SSL socket
                            sock = transport.method("get_extra_info", { S<"socket">() });
                            (G<"log.logger">()).method("debug", { S<"%r connected to %s:%r: (%r, %r)">(), sock, host, port, transport, protocol });
                        }
                        return py_tuple({ transport, protocol });
                    }) },
                { "_create_connection_transport", async_method(w, "BaseEventLoop", "_create_connection_transport", { py_positional("sock"), py_positional("protocol_factory"), py_positional("ssl"), py_positional("server_hostname"), py_positional("server_side", boolean(false)), py_positional("ssl_handshake_timeout", none()), py_positional("ssl_shutdown_timeout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py protocol_factory = a[2];
                        py ssl = a[3];
                        py server_hostname = a[4];
                        py server_side = a[5];
                        py ssl_handshake_timeout = a[6];
                        py ssl_shutdown_timeout = a[7];
                        py protocol;
                        py sslcontext;
                        py transport;
                        py waiter;
                        sock.method("setblocking", { boolean(false) });
                        protocol = protocol_factory();
                        waiter = self.method("create_future");
                        if (ssl.truthy())
                        {
                            sslcontext = (isinstance(ssl, B<"bool">()) ? none() : ssl);
                            transport = self.method("_make_ssl_transport", { sock, protocol, sslcontext, waiter }, { { "server_side", server_side }, { "server_hostname", server_hostname }, { "ssl_handshake_timeout", ssl_handshake_timeout }, { "ssl_shutdown_timeout", ssl_shutdown_timeout } });
                        }
                        else
                        {
                            transport = self.method("_make_socket_transport", { sock, protocol, waiter });
                        }
                        try
                        {
                            py_await(waiter);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler37;
                            {
                                transport.method("close");
                                py_reraise(handler37.exception);
                            }
                        }
                        return py_tuple({ transport, protocol });
                    }) },
                { "sendfile", async_method(w, "BaseEventLoop", "sendfile", { py_positional("transport"), py_positional("file"), py_positional("offset", I<0>()), py_positional("count", none()), py_keyword("fallback", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport = a[1];
                        py file = a[2];
                        py offset = a[3];
                        py count = a[4];
                        py fallback = a[5];
                        py exc;
                        py mode;
                        if (transport.method("is_closing").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Transport is closing">()));
                        }
                        mode = (B<"getattr">())(transport, S<"_sendfile_compatible">(), (G<"constants._SendfileMode">()).attr("UNSUPPORTED"));
                        if (mode.is((G<"constants._SendfileMode">()).attr("UNSUPPORTED")))
                        {
                            py_raise((B<"RuntimeError">())(S(std::string{ "sendfile is not supported for transport " } + transport.repr())));
                        }
                        if (mode.is((G<"constants._SendfileMode">()).attr("TRY_NATIVE")))
                        {
                            try
                            {
                                return py_await(self.method("_sendfile_native", { transport, file, offset, count }));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler38;
                                if (matches(handler38.exception, G<"exceptions.SendfileNotAvailableError">()))
                                {
                                    exc = handler38.exception;
                                    if (!fallback.truthy())
                                    {
                                        py_reraise(handler38.exception);
                                    }
                                }
                                else
                                    py_reraise(handler38.exception);
                            }
                        }
                        if (!fallback.truthy())
                        {
                            py_raise((B<"RuntimeError">())(S(std::string{ "fallback is disabled and native sendfile is not supported for transport " } + transport.repr())));
                        }
                        return py_await(self.method("_sendfile_fallback", { transport, file, offset, count }));
                    }) },
                { "_sendfile_native", async_method(w, "BaseEventLoop", "_sendfile_native", { py_positional("transp"), py_positional("file"), py_positional("offset"), py_positional("count") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transp = a[1];
                        py file = a[2];
                        py offset = a[3];
                        py count = a[4];
                        py_raise((G<"exceptions.SendfileNotAvailableError">())(S<"sendfile syscall is not supported">()));
                    }) },
                { "_sendfile_fallback", async_method(w, "BaseEventLoop", "_sendfile_fallback", { py_positional("transp"), py_positional("file"), py_positional("offset"), py_positional("count") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transp = a[1];
                        py file = a[2];
                        py offset = a[3];
                        py count = a[4];
                        py blocksize;
                        py buf;
                        py proto;
                        py read;
                        py total_sent;
                        py view;
                        if (has_attr(file, "seek"))
                        {
                            file.method("seek", { offset });
                        }
                        blocksize = (count.truthy() ? (B<"min">())(count, I<16384>()) : I<16384>());
                        buf = (B<"bytearray">())(blocksize);
                        total_sent = I<0>();
                        proto = (G<"base_events._SendfileFallbackProtocol">())(transp);
                        {
                            std::optional<py> pending39;
                            [[maybe_unused]] int flow39 = 0;
                            [[maybe_unused]] py result39;
                            try
                            {
                                while (boolean(true).truthy())
                                {
                                    if (count.truthy())
                                    {
                                        blocksize = (B<"min">())(count - total_sent, blocksize);
                                        if (blocksize <= (I<0>()))
                                        {
                                            result39 = total_sent;
                                            flow39 = 1;
                                            goto finally39;
                                        }
                                    }
                                    view = (B<"memoryview">())(buf)[B<"slice">()(none(), blocksize, none())];
                                    read = py_await(self.method("run_in_executor", { none(), file.attr("readinto"), view }));
                                    if (!read.truthy())
                                    {
                                        result39 = total_sent;
                                        flow39 = 1;
                                        goto finally39;
                                    }
                                    transp.method("write", { view[B<"slice">()(none(), read, none())] });
                                    py_await(proto.method("drain"));
                                    total_sent = inplace(NEOS_PY_ADD, total_sent, read);
                                }
                            }
                            catch (raised const&)
                            {
                                pending39 = py_take_exception();
                            }
                            finally39:
                            if (((total_sent > (I<0>())) && has_attr(file, "seek")))
                            {
                                file.method("seek", { offset + total_sent });
                            }
                            py_await(proto.method("restore"));
                            if (pending39)
                                py_reraise(*pending39);
                            if (flow39 == 1)
                                return result39;
                        }
                        return none();
                    }) },
                { "start_tls", async_method(w, "BaseEventLoop", "start_tls", { py_positional("transport"), py_positional("protocol"), py_positional("sslcontext"), py_keyword("server_side", boolean(false)), py_keyword("server_hostname", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport = a[1];
                        py protocol = a[2];
                        py sslcontext = a[3];
                        py server_side = a[4];
                        py server_hostname = a[5];
                        py ssl_handshake_timeout = a[6];
                        py ssl_shutdown_timeout = a[7];
                        py StreamReaderProtocol;
                        py buffer;
                        py conmade_cb;
                        py resume_cb;
                        py ssl_protocol;
                        py stream_reader;
                        py waiter;
                        if (none().is_none())
                        {
                            py_raise((B<"RuntimeError">())(S<"Python ssl module is not available">()));
                        }
                        if (!isinstance(sslcontext, none().attr("SSLContext")))
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "sslcontext is expected to be an instance of ssl.SSLContext, got " } + sslcontext.repr())));
                        }
                        if (!(B<"getattr">())(transport, S<"_start_tls_compatible">(), boolean(false)).truthy())
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "transport " } + transport.repr() + " is not supported by start_tls()")));
                        }
                        waiter = self.method("create_future");
                        ssl_protocol = (G<"sslproto.SSLProtocol">()).call({ self, protocol, sslcontext, waiter, server_side, server_hostname }, { { "ssl_handshake_timeout", ssl_handshake_timeout }, { "ssl_shutdown_timeout", ssl_shutdown_timeout }, { "call_connection_made", boolean(false) } });
                        // Pause early so that "ssl_protocol.data_received()" doesn't
                        // have a chance to get called before "ssl_protocol.connection_made()".
                        transport.method("pause_reading");
                        // gh-142352: move buffered StreamReader data to SSLProtocol
                        if (server_side.truthy())
                        {
                            StreamReaderProtocol = object_of("streams.StreamReaderProtocol");
                            if (isinstance(protocol, StreamReaderProtocol))
                            {
                                stream_reader = (B<"getattr">())(protocol, S<"_stream_reader">(), none());
                                if (!stream_reader.is_none())
                                {
                                    buffer = stream_reader.attr("_buffer");
                                    if (buffer.truthy())
                                    {
                                        ssl_protocol.attr("_incoming").method("write", { buffer });
                                        buffer.method("clear");
                                    }
                                }
                            }
                        }
                        transport.method("set_protocol", { ssl_protocol });
                        conmade_cb = self.method("call_soon", { ssl_protocol.attr("connection_made"), transport });
                        resume_cb = self.method("call_soon", { transport.attr("resume_reading") });
                        try
                        {
                            py_await(waiter);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler41;
                            if (matches(handler41.exception, B<"BaseException">()))
                            {
                                transport.method("close");
                                conmade_cb.method("cancel");
                                resume_cb.method("cancel");
                                py_reraise(handler41.exception);
                            }
                            else
                                py_reraise(handler41.exception);
                        }
                        return ssl_protocol.attr("_app_transport");
                    }) },
                { "create_datagram_endpoint", async_method(w, "BaseEventLoop", "create_datagram_endpoint", { py_positional("protocol_factory"), py_positional("local_addr", none()), py_positional("remote_addr", none()), py_keyword("family", I<0>()), py_keyword("proto", I<0>()), py_keyword("flags", I<0>()), py_keyword("reuse_port", none()), py_keyword("allow_broadcast", none()), py_keyword("sock", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol_factory = a[1];
                        py local_addr = a[2];
                        py remote_addr = a[3];
                        py family = a[4];
                        py proto = a[5];
                        py flags = a[6];
                        py reuse_port = a[7];
                        py allow_broadcast = a[8];
                        py sock = a[9];
                        py _;
                        py addr;
                        py addr_infos;
                        py addr_pairs_info;
                        py address;
                        py err;
                        py exc;
                        py exceptions;
                        py fam;
                        py idx;
                        py infos;
                        py key;
                        py local_address;
                        py opts;
                        py pro;
                        py problems;
                        py protocol;
                        py r_addr;
                        py remote_address;
                        py transport;
                        py waiter;
                        if (!sock.is_none())
                        {
                            if (sock.attr("type") == (Mod<"socket">().attr("SOCK_STREAM")))
                            {
                                py_raise((B<"ValueError">())(S(std::string{ "A datagram socket was expected, got " } + sock.repr())));
                            }
                            if ((local_addr.truthy() || remote_addr.truthy() || family.truthy() || proto.truthy() || flags.truthy() || reuse_port.truthy() || allow_broadcast.truthy()))
                            {
                                // show the problematic kwargs in exception msg
                                opts = (B<"dict">()).call({  }, { { "local_addr", local_addr }, { "remote_addr", remote_addr }, { "family", family }, { "proto", proto }, { "flags", flags }, { "reuse_port", reuse_port }, { "allow_broadcast", allow_broadcast } });
                                problems = (S<", ">()).method("join", { [&]() -> py { py const result = py_list(); py k; py v; for (py const& item : iterate{ opts.method("items") }) { auto const items42 = unpack(item, 2); k = items42[0]; v = items42[1]; if (!v.truthy()) continue; result.method("append", { S(k.str() + "=" + v.str()) }); } return result; }() });
                                py_raise((B<"ValueError">())(S(std::string{ "socket modifier keyword arguments can not be used when sock is specified. (" } + problems.str() + ")")));
                            }
                            sock.method("setblocking", { boolean(false) });
                            r_addr = none();
                        }
                        else
                        {
                            if (!([&]() -> py { py const t = local_addr; return t.truthy() ? t : remote_addr; }()).truthy())
                            {
                                if (family == (I<0>()))
                                {
                                    py_raise((B<"ValueError">())(S<"unexpected address family">()));
                                }
                                addr_pairs_info = py_tuple({ py_tuple({ py_tuple({ family, proto }), py_tuple({ none(), none() }) }) });
                            }
                            else if ((has_attr(Mod<"socket">(), "AF_UNIX") && (family == (Mod<"socket">().attr("AF_UNIX")))))
                            {
                                for (py const& item : iterate{ py_tuple({ local_addr, remote_addr }) })
                                {
                                    addr = item;
                                    if (((!addr.is_none()) && (!isinstance(addr, B<"str">()))))
                                    {
                                        py_raise((B<"TypeError">())(S<"string is expected">()));
                                    }
                                }
                                if ((local_addr.truthy() && (!py_tuple({ I<0>(), S<"\u0000">() }).contains(local_addr[I<0>()]))))
                                {
                                    try
                                    {
                                        if ((Mod<"stat">().attr("S_ISSOCK"))((Mod<"os">().attr("stat"))(local_addr).attr("st_mode")).truthy())
                                        {
                                            (Mod<"os">().attr("remove"))(local_addr);
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler44;
                                        if (matches(handler44.exception, B<"FileNotFoundError">()))
                                        {
                                        }
                                        else if (matches(handler44.exception, B<"OSError">()))
                                        {
                                            err = handler44.exception;
                                            // Directory may have permissions only to create socket.
                                            (G<"log.logger">()).method("error", { S<"Unable to check or remove stale UNIX socket %r: %r">(), local_addr, err });
                                        }
                                        else
                                            py_reraise(handler44.exception);
                                    }
                                }
                                addr_pairs_info = py_tuple({ py_tuple({ py_tuple({ family, proto }), py_tuple({ local_addr, remote_addr }) }) });
                            }
                            else
                            {
                                // join address by (family, protocol)
                                addr_infos = py_dict();
                                for (py const& item : iterate{ py_tuple({ py_tuple({ I<0>(), local_addr }), py_tuple({ I<1>(), remote_addr }) }) })
                                {
                                    auto const items46 = unpack(item, 2);
                                    idx = items46[0];
                                    addr = items46[1];
                                    if (!addr.is_none())
                                    {
                                        if (!([&]() -> py { py const t = boolean(isinstance(addr, B<"tuple">())); return !t.truthy() ? t : boolean(py{ addr.size() } == (I<2>())); }()).truthy())
                                        {
                                            py_raise((B<"TypeError">())(S<"2-tuple is expected">()));
                                        }
                                        infos = py_await(self.method("_ensure_resolved", { addr }, { { "family", family }, { "type", Mod<"socket">().attr("SOCK_DGRAM") }, { "proto", proto }, { "flags", flags }, { "loop", self } }));
                                        if (!infos.truthy())
                                        {
                                            py_raise((B<"OSError">())(S<"getaddrinfo() returned empty list">()));
                                        }
                                        for (py const& item : iterate{ infos })
                                        {
                                            auto const items48 = unpack(item, 5);
                                            fam = items48[0];
                                            _ = items48[1];
                                            pro = items48[2];
                                            _ = items48[3];
                                            address = items48[4];
                                            key = py_tuple({ fam, pro });
                                            if (!addr_infos.contains(key))
                                            {
                                                addr_infos.set_item(key, py_list({ none(), none() }));
                                            }
                                            addr_infos[key].set_item(idx, address);
                                        }
                                    }
                                }
                                // each addr has to have info for each (family, proto) pair
                                addr_pairs_info = [&]() -> py { py const result = py_list(); py addr_pair; py key; for (py const& item : iterate{ addr_infos.method("items") }) { auto const items49 = unpack(item, 2); key = items49[0]; addr_pair = items49[1]; if (([&]() -> py { py const t = [&]() -> py { py const t = local_addr; return !t.truthy() ? t : boolean(addr_pair[I<0>()].is_none()); }(); return t.truthy() ? t : [&]() -> py { py const t = remote_addr; return !t.truthy() ? t : boolean(addr_pair[I<1>()].is_none()); }(); }()).truthy()) continue; result.method("append", { py_tuple({ key, addr_pair }) }); } return result; }();
                                if (!addr_pairs_info.truthy())
                                {
                                    py_raise((B<"ValueError">())(S<"can not get address information">()));
                                }
                            }
                            exceptions = py_list();
                            bool broke50 = false;
                            for (py const& item : iterate{ addr_pairs_info })
                            {
                                auto const items51 = unpack(item, 2);
                                auto const items52 = unpack(items51[0], 2);
                                family = items52[0];
                                proto = items52[1];
                                auto const items53 = unpack(items51[1], 2);
                                local_address = items53[0];
                                remote_address = items53[1];
                                sock = none();
                                r_addr = none();
                                {
                                    bool otherwise54 = false;
                                    try
                                    {
                                        sock = (Mod<"socket">().attr("socket")).call({  }, { { "family", family }, { "type", Mod<"socket">().attr("SOCK_DGRAM") }, { "proto", proto } });
                                        if (reuse_port.truthy())
                                        {
                                            (G<"base_events._set_reuseport">())(sock);
                                        }
                                        if (allow_broadcast.truthy())
                                        {
                                            sock.method("setsockopt", { Mod<"socket">().attr("SOL_SOCKET"), Mod<"socket">().attr("SO_BROADCAST"), I<1>() });
                                        }
                                        sock.method("setblocking", { boolean(false) });
                                        if (local_addr.truthy())
                                        {
                                            sock.method("bind", { local_address });
                                        }
                                        if (remote_addr.truthy())
                                        {
                                            if (!allow_broadcast.truthy())
                                            {
                                                py_await(self.method("sock_connect", { sock, remote_address }));
                                            }
                                            r_addr = remote_address;
                                        }
                                        otherwise54 = true;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler54;
                                        if (matches(handler54.exception, B<"OSError">()))
                                        {
                                            exc = handler54.exception;
                                            if (!sock.is_none())
                                            {
                                                sock.method("close");
                                            }
                                            exceptions.method("append", { exc });
                                        }
                                        else
                                        {
                                            if (!sock.is_none())
                                            {
                                                sock.method("close");
                                            }
                                            py_reraise(handler54.exception);
                                        }
                                    }
                                    if (otherwise54)
                                    {
                                        broke50 = true;
                                        break;
                                    }
                                }
                            }
                            if (!broke50)
                            {
                                py_raise(exceptions[I<0>()]);
                            }
                        }
                        protocol = protocol_factory();
                        waiter = self.method("create_future");
                        transport = self.method("_make_datagram_transport", { sock, protocol, r_addr, waiter });
                        if (self.attr("_debug").truthy())
                        {
                            if (local_addr.truthy())
                            {
                                (G<"log.logger">()).method("info", { S<"Datagram endpoint local_addr=%r remote_addr=%r created: (%r, %r)">(), local_addr, remote_addr, transport, protocol });
                            }
                            else
                            {
                                (G<"log.logger">()).method("debug", { S<"Datagram endpoint remote_addr=%r created: (%r, %r)">(), remote_addr, transport, protocol });
                            }
                        }
                        try
                        {
                            py_await(waiter);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler55;
                            {
                                transport.method("close");
                                py_reraise(handler55.exception);
                            }
                        }
                        return py_tuple({ transport, protocol });
                    }) },
                { "_ensure_resolved", async_method(w, "BaseEventLoop", "_ensure_resolved", { py_positional("address"), py_keyword("family", I<0>()), py_keyword("type", Mod<"socket">().attr("SOCK_STREAM")), py_keyword("proto", I<0>()), py_keyword("flags", I<0>()), py_keyword("loop") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py address = a[1];
                        py family = a[2];
                        py type = a[3];
                        py proto = a[4];
                        py flags = a[5];
                        py loop = a[6];
                        py host;
                        py info;
                        py port;
                        auto const items56 = unpack(address[B<"slice">()(none(), I<2>(), none())], 2);
                        host = items56[0];
                        port = items56[1];
                        info = call_ex((G<"base_events._ipaddr_info">()), { host, port, family, type, proto }, address[B<"slice">()(I<2>(), none(), none())], py{});
                        if (!info.is_none())
                        {
                            // "host" is already a resolved IP.
                            return py_list({ info });
                        }
                        else
                        {
                            return py_await(loop.method("getaddrinfo", { host, port }, { { "family", family }, { "type", type }, { "proto", proto }, { "flags", flags } }));
                        }
                        return none();
                    }) },
                { "_create_server_getaddrinfo", async_method(w, "BaseEventLoop", "_create_server_getaddrinfo", { py_positional("host"), py_positional("port"), py_positional("family"), py_positional("flags") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py family = a[3];
                        py flags = a[4];
                        py infos;
                        infos = py_await(self.method("_ensure_resolved", { py_tuple({ host, port }) }, { { "family", family }, { "type", Mod<"socket">().attr("SOCK_STREAM") }, { "flags", flags }, { "loop", self } }));
                        if (!infos.truthy())
                        {
                            py_raise((B<"OSError">())(S(std::string{ "getaddrinfo(" } + host.repr() + ") returned empty list")));
                        }
                        return infos;
                    }) },
                { "create_server", async_method(w, "BaseEventLoop", "create_server", { py_positional("protocol_factory"), py_positional("host", none()), py_positional("port", none()), py_keyword("family", Mod<"socket">().attr("AF_UNSPEC")), py_keyword("flags", Mod<"socket">().attr("AI_PASSIVE")), py_keyword("sock", none()), py_keyword("backlog", I<100>()), py_keyword("ssl", none()), py_keyword("reuse_address", none()), py_keyword("reuse_port", none()), py_keyword("keep_alive", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()), py_keyword("start_serving", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol_factory = a[1];
                        py host = a[2];
                        py port = a[3];
                        py family = a[4];
                        py flags = a[5];
                        py sock = a[6];
                        py backlog = a[7];
                        py ssl = a[8];
                        py reuse_address = a[9];
                        py reuse_port = a[10];
                        py keep_alive = a[11];
                        py ssl_handshake_timeout = a[12];
                        py ssl_shutdown_timeout = a[13];
                        py start_serving = a[14];
                        py af;
                        py canonname;
                        py completed;
                        py err;
                        py fs;
                        py hosts;
                        py infos;
                        py msg;
                        py proto;
                        py res;
                        py sa;
                        py server;
                        py sockets;
                        py socktype;
                        if (isinstance(ssl, B<"bool">()))
                        {
                            py_raise((B<"TypeError">())(S<"ssl argument must be an SSLContext or None">()));
                        }
                        if (((!ssl_handshake_timeout.is_none()) && ssl.is_none()))
                        {
                            py_raise((B<"ValueError">())(S<"ssl_handshake_timeout is only meaningful with ssl">()));
                        }
                        if (((!ssl_shutdown_timeout.is_none()) && ssl.is_none()))
                        {
                            py_raise((B<"ValueError">())(S<"ssl_shutdown_timeout is only meaningful with ssl">()));
                        }
                        if (!sock.is_none())
                        {
                            (G<"base_events._check_ssl_socket">())(sock);
                        }
                        if (((!host.is_none()) || (!port.is_none())))
                        {
                            if (!sock.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"host/port and sock can not be specified at the same time">()));
                            }
                            if (reuse_address.is_none())
                            {
                                reuse_address = [&]() -> py { py const t = boolean((Mod<"os">().attr("name")) == (S<"posix">())); return !t.truthy() ? t : boolean((Mod<"sys">().attr("platform")) != (S<"cygwin">())); }();
                            }
                            sockets = py_list();
                            if (host == (S<"">()))
                            {
                                hosts = py_list({ none() });
                            }
                            else if ((isinstance(host, B<"str">()) || (!isinstance(host, Mod<"collections.abc">().attr("Iterable")))))
                            {
                                hosts = py_list({ host });
                            }
                            else
                            {
                                hosts = host;
                            }
                            fs = [&]() -> py { py const result = py_list(); py host; for (py const& item : iterate{ hosts }) { host = item; result.method("append", { self.method("_create_server_getaddrinfo", { host, port }, { { "family", family }, { "flags", flags } }) }); } return result; }();
                            infos = py_await(call_ex((G<"tasks.gather">()), {  }, fs, py{}));
                            infos = (B<"set">())((Mod<"itertools">().attr("chain")).method("from_iterable", { infos }));
                            completed = boolean(false);
                            {
                                std::optional<py> pending57;
                                [[maybe_unused]] int flow57 = 0;
                                [[maybe_unused]] py result57;
                                try
                                {
                                    for (py const& item : iterate{ infos })
                                    {
                                        res = item;
                                        auto const items59 = unpack(res, 5);
                                        af = items59[0];
                                        socktype = items59[1];
                                        proto = items59[2];
                                        canonname = items59[3];
                                        sa = items59[4];
                                        try
                                        {
                                            sock = (Mod<"socket">().attr("socket"))(af, socktype, proto);
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler60;
                                            if (matches(handler60.exception, Mod<"socket">().attr("error")))
                                            {
                                                // Assume it's a bad family/type/protocol combination.
                                                if (self.attr("_debug").truthy())
                                                {
                                                    (G<"log.logger">()).method("warning", { S<"create_server() failed to create socket.socket(%r, %r, %r)">(), af, socktype, proto }, { { "exc_info", boolean(true) } });
                                                }
                                                continue;
                                            }
                                            else
                                                py_reraise(handler60.exception);
                                        }
                                        sockets.method("append", { sock });
                                        if (reuse_address.truthy())
                                        {
                                            sock.method("setsockopt", { Mod<"socket">().attr("SOL_SOCKET"), Mod<"socket">().attr("SO_REUSEADDR"), boolean(true) });
                                        }
                                        // Since Linux 6.12.9, SO_REUSEPORT is not allowed
                                        // on other address families than AF_INET/AF_INET6.
                                        if ((reuse_port.truthy() && py_tuple({ Mod<"socket">().attr("AF_INET"), Mod<"socket">().attr("AF_INET6") }).contains(af)))
                                        {
                                            (G<"base_events._set_reuseport">())(sock);
                                        }
                                        if (keep_alive.truthy())
                                        {
                                            sock.method("setsockopt", { Mod<"socket">().attr("SOL_SOCKET"), Mod<"socket">().attr("SO_KEEPALIVE"), boolean(true) });
                                        }
                                        // Disable IPv4/IPv6 dual stack support (enabled by
                                        // default on Linux) which makes a single socket
                                        // listen on both address families.
                                        if (((G<"base_events._HAS_IPv6">()).truthy() && (af == (Mod<"socket">().attr("AF_INET6"))) && has_attr(Mod<"socket">(), "IPPROTO_IPV6")))
                                        {
                                            sock.method("setsockopt", { Mod<"socket">().attr("IPPROTO_IPV6"), Mod<"socket">().attr("IPV6_V6ONLY"), boolean(true) });
                                        }
                                        try
                                        {
                                            sock.method("bind", { sa });
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler61;
                                            if (matches(handler61.exception, B<"OSError">()))
                                            {
                                                err = handler61.exception;
                                                msg = (S<"error while attempting to bind on address %r: %s">()) % py_tuple({ sa, (B<"str">())(err).method("lower") });
                                                if (err.attr("errno") == (Mod<"errno">().attr("EADDRNOTAVAIL")))
                                                {
                                                    // Assume the family is not enabled (bpo-30945)
                                                    sockets.method("pop");
                                                    sock.method("close");
                                                    if (self.attr("_debug").truthy())
                                                    {
                                                        (G<"log.logger">()).method("warning", { msg });
                                                    }
                                                    continue;
                                                }
                                                fn_raise_from((B<"OSError">())(err.attr("errno"), msg).get(), none().get());
                                                throw raised{};
                                            }
                                            else
                                                py_reraise(handler61.exception);
                                        }
                                    }
                                    if (!sockets.truthy())
                                    {
                                        py_raise((B<"OSError">())((S<"could not bind on any address out of %r">()) % py_tuple({ [&]() -> py { py const result = py_list(); py info; for (py const& item : iterate{ infos }) { info = item; result.method("append", { info[I<4>()] }); } return result; }() })));
                                    }
                                    completed = boolean(true);
                                }
                                catch (raised const&)
                                {
                                    pending57 = py_take_exception();
                                }
                                if (!completed.truthy())
                                {
                                    for (py const& item : iterate{ sockets })
                                    {
                                        sock = item;
                                        sock.method("close");
                                    }
                                }
                                if (pending57)
                                    py_reraise(*pending57);
                                if (flow57 == 1)
                                    return result57;
                            }
                        }
                        else
                        {
                            if (sock.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"Neither host/port nor sock were specified">()));
                            }
                            if (sock.attr("type") != (Mod<"socket">().attr("SOCK_STREAM")))
                            {
                                py_raise((B<"ValueError">())(S(std::string{ "A Stream Socket was expected, got " } + sock.repr())));
                            }
                            sockets = py_list({ sock });
                        }
                        for (py const& item : iterate{ sockets })
                        {
                            sock = item;
                            sock.method("setblocking", { boolean(false) });
                        }
                        server = (G<"base_events.Server">())(self, sockets, protocol_factory, ssl, backlog, ssl_handshake_timeout, ssl_shutdown_timeout);
                        if (start_serving.truthy())
                        {
                            server.method("_start_serving");
                            // Skip one loop iteration so that all 'loop.add_reader'
                            // go through.
                            py_await((G<"tasks.sleep">())(I<0>()));
                        }
                        if (self.attr("_debug").truthy())
                        {
                            (G<"log.logger">()).method("info", { S<"%r is serving">(), server });
                        }
                        return server;
                    }) },
                { "connect_accepted_socket", async_method(w, "BaseEventLoop", "connect_accepted_socket", { py_positional("protocol_factory"), py_positional("sock"), py_keyword("ssl", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol_factory = a[1];
                        py sock = a[2];
                        py ssl = a[3];
                        py ssl_handshake_timeout = a[4];
                        py ssl_shutdown_timeout = a[5];
                        py protocol;
                        py transport;
                        if (sock.attr("type") != (Mod<"socket">().attr("SOCK_STREAM")))
                        {
                            py_raise((B<"ValueError">())(S(std::string{ "A Stream Socket was expected, got " } + sock.repr())));
                        }
                        if (((!ssl_handshake_timeout.is_none()) && (!ssl.truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"ssl_handshake_timeout is only meaningful with ssl">()));
                        }
                        if (((!ssl_shutdown_timeout.is_none()) && (!ssl.truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"ssl_shutdown_timeout is only meaningful with ssl">()));
                        }
                        if (!sock.is_none())
                        {
                            (G<"base_events._check_ssl_socket">())(sock);
                        }
                        auto const items64 = unpack(py_await(self.method("_create_connection_transport", { sock, protocol_factory, ssl, S<"">() }, { { "server_side", boolean(true) }, { "ssl_handshake_timeout", ssl_handshake_timeout }, { "ssl_shutdown_timeout", ssl_shutdown_timeout } })), 2);
                        transport = items64[0];
                        protocol = items64[1];
                        if (self.attr("_debug").truthy())
                        {
                            // Get the socket from the transport because SSL transport closes
                            // the old socket and creates a new SSL socket
                            sock = transport.method("get_extra_info", { S<"socket">() });
                            (G<"log.logger">()).method("debug", { S<"%r handled: (%r, %r)">(), sock, transport, protocol });
                        }
                        return py_tuple({ transport, protocol });
                    }) },
                { "connect_read_pipe", async_method(w, "BaseEventLoop", "connect_read_pipe", { py_positional("protocol_factory"), py_positional("pipe") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol_factory = a[1];
                        py pipe = a[2];
                        py protocol;
                        py transport;
                        py waiter;
                        protocol = protocol_factory();
                        waiter = self.method("create_future");
                        transport = self.method("_make_read_pipe_transport", { pipe, protocol, waiter });
                        try
                        {
                            py_await(waiter);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler65;
                            {
                                transport.method("close");
                                py_reraise(handler65.exception);
                            }
                        }
                        if (self.attr("_debug").truthy())
                        {
                            (G<"log.logger">()).method("debug", { S<"Read pipe %r connected: (%r, %r)">(), pipe.method("fileno"), transport, protocol });
                        }
                        return py_tuple({ transport, protocol });
                    }) },
                { "connect_write_pipe", async_method(w, "BaseEventLoop", "connect_write_pipe", { py_positional("protocol_factory"), py_positional("pipe") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol_factory = a[1];
                        py pipe = a[2];
                        py protocol;
                        py transport;
                        py waiter;
                        protocol = protocol_factory();
                        waiter = self.method("create_future");
                        transport = self.method("_make_write_pipe_transport", { pipe, protocol, waiter });
                        try
                        {
                            py_await(waiter);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler66;
                            {
                                transport.method("close");
                                py_reraise(handler66.exception);
                            }
                        }
                        if (self.attr("_debug").truthy())
                        {
                            (G<"log.logger">()).method("debug", { S<"Write pipe %r connected: (%r, %r)">(), pipe.method("fileno"), transport, protocol });
                        }
                        return py_tuple({ transport, protocol });
                    }) },
                { "_log_subprocess", w.method("BaseEventLoop", "_log_subprocess", { py_positional("msg"), py_positional("stdin"), py_positional("stdout"), py_positional("stderr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py stdin_ = a[2];
                        py stdout_ = a[3];
                        py stderr_ = a[4];
                        py info;
                        info = py_list({ msg });
                        if (!stdin_.is_none())
                        {
                            info.method("append", { S(std::string{ "stdin=" } + (G<"base_events._format_pipe">())(stdin_).str()) });
                        }
                        if (((!stdout_.is_none()) && (stderr_ == (py{ -2 } /* subprocess.STDOUT */))))
                        {
                            info.method("append", { S(std::string{ "stdout=stderr=" } + (G<"base_events._format_pipe">())(stdout_).str()) });
                        }
                        else
                        {
                            if (!stdout_.is_none())
                            {
                                info.method("append", { S(std::string{ "stdout=" } + (G<"base_events._format_pipe">())(stdout_).str()) });
                            }
                            if (!stderr_.is_none())
                            {
                                info.method("append", { S(std::string{ "stderr=" } + (G<"base_events._format_pipe">())(stderr_).str()) });
                            }
                        }
                        (G<"log.logger">()).method("debug", { (S<" ">()).method("join", { info }) });
                        return none();
                    }) },
                { "subprocess_shell", async_method(w, "BaseEventLoop", "subprocess_shell", { py_positional("protocol_factory"), py_positional("cmd"), py_keyword("stdin", py{ -1 } /* subprocess.PIPE */), py_keyword("stdout", py{ -1 } /* subprocess.PIPE */), py_keyword("stderr", py{ -1 } /* subprocess.PIPE */), py_keyword("universal_newlines", boolean(false)), py_keyword("shell", boolean(true)), py_keyword("bufsize", I<0>()), py_keyword("encoding", none()), py_keyword("errors", none()), py_keyword("text", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol_factory = a[1];
                        py cmd = a[2];
                        py stdin_ = a[3];
                        py stdout_ = a[4];
                        py stderr_ = a[5];
                        py universal_newlines = a[6];
                        py shell = a[7];
                        py bufsize = a[8];
                        py encoding = a[9];
                        py errors = a[10];
                        py text = a[11];
                        py kwargs = a[12];
                        py debug_log;
                        py protocol;
                        py transport;
                        if (!isinstance(cmd, py_tuple({ B<"bytes">(), B<"str">() })))
                        {
                            py_raise((B<"ValueError">())(S<"cmd must be a string">()));
                        }
                        if (universal_newlines.truthy())
                        {
                            py_raise((B<"ValueError">())(S<"universal_newlines must be False">()));
                        }
                        if (!shell.truthy())
                        {
                            py_raise((B<"ValueError">())(S<"shell must be True">()));
                        }
                        if (bufsize != (I<0>()))
                        {
                            py_raise((B<"ValueError">())(S<"bufsize must be 0">()));
                        }
                        if (text.truthy())
                        {
                            py_raise((B<"ValueError">())(S<"text must be False">()));
                        }
                        if (!encoding.is_none())
                        {
                            py_raise((B<"ValueError">())(S<"encoding must be None">()));
                        }
                        if (!errors.is_none())
                        {
                            py_raise((B<"ValueError">())(S<"errors must be None">()));
                        }
                        protocol = protocol_factory();
                        debug_log = none();
                        if (self.attr("_debug").truthy())
                        {
                            // don't log parameters: they may contain sensitive information
                            // (password) and may be too long
                            debug_log = (S<"run shell command %r">()) % cmd;
                            self.method("_log_subprocess", { debug_log, stdin_, stdout_, stderr_ });
                        }
                        transport = py_await(call_ex(self.attr("_make_subprocess_transport"), { protocol, cmd, boolean(true), stdin_, stdout_, stderr_, bufsize }, py{}, kwargs));
                        if ((self.attr("_debug").truthy() && (!debug_log.is_none())))
                        {
                            (G<"log.logger">()).method("info", { S<"%s: %r">(), debug_log, transport });
                        }
                        return py_tuple({ transport, protocol });
                    }, false, true) },
                { "subprocess_exec", async_method(w, "BaseEventLoop", "subprocess_exec", { py_positional("protocol_factory"), py_positional("program"), py_keyword("stdin", py{ -1 } /* subprocess.PIPE */), py_keyword("stdout", py{ -1 } /* subprocess.PIPE */), py_keyword("stderr", py{ -1 } /* subprocess.PIPE */), py_keyword("universal_newlines", boolean(false)), py_keyword("shell", boolean(false)), py_keyword("bufsize", I<0>()), py_keyword("encoding", none()), py_keyword("errors", none()), py_keyword("text", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol_factory = a[1];
                        py program = a[2];
                        py stdin_ = a[3];
                        py stdout_ = a[4];
                        py stderr_ = a[5];
                        py universal_newlines = a[6];
                        py shell = a[7];
                        py bufsize = a[8];
                        py encoding = a[9];
                        py errors = a[10];
                        py text = a[11];
                        py args = a[12];
                        py kwargs = a[13];
                        py debug_log;
                        py popen_args;
                        py protocol;
                        py transport;
                        if (universal_newlines.truthy())
                        {
                            py_raise((B<"ValueError">())(S<"universal_newlines must be False">()));
                        }
                        if (shell.truthy())
                        {
                            py_raise((B<"ValueError">())(S<"shell must be False">()));
                        }
                        if (bufsize != (I<0>()))
                        {
                            py_raise((B<"ValueError">())(S<"bufsize must be 0">()));
                        }
                        if (text.truthy())
                        {
                            py_raise((B<"ValueError">())(S<"text must be False">()));
                        }
                        if (!encoding.is_none())
                        {
                            py_raise((B<"ValueError">())(S<"encoding must be None">()));
                        }
                        if (!errors.is_none())
                        {
                            py_raise((B<"ValueError">())(S<"errors must be None">()));
                        }
                        popen_args = py_tuple({ program }) + args;
                        protocol = protocol_factory();
                        debug_log = none();
                        if (self.attr("_debug").truthy())
                        {
                            // don't log parameters: they may contain sensitive information
                            // (password) and may be too long
                            debug_log = S(std::string{ "execute program " } + program.repr());
                            self.method("_log_subprocess", { debug_log, stdin_, stdout_, stderr_ });
                        }
                        transport = py_await(call_ex(self.attr("_make_subprocess_transport"), { protocol, popen_args, boolean(false), stdin_, stdout_, stderr_, bufsize }, py{}, kwargs));
                        if ((self.attr("_debug").truthy() && (!debug_log.is_none())))
                        {
                            (G<"log.logger">()).method("info", { S<"%s: %r">(), debug_log, transport });
                        }
                        return py_tuple({ transport, protocol });
                    }, true, true) },
                { "get_exception_handler", w.method("BaseEventLoop", "get_exception_handler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_exception_handler");
                    }) },
                { "set_exception_handler", w.method("BaseEventLoop", "set_exception_handler", { py_positional("handler") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handler = a[1];
                        if (((!handler.is_none()) && (!callable(handler))))
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "A callable object or None is expected, got " } + handler.repr())));
                        }
                        self.set_attr("_exception_handler", handler);
                        return none();
                    }) },
                { "default_exception_handler", w.method("BaseEventLoop", "default_exception_handler", { py_positional("context") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py context = a[1];
                        py exc_info;
                        py exception;
                        py key;
                        py log_lines;
                        py message;
                        py tb;
                        py value;
                        message = context.method("get", { S<"message">() });
                        if (!message.truthy())
                        {
                            message = S<"Unhandled exception in event loop">();
                        }
                        exception = context.method("get", { S<"exception">() });
                        if (!exception.is_none())
                        {
                            exc_info = py_tuple({ (B<"type">())(exception), exception, exception.attr("__traceback__") });
                        }
                        else
                        {
                            exc_info = boolean(false);
                        }
                        if (((!context.contains((S<"source_traceback">()))) && (!self.attr("_current_handle").is_none()) && self.attr("_current_handle").attr("_source_traceback").truthy()))
                        {
                            context.set_item(S<"handle_traceback">(), self.attr("_current_handle").attr("_source_traceback"));
                        }
                        log_lines = py_list({ message });
                        for (py const& item : iterate{ (B<"sorted">())(context) })
                        {
                            key = item;
                            if (set_of({ S<"message">(), S<"exception">() }).contains(key))
                            {
                                continue;
                            }
                            value = context[key];
                            if (key == (S<"source_traceback">()))
                            {
                                tb = (S<"">()).method("join", { (Mod<"traceback">().attr("format_list"))(value) });
                                value = S<"Object created at (most recent call last):\n">();
                                value = inplace(NEOS_PY_ADD, value, tb.method("rstrip"));
                            }
                            else if (key == (S<"handle_traceback">()))
                            {
                                tb = (S<"">()).method("join", { (Mod<"traceback">().attr("format_list"))(value) });
                                value = S<"Handle created at (most recent call last):\n">();
                                value = inplace(NEOS_PY_ADD, value, tb.method("rstrip"));
                            }
                            else
                            {
                                value = S(value.repr());
                            }
                            log_lines.method("append", { S(key.str() + ": " + value.str()) });
                        }
                        (G<"log.logger">()).method("error", { (S<"\n">()).method("join", { log_lines }) }, { { "exc_info", exc_info } });
                        return none();
                    }) },
                { "call_exception_handler", w.method("BaseEventLoop", "call_exception_handler", { py_positional("context") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py context = a[1];
                        py ctx;
                        py exc;
                        py thing;
                        if (self.attr("_exception_handler").is_none())
                        {
                            try
                            {
                                self.method("default_exception_handler", { context });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler68;
                                if (matches(handler68.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler68.exception);
                                }
                                else if (matches(handler68.exception, B<"BaseException">()))
                                {
                                    // Second protection layer for unexpected errors
                                    // in the default implementation, as well as for subclassed
                                    // event loops with overloaded "default_exception_handler".
                                    (G<"log.logger">()).method("error", { S<"Exception in default exception handler">() }, { { "exc_info", boolean(true) } });
                                }
                                else
                                    py_reraise(handler68.exception);
                            }
                        }
                        else
                        {
                            try
                            {
                                ctx = none();
                                thing = context.method("get", { S<"task">() });
                                if (thing.is_none())
                                {
                                    // Even though Futures don't have a context,
                                    // Task is a subclass of Future,
                                    // and sometimes the 'future' key holds a Task.
                                    thing = context.method("get", { S<"future">() });
                                }
                                if (thing.is_none())
                                {
                                    // Handles also have a context.
                                    thing = context.method("get", { S<"handle">() });
                                }
                                if (((!thing.is_none()) && has_attr(thing, "get_context")))
                                {
                                    ctx = thing.method("get_context");
                                }
                                if (((!ctx.is_none()) && has_attr(ctx, "run")))
                                {
                                    ctx.method("run", { self.attr("_exception_handler"), self, context });
                                }
                                else
                                {
                                    self.method("_exception_handler", { self, context });
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler69;
                                if (matches(handler69.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler69.exception);
                                }
                                else if (matches(handler69.exception, B<"BaseException">()))
                                {
                                    exc = handler69.exception;
                                    // Exception in the user set custom exception handler.
                                    try
                                    {
                                        // Let's try default handler.
                                        self.method("default_exception_handler", { dict_of({ { S<"message">(), S<"Unhandled error in exception handler">() }, { S<"exception">(), exc }, { S<"context">(), context } }) });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler70;
                                        if (matches(handler70.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                        {
                                            py_reraise(handler70.exception);
                                        }
                                        else if (matches(handler70.exception, B<"BaseException">()))
                                        {
                                            // Guard 'default_exception_handler' in case it is
                                            // overloaded.
                                            (G<"log.logger">()).method("error", { S<"Exception in default exception handler while handling an unexpected error in custom exception handler">() }, { { "exc_info", boolean(true) } });
                                        }
                                        else
                                            py_reraise(handler70.exception);
                                    }
                                }
                                else
                                    py_reraise(handler69.exception);
                            }
                        }
                        return none();
                    }) },
                { "_add_callback", w.method("BaseEventLoop", "_add_callback", { py_positional("handle") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handle = a[1];
                        if (!handle.attr("_cancelled").truthy())
                        {
                            self.attr("_ready").method("append", { handle });
                        }
                        return none();
                    }) },
                { "_add_callback_signalsafe", w.method("BaseEventLoop", "_add_callback_signalsafe", { py_positional("handle") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handle = a[1];
                        self.method("_add_callback", { handle });
                        self.method("_write_to_self");
                        return none();
                    }) },
                { "_timer_handle_cancelled", w.method("BaseEventLoop", "_timer_handle_cancelled", { py_positional("handle") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handle = a[1];
                        if (handle.attr("_scheduled").truthy())
                        {
                            {
                                py const target71 = self;
                                target71.set_attr("_timer_cancelled_count", inplace(NEOS_PY_ADD, target71.attr("_timer_cancelled_count"), I<1>()));
                            }
                        }
                        return none();
                    }) },
                { "_run_once", w.method("BaseEventLoop", "_run_once", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dt;
                        py end_time;
                        py event_list;
                        py handle;
                        py i;
                        py new_scheduled;
                        py ntodo;
                        py sched_count;
                        py t0;
                        py timeout;
                        sched_count = py{ self.attr("_scheduled").size() };
                        if (((sched_count > (G<"base_events._MIN_SCHEDULED_TIMER_HANDLES">())) && ((self.attr("_timer_cancelled_count") / sched_count) > (G<"base_events._MIN_CANCELLED_TIMER_HANDLES_FRACTION">()))))
                        {
                            // Remove delayed calls that were cancelled if their number
                            // is too high
                            new_scheduled = py_list();
                            for (py const& item : iterate{ self.attr("_scheduled") })
                            {
                                handle = item;
                                if (handle.attr("_cancelled").truthy())
                                {
                                    handle.set_attr("_scheduled", boolean(false));
                                }
                                else
                                {
                                    new_scheduled.method("append", { handle });
                                }
                            }
                            (Mod<"heapq">().attr("heapify"))(new_scheduled);
                            self.set_attr("_scheduled", new_scheduled);
                            self.set_attr("_timer_cancelled_count", I<0>());
                        }
                        else
                        {
                            // Remove delayed calls that were cancelled from head of queue.
                            while ((self.attr("_scheduled").truthy() && self.attr("_scheduled")[I<0>()].attr("_cancelled").truthy()))
                            {
                                {
                                    py const target74 = self;
                                    target74.set_attr("_timer_cancelled_count", inplace(NEOS_PY_SUBTRACT, target74.attr("_timer_cancelled_count"), I<1>()));
                                }
                                handle = (Mod<"heapq">().attr("heappop"))(self.attr("_scheduled"));
                                handle.set_attr("_scheduled", boolean(false));
                            }
                        }
                        timeout = none();
                        if ((self.attr("_ready").truthy() || self.attr("_stopping").truthy()))
                        {
                            timeout = I<0>();
                        }
                        else if (self.attr("_scheduled").truthy())
                        {
                            // Compute the desired timeout.
                            timeout = self.attr("_scheduled")[I<0>()].attr("_when") - self.method("time");
                            if (timeout > (G<"base_events.MAXIMUM_SELECT_TIMEOUT">()))
                            {
                                timeout = G<"base_events.MAXIMUM_SELECT_TIMEOUT">();
                            }
                            else if (timeout < (I<0>()))
                            {
                                timeout = I<0>();
                            }
                        }
                        event_list = self.attr("_selector").method("select", { timeout });
                        self.method("_process_events", { event_list });
                        // Needed to break cycles when an exception occurs.
                        event_list = none();
                        // Handle 'later' callbacks that are ready.
                        end_time = self.method("time") + self.attr("_clock_resolution");
                        while (self.attr("_scheduled").truthy())
                        {
                            handle = self.attr("_scheduled")[I<0>()];
                            if (handle.attr("_when") >= end_time)
                            {
                                break;
                            }
                            handle = (Mod<"heapq">().attr("heappop"))(self.attr("_scheduled"));
                            handle.set_attr("_scheduled", boolean(false));
                            self.attr("_ready").method("append", { handle });
                        }
                        // This is the only place where callbacks are actually *called*.
                        // All other places just add them to ready.
                        // Note: We run all currently scheduled callbacks, but not any
                        // callbacks scheduled by callbacks run this time around --
                        // they will be run the next time (after another I/O poll).
                        // Use an idiom that is thread-safe without using locks.
                        ntodo = py{ self.attr("_ready").size() };
                        for (py const& item : iterate{ (B<"range">())(ntodo) })
                        {
                            i = item;
                            handle = self.attr("_ready").method("popleft");
                            if (handle.attr("_cancelled").truthy())
                            {
                                continue;
                            }
                            if (self.attr("_debug").truthy())
                            {
                                {
                                    std::optional<py> pending77;
                                    [[maybe_unused]] int flow77 = 0;
                                    [[maybe_unused]] py result77;
                                    try
                                    {
                                        self.set_attr("_current_handle", handle);
                                        t0 = self.method("time");
                                        handle.method("_run");
                                        dt = self.method("time") - t0;
                                        if (dt >= self.attr("slow_callback_duration"))
                                        {
                                            (G<"log.logger">()).method("warning", { S<"Executing %s took %.3f seconds">(), (G<"base_events._format_handle">())(handle), dt });
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pending77 = py_take_exception();
                                    }
                                    self.set_attr("_current_handle", none());
                                    if (pending77)
                                        py_reraise(*pending77);
                                    if (flow77 == 1)
                                        return result77;
                                    if (flow77 == 2)
                                        break;
                                    if (flow77 == 3)
                                        continue;
                                }
                            }
                            else
                            {
                                handle.method("_run");
                            }
                        }
                        handle = none();
                        return none();
                    }) },
                { "_set_coroutine_origin_tracking", w.method("BaseEventLoop", "_set_coroutine_origin_tracking", { py_positional("enabled") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py enabled = a[1];
                        if ((B<"bool">())(enabled) == (B<"bool">())(self.attr("_coroutine_origin_tracking_enabled")))
                        {
                            return none();
                        }
                        if (enabled.truthy())
                        {
                            self.set_attr("_coroutine_origin_tracking_saved_depth", (Mod<"sys">().attr("get_coroutine_origin_tracking_depth"))());
                            (Mod<"sys">().attr("set_coroutine_origin_tracking_depth"))(G<"constants.DEBUG_STACK_DEPTH">());
                        }
                        else
                        {
                            (Mod<"sys">().attr("set_coroutine_origin_tracking_depth"))(self.attr("_coroutine_origin_tracking_saved_depth"));
                        }
                        self.set_attr("_coroutine_origin_tracking_enabled", enabled);
                        return none();
                    }) },
                { "get_debug", w.method("BaseEventLoop", "get_debug", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_debug");
                    }) },
                { "set_debug", w.method("BaseEventLoop", "set_debug", { py_positional("enabled") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py enabled = a[1];
                        self.set_attr("_debug", enabled);
                        if (self.method("is_running").truthy())
                        {
                            self.method("call_soon_threadsafe", { self.attr("_set_coroutine_origin_tracking"), enabled });
                        }
                        return none();
                    }) }, }));
        }

        // --- selector_events (Lib/asyncio/selector_events.py)

        void make_selector_events()
        {
            static module_writer const& w = writer("selector_events");
            define("selector_events", "__all__", py_tuple({ S<"BaseSelectorEventLoop">() }));
            define("selector_events", "_HAS_SENDMSG", (B<"hasattr">())(Mod<"socket">().attr("socket"), S<"sendmsg">()));
            if ((G<"selector_events._HAS_SENDMSG">()).truthy())
            {
            }
            // Fallback to send
            define("selector_events", "_test_selector_event", w.function("_test_selector_event", "_test_selector_event", { py_positional("selector"), py_positional("fd"), py_positional("event") }, [](arguments a) -> py
                {
                    py selector = a[0];
                    py fd = a[1];
                    py event = a[2];
                    py key;
                    {
                        bool otherwise1 = false;
                        try
                        {
                            key = selector.method("get_key", { fd });
                            otherwise1 = true;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            if (matches(handler1.exception, B<"KeyError">()))
                            {
                                return boolean(false);
                            }
                            else
                                py_reraise(handler1.exception);
                        }
                        if (otherwise1)
                        {
                            return (B<"bool">())(key.attr("events") & event);
                        }
                    }
                    return none();
                }));

            define("selector_events", "BaseSelectorEventLoop", w.make_class("BaseSelectorEventLoop", { G<"base_events.BaseEventLoop">() }, {
                { "__init__", w.method("BaseSelectorEventLoop", "__init__", { py_positional("selector", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py selector = a[1];
                        (B<"super">()(G<"selector_events.BaseSelectorEventLoop">(), self)).method("__init__");
                        if (selector.is_none())
                        {
                            selector = (Mod<"selectors">().attr("DefaultSelector"))();
                        }
                        (G<"log.logger">()).method("debug", { S<"Using selector: %s">(), selector.attr("__class__").attr("__name__") });
                        self.set_attr("_selector", selector);
                        self.method("_make_self_pipe");
                        self.set_attr("_transports", (Mod<"weakref">().attr("WeakValueDictionary"))());
                        return none();
                    }) },
                { "_make_socket_transport", w.method("BaseSelectorEventLoop", "_make_socket_transport", { py_positional("sock"), py_positional("protocol"), py_positional("waiter", none()), py_keyword("extra", none()), py_keyword("server", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py protocol = a[2];
                        py waiter = a[3];
                        py extra = a[4];
                        py server = a[5];
                        self.method("_ensure_fd_no_transport", { sock });
                        return (G<"selector_events._SelectorSocketTransport">())(self, sock, protocol, waiter, extra, server);
                    }) },
                { "_make_ssl_transport", w.method("BaseSelectorEventLoop", "_make_ssl_transport", { py_positional("rawsock"), py_positional("protocol"), py_positional("sslcontext"), py_positional("waiter", none()), py_keyword("server_side", boolean(false)), py_keyword("server_hostname", none()), py_keyword("extra", none()), py_keyword("server", none()), py_keyword("ssl_handshake_timeout", G<"constants.SSL_HANDSHAKE_TIMEOUT">()), py_keyword("ssl_shutdown_timeout", G<"constants.SSL_SHUTDOWN_TIMEOUT">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py rawsock = a[1];
                        py protocol = a[2];
                        py sslcontext = a[3];
                        py waiter = a[4];
                        py server_side = a[5];
                        py server_hostname = a[6];
                        py extra = a[7];
                        py server = a[8];
                        py ssl_handshake_timeout = a[9];
                        py ssl_shutdown_timeout = a[10];
                        py ssl_protocol;
                        self.method("_ensure_fd_no_transport", { rawsock });
                        ssl_protocol = (G<"sslproto.SSLProtocol">()).call({ self, protocol, sslcontext, waiter, server_side, server_hostname }, { { "ssl_handshake_timeout", ssl_handshake_timeout }, { "ssl_shutdown_timeout", ssl_shutdown_timeout } });
                        (G<"selector_events._SelectorSocketTransport">()).call({ self, rawsock, ssl_protocol }, { { "extra", extra }, { "server", server } });
                        return ssl_protocol.attr("_app_transport");
                    }) },
                { "_make_datagram_transport", w.method("BaseSelectorEventLoop", "_make_datagram_transport", { py_positional("sock"), py_positional("protocol"), py_positional("address", none()), py_positional("waiter", none()), py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py protocol = a[2];
                        py address = a[3];
                        py waiter = a[4];
                        py extra = a[5];
                        self.method("_ensure_fd_no_transport", { sock });
                        return (G<"selector_events._SelectorDatagramTransport">())(self, sock, protocol, address, waiter, extra);
                    }) },
                { "close", w.method("BaseSelectorEventLoop", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.method("is_running").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Cannot close a running event loop">()));
                        }
                        if (self.method("is_closed").truthy())
                        {
                            return none();
                        }
                        self.method("_close_self_pipe");
                        (B<"super">()(G<"selector_events.BaseSelectorEventLoop">(), self)).method("close");
                        if (!self.attr("_selector").is_none())
                        {
                            self.attr("_selector").method("close");
                            self.set_attr("_selector", none());
                        }
                        return none();
                    }) },
                { "_close_self_pipe", w.method("BaseSelectorEventLoop", "_close_self_pipe", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_remove_reader", { self.attr("_ssock").method("fileno") });
                        self.attr("_ssock").method("close");
                        self.set_attr("_ssock", none());
                        self.attr("_csock").method("close");
                        self.set_attr("_csock", none());
                        {
                            py const target2 = self;
                            target2.set_attr("_internal_fds", inplace(NEOS_PY_SUBTRACT, target2.attr("_internal_fds"), I<1>()));
                        }
                        return none();
                    }) },
                { "_make_self_pipe", w.method("BaseSelectorEventLoop", "_make_self_pipe", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        auto const items3 = unpack((Mod<"socket">().attr("socketpair"))(), 2);
                        self.set_attr("_ssock", items3[0]);
                        self.set_attr("_csock", items3[1]);
                        self.attr("_ssock").method("setblocking", { boolean(false) });
                        self.attr("_csock").method("setblocking", { boolean(false) });
                        {
                            py const target4 = self;
                            target4.set_attr("_internal_fds", inplace(NEOS_PY_ADD, target4.attr("_internal_fds"), I<1>()));
                        }
                        self.method("_add_reader", { self.attr("_ssock").method("fileno"), self.attr("_read_from_self") });
                        return none();
                    }) },
                { "_process_self_data", w.method("BaseSelectorEventLoop", "_process_self_data", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        return none();
                    }) },
                { "_read_from_self", w.method("BaseSelectorEventLoop", "_read_from_self", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data;
                        while (boolean(true).truthy())
                        {
                            try
                            {
                                data = self.attr("_ssock").method("recv", { I<4096>() });
                                if (!data.truthy())
                                {
                                    break;
                                }
                                self.method("_process_self_data", { data });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler6;
                                if (matches(handler6.exception, B<"InterruptedError">()))
                                {
                                    continue;
                                }
                                else if (matches(handler6.exception, B<"BlockingIOError">()))
                                {
                                    break;
                                }
                                else
                                    py_reraise(handler6.exception);
                            }
                        }
                        return none();
                    }) },
                { "_write_to_self", w.method("BaseSelectorEventLoop", "_write_to_self", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py csock;
                        csock = self.attr("_csock");
                        if (csock.is_none())
                        {
                            return none();
                        }
                        try
                        {
                            csock.method("send", { py{ make_bytes(std::string{ "\x00", 1 }) } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler7;
                            if (matches(handler7.exception, B<"OSError">()))
                            {
                                if (self.attr("_debug").truthy())
                                {
                                    (G<"log.logger">()).method("debug", { S<"Fail to write a null byte into the self-pipe socket">() }, { { "exc_info", boolean(true) } });
                                }
                            }
                            else
                                py_reraise(handler7.exception);
                        }
                        return none();
                    }) },
                { "_start_serving", w.method("BaseSelectorEventLoop", "_start_serving", { py_positional("protocol_factory"), py_positional("sock"), py_positional("sslcontext", none()), py_positional("server", none()), py_positional("backlog", I<100>()), py_positional("ssl_handshake_timeout", G<"constants.SSL_HANDSHAKE_TIMEOUT">()), py_positional("ssl_shutdown_timeout", G<"constants.SSL_SHUTDOWN_TIMEOUT">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol_factory = a[1];
                        py sock = a[2];
                        py sslcontext = a[3];
                        py server = a[4];
                        py backlog = a[5];
                        py ssl_handshake_timeout = a[6];
                        py ssl_shutdown_timeout = a[7];
                        self.method("_add_reader", { sock.method("fileno"), self.attr("_accept_connection"), protocol_factory, sock, sslcontext, server, backlog, ssl_handshake_timeout, ssl_shutdown_timeout });
                        return none();
                    }) },
                { "_accept_connection", w.method("BaseSelectorEventLoop", "_accept_connection", { py_positional("protocol_factory"), py_positional("sock"), py_positional("sslcontext", none()), py_positional("server", none()), py_positional("backlog", I<100>()), py_positional("ssl_handshake_timeout", G<"constants.SSL_HANDSHAKE_TIMEOUT">()), py_positional("ssl_shutdown_timeout", G<"constants.SSL_SHUTDOWN_TIMEOUT">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol_factory = a[1];
                        py sock = a[2];
                        py sslcontext = a[3];
                        py server = a[4];
                        py backlog = a[5];
                        py ssl_handshake_timeout = a[6];
                        py ssl_shutdown_timeout = a[7];
                        py _;
                        py accept;
                        py addr;
                        py conn;
                        py exc;
                        py extra;
                        for (py const& item : iterate{ (B<"range">())(backlog + (I<1>())) })
                        {
                            _ = item;
                            {
                                bool otherwise9 = false;
                                try
                                {
                                    auto const items10 = unpack(sock.method("accept"), 2);
                                    conn = items10[0];
                                    addr = items10[1];
                                    if (self.attr("_debug").truthy())
                                    {
                                        (G<"log.logger">()).method("debug", { S<"%r got a new connection from %r: %r">(), server, addr, conn });
                                    }
                                    conn.method("setblocking", { boolean(false) });
                                    otherwise9 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler9;
                                    if (matches(handler9.exception, B<"ConnectionAbortedError">()))
                                    {
                                        // Discard connections that were aborted before accept().
                                        continue;
                                    }
                                    else if (matches(handler9.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                    {
                                        // Early exit because of a signal or
                                        // the socket accept buffer is empty.
                                        return none();
                                    }
                                    else if (matches(handler9.exception, B<"OSError">()))
                                    {
                                        exc = handler9.exception;
                                        // There's nowhere to send the error, so just log it.
                                        if (py_tuple({ Mod<"errno">().attr("EMFILE"), Mod<"errno">().attr("ENFILE"), Mod<"errno">().attr("ENOBUFS"), Mod<"errno">().attr("ENOMEM") }).contains(exc.attr("errno")))
                                        {
                                            // Some platforms (e.g. Linux keep reporting the FD as
                                            // ready, so we remove the read handler temporarily.
                                            // We'll try again in a while.
                                            self.method("call_exception_handler", { dict_of({ { S<"message">(), S<"socket.accept() out of system resource">() }, { S<"exception">(), exc }, { S<"socket">(), (G<"trsock.TransportSocket">())(sock) } }) });
                                            self.method("_remove_reader", { sock.method("fileno") });
                                            self.method("call_later", { G<"constants.ACCEPT_RETRY_DELAY">(), self.attr("_start_serving"), protocol_factory, sock, sslcontext, server, backlog, ssl_handshake_timeout, ssl_shutdown_timeout });
                                        }
                                        else
                                        {
                                            py_reraise(handler9.exception);
                                        }
                                    }
                                    else
                                        py_reraise(handler9.exception);
                                }
                                if (otherwise9)
                                {
                                    extra = dict_of({ { S<"peername">(), addr } });
                                    accept = self.method("_accept_connection2", { protocol_factory, conn, extra, sslcontext, server, ssl_handshake_timeout, ssl_shutdown_timeout });
                                    self.method("create_task", { accept });
                                }
                            }
                        }
                        return none();
                    }) },
                { "_accept_connection2", async_method(w, "BaseSelectorEventLoop", "_accept_connection2", { py_positional("protocol_factory"), py_positional("conn"), py_positional("extra"), py_positional("sslcontext", none()), py_positional("server", none()), py_positional("ssl_handshake_timeout", G<"constants.SSL_HANDSHAKE_TIMEOUT">()), py_positional("ssl_shutdown_timeout", G<"constants.SSL_SHUTDOWN_TIMEOUT">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol_factory = a[1];
                        py conn = a[2];
                        py extra = a[3];
                        py sslcontext = a[4];
                        py server = a[5];
                        py ssl_handshake_timeout = a[6];
                        py ssl_shutdown_timeout = a[7];
                        py context;
                        py exc;
                        py protocol;
                        py transport;
                        py waiter;
                        protocol = none();
                        transport = none();
                        try
                        {
                            protocol = protocol_factory();
                            waiter = self.method("create_future");
                            if (sslcontext.truthy())
                            {
                                transport = self.method("_make_ssl_transport", { conn, protocol, sslcontext }, { { "waiter", waiter }, { "server_side", boolean(true) }, { "extra", extra }, { "server", server }, { "ssl_handshake_timeout", ssl_handshake_timeout }, { "ssl_shutdown_timeout", ssl_shutdown_timeout } });
                            }
                            else
                            {
                                transport = self.method("_make_socket_transport", { conn, protocol }, { { "waiter", waiter }, { "extra", extra }, { "server", server } });
                            }
                            try
                            {
                                py_await(waiter);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler12;
                                if (matches(handler12.exception, B<"BaseException">()))
                                {
                                    transport.method("close");
                                    // gh-109534: When an exception is raised by the SSLProtocol object the
                                    // exception set in this future can keep the protocol object alive and
                                    // cause a reference cycle.
                                    waiter = none();
                                    py_reraise(handler12.exception);
                                }
                                else
                                    py_reraise(handler12.exception);
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler11;
                            // It's now up to the protocol to handle the connection.
                            if (matches(handler11.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                            {
                                py_reraise(handler11.exception);
                            }
                            else if (matches(handler11.exception, B<"BaseException">()))
                            {
                                exc = handler11.exception;
                                if (self.attr("_debug").truthy())
                                {
                                    context = dict_of({ { S<"message">(), S<"Error on transport creation for incoming connection">() }, { S<"exception">(), exc } });
                                    if (!protocol.is_none())
                                    {
                                        context.set_item(S<"protocol">(), protocol);
                                    }
                                    if (!transport.is_none())
                                    {
                                        context.set_item(S<"transport">(), transport);
                                    }
                                    self.method("call_exception_handler", { context });
                                }
                            }
                            else
                                py_reraise(handler11.exception);
                        }
                        return none();
                    }) },
                { "_ensure_fd_no_transport", w.method("BaseSelectorEventLoop", "_ensure_fd_no_transport", { py_positional("fd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py fileno;
                        py transport;
                        fileno = fd;
                        if (!isinstance(fileno, B<"int">()))
                        {
                            try
                            {
                                fileno = (B<"int">())(fileno.method("fileno"));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler13;
                                if (matches(handler13.exception, py_tuple({ B<"AttributeError">(), B<"TypeError">(), B<"ValueError">() })))
                                {
                                    // This code matches selectors._fileobj_to_fd function.
                                    fn_raise_from((B<"ValueError">())(S(std::string{ "Invalid file object: " } + fd.repr())).get(), none().get());
                                    throw raised{};
                                }
                                else
                                    py_reraise(handler13.exception);
                            }
                        }
                        transport = self.attr("_transports").method("get", { fileno });
                        if ((transport.truthy() && (!transport.method("is_closing").truthy())))
                        {
                            py_raise((B<"RuntimeError">())(S(std::string{ "File descriptor " } + fd.repr() + " is used by transport " + transport.repr())));
                        }
                        return none();
                    }) },
                { "_add_reader", w.method("BaseSelectorEventLoop", "_add_reader", { py_positional("fd"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py callback = a[2];
                        py args = a[3];
                        py handle;
                        py key;
                        py mask;
                        py reader;
                        py writer;
                        self.method("_check_closed");
                        handle = (G<"events.Handle">())(callback, args, self, none());
                        key = self.attr("_selector").method("get_map").method("get", { fd });
                        if (key.is_none())
                        {
                            self.attr("_selector").method("register", { fd, Mod<"selectors">().attr("EVENT_READ"), py_tuple({ handle, none() }) });
                        }
                        else
                        {
                            auto const items14 = unpack(py_tuple({ key.attr("events"), key.attr("data") }), 2);
                            mask = items14[0];
                            auto const items15 = unpack(items14[1], 2);
                            reader = items15[0];
                            writer = items15[1];
                            self.attr("_selector").method("modify", { fd, mask | (Mod<"selectors">().attr("EVENT_READ")), py_tuple({ handle, writer }) });
                            if (!reader.is_none())
                            {
                                reader.method("cancel");
                            }
                        }
                        return handle;
                    }, true) },
                { "_remove_reader", w.method("BaseSelectorEventLoop", "_remove_reader", { py_positional("fd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py key;
                        py mask;
                        py reader;
                        py writer;
                        if (self.method("is_closed").truthy())
                        {
                            return boolean(false);
                        }
                        key = self.attr("_selector").method("get_map").method("get", { fd });
                        if (key.is_none())
                        {
                            return boolean(false);
                        }
                        auto const items16 = unpack(py_tuple({ key.attr("events"), key.attr("data") }), 2);
                        mask = items16[0];
                        auto const items17 = unpack(items16[1], 2);
                        reader = items17[0];
                        writer = items17[1];
                        mask = inplace(NEOS_PY_AND, mask, py{ unary(NEOS_PY_INVERT, (Mod<"selectors">().attr("EVENT_READ")).get()) });
                        if (!mask.truthy())
                        {
                            self.attr("_selector").method("unregister", { fd });
                        }
                        else
                        {
                            self.attr("_selector").method("modify", { fd, mask, py_tuple({ none(), writer }) });
                        }
                        if (!reader.is_none())
                        {
                            reader.method("cancel");
                            return boolean(true);
                        }
                        else
                        {
                            return boolean(false);
                        }
                        return none();
                    }) },
                { "_add_writer", w.method("BaseSelectorEventLoop", "_add_writer", { py_positional("fd"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py callback = a[2];
                        py args = a[3];
                        py handle;
                        py key;
                        py mask;
                        py reader;
                        py writer;
                        self.method("_check_closed");
                        handle = (G<"events.Handle">())(callback, args, self, none());
                        key = self.attr("_selector").method("get_map").method("get", { fd });
                        if (key.is_none())
                        {
                            self.attr("_selector").method("register", { fd, Mod<"selectors">().attr("EVENT_WRITE"), py_tuple({ none(), handle }) });
                        }
                        else
                        {
                            auto const items18 = unpack(py_tuple({ key.attr("events"), key.attr("data") }), 2);
                            mask = items18[0];
                            auto const items19 = unpack(items18[1], 2);
                            reader = items19[0];
                            writer = items19[1];
                            self.attr("_selector").method("modify", { fd, mask | (Mod<"selectors">().attr("EVENT_WRITE")), py_tuple({ reader, handle }) });
                            if (!writer.is_none())
                            {
                                writer.method("cancel");
                            }
                        }
                        return handle;
                    }, true) },
                { "_remove_writer", w.method("BaseSelectorEventLoop", "_remove_writer", { py_positional("fd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py key;
                        py mask;
                        py reader;
                        py writer;
                        if (self.method("is_closed").truthy())
                        {
                            return boolean(false);
                        }
                        key = self.attr("_selector").method("get_map").method("get", { fd });
                        if (key.is_none())
                        {
                            return boolean(false);
                        }
                        auto const items20 = unpack(py_tuple({ key.attr("events"), key.attr("data") }), 2);
                        mask = items20[0];
                        auto const items21 = unpack(items20[1], 2);
                        reader = items21[0];
                        writer = items21[1];
                        // Remove both writer and connector.
                        mask = inplace(NEOS_PY_AND, mask, py{ unary(NEOS_PY_INVERT, (Mod<"selectors">().attr("EVENT_WRITE")).get()) });
                        if (!mask.truthy())
                        {
                            self.attr("_selector").method("unregister", { fd });
                        }
                        else
                        {
                            self.attr("_selector").method("modify", { fd, mask, py_tuple({ reader, none() }) });
                        }
                        if (!writer.is_none())
                        {
                            writer.method("cancel");
                            return boolean(true);
                        }
                        else
                        {
                            return boolean(false);
                        }
                        return none();
                    }) },
                { "add_reader", w.method("BaseSelectorEventLoop", "add_reader", { py_positional("fd"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py callback = a[2];
                        py args = a[3];
                        self.method("_ensure_fd_no_transport", { fd });
                        call_ex(self.attr("_add_reader"), { fd, callback }, args, py{});
                        return none();
                    }, true) },
                { "remove_reader", w.method("BaseSelectorEventLoop", "remove_reader", { py_positional("fd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        self.method("_ensure_fd_no_transport", { fd });
                        return self.method("_remove_reader", { fd });
                    }) },
                { "add_writer", w.method("BaseSelectorEventLoop", "add_writer", { py_positional("fd"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py callback = a[2];
                        py args = a[3];
                        self.method("_ensure_fd_no_transport", { fd });
                        call_ex(self.attr("_add_writer"), { fd, callback }, args, py{});
                        return none();
                    }, true) },
                { "remove_writer", w.method("BaseSelectorEventLoop", "remove_writer", { py_positional("fd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        self.method("_ensure_fd_no_transport", { fd });
                        return self.method("_remove_writer", { fd });
                    }) },
                { "sock_recv", async_method(w, "BaseSelectorEventLoop", "sock_recv", { py_positional("sock"), py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py n = a[2];
                        py fd;
                        py fut;
                        py handle;
                        (G<"base_events._check_ssl_socket">())(sock);
                        if ((self.attr("_debug").truthy() && (sock.method("gettimeout") != (I<0>()))))
                        {
                            py_raise((B<"ValueError">())(S<"the socket must be non-blocking">()));
                        }
                        try
                        {
                            return sock.method("recv", { n });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler22;
                            if (matches(handler22.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                            {
                            }
                            else
                                py_reraise(handler22.exception);
                        }
                        fut = self.method("create_future");
                        fd = sock.method("fileno");
                        self.method("_ensure_fd_no_transport", { fd });
                        handle = self.method("_add_reader", { fd, self.attr("_sock_recv"), fut, sock, n });
                        fut.method("add_done_callback", { (Mod<"functools">().attr("partial")).call({ self.attr("_sock_read_done"), fd }, { { "handle", handle } }) });
                        return py_await(fut);
                    }) },
                { "_sock_read_done", w.method("BaseSelectorEventLoop", "_sock_read_done", { py_positional("fd"), py_positional("fut"), py_positional("handle", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py fut = a[2];
                        py handle = a[3];
                        if ((handle.is_none() || (!handle.method("cancelled").truthy())))
                        {
                            self.method("remove_reader", { fd });
                        }
                        return none();
                    }) },
                { "_sock_recv", w.method("BaseSelectorEventLoop", "_sock_recv", { py_positional("fut"), py_positional("sock"), py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut = a[1];
                        py sock = a[2];
                        py n = a[3];
                        py data;
                        py exc;
                        if (fut.method("done").truthy())
                        {
                            return none();
                        }
                        {
                            bool otherwise23 = false;
                            try
                            {
                                data = sock.method("recv", { n });
                                otherwise23 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler23;
                                if (matches(handler23.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                    return none();
                                }
                                else if (matches(handler23.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler23.exception);
                                }
                                else if (matches(handler23.exception, B<"BaseException">()))
                                {
                                    exc = handler23.exception;
                                    fut.method("set_exception", { exc });
                                }
                                else
                                    py_reraise(handler23.exception);
                            }
                            if (otherwise23)
                            {
                                fut.method("set_result", { data });
                            }
                        }
                        return none();
                    }) },
                { "sock_recv_into", async_method(w, "BaseSelectorEventLoop", "sock_recv_into", { py_positional("sock"), py_positional("buf") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py buf = a[2];
                        py fd;
                        py fut;
                        py handle;
                        (G<"base_events._check_ssl_socket">())(sock);
                        if ((self.attr("_debug").truthy() && (sock.method("gettimeout") != (I<0>()))))
                        {
                            py_raise((B<"ValueError">())(S<"the socket must be non-blocking">()));
                        }
                        try
                        {
                            return sock.method("recv_into", { buf });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler24;
                            if (matches(handler24.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                            {
                            }
                            else
                                py_reraise(handler24.exception);
                        }
                        fut = self.method("create_future");
                        fd = sock.method("fileno");
                        self.method("_ensure_fd_no_transport", { fd });
                        handle = self.method("_add_reader", { fd, self.attr("_sock_recv_into"), fut, sock, buf });
                        fut.method("add_done_callback", { (Mod<"functools">().attr("partial")).call({ self.attr("_sock_read_done"), fd }, { { "handle", handle } }) });
                        return py_await(fut);
                    }) },
                { "_sock_recv_into", w.method("BaseSelectorEventLoop", "_sock_recv_into", { py_positional("fut"), py_positional("sock"), py_positional("buf") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut = a[1];
                        py sock = a[2];
                        py buf = a[3];
                        py exc;
                        py nbytes;
                        if (fut.method("done").truthy())
                        {
                            return none();
                        }
                        {
                            bool otherwise25 = false;
                            try
                            {
                                nbytes = sock.method("recv_into", { buf });
                                otherwise25 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler25;
                                if (matches(handler25.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                    return none();
                                }
                                else if (matches(handler25.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler25.exception);
                                }
                                else if (matches(handler25.exception, B<"BaseException">()))
                                {
                                    exc = handler25.exception;
                                    fut.method("set_exception", { exc });
                                }
                                else
                                    py_reraise(handler25.exception);
                            }
                            if (otherwise25)
                            {
                                fut.method("set_result", { nbytes });
                            }
                        }
                        return none();
                    }) },
                { "sock_recvfrom", async_method(w, "BaseSelectorEventLoop", "sock_recvfrom", { py_positional("sock"), py_positional("bufsize") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py bufsize = a[2];
                        py fd;
                        py fut;
                        py handle;
                        (G<"base_events._check_ssl_socket">())(sock);
                        if ((self.attr("_debug").truthy() && (sock.method("gettimeout") != (I<0>()))))
                        {
                            py_raise((B<"ValueError">())(S<"the socket must be non-blocking">()));
                        }
                        try
                        {
                            return sock.method("recvfrom", { bufsize });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler26;
                            if (matches(handler26.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                            {
                            }
                            else
                                py_reraise(handler26.exception);
                        }
                        fut = self.method("create_future");
                        fd = sock.method("fileno");
                        self.method("_ensure_fd_no_transport", { fd });
                        handle = self.method("_add_reader", { fd, self.attr("_sock_recvfrom"), fut, sock, bufsize });
                        fut.method("add_done_callback", { (Mod<"functools">().attr("partial")).call({ self.attr("_sock_read_done"), fd }, { { "handle", handle } }) });
                        return py_await(fut);
                    }) },
                { "_sock_recvfrom", w.method("BaseSelectorEventLoop", "_sock_recvfrom", { py_positional("fut"), py_positional("sock"), py_positional("bufsize") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut = a[1];
                        py sock = a[2];
                        py bufsize = a[3];
                        py exc;
                        py result;
                        if (fut.method("done").truthy())
                        {
                            return none();
                        }
                        {
                            bool otherwise27 = false;
                            try
                            {
                                result = sock.method("recvfrom", { bufsize });
                                otherwise27 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler27;
                                if (matches(handler27.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                    return none();
                                }
                                else if (matches(handler27.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler27.exception);
                                }
                                else if (matches(handler27.exception, B<"BaseException">()))
                                {
                                    exc = handler27.exception;
                                    fut.method("set_exception", { exc });
                                }
                                else
                                    py_reraise(handler27.exception);
                            }
                            if (otherwise27)
                            {
                                fut.method("set_result", { result });
                            }
                        }
                        return none();
                    }) },
                { "sock_recvfrom_into", async_method(w, "BaseSelectorEventLoop", "sock_recvfrom_into", { py_positional("sock"), py_positional("buf"), py_positional("nbytes", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py buf = a[2];
                        py nbytes = a[3];
                        py fd;
                        py fut;
                        py handle;
                        (G<"base_events._check_ssl_socket">())(sock);
                        if ((self.attr("_debug").truthy() && (sock.method("gettimeout") != (I<0>()))))
                        {
                            py_raise((B<"ValueError">())(S<"the socket must be non-blocking">()));
                        }
                        if (!nbytes.truthy())
                        {
                            nbytes = py{ buf.size() };
                        }
                        try
                        {
                            return sock.method("recvfrom_into", { buf, nbytes });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler28;
                            if (matches(handler28.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                            {
                            }
                            else
                                py_reraise(handler28.exception);
                        }
                        fut = self.method("create_future");
                        fd = sock.method("fileno");
                        self.method("_ensure_fd_no_transport", { fd });
                        handle = self.method("_add_reader", { fd, self.attr("_sock_recvfrom_into"), fut, sock, buf, nbytes });
                        fut.method("add_done_callback", { (Mod<"functools">().attr("partial")).call({ self.attr("_sock_read_done"), fd }, { { "handle", handle } }) });
                        return py_await(fut);
                    }) },
                { "_sock_recvfrom_into", w.method("BaseSelectorEventLoop", "_sock_recvfrom_into", { py_positional("fut"), py_positional("sock"), py_positional("buf"), py_positional("bufsize") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut = a[1];
                        py sock = a[2];
                        py buf = a[3];
                        py bufsize = a[4];
                        py exc;
                        py result;
                        if (fut.method("done").truthy())
                        {
                            return none();
                        }
                        {
                            bool otherwise29 = false;
                            try
                            {
                                result = sock.method("recvfrom_into", { buf, bufsize });
                                otherwise29 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler29;
                                if (matches(handler29.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                    return none();
                                }
                                else if (matches(handler29.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler29.exception);
                                }
                                else if (matches(handler29.exception, B<"BaseException">()))
                                {
                                    exc = handler29.exception;
                                    fut.method("set_exception", { exc });
                                }
                                else
                                    py_reraise(handler29.exception);
                            }
                            if (otherwise29)
                            {
                                fut.method("set_result", { result });
                            }
                        }
                        return none();
                    }) },
                { "sock_sendall", async_method(w, "BaseSelectorEventLoop", "sock_sendall", { py_positional("sock"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py data = a[2];
                        py fd;
                        py fut;
                        py handle;
                        py n;
                        (G<"base_events._check_ssl_socket">())(sock);
                        if ((self.attr("_debug").truthy() && (sock.method("gettimeout") != (I<0>()))))
                        {
                            py_raise((B<"ValueError">())(S<"the socket must be non-blocking">()));
                        }
                        try
                        {
                            n = sock.method("send", { data });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler30;
                            if (matches(handler30.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                            {
                                n = I<0>();
                            }
                            else
                                py_reraise(handler30.exception);
                        }
                        if (n == py{ data.size() })
                        {
                            // all data sent
                            return none();
                        }
                        fut = self.method("create_future");
                        fd = sock.method("fileno");
                        self.method("_ensure_fd_no_transport", { fd });
                        // use a trick with a list in closure to store a mutable state
                        handle = self.method("_add_writer", { fd, self.attr("_sock_sendall"), fut, sock, (B<"memoryview">())(data), py_list({ n }) });
                        fut.method("add_done_callback", { (Mod<"functools">().attr("partial")).call({ self.attr("_sock_write_done"), fd }, { { "handle", handle } }) });
                        return py_await(fut);
                    }) },
                { "_sock_sendall", w.method("BaseSelectorEventLoop", "_sock_sendall", { py_positional("fut"), py_positional("sock"), py_positional("view"), py_positional("pos") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut = a[1];
                        py sock = a[2];
                        py view = a[3];
                        py pos = a[4];
                        py exc;
                        py n;
                        py start;
                        if (fut.method("done").truthy())
                        {
                            // Future cancellation can be scheduled on previous loop iteration
                            return none();
                        }
                        start = pos[I<0>()];
                        try
                        {
                            n = sock.method("send", { view[B<"slice">()(start, none(), none())] });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler31;
                            if (matches(handler31.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                            {
                                return none();
                            }
                            else if (matches(handler31.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                            {
                                py_reraise(handler31.exception);
                            }
                            else if (matches(handler31.exception, B<"BaseException">()))
                            {
                                exc = handler31.exception;
                                fut.method("set_exception", { exc });
                                return none();
                            }
                            else
                                py_reraise(handler31.exception);
                        }
                        start = inplace(NEOS_PY_ADD, start, n);
                        if (start == py{ view.size() })
                        {
                            fut.method("set_result", { none() });
                        }
                        else
                        {
                            pos.set_item(I<0>(), start);
                        }
                        return none();
                    }) },
                { "sock_sendto", async_method(w, "BaseSelectorEventLoop", "sock_sendto", { py_positional("sock"), py_positional("data"), py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py data = a[2];
                        py address = a[3];
                        py fd;
                        py fut;
                        py handle;
                        (G<"base_events._check_ssl_socket">())(sock);
                        if ((self.attr("_debug").truthy() && (sock.method("gettimeout") != (I<0>()))))
                        {
                            py_raise((B<"ValueError">())(S<"the socket must be non-blocking">()));
                        }
                        try
                        {
                            return sock.method("sendto", { data, address });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler32;
                            if (matches(handler32.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                            {
                            }
                            else
                                py_reraise(handler32.exception);
                        }
                        fut = self.method("create_future");
                        fd = sock.method("fileno");
                        self.method("_ensure_fd_no_transport", { fd });
                        // use a trick with a list in closure to store a mutable state
                        handle = self.method("_add_writer", { fd, self.attr("_sock_sendto"), fut, sock, data, address });
                        fut.method("add_done_callback", { (Mod<"functools">().attr("partial")).call({ self.attr("_sock_write_done"), fd }, { { "handle", handle } }) });
                        return py_await(fut);
                    }) },
                { "_sock_sendto", w.method("BaseSelectorEventLoop", "_sock_sendto", { py_positional("fut"), py_positional("sock"), py_positional("data"), py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut = a[1];
                        py sock = a[2];
                        py data = a[3];
                        py address = a[4];
                        py exc;
                        py n;
                        if (fut.method("done").truthy())
                        {
                            // Future cancellation can be scheduled on previous loop iteration
                            return none();
                        }
                        {
                            bool otherwise33 = false;
                            try
                            {
                                n = sock.method("sendto", { data, I<0>(), address });
                                otherwise33 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler33;
                                if (matches(handler33.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                    return none();
                                }
                                else if (matches(handler33.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler33.exception);
                                }
                                else if (matches(handler33.exception, B<"BaseException">()))
                                {
                                    exc = handler33.exception;
                                    fut.method("set_exception", { exc });
                                }
                                else
                                    py_reraise(handler33.exception);
                            }
                            if (otherwise33)
                            {
                                fut.method("set_result", { n });
                            }
                        }
                        return none();
                    }) },
                { "sock_connect", async_method(w, "BaseSelectorEventLoop", "sock_connect", { py_positional("sock"), py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py address = a[2];
                        py _;
                        py fut;
                        py resolved;
                        (G<"base_events._check_ssl_socket">())(sock);
                        if ((self.attr("_debug").truthy() && (sock.method("gettimeout") != (I<0>()))))
                        {
                            py_raise((B<"ValueError">())(S<"the socket must be non-blocking">()));
                        }
                        if (((sock.attr("family") == (Mod<"socket">().attr("AF_INET"))) || ([&]() -> py { py const t = G<"base_events._HAS_IPv6">(); return !t.truthy() ? t : boolean(sock.attr("family") == (Mod<"socket">().attr("AF_INET6"))); }()).truthy()))
                        {
                            resolved = py_await(self.method("_ensure_resolved", { address }, { { "family", sock.attr("family") }, { "type", sock.attr("type") }, { "proto", sock.attr("proto") }, { "loop", self } }));
                            auto const items34 = unpack(resolved[I<0>()], 5);
                            _ = items34[0];
                            _ = items34[1];
                            _ = items34[2];
                            _ = items34[3];
                            address = items34[4];
                        }
                        fut = self.method("create_future");
                        self.method("_sock_connect", { fut, sock, address });
                        {
                            std::optional<py> pending35;
                            [[maybe_unused]] int flow35 = 0;
                            [[maybe_unused]] py result35;
                            try
                            {
                                result35 = py_await(fut);
                                flow35 = 1;
                                goto finally35;
                            }
                            catch (raised const&)
                            {
                                pending35 = py_take_exception();
                            }
                            finally35:
                            // Needed to break cycles when an exception occurs.
                            fut = none();
                            if (pending35)
                                py_reraise(*pending35);
                            if (flow35 == 1)
                                return result35;
                        }
                        return none();
                    }) },
                { "_sock_connect", w.method("BaseSelectorEventLoop", "_sock_connect", { py_positional("fut"), py_positional("sock"), py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut = a[1];
                        py sock = a[2];
                        py address = a[3];
                        py exc;
                        py fd;
                        py handle;
                        fd = sock.method("fileno");
                        {
                            std::optional<py> pending36;
                            [[maybe_unused]] int flow36 = 0;
                            [[maybe_unused]] py result36;
                            try
                            {
                                bool otherwise36 = false;
                                try
                                {
                                    sock.method("connect", { address });
                                    otherwise36 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler36;
                                    if (matches(handler36.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                    {
                                        // Issue #23618: When the C function connect() fails with EINTR, the
                                        // connection runs in background. We have to wait until the socket
                                        // becomes writable to be notified when the connection succeed or
                                        // fails.
                                        self.method("_ensure_fd_no_transport", { fd });
                                        handle = self.method("_add_writer", { fd, self.attr("_sock_connect_cb"), fut, sock, address });
                                        fut.method("add_done_callback", { (Mod<"functools">().attr("partial")).call({ self.attr("_sock_write_done"), fd }, { { "handle", handle } }) });
                                    }
                                    else if (matches(handler36.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                    {
                                        py_reraise(handler36.exception);
                                    }
                                    else if (matches(handler36.exception, B<"BaseException">()))
                                    {
                                        exc = handler36.exception;
                                        fut.method("set_exception", { exc });
                                    }
                                    else
                                        py_reraise(handler36.exception);
                                }
                                if (otherwise36)
                                {
                                    fut.method("set_result", { none() });
                                }
                            }
                            catch (raised const&)
                            {
                                pending36 = py_take_exception();
                            }
                            fut = none();
                            if (pending36)
                                py_reraise(*pending36);
                            if (flow36 == 1)
                                return result36;
                        }
                        return none();
                    }) },
                { "_sock_write_done", w.method("BaseSelectorEventLoop", "_sock_write_done", { py_positional("fd"), py_positional("fut"), py_positional("handle", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py fut = a[2];
                        py handle = a[3];
                        if ((handle.is_none() || (!handle.method("cancelled").truthy())))
                        {
                            self.method("remove_writer", { fd });
                        }
                        return none();
                    }) },
                { "_sock_connect_cb", w.method("BaseSelectorEventLoop", "_sock_connect_cb", { py_positional("fut"), py_positional("sock"), py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut = a[1];
                        py sock = a[2];
                        py address = a[3];
                        py err;
                        py exc;
                        if (fut.method("done").truthy())
                        {
                            return none();
                        }
                        {
                            std::optional<py> pending37;
                            [[maybe_unused]] int flow37 = 0;
                            [[maybe_unused]] py result37;
                            try
                            {
                                bool otherwise37 = false;
                                try
                                {
                                    err = sock.method("getsockopt", { Mod<"socket">().attr("SOL_SOCKET"), Mod<"socket">().attr("SO_ERROR") });
                                    if (err != (I<0>()))
                                    {
                                        // Jump to any except clause below.
                                        py_raise((B<"OSError">())(err, S(std::string{ "Connect call failed " } + address.str())));
                                    }
                                    otherwise37 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler37;
                                    if (matches(handler37.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                    {
                                        // socket is still registered, the callback will be retried later
                                    }
                                    else if (matches(handler37.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                    {
                                        py_reraise(handler37.exception);
                                    }
                                    else if (matches(handler37.exception, B<"BaseException">()))
                                    {
                                        exc = handler37.exception;
                                        fut.method("set_exception", { exc });
                                    }
                                    else
                                        py_reraise(handler37.exception);
                                }
                                if (otherwise37)
                                {
                                    fut.method("set_result", { none() });
                                }
                            }
                            catch (raised const&)
                            {
                                pending37 = py_take_exception();
                            }
                            fut = none();
                            if (pending37)
                                py_reraise(*pending37);
                            if (flow37 == 1)
                                return result37;
                        }
                        return none();
                    }) },
                { "sock_accept", async_method(w, "BaseSelectorEventLoop", "sock_accept", { py_positional("sock") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py fut;
                        (G<"base_events._check_ssl_socket">())(sock);
                        if ((self.attr("_debug").truthy() && (sock.method("gettimeout") != (I<0>()))))
                        {
                            py_raise((B<"ValueError">())(S<"the socket must be non-blocking">()));
                        }
                        fut = self.method("create_future");
                        self.method("_sock_accept", { fut, sock });
                        return py_await(fut);
                    }) },
                { "_sock_accept", w.method("BaseSelectorEventLoop", "_sock_accept", { py_positional("fut"), py_positional("sock") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut = a[1];
                        py sock = a[2];
                        py address;
                        py conn;
                        py exc;
                        py fd;
                        py handle;
                        if (fut.method("done").truthy())
                        {
                            return none();
                        }
                        fd = sock.method("fileno");
                        {
                            bool otherwise38 = false;
                            try
                            {
                                auto const items39 = unpack(sock.method("accept"), 2);
                                conn = items39[0];
                                address = items39[1];
                                conn.method("setblocking", { boolean(false) });
                                otherwise38 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler38;
                                if (matches(handler38.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                    self.method("_ensure_fd_no_transport", { fd });
                                    handle = self.method("_add_reader", { fd, self.attr("_sock_accept"), fut, sock });
                                    fut.method("add_done_callback", { (Mod<"functools">().attr("partial")).call({ self.attr("_sock_read_done"), fd }, { { "handle", handle } }) });
                                }
                                else if (matches(handler38.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler38.exception);
                                }
                                else if (matches(handler38.exception, B<"BaseException">()))
                                {
                                    exc = handler38.exception;
                                    fut.method("set_exception", { exc });
                                }
                                else
                                    py_reraise(handler38.exception);
                            }
                            if (otherwise38)
                            {
                                fut.method("set_result", { py_tuple({ conn, address }) });
                            }
                        }
                        return none();
                    }) },
                { "_sendfile_native", async_method(w, "BaseSelectorEventLoop", "_sendfile_native", { py_positional("transp"), py_positional("file"), py_positional("offset"), py_positional("count") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transp = a[1];
                        py file = a[2];
                        py offset = a[3];
                        py count = a[4];
                        py resume_reading;
                        del_item(self.attr("_transports"), transp.attr("_sock_fd"));
                        resume_reading = transp.method("is_reading");
                        transp.method("pause_reading");
                        py_await(transp.method("_make_empty_waiter"));
                        {
                            std::optional<py> pending40;
                            [[maybe_unused]] int flow40 = 0;
                            [[maybe_unused]] py result40;
                            try
                            {
                                result40 = py_await(self.method("sock_sendfile", { transp.attr("_sock"), file, offset, count }, { { "fallback", boolean(false) } }));
                                flow40 = 1;
                                goto finally40;
                            }
                            catch (raised const&)
                            {
                                pending40 = py_take_exception();
                            }
                            finally40:
                            transp.method("_reset_empty_waiter");
                            if (resume_reading.truthy())
                            {
                                transp.method("resume_reading");
                            }
                            self.attr("_transports").set_item(transp.attr("_sock_fd"), transp);
                            if (pending40)
                                py_reraise(*pending40);
                            if (flow40 == 1)
                                return result40;
                        }
                        return none();
                    }) },
                { "_process_events", w.method("BaseSelectorEventLoop", "_process_events", { py_positional("event_list") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py event_list = a[1];
                        py fileobj;
                        py key;
                        py mask;
                        py reader;
                        py writer;
                        for (py const& item : iterate{ event_list })
                        {
                            auto const items42 = unpack(item, 2);
                            key = items42[0];
                            mask = items42[1];
                            auto const items43 = unpack(py_tuple({ key.attr("fileobj"), key.attr("data") }), 2);
                            fileobj = items43[0];
                            auto const items44 = unpack(items43[1], 2);
                            reader = items44[0];
                            writer = items44[1];
                            if (((mask & (Mod<"selectors">().attr("EVENT_READ"))).truthy() && (!reader.is_none())))
                            {
                                if (reader.attr("_cancelled").truthy())
                                {
                                    self.method("_remove_reader", { fileobj });
                                }
                                else
                                {
                                    self.method("_add_callback", { reader });
                                }
                            }
                            if (((mask & (Mod<"selectors">().attr("EVENT_WRITE"))).truthy() && (!writer.is_none())))
                            {
                                if (writer.attr("_cancelled").truthy())
                                {
                                    self.method("_remove_writer", { fileobj });
                                }
                                else
                                {
                                    self.method("_add_callback", { writer });
                                }
                            }
                        }
                        return none();
                    }) },
                { "_stop_serving", w.method("BaseSelectorEventLoop", "_stop_serving", { py_positional("sock") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        self.method("_remove_reader", { sock.method("fileno") });
                        sock.method("close");
                        return none();
                    }) }, }));

                // Attribute used in the destructor: it must be set even if the constructor
                // is not called (see _SelectorSslTransport which may start by raising an
                // exception)
            define("selector_events", "_SelectorTransport", w.make_class("_SelectorTransport", { G<"transports._FlowControlMixin">(), G<"transports.Transport">() }, {
                { "max_size", (I<256>()) * (I<1024>()) },
                { "_sock", none() },
                { "__init__", w.method("_SelectorTransport", "__init__", { py_positional("loop"), py_positional("sock"), py_positional("protocol"), py_positional("extra", none()), py_positional("server", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        py sock = a[2];
                        py protocol = a[3];
                        py extra = a[4];
                        py server = a[5];
                        (B<"super">()(G<"selector_events._SelectorTransport">(), self)).method("__init__", { extra, loop });
                        self.attr("_extra").set_item(S<"socket">(), (G<"trsock.TransportSocket">())(sock));
                        try
                        {
                            self.attr("_extra").set_item(S<"sockname">(), sock.method("getsockname"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler45;
                            if (matches(handler45.exception, B<"OSError">()))
                            {
                                self.attr("_extra").set_item(S<"sockname">(), none());
                            }
                            else
                                py_reraise(handler45.exception);
                        }
                        if (!self.attr("_extra").contains((S<"peername">())))
                        {
                            try
                            {
                                self.attr("_extra").set_item(S<"peername">(), sock.method("getpeername"));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler46;
                                if (matches(handler46.exception, Mod<"socket">().attr("error")))
                                {
                                    self.attr("_extra").set_item(S<"peername">(), none());
                                }
                                else
                                    py_reraise(handler46.exception);
                            }
                        }
                        self.set_attr("_sock", sock);
                        self.set_attr("_sock_fd", sock.method("fileno"));
                        self.set_attr("_protocol_connected", boolean(false));
                        self.method("set_protocol", { protocol });
                        self.set_attr("_server", server);
                        self.set_attr("_buffer", (Mod<"collections">().attr("deque"))());
                        self.set_attr("_conn_lost", I<0>());
                        self.set_attr("_closing", boolean(false));
                        self.set_attr("_paused", boolean(false));
                        if (!self.attr("_server").is_none())
                        {
                            self.attr("_server").method("_attach", { self });
                        }
                        loop.attr("_transports").set_item(self.attr("_sock_fd"), self);
                        return none();
                    }) },
                { "__repr__", w.method("_SelectorTransport", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py bufsize;
                        py info;
                        py polling;
                        py state;
                        info = py_list({ self.attr("__class__").attr("__name__") });
                        if (self.attr("_sock").is_none())
                        {
                            info.method("append", { S<"closed">() });
                        }
                        else if (self.attr("_closing").truthy())
                        {
                            info.method("append", { S<"closing">() });
                        }
                        info.method("append", { S(std::string{ "fd=" } + self.attr("_sock_fd").str()) });
                        // test if the transport was closed
                        if (((!self.attr("_loop").is_none()) && (!self.attr("_loop").method("is_closed").truthy())))
                        {
                            polling = (G<"selector_events._test_selector_event">())(self.attr("_loop").attr("_selector"), self.attr("_sock_fd"), Mod<"selectors">().attr("EVENT_READ"));
                            if (polling.truthy())
                            {
                                info.method("append", { S<"read=polling">() });
                            }
                            else
                            {
                                info.method("append", { S<"read=idle">() });
                            }
                            polling = (G<"selector_events._test_selector_event">())(self.attr("_loop").attr("_selector"), self.attr("_sock_fd"), Mod<"selectors">().attr("EVENT_WRITE"));
                            if (polling.truthy())
                            {
                                state = S<"polling">();
                            }
                            else
                            {
                                state = S<"idle">();
                            }
                            bufsize = self.method("get_write_buffer_size");
                            info.method("append", { S(std::string{ "write=<" } + state.str() + ", bufsize=" + bufsize.str() + ">") });
                        }
                        return (S<"<{}>">()).method("format", { (S<" ">()).method("join", { info }) });
                    }) },
                { "abort", w.method("_SelectorTransport", "abort", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_force_close", { none() });
                        return none();
                    }) },
                { "set_protocol", w.method("_SelectorTransport", "set_protocol", { py_positional("protocol") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol = a[1];
                        self.set_attr("_protocol", protocol);
                        self.set_attr("_protocol_connected", boolean(true));
                        return none();
                    }) },
                { "get_protocol", w.method("_SelectorTransport", "get_protocol", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_protocol");
                    }) },
                { "is_closing", w.method("_SelectorTransport", "is_closing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_closing");
                    }) },
                { "is_reading", w.method("_SelectorTransport", "is_reading", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = boolean(!self.method("is_closing").truthy()); return !t.truthy() ? t : boolean(!self.attr("_paused").truthy()); }();
                    }) },
                { "pause_reading", w.method("_SelectorTransport", "pause_reading", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.method("is_reading").truthy())
                        {
                            return none();
                        }
                        self.set_attr("_paused", boolean(true));
                        self.attr("_loop").method("_remove_reader", { self.attr("_sock_fd") });
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            (G<"log.logger">()).method("debug", { S<"%r pauses reading">(), self });
                        }
                        return none();
                    }) },
                { "resume_reading", w.method("_SelectorTransport", "resume_reading", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((self.attr("_closing").truthy() || (!self.attr("_paused").truthy())))
                        {
                            return none();
                        }
                        self.set_attr("_paused", boolean(false));
                        self.method("_add_reader", { self.attr("_sock_fd"), self.attr("_read_ready") });
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            (G<"log.logger">()).method("debug", { S<"%r resumes reading">(), self });
                        }
                        return none();
                    }) },
                { "close", w.method("_SelectorTransport", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_closing").truthy())
                        {
                            return none();
                        }
                        self.set_attr("_closing", boolean(true));
                        self.attr("_loop").method("_remove_reader", { self.attr("_sock_fd") });
                        if (!self.attr("_buffer").truthy())
                        {
                            {
                                py const target47 = self;
                                target47.set_attr("_conn_lost", inplace(NEOS_PY_ADD, target47.attr("_conn_lost"), I<1>()));
                            }
                            self.attr("_loop").method("_remove_writer", { self.attr("_sock_fd") });
                            self.attr("_loop").method("call_soon", { self.attr("_call_connection_lost"), none() });
                        }
                        return none();
                    }) },
                { "__del__", w.method("_SelectorTransport", "__del__", { py_positional("_warn", Mod<"warnings">().attr("warn")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _warn = a[1];
                        if (!self.attr("_sock").is_none())
                        {
                            _warn.call({ S(std::string{ "unclosed transport " } + self.repr()), B<"ResourceWarning">() }, { { "source", self } });
                            self.attr("_sock").method("close");
                            if (!self.attr("_server").is_none())
                            {
                                self.attr("_server").method("_detach", { self });
                            }
                        }
                        return none();
                    }) },
                { "_fatal_error", w.method("_SelectorTransport", "_fatal_error", { py_positional("exc"), py_positional("message", S<"Fatal error on transport">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        py message = a[2];
                        if (isinstance(exc, B<"OSError">()))
                        {
                            if (self.attr("_loop").method("get_debug").truthy())
                            {
                                (G<"log.logger">()).method("debug", { S<"%r: %s">(), self, message }, { { "exc_info", boolean(true) } });
                            }
                        }
                        else
                        {
                            self.attr("_loop").method("call_exception_handler", { dict_of({ { S<"message">(), message }, { S<"exception">(), exc }, { S<"transport">(), self }, { S<"protocol">(), self.attr("_protocol") } }) });
                        }
                        self.method("_force_close", { exc });
                        return none();
                    }) },
                { "_force_close", w.method("_SelectorTransport", "_force_close", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        if (self.attr("_conn_lost").truthy())
                        {
                            return none();
                        }
                        if (self.attr("_buffer").truthy())
                        {
                            self.attr("_buffer").method("clear");
                            self.attr("_loop").method("_remove_writer", { self.attr("_sock_fd") });
                        }
                        if (!self.attr("_closing").truthy())
                        {
                            self.set_attr("_closing", boolean(true));
                            self.attr("_loop").method("_remove_reader", { self.attr("_sock_fd") });
                        }
                        {
                            py const target48 = self;
                            target48.set_attr("_conn_lost", inplace(NEOS_PY_ADD, target48.attr("_conn_lost"), I<1>()));
                        }
                        self.attr("_loop").method("call_soon", { self.attr("_call_connection_lost"), exc });
                        return none();
                    }) },
                { "_call_connection_lost", w.method("_SelectorTransport", "_call_connection_lost", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        py server;
                        {
                            std::optional<py> pending49;
                            [[maybe_unused]] int flow49 = 0;
                            [[maybe_unused]] py result49;
                            try
                            {
                                if (self.attr("_protocol_connected").truthy())
                                {
                                    self.attr("_protocol").method("connection_lost", { exc });
                                }
                            }
                            catch (raised const&)
                            {
                                pending49 = py_take_exception();
                            }
                            self.attr("_sock").method("close");
                            self.set_attr("_sock", none());
                            self.set_attr("_protocol", none());
                            self.set_attr("_loop", none());
                            server = self.attr("_server");
                            if (!server.is_none())
                            {
                                server.method("_detach", { self });
                                self.set_attr("_server", none());
                            }
                            if (pending49)
                                py_reraise(*pending49);
                            if (flow49 == 1)
                                return result49;
                        }
                        return none();
                    }) },
                { "get_write_buffer_size", w.method("_SelectorTransport", "get_write_buffer_size", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"sum">())((B<"map">())(B<"len">(), self.attr("_buffer")));
                    }) },
                { "_add_reader", w.method("_SelectorTransport", "_add_reader", { py_positional("fd"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py callback = a[2];
                        py args = a[3];
                        if (!self.method("is_reading").truthy())
                        {
                            return none();
                        }
                        call_ex(self.attr("_loop").attr("_add_reader"), { fd, callback }, args, py{});
                        return none();
                    }, true) }, }));

            define("selector_events", "_SelectorSocketTransport", w.make_class("_SelectorSocketTransport", { G<"selector_events._SelectorTransport">() }, {
                { "_start_tls_compatible", boolean(true) },
                { "_sendfile_compatible", (G<"constants._SendfileMode">()).attr("TRY_NATIVE") },
                { "__init__", w.method("_SelectorSocketTransport", "__init__", { py_positional("loop"), py_positional("sock"), py_positional("protocol"), py_positional("waiter", none()), py_positional("extra", none()), py_positional("server", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        py sock = a[2];
                        py protocol = a[3];
                        py waiter = a[4];
                        py extra = a[5];
                        py server = a[6];
                        self.set_attr("_read_ready_cb", none());
                        (B<"super">()(G<"selector_events._SelectorSocketTransport">(), self)).method("__init__", { loop, sock, protocol, extra, server });
                        self.set_attr("_eof", boolean(false));
                        self.set_attr("_empty_waiter", none());
                        if ((G<"selector_events._HAS_SENDMSG">()).truthy())
                        {
                            self.set_attr("_write_ready", self.attr("_write_sendmsg"));
                        }
                        else
                        {
                            self.set_attr("_write_ready", self.attr("_write_send"));
                        }
                        // Disable the Nagle algorithm -- small writes will be
                        // sent without waiting for the TCP ACK.  This generally
                        // decreases the latency (in some cases significantly.)
                        (G<"base_events._set_nodelay">())(self.attr("_sock"));
                        self.attr("_loop").method("call_soon", { self.attr("_protocol").attr("connection_made"), self });
                        // only start reading when connection_made() has been called
                        self.attr("_loop").method("call_soon", { self.attr("_add_reader"), self.attr("_sock_fd"), self.attr("_read_ready") });
                        if (!waiter.is_none())
                        {
                            // only wake up the waiter when connection_made() has been called
                            self.attr("_loop").method("call_soon", { G<"futures._set_result_unless_cancelled">(), waiter, none() });
                        }
                        return none();
                    }) },
                { "set_protocol", w.method("_SelectorSocketTransport", "set_protocol", { py_positional("protocol") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol = a[1];
                        if (isinstance(protocol, G<"protocols.BufferedProtocol">()))
                        {
                            self.set_attr("_read_ready_cb", self.attr("_read_ready__get_buffer"));
                        }
                        else
                        {
                            self.set_attr("_read_ready_cb", self.attr("_read_ready__data_received"));
                        }
                        (B<"super">()(G<"selector_events._SelectorSocketTransport">(), self)).method("set_protocol", { protocol });
                        return none();
                    }) },
                { "_read_ready", w.method("_SelectorSocketTransport", "_read_ready", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_read_ready_cb");
                        return none();
                    }) },
                { "_read_ready__get_buffer", w.method("_SelectorSocketTransport", "_read_ready__get_buffer", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py buf;
                        py exc;
                        py nbytes;
                        if (self.attr("_conn_lost").truthy())
                        {
                            return none();
                        }
                        try
                        {
                            buf = self.attr("_protocol").method("get_buffer", { I<-1>() });
                            if (!py{ buf.size() }.truthy())
                            {
                                py_raise((B<"RuntimeError">())(S<"get_buffer() returned an empty buffer">()));
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler50;
                            if (matches(handler50.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                            {
                                py_reraise(handler50.exception);
                            }
                            else if (matches(handler50.exception, B<"BaseException">()))
                            {
                                exc = handler50.exception;
                                self.method("_fatal_error", { exc, S<"Fatal error: protocol.get_buffer() call failed.">() });
                                return none();
                            }
                            else
                                py_reraise(handler50.exception);
                        }
                        try
                        {
                            nbytes = self.attr("_sock").method("recv_into", { buf });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler51;
                            if (matches(handler51.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                            {
                                return none();
                            }
                            else if (matches(handler51.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                            {
                                py_reraise(handler51.exception);
                            }
                            else if (matches(handler51.exception, B<"BaseException">()))
                            {
                                exc = handler51.exception;
                                self.method("_fatal_error", { exc, S<"Fatal read error on socket transport">() });
                                return none();
                            }
                            else
                                py_reraise(handler51.exception);
                        }
                        if (!nbytes.truthy())
                        {
                            self.method("_read_ready__on_eof");
                            return none();
                        }
                        try
                        {
                            self.attr("_protocol").method("buffer_updated", { nbytes });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler52;
                            if (matches(handler52.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                            {
                                py_reraise(handler52.exception);
                            }
                            else if (matches(handler52.exception, B<"BaseException">()))
                            {
                                exc = handler52.exception;
                                self.method("_fatal_error", { exc, S<"Fatal error: protocol.buffer_updated() call failed.">() });
                            }
                            else
                                py_reraise(handler52.exception);
                        }
                        return none();
                    }) },
                { "_read_ready__data_received", w.method("_SelectorSocketTransport", "_read_ready__data_received", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data;
                        py exc;
                        if (self.attr("_conn_lost").truthy())
                        {
                            return none();
                        }
                        try
                        {
                            data = self.attr("_sock").method("recv", { self.attr("max_size") });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler53;
                            if (matches(handler53.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                            {
                                return none();
                            }
                            else if (matches(handler53.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                            {
                                py_reraise(handler53.exception);
                            }
                            else if (matches(handler53.exception, B<"BaseException">()))
                            {
                                exc = handler53.exception;
                                self.method("_fatal_error", { exc, S<"Fatal read error on socket transport">() });
                                return none();
                            }
                            else
                                py_reraise(handler53.exception);
                        }
                        if (!data.truthy())
                        {
                            self.method("_read_ready__on_eof");
                            return none();
                        }
                        try
                        {
                            self.attr("_protocol").method("data_received", { data });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler54;
                            if (matches(handler54.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                            {
                                py_reraise(handler54.exception);
                            }
                            else if (matches(handler54.exception, B<"BaseException">()))
                            {
                                exc = handler54.exception;
                                self.method("_fatal_error", { exc, S<"Fatal error: protocol.data_received() call failed.">() });
                            }
                            else
                                py_reraise(handler54.exception);
                        }
                        return none();
                    }) },
                { "_read_ready__on_eof", w.method("_SelectorSocketTransport", "_read_ready__on_eof", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc;
                        py keep_open;
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            (G<"log.logger">()).method("debug", { S<"%r received EOF">(), self });
                        }
                        try
                        {
                            keep_open = self.attr("_protocol").method("eof_received");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler55;
                            if (matches(handler55.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                            {
                                py_reraise(handler55.exception);
                            }
                            else if (matches(handler55.exception, B<"BaseException">()))
                            {
                                exc = handler55.exception;
                                self.method("_fatal_error", { exc, S<"Fatal error: protocol.eof_received() call failed.">() });
                                return none();
                            }
                            else
                                py_reraise(handler55.exception);
                        }
                        if (keep_open.truthy())
                        {
                            // We're keeping the connection open so the
                            // protocol can write more, but we still can't
                            // receive more, so remove the reader callback.
                            self.attr("_loop").method("_remove_reader", { self.attr("_sock_fd") });
                        }
                        else
                        {
                            self.method("close");
                        }
                        return none();
                    }) },
                { "write", w.method("_SelectorSocketTransport", "write", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py exc;
                        py n;
                        if (!isinstance(data, py_tuple({ B<"bytes">(), B<"bytearray">(), B<"memoryview">() })))
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "data argument must be a bytes, bytearray, or memoryview object, not " } + (B<"type">())(data).attr("__name__").repr())));
                        }
                        if (self.attr("_eof").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Cannot call write() after write_eof()">()));
                        }
                        if (!self.attr("_empty_waiter").is_none())
                        {
                            py_raise((B<"RuntimeError">())(S<"unable to write; sendfile is in progress">()));
                        }
                        if (!data.truthy())
                        {
                            return none();
                        }
                        if (self.attr("_conn_lost").truthy())
                        {
                            if (self.attr("_conn_lost") >= (G<"constants.LOG_THRESHOLD_FOR_CONNLOST_WRITES">()))
                            {
                                (G<"log.logger">()).method("warning", { S<"socket.send() raised exception.">() });
                            }
                            {
                                py const target56 = self;
                                target56.set_attr("_conn_lost", inplace(NEOS_PY_ADD, target56.attr("_conn_lost"), I<1>()));
                            }
                            return none();
                        }
                        if (!self.attr("_buffer").truthy())
                        {
                            // Optimization: try to send now.
                            {
                                bool otherwise57 = false;
                                try
                                {
                                    n = self.attr("_sock").method("send", { data });
                                    otherwise57 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler57;
                                    if (matches(handler57.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                    {
                                    }
                                    else if (matches(handler57.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                    {
                                        py_reraise(handler57.exception);
                                    }
                                    else if (matches(handler57.exception, B<"BaseException">()))
                                    {
                                        exc = handler57.exception;
                                        self.method("_fatal_error", { exc, S<"Fatal write error on socket transport">() });
                                        return none();
                                    }
                                    else
                                        py_reraise(handler57.exception);
                                }
                                if (otherwise57)
                                {
                                    data = (B<"memoryview">())(data)[B<"slice">()(n, none(), none())];
                                    if (!data.truthy())
                                    {
                                        return none();
                                    }
                                }
                            }
                            // Not all was written; register write handler.
                            self.attr("_loop").method("_add_writer", { self.attr("_sock_fd"), self.attr("_write_ready") });
                        }
                        // Add it to the buffer.
                        self.attr("_buffer").method("append", { data });
                        self.method("_maybe_pause_protocol");
                        return none();
                    }) },
                { "_get_sendmsg_buffer", w.method("_SelectorSocketTransport", "_get_sendmsg_buffer", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (Mod<"itertools">().attr("islice"))(self.attr("_buffer"), G<"selector_events.SC_IOV_MAX">());
                    }) },
                { "_write_sendmsg", w.method("_SelectorSocketTransport", "_write_sendmsg", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc;
                        py nbytes;
                        if (!self.attr("_buffer").truthy())
                            py_raise(B<"AssertionError">()(S<"Data should not be empty">()));
                        if (self.attr("_conn_lost").truthy())
                        {
                            return none();
                        }
                        {
                            bool otherwise58 = false;
                            try
                            {
                                nbytes = self.attr("_sock").method("sendmsg", { self.method("_get_sendmsg_buffer") });
                                self.method("_adjust_leftover_buffer", { nbytes });
                                otherwise58 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler58;
                                if (matches(handler58.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                }
                                else if (matches(handler58.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler58.exception);
                                }
                                else if (matches(handler58.exception, B<"BaseException">()))
                                {
                                    exc = handler58.exception;
                                    self.attr("_loop").method("_remove_writer", { self.attr("_sock_fd") });
                                    self.attr("_buffer").method("clear");
                                    self.method("_fatal_error", { exc, S<"Fatal write error on socket transport">() });
                                    if (!self.attr("_empty_waiter").is_none())
                                    {
                                        self.attr("_empty_waiter").method("set_exception", { exc });
                                    }
                                }
                                else
                                    py_reraise(handler58.exception);
                            }
                            if (otherwise58)
                            {
                                self.method("_maybe_resume_protocol");
                                if (!self.attr("_buffer").truthy())
                                {
                                    self.attr("_loop").method("_remove_writer", { self.attr("_sock_fd") });
                                    if (!self.attr("_empty_waiter").is_none())
                                    {
                                        self.attr("_empty_waiter").method("set_result", { none() });
                                    }
                                    if (self.attr("_closing").truthy())
                                    {
                                        self.method("_call_connection_lost", { none() });
                                    }
                                    else if (self.attr("_eof").truthy())
                                    {
                                        self.attr("_sock").method("shutdown", { Mod<"socket">().attr("SHUT_WR") });
                                    }
                                }
                            }
                        }
                        return none();
                    }) },
                { "_adjust_leftover_buffer", w.method("_SelectorSocketTransport", "_adjust_leftover_buffer", { py_positional("nbytes") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py nbytes = a[1];
                        py b;
                        py b_len;
                        py buffer;
                        buffer = self.attr("_buffer");
                        while (nbytes.truthy())
                        {
                            b = buffer.method("popleft");
                            b_len = py{ b.size() };
                            if (b_len <= nbytes)
                            {
                                nbytes = inplace(NEOS_PY_SUBTRACT, nbytes, b_len);
                            }
                            else
                            {
                                buffer.method("appendleft", { b[B<"slice">()(nbytes, none(), none())] });
                                break;
                            }
                        }
                        return none();
                    }) },
                { "_write_send", w.method("_SelectorSocketTransport", "_write_send", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py buffer;
                        py exc;
                        py n;
                        if (!self.attr("_buffer").truthy())
                            py_raise(B<"AssertionError">()(S<"Data should not be empty">()));
                        if (self.attr("_conn_lost").truthy())
                        {
                            return none();
                        }
                        {
                            bool otherwise60 = false;
                            try
                            {
                                buffer = self.attr("_buffer").method("popleft");
                                n = self.attr("_sock").method("send", { buffer });
                                if (n != py{ buffer.size() })
                                {
                                    // Not all data was written
                                    self.attr("_buffer").method("appendleft", { buffer[B<"slice">()(n, none(), none())] });
                                }
                                otherwise60 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler60;
                                if (matches(handler60.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                }
                                else if (matches(handler60.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler60.exception);
                                }
                                else if (matches(handler60.exception, B<"BaseException">()))
                                {
                                    exc = handler60.exception;
                                    self.attr("_loop").method("_remove_writer", { self.attr("_sock_fd") });
                                    self.attr("_buffer").method("clear");
                                    self.method("_fatal_error", { exc, S<"Fatal write error on socket transport">() });
                                    if (!self.attr("_empty_waiter").is_none())
                                    {
                                        self.attr("_empty_waiter").method("set_exception", { exc });
                                    }
                                }
                                else
                                    py_reraise(handler60.exception);
                            }
                            if (otherwise60)
                            {
                                self.method("_maybe_resume_protocol");
                                if (!self.attr("_buffer").truthy())
                                {
                                    self.attr("_loop").method("_remove_writer", { self.attr("_sock_fd") });
                                    if (!self.attr("_empty_waiter").is_none())
                                    {
                                        self.attr("_empty_waiter").method("set_result", { none() });
                                    }
                                    if (self.attr("_closing").truthy())
                                    {
                                        self.method("_call_connection_lost", { none() });
                                    }
                                    else if (self.attr("_eof").truthy())
                                    {
                                        self.attr("_sock").method("shutdown", { Mod<"socket">().attr("SHUT_WR") });
                                    }
                                }
                            }
                        }
                        return none();
                    }) },
                { "write_eof", w.method("_SelectorSocketTransport", "write_eof", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((self.attr("_closing").truthy() || self.attr("_eof").truthy()))
                        {
                            return none();
                        }
                        self.set_attr("_eof", boolean(true));
                        if (!self.attr("_buffer").truthy())
                        {
                            self.attr("_sock").method("shutdown", { Mod<"socket">().attr("SHUT_WR") });
                        }
                        return none();
                    }) },
                { "writelines", w.method("_SelectorSocketTransport", "writelines", { py_positional("list_of_data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py list_of_data = a[1];
                        if (self.attr("_eof").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Cannot call writelines() after write_eof()">()));
                        }
                        if (!self.attr("_empty_waiter").is_none())
                        {
                            py_raise((B<"RuntimeError">())(S<"unable to writelines; sendfile is in progress">()));
                        }
                        if (!list_of_data.truthy())
                        {
                            return none();
                        }
                        if (self.attr("_conn_lost").truthy())
                        {
                            if (self.attr("_conn_lost") >= (G<"constants.LOG_THRESHOLD_FOR_CONNLOST_WRITES">()))
                            {
                                (G<"log.logger">()).method("warning", { S<"socket.send() raised exception.">() });
                            }
                            {
                                py const target61 = self;
                                target61.set_attr("_conn_lost", inplace(NEOS_PY_ADD, target61.attr("_conn_lost"), I<1>()));
                            }
                            return none();
                        }
                        self.attr("_buffer").method("extend", { [&]() -> py { py const result = py_list(); py data; for (py const& item : iterate{ list_of_data }) { data = item; result.method("append", { (B<"memoryview">())(data) }); } return result; }() });
                        self.method("_write_ready");
                        // If the entire buffer couldn't be written, register a write handler
                        if (self.attr("_buffer").truthy())
                        {
                            self.attr("_loop").method("_add_writer", { self.attr("_sock_fd"), self.attr("_write_ready") });
                            self.method("_maybe_pause_protocol");
                        }
                        return none();
                    }) },
                { "can_write_eof", w.method("_SelectorSocketTransport", "can_write_eof", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) },
                { "_call_connection_lost", w.method("_SelectorSocketTransport", "_call_connection_lost", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        {
                            std::optional<py> pending62;
                            [[maybe_unused]] int flow62 = 0;
                            [[maybe_unused]] py result62;
                            try
                            {
                                (B<"super">()(G<"selector_events._SelectorSocketTransport">(), self)).method("_call_connection_lost", { exc });
                            }
                            catch (raised const&)
                            {
                                pending62 = py_take_exception();
                            }
                            self.set_attr("_write_ready", none());
                            if (!self.attr("_empty_waiter").is_none())
                            {
                                self.attr("_empty_waiter").method("set_exception", { (B<"ConnectionError">())(S<"Connection is closed by peer">()) });
                            }
                            if (pending62)
                                py_reraise(*pending62);
                            if (flow62 == 1)
                                return result62;
                        }
                        return none();
                    }) },
                { "_make_empty_waiter", w.method("_SelectorSocketTransport", "_make_empty_waiter", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_empty_waiter").is_none())
                        {
                            py_raise((B<"RuntimeError">())(S<"Empty waiter is already set">()));
                        }
                        self.set_attr("_empty_waiter", self.attr("_loop").method("create_future"));
                        if (!self.attr("_buffer").truthy())
                        {
                            self.attr("_empty_waiter").method("set_result", { none() });
                        }
                        return self.attr("_empty_waiter");
                    }) },
                { "_reset_empty_waiter", w.method("_SelectorSocketTransport", "_reset_empty_waiter", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_empty_waiter", none());
                        return none();
                    }) },
                { "close", w.method("_SelectorSocketTransport", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_read_ready_cb", none());
                        (B<"super">()(G<"selector_events._SelectorSocketTransport">(), self)).method("close");
                        return none();
                    }) }, }));

            define("selector_events", "_SelectorDatagramTransport", w.make_class("_SelectorDatagramTransport", { G<"selector_events._SelectorTransport">(), G<"transports.DatagramTransport">() }, {
                { "_buffer_factory", Mod<"collections">().attr("deque") },
                { "_header_size", I<8>() },
                { "__init__", w.method("_SelectorDatagramTransport", "__init__", { py_positional("loop"), py_positional("sock"), py_positional("protocol"), py_positional("address", none()), py_positional("waiter", none()), py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        py sock = a[2];
                        py protocol = a[3];
                        py address = a[4];
                        py waiter = a[5];
                        py extra = a[6];
                        (B<"super">()(G<"selector_events._SelectorDatagramTransport">(), self)).method("__init__", { loop, sock, protocol, extra });
                        self.set_attr("_address", address);
                        self.set_attr("_buffer_size", I<0>());
                        self.attr("_loop").method("call_soon", { self.attr("_protocol").attr("connection_made"), self });
                        // only start reading when connection_made() has been called
                        self.attr("_loop").method("call_soon", { self.attr("_add_reader"), self.attr("_sock_fd"), self.attr("_read_ready") });
                        if (!waiter.is_none())
                        {
                            // only wake up the waiter when connection_made() has been called
                            self.attr("_loop").method("call_soon", { G<"futures._set_result_unless_cancelled">(), waiter, none() });
                        }
                        return none();
                    }) },
                { "get_write_buffer_size", w.method("_SelectorDatagramTransport", "get_write_buffer_size", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_buffer_size");
                    }) },
                { "_read_ready", w.method("_SelectorDatagramTransport", "_read_ready", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py addr;
                        py data;
                        py exc;
                        if (self.attr("_conn_lost").truthy())
                        {
                            return none();
                        }
                        {
                            bool otherwise63 = false;
                            try
                            {
                                auto const items64 = unpack(self.attr("_sock").method("recvfrom", { self.attr("max_size") }), 2);
                                data = items64[0];
                                addr = items64[1];
                                otherwise63 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler63;
                                if (matches(handler63.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                }
                                else if (matches(handler63.exception, B<"OSError">()))
                                {
                                    exc = handler63.exception;
                                    self.attr("_protocol").method("error_received", { exc });
                                }
                                else if (matches(handler63.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler63.exception);
                                }
                                else if (matches(handler63.exception, B<"BaseException">()))
                                {
                                    exc = handler63.exception;
                                    self.method("_fatal_error", { exc, S<"Fatal read error on datagram transport">() });
                                }
                                else
                                    py_reraise(handler63.exception);
                            }
                            if (otherwise63)
                            {
                                self.attr("_protocol").method("datagram_received", { data, addr });
                            }
                        }
                        return none();
                    }) },
                { "sendto", w.method("_SelectorDatagramTransport", "sendto", { py_positional("data"), py_positional("addr", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py addr = a[2];
                        py exc;
                        if (!isinstance(data, py_tuple({ B<"bytes">(), B<"bytearray">(), B<"memoryview">() })))
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "data argument must be a bytes-like object, not " } + (B<"type">())(data).attr("__name__").repr())));
                        }
                        if (self.attr("_address").truthy())
                        {
                            if (!py_tuple({ none(), self.attr("_address") }).contains(addr))
                            {
                                py_raise((B<"ValueError">())(S(std::string{ "Invalid address: must be None or " } + self.attr("_address").str())));
                            }
                            addr = self.attr("_address");
                        }
                        if ((self.attr("_conn_lost").truthy() && self.attr("_address").truthy()))
                        {
                            if (self.attr("_conn_lost") >= (G<"constants.LOG_THRESHOLD_FOR_CONNLOST_WRITES">()))
                            {
                                (G<"log.logger">()).method("warning", { S<"socket.send() raised exception.">() });
                            }
                            {
                                py const target65 = self;
                                target65.set_attr("_conn_lost", inplace(NEOS_PY_ADD, target65.attr("_conn_lost"), I<1>()));
                            }
                            return none();
                        }
                        if (!self.attr("_buffer").truthy())
                        {
                            // Attempt to send it right away first.
                            try
                            {
                                if (self.attr("_extra")[S<"peername">()].truthy())
                                {
                                    self.attr("_sock").method("send", { data });
                                }
                                else
                                {
                                    self.attr("_sock").method("sendto", { data, addr });
                                }
                                return none();
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler66;
                                if (matches(handler66.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                    self.attr("_loop").method("_add_writer", { self.attr("_sock_fd"), self.attr("_sendto_ready") });
                                }
                                else if (matches(handler66.exception, B<"OSError">()))
                                {
                                    exc = handler66.exception;
                                    self.attr("_protocol").method("error_received", { exc });
                                    return none();
                                }
                                else if (matches(handler66.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler66.exception);
                                }
                                else if (matches(handler66.exception, B<"BaseException">()))
                                {
                                    exc = handler66.exception;
                                    self.method("_fatal_error", { exc, S<"Fatal write error on datagram transport">() });
                                    return none();
                                }
                                else
                                    py_reraise(handler66.exception);
                            }
                        }
                        // Ensure that what we buffer is immutable.
                        self.attr("_buffer").method("append", { py_tuple({ (B<"bytes">())(data), addr }) });
                        {
                            py const target67 = self;
                            target67.set_attr("_buffer_size", inplace(NEOS_PY_ADD, target67.attr("_buffer_size"), py{ data.size() } + self.attr("_header_size")));
                        }
                        self.method("_maybe_pause_protocol");
                        return none();
                    }) },
                { "_sendto_ready", w.method("_SelectorDatagramTransport", "_sendto_ready", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py addr;
                        py data;
                        py exc;
                        while (self.attr("_buffer").truthy())
                        {
                            auto const items69 = unpack(self.attr("_buffer").method("popleft"), 2);
                            data = items69[0];
                            addr = items69[1];
                            {
                                py const target70 = self;
                                target70.set_attr("_buffer_size", inplace(NEOS_PY_SUBTRACT, target70.attr("_buffer_size"), py{ data.size() } + self.attr("_header_size")));
                            }
                            try
                            {
                                if (self.attr("_extra")[S<"peername">()].truthy())
                                {
                                    self.attr("_sock").method("send", { data });
                                }
                                else
                                {
                                    self.attr("_sock").method("sendto", { data, addr });
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler71;
                                if (matches(handler71.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                    self.attr("_buffer").method("appendleft", { py_tuple({ data, addr }) });
                                    {
                                        py const target72 = self;
                                        target72.set_attr("_buffer_size", inplace(NEOS_PY_ADD, target72.attr("_buffer_size"), py{ data.size() } + self.attr("_header_size")));
                                    }
                                    break;
                                }
                                else if (matches(handler71.exception, B<"OSError">()))
                                {
                                    exc = handler71.exception;
                                    self.attr("_protocol").method("error_received", { exc });
                                    return none();
                                }
                                else if (matches(handler71.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler71.exception);
                                }
                                else if (matches(handler71.exception, B<"BaseException">()))
                                {
                                    exc = handler71.exception;
                                    self.method("_fatal_error", { exc, S<"Fatal write error on datagram transport">() });
                                    return none();
                                }
                                else
                                    py_reraise(handler71.exception);
                            }
                        }
                        self.method("_maybe_resume_protocol");
                        if (!self.attr("_buffer").truthy())
                        {
                            self.attr("_loop").method("_remove_writer", { self.attr("_sock_fd") });
                            if (self.attr("_closing").truthy())
                            {
                                self.method("_call_connection_lost", { none() });
                            }
                        }
                        return none();
                    }) }, }));
        }

        // --- base_subprocess (Lib/asyncio/base_subprocess.py)

        void make_base_subprocess()
        {
            static module_writer const& w = writer("base_subprocess");
                // Don't clear the _proc reference yet: _post_init() may still run
            define("base_subprocess", "BaseSubprocessTransport", w.make_class("BaseSubprocessTransport", { G<"transports.SubprocessTransport">() }, {
                { "__init__", w.method("BaseSubprocessTransport", "__init__", { py_positional("loop"), py_positional("protocol"), py_positional("args"), py_positional("shell"), py_positional("stdin"), py_positional("stdout"), py_positional("stderr"), py_positional("bufsize"), py_positional("waiter", none()), py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        py protocol = a[2];
                        py args = a[3];
                        py shell = a[4];
                        py stdin_ = a[5];
                        py stdout_ = a[6];
                        py stderr_ = a[7];
                        py bufsize = a[8];
                        py waiter = a[9];
                        py extra = a[10];
                        py kwargs = a[11];
                        py program;
                        (B<"super">()(G<"base_subprocess.BaseSubprocessTransport">(), self)).method("__init__", { extra });
                        self.set_attr("_closed", boolean(false));
                        self.set_attr("_protocol", protocol);
                        self.set_attr("_loop", loop);
                        self.set_attr("_proc", none());
                        self.set_attr("_pid", none());
                        self.set_attr("_returncode", none());
                        self.set_attr("_exit_waiters", py_list());
                        self.set_attr("_pending_calls", (Mod<"collections">().attr("deque"))());
                        self.set_attr("_pipes", py_dict());
                        self.set_attr("_finished", boolean(false));
                        if (stdin_ == (py{ -1 } /* subprocess.PIPE */))
                        {
                            self.attr("_pipes").set_item(I<0>(), none());
                        }
                        if (stdout_ == (py{ -1 } /* subprocess.PIPE */))
                        {
                            self.attr("_pipes").set_item(I<1>(), none());
                        }
                        if (stderr_ == (py{ -1 } /* subprocess.PIPE */))
                        {
                            self.attr("_pipes").set_item(I<2>(), none());
                        }
                        // Create the child process: set the _proc attribute
                        try
                        {
                            call_ex(self.attr("_start"), {  }, py{}, kwargs, { { "args", args }, { "shell", shell }, { "stdin", stdin_ }, { "stdout", stdout_ }, { "stderr", stderr_ }, { "bufsize", bufsize } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            {
                                self.method("close");
                                py_reraise(handler1.exception);
                            }
                        }
                        self.set_attr("_pid", self.attr("_proc").attr("pid"));
                        self.attr("_extra").set_item(S<"subprocess">(), self.attr("_proc"));
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            if (isinstance(args, py_tuple({ B<"bytes">(), B<"str">() })))
                            {
                                program = args;
                            }
                            else
                            {
                                program = args[I<0>()];
                            }
                            (G<"log.logger">()).method("debug", { S<"process %r created: pid %s">(), program, self.attr("_pid") });
                        }
                        self.attr("_loop").method("create_task", { self.method("_connect_pipes", { waiter }) });
                        return none();
                    }, false, true) },
                { "__repr__", w.method("BaseSubprocessTransport", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info;
                        py stderr_;
                        py stdin_;
                        py stdout_;
                        info = py_list({ self.attr("__class__").attr("__name__") });
                        if (self.attr("_closed").truthy())
                        {
                            info.method("append", { S<"closed">() });
                        }
                        if (!self.attr("_pid").is_none())
                        {
                            info.method("append", { S(std::string{ "pid=" } + self.attr("_pid").str()) });
                        }
                        if (!self.attr("_returncode").is_none())
                        {
                            info.method("append", { S(std::string{ "returncode=" } + self.attr("_returncode").str()) });
                        }
                        else if (!self.attr("_pid").is_none())
                        {
                            info.method("append", { S<"running">() });
                        }
                        else
                        {
                            info.method("append", { S<"not started">() });
                        }
                        stdin_ = self.attr("_pipes").method("get", { I<0>() });
                        if (!stdin_.is_none())
                        {
                            info.method("append", { S(std::string{ "stdin=" } + stdin_.attr("pipe").str()) });
                        }
                        stdout_ = self.attr("_pipes").method("get", { I<1>() });
                        stderr_ = self.attr("_pipes").method("get", { I<2>() });
                        if (((!stdout_.is_none()) && stderr_.is(stdout_)))
                        {
                            info.method("append", { S(std::string{ "stdout=stderr=" } + stdout_.attr("pipe").str()) });
                        }
                        else
                        {
                            if (!stdout_.is_none())
                            {
                                info.method("append", { S(std::string{ "stdout=" } + stdout_.attr("pipe").str()) });
                            }
                            if (!stderr_.is_none())
                            {
                                info.method("append", { S(std::string{ "stderr=" } + stderr_.attr("pipe").str()) });
                            }
                        }
                        return (S<"<{}>">()).method("format", { (S<" ">()).method("join", { info }) });
                    }) },
                { "_start", w.method("BaseSubprocessTransport", "_start", { py_positional("args"), py_positional("shell"), py_positional("stdin"), py_positional("stdout"), py_positional("stderr"), py_positional("bufsize") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py shell = a[2];
                        py stdin_ = a[3];
                        py stdout_ = a[4];
                        py stderr_ = a[5];
                        py bufsize = a[6];
                        py kwargs = a[7];
                        py_raise(B<"NotImplementedError">());
                    }, false, true) },
                { "set_protocol", w.method("BaseSubprocessTransport", "set_protocol", { py_positional("protocol") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol = a[1];
                        self.set_attr("_protocol", protocol);
                        return none();
                    }) },
                { "get_protocol", w.method("BaseSubprocessTransport", "get_protocol", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_protocol");
                    }) },
                { "is_closing", w.method("BaseSubprocessTransport", "is_closing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_closed");
                    }) },
                { "close", w.method("BaseSubprocessTransport", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py proto;
                        if (self.attr("_closed").truthy())
                        {
                            return none();
                        }
                        self.set_attr("_closed", boolean(true));
                        for (py const& item : iterate{ self.attr("_pipes").method("values") })
                        {
                            proto = item;
                            if (proto.is_none())
                            {
                                continue;
                            }
                            // See gh-114177
                            // skip closing the pipe if loop is already closed
                            // this can happen e.g. when loop is closed immediately after
                            // process is killed
                            if ((self.attr("_loop").truthy() && (!self.attr("_loop").method("is_closed").truthy())))
                            {
                                proto.attr("pipe").method("close");
                            }
                        }
                        if (((!self.attr("_proc").is_none()) && self.attr("_returncode").is_none() && self.attr("_proc").method("poll").is_none()))
                        {
                            // has the child process finished?
                            // the child process has finished, but the
                            // transport hasn't been notified yet?
                            if (self.attr("_loop").method("get_debug").truthy())
                            {
                                (G<"log.logger">()).method("warning", { S<"Close running child process: kill %r">(), self });
                            }
                            try
                            {
                                self.attr("_proc").method("kill");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler3;
                                if (matches(handler3.exception, py_tuple({ B<"ProcessLookupError">(), B<"PermissionError">() })))
                                {
                                    // the process may have already exited or may be running setuid
                                }
                                else
                                    py_reraise(handler3.exception);
                            }
                        }
                        return none();
                    }) },
                { "__del__", w.method("BaseSubprocessTransport", "__del__", { py_positional("_warn", Mod<"warnings">().attr("warn")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _warn = a[1];
                        if (!self.attr("_closed").truthy())
                        {
                            _warn.call({ S(std::string{ "unclosed transport " } + self.repr()), B<"ResourceWarning">() }, { { "source", self } });
                            self.method("close");
                        }
                        return none();
                    }) },
                { "get_pid", w.method("BaseSubprocessTransport", "get_pid", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_pid");
                    }) },
                { "get_returncode", w.method("BaseSubprocessTransport", "get_returncode", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_returncode");
                    }) },
                { "get_pipe_transport", w.method("BaseSubprocessTransport", "get_pipe_transport", { py_positional("fd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        if (self.attr("_pipes").contains(fd))
                        {
                            return self.attr("_pipes")[fd].attr("pipe");
                        }
                        else
                        {
                            return none();
                        }
                        return none();
                    }) },
                { "_check_proc", w.method("BaseSubprocessTransport", "_check_proc", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_proc").is_none())
                        {
                            py_raise((B<"ProcessLookupError">())());
                        }
                        return none();
                    }) },
#if defined(_WIN32)
                { "send_signal", w.method("BaseSubprocessTransport", "send_signal", { py_positional("signal") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py signal = a[1];
                        self.method("_check_proc");
                        self.attr("_proc").method("send_signal", { signal });
                        return none();
                    }) },
                { "terminate", w.method("BaseSubprocessTransport", "terminate", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_check_proc");
                        self.attr("_proc").method("terminate");
                        return none();
                    }) },
                { "kill", w.method("BaseSubprocessTransport", "kill", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_check_proc");
                        self.attr("_proc").method("kill");
                        return none();
                    }) },
#else
                { "send_signal", w.method("BaseSubprocessTransport", "send_signal", { py_positional("signal") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py signal = a[1];
                        self.method("_check_proc");
                        try
                        {
                            (Mod<"os">().attr("kill"))(self.attr("_proc").attr("pid"), signal);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler4;
                            if (matches(handler4.exception, B<"ProcessLookupError">()))
                            {
                            }
                            else
                                py_reraise(handler4.exception);
                        }
                        return none();
                    }) },
                { "terminate", w.method("BaseSubprocessTransport", "terminate", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("send_signal", { Mod<"signal">().attr("SIGTERM") });
                        return none();
                    }) },
                { "kill", w.method("BaseSubprocessTransport", "kill", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("send_signal", { Mod<"signal">().attr("SIGKILL") });
                        return none();
                    }) },
#endif
                { "_connect_pipes", async_method(w, "BaseSubprocessTransport", "_connect_pipes", { py_positional("waiter") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py waiter = a[1];
                        py _;
                        py callback;
                        py data;
                        py exc;
                        py loop;
                        py pipe;
                        py proc;
                        {
                            bool otherwise5 = false;
                            try
                            {
                                proc = (*self).attr("_proc");
                                loop = (*self).attr("_loop");
                                if (!proc.attr("stdin").is_none())
                                {
                                    auto const items6 = unpack(py_await(loop.method("connect_write_pipe", { w.function("<lambda>", "BaseSubprocessTransport._connect_pipes.<locals>.<lambda>", {  }, [self](arguments a) -> py
                {
                    return (G<"base_subprocess.WriteSubprocessPipeProto">())((*self), I<0>());
                }), proc.attr("stdin") })), 2);
                                    _ = items6[0];
                                    pipe = items6[1];
                                    (*self).attr("_pipes").set_item(I<0>(), pipe);
                                }
                                if (!proc.attr("stdout").is_none())
                                {
                                    auto const items7 = unpack(py_await(loop.method("connect_read_pipe", { w.function("<lambda>", "BaseSubprocessTransport._connect_pipes.<locals>.<lambda>", {  }, [self](arguments a) -> py
                {
                    return (G<"base_subprocess.ReadSubprocessPipeProto">())((*self), I<1>());
                }), proc.attr("stdout") })), 2);
                                    _ = items7[0];
                                    pipe = items7[1];
                                    (*self).attr("_pipes").set_item(I<1>(), pipe);
                                }
                                if (!proc.attr("stderr").is_none())
                                {
                                    auto const items8 = unpack(py_await(loop.method("connect_read_pipe", { w.function("<lambda>", "BaseSubprocessTransport._connect_pipes.<locals>.<lambda>", {  }, [self](arguments a) -> py
                {
                    return (G<"base_subprocess.ReadSubprocessPipeProto">())((*self), I<2>());
                }), proc.attr("stderr") })), 2);
                                    _ = items8[0];
                                    pipe = items8[1];
                                    (*self).attr("_pipes").set_item(I<2>(), pipe);
                                }
                                if ((*self).attr("_pending_calls").is_none())
                                    py_raise(B<"AssertionError">()());
                                loop.method("call_soon", { (*self).attr("_protocol").attr("connection_made"), (*self) });
                                for (py const& item : iterate{ (*self).attr("_pending_calls") })
                                {
                                    auto const items10 = unpack(item, 2);
                                    callback = items10[0];
                                    data = items10[1];
                                    call_ex(loop.attr("call_soon"), { callback }, data, py{});
                                }
                                (*self).set_attr("_pending_calls", none());
                                otherwise5 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler5;
                                if (matches(handler5.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler5.exception);
                                }
                                else if (matches(handler5.exception, B<"BaseException">()))
                                {
                                    exc = handler5.exception;
                                    if (((!waiter.is_none()) && (!waiter.method("cancelled").truthy())))
                                    {
                                        waiter.method("set_exception", { exc });
                                    }
                                }
                                else
                                    py_reraise(handler5.exception);
                            }
                            if (otherwise5)
                            {
                                if (((!waiter.is_none()) && (!waiter.method("cancelled").truthy())))
                                {
                                    waiter.method("set_result", { none() });
                                }
                            }
                        }
                        return none();
                    }) },
                { "_call", w.method("BaseSubprocessTransport", "_call", { py_positional("cb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cb = a[1];
                        py data = a[2];
                        if (!self.attr("_pending_calls").is_none())
                        {
                            self.attr("_pending_calls").method("append", { py_tuple({ cb, data }) });
                        }
                        else
                        {
                            call_ex(self.attr("_loop").attr("call_soon"), { cb }, data, py{});
                        }
                        return none();
                    }, true) },
                { "_pipe_connection_lost", w.method("BaseSubprocessTransport", "_pipe_connection_lost", { py_positional("fd"), py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py exc = a[2];
                        self.method("_call", { self.attr("_protocol").attr("pipe_connection_lost"), fd, exc });
                        self.method("_try_finish");
                        return none();
                    }) },
                { "_pipe_data_received", w.method("BaseSubprocessTransport", "_pipe_data_received", { py_positional("fd"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py data = a[2];
                        self.method("_call", { self.attr("_protocol").attr("pipe_data_received"), fd, data });
                        return none();
                    }) },
                { "_process_exited", w.method("BaseSubprocessTransport", "_process_exited", { py_positional("returncode") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py returncode = a[1];
                        py waiter;
                        if (returncode.is_none())
                            py_raise(B<"AssertionError">()(returncode));
                        if (!self.attr("_returncode").is_none())
                            py_raise(B<"AssertionError">()(self.attr("_returncode")));
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            (G<"log.logger">()).method("info", { S<"%r exited with return code %r">(), self, returncode });
                        }
                        self.set_attr("_returncode", returncode);
                        if (self.attr("_proc").attr("returncode").is_none())
                        {
                            // asyncio uses a child watcher: copy the status into the Popen
                            // object. On Python 3.6, it is required to avoid a ResourceWarning.
                            self.attr("_proc").set_attr("returncode", returncode);
                        }
                        self.method("_call", { self.attr("_protocol").attr("process_exited") });
                        self.method("_try_finish");
                        // gh-119710: Wake up futures waiting for wait() as soon as the process
                        // exits.
                        for (py const& item : iterate{ self.attr("_exit_waiters") })
                        {
                            waiter = item;
                            if (!waiter.method("done").truthy())
                            {
                                waiter.method("set_result", { returncode });
                            }
                        }
                        self.set_attr("_exit_waiters", none());
                        return none();
                    }) },
                { "_wait", async_method(w, "BaseSubprocessTransport", "_wait", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py waiter;
                        if (!self.attr("_returncode").is_none())
                        {
                            return self.attr("_returncode");
                        }
                        waiter = self.attr("_loop").method("create_future");
                        self.attr("_exit_waiters").method("append", { waiter });
                        return py_await(waiter);
                    }) },
                { "_try_finish", w.method("BaseSubprocessTransport", "_try_finish", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_finished").truthy())
                            py_raise(B<"AssertionError">()());
                        if (self.attr("_returncode").is_none())
                        {
                            return none();
                        }
                        if ((B<"all">())([&]() -> py { py const result = py_list(); py p; for (py const& item : iterate{ self.attr("_pipes").method("values") }) { p = item; result.method("append", { [&]() -> py { py const t = boolean(!p.is_none()); return !t.truthy() ? t : p.attr("disconnected"); }() }); } return result; }()).truthy())
                        {
                            self.set_attr("_finished", boolean(true));
                            self.method("_call", { self.attr("_call_connection_lost"), none() });
                        }
                        return none();
                    }) },
                { "_call_connection_lost", w.method("BaseSubprocessTransport", "_call_connection_lost", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        {
                            std::optional<py> pending12;
                            [[maybe_unused]] int flow12 = 0;
                            [[maybe_unused]] py result12;
                            try
                            {
                                self.attr("_protocol").method("connection_lost", { exc });
                            }
                            catch (raised const&)
                            {
                                pending12 = py_take_exception();
                            }
                            self.set_attr("_loop", none());
                            self.set_attr("_proc", none());
                            self.set_attr("_protocol", none());
                            if (pending12)
                                py_reraise(*pending12);
                            if (flow12 == 1)
                                return result12;
                        }
                        return none();
                    }) }, }));

            define("base_subprocess", "WriteSubprocessPipeProto", w.make_class("WriteSubprocessPipeProto", { G<"protocols.BaseProtocol">() }, {
                { "__init__", w.method("WriteSubprocessPipeProto", "__init__", { py_positional("proc"), py_positional("fd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py proc = a[1];
                        py fd = a[2];
                        self.set_attr("proc", proc);
                        self.set_attr("fd", fd);
                        self.set_attr("pipe", none());
                        self.set_attr("disconnected", boolean(false));
                        return none();
                    }) },
                { "connection_made", w.method("WriteSubprocessPipeProto", "connection_made", { py_positional("transport") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport = a[1];
                        self.set_attr("pipe", transport);
                        return none();
                    }) },
                { "__repr__", w.method("WriteSubprocessPipeProto", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S(std::string{ "<" } + self.attr("__class__").attr("__name__").str() + " fd=" + self.attr("fd").str() + " pipe=" + self.attr("pipe").repr() + ">");
                    }) },
                { "connection_lost", w.method("WriteSubprocessPipeProto", "connection_lost", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        self.set_attr("disconnected", boolean(true));
                        self.attr("proc").method("_pipe_connection_lost", { self.attr("fd"), exc });
                        self.set_attr("proc", none());
                        return none();
                    }) },
                { "pause_writing", w.method("WriteSubprocessPipeProto", "pause_writing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("proc").attr("_protocol").method("pause_writing");
                        return none();
                    }) },
                { "resume_writing", w.method("WriteSubprocessPipeProto", "resume_writing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("proc").attr("_protocol").method("resume_writing");
                        return none();
                    }) }, }));

            define("base_subprocess", "ReadSubprocessPipeProto", w.make_class("ReadSubprocessPipeProto", { G<"base_subprocess.WriteSubprocessPipeProto">(), G<"protocols.Protocol">() }, {
                { "data_received", w.method("ReadSubprocessPipeProto", "data_received", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.attr("proc").method("_pipe_data_received", { self.attr("fd"), data });
                        return none();
                    }) }, }));
        }

        // --- unix_events (Lib/asyncio/unix_events.py)

        [[maybe_unused]] void make_unix_events()
        {
            static module_writer const& w = writer("unix_events");
            define("unix_events", "__all__", py_tuple({ S<"SelectorEventLoop">(), S<"AbstractChildWatcher">(), S<"SafeChildWatcher">(), S<"FastChildWatcher">(), S<"PidfdChildWatcher">(), S<"MultiLoopChildWatcher">(), S<"ThreadedChildWatcher">(), S<"DefaultEventLoopPolicy">(), S<"EventLoop">() }));
#if defined(_WIN32)
            // (raise ImportError('Signals are not really supported on Windows'): the runtime registers the module only where it is supported)
#endif
            define("unix_events", "_sighandler_noop", w.function("_sighandler_noop", "_sighandler_noop", { py_positional("signum"), py_positional("frame") }, [](arguments a) -> py
                {
                    py signum = a[0];
                    py frame = a[1];
                    return none();
                }));

            define("unix_events", "waitstatus_to_exitcode", w.function("waitstatus_to_exitcode", "waitstatus_to_exitcode", { py_positional("status") }, [](arguments a) -> py
                {
                    py status = a[0];
                    try
                    {
                        return (Mod<"os">().attr("waitstatus_to_exitcode"))(status);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler1;
                        if (matches(handler1.exception, B<"ValueError">()))
                        {
                            // The child exited, but we don't understand its status.
                            // This shouldn't happen, but if it does, let's just
                            // return that status; perhaps that helps debug it.
                            return status;
                        }
                        else
                            py_reraise(handler1.exception);
                    }
                    return none();
                }));

            define("unix_events", "_UnixSelectorEventLoop", w.make_class("_UnixSelectorEventLoop", { G<"selector_events.BaseSelectorEventLoop">() }, {
                { "__init__", w.method("_UnixSelectorEventLoop", "__init__", { py_positional("selector", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py selector = a[1];
                        (B<"super">()(G<"unix_events._UnixSelectorEventLoop">(), self)).method("__init__", { selector });
                        self.set_attr("_signal_handlers", py_dict());
                        self.set_attr("_unix_server_sockets", py_dict());
                        return none();
                    }) },
                { "close", w.method("_UnixSelectorEventLoop", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sig;
                        (B<"super">()(G<"unix_events._UnixSelectorEventLoop">(), self)).method("close");
                        if (!(Mod<"sys">().attr("is_finalizing"))().truthy())
                        {
                            for (py const& item : iterate{ (B<"list">())(self.attr("_signal_handlers")) })
                            {
                                sig = item;
                                self.method("remove_signal_handler", { sig });
                            }
                        }
                        else if (self.attr("_signal_handlers").truthy())
                        {
                            (Mod<"warnings">().attr("warn")).call({ S(std::string{ "Closing the loop " } + self.repr() + " on interpreter shutdown stage, skipping signal handlers removal"), B<"ResourceWarning">() }, { { "source", self } });
                            self.attr("_signal_handlers").method("clear");
                        }
                        return none();
                    }) },
                { "_process_self_data", w.method("_UnixSelectorEventLoop", "_process_self_data", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py signum;
                        for (py const& item : iterate{ data })
                        {
                            signum = item;
                            if (!signum.truthy())
                            {
                                // ignore null bytes written by _write_to_self()
                                continue;
                            }
                            self.method("_handle_signal", { signum });
                        }
                        return none();
                    }) },
                { "add_signal_handler", w.method("_UnixSelectorEventLoop", "add_signal_handler", { py_positional("sig"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sig = a[1];
                        py callback = a[2];
                        py args = a[3];
                        py exc;
                        py handle;
                        py nexc;
                        if (((G<"coroutines.iscoroutine">())(callback).truthy() || (G<"coroutines.iscoroutinefunction">())(callback).truthy()))
                        {
                            py_raise((B<"TypeError">())(S<"coroutines cannot be used with add_signal_handler()">()));
                        }
                        self.method("_check_signal", { sig });
                        self.method("_check_closed");
                        try
                        {
                            // set_wakeup_fd() raises ValueError if this is not the
                            // main thread.  By calling it early we ensure that an
                            // event loop running in another thread cannot add a signal
                            // handler.
                            (Mod<"signal">().attr("set_wakeup_fd"))(self.attr("_csock").method("fileno"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler4;
                            if (matches(handler4.exception, py_tuple({ B<"ValueError">(), B<"OSError">() })))
                            {
                                exc = handler4.exception;
                                py_raise((B<"RuntimeError">())((B<"str">())(exc)));
                            }
                            else
                                py_reraise(handler4.exception);
                        }
                        handle = (G<"events.Handle">())(callback, args, self, none());
                        self.attr("_signal_handlers").set_item(sig, handle);
                        try
                        {
                            // Register a dummy signal handler to ask Python to write the signal
                            // number in the wakeup file descriptor. _process_self_data() will
                            // read signal numbers from this file descriptor to handle signals.
                            (Mod<"signal">().attr("signal"))(sig, G<"unix_events._sighandler_noop">());
                            // Set SA_RESTART to limit EINTR occurrences.
                            (Mod<"signal">().attr("siginterrupt"))(sig, boolean(false));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler5;
                            if (matches(handler5.exception, B<"OSError">()))
                            {
                                exc = handler5.exception;
                                del_item(self.attr("_signal_handlers"), sig);
                                if (!self.attr("_signal_handlers").truthy())
                                {
                                    try
                                    {
                                        (Mod<"signal">().attr("set_wakeup_fd"))(I<-1>());
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler6;
                                        if (matches(handler6.exception, py_tuple({ B<"ValueError">(), B<"OSError">() })))
                                        {
                                            nexc = handler6.exception;
                                            (G<"log.logger">()).method("info", { S<"set_wakeup_fd(-1) failed: %s">(), nexc });
                                        }
                                        else
                                            py_reraise(handler6.exception);
                                    }
                                }
                                if (exc.attr("errno") == (Mod<"errno">().attr("EINVAL")))
                                {
                                    py_raise((B<"RuntimeError">())(S(std::string{ "sig " } + sig.str() + " cannot be caught")));
                                }
                                else
                                {
                                    py_reraise(handler5.exception);
                                }
                            }
                            else
                                py_reraise(handler5.exception);
                        }
                        return none();
                    }, true) },
                { "_handle_signal", w.method("_UnixSelectorEventLoop", "_handle_signal", { py_positional("sig") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sig = a[1];
                        py handle;
                        handle = self.attr("_signal_handlers").method("get", { sig });
                        if (handle.is_none())
                        {
                            return none();
                        }
                        if (handle.attr("_cancelled").truthy())
                        {
                            self.method("remove_signal_handler", { sig });
                        }
                        else
                        {
                            self.method("_add_callback_signalsafe", { handle });
                        }
                        return none();
                    }) },
                { "remove_signal_handler", w.method("_UnixSelectorEventLoop", "remove_signal_handler", { py_positional("sig") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sig = a[1];
                        py exc;
                        py handler;
                        self.method("_check_signal", { sig });
                        try
                        {
                            del_item(self.attr("_signal_handlers"), sig);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler7;
                            if (matches(handler7.exception, B<"KeyError">()))
                            {
                                return boolean(false);
                            }
                            else
                                py_reraise(handler7.exception);
                        }
                        if (sig == (Mod<"signal">().attr("SIGINT")))
                        {
                            handler = Mod<"signal">().attr("default_int_handler");
                        }
                        else
                        {
                            handler = Mod<"signal">().attr("SIG_DFL");
                        }
                        try
                        {
                            (Mod<"signal">().attr("signal"))(sig, handler);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler8;
                            if (matches(handler8.exception, B<"OSError">()))
                            {
                                exc = handler8.exception;
                                if (exc.attr("errno") == (Mod<"errno">().attr("EINVAL")))
                                {
                                    py_raise((B<"RuntimeError">())(S(std::string{ "sig " } + sig.str() + " cannot be caught")));
                                }
                                else
                                {
                                    py_reraise(handler8.exception);
                                }
                            }
                            else
                                py_reraise(handler8.exception);
                        }
                        if (!self.attr("_signal_handlers").truthy())
                        {
                            try
                            {
                                (Mod<"signal">().attr("set_wakeup_fd"))(I<-1>());
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler9;
                                if (matches(handler9.exception, py_tuple({ B<"ValueError">(), B<"OSError">() })))
                                {
                                    exc = handler9.exception;
                                    (G<"log.logger">()).method("info", { S<"set_wakeup_fd(-1) failed: %s">(), exc });
                                }
                                else
                                    py_reraise(handler9.exception);
                            }
                        }
                        return boolean(true);
                    }) },
                { "_check_signal", w.method("_UnixSelectorEventLoop", "_check_signal", { py_positional("sig") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sig = a[1];
                        if (!isinstance(sig, B<"int">()))
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "sig must be an int, not " } + sig.repr())));
                        }
                        if (!(Mod<"signal">().attr("valid_signals"))().contains(sig))
                        {
                            py_raise((B<"ValueError">())(S(std::string{ "invalid signal number " } + sig.str())));
                        }
                        return none();
                    }) },
                { "_make_read_pipe_transport", w.method("_UnixSelectorEventLoop", "_make_read_pipe_transport", { py_positional("pipe"), py_positional("protocol"), py_positional("waiter", none()), py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pipe = a[1];
                        py protocol = a[2];
                        py waiter = a[3];
                        py extra = a[4];
                        return (G<"unix_events._UnixReadPipeTransport">())(self, pipe, protocol, waiter, extra);
                    }) },
                { "_make_write_pipe_transport", w.method("_UnixSelectorEventLoop", "_make_write_pipe_transport", { py_positional("pipe"), py_positional("protocol"), py_positional("waiter", none()), py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pipe = a[1];
                        py protocol = a[2];
                        py waiter = a[3];
                        py extra = a[4];
                        return (G<"unix_events._UnixWritePipeTransport">())(self, pipe, protocol, waiter, extra);
                    }) },
                { "_make_subprocess_transport", async_method(w, "_UnixSelectorEventLoop", "_make_subprocess_transport", { py_positional("protocol"), py_positional("args"), py_positional("shell"), py_positional("stdin"), py_positional("stdout"), py_positional("stderr"), py_positional("bufsize"), py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol = a[1];
                        py args = a[2];
                        py shell = a[3];
                        py stdin_ = a[4];
                        py stdout_ = a[5];
                        py stderr_ = a[6];
                        py bufsize = a[7];
                        py extra = a[8];
                        py kwargs = a[9];
                        py transp;
                        py waiter;
                        py watcher;
                        {
                            py const manager10 = (Mod<"warnings">().attr("catch_warnings"))();
                            py const exit10 = pyclass::special_method(manager10, "__exit__");
                            py const value10 = pyclass::special_method(manager10, "__enter__")(manager10);
                            [[maybe_unused]] int flow10 = 0;
                            [[maybe_unused]] py result10;
                            bool exited10 = false;
                            try
                            {
                                (Mod<"warnings">().attr("simplefilter"))(S<"ignore">(), B<"DeprecationWarning">());
                                watcher = (G<"events.get_child_watcher">())();
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler10;
                                exited10 = true;
                                if (!exit10(manager10, type_of(handler10.exception), handler10.exception, handler10.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler10.exception);
                            }
                            if (!exited10)
                                exit10(manager10, none(), none(), none());
                            if (flow10 == 1)
                                return result10;
                        }
                        {
                            py const manager11 = watcher;
                            py const exit11 = pyclass::special_method(manager11, "__exit__");
                            py const value11 = pyclass::special_method(manager11, "__enter__")(manager11);
                            [[maybe_unused]] int flow11 = 0;
                            [[maybe_unused]] py result11;
                            bool exited11 = false;
                            try
                            {
                                if (!watcher.method("is_active").truthy())
                                {
                                    // Check early.
                                    // Raising exception before process creation
                                    // prevents subprocess execution if the watcher
                                    // is not ready to handle it.
                                    py_raise((B<"RuntimeError">())(S<"asyncio.get_child_watcher() is not activated, subprocess support is not installed.">()));
                                }
                                waiter = self.method("create_future");
                                transp = call_ex((G<"unix_events._UnixSubprocessTransport">()), { self, protocol, args, shell, stdin_, stdout_, stderr_, bufsize }, py{}, kwargs, { { "waiter", waiter }, { "extra", extra } });
                                watcher.method("add_child_handler", { transp.method("get_pid"), self.attr("_child_watcher_callback"), transp });
                                try
                                {
                                    py_await(waiter);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler12;
                                    if (matches(handler12.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                    {
                                        py_reraise(handler12.exception);
                                    }
                                    else if (matches(handler12.exception, B<"BaseException">()))
                                    {
                                        transp.method("close");
                                        py_await(transp.method("_wait"));
                                        py_reraise(handler12.exception);
                                    }
                                    else
                                        py_reraise(handler12.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler11;
                                exited11 = true;
                                if (!exit11(manager11, type_of(handler11.exception), handler11.exception, handler11.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler11.exception);
                            }
                            if (!exited11)
                                exit11(manager11, none(), none(), none());
                            if (flow11 == 1)
                                return result11;
                        }
                        return transp;
                    }, false, true) },
                { "_child_watcher_callback", w.method("_UnixSelectorEventLoop", "_child_watcher_callback", { py_positional("pid"), py_positional("returncode"), py_positional("transp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        py returncode = a[2];
                        py transp = a[3];
                        self.method("call_soon_threadsafe", { transp.attr("_process_exited"), returncode });
                        return none();
                    }) },
                { "create_unix_connection", async_method(w, "_UnixSelectorEventLoop", "create_unix_connection", { py_positional("protocol_factory"), py_positional("path", none()), py_keyword("ssl", none()), py_keyword("sock", none()), py_keyword("server_hostname", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol_factory = a[1];
                        py path = a[2];
                        py ssl = a[3];
                        py sock = a[4];
                        py server_hostname = a[5];
                        py ssl_handshake_timeout = a[6];
                        py ssl_shutdown_timeout = a[7];
                        py protocol;
                        py transport;
                        if (!([&]() -> py { py const t = boolean(server_hostname.is_none()); return t.truthy() ? t : boolean(isinstance(server_hostname, B<"str">())); }()).truthy())
                            py_raise(B<"AssertionError">()());
                        if (ssl.truthy())
                        {
                            if (server_hostname.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"you have to pass server_hostname when using ssl">()));
                            }
                        }
                        else
                        {
                            if (!server_hostname.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"server_hostname is only meaningful with ssl">()));
                            }
                            if (!ssl_handshake_timeout.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"ssl_handshake_timeout is only meaningful with ssl">()));
                            }
                            if (!ssl_shutdown_timeout.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"ssl_shutdown_timeout is only meaningful with ssl">()));
                            }
                        }
                        if (!path.is_none())
                        {
                            if (!sock.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"path and sock can not be specified at the same time">()));
                            }
                            path = (Mod<"os">().attr("fspath"))(path);
                            sock = (Mod<"socket">().attr("socket"))(Mod<"socket">().attr("AF_UNIX"), Mod<"socket">().attr("SOCK_STREAM"), I<0>());
                            try
                            {
                                sock.method("setblocking", { boolean(false) });
                                py_await(self.method("sock_connect", { sock, path }));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler13;
                                {
                                    sock.method("close");
                                    py_reraise(handler13.exception);
                                }
                            }
                        }
                        else
                        {
                            if (sock.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"no path and sock were specified">()));
                            }
                            if (((sock.attr("family") != (Mod<"socket">().attr("AF_UNIX"))) || (sock.attr("type") != (Mod<"socket">().attr("SOCK_STREAM")))))
                            {
                                py_raise((B<"ValueError">())(S(std::string{ "A UNIX Domain Stream Socket was expected, got " } + sock.repr())));
                            }
                            sock.method("setblocking", { boolean(false) });
                        }
                        auto const items14 = unpack(py_await(self.method("_create_connection_transport", { sock, protocol_factory, ssl, server_hostname }, { { "ssl_handshake_timeout", ssl_handshake_timeout }, { "ssl_shutdown_timeout", ssl_shutdown_timeout } })), 2);
                        transport = items14[0];
                        protocol = items14[1];
                        return py_tuple({ transport, protocol });
                    }) },
                { "create_unix_server", async_method(w, "_UnixSelectorEventLoop", "create_unix_server", { py_positional("protocol_factory"), py_positional("path", none()), py_keyword("sock", none()), py_keyword("backlog", I<100>()), py_keyword("ssl", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()), py_keyword("start_serving", boolean(true)), py_keyword("cleanup_socket", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol_factory = a[1];
                        py path = a[2];
                        py sock = a[3];
                        py backlog = a[4];
                        py ssl = a[5];
                        py ssl_handshake_timeout = a[6];
                        py ssl_shutdown_timeout = a[7];
                        py start_serving = a[8];
                        py cleanup_socket = a[9];
                        py err;
                        py exc;
                        py msg;
                        py server;
                        if (isinstance(ssl, B<"bool">()))
                        {
                            py_raise((B<"TypeError">())(S<"ssl argument must be an SSLContext or None">()));
                        }
                        if (((!ssl_handshake_timeout.is_none()) && (!ssl.truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"ssl_handshake_timeout is only meaningful with ssl">()));
                        }
                        if (((!ssl_shutdown_timeout.is_none()) && (!ssl.truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"ssl_shutdown_timeout is only meaningful with ssl">()));
                        }
                        if (!path.is_none())
                        {
                            if (!sock.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"path and sock can not be specified at the same time">()));
                            }
                            path = (Mod<"os">().attr("fspath"))(path);
                            sock = (Mod<"socket">().attr("socket"))(Mod<"socket">().attr("AF_UNIX"), Mod<"socket">().attr("SOCK_STREAM"));
                            // Check for abstract socket. `str` and `bytes` paths are supported.
                            if (!py_tuple({ I<0>(), S<"\u0000">() }).contains(path[I<0>()]))
                            {
                                try
                                {
                                    if ((Mod<"stat">().attr("S_ISSOCK"))((Mod<"os">().attr("stat"))(path).attr("st_mode")).truthy())
                                    {
                                        (Mod<"os">().attr("remove"))(path);
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler15;
                                    if (matches(handler15.exception, B<"FileNotFoundError">()))
                                    {
                                    }
                                    else if (matches(handler15.exception, B<"OSError">()))
                                    {
                                        err = handler15.exception;
                                        // Directory may have permissions only to create socket.
                                        (G<"log.logger">()).method("error", { S<"Unable to check or remove stale UNIX socket %r: %r">(), path, err });
                                    }
                                    else
                                        py_reraise(handler15.exception);
                                }
                            }
                            try
                            {
                                sock.method("bind", { path });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler16;
                                if (matches(handler16.exception, B<"OSError">()))
                                {
                                    exc = handler16.exception;
                                    sock.method("close");
                                    if (exc.attr("errno") == (Mod<"errno">().attr("EADDRINUSE")))
                                    {
                                        // Let's improve the error message by adding
                                        // with what exact address it occurs.
                                        msg = S(std::string{ "Address " } + path.repr() + " is already in use");
                                        fn_raise_from((B<"OSError">())(Mod<"errno">().attr("EADDRINUSE"), msg).get(), none().get());
                                        throw raised{};
                                    }
                                    else
                                    {
                                        py_reraise(handler16.exception);
                                    }
                                }
                                else
                                {
                                    sock.method("close");
                                    py_reraise(handler16.exception);
                                }
                            }
                        }
                        else
                        {
                            if (sock.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"path was not specified, and no sock specified">()));
                            }
                            if (((sock.attr("family") != (Mod<"socket">().attr("AF_UNIX"))) || (sock.attr("type") != (Mod<"socket">().attr("SOCK_STREAM")))))
                            {
                                py_raise((B<"ValueError">())(S(std::string{ "A UNIX Domain Stream Socket was expected, got " } + sock.repr())));
                            }
                        }
                        if (cleanup_socket.truthy())
                        {
                            path = sock.method("getsockname");
                            // Check for abstract socket. `str` and `bytes` paths are supported.
                            if (!py_tuple({ I<0>(), S<"\u0000">() }).contains(path[I<0>()]))
                            {
                                try
                                {
                                    self.attr("_unix_server_sockets").set_item(sock, (Mod<"os">().attr("stat"))(path).attr("st_ino"));
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler17;
                                    if (matches(handler17.exception, B<"FileNotFoundError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler17.exception);
                                }
                            }
                        }
                        sock.method("setblocking", { boolean(false) });
                        server = (G<"base_events.Server">())(self, py_list({ sock }), protocol_factory, ssl, backlog, ssl_handshake_timeout, ssl_shutdown_timeout);
                        if (start_serving.truthy())
                        {
                            server.method("_start_serving");
                            // Skip one loop iteration so that all 'loop.add_reader'
                            // go through.
                            py_await((G<"tasks.sleep">())(I<0>()));
                        }
                        return server;
                    }) },
                { "_sock_sendfile_native", async_method(w, "_UnixSelectorEventLoop", "_sock_sendfile_native", { py_positional("sock"), py_positional("file"), py_positional("offset"), py_positional("count") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py file = a[2];
                        py offset = a[3];
                        py count = a[4];
                        py blocksize;
                        py err;
                        py fileno;
                        py fsize;
                        py fut;
                        try
                        {
                            Mod<"os">().attr("sendfile");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler18;
                            if (matches(handler18.exception, B<"AttributeError">()))
                            {
                                py_raise((G<"exceptions.SendfileNotAvailableError">())(S<"os.sendfile() is not available">()));
                            }
                            else
                                py_reraise(handler18.exception);
                        }
                        try
                        {
                            fileno = file.method("fileno");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler19;
                            if (matches(handler19.exception, py_tuple({ B<"AttributeError">(), Mod<"io">().attr("UnsupportedOperation") })))
                            {
                                err = handler19.exception;
                                py_raise((G<"exceptions.SendfileNotAvailableError">())(S<"not a regular file">()));
                            }
                            else
                                py_reraise(handler19.exception);
                        }
                        try
                        {
                            fsize = (Mod<"os">().attr("fstat"))(fileno).attr("st_size");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler20;
                            if (matches(handler20.exception, B<"OSError">()))
                            {
                                py_raise((G<"exceptions.SendfileNotAvailableError">())(S<"not a regular file">()));
                            }
                            else
                                py_reraise(handler20.exception);
                        }
                        blocksize = (count.truthy() ? count : fsize);
                        if (!blocksize.truthy())
                        {
                            return I<0>();
                        }
                        fut = self.method("create_future");
                        self.method("_sock_sendfile_native_impl", { fut, none(), sock, fileno, offset, count, blocksize, I<0>() });
                        return py_await(fut);
                    }) },
                { "_sock_sendfile_native_impl", w.method("_UnixSelectorEventLoop", "_sock_sendfile_native_impl", { py_positional("fut"), py_positional("registered_fd"), py_positional("sock"), py_positional("fileno"), py_positional("offset"), py_positional("count"), py_positional("blocksize"), py_positional("total_sent") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut = a[1];
                        py registered_fd = a[2];
                        py sock = a[3];
                        py fileno = a[4];
                        py offset = a[5];
                        py count = a[6];
                        py blocksize = a[7];
                        py total_sent = a[8];
                        py err;
                        py exc;
                        py fd;
                        py new_exc;
                        py sent;
                        fd = sock.method("fileno");
                        if (!registered_fd.is_none())
                        {
                            // Remove the callback early.  It should be rare that the
                            // selector says the fd is ready but the call still returns
                            // EAGAIN, and I am willing to take a hit in that case in
                            // order to simplify the common case.
                            self.method("remove_writer", { registered_fd });
                        }
                        if (fut.method("cancelled").truthy())
                        {
                            self.method("_sock_sendfile_update_filepos", { fileno, offset });
                            return none();
                        }
                        if (count.truthy())
                        {
                            blocksize = count - total_sent;
                            if (blocksize <= (I<0>()))
                            {
                                self.method("_sock_sendfile_update_filepos", { fileno, offset });
                                fut.method("set_result", { total_sent });
                                return none();
                            }
                        }
                        // On 32-bit architectures truncate to 1GiB to avoid OverflowError
                        blocksize = (B<"min">())(blocksize, floordiv((Mod<"sys">().attr("maxsize")), (I<2>())) + (I<1>()));
                        {
                            bool otherwise21 = false;
                            try
                            {
                                sent = (Mod<"os">().attr("sendfile"))(fd, fileno, offset, blocksize);
                                otherwise21 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler21;
                                if (matches(handler21.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                    if (registered_fd.is_none())
                                    {
                                        self.method("_sock_add_cancellation_callback", { fut, sock });
                                    }
                                    self.method("add_writer", { fd, self.attr("_sock_sendfile_native_impl"), fut, fd, sock, fileno, offset, count, blocksize, total_sent });
                                }
                                else if (matches(handler21.exception, B<"OSError">()))
                                {
                                    exc = handler21.exception;
                                    if (((!registered_fd.is_none()) && (exc.attr("errno") == (Mod<"errno">().attr("ENOTCONN"))) && (!(B<"type">())(exc).is((B<"ConnectionError">())))))
                                    {
                                        // If we have an ENOTCONN and this isn't a first call to
                                        // sendfile(), i.e. the connection was closed in the middle
                                        // of the operation, normalize the error to ConnectionError
                                        // to make it consistent across all Posix systems.
                                        new_exc = (B<"ConnectionError">())(S<"socket is not connected">(), Mod<"errno">().attr("ENOTCONN"));
                                        new_exc.set_attr("__cause__", exc);
                                        exc = new_exc;
                                    }
                                    if (total_sent == (I<0>()))
                                    {
                                        // We can get here for different reasons, the main
                                        // one being 'file' is not a regular mmap(2)-like
                                        // file, in which case we'll fall back on using
                                        // plain send().
                                        err = (G<"exceptions.SendfileNotAvailableError">())(S<"os.sendfile call failed">());
                                        self.method("_sock_sendfile_update_filepos", { fileno, offset });
                                        fut.method("set_exception", { err });
                                    }
                                    else
                                    {
                                        self.method("_sock_sendfile_update_filepos", { fileno, offset });
                                        fut.method("set_exception", { exc });
                                    }
                                }
                                else if (matches(handler21.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler21.exception);
                                }
                                else if (matches(handler21.exception, B<"BaseException">()))
                                {
                                    exc = handler21.exception;
                                    self.method("_sock_sendfile_update_filepos", { fileno, offset });
                                    fut.method("set_exception", { exc });
                                }
                                else
                                    py_reraise(handler21.exception);
                            }
                            if (otherwise21)
                            {
                                if (sent == (I<0>()))
                                {
                                    // EOF
                                    self.method("_sock_sendfile_update_filepos", { fileno, offset });
                                    fut.method("set_result", { total_sent });
                                }
                                else
                                {
                                    offset = inplace(NEOS_PY_ADD, offset, sent);
                                    total_sent = inplace(NEOS_PY_ADD, total_sent, sent);
                                    if (registered_fd.is_none())
                                    {
                                        self.method("_sock_add_cancellation_callback", { fut, sock });
                                    }
                                    self.method("add_writer", { fd, self.attr("_sock_sendfile_native_impl"), fut, fd, sock, fileno, offset, count, blocksize, total_sent });
                                }
                            }
                        }
                        return none();
                    }) },
                { "_sock_sendfile_update_filepos", w.method("_UnixSelectorEventLoop", "_sock_sendfile_update_filepos", { py_positional("fileno"), py_positional("offset") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fileno = a[1];
                        py offset = a[2];
                        (Mod<"os">().attr("lseek"))(fileno, offset, Mod<"os">().attr("SEEK_SET"));
                        return none();
                    }) },
                { "_sock_add_cancellation_callback", w.method("_UnixSelectorEventLoop", "_sock_add_cancellation_callback", { py_positional("fut"), py_positional("sock") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py fut = a[1];
                        auto const sock = std::make_shared<py>(a[2]);
                        py cb;
                        cb = w.function("cb", "_UnixSelectorEventLoop._sock_add_cancellation_callback.<locals>.cb", { py_positional("fut") }, [self, sock](arguments a) -> py
    {
        py fut = a[0];
        py fd;
        if (fut.method("cancelled").truthy())
        {
            fd = (*sock).method("fileno");
            if (fd != (I<-1>()))
            {
                (*self).method("remove_writer", { fd });
            }
        }
        return none();
    });
                        fut.method("add_done_callback", { cb });
                        return none();
                    }) },
                { "_stop_serving", w.method("_UnixSelectorEventLoop", "_stop_serving", { py_positional("sock") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py err;
                        py path;
                        py prev_ino;
                        if (self.attr("_unix_server_sockets").contains(sock))
                        {
                            path = sock.method("getsockname");
                        }
                        else
                        {
                            path = none();
                        }
                        (B<"super">()(G<"unix_events._UnixSelectorEventLoop">(), self)).method("_stop_serving", { sock });
                        if (!path.is_none())
                        {
                            prev_ino = self.attr("_unix_server_sockets")[sock];
                            del_item(self.attr("_unix_server_sockets"), sock);
                            try
                            {
                                if ((Mod<"os">().attr("stat"))(path).attr("st_ino") == prev_ino)
                                {
                                    (Mod<"os">().attr("unlink"))(path);
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler22;
                                if (matches(handler22.exception, B<"FileNotFoundError">()))
                                {
                                }
                                else if (matches(handler22.exception, B<"OSError">()))
                                {
                                    err = handler22.exception;
                                    (G<"log.logger">()).method("error", { S<"Unable to clean up listening UNIX socket %r: %r">(), path, err });
                                }
                                else
                                    py_reraise(handler22.exception);
                            }
                        }
                        return none();
                    }) }, }));

            define("unix_events", "_UnixReadPipeTransport", w.make_class("_UnixReadPipeTransport", { G<"transports.ReadTransport">() }, {
                { "max_size", (I<256>()) * (I<1024>()) },
                { "__init__", w.method("_UnixReadPipeTransport", "__init__", { py_positional("loop"), py_positional("pipe"), py_positional("protocol"), py_positional("waiter", none()), py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        py pipe = a[2];
                        py protocol = a[3];
                        py waiter = a[4];
                        py extra = a[5];
                        py mode;
                        (B<"super">()(G<"unix_events._UnixReadPipeTransport">(), self)).method("__init__", { extra });
                        self.attr("_extra").set_item(S<"pipe">(), pipe);
                        self.set_attr("_loop", loop);
                        self.set_attr("_pipe", pipe);
                        self.set_attr("_fileno", pipe.method("fileno"));
                        self.set_attr("_protocol", protocol);
                        self.set_attr("_closing", boolean(false));
                        self.set_attr("_paused", boolean(false));
                        mode = (Mod<"os">().attr("fstat"))(self.attr("_fileno")).attr("st_mode");
                        if (!([&]() -> py { py const t = (Mod<"stat">().attr("S_ISFIFO"))(mode); return t.truthy() ? t : [&]() -> py { py const t = (Mod<"stat">().attr("S_ISSOCK"))(mode); return t.truthy() ? t : (Mod<"stat">().attr("S_ISCHR"))(mode); }(); }()).truthy())
                        {
                            self.set_attr("_pipe", none());
                            self.set_attr("_fileno", none());
                            self.set_attr("_protocol", none());
                            py_raise((B<"ValueError">())(S<"Pipe transport is for pipes/sockets only.">()));
                        }
                        (Mod<"os">().attr("set_blocking"))(self.attr("_fileno"), boolean(false));
                        self.attr("_loop").method("call_soon", { self.attr("_protocol").attr("connection_made"), self });
                        // only start reading when connection_made() has been called
                        self.attr("_loop").method("call_soon", { self.attr("_add_reader"), self.attr("_fileno"), self.attr("_read_ready") });
                        if (!waiter.is_none())
                        {
                            // only wake up the waiter when connection_made() has been called
                            self.attr("_loop").method("call_soon", { G<"futures._set_result_unless_cancelled">(), waiter, none() });
                        }
                        return none();
                    }) },
                { "_add_reader", w.method("_UnixReadPipeTransport", "_add_reader", { py_positional("fd"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py callback = a[2];
                        if (!self.method("is_reading").truthy())
                        {
                            return none();
                        }
                        self.attr("_loop").method("_add_reader", { fd, callback });
                        return none();
                    }) },
                { "is_reading", w.method("_UnixReadPipeTransport", "is_reading", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = boolean(!self.attr("_paused").truthy()); return !t.truthy() ? t : boolean(!self.attr("_closing").truthy()); }();
                    }) },
                { "__repr__", w.method("_UnixReadPipeTransport", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info;
                        py polling;
                        py selector;
                        info = py_list({ self.attr("__class__").attr("__name__") });
                        if (self.attr("_pipe").is_none())
                        {
                            info.method("append", { S<"closed">() });
                        }
                        else if (self.attr("_closing").truthy())
                        {
                            info.method("append", { S<"closing">() });
                        }
                        info.method("append", { S(std::string{ "fd=" } + self.attr("_fileno").str()) });
                        selector = (B<"getattr">())(self.attr("_loop"), S<"_selector">(), none());
                        if (((!self.attr("_pipe").is_none()) && (!selector.is_none())))
                        {
                            polling = (G<"selector_events._test_selector_event">())(selector, self.attr("_fileno"), Mod<"selectors">().attr("EVENT_READ"));
                            if (polling.truthy())
                            {
                                info.method("append", { S<"polling">() });
                            }
                            else
                            {
                                info.method("append", { S<"idle">() });
                            }
                        }
                        else if (!self.attr("_pipe").is_none())
                        {
                            info.method("append", { S<"open">() });
                        }
                        else
                        {
                            info.method("append", { S<"closed">() });
                        }
                        return (S<"<{}>">()).method("format", { (S<" ">()).method("join", { info }) });
                    }) },
                { "_read_ready", w.method("_UnixReadPipeTransport", "_read_ready", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data;
                        py exc;
                        {
                            bool otherwise23 = false;
                            try
                            {
                                data = (Mod<"os">().attr("read"))(self.attr("_fileno"), self.attr("max_size"));
                                otherwise23 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler23;
                                if (matches(handler23.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                }
                                else if (matches(handler23.exception, B<"OSError">()))
                                {
                                    exc = handler23.exception;
                                    self.method("_fatal_error", { exc, S<"Fatal read error on pipe transport">() });
                                }
                                else
                                    py_reraise(handler23.exception);
                            }
                            if (otherwise23)
                            {
                                if (data.truthy())
                                {
                                    self.attr("_protocol").method("data_received", { data });
                                }
                                else
                                {
                                    if (self.attr("_loop").method("get_debug").truthy())
                                    {
                                        (G<"log.logger">()).method("info", { S<"%r was closed by peer">(), self });
                                    }
                                    self.set_attr("_closing", boolean(true));
                                    self.attr("_loop").method("_remove_reader", { self.attr("_fileno") });
                                    self.attr("_loop").method("call_soon", { self.attr("_protocol").attr("eof_received") });
                                    self.attr("_loop").method("call_soon", { self.attr("_call_connection_lost"), none() });
                                }
                            }
                        }
                        return none();
                    }) },
                { "pause_reading", w.method("_UnixReadPipeTransport", "pause_reading", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.method("is_reading").truthy())
                        {
                            return none();
                        }
                        self.set_attr("_paused", boolean(true));
                        self.attr("_loop").method("_remove_reader", { self.attr("_fileno") });
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            (G<"log.logger">()).method("debug", { S<"%r pauses reading">(), self });
                        }
                        return none();
                    }) },
                { "resume_reading", w.method("_UnixReadPipeTransport", "resume_reading", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((self.attr("_closing").truthy() || (!self.attr("_paused").truthy())))
                        {
                            return none();
                        }
                        self.set_attr("_paused", boolean(false));
                        self.attr("_loop").method("_add_reader", { self.attr("_fileno"), self.attr("_read_ready") });
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            (G<"log.logger">()).method("debug", { S<"%r resumes reading">(), self });
                        }
                        return none();
                    }) },
                { "set_protocol", w.method("_UnixReadPipeTransport", "set_protocol", { py_positional("protocol") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol = a[1];
                        self.set_attr("_protocol", protocol);
                        return none();
                    }) },
                { "get_protocol", w.method("_UnixReadPipeTransport", "get_protocol", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_protocol");
                    }) },
                { "is_closing", w.method("_UnixReadPipeTransport", "is_closing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_closing");
                    }) },
                { "close", w.method("_UnixReadPipeTransport", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_closing").truthy())
                        {
                            self.method("_close", { none() });
                        }
                        return none();
                    }) },
                { "__del__", w.method("_UnixReadPipeTransport", "__del__", { py_positional("_warn", Mod<"warnings">().attr("warn")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _warn = a[1];
                        if (!self.attr("_pipe").is_none())
                        {
                            _warn.call({ S(std::string{ "unclosed transport " } + self.repr()), B<"ResourceWarning">() }, { { "source", self } });
                            self.attr("_pipe").method("close");
                        }
                        return none();
                    }) },
                { "_fatal_error", w.method("_UnixReadPipeTransport", "_fatal_error", { py_positional("exc"), py_positional("message", S<"Fatal error on pipe transport">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        py message = a[2];
                        if ((isinstance(exc, B<"OSError">()) && (exc.attr("errno") == (Mod<"errno">().attr("EIO")))))
                        {
                            if (self.attr("_loop").method("get_debug").truthy())
                            {
                                (G<"log.logger">()).method("debug", { S<"%r: %s">(), self, message }, { { "exc_info", boolean(true) } });
                            }
                        }
                        else
                        {
                            self.attr("_loop").method("call_exception_handler", { dict_of({ { S<"message">(), message }, { S<"exception">(), exc }, { S<"transport">(), self }, { S<"protocol">(), self.attr("_protocol") } }) });
                        }
                        self.method("_close", { exc });
                        return none();
                    }) },
                { "_close", w.method("_UnixReadPipeTransport", "_close", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        self.set_attr("_closing", boolean(true));
                        self.attr("_loop").method("_remove_reader", { self.attr("_fileno") });
                        self.attr("_loop").method("call_soon", { self.attr("_call_connection_lost"), exc });
                        return none();
                    }) },
                { "_call_connection_lost", w.method("_UnixReadPipeTransport", "_call_connection_lost", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        {
                            std::optional<py> pending24;
                            [[maybe_unused]] int flow24 = 0;
                            [[maybe_unused]] py result24;
                            try
                            {
                                self.attr("_protocol").method("connection_lost", { exc });
                            }
                            catch (raised const&)
                            {
                                pending24 = py_take_exception();
                            }
                            self.attr("_pipe").method("close");
                            self.set_attr("_pipe", none());
                            self.set_attr("_protocol", none());
                            self.set_attr("_loop", none());
                            if (pending24)
                                py_reraise(*pending24);
                            if (flow24 == 1)
                                return result24;
                        }
                        return none();
                    }) }, }));

            define("unix_events", "_UnixWritePipeTransport", w.make_class("_UnixWritePipeTransport", { G<"transports._FlowControlMixin">(), G<"transports.WriteTransport">() }, {
                { "__init__", w.method("_UnixWritePipeTransport", "__init__", { py_positional("loop"), py_positional("pipe"), py_positional("protocol"), py_positional("waiter", none()), py_positional("extra", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        py pipe = a[2];
                        py protocol = a[3];
                        py waiter = a[4];
                        py extra = a[5];
                        py is_char;
                        py is_fifo;
                        py is_named_fifo_without_close_event;
                        py is_socket;
                        py mode;
                        py pipe_stat;
                        (B<"super">()(G<"unix_events._UnixWritePipeTransport">(), self)).method("__init__", { extra, loop });
                        self.attr("_extra").set_item(S<"pipe">(), pipe);
                        self.set_attr("_pipe", pipe);
                        self.set_attr("_fileno", pipe.method("fileno"));
                        self.set_attr("_protocol", protocol);
                        self.set_attr("_buffer", (B<"bytearray">())());
                        self.set_attr("_conn_lost", I<0>());
                        self.set_attr("_closing", boolean(false));
                        pipe_stat = (Mod<"os">().attr("fstat"))(self.attr("_fileno"));
                        mode = pipe_stat.attr("st_mode");
                        is_char = (Mod<"stat">().attr("S_ISCHR"))(mode);
                        is_fifo = (Mod<"stat">().attr("S_ISFIFO"))(mode);
                        is_socket = (Mod<"stat">().attr("S_ISSOCK"))(mode);
                        if (!([&]() -> py { py const t = is_char; return t.truthy() ? t : [&]() -> py { py const t = is_fifo; return t.truthy() ? t : is_socket; }(); }()).truthy())
                        {
                            self.set_attr("_pipe", none());
                            self.set_attr("_fileno", none());
                            self.set_attr("_protocol", none());
                            py_raise((B<"ValueError">())(S<"Pipe transport is only for pipes, sockets and character devices">()));
                        }
                        (Mod<"os">().attr("set_blocking"))(self.attr("_fileno"), boolean(false));
                        self.attr("_loop").method("call_soon", { self.attr("_protocol").attr("connection_made"), self });
                        // On AIX, the reader trick (to be notified when the read end of the
                        // socket is closed) only works for sockets. On other platforms it
                        // works for pipes and sockets. (Exception: OS X 10.4?  Issue #19294.)
                        // On macOS and Solaris, the trick misfires for named FIFOs (but not for
                        // pipes created with os.pipe(), which have st_nlink == 0): the write
                        // end polls as readable whenever unread data sits in the FIFO, and no
                        // event is delivered when the read end is closed, so it can only
                        // ever report a false disconnection (gh-145030). The same XNU
                        // behaviour applies on iOS/tvOS/watchOS (sys.platform is not
                        // "darwin" there).
                        is_named_fifo_without_close_event = [&]() -> py { py const t = boolean(set_of({ S<"darwin">(), S<"ios">(), S<"tvos">(), S<"watchos">(), S<"sunos5">() }).contains((Mod<"sys">().attr("platform")))); return !t.truthy() ? t : [&]() -> py { py const t = is_fifo; return !t.truthy() ? t : boolean(pipe_stat.attr("st_nlink") > (I<0>())); }(); }();
                        if ((is_socket.truthy() || ([&]() -> py { py const t = is_fifo; return !t.truthy() ? t : [&]() -> py { py const t = boolean(!(Mod<"sys">().attr("platform")).method("startswith", { S<"aix">() }).truthy()); return !t.truthy() ? t : boolean(!is_named_fifo_without_close_event.truthy()); }(); }()).truthy()))
                        {
                            // only start reading when connection_made() has been called
                            self.attr("_loop").method("call_soon", { self.attr("_loop").attr("_add_reader"), self.attr("_fileno"), self.attr("_read_ready") });
                        }
                        if (!waiter.is_none())
                        {
                            // only wake up the waiter when connection_made() has been called
                            self.attr("_loop").method("call_soon", { G<"futures._set_result_unless_cancelled">(), waiter, none() });
                        }
                        return none();
                    }) },
                { "__repr__", w.method("_UnixWritePipeTransport", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py bufsize;
                        py info;
                        py polling;
                        py selector;
                        info = py_list({ self.attr("__class__").attr("__name__") });
                        if (self.attr("_pipe").is_none())
                        {
                            info.method("append", { S<"closed">() });
                        }
                        else if (self.attr("_closing").truthy())
                        {
                            info.method("append", { S<"closing">() });
                        }
                        info.method("append", { S(std::string{ "fd=" } + self.attr("_fileno").str()) });
                        selector = (B<"getattr">())(self.attr("_loop"), S<"_selector">(), none());
                        if (((!self.attr("_pipe").is_none()) && (!selector.is_none())))
                        {
                            polling = (G<"selector_events._test_selector_event">())(selector, self.attr("_fileno"), Mod<"selectors">().attr("EVENT_WRITE"));
                            if (polling.truthy())
                            {
                                info.method("append", { S<"polling">() });
                            }
                            else
                            {
                                info.method("append", { S<"idle">() });
                            }
                            bufsize = self.method("get_write_buffer_size");
                            info.method("append", { S(std::string{ "bufsize=" } + bufsize.str()) });
                        }
                        else if (!self.attr("_pipe").is_none())
                        {
                            info.method("append", { S<"open">() });
                        }
                        else
                        {
                            info.method("append", { S<"closed">() });
                        }
                        return (S<"<{}>">()).method("format", { (S<" ">()).method("join", { info }) });
                    }) },
                { "get_write_buffer_size", w.method("_UnixWritePipeTransport", "get_write_buffer_size", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("_buffer").size() };
                    }) },
                { "_read_ready", w.method("_UnixWritePipeTransport", "_read_ready", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            (G<"log.logger">()).method("info", { S<"%r was closed by peer">(), self });
                        }
                        if (self.attr("_buffer").truthy())
                        {
                            self.method("_close", { (B<"BrokenPipeError">())() });
                        }
                        else
                        {
                            self.method("_close");
                        }
                        return none();
                    }) },
                { "write", w.method("_UnixWritePipeTransport", "write", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py exc;
                        py n;
                        if (!isinstance(data, py_tuple({ B<"bytes">(), B<"bytearray">(), B<"memoryview">() })))
                            py_raise(B<"AssertionError">()(S(data.repr())));
                        if (isinstance(data, B<"bytearray">()))
                        {
                            data = (B<"memoryview">())(data);
                        }
                        if (!data.truthy())
                        {
                            return none();
                        }
                        if ((self.attr("_conn_lost").truthy() || self.attr("_closing").truthy()))
                        {
                            if (self.attr("_conn_lost") >= (G<"constants.LOG_THRESHOLD_FOR_CONNLOST_WRITES">()))
                            {
                                (G<"log.logger">()).method("warning", { S<"pipe closed by peer or os.write(pipe, data) raised exception.">() });
                            }
                            {
                                py const target25 = self;
                                target25.set_attr("_conn_lost", inplace(NEOS_PY_ADD, target25.attr("_conn_lost"), I<1>()));
                            }
                            return none();
                        }
                        if (!self.attr("_buffer").truthy())
                        {
                            // Attempt to send it right away first.
                            try
                            {
                                n = (Mod<"os">().attr("write"))(self.attr("_fileno"), data);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler26;
                                if (matches(handler26.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                    n = I<0>();
                                }
                                else if (matches(handler26.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler26.exception);
                                }
                                else if (matches(handler26.exception, B<"BaseException">()))
                                {
                                    exc = handler26.exception;
                                    {
                                        py const target27 = self;
                                        target27.set_attr("_conn_lost", inplace(NEOS_PY_ADD, target27.attr("_conn_lost"), I<1>()));
                                    }
                                    self.method("_fatal_error", { exc, S<"Fatal write error on pipe transport">() });
                                    return none();
                                }
                                else
                                    py_reraise(handler26.exception);
                            }
                            if (n == py{ data.size() })
                            {
                                return none();
                            }
                            else if (n > (I<0>()))
                            {
                                data = (B<"memoryview">())(data)[B<"slice">()(n, none(), none())];
                            }
                            self.attr("_loop").method("_add_writer", { self.attr("_fileno"), self.attr("_write_ready") });
                        }
                        {
                            py const target28 = self;
                            target28.set_attr("_buffer", inplace(NEOS_PY_ADD, target28.attr("_buffer"), data));
                        }
                        self.method("_maybe_pause_protocol");
                        return none();
                    }) },
                { "_write_ready", w.method("_UnixWritePipeTransport", "_write_ready", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc;
                        py n;
                        if (!self.attr("_buffer").truthy())
                            py_raise(B<"AssertionError">()(S<"Data should not be empty">()));
                        {
                            bool otherwise29 = false;
                            try
                            {
                                n = (Mod<"os">().attr("write"))(self.attr("_fileno"), self.attr("_buffer"));
                                otherwise29 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler29;
                                if (matches(handler29.exception, py_tuple({ B<"BlockingIOError">(), B<"InterruptedError">() })))
                                {
                                }
                                else if (matches(handler29.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                                {
                                    py_reraise(handler29.exception);
                                }
                                else if (matches(handler29.exception, B<"BaseException">()))
                                {
                                    exc = handler29.exception;
                                    self.attr("_buffer").method("clear");
                                    {
                                        py const target30 = self;
                                        target30.set_attr("_conn_lost", inplace(NEOS_PY_ADD, target30.attr("_conn_lost"), I<1>()));
                                    }
                                    // Remove writer here, _fatal_error() doesn't it
                                    // because _buffer is empty.
                                    self.attr("_loop").method("_remove_writer", { self.attr("_fileno") });
                                    self.method("_fatal_error", { exc, S<"Fatal write error on pipe transport">() });
                                }
                                else
                                    py_reraise(handler29.exception);
                            }
                            if (otherwise29)
                            {
                                if (n == py{ self.attr("_buffer").size() })
                                {
                                    self.attr("_buffer").method("clear");
                                    self.attr("_loop").method("_remove_writer", { self.attr("_fileno") });
                                    self.method("_maybe_resume_protocol");
                                    if (self.attr("_closing").truthy())
                                    {
                                        self.attr("_loop").method("_remove_reader", { self.attr("_fileno") });
                                        self.method("_call_connection_lost", { none() });
                                    }
                                    return none();
                                }
                                else if (n > (I<0>()))
                                {
                                    del_item(self.attr("_buffer"), B<"slice">()(none(), n, none()));
                                }
                            }
                        }
                        return none();
                    }) },
                { "can_write_eof", w.method("_UnixWritePipeTransport", "can_write_eof", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) },
                { "write_eof", w.method("_UnixWritePipeTransport", "write_eof", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_closing").truthy())
                        {
                            return none();
                        }
                        if (!self.attr("_pipe").truthy())
                            py_raise(B<"AssertionError">()());
                        self.set_attr("_closing", boolean(true));
                        if (!self.attr("_buffer").truthy())
                        {
                            self.attr("_loop").method("_remove_reader", { self.attr("_fileno") });
                            self.attr("_loop").method("call_soon", { self.attr("_call_connection_lost"), none() });
                        }
                        return none();
                    }) },
                { "set_protocol", w.method("_UnixWritePipeTransport", "set_protocol", { py_positional("protocol") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py protocol = a[1];
                        self.set_attr("_protocol", protocol);
                        return none();
                    }) },
                { "get_protocol", w.method("_UnixWritePipeTransport", "get_protocol", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_protocol");
                    }) },
                { "is_closing", w.method("_UnixWritePipeTransport", "is_closing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_closing");
                    }) },
                { "close", w.method("_UnixWritePipeTransport", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (((!self.attr("_pipe").is_none()) && (!self.attr("_closing").truthy())))
                        {
                            // write_eof is all what we needed to close the write pipe
                            self.method("write_eof");
                        }
                        return none();
                    }) },
                { "__del__", w.method("_UnixWritePipeTransport", "__del__", { py_positional("_warn", Mod<"warnings">().attr("warn")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _warn = a[1];
                        if (!self.attr("_pipe").is_none())
                        {
                            _warn.call({ S(std::string{ "unclosed transport " } + self.repr()), B<"ResourceWarning">() }, { { "source", self } });
                            self.attr("_pipe").method("close");
                        }
                        return none();
                    }) },
                { "abort", w.method("_UnixWritePipeTransport", "abort", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_close", { none() });
                        return none();
                    }) },
                { "_fatal_error", w.method("_UnixWritePipeTransport", "_fatal_error", { py_positional("exc"), py_positional("message", S<"Fatal error on pipe transport">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        py message = a[2];
                        if (isinstance(exc, B<"OSError">()))
                        {
                            if (self.attr("_loop").method("get_debug").truthy())
                            {
                                (G<"log.logger">()).method("debug", { S<"%r: %s">(), self, message }, { { "exc_info", boolean(true) } });
                            }
                        }
                        else
                        {
                            self.attr("_loop").method("call_exception_handler", { dict_of({ { S<"message">(), message }, { S<"exception">(), exc }, { S<"transport">(), self }, { S<"protocol">(), self.attr("_protocol") } }) });
                        }
                        self.method("_close", { exc });
                        return none();
                    }) },
                { "_close", w.method("_UnixWritePipeTransport", "_close", { py_positional("exc", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        self.set_attr("_closing", boolean(true));
                        if (self.attr("_buffer").truthy())
                        {
                            self.attr("_loop").method("_remove_writer", { self.attr("_fileno") });
                        }
                        self.attr("_buffer").method("clear");
                        self.attr("_loop").method("_remove_reader", { self.attr("_fileno") });
                        self.attr("_loop").method("call_soon", { self.attr("_call_connection_lost"), exc });
                        return none();
                    }) },
                { "_call_connection_lost", w.method("_UnixWritePipeTransport", "_call_connection_lost", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        {
                            std::optional<py> pending31;
                            [[maybe_unused]] int flow31 = 0;
                            [[maybe_unused]] py result31;
                            try
                            {
                                self.attr("_protocol").method("connection_lost", { exc });
                            }
                            catch (raised const&)
                            {
                                pending31 = py_take_exception();
                            }
                            self.attr("_pipe").method("close");
                            self.set_attr("_pipe", none());
                            self.set_attr("_protocol", none());
                            self.set_attr("_loop", none());
                            if (pending31)
                                py_reraise(*pending31);
                            if (flow31 == 1)
                                return result31;
                        }
                        return none();
                    }) }, }));

            define("unix_events", "_UnixSubprocessTransport", w.make_class("_UnixSubprocessTransport", { G<"base_subprocess.BaseSubprocessTransport">() }, {
                { "_start", w.method("_UnixSubprocessTransport", "_start", { py_positional("args"), py_positional("shell"), py_positional("stdin"), py_positional("stdout"), py_positional("stderr"), py_positional("bufsize") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py shell = a[2];
                        py stdin_ = a[3];
                        py stdout_ = a[4];
                        py stderr_ = a[5];
                        py bufsize = a[6];
                        py kwargs = a[7];
                        py stdin_w;
                        stdin_w = none();
                        if (((stdin_ == (py{ -1 } /* subprocess.PIPE */)) && (Mod<"sys">().attr("platform")).method("startswith", { S<"aix">() }).truthy()))
                        {
                            // Use a socket pair for stdin on AIX, since it does not
                            // support selecting read events on the write end of a
                            // socket (which we use in order to detect closing of the
                            // other end).
                            auto const items32 = unpack((Mod<"socket">().attr("socketpair"))(), 2);
                            stdin_ = items32[0];
                            stdin_w = items32[1];
                        }
                        {
                            std::optional<py> pending33;
                            [[maybe_unused]] int flow33 = 0;
                            [[maybe_unused]] py result33;
                            try
                            {
                                self.set_attr("_proc", call_ex((Mod<"subprocess">().attr("Popen")), { args }, py{}, kwargs, { { "shell", shell }, { "stdin", stdin_ }, { "stdout", stdout_ }, { "stderr", stderr_ }, { "universal_newlines", boolean(false) }, { "bufsize", bufsize } }));
                                if (!stdin_w.is_none())
                                {
                                    stdin_.method("close");
                                    self.attr("_proc").set_attr("stdin", (B<"open">()).call({ stdin_w.method("detach"), S<"wb">() }, { { "buffering", bufsize } }));
                                    stdin_w = none();
                                }
                            }
                            catch (raised const&)
                            {
                                pending33 = py_take_exception();
                            }
                            if (!stdin_w.is_none())
                            {
                                stdin_.method("close");
                                stdin_w.method("close");
                            }
                            if (pending33)
                                py_reraise(*pending33);
                            if (flow33 == 1)
                                return result33;
                        }
                        return none();
                    }, false, true) }, }));

            define("unix_events", "AbstractChildWatcher", w.make_class("AbstractChildWatcher", {  }, {
                { "__init_subclass__", w.method("AbstractChildWatcher", "__init_subclass__", {  }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        if (cls.attr("__module__") != "asyncio.unix_events")
                        {
                            (Mod<"warnings">().attr("_deprecated")).call({ S<"AbstractChildWatcher">(), S<"{name!r} is deprecated as of Python 3.12 and will be removed in Python {remove}.">() }, { { "remove", py_tuple({ I<3>(), I<14>() }) } });
                        }
                        return none();
                    }) },
                { "add_child_handler", w.method("AbstractChildWatcher", "add_child_handler", { py_positional("pid"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        py callback = a[2];
                        py args = a[3];
                        py_raise((B<"NotImplementedError">())());
                    }, true) },
                { "remove_child_handler", w.method("AbstractChildWatcher", "remove_child_handler", { py_positional("pid") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        py_raise((B<"NotImplementedError">())());
                    }) },
                { "attach_loop", w.method("AbstractChildWatcher", "attach_loop", { py_positional("loop") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        py_raise((B<"NotImplementedError">())());
                    }) },
                { "close", w.method("AbstractChildWatcher", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())());
                    }) },
                { "is_active", w.method("AbstractChildWatcher", "is_active", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())());
                    }) },
                { "__enter__", w.method("AbstractChildWatcher", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())());
                    }) },
                { "__exit__", w.method("AbstractChildWatcher", "__exit__", { py_positional("a"), py_positional("b"), py_positional("c") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_ = a[1];
                        py b = a[2];
                        py c = a[3];
                        py_raise((B<"NotImplementedError">())());
                    }) }, }));

            define("unix_events", "PidfdChildWatcher", w.make_class("PidfdChildWatcher", { G<"unix_events.AbstractChildWatcher">() }, {
                { "__enter__", w.method("PidfdChildWatcher", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("PidfdChildWatcher", "__exit__", { py_positional("exc_type"), py_positional("exc_value"), py_positional("exc_traceback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_type = a[1];
                        py exc_value = a[2];
                        py exc_traceback = a[3];
                        return none();
                    }) },
                { "is_active", w.method("PidfdChildWatcher", "is_active", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) },
                { "close", w.method("PidfdChildWatcher", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "attach_loop", w.method("PidfdChildWatcher", "attach_loop", { py_positional("loop") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        return none();
                    }) },
                { "add_child_handler", w.method("PidfdChildWatcher", "add_child_handler", { py_positional("pid"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        py callback = a[2];
                        py args = a[3];
                        py loop;
                        py pidfd;
                        loop = (G<"events.get_running_loop">())();
                        pidfd = (Mod<"os">().attr("pidfd_open"))(pid);
                        loop.method("_add_reader", { pidfd, self.attr("_do_wait"), pid, pidfd, callback, args });
                        return none();
                    }, true) },
                { "_do_wait", w.method("PidfdChildWatcher", "_do_wait", { py_positional("pid"), py_positional("pidfd"), py_positional("callback"), py_positional("args") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        py pidfd = a[2];
                        py callback = a[3];
                        py args = a[4];
                        py _;
                        py loop;
                        py returncode;
                        py status;
                        loop = (G<"events.get_running_loop">())();
                        loop.method("_remove_reader", { pidfd });
                        {
                            bool otherwise34 = false;
                            try
                            {
                                auto const items35 = unpack((Mod<"os">().attr("waitpid"))(pid, I<0>()), 2);
                                _ = items35[0];
                                status = items35[1];
                                otherwise34 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler34;
                                if (matches(handler34.exception, B<"ChildProcessError">()))
                                {
                                    // The child process is already reaped
                                    // (may happen if waitpid() is called elsewhere).
                                    returncode = I<255>();
                                    (G<"log.logger">()).method("warning", { S<"child process pid %d exit status already read:  will report returncode 255">(), pid });
                                }
                                else
                                    py_reraise(handler34.exception);
                            }
                            if (otherwise34)
                            {
                                returncode = (G<"unix_events.waitstatus_to_exitcode">())(status);
                            }
                        }
                        (Mod<"os">().attr("close"))(pidfd);
                        call_ex(callback, { pid, returncode }, args, py{});
                        return none();
                    }) },
                { "remove_child_handler", w.method("PidfdChildWatcher", "remove_child_handler", { py_positional("pid") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        return boolean(true);
                    }) }, }));

            define("unix_events", "BaseChildWatcher", w.make_class("BaseChildWatcher", { G<"unix_events.AbstractChildWatcher">() }, {
                { "__init__", w.method("BaseChildWatcher", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_loop", none());
                        self.set_attr("_callbacks", py_dict());
                        return none();
                    }) },
                { "close", w.method("BaseChildWatcher", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("attach_loop", { none() });
                        return none();
                    }) },
                { "is_active", w.method("BaseChildWatcher", "is_active", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = boolean(!self.attr("_loop").is_none()); return !t.truthy() ? t : self.attr("_loop").method("is_running"); }();
                    }) },
                { "_do_waitpid", w.method("BaseChildWatcher", "_do_waitpid", { py_positional("expected_pid") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expected_pid = a[1];
                        py_raise((B<"NotImplementedError">())());
                    }) },
                { "_do_waitpid_all", w.method("BaseChildWatcher", "_do_waitpid_all", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())());
                    }) },
                { "attach_loop", w.method("BaseChildWatcher", "attach_loop", { py_positional("loop") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        if (!([&]() -> py { py const t = boolean(loop.is_none()); return t.truthy() ? t : boolean(isinstance(loop, G<"events.AbstractEventLoop">())); }()).truthy())
                            py_raise(B<"AssertionError">()());
                        if (((!self.attr("_loop").is_none()) && loop.is_none() && self.attr("_callbacks").truthy()))
                        {
                            (Mod<"warnings">().attr("warn"))(S<"A loop is being detached from a child watcher with pending handlers">(), B<"RuntimeWarning">());
                        }
                        if (!self.attr("_loop").is_none())
                        {
                            self.attr("_loop").method("remove_signal_handler", { Mod<"signal">().attr("SIGCHLD") });
                        }
                        self.set_attr("_loop", loop);
                        if (!loop.is_none())
                        {
                            loop.method("add_signal_handler", { Mod<"signal">().attr("SIGCHLD"), self.attr("_sig_chld") });
                            // Prevent a race condition in case a child terminated
                            // during the switch.
                            self.method("_do_waitpid_all");
                        }
                        return none();
                    }) },
                { "_sig_chld", w.method("BaseChildWatcher", "_sig_chld", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc;
                        try
                        {
                            self.method("_do_waitpid_all");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler36;
                            if (matches(handler36.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                            {
                                py_reraise(handler36.exception);
                            }
                            else if (matches(handler36.exception, B<"BaseException">()))
                            {
                                exc = handler36.exception;
                                // self._loop should always be available here
                                // as '_sig_chld' is added as a signal handler
                                // in 'attach_loop'
                                self.attr("_loop").method("call_exception_handler", { dict_of({ { S<"message">(), S<"Unknown exception in SIGCHLD handler">() }, { S<"exception">(), exc } }) });
                            }
                            else
                                py_reraise(handler36.exception);
                        }
                        return none();
                    }) }, }));

            define("unix_events", "SafeChildWatcher", w.make_class("SafeChildWatcher", { G<"unix_events.BaseChildWatcher">() }, {
                { "__init__", w.method("SafeChildWatcher", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (B<"super">()(G<"unix_events.SafeChildWatcher">(), self)).method("__init__");
                        (Mod<"warnings">().attr("_deprecated")).call({ S<"SafeChildWatcher">(), S<"{name!r} is deprecated as of Python 3.12 and will be removed in Python {remove}.">() }, { { "remove", py_tuple({ I<3>(), I<14>() }) } });
                        return none();
                    }) },
                { "close", w.method("SafeChildWatcher", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_callbacks").method("clear");
                        (B<"super">()(G<"unix_events.SafeChildWatcher">(), self)).method("close");
                        return none();
                    }) },
                { "__enter__", w.method("SafeChildWatcher", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("SafeChildWatcher", "__exit__", { py_positional("a"), py_positional("b"), py_positional("c") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_ = a[1];
                        py b = a[2];
                        py c = a[3];
                        return none();
                    }) },
                { "add_child_handler", w.method("SafeChildWatcher", "add_child_handler", { py_positional("pid"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        py callback = a[2];
                        py args = a[3];
                        self.attr("_callbacks").set_item(pid, py_tuple({ callback, args }));
                        // Prevent a race condition in case the child is already terminated.
                        self.method("_do_waitpid", { pid });
                        return none();
                    }, true) },
                { "remove_child_handler", w.method("SafeChildWatcher", "remove_child_handler", { py_positional("pid") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        try
                        {
                            del_item(self.attr("_callbacks"), pid);
                            return boolean(true);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler37;
                            if (matches(handler37.exception, B<"KeyError">()))
                            {
                                return boolean(false);
                            }
                            else
                                py_reraise(handler37.exception);
                        }
                        return none();
                    }) },
                { "_do_waitpid_all", w.method("SafeChildWatcher", "_do_waitpid_all", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid;
                        for (py const& item : iterate{ (B<"list">())(self.attr("_callbacks")) })
                        {
                            pid = item;
                            self.method("_do_waitpid", { pid });
                        }
                        return none();
                    }) },
                { "_do_waitpid", w.method("SafeChildWatcher", "_do_waitpid", { py_positional("expected_pid") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expected_pid = a[1];
                        py args;
                        py callback;
                        py pid;
                        py returncode;
                        py status;
                        if (!(expected_pid > (I<0>())))
                            py_raise(B<"AssertionError">()());
                        {
                            bool otherwise39 = false;
                            try
                            {
                                auto const items40 = unpack((Mod<"os">().attr("waitpid"))(expected_pid, Mod<"os">().attr("WNOHANG")), 2);
                                pid = items40[0];
                                status = items40[1];
                                otherwise39 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler39;
                                if (matches(handler39.exception, B<"ChildProcessError">()))
                                {
                                    // The child process is already reaped
                                    // (may happen if waitpid() is called elsewhere).
                                    pid = expected_pid;
                                    returncode = I<255>();
                                    (G<"log.logger">()).method("warning", { S<"Unknown child process pid %d, will report returncode 255">(), pid });
                                }
                                else
                                    py_reraise(handler39.exception);
                            }
                            if (otherwise39)
                            {
                                if (pid == (I<0>()))
                                {
                                    // The child process is still alive.
                                    return none();
                                }
                                returncode = (G<"unix_events.waitstatus_to_exitcode">())(status);
                                if (self.attr("_loop").method("get_debug").truthy())
                                {
                                    (G<"log.logger">()).method("debug", { S<"process %s exited with returncode %s">(), expected_pid, returncode });
                                }
                            }
                        }
                        {
                            bool otherwise41 = false;
                            try
                            {
                                auto const items42 = unpack(self.attr("_callbacks").method("pop", { pid }), 2);
                                callback = items42[0];
                                args = items42[1];
                                otherwise41 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler41;
                                if (matches(handler41.exception, B<"KeyError">()))
                                {
                                    // May happen if .remove_child_handler() is called
                                    // after os.waitpid() returns.
                                    if (self.attr("_loop").method("get_debug").truthy())
                                    {
                                        (G<"log.logger">()).method("warning", { S<"Child watcher got an unexpected pid: %r">(), pid }, { { "exc_info", boolean(true) } });
                                    }
                                }
                                else
                                    py_reraise(handler41.exception);
                            }
                            if (otherwise41)
                            {
                                call_ex(callback, { pid, returncode }, args, py{});
                            }
                        }
                        return none();
                    }) }, }));

            define("unix_events", "FastChildWatcher", w.make_class("FastChildWatcher", { G<"unix_events.BaseChildWatcher">() }, {
                { "__init__", w.method("FastChildWatcher", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (B<"super">()(G<"unix_events.FastChildWatcher">(), self)).method("__init__");
                        self.set_attr("_lock", (Mod<"threading">().attr("Lock"))());
                        self.set_attr("_zombies", py_dict());
                        self.set_attr("_forks", I<0>());
                        (Mod<"warnings">().attr("_deprecated")).call({ S<"FastChildWatcher">(), S<"{name!r} is deprecated as of Python 3.12 and will be removed in Python {remove}.">() }, { { "remove", py_tuple({ I<3>(), I<14>() }) } });
                        return none();
                    }) },
                { "close", w.method("FastChildWatcher", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_callbacks").method("clear");
                        self.attr("_zombies").method("clear");
                        (B<"super">()(G<"unix_events.FastChildWatcher">(), self)).method("close");
                        return none();
                    }) },
                { "__enter__", w.method("FastChildWatcher", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            py const manager43 = self.attr("_lock");
                            py const exit43 = pyclass::special_method(manager43, "__exit__");
                            py const value43 = pyclass::special_method(manager43, "__enter__")(manager43);
                            [[maybe_unused]] int flow43 = 0;
                            [[maybe_unused]] py result43;
                            bool exited43 = false;
                            try
                            {
                                {
                                    py const target44 = self;
                                    target44.set_attr("_forks", inplace(NEOS_PY_ADD, target44.attr("_forks"), I<1>()));
                                }
                                result43 = self;
                                flow43 = 1;
                                goto finally43;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler43;
                                exited43 = true;
                                if (!exit43(manager43, type_of(handler43.exception), handler43.exception, handler43.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler43.exception);
                            }
                            finally43:
                            if (!exited43)
                                exit43(manager43, none(), none(), none());
                            if (flow43 == 1)
                                return result43;
                        }
                        return none();
                    }) },
                { "__exit__", w.method("FastChildWatcher", "__exit__", { py_positional("a"), py_positional("b"), py_positional("c") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_ = a[1];
                        py b = a[2];
                        py c = a[3];
                        py collateral_victims;
                        {
                            py const manager45 = self.attr("_lock");
                            py const exit45 = pyclass::special_method(manager45, "__exit__");
                            py const value45 = pyclass::special_method(manager45, "__enter__")(manager45);
                            [[maybe_unused]] int flow45 = 0;
                            [[maybe_unused]] py result45;
                            bool exited45 = false;
                            try
                            {
                                {
                                    py const target46 = self;
                                    target46.set_attr("_forks", inplace(NEOS_PY_SUBTRACT, target46.attr("_forks"), I<1>()));
                                }
                                if ((self.attr("_forks").truthy() || (!self.attr("_zombies").truthy())))
                                {
                                    result45 = none();
                                    flow45 = 1;
                                    goto finally45;
                                }
                                collateral_victims = (B<"str">())(self.attr("_zombies"));
                                self.attr("_zombies").method("clear");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler45;
                                exited45 = true;
                                if (!exit45(manager45, type_of(handler45.exception), handler45.exception, handler45.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler45.exception);
                            }
                            finally45:
                            if (!exited45)
                                exit45(manager45, none(), none(), none());
                            if (flow45 == 1)
                                return result45;
                        }
                        (G<"log.logger">()).method("warning", { S<"Caught subprocesses termination from unknown pids: %s">(), collateral_victims });
                        return none();
                    }) },
                { "add_child_handler", w.method("FastChildWatcher", "add_child_handler", { py_positional("pid"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        py callback = a[2];
                        py args = a[3];
                        py returncode;
                        if (!self.attr("_forks").truthy())
                            py_raise(B<"AssertionError">()(S<"Must use the context manager">()));
                        {
                            py const manager47 = self.attr("_lock");
                            py const exit47 = pyclass::special_method(manager47, "__exit__");
                            py const value47 = pyclass::special_method(manager47, "__enter__")(manager47);
                            [[maybe_unused]] int flow47 = 0;
                            [[maybe_unused]] py result47;
                            bool exited47 = false;
                            try
                            {
                                try
                                {
                                    returncode = self.attr("_zombies").method("pop", { pid });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler48;
                                    if (matches(handler48.exception, B<"KeyError">()))
                                    {
                                        // The child is running.
                                        self.attr("_callbacks").set_item(pid, py_tuple({ callback, args }));
                                        result47 = none();
                                        flow47 = 1;
                                        goto finally47;
                                    }
                                    else
                                        py_reraise(handler48.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler47;
                                exited47 = true;
                                if (!exit47(manager47, type_of(handler47.exception), handler47.exception, handler47.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler47.exception);
                            }
                            finally47:
                            if (!exited47)
                                exit47(manager47, none(), none(), none());
                            if (flow47 == 1)
                                return result47;
                        }
                        // The child is dead already. We can fire the callback.
                        call_ex(callback, { pid, returncode }, args, py{});
                        return none();
                    }, true) },
                { "remove_child_handler", w.method("FastChildWatcher", "remove_child_handler", { py_positional("pid") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        try
                        {
                            del_item(self.attr("_callbacks"), pid);
                            return boolean(true);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler49;
                            if (matches(handler49.exception, B<"KeyError">()))
                            {
                                return boolean(false);
                            }
                            else
                                py_reraise(handler49.exception);
                        }
                        return none();
                    }) },
                { "_do_waitpid_all", w.method("FastChildWatcher", "_do_waitpid_all", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args;
                        py callback;
                        py pid;
                        py returncode;
                        py status;
                        while (boolean(true).truthy())
                        {
                            {
                                bool otherwise51 = false;
                                try
                                {
                                    auto const items52 = unpack((Mod<"os">().attr("waitpid"))(I<-1>(), Mod<"os">().attr("WNOHANG")), 2);
                                    pid = items52[0];
                                    status = items52[1];
                                    otherwise51 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler51;
                                    if (matches(handler51.exception, B<"ChildProcessError">()))
                                    {
                                        // No more child processes exist.
                                        return none();
                                    }
                                    else
                                        py_reraise(handler51.exception);
                                }
                                if (otherwise51)
                                {
                                    if (pid == (I<0>()))
                                    {
                                        // A child process is still alive.
                                        return none();
                                    }
                                    returncode = (G<"unix_events.waitstatus_to_exitcode">())(status);
                                }
                            }
                            {
                                py const manager53 = self.attr("_lock");
                                py const exit53 = pyclass::special_method(manager53, "__exit__");
                                py const value53 = pyclass::special_method(manager53, "__enter__")(manager53);
                                [[maybe_unused]] int flow53 = 0;
                                [[maybe_unused]] py result53;
                                bool exited53 = false;
                                try
                                {
                                    {
                                        bool otherwise54 = false;
                                        try
                                        {
                                            auto const items55 = unpack(self.attr("_callbacks").method("pop", { pid }), 2);
                                            callback = items55[0];
                                            args = items55[1];
                                            otherwise54 = true;
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler54;
                                            if (matches(handler54.exception, B<"KeyError">()))
                                            {
                                                // unknown child
                                                if (self.attr("_forks").truthy())
                                                {
                                                    // It may not be registered yet.
                                                    self.attr("_zombies").set_item(pid, returncode);
                                                    if (self.attr("_loop").method("get_debug").truthy())
                                                    {
                                                        (G<"log.logger">()).method("debug", { S<"unknown process %s exited with returncode %s">(), pid, returncode });
                                                    }
                                                    flow53 = 3;
                                                    goto finally53;
                                                }
                                                callback = none();
                                            }
                                            else
                                                py_reraise(handler54.exception);
                                        }
                                        if (otherwise54)
                                        {
                                            if (self.attr("_loop").method("get_debug").truthy())
                                            {
                                                (G<"log.logger">()).method("debug", { S<"process %s exited with returncode %s">(), pid, returncode });
                                            }
                                        }
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler53;
                                    exited53 = true;
                                    if (!exit53(manager53, type_of(handler53.exception), handler53.exception, handler53.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler53.exception);
                                }
                                finally53:
                                if (!exited53)
                                    exit53(manager53, none(), none(), none());
                                if (flow53 == 1)
                                    return result53;
                                if (flow53 == 2)
                                    break;
                                if (flow53 == 3)
                                    continue;
                            }
                            if (callback.is_none())
                            {
                                (G<"log.logger">()).method("warning", { S<"Caught subprocess termination from unknown pid: %d -> %d">(), pid, returncode });
                            }
                            else
                            {
                                call_ex(callback, { pid, returncode }, args, py{});
                            }
                        }
                        return none();
                    }) }, }));

                // Implementation note:
                // The class keeps compatibility with AbstractChildWatcher ABC
                // To achieve this it has empty attach_loop() method
                // and doesn't accept explicit loop argument
                // for add_child_handler()/remove_child_handler()
                // but retrieves the current loop by get_running_loop()
            define("unix_events", "MultiLoopChildWatcher", w.make_class("MultiLoopChildWatcher", { G<"unix_events.AbstractChildWatcher">() }, {
                { "__init__", w.method("MultiLoopChildWatcher", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_callbacks", py_dict());
                        self.set_attr("_saved_sighandler", none());
                        (Mod<"warnings">().attr("_deprecated")).call({ S<"MultiLoopChildWatcher">(), S<"{name!r} is deprecated as of Python 3.12 and will be removed in Python {remove}.">() }, { { "remove", py_tuple({ I<3>(), I<14>() }) } });
                        return none();
                    }) },
                { "is_active", w.method("MultiLoopChildWatcher", "is_active", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(!self.attr("_saved_sighandler").is_none());
                    }) },
                { "close", w.method("MultiLoopChildWatcher", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handler;
                        self.attr("_callbacks").method("clear");
                        if (self.attr("_saved_sighandler").is_none())
                        {
                            return none();
                        }
                        handler = (Mod<"signal">().attr("getsignal"))(Mod<"signal">().attr("SIGCHLD"));
                        if (handler != self.attr("_sig_chld"))
                        {
                            (G<"log.logger">()).method("warning", { S<"SIGCHLD handler was changed by outside code">() });
                        }
                        else
                        {
                            (Mod<"signal">().attr("signal"))(Mod<"signal">().attr("SIGCHLD"), self.attr("_saved_sighandler"));
                        }
                        self.set_attr("_saved_sighandler", none());
                        return none();
                    }) },
                { "__enter__", w.method("MultiLoopChildWatcher", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("MultiLoopChildWatcher", "__exit__", { py_positional("exc_type"), py_positional("exc_val"), py_positional("exc_tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_type = a[1];
                        py exc_val = a[2];
                        py exc_tb = a[3];
                        return none();
                    }) },
                { "add_child_handler", w.method("MultiLoopChildWatcher", "add_child_handler", { py_positional("pid"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        py callback = a[2];
                        py args = a[3];
                        py loop;
                        loop = (G<"events.get_running_loop">())();
                        self.attr("_callbacks").set_item(pid, py_tuple({ loop, callback, args }));
                        // Prevent a race condition in case the child is already terminated.
                        self.method("_do_waitpid", { pid });
                        return none();
                    }, true) },
                { "remove_child_handler", w.method("MultiLoopChildWatcher", "remove_child_handler", { py_positional("pid") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        try
                        {
                            del_item(self.attr("_callbacks"), pid);
                            return boolean(true);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler56;
                            if (matches(handler56.exception, B<"KeyError">()))
                            {
                                return boolean(false);
                            }
                            else
                                py_reraise(handler56.exception);
                        }
                        return none();
                    }) },
                { "attach_loop", w.method("MultiLoopChildWatcher", "attach_loop", { py_positional("loop") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        if (!self.attr("_saved_sighandler").is_none())
                        {
                            return none();
                        }
                        self.set_attr("_saved_sighandler", (Mod<"signal">().attr("signal"))(Mod<"signal">().attr("SIGCHLD"), self.attr("_sig_chld")));
                        if (self.attr("_saved_sighandler").is_none())
                        {
                            (G<"log.logger">()).method("warning", { S<"Previous SIGCHLD handler was set by non-Python code, restore to default handler on watcher close.">() });
                            self.set_attr("_saved_sighandler", Mod<"signal">().attr("SIG_DFL"));
                        }
                        // Set SA_RESTART to limit EINTR occurrences.
                        (Mod<"signal">().attr("siginterrupt"))(Mod<"signal">().attr("SIGCHLD"), boolean(false));
                        return none();
                    }) },
                { "_do_waitpid_all", w.method("MultiLoopChildWatcher", "_do_waitpid_all", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid;
                        for (py const& item : iterate{ (B<"list">())(self.attr("_callbacks")) })
                        {
                            pid = item;
                            self.method("_do_waitpid", { pid });
                        }
                        return none();
                    }) },
                { "_do_waitpid", w.method("MultiLoopChildWatcher", "_do_waitpid", { py_positional("expected_pid") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expected_pid = a[1];
                        py args;
                        py callback;
                        py debug_log;
                        py loop;
                        py pid;
                        py returncode;
                        py status;
                        if (!(expected_pid > (I<0>())))
                            py_raise(B<"AssertionError">()());
                        {
                            bool otherwise58 = false;
                            try
                            {
                                auto const items59 = unpack((Mod<"os">().attr("waitpid"))(expected_pid, Mod<"os">().attr("WNOHANG")), 2);
                                pid = items59[0];
                                status = items59[1];
                                otherwise58 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler58;
                                if (matches(handler58.exception, B<"ChildProcessError">()))
                                {
                                    // The child process is already reaped
                                    // (may happen if waitpid() is called elsewhere).
                                    pid = expected_pid;
                                    returncode = I<255>();
                                    (G<"log.logger">()).method("warning", { S<"Unknown child process pid %d, will report returncode 255">(), pid });
                                    debug_log = boolean(false);
                                }
                                else
                                    py_reraise(handler58.exception);
                            }
                            if (otherwise58)
                            {
                                if (pid == (I<0>()))
                                {
                                    // The child process is still alive.
                                    return none();
                                }
                                returncode = (G<"unix_events.waitstatus_to_exitcode">())(status);
                                debug_log = boolean(true);
                            }
                        }
                        {
                            bool otherwise60 = false;
                            try
                            {
                                auto const items61 = unpack(self.attr("_callbacks").method("pop", { pid }), 3);
                                loop = items61[0];
                                callback = items61[1];
                                args = items61[2];
                                otherwise60 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler60;
                                if (matches(handler60.exception, B<"KeyError">()))
                                {
                                    // May happen if .remove_child_handler() is called
                                    // after os.waitpid() returns.
                                    (G<"log.logger">()).method("warning", { S<"Child watcher got an unexpected pid: %r">(), pid }, { { "exc_info", boolean(true) } });
                                }
                                else
                                    py_reraise(handler60.exception);
                            }
                            if (otherwise60)
                            {
                                if (loop.method("is_closed").truthy())
                                {
                                    (G<"log.logger">()).method("warning", { S<"Loop %r that handles pid %r is closed">(), loop, pid });
                                }
                                else
                                {
                                    if ((debug_log.truthy() && loop.method("get_debug").truthy()))
                                    {
                                        (G<"log.logger">()).method("debug", { S<"process %s exited with returncode %s">(), expected_pid, returncode });
                                    }
                                    call_ex(loop.attr("call_soon_threadsafe"), { callback, pid, returncode }, args, py{});
                                }
                            }
                        }
                        return none();
                    }) },
                { "_sig_chld", w.method("MultiLoopChildWatcher", "_sig_chld", { py_positional("signum"), py_positional("frame") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py signum = a[1];
                        py frame = a[2];
                        try
                        {
                            self.method("_do_waitpid_all");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler62;
                            if (matches(handler62.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                            {
                                py_reraise(handler62.exception);
                            }
                            else if (matches(handler62.exception, B<"BaseException">()))
                            {
                                (G<"log.logger">()).method("warning", { S<"Unknown exception in SIGCHLD handler">() }, { { "exc_info", boolean(true) } });
                            }
                            else
                                py_reraise(handler62.exception);
                        }
                        return none();
                    }) }, }));

            define("unix_events", "ThreadedChildWatcher", w.make_class("ThreadedChildWatcher", { G<"unix_events.AbstractChildWatcher">() }, {
                { "__init__", w.method("ThreadedChildWatcher", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_pid_counter", (Mod<"itertools">().attr("count"))(I<0>()));
                        self.set_attr("_threads", py_dict());
                        return none();
                    }) },
                { "is_active", w.method("ThreadedChildWatcher", "is_active", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) },
                { "close", w.method("ThreadedChildWatcher", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "__enter__", w.method("ThreadedChildWatcher", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("ThreadedChildWatcher", "__exit__", { py_positional("exc_type"), py_positional("exc_val"), py_positional("exc_tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_type = a[1];
                        py exc_val = a[2];
                        py exc_tb = a[3];
                        return none();
                    }) },
                { "__del__", w.method("ThreadedChildWatcher", "__del__", { py_positional("_warn", Mod<"warnings">().attr("warn")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _warn = a[1];
                        py threads;
                        threads = [&]() -> py { py const result = py_list(); py thread; for (py const& item : iterate{ (B<"list">())(self.attr("_threads").method("values")) }) { thread = item; if (!thread.method("is_alive").truthy()) continue; result.method("append", { thread }); } return result; }();
                        if (threads.truthy())
                        {
                            _warn.call({ S(self.attr("__class__").str() + " has registered but not finished child processes"), B<"ResourceWarning">() }, { { "source", self } });
                        }
                        return none();
                    }) },
                { "add_child_handler", w.method("ThreadedChildWatcher", "add_child_handler", { py_positional("pid"), py_positional("callback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        py callback = a[2];
                        py args = a[3];
                        py loop;
                        py thread;
                        loop = (G<"events.get_running_loop">())();
                        thread = (Mod<"threading">().attr("Thread")).call({  }, { { "target", self.attr("_do_waitpid") }, { "name", S(std::string{ "asyncio-waitpid-" } + (B<"next">())(self.attr("_pid_counter")).str()) }, { "args", py_tuple({ loop, pid, callback, args }) }, { "daemon", boolean(true) } });
                        self.attr("_threads").set_item(pid, thread);
                        thread.method("start");
                        return none();
                    }, true) },
                { "remove_child_handler", w.method("ThreadedChildWatcher", "remove_child_handler", { py_positional("pid") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pid = a[1];
                        return boolean(true);
                    }) },
                { "attach_loop", w.method("ThreadedChildWatcher", "attach_loop", { py_positional("loop") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        return none();
                    }) },
                { "_do_waitpid", w.method("ThreadedChildWatcher", "_do_waitpid", { py_positional("loop"), py_positional("expected_pid"), py_positional("callback"), py_positional("args") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        py expected_pid = a[2];
                        py callback = a[3];
                        py args = a[4];
                        py pid;
                        py returncode;
                        py status;
                        if (!(expected_pid > (I<0>())))
                            py_raise(B<"AssertionError">()());
                        {
                            bool otherwise63 = false;
                            try
                            {
                                auto const items64 = unpack((Mod<"os">().attr("waitpid"))(expected_pid, I<0>()), 2);
                                pid = items64[0];
                                status = items64[1];
                                otherwise63 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler63;
                                if (matches(handler63.exception, B<"ChildProcessError">()))
                                {
                                    // The child process is already reaped
                                    // (may happen if waitpid() is called elsewhere).
                                    pid = expected_pid;
                                    returncode = I<255>();
                                    (G<"log.logger">()).method("warning", { S<"Unknown child process pid %d, will report returncode 255">(), pid });
                                }
                                else
                                    py_reraise(handler63.exception);
                            }
                            if (otherwise63)
                            {
                                returncode = (G<"unix_events.waitstatus_to_exitcode">())(status);
                                if (loop.method("get_debug").truthy())
                                {
                                    (G<"log.logger">()).method("debug", { S<"process %s exited with returncode %s">(), expected_pid, returncode });
                                }
                            }
                        }
                        if (loop.method("is_closed").truthy())
                        {
                            (G<"log.logger">()).method("warning", { S<"Loop %r that handles pid %r is closed">(), loop, pid });
                        }
                        else
                        {
                            call_ex(loop.attr("call_soon_threadsafe"), { callback, pid, returncode }, args, py{});
                        }
                        self.attr("_threads").method("pop", { expected_pid });
                        return none();
                    }) }, }));

            define("unix_events", "can_use_pidfd", w.function("can_use_pidfd", "can_use_pidfd", {  }, [](arguments a) -> py
                {
                    py pid;
                    if (!has_attr(Mod<"os">(), "pidfd_open"))
                    {
                        return boolean(false);
                    }
                    try
                    {
                        pid = (Mod<"os">().attr("getpid"))();
                        (Mod<"os">().attr("close"))((Mod<"os">().attr("pidfd_open"))(pid, I<0>()));
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler65;
                        if (matches(handler65.exception, B<"OSError">()))
                        {
                            // blocked by security policy like SECCOMP
                            return boolean(false);
                        }
                        else
                            py_reraise(handler65.exception);
                    }
                    return boolean(true);
                }));

            define("unix_events", "_UnixDefaultEventLoopPolicy", w.make_class("_UnixDefaultEventLoopPolicy", { G<"events.BaseDefaultEventLoopPolicy">() }, {
                { "_loop_factory", G<"unix_events._UnixSelectorEventLoop">() },
                { "__init__", w.method("_UnixDefaultEventLoopPolicy", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (B<"super">()(G<"unix_events._UnixDefaultEventLoopPolicy">(), self)).method("__init__");
                        self.set_attr("_watcher", none());
                        return none();
                    }) },
                { "_init_watcher", w.method("_UnixDefaultEventLoopPolicy", "_init_watcher", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            py const manager66 = G<"events._lock">();
                            py const exit66 = pyclass::special_method(manager66, "__exit__");
                            py const value66 = pyclass::special_method(manager66, "__enter__")(manager66);
                            [[maybe_unused]] int flow66 = 0;
                            [[maybe_unused]] py result66;
                            bool exited66 = false;
                            try
                            {
                                if (self.attr("_watcher").is_none())
                                {
                                    if ((G<"unix_events.can_use_pidfd">())().truthy())
                                    {
                                        self.set_attr("_watcher", (G<"unix_events.PidfdChildWatcher">())());
                                    }
                                    else
                                    {
                                        self.set_attr("_watcher", (G<"unix_events.ThreadedChildWatcher">())());
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler66;
                                exited66 = true;
                                if (!exit66(manager66, type_of(handler66.exception), handler66.exception, handler66.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler66.exception);
                            }
                            if (!exited66)
                                exit66(manager66, none(), none(), none());
                            if (flow66 == 1)
                                return result66;
                        }
                        return none();
                    }) },
                { "set_event_loop", w.method("_UnixDefaultEventLoopPolicy", "set_event_loop", { py_positional("loop") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        (B<"super">()(G<"unix_events._UnixDefaultEventLoopPolicy">(), self)).method("set_event_loop", { loop });
                        if (((!self.attr("_watcher").is_none()) && (Mod<"threading">().attr("current_thread"))().is((Mod<"threading">().attr("main_thread"))())))
                        {
                            self.attr("_watcher").method("attach_loop", { loop });
                        }
                        return none();
                    }) },
                { "get_child_watcher", w.method("_UnixDefaultEventLoopPolicy", "get_child_watcher", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_watcher").is_none())
                        {
                            self.method("_init_watcher");
                        }
                        (Mod<"warnings">().attr("_deprecated")).call({ S<"get_child_watcher">(), S<"{name!r} is deprecated as of Python 3.12 and will be removed in Python {remove}.">() }, { { "remove", py_tuple({ I<3>(), I<14>() }) } });
                        return self.attr("_watcher");
                    }) },
                { "set_child_watcher", w.method("_UnixDefaultEventLoopPolicy", "set_child_watcher", { py_positional("watcher") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py watcher = a[1];
                        if (!([&]() -> py { py const t = boolean(watcher.is_none()); return t.truthy() ? t : boolean(isinstance(watcher, G<"unix_events.AbstractChildWatcher">())); }()).truthy())
                            py_raise(B<"AssertionError">()());
                        if (!self.attr("_watcher").is_none())
                        {
                            self.attr("_watcher").method("close");
                        }
                        self.set_attr("_watcher", watcher);
                        (Mod<"warnings">().attr("_deprecated")).call({ S<"set_child_watcher">(), S<"{name!r} is deprecated as of Python 3.12 and will be removed in Python {remove}.">() }, { { "remove", py_tuple({ I<3>(), I<14>() }) } });
                        return none();
                    }) }, }));

            define("unix_events", "SelectorEventLoop", G<"unix_events._UnixSelectorEventLoop">());
            define("unix_events", "DefaultEventLoopPolicy", G<"unix_events._UnixDefaultEventLoopPolicy">());
            define("unix_events", "EventLoop", G<"unix_events.SelectorEventLoop">());
        }

        // --- runners (Lib/asyncio/runners.py)

        void make_runners()
        {
            static module_writer const& w = writer("runners");
            define("runners", "__all__", py_tuple({ S<"Runner">(), S<"run">() }));
            define("runners", "_State", w.make_class("_State", { Mod<"enum">().attr("Enum") }, {
                { "CREATED", S<"created">() },
                { "INITIALIZED", S<"initialized">() },
                { "CLOSED", S<"closed">() }, }));

                // Note: the class is final, it is not intended for inheritance.
            define("runners", "Runner", w.make_class("Runner", {  }, {
                { "__init__", w.method("Runner", "__init__", { py_keyword("debug", none()), py_keyword("loop_factory", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py debug = a[1];
                        py loop_factory = a[2];
                        self.set_attr("_state", (G<"runners._State">()).attr("CREATED"));
                        self.set_attr("_debug", debug);
                        self.set_attr("_loop_factory", loop_factory);
                        self.set_attr("_loop", none());
                        self.set_attr("_context", none());
                        self.set_attr("_interrupt_count", I<0>());
                        self.set_attr("_set_event_loop", boolean(false));
                        return none();
                    }) },
                { "__enter__", w.method("Runner", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_lazy_init");
                        return self;
                    }) },
                { "__exit__", w.method("Runner", "__exit__", { py_positional("exc_type"), py_positional("exc_val"), py_positional("exc_tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_type = a[1];
                        py exc_val = a[2];
                        py exc_tb = a[3];
                        self.method("close");
                        return none();
                    }) },
                { "close", w.method("Runner", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop;
                        if (!self.attr("_state").is((G<"runners._State">()).attr("INITIALIZED")))
                        {
                            return none();
                        }
                        {
                            std::optional<py> pending1;
                            [[maybe_unused]] int flow1 = 0;
                            [[maybe_unused]] py result1;
                            try
                            {
                                loop = self.attr("_loop");
                                (G<"runners._cancel_all_tasks">())(loop);
                                loop.method("run_until_complete", { loop.method("shutdown_asyncgens") });
                                loop.method("run_until_complete", { loop.method("shutdown_default_executor", { G<"constants.THREAD_JOIN_TIMEOUT">() }) });
                            }
                            catch (raised const&)
                            {
                                pending1 = py_take_exception();
                            }
                            if (self.attr("_set_event_loop").truthy())
                            {
                                (G<"events.set_event_loop">())(none());
                            }
                            loop.method("close");
                            self.set_attr("_loop", none());
                            self.set_attr("_state", (G<"runners._State">()).attr("CLOSED"));
                            if (pending1)
                                py_reraise(*pending1);
                            if (flow1 == 1)
                                return result1;
                        }
                        return none();
                    }) },
                { "get_loop", w.method("Runner", "get_loop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_lazy_init");
                        return self.attr("_loop");
                    }) },
                { "run", w.method("Runner", "run", { py_positional("coro"), py_keyword("context", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py coro = a[1];
                        py context = a[2];
                        py sigint_handler;
                        py task;
                        py uncancel;
                        if (!(G<"coroutines.iscoroutine">())(coro).truthy())
                        {
                            py_raise((B<"ValueError">())((S<"a coroutine was expected, got {!r}">()).method("format", { coro })));
                        }
                        if (!(G<"events._get_running_loop">())().is_none())
                        {
                            // fail fast with short traceback
                            py_raise((B<"RuntimeError">())(S<"Runner.run() cannot be called from a running event loop">()));
                        }
                        self.method("_lazy_init");
                        if (context.is_none())
                        {
                            context = self.attr("_context");
                        }
                        task = self.attr("_loop").method("create_task", { coro }, { { "context", context } });
                        if (((Mod<"threading">().attr("current_thread"))().is((Mod<"threading">().attr("main_thread"))()) && (Mod<"signal">().attr("getsignal"))(Mod<"signal">().attr("SIGINT")).is((Mod<"signal">().attr("default_int_handler")))))
                        {
                            sigint_handler = (Mod<"functools">().attr("partial")).call({ self.attr("_on_sigint") }, { { "main_task", task } });
                            try
                            {
                                (Mod<"signal">().attr("signal"))(Mod<"signal">().attr("SIGINT"), sigint_handler);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler2;
                                if (matches(handler2.exception, B<"ValueError">()))
                                {
                                    // `signal.signal` may throw if `threading.main_thread` does
                                    // not support signals (e.g. embedded interpreter with signals
                                    // not registered - see gh-91880)
                                    sigint_handler = none();
                                }
                                else
                                    py_reraise(handler2.exception);
                            }
                        }
                        else
                        {
                            sigint_handler = none();
                        }
                        self.set_attr("_interrupt_count", I<0>());
                        {
                            std::optional<py> pending3;
                            [[maybe_unused]] int flow3 = 0;
                            [[maybe_unused]] py result3;
                            try
                            {
                                try
                                {
                                    result3 = self.attr("_loop").method("run_until_complete", { task });
                                    flow3 = 1;
                                    goto finally3;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler3;
                                    if (matches(handler3.exception, G<"exceptions.CancelledError">()))
                                    {
                                        if (self.attr("_interrupt_count") > (I<0>()))
                                        {
                                            uncancel = (B<"getattr">())(task, S<"uncancel">(), none());
                                            if (((!uncancel.is_none()) && (uncancel() == (I<0>()))))
                                            {
                                                py_raise((B<"KeyboardInterrupt">())());
                                            }
                                        }
                                        py_reraise(handler3.exception);
                                    }
                                    else
                                        py_reraise(handler3.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pending3 = py_take_exception();
                            }
                            finally3:
                            if (((!sigint_handler.is_none()) && (Mod<"signal">().attr("getsignal"))(Mod<"signal">().attr("SIGINT")).is(sigint_handler)))
                            {
                                (Mod<"signal">().attr("signal"))(Mod<"signal">().attr("SIGINT"), Mod<"signal">().attr("default_int_handler"));
                            }
                            if (pending3)
                                py_reraise(*pending3);
                            if (flow3 == 1)
                                return result3;
                        }
                        return none();
                    }) },
                { "_lazy_init", w.method("Runner", "_lazy_init", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_state").is((G<"runners._State">()).attr("CLOSED")))
                        {
                            py_raise((B<"RuntimeError">())(S<"Runner is closed">()));
                        }
                        if (self.attr("_state").is((G<"runners._State">()).attr("INITIALIZED")))
                        {
                            return none();
                        }
                        if (self.attr("_loop_factory").is_none())
                        {
                            self.set_attr("_loop", (G<"events.new_event_loop">())());
                            if (!self.attr("_set_event_loop").truthy())
                            {
                                // Call set_event_loop only once to avoid calling
                                // attach_loop multiple times on child watchers
                                (G<"events.set_event_loop">())(self.attr("_loop"));
                                self.set_attr("_set_event_loop", boolean(true));
                            }
                        }
                        else
                        {
                            self.set_attr("_loop", self.method("_loop_factory"));
                        }
                        if (!self.attr("_debug").is_none())
                        {
                            self.attr("_loop").method("set_debug", { self.attr("_debug") });
                        }
                        self.set_attr("_context", (Mod<"contextvars">().attr("copy_context"))());
                        self.set_attr("_state", (G<"runners._State">()).attr("INITIALIZED"));
                        return none();
                    }) },
                { "_on_sigint", w.method("Runner", "_on_sigint", { py_positional("signum"), py_positional("frame"), py_positional("main_task") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py signum = a[1];
                        py frame = a[2];
                        py main_task = a[3];
                        {
                            py const target4 = self;
                            target4.set_attr("_interrupt_count", inplace(NEOS_PY_ADD, target4.attr("_interrupt_count"), I<1>()));
                        }
                        if (((self.attr("_interrupt_count") == (I<1>())) && (!main_task.method("done").truthy())))
                        {
                            main_task.method("cancel");
                            // wakeup loop if it is blocked by select() with long timeout
                            self.attr("_loop").method("call_soon_threadsafe", { w.function("<lambda>", "Runner._on_sigint.<locals>.<lambda>", {  }, [](arguments a) -> py
        {
            return none();
        }) });
                            return none();
                        }
                        py_raise((B<"KeyboardInterrupt">())());
                    }) }, }));

            define("runners", "run", w.function("run", "run", { py_positional("main"), py_keyword("debug", none()), py_keyword("loop_factory", none()) }, [](arguments a) -> py
                {
                    py main = a[0];
                    py debug = a[1];
                    py loop_factory = a[2];
                    py runner;
                    if (!(G<"events._get_running_loop">())().is_none())
                    {
                        // fail fast with short traceback
                        py_raise((B<"RuntimeError">())(S<"asyncio.run() cannot be called from a running event loop">()));
                    }
                    {
                        py const manager5 = (G<"runners.Runner">()).call({  }, { { "debug", debug }, { "loop_factory", loop_factory } });
                        py const exit5 = pyclass::special_method(manager5, "__exit__");
                        py const value5 = pyclass::special_method(manager5, "__enter__")(manager5);
                        runner = value5;
                        [[maybe_unused]] int flow5 = 0;
                        [[maybe_unused]] py result5;
                        bool exited5 = false;
                        try
                        {
                            result5 = runner.method("run", { main });
                            flow5 = 1;
                            goto finally5;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler5;
                            exited5 = true;
                            if (!exit5(manager5, type_of(handler5.exception), handler5.exception, handler5.exception.attr("__traceback__")).truthy())
                                py_reraise(handler5.exception);
                        }
                        finally5:
                        if (!exited5)
                            exit5(manager5, none(), none(), none());
                        if (flow5 == 1)
                            return result5;
                    }
                    return none();
                }));

            define("runners", "_cancel_all_tasks", w.function("_cancel_all_tasks", "_cancel_all_tasks", { py_positional("loop") }, [](arguments a) -> py
                {
                    py loop = a[0];
                    py task;
                    py to_cancel;
                    to_cancel = (G<"tasks.all_tasks">())(loop);
                    if (!to_cancel.truthy())
                    {
                        return none();
                    }
                    for (py const& item : iterate{ to_cancel })
                    {
                        task = item;
                        task.method("cancel");
                    }
                    loop.method("run_until_complete", { call_ex((G<"tasks.gather">()), {  }, to_cancel, py{}, { { "return_exceptions", boolean(true) } }) });
                    for (py const& item : iterate{ to_cancel })
                    {
                        task = item;
                        if (task.method("cancelled").truthy())
                        {
                            continue;
                        }
                        if (!task.method("exception").is_none())
                        {
                            loop.method("call_exception_handler", { dict_of({ { S<"message">(), S<"unhandled exception during asyncio.run() shutdown">() }, { S<"exception">(), task.method("exception") }, { S<"task">(), task } }) });
                        }
                    }
                    return none();
                }));
        }

        // --- threads (Lib/asyncio/threads.py)

        void make_threads()
        {
            static module_writer const& w = writer("threads");
            define("threads", "__all__", py_tuple({ S<"to_thread">() }));
            define("threads", "to_thread", async_function(w, "to_thread", { py_positional_only("func") }, [](arguments a) -> py
                {
                    py func = a[0];
                    py args = a[1];
                    py kwargs = a[2];
                    py ctx;
                    py func_call;
                    py loop;
                    loop = (G<"events.get_running_loop">())();
                    ctx = (Mod<"contextvars">().attr("copy_context"))();
                    func_call = call_ex((Mod<"functools">().attr("partial")), { ctx.attr("run"), func }, args, kwargs);
                    return py_await(loop.method("run_in_executor", { none(), func_call }));
                }, true, true));
        }
    }

    void make_loop()
    {
        make_protocols();
        make_transports();
        make_trsock();
        make_base_events();
        make_selector_events();
        make_base_subprocess();
#if !defined(_WIN32)
        make_unix_events();
#endif
        make_runners();
        make_threads();
    }
}
