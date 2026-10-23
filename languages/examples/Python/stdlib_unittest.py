import unittest
class T(unittest.TestCase):
    def setUp(self): self.x = 1
    def test_ok(self): self.assertEqual(self.x, 1)
    def test_fail(self): self.assertEqual([1, 2], [1, 3])
    def test_raises(self):
        with self.assertRaises(ZeroDivisionError): 1 / 0
    def test_error(self): raise ValueError("boom")
    @unittest.skip("why")
    def test_skip(self): pass
    @unittest.expectedFailure
    def test_xf(self): self.assertTrue(False)
    def test_sub(self):
        for i in range(3):
            with self.subTest(i=i): self.assertLess(i, 2)
    def test_in(self): self.assertIn(3, [1, 2]); 
    def test_dict(self): self.assertDictEqual({"a": 1}, {"a": 2})
    def test_almost(self): self.assertAlmostEqual(1.0, 1.00000001)
    def test_regex(self): self.assertRegex("abc", "b")
import io
s = io.StringIO()
r = unittest.TextTestRunner(stream=s, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(T))
import re
print(re.sub(r"Ran (\d+) tests? in [0-9.]+s", r"Ran \1 tests", s.getvalue()))
print(r.wasSuccessful(), len(r.failures), len(r.errors), len(r.skipped), len(r.expectedFailures))

import warnings, logging, asyncio
log = []
class A(unittest.TestCase):
    @classmethod
    def setUpClass(cls): log.append("setUpClass")
    @classmethod
    def tearDownClass(cls): log.append("tearDownClass")
    def setUp(self): self.addCleanup(log.append, "cleanup")
    def test_warns(self):
        with self.assertWarns(DeprecationWarning) as cm: warnings.warn("old", DeprecationWarning)
        self.assertEqual(str(cm.warning), "old")
    def test_logs(self):
        with self.assertLogs("x", level="INFO") as cm: logging.getLogger("x").info("hello")
        self.assertEqual(cm.output, ["INFO:x:hello"])
    def test_regex(self):
        with self.assertRaisesRegex(ValueError, "b.d"): raise ValueError("bad")
    def test_count(self): self.assertCountEqual([1, 2, 2], [2, 1, 2])
    def test_multiline(self): self.assertMultiLineEqual("a\nb\nc\n", "a\nB\nc\n")
    def test_set(self): self.assertSetEqual({1, 2}, {2, 3})
    def test_long_list(self): self.assertEqual(list(range(30)), list(range(1, 31)))
class B(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self): self.v = await asyncio.sleep(0, result=5)
    async def test_async(self): self.assertEqual(self.v, 5)
s = io.StringIO()
r = unittest.TextTestRunner(stream=s, verbosity=2).run(unittest.TestSuite([unittest.defaultTestLoader.loadTestsFromTestCase(A), unittest.defaultTestLoader.loadTestsFromTestCase(B)]))
print(re.sub(r"Ran (\d+) tests? in [0-9.]+s", r"Ran \1 tests", s.getvalue()))
print(log, r.testsRun, len(r.failures), len(r.errors))
p = unittest.main(argv=["prog", "-q", "A.test_count", "T.test_ok"], exit=False, testRunner=unittest.TextTestRunner(stream=io.StringIO()))
print(p.result.wasSuccessful(), p.result.testsRun)
