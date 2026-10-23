from unittest import mock
from unittest.mock import Mock, MagicMock, patch, call, sentinel, ANY, AsyncMock, create_autospec, PropertyMock, mock_open
import os, asyncio
m = Mock(return_value=3)
print(m(1, b=2), m.call_args, m.call_count, m.called)
m.assert_called_once_with(1, b=2)
m.foo.bar(5); print(m.foo.bar.call_args_list, m.mock_calls)
try: m.assert_called_with(2)
except AssertionError as e: print("AE", str(e).splitlines()[0])
mm = MagicMock(); mm.__len__.return_value = 7; print(len(mm), repr(mm[3]).split(" id=")[0], int(mm), bool(mm))
mm.__getitem__.side_effect = lambda k: k * 2; print(mm[4])
s = Mock(side_effect=[1, ValueError("x"), 3]); print(s())
try: s()
except ValueError as e: print("VE", e)
print(s())
with patch("os.getcwd", return_value="/fake") as p: print(os.getcwd(), p.called)
print(os.getcwd() != "/fake")
@patch("os.path.exists")
def f(mock_exists): mock_exists.return_value = True; return os.path.exists("/nope")
print(f())
with patch.object(os, "sep", "!"): print(os.sep)
with patch.dict(os.environ, {"MOCKVAR": "1"}): print(os.environ["MOCKVAR"])
print("MOCKVAR" in os.environ)
print(sentinel.foo is sentinel.foo, repr(sentinel.foo), ANY == 5, call(1, x=2) == call(1, x=2))
class C:
    def meth(self, a, b): return a + b
spec = create_autospec(C); inst = spec(); inst.meth(1, 2); print(inst.meth.call_args)
try: inst.meth(1)
except TypeError as e: print("TE", e)
am = AsyncMock(return_value=9); print(asyncio.run(am()), am.await_count)
with patch("builtins.open", mock_open(read_data="data!")) as mo:
    with open("x") as fh: print(fh.read())
class P:
    @property
    def x(self): return 1
with patch.object(P, "x", new_callable=PropertyMock) as px:
    px.return_value = 5; print(P().x)
print(repr(Mock(name="nm"))[:12], Mock(spec=["a"]).a is not None)
try: Mock(spec=["a"]).b
except AttributeError as e: print("AttrE", e)
m2 = Mock(); m2.method(1); m2.method.assert_called_once(); m2.reset_mock(); print(m2.method.called)
m3 = Mock(); m3.configure_mock(**{"a.return_value": 1, "b": 2}); print(m3.a(), m3.b)
import pkgutil
print(pkgutil.resolve_name("os.path:join")("a", "b"), pkgutil.resolve_name("collections.OrderedDict").__name__)
try: pkgutil.resolve_name("os:nope")
except AttributeError as e: print("AttributeError", e)
