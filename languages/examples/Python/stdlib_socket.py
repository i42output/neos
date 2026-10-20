# socket: a threaded TCP line server, UDP datagrams, timeouts, sendfile and name lookups
import errno
import os
import socket
import threading

def serve(server, connections):
    # each connection is a line protocol: every line comes back reversed with its length; 'quit' ends it
    for _ in range(connections):
        conn, _ = server.accept()
        with conn, conn.makefile('rwb') as stream:
            for line in stream:
                command = line.rstrip(b'\n').decode()
                if command == 'quit':
                    stream.write(b'bye\n')
                    stream.flush()
                    break
                stream.write(f'{command[::-1]} ({len(command)})\n'.encode())
                stream.flush()

server = socket.create_server(('127.0.0.1', 0))
host, port = server.getsockname()
print('server:', host, server.family, server.type, server.getsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR) != 0 or os.name == 'nt')
thread = threading.Thread(target=serve, args=(server, 2))
thread.start()

with socket.create_connection((host, port), timeout=5) as client:
    print('client:', client.getpeername() == (host, port), client.gettimeout(), client.getblocking())
    replies = client.makefile('r', encoding='utf-8', newline='\n')
    for word in ['hello', 'neos', 'sockets']:
        client.sendall(word.encode() + b'\n')
        print(word, '->', replies.readline().rstrip())
    client.sendall(b'quit\n')
    print('quit ->', replies.readline().rstrip())
    replies.close()

with socket.create_connection((host, port), timeout=5) as client:
    client.sendall(b'x' * 100000 + b'y\nquit\n')
    received = b''
    while not received.endswith(b'bye\n'):
        chunk = client.recv(65536)
        if not chunk:
            break
        received += chunk
    first, second = received.splitlines()
    print('large line:', len(first), first[:3], first[-10:], second)
thread.join()
server.close()

# UDP: datagrams keep their boundaries and say where they came from
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
    receiver.bind(('127.0.0.1', 0))
    receiver.settimeout(5)
    for i in range(3):
        sender.sendto(f'datagram {i}'.encode(), receiver.getsockname())
    for i in range(3):
        data, address = receiver.recvfrom(1024)
        print('udp:', data, address[1] == sender.getsockname()[1])
    buffer = bytearray(4)
    sender.sendto(b'truncated datagram', receiver.getsockname())
    print('udp recvfrom_into:', receiver.recvfrom_into(buffer)[0], buffer)
    sender.connect(receiver.getsockname())
    sender.send(b'connected')
    view = memoryview(bytearray(12))
    print('udp recv_into:', receiver.recv_into(view[2:]), bytes(view))
    receiver.settimeout(0.1)
    try:
        receiver.recvfrom(1024)
    except TimeoutError as e:
        print('TimeoutError:', e, isinstance(e, OSError), socket.timeout is TimeoutError)

# non-blocking sockets raise BlockingIOError instead of waiting
a, b = socket.socketpair()
a.setblocking(False)
print('non-blocking:', a.gettimeout(), a.getblocking())
try:
    a.recv(10)
except BlockingIOError as e:
    print('BlockingIOError:', e.errno in (errno.EAGAIN, errno.EWOULDBLOCK))
a.setblocking(True)

# sendfile: the contents of a file sent over a socket
path = 'neos_socket_sendfile.txt'
with open(path, 'wb') as f:
    f.write(b'file contents sent with sendfile\n' * 100)
with open(path, 'rb') as f:
    sent = a.sendfile(f, offset=0, count=330)
    print('sendfile:', sent, f.tell())
got = b''
while len(got) < sent:
    got += b.recv(4096)
print('received:', len(got), got[:33])
os.remove(path)
a.close()
b.close()

# connecting to a port nothing is listening on
probe = socket.socket()
probe.bind(('127.0.0.1', 0))
free = probe.getsockname()
probe.close()
try:
    socket.create_connection(free, timeout=5)
except OSError as e:
    print('refused:', type(e).__name__)

# a closed socket
s = socket.socket()
with s:
    pass
print('closed socket:', s.fileno(), repr(s).startswith('<socket.socket [closed]'))
try:
    s.send(b'x')
except OSError as e:
    print('OSError:', e.errno == errno.EBADF or e.errno == getattr(errno, 'WSAENOTSOCK', None))

# name and address helpers
print(socket.getaddrinfo('127.0.0.1', 80, socket.AF_INET, socket.SOCK_STREAM))
print(socket.getaddrinfo('localhost', 'http', socket.AF_INET, socket.SOCK_STREAM, socket.IPPROTO_TCP)[0][4])
print(socket.getnameinfo(('127.0.0.1', 80), socket.NI_NUMERICHOST | socket.NI_NUMERICSERV))
print(socket.gethostbyname('localhost'), socket.getservbyname('http', 'tcp'), socket.getservbyport(443, 'tcp'), socket.getprotobyname('udp'))
print(socket.inet_aton('192.168.1.20'), socket.inet_ntoa(b'\x0a\x00\x00\x01'))
print(socket.inet_pton(socket.AF_INET6, '2001:db8::1'), socket.inet_ntop(socket.AF_INET6, b'\xfe\x80' + b'\x00' * 13 + b'\x01'))
print(socket.htons(1), socket.ntohs(256), socket.htonl(1), socket.ntohl(16777216))
print(isinstance(socket.gethostname(), str), socket.has_ipv6, socket.getdefaulttimeout())
try:
    socket.getaddrinfo('no.such.host.invalid', 80)
except socket.gaierror as e:
    print('gaierror:', isinstance(e, OSError))
print(repr(socket.AF_INET), repr(socket.SOCK_DGRAM), socket.AddressFamily(socket.AF_INET).name, socket.SocketKind.SOCK_STREAM.value)
print(repr(socket.MSG_PEEK), socket.MsgFlag.__mro__[1].__name__, repr(socket.AI_PASSIVE))
