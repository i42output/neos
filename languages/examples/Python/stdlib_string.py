# string: Formatter (str.format's machinery, open to subclasses), Template ($-substitution), capwords and the constants
import string

f = string.Formatter()
print(f.format('{0} + {1} = {result}', 2, 3, result=5))
print(f.format('{:>8}|{:<8}|{:^8}|', 'right', 'left', 'centre'))
print(f.format('{0.real}, {0.imag}; {1[1]}; {2[key]}', 3 + 4j, ['a', 'b'], {'key': 'value'}))
print(f.format('{:{width}.{precision}f}', 3.14159265, width=10, precision=3))
print(f.format('{!r} {!s} {!a}', 'quoted', 'plain', 'café'))
print(f.vformat('{greeting}, {0}!', ('world',), {'greeting': 'Hello'}))

# parse() splits a format string into literal text and replacement fields
for literal, field, spec, conversion in f.parse('Total: {amount:>10.2f} {currency!r} {{braces}}'):
    print((literal, field, spec, conversion))

# a subclass can change how fields are looked up, converted and formatted
class Report(string.Formatter):
    def get_value(self, key, args, kwargs):
        if isinstance(key, str) and key not in kwargs:
            return '<' + key + '?>'
        return super().get_value(key, args, kwargs)
    def convert_field(self, value, conversion):
        if conversion == 'u':
            return str(value).upper()
        return super().convert_field(value, conversion)
    def format_field(self, value, format_spec):
        if format_spec == 'money':
            return f'${value:,.2f}'
        return super().format_field(value, format_spec)

report = Report()
print(report.format('{customer!u} owes {amount:money} (account {account})', customer='acme', amount=1234567.891))

# check_unused_args() can reject arguments that were never used
class Strict(string.Formatter):
    def check_unused_args(self, used_args, args, kwargs):
        unused = [str(i) for i in range(len(args)) if i not in used_args] + sorted(k for k in kwargs if k not in used_args)
        if unused:
            raise ValueError('unused arguments: ' + ', '.join(unused))

try:
    Strict().format('{0} {name}', 'used', 'spare', name='n', extra='x')
except ValueError as e:
    print('ValueError:', e)

for bad in ['{0', '}', '{0!x}', '{} {0}', '{3}', '{missing}']:
    try:
        f.format(bad, 'only')
    except (ValueError, IndexError, KeyError) as e:
        print(repr(bad), type(e).__name__ + ':', e)

# Template: simple $-substitution
t = string.Template('$name has $$${amount} in ${place}')
print(t.substitute(name='Ann', amount=20, place='the bank'))
print(t.safe_substitute(name='Ann'))
print(t.substitute({'name': 'Bob', 'place': 'savings'}, amount=5))
print(t.get_identifiers(), t.is_valid(), string.Template('$ broken').is_valid())
try:
    t.substitute(name='Ann')
except KeyError as e:
    print('KeyError:', e)
try:
    string.Template('cost: $').substitute()
except ValueError as e:
    print('ValueError:', e)

# a Template subclass with its own delimiter and identifier pattern
class Percent(string.Template):
    delimiter = '%'
    idpattern = '[a-z][a-z0-9]*'

print(Percent('%user scored %score1 (%% of %{total})').substitute(user='dee', score1=91, total=100))

# helpers and constants
print(string.capwords('the quick  brown\tfox'), '|', string.capwords('a-b-c', '-'))
print(string.ascii_letters, string.digits, string.hexdigits, string.octdigits)
print(repr(string.whitespace), len(string.printable), string.punctuation)
