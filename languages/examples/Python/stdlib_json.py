import json
from collections import OrderedDict, namedtuple

data = {'name': 'Ann', 'age': 30, 'tags': ['a', 'b'], 'ok': True, 'none': None, 'pi': 3.14159, 'nested': {'x': [1, 2.5, {'y': False}]}, 'big': 2**70}
s = json.dumps(data)
print(s)
print(json.dumps(data, indent=2))
print(json.dumps(data, indent='\t', sort_keys=True))
print(json.dumps(data, separators=(',', ':')), json.dumps([1, (2, 3)], indent=0))
print(json.dumps('héllo €😀 "q" \\ \n\t\x01'), json.dumps('héllo €😀', ensure_ascii=False), json.dumps({1: 'a', 2.5: 'b', True: 'c', None: 'd'}))
print(json.dumps(float('nan')), json.dumps(float('inf')), json.dumps(-float('inf')), json.dumps(1e100), json.dumps(0.1 + 0.2), json.dumps(-0.0), json.dumps([]), json.dumps({}), json.dumps({}, indent=2))
print(json.loads(s) == data, json.loads('[1, 2.0, "x", null, true, false, {"a": {}}]'), json.loads(' {"a" : 1 , "b":[ ]} '), json.loads('"\\u00e9\\ud83d\\ude00\\n"'), json.loads('-12.5e3'), json.loads('123456789012345678901234567890'))
print(json.loads('NaN'), json.loads('[Infinity, -Infinity]'), json.loads(b'{"b": 1}'), json.loads('{"a": 1, "a": 2}'), json.loads('0'), json.loads('1E2'), json.loads('"\\/"'))
print(json.loads('{"x": 1.5, "y": 2}', parse_float=str, parse_int=lambda v: int(v) * 10), json.loads('{"b": 1, "a": 2}', object_pairs_hook=OrderedDict), json.loads('[NaN]', parse_constant=lambda c: c.lower()))
def as_point(d):
    return (d['x'], d['y']) if 'x' in d else d
print(json.loads('[{"x": 1, "y": 2}, {"z": 3}]', object_hook=as_point))
print(json.dumps(OrderedDict([('z', 1), ('a', 2)])), json.dumps(namedtuple('P', 'x y')(1, 2)), json.dumps({'k': [1, {'m': None}]}, indent=1, sort_keys=True))
class Thing:
    def __init__(self, v):
        self.v = v
print(json.dumps({'t': Thing(5)}, default=lambda o: {'thing': o.v}), json.dumps([1, Thing(2)], default=vars))
class ThingEncoder(json.JSONEncoder):
    def default(self, o):
        if isinstance(o, Thing):
            return ['Thing', o.v]
        if isinstance(o, set):
            return sorted(o)
        return super().default(o)
print(json.dumps({'a': Thing(1), 's': {3, 1, 2}}, cls=ThingEncoder), ThingEncoder(indent=1).encode([Thing(9)]), json.JSONEncoder(sort_keys=True).encode({'b': 1, 'a': 2}))
print(json.JSONDecoder().decode('[1]'), json.JSONDecoder(object_hook=lambda d: sorted(d)).decode('{"b": 1, "a": 2}'), json.JSONDecoder().raw_decode('{"a": 1} extra'))
for bad in ['', '[1, 2', '{"a" 1}', '{"a": 1,}', '[1,]', '{a: 1}', '[1] x', '"abc', '"\\x"', 'tru', '{"a": 1 "b": 2}', '"\\u12"', '\n\n  [', '"a\x01"']:
    try:
        json.loads(bad)
    except json.JSONDecodeError as e:
        print(repr(bad), '|', e, '|', e.msg, e.pos, e.lineno, e.colno, isinstance(e, ValueError))
for bad in [lambda: json.dumps({(1, 2): 3}), lambda: json.dumps(Thing(1)), lambda: json.dumps(float('nan'), allow_nan=False), lambda: json.dumps({1, 2}), lambda: json.loads(5)]:
    try:
        bad()
    except (TypeError, ValueError) as e:
        print(type(e).__name__, e)
circular = []
circular.append(circular)
try:
    json.dumps(circular)
except ValueError as e:
    print('ValueError', e)
print(json.dumps({(1, 2): 3, 'ok': 1}, skipkeys=True))
import io
buffer = io.StringIO()
json.dump({'a': [1, 2]}, buffer, indent=1)
print(repr(buffer.getvalue()), json.load(io.StringIO('{"z": 26}')))
