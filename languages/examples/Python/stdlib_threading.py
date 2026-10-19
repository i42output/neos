# threading: threads, locks, conditions, events, semaphores, barriers, timers, thread-local data
import threading
results = []
lock = threading.Lock()
def worker(n):
    total = 0
    for i in range(n):
        total += i
    with lock:
        results.append(total)
threads = [threading.Thread(target=worker, args=(1000 * (k + 1),)) for k in range(4)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print(sorted(results), threads[0].name, threads[0].is_alive())

class Counter(threading.Thread):
    def __init__(self, count):
        super().__init__()
        self.count = count
        self.value = 0
    def run(self):
        for _ in range(self.count):
            self.value += 1
c = Counter(500)
c.start()
c.join()
print(c.value, isinstance(c, threading.Thread), threading.current_thread().name)

counter = 0
clock = threading.Lock()
def increment():
    global counter
    for _ in range(1000):
        with clock:
            counter += 1
ts = [threading.Thread(target=increment) for _ in range(5)]
for t in ts: t.start()
for t in ts: t.join()
print(counter)

ev = threading.Event()
out = []
def waiter():
    ev.wait()
    out.append('released')
w = threading.Thread(target=waiter)
w.start()
out.append('setting')
ev.set()
w.join()
print(out, ev.is_set())

cond = threading.Condition()
items = []
def consumer():
    with cond:
        while not items:
            cond.wait()
        items.append('consumed ' + items.pop(0))
cthread = threading.Thread(target=consumer)
cthread.start()
with cond:
    items.append('x')
    cond.notify()
cthread.join()
print(items)

sem = threading.Semaphore(2)
print(sem.acquire(), sem.acquire(), sem.acquire(blocking=False))
sem.release(2)
rl = threading.RLock()
with rl:
    with rl:
        print('reentrant ok')
local = threading.local()
local.x = 1
def setlocal():
    local.x = 2
    print('thread sees', local.x)
lt = threading.Thread(target=setlocal)
lt.start(); lt.join()
print('main sees', local.x)
barrier = threading.Barrier(3)
arrived = []
def at_barrier(i):
    barrier.wait()
    with lock:
        arrived.append(i)
bs = [threading.Thread(target=at_barrier, args=(i,)) for i in range(3)]
for b in bs: b.start()
for b in bs: b.join()
print(sorted(arrived))
fired = []
timer = threading.Timer(0.01, lambda: fired.append('fired'))
timer.start(); timer.join()
print(fired, threading.active_count())
