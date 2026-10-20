# selectors: a single-threaded echo server that multiplexes several connections, and clients waiting on their own selector
import selectors
import socket

server = socket.create_server(('127.0.0.1', 0))
server.setblocking(False)
sel = selectors.DefaultSelector()
listener_key = sel.register(server, selectors.EVENT_READ, data='listener')
print('listener key:', listener_key.fileobj is server, listener_key.events == selectors.EVENT_READ, listener_key.data)
closed = []

def accept(sock):
    conn, _ = sock.accept()
    conn.setblocking(False)
    sel.register(conn, selectors.EVENT_READ, data=bytearray())

def service(key, mask):
    conn, pending = key.fileobj, key.data
    if mask & selectors.EVENT_READ:
        chunk = conn.recv(1024)
        if not chunk:
            sel.unregister(conn)
            conn.close()
            closed.append(True)
            return
        pending += chunk.upper()
        sel.modify(conn, selectors.EVENT_READ | selectors.EVENT_WRITE, data=pending)
    if mask & selectors.EVENT_WRITE and pending:
        del pending[:conn.send(pending)]
        if not pending:
            sel.modify(conn, selectors.EVENT_READ, data=pending)

def run_server_once():
    for key, mask in sel.select(timeout=1):
        if key.fileobj is server:
            accept(server)
        else:
            service(key, mask)

messages = [b'alpha', b'bravo', b'charlie']
clients = [socket.create_connection(server.getsockname()) for _ in messages]
client_sel = selectors.DefaultSelector()
for client, message in zip(clients, messages):
    client.sendall(message)
    client_sel.register(client, selectors.EVENT_READ, data=message)

replies = {}
while len(replies) < len(clients):
    run_server_once()
    for key, mask in client_sel.select(timeout=0):
        replies[key.data] = key.fileobj.recv(1024)
        client_sel.unregister(key.fileobj)
for message in messages:
    print(message, '->', replies[message])
print('connections registered with the server:', len(sel.get_map()) - 1)

for client in clients:
    client.close()
while len(closed) < len(clients):
    run_server_once()
print('closed by the server:', len(closed), 'still registered:', len(sel.get_map()))

# the map from file descriptors to keys
mapping = sel.get_map()
print('map:', len(mapping), server.fileno() in mapping, mapping[server.fileno()].data, list(mapping) == [server.fileno()])
print('get_key:', sel.get_key(server).data)

# errors
for action in [lambda: sel.register(server, selectors.EVENT_READ), lambda: sel.register(server.fileno() + 1000, 0),
               lambda: sel.get_key(12345), lambda: sel.unregister(12345), lambda: sel.register(object(), selectors.EVENT_READ),
               lambda: sel.register(-1, selectors.EVENT_READ)]:
    try:
        action()
    except (KeyError, ValueError) as e:
        print(type(e).__name__ + ':', str(e).replace(repr(server), '<server>').split(' at 0x')[0])

# a selector is a context manager; once closed it has no map
sel.unregister(server)
server.close()
with client_sel:
    pass
sel.close()
print('closed map:', sel.get_map())
try:
    sel.get_key(0)
except RuntimeError as e:
    print('RuntimeError:', e)

# every selector available on this platform behaves the same way
for name in ['SelectSelector', 'PollSelector', 'EpollSelector', 'DevpollSelector', 'KqueueSelector']:
    cls = getattr(selectors, name, None)
    if cls is None:
        continue
    a, b = socket.socketpair()
    with cls() as s:
        s.register(a, selectors.EVENT_READ, 'a')
        s.register(b, selectors.EVENT_WRITE, 'b')
        before = sorted((key.data, ev) for key, ev in s.select(0))
        b.send(b'x')
        after = sorted((key.data, ev) for key, ev in s.select(1))
        print(name, before, after)
    a.close()
    b.close()
try:
    selectors.BaseSelector()
except TypeError as e:
    print('TypeError:', e)
