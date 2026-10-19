# the time module
import time
t0 = time.time()
time.sleep(0.02)
print(time.time() - t0 >= 0.02, time.monotonic() > 0, isinstance(time.perf_counter(), float))
g = time.gmtime(0)
print(g, g.tm_year, g[0], len(g), tuple(g)[:3])
print(time.gmtime(1700000000))
print(time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(1234567890)), time.asctime(time.gmtime(0)))
p = time.strptime('2024-02-29 13:45:10', '%Y-%m-%d %H:%M:%S')
print(p.tm_year, p.tm_mon, p.tm_mday, p.tm_wday, p.tm_yday, p[:6])
print(time.strptime('Mar 5 2001', '%b %d %Y')[:8])
import threading
order = []
def slow(name, delay):
    time.sleep(delay)
    order.append(name)
ts = [threading.Thread(target=slow, args=('b', 0.06)), threading.Thread(target=slow, args=('a', 0.01))]
for t in ts: t.start()
for t in ts: t.join()
print(order)
try:
    time.sleep(-1)
except ValueError as e:
    print(e)
