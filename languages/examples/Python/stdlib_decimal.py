import decimal
from decimal import Decimal, getcontext, localcontext, Context, ROUND_HALF_UP, ROUND_DOWN, ROUND_FLOOR, ROUND_CEILING, InvalidOperation, DivisionByZero

print(Decimal('1.1') + Decimal('2.2'))
print(Decimal(10) / Decimal(3))
print(Decimal('1') / Decimal('7'))
print(repr(Decimal('3.14')), str(Decimal('-0.000001234')), Decimal('1e-7'), Decimal('123e10'))
print(Decimal('1.30') * Decimal('2'), Decimal('100') - Decimal('0.01'))
print(Decimal('7') // Decimal('2'), Decimal('-7') // Decimal('2'), Decimal('7') % Decimal('-2'), Decimal('-7') % Decimal('2'))
print(divmod(Decimal('7.5'), Decimal('2')))
print(Decimal(2) ** 10, Decimal('2') ** Decimal('0.5'), Decimal('1.5') ** 3, Decimal(2) ** -2)
print(Decimal(2).sqrt(), Decimal('16').sqrt(), Decimal('0.25').sqrt())
print(Decimal(1).exp(), Decimal(10).ln(), Decimal(1000).log10(), Decimal('2').log10())
print(Decimal('3.14159').quantize(Decimal('0.01')), Decimal('2.675').quantize(Decimal('0.01'), rounding=ROUND_HALF_UP))
print(Decimal('7.325').quantize(Decimal('.01'), rounding=ROUND_DOWN), Decimal('-1.5').quantize(Decimal('1'), rounding=ROUND_FLOOR))
print(Decimal('1.200').normalize(), Decimal('120E+3').normalize(), Decimal('0.00').normalize())
print(Decimal(0.1), Decimal.from_float(0.5), Decimal(-2.75))
print(Decimal('NaN'), Decimal('-Infinity'), Decimal('inf') + 1, Decimal('sNaN').is_snan())
print(Decimal('1.5').as_tuple(), Decimal('-0.025').as_integer_ratio(), Decimal('inf').as_tuple())
print(Decimal((1, (3, 1, 4), -2)), Decimal('  12_345.6  '))
print(int(Decimal('-3.9')), float(Decimal('2.5')), round(Decimal('2.5')), round(Decimal('3.5')), round(Decimal('3.14159'), 3))
import math
print(math.floor(Decimal('-2.5')), math.ceil(Decimal('2.1')), math.trunc(Decimal('-2.9')))
print(Decimal('1.0') == Decimal('1'), Decimal('1') == 1, Decimal('0.5') < 1, Decimal('0.1') == 0.1, hash(Decimal('1.0')) == hash(1), hash(Decimal('0.5')) == hash(0.5))
print(sorted([Decimal('3'), Decimal('-1.5'), Decimal('2.25')]), max(Decimal('1'), Decimal('2')), sum([Decimal('0.1')] * 10))
print(Decimal('-0') + Decimal('0'), -Decimal('0'), abs(Decimal('-3.2')), +Decimal('1.23456789012345678901234567890'))
print(Decimal('123.456').adjusted(), Decimal('0.001').adjusted(), Decimal('1e5').to_eng_string(), Decimal('1.23E-7').to_eng_string())
print(Decimal('2.5').to_integral_value(), Decimal('3.5').to_integral(), Decimal('-1.7').to_integral_value(rounding=ROUND_CEILING))
print(Decimal(1).compare(Decimal(2)), Decimal('2.0').max(Decimal('2')), Decimal('-3').min(Decimal('1')), Decimal('1.10').same_quantum(Decimal('2.20')))
print(Decimal('-5').copy_abs(), Decimal('5').copy_negate(), Decimal('5').copy_sign(Decimal('-1')), Decimal('10').remainder_near(Decimal('6')))
print(Decimal('-0').is_signed(), Decimal('0e3').is_zero(), Decimal('1').is_finite(), Decimal('1e-999999').is_subnormal(), Decimal('3').number_class())
print(format(Decimal('1234567.891'), ','), format(Decimal('1234567.891'), ',.2f'), format(Decimal('3.14159'), '.3'), format(Decimal('0.5'), '%'))
print(format(Decimal('1.5'), '>10'), format(Decimal('-1.5'), '010.3f'), format(Decimal('12345'), '.2e'), format(Decimal('1e100'), 'f')[:10], '{:+.1f}'.format(Decimal('2.25')))
print(f"{Decimal('0.000012345'):g} {Decimal('123'):E} {Decimal('-0.00'):z.1f} {Decimal('1.5'):_^9}")

c = getcontext()
print(c.prec, c.rounding, c.Emax, c.Emin, c.capitals, c.clamp)
print(c)
c.prec = 6
print(Decimal(1) / Decimal(7), Decimal('123456789') + 0)
c.prec = 28
with localcontext() as ctx:
    ctx.prec = 3
    print(Decimal('1.23456') * 1, ctx.prec)
    print(Decimal(2).sqrt())
print(getcontext().prec, Decimal(2).sqrt())
with localcontext(prec=10, rounding=ROUND_DOWN):
    print(Decimal(2) / 3)

try:
    Decimal(1) / Decimal(0)
except DivisionByZero as e:
    print('DivisionByZero', type(e).__name__, isinstance(e, ZeroDivisionError), isinstance(e, ArithmeticError), e)
try:
    Decimal('abc')
except InvalidOperation as e:
    print('InvalidOperation', e)
try:
    Decimal(0) / Decimal(0)
except InvalidOperation as e:
    print('undefined', e, isinstance(e, ZeroDivisionError))
try:
    Decimal('inf') - Decimal('inf')
except decimal.InvalidOperation as e:
    print('inf-inf', e)
try:
    Decimal(1.5) < 2.5
    print('float compare ok')
except Exception as e:
    print(type(e).__name__)
try:
    Decimal('1') + 1.5
except TypeError as e:
    print('TypeError', e)
try:
    Decimal([1, 2])
except ValueError as e:
    print('ValueError', e)
try:
    int(Decimal('nan'))
except ValueError as e:
    print('ValueError', e)

ctx = Context(prec=5, rounding=ROUND_HALF_UP)
print(ctx.prec, ctx.rounding, ctx.add(Decimal('1.23456'), Decimal('2')), ctx.divide(Decimal(1), Decimal(3)), ctx.create_decimal('3.14159265'))
c.traps[DivisionByZero] = False
print(Decimal(1) / Decimal(0), Decimal(-1) / Decimal(0), c.flags[DivisionByZero])
c.traps[DivisionByZero] = True
c.clear_flags()
x = Decimal(1) / Decimal(3)
print(c.flags[decimal.Inexact], c.flags[decimal.Rounded], c.flags[DivisionByZero])
ext = decimal.ExtendedContext.copy()
print(ext.prec, ext.divide(Decimal(0), Decimal(0)), ext.sqrt(Decimal(-1)))
print(issubclass(decimal.Overflow, decimal.Inexact), issubclass(decimal.FloatOperation, TypeError), issubclass(decimal.ConversionSyntax, decimal.DecimalException))
big = Decimal(10) ** 50
print(big, big + 1, Decimal('9' * 40) + 1)
print(Decimal('1e999999') * 10 if False else Decimal('0.1') * 3 == Decimal('0.3'))
print(isinstance(Decimal(1), Decimal), Decimal(1).real, Decimal('2.5').imag, Decimal('1.50').__round__(1))
print(Decimal(3).fma(2, 1), Decimal('1.23').scaleb(2), str(Decimal('1E+2')), Decimal(100).ln())
