# socket: sendmsg() and recvmsg() - scatter/gather I/O, ancillary data and passing file descriptors between processes (POSIX)
import array
import os
import socket
import sys
import warnings

if not hasattr(socket.socket, 'sendmsg'):
    print('sendmsg() and recvmsg() are not available on', sys.platform)
    sys.exit(0)

a, b = socket.socketpair()

# gather: several buffers sent as one message
print('sent:', a.sendmsg([b'header:', bytearray(b'body'), memoryview(b':trailer')]))
data, ancdata, flags, address = b.recvmsg(100)
print('received:', data, ancdata, flags, address)

# scatter: one message received into several buffers
a.sendmsg([b'0123456789abcdef'])
first, second = bytearray(4), bytearray(20)
nbytes, ancdata, flags, address = b.recvmsg_into([first, memoryview(second)[:12]])
print('scattered:', nbytes, first, second[:12], flags)

# the sizes of ancillary data items
int_size = array.array('i').itemsize
print('CMSG_LEN:', socket.CMSG_LEN(0), socket.CMSG_LEN(int_size), 'CMSG_SPACE:', socket.CMSG_SPACE(int_size), socket.CMSG_SPACE(3 * int_size))

# passing file descriptors with SCM_RIGHTS: the receiver gets new descriptors for the same open files
r, w = os.pipe()
a.sendmsg([b'pipe ends'], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', [r, w]))])
data, ancdata, flags, address = b.recvmsg(100, socket.CMSG_SPACE(2 * int_size))
level, kind, payload = ancdata[0]
fds = array.array('i')
fds.frombytes(payload)
print('ancillary:', data, level == socket.SOL_SOCKET, kind == socket.SCM_RIGHTS, len(fds), fds[0] not in (r, w))
os.write(fds[1], b'written through a received descriptor')
print('read from the original pipe:', os.read(r, 100))
for fd in (r, w, *fds):
    os.close(fd)

# the helpers built on them: socket.send_fds() and socket.recv_fds()
path = 'neos_socket_msg.txt'
with open(path, 'w+b') as f:
    f.write(b'shared between both descriptors')
    f.flush()
    print('send_fds:', socket.send_fds(a, [b'one file'], [f.fileno()]))
    message, received_fds, flags, address = socket.recv_fds(b, 100, 1)
    print('recv_fds:', message, len(received_fds), flags, address)
    os.lseek(received_fds[0], 0, os.SEEK_SET)
    print('read through the received descriptor:', os.read(received_fds[0], 100))
    os.close(received_fds[0])
os.remove(path)

# too little room for the ancillary data: MSG_CTRUNC is set, and only the descriptors that fitted arrive
a.sendmsg([b'truncated'], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', [0, 1, 2]))])
with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always')
    data, ancdata, flags, address = b.recvmsg(100, socket.CMSG_LEN(int_size))
print('truncated:', data, flags & socket.MSG_CTRUNC != 0, len(ancdata), 'warnings:', [(w.category.__name__, str(w.message)) for w in caught])
for level, kind, payload in ancdata:
    for fd in array.array('i', payload[:len(payload) - len(payload) % int_size]):
        os.close(fd)

# datagrams: sendmsg() with a destination address, recvmsg() reports the sender
receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
receiver.bind(('127.0.0.1', 0))
sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
print('udp sent:', sender.sendmsg([b'udp ', b'message'], [], 0, receiver.getsockname()))
data, ancdata, flags, address = receiver.recvmsg(100)
print('udp received:', data, ancdata, flags, address[0], address[1] == sender.getsockname()[1])
receiver.close()
sender.close()

# bad arguments
for action in [lambda: a.sendmsg(5), lambda: a.sendmsg(['text']), lambda: a.sendmsg([b'x'], [(1, 2)]),
               lambda: b.recvmsg(-1), lambda: b.recvmsg_into([b'read-only']), lambda: socket.CMSG_LEN(-1)]:
    try:
        action()
    except (TypeError, ValueError, OverflowError) as e:
        print(type(e).__name__ + ':', e)
a.close()
b.close()
