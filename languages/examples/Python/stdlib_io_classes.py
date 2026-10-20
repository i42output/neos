# io: the stream class hierarchy - raw, buffered and text layers, and streams written in Python
import io
import os

# a raw stream written in Python: it hands out at most three bytes per read
class Trickle(io.RawIOBase):
    def __init__(self, data):
        self.data = data
        self.pos = 0
    def readable(self):
        return True
    def readinto(self, buffer):
        chunk = self.data[self.pos:self.pos + min(3, len(buffer))]
        buffer[:len(chunk)] = chunk
        self.pos += len(chunk)
        return len(chunk)

raw = Trickle(b'the quick brown fox\njumps over\nthe lazy dog\n')
print('ABCs:', isinstance(raw, io.IOBase), isinstance(raw, io.RawIOBase), isinstance(raw, io.BufferedIOBase), isinstance(raw, io.TextIOBase))
print('raw read(5):', raw.read(5))
print('raw readline():', raw.readline())
print('raw readall():', raw.readall())
print('raw at EOF:', raw.read(5), raw.readable(), raw.writable(), raw.seekable())

# a buffered reader over it gathers the short reads together
buffered = io.BufferedReader(Trickle(b'line 1\nline 2\nline 3\nline 4\n'), buffer_size=8)
print('buffered:', buffered.read(4), buffered.readline(), buffered.peek(1)[:1], buffered.readlines())

# a text layer over that decodes and splits lines
text = io.TextIOWrapper(io.BufferedReader(Trickle('naïve\ncafé\r\nüber\rend'.encode('utf-8'))), encoding='utf-8')
print('text lines:', text.readlines(), 'newlines seen:', text.newlines)

# a raw writer written in Python that records what reaches it
class Recorder(io.RawIOBase):
    def __init__(self):
        self.chunks = []
    def writable(self):
        return True
    def write(self, b):
        self.chunks.append(bytes(b))
        return len(b)

recorder = Recorder()
writer = io.BufferedWriter(recorder, buffer_size=16)
writer.write(b'small ')
writer.write(b'writes ')
print('before flush:', recorder.chunks)
writer.flush()
print('after flush:', recorder.chunks)
writer.write(b'a write larger than the buffer goes straight through')
print('large write reached the raw stream:', b''.join(recorder.chunks)[-12:])
writer.close()
print('closed:', writer.closed, recorder.closed)

# TextIOWrapper writing: encoding, newline translation and write_through
data = io.BytesIO()
out = io.TextIOWrapper(data, encoding='utf-8', newline='\r\n', write_through=True)
out.write('first line\nsecond line\n')
print('encoded:', data.getvalue())
out.seek(0)
print('read back:', repr(out.read()))
print('attributes:', out.encoding, out.errors, out.line_buffering, out.buffer is data)
detached = out.detach()
print('detached:', detached is data, data.closed)
try:
    out.write('x')
except ValueError as e:
    print('ValueError:', e)

latin = io.TextIOWrapper(io.BytesIO(), encoding='latin-1', errors='replace')
latin.write('price: 5€')
latin.flush()
print('latin-1 with errors=replace:', latin.buffer.getvalue())

# universal newlines, incrementally
decoder = io.IncrementalNewlineDecoder(None, translate=True)
print('decoded:', [decoder.decode('one\r'), decoder.decode('\ntwo\r'), decoder.decode('three\n'), decoder.decode('', final=True)])
print('newlines:', decoder.newlines)

# BufferedRWPair: a reader and a writer behaving as one stream
requests, responses = io.BytesIO(b'GET /index\n'), io.BytesIO()
pair = io.BufferedRWPair(requests, responses)
request = pair.readline()
pair.write(b'200 OK for ' + request.split()[1] + b'\n')
pair.flush()
print('pair:', request, responses.getvalue(), pair.readable(), pair.writable())

# BufferedRandom over a seekable raw stream written in Python: reads and writes share one position
class MemoryRaw(io.RawIOBase):
    def __init__(self):
        self.data = bytearray()
        self.pos = 0
    def readable(self):
        return True
    def writable(self):
        return True
    def seekable(self):
        return True
    def readinto(self, buffer):
        chunk = self.data[self.pos:self.pos + len(buffer)]
        buffer[:len(chunk)] = chunk
        self.pos += len(chunk)
        return len(chunk)
    def write(self, b):
        b = bytes(b)
        self.data[self.pos:self.pos + len(b)] = b
        self.pos += len(b)
        return len(b)
    def seek(self, pos, whence=io.SEEK_SET):
        self.pos = [pos, self.pos + pos, len(self.data) + pos][whence]
        return self.pos
    def tell(self):
        return self.pos

memory = MemoryRaw()
with io.BufferedRandom(memory, buffer_size=4) as f:
    f.write(b'0123456789')
    f.seek(3)
    print('random:', f.read(3), f.tell())
    f.write(b'XYZ')
    f.seek(-2, io.SEEK_END)
    print('random from the end:', f.read())
    f.seek(0)
    print('random after overwrite:', f.read(), f.raw is memory)
print('raw contents:', bytes(memory.data), memory.closed)

# FileIO: the raw stream of a file descriptor, under the buffered and text layers
path = 'neos_io_fileio.dat'
with io.FileIO(path, 'w') as raw:
    print('FileIO:', raw.mode, raw.write(b'raw bytes\nsecond line\n'), raw.tell(), raw.readable(), raw.writable(), raw.closefd)
with io.FileIO(path) as raw:
    print('FileIO read:', raw.read(3), raw.readall(), raw.read(5), isinstance(raw, io.RawIOBase))
    try:
        raw.write(b'x')
    except io.UnsupportedOperation as e:
        print('UnsupportedOperation:', e)
with io.TextIOWrapper(io.BufferedReader(io.FileIO(path)), encoding='ascii') as text:
    print('layers:', text.readline().rstrip(), '|', type(text.buffer).__name__, type(text.buffer.raw).__name__, text.buffer.raw.mode)
for mode in ['rw', 'rt', '']:
    try:
        io.FileIO(path, mode)
    except ValueError as e:
        print('ValueError:', e)
raw = io.FileIO(path, 'a')
print('append position:', raw.tell(), repr(raw).startswith('<_io.FileIO name='))
raw.close()
print('closed:', raw.closed, repr(raw))
os.remove(path)

# open() builds the layers for you
path = 'neos_io_classes.dat'
with open(path, 'wb') as f:
    print('binary writer:', isinstance(f, io.BufferedIOBase), f.write(b'0123456789'), f.mode)
with open(path, 'rb') as f:
    print('binary reader:', isinstance(f, io.BufferedIOBase), f.read(4), f.tell(), f.readline())
with open(path, encoding='ascii') as f:
    print('text reader:', isinstance(f, io.TextIOBase), f.read(4), f.encoding, f.mode)
with io.open_code(path) as f:
    print('open_code:', f.read(2))
os.remove(path)

# in-memory streams
s = io.StringIO('alpha\nbeta\ngamma\n')
print('StringIO:', s.readline(), s.read(3), s.tell(), list(s), isinstance(s, io.TextIOBase))
b = io.BytesIO(b'abcdef')
b.seek(2)
b.write(b'CD')
print('BytesIO:', b.tell(), b.read(), b.getvalue(), isinstance(b, io.BufferedIOBase))

# unsupported operations and closed streams
for action in [lambda: Trickle(b'').fileno(), lambda: Trickle(b'').seek(0), lambda: io.BufferedReader(Trickle(b'')).write(b'x'), lambda: io.TextIOWrapper(Trickle(b''), encoding='ascii').write('x')]:
    try:
        action()
    except io.UnsupportedOperation as e:
        print('UnsupportedOperation:', e, isinstance(e, OSError), isinstance(e, ValueError))
closed = io.BytesIO(b'x')
closed.close()
try:
    closed.read()
except ValueError as e:
    print('ValueError:', e)
print('constants:', io.DEFAULT_BUFFER_SIZE, io.SEEK_SET, io.SEEK_CUR, io.SEEK_END)
