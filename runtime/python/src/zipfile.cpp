/*
  zipfile.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/zipfile/__init__.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module zipfile (CPython 3.13's Lib/zipfile/__init__.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "zipfile_docs.hpp"

namespace neos::python_runtime::detail::zipfile_module
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
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "zipfile.name"))
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
                py_raise("SystemError", std::string{ "zipfile: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"zipfile.name">()))
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
            static module_writer const sWriter{ "zipfile", zipfile_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- zipfile (Lib/zipfile/__init__.py)

        void make_zipfile()
        {
            static module_writer const& w = writer("zipfile");
            define("zipfile", "crc32", Mod<"zlib">().attr("crc32"));
            define("zipfile", "bz2", none());
            define("zipfile", "lzma", none());
            define("zipfile", "__all__", py_list({ S<"BadZipFile">(), S<"BadZipfile">(), S<"error">(), S<"ZIP_STORED">(), S<"ZIP_DEFLATED">(), S<"ZIP_BZIP2">(), S<"ZIP_LZMA">(), S<"is_zipfile">(), S<"ZipInfo">(), S<"ZipFile">(), S<"PyZipFile">(), S<"LargeZipFile">(), S<"Path">() }));
            define("zipfile", "BadZipFile", w.make_class("BadZipFile", { B<"Exception">() }, {
 }));

            define("zipfile", "LargeZipFile", w.make_class("LargeZipFile", { B<"Exception">() }, {
 }));

            define("zipfile", "error", G<"zipfile.BadZipFile">());
            define("zipfile", "BadZipfile", G<"zipfile.BadZipFile">());
            define("zipfile", "ZIP64_LIMIT", ((I<1>()) << (I<31>())) - (I<1>()));
            define("zipfile", "ZIP_FILECOUNT_LIMIT", ((I<1>()) << (I<16>())) - (I<1>()));
            define("zipfile", "ZIP_MAX_COMMENT", ((I<1>()) << (I<16>())) - (I<1>()));
            // constants for Zip file compression methods
            define("zipfile", "ZIP_STORED", I<0>());
            define("zipfile", "ZIP_DEFLATED", I<8>());
            define("zipfile", "ZIP_BZIP2", I<12>());
            define("zipfile", "ZIP_LZMA", I<14>());
            // Other ZIP compression methods not supported
            define("zipfile", "DEFAULT_VERSION", I<20>());
            define("zipfile", "ZIP64_VERSION", I<45>());
            define("zipfile", "BZIP2_VERSION", I<46>());
            define("zipfile", "LZMA_VERSION", I<63>());
            // we recognize (but not necessarily support) all features up to that version
            define("zipfile", "MAX_EXTRACT_VERSION", I<63>());
            // Below are some formats and associated data for reading/writing headers using
            // the struct module.  The names and structures of headers/records are those used
            // in the PKWARE description of the ZIP file format:
            // http://www.pkware.com/documents/casestudies/APPNOTE.TXT
            // (URL valid as of January 2008)
            // The "end of central directory" structure, magic number, size, and indices
            // (section V.I in the format document)
            define("zipfile", "structEndArchive", py{ make_bytes(std::string{ "\x3c\x34\x73\x34\x48\x32\x4c\x48", 8 }) });
            define("zipfile", "stringEndArchive", py{ make_bytes(std::string{ "\x50\x4b\x05\x06", 4 }) });
            define("zipfile", "sizeEndCentDir", (Mod<"struct">()).method("calcsize", { G<"zipfile.structEndArchive">() }));
            define("zipfile", "_ECD_SIGNATURE", I<0>());
            define("zipfile", "_ECD_DISK_NUMBER", I<1>());
            define("zipfile", "_ECD_DISK_START", I<2>());
            define("zipfile", "_ECD_ENTRIES_THIS_DISK", I<3>());
            define("zipfile", "_ECD_ENTRIES_TOTAL", I<4>());
            define("zipfile", "_ECD_SIZE", I<5>());
            define("zipfile", "_ECD_OFFSET", I<6>());
            define("zipfile", "_ECD_COMMENT_SIZE", I<7>());
            // These last two indices are not part of the structure as defined in the
            // spec, but they are used internally by this module as a convenience
            define("zipfile", "_ECD_COMMENT", I<8>());
            define("zipfile", "_ECD_LOCATION", I<9>());
            // The "central directory" structure, magic number, size, and indices
            // of entries in the structure (section V.F in the format document)
            define("zipfile", "structCentralDir", S<"<4s4B4HL2L5H2L">());
            define("zipfile", "stringCentralDir", py{ make_bytes(std::string{ "\x50\x4b\x01\x02", 4 }) });
            define("zipfile", "sizeCentralDir", (Mod<"struct">()).method("calcsize", { G<"zipfile.structCentralDir">() }));
            // indexes of entries in the central directory structure
            define("zipfile", "_CD_SIGNATURE", I<0>());
            define("zipfile", "_CD_CREATE_VERSION", I<1>());
            define("zipfile", "_CD_CREATE_SYSTEM", I<2>());
            define("zipfile", "_CD_EXTRACT_VERSION", I<3>());
            define("zipfile", "_CD_EXTRACT_SYSTEM", I<4>());
            define("zipfile", "_CD_FLAG_BITS", I<5>());
            define("zipfile", "_CD_COMPRESS_TYPE", I<6>());
            define("zipfile", "_CD_TIME", I<7>());
            define("zipfile", "_CD_DATE", I<8>());
            define("zipfile", "_CD_CRC", I<9>());
            define("zipfile", "_CD_COMPRESSED_SIZE", I<10>());
            define("zipfile", "_CD_UNCOMPRESSED_SIZE", I<11>());
            define("zipfile", "_CD_FILENAME_LENGTH", I<12>());
            define("zipfile", "_CD_EXTRA_FIELD_LENGTH", I<13>());
            define("zipfile", "_CD_COMMENT_LENGTH", I<14>());
            define("zipfile", "_CD_DISK_NUMBER_START", I<15>());
            define("zipfile", "_CD_INTERNAL_FILE_ATTRIBUTES", I<16>());
            define("zipfile", "_CD_EXTERNAL_FILE_ATTRIBUTES", I<17>());
            define("zipfile", "_CD_LOCAL_HEADER_OFFSET", I<18>());
            // General purpose bit flags
            // Zip Appnote: 4.4.4 general purpose bit flag: (2 bytes)
            define("zipfile", "_MASK_ENCRYPTED", (I<1>()) << (I<0>()));
            // Bits 1 and 2 have different meanings depending on the compression used.
            define("zipfile", "_MASK_COMPRESS_OPTION_1", (I<1>()) << (I<1>()));
            // _MASK_COMPRESS_OPTION_2 = 1 << 2
            // _MASK_USE_DATA_DESCRIPTOR: If set, crc-32, compressed size and uncompressed
            // size are zero in the local header and the real values are written in the data
            // descriptor immediately following the compressed data.
            define("zipfile", "_MASK_USE_DATA_DESCRIPTOR", (I<1>()) << (I<3>()));
            // Bit 4: Reserved for use with compression method 8, for enhanced deflating.
            // _MASK_RESERVED_BIT_4 = 1 << 4
            define("zipfile", "_MASK_COMPRESSED_PATCH", (I<1>()) << (I<5>()));
            define("zipfile", "_MASK_STRONG_ENCRYPTION", (I<1>()) << (I<6>()));
            // _MASK_UNUSED_BIT_7 = 1 << 7
            // _MASK_UNUSED_BIT_8 = 1 << 8
            // _MASK_UNUSED_BIT_9 = 1 << 9
            // _MASK_UNUSED_BIT_10 = 1 << 10
            define("zipfile", "_MASK_UTF_FILENAME", (I<1>()) << (I<11>()));
            // Bit 12: Reserved by PKWARE for enhanced compression.
            // _MASK_RESERVED_BIT_12 = 1 << 12
            // _MASK_ENCRYPTED_CENTRAL_DIR = 1 << 13
            // Bit 14, 15: Reserved by PKWARE
            // _MASK_RESERVED_BIT_14 = 1 << 14
            // _MASK_RESERVED_BIT_15 = 1 << 15
            // The "local file header" structure, magic number, size, and indices
            // (section V.A in the format document)
            define("zipfile", "structFileHeader", S<"<4s2B4HL2L2H">());
            define("zipfile", "stringFileHeader", py{ make_bytes(std::string{ "\x50\x4b\x03\x04", 4 }) });
            define("zipfile", "sizeFileHeader", (Mod<"struct">()).method("calcsize", { G<"zipfile.structFileHeader">() }));
            define("zipfile", "_FH_SIGNATURE", I<0>());
            define("zipfile", "_FH_EXTRACT_VERSION", I<1>());
            define("zipfile", "_FH_EXTRACT_SYSTEM", I<2>());
            define("zipfile", "_FH_GENERAL_PURPOSE_FLAG_BITS", I<3>());
            define("zipfile", "_FH_COMPRESSION_METHOD", I<4>());
            define("zipfile", "_FH_LAST_MOD_TIME", I<5>());
            define("zipfile", "_FH_LAST_MOD_DATE", I<6>());
            define("zipfile", "_FH_CRC", I<7>());
            define("zipfile", "_FH_COMPRESSED_SIZE", I<8>());
            define("zipfile", "_FH_UNCOMPRESSED_SIZE", I<9>());
            define("zipfile", "_FH_FILENAME_LENGTH", I<10>());
            define("zipfile", "_FH_EXTRA_FIELD_LENGTH", I<11>());
            // The "Zip64 end of central directory locator" structure, magic number, and size
            define("zipfile", "structEndArchive64Locator", S<"<4sLQL">());
            define("zipfile", "stringEndArchive64Locator", py{ make_bytes(std::string{ "\x50\x4b\x06\x07", 4 }) });
            define("zipfile", "sizeEndCentDir64Locator", (Mod<"struct">()).method("calcsize", { G<"zipfile.structEndArchive64Locator">() }));
            // The "Zip64 end of central directory" record, magic number, size, and indices
            // (section V.G in the format document)
            define("zipfile", "structEndArchive64", S<"<4sQ2H2L4Q">());
            define("zipfile", "stringEndArchive64", py{ make_bytes(std::string{ "\x50\x4b\x06\x06", 4 }) });
            define("zipfile", "sizeEndCentDir64", (Mod<"struct">()).method("calcsize", { G<"zipfile.structEndArchive64">() }));
            define("zipfile", "_CD64_SIGNATURE", I<0>());
            define("zipfile", "_CD64_DIRECTORY_RECSIZE", I<1>());
            define("zipfile", "_CD64_CREATE_VERSION", I<2>());
            define("zipfile", "_CD64_EXTRACT_VERSION", I<3>());
            define("zipfile", "_CD64_DISK_NUMBER", I<4>());
            define("zipfile", "_CD64_DISK_NUMBER_START", I<5>());
            define("zipfile", "_CD64_NUMBER_ENTRIES_THIS_DISK", I<6>());
            define("zipfile", "_CD64_NUMBER_ENTRIES_TOTAL", I<7>());
            define("zipfile", "_CD64_DIRECTORY_SIZE", I<8>());
            define("zipfile", "_CD64_OFFSET_START_CENTDIR", I<9>());
            define("zipfile", "_DD_SIGNATURE", I<134695760>());
            define("zipfile", "_Extra", w.make_class("_Extra", { B<"bytes">() }, {
                { "FIELD_STRUCT", (Mod<"struct">()).method("Struct", { S<"<HH">() }) },
                { "__new__", w.method("_Extra", "__new__", { py_positional("val"), py_positional("id", none()) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py val = a[1];
                        py id = a[2];
                        return (B<"super">()(G<"zipfile._Extra">(), cls)).method("__new__", { cls, val });
                    }) },
                { "__init__", w.method("_Extra", "__init__", { py_positional("val"), py_positional("id", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py val = a[1];
                        py id = a[2];
                        self.set_attr("id", id);
                        return none();
                    }) },
                { "read_one", w.class_method("_Extra", "read_one", { py_positional("raw") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py raw = a[1];
                        py xid;
                        py xlen;
                        try
                        {
                            auto const items2 = unpack(cls.attr("FIELD_STRUCT").method("unpack", { raw[B<"slice">()(none(), I<4>(), none())] }), 2);
                            xid = items2[0];
                            xlen = items2[1];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            if (matches(handler1.exception, Mod<"struct">().attr("error")))
                            {
                                xid = none();
                                xlen = I<0>();
                            }
                            else
                                py_reraise(handler1.exception);
                        }
                        return py_tuple({ cls(raw[B<"slice">()(none(), (I<4>()) + xlen, none())], xid), raw[B<"slice">()((I<4>()) + xlen, none(), none())] });
                    }) },
                { "split", w.class_method("_Extra", "split", { py_positional("data") }, [](arguments a) -> py
                    {
                        return make_native_generator("split", "_Extra.split", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py cls = a[0];
                            py data = a[1];
                            py extra;
                            py rest;
                            rest = (B<"memoryview">())(data);
                            while (rest.truthy())
                            {
                                auto const items4 = unpack((G<"zipfile._Extra">()).method("read_one", { rest }), 2);
                                extra = items4[0];
                                rest = items4[1];
                                py_yield(extra);
                            }
                            return none();
                        });
                    }) },
                { "strip", w.class_method("_Extra", "strip", { py_positional("data"), py_positional("xids") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py data = a[1];
                        py xids = a[2];
                        return py{ make_bytes(std::string{ "", 0 }) }.method("join", { [&]() -> py { py const comprehension5 = py_list(); py ex; for (py const& item : iterate{ cls.method("split", { data }) }) { ex = item; if (xids.contains(ex.attr("id"))) continue; comprehension5.method("append", { ex }); } return comprehension5; }() });
                    }) }, }));

            define("zipfile", "_check_zipfile", w.function("_check_zipfile", "_check_zipfile", { py_positional("fp") }, [](arguments a) -> py
                {
                    py fp = a[0];
                    try
                    {
                        if ((G<"zipfile._EndRecData">())(fp).truthy())
                        {
                            return boolean(true);
                        }
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler6;
                        if (matches(handler6.exception, B<"OSError">()))
                        {
                        }
                        else
                            py_reraise(handler6.exception);
                    }
                    return boolean(false);
                }));

            define("zipfile", "is_zipfile", w.function("is_zipfile", "is_zipfile", { py_positional("filename") }, [](arguments a) -> py
                {
                    py filename = a[0];
                    py fp;
                    py result;
                    result = boolean(false);
                    try
                    {
                        if (has_attr(filename, "read"))
                        {
                            result = (G<"zipfile._check_zipfile">()).call({  }, { { "fp", filename } });
                        }
                        else
                        {
                            {
                                py const manager8 = (B<"open">())(filename, S<"rb">());
                                py const exit8 = pyclass::special_method(manager8, "__exit__");
                                py const value8 = pyclass::special_method(manager8, "__enter__")(manager8);
                                fp = value8;
                                [[maybe_unused]] int flow8 = 0;
                                [[maybe_unused]] py result8;
                                bool exited8 = false;
                                try
                                {
                                    result = (G<"zipfile._check_zipfile">())(fp);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler8;
                                    exited8 = true;
                                    if (!exit8(manager8, type_of(handler8.exception), handler8.exception, handler8.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler8.exception);
                                }
                                finally8:
                                if (!exited8)
                                    exit8(manager8, none(), none(), none());
                                if (flow8 == 1)
                                    return result8;
                            }
                        }
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler7;
                        if (matches(handler7.exception, py_tuple({ B<"OSError">(), G<"zipfile.BadZipFile">() })))
                        {
                        }
                        else
                            py_reraise(handler7.exception);
                    }
                    return result;
                }));

            define("zipfile", "_EndRecData64", w.function("_EndRecData64", "_EndRecData64", { py_positional("fpin"), py_positional("offset"), py_positional("endrec") }, [](arguments a) -> py
                {
                    py fpin = a[0];
                    py offset = a[1];
                    py endrec = a[2];
                    py create_version;
                    py data;
                    py dircount;
                    py dircount2;
                    py diroffset;
                    py dirsize;
                    py disk_dir;
                    py disk_num;
                    py diskno;
                    py disks;
                    py extrasz;
                    py read_version;
                    py reloff;
                    py sig;
                    py sz;
                    offset = inplace(NEOS_PY_SUBTRACT, offset, G<"zipfile.sizeEndCentDir64Locator">());
                    if (offset < (I<0>()))
                    {
                        // The file is not large enough to contain a ZIP64
                        // end-of-archive record, so just return the end record we were given.
                        return endrec;
                    }
                    fpin.method("seek", { offset });
                    data = fpin.method("read", { G<"zipfile.sizeEndCentDir64Locator">() });
                    if (py{ data.size() } != (G<"zipfile.sizeEndCentDir64Locator">()))
                    {
                        py_raise((B<"OSError">())(S<"Unknown I/O error">()));
                    }
                    auto const items9 = unpack((Mod<"struct">().attr("unpack"))(G<"zipfile.structEndArchive64Locator">(), data), 4);
                    sig = items9[0];
                    diskno = items9[1];
                    reloff = items9[2];
                    disks = items9[3];
                    if (sig != (G<"zipfile.stringEndArchive64Locator">()))
                    {
                        return endrec;
                    }
                    if (((diskno != (I<0>())) || (disks > (I<1>()))))
                    {
                        py_raise((G<"zipfile.BadZipFile">())(S<"zipfiles that span multiple disks are not supported">()));
                    }
                    offset = inplace(NEOS_PY_SUBTRACT, offset, G<"zipfile.sizeEndCentDir64">());
                    if (reloff > offset)
                    {
                        py_raise((G<"zipfile.BadZipFile">())(S<"Corrupt zip64 end of central directory locator">()));
                    }
                    // First, check the assumption that there is no prepended data.
                    fpin.method("seek", { reloff });
                    extrasz = offset - reloff;
                    data = fpin.method("read", { G<"zipfile.sizeEndCentDir64">() });
                    if (py{ data.size() } != (G<"zipfile.sizeEndCentDir64">()))
                    {
                        py_raise((B<"OSError">())(S<"Unknown I/O error">()));
                    }
                    if (((!data.method("startswith", { G<"zipfile.stringEndArchive64">() }).truthy()) && (reloff != offset)))
                    {
                        // Since we already have seen the Zip64 EOCD Locator, it's
                        // possible we got here because there is prepended data.
                        // Assume no 'zip64 extensible data'
                        fpin.method("seek", { offset });
                        extrasz = I<0>();
                        data = fpin.method("read", { G<"zipfile.sizeEndCentDir64">() });
                        if (py{ data.size() } != (G<"zipfile.sizeEndCentDir64">()))
                        {
                            py_raise((B<"OSError">())(S<"Unknown I/O error">()));
                        }
                    }
                    if (!data.method("startswith", { G<"zipfile.stringEndArchive64">() }).truthy())
                    {
                        py_raise((G<"zipfile.BadZipFile">())(S<"Zip64 end of central directory record not found">()));
                    }
                    auto const items10 = unpack((Mod<"struct">().attr("unpack"))(G<"zipfile.structEndArchive64">(), data), 10);
                    sig = items10[0];
                    sz = items10[1];
                    create_version = items10[2];
                    read_version = items10[3];
                    disk_num = items10[4];
                    disk_dir = items10[5];
                    dircount = items10[6];
                    dircount2 = items10[7];
                    dirsize = items10[8];
                    diroffset = items10[9];
                    if ((((diroffset + dirsize) != reloff) || ((sz + (I<12>())) != ((G<"zipfile.sizeEndCentDir64">()) + extrasz))))
                    {
                        py_raise((G<"zipfile.BadZipFile">())(S<"Corrupt zip64 end of central directory record">()));
                    }
                    // Update the original endrec using data from the ZIP64 record
                    endrec.set_item(G<"zipfile._ECD_SIGNATURE">(), sig);
                    endrec.set_item(G<"zipfile._ECD_DISK_NUMBER">(), disk_num);
                    endrec.set_item(G<"zipfile._ECD_DISK_START">(), disk_dir);
                    endrec.set_item(G<"zipfile._ECD_ENTRIES_THIS_DISK">(), dircount);
                    endrec.set_item(G<"zipfile._ECD_ENTRIES_TOTAL">(), dircount2);
                    endrec.set_item(G<"zipfile._ECD_SIZE">(), dirsize);
                    endrec.set_item(G<"zipfile._ECD_OFFSET">(), diroffset);
                    endrec.set_item(G<"zipfile._ECD_LOCATION">(), offset - extrasz);
                    return endrec;
                }));

            define("zipfile", "_EndRecData", w.function("_EndRecData", "_EndRecData", { py_positional("fpin") }, [](arguments a) -> py
                {
                    py fpin = a[0];
                    py comment;
                    py commentSize;
                    py data;
                    py endrec;
                    py filesize;
                    py maxCommentStart;
                    py recData;
                    py start;
                    // Determine file size
                    fpin.method("seek", { I<0>(), I<2>() });
                    filesize = fpin.method("tell");
                    // Check to see if this is ZIP file with no archive comment (the
                    // "end of central directory" structure should be the last item in the
                    // file if this is the case).
                    try
                    {
                        fpin.method("seek", { -(G<"zipfile.sizeEndCentDir">()), I<2>() });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler11;
                        if (matches(handler11.exception, B<"OSError">()))
                        {
                            return none();
                        }
                        else
                            py_reraise(handler11.exception);
                    }
                    data = fpin.method("read", { G<"zipfile.sizeEndCentDir">() });
                    if (((py{ data.size() } == (G<"zipfile.sizeEndCentDir">())) && (data[B<"slice">()(I<0>(), I<4>(), none())] == (G<"zipfile.stringEndArchive">())) && (data[B<"slice">()(I<-2>(), none(), none())] == py{ make_bytes(std::string{ "\x00\x00", 2 }) })))
                    {
                        // the signature is correct and there's no comment, unpack structure
                        endrec = (Mod<"struct">().attr("unpack"))(G<"zipfile.structEndArchive">(), data);
                        endrec = (B<"list">())(endrec);
                        // Append a blank comment and record start offset
                        endrec.method("append", { py{ make_bytes(std::string{ "", 0 }) } });
                        endrec.method("append", { filesize - (G<"zipfile.sizeEndCentDir">()) });
                        // Try to read the "Zip64 end of central directory" structure
                        return (G<"zipfile._EndRecData64">())(fpin, filesize - (G<"zipfile.sizeEndCentDir">()), endrec);
                    }
                    // Either this is not a ZIP file, or it is a ZIP file with an archive
                    // comment.  Search the end of the file for the "end of central directory"
                    // record signature. The comment is the last item in the ZIP file and may be
                    // up to 64K long.  It is assumed that the "end of central directory" magic
                    // number does not appear in the comment.
                    maxCommentStart = (B<"max">())((filesize - (G<"zipfile.ZIP_MAX_COMMENT">())) - (G<"zipfile.sizeEndCentDir">()), I<0>());
                    fpin.method("seek", { maxCommentStart, I<0>() });
                    data = fpin.method("read", { (G<"zipfile.ZIP_MAX_COMMENT">()) + (G<"zipfile.sizeEndCentDir">()) });
                    start = data.method("rfind", { G<"zipfile.stringEndArchive">() });
                    if (start >= (I<0>()))
                    {
                        // found the magic number; attempt to unpack and interpret
                        recData = data[B<"slice">()(start, start + (G<"zipfile.sizeEndCentDir">()), none())];
                        if (py{ recData.size() } != (G<"zipfile.sizeEndCentDir">()))
                        {
                            // Zip file is corrupted.
                            return none();
                        }
                        endrec = (B<"list">())((Mod<"struct">().attr("unpack"))(G<"zipfile.structEndArchive">(), recData));
                        commentSize = endrec[G<"zipfile._ECD_COMMENT_SIZE">()];
                        comment = data[B<"slice">()(start + (G<"zipfile.sizeEndCentDir">()), (start + (G<"zipfile.sizeEndCentDir">())) + commentSize, none())];
                        endrec.method("append", { comment });
                        endrec.method("append", { maxCommentStart + start });
                        // Try to read the "Zip64 end of central directory" structure
                        return (G<"zipfile._EndRecData64">())(fpin, maxCommentStart + start, endrec);
                    }
                    // Unable to find a valid end of central directory structure
                    return none();
                }));

            define("zipfile", "_sanitize_filename", w.function("_sanitize_filename", "_sanitize_filename", { py_positional("filename") }, [](arguments a) -> py
                {
                    py filename = a[0];
                    py null_byte;
                    // Terminate the file name at the first null byte.  Null bytes in file
                    // names are used as tricks by viruses in archives.
                    null_byte = filename.method("find", { (B<"chr">())(I<0>()) });
                    if (null_byte >= (I<0>()))
                    {
                        filename = filename[B<"slice">()(I<0>(), null_byte, none())];
                    }
                    // This is used to ensure paths in generated ZIP files always use
                    // forward slashes as the directory separator, as required by the
                    // ZIP format specification.
                    if ((((Mod<"os">().attr("sep")) != (S<"/">())) && filename.contains((Mod<"os">().attr("sep")))))
                    {
                        filename = filename.method("replace", { Mod<"os">().attr("sep"), S<"/">() });
                    }
                    if (((Mod<"os">().attr("altsep")).truthy() && ((Mod<"os">().attr("altsep")) != (S<"/">())) && filename.contains((Mod<"os">().attr("altsep")))))
                    {
                        filename = filename.method("replace", { Mod<"os">().attr("altsep"), S<"/">() });
                    }
                    return filename;
                }));

                // Other attributes are set by class ZipFile:
                // header_offset         Byte offset to the file header
                // CRC                   CRC-32 of the uncompressed file
                // Maintain backward compatibility with the old protected attribute name.
            define("zipfile", "ZipInfo", w.make_class("ZipInfo", {  }, {
                { "__slots__", py_tuple({ S<"orig_filename">(), S<"filename">(), S<"date_time">(), S<"compress_type">(), S<"compress_level">(), S<"comment">(), S<"extra">(), S<"create_system">(), S<"create_version">(), S<"extract_version">(), S<"reserved">(), S<"flag_bits">(), S<"volume">(), S<"internal_attr">(), S<"external_attr">(), S<"header_offset">(), S<"CRC">(), S<"compress_size">(), S<"file_size">(), S<"_raw_time">(), S<"_end_offset">() }) },
                { "__init__", w.method("ZipInfo", "__init__", { py_positional("filename", S<"NoName">()), py_positional("date_time", py_tuple({ I<1980>(), I<1>(), I<1>(), I<0>(), I<0>(), I<0>() })) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py date_time = a[2];
                        self.set_attr("orig_filename", filename);
                        // Terminate the file name at the first null byte and
                        // ensure paths always use forward slashes as the directory separator.
                        filename = (G<"zipfile._sanitize_filename">())(filename);
                        self.set_attr("filename", filename);
                        self.set_attr("date_time", date_time);
                        if (date_time[I<0>()] < (I<1980>()))
                        {
                            py_raise((B<"ValueError">())(S<"ZIP does not support timestamps before 1980">()));
                        }
                        // Standard values:
                        self.set_attr("compress_type", G<"zipfile.ZIP_STORED">());
                        self.set_attr("compress_level", none());
                        self.set_attr("comment", py{ make_bytes(std::string{ "", 0 }) });
                        self.set_attr("extra", py{ make_bytes(std::string{ "", 0 }) });
                        #if defined(_WIN32)
                        {
                            self.set_attr("create_system", I<0>());
                        }
                        #else
                        {
                            // Assume everything else is unix-y
                            self.set_attr("create_system", I<3>());
                        }
                        #endif
                        self.set_attr("create_version", G<"zipfile.DEFAULT_VERSION">());
                        self.set_attr("extract_version", G<"zipfile.DEFAULT_VERSION">());
                        self.set_attr("reserved", I<0>());
                        self.set_attr("flag_bits", I<0>());
                        self.set_attr("volume", I<0>());
                        self.set_attr("internal_attr", I<0>());
                        self.set_attr("external_attr", I<0>());
                        self.set_attr("compress_size", I<0>());
                        self.set_attr("file_size", I<0>());
                        self.set_attr("_end_offset", none());
                        return none();
                    }) },
                { "_compresslevel", B<"property">()(w.method("ZipInfo", "_compresslevel", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("compress_level");
                    }), w.method("ZipInfo", "_compresslevel", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("compress_level", value);
                        return none();
                    })) },
                { "__repr__", w.method("ZipInfo", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py hi;
                        py isdir;
                        py lo;
                        py result;
                        result = py_list({ (S<"<%s filename=%r">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.attr("filename") }) });
                        if (self.attr("compress_type") != (G<"zipfile.ZIP_STORED">()))
                        {
                            result.method("append", { (S<" compress_type=%s">()) % (G<"zipfile.compressor_names">()).method("get", { self.attr("compress_type"), self.attr("compress_type") }) });
                        }
                        hi = self.attr("external_attr") >> (I<16>());
                        lo = self.attr("external_attr") & (I<65535>());
                        if (hi.truthy())
                        {
                            result.method("append", { (S<" filemode=%r">()) % (Mod<"stat">().attr("filemode"))(hi) });
                        }
                        if (lo.truthy())
                        {
                            result.method("append", { (S<" external_attr=%#x">()) % lo });
                        }
                        isdir = self.method("is_dir");
                        if (((!isdir.truthy()) || self.attr("file_size").truthy()))
                        {
                            result.method("append", { (S<" file_size=%r">()) % self.attr("file_size") });
                        }
                        if ((([&]() -> py { py const t = boolean(!isdir.truthy()); return t.truthy() ? t : self.attr("compress_size"); }()).truthy() && ([&]() -> py { py const t = boolean(self.attr("compress_type") != (G<"zipfile.ZIP_STORED">())); return t.truthy() ? t : boolean(self.attr("file_size") != self.attr("compress_size")); }()).truthy()))
                        {
                            result.method("append", { (S<" compress_size=%r">()) % self.attr("compress_size") });
                        }
                        result.method("append", { S<">">() });
                        return (S<"">()).method("join", { result });
                    }) },
                { "FileHeader", w.method("ZipInfo", "FileHeader", { py_positional("zip64", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py zip64 = a[1];
                        py CRC;
                        py compress_size;
                        py dosdate;
                        py dostime;
                        py dt;
                        py extra;
                        py file_size;
                        py filename;
                        py flag_bits;
                        py fmt;
                        py header;
                        py min_version;
                        dt = self.attr("date_time");
                        dosdate = (((dt[I<0>()] - (I<1980>())) << (I<9>())) | (dt[I<1>()] << (I<5>()))) | dt[I<2>()];
                        dostime = ((dt[I<3>()] << (I<11>())) | (dt[I<4>()] << (I<5>()))) | floordiv(dt[I<5>()], (I<2>()));
                        if ((self.attr("flag_bits") & (G<"zipfile._MASK_USE_DATA_DESCRIPTOR">())).truthy())
                        {
                            // Set these to zero because we write them after the file data
                            py const value12 = I<0>();
                            CRC = value12;
                            compress_size = value12;
                            file_size = value12;
                        }
                        else
                        {
                            CRC = self.attr("CRC");
                            compress_size = self.attr("compress_size");
                            file_size = self.attr("file_size");
                        }
                        extra = self.attr("extra");
                        min_version = I<0>();
                        if (zip64.is_none())
                        {
                            // We always explicitly pass zip64 within this module.... This
                            // remains for anyone using ZipInfo.FileHeader as a public API.
                            zip64 = [&]() -> py { py const t = boolean(file_size > (G<"zipfile.ZIP64_LIMIT">())); return t.truthy() ? t : boolean(compress_size > (G<"zipfile.ZIP64_LIMIT">())); }();
                        }
                        if (zip64.truthy())
                        {
                            fmt = S<"<HHQQ">();
                            extra = extra + (Mod<"struct">().attr("pack"))(fmt, I<1>(), (Mod<"struct">().attr("calcsize"))(fmt) - (I<4>()), file_size, compress_size);
                            file_size = I<4294967295ll>();
                            compress_size = I<4294967295ll>();
                            min_version = G<"zipfile.ZIP64_VERSION">();
                        }
                        if (self.attr("compress_type") == (G<"zipfile.ZIP_BZIP2">()))
                        {
                            min_version = (B<"max">())(G<"zipfile.BZIP2_VERSION">(), min_version);
                        }
                        else if (self.attr("compress_type") == (G<"zipfile.ZIP_LZMA">()))
                        {
                            min_version = (B<"max">())(G<"zipfile.LZMA_VERSION">(), min_version);
                        }
                        self.set_attr("extract_version", (B<"max">())(min_version, self.attr("extract_version")));
                        self.set_attr("create_version", (B<"max">())(min_version, self.attr("create_version")));
                        auto const items13 = unpack(self.method("_encodeFilenameFlags"), 2);
                        filename = items13[0];
                        flag_bits = items13[1];
                        header = (Mod<"struct">().attr("pack"))(G<"zipfile.structFileHeader">(), G<"zipfile.stringFileHeader">(), self.attr("extract_version"), self.attr("reserved"), flag_bits, self.attr("compress_type"), dostime, dosdate, CRC, compress_size, file_size, py{ filename.size() }, py{ extra.size() });
                        return (header + filename) + extra;
                    }) },
                { "_encodeFilenameFlags", w.method("ZipInfo", "_encodeFilenameFlags", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py encoding;
                        if ((self.attr("flag_bits") & (G<"zipfile._MASK_UTF_FILENAME">())).truthy())
                        {
                            encoding = S<"ascii">();
                        }
                        else
                        {
                            encoding = S<"cp437">();
                        }
                        try
                        {
                            return py_tuple({ self.attr("filename").method("encode", { encoding }), self.attr("flag_bits") & py{ unary(NEOS_PY_INVERT, (G<"zipfile._MASK_UTF_FILENAME">()).get()) } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler14;
                            if (matches(handler14.exception, B<"UnicodeEncodeError">()))
                            {
                                return py_tuple({ self.attr("filename").method("encode", { S<"utf-8">() }), self.attr("flag_bits") | (G<"zipfile._MASK_UTF_FILENAME">()) });
                            }
                            else
                                py_reraise(handler14.exception);
                        }
                        return none();
                    }) },
                { "_decodeExtra", w.method("ZipInfo", "_decodeExtra", { py_positional("filename_crc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename_crc = a[1];
                        py data;
                        py e;
                        py extra;
                        py field;
                        py ln;
                        py tp;
                        py unpack_;
                        py up_name_crc;
                        py up_unicode_name;
                        py up_version;
                        py warnings;
                        extra = self.attr("extra");
                        unpack_ = Mod<"struct">().attr("unpack");
                        while (py{ extra.size() } >= (I<4>()))
                        {
                            auto const items16 = unpack(unpack_(S<"<HH">(), extra[B<"slice">()(none(), I<4>(), none())]), 2);
                            tp = items16[0];
                            ln = items16[1];
                            if ((ln + (I<4>())) > py{ extra.size() })
                            {
                                py_raise((G<"zipfile.BadZipFile">())((S<"Corrupt extra field %04x (size=%d)">()) % py_tuple({ tp, ln })));
                            }
                            if (tp == (I<1>()))
                            {
                                data = extra[B<"slice">()(I<4>(), ln + (I<4>()), none())];
                                // ZIP64 extension (large files and/or large archives)
                                try
                                {
                                    if (py_tuple({ BigI<"18446744073709551615">(), I<4294967295ll>() }).contains(self.attr("file_size")))
                                    {
                                        field = S<"File size">();
                                        auto const items18 = unpack(unpack_(S<"<Q">(), data[B<"slice">()(none(), I<8>(), none())]), 1);
                                        self.set_attr("file_size", items18[0]);
                                        data = data[B<"slice">()(I<8>(), none(), none())];
                                    }
                                    if (self.attr("compress_size") == (I<4294967295ll>()))
                                    {
                                        field = S<"Compress size">();
                                        auto const items19 = unpack(unpack_(S<"<Q">(), data[B<"slice">()(none(), I<8>(), none())]), 1);
                                        self.set_attr("compress_size", items19[0]);
                                        data = data[B<"slice">()(I<8>(), none(), none())];
                                    }
                                    if (self.attr("header_offset") == (I<4294967295ll>()))
                                    {
                                        field = S<"Header offset">();
                                        auto const items20 = unpack(unpack_(S<"<Q">(), data[B<"slice">()(none(), I<8>(), none())]), 1);
                                        self.set_attr("header_offset", items20[0]);
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler17;
                                    if (matches(handler17.exception, Mod<"struct">().attr("error")))
                                    {
                                        fn_raise_from((G<"zipfile.BadZipFile">())(S(std::string{ "Corrupt zip64 extra field. " } + field.str() + " not found.")).get(), none().get());
                                        throw raised{};
                                    }
                                    else
                                        py_reraise(handler17.exception);
                                }
                            }
                            else if (tp == (I<28789>()))
                            {
                                data = extra[B<"slice">()(I<4>(), ln + (I<4>()), none())];
                                // Unicode Path Extra Field
                                try
                                {
                                    auto const items22 = unpack(unpack_(S<"<BL">(), data[B<"slice">()(none(), I<5>(), none())]), 2);
                                    up_version = items22[0];
                                    up_name_crc = items22[1];
                                    if (((up_version == (I<1>())) && (up_name_crc == filename_crc)))
                                    {
                                        up_unicode_name = data[B<"slice">()(I<5>(), none(), none())].method("decode", { S<"utf-8">() });
                                        if (up_unicode_name.truthy())
                                        {
                                            self.set_attr("filename", (G<"zipfile._sanitize_filename">())(up_unicode_name));
                                        }
                                        else
                                        {
                                            warnings = module_of("warnings");
                                            warnings.method("warn", { S<"Empty unicode path extra field (0x7075)">() }, { { "stacklevel", I<1>() } });
                                        }
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler21;
                                    if (matches(handler21.exception, Mod<"struct">().attr("error")))
                                    {
                                        e = handler21.exception;
                                        fn_raise_from((G<"zipfile.BadZipFile">())(S<"Corrupt unicode path extra field (0x7075)">()).get(), e.get());
                                        throw raised{};
                                    }
                                    else if (matches(handler21.exception, B<"UnicodeDecodeError">()))
                                    {
                                        e = handler21.exception;
                                        fn_raise_from((G<"zipfile.BadZipFile">())(S<"Corrupt unicode path extra field (0x7075): invalid utf-8 bytes">()).get(), e.get());
                                        throw raised{};
                                    }
                                    else
                                        py_reraise(handler21.exception);
                                }
                            }
                            extra = extra[B<"slice">()(ln + (I<4>()), none(), none())];
                        }
                        return none();
                    }) },
                { "from_file", w.class_method("ZipInfo", "from_file", { py_positional("filename"), py_positional("arcname", none()), py_keyword("strict_timestamps", boolean(true)) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py filename = a[1];
                        py arcname = a[2];
                        py strict_timestamps = a[3];
                        py date_time;
                        py isdir;
                        py mtime;
                        py st;
                        py zinfo;
                        if (isinstance(filename, Mod<"os">().attr("PathLike")))
                        {
                            filename = (Mod<"os">().attr("fspath"))(filename);
                        }
                        st = (Mod<"os">().attr("stat"))(filename);
                        isdir = (Mod<"stat">().attr("S_ISDIR"))(st.attr("st_mode"));
                        mtime = (Mod<"time">().attr("localtime"))(st.attr("st_mtime"));
                        date_time = mtime[B<"slice">()(I<0>(), I<6>(), none())];
                        if (((!strict_timestamps.truthy()) && (date_time[I<0>()] < (I<1980>()))))
                        {
                            date_time = py_tuple({ I<1980>(), I<1>(), I<1>(), I<0>(), I<0>(), I<0>() });
                        }
                        else if (((!strict_timestamps.truthy()) && (date_time[I<0>()] > (I<2107>()))))
                        {
                            date_time = py_tuple({ I<2107>(), I<12>(), I<31>(), I<23>(), I<59>(), I<59>() });
                        }
                        // Create ZipInfo instance to store file information
                        if (arcname.is_none())
                        {
                            arcname = filename;
                        }
                        arcname = (Mod<"os">().attr("path")).method("normpath", { (Mod<"os">().attr("path")).method("splitdrive", { arcname })[I<1>()] });
                        while (py_tuple({ Mod<"os">().attr("sep"), Mod<"os">().attr("altsep") }).contains(arcname[I<0>()]))
                        {
                            arcname = arcname[B<"slice">()(I<1>(), none(), none())];
                        }
                        if (isdir.truthy())
                        {
                            arcname = inplace(NEOS_PY_ADD, arcname, S<"/">());
                        }
                        zinfo = cls(arcname, date_time);
                        zinfo.set_attr("external_attr", (st.attr("st_mode") & (I<65535>())) << (I<16>()));
                        if (isdir.truthy())
                        {
                            zinfo.set_attr("file_size", I<0>());
                            {
                                py const target24 = zinfo;
                                target24.set_attr("external_attr", inplace(NEOS_PY_OR, target24.attr("external_attr"), I<16>()));
                            }
                        }
                        else
                        {
                            zinfo.set_attr("file_size", st.attr("st_size"));
                        }
                        return zinfo;
                    }) },
                { "is_dir", w.method("ZipInfo", "is_dir", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("filename").method("endswith", { S<"/">() }).truthy())
                        {
                            return boolean(true);
                        }
                        // The ZIP format specification requires to use forward slashes
                        // as the directory separator, but in practice some ZIP files
                        // created on Windows can use backward slashes.  For compatibility
                        // with the extraction code which already handles this:
                        if ((Mod<"os">().attr("path")).attr("altsep").truthy())
                        {
                            return self.attr("filename").method("endswith", { py_tuple({ (Mod<"os">().attr("path")).attr("sep"), (Mod<"os">().attr("path")).attr("altsep") }) });
                        }
                        return boolean(false);
                    }) }, }));

            // ZIP encryption uses the CRC32 one-byte primitive for scrambling some
            // internal keys. We noticed that a direct implementation is faster than
            // relying on binascii.crc32().
            define("zipfile", "_crctable", none());
            define("zipfile", "_gen_crc", w.function("_gen_crc", "_gen_crc", { py_positional("crc") }, [](arguments a) -> py
                {
                    py crc = a[0];
                    py j;
                    for (py const& item : iterate{ (B<"range">())(I<8>()) })
                    {
                        j = item;
                        if ((crc & (I<1>())).truthy())
                        {
                            crc = (crc >> (I<1>())) ^ (I<3988292384ll>());
                        }
                        else
                        {
                            crc = inplace(NEOS_PY_RSHIFT, crc, I<1>());
                        }
                    }
                    return crc;
                }));

            // ZIP supports a password-based form of encryption. Even though known
            // plaintext attacks have been found against it, it is still useful
            // to be able to get data out of such a file.
            // 
            // Usage:
            // zd = _ZipDecrypter(mypwd)
            // plain_bytes = zd(cypher_bytes)
            define("zipfile", "_ZipDecrypter", w.function("_ZipDecrypter", "_ZipDecrypter", { py_positional("pwd") }, [](arguments a) -> py
                {
                    py pwd = a[0];
                    auto const crc32 = std::make_shared<py>();
                    auto const crctable = std::make_shared<py>();
                    py decrypter;
                    auto const key0 = std::make_shared<py>();
                    auto const key1 = std::make_shared<py>();
                    auto const key2 = std::make_shared<py>();
                    py p;
                    auto const update_keys = std::make_shared<py>();
                    *key0 = I<305419896>();
                    *key1 = I<591751049>();
                    *key2 = I<878082192>();
                    if ((G<"zipfile._crctable">()).is_none())
                    {
                        set_global("zipfile", "_crctable", (B<"list">())((B<"map">())(G<"zipfile._gen_crc">(), (B<"range">())(I<256>()))));
                    }
                    *crctable = G<"zipfile._crctable">();
                    *crc32 = w.function("crc32", "_ZipDecrypter.<locals>.crc32", { py_positional("ch"), py_positional("crc") }, [crctable](arguments a) -> py
    {
        py ch = a[0];
        py crc = a[1];
        return (crc >> (I<8>())) ^ (*crctable)[(crc ^ ch) & (I<255>())];
    });
                    *update_keys = w.function("update_keys", "_ZipDecrypter.<locals>.update_keys", { py_positional("c") }, [crc32, key0, key1, key2](arguments a) -> py
    {
        py c = a[0];
        *key0 = (*crc32)(c, (*key0));
        *key1 = ((*key1) + ((*key0) & (I<255>()))) & (I<4294967295ll>());
        *key1 = (((*key1) * (I<134775813>())) + (I<1>())) & (I<4294967295ll>());
        *key2 = (*crc32)((*key1) >> (I<24>()), (*key2));
        return none();
    });
                    for (py const& item : iterate{ pwd })
                    {
                        p = item;
                        (*update_keys)(p);
                    }
                    decrypter = w.function("decrypter", "_ZipDecrypter.<locals>.decrypter", { py_positional("data") }, [key2, update_keys](arguments a) -> py
    {
        py data = a[0];
        py append;
        py c;
        py k;
        py result;
        result = (B<"bytearray">())();
        append = result.attr("append");
        for (py const& item : iterate{ data })
        {
            c = item;
            k = (*key2) | (I<2>());
            c = inplace(NEOS_PY_XOR, c, ((k * (k ^ (I<1>()))) >> (I<8>())) & (I<255>()));
            (*update_keys)(c);
            append(c);
        }
        return (B<"bytes">())(result);
    });
                    return decrypter;
                }));

            define("zipfile", "LZMACompressor", w.make_class("LZMACompressor", {  }, {
                { "__init__", w.method("LZMACompressor", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_comp", none());
                        return none();
                    }) },
                { "_init", w.method("LZMACompressor", "_init", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py props;
                        props = (G<"zipfile.lzma">()).method("_encode_filter_properties", { dict_of({ { S<"id">(), (G<"zipfile.lzma">()).attr("FILTER_LZMA1") } }) });
                        self.set_attr("_comp", (G<"zipfile.lzma">()).method("LZMACompressor", { (G<"zipfile.lzma">()).attr("FORMAT_RAW") }, { { "filters", py_list({ (G<"zipfile.lzma">()).method("_decode_filter_properties", { (G<"zipfile.lzma">()).attr("FILTER_LZMA1"), props }) }) } }));
                        return (Mod<"struct">().attr("pack"))(S<"<BBH">(), I<9>(), I<4>(), py{ props.size() }) + props;
                    }) },
                { "compress", w.method("LZMACompressor", "compress", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_comp").is_none())
                        {
                            return self.method("_init") + self.attr("_comp").method("compress", { data });
                        }
                        return self.attr("_comp").method("compress", { data });
                    }) },
                { "flush", w.method("LZMACompressor", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_comp").is_none())
                        {
                            return self.method("_init") + self.attr("_comp").method("flush");
                        }
                        return self.attr("_comp").method("flush");
                    }) }, }));

            define("zipfile", "LZMADecompressor", w.make_class("LZMADecompressor", {  }, {
                { "__init__", w.method("LZMADecompressor", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_decomp", none());
                        self.set_attr("_unconsumed", py{ make_bytes(std::string{ "", 0 }) });
                        self.set_attr("eof", boolean(false));
                        return none();
                    }) },
                { "needs_input", B<"property">()(w.method("LZMADecompressor", "needs_input", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_decomp").is_none())
                        {
                            return boolean(true);
                        }
                        return self.attr("_decomp").attr("needs_input");
                    })) },
                { "decompress", w.method("LZMADecompressor", "decompress", { py_positional("data"), py_positional("max_length", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py max_length = a[2];
                        py psize;
                        py result;
                        if (self.attr("_decomp").is_none())
                        {
                            {
                                py const target28 = self;
                                target28.set_attr("_unconsumed", inplace(NEOS_PY_ADD, target28.attr("_unconsumed"), data));
                            }
                            if (py{ self.attr("_unconsumed").size() } <= (I<4>()))
                            {
                                return py{ make_bytes(std::string{ "", 0 }) };
                            }
                            auto const items29 = unpack((Mod<"struct">().attr("unpack"))(S<"<H">(), self.attr("_unconsumed")[B<"slice">()(I<2>(), I<4>(), none())]), 1);
                            psize = items29[0];
                            if (py{ self.attr("_unconsumed").size() } <= ((I<4>()) + psize))
                            {
                                return py{ make_bytes(std::string{ "", 0 }) };
                            }
                            self.set_attr("_decomp", (G<"zipfile.lzma">()).method("LZMADecompressor", { (G<"zipfile.lzma">()).attr("FORMAT_RAW") }, { { "filters", py_list({ (G<"zipfile.lzma">()).method("_decode_filter_properties", { (G<"zipfile.lzma">()).attr("FILTER_LZMA1"), self.attr("_unconsumed")[B<"slice">()(I<4>(), (I<4>()) + psize, none())] }) }) } }));
                            data = self.attr("_unconsumed")[B<"slice">()((I<4>()) + psize, none(), none())];
                            del_attr(self, "_unconsumed");
                        }
                        result = self.attr("_decomp").method("decompress", { data, max_length });
                        self.set_attr("eof", self.attr("_decomp").attr("eof"));
                        return result;
                    }) }, }));

            define("zipfile", "compressor_names", dict_of({ { I<0>(), S<"store">() }, { I<1>(), S<"shrink">() }, { I<2>(), S<"reduce">() }, { I<3>(), S<"reduce">() }, { I<4>(), S<"reduce">() }, { I<5>(), S<"reduce">() }, { I<6>(), S<"implode">() }, { I<7>(), S<"tokenize">() }, { I<8>(), S<"deflate">() }, { I<9>(), S<"deflate64">() }, { I<10>(), S<"implode">() }, { I<12>(), S<"bzip2">() }, { I<14>(), S<"lzma">() }, { I<18>(), S<"terse">() }, { I<19>(), S<"lz77">() }, { I<97>(), S<"wavpack">() }, { I<98>(), S<"ppmd">() } }));
            define("zipfile", "_check_compression", w.function("_check_compression", "_check_compression", { py_positional("compression") }, [](arguments a) -> py
                {
                    py compression = a[0];
                    if (compression == (G<"zipfile.ZIP_STORED">()))
                    {
                    }
                    else if (compression == (G<"zipfile.ZIP_DEFLATED">()))
                    {
                        if (!(Mod<"zlib">()).truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Compression requires the (missing) zlib module">()));
                        }
                    }
                    else if (compression == (G<"zipfile.ZIP_BZIP2">()))
                    {
                        if (!(G<"zipfile.bz2">()).truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Compression requires the (missing) bz2 module">()));
                        }
                    }
                    else if (compression == (G<"zipfile.ZIP_LZMA">()))
                    {
                        if (!(G<"zipfile.lzma">()).truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Compression requires the (missing) lzma module">()));
                        }
                    }
                    else
                    {
                        py_raise((B<"NotImplementedError">())(S<"That compression method is not supported">()));
                    }
                    return none();
                }));

            define("zipfile", "_get_compressor", w.function("_get_compressor", "_get_compressor", { py_positional("compress_type"), py_positional("compresslevel", none()) }, [](arguments a) -> py
                {
                    py compress_type = a[0];
                    py compresslevel = a[1];
                    if (compress_type == (G<"zipfile.ZIP_DEFLATED">()))
                    {
                        if (!compresslevel.is_none())
                        {
                            return (Mod<"zlib">().attr("compressobj"))(compresslevel, Mod<"zlib">().attr("DEFLATED"), I<-15>());
                        }
                        return (Mod<"zlib">().attr("compressobj"))(Mod<"zlib">().attr("Z_DEFAULT_COMPRESSION"), Mod<"zlib">().attr("DEFLATED"), I<-15>());
                    }
                    else if (compress_type == (G<"zipfile.ZIP_BZIP2">()))
                    {
                        if (!compresslevel.is_none())
                        {
                            return (G<"zipfile.bz2">()).method("BZ2Compressor", { compresslevel });
                        }
                        return (G<"zipfile.bz2">()).method("BZ2Compressor");
                    }
                    // compresslevel is ignored for ZIP_LZMA
                    else if (compress_type == (G<"zipfile.ZIP_LZMA">()))
                    {
                        return (G<"zipfile.LZMACompressor">())();
                    }
                    else
                    {
                        return none();
                    }
                    return none();
                }));

            define("zipfile", "_get_decompressor", w.function("_get_decompressor", "_get_decompressor", { py_positional("compress_type") }, [](arguments a) -> py
                {
                    py compress_type = a[0];
                    py descr;
                    (G<"zipfile._check_compression">())(compress_type);
                    if (compress_type == (G<"zipfile.ZIP_STORED">()))
                    {
                        return none();
                    }
                    else if (compress_type == (G<"zipfile.ZIP_DEFLATED">()))
                    {
                        return (Mod<"zlib">().attr("decompressobj"))(I<-15>());
                    }
                    else if (compress_type == (G<"zipfile.ZIP_BZIP2">()))
                    {
                        return (G<"zipfile.bz2">()).method("BZ2Decompressor");
                    }
                    else if (compress_type == (G<"zipfile.ZIP_LZMA">()))
                    {
                        return (G<"zipfile.LZMADecompressor">())();
                    }
                    else
                    {
                        descr = (G<"zipfile.compressor_names">()).method("get", { compress_type });
                        if (descr.truthy())
                        {
                            py_raise((B<"NotImplementedError">())((S<"compression type %d (%s)">()) % py_tuple({ compress_type, descr })));
                        }
                        else
                        {
                            py_raise((B<"NotImplementedError">())((S<"compression type %d">()) % py_tuple({ compress_type })));
                        }
                    }
                    return none();
                }));

            define("zipfile", "_SharedFile", w.make_class("_SharedFile", {  }, {
                { "__init__", w.method("_SharedFile", "__init__", { py_positional("file"), py_positional("pos"), py_positional("close"), py_positional("lock"), py_positional("writing") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py pos = a[2];
                        py close = a[3];
                        py lock = a[4];
                        py writing = a[5];
                        self.set_attr("_file", file);
                        self.set_attr("_pos", pos);
                        self.set_attr("_close", close);
                        self.set_attr("_lock", lock);
                        self.set_attr("_writing", writing);
                        self.set_attr("seekable", file.attr("seekable"));
                        return none();
                    }) },
                { "tell", w.method("_SharedFile", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_pos");
                    }) },
                { "seek", w.method("_SharedFile", "seek", { py_positional("offset"), py_positional("whence", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py offset = a[1];
                        py whence = a[2];
                        {
                            py const manager30 = self.attr("_lock");
                            py const exit30 = pyclass::special_method(manager30, "__exit__");
                            py const value30 = pyclass::special_method(manager30, "__enter__")(manager30);
                            [[maybe_unused]] int flow30 = 0;
                            [[maybe_unused]] py result30;
                            bool exited30 = false;
                            try
                            {
                                if (self.method("_writing").truthy())
                                {
                                    py_raise((B<"ValueError">())(S<"Can't reposition in the ZIP file while there is an open writing handle on it. Close the writing handle before trying to read.">()));
                                }
                                if (whence == (Mod<"os">().attr("SEEK_CUR")))
                                {
                                    self.attr("_file").method("seek", { self.attr("_pos") + offset });
                                }
                                else
                                {
                                    self.attr("_file").method("seek", { offset, whence });
                                }
                                self.set_attr("_pos", self.attr("_file").method("tell"));
                                result30 = self.attr("_pos");
                                flow30 = 1;
                                goto finally30;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler30;
                                exited30 = true;
                                if (!exit30(manager30, type_of(handler30.exception), handler30.exception, handler30.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler30.exception);
                            }
                            finally30:
                            if (!exited30)
                                exit30(manager30, none(), none(), none());
                            if (flow30 == 1)
                                return result30;
                        }
                        return none();
                    }) },
                { "read", w.method("_SharedFile", "read", { py_positional("n", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py data;
                        {
                            py const manager31 = self.attr("_lock");
                            py const exit31 = pyclass::special_method(manager31, "__exit__");
                            py const value31 = pyclass::special_method(manager31, "__enter__")(manager31);
                            [[maybe_unused]] int flow31 = 0;
                            [[maybe_unused]] py result31;
                            bool exited31 = false;
                            try
                            {
                                if (self.method("_writing").truthy())
                                {
                                    py_raise((B<"ValueError">())(S<"Can't read from the ZIP file while there is an open writing handle on it. Close the writing handle before trying to read.">()));
                                }
                                self.attr("_file").method("seek", { self.attr("_pos") });
                                data = self.attr("_file").method("read", { n });
                                self.set_attr("_pos", self.attr("_file").method("tell"));
                                result31 = data;
                                flow31 = 1;
                                goto finally31;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler31;
                                exited31 = true;
                                if (!exit31(manager31, type_of(handler31.exception), handler31.exception, handler31.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler31.exception);
                            }
                            finally31:
                            if (!exited31)
                                exit31(manager31, none(), none(), none());
                            if (flow31 == 1)
                                return result31;
                        }
                        return none();
                    }) },
                { "close", w.method("_SharedFile", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fileobj;
                        if (!self.attr("_file").is_none())
                        {
                            fileobj = self.attr("_file");
                            self.set_attr("_file", none());
                            self.method("_close", { fileobj });
                        }
                        return none();
                    }) }, }));

            // Provide the tell method for unseekable stream
            define("zipfile", "_Tellable", w.make_class("_Tellable", {  }, {
                { "__init__", w.method("_Tellable", "__init__", { py_positional("fp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        self.set_attr("fp", fp);
                        self.set_attr("offset", I<0>());
                        return none();
                    }) },
                { "write", w.method("_Tellable", "write", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py n;
                        n = self.attr("fp").method("write", { data });
                        {
                            py const target32 = self;
                            target32.set_attr("offset", inplace(NEOS_PY_ADD, target32.attr("offset"), n));
                        }
                        return n;
                    }) },
                { "tell", w.method("_Tellable", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("offset");
                    }) },
                { "flush", w.method("_Tellable", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("fp").method("flush");
                        return none();
                    }) },
                { "close", w.method("_Tellable", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("fp").method("close");
                        return none();
                    }) }, }));

                // Max size supported by decompressor.
                // Read from compressed files in 4k blocks.
                // Chunk size to read during seek
            define("zipfile", "ZipExtFile", w.make_class("ZipExtFile", { Mod<"io">().attr("BufferedIOBase") }, {
                { "MAX_N", ((I<1>()) << (I<31>())) - (I<1>()) },
                { "MIN_READ_SIZE", I<4096>() },
                { "MAX_SEEK_READ", (I<1>()) << (I<24>()) },
                { "__init__", w.method("ZipExtFile", "__init__", { py_positional("fileobj"), py_positional("mode"), py_positional("zipinfo"), py_positional("pwd", none()), py_positional("close_fileobj", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fileobj = a[1];
                        py mode = a[2];
                        py zipinfo = a[3];
                        py pwd = a[4];
                        py close_fileobj = a[5];
                        py check_byte;
                        py h;
                        self.set_attr("_fileobj", fileobj);
                        self.set_attr("_pwd", pwd);
                        self.set_attr("_close_fileobj", close_fileobj);
                        self.set_attr("_compress_type", zipinfo.attr("compress_type"));
                        self.set_attr("_compress_left", zipinfo.attr("compress_size"));
                        self.set_attr("_left", zipinfo.attr("file_size"));
                        self.set_attr("_decompressor", (G<"zipfile._get_decompressor">())(self.attr("_compress_type")));
                        self.set_attr("_eof", boolean(false));
                        self.set_attr("_readbuffer", py{ make_bytes(std::string{ "", 0 }) });
                        self.set_attr("_offset", I<0>());
                        self.set_attr("newlines", none());
                        self.set_attr("mode", mode);
                        self.set_attr("name", zipinfo.attr("filename"));
                        if (has_attr(zipinfo, "CRC"))
                        {
                            self.set_attr("_expected_crc", zipinfo.attr("CRC"));
                            self.set_attr("_running_crc", (G<"zipfile.crc32">())(py{ make_bytes(std::string{ "", 0 }) }));
                        }
                        else
                        {
                            self.set_attr("_expected_crc", none());
                        }
                        self.set_attr("_seekable", boolean(false));
                        try
                        {
                            if (fileobj.method("seekable").truthy())
                            {
                                self.set_attr("_orig_compress_start", fileobj.method("tell"));
                                self.set_attr("_orig_compress_size", zipinfo.attr("compress_size"));
                                self.set_attr("_orig_file_size", zipinfo.attr("file_size"));
                                self.set_attr("_orig_start_crc", self.attr("_running_crc"));
                                self.set_attr("_orig_crc", self.attr("_expected_crc"));
                                self.set_attr("_seekable", boolean(true));
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler33;
                            if (matches(handler33.exception, B<"AttributeError">()))
                            {
                            }
                            else
                                py_reraise(handler33.exception);
                        }
                        self.set_attr("_decrypter", none());
                        if (pwd.truthy())
                        {
                            if ((zipinfo.attr("flag_bits") & (G<"zipfile._MASK_USE_DATA_DESCRIPTOR">())).truthy())
                            {
                                // compare against the file type from extended local headers
                                check_byte = (zipinfo.attr("_raw_time") >> (I<8>())) & (I<255>());
                            }
                            else
                            {
                                // compare against the CRC otherwise
                                check_byte = (zipinfo.attr("CRC") >> (I<24>())) & (I<255>());
                            }
                            h = self.method("_init_decrypter");
                            if (h != check_byte)
                            {
                                py_raise((B<"RuntimeError">())((S<"Bad password for file %r">()) % zipinfo.attr("orig_filename")));
                            }
                        }
                        return none();
                    }) },
                { "_init_decrypter", w.method("ZipExtFile", "_init_decrypter", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py header;
                        self.set_attr("_decrypter", (G<"zipfile._ZipDecrypter">())(self.attr("_pwd")));
                        // The first 12 bytes in the cypher stream is an encryption header
                        // used to strengthen the algorithm. The first 11 bytes are
                        // completely random, while the 12th contains the MSB of the CRC,
                        // or the MSB of the file time depending on the header type
                        // and is used to check the correctness of the password.
                        header = self.attr("_fileobj").method("read", { I<12>() });
                        {
                            py const target34 = self;
                            target34.set_attr("_compress_left", inplace(NEOS_PY_SUBTRACT, target34.attr("_compress_left"), I<12>()));
                        }
                        return self.method("_decrypter", { header })[I<11>()];
                    }) },
                { "__repr__", w.method("ZipExtFile", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result;
                        result = py_list({ (S<"<%s.%s">()) % py_tuple({ self.attr("__class__").attr("__module__"), self.attr("__class__").attr("__qualname__") }) });
                        if (!self.attr("closed").truthy())
                        {
                            result.method("append", { (S<" name=%r">()) % py_tuple({ self.attr("name") }) });
                            if (self.attr("_compress_type") != (G<"zipfile.ZIP_STORED">()))
                            {
                                result.method("append", { (S<" compress_type=%s">()) % (G<"zipfile.compressor_names">()).method("get", { self.attr("_compress_type"), self.attr("_compress_type") }) });
                            }
                        }
                        else
                        {
                            result.method("append", { S<" [closed]">() });
                        }
                        result.method("append", { S<">">() });
                        return (S<"">()).method("join", { result });
                    }) },
                { "readline", w.method("ZipExtFile", "readline", { py_positional("limit", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py limit = a[1];
                        py i;
                        py line;
                        if (limit < (I<0>()))
                        {
                            // Shortcut common case - newline found in buffer.
                            i = self.attr("_readbuffer").method("find", { py{ make_bytes(std::string{ "\x0a", 1 }) }, self.attr("_offset") }) + (I<1>());
                            if (i > (I<0>()))
                            {
                                line = self.attr("_readbuffer")[B<"slice">()(self.attr("_offset"), i, none())];
                                self.set_attr("_offset", i);
                                return line;
                            }
                        }
                        return (Mod<"io">().attr("BufferedIOBase")).method("readline", { self, limit });
                    }) },
                { "peek", w.method("ZipExtFile", "peek", { py_positional("n", I<1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py chunk;
                        if (n > (py{ self.attr("_readbuffer").size() } - self.attr("_offset")))
                        {
                            chunk = self.method("read", { n });
                            if (py{ chunk.size() } > self.attr("_offset"))
                            {
                                self.set_attr("_readbuffer", chunk + self.attr("_readbuffer")[B<"slice">()(self.attr("_offset"), none(), none())]);
                                self.set_attr("_offset", I<0>());
                            }
                            else
                            {
                                {
                                    py const target35 = self;
                                    target35.set_attr("_offset", inplace(NEOS_PY_SUBTRACT, target35.attr("_offset"), py{ chunk.size() }));
                                }
                            }
                        }
                        // Return up to 512 bytes to reduce allocation overhead for tight loops.
                        return self.attr("_readbuffer")[B<"slice">()(self.attr("_offset"), self.attr("_offset") + (I<512>()), none())];
                    }) },
                { "readable", w.method("ZipExtFile", "readable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("closed").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"I/O operation on closed file.">()));
                        }
                        return boolean(true);
                    }) },
                { "read", w.method("ZipExtFile", "read", { py_positional("n", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py buf;
                        py data;
                        py end;
                        if (self.attr("closed").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"read from closed file.">()));
                        }
                        if ((n.is_none() || (n < (I<0>()))))
                        {
                            buf = self.attr("_readbuffer")[B<"slice">()(self.attr("_offset"), none(), none())];
                            self.set_attr("_readbuffer", py{ make_bytes(std::string{ "", 0 }) });
                            self.set_attr("_offset", I<0>());
                            while (!self.attr("_eof").truthy())
                            {
                                buf = inplace(NEOS_PY_ADD, buf, self.method("_read1", { self.attr("MAX_N") }));
                            }
                            return buf;
                        }
                        end = n + self.attr("_offset");
                        if (end < py{ self.attr("_readbuffer").size() })
                        {
                            buf = self.attr("_readbuffer")[B<"slice">()(self.attr("_offset"), end, none())];
                            self.set_attr("_offset", end);
                            return buf;
                        }
                        n = end - py{ self.attr("_readbuffer").size() };
                        buf = self.attr("_readbuffer")[B<"slice">()(self.attr("_offset"), none(), none())];
                        self.set_attr("_readbuffer", py{ make_bytes(std::string{ "", 0 }) });
                        self.set_attr("_offset", I<0>());
                        while (((n > (I<0>())) && (!self.attr("_eof").truthy())))
                        {
                            data = self.method("_read1", { n });
                            if (n < py{ data.size() })
                            {
                                self.set_attr("_readbuffer", data);
                                self.set_attr("_offset", n);
                                buf = inplace(NEOS_PY_ADD, buf, data[B<"slice">()(none(), n, none())]);
                                break;
                            }
                            buf = inplace(NEOS_PY_ADD, buf, data);
                            n = inplace(NEOS_PY_SUBTRACT, n, py{ data.size() });
                        }
                        return buf;
                    }) },
                { "_update_crc", w.method("ZipExtFile", "_update_crc", { py_positional("newdata") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py newdata = a[1];
                        if (self.attr("_expected_crc").is_none())
                        {
                            // No need to compute the CRC if we don't have a reference value
                            return none();
                        }
                        self.set_attr("_running_crc", (G<"zipfile.crc32">())(newdata, self.attr("_running_crc")));
                        // Check the CRC if we're at the end of the file
                        if ((self.attr("_eof").truthy() && (self.attr("_running_crc") != self.attr("_expected_crc"))))
                        {
                            py_raise((G<"zipfile.BadZipFile">())((S<"Bad CRC-32 for file %r">()) % self.attr("name")));
                        }
                        return none();
                    }) },
                { "read1", w.method("ZipExtFile", "read1", { py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py buf;
                        py data;
                        py end;
                        if ((n.is_none() || (n < (I<0>()))))
                        {
                            buf = self.attr("_readbuffer")[B<"slice">()(self.attr("_offset"), none(), none())];
                            self.set_attr("_readbuffer", py{ make_bytes(std::string{ "", 0 }) });
                            self.set_attr("_offset", I<0>());
                            while (!self.attr("_eof").truthy())
                            {
                                data = self.method("_read1", { self.attr("MAX_N") });
                                if (data.truthy())
                                {
                                    buf = inplace(NEOS_PY_ADD, buf, data);
                                    break;
                                }
                            }
                            return buf;
                        }
                        end = n + self.attr("_offset");
                        if (end < py{ self.attr("_readbuffer").size() })
                        {
                            buf = self.attr("_readbuffer")[B<"slice">()(self.attr("_offset"), end, none())];
                            self.set_attr("_offset", end);
                            return buf;
                        }
                        n = end - py{ self.attr("_readbuffer").size() };
                        buf = self.attr("_readbuffer")[B<"slice">()(self.attr("_offset"), none(), none())];
                        self.set_attr("_readbuffer", py{ make_bytes(std::string{ "", 0 }) });
                        self.set_attr("_offset", I<0>());
                        if (n > (I<0>()))
                        {
                            while (!self.attr("_eof").truthy())
                            {
                                data = self.method("_read1", { n });
                                if (n < py{ data.size() })
                                {
                                    self.set_attr("_readbuffer", data);
                                    self.set_attr("_offset", n);
                                    buf = inplace(NEOS_PY_ADD, buf, data[B<"slice">()(none(), n, none())]);
                                    break;
                                }
                                if (data.truthy())
                                {
                                    buf = inplace(NEOS_PY_ADD, buf, data);
                                    break;
                                }
                            }
                        }
                        return buf;
                    }) },
                { "_read1", w.method("ZipExtFile", "_read1", { py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py data;
                        if ((self.attr("_eof").truthy() || (n <= (I<0>()))))
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        // Read from file.
                        if (self.attr("_compress_type") == (G<"zipfile.ZIP_DEFLATED">()))
                        {
                            // # Handle unconsumed data.
                            data = self.attr("_decompressor").attr("unconsumed_tail");
                            if (n > py{ data.size() })
                            {
                                data = inplace(NEOS_PY_ADD, data, self.method("_read2", { n - py{ data.size() } }));
                            }
                        }
                        else if (self.attr("_compress_type") == (G<"zipfile.ZIP_STORED">()))
                        {
                            data = self.method("_read2", { n });
                        }
                        // bzip2/lzma/zstd: a bounded decompress() call may leave input
                        // buffered inside the decompressor; drain that before reading more.
                        else if ((B<"getattr">())(self.attr("_decompressor"), S<"needs_input">(), boolean(true)).truthy())
                        {
                            data = self.method("_read2", { n });
                        }
                        else
                        {
                            data = py{ make_bytes(std::string{ "", 0 }) };
                        }
                        if (self.attr("_compress_type") == (G<"zipfile.ZIP_STORED">()))
                        {
                            self.set_attr("_eof", boolean(self.attr("_compress_left") <= (I<0>())));
                        }
                        else if (self.attr("_compress_type") == (G<"zipfile.ZIP_DEFLATED">()))
                        {
                            n = (B<"max">())(n, self.attr("MIN_READ_SIZE"));
                            data = self.attr("_decompressor").method("decompress", { data, n });
                            self.set_attr("_eof", [&]() -> py { py const t = self.attr("_decompressor").attr("eof"); return t.truthy() ? t : [&]() -> py { py const t = boolean(self.attr("_compress_left") <= (I<0>())); return !t.truthy() ? t : boolean(!self.attr("_decompressor").attr("unconsumed_tail").truthy()); }(); }());
                            if (self.attr("_eof").truthy())
                            {
                                data = inplace(NEOS_PY_ADD, data, self.attr("_decompressor").method("flush"));
                            }
                        }
                        else
                        {
                            // Bound the output of a single decompress() call (mirroring the
                            // DEFLATE path above) so that a small compressed member cannot
                            // expand into one unbounded read.
                            try
                            {
                                data = self.attr("_decompressor").method("decompress", { data, (B<"max">())(n, self.attr("MIN_READ_SIZE")) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler40;
                                if (matches(handler40.exception, B<"TypeError">()))
                                {
                                    // See MonkeypatchedDecompressorTests in test_core.py
                                    data = self.attr("_decompressor").method("decompress", { data });
                                }
                                else
                                    py_reraise(handler40.exception);
                            }
                            self.set_attr("_eof", [&]() -> py { py const t = self.attr("_decompressor").attr("eof"); return t.truthy() ? t : [&]() -> py { py const t = boolean(self.attr("_compress_left") <= (I<0>())); return !t.truthy() ? t : (B<"getattr">())(self.attr("_decompressor"), S<"needs_input">(), boolean(true)); }(); }());
                        }
                        data = data[B<"slice">()(none(), self.attr("_left"), none())];
                        {
                            py const target41 = self;
                            target41.set_attr("_left", inplace(NEOS_PY_SUBTRACT, target41.attr("_left"), py{ data.size() }));
                        }
                        if (self.attr("_left") <= (I<0>()))
                        {
                            self.set_attr("_eof", boolean(true));
                        }
                        self.method("_update_crc", { data });
                        return data;
                    }) },
                { "_read2", w.method("ZipExtFile", "_read2", { py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py data;
                        if (self.attr("_compress_left") <= (I<0>()))
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        n = (B<"max">())(n, self.attr("MIN_READ_SIZE"));
                        n = (B<"min">())(n, self.attr("_compress_left"));
                        data = self.attr("_fileobj").method("read", { n });
                        {
                            py const target42 = self;
                            target42.set_attr("_compress_left", inplace(NEOS_PY_SUBTRACT, target42.attr("_compress_left"), py{ data.size() }));
                        }
                        if (!data.truthy())
                        {
                            py_raise(B<"EOFError">());
                        }
                        if (!self.attr("_decrypter").is_none())
                        {
                            data = self.method("_decrypter", { data });
                        }
                        return data;
                    }) },
                { "close", w.method("ZipExtFile", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            std::optional<py> pending43;
                            [[maybe_unused]] int flow43 = 0;
                            [[maybe_unused]] py result43;
                            try
                            {
                                if (self.attr("_close_fileobj").truthy())
                                {
                                    self.attr("_fileobj").method("close");
                                }
                            }
                            catch (raised const&)
                            {
                                pending43 = py_take_exception();
                            }
                            finally43:
                            (B<"super">()(G<"zipfile.ZipExtFile">(), self)).method("close");
                            if (pending43)
                                py_reraise(*pending43);
                            if (flow43 == 1)
                                return result43;
                        }
                        return none();
                    }) },
                { "seekable", w.method("ZipExtFile", "seekable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("closed").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"I/O operation on closed file.">()));
                        }
                        return self.attr("_seekable");
                    }) },
                { "seek", w.method("ZipExtFile", "seek", { py_positional("offset"), py_positional("whence", Mod<"os">().attr("SEEK_SET")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py offset = a[1];
                        py whence = a[2];
                        py buff_offset;
                        py curr_pos;
                        py new_pos;
                        py read_len;
                        py read_offset;
                        if (self.attr("closed").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"seek on closed file.">()));
                        }
                        if (!self.attr("_seekable").truthy())
                        {
                            py_raise((Mod<"io">().attr("UnsupportedOperation"))(S<"underlying stream is not seekable">()));
                        }
                        curr_pos = self.method("tell");
                        if (whence == (Mod<"os">().attr("SEEK_SET")))
                        {
                            new_pos = offset;
                        }
                        else if (whence == (Mod<"os">().attr("SEEK_CUR")))
                        {
                            new_pos = curr_pos + offset;
                        }
                        else if (whence == (Mod<"os">().attr("SEEK_END")))
                        {
                            new_pos = self.attr("_orig_file_size") + offset;
                        }
                        else
                        {
                            py_raise((B<"ValueError">())(S<"whence must be os.SEEK_SET (0), os.SEEK_CUR (1), or os.SEEK_END (2)">()));
                        }
                        if (new_pos > self.attr("_orig_file_size"))
                        {
                            new_pos = self.attr("_orig_file_size");
                        }
                        if (new_pos < (I<0>()))
                        {
                            new_pos = I<0>();
                        }
                        read_offset = new_pos - curr_pos;
                        buff_offset = read_offset + self.attr("_offset");
                        if (((buff_offset >= (I<0>())) && (buff_offset < py{ self.attr("_readbuffer").size() })))
                        {
                            // Just move the _offset index if the new position is in the _readbuffer
                            self.set_attr("_offset", buff_offset);
                            read_offset = I<0>();
                        }
                        // Fast seek uncompressed unencrypted file
                        else if (((self.attr("_compress_type") == (G<"zipfile.ZIP_STORED">())) && self.attr("_decrypter").is_none() && (read_offset != (I<0>()))))
                        {
                            // disable CRC checking after first seeking - it would be invalid
                            self.set_attr("_expected_crc", none());
                            // seek actual file taking already buffered data into account
                            read_offset = inplace(NEOS_PY_SUBTRACT, read_offset, py{ self.attr("_readbuffer").size() } - self.attr("_offset"));
                            self.attr("_fileobj").method("seek", { read_offset, Mod<"os">().attr("SEEK_CUR") });
                            {
                                py const target44 = self;
                                target44.set_attr("_left", inplace(NEOS_PY_SUBTRACT, target44.attr("_left"), read_offset));
                            }
                            {
                                py const target45 = self;
                                target45.set_attr("_compress_left", inplace(NEOS_PY_SUBTRACT, target45.attr("_compress_left"), read_offset));
                            }
                            self.set_attr("_eof", boolean(self.attr("_left") <= (I<0>())));
                            read_offset = I<0>();
                            // flush read buffer
                            self.set_attr("_readbuffer", py{ make_bytes(std::string{ "", 0 }) });
                            self.set_attr("_offset", I<0>());
                        }
                        else if (read_offset < (I<0>()))
                        {
                            // Position is before the current position. Reset the ZipExtFile
                            self.attr("_fileobj").method("seek", { self.attr("_orig_compress_start") });
                            self.set_attr("_running_crc", self.attr("_orig_start_crc"));
                            self.set_attr("_expected_crc", self.attr("_orig_crc"));
                            self.set_attr("_compress_left", self.attr("_orig_compress_size"));
                            self.set_attr("_left", self.attr("_orig_file_size"));
                            self.set_attr("_readbuffer", py{ make_bytes(std::string{ "", 0 }) });
                            self.set_attr("_offset", I<0>());
                            self.set_attr("_decompressor", (G<"zipfile._get_decompressor">())(self.attr("_compress_type")));
                            self.set_attr("_eof", boolean(false));
                            read_offset = new_pos;
                            if (!self.attr("_decrypter").is_none())
                            {
                                self.method("_init_decrypter");
                            }
                        }
                        while (read_offset > (I<0>()))
                        {
                            read_len = (B<"min">())(self.attr("MAX_SEEK_READ"), read_offset);
                            self.method("read", { read_len });
                            read_offset = inplace(NEOS_PY_SUBTRACT, read_offset, read_len);
                        }
                        return self.method("tell");
                    }) },
                { "tell", w.method("ZipExtFile", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filepos;
                        if (self.attr("closed").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"tell on closed file.">()));
                        }
                        if (!self.attr("_seekable").truthy())
                        {
                            py_raise((Mod<"io">().attr("UnsupportedOperation"))(S<"underlying stream is not seekable">()));
                        }
                        filepos = ((self.attr("_orig_file_size") - self.attr("_left")) - py{ self.attr("_readbuffer").size() }) + self.attr("_offset");
                        return filepos;
                    }) }, }));

            define("zipfile", "_ZipWriteFile", w.make_class("_ZipWriteFile", { Mod<"io">().attr("BufferedIOBase") }, {
                { "__init__", w.method("_ZipWriteFile", "__init__", { py_positional("zf"), py_positional("zinfo"), py_positional("zip64") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py zf = a[1];
                        py zinfo = a[2];
                        py zip64 = a[3];
                        self.set_attr("_zinfo", zinfo);
                        self.set_attr("_zip64", zip64);
                        self.set_attr("_zipfile", zf);
                        self.set_attr("_compressor", (G<"zipfile._get_compressor">())(zinfo.attr("compress_type"), zinfo.attr("compress_level")));
                        self.set_attr("_file_size", I<0>());
                        self.set_attr("_compress_size", I<0>());
                        self.set_attr("_crc", I<0>());
                        return none();
                    }) },
                { "_fileobj", B<"property">()(w.method("_ZipWriteFile", "_fileobj", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_zipfile").attr("fp");
                    })) },
                { "name", B<"property">()(w.method("_ZipWriteFile", "name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_zinfo").attr("filename");
                    })) },
                { "mode", B<"property">()(w.method("_ZipWriteFile", "mode", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S<"wb">();
                    })) },
                { "writable", w.method("_ZipWriteFile", "writable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) },
                { "write", w.method("_ZipWriteFile", "write", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py nbytes;
                        if (self.attr("closed").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"I/O operation on closed file.">()));
                        }
                        // Accept any data that supports the buffer protocol
                        if (isinstance(data, py_tuple({ B<"bytes">(), B<"bytearray">() })))
                        {
                            nbytes = py{ data.size() };
                        }
                        else
                        {
                            data = (B<"memoryview">())(data);
                            nbytes = data.attr("nbytes");
                        }
                        {
                            py const target47 = self;
                            target47.set_attr("_file_size", inplace(NEOS_PY_ADD, target47.attr("_file_size"), nbytes));
                        }
                        self.set_attr("_crc", (G<"zipfile.crc32">())(data, self.attr("_crc")));
                        if (self.attr("_compressor").truthy())
                        {
                            data = self.attr("_compressor").method("compress", { data });
                            {
                                py const target48 = self;
                                target48.set_attr("_compress_size", inplace(NEOS_PY_ADD, target48.attr("_compress_size"), py{ data.size() }));
                            }
                        }
                        self.attr("_fileobj").method("write", { data });
                        return nbytes;
                    }) },
                { "close", w.method("_ZipWriteFile", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py buf;
                        py fmt;
                        if (self.attr("closed").truthy())
                        {
                            return none();
                        }
                        {
                            std::optional<py> pending49;
                            [[maybe_unused]] int flow49 = 0;
                            [[maybe_unused]] py result49;
                            try
                            {
                                (B<"super">()(G<"zipfile._ZipWriteFile">(), self)).method("close");
                                // Flush any data from the compressor, and update header info
                                if (self.attr("_compressor").truthy())
                                {
                                    buf = self.attr("_compressor").method("flush");
                                    {
                                        py const target50 = self;
                                        target50.set_attr("_compress_size", inplace(NEOS_PY_ADD, target50.attr("_compress_size"), py{ buf.size() }));
                                    }
                                    self.attr("_fileobj").method("write", { buf });
                                    self.attr("_zinfo").set_attr("compress_size", self.attr("_compress_size"));
                                }
                                else
                                {
                                    self.attr("_zinfo").set_attr("compress_size", self.attr("_file_size"));
                                }
                                self.attr("_zinfo").set_attr("CRC", self.attr("_crc"));
                                self.attr("_zinfo").set_attr("file_size", self.attr("_file_size"));
                                if (!self.attr("_zip64").truthy())
                                {
                                    if (self.attr("_file_size") > (G<"zipfile.ZIP64_LIMIT">()))
                                    {
                                        py_raise((B<"RuntimeError">())(S<"File size too large, try using force_zip64">()));
                                    }
                                    if (self.attr("_compress_size") > (G<"zipfile.ZIP64_LIMIT">()))
                                    {
                                        py_raise((B<"RuntimeError">())(S<"Compressed size too large, try using force_zip64">()));
                                    }
                                }
                                // Write updated header info
                                if ((self.attr("_zinfo").attr("flag_bits") & (G<"zipfile._MASK_USE_DATA_DESCRIPTOR">())).truthy())
                                {
                                    // Write CRC and file sizes after the file data
                                    fmt = (self.attr("_zip64").truthy() ? S<"<LLQQ">() : S<"<LLLL">());
                                    self.attr("_fileobj").method("write", { (Mod<"struct">().attr("pack"))(fmt, G<"zipfile._DD_SIGNATURE">(), self.attr("_zinfo").attr("CRC"), self.attr("_zinfo").attr("compress_size"), self.attr("_zinfo").attr("file_size")) });
                                    self.attr("_zipfile").set_attr("start_dir", self.attr("_fileobj").method("tell"));
                                }
                                else
                                {
                                    // Seek backwards and write file header (which will now include
                                    // correct CRC and file sizes)
                                    // Preserve current position in file
                                    self.attr("_zipfile").set_attr("start_dir", self.attr("_fileobj").method("tell"));
                                    self.attr("_fileobj").method("seek", { self.attr("_zinfo").attr("header_offset") });
                                    self.attr("_fileobj").method("write", { self.attr("_zinfo").method("FileHeader", { self.attr("_zip64") }) });
                                    self.attr("_fileobj").method("seek", { self.attr("_zipfile").attr("start_dir") });
                                }
                                // Successfully written: Add file to our caches
                                self.attr("_zipfile").attr("filelist").method("append", { self.attr("_zinfo") });
                                self.attr("_zipfile").attr("NameToInfo").set_item(self.attr("_zinfo").attr("filename"), self.attr("_zinfo"));
                            }
                            catch (raised const&)
                            {
                                pending49 = py_take_exception();
                            }
                            finally49:
                            self.attr("_zipfile").set_attr("_writing", boolean(false));
                            if (pending49)
                                py_reraise(*pending49);
                            if (flow49 == 1)
                                return result49;
                        }
                        return none();
                    }) }, }));

            define("zipfile", "ZipFile", w.make_class("ZipFile", {  }, {
                { "fp", none() },
                { "_windows_illegal_name_trans_table", none() },
                { "_ignore_invalid_names", boolean(false) },
                { "__init__", w.method("ZipFile", "__init__", { py_positional("file"), py_positional("mode", S<"r">()), py_positional("compression", G<"zipfile.ZIP_STORED">()), py_positional("allowZip64", boolean(true)), py_positional("compresslevel", none()), py_keyword("strict_timestamps", boolean(true)), py_keyword("metadata_encoding", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py mode = a[2];
                        py compression = a[3];
                        py allowZip64 = a[4];
                        py compresslevel = a[5];
                        py strict_timestamps = a[6];
                        py metadata_encoding = a[7];
                        py filemode;
                        py fp;
                        py modeDict;
                        if (!py_tuple({ S<"r">(), S<"w">(), S<"x">(), S<"a">() }).contains(mode))
                        {
                            py_raise((B<"ValueError">())(S<"ZipFile requires mode 'r', 'w', 'x', or 'a'">()));
                        }
                        (G<"zipfile._check_compression">())(compression);
                        self.set_attr("_allowZip64", allowZip64);
                        self.set_attr("_didModify", boolean(false));
                        self.set_attr("debug", I<0>());
                        self.set_attr("NameToInfo", py_dict());
                        self.set_attr("filelist", py_list());
                        self.set_attr("compression", compression);
                        self.set_attr("compresslevel", compresslevel);
                        self.set_attr("mode", mode);
                        self.set_attr("pwd", none());
                        self.set_attr("_comment", py{ make_bytes(std::string{ "", 0 }) });
                        self.set_attr("_strict_timestamps", strict_timestamps);
                        self.set_attr("metadata_encoding", metadata_encoding);
                        // Check that we don't try to write with nonconforming codecs
                        if ((self.attr("metadata_encoding").truthy() && (mode != (S<"r">()))))
                        {
                            py_raise((B<"ValueError">())(S<"metadata_encoding is only supported for reading files">()));
                        }
                        // Check if we were passed a file-like object
                        if (isinstance(file, Mod<"os">().attr("PathLike")))
                        {
                            file = (Mod<"os">().attr("fspath"))(file);
                        }
                        if (isinstance(file, B<"str">()))
                        {
                            // No, it's a filename
                            self.set_attr("_filePassed", I<0>());
                            self.set_attr("filename", file);
                            modeDict = dict_of({ { S<"r">(), S<"rb">() }, { S<"w">(), S<"w+b">() }, { S<"x">(), S<"x+b">() }, { S<"a">(), S<"r+b">() }, { S<"r+b">(), S<"w+b">() }, { S<"w+b">(), S<"wb">() }, { S<"x+b">(), S<"xb">() } });
                            filemode = modeDict[mode];
                            while (boolean(true).truthy())
                            {
                                try
                                {
                                    self.set_attr("fp", (Mod<"io">().attr("open"))(file, filemode));
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler52;
                                    if (matches(handler52.exception, B<"OSError">()))
                                    {
                                        if (modeDict.contains(filemode))
                                        {
                                            filemode = modeDict[filemode];
                                            continue;
                                        }
                                        py_reraise(handler52.exception);
                                    }
                                    else
                                        py_reraise(handler52.exception);
                                }
                                break;
                            }
                        }
                        else
                        {
                            self.set_attr("_filePassed", I<1>());
                            self.set_attr("fp", file);
                            self.set_attr("filename", (B<"getattr">())(file, S<"name">(), none()));
                        }
                        self.set_attr("_fileRefCnt", I<1>());
                        self.set_attr("_lock", (Mod<"threading">().attr("RLock"))());
                        self.set_attr("_seekable", boolean(true));
                        self.set_attr("_writing", boolean(false));
                        try
                        {
                            if (mode == (S<"r">()))
                            {
                                self.method("_RealGetContents");
                            }
                            else if (py_tuple({ S<"w">(), S<"x">() }).contains(mode))
                            {
                                // set the modified flag so central directory gets written
                                // even if no files are added to the archive
                                self.set_attr("_didModify", boolean(true));
                                {
                                    bool otherwise54 = false;
                                    try
                                    {
                                        self.set_attr("start_dir", self.attr("fp").method("tell"));
                                        otherwise54 = true;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler54;
                                        if (matches(handler54.exception, py_tuple({ B<"AttributeError">(), B<"OSError">() })))
                                        {
                                            self.set_attr("fp", (G<"zipfile._Tellable">())(self.attr("fp")));
                                            self.set_attr("start_dir", I<0>());
                                            self.set_attr("_seekable", boolean(false));
                                        }
                                        else
                                            py_reraise(handler54.exception);
                                    }
                                    if (otherwise54)
                                    {
                                        // Some file-like objects can provide tell() but not seek()
                                        try
                                        {
                                            self.attr("fp").method("seek", { self.attr("start_dir") });
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler55;
                                            if (matches(handler55.exception, py_tuple({ B<"AttributeError">(), B<"OSError">() })))
                                            {
                                                self.set_attr("_seekable", boolean(false));
                                            }
                                            else
                                                py_reraise(handler55.exception);
                                        }
                                    }
                                }
                            }
                            else if (mode == (S<"a">()))
                            {
                                try
                                {
                                    // See if file is a zip file
                                    self.method("_RealGetContents");
                                    // seek to start of directory and overwrite
                                    self.attr("fp").method("seek", { self.attr("start_dir") });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler56;
                                    if (matches(handler56.exception, G<"zipfile.BadZipFile">()))
                                    {
                                        // file is not a zip file, just append
                                        self.attr("fp").method("seek", { I<0>(), I<2>() });
                                        // set the modified flag so central directory gets written
                                        // even if no files are added to the archive
                                        self.set_attr("_didModify", boolean(true));
                                        self.set_attr("start_dir", self.attr("fp").method("tell"));
                                    }
                                    else
                                        py_reraise(handler56.exception);
                                }
                            }
                            else
                            {
                                py_raise((B<"ValueError">())(S<"Mode must be 'r', 'w', 'x', or 'a'">()));
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler53;
                            {
                                fp = self.attr("fp");
                                self.set_attr("fp", none());
                                self.method("_fpclose", { fp });
                                py_reraise(handler53.exception);
                            }
                        }
                        return none();
                    }) },
                { "__enter__", w.method("ZipFile", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("ZipFile", "__exit__", { py_positional("type"), py_positional("value"), py_positional("traceback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py type = a[1];
                        py value = a[2];
                        py traceback = a[3];
                        self.method("close");
                        return none();
                    }) },
                { "__repr__", w.method("ZipFile", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result;
                        result = py_list({ (S<"<%s.%s">()) % py_tuple({ self.attr("__class__").attr("__module__"), self.attr("__class__").attr("__qualname__") }) });
                        if (!self.attr("fp").is_none())
                        {
                            if (self.attr("_filePassed").truthy())
                            {
                                result.method("append", { (S<" file=%r">()) % self.attr("fp") });
                            }
                            else if (!self.attr("filename").is_none())
                            {
                                result.method("append", { (S<" filename=%r">()) % self.attr("filename") });
                            }
                            result.method("append", { (S<" mode=%r">()) % self.attr("mode") });
                        }
                        else
                        {
                            result.method("append", { S<" [closed]">() });
                        }
                        result.method("append", { S<">">() });
                        return (S<"">()).method("join", { result });
                    }) },
                { "_RealGetContents", w.method("ZipFile", "_RealGetContents", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py centdir;
                        py concat;
                        py d;
                        py data;
                        py end_offset;
                        py endrec;
                        py filename;
                        py flags;
                        py fp;
                        py inferred;
                        py offset_cd;
                        py orig_filename_crc;
                        py size_cd;
                        py t;
                        py total;
                        py x;
                        py zinfo;
                        fp = self.attr("fp");
                        try
                        {
                            endrec = (G<"zipfile._EndRecData">())(fp);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler57;
                            if (matches(handler57.exception, B<"OSError">()))
                            {
                                py_raise((G<"zipfile.BadZipFile">())(S<"File is not a zip file">()));
                            }
                            else
                                py_reraise(handler57.exception);
                        }
                        if (!endrec.truthy())
                        {
                            py_raise((G<"zipfile.BadZipFile">())(S<"File is not a zip file">()));
                        }
                        if (self.attr("debug") > (I<1>()))
                        {
                            (B<"print">())(endrec);
                        }
                        size_cd = endrec[G<"zipfile._ECD_SIZE">()];
                        offset_cd = endrec[G<"zipfile._ECD_OFFSET">()];
                        self.set_attr("_comment", endrec[G<"zipfile._ECD_COMMENT">()]);
                        // "concat" is zero, unless zip was concatenated to another file
                        concat = (endrec[G<"zipfile._ECD_LOCATION">()] - size_cd) - offset_cd;
                        if (self.attr("debug") > (I<2>()))
                        {
                            inferred = concat + offset_cd;
                            (B<"print">())(S<"given, inferred, offset">(), offset_cd, inferred, concat);
                        }
                        // self.start_dir:  Position of start of central directory
                        self.set_attr("start_dir", offset_cd + concat);
                        if (self.attr("start_dir") < (I<0>()))
                        {
                            py_raise((G<"zipfile.BadZipFile">())(S<"Bad offset for central directory">()));
                        }
                        fp.method("seek", { self.attr("start_dir"), I<0>() });
                        data = fp.method("read", { size_cd });
                        fp = (Mod<"io">().attr("BytesIO"))(data);
                        total = I<0>();
                        while (total < size_cd)
                        {
                            centdir = fp.method("read", { G<"zipfile.sizeCentralDir">() });
                            if (py{ centdir.size() } != (G<"zipfile.sizeCentralDir">()))
                            {
                                py_raise((G<"zipfile.BadZipFile">())(S<"Truncated central directory">()));
                            }
                            centdir = (Mod<"struct">().attr("unpack"))(G<"zipfile.structCentralDir">(), centdir);
                            if (centdir[G<"zipfile._CD_SIGNATURE">()] != (G<"zipfile.stringCentralDir">()))
                            {
                                py_raise((G<"zipfile.BadZipFile">())(S<"Bad magic number for central directory">()));
                            }
                            if (self.attr("debug") > (I<2>()))
                            {
                                (B<"print">())(centdir);
                            }
                            filename = fp.method("read", { centdir[G<"zipfile._CD_FILENAME_LENGTH">()] });
                            orig_filename_crc = (G<"zipfile.crc32">())(filename);
                            flags = centdir[G<"zipfile._CD_FLAG_BITS">()];
                            if ((flags & (G<"zipfile._MASK_UTF_FILENAME">())).truthy())
                            {
                                // UTF-8 file names extension
                                filename = filename.method("decode", { S<"utf-8">() });
                            }
                            else
                            {
                                // Historical ZIP filename encoding
                                filename = filename.method("decode", { [&]() -> py { py const t = self.attr("metadata_encoding"); return t.truthy() ? t : S<"cp437">(); }() });
                            }
                            // Create ZipInfo instance to store file information
                            x = (G<"zipfile.ZipInfo">())(filename);
                            x.set_attr("extra", fp.method("read", { centdir[G<"zipfile._CD_EXTRA_FIELD_LENGTH">()] }));
                            x.set_attr("comment", fp.method("read", { centdir[G<"zipfile._CD_COMMENT_LENGTH">()] }));
                            x.set_attr("header_offset", centdir[G<"zipfile._CD_LOCAL_HEADER_OFFSET">()]);
                            auto const items59 = unpack(centdir[B<"slice">()(I<1>(), I<12>(), none())], 11);
                            x.set_attr("create_version", items59[0]);
                            x.set_attr("create_system", items59[1]);
                            x.set_attr("extract_version", items59[2]);
                            x.set_attr("reserved", items59[3]);
                            x.set_attr("flag_bits", items59[4]);
                            x.set_attr("compress_type", items59[5]);
                            t = items59[6];
                            d = items59[7];
                            x.set_attr("CRC", items59[8]);
                            x.set_attr("compress_size", items59[9]);
                            x.set_attr("file_size", items59[10]);
                            if (x.attr("extract_version") > (G<"zipfile.MAX_EXTRACT_VERSION">()))
                            {
                                py_raise((B<"NotImplementedError">())((S<"zip file version %.1f">()) % (x.attr("extract_version") / (I<10>()))));
                            }
                            auto const items60 = unpack(centdir[B<"slice">()(I<15>(), I<18>(), none())], 3);
                            x.set_attr("volume", items60[0]);
                            x.set_attr("internal_attr", items60[1]);
                            x.set_attr("external_attr", items60[2]);
                            // Convert date/time code to (year, month, day, hour, min, sec)
                            x.set_attr("_raw_time", t);
                            x.set_attr("date_time", py_tuple({ (d >> (I<9>())) + (I<1980>()), (d >> (I<5>())) & (I<15>()), d & (I<31>()), t >> (I<11>()), (t >> (I<5>())) & (I<63>()), (t & (I<31>())) * (I<2>()) }));
                            x.method("_decodeExtra", { orig_filename_crc });
                            x.set_attr("header_offset", x.attr("header_offset") + concat);
                            self.attr("filelist").method("append", { x });
                            self.attr("NameToInfo").set_item(x.attr("filename"), x);
                            // update total bytes read from central directory
                            total = (((total + (G<"zipfile.sizeCentralDir">())) + centdir[G<"zipfile._CD_FILENAME_LENGTH">()]) + centdir[G<"zipfile._CD_EXTRA_FIELD_LENGTH">()]) + centdir[G<"zipfile._CD_COMMENT_LENGTH">()];
                            if (self.attr("debug") > (I<2>()))
                            {
                                (B<"print">())(S<"total">(), total);
                            }
                        }
                        end_offset = self.attr("start_dir");
                        for (py const& item : iterate{ (B<"reversed">())((B<"sorted">()).call({ self.attr("filelist") }, { { "key", w.function("<lambda>", "ZipFile._RealGetContents.<locals>.<lambda>", { py_positional("zinfo") }, [](arguments a) -> py
    {
        py zinfo = a[0];
        return zinfo.attr("header_offset");
    }) } })) })
                        {
                            zinfo = item;
                            zinfo.set_attr("_end_offset", end_offset);
                            end_offset = zinfo.attr("header_offset");
                        }
                        return none();
                    }) },
                { "namelist", w.method("ZipFile", "namelist", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension62 = py_list(); py data; for (py const& item : iterate{ self.attr("filelist") }) { data = item; comprehension62.method("append", { data.attr("filename") }); } return comprehension62; }();
                    }) },
                { "infolist", w.method("ZipFile", "infolist", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("filelist");
                    }) },
                { "printdir", w.method("ZipFile", "printdir", { py_positional("file", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py date;
                        py zinfo;
                        (B<"print">()).call({ (S<"%-46s %19s %12s">()) % py_tuple({ S<"File Name">(), S<"Modified    ">(), S<"Size">() }) }, { { "file", file } });
                        for (py const& item : iterate{ self.attr("filelist") })
                        {
                            zinfo = item;
                            date = (S<"%d-%02d-%02d %02d:%02d:%02d">()) % zinfo.attr("date_time")[B<"slice">()(none(), I<6>(), none())];
                            (B<"print">()).call({ (S<"%-46s %s %12d">()) % py_tuple({ zinfo.attr("filename"), date, zinfo.attr("file_size") }) }, { { "file", file } });
                        }
                        return none();
                    }) },
                { "testzip", w.method("ZipFile", "testzip", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py chunk_size;
                        py f;
                        py zinfo;
                        chunk_size = power((I<2>()), (I<20>()));
                        for (py const& item : iterate{ self.attr("filelist") })
                        {
                            zinfo = item;
                            try
                            {
                                // Read by chunks, to avoid an OverflowError or a
                                // MemoryError with very large embedded files.
                                {
                                    py const manager66 = self.method("open", { zinfo.attr("filename"), S<"r">() });
                                    py const exit66 = pyclass::special_method(manager66, "__exit__");
                                    py const value66 = pyclass::special_method(manager66, "__enter__")(manager66);
                                    f = value66;
                                    [[maybe_unused]] int flow66 = 0;
                                    [[maybe_unused]] py result66;
                                    bool exited66 = false;
                                    try
                                    {
                                        while (f.method("read", { chunk_size }).truthy())
                                        {
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler66;
                                        exited66 = true;
                                        if (!exit66(manager66, type_of(handler66.exception), handler66.exception, handler66.exception.attr("__traceback__")).truthy())
                                            py_reraise(handler66.exception);
                                    }
                                    finally66:
                                    if (!exited66)
                                        exit66(manager66, none(), none(), none());
                                    if (flow66 == 1)
                                        return result66;
                                    if (flow66 == 2)
                                        break;
                                    if (flow66 == 3)
                                        continue;
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler65;
                                if (matches(handler65.exception, G<"zipfile.BadZipFile">()))
                                {
                                    return zinfo.attr("filename");
                                }
                                else
                                    py_reraise(handler65.exception);
                            }
                        }
                        return none();
                    }) },
                { "getinfo", w.method("ZipFile", "getinfo", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py info;
                        info = self.attr("NameToInfo").method("get", { name });
                        if (info.is_none())
                        {
                            py_raise((B<"KeyError">())((S<"There is no item named %r in the archive">()) % name));
                        }
                        return info;
                    }) },
                { "setpassword", w.method("ZipFile", "setpassword", { py_positional("pwd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pwd = a[1];
                        if ((pwd.truthy() && (!isinstance(pwd, B<"bytes">()))))
                        {
                            py_raise((B<"TypeError">())((S<"pwd: expected bytes, got %s">()) % (B<"type">())(pwd).attr("__name__")));
                        }
                        if (pwd.truthy())
                        {
                            self.set_attr("pwd", pwd);
                        }
                        else
                        {
                            self.set_attr("pwd", none());
                        }
                        return none();
                    }) },
                { "comment", B<"property">()(w.method("ZipFile", "comment", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_comment");
                    }), w.method("ZipFile", "comment", { py_positional("comment") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py comment = a[1];
                        py warnings;
                        if (!isinstance(comment, B<"bytes">()))
                        {
                            py_raise((B<"TypeError">())((S<"comment: expected bytes, got %s">()) % (B<"type">())(comment).attr("__name__")));
                        }
                        // check for valid comment length
                        if (py{ comment.size() } > (G<"zipfile.ZIP_MAX_COMMENT">()))
                        {
                            warnings = module_of("warnings");
                            warnings.method("warn", { (S<"Archive comment is too long; truncating to %d bytes">()) % (G<"zipfile.ZIP_MAX_COMMENT">()) }, { { "stacklevel", I<1>() } });
                            comment = comment[B<"slice">()(none(), G<"zipfile.ZIP_MAX_COMMENT">(), none())];
                        }
                        self.set_attr("_comment", comment);
                        self.set_attr("_didModify", boolean(true));
                        return none();
                    })) },
                { "read", w.method("ZipFile", "read", { py_positional("name"), py_positional("pwd", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py pwd = a[2];
                        py fp;
                        {
                            py const manager68 = self.method("open", { name, S<"r">(), pwd });
                            py const exit68 = pyclass::special_method(manager68, "__exit__");
                            py const value68 = pyclass::special_method(manager68, "__enter__")(manager68);
                            fp = value68;
                            [[maybe_unused]] int flow68 = 0;
                            [[maybe_unused]] py result68;
                            bool exited68 = false;
                            try
                            {
                                result68 = fp.method("read");
                                flow68 = 1;
                                goto finally68;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler68;
                                exited68 = true;
                                if (!exit68(manager68, type_of(handler68.exception), handler68.exception, handler68.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler68.exception);
                            }
                            finally68:
                            if (!exited68)
                                exit68(manager68, none(), none(), none());
                            if (flow68 == 1)
                                return result68;
                        }
                        return none();
                    }) },
                { "open", w.method("ZipFile", "open", { py_positional("name"), py_positional("mode", S<"r">()), py_positional("pwd", none()), py_keyword("force_zip64", boolean(false)) }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py name = a[1];
                        py mode = a[2];
                        py pwd = a[3];
                        py force_zip64 = a[4];
                        py fheader;
                        py fname;
                        py fname_str;
                        py is_encrypted;
                        py warnings;
                        py zef_file;
                        py zinfo;
                        if (!set_of({ S<"r">(), S<"w">() }).contains(mode))
                        {
                            py_raise((B<"ValueError">())(S<"open() requires mode \"r\" or \"w\"">()));
                        }
                        if ((pwd.truthy() && (mode == (S<"w">()))))
                        {
                            py_raise((B<"ValueError">())(S<"pwd is only supported for reading files">()));
                        }
                        if (!(*self).attr("fp").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Attempt to use ZIP archive that was already closed">()));
                        }
                        // Make sure we have an info object
                        if (isinstance(name, G<"zipfile.ZipInfo">()))
                        {
                            // 'name' is already an info object
                            zinfo = name;
                        }
                        else if (mode == (S<"w">()))
                        {
                            zinfo = (G<"zipfile.ZipInfo">())(name);
                            zinfo.set_attr("compress_type", (*self).attr("compression"));
                            zinfo.set_attr("compress_level", (*self).attr("compresslevel"));
                        }
                        else
                        {
                            // Get info object for name
                            zinfo = (*self).method("getinfo", { name });
                        }
                        if (mode == (S<"w">()))
                        {
                            return (*self).method("_open_to_write", { zinfo }, { { "force_zip64", force_zip64 } });
                        }
                        if ((*self).attr("_writing").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Can't read from the ZIP file while there is an open writing handle on it. Close the writing handle before trying to read.">()));
                        }
                        // Open for reading:
                        {
                            py const target69 = (*self);
                            target69.set_attr("_fileRefCnt", inplace(NEOS_PY_ADD, target69.attr("_fileRefCnt"), I<1>()));
                        }
                        zef_file = (G<"zipfile._SharedFile">())((*self).attr("fp"), zinfo.attr("header_offset"), (*self).attr("_fpclose"), (*self).attr("_lock"), w.function("<lambda>", "ZipFile.open.<locals>.<lambda>", {  }, [self](arguments a) -> py
    {
        return (*self).attr("_writing");
    }));
                        try
                        {
                            // Skip the file header:
                            fheader = zef_file.method("read", { G<"zipfile.sizeFileHeader">() });
                            if (py{ fheader.size() } != (G<"zipfile.sizeFileHeader">()))
                            {
                                py_raise((G<"zipfile.BadZipFile">())(S<"Truncated file header">()));
                            }
                            fheader = (Mod<"struct">().attr("unpack"))(G<"zipfile.structFileHeader">(), fheader);
                            if (fheader[G<"zipfile._FH_SIGNATURE">()] != (G<"zipfile.stringFileHeader">()))
                            {
                                py_raise((G<"zipfile.BadZipFile">())(S<"Bad magic number for file header">()));
                            }
                            fname = zef_file.method("read", { fheader[G<"zipfile._FH_FILENAME_LENGTH">()] });
                            if (fheader[G<"zipfile._FH_EXTRA_FIELD_LENGTH">()].truthy())
                            {
                                zef_file.method("seek", { fheader[G<"zipfile._FH_EXTRA_FIELD_LENGTH">()] }, { { "whence", I<1>() } });
                            }
                            if ((zinfo.attr("flag_bits") & (G<"zipfile._MASK_COMPRESSED_PATCH">())).truthy())
                            {
                                // Zip 2.7: compressed patched data
                                py_raise((B<"NotImplementedError">())(S<"compressed patched data (flag bit 5)">()));
                            }
                            if ((zinfo.attr("flag_bits") & (G<"zipfile._MASK_STRONG_ENCRYPTION">())).truthy())
                            {
                                // strong encryption
                                py_raise((B<"NotImplementedError">())(S<"strong encryption (flag bit 6)">()));
                            }
                            if ((fheader[G<"zipfile._FH_GENERAL_PURPOSE_FLAG_BITS">()] & (G<"zipfile._MASK_UTF_FILENAME">())).truthy())
                            {
                                // UTF-8 filename
                                fname_str = fname.method("decode", { S<"utf-8">() });
                            }
                            else
                            {
                                fname_str = fname.method("decode", { [&]() -> py { py const t = (*self).attr("metadata_encoding"); return t.truthy() ? t : S<"cp437">(); }() });
                            }
                            if (fname_str != zinfo.attr("orig_filename"))
                            {
                                py_raise((G<"zipfile.BadZipFile">())((S<"File name in directory %r and header %r differ.">()) % py_tuple({ zinfo.attr("orig_filename"), fname })));
                            }
                            if (((!zinfo.attr("_end_offset").is_none()) && ((zef_file.method("tell") + zinfo.attr("compress_size")) > zinfo.attr("_end_offset"))))
                            {
                                if (zinfo.attr("_end_offset") == zinfo.attr("header_offset"))
                                {
                                    warnings = module_of("warnings");
                                    warnings.method("warn", { S(std::string{ "Overlapped entries: " } + zinfo.attr("orig_filename").repr() + " (possible zip bomb)") }, { { "skip_file_prefixes", py_tuple({}) } });
                                }
                                else
                                {
                                    py_raise((G<"zipfile.BadZipFile">())(S(std::string{ "Overlapped entries: " } + zinfo.attr("orig_filename").repr() + " (possible zip bomb)")));
                                }
                            }
                            // check for encrypted flag & handle password
                            is_encrypted = zinfo.attr("flag_bits") & (G<"zipfile._MASK_ENCRYPTED">());
                            if (is_encrypted.truthy())
                            {
                                if (!pwd.truthy())
                                {
                                    pwd = (*self).attr("pwd");
                                }
                                if ((pwd.truthy() && (!isinstance(pwd, B<"bytes">()))))
                                {
                                    py_raise((B<"TypeError">())((S<"pwd: expected bytes, got %s">()) % (B<"type">())(pwd).attr("__name__")));
                                }
                                if (!pwd.truthy())
                                {
                                    py_raise((B<"RuntimeError">())((S<"File %r is encrypted, password required for extraction">()) % name));
                                }
                            }
                            else
                            {
                                pwd = none();
                            }
                            return (G<"zipfile.ZipExtFile">())(zef_file, mode + (S<"b">()), zinfo, pwd, boolean(true));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler70;
                            {
                                zef_file.method("close");
                                py_reraise(handler70.exception);
                            }
                        }
                        return none();
                    }) },
                { "_open_to_write", w.method("ZipFile", "_open_to_write", { py_positional("zinfo"), py_positional("force_zip64", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py zinfo = a[1];
                        py force_zip64 = a[2];
                        py zip64;
                        if ((force_zip64.truthy() && (!self.attr("_allowZip64").truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"force_zip64 is True, but allowZip64 was False when opening the ZIP file.">()));
                        }
                        if (self.attr("_writing").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Can't write to the ZIP file while there is another write handle open on it. Close the first handle before opening another.">()));
                        }
                        // Size and CRC are overwritten with correct data after processing the file
                        zinfo.set_attr("compress_size", I<0>());
                        zinfo.set_attr("CRC", I<0>());
                        zinfo.set_attr("flag_bits", G<"zipfile._MASK_UTF_FILENAME">());
                        if (zinfo.attr("compress_type") == (G<"zipfile.ZIP_LZMA">()))
                        {
                            // Compressed data includes an end-of-stream (EOS) marker
                            {
                                py const target71 = zinfo;
                                target71.set_attr("flag_bits", inplace(NEOS_PY_OR, target71.attr("flag_bits"), G<"zipfile._MASK_COMPRESS_OPTION_1">()));
                            }
                        }
                        if (!self.attr("_seekable").truthy())
                        {
                            {
                                py const target72 = zinfo;
                                target72.set_attr("flag_bits", inplace(NEOS_PY_OR, target72.attr("flag_bits"), G<"zipfile._MASK_USE_DATA_DESCRIPTOR">()));
                            }
                        }
                        if (!zinfo.attr("external_attr").truthy())
                        {
                            zinfo.set_attr("external_attr", (I<384>()) << (I<16>()));
                        }
                        // Compressed size can be larger than uncompressed size
                        zip64 = [&]() -> py { py const t = force_zip64; return t.truthy() ? t : boolean((zinfo.attr("file_size") * (F<1.05>())) > (G<"zipfile.ZIP64_LIMIT">())); }();
                        if (((!self.attr("_allowZip64").truthy()) && zip64.truthy()))
                        {
                            py_raise((G<"zipfile.LargeZipFile">())(S<"Filesize would require ZIP64 extensions">()));
                        }
                        if (self.attr("_seekable").truthy())
                        {
                            self.attr("fp").method("seek", { self.attr("start_dir") });
                        }
                        zinfo.set_attr("header_offset", self.attr("fp").method("tell"));
                        self.method("_writecheck", { zinfo });
                        self.set_attr("_didModify", boolean(true));
                        self.attr("fp").method("write", { zinfo.method("FileHeader", { zip64 }) });
                        self.set_attr("_writing", boolean(true));
                        return (G<"zipfile._ZipWriteFile">())(self, zinfo, zip64);
                    }) },
                { "extract", w.method("ZipFile", "extract", { py_positional("member"), py_positional("path", none()), py_positional("pwd", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py member = a[1];
                        py path = a[2];
                        py pwd = a[3];
                        if (path.is_none())
                        {
                            path = (Mod<"os">().attr("getcwd"))();
                        }
                        else
                        {
                            path = (Mod<"os">().attr("fspath"))(path);
                        }
                        return self.method("_extract_member", { member, path, pwd });
                    }) },
                { "extractall", w.method("ZipFile", "extractall", { py_positional("path", none()), py_positional("members", none()), py_positional("pwd", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py members = a[2];
                        py pwd = a[3];
                        py zipinfo;
                        if (members.is_none())
                        {
                            members = self.method("namelist");
                        }
                        if (path.is_none())
                        {
                            path = (Mod<"os">().attr("getcwd"))();
                        }
                        else
                        {
                            path = (Mod<"os">().attr("fspath"))(path);
                        }
                        for (py const& item : iterate{ members })
                        {
                            zipinfo = item;
                            self.method("_extract_member", { zipinfo, path, pwd });
                        }
                        return none();
                    }) },
                { "_sanitize_windows_name", w.class_method("ZipFile", "_sanitize_windows_name", { py_positional("arcname"), py_positional("pathsep") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py arcname = a[1];
                        py pathsep = a[2];
                        py illegal;
                        py table;
                        table = cls.attr("_windows_illegal_name_trans_table");
                        if (!table.truthy())
                        {
                            illegal = S<":<>|\"?*">();
                            table = (B<"str">()).method("maketrans", { illegal, (S<"_">()) * py{ illegal.size() } });
                            cls.set_attr("_windows_illegal_name_trans_table", table);
                        }
                        arcname = arcname.method("translate", { table });
                        // remove trailing dots and spaces
                        arcname = [&]() -> py { py const comprehension74 = py_list(); py x; for (py const& item : iterate{ arcname.method("split", { pathsep }) }) { x = item; comprehension74.method("append", { x.method("rstrip", { S<" .">() }) }); } return comprehension74; }();
                        // rejoin, removing empty parts.
                        arcname = pathsep.method("join", { [&]() -> py { py const comprehension75 = py_list(); py x; for (py const& item : iterate{ arcname }) { x = item; if (!x.truthy()) continue; comprehension75.method("append", { x }); } return comprehension75; }() });
                        return arcname;
                    }) },
                { "_extract_member", w.method("ZipFile", "_extract_member", { py_positional("member"), py_positional("targetpath"), py_positional("pwd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py member = a[1];
                        py targetpath = a[2];
                        py pwd = a[3];
                        py arcname;
                        py arcname2;
                        py drive;
                        py invalid_path_parts;
                        py root;
                        py source;
                        py target;
                        py upperdirs;
                        if (!isinstance(member, G<"zipfile.ZipInfo">()))
                        {
                            member = self.method("getinfo", { member });
                        }
                        // build the destination pathname, replacing
                        // forward slashes to platform specific separators.
                        arcname = member.attr("filename");
                        if ((Mod<"os">().attr("path")).attr("sep") != (S<"/">()))
                        {
                            arcname = arcname.method("replace", { S<"/">(), (Mod<"os">().attr("path")).attr("sep") });
                        }
                        if (((Mod<"os">().attr("path")).attr("altsep").truthy() && ((Mod<"os">().attr("path")).attr("altsep") != (S<"/">()))))
                        {
                            arcname = arcname.method("replace", { (Mod<"os">().attr("path")).attr("altsep"), (Mod<"os">().attr("path")).attr("sep") });
                        }
                        // interpret absolute pathname as relative, remove drive letter or
                        // UNC path, redundant separators, "." and ".." components.
                        auto const items76 = unpack((Mod<"os">().attr("path")).method("splitroot", { arcname }), 3);
                        drive = items76[0];
                        root = items76[1];
                        arcname = items76[2];
                        if ((self.attr("_ignore_invalid_names").truthy() && ([&]() -> py { py const t = drive; return t.truthy() ? t : root; }()).truthy()))
                        {
                            return none();
                        }
                        if ((self.attr("_ignore_invalid_names").truthy() && arcname.method("split", { (Mod<"os">().attr("path")).attr("sep") }).contains((Mod<"os">().attr("path")).attr("pardir"))))
                        {
                            return none();
                        }
                        invalid_path_parts = py_tuple({ S<"">(), (Mod<"os">().attr("path")).attr("curdir"), (Mod<"os">().attr("path")).attr("pardir") });
                        arcname = (Mod<"os">().attr("path")).attr("sep").method("join", { [&]() -> py { py const comprehension77 = py_list(); py x; for (py const& item : iterate{ arcname.method("split", { (Mod<"os">().attr("path")).attr("sep") }) }) { x = item; if (invalid_path_parts.contains(x)) continue; comprehension77.method("append", { x }); } return comprehension77; }() });
                        if ((Mod<"os">().attr("path")).attr("sep") == (S<"\\">()))
                        {
                            // filter illegal characters on Windows
                            arcname2 = self.method("_sanitize_windows_name", { arcname, (Mod<"os">().attr("path")).attr("sep") });
                            if ((self.attr("_ignore_invalid_names").truthy() && (arcname2 != arcname)))
                            {
                                return none();
                            }
                            arcname = arcname2;
                        }
                        if (((!arcname.truthy()) && (!member.method("is_dir").truthy())))
                        {
                            if (self.attr("_ignore_invalid_names").truthy())
                            {
                                return none();
                            }
                            py_raise((B<"ValueError">())(S<"Empty filename.">()));
                        }
                        targetpath = (Mod<"os">().attr("path")).method("join", { targetpath, arcname });
                        targetpath = (Mod<"os">().attr("path")).method("normpath", { targetpath });
                        // Create all upper directories if necessary.
                        upperdirs = (Mod<"os">().attr("path")).method("dirname", { targetpath });
                        if ((upperdirs.truthy() && (!(Mod<"os">().attr("path")).method("exists", { upperdirs }).truthy())))
                        {
                            (Mod<"os">().attr("makedirs")).call({ upperdirs }, { { "exist_ok", boolean(true) } });
                        }
                        if (member.method("is_dir").truthy())
                        {
                            if (!(Mod<"os">().attr("path")).method("isdir", { targetpath }).truthy())
                            {
                                try
                                {
                                    (Mod<"os">().attr("mkdir"))(targetpath);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler78;
                                    if (matches(handler78.exception, B<"FileExistsError">()))
                                    {
                                        if (!(Mod<"os">().attr("path")).method("isdir", { targetpath }).truthy())
                                        {
                                            py_reraise(handler78.exception);
                                        }
                                    }
                                    else
                                        py_reraise(handler78.exception);
                                }
                            }
                            return targetpath;
                        }
                        {
                            py const manager79 = self.method("open", { member }, { { "pwd", pwd } });
                            py const exit79 = pyclass::special_method(manager79, "__exit__");
                            py const value79 = pyclass::special_method(manager79, "__enter__")(manager79);
                            source = value79;
                            [[maybe_unused]] int flow79 = 0;
                            [[maybe_unused]] py result79;
                            bool exited79 = false;
                            try
                            {
                                {
                                    py const manager80 = (B<"open">())(targetpath, S<"wb">());
                                    py const exit80 = pyclass::special_method(manager80, "__exit__");
                                    py const value80 = pyclass::special_method(manager80, "__enter__")(manager80);
                                    target = value80;
                                    [[maybe_unused]] int flow80 = 0;
                                    [[maybe_unused]] py result80;
                                    bool exited80 = false;
                                    try
                                    {
                                        (Mod<"shutil">().attr("copyfileobj"))(source, target);
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler80;
                                        exited80 = true;
                                        if (!exit80(manager80, type_of(handler80.exception), handler80.exception, handler80.exception.attr("__traceback__")).truthy())
                                            py_reraise(handler80.exception);
                                    }
                                    finally80:
                                    if (!exited80)
                                        exit80(manager80, none(), none(), none());
                                    if (flow80 == 1)
                                        { result79 = result80; flow79 = 1; goto finally79; }
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler79;
                                exited79 = true;
                                if (!exit79(manager79, type_of(handler79.exception), handler79.exception, handler79.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler79.exception);
                            }
                            finally79:
                            if (!exited79)
                                exit79(manager79, none(), none(), none());
                            if (flow79 == 1)
                                return result79;
                        }
                        return targetpath;
                    }) },
                { "_writecheck", w.method("ZipFile", "_writecheck", { py_positional("zinfo") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py zinfo = a[1];
                        py requires_zip64;
                        py warnings;
                        if (self.attr("NameToInfo").contains(zinfo.attr("filename")))
                        {
                            warnings = module_of("warnings");
                            warnings.method("warn", { (S<"Duplicate name: %r">()) % zinfo.attr("filename") }, { { "stacklevel", I<2>() } });
                        }
                        if (!py_tuple({ S<"w">(), S<"x">(), S<"a">() }).contains(self.attr("mode")))
                        {
                            py_raise((B<"ValueError">())(S<"write() requires mode 'w', 'x', or 'a'">()));
                        }
                        if (!self.attr("fp").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Attempt to write ZIP archive that was already closed">()));
                        }
                        (G<"zipfile._check_compression">())(zinfo.attr("compress_type"));
                        if (!self.attr("_allowZip64").truthy())
                        {
                            requires_zip64 = none();
                            if (py{ self.attr("filelist").size() } >= (G<"zipfile.ZIP_FILECOUNT_LIMIT">()))
                            {
                                requires_zip64 = S<"Files count">();
                            }
                            else if (zinfo.attr("file_size") > (G<"zipfile.ZIP64_LIMIT">()))
                            {
                                requires_zip64 = S<"Filesize">();
                            }
                            else if (zinfo.attr("header_offset") > (G<"zipfile.ZIP64_LIMIT">()))
                            {
                                requires_zip64 = S<"Zipfile size">();
                            }
                            if (requires_zip64.truthy())
                            {
                                py_raise((G<"zipfile.LargeZipFile">())(requires_zip64 + (S<" would require ZIP64 extensions">())));
                            }
                        }
                        return none();
                    }) },
                { "write", w.method("ZipFile", "write", { py_positional("filename"), py_positional("arcname", none()), py_positional("compress_type", none()), py_positional("compresslevel", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py arcname = a[2];
                        py compress_type = a[3];
                        py compresslevel = a[4];
                        py dest;
                        py src;
                        py zinfo;
                        if (!self.attr("fp").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Attempt to write to ZIP archive that was already closed">()));
                        }
                        if (self.attr("_writing").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Can't write to ZIP archive while an open writing handle exists">()));
                        }
                        zinfo = (G<"zipfile.ZipInfo">()).method("from_file", { filename, arcname }, { { "strict_timestamps", self.attr("_strict_timestamps") } });
                        if (zinfo.method("is_dir").truthy())
                        {
                            zinfo.set_attr("compress_size", I<0>());
                            zinfo.set_attr("CRC", I<0>());
                            self.method("mkdir", { zinfo });
                        }
                        else
                        {
                            if (!compress_type.is_none())
                            {
                                zinfo.set_attr("compress_type", compress_type);
                            }
                            else
                            {
                                zinfo.set_attr("compress_type", self.attr("compression"));
                            }
                            if (!compresslevel.is_none())
                            {
                                zinfo.set_attr("compress_level", compresslevel);
                            }
                            else
                            {
                                zinfo.set_attr("compress_level", self.attr("compresslevel"));
                            }
                            {
                                py const manager81 = (B<"open">())(filename, S<"rb">());
                                py const exit81 = pyclass::special_method(manager81, "__exit__");
                                py const value81 = pyclass::special_method(manager81, "__enter__")(manager81);
                                src = value81;
                                [[maybe_unused]] int flow81 = 0;
                                [[maybe_unused]] py result81;
                                bool exited81 = false;
                                try
                                {
                                    {
                                        py const manager82 = self.method("open", { zinfo, S<"w">() });
                                        py const exit82 = pyclass::special_method(manager82, "__exit__");
                                        py const value82 = pyclass::special_method(manager82, "__enter__")(manager82);
                                        dest = value82;
                                        [[maybe_unused]] int flow82 = 0;
                                        [[maybe_unused]] py result82;
                                        bool exited82 = false;
                                        try
                                        {
                                            (Mod<"shutil">().attr("copyfileobj"))(src, dest, (I<1024>()) * (I<8>()));
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler82;
                                            exited82 = true;
                                            if (!exit82(manager82, type_of(handler82.exception), handler82.exception, handler82.exception.attr("__traceback__")).truthy())
                                                py_reraise(handler82.exception);
                                        }
                                        finally82:
                                        if (!exited82)
                                            exit82(manager82, none(), none(), none());
                                        if (flow82 == 1)
                                            { result81 = result82; flow81 = 1; goto finally81; }
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler81;
                                    exited81 = true;
                                    if (!exit81(manager81, type_of(handler81.exception), handler81.exception, handler81.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler81.exception);
                                }
                                finally81:
                                if (!exited81)
                                    exit81(manager81, none(), none(), none());
                                if (flow81 == 1)
                                    return result81;
                            }
                        }
                        return none();
                    }) },
                { "writestr", w.method("ZipFile", "writestr", { py_positional("zinfo_or_arcname"), py_positional("data"), py_positional("compress_type", none()), py_positional("compresslevel", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py zinfo_or_arcname = a[1];
                        py data = a[2];
                        py compress_type = a[3];
                        py compresslevel = a[4];
                        py dest;
                        py zinfo;
                        if (isinstance(data, B<"str">()))
                        {
                            data = data.method("encode", { S<"utf-8">() });
                        }
                        if (!isinstance(zinfo_or_arcname, G<"zipfile.ZipInfo">()))
                        {
                            zinfo = (G<"zipfile.ZipInfo">()).call({  }, { { "filename", zinfo_or_arcname }, { "date_time", (Mod<"time">().attr("localtime"))((Mod<"time">().attr("time"))())[B<"slice">()(none(), I<6>(), none())] } });
                            zinfo.set_attr("compress_type", self.attr("compression"));
                            zinfo.set_attr("compress_level", self.attr("compresslevel"));
                            if (zinfo.attr("filename").method("endswith", { S<"/">() }).truthy())
                            {
                                zinfo.set_attr("external_attr", (I<16893>()) << (I<16>()));
                                {
                                    py const target83 = zinfo;
                                    target83.set_attr("external_attr", inplace(NEOS_PY_OR, target83.attr("external_attr"), I<16>()));
                                }
                            }
                            else
                            {
                                zinfo.set_attr("external_attr", (I<384>()) << (I<16>()));
                            }
                        }
                        else
                        {
                            zinfo = zinfo_or_arcname;
                        }
                        if (!self.attr("fp").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Attempt to write to ZIP archive that was already closed">()));
                        }
                        if (self.attr("_writing").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Can't write to ZIP archive while an open writing handle exists.">()));
                        }
                        if (!compress_type.is_none())
                        {
                            zinfo.set_attr("compress_type", compress_type);
                        }
                        if (!compresslevel.is_none())
                        {
                            zinfo.set_attr("compress_level", compresslevel);
                        }
                        zinfo.set_attr("file_size", py{ data.size() });
                        {
                            py const manager84 = self.attr("_lock");
                            py const exit84 = pyclass::special_method(manager84, "__exit__");
                            py const value84 = pyclass::special_method(manager84, "__enter__")(manager84);
                            [[maybe_unused]] int flow84 = 0;
                            [[maybe_unused]] py result84;
                            bool exited84 = false;
                            try
                            {
                                {
                                    py const manager85 = self.method("open", { zinfo }, { { "mode", S<"w">() } });
                                    py const exit85 = pyclass::special_method(manager85, "__exit__");
                                    py const value85 = pyclass::special_method(manager85, "__enter__")(manager85);
                                    dest = value85;
                                    [[maybe_unused]] int flow85 = 0;
                                    [[maybe_unused]] py result85;
                                    bool exited85 = false;
                                    try
                                    {
                                        dest.method("write", { data });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler85;
                                        exited85 = true;
                                        if (!exit85(manager85, type_of(handler85.exception), handler85.exception, handler85.exception.attr("__traceback__")).truthy())
                                            py_reraise(handler85.exception);
                                    }
                                    finally85:
                                    if (!exited85)
                                        exit85(manager85, none(), none(), none());
                                    if (flow85 == 1)
                                        { result84 = result85; flow84 = 1; goto finally84; }
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler84;
                                exited84 = true;
                                if (!exit84(manager84, type_of(handler84.exception), handler84.exception, handler84.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler84.exception);
                            }
                            finally84:
                            if (!exited84)
                                exit84(manager84, none(), none(), none());
                            if (flow84 == 1)
                                return result84;
                        }
                        return none();
                    }) },
                { "mkdir", w.method("ZipFile", "mkdir", { py_positional("zinfo_or_directory_name"), py_positional("mode", I<511>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py zinfo_or_directory_name = a[1];
                        py mode = a[2];
                        py directory_name;
                        py zinfo;
                        if (isinstance(zinfo_or_directory_name, G<"zipfile.ZipInfo">()))
                        {
                            zinfo = zinfo_or_directory_name;
                            if (!zinfo.method("is_dir").truthy())
                            {
                                py_raise((B<"ValueError">())(S<"The given ZipInfo does not describe a directory">()));
                            }
                        }
                        else if (isinstance(zinfo_or_directory_name, B<"str">()))
                        {
                            directory_name = zinfo_or_directory_name;
                            if (!directory_name.method("endswith", { S<"/">() }).truthy())
                            {
                                directory_name = inplace(NEOS_PY_ADD, directory_name, S<"/">());
                            }
                            zinfo = (G<"zipfile.ZipInfo">())(directory_name);
                            zinfo.set_attr("compress_size", I<0>());
                            zinfo.set_attr("CRC", I<0>());
                            zinfo.set_attr("external_attr", (((I<16384>()) | mode) & (I<65535>())) << (I<16>()));
                            zinfo.set_attr("file_size", I<0>());
                            {
                                py const target86 = zinfo;
                                target86.set_attr("external_attr", inplace(NEOS_PY_OR, target86.attr("external_attr"), I<16>()));
                            }
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"Expected type str or ZipInfo">()));
                        }
                        {
                            py const manager87 = self.attr("_lock");
                            py const exit87 = pyclass::special_method(manager87, "__exit__");
                            py const value87 = pyclass::special_method(manager87, "__enter__")(manager87);
                            [[maybe_unused]] int flow87 = 0;
                            [[maybe_unused]] py result87;
                            bool exited87 = false;
                            try
                            {
                                if (self.attr("_seekable").truthy())
                                {
                                    self.attr("fp").method("seek", { self.attr("start_dir") });
                                }
                                zinfo.set_attr("header_offset", self.attr("fp").method("tell"));
                                if (zinfo.attr("compress_type") == (G<"zipfile.ZIP_LZMA">()))
                                {
                                    // Compressed data includes an end-of-stream (EOS) marker
                                    {
                                        py const target88 = zinfo;
                                        target88.set_attr("flag_bits", inplace(NEOS_PY_OR, target88.attr("flag_bits"), G<"zipfile._MASK_COMPRESS_OPTION_1">()));
                                    }
                                }
                                self.method("_writecheck", { zinfo });
                                self.set_attr("_didModify", boolean(true));
                                self.attr("filelist").method("append", { zinfo });
                                self.attr("NameToInfo").set_item(zinfo.attr("filename"), zinfo);
                                self.attr("fp").method("write", { zinfo.method("FileHeader", { boolean(false) }) });
                                self.set_attr("start_dir", self.attr("fp").method("tell"));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler87;
                                exited87 = true;
                                if (!exit87(manager87, type_of(handler87.exception), handler87.exception, handler87.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler87.exception);
                            }
                            finally87:
                            if (!exited87)
                                exit87(manager87, none(), none(), none());
                            if (flow87 == 1)
                                return result87;
                        }
                        return none();
                    }) },
                { "__del__", w.method("ZipFile", "__del__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("close");
                        return none();
                    }) },
                { "close", w.method("ZipFile", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp;
                        if (self.attr("fp").is_none())
                        {
                            return none();
                        }
                        if (self.attr("_writing").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Can't close the ZIP file while there is an open writing handle on it. Close the writing handle before closing the zip.">()));
                        }
                        {
                            std::optional<py> pending89;
                            [[maybe_unused]] int flow89 = 0;
                            [[maybe_unused]] py result89;
                            try
                            {
                                if ((py_tuple({ S<"w">(), S<"x">(), S<"a">() }).contains(self.attr("mode")) && self.attr("_didModify").truthy()))
                                {
                                    {
                                        py const manager90 = self.attr("_lock");
                                        py const exit90 = pyclass::special_method(manager90, "__exit__");
                                        py const value90 = pyclass::special_method(manager90, "__enter__")(manager90);
                                        [[maybe_unused]] int flow90 = 0;
                                        [[maybe_unused]] py result90;
                                        bool exited90 = false;
                                        try
                                        {
                                            if (self.attr("_seekable").truthy())
                                            {
                                                self.attr("fp").method("seek", { self.attr("start_dir") });
                                            }
                                            self.method("_write_end_record");
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler90;
                                            exited90 = true;
                                            if (!exit90(manager90, type_of(handler90.exception), handler90.exception, handler90.exception.attr("__traceback__")).truthy())
                                                py_reraise(handler90.exception);
                                        }
                                        finally90:
                                        if (!exited90)
                                            exit90(manager90, none(), none(), none());
                                        if (flow90 == 1)
                                            { result89 = result90; flow89 = 1; goto finally89; }
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending89 = py_take_exception();
                            }
                            finally89:
                            fp = self.attr("fp");
                            self.set_attr("fp", none());
                            self.method("_fpclose", { fp });
                            if (pending89)
                                py_reraise(*pending89);
                            if (flow89 == 1)
                                return result89;
                        }
                        return none();
                    }) },
                { "_write_end_record", w.method("ZipFile", "_write_end_record", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py centDirCount;
                        py centDirOffset;
                        py centDirSize;
                        py centdir;
                        py compress_size;
                        py create_version;
                        py dosdate;
                        py dostime;
                        py dt;
                        py endrec;
                        py extra;
                        py extra_data;
                        py extract_version;
                        py file_size;
                        py filename;
                        py flag_bits;
                        py header_offset;
                        py min_version;
                        py pos2;
                        py requires_zip64;
                        py zinfo;
                        py zip64endrec;
                        py zip64locrec;
                        for (py const& item : iterate{ self.attr("filelist") })
                        {
                            zinfo = item;
                            dt = zinfo.attr("date_time");
                            dosdate = (((dt[I<0>()] - (I<1980>())) << (I<9>())) | (dt[I<1>()] << (I<5>()))) | dt[I<2>()];
                            dostime = ((dt[I<3>()] << (I<11>())) | (dt[I<4>()] << (I<5>()))) | floordiv(dt[I<5>()], (I<2>()));
                            extra = py_list();
                            if (((zinfo.attr("file_size") > (G<"zipfile.ZIP64_LIMIT">())) || (zinfo.attr("compress_size") > (G<"zipfile.ZIP64_LIMIT">()))))
                            {
                                extra.method("append", { zinfo.attr("file_size") });
                                extra.method("append", { zinfo.attr("compress_size") });
                                file_size = I<4294967295ll>();
                                compress_size = I<4294967295ll>();
                            }
                            else
                            {
                                file_size = zinfo.attr("file_size");
                                compress_size = zinfo.attr("compress_size");
                            }
                            if (zinfo.attr("header_offset") > (G<"zipfile.ZIP64_LIMIT">()))
                            {
                                extra.method("append", { zinfo.attr("header_offset") });
                                header_offset = I<4294967295ll>();
                            }
                            else
                            {
                                header_offset = zinfo.attr("header_offset");
                            }
                            extra_data = zinfo.attr("extra");
                            min_version = I<0>();
                            if (extra.truthy())
                            {
                                // Append a ZIP64 field to the extra's
                                extra_data = (G<"zipfile._Extra">()).method("strip", { extra_data, py_tuple({ I<1>() }) });
                                extra_data = call_ex((Mod<"struct">().attr("pack")), { (S<"<HH">()) + ((S<"Q">()) * py{ extra.size() }), I<1>(), (I<8>()) * py{ extra.size() } }, extra, py{}) + extra_data;
                                min_version = G<"zipfile.ZIP64_VERSION">();
                            }
                            if (zinfo.attr("compress_type") == (G<"zipfile.ZIP_BZIP2">()))
                            {
                                min_version = (B<"max">())(G<"zipfile.BZIP2_VERSION">(), min_version);
                            }
                            else if (zinfo.attr("compress_type") == (G<"zipfile.ZIP_LZMA">()))
                            {
                                min_version = (B<"max">())(G<"zipfile.LZMA_VERSION">(), min_version);
                            }
                            extract_version = (B<"max">())(min_version, zinfo.attr("extract_version"));
                            create_version = (B<"max">())(min_version, zinfo.attr("create_version"));
                            auto const items92 = unpack(zinfo.method("_encodeFilenameFlags"), 2);
                            filename = items92[0];
                            flag_bits = items92[1];
                            centdir = (Mod<"struct">().attr("pack"))(G<"zipfile.structCentralDir">(), G<"zipfile.stringCentralDir">(), create_version, zinfo.attr("create_system"), extract_version, zinfo.attr("reserved"), flag_bits, zinfo.attr("compress_type"), dostime, dosdate, zinfo.attr("CRC"), compress_size, file_size, py{ filename.size() }, py{ extra_data.size() }, py{ zinfo.attr("comment").size() }, I<0>(), zinfo.attr("internal_attr"), zinfo.attr("external_attr"), header_offset);
                            self.attr("fp").method("write", { centdir });
                            self.attr("fp").method("write", { filename });
                            self.attr("fp").method("write", { extra_data });
                            self.attr("fp").method("write", { zinfo.attr("comment") });
                        }
                        pos2 = self.attr("fp").method("tell");
                        // Write end-of-zip-archive record
                        centDirCount = py{ self.attr("filelist").size() };
                        centDirSize = pos2 - self.attr("start_dir");
                        centDirOffset = self.attr("start_dir");
                        requires_zip64 = none();
                        if (centDirCount > (G<"zipfile.ZIP_FILECOUNT_LIMIT">()))
                        {
                            requires_zip64 = S<"Files count">();
                        }
                        else if (centDirOffset > (G<"zipfile.ZIP64_LIMIT">()))
                        {
                            requires_zip64 = S<"Central directory offset">();
                        }
                        else if (centDirSize > (G<"zipfile.ZIP64_LIMIT">()))
                        {
                            requires_zip64 = S<"Central directory size">();
                        }
                        if (requires_zip64.truthy())
                        {
                            // Need to write the ZIP64 end-of-archive records
                            if (!self.attr("_allowZip64").truthy())
                            {
                                py_raise((G<"zipfile.LargeZipFile">())(requires_zip64 + (S<" would require ZIP64 extensions">())));
                            }
                            zip64endrec = (Mod<"struct">().attr("pack"))(G<"zipfile.structEndArchive64">(), G<"zipfile.stringEndArchive64">(), (G<"zipfile.sizeEndCentDir64">()) - (I<12>()), I<45>(), I<45>(), I<0>(), I<0>(), centDirCount, centDirCount, centDirSize, centDirOffset);
                            self.attr("fp").method("write", { zip64endrec });
                            zip64locrec = (Mod<"struct">().attr("pack"))(G<"zipfile.structEndArchive64Locator">(), G<"zipfile.stringEndArchive64Locator">(), I<0>(), pos2, I<1>());
                            self.attr("fp").method("write", { zip64locrec });
                            centDirCount = (B<"min">())(centDirCount, I<65535>());
                            centDirSize = (B<"min">())(centDirSize, I<4294967295ll>());
                            centDirOffset = (B<"min">())(centDirOffset, I<4294967295ll>());
                        }
                        endrec = (Mod<"struct">().attr("pack"))(G<"zipfile.structEndArchive">(), G<"zipfile.stringEndArchive">(), I<0>(), I<0>(), centDirCount, centDirCount, centDirSize, centDirOffset, py{ self.attr("_comment").size() });
                        self.attr("fp").method("write", { endrec });
                        self.attr("fp").method("write", { self.attr("_comment") });
                        if (self.attr("mode") == (S<"a">()))
                        {
                            self.attr("fp").method("truncate");
                        }
                        self.attr("fp").method("flush");
                        return none();
                    }) },
                { "_fpclose", w.method("ZipFile", "_fpclose", { py_positional("fp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        if (!(self.attr("_fileRefCnt") > (I<0>())))
                            py_raise(B<"AssertionError">()());
                        {
                            py const target93 = self;
                            target93.set_attr("_fileRefCnt", inplace(NEOS_PY_SUBTRACT, target93.attr("_fileRefCnt"), I<1>()));
                        }
                        if (((!self.attr("_fileRefCnt").truthy()) && (!self.attr("_filePassed").truthy())))
                        {
                            fp.method("close");
                        }
                        return none();
                    }) }, }));

            define("zipfile", "PyZipFile", w.make_class("PyZipFile", { G<"zipfile.ZipFile">() }, {
                { "__init__", w.method("PyZipFile", "__init__", { py_positional("file"), py_positional("mode", S<"r">()), py_positional("compression", G<"zipfile.ZIP_STORED">()), py_positional("allowZip64", boolean(true)), py_positional("optimize", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py mode = a[2];
                        py compression = a[3];
                        py allowZip64 = a[4];
                        py optimize = a[5];
                        (G<"zipfile.ZipFile">()).method("__init__", { self, file }, { { "mode", mode }, { "compression", compression }, { "allowZip64", allowZip64 } });
                        self.set_attr("_optimize", optimize);
                        return none();
                    }) },
                { "writepy", w.method("PyZipFile", "writepy", { py_positional("pathname"), py_positional("basename", S<"">()), py_positional("filterfunc", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pathname = a[1];
                        py basename = a[2];
                        py filterfunc = a[3];
                        py arcname;
                        py dir;
                        py dirlist;
                        py ext;
                        py filename;
                        py fname;
                        py initname;
                        py label;
                        py name;
                        py path;
                        py root;
                        pathname = (Mod<"os">().attr("fspath"))(pathname);
                        if ((filterfunc.truthy() && (!filterfunc(pathname).truthy())))
                        {
                            if (self.attr("debug").truthy())
                            {
                                label = ((Mod<"os">().attr("path")).method("isdir", { pathname }).truthy() ? S<"path">() : S<"file">());
                                (B<"print">())((S<"%s %r skipped by filterfunc">()) % py_tuple({ label, pathname }));
                            }
                            return none();
                        }
                        auto const items94 = unpack((Mod<"os">().attr("path")).method("split", { pathname }), 2);
                        dir = items94[0];
                        name = items94[1];
                        if ((Mod<"os">().attr("path")).method("isdir", { pathname }).truthy())
                        {
                            initname = (Mod<"os">().attr("path")).method("join", { pathname, S<"__init__.py">() });
                            if ((Mod<"os">().attr("path")).method("isfile", { initname }).truthy())
                            {
                                // This is a package directory, add it
                                if (basename.truthy())
                                {
                                    basename = (S<"%s/%s">()) % py_tuple({ basename, name });
                                }
                                else
                                {
                                    basename = name;
                                }
                                if (self.attr("debug").truthy())
                                {
                                    (B<"print">())(S<"Adding package in">(), pathname, S<"as">(), basename);
                                }
                                auto const items95 = unpack(self.method("_get_codename", { initname[B<"slice">()(I<0>(), I<-3>(), none())], basename }), 2);
                                fname = items95[0];
                                arcname = items95[1];
                                if (self.attr("debug").truthy())
                                {
                                    (B<"print">())(S<"Adding">(), arcname);
                                }
                                self.method("write", { fname, arcname });
                                dirlist = (B<"sorted">())((Mod<"os">().attr("listdir"))(pathname));
                                dirlist.method("remove", { S<"__init__.py">() });
                                // Add all *.py files and package subdirectories
                                for (py const& item : iterate{ dirlist })
                                {
                                    filename = item;
                                    path = (Mod<"os">().attr("path")).method("join", { pathname, filename });
                                    auto const items97 = unpack((Mod<"os">().attr("path")).method("splitext", { filename }), 2);
                                    root = items97[0];
                                    ext = items97[1];
                                    if ((Mod<"os">().attr("path")).method("isdir", { path }).truthy())
                                    {
                                        if ((Mod<"os">().attr("path")).method("isfile", { (Mod<"os">().attr("path")).method("join", { path, S<"__init__.py">() }) }).truthy())
                                        {
                                            // This is a package directory, add it
                                            self.method("writepy", { path, basename }, { { "filterfunc", filterfunc } });
                                        }
                                    }
                                    else if (ext == (S<".py">()))
                                    {
                                        if ((filterfunc.truthy() && (!filterfunc(path).truthy())))
                                        {
                                            if (self.attr("debug").truthy())
                                            {
                                                (B<"print">())((S<"file %r skipped by filterfunc">()) % path);
                                            }
                                            continue;
                                        }
                                        auto const items98 = unpack(self.method("_get_codename", { path[B<"slice">()(I<0>(), I<-3>(), none())], basename }), 2);
                                        fname = items98[0];
                                        arcname = items98[1];
                                        if (self.attr("debug").truthy())
                                        {
                                            (B<"print">())(S<"Adding">(), arcname);
                                        }
                                        self.method("write", { fname, arcname });
                                    }
                                }
                            }
                            else
                            {
                                // This is NOT a package directory, add its files at top level
                                if (self.attr("debug").truthy())
                                {
                                    (B<"print">())(S<"Adding files from directory">(), pathname);
                                }
                                for (py const& item : iterate{ (B<"sorted">())((Mod<"os">().attr("listdir"))(pathname)) })
                                {
                                    filename = item;
                                    path = (Mod<"os">().attr("path")).method("join", { pathname, filename });
                                    auto const items100 = unpack((Mod<"os">().attr("path")).method("splitext", { filename }), 2);
                                    root = items100[0];
                                    ext = items100[1];
                                    if (ext == (S<".py">()))
                                    {
                                        if ((filterfunc.truthy() && (!filterfunc(path).truthy())))
                                        {
                                            if (self.attr("debug").truthy())
                                            {
                                                (B<"print">())((S<"file %r skipped by filterfunc">()) % path);
                                            }
                                            continue;
                                        }
                                        auto const items101 = unpack(self.method("_get_codename", { path[B<"slice">()(I<0>(), I<-3>(), none())], basename }), 2);
                                        fname = items101[0];
                                        arcname = items101[1];
                                        if (self.attr("debug").truthy())
                                        {
                                            (B<"print">())(S<"Adding">(), arcname);
                                        }
                                        self.method("write", { fname, arcname });
                                    }
                                }
                            }
                        }
                        else
                        {
                            if (pathname[B<"slice">()(I<-3>(), none(), none())] != (S<".py">()))
                            {
                                py_raise((B<"RuntimeError">())(S<"Files added with writepy() must end with \".py\"">()));
                            }
                            auto const items102 = unpack(self.method("_get_codename", { pathname[B<"slice">()(I<0>(), I<-3>(), none())], basename }), 2);
                            fname = items102[0];
                            arcname = items102[1];
                            if (self.attr("debug").truthy())
                            {
                                (B<"print">())(S<"Adding file">(), arcname);
                            }
                            self.method("write", { fname, arcname });
                        }
                        return none();
                    }) },
                { "_get_codename", w.method("PyZipFile", "_get_codename", { py_positional("pathname"), py_positional("basename") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py pathname = a[1];
                        py basename = a[2];
                        py _compile;
                        py archivename;
                        py arcname;
                        py file_py;
                        py file_pyc;
                        py fname;
                        py msg;
                        py pycache_opt0;
                        py pycache_opt1;
                        py pycache_opt2;
                        _compile = w.function("_compile", "PyZipFile._get_codename.<locals>._compile", { py_positional("file"), py_positional("optimize", I<-1>()) }, [self](arguments a) -> py
    {
        py file = a[0];
        py optimize = a[1];
        py err;
        py py_compile;
        py_compile = module_of("py_compile");
        if ((*self).attr("debug").truthy())
        {
            (B<"print">())(S<"Compiling">(), file);
        }
        try
        {
            py_compile.method("compile", { file }, { { "doraise", boolean(true) }, { "optimize", optimize } });
        }
        catch (raised const&)
        {
            pyclass::handler_scope handler103;
            if (matches(handler103.exception, py_compile.attr("PyCompileError")))
            {
                err = handler103.exception;
                (B<"print">())(err.attr("msg"));
                return boolean(false);
            }
            else
                py_reraise(handler103.exception);
        }
        return boolean(true);
    });
                        file_py = pathname + (S<".py">());
                        file_pyc = pathname + (S<".pyc">());
                        pycache_opt0 = (Mod<"importlib.util">().attr("cache_from_source")).call({ file_py }, { { "optimization", S<"">() } });
                        pycache_opt1 = (Mod<"importlib.util">().attr("cache_from_source")).call({ file_py }, { { "optimization", I<1>() } });
                        pycache_opt2 = (Mod<"importlib.util">().attr("cache_from_source")).call({ file_py }, { { "optimization", I<2>() } });
                        if ((*self).attr("_optimize") == (I<-1>()))
                        {
                            // legacy mode: use whatever file is present
                            if (((Mod<"os">().attr("path")).method("isfile", { file_pyc }).truthy() && ((Mod<"os">().attr("stat"))(file_pyc).attr("st_mtime") >= (Mod<"os">().attr("stat"))(file_py).attr("st_mtime"))))
                            {
                                // Use .pyc file.
                                py const value104 = file_pyc;
                                arcname = value104;
                                fname = value104;
                            }
                            else if (((Mod<"os">().attr("path")).method("isfile", { pycache_opt0 }).truthy() && ((Mod<"os">().attr("stat"))(pycache_opt0).attr("st_mtime") >= (Mod<"os">().attr("stat"))(file_py).attr("st_mtime"))))
                            {
                                // Use the __pycache__/*.pyc file, but write it to the legacy pyc
                                // file name in the archive.
                                fname = pycache_opt0;
                                arcname = file_pyc;
                            }
                            else if (((Mod<"os">().attr("path")).method("isfile", { pycache_opt1 }).truthy() && ((Mod<"os">().attr("stat"))(pycache_opt1).attr("st_mtime") >= (Mod<"os">().attr("stat"))(file_py).attr("st_mtime"))))
                            {
                                // Use the __pycache__/*.pyc file, but write it to the legacy pyc
                                // file name in the archive.
                                fname = pycache_opt1;
                                arcname = file_pyc;
                            }
                            else if (((Mod<"os">().attr("path")).method("isfile", { pycache_opt2 }).truthy() && ((Mod<"os">().attr("stat"))(pycache_opt2).attr("st_mtime") >= (Mod<"os">().attr("stat"))(file_py).attr("st_mtime"))))
                            {
                                // Use the __pycache__/*.pyc file, but write it to the legacy pyc
                                // file name in the archive.
                                fname = pycache_opt2;
                                arcname = file_pyc;
                            }
                            // Compile py into PEP 3147 pyc file.
                            else if (_compile(file_py).truthy())
                            {
                                if ((Mod<"sys">().attr("flags")).attr("optimize") == (I<0>()))
                                {
                                    fname = pycache_opt0;
                                }
                                else if ((Mod<"sys">().attr("flags")).attr("optimize") == (I<1>()))
                                {
                                    fname = pycache_opt1;
                                }
                                else
                                {
                                    fname = pycache_opt2;
                                }
                                arcname = file_pyc;
                            }
                            else
                            {
                                py const value105 = file_py;
                                fname = value105;
                                arcname = value105;
                            }
                        }
                        else
                        {
                            // new mode: use given optimization level
                            if ((*self).attr("_optimize") == (I<0>()))
                            {
                                fname = pycache_opt0;
                                arcname = file_pyc;
                            }
                            else
                            {
                                arcname = file_pyc;
                                if ((*self).attr("_optimize") == (I<1>()))
                                {
                                    fname = pycache_opt1;
                                }
                                else if ((*self).attr("_optimize") == (I<2>()))
                                {
                                    fname = pycache_opt2;
                                }
                                else
                                {
                                    msg = (S<"invalid value for 'optimize': {!r}">()).method("format", { (*self).attr("_optimize") });
                                    py_raise((B<"ValueError">())(msg));
                                }
                            }
                            if (!([&]() -> py { py const t = (Mod<"os">().attr("path")).method("isfile", { fname }); return !t.truthy() ? t : boolean((Mod<"os">().attr("stat"))(fname).attr("st_mtime") >= (Mod<"os">().attr("stat"))(file_py).attr("st_mtime")); }()).truthy())
                            {
                                if (!_compile.call({ file_py }, { { "optimize", (*self).attr("_optimize") } }).truthy())
                                {
                                    py const value106 = file_py;
                                    fname = value106;
                                    arcname = value106;
                                }
                            }
                        }
                        archivename = (Mod<"os">().attr("path")).method("split", { arcname })[I<1>()];
                        if (basename.truthy())
                        {
                            archivename = (S<"%s/%s">()) % py_tuple({ basename, archivename });
                        }
                        return py_tuple({ fname, archivename });
                    }) }, }));

            define("zipfile", "main", w.function("main", "main", { py_positional("args", none()) }, [](arguments a) -> py
                {
                    py args = a[0];
                    auto const addToZip = std::make_shared<py>();
                    py argparse;
                    py badfile;
                    py curdir;
                    py description;
                    py encoding;
                    py files;
                    py group;
                    py parser;
                    py path;
                    py src;
                    py zf;
                    py zip_name;
                    py zippath;
                    argparse = module_of("argparse");
                    description = S<"A simple command-line interface for zipfile module.">();
                    parser = argparse.method("ArgumentParser", {  }, { { "description", description } });
                    group = parser.method("add_mutually_exclusive_group", {  }, { { "required", boolean(true) } });
                    group.method("add_argument", { S<"-l">(), S<"--list">() }, { { "metavar", S<"<zipfile>">() }, { "help", S<"Show listing of a zipfile">() } });
                    group.method("add_argument", { S<"-e">(), S<"--extract">() }, { { "nargs", I<2>() }, { "metavar", py_tuple({ S<"<zipfile>">(), S<"<output_dir>">() }) }, { "help", S<"Extract zipfile into target dir">() } });
                    group.method("add_argument", { S<"-c">(), S<"--create">() }, { { "nargs", S<"+">() }, { "metavar", py_tuple({ S<"<name>">(), S<"<file>">() }) }, { "help", S<"Create zipfile from sources">() } });
                    group.method("add_argument", { S<"-t">(), S<"--test">() }, { { "metavar", S<"<zipfile>">() }, { "help", S<"Test if a zipfile is valid">() } });
                    parser.method("add_argument", { S<"--metadata-encoding">() }, { { "metavar", S<"<encoding>">() }, { "help", S<"Specify encoding of member names for -l, -e and -t">() } });
                    args = parser.method("parse_args", { args });
                    encoding = args.attr("metadata_encoding");
                    if (!args.attr("test").is_none())
                    {
                        src = args.attr("test");
                        {
                            py const manager107 = (G<"zipfile.ZipFile">()).call({ src, S<"r">() }, { { "metadata_encoding", encoding } });
                            py const exit107 = pyclass::special_method(manager107, "__exit__");
                            py const value107 = pyclass::special_method(manager107, "__enter__")(manager107);
                            zf = value107;
                            [[maybe_unused]] int flow107 = 0;
                            [[maybe_unused]] py result107;
                            bool exited107 = false;
                            try
                            {
                                badfile = zf.method("testzip");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler107;
                                exited107 = true;
                                if (!exit107(manager107, type_of(handler107.exception), handler107.exception, handler107.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler107.exception);
                            }
                            finally107:
                            if (!exited107)
                                exit107(manager107, none(), none(), none());
                            if (flow107 == 1)
                                return result107;
                        }
                        if (badfile.truthy())
                        {
                            (B<"print">())((S<"The following enclosed file is corrupted: {!r}">()).method("format", { badfile }));
                        }
                        (B<"print">())(S<"Done testing">());
                    }
                    else if (!args.attr("list").is_none())
                    {
                        src = args.attr("list");
                        {
                            py const manager108 = (G<"zipfile.ZipFile">()).call({ src, S<"r">() }, { { "metadata_encoding", encoding } });
                            py const exit108 = pyclass::special_method(manager108, "__exit__");
                            py const value108 = pyclass::special_method(manager108, "__enter__")(manager108);
                            zf = value108;
                            [[maybe_unused]] int flow108 = 0;
                            [[maybe_unused]] py result108;
                            bool exited108 = false;
                            try
                            {
                                zf.method("printdir");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler108;
                                exited108 = true;
                                if (!exit108(manager108, type_of(handler108.exception), handler108.exception, handler108.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler108.exception);
                            }
                            finally108:
                            if (!exited108)
                                exit108(manager108, none(), none(), none());
                            if (flow108 == 1)
                                return result108;
                        }
                    }
                    else if (!args.attr("extract").is_none())
                    {
                        auto const items109 = unpack(args.attr("extract"), 2);
                        src = items109[0];
                        curdir = items109[1];
                        {
                            py const manager110 = (G<"zipfile.ZipFile">()).call({ src, S<"r">() }, { { "metadata_encoding", encoding } });
                            py const exit110 = pyclass::special_method(manager110, "__exit__");
                            py const value110 = pyclass::special_method(manager110, "__enter__")(manager110);
                            zf = value110;
                            [[maybe_unused]] int flow110 = 0;
                            [[maybe_unused]] py result110;
                            bool exited110 = false;
                            try
                            {
                                zf.method("extractall", { curdir });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler110;
                                exited110 = true;
                                if (!exit110(manager110, type_of(handler110.exception), handler110.exception, handler110.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler110.exception);
                            }
                            finally110:
                            if (!exited110)
                                exit110(manager110, none(), none(), none());
                            if (flow110 == 1)
                                return result110;
                        }
                    }
                    else if (!args.attr("create").is_none())
                    {
                        if (encoding.truthy())
                        {
                            (B<"print">()).call({ S<"Non-conforming encodings not supported with -c.">() }, { { "file", Mod<"sys">().attr("stderr") } });
                            (Mod<"sys">().attr("exit"))(I<1>());
                        }
                        zip_name = args.attr("create").method("pop", { I<0>() });
                        files = args.attr("create");
                        *addToZip = w.function("addToZip", "main.<locals>.addToZip", { py_positional("zf"), py_positional("path"), py_positional("zippath") }, [addToZip](arguments a) -> py
        {
            py zf = a[0];
            py path = a[1];
            py zippath = a[2];
            py nm;
            if ((Mod<"os">().attr("path")).method("isfile", { path }).truthy())
            {
                zf.method("write", { path, zippath, G<"zipfile.ZIP_DEFLATED">() });
            }
            else if ((Mod<"os">().attr("path")).method("isdir", { path }).truthy())
            {
                if (zippath.truthy())
                {
                    zf.method("write", { path, zippath });
                }
                for (py const& item : iterate{ (B<"sorted">())((Mod<"os">().attr("listdir"))(path)) })
                {
                    nm = item;
                    (*addToZip)(zf, (Mod<"os">().attr("path")).method("join", { path, nm }), (Mod<"os">().attr("path")).method("join", { zippath, nm }));
                }
            }
            return none();
        });
                        // else: ignore
                        {
                            py const manager112 = (G<"zipfile.ZipFile">())(zip_name, S<"w">());
                            py const exit112 = pyclass::special_method(manager112, "__exit__");
                            py const value112 = pyclass::special_method(manager112, "__enter__")(manager112);
                            zf = value112;
                            [[maybe_unused]] int flow112 = 0;
                            [[maybe_unused]] py result112;
                            bool exited112 = false;
                            try
                            {
                                for (py const& item : iterate{ files })
                                {
                                    path = item;
                                    zippath = (Mod<"os">().attr("path")).method("basename", { path });
                                    if (!zippath.truthy())
                                    {
                                        zippath = (Mod<"os">().attr("path")).method("basename", { (Mod<"os">().attr("path")).method("dirname", { path }) });
                                    }
                                    if (py_tuple({ S<"">(), Mod<"os">().attr("curdir"), Mod<"os">().attr("pardir") }).contains(zippath))
                                    {
                                        zippath = S<"">();
                                    }
                                    (*addToZip)(zf, path, zippath);
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler112;
                                exited112 = true;
                                if (!exit112(manager112, type_of(handler112.exception), handler112.exception, handler112.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler112.exception);
                            }
                            finally112:
                            if (!exited112)
                                exit112(manager112, none(), none(), none());
                            if (flow112 == 1)
                                return result112;
                        }
                    }
                    return none();
                }));

            define("zipfile", "__getattr__", w.function("__getattr__", "__getattr__", { py_positional("name") }, [](arguments a) -> py
                {
                    py name = a[0];
                    py importlib;
                    if (py_tuple({ S<"Path">(), S<"CompleteDirs">() }).contains(name))
                    {
                        importlib = module_of("importlib");
                        return (B<"getattr">())(importlib.method("import_module", { S<"zipfile._path">() }), name);
                    }
                    py_raise((B<"AttributeError">())(S(std::string{ "module 'zipfile' has no attribute " } + name.repr())));
                }));
        }

        bool zipfile_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_zipfile();
                }
                if (auto const doc = zipfile_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("binascii", py{ fn_import("binascii") }.release());
                aAttributes.emplace_back("importlib", py{ fn_import("importlib") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("shutil", py{ fn_import("shutil") }.release());
                aAttributes.emplace_back("stat", py{ fn_import("stat") }.release());
                aAttributes.emplace_back("struct", py{ fn_import("struct") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("threading", py{ fn_import("threading") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("zipfile", zipfile_module);
    }
}
