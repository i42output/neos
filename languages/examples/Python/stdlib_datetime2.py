from datetime import datetime, date, time, timedelta, timezone, tzinfo
import copy
print(datetime.fromtimestamp(0), datetime.fromtimestamp(1719792000), datetime.fromtimestamp(1700000000.123456), date.fromtimestamp(1e9))
print(datetime(2024, 7, 1, 12).timestamp(), datetime(2024, 1, 1).timestamp(), datetime(2024, 3, 31, 1, 30).timestamp(), datetime(2024, 10, 27, 1, 30).timestamp(), datetime(2024, 10, 27, 1, 30, fold=1).timestamp())
print(datetime.fromtimestamp(1729989000), repr(datetime.fromtimestamp(1729992600)))
print(datetime(2024, 7, 1, 12).astimezone(), datetime(2024, 1, 1, 12).astimezone(), datetime(2024, 7, 1, 12, tzinfo=timezone.utc).astimezone())
print(datetime(2024, 7, 1, 12).astimezone(timezone.utc), datetime(2024, 7, 1, 12).astimezone().tzname())
print(datetime(2024, 7, 1).strftime('%s'), datetime.utcfromtimestamp(0) if False else '', datetime.fromtimestamp(0, timezone.utc).timestamp())
n = datetime.now()
print(type(n).__name__, abs(n - datetime.now()) < timedelta(seconds=5), abs(datetime.now(timezone.utc) - n.astimezone(timezone.utc)) < timedelta(seconds=5), date.today() == n.date() or True)
x = datetime(2024, 5, 6, 7, 8, 9)
print(copy.copy(x) == x, copy.deepcopy(timedelta(5)), copy.copy(date(2024, 1, 1)))

ZERO = timedelta(0)
HOUR = timedelta(hours=1)
def first_sunday_on_or_after(d):
    days_to_go = 6 - d.weekday()
    if days_to_go:
        d += timedelta(days_to_go)
    return d
class USTimeZone(tzinfo):
    def __init__(self, hours, reprname, stdname, dstname):
        self.stdoffset = timedelta(hours=hours)
        self.reprname = reprname
        self.stdname = stdname
        self.dstname = dstname
    def __repr__(self):
        return self.reprname
    def tzname(self, d):
        return self.dstname if self.dst(d) else self.stdname
    def utcoffset(self, d):
        return self.stdoffset + self.dst(d)
    def dst(self, d):
        if d is None or d.tzinfo is None:
            return ZERO
        start = first_sunday_on_or_after(datetime(d.year, 3, 8, 2))
        end = first_sunday_on_or_after(datetime(d.year, 11, 1, 1))
        if start <= d.replace(tzinfo=None) < end:
            return HOUR
        return ZERO
Eastern = USTimeZone(-5, "Eastern", "EST", "EDT")
for m in (1, 7):
    d = datetime(2024, m, 15, 12, tzinfo=Eastern)
    print(d, d.tzname(), d.utcoffset(), d.dst(), d.astimezone(timezone.utc), repr(d))
u = datetime(2024, 7, 4, 16, tzinfo=timezone.utc)
print(u.astimezone(Eastern), Eastern.fromutc(u.replace(tzinfo=Eastern)), datetime.fromtimestamp(1720108800, Eastern))
print(sorted([datetime(2024, 7, 1, 12, tzinfo=Eastern), datetime(2024, 7, 1, 15, tzinfo=timezone.utc)]))
print(time(10, tzinfo=Eastern).utcoffset(), time(10, tzinfo=Eastern).tzname())
class Bad(tzinfo):
    def utcoffset(self, d): return 5
for f in [lambda: datetime(2024, 1, 1, tzinfo=Bad()).utcoffset(), lambda: datetime(2024, 1, 1, tzinfo=tzinfo()).utcoffset(), lambda: Eastern.fromutc(u)]:
    try: print(f())
    except Exception as e: print(type(e).__name__, e)
