from decimal import Decimal, Context, getcontext, localcontext, InvalidOperation, Overflow, Underflow, Subnormal, ROUND_UP, ROUND_DOWN, ROUND_CEILING, ROUND_FLOOR, ROUND_HALF_UP, ROUND_HALF_DOWN, ROUND_HALF_EVEN, ROUND_05UP
modes = [ROUND_UP, ROUND_DOWN, ROUND_CEILING, ROUND_FLOOR, ROUND_HALF_UP, ROUND_HALF_DOWN, ROUND_HALF_EVEN, ROUND_05UP]
for v in ['5.5', '2.5', '1.6', '1.1', '-1.1', '-2.5', '-5.5', '0.5', '1.05', '-0.4']:
    print(v, [str(Decimal(v).quantize(Decimal('1'), rounding=m)) for m in modes])
getcontext().prec = 50
print(Decimal(2).sqrt(), Decimal(3) ** Decimal('1.5'), Decimal('0.5').ln(), Decimal(-3).exp(), Decimal('12.5').log10())
print(Decimal('1.0001') ** 10000, Decimal(7) ** Decimal('-0.25'), Decimal('1e-10').ln())
getcontext().prec = 28
ctx = Context(prec=5, Emax=10, Emin=-10, traps=[])
print(ctx.multiply(Decimal('1e8'), Decimal('1e8')), ctx.flags[Overflow], ctx.multiply(Decimal('1e-8'), Decimal('1e-8')), ctx.flags[Underflow])
print(ctx.create_decimal('1.23e-13'), ctx.flags[Subnormal], ctx.create_decimal('1e20'))
c = Context(prec=3, rounding=ROUND_DOWN, traps=[], Emax=5)
print(c.create_decimal('999999999'), c)
try:
    Context(prec=0)
except ValueError as e:
    print(e)
x = Decimal('-12345.6789')
for spec in ['', '.2f', '15.3f', '<15', '^15,.1f', 'e', '.0e', 'g', '.10g', '+', ' .1%', 'F']:
    print(repr(format(x, spec)))
print(Decimal('nan').is_nan(), Decimal('nan') == Decimal('nan'), Decimal('NaN123'), Decimal('-sNaN5'))
try:
    Decimal('nan') < 1
except InvalidOperation as e:
    print('nan compare', e)
print(Decimal(123456789) * Decimal(987654321), Decimal(1) / Decimal(1024), Decimal('0.1') + Decimal('0.2') == Decimal('0.3'))
print([Decimal(i) / 8 for i in range(5)])
import threading
out = []
def worker(p):
    with localcontext() as c:
        c.prec = p
        out.append((p, str(Decimal(1) / Decimal(7))))
ts = [threading.Thread(target=worker, args=(p,)) for p in (3, 6, 9)]
for t in ts: t.start()
for t in ts: t.join()
print(sorted(out), getcontext().prec)
