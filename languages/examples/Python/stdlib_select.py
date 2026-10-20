# select: waiting until sockets are readable or writable (select() everywhere; poll() and epoll() where the platform has them)
import select
import socket

a, b = socket.socketpair()
names = {a.fileno(): 'a', b.fileno(): 'b'}

def name_of(s):
    return names[s if isinstance(s, int) else s.fileno()]

def ready(timeout=0):
    r, w, x = select.select([a, b], [a, b], [a, b], timeout)
    return [name_of(s) for s in r], [name_of(s) for s in w], [name_of(s) for s in x]

print('idle:', ready())
a.send(b'ping')
print('after a.send:', ready())
print('b received', b.recv(16))
print('drained:', ready())

# a timeout with nothing ready returns three empty lists
print('timed out:', select.select([a], [], [], 0.05))

# anything with a fileno() method can be waited on, and the same objects come back
class Endpoint:
    def __init__(self, sock, name):
        self.sock = sock
        self.name = name
    def fileno(self):
        return self.sock.fileno()
    def __repr__(self):
        return '<Endpoint ' + self.name + '>'

endpoints = [Endpoint(a, 'a'), Endpoint(b, 'b')]
b.send(b'pong')
r, w, x = select.select(endpoints, [], [], 1.0)
print('readable endpoints:', r, r[0] is endpoints[0])
print('a received', a.recv(16))

# bad arguments
for args in [(1, [], []), ([], [], [], -1), ([], [], [], 'soon'), ([object()], [], [])]:
    try:
        select.select(*args)
    except (TypeError, ValueError) as e:
        print(type(e).__name__ + ':', e)
print('select.error is OSError:', select.error is OSError)

# poll(): register descriptors with an event mask, then ask which are ready
if hasattr(select, 'poll'):
    p = select.poll()
    p.register(a, select.POLLIN)
    p.register(b, select.POLLIN | select.POLLOUT)
    def events(timeout=0):
        return sorted((name_of(fd), ev) for fd, ev in p.poll(timeout))
    print('poll idle:', events())
    b.send(b'hello')
    print('poll after b.send:', events(), 'POLLIN =', select.POLLIN, 'POLLOUT =', select.POLLOUT)
    print('a received', a.recv(16))
    p.modify(b, select.POLLIN)
    p.unregister(a)
    print('poll after modify and unregister:', events(10))
    try:
        p.unregister(a)
    except KeyError:
        print('KeyError: a is no longer registered')

# epoll() (Linux): the same idea, with level- or edge-triggered events
if hasattr(select, 'epoll'):
    with select.epoll() as ep:
        ep.register(a, select.EPOLLIN | select.EPOLLET)
        b.send(b'edge')
        print('epoll first:', [(name_of(fd), ev & select.EPOLLIN != 0) for fd, ev in ep.poll(1)])
        print('epoll again (edge-triggered, nothing new):', ep.poll(0))
        print('a received', a.recv(16))
        print('epoll open:', not ep.closed)
    print('epoll closed:', ep.closed)

a.close()
b.close()
