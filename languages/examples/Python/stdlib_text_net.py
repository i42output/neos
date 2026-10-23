import shlex, getopt, colorsys, graphlib
print(shlex.split('a "b c" d\\ e \'f g\' # comment', comments=True), shlex.quote("it's"), shlex.quote("safe"), shlex.join(["a b", "c"]))
lx = shlex.shlex("a=b; c && d", posix=True, punctuation_chars=True); print(list(lx))
lx = shlex.shlex("key = 'value with spaces'"); print(list(lx))
try: shlex.split('"unterminated')
except ValueError as e: print(e)
print(getopt.getopt(["-a", "-b", "val", "--long=x", "rest"], "ab:", ["long="]))
print(getopt.gnu_getopt(["x", "-a", "y", "--flag"], "a", ["flag"]))
try: getopt.getopt(["-z"], "a")
except getopt.GetoptError as e: print(e, e.opt, e.msg)
try: getopt.getopt(["--lo"], "", ["long", "lower"])
except getopt.GetoptError as e: print(e)
print(colorsys.rgb_to_hsv(0.2, 0.4, 0.4), colorsys.hsv_to_rgb(0.5, 0.5, 0.4), colorsys.rgb_to_hls(1, 0, 0), colorsys.hls_to_rgb(0.3, 0.5, 0.5), colorsys.rgb_to_yiq(1, 0.5, 0.2), colorsys.yiq_to_rgb(0.5, 0.1, 0.1))
ts = graphlib.TopologicalSorter({"d": {"b", "c"}, "c": {"a"}, "b": {"a"}})
print(list(ts.static_order()))
ts = graphlib.TopologicalSorter(); ts.add(3, 2, 1); ts.add(1, 0); ts.prepare()
while ts.is_active():
    nodes = ts.get_ready(); print(sorted(nodes)); ts.done(*nodes)
try: list(graphlib.TopologicalSorter({1: [2], 2: [1]}).static_order())
except graphlib.CycleError as e: print("cycle", e.args[0], e.args[1])
print(graphlib.TopologicalSorter[int])
import hmac, hashlib, difflib
h = hmac.new(b"key", b"msg", hashlib.sha256); print(h.hexdigest(), h.name, h.digest_size, h.block_size)
h2 = h.copy(); h2.update(b"more"); print(h2.hexdigest() != h.hexdigest())
print(hmac.digest(b"k", b"m", "md5").hex(), hmac.compare_digest(b"a", b"a"), hmac.compare_digest("a", "b"))
print(hmac.new(b"k", digestmod="sha1").hexdigest())
try: hmac.new(b"k")
except TypeError as e: print(e)
a = "one two three four".split(); b = "zero one tree four five".split()
print(list(difflib.unified_diff(a, b, "a", "b", lineterm="")))
print(list(difflib.context_diff(a, b, lineterm=""))[:6])
print(list(difflib.ndiff(["abc\n", "def\n"], ["abd\n", "def\n"])))
s = difflib.SequenceMatcher(None, "abxcd", "abcd"); print(s.ratio(), s.get_matching_blocks(), s.get_opcodes(), s.quick_ratio())
print(difflib.get_close_matches("appel", ["ape", "apple", "peach", "puppy"]))
print(list(difflib.restore(difflib.ndiff(["a\n"], ["b\n"]), 2)))
print(difflib.IS_CHARACTER_JUNK(" "), difflib.IS_LINE_JUNK("  #  \n"))
d = difflib.HtmlDiff(); out = d.make_table(["a"], ["b"]); print(len(out) > 100, out.count("<tr>"))
print(list(difflib.diff_bytes(difflib.unified_diff, [b"a"], [b"b"], b"x", b"y", lineterm=b"")))
import ipaddress as ip
a = ip.ip_address("192.168.1.10"); print(a, repr(a), a.version, a.is_private, a.packed, int(a), a + 1, a.reverse_pointer, a.is_global)
n = ip.ip_network("10.0.0.0/30"); print(n, list(n.hosts()), n.num_addresses, n.netmask, n.broadcast_address, n.prefixlen, n.supernet(), list(n.subnets()))
print(ip.ip_interface("10.1.2.3/24"), ip.ip_interface("10.1.2.3/24").network, ip.IPv4Address(3232235777))
b = ip.ip_address("2001:db8::1"); print(b, b.exploded, b.compressed, b.is_private, b.ipv4_mapped, ip.ip_address("::ffff:1.2.3.4").ipv4_mapped)
print(ip.ip_network("2001:db8::/126").num_addresses, list(ip.collapse_addresses([ip.ip_network("1.0.0.0/24"), ip.ip_network("1.0.1.0/24")])))
print(list(ip.summarize_address_range(ip.ip_address("1.1.1.0"), ip.ip_address("1.1.1.5"))))
try: ip.ip_address("300.1.1.1")
except ValueError as e: print(e)
try: ip.ip_network("10.0.0.1/24")
except ValueError as e: print(e)
print(ip.ip_network("10.0.0.1/24", strict=False), ip.ip_address("1.2.3.4") < ip.ip_address("1.2.3.5"), hash(ip.ip_address("1.2.3.4")) == hash(ip.ip_address("1.2.3.4")))
print(sorted([ip.ip_address("10.0.0.2"), ip.ip_address("10.0.0.1")]), ip.ip_address("fe80::1%eth0").scope_id, ip.v4_int_to_packed(1))
print(ip.ip_network("192.168.0.0/16").overlaps(ip.ip_network("192.168.5.0/24")), ip.ip_network("192.168.5.0/24").subnet_of(ip.ip_network("192.168.0.0/16")))
print(list(ip.ip_network("10.0.0.0/24").address_exclude(ip.ip_network("10.0.0.0/26"))))
import html, html.parser, html.entities, urllib.parse as up
print(html.escape('<a href="x">&\'</a>'), html.escape("<>", quote=False), html.unescape("&lt;p&gt; &amp; &#39; &#x41; &copy; &notanentity; &nbsp;x"))
print(html.entities.name2codepoint["amp"], html.entities.codepoint2name[60], html.entities.html5["gt;"], len(html.entities.entitydefs))
class P(html.parser.HTMLParser):
    def handle_starttag(self, tag, attrs): print("start", tag, attrs)
    def handle_endtag(self, tag): print("end", tag)
    def handle_data(self, data): print("data", repr(data))
    def handle_comment(self, data): print("comment", repr(data))
    def handle_entityref(self, name): print("entity", name)
    def handle_decl(self, d): print("decl", d)
p = P(convert_charrefs=False); p.feed('<!DOCTYPE html><html><body class="a" id=b><!-- c --><p>Hi &amp; bye<br/></p></body></html>'); p.close()
P().feed("<script>if (a < b) {}</script><img src='x'>")
u = up.urlparse("https://user:pw@example.com:8080/p/a/t/h;params?q=1&r=2#frag"); print(u, u.hostname, u.port, u.username, u.password, u.geturl())
print(up.urlsplit("http://[::1]:80/x"), up.urljoin("http://a/b/c/d;p?q", "../g"), up.urljoin("http://a/b/", "//x/y"))
print(up.quote("a b/c?d=é"), up.quote_plus("a b&c"), up.unquote("%41%20b%C3%A9"), up.unquote_plus("a+b%2B"), up.quote(b"\xff"))
print(up.urlencode({"a": 1, "b": "x y", "c": ["1", "2"]}, doseq=True), up.parse_qs("a=1&a=2&b=x+y"), up.parse_qsl("a=1&b=&c", keep_blank_values=True))
print(up.urlunparse(("http", "h", "/p", "", "q=1", "f")), up.urldefrag("http://x/y#z"), up.urlsplit("mailto:a@b.c"), up.unwrap("<URL:http://x>"))
import tomllib
doc = '''
title = "TOML Example"
[owner]
name = "Tom"
dob = 1979-05-27T07:32:00-08:00
[database]
enabled = true
ports = [ 8000, 8001, 8002 ]
data = [ ["delta", "phi"], [3.14] ]
temp_targets = { cpu = 79.5, case = 72.0 }
[[products]]
name = "Hammer"
sku = 738594937
[[products]]
name = "Nail"
color = "gray"
str2 = """
multi
line"""
lit = 'C:\\path'
hex = 0xDEADBEEF
inf = inf
t = 07:32:00
d = 1979-05-27
'''
d = tomllib.loads(doc)
print(d)
try: tomllib.loads("a = ")
except tomllib.TOMLDecodeError as e: print("err", e)
try: tomllib.loads("a = 1\na = 2")
except tomllib.TOMLDecodeError as e: print("err", e)
print(tomllib.loads("x = 1.5", parse_float=str))
import io
print(tomllib.load(io.BytesIO(b"k = 'v'")))
