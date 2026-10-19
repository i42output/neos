import string
import struct
import binascii
import base64
import hashlib

print(string.ascii_letters, string.digits, string.hexdigits, string.octdigits, repr(string.whitespace), string.punctuation)
print(len(string.printable), string.capwords('hello   wORLD  foo'), string.capwords('a-b-c', '-'))
t = string.Template('$who likes $what, ${who}s cost $$10')
print(t.substitute(who='tim', what='kung pao'), t.safe_substitute(who='ann'), t.get_identifiers(), t.is_valid(), t.template)
print(string.Template('$x and $y').substitute({'x': 1}, y=[2]))
for bad in [lambda: string.Template('$missing').substitute(), lambda: string.Template('cost: $').substitute(), lambda: string.Template('a\n  $1').substitute()]:
    try:
        bad()
    except (KeyError, ValueError) as e:
        print(type(e).__name__, e)
class PercentTemplate(string.Template):
    delimiter = '%'
print(PercentTemplate('%name is %%100').substitute(name='x'))

print(struct.pack('<i', 1), struct.pack('>H', 258), struct.pack('<hhl', 1, 2, 3), struct.pack('<q', -2), struct.pack('>Q', 2**64 - 1), struct.pack('<?', True))
print(struct.pack('<4s', b'ab'), struct.pack('<3p', b'abcd'), struct.pack('c', b'x'), struct.pack('<d', 1.5), struct.pack('>f', -2.25), struct.pack('<e', 0.5), struct.pack('<2xb', 7))
print(struct.unpack('<i', b'\x01\x00\x00\x00'), struct.unpack('>hH', b'\xff\xfe\xff\xfe'), struct.unpack('<d', struct.pack('<d', 3.14)), struct.unpack('>Q', b'\xff' * 8))
print(struct.calcsize('<iH'), struct.calcsize('>q2s'), struct.calcsize('<5s'), struct.unpack('<3s?', b'abc\x01'), struct.unpack('<e', struct.pack('<e', 65504.0)), struct.unpack('<f', struct.pack('<f', 0.1)))
s = struct.Struct('<IHb')
packed = s.pack(70000, 500, -3)
print(s.size, s.format, packed, s.unpack(packed), list(struct.iter_unpack('<h', b'\x01\x00\x02\x00')), struct.unpack_from('<h', b'xx\x05\x00', 2))
buf = bytearray(6)
struct.pack_into('<hh', buf, 1, 5, -1)
print(buf, s)
for bad in [lambda: struct.pack('<h', 70000), lambda: struct.pack('<B', -1), lambda: struct.pack('<b', 200), lambda: struct.pack('<i', 'x'), lambda: struct.pack('<ii', 1),
            lambda: struct.unpack('<i', b'ab'), lambda: struct.pack('<Q', -1), lambda: struct.calcsize('<y'), lambda: struct.pack('<I', 2**32)]:
    try:
        bad()
    except struct.error as e:
        print('struct.error', e)

print(binascii.hexlify(b'\x01\xab\xff'), binascii.hexlify(b'abcdef', '-'), binascii.hexlify(b'abcdef', ':', 2), binascii.hexlify(b'abcde', ' ', -2), binascii.unhexlify('01abff'), binascii.b2a_hex(b'hi'))
print(binascii.b2a_base64(b'hello'), binascii.a2b_base64(b'aGVsbG8=\n'), binascii.crc32(b'hello world'), binascii.crc32(b'world', binascii.crc32(b'hello ')), binascii.crc_hqx(b'abc', 0))
for bad in [lambda: binascii.unhexlify('abc'), lambda: binascii.unhexlify('zz'), lambda: binascii.a2b_base64(b'aGVsbG8')]:
    try:
        bad()
    except binascii.Error as e:
        print('binascii.Error', e, isinstance(e, ValueError))

data = b'Python \xe2\x9c\x93 base64!'
e = base64.b64encode(data)
print(e, base64.b64decode(e), base64.b64decode('UHl0aG9u'), base64.urlsafe_b64encode(b'\xfb\xff\xfe'), base64.urlsafe_b64decode(b'-__-'), base64.b64encode(b'\xfb\xff', altchars=b'*.'))
print(base64.b32encode(b'hello'), base64.b32decode(b'NBSWY3DP'), base64.b32hexencode(b'abc'), base64.b16encode(b'\x01\xab'), base64.b16decode('01AB'), base64.b16decode('01ab', casefold=True))
print(base64.b85encode(b'hello world'), base64.b85decode(base64.b85encode(b'hello world')), base64.a85encode(b'hello world'), base64.a85decode(base64.a85encode(b'\x00\x00\x00\x00abc')))
print(base64.encodebytes(b'x' * 60), base64.decodebytes(b'eHh4\n'), base64.standard_b64encode(b'ab'), base64.b64decode(b'YWJj', validate=True))
for bad in [lambda: base64.b64decode('abc'), lambda: base64.b64decode('ab$c', validate=True), lambda: base64.b32decode('ABC')]:
    try:
        bad()
    except binascii.Error as e:
        print('binascii.Error', e)

for name in ['md5', 'sha1', 'sha224', 'sha256', 'sha384', 'sha512', 'sha3_224', 'sha3_256', 'sha3_384', 'sha3_512', 'blake2b', 'blake2s']:
    h = hashlib.new(name, b'The quick brown fox')
    print(name, h.hexdigest(), h.digest_size, h.block_size, h.name)
h = hashlib.sha256()
h.update(b'hello ')
h2 = h.copy()
h.update(b'world')
print(h.hexdigest() == hashlib.sha256(b'hello world').hexdigest(), h2.hexdigest() == hashlib.sha256(b'hello ').hexdigest(), hashlib.md5(b'').hexdigest(), hashlib.sha1(b'a' * 200).hexdigest())
print(hashlib.shake_128(b'abc').hexdigest(16), hashlib.shake_256(b'abc').hexdigest(8), len(hashlib.sha512(b'x' * 1000).digest()), hashlib.blake2b(b'abc', digest_size=16).hexdigest(), hashlib.blake2s(b'abc', key=b'key').hexdigest())
print(hashlib.blake2b(b'data', salt=b'salty', person=b'me').hexdigest(), hashlib.pbkdf2_hmac('sha256', b'password', b'salt', 1000).hex(), hashlib.pbkdf2_hmac('sha1', b'pw', b'NaCl', 2, 10).hex())
print(sorted(hashlib.algorithms_guaranteed), hashlib.sha256(b'\x00' * 64).hexdigest(), hashlib.md5(b'a' * 1000000).hexdigest())
try:
    hashlib.sha256('text')
except TypeError as e:
    print('TypeError', e)
try:
    hashlib.new('nope')
except ValueError as e:
    print('ValueError', e)
