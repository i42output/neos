# the queue module
import queue
import threading
q = queue.Queue()
for i in range(5):
    q.put(i)
print(q.qsize(), q.empty(), [q.get() for _ in range(5)], q.empty())
lq = queue.LifoQueue()
for i in range(3): lq.put(i)
print([lq.get() for _ in range(3)])
pq = queue.PriorityQueue()
for v in [5, 1, 4, 2, 3, 9, 0, 7]:
    pq.put((v, 'item%d' % v))
print([pq.get()[1] for _ in range(8)])
bq = queue.Queue(maxsize=2)
bq.put(1); bq.put(2)
print(bq.full())
try:
    bq.put_nowait(3)
except queue.Full:
    print('full')
try:
    queue.Queue().get(timeout=0.01)
except queue.Empty:
    print('empty')
work = queue.Queue()
results = []
rlock = threading.Lock()
def worker():
    while True:
        item = work.get()
        if item is None:
            work.task_done()
            break
        with rlock:
            results.append(item * item)
        work.task_done()
ws = [threading.Thread(target=worker) for _ in range(3)]
for w in ws: w.start()
for i in range(20): work.put(i)
work.join()
for _ in ws: work.put(None)
for w in ws: w.join()
print(sorted(results))
sq = queue.SimpleQueue()
sq.put('a'); sq.put('b')
print(sq.get(), sq.qsize(), isinstance(q, queue.Queue), isinstance(lq, queue.Queue))
def producer(out):
    for i in range(5):
        out.put(i)
    out.put(None)
pipe = queue.Queue()
pt = threading.Thread(target=producer, args=(pipe,))
pt.start()
got = []
while True:
    v = pipe.get()
    if v is None:
        break
    got.append(v)
pt.join()
print(got)
