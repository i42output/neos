import datetime
from datetime import date, time, datetime as dt, timedelta, timezone, tzinfo

# timedelta
d = timedelta(days=1, hours=2, minutes=3, seconds=4, microseconds=5, milliseconds=6, weeks=1)
print(d, repr(d), d.days, d.seconds, d.microseconds, d.total_seconds())
print(timedelta(), repr(timedelta()), timedelta(hours=-1), repr(timedelta(hours=-1)), timedelta(seconds=-0.5), timedelta(days=1.5), timedelta(microseconds=1.5), timedelta(microseconds=2.5))
print(timedelta(minutes=90) + timedelta(seconds=30), timedelta(1) - timedelta(hours=1), -timedelta(1), abs(timedelta(-2)), +timedelta(3))
print(timedelta(hours=1) * 3, 2 * timedelta(minutes=7), timedelta(hours=1) * 0.25, timedelta(1) / 3, timedelta(1) / timedelta(hours=6), timedelta(1) // timedelta(hours=7))
print(timedelta(1) % timedelta(hours=7), divmod(timedelta(1), timedelta(hours=7)), timedelta(seconds=10) // 3, timedelta(seconds=10) / 4.0)
print(timedelta(1) == timedelta(hours=24), timedelta(1) < timedelta(2), timedelta(0) == 0, bool(timedelta(0)), bool(timedelta(microseconds=1)), hash(timedelta(1)) == hash(timedelta(hours=24)))
print(timedelta.max, timedelta.min, timedelta.resolution, sorted([timedelta(3), timedelta(-1), timedelta(hours=5)]))
print(timedelta(days=-1, seconds=1), timedelta(weeks=-1.5), timedelta(seconds=86399.9999999))

# date
a = date(2024, 2, 29)
print(a, repr(a), a.year, a.month, a.day, a.weekday(), a.isoweekday(), a.isocalendar(), a.toordinal(), a.ctime())
print(a.isocalendar().week, tuple(a.isocalendar()), a.timetuple())
print(a + timedelta(days=1), a - timedelta(days=60), timedelta(days=366) + a, a - date(2024, 1, 1), date(2023, 1, 1) - a)
print(a.replace(year=2028), a.replace(day=1), date.fromordinal(738945), date.fromisoformat('2024-03-15'), date.fromisoformat('20240315'), date.fromisoformat('2024-W11-5'))
print(date.fromisocalendar(2024, 1, 1), date.fromisocalendar(2020, 53, 7), date.min, date.max, date.resolution)
print(a.strftime('%Y-%m-%d %A %B %a %b %j %U %W %w %u %V %G %y %e %%'), a.strftime('%H:%M:%S %f %z|%Z|'), f'{a:%d/%m/%Y}', format(a, ''), a.isoformat())
print(date(2024, 1, 1) < date(2024, 1, 2), date(2024, 1, 1) == date(2024, 1, 1), date(2024, 1, 1) != date(2024, 1, 2), max(date(2020, 5, 5), date(2021, 1, 1)))
print(hash(date(2024, 1, 1)) == hash(date(2024, 1, 1)), {date(2024, 1, 1): 'new year'}[date(2024, 1, 1)], isinstance(a, date), isinstance(a, dt))
print(date(1, 1, 1).isocalendar(), date(2021, 1, 3).isocalendar(), date(2026, 12, 31).isocalendar(), date(2008, 12, 29).isocalendar())

# time
t = time(13, 5, 9, 123456)
print(t, repr(t), t.hour, t.minute, t.second, t.microsecond, t.tzinfo, t.fold, repr(time()), repr(time(1, 2)), repr(time(1, 2, 3)))
print(t.isoformat(), t.isoformat('hours'), t.isoformat(timespec='minutes'), t.isoformat('seconds'), t.isoformat('milliseconds'), t.isoformat('microseconds'))
print(t.strftime('%I:%M %p %H %S.%f'), t.replace(hour=1), t.replace(microsecond=0), time.fromisoformat('10:30'), time.fromisoformat('T10:30:45.5'), time.fromisoformat('103045'))
print(time(1) < time(2), time(1, 30) == time(1, 30), time.min, time.max, time.resolution, bool(time(0)))
tz5 = timezone(timedelta(hours=5, minutes=30))
tt = time(12, 0, tzinfo=tz5)
print(tt, repr(tt), tt.utcoffset(), tt.tzname(), tt.dst(), tt.isoformat(), tt == time(6, 30, tzinfo=timezone.utc), tt < time(7, tzinfo=timezone.utc))

# datetime
x = dt(2024, 3, 10, 14, 30, 15, 250000)
print(x, repr(x), x.date(), x.time(), x.timetz(), x.year, x.hour, x.microsecond, x.tzinfo, x.weekday(), x.isocalendar())
print(x.isoformat(), x.isoformat(' '), x.isoformat(timespec='seconds'), x.isoformat(sep='_', timespec='milliseconds'), str(dt(2024, 1, 1)), repr(dt(2024, 1, 1)), repr(dt(2024, 1, 1, 0, 0, 5)))
print(x.strftime('%Y-%m-%dT%H:%M:%S.%f'), x.strftime('%c|%x|%X|%D|%T|%R|%r|%I %p|%-d|%-H|%k|%l'), x.ctime(), f'{x:%H:%M}', f'{x}')
print(x + timedelta(hours=12), x - timedelta(days=10, microseconds=250001), x - dt(2024, 1, 1), dt(2024, 1, 1) - x, timedelta(1) + x)
print(x.replace(hour=0, minute=0, second=0, microsecond=0), x.replace(year=2025, tzinfo=timezone.utc), x.timetuple(), x.toordinal())
print(dt.combine(date(2024, 5, 1), time(9, 30)), dt.combine(date(2024, 5, 1), time(9, 30), tzinfo=timezone.utc), dt.fromordinal(738945), dt.fromisocalendar(2024, 10, 3))
print(dt.fromisoformat('2024-03-10T14:30:15.250'), dt.fromisoformat('2024-03-10 14:30'), dt.fromisoformat('2024-03-10'), repr(dt.fromisoformat('2024-03-10T14:30:15+01:00')), repr(dt.fromisoformat('2024-03-10T14:30Z')))
print(dt.strptime('2024-03-10 14:30:15', '%Y-%m-%d %H:%M:%S'), dt.strptime('10/03/24 2:05 PM', '%d/%m/%y %I:%M %p'), dt.strptime('Sunday 10 March 2024', '%A %d %B %Y'), dt.strptime('2024-070', '%Y-%j'))
print(repr(dt.strptime('2024-03-10T14:30:15.5+0530', '%Y-%m-%dT%H:%M:%S.%f%z')), repr(dt.strptime('2024 Z', '%Y %z')), dt.strptime('1 2 2024', '%w %U %Y'), dt.strptime('2024 10 3', '%G %V %u'))
print(dt.min, dt.max, dt.resolution, dt(2024, 1, 1) < dt(2024, 1, 1, 0, 0, 1), dt(2024, 1, 1) == dt(2024, 1, 1), sorted([dt(2024, 5, 1), dt(2023, 5, 1)]))

# aware datetimes
u = dt(2024, 6, 1, 12, 0, tzinfo=timezone.utc)
k = dt(2024, 6, 1, 17, 30, tzinfo=tz5)
print(u, repr(u), k, repr(k), u == k, u - k, hash(u) == hash(k), u.utcoffset(), k.utcoffset(), k.tzname(), u.tzname(), k.dst())
print(u.astimezone(tz5), k.astimezone(timezone.utc), u.timestamp(), k.timestamp(), u.utctimetuple(), k.utctimetuple())
print(dt.fromtimestamp(0, timezone.utc), dt.fromtimestamp(1700000000.5, tz5), dt.fromtimestamp(86400 * 365.25, timezone(timedelta(hours=-8), 'PST')), dt.fromtimestamp(-1.5, timezone.utc))
print(u.isoformat(), k.isoformat(), k.strftime('%z %:z %Z'), u.strftime('%z|%Z'), dt(2024, 1, 1, tzinfo=timezone(timedelta(seconds=-3601, microseconds=5))).isoformat())
print(timezone.utc, repr(timezone.utc), timezone(timedelta(hours=-3)), repr(timezone(timedelta(hours=-3))), repr(timezone(timedelta(hours=1), 'CET')), str(timezone(timedelta(hours=1), 'CET')))
print(timezone(timedelta(0)) is timezone.utc, timezone(timedelta(hours=2)) == timezone(timedelta(hours=2), 'X'), timezone.min, timezone.max, datetime.UTC is timezone.utc, datetime.MINYEAR, datetime.MAXYEAR)
print(isinstance(timezone.utc, tzinfo), isinstance(u, date), isinstance(u, dt), type(u).__name__, type(u) is dt, dt.__name__, date.__module__)

class EST(tzinfo):
    def utcoffset(self, d):
        return timedelta(hours=-5)
    def dst(self, d):
        return timedelta(0)
    def tzname(self, d):
        return 'EST'
    def __repr__(self):
        return 'EST()'

e = dt(2024, 1, 15, 9, 0, tzinfo=EST())
print(e, repr(e), e.utcoffset(), e.tzname(), e.dst(), e.astimezone(timezone.utc), u.astimezone(EST()), e.isoformat(), e.strftime('%H:%M %Z %z'), isinstance(EST(), tzinfo))
print(e - dt(2024, 1, 15, 14, 0, tzinfo=timezone.utc), e == dt(2024, 1, 15, 14, 0, tzinfo=timezone.utc), e.timestamp())

# errors
for f in [lambda: date(2024, 13, 1), lambda: date(2023, 2, 29), lambda: date(0, 1, 1), lambda: time(24), lambda: time(0, 60), lambda: dt(2024, 1, 1, 0, 0, 60),
          lambda: time(microsecond=1000000), lambda: dt(2024, 1, 1, fold=2), lambda: dt(2024, 1, 1, tzinfo=1), lambda: timedelta(days=1000000000),
          lambda: date.fromisoformat('2024-1-1'), lambda: date(2024, 1, 1) + 1, lambda: date(2024, 1, 1) < dt(2024, 1, 1), lambda: dt(2024, 1, 1) < u,
          lambda: dt(2024, 1, 1) - u, lambda: timedelta(1) // 0, lambda: timezone(timedelta(hours=24)), lambda: timezone(5), lambda: tzinfo().utcoffset(None),
          lambda: dt.strptime('2024', '%Y-%m'), lambda: dt.strptime('2024-01x', '%Y-%m'), lambda: dt.strptime('x', '%Q'), lambda: date.max + timedelta(1),
          lambda: date.fromordinal(0), lambda: time(1) + timedelta(1), lambda: x.isoformat(timespec='x'), lambda: date.fromisocalendar(2024, 54, 1),
          lambda: dt.combine(1, 2), lambda: timedelta(1) < 1, lambda: dt.fromisoformat('2024-01-15T25:00'), lambda: timedelta(days=1, x=1), lambda: x.strftime(5),
          lambda: timedelta(1) * 'x', lambda: date(2024, 1, 1) - dt(2024, 1, 1), lambda: x.astimezone(5), lambda: time(1, tzinfo=tz5) < time(1)]:
    try:
        print(f())
    except Exception as ex:
        print(type(ex).__name__, ex)
