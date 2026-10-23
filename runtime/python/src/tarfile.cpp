/*
  tarfile.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/tarfile.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/tarfile.py's copyright: Copyright (C) 2002 Lars Gustaebel
  <lars@gustaebel.de>. All rights reserved. (MIT license: see runtime/python/NOTICE).
*/

// The module tarfile (CPython 3.13's Lib/tarfile.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "tarfile_docs.hpp"

namespace neos::python_runtime::detail::tarfile_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "tarfile.name"))
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
                py_raise("SystemError", std::string{ "tarfile: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"tarfile.name">()))
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
            if (modules_imported("tarfile"))
                return py{ fn_import("tarfile") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "tarfile" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "tarfile", tarfile_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- tarfile (Lib/tarfile.py)

        void make_tarfile()
        {
            static module_writer const& w = writer("tarfile");
            // !/usr/bin/env python3
            // -------------------------------------------------------------------
            // tarfile.py
            // -------------------------------------------------------------------
            // Copyright (C) 2002 Lars Gustaebel <lars@gustaebel.de>
            // All rights reserved.
            // 
            // Permission  is  hereby granted,  free  of charge,  to  any person
            // obtaining a  copy of  this software  and associated documentation
            // files  (the  "Software"),  to   deal  in  the  Software   without
            // restriction,  including  without limitation  the  rights to  use,
            // copy, modify, merge, publish, distribute, sublicense, and/or sell
            // copies  of  the  Software,  and to  permit  persons  to  whom the
            // Software  is  furnished  to  do  so,  subject  to  the  following
            // conditions:
            // 
            // The above copyright  notice and this  permission notice shall  be
            // included in all copies or substantial portions of the Software.
            // 
            // THE SOFTWARE IS PROVIDED "AS  IS", WITHOUT WARRANTY OF ANY  KIND,
            // EXPRESS OR IMPLIED, INCLUDING  BUT NOT LIMITED TO  THE WARRANTIES
            // OF  MERCHANTABILITY,  FITNESS   FOR  A  PARTICULAR   PURPOSE  AND
            // NONINFRINGEMENT.  IN  NO  EVENT SHALL  THE  AUTHORS  OR COPYRIGHT
            // HOLDERS  BE LIABLE  FOR ANY  CLAIM, DAMAGES  OR OTHER  LIABILITY,
            // WHETHER  IN AN  ACTION OF  CONTRACT, TORT  OR OTHERWISE,  ARISING
            // FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
            // OTHER DEALINGS IN THE SOFTWARE.
            // 
            define("tarfile", "version", S<"0.9.0">());
            define("tarfile", "__author__", S<"Lars Gustäbel (lars@gustaebel.de)">());
            define("tarfile", "__credits__", S<"Gustavo Niemeyer, Niels Gustäbel, Richard Townsend.">());
            // ---------
            // Imports
            // ---------
            // os.symlink on Windows prior to 6.0 raises NotImplementedError
            // OSError (winerror=1314) will be raised if the caller does not hold the
            // SeCreateSymbolicLinkPrivilege privilege
            define("tarfile", "symlink_exception", py_tuple({ B<"AttributeError">(), B<"NotImplementedError">(), B<"OSError">() }));
            // from tarfile import *
            define("tarfile", "__all__", py_list({ S<"TarFile">(), S<"TarInfo">(), S<"is_tarfile">(), S<"TarError">(), S<"ReadError">(), S<"CompressionError">(), S<"StreamError">(), S<"ExtractError">(), S<"HeaderError">(), S<"ENCODING">(), S<"USTAR_FORMAT">(), S<"GNU_FORMAT">(), S<"PAX_FORMAT">(), S<"DEFAULT_FORMAT">(), S<"open">(), S<"fully_trusted_filter">(), S<"data_filter">(), S<"tar_filter">(), S<"FilterError">(), S<"AbsoluteLinkError">(), S<"OutsideDestinationError">(), S<"SpecialFileError">(), S<"AbsolutePathError">(), S<"LinkOutsideDestinationError">(), S<"LinkFallbackError">() }));
            // ---------------------------------------------------------
            // tar constants
            // ---------------------------------------------------------
            define("tarfile", "NUL", py{ make_bytes(std::string{ "\x00", 1 }) });
            define("tarfile", "BLOCKSIZE", I<512>());
            define("tarfile", "RECORDSIZE", (G<"tarfile.BLOCKSIZE">()) * (I<20>()));
            define("tarfile", "GNU_MAGIC", py{ make_bytes(std::string{ "\x75\x73\x74\x61\x72\x20\x20\x00", 8 }) });
            define("tarfile", "POSIX_MAGIC", py{ make_bytes(std::string{ "\x75\x73\x74\x61\x72\x00\x30\x30", 8 }) });
            define("tarfile", "LENGTH_NAME", I<100>());
            define("tarfile", "LENGTH_LINK", I<100>());
            define("tarfile", "LENGTH_PREFIX", I<155>());
            define("tarfile", "REGTYPE", py{ make_bytes(std::string{ "\x30", 1 }) });
            define("tarfile", "AREGTYPE", py{ make_bytes(std::string{ "\x00", 1 }) });
            define("tarfile", "LNKTYPE", py{ make_bytes(std::string{ "\x31", 1 }) });
            define("tarfile", "SYMTYPE", py{ make_bytes(std::string{ "\x32", 1 }) });
            define("tarfile", "CHRTYPE", py{ make_bytes(std::string{ "\x33", 1 }) });
            define("tarfile", "BLKTYPE", py{ make_bytes(std::string{ "\x34", 1 }) });
            define("tarfile", "DIRTYPE", py{ make_bytes(std::string{ "\x35", 1 }) });
            define("tarfile", "FIFOTYPE", py{ make_bytes(std::string{ "\x36", 1 }) });
            define("tarfile", "CONTTYPE", py{ make_bytes(std::string{ "\x37", 1 }) });
            define("tarfile", "GNUTYPE_LONGNAME", py{ make_bytes(std::string{ "\x4c", 1 }) });
            define("tarfile", "GNUTYPE_LONGLINK", py{ make_bytes(std::string{ "\x4b", 1 }) });
            define("tarfile", "GNUTYPE_SPARSE", py{ make_bytes(std::string{ "\x53", 1 }) });
            define("tarfile", "XHDTYPE", py{ make_bytes(std::string{ "\x78", 1 }) });
            define("tarfile", "XGLTYPE", py{ make_bytes(std::string{ "\x67", 1 }) });
            define("tarfile", "SOLARIS_XHDTYPE", py{ make_bytes(std::string{ "\x58", 1 }) });
            define("tarfile", "USTAR_FORMAT", I<0>());
            define("tarfile", "GNU_FORMAT", I<1>());
            define("tarfile", "PAX_FORMAT", I<2>());
            define("tarfile", "DEFAULT_FORMAT", G<"tarfile.PAX_FORMAT">());
            // ---------------------------------------------------------
            // tarfile constants
            // ---------------------------------------------------------
            // File types that tarfile supports:
            define("tarfile", "SUPPORTED_TYPES", py_tuple({ G<"tarfile.REGTYPE">(), G<"tarfile.AREGTYPE">(), G<"tarfile.LNKTYPE">(), G<"tarfile.SYMTYPE">(), G<"tarfile.DIRTYPE">(), G<"tarfile.FIFOTYPE">(), G<"tarfile.CONTTYPE">(), G<"tarfile.CHRTYPE">(), G<"tarfile.BLKTYPE">(), G<"tarfile.GNUTYPE_LONGNAME">(), G<"tarfile.GNUTYPE_LONGLINK">(), G<"tarfile.GNUTYPE_SPARSE">() }));
            // File types that will be treated as a regular file.
            define("tarfile", "REGULAR_TYPES", py_tuple({ G<"tarfile.REGTYPE">(), G<"tarfile.AREGTYPE">(), G<"tarfile.CONTTYPE">(), G<"tarfile.GNUTYPE_SPARSE">() }));
            // File types that are part of the GNU tar format.
            define("tarfile", "GNU_TYPES", py_tuple({ G<"tarfile.GNUTYPE_LONGNAME">(), G<"tarfile.GNUTYPE_LONGLINK">(), G<"tarfile.GNUTYPE_SPARSE">() }));
            // Fields from a pax header that override a TarInfo attribute.
            define("tarfile", "PAX_FIELDS", py_tuple({ S<"path">(), S<"linkpath">(), S<"size">(), S<"mtime">(), S<"uid">(), S<"gid">(), S<"uname">(), S<"gname">() }));
            // Fields from a pax header that are affected by hdrcharset.
            define("tarfile", "PAX_NAME_FIELDS", set_of({ S<"path">(), S<"linkpath">(), S<"uname">(), S<"gname">() }));
            // Fields in a pax header that are numbers, all other fields
            // are treated as strings.
            define("tarfile", "PAX_NUMBER_FIELDS", dict_of({ { S<"atime">(), B<"float">() }, { S<"ctime">(), B<"float">() }, { S<"mtime">(), B<"float">() }, { S<"uid">(), B<"int">() }, { S<"gid">(), B<"int">() }, { S<"size">(), B<"int">() } }));
            // ---------------------------------------------------------
            // initialization
            // ---------------------------------------------------------
#if defined(_WIN32)
            define("tarfile", "ENCODING", S<"utf-8">());
#else
            define("tarfile", "ENCODING", (Mod<"sys">()).method("getfilesystemencoding"));
#endif
            // ---------------------------------------------------------
            // Some useful functions
            // ---------------------------------------------------------
            define("tarfile", "stn", w.function("stn", "stn", { py_positional("s"), py_positional("length"), py_positional("encoding"), py_positional("errors") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py length = a[1];
                    py encoding = a[2];
                    py errors = a[3];
                    if (s.is_none())
                    {
                        py_raise((B<"ValueError">())(S<"metadata cannot contain None">()));
                    }
                    s = s.method("encode", { encoding, errors });
                    return s[B<"slice">()(none(), length, none())] + ((length - py{ s.size() }) * (G<"tarfile.NUL">()));
                }));

            define("tarfile", "nts", w.function("nts", "nts", { py_positional("s"), py_positional("encoding"), py_positional("errors") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py encoding = a[1];
                    py errors = a[2];
                    py p;
                    p = s.method("find", { py{ make_bytes(std::string{ "\x00", 1 }) } });
                    if (p != (I<-1>()))
                    {
                        s = s[B<"slice">()(none(), p, none())];
                    }
                    return s.method("decode", { encoding, errors });
                }));

            define("tarfile", "nti", w.function("nti", "nti", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py i;
                    py n;
                    // There are two possible encodings for a number field, see
                    // itn() below.
                    if (py_tuple({ I<128>(), I<255>() }).contains(s[I<0>()]))
                    {
                        n = I<0>();
                        for (py const& item : iterate{ (B<"range">())(py{ s.size() } - (I<1>())) })
                        {
                            i = item;
                            n = inplace(NEOS_PY_LSHIFT, n, I<8>());
                            n = inplace(NEOS_PY_ADD, n, s[i + (I<1>())]);
                        }
                        if (s[I<0>()] == (I<255>()))
                        {
                            n = -(power((I<256>()), (py{ s.size() } - (I<1>()))) - n);
                        }
                    }
                    else
                    {
                        try
                        {
                            s = (G<"tarfile.nts">())(s, S<"ascii">(), S<"strict">());
                            n = (B<"int">())([&]() -> py { py const t = s.method("strip"); return t.truthy() ? t : S<"0">(); }(), I<8>());
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler2;
                            if (matches(handler2.exception, B<"ValueError">()))
                            {
                                py_raise((G<"tarfile.InvalidHeaderError">())(S<"invalid header">()));
                            }
                            else
                                py_reraise(handler2.exception);
                        }
                    }
                    return n;
                }));

            define("tarfile", "itn", w.function("itn", "itn", { py_positional("n"), py_positional("digits", I<8>()), py_positional("format", G<"tarfile.DEFAULT_FORMAT">()) }, [](arguments a) -> py
                {
                    py n = a[0];
                    py digits = a[1];
                    py format_ = a[2];
                    py i;
                    py original_n;
                    py s;
                    // POSIX 1003.1-1988 requires numbers to be encoded as a string of
                    // octal digits followed by a null-byte, this allows values up to
                    // (8**(digits-1))-1. GNU tar allows storing numbers greater than
                    // that if necessary. A leading 0o200 or 0o377 byte indicate this
                    // particular encoding, the following digits-1 bytes are a big-endian
                    // base-256 representation. This allows values up to (256**(digits-1))-1.
                    // A 0o200 byte indicates a positive number, a 0o377 byte a negative
                    // number.
                    original_n = n;
                    n = (B<"int">())(n);
                    if (((I<0>()) <= n) && (n < power((I<8>()), (digits - (I<1>())))))
                    {
                        s = (B<"bytes">())((S<"%0*o">()) % py_tuple({ digits - (I<1>()), n }), S<"ascii">()) + (G<"tarfile.NUL">());
                    }
                    else if (((format_ == (G<"tarfile.GNU_FORMAT">())) && (((-power((I<256>()), (digits - (I<1>())))) <= n) && (n < power((I<256>()), (digits - (I<1>())))))))
                    {
                        if (n >= (I<0>()))
                        {
                            s = (B<"bytearray">())(py_list({ I<128>() }));
                        }
                        else
                        {
                            s = (B<"bytearray">())(py_list({ I<255>() }));
                            n = power((I<256>()), digits) + n;
                        }
                        for (py const& item : iterate{ (B<"range">())(digits - (I<1>())) })
                        {
                            i = item;
                            s.method("insert", { I<1>(), n & (I<255>()) });
                            n = inplace(NEOS_PY_RSHIFT, n, I<8>());
                        }
                    }
                    else
                    {
                        py_raise((B<"ValueError">())(S<"overflow in number field">()));
                    }
                    return s;
                }));

            define("tarfile", "calc_chksums", w.function("calc_chksums", "calc_chksums", { py_positional("buf") }, [](arguments a) -> py
                {
                    py buf = a[0];
                    py signed_chksum;
                    py unsigned_chksum;
                    unsigned_chksum = (I<256>()) + (B<"sum">())((Mod<"struct">().attr("unpack_from"))(S<"148B8x356B">(), buf));
                    signed_chksum = (I<256>()) + (B<"sum">())((Mod<"struct">().attr("unpack_from"))(S<"148b8x356b">(), buf));
                    return py_tuple({ unsigned_chksum, signed_chksum });
                }));

            define("tarfile", "copyfileobj", w.function("copyfileobj", "copyfileobj", { py_positional("src"), py_positional("dst"), py_positional("length", none()), py_positional("exception", B<"OSError">()), py_positional("bufsize", none()) }, [](arguments a) -> py
                {
                    py src = a[0];
                    py dst = a[1];
                    py length = a[2];
                    py exception = a[3];
                    py bufsize = a[4];
                    py b;
                    py blocks;
                    py buf;
                    py remainder;
                    bufsize = [&]() -> py { py const t = bufsize; return t.truthy() ? t : (I<16>()) * (I<1024>()); }();
                    if (length == (I<0>()))
                    {
                        return none();
                    }
                    if (length.is_none())
                    {
                        (Mod<"shutil">().attr("copyfileobj"))(src, dst, bufsize);
                        return none();
                    }
                    auto const items4 = unpack((B<"divmod">())(length, bufsize), 2);
                    blocks = items4[0];
                    remainder = items4[1];
                    for (py const& item : iterate{ (B<"range">())(blocks) })
                    {
                        b = item;
                        buf = src.method("read", { bufsize });
                        if (py{ buf.size() } < bufsize)
                        {
                            py_raise(exception(S<"unexpected end of data">()));
                        }
                        dst.method("write", { buf });
                    }
                    if (remainder != (I<0>()))
                    {
                        buf = src.method("read", { remainder });
                        if (py{ buf.size() } < remainder)
                        {
                            py_raise(exception(S<"unexpected end of data">()));
                        }
                        dst.method("write", { buf });
                    }
                    return none();
                }));

            // Maximum number of bytes read in a single call when reading a member's
            // extended header (a GNU long name/link or a pax header).  The size of such
            // a header is taken from the archive and is not trustworthy, so it is read in
            // bounded chunks to avoid a huge up-front allocation when a crafted or
            // truncated archive claims far more data than the file actually contains
            // (gh-151497).
            define("tarfile", "_EXTHEADER_READ_CHUNK", (I<1024>()) * (I<1024>()));
            define("tarfile", "_safe_read", w.function("_safe_read", "_safe_read", { py_positional("fileobj"), py_positional("size") }, [](arguments a) -> py
                {
                    py fileobj = a[0];
                    py size = a[1];
                    py chunk;
                    py chunks;
                    if (size <= (G<"tarfile._EXTHEADER_READ_CHUNK">()))
                    {
                        return fileobj.method("read", { size });
                    }
                    chunks = py_list();
                    while (size > (I<0>()))
                    {
                        chunk = fileobj.method("read", { (B<"min">())(size, G<"tarfile._EXTHEADER_READ_CHUNK">()) });
                        if (!chunk.truthy())
                        {
                            break;
                        }
                        chunks.method("append", { chunk });
                        size = inplace(NEOS_PY_SUBTRACT, size, py{ chunk.size() });
                    }
                    return py{ make_bytes(std::string{ "", 0 }) }.method("join", { chunks });
                }));

            define("tarfile", "_safe_print", w.function("_safe_print", "_safe_print", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py encoding;
                    encoding = (B<"getattr">())(Mod<"sys">().attr("stdout"), S<"encoding">(), none());
                    if (!encoding.is_none())
                    {
                        s = s.method("encode", { encoding, S<"backslashreplace">() }).method("decode", { encoding });
                    }
                    (B<"print">()).call({ s }, { { "end", S<" ">() } });
                    return none();
                }));

            define("tarfile", "TarError", w.make_class("TarError", { B<"Exception">() }, {
 }));

            define("tarfile", "ExtractError", w.make_class("ExtractError", { G<"tarfile.TarError">() }, {
 }));

            define("tarfile", "ReadError", w.make_class("ReadError", { G<"tarfile.TarError">() }, {
 }));

            define("tarfile", "CompressionError", w.make_class("CompressionError", { G<"tarfile.TarError">() }, {
 }));

            define("tarfile", "StreamError", w.make_class("StreamError", { G<"tarfile.TarError">() }, {
 }));

            define("tarfile", "HeaderError", w.make_class("HeaderError", { G<"tarfile.TarError">() }, {
 }));

            define("tarfile", "EmptyHeaderError", w.make_class("EmptyHeaderError", { G<"tarfile.HeaderError">() }, {
 }));

            define("tarfile", "TruncatedHeaderError", w.make_class("TruncatedHeaderError", { G<"tarfile.HeaderError">() }, {
 }));

            define("tarfile", "EOFHeaderError", w.make_class("EOFHeaderError", { G<"tarfile.HeaderError">() }, {
 }));

            define("tarfile", "InvalidHeaderError", w.make_class("InvalidHeaderError", { G<"tarfile.HeaderError">() }, {
 }));

            define("tarfile", "SubsequentHeaderError", w.make_class("SubsequentHeaderError", { G<"tarfile.HeaderError">() }, {
 }));

            // ---------------------------
            // internal stream interface
            // ---------------------------
            define("tarfile", "_LowLevelFile", w.make_class("_LowLevelFile", {  }, {
                { "__init__", w.method("_LowLevelFile", "__init__", { py_positional("name"), py_positional("mode") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py mode = a[2];
                        mode = dict_of({ { S<"r">(), Mod<"os">().attr("O_RDONLY") }, { S<"w">(), ((Mod<"os">().attr("O_WRONLY")) | (Mod<"os">().attr("O_CREAT"))) | (Mod<"os">().attr("O_TRUNC")) } })[mode];
                        if (has_attr(Mod<"os">(), "O_BINARY"))
                        {
                            mode = inplace(NEOS_PY_OR, mode, Mod<"os">().attr("O_BINARY"));
                        }
                        self.set_attr("fd", (Mod<"os">().attr("open"))(name, mode, I<438>()));
                        return none();
                    }) },
                { "close", w.method("_LowLevelFile", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (Mod<"os">().attr("close"))(self.attr("fd"));
                        return none();
                    }) },
                { "read", w.method("_LowLevelFile", "read", { py_positional("size") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        return (Mod<"os">().attr("read"))(self.attr("fd"), size);
                    }) },
                { "write", w.method("_LowLevelFile", "write", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        (Mod<"os">().attr("write"))(self.attr("fd"), s);
                        return none();
                    }) }, }));

            define("tarfile", "_Stream", w.make_class("_Stream", {  }, {
                { "__init__", w.method("_Stream", "__init__", { py_positional("name"), py_positional("mode"), py_positional("comptype"), py_positional("fileobj"), py_positional("bufsize"), py_positional("compresslevel") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py mode = a[2];
                        py comptype = a[3];
                        py fileobj = a[4];
                        py bufsize = a[5];
                        py compresslevel = a[6];
                        py bz2;
                        py lzma;
                        py zlib;
                        self.set_attr("_extfileobj", boolean(true));
                        if (fileobj.is_none())
                        {
                            fileobj = (G<"tarfile._LowLevelFile">())(name, mode);
                            self.set_attr("_extfileobj", boolean(false));
                        }
                        if (comptype == (S<"*">()))
                        {
                            // Enable transparent compression detection for the
                            // stream interface
                            fileobj = (G<"tarfile._StreamProxy">())(fileobj);
                            comptype = fileobj.method("getcomptype");
                        }
                        self.set_attr("name", (!name.is_none() ? (Mod<"os">().attr("fspath"))(name) : S<"">()));
                        self.set_attr("mode", mode);
                        self.set_attr("comptype", comptype);
                        self.set_attr("fileobj", fileobj);
                        self.set_attr("bufsize", bufsize);
                        self.set_attr("buf", py{ make_bytes(std::string{ "", 0 }) });
                        self.set_attr("pos", I<0>());
                        self.set_attr("closed", boolean(false));
                        try
                        {
                            if (comptype == (S<"gz">()))
                            {
                                try
                                {
                                    zlib = module_of("zlib");
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler8;
                                    if (matches(handler8.exception, B<"ImportError">()))
                                    {
                                        fn_raise_from((G<"tarfile.CompressionError">())(S<"zlib module is not available">()).get(), none().get());
                                        throw raised{};
                                    }
                                    else
                                        py_reraise(handler8.exception);
                                }
                                self.set_attr("zlib", zlib);
                                self.set_attr("crc", zlib.method("crc32", { py{ make_bytes(std::string{ "", 0 }) } }));
                                if (mode == (S<"r">()))
                                {
                                    self.set_attr("exception", zlib.attr("error"));
                                    self.method("_init_read_gz");
                                }
                                else
                                {
                                    self.method("_init_write_gz", { compresslevel });
                                }
                            }
                            else if (comptype == (S<"bz2">()))
                            {
                                try
                                {
                                    bz2 = module_of("bz2");
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler9;
                                    if (matches(handler9.exception, B<"ImportError">()))
                                    {
                                        fn_raise_from((G<"tarfile.CompressionError">())(S<"bz2 module is not available">()).get(), none().get());
                                        throw raised{};
                                    }
                                    else
                                        py_reraise(handler9.exception);
                                }
                                if (mode == (S<"r">()))
                                {
                                    self.set_attr("dbuf", py{ make_bytes(std::string{ "", 0 }) });
                                    self.set_attr("cmp", bz2.method("BZ2Decompressor"));
                                    self.set_attr("exception", B<"OSError">());
                                }
                                else
                                {
                                    self.set_attr("cmp", bz2.method("BZ2Compressor", { compresslevel }));
                                }
                            }
                            else if (comptype == (S<"xz">()))
                            {
                                try
                                {
                                    lzma = module_of("lzma");
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler10;
                                    if (matches(handler10.exception, B<"ImportError">()))
                                    {
                                        fn_raise_from((G<"tarfile.CompressionError">())(S<"lzma module is not available">()).get(), none().get());
                                        throw raised{};
                                    }
                                    else
                                        py_reraise(handler10.exception);
                                }
                                if (mode == (S<"r">()))
                                {
                                    self.set_attr("dbuf", py{ make_bytes(std::string{ "", 0 }) });
                                    self.set_attr("cmp", lzma.method("LZMADecompressor"));
                                    self.set_attr("exception", lzma.attr("LZMAError"));
                                }
                                else
                                {
                                    self.set_attr("cmp", lzma.method("LZMACompressor"));
                                }
                            }
                            else if (comptype != (S<"tar">()))
                            {
                                py_raise((G<"tarfile.CompressionError">())((S<"unknown compression type %r">()) % comptype));
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler7;
                            {
                                if (!self.attr("_extfileobj").truthy())
                                {
                                    self.attr("fileobj").method("close");
                                }
                                self.set_attr("closed", boolean(true));
                                py_reraise(handler7.exception);
                            }
                        }
                        return none();
                    }) },
                { "__del__", w.method("_Stream", "__del__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((has_attr(self, "closed") && (!self.attr("closed").truthy())))
                        {
                            self.method("close");
                        }
                        return none();
                    }) },
                { "_init_write_gz", w.method("_Stream", "_init_write_gz", { py_positional("compresslevel") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py compresslevel = a[1];
                        py timestamp;
                        self.set_attr("cmp", self.attr("zlib").method("compressobj", { compresslevel, self.attr("zlib").attr("DEFLATED"), -self.attr("zlib").attr("MAX_WBITS"), self.attr("zlib").attr("DEF_MEM_LEVEL"), I<0>() }));
                        timestamp = (Mod<"struct">().attr("pack"))(S<"<L">(), (B<"int">())((Mod<"time">().attr("time"))()));
                        self.method("_Stream__write", { (py{ make_bytes(std::string{ "\x1f\x8b\x08\x08", 4 }) } + timestamp) + py{ make_bytes(std::string{ "\x02\xff", 2 }) } });
                        if (self.attr("name").method("endswith", { S<".gz">() }).truthy())
                        {
                            self.set_attr("name", self.attr("name")[B<"slice">()(none(), I<-3>(), none())]);
                        }
                        // Honor "directory components removed" from RFC1952
                        self.set_attr("name", (Mod<"os">().attr("path")).method("basename", { self.attr("name") }));
                        // RFC1952 says we must use ISO-8859-1 for the FNAME field.
                        self.method("_Stream__write", { self.attr("name").method("encode", { S<"iso-8859-1">(), S<"replace">() }) + (G<"tarfile.NUL">()) });
                        return none();
                    }) },
                { "write", w.method("_Stream", "write", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        if (self.attr("comptype") == (S<"gz">()))
                        {
                            self.set_attr("crc", self.attr("zlib").method("crc32", { s, self.attr("crc") }));
                        }
                        {
                            py const target11 = self;
                            target11.set_attr("pos", inplace(NEOS_PY_ADD, target11.attr("pos"), py{ s.size() }));
                        }
                        if (self.attr("comptype") != (S<"tar">()))
                        {
                            s = self.attr("cmp").method("compress", { s });
                        }
                        self.method("_Stream__write", { s });
                        return none();
                    }) },
                { "_Stream__write", w.method("_Stream", "__write", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        {
                            py const target12 = self;
                            target12.set_attr("buf", inplace(NEOS_PY_ADD, target12.attr("buf"), s));
                        }
                        while (py{ self.attr("buf").size() } > self.attr("bufsize"))
                        {
                            self.attr("fileobj").method("write", { self.attr("buf")[B<"slice">()(none(), self.attr("bufsize"), none())] });
                            self.set_attr("buf", self.attr("buf")[B<"slice">()(self.attr("bufsize"), none(), none())]);
                        }
                        return none();
                    }) },
                { "close", w.method("_Stream", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("closed").truthy())
                        {
                            return none();
                        }
                        self.set_attr("closed", boolean(true));
                        {
                            std::optional<py> pending14;
                            [[maybe_unused]] int flow14 = 0;
                            [[maybe_unused]] py result14;
                            try
                            {
                                if (((self.attr("mode") == (S<"w">())) && (self.attr("comptype") != (S<"tar">()))))
                                {
                                    {
                                        py const target15 = self;
                                        target15.set_attr("buf", inplace(NEOS_PY_ADD, target15.attr("buf"), self.attr("cmp").method("flush")));
                                    }
                                }
                                if (((self.attr("mode") == (S<"w">())) && self.attr("buf").truthy()))
                                {
                                    self.attr("fileobj").method("write", { self.attr("buf") });
                                    self.set_attr("buf", py{ make_bytes(std::string{ "", 0 }) });
                                    if (self.attr("comptype") == (S<"gz">()))
                                    {
                                        self.attr("fileobj").method("write", { (Mod<"struct">().attr("pack"))(S<"<L">(), self.attr("crc")) });
                                        self.attr("fileobj").method("write", { (Mod<"struct">().attr("pack"))(S<"<L">(), self.attr("pos") & (I<4294967295ll>())) });
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending14 = py_take_exception();
                            }
                            finally14:
                            if (!self.attr("_extfileobj").truthy())
                            {
                                self.attr("fileobj").method("close");
                            }
                            if (pending14)
                                py_reraise(*pending14);
                            if (flow14 == 1)
                                return result14;
                        }
                        return none();
                    }) },
                { "_init_read_gz", w.method("_Stream", "_init_read_gz", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py flag;
                        py s;
                        py xlen;
                        self.set_attr("cmp", self.attr("zlib").method("decompressobj", { -self.attr("zlib").attr("MAX_WBITS") }));
                        self.set_attr("dbuf", py{ make_bytes(std::string{ "", 0 }) });
                        // taken from gzip.GzipFile with some alterations
                        if (self.method("_Stream__read", { I<2>() }) != py{ make_bytes(std::string{ "\x1f\x8b", 2 }) })
                        {
                            py_raise((G<"tarfile.ReadError">())(S<"not a gzip file">()));
                        }
                        if (self.method("_Stream__read", { I<1>() }) != py{ make_bytes(std::string{ "\x08", 1 }) })
                        {
                            py_raise((G<"tarfile.CompressionError">())(S<"unsupported compression method">()));
                        }
                        flag = (B<"ord">())(self.method("_Stream__read", { I<1>() }));
                        self.method("_Stream__read", { I<6>() });
                        if ((flag & (I<4>())).truthy())
                        {
                            xlen = (B<"ord">())(self.method("_Stream__read", { I<1>() })) + ((I<256>()) * (B<"ord">())(self.method("_Stream__read", { I<1>() })));
                            self.method("_Stream__read", { xlen });
                        }
                        if ((flag & (I<8>())).truthy())
                        {
                            while (boolean(true).truthy())
                            {
                                s = self.method("_Stream__read", { I<1>() });
                                if (((!s.truthy()) || (s == (G<"tarfile.NUL">()))))
                                {
                                    break;
                                }
                            }
                        }
                        if ((flag & (I<16>())).truthy())
                        {
                            while (boolean(true).truthy())
                            {
                                s = self.method("_Stream__read", { I<1>() });
                                if (((!s.truthy()) || (s == (G<"tarfile.NUL">()))))
                                {
                                    break;
                                }
                            }
                        }
                        if ((flag & (I<2>())).truthy())
                        {
                            self.method("_Stream__read", { I<2>() });
                        }
                        return none();
                    }) },
                { "tell", w.method("_Stream", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("pos");
                    }) },
                { "seek", w.method("_Stream", "seek", { py_positional("pos", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pos = a[1];
                        py blocks;
                        py data;
                        py i;
                        py remainder;
                        if ((pos - self.attr("pos")) >= (I<0>()))
                        {
                            auto const items18 = unpack((B<"divmod">())(pos - self.attr("pos"), self.attr("bufsize")), 2);
                            blocks = items18[0];
                            remainder = items18[1];
                            for (py const& item : iterate{ (B<"range">())(blocks) })
                            {
                                i = item;
                                data = self.method("read", { self.attr("bufsize") });
                                if (!data.truthy())
                                {
                                    break;
                                }
                            }
                            self.method("read", { remainder });
                        }
                        else
                        {
                            py_raise((G<"tarfile.StreamError">())(S<"seeking backwards is not allowed">()));
                        }
                        return self.attr("pos");
                    }) },
                { "read", w.method("_Stream", "read", { py_positional("size") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py buf;
                        if (size.is_none())
                            py_raise(B<"AssertionError">()());
                        buf = self.method("_read", { size });
                        {
                            py const target20 = self;
                            target20.set_attr("pos", inplace(NEOS_PY_ADD, target20.attr("pos"), py{ buf.size() }));
                        }
                        return buf;
                    }) },
                { "_read", w.method("_Stream", "_read", { py_positional("size") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py buf;
                        py c;
                        py e;
                        py t;
                        if (self.attr("comptype") == (S<"tar">()))
                        {
                            return self.method("_Stream__read", { size });
                        }
                        c = py{ self.attr("dbuf").size() };
                        t = py_list({ self.attr("dbuf") });
                        while (c < size)
                        {
                            // Skip underlying buffer to avoid unaligned double buffering.
                            if (self.attr("buf").truthy())
                            {
                                buf = self.attr("buf");
                                self.set_attr("buf", py{ make_bytes(std::string{ "", 0 }) });
                            }
                            else
                            {
                                buf = self.attr("fileobj").method("read", { self.attr("bufsize") });
                                if (!buf.truthy())
                                {
                                    break;
                                }
                            }
                            try
                            {
                                buf = self.attr("cmp").method("decompress", { buf });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler22;
                                if (matches(handler22.exception, self.attr("exception")))
                                {
                                    e = handler22.exception;
                                    fn_raise_from((G<"tarfile.ReadError">())(S<"invalid compressed data">()).get(), e.get());
                                    throw raised{};
                                }
                                else
                                    py_reraise(handler22.exception);
                            }
                            t.method("append", { buf });
                            c = inplace(NEOS_PY_ADD, c, py{ buf.size() });
                        }
                        t = py{ make_bytes(std::string{ "", 0 }) }.method("join", { t });
                        self.set_attr("dbuf", t[B<"slice">()(size, none(), none())]);
                        return t[B<"slice">()(none(), size, none())];
                    }) },
                { "_Stream__read", w.method("_Stream", "__read", { py_positional("size") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py buf;
                        py c;
                        py t;
                        c = py{ self.attr("buf").size() };
                        t = py_list({ self.attr("buf") });
                        while (c < size)
                        {
                            buf = self.attr("fileobj").method("read", { self.attr("bufsize") });
                            if (!buf.truthy())
                            {
                                break;
                            }
                            t.method("append", { buf });
                            c = inplace(NEOS_PY_ADD, c, py{ buf.size() });
                        }
                        t = py{ make_bytes(std::string{ "", 0 }) }.method("join", { t });
                        self.set_attr("buf", t[B<"slice">()(size, none(), none())]);
                        return t[B<"slice">()(none(), size, none())];
                    }) }, }));

            // class _Stream
            define("tarfile", "_StreamProxy", w.make_class("_StreamProxy", { B<"object">() }, {
                { "__init__", w.method("_StreamProxy", "__init__", { py_positional("fileobj") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fileobj = a[1];
                        self.set_attr("fileobj", fileobj);
                        self.set_attr("buf", self.attr("fileobj").method("read", { G<"tarfile.BLOCKSIZE">() }));
                        return none();
                    }) },
                { "read", w.method("_StreamProxy", "read", { py_positional("size") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        self.set_attr("read", self.attr("fileobj").attr("read"));
                        return self.attr("buf");
                    }) },
                { "getcomptype", w.method("_StreamProxy", "getcomptype", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("buf").method("startswith", { py{ make_bytes(std::string{ "\x1f\x8b\x08", 3 }) } }).truthy())
                        {
                            return S<"gz">();
                        }
                        else if (((self.attr("buf")[B<"slice">()(I<0>(), I<3>(), none())] == py{ make_bytes(std::string{ "\x42\x5a\x68", 3 }) }) && (self.attr("buf")[B<"slice">()(I<4>(), I<10>(), none())] == py{ make_bytes(std::string{ "\x31\x41\x59\x26\x53\x59", 6 }) })))
                        {
                            return S<"bz2">();
                        }
                        else if (self.attr("buf").method("startswith", { py_tuple({ py{ make_bytes(std::string{ "\x5d\x00\x00\x80", 4 }) }, py{ make_bytes(std::string{ "\xfd\x37\x7a\x58\x5a", 5 }) } }) }).truthy())
                        {
                            return S<"xz">();
                        }
                        else
                        {
                            return S<"tar">();
                        }
                        return none();
                    }) },
                { "close", w.method("_StreamProxy", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("fileobj").method("close");
                        return none();
                    }) }, }));

            // class StreamProxy
            // ------------------------
            // Extraction file object
            // ------------------------
            define("tarfile", "_FileInFile", w.make_class("_FileInFile", { B<"object">() }, {
                { "__init__", w.method("_FileInFile", "__init__", { py_positional("fileobj"), py_positional("offset"), py_positional("size"), py_positional("name"), py_positional("blockinfo", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fileobj = a[1];
                        py offset = a[2];
                        py size = a[3];
                        py name = a[4];
                        py blockinfo = a[5];
                        py lastpos;
                        py realpos;
                        self.set_attr("fileobj", fileobj);
                        self.set_attr("offset", offset);
                        self.set_attr("size", size);
                        self.set_attr("position", I<0>());
                        self.set_attr("name", name);
                        self.set_attr("closed", boolean(false));
                        if (blockinfo.is_none())
                        {
                            blockinfo = py_list({ py_tuple({ I<0>(), size }) });
                        }
                        // Construct a map with data and zero blocks.
                        self.set_attr("map_index", I<0>());
                        self.set_attr("map", py_list());
                        lastpos = I<0>();
                        realpos = self.attr("offset");
                        for (py const& item : iterate{ blockinfo })
                        {
                            auto const items25 = unpack(item, 2);
                            offset = items25[0];
                            size = items25[1];
                            if (offset > lastpos)
                            {
                                self.attr("map").method("append", { py_tuple({ boolean(false), lastpos, offset, none() }) });
                            }
                            self.attr("map").method("append", { py_tuple({ boolean(true), offset, offset + size, realpos }) });
                            realpos = inplace(NEOS_PY_ADD, realpos, size);
                            lastpos = offset + size;
                        }
                        if (lastpos < self.attr("size"))
                        {
                            self.attr("map").method("append", { py_tuple({ boolean(false), lastpos, self.attr("size"), none() }) });
                        }
                        return none();
                    }) },
                { "flush", w.method("_FileInFile", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "mode", B<"property">()(w.method("_FileInFile", "mode", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S<"rb">();
                    })) },
                { "readable", w.method("_FileInFile", "readable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) },
                { "writable", w.method("_FileInFile", "writable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(false);
                    }) },
                { "seekable", w.method("_FileInFile", "seekable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("fileobj").method("seekable");
                    }) },
                { "tell", w.method("_FileInFile", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("position");
                    }) },
                { "seek", w.method("_FileInFile", "seek", { py_positional("position"), py_positional("whence", Mod<"io">().attr("SEEK_SET")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py position = a[1];
                        py whence = a[2];
                        if (whence == (Mod<"io">().attr("SEEK_SET")))
                        {
                            self.set_attr("position", (B<"min">())((B<"max">())(position, I<0>()), self.attr("size")));
                        }
                        else if (whence == (Mod<"io">().attr("SEEK_CUR")))
                        {
                            if (position < (I<0>()))
                            {
                                self.set_attr("position", (B<"max">())(self.attr("position") + position, I<0>()));
                            }
                            else
                            {
                                self.set_attr("position", (B<"min">())(self.attr("position") + position, self.attr("size")));
                            }
                        }
                        else if (whence == (Mod<"io">().attr("SEEK_END")))
                        {
                            self.set_attr("position", (B<"max">())((B<"min">())(self.attr("size") + position, self.attr("size")), I<0>()));
                        }
                        else
                        {
                            py_raise((B<"ValueError">())(S<"Invalid argument">()));
                        }
                        return self.attr("position");
                    }) },
                { "read", w.method("_FileInFile", "read", { py_positional("size", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py b;
                        py buf;
                        py data;
                        py length;
                        py offset;
                        py start;
                        py stop;
                        if (size.is_none())
                        {
                            size = self.attr("size") - self.attr("position");
                        }
                        else
                        {
                            size = (B<"min">())(size, self.attr("size") - self.attr("position"));
                        }
                        buf = py{ make_bytes(std::string{ "", 0 }) };
                        while (size > (I<0>()))
                        {
                            while (boolean(true).truthy())
                            {
                                auto const items28 = unpack(self.attr("map")[self.attr("map_index")], 4);
                                data = items28[0];
                                start = items28[1];
                                stop = items28[2];
                                offset = items28[3];
                                if ((start <= self.attr("position")) && (self.attr("position") < stop))
                                {
                                    break;
                                }
                                else
                                {
                                    {
                                        py const target29 = self;
                                        target29.set_attr("map_index", inplace(NEOS_PY_ADD, target29.attr("map_index"), I<1>()));
                                    }
                                    if (self.attr("map_index") == py{ self.attr("map").size() })
                                    {
                                        self.set_attr("map_index", I<0>());
                                    }
                                }
                            }
                            length = (B<"min">())(size, stop - self.attr("position"));
                            if (data.truthy())
                            {
                                self.attr("fileobj").method("seek", { offset + (self.attr("position") - start) });
                                b = self.attr("fileobj").method("read", { length });
                                if (py{ b.size() } != length)
                                {
                                    py_raise((G<"tarfile.ReadError">())(S<"unexpected end of data">()));
                                }
                                buf = inplace(NEOS_PY_ADD, buf, b);
                            }
                            else
                            {
                                buf = inplace(NEOS_PY_ADD, buf, (G<"tarfile.NUL">()) * length);
                            }
                            size = inplace(NEOS_PY_SUBTRACT, size, length);
                            {
                                py const target30 = self;
                                target30.set_attr("position", inplace(NEOS_PY_ADD, target30.attr("position"), length));
                            }
                        }
                        return buf;
                    }) },
                { "readinto", w.method("_FileInFile", "readinto", { py_positional("b") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py b = a[1];
                        py buf;
                        buf = self.method("read", { py{ b.size() } });
                        b.set_item(B<"slice">()(none(), py{ buf.size() }, none()), buf);
                        return py{ buf.size() };
                    }) },
                { "close", w.method("_FileInFile", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("closed", boolean(true));
                        return none();
                    }) }, }));

            // class _FileInFile
            define("tarfile", "ExFileObject", w.make_class("ExFileObject", { Mod<"io">().attr("BufferedReader") }, {
                { "__init__", w.method("ExFileObject", "__init__", { py_positional("tarfile"), py_positional("tarinfo") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarfile = a[1];
                        py tarinfo = a[2];
                        py fileobj;
                        fileobj = (G<"tarfile._FileInFile">())(tarfile.attr("fileobj"), tarinfo.attr("offset_data"), tarinfo.attr("size"), tarinfo.attr("name"), tarinfo.attr("sparse"));
                        (B<"super">()(G<"tarfile.ExFileObject">(), self)).method("__init__", { fileobj });
                        return none();
                    }) }, }));

            // class ExFileObject
            // -----------------------------
            // extraction filters (PEP 706)
            // -----------------------------
            define("tarfile", "FilterError", w.make_class("FilterError", { G<"tarfile.TarError">() }, {
 }));

            define("tarfile", "AbsolutePathError", w.make_class("AbsolutePathError", { G<"tarfile.FilterError">() }, {
                { "__init__", w.method("AbsolutePathError", "__init__", { py_positional("tarinfo") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        self.set_attr("tarinfo", tarinfo);
                        (B<"super">()(G<"tarfile.AbsolutePathError">(), self)).method("__init__", { S(std::string{ "member " } + tarinfo.attr("name").repr() + " has an absolute path") });
                        return none();
                    }) }, }));

            define("tarfile", "OutsideDestinationError", w.make_class("OutsideDestinationError", { G<"tarfile.FilterError">() }, {
                { "__init__", w.method("OutsideDestinationError", "__init__", { py_positional("tarinfo"), py_positional("path") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py path = a[2];
                        self.set_attr("tarinfo", tarinfo);
                        self.set_attr("_path", path);
                        (B<"super">()(G<"tarfile.OutsideDestinationError">(), self)).method("__init__", { S(tarinfo.attr("name").repr() + " would be extracted to " + path.repr() + ", ") + (S<"which is outside the destination">()) });
                        return none();
                    }) }, }));

            define("tarfile", "SpecialFileError", w.make_class("SpecialFileError", { G<"tarfile.FilterError">() }, {
                { "__init__", w.method("SpecialFileError", "__init__", { py_positional("tarinfo") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        self.set_attr("tarinfo", tarinfo);
                        (B<"super">()(G<"tarfile.SpecialFileError">(), self)).method("__init__", { S(tarinfo.attr("name").repr() + " is a special file") });
                        return none();
                    }) }, }));

            define("tarfile", "AbsoluteLinkError", w.make_class("AbsoluteLinkError", { G<"tarfile.FilterError">() }, {
                { "__init__", w.method("AbsoluteLinkError", "__init__", { py_positional("tarinfo") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        self.set_attr("tarinfo", tarinfo);
                        (B<"super">()(G<"tarfile.AbsoluteLinkError">(), self)).method("__init__", { S(tarinfo.attr("name").repr() + " is a link to an absolute path") });
                        return none();
                    }) }, }));

            define("tarfile", "LinkOutsideDestinationError", w.make_class("LinkOutsideDestinationError", { G<"tarfile.FilterError">() }, {
                { "__init__", w.method("LinkOutsideDestinationError", "__init__", { py_positional("tarinfo"), py_positional("path") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py path = a[2];
                        self.set_attr("tarinfo", tarinfo);
                        self.set_attr("_path", path);
                        (B<"super">()(G<"tarfile.LinkOutsideDestinationError">(), self)).method("__init__", { S(tarinfo.attr("name").repr() + " would link to " + path.repr() + ", ") + (S<"which is outside the destination">()) });
                        return none();
                    }) }, }));

            define("tarfile", "LinkFallbackError", w.make_class("LinkFallbackError", { G<"tarfile.FilterError">() }, {
                { "__init__", w.method("LinkFallbackError", "__init__", { py_positional("tarinfo"), py_positional("path") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py path = a[2];
                        self.set_attr("tarinfo", tarinfo);
                        self.set_attr("_path", path);
                        (B<"super">()(G<"tarfile.LinkFallbackError">(), self)).method("__init__", { S(std::string{ "link " } + tarinfo.attr("name").repr() + " would be extracted as a ") + S(std::string{ "copy of " } + path.repr() + ", which was rejected") });
                        return none();
                    }) }, }));

            // Errors caused by filters -- both "fatal" and "non-fatal" -- that
            // we consider to be issues with the argument, rather than a bug in the
            // filter function
            define("tarfile", "_FILTER_ERRORS", py_tuple({ G<"tarfile.FilterError">(), B<"OSError">(), G<"tarfile.ExtractError">() }));
            define("tarfile", "_get_filtered_attrs", w.function("_get_filtered_attrs", "_get_filtered_attrs", { py_positional("member"), py_positional("dest_path"), py_positional("for_data", boolean(true)) }, [](arguments a) -> py
                {
                    py member = a[0];
                    py dest_path = a[1];
                    py for_data = a[2];
                    py link_dir;
                    py mode;
                    py name;
                    py new_attrs;
                    py normalized;
                    py target_path;
                    new_attrs = py_dict();
                    name = member.attr("name");
                    dest_path = (Mod<"os">().attr("path")).method("realpath", { dest_path }, { { "strict", (Mod<"os">().attr("path")).attr("ALLOW_MISSING") } });
                    // Strip leading / (tar's directory separator) from filenames.
                    // Include os.sep (target OS directory separator) as well.
                    if (name.method("startswith", { py_tuple({ S<"/">(), Mod<"os">().attr("sep") }) }).truthy())
                    {
                        py const value31 = member.attr("path").method("lstrip", { (S<"/">()) + (Mod<"os">().attr("sep")) });
                        name = value31;
                        new_attrs.set_item(S<"name">(), value31);
                    }
                    if ((Mod<"os">().attr("path")).method("isabs", { name }).truthy())
                    {
                        // Path is absolute even after stripping.
                        // For example, 'C:/foo' on Windows.
                        py_raise((G<"tarfile.AbsolutePathError">())(member));
                    }
                    // Ensure we stay in the destination
                    if (name.method("replace", { Mod<"os">().attr("sep"), S<"/">() }).method("split", { S<"/">() }).contains((S<"..">())))
                    {
                        // Directories are created from the name as given, so a name that
                        // leaves the destination part-way through would create them
                        // outside it even if the resolved path stays inside.
                        normalized = (Mod<"os">().attr("path")).method("normpath", { name });
                        if (normalized != name)
                        {
                            py const value32 = normalized;
                            name = value32;
                            new_attrs.set_item(S<"name">(), value32);
                        }
                    }
                    target_path = (Mod<"os">().attr("path")).method("realpath", { (Mod<"os">().attr("path")).method("join", { dest_path, name }) }, { { "strict", (Mod<"os">().attr("path")).attr("ALLOW_MISSING") } });
                    if ((Mod<"os">().attr("path")).method("commonpath", { py_list({ target_path, dest_path }) }) != dest_path)
                    {
                        py_raise((G<"tarfile.OutsideDestinationError">())(member, target_path));
                    }
                    // Limit permissions (no high bits, and go-w)
                    mode = member.attr("mode");
                    if (!mode.is_none())
                    {
                        // Strip high bits & group/other write bits
                        mode = mode & (I<493>());
                        if (for_data.truthy())
                        {
                            // For data, handle permissions & file types
                            if ((member.method("isreg").truthy() || member.method("islnk").truthy()))
                            {
                                if (!(mode & (I<64>())).truthy())
                                {
                                    // Clear executable bits if not executable by user
                                    mode = inplace(NEOS_PY_AND, mode, py{ unary(NEOS_PY_INVERT, (I<73>()).get()) });
                                }
                                // Ensure owner can read & write
                                mode = inplace(NEOS_PY_OR, mode, I<384>());
                            }
                            else if ((member.method("isdir").truthy() || member.method("issym").truthy()))
                            {
                                // Ignore mode for directories & symlinks
                                mode = none();
                            }
                            else
                            {
                                // Reject special files
                                py_raise((G<"tarfile.SpecialFileError">())(member));
                            }
                        }
                        if (mode != member.attr("mode"))
                        {
                            new_attrs.set_item(S<"mode">(), mode);
                        }
                    }
                    if (for_data.truthy())
                    {
                        // Ignore ownership for 'data'
                        if (!member.attr("uid").is_none())
                        {
                            new_attrs.set_item(S<"uid">(), none());
                        }
                        if (!member.attr("gid").is_none())
                        {
                            new_attrs.set_item(S<"gid">(), none());
                        }
                        if (!member.attr("uname").is_none())
                        {
                            new_attrs.set_item(S<"uname">(), none());
                        }
                        if (!member.attr("gname").is_none())
                        {
                            new_attrs.set_item(S<"gname">(), none());
                        }
                        // Check link destination for 'data'
                        if ((member.method("islnk").truthy() || member.method("issym").truthy()))
                        {
                            if ((Mod<"os">().attr("path")).method("isabs", { member.attr("linkname") }).truthy())
                            {
                                py_raise((G<"tarfile.AbsoluteLinkError">())(member));
                            }
                            // A link member that resolves to the destination directory itself
                            // would replace it with a (sym)link, redirecting the destination
                            // for all subsequent members.
                            if (target_path == dest_path)
                            {
                                py_raise((G<"tarfile.OutsideDestinationError">())(member, target_path));
                            }
                            normalized = (Mod<"os">().attr("path")).method("normpath", { member.attr("linkname") });
                            if (normalized != member.attr("linkname"))
                            {
                                new_attrs.set_item(S<"linkname">(), normalized);
                            }
                            if (member.method("issym").truthy())
                            {
                                // The symlink is created at `name` with trailing separators
                                // stripped, so its target is relative to the directory
                                // containing that path.
                                link_dir = (Mod<"os">().attr("path")).method("dirname", { name.method("rstrip", { (S<"/">()) + (Mod<"os">().attr("sep")) }) });
                                target_path = (Mod<"os">().attr("path")).method("join", { dest_path, link_dir, normalized });
                            }
                            else
                            {
                                target_path = (Mod<"os">().attr("path")).method("join", { dest_path, normalized });
                            }
                            target_path = (Mod<"os">().attr("path")).method("realpath", { target_path }, { { "strict", (Mod<"os">().attr("path")).attr("ALLOW_MISSING") } });
                            if ((Mod<"os">().attr("path")).method("commonpath", { py_list({ target_path, dest_path }) }) != dest_path)
                            {
                                py_raise((G<"tarfile.LinkOutsideDestinationError">())(member, target_path));
                            }
                        }
                    }
                    return new_attrs;
                }));

            define("tarfile", "fully_trusted_filter", w.function("fully_trusted_filter", "fully_trusted_filter", { py_positional("member"), py_positional("dest_path") }, [](arguments a) -> py
                {
                    py member = a[0];
                    py dest_path = a[1];
                    return member;
                }));

            define("tarfile", "tar_filter", w.function("tar_filter", "tar_filter", { py_positional("member"), py_positional("dest_path") }, [](arguments a) -> py
                {
                    py member = a[0];
                    py dest_path = a[1];
                    py new_attrs;
                    new_attrs = (G<"tarfile._get_filtered_attrs">())(member, dest_path, boolean(false));
                    if (new_attrs.truthy())
                    {
                        return call_ex(member.attr("replace"), {  }, py{}, new_attrs, { { "deep", boolean(false) } });
                    }
                    return member;
                }));

            define("tarfile", "data_filter", w.function("data_filter", "data_filter", { py_positional("member"), py_positional("dest_path") }, [](arguments a) -> py
                {
                    py member = a[0];
                    py dest_path = a[1];
                    py new_attrs;
                    new_attrs = (G<"tarfile._get_filtered_attrs">())(member, dest_path, boolean(true));
                    if (new_attrs.truthy())
                    {
                        return call_ex(member.attr("replace"), {  }, py{}, new_attrs, { { "deep", boolean(false) } });
                    }
                    return member;
                }));

            define("tarfile", "_NAMED_FILTERS", dict_of({ { S<"fully_trusted">(), G<"tarfile.fully_trusted_filter">() }, { S<"tar">(), G<"tarfile.tar_filter">() }, { S<"data">(), G<"tarfile.data_filter">() } }));
            // ------------------
            // Exported Classes
            // ------------------
            // Sentinel for replace() defaults, meaning "don't change the attribute"
            define("tarfile", "_KEEP", (B<"object">())());
            // Header length is digits followed by a space.
            define("tarfile", "_header_length_prefix_re", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x28\x5b\x30\x2d\x39\x5d\x7b\x31\x2c\x32\x30\x7d\x29\x20", 14 }) } }));
                // --------------------------------------------------------------------------
                // The following are methods that are called depending on the type of a
                // member. The entry point is _proc_member() which can be overridden in a
                // subclass to add custom _proc_*() methods. A _proc_*() method MUST
                // implement the following
                // operations:
                // 1. Set self.offset_data to the position where the data blocks begin,
                // if there is data that follows.
                // 2. Set tarfile.offset to the position where the next member's header will
                // begin.
                // 3. Return self or another valid TarInfo object.
            define("tarfile", "TarInfo", w.make_class("TarInfo", { B<"object">() }, {
                { "__slots__", (B<"dict">()).call({  }, { { "name", S<"Name of the archive member.">() }, { "mode", S<"Permission bits.">() }, { "uid", S<"User ID of the user who originally stored this member.">() }, { "gid", S<"Group ID of the user who originally stored this member.">() }, { "size", S<"Size in bytes.">() }, { "mtime", S<"Time of last modification.">() }, { "chksum", S<"Header checksum.">() }, { "type", S<"File type.  type is usually one of these constants: REGTYPE,\nAREGTYPE, LNKTYPE, SYMTYPE, DIRTYPE, FIFOTYPE, CONTTYPE, CHRTYPE,\nBLKTYPE, GNUTYPE_SPARSE.">() }, { "linkname", S<"Name of the target file name, which is only present in TarInfo\nobjects of type LNKTYPE and SYMTYPE.">() }, { "uname", S<"User name.">() }, { "gname", S<"Group name.">() }, { "devmajor", S<"Device major number.">() }, { "devminor", S<"Device minor number.">() }, { "offset", S<"The tar header starts here.">() }, { "offset_data", S<"The file's data starts here.">() }, { "pax_headers", S<"A dictionary containing key-value pairs of an associated pax\nextended header.">() }, { "sparse", S<"Sparse member information.">() }, { "_tarfile", none() }, { "_sparse_structs", none() }, { "_link_target", none() } }) },
                { "__init__", w.method("TarInfo", "__init__", { py_positional("name", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        self.set_attr("name", name);
                        self.set_attr("mode", I<420>());
                        self.set_attr("uid", I<0>());
                        self.set_attr("gid", I<0>());
                        self.set_attr("size", I<0>());
                        self.set_attr("mtime", I<0>());
                        self.set_attr("chksum", I<0>());
                        self.set_attr("type", G<"tarfile.REGTYPE">());
                        self.set_attr("linkname", S<"">());
                        self.set_attr("uname", S<"">());
                        self.set_attr("gname", S<"">());
                        self.set_attr("devmajor", I<0>());
                        self.set_attr("devminor", I<0>());
                        self.set_attr("offset", I<0>());
                        self.set_attr("offset_data", I<0>());
                        self.set_attr("sparse", none());
                        self.set_attr("pax_headers", py_dict());
                        return none();
                    }) },
                { "tarfile", B<"property">()(w.method("TarInfo", "tarfile", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py warnings;
                        warnings = module_of("warnings");
                        warnings.method("warn", { (S<"The undocumented \"tarfile\" attribute of TarInfo objects ">()) + (S<"is deprecated and will be removed in Python 3.16">()), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                        return self.attr("_tarfile");
                    }), w.method("TarInfo", "tarfile", { py_positional("tarfile") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarfile = a[1];
                        py warnings;
                        warnings = module_of("warnings");
                        warnings.method("warn", { (S<"The undocumented \"tarfile\" attribute of TarInfo objects ">()) + (S<"is deprecated and will be removed in Python 3.16">()), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                        self.set_attr("_tarfile", tarfile);
                        return none();
                    })) },
                { "path", B<"property">()(w.method("TarInfo", "path", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("name");
                    }), w.method("TarInfo", "path", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        self.set_attr("name", name);
                        return none();
                    })) },
                { "linkpath", B<"property">()(w.method("TarInfo", "linkpath", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("linkname");
                    }), w.method("TarInfo", "linkpath", { py_positional("linkname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py linkname = a[1];
                        self.set_attr("linkname", linkname);
                        return none();
                    })) },
                { "__repr__", w.method("TarInfo", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s %r at %#x>">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.attr("name"), (B<"id">())(self) });
                    }) },
                { "replace", w.method("TarInfo", "replace", { py_keyword("name", G<"tarfile._KEEP">()), py_keyword("mtime", G<"tarfile._KEEP">()), py_keyword("mode", G<"tarfile._KEEP">()), py_keyword("linkname", G<"tarfile._KEEP">()), py_keyword("uid", G<"tarfile._KEEP">()), py_keyword("gid", G<"tarfile._KEEP">()), py_keyword("uname", G<"tarfile._KEEP">()), py_keyword("gname", G<"tarfile._KEEP">()), py_keyword("deep", boolean(true)), py_keyword("_KEEP", G<"tarfile._KEEP">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py mtime = a[2];
                        py mode = a[3];
                        py linkname = a[4];
                        py uid = a[5];
                        py gid = a[6];
                        py uname = a[7];
                        py gname = a[8];
                        py deep = a[9];
                        py _KEEP = a[10];
                        py result;
                        if (deep.truthy())
                        {
                            result = (Mod<"copy">().attr("deepcopy"))(self);
                        }
                        else
                        {
                            result = (Mod<"copy">().attr("copy"))(self);
                        }
                        if (!name.is(_KEEP))
                        {
                            result.set_attr("name", name);
                        }
                        if (!mtime.is(_KEEP))
                        {
                            result.set_attr("mtime", mtime);
                        }
                        if (!mode.is(_KEEP))
                        {
                            result.set_attr("mode", mode);
                        }
                        if (!linkname.is(_KEEP))
                        {
                            result.set_attr("linkname", linkname);
                        }
                        if (!uid.is(_KEEP))
                        {
                            result.set_attr("uid", uid);
                        }
                        if (!gid.is(_KEEP))
                        {
                            result.set_attr("gid", gid);
                        }
                        if (!uname.is(_KEEP))
                        {
                            result.set_attr("uname", uname);
                        }
                        if (!gname.is(_KEEP))
                        {
                            result.set_attr("gname", gname);
                        }
                        return result;
                    }) },
                { "get_info", w.method("TarInfo", "get_info", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info;
                        py mode;
                        if (self.attr("mode").is_none())
                        {
                            mode = none();
                        }
                        else
                        {
                            mode = self.attr("mode") & (I<4095>());
                        }
                        info = dict_of({ { S<"name">(), self.attr("name") }, { S<"mode">(), mode }, { S<"uid">(), self.attr("uid") }, { S<"gid">(), self.attr("gid") }, { S<"size">(), self.attr("size") }, { S<"mtime">(), self.attr("mtime") }, { S<"chksum">(), self.attr("chksum") }, { S<"type">(), self.attr("type") }, { S<"linkname">(), self.attr("linkname") }, { S<"uname">(), self.attr("uname") }, { S<"gname">(), self.attr("gname") }, { S<"devmajor">(), self.attr("devmajor") }, { S<"devminor">(), self.attr("devminor") } });
                        if (((info[S<"type">()] == (G<"tarfile.DIRTYPE">())) && (!info[S<"name">()].method("endswith", { S<"/">() }).truthy())))
                        {
                            {
                                py const target33 = info;
                                py const key33 = S<"name">();
                                target33.set_item(key33, inplace(NEOS_PY_ADD, target33[key33], S<"/">()));
                            }
                        }
                        return info;
                    }) },
                { "tobuf", w.method("TarInfo", "tobuf", { py_positional("format", G<"tarfile.DEFAULT_FORMAT">()), py_positional("encoding", G<"tarfile.ENCODING">()), py_positional("errors", S<"surrogateescape">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py format_ = a[1];
                        py encoding = a[2];
                        py errors = a[3];
                        py info;
                        py name;
                        py value;
                        info = self.method("get_info");
                        for (py const& item : iterate{ info.method("items") })
                        {
                            auto const items35 = unpack(item, 2);
                            name = items35[0];
                            value = items35[1];
                            if (value.is_none())
                            {
                                py_raise((B<"ValueError">())((S<"%s may not be None">()) % name));
                            }
                        }
                        if (format_ == (G<"tarfile.USTAR_FORMAT">()))
                        {
                            return self.method("create_ustar_header", { info, encoding, errors });
                        }
                        else if (format_ == (G<"tarfile.GNU_FORMAT">()))
                        {
                            return self.method("create_gnu_header", { info, encoding, errors });
                        }
                        else if (format_ == (G<"tarfile.PAX_FORMAT">()))
                        {
                            return self.method("create_pax_header", { info, encoding });
                        }
                        else
                        {
                            py_raise((B<"ValueError">())(S<"invalid format">()));
                        }
                        return none();
                    }) },
                { "create_ustar_header", w.method("TarInfo", "create_ustar_header", { py_positional("info"), py_positional("encoding"), py_positional("errors") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info = a[1];
                        py encoding = a[2];
                        py errors = a[3];
                        info.set_item(S<"magic">(), G<"tarfile.POSIX_MAGIC">());
                        if (py{ info[S<"linkname">()].method("encode", { encoding, errors }).size() } > (G<"tarfile.LENGTH_LINK">()))
                        {
                            py_raise((B<"ValueError">())(S<"linkname is too long">()));
                        }
                        if (py{ info[S<"name">()].method("encode", { encoding, errors }).size() } > (G<"tarfile.LENGTH_NAME">()))
                        {
                            auto const items36 = unpack(self.method("_posix_split_name", { info[S<"name">()], encoding, errors }), 2);
                            info.set_item(S<"prefix">(), items36[0]);
                            info.set_item(S<"name">(), items36[1]);
                        }
                        return self.method("_create_header", { info, G<"tarfile.USTAR_FORMAT">(), encoding, errors });
                    }) },
                { "create_gnu_header", w.method("TarInfo", "create_gnu_header", { py_positional("info"), py_positional("encoding"), py_positional("errors") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info = a[1];
                        py encoding = a[2];
                        py errors = a[3];
                        py buf;
                        info.set_item(S<"magic">(), G<"tarfile.GNU_MAGIC">());
                        buf = py{ make_bytes(std::string{ "", 0 }) };
                        if (py{ info[S<"linkname">()].method("encode", { encoding, errors }).size() } > (G<"tarfile.LENGTH_LINK">()))
                        {
                            buf = inplace(NEOS_PY_ADD, buf, self.method("_create_gnu_long_header", { info[S<"linkname">()], G<"tarfile.GNUTYPE_LONGLINK">(), encoding, errors }));
                        }
                        if (py{ info[S<"name">()].method("encode", { encoding, errors }).size() } > (G<"tarfile.LENGTH_NAME">()))
                        {
                            buf = inplace(NEOS_PY_ADD, buf, self.method("_create_gnu_long_header", { info[S<"name">()], G<"tarfile.GNUTYPE_LONGNAME">(), encoding, errors }));
                        }
                        return buf + self.method("_create_header", { info, G<"tarfile.GNU_FORMAT">(), encoding, errors });
                    }) },
                { "create_pax_header", w.method("TarInfo", "create_pax_header", { py_positional("info"), py_positional("encoding") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info = a[1];
                        py encoding = a[2];
                        py buf;
                        py digits;
                        py hname;
                        py length;
                        py name;
                        py needs_pax;
                        py pax_headers;
                        py val;
                        py val_int;
                        py val_is_float;
                        info.set_item(S<"magic">(), G<"tarfile.POSIX_MAGIC">());
                        pax_headers = self.attr("pax_headers").method("copy");
                        // Test string fields for values that exceed the field length or cannot
                        // be represented in ASCII encoding.
                        for (py const& item : iterate{ py_tuple({ py_tuple({ S<"name">(), S<"path">(), G<"tarfile.LENGTH_NAME">() }), py_tuple({ S<"linkname">(), S<"linkpath">(), G<"tarfile.LENGTH_LINK">() }), py_tuple({ S<"uname">(), S<"uname">(), I<32>() }), py_tuple({ S<"gname">(), S<"gname">(), I<32>() }) }) })
                        {
                            auto const items38 = unpack(item, 3);
                            name = items38[0];
                            hname = items38[1];
                            length = items38[2];
                            if (pax_headers.contains(hname))
                            {
                                // The pax header has priority.
                                continue;
                            }
                            // Try to encode the string as ASCII.
                            try
                            {
                                info[name].method("encode", { S<"ascii">(), S<"strict">() });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler39;
                                if (matches(handler39.exception, B<"UnicodeEncodeError">()))
                                {
                                    pax_headers.set_item(hname, info[name]);
                                    continue;
                                }
                                else
                                    py_reraise(handler39.exception);
                            }
                            if (py{ info[name].size() } > length)
                            {
                                pax_headers.set_item(hname, info[name]);
                            }
                        }
                        // Test number fields for values that exceed the field limit or values
                        // that like to be stored as float.
                        for (py const& item : iterate{ py_tuple({ py_tuple({ S<"uid">(), I<8>() }), py_tuple({ S<"gid">(), I<8>() }), py_tuple({ S<"size">(), I<12>() }), py_tuple({ S<"mtime">(), I<12>() }) }) })
                        {
                            auto const items41 = unpack(item, 2);
                            name = items41[0];
                            digits = items41[1];
                            needs_pax = boolean(false);
                            val = info[name];
                            val_is_float = boolean(isinstance(val, B<"float">()));
                            val_int = (val_is_float.truthy() ? (B<"round">())(val) : val);
                            if (!(((I<0>()) <= val_int) && (val_int < power((I<8>()), (digits - (I<1>()))))))
                            {
                                // Avoid overflow.
                                info.set_item(name, I<0>());
                                needs_pax = boolean(true);
                            }
                            else if (val_is_float.truthy())
                            {
                                // Put rounded value in ustar header, and full
                                // precision value in pax header.
                                info.set_item(name, val_int);
                                needs_pax = boolean(true);
                            }
                            // The existing pax header has priority.
                            if ((needs_pax.truthy() && (!pax_headers.contains(name))))
                            {
                                pax_headers.set_item(name, (B<"str">())(val));
                            }
                        }
                        // Create a pax extended header if necessary.
                        if (pax_headers.truthy())
                        {
                            buf = self.method("_create_pax_generic_header", { pax_headers, G<"tarfile.XHDTYPE">(), encoding });
                        }
                        else
                        {
                            buf = py{ make_bytes(std::string{ "", 0 }) };
                        }
                        return buf + self.method("_create_header", { info, G<"tarfile.USTAR_FORMAT">(), S<"ascii">(), S<"replace">() });
                    }) },
                { "create_pax_global_header", w.class_method("TarInfo", "create_pax_global_header", { py_positional("pax_headers") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py pax_headers = a[1];
                        return cls.method("_create_pax_generic_header", { pax_headers, G<"tarfile.XGLTYPE">(), S<"utf-8">() });
                    }) },
                { "_posix_split_name", w.method("TarInfo", "_posix_split_name", { py_positional("name"), py_positional("encoding"), py_positional("errors") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py encoding = a[2];
                        py errors = a[3];
                        py components;
                        py i;
                        py prefix;
                        components = name.method("split", { S<"/">() });
                        bool broke42 = false;
                        for (py const& item : iterate{ (B<"range">())(I<1>(), py{ components.size() }) })
                        {
                            i = item;
                            prefix = (S<"/">()).method("join", { components[B<"slice">()(none(), i, none())] });
                            name = (S<"/">()).method("join", { components[B<"slice">()(i, none(), none())] });
                            if (((py{ prefix.method("encode", { encoding, errors }).size() } <= (G<"tarfile.LENGTH_PREFIX">())) && (py{ name.method("encode", { encoding, errors }).size() } <= (G<"tarfile.LENGTH_NAME">()))))
                            {
                                broke42 = true;
                                break;
                            }
                        }
                        if (!broke42)
                        {
                            py_raise((B<"ValueError">())(S<"name is too long">()));
                        }
                        return py_tuple({ prefix, name });
                    }) },
                { "_create_header", w.static_method("TarInfo", "_create_header", { py_positional("info"), py_positional("format"), py_positional("encoding"), py_positional("errors") }, [](arguments a) -> py
                    {
                        py info = a[0];
                        py format_ = a[1];
                        py encoding = a[2];
                        py errors = a[3];
                        py buf;
                        py chksum;
                        py devmajor;
                        py devminor;
                        py filetype;
                        py has_device_fields;
                        py parts;
                        has_device_fields = boolean(py_tuple({ G<"tarfile.CHRTYPE">(), G<"tarfile.BLKTYPE">() }).contains(info.method("get", { S<"type">() })));
                        if (has_device_fields.truthy())
                        {
                            devmajor = (G<"tarfile.itn">())(info.method("get", { S<"devmajor">(), I<0>() }), I<8>(), format_);
                            devminor = (G<"tarfile.itn">())(info.method("get", { S<"devminor">(), I<0>() }), I<8>(), format_);
                        }
                        else
                        {
                            devmajor = (G<"tarfile.stn">())(S<"">(), I<8>(), encoding, errors);
                            devminor = (G<"tarfile.stn">())(S<"">(), I<8>(), encoding, errors);
                        }
                        // None values in metadata should cause ValueError.
                        // itn()/stn() do this for all fields except type.
                        filetype = info.method("get", { S<"type">(), G<"tarfile.REGTYPE">() });
                        if (filetype.is_none())
                        {
                            py_raise((B<"ValueError">())(S<"TarInfo.type must not be None">()));
                        }
                        parts = py_list({ (G<"tarfile.stn">())(info.method("get", { S<"name">(), S<"">() }), I<100>(), encoding, errors), (G<"tarfile.itn">())(info.method("get", { S<"mode">(), I<0>() }) & (I<4095>()), I<8>(), format_), (G<"tarfile.itn">())(info.method("get", { S<"uid">(), I<0>() }), I<8>(), format_), (G<"tarfile.itn">())(info.method("get", { S<"gid">(), I<0>() }), I<8>(), format_), (G<"tarfile.itn">())(info.method("get", { S<"size">(), I<0>() }), I<12>(), format_), (G<"tarfile.itn">())(info.method("get", { S<"mtime">(), I<0>() }), I<12>(), format_), py{ make_bytes(std::string{ "\x20\x20\x20\x20\x20\x20\x20\x20", 8 }) }, filetype, (G<"tarfile.stn">())(info.method("get", { S<"linkname">(), S<"">() }), I<100>(), encoding, errors), info.method("get", { S<"magic">(), G<"tarfile.POSIX_MAGIC">() }), (G<"tarfile.stn">())(info.method("get", { S<"uname">(), S<"">() }), I<32>(), encoding, errors), (G<"tarfile.stn">())(info.method("get", { S<"gname">(), S<"">() }), I<32>(), encoding, errors), devmajor, devminor, (G<"tarfile.stn">())(info.method("get", { S<"prefix">(), S<"">() }), I<155>(), encoding, errors) });
                        buf = (Mod<"struct">().attr("pack"))((S<"%ds">()) % (G<"tarfile.BLOCKSIZE">()), py{ make_bytes(std::string{ "", 0 }) }.method("join", { parts }));
                        chksum = (G<"tarfile.calc_chksums">())(buf[B<"slice">()(-(G<"tarfile.BLOCKSIZE">()), none(), none())])[I<0>()];
                        buf = (buf[B<"slice">()(none(), I<-364>(), none())] + (B<"bytes">())((S<"%06o\u0000">()) % chksum, S<"ascii">())) + buf[B<"slice">()(I<-357>(), none(), none())];
                        return buf;
                    }) },
                { "_create_payload", w.static_method("TarInfo", "_create_payload", { py_positional("payload") }, [](arguments a) -> py
                    {
                        py payload = a[0];
                        py blocks;
                        py remainder;
                        auto const items43 = unpack((B<"divmod">())(py{ payload.size() }, G<"tarfile.BLOCKSIZE">()), 2);
                        blocks = items43[0];
                        remainder = items43[1];
                        if (remainder > (I<0>()))
                        {
                            payload = inplace(NEOS_PY_ADD, payload, ((G<"tarfile.BLOCKSIZE">()) - remainder) * (G<"tarfile.NUL">()));
                        }
                        return payload;
                    }) },
                { "_create_gnu_long_header", w.class_method("TarInfo", "_create_gnu_long_header", { py_positional("name"), py_positional("type"), py_positional("encoding"), py_positional("errors") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py name = a[1];
                        py type = a[2];
                        py encoding = a[3];
                        py errors = a[4];
                        py info;
                        name = name.method("encode", { encoding, errors }) + (G<"tarfile.NUL">());
                        info = py_dict();
                        info.set_item(S<"name">(), S<"././@LongLink">());
                        info.set_item(S<"type">(), type);
                        info.set_item(S<"size">(), py{ name.size() });
                        info.set_item(S<"magic">(), G<"tarfile.GNU_MAGIC">());
                        // create extended header + name blocks.
                        return cls.method("_create_header", { info, G<"tarfile.USTAR_FORMAT">(), encoding, errors }) + cls.method("_create_payload", { name });
                    }) },
                { "_create_pax_generic_header", w.class_method("TarInfo", "_create_pax_generic_header", { py_positional("pax_headers"), py_positional("type"), py_positional("encoding") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py pax_headers = a[1];
                        py type = a[2];
                        py encoding = a[3];
                        py binary;
                        py info;
                        py keyword;
                        py l;
                        py n;
                        py p;
                        py records;
                        py value;
                        // Check if one of the fields contains surrogate characters and thereby
                        // forces hdrcharset=BINARY, see _proc_pax() for more information.
                        binary = boolean(false);
                        for (py const& item : iterate{ pax_headers.method("items") })
                        {
                            auto const items45 = unpack(item, 2);
                            keyword = items45[0];
                            value = items45[1];
                            try
                            {
                                value.method("encode", { S<"utf-8">(), S<"strict">() });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler46;
                                if (matches(handler46.exception, B<"UnicodeEncodeError">()))
                                {
                                    binary = boolean(true);
                                    break;
                                }
                                else
                                    py_reraise(handler46.exception);
                            }
                        }
                        records = py{ make_bytes(std::string{ "", 0 }) };
                        if (binary.truthy())
                        {
                            // Put the hdrcharset field at the beginning of the header.
                            records = inplace(NEOS_PY_ADD, records, py{ make_bytes(std::string{ "\x32\x31\x20\x68\x64\x72\x63\x68\x61\x72\x73\x65\x74\x3d\x42\x49\x4e\x41\x52\x59\x0a", 21 }) });
                        }
                        for (py const& item : iterate{ pax_headers.method("items") })
                        {
                            auto const items48 = unpack(item, 2);
                            keyword = items48[0];
                            value = items48[1];
                            keyword = keyword.method("encode", { S<"utf-8">() });
                            if (binary.truthy())
                            {
                                // Try to restore the original byte representation of `value'.
                                // Needless to say, that the encoding must match the string.
                                value = value.method("encode", { encoding, S<"surrogateescape">() });
                            }
                            else
                            {
                                value = value.method("encode", { S<"utf-8">() });
                            }
                            l = (py{ keyword.size() } + py{ value.size() }) + (I<3>());
                            py const value49 = I<0>();
                            n = value49;
                            p = value49;
                            while (boolean(true).truthy())
                            {
                                n = l + py{ (B<"str">())(p).size() };
                                if (n == p)
                                {
                                    break;
                                }
                                p = n;
                            }
                            records = inplace(NEOS_PY_ADD, records, (((((B<"bytes">())((B<"str">())(p), S<"ascii">()) + py{ make_bytes(std::string{ "\x20", 1 }) }) + keyword) + py{ make_bytes(std::string{ "\x3d", 1 }) }) + value) + py{ make_bytes(std::string{ "\x0a", 1 }) });
                        }
                        // We use a hardcoded "././@PaxHeader" name like star does
                        // instead of the one that POSIX recommends.
                        info = py_dict();
                        info.set_item(S<"name">(), S<"././@PaxHeader">());
                        info.set_item(S<"type">(), type);
                        info.set_item(S<"size">(), py{ records.size() });
                        info.set_item(S<"magic">(), G<"tarfile.POSIX_MAGIC">());
                        // Create pax header + record blocks.
                        return cls.method("_create_header", { info, G<"tarfile.USTAR_FORMAT">(), S<"ascii">(), S<"replace">() }) + cls.method("_create_payload", { records });
                    }) },
                { "frombuf", w.class_method("TarInfo", "frombuf", { py_positional("buf"), py_positional("encoding"), py_positional("errors") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py buf = a[1];
                        py encoding = a[2];
                        py errors = a[3];
                        return cls.method("_frombuf", { buf, encoding, errors });
                    }) },
                { "_frombuf", w.class_method("TarInfo", "_frombuf", { py_positional("buf"), py_positional("encoding"), py_positional("errors"), py_keyword("dircheck", boolean(true)) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py buf = a[1];
                        py encoding = a[2];
                        py errors = a[3];
                        py dircheck = a[4];
                        py chksum;
                        py i;
                        py isextended;
                        py numbytes;
                        py obj;
                        py offset;
                        py origsize;
                        py pos;
                        py prefix;
                        py structs;
                        if (py{ buf.size() } == (I<0>()))
                        {
                            py_raise((G<"tarfile.EmptyHeaderError">())(S<"empty header">()));
                        }
                        if (py{ buf.size() } != (G<"tarfile.BLOCKSIZE">()))
                        {
                            py_raise((G<"tarfile.TruncatedHeaderError">())(S<"truncated header">()));
                        }
                        if (buf.method("count", { G<"tarfile.NUL">() }) == (G<"tarfile.BLOCKSIZE">()))
                        {
                            py_raise((G<"tarfile.EOFHeaderError">())(S<"end of file header">()));
                        }
                        chksum = (G<"tarfile.nti">())(buf[B<"slice">()(I<148>(), I<156>(), none())]);
                        if (!(G<"tarfile.calc_chksums">())(buf).contains(chksum))
                        {
                            py_raise((G<"tarfile.InvalidHeaderError">())(S<"bad checksum">()));
                        }
                        obj = cls();
                        obj.set_attr("name", (G<"tarfile.nts">())(buf[B<"slice">()(I<0>(), I<100>(), none())], encoding, errors));
                        obj.set_attr("mode", (G<"tarfile.nti">())(buf[B<"slice">()(I<100>(), I<108>(), none())]));
                        obj.set_attr("uid", (G<"tarfile.nti">())(buf[B<"slice">()(I<108>(), I<116>(), none())]));
                        obj.set_attr("gid", (G<"tarfile.nti">())(buf[B<"slice">()(I<116>(), I<124>(), none())]));
                        obj.set_attr("size", (G<"tarfile.nti">())(buf[B<"slice">()(I<124>(), I<136>(), none())]));
                        obj.set_attr("mtime", (G<"tarfile.nti">())(buf[B<"slice">()(I<136>(), I<148>(), none())]));
                        obj.set_attr("chksum", chksum);
                        obj.set_attr("type", buf[B<"slice">()(I<156>(), I<157>(), none())]);
                        obj.set_attr("linkname", (G<"tarfile.nts">())(buf[B<"slice">()(I<157>(), I<257>(), none())], encoding, errors));
                        obj.set_attr("uname", (G<"tarfile.nts">())(buf[B<"slice">()(I<265>(), I<297>(), none())], encoding, errors));
                        obj.set_attr("gname", (G<"tarfile.nts">())(buf[B<"slice">()(I<297>(), I<329>(), none())], encoding, errors));
                        obj.set_attr("devmajor", (G<"tarfile.nti">())(buf[B<"slice">()(I<329>(), I<337>(), none())]));
                        obj.set_attr("devminor", (G<"tarfile.nti">())(buf[B<"slice">()(I<337>(), I<345>(), none())]));
                        prefix = (G<"tarfile.nts">())(buf[B<"slice">()(I<345>(), I<500>(), none())], encoding, errors);
                        // Old V7 tar format represents a directory as a regular
                        // file with a trailing slash.
                        if ((dircheck.truthy() && (obj.attr("type") == (G<"tarfile.AREGTYPE">())) && obj.attr("name").method("endswith", { S<"/">() }).truthy()))
                        {
                            obj.set_attr("type", G<"tarfile.DIRTYPE">());
                        }
                        // The old GNU sparse format occupies some of the unused
                        // space in the buffer for up to 4 sparse structures.
                        // Save them for later processing in _proc_sparse().
                        if (obj.attr("type") == (G<"tarfile.GNUTYPE_SPARSE">()))
                        {
                            pos = I<386>();
                            structs = py_list();
                            for (py const& item : iterate{ (B<"range">())(I<4>()) })
                            {
                                i = item;
                                try
                                {
                                    offset = (G<"tarfile.nti">())(buf[B<"slice">()(pos, pos + (I<12>()), none())]);
                                    numbytes = (G<"tarfile.nti">())(buf[B<"slice">()(pos + (I<12>()), pos + (I<24>()), none())]);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler52;
                                    if (matches(handler52.exception, B<"ValueError">()))
                                    {
                                        break;
                                    }
                                    else
                                        py_reraise(handler52.exception);
                                }
                                structs.method("append", { py_tuple({ offset, numbytes }) });
                                pos = inplace(NEOS_PY_ADD, pos, I<24>());
                            }
                            isextended = (B<"bool">())(buf[I<482>()]);
                            origsize = (G<"tarfile.nti">())(buf[B<"slice">()(I<483>(), I<495>(), none())]);
                            obj.set_attr("_sparse_structs", py_tuple({ structs, isextended, origsize }));
                        }
                        // Remove redundant slashes from directories.
                        if (obj.method("isdir").truthy())
                        {
                            obj.set_attr("name", obj.attr("name").method("rstrip", { S<"/">() }));
                        }
                        // Reconstruct a ustar longname.
                        if ((prefix.truthy() && (!(G<"tarfile.GNU_TYPES">()).contains(obj.attr("type")))))
                        {
                            obj.set_attr("name", (prefix + (S<"/">())) + obj.attr("name"));
                        }
                        return obj;
                    }) },
                { "fromtarfile", w.class_method("TarInfo", "fromtarfile", { py_positional("tarfile") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py tarfile = a[1];
                        return cls.method("_fromtarfile", { tarfile });
                    }) },
                { "_fromtarfile", w.class_method("TarInfo", "_fromtarfile", { py_positional("tarfile"), py_keyword("dircheck", boolean(true)) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py tarfile = a[1];
                        py dircheck = a[2];
                        py buf;
                        py obj;
                        buf = tarfile.attr("fileobj").method("read", { G<"tarfile.BLOCKSIZE">() });
                        obj = cls.method("_frombuf", { buf, tarfile.attr("encoding"), tarfile.attr("errors") }, { { "dircheck", dircheck } });
                        obj.set_attr("offset", tarfile.attr("fileobj").method("tell") - (G<"tarfile.BLOCKSIZE">()));
                        return obj.method("_proc_member", { tarfile });
                    }) },
                { "_proc_member", w.method("TarInfo", "_proc_member", { py_positional("tarfile") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarfile = a[1];
                        if (py_tuple({ G<"tarfile.GNUTYPE_LONGNAME">(), G<"tarfile.GNUTYPE_LONGLINK">() }).contains(self.attr("type")))
                        {
                            return self.method("_proc_gnulong", { tarfile });
                        }
                        else if (self.attr("type") == (G<"tarfile.GNUTYPE_SPARSE">()))
                        {
                            return self.method("_proc_sparse", { tarfile });
                        }
                        else if (py_tuple({ G<"tarfile.XHDTYPE">(), G<"tarfile.XGLTYPE">(), G<"tarfile.SOLARIS_XHDTYPE">() }).contains(self.attr("type")))
                        {
                            return self.method("_proc_pax", { tarfile });
                        }
                        else
                        {
                            return self.method("_proc_builtin", { tarfile });
                        }
                        return none();
                    }) },
                { "_proc_builtin", w.method("TarInfo", "_proc_builtin", { py_positional("tarfile") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarfile = a[1];
                        py offset;
                        self.set_attr("offset_data", tarfile.attr("fileobj").method("tell"));
                        offset = self.attr("offset_data");
                        if ((self.method("isreg").truthy() || (!(G<"tarfile.SUPPORTED_TYPES">()).contains(self.attr("type")))))
                        {
                            // Skip the following data blocks.
                            offset = inplace(NEOS_PY_ADD, offset, self.method("_block", { self.attr("size") }));
                        }
                        tarfile.set_attr("offset", offset);
                        // Patch the TarInfo object with saved global
                        // header information.
                        self.method("_apply_pax_info", { tarfile.attr("pax_headers"), tarfile.attr("encoding"), tarfile.attr("errors") });
                        // Remove redundant slashes from directories. This is to be consistent
                        // with frombuf().
                        if (self.method("isdir").truthy())
                        {
                            self.set_attr("name", self.attr("name").method("rstrip", { S<"/">() }));
                        }
                        return self;
                    }) },
                { "_proc_gnulong", w.method("TarInfo", "_proc_gnulong", { py_positional("tarfile") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarfile = a[1];
                        py buf;
                        py e;
                        py next;
                        buf = (G<"tarfile._safe_read">())(tarfile.attr("fileobj"), self.method("_block", { self.attr("size") }));
                        // Fetch the next header and process it.
                        try
                        {
                            next = self.method("_fromtarfile", { tarfile }, { { "dircheck", boolean(false) } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler53;
                            if (matches(handler53.exception, G<"tarfile.HeaderError">()))
                            {
                                e = handler53.exception;
                                fn_raise_from((G<"tarfile.SubsequentHeaderError">())((B<"str">())(e)).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler53.exception);
                        }
                        // Patch the TarInfo object from the next header with
                        // the longname information.
                        next.set_attr("offset", self.attr("offset"));
                        if (self.attr("type") == (G<"tarfile.GNUTYPE_LONGNAME">()))
                        {
                            next.set_attr("name", (G<"tarfile.nts">())(buf, tarfile.attr("encoding"), tarfile.attr("errors")));
                        }
                        else if (self.attr("type") == (G<"tarfile.GNUTYPE_LONGLINK">()))
                        {
                            next.set_attr("linkname", (G<"tarfile.nts">())(buf, tarfile.attr("encoding"), tarfile.attr("errors")));
                        }
                        // Remove redundant slashes from directories. This is to be consistent
                        // with frombuf().
                        if (next.method("isdir").truthy())
                        {
                            next.set_attr("name", next.attr("name").method("removesuffix", { S<"/">() }));
                        }
                        return next;
                    }) },
                { "_proc_sparse", w.method("TarInfo", "_proc_sparse", { py_positional("tarfile") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarfile = a[1];
                        py buf;
                        py i;
                        py isextended;
                        py numbytes;
                        py offset;
                        py origsize;
                        py pos;
                        py structs;
                        // We already collected some sparse structures in frombuf().
                        auto const items54 = unpack(self.attr("_sparse_structs"), 3);
                        structs = items54[0];
                        isextended = items54[1];
                        origsize = items54[2];
                        del_attr(self, "_sparse_structs");
                        // Collect sparse structures from extended header blocks.
                        while (isextended.truthy())
                        {
                            buf = tarfile.attr("fileobj").method("read", { G<"tarfile.BLOCKSIZE">() });
                            pos = I<0>();
                            for (py const& item : iterate{ (B<"range">())(I<21>()) })
                            {
                                i = item;
                                try
                                {
                                    offset = (G<"tarfile.nti">())(buf[B<"slice">()(pos, pos + (I<12>()), none())]);
                                    numbytes = (G<"tarfile.nti">())(buf[B<"slice">()(pos + (I<12>()), pos + (I<24>()), none())]);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler57;
                                    if (matches(handler57.exception, B<"ValueError">()))
                                    {
                                        break;
                                    }
                                    else
                                        py_reraise(handler57.exception);
                                }
                                if ((offset.truthy() && numbytes.truthy()))
                                {
                                    structs.method("append", { py_tuple({ offset, numbytes }) });
                                }
                                pos = inplace(NEOS_PY_ADD, pos, I<24>());
                            }
                            isextended = (B<"bool">())(buf[I<504>()]);
                        }
                        self.set_attr("sparse", structs);
                        self.set_attr("offset_data", tarfile.attr("fileobj").method("tell"));
                        tarfile.set_attr("offset", self.attr("offset_data") + self.method("_block", { self.attr("size") }));
                        self.set_attr("size", origsize);
                        return self;
                    }) },
                { "_proc_pax", w.method("TarInfo", "_proc_pax", { py_positional("tarfile") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarfile = a[1];
                        py buf;
                        py e;
                        py encoding;
                        py equals;
                        py header_value_end_offset;
                        py keyword;
                        py keyword_and_value;
                        py length;
                        py match;
                        py next;
                        py offset;
                        py pax_headers;
                        py pos;
                        py raw_headers;
                        py raw_keyword;
                        py raw_value;
                        py size;
                        py value;
                        // Read the header information.
                        buf = (G<"tarfile._safe_read">())(tarfile.attr("fileobj"), self.method("_block", { self.attr("size") }));
                        // A pax header stores supplemental information for either
                        // the following file (extended) or all following files
                        // (global).
                        if (self.attr("type") == (G<"tarfile.XGLTYPE">()))
                        {
                            pax_headers = tarfile.attr("pax_headers");
                        }
                        else
                        {
                            pax_headers = tarfile.attr("pax_headers").method("copy");
                        }
                        // Parse pax header information. A record looks like that:
                        // "%d %s=%s\n" % (length, keyword, value). length is the size
                        // of the complete record including the length field itself and
                        // the newline.
                        pos = I<0>();
                        encoding = none();
                        raw_headers = py_list();
                        while (((py{ buf.size() } > pos) && (buf[pos] != (I<0>()))))
                        {
                            if (!([&]() -> py { match = (G<"tarfile._header_length_prefix_re">()).method("match", { buf, pos }); return match; }()).truthy())
                            {
                                py_raise((G<"tarfile.InvalidHeaderError">())(S<"invalid header">()));
                            }
                            try
                            {
                                length = (B<"int">())(match.method("group", { I<1>() }));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler59;
                                if (matches(handler59.exception, B<"ValueError">()))
                                {
                                    py_raise((G<"tarfile.InvalidHeaderError">())(S<"invalid header">()));
                                }
                                else
                                    py_reraise(handler59.exception);
                            }
                            // Headers must be at least 5 bytes, shortest being '5 x=\n'.
                            // Value is allowed to be empty.
                            if (length < (I<5>()))
                            {
                                py_raise((G<"tarfile.InvalidHeaderError">())(S<"invalid header">()));
                            }
                            if ((pos + length) > py{ buf.size() })
                            {
                                py_raise((G<"tarfile.InvalidHeaderError">())(S<"invalid header">()));
                            }
                            header_value_end_offset = (match.method("start", { I<1>() }) + length) - (I<1>());
                            keyword_and_value = buf[B<"slice">()(match.method("end", { I<1>() }) + (I<1>()), header_value_end_offset, none())];
                            auto const items60 = unpack(keyword_and_value.method("partition", { py{ make_bytes(std::string{ "\x3d", 1 }) } }), 3);
                            raw_keyword = items60[0];
                            equals = items60[1];
                            raw_value = items60[2];
                            // Check the framing of the header. The last character must be '\n' (0x0A)
                            if (((!raw_keyword.truthy()) || (equals != py{ make_bytes(std::string{ "\x3d", 1 }) }) || (buf[header_value_end_offset] != (I<10>()))))
                            {
                                py_raise((G<"tarfile.InvalidHeaderError">())(S<"invalid header">()));
                            }
                            raw_headers.method("append", { py_tuple({ length, raw_keyword, raw_value }) });
                            // Check if the pax header contains a hdrcharset field. This tells us
                            // the encoding of the path, linkpath, uname and gname fields. Normally,
                            // these fields are UTF-8 encoded but since POSIX.1-2008 tar
                            // implementations are allowed to store them as raw binary strings if
                            // the translation to UTF-8 fails. For the time being, we don't care about
                            // anything other than "BINARY". The only other value that is currently
                            // allowed by the standard is "ISO-IR 10646 2000 UTF-8" in other words UTF-8.
                            // Note that we only follow the initial 'hdrcharset' setting to preserve
                            // the initial behavior of the 'tarfile' module.
                            if (((raw_keyword == py{ make_bytes(std::string{ "\x68\x64\x72\x63\x68\x61\x72\x73\x65\x74", 10 }) }) && encoding.is_none()))
                            {
                                if (raw_value == py{ make_bytes(std::string{ "\x42\x49\x4e\x41\x52\x59", 6 }) })
                                {
                                    encoding = tarfile.attr("encoding");
                                }
                                else
                                {
                                    encoding = S<"utf-8">();
                                }
                            }
                            pos = inplace(NEOS_PY_ADD, pos, length);
                        }
                        // If no explicit hdrcharset is set, we use UTF-8 as a default.
                        if (encoding.is_none())
                        {
                            encoding = S<"utf-8">();
                        }
                        // After parsing the raw headers we can decode them to text.
                        for (py const& item : iterate{ raw_headers })
                        {
                            auto const items62 = unpack(item, 3);
                            length = items62[0];
                            raw_keyword = items62[1];
                            raw_value = items62[2];
                            // Normally, we could just use "utf-8" as the encoding and "strict"
                            // as the error handler, but we better not take the risk. For
                            // example, GNU tar <= 1.23 is known to store filenames it cannot
                            // translate to UTF-8 as raw strings (unfortunately without a
                            // hdrcharset=BINARY header).
                            // We first try the strict standard encoding, and if that fails we
                            // fall back on the user's encoding and error handler.
                            keyword = self.method("_decode_pax_field", { raw_keyword, S<"utf-8">(), S<"utf-8">(), tarfile.attr("errors") });
                            if ((G<"tarfile.PAX_NAME_FIELDS">()).contains(keyword))
                            {
                                value = self.method("_decode_pax_field", { raw_value, encoding, tarfile.attr("encoding"), tarfile.attr("errors") });
                            }
                            else
                            {
                                value = self.method("_decode_pax_field", { raw_value, S<"utf-8">(), S<"utf-8">(), tarfile.attr("errors") });
                            }
                            pax_headers.set_item(keyword, value);
                        }
                        // Fetch the next header.
                        try
                        {
                            next = self.method("_fromtarfile", { tarfile }, { { "dircheck", boolean(false) } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler63;
                            if (matches(handler63.exception, G<"tarfile.HeaderError">()))
                            {
                                e = handler63.exception;
                                fn_raise_from((G<"tarfile.SubsequentHeaderError">())((B<"str">())(e)).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler63.exception);
                        }
                        // Process GNU sparse information.
                        if (pax_headers.contains((S<"GNU.sparse.map">())))
                        {
                            // GNU extended sparse format version 0.1.
                            self.method("_proc_gnusparse_01", { next, pax_headers });
                        }
                        else if (pax_headers.contains((S<"GNU.sparse.size">())))
                        {
                            // GNU extended sparse format version 0.0.
                            self.method("_proc_gnusparse_00", { next, raw_headers });
                        }
                        else if (((pax_headers.method("get", { S<"GNU.sparse.major">() }) == (S<"1">())) && (pax_headers.method("get", { S<"GNU.sparse.minor">() }) == (S<"0">()))))
                        {
                            // GNU extended sparse format version 1.0.
                            self.method("_proc_gnusparse_10", { next, pax_headers, tarfile });
                        }
                        if (py_tuple({ G<"tarfile.XHDTYPE">(), G<"tarfile.SOLARIS_XHDTYPE">() }).contains(self.attr("type")))
                        {
                            // Patch the TarInfo object with the extended header info.
                            next.method("_apply_pax_info", { pax_headers, tarfile.attr("encoding"), tarfile.attr("errors") });
                            if (pax_headers.contains((S<"size">())))
                            {
                                // If the extended header replaces the size field,
                                // we need to recalculate the offset where the next
                                // header starts.
                                offset = next.attr("offset") + (G<"tarfile.BLOCKSIZE">());
                                if ((next.method("isreg").truthy() || (!(G<"tarfile.SUPPORTED_TYPES">()).contains(next.attr("type")))))
                                {
                                    try
                                    {
                                        size = (G<"tarfile.PAX_NUMBER_FIELDS">())[S<"size">()](pax_headers[S<"size">()]);
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler64;
                                        if (matches(handler64.exception, B<"ValueError">()))
                                        {
                                            size = I<0>();
                                        }
                                        else
                                            py_reraise(handler64.exception);
                                    }
                                    offset = inplace(NEOS_PY_ADD, offset, next.method("_block", { size }));
                                }
                                tarfile.set_attr("offset", offset);
                            }
                            next.set_attr("offset", self.attr("offset"));
                        }
                        return next;
                    }) },
                { "_proc_gnusparse_00", w.method("TarInfo", "_proc_gnusparse_00", { py_positional("next"), py_positional("raw_headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py next = a[1];
                        py raw_headers = a[2];
                        py _;
                        py keyword;
                        py numbytes;
                        py offsets;
                        py value;
                        offsets = py_list();
                        numbytes = py_list();
                        for (py const& item : iterate{ raw_headers })
                        {
                            auto const items66 = unpack(item, 3);
                            _ = items66[0];
                            keyword = items66[1];
                            value = items66[2];
                            if (keyword == py{ make_bytes(std::string{ "\x47\x4e\x55\x2e\x73\x70\x61\x72\x73\x65\x2e\x6f\x66\x66\x73\x65\x74", 17 }) })
                            {
                                try
                                {
                                    offsets.method("append", { (B<"int">())(value.method("decode")) });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler67;
                                    if (matches(handler67.exception, B<"ValueError">()))
                                    {
                                        py_raise((G<"tarfile.InvalidHeaderError">())(S<"invalid header">()));
                                    }
                                    else
                                        py_reraise(handler67.exception);
                                }
                            }
                            else if (keyword == py{ make_bytes(std::string{ "\x47\x4e\x55\x2e\x73\x70\x61\x72\x73\x65\x2e\x6e\x75\x6d\x62\x79\x74\x65\x73", 19 }) })
                            {
                                try
                                {
                                    numbytes.method("append", { (B<"int">())(value.method("decode")) });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler68;
                                    if (matches(handler68.exception, B<"ValueError">()))
                                    {
                                        py_raise((G<"tarfile.InvalidHeaderError">())(S<"invalid header">()));
                                    }
                                    else
                                        py_reraise(handler68.exception);
                                }
                            }
                        }
                        next.set_attr("sparse", (B<"list">())((B<"zip">())(offsets, numbytes)));
                        return none();
                    }) },
                { "_proc_gnusparse_01", w.method("TarInfo", "_proc_gnusparse_01", { py_positional("next"), py_positional("pax_headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py next = a[1];
                        py pax_headers = a[2];
                        py sparse;
                        sparse = [&]() -> py { py const comprehension69 = py_list(); py x; for (py const& item : iterate{ pax_headers[S<"GNU.sparse.map">()].method("split", { S<",">() }) }) { x = item; comprehension69.method("append", { (B<"int">())(x) }); } return comprehension69; }();
                        next.set_attr("sparse", (B<"list">())((B<"zip">())(sparse[B<"slice">()(none(), none(), I<2>())], sparse[B<"slice">()(I<1>(), none(), I<2>())])));
                        return none();
                    }) },
                { "_proc_gnusparse_10", w.method("TarInfo", "_proc_gnusparse_10", { py_positional("next"), py_positional("pax_headers"), py_positional("tarfile") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py next = a[1];
                        py pax_headers = a[2];
                        py tarfile = a[3];
                        py buf;
                        py fields;
                        py number;
                        py sparse;
                        fields = none();
                        sparse = py_list();
                        buf = tarfile.attr("fileobj").method("read", { G<"tarfile.BLOCKSIZE">() });
                        auto const items70 = unpack(buf.method("split", { py{ make_bytes(std::string{ "\x0a", 1 }) }, I<1>() }), 2);
                        fields = items70[0];
                        buf = items70[1];
                        fields = (B<"int">())(fields);
                        while (py{ sparse.size() } < (fields * (I<2>())))
                        {
                            if (!buf.contains(py{ make_bytes(std::string{ "\x0a", 1 }) }))
                            {
                                buf = inplace(NEOS_PY_ADD, buf, tarfile.attr("fileobj").method("read", { G<"tarfile.BLOCKSIZE">() }));
                            }
                            auto const items72 = unpack(buf.method("split", { py{ make_bytes(std::string{ "\x0a", 1 }) }, I<1>() }), 2);
                            number = items72[0];
                            buf = items72[1];
                            sparse.method("append", { (B<"int">())(number) });
                        }
                        next.set_attr("offset_data", tarfile.attr("fileobj").method("tell"));
                        next.set_attr("sparse", (B<"list">())((B<"zip">())(sparse[B<"slice">()(none(), none(), I<2>())], sparse[B<"slice">()(I<1>(), none(), I<2>())])));
                        return none();
                    }) },
                { "_apply_pax_info", w.method("TarInfo", "_apply_pax_info", { py_positional("pax_headers"), py_positional("encoding"), py_positional("errors") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pax_headers = a[1];
                        py encoding = a[2];
                        py errors = a[3];
                        py keyword;
                        py value;
                        for (py const& item : iterate{ pax_headers.method("items") })
                        {
                            auto const items74 = unpack(item, 2);
                            keyword = items74[0];
                            value = items74[1];
                            if (keyword == (S<"GNU.sparse.name">()))
                            {
                                (B<"setattr">())(self, S<"path">(), value);
                            }
                            else if (keyword == (S<"GNU.sparse.size">()))
                            {
                                (B<"setattr">())(self, S<"size">(), (B<"int">())(value));
                            }
                            else if (keyword == (S<"GNU.sparse.realsize">()))
                            {
                                (B<"setattr">())(self, S<"size">(), (B<"int">())(value));
                            }
                            else if ((G<"tarfile.PAX_FIELDS">()).contains(keyword))
                            {
                                if ((G<"tarfile.PAX_NUMBER_FIELDS">()).contains(keyword))
                                {
                                    try
                                    {
                                        value = (G<"tarfile.PAX_NUMBER_FIELDS">())[keyword](value);
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler75;
                                        if (matches(handler75.exception, B<"ValueError">()))
                                        {
                                            value = I<0>();
                                        }
                                        else
                                            py_reraise(handler75.exception);
                                    }
                                }
                                if (keyword == (S<"path">()))
                                {
                                    value = value.method("rstrip", { S<"/">() });
                                }
                                (B<"setattr">())(self, keyword, value);
                            }
                        }
                        self.set_attr("pax_headers", pax_headers.method("copy"));
                        return none();
                    }) },
                { "_decode_pax_field", w.method("TarInfo", "_decode_pax_field", { py_positional("value"), py_positional("encoding"), py_positional("fallback_encoding"), py_positional("fallback_errors") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py encoding = a[2];
                        py fallback_encoding = a[3];
                        py fallback_errors = a[4];
                        try
                        {
                            return value.method("decode", { encoding, S<"strict">() });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler76;
                            if (matches(handler76.exception, B<"UnicodeDecodeError">()))
                            {
                                return value.method("decode", { fallback_encoding, fallback_errors });
                            }
                            else
                                py_reraise(handler76.exception);
                        }
                        return none();
                    }) },
                { "_block", w.method("TarInfo", "_block", { py_positional("count") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py count = a[1];
                        py blocks;
                        py remainder;
                        // Only non-negative offsets are allowed
                        if (count < (I<0>()))
                        {
                            py_raise((G<"tarfile.InvalidHeaderError">())(S<"invalid offset">()));
                        }
                        auto const items77 = unpack((B<"divmod">())(count, G<"tarfile.BLOCKSIZE">()), 2);
                        blocks = items77[0];
                        remainder = items77[1];
                        if (remainder.truthy())
                        {
                            blocks = inplace(NEOS_PY_ADD, blocks, I<1>());
                        }
                        return blocks * (G<"tarfile.BLOCKSIZE">());
                    }) },
                { "isreg", w.method("TarInfo", "isreg", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean((G<"tarfile.REGULAR_TYPES">()).contains(self.attr("type")));
                    }) },
                { "isfile", w.method("TarInfo", "isfile", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("isreg");
                    }) },
                { "isdir", w.method("TarInfo", "isdir", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("type") == (G<"tarfile.DIRTYPE">()));
                    }) },
                { "issym", w.method("TarInfo", "issym", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("type") == (G<"tarfile.SYMTYPE">()));
                    }) },
                { "islnk", w.method("TarInfo", "islnk", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("type") == (G<"tarfile.LNKTYPE">()));
                    }) },
                { "ischr", w.method("TarInfo", "ischr", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("type") == (G<"tarfile.CHRTYPE">()));
                    }) },
                { "isblk", w.method("TarInfo", "isblk", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("type") == (G<"tarfile.BLKTYPE">()));
                    }) },
                { "isfifo", w.method("TarInfo", "isfifo", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("type") == (G<"tarfile.FIFOTYPE">()));
                    }) },
                { "issparse", w.method("TarInfo", "issparse", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(!self.attr("sparse").is_none());
                    }) },
                { "isdev", w.method("TarInfo", "isdev", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(py_tuple({ G<"tarfile.CHRTYPE">(), G<"tarfile.BLKTYPE">(), G<"tarfile.FIFOTYPE">() }).contains(self.attr("type")));
                    }) }, }));

            // class TarInfo
                // tar file, else the link.
                // continues processing.
                // messages (if debug >= 0). If > 0, errors
                // are passed to the caller as exceptions.
                // --------------------------------------------------------------------------
                // Below are the classmethods which act as alternate constructors to the
                // TarFile class. The open() method is the only one that is needed for
                // public use; it is the "super"-constructor and is able to select an
                // adequate "sub"-constructor for a particular compression using the mapping
                // from OPEN_METH.
                // 
                // This concept allows one to subclass TarFile without losing the comfort of
                // the super-constructor. A sub-constructor is registered and made available
                // by adding it to the mapping in OPEN_METH.
                // All *open() methods are registered here.
                // --------------------------------------------------------------------------
                // The public methods which TarFile provides:
                // --------------------------------------------------------------------------
                // Below are the different file methods. They are called via
                // _extract_member() when extract() is called. They can be replaced in a
                // subclass to implement other functionality.
                // --------------------------------------------------------------------------
                // --------------------------------------------------------------------------
                // Little helper methods:
            define("tarfile", "TarFile", w.make_class("TarFile", { B<"object">() }, {
                { "debug", I<0>() },
                { "dereference", boolean(false) },
                { "ignore_zeros", boolean(false) },
                { "errorlevel", I<1>() },
                { "format", G<"tarfile.DEFAULT_FORMAT">() },
                { "encoding", G<"tarfile.ENCODING">() },
                { "errors", none() },
                { "tarinfo", G<"tarfile.TarInfo">() },
                { "fileobject", G<"tarfile.ExFileObject">() },
                { "extraction_filter", none() },
                { "__init__", w.method("TarFile", "__init__", { py_positional("name", none()), py_positional("mode", S<"r">()), py_positional("fileobj", none()), py_positional("format", none()), py_positional("tarinfo", none()), py_positional("dereference", none()), py_positional("ignore_zeros", none()), py_positional("encoding", none()), py_positional("errors", S<"surrogateescape">()), py_positional("pax_headers", none()), py_positional("debug", none()), py_positional("errorlevel", none()), py_positional("copybufsize", none()), py_positional("stream", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py mode = a[2];
                        py fileobj = a[3];
                        py format_ = a[4];
                        py tarinfo = a[5];
                        py dereference = a[6];
                        py ignore_zeros = a[7];
                        py encoding = a[8];
                        py errors = a[9];
                        py pax_headers = a[10];
                        py debug = a[11];
                        py errorlevel = a[12];
                        py copybufsize = a[13];
                        py stream = a[14];
                        py buf;
                        py e;
                        py modes;
                        modes = dict_of({ { S<"r">(), S<"rb">() }, { S<"a">(), S<"r+b">() }, { S<"w">(), S<"wb">() }, { S<"x">(), S<"xb">() } });
                        if (!modes.contains(mode))
                        {
                            py_raise((B<"ValueError">())(S<"mode must be 'r', 'a', 'w' or 'x'">()));
                        }
                        self.set_attr("mode", mode);
                        self.set_attr("_mode", modes[mode]);
                        if (!fileobj.truthy())
                        {
                            if (((self.attr("mode") == (S<"a">())) && (!(Mod<"os">().attr("path")).method("exists", { name }).truthy())))
                            {
                                // Create nonexistent files in append mode.
                                self.set_attr("mode", S<"w">());
                                self.set_attr("_mode", S<"wb">());
                            }
                            fileobj = (Mod<"builtins">().attr("open"))(name, self.attr("_mode"));
                            self.set_attr("_extfileobj", boolean(false));
                        }
                        else
                        {
                            if ((name.is_none() && has_attr(fileobj, "name") && isinstance(fileobj.attr("name"), py_tuple({ B<"str">(), B<"bytes">() }))))
                            {
                                name = fileobj.attr("name");
                            }
                            if (has_attr(fileobj, "mode"))
                            {
                                self.set_attr("_mode", fileobj.attr("mode"));
                            }
                            self.set_attr("_extfileobj", boolean(true));
                        }
                        self.set_attr("name", (name.truthy() ? (Mod<"os">().attr("path")).method("abspath", { name }) : none()));
                        self.set_attr("fileobj", fileobj);
                        self.set_attr("stream", stream);
                        // Init attributes.
                        if (!format_.is_none())
                        {
                            self.set_attr("format", format_);
                        }
                        if (!tarinfo.is_none())
                        {
                            self.set_attr("tarinfo", tarinfo);
                        }
                        if (!dereference.is_none())
                        {
                            self.set_attr("dereference", dereference);
                        }
                        if (!ignore_zeros.is_none())
                        {
                            self.set_attr("ignore_zeros", ignore_zeros);
                        }
                        if (!encoding.is_none())
                        {
                            self.set_attr("encoding", encoding);
                        }
                        self.set_attr("errors", errors);
                        if (((!pax_headers.is_none()) && (self.attr("format") == (G<"tarfile.PAX_FORMAT">()))))
                        {
                            self.set_attr("pax_headers", pax_headers);
                        }
                        else
                        {
                            self.set_attr("pax_headers", py_dict());
                        }
                        if (!debug.is_none())
                        {
                            self.set_attr("debug", debug);
                        }
                        if (!errorlevel.is_none())
                        {
                            self.set_attr("errorlevel", errorlevel);
                        }
                        // Init datastructures.
                        self.set_attr("copybufsize", copybufsize);
                        self.set_attr("closed", boolean(false));
                        self.set_attr("members", py_list());
                        self.set_attr("_loaded", boolean(false));
                        self.set_attr("offset", self.attr("fileobj").method("tell"));
                        // current position in the archive file
                        self.set_attr("inodes", py_dict());
                        // archive members already added
                        try
                        {
                            if (self.attr("mode") == (S<"r">()))
                            {
                                self.set_attr("firstmember", none());
                                self.set_attr("firstmember", self.method("next"));
                            }
                            if (self.attr("mode") == (S<"a">()))
                            {
                                // Move to the end of the archive,
                                // before the first empty block.
                                while (boolean(true).truthy())
                                {
                                    self.attr("fileobj").method("seek", { self.attr("offset") });
                                    try
                                    {
                                        tarinfo = self.attr("tarinfo").method("fromtarfile", { self });
                                        self.attr("members").method("append", { tarinfo });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler80;
                                        if (matches(handler80.exception, G<"tarfile.EOFHeaderError">()))
                                        {
                                            self.attr("fileobj").method("seek", { self.attr("offset") });
                                            break;
                                        }
                                        else if (matches(handler80.exception, G<"tarfile.HeaderError">()))
                                        {
                                            e = handler80.exception;
                                            fn_raise_from((G<"tarfile.ReadError">())((B<"str">())(e)).get(), none().get());
                                            throw raised{};
                                        }
                                        else
                                            py_reraise(handler80.exception);
                                    }
                                }
                            }
                            if (py_tuple({ S<"a">(), S<"w">(), S<"x">() }).contains(self.attr("mode")))
                            {
                                self.set_attr("_loaded", boolean(true));
                                if (self.attr("pax_headers").truthy())
                                {
                                    buf = self.attr("tarinfo").method("create_pax_global_header", { self.attr("pax_headers").method("copy") });
                                    self.attr("fileobj").method("write", { buf });
                                    {
                                        py const target81 = self;
                                        target81.set_attr("offset", inplace(NEOS_PY_ADD, target81.attr("offset"), py{ buf.size() }));
                                    }
                                }
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler78;
                            {
                                if (!self.attr("_extfileobj").truthy())
                                {
                                    self.attr("fileobj").method("close");
                                }
                                self.set_attr("closed", boolean(true));
                                py_reraise(handler78.exception);
                            }
                        }
                        return none();
                    }) },
                { "open", w.class_method("TarFile", "open", { py_positional("name", none()), py_positional("mode", S<"r">()), py_positional("fileobj", none()), py_positional("bufsize", G<"tarfile.RECORDSIZE">()) }, [](arguments a) -> py
                    {
                        auto const cls = std::make_shared<py>(a[0]);
                        py name = a[1];
                        py mode = a[2];
                        py fileobj = a[3];
                        py bufsize = a[4];
                        py kwargs = a[5];
                        py compresslevel;
                        py comptype;
                        py e;
                        py error_msgs;
                        py error_msgs_summary;
                        py filemode;
                        py func;
                        py not_compressed;
                        py saved_pos;
                        py stream;
                        py t;
                        if (((!name.truthy()) && (!fileobj.truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"nothing to open">()));
                        }
                        if (py_tuple({ S<"r">(), S<"r:*">() }).contains(mode))
                        {
                            // Find out which *open() is appropriate for opening the file.
                            not_compressed = w.function("not_compressed", "TarFile.open.<locals>.not_compressed", { py_positional("comptype") }, [cls](arguments a) -> py
        {
            py comptype = a[0];
            return boolean((*cls).attr("OPEN_METH")[comptype] == (S<"taropen">()));
        });
                            error_msgs = py_list();
                            for (py const& item : iterate{ (B<"sorted">()).call({ (*cls).attr("OPEN_METH") }, { { "key", not_compressed } }) })
                            {
                                comptype = item;
                                func = (B<"getattr">())((*cls), (*cls).attr("OPEN_METH")[comptype]);
                                if (!fileobj.is_none())
                                {
                                    saved_pos = fileobj.method("tell");
                                }
                                try
                                {
                                    return call_ex(func, { name, S<"r">(), fileobj }, py{}, kwargs);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler83;
                                    if (matches(handler83.exception, py_tuple({ G<"tarfile.ReadError">(), G<"tarfile.CompressionError">() })))
                                    {
                                        e = handler83.exception;
                                        error_msgs.method("append", { S(std::string{ "- method " } + comptype.str() + ": " + e.repr()) });
                                        if (!fileobj.is_none())
                                        {
                                            fileobj.method("seek", { saved_pos });
                                        }
                                        continue;
                                    }
                                    else
                                        py_reraise(handler83.exception);
                                }
                            }
                            error_msgs_summary = (S<"\n">()).method("join", { error_msgs });
                            py_raise((G<"tarfile.ReadError">())(S(std::string{ "file could not be opened successfully:\n" } + error_msgs_summary.str())));
                        }
                        else if (mode.contains((S<":">())))
                        {
                            auto const items84 = unpack(mode.method("split", { S<":">(), I<1>() }), 2);
                            filemode = items84[0];
                            comptype = items84[1];
                            filemode = [&]() -> py { py const t = filemode; return t.truthy() ? t : S<"r">(); }();
                            comptype = [&]() -> py { py const t = comptype; return t.truthy() ? t : S<"tar">(); }();
                            // Select the *open() function according to
                            // given compression.
                            if ((*cls).attr("OPEN_METH").contains(comptype))
                            {
                                func = (B<"getattr">())((*cls), (*cls).attr("OPEN_METH")[comptype]);
                            }
                            else
                            {
                                py_raise((G<"tarfile.CompressionError">())((S<"unknown compression type %r">()) % comptype));
                            }
                            return call_ex(func, { name, filemode, fileobj }, py{}, kwargs);
                        }
                        else if (mode.contains((S<"|">())))
                        {
                            auto const items85 = unpack(mode.method("split", { S<"|">(), I<1>() }), 2);
                            filemode = items85[0];
                            comptype = items85[1];
                            filemode = [&]() -> py { py const t = filemode; return t.truthy() ? t : S<"r">(); }();
                            comptype = [&]() -> py { py const t = comptype; return t.truthy() ? t : S<"tar">(); }();
                            if (!py_tuple({ S<"r">(), S<"w">() }).contains(filemode))
                            {
                                py_raise((B<"ValueError">())(S<"mode must be 'r' or 'w'">()));
                            }
                            compresslevel = kwargs.method("pop", { S<"compresslevel">(), I<9>() });
                            stream = (G<"tarfile._Stream">())(name, filemode, comptype, fileobj, bufsize, compresslevel);
                            try
                            {
                                t = call_ex((*cls), { name, filemode, stream }, py{}, kwargs);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler86;
                                {
                                    stream.method("close");
                                    py_reraise(handler86.exception);
                                }
                            }
                            t.set_attr("_extfileobj", boolean(false));
                            return t;
                        }
                        else if (py_tuple({ S<"a">(), S<"w">(), S<"x">() }).contains(mode))
                        {
                            return call_ex((*cls).attr("taropen"), { name, mode, fileobj }, py{}, kwargs);
                        }
                        py_raise((B<"ValueError">())(S<"undiscernible mode">()));
                    }, false, true) },
                { "taropen", w.class_method("TarFile", "taropen", { py_positional("name"), py_positional("mode", S<"r">()), py_positional("fileobj", none()) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py name = a[1];
                        py mode = a[2];
                        py fileobj = a[3];
                        py kwargs = a[4];
                        if (!py_tuple({ S<"r">(), S<"a">(), S<"w">(), S<"x">() }).contains(mode))
                        {
                            py_raise((B<"ValueError">())(S<"mode must be 'r', 'a', 'w' or 'x'">()));
                        }
                        return call_ex(cls, { name, mode, fileobj }, py{}, kwargs);
                    }, false, true) },
                { "gzopen", w.class_method("TarFile", "gzopen", { py_positional("name"), py_positional("mode", S<"r">()), py_positional("fileobj", none()), py_positional("compresslevel", I<9>()) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py name = a[1];
                        py mode = a[2];
                        py fileobj = a[3];
                        py compresslevel = a[4];
                        py kwargs = a[5];
                        py GzipFile;
                        py e;
                        py t;
                        if (!py_tuple({ S<"r">(), S<"w">(), S<"x">() }).contains(mode))
                        {
                            py_raise((B<"ValueError">())(S<"mode must be 'r', 'w' or 'x'">()));
                        }
                        try
                        {
                            GzipFile = M("gzip", "GzipFile");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler87;
                            if (matches(handler87.exception, B<"ImportError">()))
                            {
                                fn_raise_from((G<"tarfile.CompressionError">())(S<"gzip module is not available">()).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler87.exception);
                        }
                        try
                        {
                            fileobj = GzipFile(name, mode + (S<"b">()), compresslevel, fileobj);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler88;
                            if (matches(handler88.exception, B<"OSError">()))
                            {
                                e = handler88.exception;
                                if (((!fileobj.is_none()) && (mode == (S<"r">()))))
                                {
                                    fn_raise_from((G<"tarfile.ReadError">())(S<"not a gzip file">()).get(), e.get());
                                    throw raised{};
                                }
                                py_reraise(handler88.exception);
                            }
                            else
                                py_reraise(handler88.exception);
                        }
                        try
                        {
                            t = call_ex(cls.attr("taropen"), { name, mode, fileobj }, py{}, kwargs);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler89;
                            if (matches(handler89.exception, B<"OSError">()))
                            {
                                e = handler89.exception;
                                fileobj.method("close");
                                if (mode == (S<"r">()))
                                {
                                    fn_raise_from((G<"tarfile.ReadError">())(S<"not a gzip file">()).get(), e.get());
                                    throw raised{};
                                }
                                py_reraise(handler89.exception);
                            }
                            else
                            {
                                fileobj.method("close");
                                py_reraise(handler89.exception);
                            }
                        }
                        t.set_attr("_extfileobj", boolean(false));
                        return t;
                    }, false, true) },
                { "bz2open", w.class_method("TarFile", "bz2open", { py_positional("name"), py_positional("mode", S<"r">()), py_positional("fileobj", none()), py_positional("compresslevel", I<9>()) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py name = a[1];
                        py mode = a[2];
                        py fileobj = a[3];
                        py compresslevel = a[4];
                        py kwargs = a[5];
                        py BZ2File;
                        py e;
                        py t;
                        if (!py_tuple({ S<"r">(), S<"w">(), S<"x">() }).contains(mode))
                        {
                            py_raise((B<"ValueError">())(S<"mode must be 'r', 'w' or 'x'">()));
                        }
                        try
                        {
                            BZ2File = M("bz2", "BZ2File");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler90;
                            if (matches(handler90.exception, B<"ImportError">()))
                            {
                                fn_raise_from((G<"tarfile.CompressionError">())(S<"bz2 module is not available">()).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler90.exception);
                        }
                        fileobj = BZ2File.call({ [&]() -> py { py const t = fileobj; return t.truthy() ? t : name; }(), mode }, { { "compresslevel", compresslevel } });
                        try
                        {
                            t = call_ex(cls.attr("taropen"), { name, mode, fileobj }, py{}, kwargs);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler91;
                            if (matches(handler91.exception, py_tuple({ B<"OSError">(), B<"EOFError">() })))
                            {
                                e = handler91.exception;
                                fileobj.method("close");
                                if (mode == (S<"r">()))
                                {
                                    fn_raise_from((G<"tarfile.ReadError">())(S<"not a bzip2 file">()).get(), e.get());
                                    throw raised{};
                                }
                                py_reraise(handler91.exception);
                            }
                            else
                            {
                                fileobj.method("close");
                                py_reraise(handler91.exception);
                            }
                        }
                        t.set_attr("_extfileobj", boolean(false));
                        return t;
                    }, false, true) },
                { "xzopen", w.class_method("TarFile", "xzopen", { py_positional("name"), py_positional("mode", S<"r">()), py_positional("fileobj", none()), py_positional("preset", none()) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py name = a[1];
                        py mode = a[2];
                        py fileobj = a[3];
                        py preset = a[4];
                        py kwargs = a[5];
                        py LZMAError;
                        py LZMAFile;
                        py e;
                        py t;
                        if (!py_tuple({ S<"r">(), S<"w">(), S<"x">() }).contains(mode))
                        {
                            py_raise((B<"ValueError">())(S<"mode must be 'r', 'w' or 'x'">()));
                        }
                        try
                        {
                            LZMAFile = M("lzma", "LZMAFile");
                            LZMAError = M("lzma", "LZMAError");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler92;
                            if (matches(handler92.exception, B<"ImportError">()))
                            {
                                fn_raise_from((G<"tarfile.CompressionError">())(S<"lzma module is not available">()).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler92.exception);
                        }
                        fileobj = LZMAFile.call({ [&]() -> py { py const t = fileobj; return t.truthy() ? t : name; }(), mode }, { { "preset", preset } });
                        try
                        {
                            t = call_ex(cls.attr("taropen"), { name, mode, fileobj }, py{}, kwargs);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler93;
                            if (matches(handler93.exception, py_tuple({ LZMAError, B<"EOFError">() })))
                            {
                                e = handler93.exception;
                                fileobj.method("close");
                                if (mode == (S<"r">()))
                                {
                                    fn_raise_from((G<"tarfile.ReadError">())(S<"not an lzma file">()).get(), e.get());
                                    throw raised{};
                                }
                                py_reraise(handler93.exception);
                            }
                            else
                            {
                                fileobj.method("close");
                                py_reraise(handler93.exception);
                            }
                        }
                        t.set_attr("_extfileobj", boolean(false));
                        return t;
                    }, false, true) },
                { "OPEN_METH", dict_of({ { S<"tar">(), S<"taropen">() }, { S<"gz">(), S<"gzopen">() }, { S<"bz2">(), S<"bz2open">() }, { S<"xz">(), S<"xzopen">() } }) },
                { "close", w.method("TarFile", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py blocks;
                        py remainder;
                        if (self.attr("closed").truthy())
                        {
                            return none();
                        }
                        self.set_attr("closed", boolean(true));
                        {
                            std::optional<py> pending94;
                            [[maybe_unused]] int flow94 = 0;
                            [[maybe_unused]] py result94;
                            try
                            {
                                if (py_tuple({ S<"a">(), S<"w">(), S<"x">() }).contains(self.attr("mode")))
                                {
                                    self.attr("fileobj").method("write", { (G<"tarfile.NUL">()) * ((G<"tarfile.BLOCKSIZE">()) * (I<2>())) });
                                    {
                                        py const target95 = self;
                                        target95.set_attr("offset", inplace(NEOS_PY_ADD, target95.attr("offset"), (G<"tarfile.BLOCKSIZE">()) * (I<2>())));
                                    }
                                    // fill up the end with zero-blocks
                                    // (like option -b20 for tar does)
                                    auto const items96 = unpack((B<"divmod">())(self.attr("offset"), G<"tarfile.RECORDSIZE">()), 2);
                                    blocks = items96[0];
                                    remainder = items96[1];
                                    if (remainder > (I<0>()))
                                    {
                                        self.attr("fileobj").method("write", { (G<"tarfile.NUL">()) * ((G<"tarfile.RECORDSIZE">()) - remainder) });
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending94 = py_take_exception();
                            }
                            finally94:
                            if (!self.attr("_extfileobj").truthy())
                            {
                                self.attr("fileobj").method("close");
                            }
                            if (pending94)
                                py_reraise(*pending94);
                            if (flow94 == 1)
                                return result94;
                        }
                        return none();
                    }) },
                { "getmember", w.method("TarFile", "getmember", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py tarinfo;
                        tarinfo = self.method("_getmember", { name.method("rstrip", { S<"/">() }) });
                        if (tarinfo.is_none())
                        {
                            py_raise((B<"KeyError">())((S<"filename %r not found">()) % name));
                        }
                        return tarinfo;
                    }) },
                { "getmembers", w.method("TarFile", "getmembers", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_check");
                        if (!self.attr("_loaded").truthy())
                        {
                            self.method("_load");
                        }
                        // scan the whole archive.
                        return self.attr("members");
                    }) },
                { "getnames", w.method("TarFile", "getnames", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension97 = py_list(); py tarinfo; for (py const& item : iterate{ self.method("getmembers") }) { tarinfo = item; comprehension97.method("append", { tarinfo.attr("name") }); } return comprehension97; }();
                    }) },
                { "gettarinfo", w.method("TarFile", "gettarinfo", { py_positional("name", none()), py_positional("arcname", none()), py_positional("fileobj", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py arcname = a[2];
                        py fileobj = a[3];
                        py drv;
                        py inode;
                        py linkname;
                        py statres;
                        py stmd;
                        py tarinfo;
                        py type;
                        self.method("_check", { S<"awx">() });
                        // When fileobj is given, replace name by
                        // fileobj's real name.
                        if (!fileobj.is_none())
                        {
                            name = fileobj.attr("name");
                        }
                        // Building the name of the member in the archive.
                        // Backward slashes are converted to forward slashes,
                        // Absolute paths are turned to relative paths.
                        if (arcname.is_none())
                        {
                            arcname = name;
                        }
                        auto const items98 = unpack((Mod<"os">().attr("path")).method("splitdrive", { arcname }), 2);
                        drv = items98[0];
                        arcname = items98[1];
                        arcname = arcname.method("replace", { Mod<"os">().attr("sep"), S<"/">() });
                        arcname = arcname.method("lstrip", { S<"/">() });
                        // Now, fill the TarInfo object with
                        // information specific for the file.
                        tarinfo = self.method("tarinfo");
                        tarinfo.set_attr("_tarfile", self);
                        // Use os.stat or os.lstat, depending on if symlinks shall be resolved.
                        if (fileobj.is_none())
                        {
                            if (!self.attr("dereference").truthy())
                            {
                                statres = (Mod<"os">().attr("lstat"))(name);
                            }
                            else
                            {
                                statres = (Mod<"os">().attr("stat"))(name);
                            }
                        }
                        else
                        {
                            statres = (Mod<"os">().attr("fstat"))(fileobj.method("fileno"));
                        }
                        linkname = S<"">();
                        stmd = statres.attr("st_mode");
                        if ((Mod<"stat">().attr("S_ISREG"))(stmd).truthy())
                        {
                            inode = py_tuple({ statres.attr("st_ino"), statres.attr("st_dev") });
                            if (((!self.attr("dereference").truthy()) && (statres.attr("st_nlink") > (I<1>())) && self.attr("inodes").contains(inode) && (arcname != self.attr("inodes")[inode])))
                            {
                                // Is it a hardlink to an already
                                // archived file?
                                type = G<"tarfile.LNKTYPE">();
                                linkname = self.attr("inodes")[inode];
                            }
                            else
                            {
                                // The inode is added only if its valid.
                                // For win32 it is always 0.
                                type = G<"tarfile.REGTYPE">();
                                if (inode[I<0>()].truthy())
                                {
                                    self.attr("inodes").set_item(inode, arcname);
                                }
                            }
                        }
                        else if ((Mod<"stat">().attr("S_ISDIR"))(stmd).truthy())
                        {
                            type = G<"tarfile.DIRTYPE">();
                        }
                        else if ((Mod<"stat">().attr("S_ISFIFO"))(stmd).truthy())
                        {
                            type = G<"tarfile.FIFOTYPE">();
                        }
                        else if ((Mod<"stat">().attr("S_ISLNK"))(stmd).truthy())
                        {
                            type = G<"tarfile.SYMTYPE">();
                            linkname = (Mod<"os">().attr("readlink"))(name);
                        }
                        else if ((Mod<"stat">().attr("S_ISCHR"))(stmd).truthy())
                        {
                            type = G<"tarfile.CHRTYPE">();
                        }
                        else if ((Mod<"stat">().attr("S_ISBLK"))(stmd).truthy())
                        {
                            type = G<"tarfile.BLKTYPE">();
                        }
                        else
                        {
                            return none();
                        }
                        // Fill the TarInfo object with all
                        // information we can get.
                        tarinfo.set_attr("name", arcname);
                        tarinfo.set_attr("mode", stmd);
                        tarinfo.set_attr("uid", statres.attr("st_uid"));
                        tarinfo.set_attr("gid", statres.attr("st_gid"));
                        if (type == (G<"tarfile.REGTYPE">()))
                        {
                            tarinfo.set_attr("size", statres.attr("st_size"));
                        }
                        else
                        {
                            tarinfo.set_attr("size", I<0>());
                        }
                        tarinfo.set_attr("mtime", statres.attr("st_mtime"));
                        tarinfo.set_attr("type", type);
                        tarinfo.set_attr("linkname", linkname);
                        if ((Mod<"pwd">()).truthy())
                        {
                            try
                            {
                                tarinfo.set_attr("uname", (Mod<"pwd">().attr("getpwuid"))(tarinfo.attr("uid"))[I<0>()]);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler99;
                                if (matches(handler99.exception, B<"KeyError">()))
                                {
                                }
                                else
                                    py_reraise(handler99.exception);
                            }
                        }
                        if ((Mod<"grp">()).truthy())
                        {
                            try
                            {
                                tarinfo.set_attr("gname", (Mod<"grp">().attr("getgrgid"))(tarinfo.attr("gid"))[I<0>()]);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler100;
                                if (matches(handler100.exception, B<"KeyError">()))
                                {
                                }
                                else
                                    py_reraise(handler100.exception);
                            }
                        }
                        if (py_tuple({ G<"tarfile.CHRTYPE">(), G<"tarfile.BLKTYPE">() }).contains(type))
                        {
                            if ((has_attr(Mod<"os">(), "major") && has_attr(Mod<"os">(), "minor")))
                            {
                                tarinfo.set_attr("devmajor", (Mod<"os">().attr("major"))(statres.attr("st_rdev")));
                                tarinfo.set_attr("devminor", (Mod<"os">().attr("minor"))(statres.attr("st_rdev")));
                            }
                        }
                        return tarinfo;
                    }) },
                { "list", w.method("TarFile", "list", { py_positional("verbose", boolean(true)), py_keyword("members", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py verbose = a[1];
                        py members = a[2];
                        py modetype;
                        py tarinfo;
                        py type2mode;
                        // Convert tarinfo type to stat type.
                        type2mode = dict_of({ { G<"tarfile.REGTYPE">(), Mod<"stat">().attr("S_IFREG") }, { G<"tarfile.SYMTYPE">(), Mod<"stat">().attr("S_IFLNK") }, { G<"tarfile.FIFOTYPE">(), Mod<"stat">().attr("S_IFIFO") }, { G<"tarfile.CHRTYPE">(), Mod<"stat">().attr("S_IFCHR") }, { G<"tarfile.DIRTYPE">(), Mod<"stat">().attr("S_IFDIR") }, { G<"tarfile.BLKTYPE">(), Mod<"stat">().attr("S_IFBLK") } });
                        self.method("_check");
                        if (members.is_none())
                        {
                            members = self;
                        }
                        for (py const& item : iterate{ members })
                        {
                            tarinfo = item;
                            if (verbose.truthy())
                            {
                                if (tarinfo.attr("mode").is_none())
                                {
                                    (G<"tarfile._safe_print">())(S<"?\?\?\?\?\?\?\?\?\?">());
                                }
                                else
                                {
                                    modetype = type2mode.method("get", { tarinfo.attr("type"), I<0>() });
                                    (G<"tarfile._safe_print">())((Mod<"stat">().attr("filemode"))(modetype | tarinfo.attr("mode")));
                                }
                                (G<"tarfile._safe_print">())((S<"%s/%s">()) % py_tuple({ [&]() -> py { py const t = tarinfo.attr("uname"); return t.truthy() ? t : tarinfo.attr("uid"); }(), [&]() -> py { py const t = tarinfo.attr("gname"); return t.truthy() ? t : tarinfo.attr("gid"); }() }));
                                if ((tarinfo.method("ischr").truthy() || tarinfo.method("isblk").truthy()))
                                {
                                    (G<"tarfile._safe_print">())((S<"%10s">()) % ((S<"%d,%d">()) % py_tuple({ tarinfo.attr("devmajor"), tarinfo.attr("devminor") })));
                                }
                                else
                                {
                                    (G<"tarfile._safe_print">())((S<"%10d">()) % tarinfo.attr("size"));
                                }
                                if (tarinfo.attr("mtime").is_none())
                                {
                                    (G<"tarfile._safe_print">())(S<"?\?\?\?-?\?-?\? ?\?:?\?:?\?">());
                                }
                                else
                                {
                                    (G<"tarfile._safe_print">())((S<"%d-%02d-%02d %02d:%02d:%02d">()) % (Mod<"time">().attr("localtime"))(tarinfo.attr("mtime"))[B<"slice">()(none(), I<6>(), none())]);
                                }
                            }
                            (G<"tarfile._safe_print">())(tarinfo.attr("name") + (tarinfo.method("isdir").truthy() ? S<"/">() : S<"">()));
                            if (verbose.truthy())
                            {
                                if (tarinfo.method("issym").truthy())
                                {
                                    (G<"tarfile._safe_print">())((S<"-> ">()) + tarinfo.attr("linkname"));
                                }
                                if (tarinfo.method("islnk").truthy())
                                {
                                    (G<"tarfile._safe_print">())((S<"link to ">()) + tarinfo.attr("linkname"));
                                }
                            }
                            (B<"print">())();
                        }
                        return none();
                    }) },
                { "add", w.method("TarFile", "add", { py_positional("name"), py_positional("arcname", none()), py_positional("recursive", boolean(true)), py_keyword("filter", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py arcname = a[2];
                        py recursive = a[3];
                        py filter = a[4];
                        py f;
                        py tarinfo;
                        self.method("_check", { S<"awx">() });
                        if (arcname.is_none())
                        {
                            arcname = name;
                        }
                        // Skip if somebody tries to archive the archive...
                        if (((!self.attr("name").is_none()) && ((Mod<"os">().attr("path")).method("abspath", { name }) == self.attr("name"))))
                        {
                            self.method("_dbg", { I<2>(), (S<"tarfile: Skipped %r">()) % name });
                            return none();
                        }
                        self.method("_dbg", { I<1>(), name });
                        // Create a TarInfo object from the file.
                        tarinfo = self.method("gettarinfo", { name, arcname });
                        if (tarinfo.is_none())
                        {
                            self.method("_dbg", { I<1>(), (S<"tarfile: Unsupported type %r">()) % name });
                            return none();
                        }
                        // Change or exclude the TarInfo object.
                        if (!filter.is_none())
                        {
                            tarinfo = filter(tarinfo);
                            if (tarinfo.is_none())
                            {
                                self.method("_dbg", { I<2>(), (S<"tarfile: Excluded %r">()) % name });
                                return none();
                            }
                        }
                        // Append the tar header and data to the archive.
                        if (tarinfo.method("isreg").truthy())
                        {
                            {
                                py const manager102 = (Mod<"builtins">().attr("open"))(name, S<"rb">());
                                py const exit102 = pyclass::special_method(manager102, "__exit__");
                                py const value102 = pyclass::special_method(manager102, "__enter__")(manager102);
                                f = value102;
                                [[maybe_unused]] int flow102 = 0;
                                [[maybe_unused]] py result102;
                                bool exited102 = false;
                                try
                                {
                                    self.method("addfile", { tarinfo, f });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler102;
                                    exited102 = true;
                                    if (!exit102(manager102, type_of(handler102.exception), handler102.exception, handler102.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler102.exception);
                                }
                                finally102:
                                if (!exited102)
                                    exit102(manager102, none(), none(), none());
                                if (flow102 == 1)
                                    return result102;
                            }
                        }
                        else if (tarinfo.method("isdir").truthy())
                        {
                            self.method("addfile", { tarinfo });
                            if (recursive.truthy())
                            {
                                for (py const& item : iterate{ (B<"sorted">())((Mod<"os">().attr("listdir"))(name)) })
                                {
                                    f = item;
                                    self.method("add", { (Mod<"os">().attr("path")).method("join", { name, f }), (Mod<"os">().attr("path")).method("join", { arcname, f }), recursive }, { { "filter", filter } });
                                }
                            }
                        }
                        else
                        {
                            self.method("addfile", { tarinfo });
                        }
                        return none();
                    }) },
                { "addfile", w.method("TarFile", "addfile", { py_positional("tarinfo"), py_positional("fileobj", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py fileobj = a[2];
                        py blocks;
                        py buf;
                        py bufsize;
                        py remainder;
                        self.method("_check", { S<"awx">() });
                        if ((fileobj.is_none() && tarinfo.method("isreg").truthy() && (tarinfo.attr("size") != (I<0>()))))
                        {
                            py_raise((B<"ValueError">())(S<"fileobj not provided for non zero-size regular file">()));
                        }
                        tarinfo = (Mod<"copy">().attr("copy"))(tarinfo);
                        buf = tarinfo.method("tobuf", { self.attr("format"), self.attr("encoding"), self.attr("errors") });
                        self.attr("fileobj").method("write", { buf });
                        {
                            py const target104 = self;
                            target104.set_attr("offset", inplace(NEOS_PY_ADD, target104.attr("offset"), py{ buf.size() }));
                        }
                        bufsize = self.attr("copybufsize");
                        // If there's data to follow, append it.
                        if (!fileobj.is_none())
                        {
                            (G<"tarfile.copyfileobj">()).call({ fileobj, self.attr("fileobj"), tarinfo.attr("size") }, { { "bufsize", bufsize } });
                            auto const items105 = unpack((B<"divmod">())(tarinfo.attr("size"), G<"tarfile.BLOCKSIZE">()), 2);
                            blocks = items105[0];
                            remainder = items105[1];
                            if (remainder > (I<0>()))
                            {
                                self.attr("fileobj").method("write", { (G<"tarfile.NUL">()) * ((G<"tarfile.BLOCKSIZE">()) - remainder) });
                                blocks = inplace(NEOS_PY_ADD, blocks, I<1>());
                            }
                            {
                                py const target106 = self;
                                target106.set_attr("offset", inplace(NEOS_PY_ADD, target106.attr("offset"), blocks * (G<"tarfile.BLOCKSIZE">())));
                            }
                        }
                        self.attr("members").method("append", { tarinfo });
                        return none();
                    }) },
                { "_get_filter_function", w.method("TarFile", "_get_filter_function", { py_positional("filter") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filter = a[1];
                        py warnings;
                        if (filter.is_none())
                        {
                            filter = self.attr("extraction_filter");
                            if (filter.is_none())
                            {
                                warnings = module_of("warnings");
                                warnings.method("warn", { ((S<"Python 3.14 will, by default, filter extracted tar ">()) + (S<"archives and reject files or modify their metadata. ">())) + (S<"Use the filter argument to control this behavior.">()), B<"DeprecationWarning">() }, { { "stacklevel", I<2>() } });
                                return G<"tarfile.fully_trusted_filter">();
                            }
                            if (isinstance(filter, B<"str">()))
                            {
                                py_raise((B<"TypeError">())(((S<"String names are not supported for ">()) + (S<"TarFile.extraction_filter. Use a function such as ">())) + (S<"tarfile.data_filter directly.">())));
                            }
                            return filter;
                        }
                        if (callable(filter))
                        {
                            return filter;
                        }
                        try
                        {
                            return (G<"tarfile._NAMED_FILTERS">())[filter];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler107;
                            if (matches(handler107.exception, B<"KeyError">()))
                            {
                                fn_raise_from((B<"ValueError">())(S(std::string{ "filter " } + filter.repr() + " not found")).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler107.exception);
                        }
                        return none();
                    }) },
                { "extractall", w.method("TarFile", "extractall", { py_positional("path", S<".">()), py_positional("members", none()), py_keyword("numeric_owner", boolean(false)), py_keyword("filter", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py members = a[2];
                        py numeric_owner = a[3];
                        py filter = a[4];
                        py directories;
                        py dirpath;
                        py e;
                        py exc;
                        py filter_function;
                        py lstat;
                        py member;
                        py tarinfo;
                        py unfiltered;
                        directories = py_list();
                        filter_function = self.method("_get_filter_function", { filter });
                        if (members.is_none())
                        {
                            members = self;
                        }
                        for (py const& item : iterate{ members })
                        {
                            member = item;
                            auto const items109 = unpack(self.method("_get_extract_tarinfo", { member, filter_function, path }), 2);
                            tarinfo = items109[0];
                            unfiltered = items109[1];
                            if (tarinfo.is_none())
                            {
                                continue;
                            }
                            if (tarinfo.method("isdir").truthy())
                            {
                                // For directories, delay setting attributes until later,
                                // since permissions can interfere with extraction and
                                // extracting contents can reset mtime.
                                directories.method("append", { unfiltered });
                            }
                            self.method("_extract_one", { tarinfo, path }, { { "set_attrs", boolean(!tarinfo.method("isdir").truthy()) }, { "numeric_owner", numeric_owner }, { "filter_function", filter_function } });
                        }
                        // Reverse sort directories.
                        directories.method("sort", {  }, { { "key", w.function("<lambda>", "TarFile.extractall.<locals>.<lambda>", { py_positional("a") }, [](arguments a) -> py
    {
        py a_ = a[0];
        return a_.attr("name");
    }) }, { "reverse", boolean(true) } });
                        // Set correct owner, mtime and filemode on directories.
                        for (py const& item : iterate{ directories })
                        {
                            unfiltered = item;
                            try
                            {
                                // Need to re-apply any filter, to take the *current* filesystem
                                // state into account.
                                try
                                {
                                    tarinfo = filter_function(unfiltered, path);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler112;
                                    if (matches(handler112.exception, G<"tarfile._FILTER_ERRORS">()))
                                    {
                                        exc = handler112.exception;
                                        self.method("_log_no_directory_fixup", { unfiltered, S(exc.repr()) });
                                        continue;
                                    }
                                    else
                                        py_reraise(handler112.exception);
                                }
                                if (tarinfo.is_none())
                                {
                                    self.method("_log_no_directory_fixup", { unfiltered, S<"excluded by filter">() });
                                    continue;
                                }
                                dirpath = (Mod<"os">().attr("path")).method("join", { path, tarinfo.attr("name") });
                                try
                                {
                                    lstat = (Mod<"os">().attr("lstat"))(dirpath);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler113;
                                    if (matches(handler113.exception, B<"FileNotFoundError">()))
                                    {
                                        self.method("_log_no_directory_fixup", { tarinfo, S<"missing">() });
                                        continue;
                                    }
                                    else
                                        py_reraise(handler113.exception);
                                }
                                if (!(Mod<"stat">().attr("S_ISDIR"))(lstat.attr("st_mode")).truthy())
                                {
                                    // This is no longer a directory; presumably a later
                                    // member overwrote the entry.
                                    self.method("_log_no_directory_fixup", { tarinfo, S<"not a directory">() });
                                    continue;
                                }
                                self.method("chown", { tarinfo, dirpath }, { { "numeric_owner", numeric_owner } });
                                self.method("utime", { tarinfo, dirpath });
                                self.method("chmod", { tarinfo, dirpath });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler111;
                                if (matches(handler111.exception, G<"tarfile.ExtractError">()))
                                {
                                    e = handler111.exception;
                                    self.method("_handle_nonfatal_error", { e });
                                }
                                else
                                    py_reraise(handler111.exception);
                            }
                        }
                        return none();
                    }) },
                { "_log_no_directory_fixup", w.method("TarFile", "_log_no_directory_fixup", { py_positional("member"), py_positional("reason") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py member = a[1];
                        py reason = a[2];
                        self.method("_dbg", { I<2>(), (S<"tarfile: Not fixing up directory %r (%s)">()) % py_tuple({ member.attr("name"), reason }) });
                        return none();
                    }) },
                { "extract", w.method("TarFile", "extract", { py_positional("member"), py_positional("path", S<"">()), py_positional("set_attrs", boolean(true)), py_keyword("numeric_owner", boolean(false)), py_keyword("filter", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py member = a[1];
                        py path = a[2];
                        py set_attrs = a[3];
                        py numeric_owner = a[4];
                        py filter = a[5];
                        py filter_function;
                        py tarinfo;
                        py unfiltered;
                        filter_function = self.method("_get_filter_function", { filter });
                        auto const items114 = unpack(self.method("_get_extract_tarinfo", { member, filter_function, path }), 2);
                        tarinfo = items114[0];
                        unfiltered = items114[1];
                        if (!tarinfo.is_none())
                        {
                            self.method("_extract_one", { tarinfo, path, set_attrs, numeric_owner }, { { "filter_function", filter_function } });
                        }
                        return none();
                    }) },
                { "_get_extract_tarinfo", w.method("TarFile", "_get_extract_tarinfo", { py_positional("member"), py_positional("filter_function"), py_positional("path") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py member = a[1];
                        py filter_function = a[2];
                        py path = a[3];
                        py e;
                        py filtered;
                        py unfiltered;
                        if (isinstance(member, B<"str">()))
                        {
                            unfiltered = self.method("getmember", { member });
                        }
                        else
                        {
                            unfiltered = member;
                        }
                        filtered = none();
                        try
                        {
                            filtered = filter_function(unfiltered, path);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler115;
                            if (matches(handler115.exception, py_tuple({ B<"OSError">(), B<"UnicodeEncodeError">(), G<"tarfile.FilterError">() })))
                            {
                                e = handler115.exception;
                                self.method("_handle_fatal_error", { e });
                            }
                            else if (matches(handler115.exception, G<"tarfile.ExtractError">()))
                            {
                                e = handler115.exception;
                                self.method("_handle_nonfatal_error", { e });
                            }
                            else
                                py_reraise(handler115.exception);
                        }
                        if (filtered.is_none())
                        {
                            self.method("_dbg", { I<2>(), (S<"tarfile: Excluded %r">()) % unfiltered.attr("name") });
                            return py_tuple({ none(), none() });
                        }
                        // Prepare the link target for makelink().
                        if (filtered.method("islnk").truthy())
                        {
                            filtered = (Mod<"copy">().attr("copy"))(filtered);
                            filtered.set_attr("_link_target", (Mod<"os">().attr("path")).method("join", { path, filtered.attr("linkname") }));
                        }
                        return py_tuple({ filtered, unfiltered });
                    }) },
                { "_extract_one", w.method("TarFile", "_extract_one", { py_positional("tarinfo"), py_positional("path"), py_positional("set_attrs"), py_positional("numeric_owner"), py_positional("filter_function", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py path = a[2];
                        py set_attrs = a[3];
                        py numeric_owner = a[4];
                        py filter_function = a[5];
                        py e;
                        self.method("_check", { S<"r">() });
                        try
                        {
                            self.method("_extract_member", { tarinfo, (Mod<"os">().attr("path")).method("join", { path, tarinfo.attr("name") }) }, { { "set_attrs", set_attrs }, { "numeric_owner", numeric_owner }, { "filter_function", filter_function }, { "extraction_root", path } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler116;
                            if (matches(handler116.exception, py_tuple({ B<"OSError">(), B<"UnicodeEncodeError">() })))
                            {
                                e = handler116.exception;
                                self.method("_handle_fatal_error", { e });
                            }
                            else if (matches(handler116.exception, G<"tarfile.ExtractError">()))
                            {
                                e = handler116.exception;
                                self.method("_handle_nonfatal_error", { e });
                            }
                            else
                                py_reraise(handler116.exception);
                        }
                        return none();
                    }) },
                { "_handle_nonfatal_error", w.method("TarFile", "_handle_nonfatal_error", { py_positional("e") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py e = a[1];
                        if (self.attr("errorlevel") > (I<1>()))
                        {
                            py_reraise(handled_exception());
                        }
                        else
                        {
                            self.method("_dbg", { I<1>(), (S<"tarfile: %s">()) % e });
                        }
                        return none();
                    }) },
                { "_handle_fatal_error", w.method("TarFile", "_handle_fatal_error", { py_positional("e") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py e = a[1];
                        if (self.attr("errorlevel") > (I<0>()))
                        {
                            py_reraise(handled_exception());
                        }
                        else if (isinstance(e, B<"OSError">()))
                        {
                            if (e.attr("filename").is_none())
                            {
                                self.method("_dbg", { I<1>(), (S<"tarfile: %s">()) % e.attr("strerror") });
                            }
                            else
                            {
                                self.method("_dbg", { I<1>(), (S<"tarfile: %s %r">()) % py_tuple({ e.attr("strerror"), e.attr("filename") }) });
                            }
                        }
                        else
                        {
                            self.method("_dbg", { I<1>(), (S<"tarfile: %s %s">()) % py_tuple({ (B<"type">())(e).attr("__name__"), e }) });
                        }
                        return none();
                    }) },
                { "extractfile", w.method("TarFile", "extractfile", { py_positional("member") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py member = a[1];
                        py tarinfo;
                        self.method("_check", { S<"r">() });
                        if (isinstance(member, B<"str">()))
                        {
                            tarinfo = self.method("getmember", { member });
                        }
                        else
                        {
                            tarinfo = member;
                        }
                        if ((tarinfo.method("isreg").truthy() || (!(G<"tarfile.SUPPORTED_TYPES">()).contains(tarinfo.attr("type")))))
                        {
                            // Members with unknown types are treated as regular files.
                            return self.method("fileobject", { self, tarinfo });
                        }
                        else if ((tarinfo.method("islnk").truthy() || tarinfo.method("issym").truthy()))
                        {
                            if (isinstance(self.attr("fileobj"), G<"tarfile._Stream">()))
                            {
                                // A small but ugly workaround for the case that someone tries
                                // to extract a (sym)link as a file-object from a non-seekable
                                // stream of tar blocks.
                                py_raise((G<"tarfile.StreamError">())(S<"cannot extract (sym)link as file object">()));
                            }
                            else
                            {
                                // A (sym)link's file object is its target's file object.
                                return self.method("extractfile", { self.method("_find_link_target", { tarinfo }) });
                            }
                        }
                        else
                        {
                            // If there's no data associated with the member (directory, chrdev,
                            // blkdev, etc.), return None instead of a file object.
                            return none();
                        }
                        return none();
                    }) },
                { "_extract_member", w.method("TarFile", "_extract_member", { py_positional("tarinfo"), py_positional("targetpath"), py_positional("set_attrs", boolean(true)), py_positional("numeric_owner", boolean(false)), py_keyword("filter_function", none()), py_keyword("extraction_root", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py targetpath = a[2];
                        py set_attrs = a[3];
                        py numeric_owner = a[4];
                        py filter_function = a[5];
                        py extraction_root = a[6];
                        py upperdirs;
                        // Fetch the TarInfo object for the given name
                        // and build the destination pathname, replacing
                        // forward slashes to platform specific separators.
                        targetpath = targetpath.method("rstrip", { S<"/">() });
                        targetpath = targetpath.method("replace", { S<"/">(), Mod<"os">().attr("sep") });
                        // Create all upper directories.
                        upperdirs = (Mod<"os">().attr("path")).method("dirname", { targetpath });
                        if ((upperdirs.truthy() && (!(Mod<"os">().attr("path")).method("exists", { upperdirs }).truthy())))
                        {
                            // Create directories that are not part of the archive with
                            // default permissions.
                            (Mod<"os">().attr("makedirs")).call({ upperdirs }, { { "exist_ok", boolean(true) } });
                        }
                        if ((tarinfo.method("islnk").truthy() || tarinfo.method("issym").truthy()))
                        {
                            self.method("_dbg", { I<1>(), (S<"%s -> %s">()) % py_tuple({ tarinfo.attr("name"), tarinfo.attr("linkname") }) });
                        }
                        else
                        {
                            self.method("_dbg", { I<1>(), tarinfo.attr("name") });
                        }
                        if (tarinfo.method("isreg").truthy())
                        {
                            self.method("makefile", { tarinfo, targetpath });
                        }
                        else if (tarinfo.method("isdir").truthy())
                        {
                            self.method("makedir", { tarinfo, targetpath });
                        }
                        else if (tarinfo.method("isfifo").truthy())
                        {
                            self.method("makefifo", { tarinfo, targetpath });
                        }
                        else if ((tarinfo.method("ischr").truthy() || tarinfo.method("isblk").truthy()))
                        {
                            self.method("makedev", { tarinfo, targetpath });
                        }
                        else if ((tarinfo.method("islnk").truthy() || tarinfo.method("issym").truthy()))
                        {
                            self.method("makelink_with_filter", { tarinfo, targetpath }, { { "filter_function", filter_function }, { "extraction_root", extraction_root } });
                        }
                        else if (!(G<"tarfile.SUPPORTED_TYPES">()).contains(tarinfo.attr("type")))
                        {
                            self.method("makeunknown", { tarinfo, targetpath });
                        }
                        else
                        {
                            self.method("makefile", { tarinfo, targetpath });
                        }
                        if (set_attrs.truthy())
                        {
                            self.method("chown", { tarinfo, targetpath, numeric_owner });
                            if (!tarinfo.method("issym").truthy())
                            {
                                self.method("chmod", { tarinfo, targetpath });
                                self.method("utime", { tarinfo, targetpath });
                            }
                        }
                        return none();
                    }) },
                { "makedir", w.method("TarFile", "makedir", { py_positional("tarinfo"), py_positional("targetpath") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py targetpath = a[2];
                        try
                        {
                            if (tarinfo.attr("mode").is_none())
                            {
                                // Use the system's default mode
                                (Mod<"os">().attr("mkdir"))(targetpath);
                            }
                            else
                            {
                                // Use a safe mode for the directory, the real mode is set
                                // later in _extract_member().
                                (Mod<"os">().attr("mkdir"))(targetpath, I<448>());
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler117;
                            if (matches(handler117.exception, B<"FileExistsError">()))
                            {
                                if (!(Mod<"os">().attr("path")).method("isdir", { targetpath }).truthy())
                                {
                                    py_reraise(handler117.exception);
                                }
                            }
                            else
                                py_reraise(handler117.exception);
                        }
                        return none();
                    }) },
                { "makefile", w.method("TarFile", "makefile", { py_positional("tarinfo"), py_positional("targetpath") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py targetpath = a[2];
                        py bufsize;
                        py offset;
                        py size;
                        py source;
                        py target;
                        source = self.attr("fileobj");
                        source.method("seek", { tarinfo.attr("offset_data") });
                        bufsize = self.attr("copybufsize");
                        {
                            py const manager118 = (Mod<"builtins">().attr("open"))(targetpath, S<"wb">());
                            py const exit118 = pyclass::special_method(manager118, "__exit__");
                            py const value118 = pyclass::special_method(manager118, "__enter__")(manager118);
                            target = value118;
                            [[maybe_unused]] int flow118 = 0;
                            [[maybe_unused]] py result118;
                            bool exited118 = false;
                            try
                            {
                                if (!tarinfo.attr("sparse").is_none())
                                {
                                    for (py const& item : iterate{ tarinfo.attr("sparse") })
                                    {
                                        auto const items120 = unpack(item, 2);
                                        offset = items120[0];
                                        size = items120[1];
                                        target.method("seek", { offset });
                                        (G<"tarfile.copyfileobj">())(source, target, size, G<"tarfile.ReadError">(), bufsize);
                                    }
                                    target.method("seek", { tarinfo.attr("size") });
                                    target.method("truncate");
                                }
                                else
                                {
                                    (G<"tarfile.copyfileobj">())(source, target, tarinfo.attr("size"), G<"tarfile.ReadError">(), bufsize);
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler118;
                                exited118 = true;
                                if (!exit118(manager118, type_of(handler118.exception), handler118.exception, handler118.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler118.exception);
                            }
                            finally118:
                            if (!exited118)
                                exit118(manager118, none(), none(), none());
                            if (flow118 == 1)
                                return result118;
                        }
                        return none();
                    }) },
                { "makeunknown", w.method("TarFile", "makeunknown", { py_positional("tarinfo"), py_positional("targetpath") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py targetpath = a[2];
                        self.method("makefile", { tarinfo, targetpath });
                        self.method("_dbg", { I<1>(), (S<"tarfile: Unknown file type %r, extracted as regular file.">()) % tarinfo.attr("type") });
                        return none();
                    }) },
                { "makefifo", w.method("TarFile", "makefifo", { py_positional("tarinfo"), py_positional("targetpath") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py targetpath = a[2];
                        if (has_attr(Mod<"os">(), "mkfifo"))
                        {
                            (Mod<"os">().attr("mkfifo"))(targetpath);
                        }
                        else
                        {
                            py_raise((G<"tarfile.ExtractError">())(S<"fifo not supported by system">()));
                        }
                        return none();
                    }) },
                { "makedev", w.method("TarFile", "makedev", { py_positional("tarinfo"), py_positional("targetpath") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py targetpath = a[2];
                        py mode;
                        if (((!has_attr(Mod<"os">(), "mknod")) || (!has_attr(Mod<"os">(), "makedev"))))
                        {
                            py_raise((G<"tarfile.ExtractError">())(S<"special devices not supported by system">()));
                        }
                        mode = tarinfo.attr("mode");
                        if (mode.is_none())
                        {
                            // Use mknod's default
                            mode = I<384>();
                        }
                        if (tarinfo.method("isblk").truthy())
                        {
                            mode = inplace(NEOS_PY_OR, mode, Mod<"stat">().attr("S_IFBLK"));
                        }
                        else
                        {
                            mode = inplace(NEOS_PY_OR, mode, Mod<"stat">().attr("S_IFCHR"));
                        }
                        (Mod<"os">().attr("mknod"))(targetpath, mode, (Mod<"os">().attr("makedev"))(tarinfo.attr("devmajor"), tarinfo.attr("devminor")));
                        return none();
                    }) },
                { "makelink", w.method("TarFile", "makelink", { py_positional("tarinfo"), py_positional("targetpath") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py targetpath = a[2];
                        return self.method("makelink_with_filter", { tarinfo, targetpath, none(), none() });
                    }) },
                { "makelink_with_filter", w.method("TarFile", "makelink_with_filter", { py_positional("tarinfo"), py_positional("targetpath"), py_positional("filter_function"), py_positional("extraction_root") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py targetpath = a[2];
                        py filter_function = a[3];
                        py extraction_root = a[4];
                        py cause;
                        py filtered;
                        py keyerror_to_extracterror;
                        py unfiltered;
                        keyerror_to_extracterror = boolean(false);
                        try
                        {
                            // For systems that support symbolic and hard links.
                            if (tarinfo.method("issym").truthy())
                            {
                                if ((Mod<"os">().attr("path")).method("lexists", { targetpath }).truthy())
                                {
                                    // Avoid FileExistsError on following os.symlink.
                                    (Mod<"os">().attr("unlink"))(targetpath);
                                }
                                (Mod<"os">().attr("symlink"))(tarinfo.attr("linkname"), targetpath);
                                return none();
                            }
                            else if ((Mod<"os">().attr("path")).method("exists", { tarinfo.attr("_link_target") }).truthy())
                            {
                                if ((Mod<"os">().attr("path")).method("lexists", { targetpath }).truthy())
                                {
                                    // Avoid FileExistsError on following os.link.
                                    (Mod<"os">().attr("unlink"))(targetpath);
                                }
                                // Resolve the target so the hard link points to the file
                                // itself. Otherwise os.link() may duplicate a symlink to a
                                // shallower location, where it's relative target escapes the
                                // destination directory. (CVE-2026-82049)
                                (Mod<"os">().attr("link"))((Mod<"os">().attr("path")).method("realpath", { tarinfo.attr("_link_target") }), targetpath);
                                return none();
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler121;
                            if (matches(handler121.exception, G<"tarfile.symlink_exception">()))
                            {
                                keyerror_to_extracterror = boolean(true);
                            }
                            else
                                py_reraise(handler121.exception);
                        }
                        try
                        {
                            unfiltered = self.method("_find_link_target", { tarinfo });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler122;
                            if (matches(handler122.exception, B<"KeyError">()))
                            {
                                if (keyerror_to_extracterror.truthy())
                                {
                                    fn_raise_from((G<"tarfile.ExtractError">())(S<"unable to resolve link inside archive">()).get(), none().get());
                                    throw raised{};
                                }
                                else
                                {
                                    py_reraise(handler122.exception);
                                }
                            }
                            else
                                py_reraise(handler122.exception);
                        }
                        if (filter_function.is_none())
                        {
                            filtered = unfiltered;
                        }
                        else
                        {
                            if (extraction_root.is_none())
                            {
                                py_raise((G<"tarfile.ExtractError">())((S<"makelink_with_filter: if filter_function is not None, ">()) + (S<"extraction_root must also not be None">())));
                            }
                            try
                            {
                                filtered = filter_function(unfiltered.method("replace", {  }, { { "name", tarinfo.attr("name") }, { "deep", boolean(false) } }), extraction_root);
                                if (filtered.is_none())
                                {
                                    return none();
                                }
                                filtered = filter_function(unfiltered, extraction_root);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler123;
                                if (matches(handler123.exception, G<"tarfile._FILTER_ERRORS">()))
                                {
                                    cause = handler123.exception;
                                    fn_raise_from((G<"tarfile.LinkFallbackError">())(tarinfo, unfiltered.attr("name")).get(), cause.get());
                                    throw raised{};
                                }
                                else
                                    py_reraise(handler123.exception);
                            }
                        }
                        if (!filtered.is_none())
                        {
                            self.method("_extract_member", { filtered, targetpath }, { { "filter_function", filter_function }, { "extraction_root", extraction_root } });
                        }
                        return none();
                    }) },
                { "chown", w.method("TarFile", "chown", { py_positional("tarinfo"), py_positional("targetpath"), py_positional("numeric_owner") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py targetpath = a[2];
                        py numeric_owner = a[3];
                        py e;
                        py g;
                        py u;
                        if ((has_attr(Mod<"os">(), "geteuid") && ((Mod<"os">().attr("geteuid"))() == (I<0>()))))
                        {
                            // We have to be root to do so.
                            g = tarinfo.attr("gid");
                            u = tarinfo.attr("uid");
                            if (!numeric_owner.truthy())
                            {
                                try
                                {
                                    if (((Mod<"grp">()).truthy() && tarinfo.attr("gname").truthy()))
                                    {
                                        g = (Mod<"grp">().attr("getgrnam"))(tarinfo.attr("gname"))[I<2>()];
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler124;
                                    if (matches(handler124.exception, B<"KeyError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler124.exception);
                                }
                                try
                                {
                                    if (((Mod<"pwd">()).truthy() && tarinfo.attr("uname").truthy()))
                                    {
                                        u = (Mod<"pwd">().attr("getpwnam"))(tarinfo.attr("uname"))[I<2>()];
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler125;
                                    if (matches(handler125.exception, B<"KeyError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler125.exception);
                                }
                            }
                            if (g.is_none())
                            {
                                g = I<-1>();
                            }
                            if (u.is_none())
                            {
                                u = I<-1>();
                            }
                            try
                            {
                                if ((tarinfo.method("issym").truthy() && has_attr(Mod<"os">(), "lchown")))
                                {
                                    (Mod<"os">().attr("lchown"))(targetpath, u, g);
                                }
                                else
                                {
                                    (Mod<"os">().attr("chown"))(targetpath, u, g);
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler126;
                                if (matches(handler126.exception, py_tuple({ B<"OSError">(), B<"OverflowError">() })))
                                {
                                    e = handler126.exception;
                                    // OverflowError can be raised if an ID doesn't fit in `id_t`
                                    fn_raise_from((G<"tarfile.ExtractError">())(S<"could not change owner">()).get(), e.get());
                                    throw raised{};
                                }
                                else
                                    py_reraise(handler126.exception);
                            }
                        }
                        return none();
                    }) },
                { "chmod", w.method("TarFile", "chmod", { py_positional("tarinfo"), py_positional("targetpath") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py targetpath = a[2];
                        py e;
                        if (tarinfo.attr("mode").is_none())
                        {
                            return none();
                        }
                        try
                        {
                            (Mod<"os">().attr("chmod"))(targetpath, tarinfo.attr("mode"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler127;
                            if (matches(handler127.exception, B<"OSError">()))
                            {
                                e = handler127.exception;
                                fn_raise_from((G<"tarfile.ExtractError">())(S<"could not change mode">()).get(), e.get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler127.exception);
                        }
                        return none();
                    }) },
                { "utime", w.method("TarFile", "utime", { py_positional("tarinfo"), py_positional("targetpath") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py targetpath = a[2];
                        py e;
                        py mtime;
                        mtime = tarinfo.attr("mtime");
                        if (mtime.is_none())
                        {
                            return none();
                        }
                        if (!has_attr(Mod<"os">(), "utime"))
                        {
                            return none();
                        }
                        try
                        {
                            (Mod<"os">().attr("utime"))(targetpath, py_tuple({ mtime, mtime }));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler128;
                            if (matches(handler128.exception, B<"OSError">()))
                            {
                                e = handler128.exception;
                                fn_raise_from((G<"tarfile.ExtractError">())(S<"could not change modification time">()).get(), e.get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler128.exception);
                        }
                        return none();
                    }) },
                { "next", w.method("TarFile", "next", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py e;
                        py m;
                        py tarinfo;
                        py zlib;
                        self.method("_check", { S<"ra">() });
                        if (!self.attr("firstmember").is_none())
                        {
                            m = self.attr("firstmember");
                            self.set_attr("firstmember", none());
                            return m;
                        }
                        // Advance the file pointer.
                        if (self.attr("offset") != self.attr("fileobj").method("tell"))
                        {
                            if (self.attr("offset") == (I<0>()))
                            {
                                return none();
                            }
                            self.attr("fileobj").method("seek", { self.attr("offset") - (I<1>()) });
                            if (!self.attr("fileobj").method("read", { I<1>() }).truthy())
                            {
                                py_raise((G<"tarfile.ReadError">())(S<"unexpected end of data">()));
                            }
                        }
                        // Read the next block.
                        tarinfo = none();
                        while (boolean(true).truthy())
                        {
                            try
                            {
                                tarinfo = self.attr("tarinfo").method("fromtarfile", { self });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler130;
                                if (matches(handler130.exception, G<"tarfile.EOFHeaderError">()))
                                {
                                    e = handler130.exception;
                                    if (self.attr("ignore_zeros").truthy())
                                    {
                                        self.method("_dbg", { I<2>(), (S<"0x%X: %s">()) % py_tuple({ self.attr("offset"), e }) });
                                        {
                                            py const target131 = self;
                                            target131.set_attr("offset", inplace(NEOS_PY_ADD, target131.attr("offset"), G<"tarfile.BLOCKSIZE">()));
                                        }
                                        continue;
                                    }
                                }
                                else if (matches(handler130.exception, G<"tarfile.InvalidHeaderError">()))
                                {
                                    e = handler130.exception;
                                    if (self.attr("ignore_zeros").truthy())
                                    {
                                        self.method("_dbg", { I<2>(), (S<"0x%X: %s">()) % py_tuple({ self.attr("offset"), e }) });
                                        {
                                            py const target132 = self;
                                            target132.set_attr("offset", inplace(NEOS_PY_ADD, target132.attr("offset"), G<"tarfile.BLOCKSIZE">()));
                                        }
                                        continue;
                                    }
                                    else if (self.attr("offset") == (I<0>()))
                                    {
                                        fn_raise_from((G<"tarfile.ReadError">())((B<"str">())(e)).get(), none().get());
                                        throw raised{};
                                    }
                                }
                                else if (matches(handler130.exception, G<"tarfile.EmptyHeaderError">()))
                                {
                                    if (self.attr("offset") == (I<0>()))
                                    {
                                        fn_raise_from((G<"tarfile.ReadError">())(S<"empty file">()).get(), none().get());
                                        throw raised{};
                                    }
                                }
                                else if (matches(handler130.exception, G<"tarfile.TruncatedHeaderError">()))
                                {
                                    e = handler130.exception;
                                    if (self.attr("offset") == (I<0>()))
                                    {
                                        fn_raise_from((G<"tarfile.ReadError">())((B<"str">())(e)).get(), none().get());
                                        throw raised{};
                                    }
                                }
                                else if (matches(handler130.exception, G<"tarfile.SubsequentHeaderError">()))
                                {
                                    e = handler130.exception;
                                    fn_raise_from((G<"tarfile.ReadError">())((B<"str">())(e)).get(), none().get());
                                    throw raised{};
                                }
                                else if (matches(handler130.exception, B<"Exception">()))
                                {
                                    e = handler130.exception;
                                    try
                                    {
                                        zlib = module_of("zlib");
                                        if (isinstance(e, zlib.attr("error")))
                                        {
                                            fn_raise_from((G<"tarfile.ReadError">())(S(std::string{ "zlib error: " } + e.str())).get(), none().get());
                                            throw raised{};
                                        }
                                        else
                                        {
                                            py_raise(e);
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler133;
                                        if (matches(handler133.exception, B<"ImportError">()))
                                        {
                                            py_raise(e);
                                        }
                                        else
                                            py_reraise(handler133.exception);
                                    }
                                }
                                else
                                    py_reraise(handler130.exception);
                            }
                            break;
                        }
                        if (!tarinfo.is_none())
                        {
                            // if streaming the file we do not want to cache the tarinfo
                            if (!self.attr("stream").truthy())
                            {
                                self.attr("members").method("append", { tarinfo });
                            }
                        }
                        else
                        {
                            self.set_attr("_loaded", boolean(true));
                        }
                        return tarinfo;
                    }) },
                { "_getmember", w.method("TarFile", "_getmember", { py_positional("name"), py_positional("tarinfo", none()), py_positional("normalize", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py tarinfo = a[2];
                        py normalize = a[3];
                        py index;
                        py member;
                        py member_name;
                        py members;
                        py skipping;
                        // Ensure that all members have been loaded.
                        members = self.method("getmembers");
                        // Limit the member search list up to tarinfo.
                        skipping = boolean(false);
                        if (!tarinfo.is_none())
                        {
                            {
                                bool otherwise134 = false;
                                try
                                {
                                    index = members.method("index", { tarinfo });
                                    otherwise134 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler134;
                                    if (matches(handler134.exception, B<"ValueError">()))
                                    {
                                        // The given starting point might be a (modified) copy.
                                        // We'll later skip members until we find an equivalent.
                                        skipping = boolean(true);
                                    }
                                    else
                                        py_reraise(handler134.exception);
                                }
                                if (otherwise134)
                                {
                                    // Happy fast path
                                    members = members[B<"slice">()(none(), index, none())];
                                }
                            }
                        }
                        if (normalize.truthy())
                        {
                            name = (Mod<"os">().attr("path")).method("normpath", { name });
                        }
                        for (py const& item : iterate{ (B<"reversed">())(members) })
                        {
                            member = item;
                            if (skipping.truthy())
                            {
                                if (tarinfo.attr("offset") == member.attr("offset"))
                                {
                                    skipping = boolean(false);
                                }
                                continue;
                            }
                            if (normalize.truthy())
                            {
                                member_name = (Mod<"os">().attr("path")).method("normpath", { member.attr("name") });
                            }
                            else
                            {
                                member_name = member.attr("name");
                            }
                            if (name == member_name)
                            {
                                return member;
                            }
                        }
                        if (skipping.truthy())
                        {
                            // Starting point was not found
                            py_raise((B<"ValueError">())(tarinfo));
                        }
                        return none();
                    }) },
                { "_load", w.method("TarFile", "_load", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("stream").truthy())
                        {
                            while (!self.method("next").is_none())
                            {
                            }
                            self.set_attr("_loaded", boolean(true));
                        }
                        return none();
                    }) },
                { "_check", w.method("TarFile", "_check", { py_positional("mode", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mode = a[1];
                        if (self.attr("closed").truthy())
                        {
                            py_raise((B<"OSError">())((S<"%s is closed">()) % self.attr("__class__").attr("__name__")));
                        }
                        if (((!mode.is_none()) && (!mode.contains(self.attr("mode")))))
                        {
                            py_raise((B<"OSError">())((S<"bad operation for mode %r">()) % self.attr("mode")));
                        }
                        return none();
                    }) },
                { "_find_link_target", w.method("TarFile", "_find_link_target", { py_positional("tarinfo") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tarinfo = a[1];
                        py limit;
                        py linkname;
                        py member;
                        if (tarinfo.method("issym").truthy())
                        {
                            // Always search the entire archive.
                            linkname = (S<"/">()).method("join", { (B<"filter">())(none(), py_tuple({ (Mod<"os">().attr("path")).method("dirname", { tarinfo.attr("name") }), tarinfo.attr("linkname") })) });
                            limit = none();
                        }
                        else
                        {
                            // Search the archive before the link, because a hard link is
                            // just a reference to an already archived file.
                            linkname = tarinfo.attr("linkname");
                            limit = tarinfo;
                        }
                        member = self.method("_getmember", { linkname }, { { "tarinfo", limit }, { "normalize", boolean(true) } });
                        if (member.is_none())
                        {
                            py_raise((B<"KeyError">())((S<"linkname %r not found">()) % linkname));
                        }
                        return member;
                    }) },
                { "__iter__", w.method("TarFile", "__iter__", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("__iter__", "TarFile.__iter__", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py index;
                            py tarinfo;
                            if (self.attr("_loaded").truthy())
                            {
                                yield_from(self.attr("members"));
                                return none();
                            }
                            // Yield items using TarFile's next() method.
                            // When all members have been read, set TarFile as _loaded.
                            index = I<0>();
                            // Fix for SF #1100429: Under rare circumstances it can
                            // happen that getmembers() is called during iteration,
                            // which will have already exhausted the next() method.
                            if (!self.attr("firstmember").is_none())
                            {
                                tarinfo = self.method("next");
                                index = inplace(NEOS_PY_ADD, index, I<1>());
                                py_yield(tarinfo);
                            }
                            while (boolean(true).truthy())
                            {
                                if (index < py{ self.attr("members").size() })
                                {
                                    tarinfo = self.attr("members")[index];
                                }
                                else if (!self.attr("_loaded").truthy())
                                {
                                    tarinfo = self.method("next");
                                    if (!tarinfo.truthy())
                                    {
                                        self.set_attr("_loaded", boolean(true));
                                        return none();
                                    }
                                }
                                else
                                {
                                    return none();
                                }
                                index = inplace(NEOS_PY_ADD, index, I<1>());
                                py_yield(tarinfo);
                            }
                            return none();
                        });
                    }) },
                { "_dbg", w.method("TarFile", "_dbg", { py_positional("level"), py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py level = a[1];
                        py msg = a[2];
                        if (level <= self.attr("debug"))
                        {
                            (B<"print">()).call({ msg }, { { "file", Mod<"sys">().attr("stderr") } });
                        }
                        return none();
                    }) },
                { "__enter__", w.method("TarFile", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_check");
                        return self;
                    }) },
                { "__exit__", w.method("TarFile", "__exit__", { py_positional("type"), py_positional("value"), py_positional("traceback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py type = a[1];
                        py value = a[2];
                        py traceback = a[3];
                        if (type.is_none())
                        {
                            self.method("close");
                        }
                        else
                        {
                            // An exception occurred. We must not call close() because
                            // it would try to write end-of-archive blocks and padding.
                            if (!self.attr("_extfileobj").truthy())
                            {
                                self.attr("fileobj").method("close");
                            }
                            self.set_attr("closed", boolean(true));
                        }
                        return none();
                    }) }, }));

            // --------------------
            // exported functions
            // --------------------
            define("tarfile", "is_tarfile", w.function("is_tarfile", "is_tarfile", { py_positional("name") }, [](arguments a) -> py
                {
                    py name = a[0];
                    py pos;
                    py t;
                    try
                    {
                        if (has_attr(name, "read"))
                        {
                            pos = name.method("tell");
                            t = (G<"tarfile.open">()).call({  }, { { "fileobj", name } });
                            name.method("seek", { pos });
                        }
                        else
                        {
                            t = (G<"tarfile.open">())(name);
                        }
                        t.method("close");
                        return boolean(true);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler138;
                        if (matches(handler138.exception, G<"tarfile.TarError">()))
                        {
                            return boolean(false);
                        }
                        else
                            py_reraise(handler138.exception);
                    }
                    return none();
                }));

            define("tarfile", "open", (G<"tarfile.TarFile">()).attr("open"));
            define("tarfile", "main", w.function("main", "main", {  }, [](arguments a) -> py
                {
                    py _;
                    py argparse;
                    py args;
                    py compressions;
                    py curdir;
                    py description;
                    py ext;
                    py file_name;
                    py group;
                    py msg;
                    py parser;
                    py src;
                    py tar;
                    py tar_files;
                    py tar_mode;
                    py tar_name;
                    py tf;
                    argparse = module_of("argparse");
                    description = S<"A simple command-line interface for tarfile module.">();
                    parser = argparse.method("ArgumentParser", {  }, { { "description", description } });
                    parser.method("add_argument", { S<"-v">(), S<"--verbose">() }, { { "action", S<"store_true">() }, { "default", boolean(false) }, { "help", S<"Verbose output">() } });
                    parser.method("add_argument", { S<"--filter">() }, { { "metavar", S<"<filtername>">() }, { "choices", G<"tarfile._NAMED_FILTERS">() }, { "help", S<"Filter for extraction">() } });
                    group = parser.method("add_mutually_exclusive_group", {  }, { { "required", boolean(true) } });
                    group.method("add_argument", { S<"-l">(), S<"--list">() }, { { "metavar", S<"<tarfile>">() }, { "help", S<"Show listing of a tarfile">() } });
                    group.method("add_argument", { S<"-e">(), S<"--extract">() }, { { "nargs", S<"+">() }, { "metavar", py_tuple({ S<"<tarfile>">(), S<"<output_dir>">() }) }, { "help", S<"Extract tarfile into target dir">() } });
                    group.method("add_argument", { S<"-c">(), S<"--create">() }, { { "nargs", S<"+">() }, { "metavar", py_tuple({ S<"<name>">(), S<"<file>">() }) }, { "help", S<"Create tarfile from sources">() } });
                    group.method("add_argument", { S<"-t">(), S<"--test">() }, { { "metavar", S<"<tarfile>">() }, { "help", S<"Test if a tarfile is valid">() } });
                    args = parser.method("parse_args");
                    if ((args.attr("filter").truthy() && args.attr("extract").is_none()))
                    {
                        parser.method("exit", { I<1>(), S<"--filter is only valid for extraction\n">() });
                    }
                    if (!args.attr("test").is_none())
                    {
                        src = args.attr("test");
                        if ((G<"tarfile.is_tarfile">())(src).truthy())
                        {
                            {
                                py const manager139 = (G<"tarfile.open">())(src, S<"r">());
                                py const exit139 = pyclass::special_method(manager139, "__exit__");
                                py const value139 = pyclass::special_method(manager139, "__enter__")(manager139);
                                tar = value139;
                                [[maybe_unused]] int flow139 = 0;
                                [[maybe_unused]] py result139;
                                bool exited139 = false;
                                try
                                {
                                    tar.method("getmembers");
                                    (B<"print">()).call({ tar.method("getmembers") }, { { "file", Mod<"sys">().attr("stderr") } });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler139;
                                    exited139 = true;
                                    if (!exit139(manager139, type_of(handler139.exception), handler139.exception, handler139.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler139.exception);
                                }
                                finally139:
                                if (!exited139)
                                    exit139(manager139, none(), none(), none());
                                if (flow139 == 1)
                                    return result139;
                            }
                            if (args.attr("verbose").truthy())
                            {
                                (B<"print">())((S<"{!r} is a tar archive.">()).method("format", { src }));
                            }
                        }
                        else
                        {
                            parser.method("exit", { I<1>(), (S<"{!r} is not a tar archive.\n">()).method("format", { src }) });
                        }
                    }
                    else if (!args.attr("list").is_none())
                    {
                        src = args.attr("list");
                        if ((G<"tarfile.is_tarfile">())(src).truthy())
                        {
                            {
                                py const manager140 = (G<"tarfile.TarFile">()).method("open", { src, S<"r:*">() });
                                py const exit140 = pyclass::special_method(manager140, "__exit__");
                                py const value140 = pyclass::special_method(manager140, "__enter__")(manager140);
                                tf = value140;
                                [[maybe_unused]] int flow140 = 0;
                                [[maybe_unused]] py result140;
                                bool exited140 = false;
                                try
                                {
                                    tf.method("list", {  }, { { "verbose", args.attr("verbose") } });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler140;
                                    exited140 = true;
                                    if (!exit140(manager140, type_of(handler140.exception), handler140.exception, handler140.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler140.exception);
                                }
                                finally140:
                                if (!exited140)
                                    exit140(manager140, none(), none(), none());
                                if (flow140 == 1)
                                    return result140;
                            }
                        }
                        else
                        {
                            parser.method("exit", { I<1>(), (S<"{!r} is not a tar archive.\n">()).method("format", { src }) });
                        }
                    }
                    else if (!args.attr("extract").is_none())
                    {
                        if (py{ args.attr("extract").size() } == (I<1>()))
                        {
                            src = args.attr("extract")[I<0>()];
                            curdir = Mod<"os">().attr("curdir");
                        }
                        else if (py{ args.attr("extract").size() } == (I<2>()))
                        {
                            auto const items141 = unpack(args.attr("extract"), 2);
                            src = items141[0];
                            curdir = items141[1];
                        }
                        else
                        {
                            parser.method("exit", { I<1>(), parser.method("format_help") });
                        }
                        if ((G<"tarfile.is_tarfile">())(src).truthy())
                        {
                            {
                                py const manager142 = (G<"tarfile.TarFile">()).method("open", { src, S<"r:*">() });
                                py const exit142 = pyclass::special_method(manager142, "__exit__");
                                py const value142 = pyclass::special_method(manager142, "__enter__")(manager142);
                                tf = value142;
                                [[maybe_unused]] int flow142 = 0;
                                [[maybe_unused]] py result142;
                                bool exited142 = false;
                                try
                                {
                                    tf.method("extractall", {  }, { { "path", curdir }, { "filter", args.attr("filter") } });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler142;
                                    exited142 = true;
                                    if (!exit142(manager142, type_of(handler142.exception), handler142.exception, handler142.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler142.exception);
                                }
                                finally142:
                                if (!exited142)
                                    exit142(manager142, none(), none(), none());
                                if (flow142 == 1)
                                    return result142;
                            }
                            if (args.attr("verbose").truthy())
                            {
                                if (curdir == (S<".">()))
                                {
                                    msg = (S<"{!r} file is extracted.">()).method("format", { src });
                                }
                                else
                                {
                                    msg = (S<"{!r} file is extracted into {!r} directory.">()).method("format", { src, curdir });
                                }
                                (B<"print">())(msg);
                            }
                        }
                        else
                        {
                            parser.method("exit", { I<1>(), (S<"{!r} is not a tar archive.\n">()).method("format", { src }) });
                        }
                    }
                    else if (!args.attr("create").is_none())
                    {
                        tar_name = args.attr("create").method("pop", { I<0>() });
                        auto const items143 = unpack((Mod<"os">().attr("path")).method("splitext", { tar_name }), 2);
                        _ = items143[0];
                        ext = items143[1];
                        compressions = dict_of({ { S<".gz">(), S<"gz">() }, { S<".tgz">(), S<"gz">() }, { S<".xz">(), S<"xz">() }, { S<".txz">(), S<"xz">() }, { S<".bz2">(), S<"bz2">() }, { S<".tbz">(), S<"bz2">() }, { S<".tbz2">(), S<"bz2">() }, { S<".tb2">(), S<"bz2">() } });
                        // gz
                        // xz
                        // bz2
                        tar_mode = (compressions.contains(ext) ? (S<"w:">()) + compressions[ext] : S<"w">());
                        tar_files = args.attr("create");
                        {
                            py const manager144 = (G<"tarfile.TarFile">()).method("open", { tar_name, tar_mode });
                            py const exit144 = pyclass::special_method(manager144, "__exit__");
                            py const value144 = pyclass::special_method(manager144, "__enter__")(manager144);
                            tf = value144;
                            [[maybe_unused]] int flow144 = 0;
                            [[maybe_unused]] py result144;
                            bool exited144 = false;
                            try
                            {
                                for (py const& item : iterate{ tar_files })
                                {
                                    file_name = item;
                                    tf.method("add", { file_name });
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler144;
                                exited144 = true;
                                if (!exit144(manager144, type_of(handler144.exception), handler144.exception, handler144.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler144.exception);
                            }
                            finally144:
                            if (!exited144)
                                exit144(manager144, none(), none(), none());
                            if (flow144 == 1)
                                return result144;
                        }
                        if (args.attr("verbose").truthy())
                        {
                            (B<"print">())((S<"{!r} file created.">()).method("format", { tar_name }));
                        }
                    }
                    return none();
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool tarfile_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_tarfile();
                }
                if (auto const doc = tarfile_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("bltn_open", import_attribute("builtins", "open").release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("shutil", py{ fn_import("shutil") }.release());
                aAttributes.emplace_back("stat", py{ fn_import("stat") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("struct", py{ fn_import("struct") }.release());
                aAttributes.emplace_back("copy", py{ fn_import("copy") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("tarfile", tarfile_module);
    }
}
