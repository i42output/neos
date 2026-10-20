/*
  pyio_docs.hpp

  The docstrings of the classes of CPython 3.13's io module that pyio.cpp ports (those of its C implementation, Modules/_io,
  and, for what only Lib/_pyio.py has, _pyio's), generated from CPython 3.13: Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: extracted from Python as C++ string literals for the neos Python runtime plugin (see
  runtime/python/NOTICE).
*/

#pragma once

#include <cstring>

namespace neos::python_runtime::detail::pyio_docs
{
    struct entry { char const* name; char const* doc; };
    inline entry const sDocs[] =
    {
        { "BufferedIOBase", "Base class for buffered IO objects.\n\nThe main difference with RawIOBase is that the read() method\nsupports omitting the size argument, and does not have a default\nimplementation that defers to readinto().\n\nIn addition, read(), readinto() and write() may raise\nBlockingIOError if the underlying raw stream is in non-blocking\nmode and not ready; unlike their raw counterparts, they will never\nreturn None.\n\nA typical implementation should not inherit from a RawIOBase\nimplementation, but wrap one.\n" },
        { "BufferedIOBase.detach", "Disconnect this buffer from its underlying raw stream and return it.\n\nAfter the raw stream has been detached, the buffer is in an unusable\nstate." },
        { "BufferedIOBase.read", "Read and return up to n bytes.\n\nIf the size argument is omitted, None, or negative, read and\nreturn all data until EOF.\n\nIf the size argument is positive, and the underlying raw stream is\nnot 'interactive', multiple raw reads may be issued to satisfy\nthe byte count (unless EOF is reached first).\nHowever, for interactive raw streams (as well as sockets and pipes),\nat most one raw read will be issued, and a short result does not\nimply that EOF is imminent.\n\nReturn an empty bytes object on EOF.\n\nReturn None if the underlying raw stream was open in non-blocking\nmode and no data is available at the moment." },
        { "BufferedIOBase.read1", "Read and return up to size bytes, with at most one read() call to the underlying raw stream.\n\nReturn an empty bytes object on EOF.\nA short result does not imply that EOF is imminent." },
        { "BufferedIOBase.write", "Write buffer b to the IO stream.\n\nReturn the number of bytes written, which is always\nthe length of b in bytes.\n\nRaise BlockingIOError if the buffer is full and the\nunderlying raw stream cannot accept more data at the moment." },
        { "BufferedRWPair", "A buffered reader and writer object together.\n\nA buffered reader object and buffered writer object put together to\nform a sequential IO object that can read and write. This is typically\nused with a socket or two-way pipe.\n\nreader and writer are RawIOBase objects that are readable and\nwriteable respectively. If the buffer_size is omitted it defaults to\nDEFAULT_BUFFER_SIZE." },
        { "BufferedRWPair.__init__", "Initialize self.  See help(type(self)) for accurate signature." },
        { "BufferedRandom", "A buffered interface to random access streams.\n\nThe constructor creates a reader and writer for a seekable stream,\nraw, given in the first argument. If the buffer_size is omitted it\ndefaults to DEFAULT_BUFFER_SIZE." },
        { "BufferedRandom.__init__", "Initialize self.  See help(type(self)) for accurate signature." },
        { "BufferedReader", "Create a new buffered reader using the given readable raw IO object." },
        { "BufferedReader.__init__", "Initialize self.  See help(type(self)) for accurate signature." },
        { "BufferedReader._readinto", "Read data into *buf* with at most one system call." },
        { "BufferedWriter", "A buffer for a writeable sequential RawIO object.\n\nThe constructor creates a BufferedWriter for the given writeable raw\nstream. If the buffer_size is not given, it defaults to\nDEFAULT_BUFFER_SIZE." },
        { "BufferedWriter.__init__", "Initialize self.  See help(type(self)) for accurate signature." },
        { "FileIO", "Open a file.\n\nThe mode can be 'r' (default), 'w', 'x' or 'a' for reading,\nwriting, exclusive creation or appending.  The file will be created\nif it doesn't exist when opened for writing or appending; it will be\ntruncated when opened for writing.  A FileExistsError will be raised\nif it already exists when opened for creating.  Opening a file for\ncreating implies writing so this mode behaves in a similar way to\n'w'.  Add a '+' to the mode to allow simultaneous reading and\nwriting.\n\nA custom opener can be used by passing a callable as *opener*.\nThe underlying file descriptor for the file object is then obtained\nby calling opener with (*name*, *flags*).  *opener* must return\nan open file descriptor (passing os.open as *opener* results in\nfunctionality similar to passing None)." },
        { "FileIO.__init__", "Initialize self.  See help(type(self)) for accurate signature." },
        { "FileIO.__repr__", "Return repr(self)." },
        { "FileIO.close", "Close the file.\n\nA closed file cannot be used for further I/O operations.  close()\nmay be called more than once without error." },
        { "FileIO.closefd", "True if the file descriptor will be closed by close()." },
        { "FileIO.fileno", "Return the underlying file descriptor (an integer)." },
        { "FileIO.isatty", "True if the file is connected to a TTY device." },
        { "FileIO.mode", "String giving the file mode" },
        { "FileIO.read", "Read at most size bytes, returned as bytes.\n\nOnly makes one system call, so less data may be returned than\nrequested.  In non-blocking mode, returns None if no data is\navailable.  Return an empty bytes object at EOF." },
        { "FileIO.readable", "True if file was opened in a read mode." },
        { "FileIO.readall", "Read all data from the file, returned as bytes.\n\nIn non-blocking mode, returns as much as is immediately available,\nor None if no data is available.  Return an empty bytes object at\nEOF." },
        { "FileIO.readinto", "Same as RawIOBase.readinto()." },
        { "FileIO.seek", "Move to new file position and return the file position.\n\nArgument offset is a byte count.  Optional argument whence defaults\nto SEEK_SET or 0 (offset from start of file, offset should be >= 0);\nother values are SEEK_CUR or 1 (move relative to current position,\npositive or negative), and SEEK_END or 2 (move relative to end of\nfile, usually negative, although many platforms allow seeking beyond\nthe end of a file).\n\nNote that not all file objects are seekable." },
        { "FileIO.seekable", "True if file supports random-access." },
        { "FileIO.tell", "Current file position.\n\nCan raise OSError for non seekable files." },
        { "FileIO.truncate", "Truncate the file to at most size bytes and return the truncated size.\n\nSize defaults to the current file position, as returned by tell().\nThe current file position is changed to the value of size." },
        { "FileIO.writable", "True if file was opened in a write mode." },
        { "FileIO.write", "Write buffer b to file, return number of bytes written.\n\nOnly makes one system call, so not all of the data may be written.\nThe number of bytes actually written is returned.  In non-blocking\nmode, returns None if the write would block." },
        { "IOBase", "The abstract base class for all I/O classes.\n\nThis class provides dummy implementations for many methods that\nderived classes can override selectively; the default implementations\nrepresent a file that cannot be read, written or seeked.\n\nEven though IOBase does not declare read, readinto, or write because\ntheir signatures will vary, implementations and clients should\nconsider those methods part of the interface. Also, implementations\nmay raise UnsupportedOperation when operations they do not support are\ncalled.\n\nThe basic type used for binary data read from or written to a file is\nbytes. Other bytes-like objects are accepted as method arguments too.\nIn some cases (such as readinto), a writable object is required. Text\nI/O classes work with str data.\n\nNote that calling any method (except additional calls to close(),\nwhich are ignored) on a closed stream should raise a ValueError.\n\nIOBase (and its subclasses) support the iterator protocol, meaning\nthat an IOBase object can be iterated over yielding the lines in a\nstream.\n\nIOBase also supports the :keyword:`with` statement. In this example,\nfp is closed after the suite of the with statement is complete:\n\nwith open('spam.txt', 'r') as fp:\n    fp.write('Spam and eggs!')\n" },
        { "IOBase.__del__", "Called when the instance is about to be destroyed." },
        { "IOBase.__iter__", "Implement iter(self)." },
        { "IOBase.__next__", "Implement next(self)." },
        { "IOBase._checkClosed", "Internal: raise a ValueError if file is closed\n        " },
        { "IOBase._checkReadable", "Internal: raise UnsupportedOperation if file is not readable\n        " },
        { "IOBase._checkSeekable", "Internal: raise UnsupportedOperation if file is not seekable\n        " },
        { "IOBase._checkWritable", "Internal: raise UnsupportedOperation if file is not writable\n        " },
        { "IOBase._unsupported", "Internal: raise an OSError exception for unsupported operations." },
        { "IOBase.close", "Flush and close the IO object.\n\nThis method has no effect if the file is already closed." },
        { "IOBase.fileno", "Return underlying file descriptor if one exists.\n\nRaise OSError if the IO object does not use a file descriptor." },
        { "IOBase.flush", "Flush write buffers, if applicable.\n\nThis is not implemented for read-only and non-blocking streams." },
        { "IOBase.isatty", "Return whether this is an 'interactive' stream.\n\nReturn False if it can't be determined." },
        { "IOBase.readable", "Return whether object was opened for reading.\n\nIf False, read() will raise OSError." },
        { "IOBase.readline", "Read and return a line from the stream.\n\nIf size is specified, at most size bytes will be read.\n\nThe line terminator is always b'\\n' for binary files; for text\nfiles, the newlines argument to open can be used to select the line\nterminator(s) recognized." },
        { "IOBase.readlines", "Return a list of lines from the stream.\n\nhint can be specified to control the number of lines read: no more\nlines will be read if the total size (in bytes/characters) of all\nlines so far exceeds hint." },
        { "IOBase.seek", "Change the stream position to the given byte offset.\n\n  offset\n    The stream position, relative to 'whence'.\n  whence\n    The relative position to seek from.\n\nThe offset is interpreted relative to the position indicated by\nwhence.  Values for whence are:\n\n* os.SEEK_SET or 0 -- start of stream (the default); offset should\n  be zero or positive\n* os.SEEK_CUR or 1 -- current stream position; offset may be\n  negative\n* os.SEEK_END or 2 -- end of stream; offset is usually negative\n\nReturn the new absolute position." },
        { "IOBase.seekable", "Return whether object supports random access.\n\nIf False, seek(), tell() and truncate() will raise OSError.\nThis method may need to do a test seek()." },
        { "IOBase.tell", "Return current stream position." },
        { "IOBase.truncate", "Truncate file to size bytes.\n\nFile pointer is left unchanged.  Size defaults to the current IO\nposition as reported by tell().  Return the new size." },
        { "IOBase.writable", "Return whether object was opened for writing.\n\nIf False, write() will raise OSError." },
        { "IOBase.writelines", "Write a list of lines to stream.\n\nLine separators are not added, so it is usual for each of the\nlines provided to have a line separator at the end." },
        { "IncrementalNewlineDecoder", "Codec used when reading a file in universal newlines mode.\n\nIt wraps another incremental decoder, translating \\r\\n and \\r into \\n.\nIt also records the types of newlines encountered.  When used with\ntranslate=False, it ensures that the newline sequence is returned in\none piece. When used with decoder=None, it expects unicode strings as\ndecode input and translates newlines without first invoking an external\ndecoder." },
        { "IncrementalNewlineDecoder.__init__", "Initialize self.  See help(type(self)) for accurate signature." },
        { "RawIOBase", "Base class for raw binary I/O." },
        { "RawIOBase.readall", "Read until EOF, using multiple read() call." },
        { "TextIOBase", "Base class for text I/O.\n\nThis class provides a character and line based interface to stream\nI/O. There is no readinto method because Python's character strings\nare immutable.\n" },
        { "TextIOBase.detach", "Separate the underlying buffer from the TextIOBase and return it.\n\nAfter the underlying buffer has been detached, the TextIO is in\nan unusable state." },
        { "TextIOBase.encoding", "Encoding of the text stream.\n\nSubclasses should override." },
        { "TextIOBase.errors", "The error setting of the decoder or encoder.\n\nSubclasses should override." },
        { "TextIOBase.newlines", "Line endings translated so far.\n\nOnly line endings translated during reading are considered.\n\nSubclasses should override." },
        { "TextIOBase.read", "Read at most size characters from stream.\n\nRead from underlying buffer until we have size characters or we hit\nEOF.  If size is negative or omitted, read until EOF." },
        { "TextIOBase.readline", "Read until newline or EOF.\n\nReturn an empty string if EOF is hit immediately.\nIf size is specified, at most size characters will be read." },
        { "TextIOBase.truncate", "Truncate file to size bytes.\n\nFile pointer is left unchanged.  Size defaults to the current IO\nposition as reported by tell().  Return the new size." },
        { "TextIOBase.write", "Write string s to stream.\n\nReturn the number of characters written\n(which is always equal to the length of the string)." },
        { "TextIOWrapper", "Character and line based layer over a BufferedIOBase object, buffer.\n\nencoding gives the name of the encoding that the stream will be\ndecoded or encoded with. It defaults to locale.getencoding().\n\nerrors determines the strictness of encoding and decoding (see\nhelp(codecs.Codec) or the documentation for codecs.register) and\ndefaults to \"strict\".\n\nnewline controls how line endings are handled. It can be None, '',\n'\\n', '\\r', and '\\r\\n'.  It works as follows:\n\n* On input, if newline is None, universal newlines mode is\n  enabled. Lines in the input can end in '\\n', '\\r', or '\\r\\n', and\n  these are translated into '\\n' before being returned to the\n  caller. If it is '', universal newline mode is enabled, but line\n  endings are returned to the caller untranslated. If it has any of\n  the other legal values, input lines are only terminated by the given\n  string, and the line ending is returned to the caller untranslated.\n\n* On output, if newline is None, any '\\n' characters written are\n  translated to the system default line separator, os.linesep. If\n  newline is '' or '\\n', no translation takes place. If newline is any\n  of the other legal values, any '\\n' characters written are translated\n  to the given string.\n\nIf line_buffering is True, a call to flush is implied when a call to\nwrite contains a newline character." },
        { "TextIOWrapper.__init__", "Initialize self.  See help(type(self)) for accurate signature." },
        { "TextIOWrapper.__next__", "Implement next(self)." },
        { "TextIOWrapper.__repr__", "Return repr(self)." },
        { "TextIOWrapper._get_decoded_chars", "Advance into the _decoded_chars buffer." },
        { "TextIOWrapper._read_chunk", "\n        Read and decode the next chunk of data from the BufferedReader.\n        " },
        { "TextIOWrapper._rewind_decoded_chars", "Rewind the _decoded_chars buffer." },
        { "TextIOWrapper._set_decoded_chars", "Set the _decoded_chars buffer." },
        { "TextIOWrapper.reconfigure", "Reconfigure the text stream with new parameters.\n\nThis also does an implicit stream flush." },
        { "TextIOWrapper.seek", "Set the stream position, and return the new stream position.\n\n  cookie\n    Zero or an opaque number returned by tell().\n  whence\n    The relative position to seek from.\n\nFour operations are supported, given by the following argument\ncombinations:\n\n- seek(0, SEEK_SET): Rewind to the start of the stream.\n- seek(cookie, SEEK_SET): Restore a previous position;\n  'cookie' must be a number returned by tell().\n- seek(0, SEEK_END): Fast-forward to the end of the stream.\n- seek(0, SEEK_CUR): Leave the current stream position unchanged.\n\nAny other argument combinations are invalid,\nand may raise exceptions." },
        { "TextIOWrapper.tell", "Return the stream position as an opaque number.\n\nThe return value of tell() can be given as input to seek(), to\nrestore a previous stream position." },
        { "_BufferedIOMixin", "A mixin implementation of BufferedIOBase with an underlying raw stream.\n\n    This passes most requests on to the underlying raw stream.  It\n    does *not* provide implementations of read(), readinto() or\n    write().\n    " },
    };
    // (the docstring of a class ("Class") or of its method ("Class.method"): null if it has none)
    inline char const* doc(char const* aName)
    {
        for (auto const& e : sDocs)
            if (std::strcmp(e.name, aName) == 0)
                return e.doc;
        return nullptr;
    }
}
