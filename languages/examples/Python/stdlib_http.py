# The standard library's http and urllib packages: status codes, cookies (http.cookies, http.cookiejar), robots.txt, urllib's
# requests and errors, and an http.server serving http.client and urllib.request on the loopback interface
import http, http.client, http.cookies, http.cookiejar, http.server
import urllib.error, urllib.request, urllib.response, urllib.robotparser
import threading, io

print(http.HTTPStatus.NOT_FOUND, int(http.HTTPStatus.OK), http.HTTPStatus(418).phrase)
print(http.HTTPMethod.GET, http.client.responses[404])

# cookies
c = http.cookies.SimpleCookie()
c["session"] = "abc123"
c["session"]["path"] = "/"
c["session"]["httponly"] = True
c["theme"] = 'dark "blue"'
print(c.output())
c2 = http.cookies.SimpleCookie('a=1; b="two words"; c=3')
print(sorted((k, m.value) for k, m in c2.items()))

# cookiejar helpers
print(http.cookiejar.http2time("Wed, 09 Feb 1994 22:23:32 GMT"))
print(http.cookiejar.time2isoz(0), http.cookiejar.time2netscape(86400))
print(http.cookiejar.split_header_words(['foo="bar"; port="80,81"; discard, bar=baz']))
print(http.cookiejar.domain_match("www.example.com", ".example.com"))

# robotparser
rp = urllib.robotparser.RobotFileParser()
rp.parse("""User-agent: *
Disallow: /private/
Allow: /private/ok
Crawl-delay: 2
""".splitlines())
print(rp.can_fetch("bot", "http://x/private/a"), rp.can_fetch("bot", "http://x/private/ok"), rp.can_fetch("bot", "http://x/pub"), rp.crawl_delay("bot"))

# urllib.error / response
e = urllib.error.HTTPError("http://x", 500, "Server Error", {}, io.BytesIO(b"body"))
print(e.code, e.reason, str(e), e.read())
r = urllib.response.addinfourl(io.BytesIO(b"hello"), {"X": "1"}, "http://x/", 200)
print(r.read(), r.geturl(), r.getcode(), r.status)
print(urllib.error.URLError("nope"))

# request objects
req = urllib.request.Request("http://example.com/a?b=1", data=b"x=1", headers={"X-Test": "yes"})
print(req.get_method(), req.host, req.selector, req.full_url, req.header_items())
print(urllib.request.pathname2url("/tmp/a b"), urllib.request.url2pathname("/tmp/a%20b"))

# server + client round trip
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = ("path=" + self.path + " ua=" + str(self.headers.get("X-Test"))).encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Set-Cookie", "k=v; Path=/")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_POST(self):
        n = int(self.headers["Content-Length"])
        data = self.rfile.read(n)
        self.send_response(201)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data.upper())
    def log_message(self, *a):
        pass

srv = http.server.HTTPServer(("127.0.0.1", 0), Handler)
port = srv.server_address[1]
t = threading.Thread(target=srv.serve_forever, daemon=True)
t.start()

conn = http.client.HTTPConnection("127.0.0.1", port)
conn.request("GET", "/hello", headers={"X-Test": "client"})
resp = conn.getresponse()
print(resp.status, resp.reason, resp.getheader("Content-Type"), resp.read())
conn.request("POST", "/p", body=b"payload")
resp = conn.getresponse()
print(resp.status, resp.read())
conn.close()

jar = http.cookiejar.CookieJar()
opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
with opener.open(urllib.request.Request("http://127.0.0.1:%d/x" % port, headers={"X-Test": "urllib"})) as f:
    print(f.status, f.read())
print([(ck.name, ck.value) for ck in jar])
try:
    urllib.request.urlopen("http://127.0.0.1:%d/" % port, data=b"").read()
except urllib.error.HTTPError as ex:
    print("HTTPError", ex.code)
srv.shutdown()
srv.server_close()

import os, tempfile, functools, base64, email.message, urllib.parse, shutil

d = tempfile.mkdtemp()
with open(os.path.join(d, "a.txt"), "w") as f:
    f.write("hello file\n")
os.mkdir(os.path.join(d, "sub"))

class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

class Chunky(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def do_GET(self):
        if self.path == "/auth":
            if self.headers.get("Authorization") != "Basic " + base64.b64encode(b"u:p").decode():
                self.send_response(401)
                self.send_header("WWW-Authenticate", 'Basic realm="r"')
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            body = b"authorized"
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path == "/redir":
            self.send_response(302)
            self.send_header("Location", "/chunks")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Transfer-Encoding", "chunked")
        self.send_header("Set-Cookie", "sid=42; Path=/; Max-Age=3600")
        self.end_headers()
        for part in (b"one ", b"two ", b"three"):
            self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
        self.wfile.write(b"0\r\n\r\n")
    def log_message(self, *a):
        pass

def serve(handler):
    s = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=s.serve_forever, daemon=True).start()
    return s

fs = serve(functools.partial(Quiet, directory=d))
cs = serve(Chunky)
fp, cp = fs.server_address[1], cs.server_address[1]

with urllib.request.urlopen("http://127.0.0.1:%d/a.txt" % fp) as r:
    print(r.status, r.headers["Content-Type"], r.headers["Content-Length"], r.read())
listing = urllib.request.urlopen("http://127.0.0.1:%d/" % fp).read().decode()
print("a.txt" in listing, "sub/" in listing)
try:
    urllib.request.urlopen("http://127.0.0.1:%d/missing" % fp)
except urllib.error.HTTPError as e:
    print("missing", e.code, e.reason)
r = urllib.request.urlopen("http://127.0.0.1:%d/sub" % fp)
print("redirected", r.geturl().endswith("/sub/"))

conn = http.client.HTTPConnection("127.0.0.1", cp)
conn.request("GET", "/chunks")
resp = conn.getresponse()
print(resp.status, resp.getheader("Transfer-Encoding"), resp.chunked, resp.read())
conn.request("GET", "/chunks")
resp = conn.getresponse()
print(resp.read(4), resp.read())
conn.close()

jar = http.cookiejar.MozillaCookieJar()
pm = urllib.request.HTTPPasswordMgrWithDefaultRealm()
pm.add_password(None, "http://127.0.0.1:%d/" % cp, "u", "p")
opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar), urllib.request.HTTPBasicAuthHandler(pm))
print(opener.open("http://127.0.0.1:%d/redir" % cp).read())
print(opener.open("http://127.0.0.1:%d/auth" % cp).read())
print([(c.name, c.value, c.path) for c in jar])
path = os.path.join(d, "cookies.txt")
jar.save(path, ignore_discard=True)
jar2 = http.cookiejar.MozillaCookieJar()
jar2.load(path, ignore_discard=True)
print([(c.name, c.value) for c in jar2])
lwp = http.cookiejar.LWPCookieJar()
for c in jar2:
    lwp.set_cookie(c)
print(lwp.as_lwp_str().split(";")[0])

print(urllib.request.urlopen("data:text/plain;base64,SGVsbG8=").read())
with urllib.request.urlopen(urllib.parse.urljoin("file:", urllib.request.pathname2url(os.path.join(d, "a.txt")))) as r:
    print(r.read(), r.headers["Content-length"])

m = http.cookies.Morsel()
m.set("k", "v", "v")
m["expires"] = 0
m["samesite"] = "Lax"
print(m.OutputString().split("; ")[0], m.js_output().count("document.cookie"))

msg = http.client.parse_headers(io.BytesIO(b"A: 1\r\nB: two\r\nA: 3\r\n\r\n"))
print(msg.get_all("A"), msg["b"], isinstance(msg, email.message.Message))
print(http.server.BaseHTTPRequestHandler.responses[404])
print(urllib.request.parse_http_list('a, b="c, d", e'), urllib.request.parse_keqv_list(['a=1', 'b="2"']))
print(urllib.request.getproxies_environment() == {} or True)

fs.shutdown(); cs.shutdown(); fs.server_close(); cs.server_close()
shutil.rmtree(d)
print("done")
