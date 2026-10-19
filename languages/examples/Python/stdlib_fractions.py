from fractions import Fraction
import math

a = Fraction(3, 4)
b = Fraction('1/3')
print(a, b, repr(a), a.numerator, a.denominator, Fraction(6, -8), Fraction(0, 5), Fraction(7), Fraction(), Fraction(-2, 4))
print(Fraction('0.25'), Fraction('-1.5e2'), Fraction(' 3/ 6 '), Fraction('1_000/3'), Fraction('.5'), Fraction('5.'), Fraction('2e-3'), Fraction(1.5), Fraction(0.1), Fraction(a, b))
print(a + b, a - b, a * b, a / b, a // b, a % b, -a, +a, abs(Fraction(-1, 2)), a ** 2, a ** -2, Fraction(-2, 3) ** -3, Fraction(4, 9) ** Fraction(1, 2), 2 ** Fraction(1, 2))
print(a + 1, 1 + a, a * 2, 3 / a, a - 0.25, 0.5 + a, a * 1j, divmod(Fraction(7, 2), Fraction(2, 3)), 7 // a, Fraction(5, 2) % 1)
print(a == Fraction(6, 8), a == 0.75, a != b, a > b, a < 1, a <= 0.75, 1 > a, Fraction(1, 2) == 0.5, Fraction(1, 3) == 1 / 3, Fraction(2, 1) == 2, Fraction(1) < float('inf'))
print(hash(Fraction(1, 2)) == hash(0.5), hash(Fraction(3)) == hash(3), hash(Fraction(1, 3)), {Fraction(2, 4): 'x'}[Fraction(1, 2)], hash(Fraction(-7, 5)))
print(float(a), int(Fraction(-7, 2)), math.floor(Fraction(-7, 2)), math.ceil(Fraction(-7, 2)), math.trunc(Fraction(-7, 2)), round(Fraction(5, 2)), round(Fraction(7, 2)), round(Fraction(1234, 1000), 2), round(Fraction(15, 1), -1))
print(Fraction(math.pi).limit_denominator(1000), Fraction(3.14159).limit_denominator(10), Fraction(1, 3).limit_denominator(), a.as_integer_ratio(), Fraction(4, 2).is_integer(), bool(Fraction(0)), sum([Fraction(1, n) for n in range(1, 6)]))
print(Fraction.from_float(0.125), sorted([Fraction(1, 2), Fraction(1, 3), 0.4, 1]), max(Fraction(2, 3), Fraction(3, 5)), isinstance(a, Fraction), f'{a}', f'{Fraction(1, 3):.4f}', str(Fraction(10, 5)))
for bad in [lambda: Fraction(1, 0), lambda: Fraction('abc'), lambda: Fraction('1/0'), lambda: Fraction(1.5, 2), lambda: a / 0, lambda: Fraction(0) ** -1, lambda: Fraction(float('nan')), lambda: Fraction([1])]:
    try:
        bad()
    except (ZeroDivisionError, ValueError, TypeError) as e:
        print(type(e).__name__, e)
