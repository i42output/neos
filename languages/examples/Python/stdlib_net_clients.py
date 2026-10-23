# Clients of the standard library's network protocols (smtplib, poplib, imaplib, ftplib) against local servers; mailbox's
# mbox, Maildir and MH; wsgiref; nturl2path; code, codeop and rlcompleter
import smtplib, poplib, imaplib, ftplib, mailbox, nturl2path, code, codeop, rlcompleter
import wsgiref, wsgiref.util, wsgiref.headers, wsgiref.simple_server, wsgiref.validate, wsgiref.handlers
import socketserver, threading, tempfile, os, io, email.message, urllib.request, shutil, sys

# --- a line-based fake server: replies from a script of (expected prefix, reply lines)
class LineServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

def serve(greeting, respond):
    class H(socketserver.StreamRequestHandler):
        def handle(self):
            self.wfile.write(greeting)
            while True:
                line = self.rfile.readline()
                if not line:
                    break
                reply, done = respond(line.rstrip(b"\r\n"), self)
                self.wfile.write(reply)
                if done:
                    break
    s = LineServer(("127.0.0.1", 0), H)
    threading.Thread(target=s.serve_forever, daemon=True).start()
    return s

# SMTP
received = []
def smtp_respond(line, h):
    cmd = line.split(b" ")[0].upper()
    if cmd == b"EHLO":
        return b"250-localhost\r\n250-SIZE 1000000\r\n250-8BITMIME\r\n250 AUTH PLAIN LOGIN\r\n", False
    if cmd == b"AUTH":
        return b"235 ok\r\n", False
    if cmd in (b"MAIL", b"RCPT"):
        received.append(line.decode())
        return b"250 ok\r\n", False
    if cmd == b"DATA":
        h.wfile.write(b"354 go\r\n")
        body = []
        while True:
            l = h.rfile.readline()
            if l == b".\r\n":
                break
            body.append(l)
        received.append(b"".join(body).decode())
        return b"250 queued\r\n", False
    if cmd == b"QUIT":
        return b"221 bye\r\n", True
    if cmd == b"NOOP":
        return b"250 ok\r\n", False
    return b"502 no\r\n", False

s = serve(b"220 localhost ESMTP test\r\n", smtp_respond)
with smtplib.SMTP("127.0.0.1", s.server_address[1]) as c:
    print(c.ehlo())
    print(c.has_extn("size"), c.esmtp_features.get("auth"))
    c.login("user", "pass")
    m = email.message.EmailMessage()
    m["From"] = "a@example.com"
    m["To"] = "b@example.com, c@example.com"
    m["Subject"] = "hello"
    m.set_content("line one\n.leading dot\n")
    print(c.send_message(m))
    print(c.noop())
print(received[:3])
print(received[3].replace("\r\n", "\n").split("\n\n", 1)[1])
print(smtplib.quoteaddr("Joe <joe@x.org>"), smtplib.quotedata("a\n.b"))
s.shutdown()

# POP3
msgs = [b"Subject: one\r\n\r\nfirst", b"Subject: two\r\n\r\nsecond body\r\n..dotted"]
def pop_respond(line, h):
    cmd = line.split(b" ")[0].upper()
    if cmd in (b"USER", b"PASS"):
        return b"+OK\r\n", False
    if cmd == b"STAT":
        return b"+OK %d %d\r\n" % (len(msgs), sum(map(len, msgs))), False
    if cmd == b"LIST":
        return b"+OK\r\n" + b"".join(b"%d %d\r\n" % (i + 1, len(m)) for i, m in enumerate(msgs)) + b".\r\n", False
    if cmd == b"RETR":
        i = int(line.split()[1]) - 1
        return b"+OK\r\n" + msgs[i] + b"\r\n.\r\n", False
    if cmd == b"QUIT":
        return b"+OK bye\r\n", True
    return b"-ERR\r\n", False
s = serve(b"+OK POP3 ready <1896.697170952@dbc.mtview.ca.us>\r\n", pop_respond)
p = poplib.POP3("127.0.0.1", s.server_address[1])
print(p.getwelcome(), p.user("u"), p.pass_("p"), p.stat(), p.list())
print(p.retr(2))
try:
    p.dele(1)
except poplib.error_proto as e:
    print("error_proto", e)
print(p.quit())
s.shutdown()

# IMAP
def imap_respond(line, h):
    tag, _, rest = line.partition(b" ")
    cmd = rest.split(b" ")[0].upper()
    if cmd == b"CAPABILITY":
        return b"* CAPABILITY IMAP4rev1 AUTH=PLAIN\r\n" + tag + b" OK done\r\n", False
    if cmd == b"LOGIN":
        return tag + b" OK logged in\r\n", False
    if cmd == b"SELECT":
        return b"* 2 EXISTS\r\n* 0 RECENT\r\n* FLAGS (\\Seen)\r\n" + tag + b" OK [READ-WRITE] selected\r\n", False
    if cmd == b"SEARCH":
        return b"* SEARCH 1 2\r\n" + tag + b" OK done\r\n", False
    if cmd == b"FETCH":
        body = b"Subject: hi\r\n\r\nbody"
        return b"* 1 FETCH (RFC822 {%d}\r\n" % len(body) + body + b")\r\n" + tag + b" OK done\r\n", False
    if cmd == b"LOGOUT":
        return b"* BYE\r\n" + tag + b" OK bye\r\n", True
    return tag + b" BAD\r\n", False
s = serve(b"* OK IMAP4rev1 ready\r\n", imap_respond)
i = imaplib.IMAP4("127.0.0.1", s.server_address[1])
print(i.capabilities, i.login("u", "p"), i.select("INBOX"), i.search(None, "ALL"))
print(i.fetch("1", "(RFC822)"))
print(i.logout())
print(imaplib.ParseFlags(b'1 (FLAGS (\\Seen \\Answered))'), imaplib.Time2Internaldate(0).startswith('"01-Jan-1970'))
print(imaplib.Int2AP(1234))
s.shutdown()

# FTP (control connection only)
def ftp_respond(line, h):
    cmd = line.split(b" ")[0].upper()
    replies = {b"USER": b"331 need pass\r\n", b"PASS": b"230 in\r\n", b"PWD": b'257 "/home/x" is cwd\r\n', b"CWD": b"250 ok\r\n",
               b"SIZE": b"213 1234\r\n", b"MKD": b'257 "/home/x/new" created\r\n', b"TYPE": b"200 ok\r\n", b"NOOP": b"200 ok\r\n"}
    if cmd == b"QUIT":
        return b"221 bye\r\n", True
    return replies.get(cmd, b"502 no\r\n"), False
s = serve(b"220 FTP ready\r\n", ftp_respond)
f = ftplib.FTP()
print(f.connect("127.0.0.1", s.server_address[1]), f.login("u", "p"), f.pwd(), f.cwd("/tmp"), f.size("a"), f.mkd("new"), f.voidcmd("NOOP"))
try:
    f.sendcmd("BOGUS")
except ftplib.error_perm as e:
    print("error_perm", e)
print(f.quit())
print(ftplib.parse227("227 Entering Passive Mode (127,0,0,1,4,1)"), ftplib.parse257('257 "/a ""b"""'))
s.shutdown()

# mailbox
d = tempfile.mkdtemp()
mb = mailbox.mbox(os.path.join(d, "box"))
for n in range(3):
    m = mailbox.mboxMessage()
    m["Subject"] = "msg %d" % n
    m["From"] = "x@y"
    m.set_payload("From the start\nbody %d\n" % n)
    m.set_flags("RO" if n else "")
    mb.add(m)
mb.flush()
print(len(mb), [mb[k]["Subject"] for k in sorted(mb.keys())], [mb[k].get_flags() for k in sorted(mb.keys())])
mb.remove(sorted(mb.keys())[1])
mb.flush()
mb.close()
mb = mailbox.mbox(os.path.join(d, "box"))
print([m["Subject"] for m in mb], [m.get_payload() for m in mb])
mb.close()
with open(os.path.join(d, "box")) as fh:
    print(fh.read().count(">From the start"))
md = mailbox.Maildir(os.path.join(d, "md"))
k = md.add("Subject: m\n\nhello")
md.add_folder("sub")
msg = md[k]
msg.set_subdir("cur")
msg.add_flag("S")
md[k] = msg
print(len(md), md.list_folders(), md[k].get_flags(), md[k].get_subdir(), md.get_file(k).read().replace(b"\r\n", b"\n"))
mh = mailbox.MH(os.path.join(d, "mh"))
k = mh.add("Subject: mh\n\nbody")
mh.set_sequences({"unseen": [k]})
print(mh.keys(), mh.get_sequences(), mh[k]["Subject"])
shutil.rmtree(d)

# wsgiref
def app(environ, start_response):
    start_response("200 OK", [("Content-Type", "text/plain; charset=utf-8")])
    return [("path=%s method=%s q=%s url=%s" % (environ["PATH_INFO"], environ["REQUEST_METHOD"], environ.get("QUERY_STRING"),
        wsgiref.util.request_uri(environ, include_query=False))).encode()]
srv = wsgiref.simple_server.make_server("127.0.0.1", 0, wsgiref.validate.validator(app), handler_class=type("Q", (wsgiref.simple_server.WSGIRequestHandler,), {"log_message": lambda *a: None}))
threading.Thread(target=srv.serve_forever, daemon=True).start()
print(urllib.request.urlopen("http://127.0.0.1:%d/a/b?x=1" % srv.server_port).read().replace(b":%d/" % srv.server_port, b":PORT/"))
srv.shutdown()
env = {}
wsgiref.util.setup_testing_defaults(env)
print(sorted(k for k in env if not k.startswith("wsgi.")), wsgiref.util.application_uri(env), wsgiref.util.guess_scheme(env))
env["PATH_INFO"] = "/a/b/c"; env["SCRIPT_NAME"] = ""
print(wsgiref.util.shift_path_info(env), env["SCRIPT_NAME"], env["PATH_INFO"], wsgiref.util.is_hop_by_hop("Connection"))
h = wsgiref.headers.Headers([("Content-Type", "text/html")])
h.add_header("Content-Disposition", "attachment", filename="a.txt")
h["X-A"] = "1"
print(str(h).replace("\r\n", "|"), h.get_all("x-a"), len(h))
out = io.BytesIO()
handler = wsgiref.handlers.SimpleHandler(io.BytesIO(), out, sys.stderr, dict(env, REQUEST_METHOD="GET"), multithread=False)
handler.run(app)
print(out.getvalue().split(b"\r\n")[0], out.getvalue().endswith(b"url=http://127.0.0.1/a"))

# nturl2path
print(nturl2path.pathname2url(r"C:\Program Files\a.txt"), nturl2path.url2pathname("///C:/Program%20Files/a.txt"))

# code, codeop, rlcompleter
print(codeop.compile_command("x = 1") is not None, codeop.compile_command("if x:") is None)
try:
    codeop.compile_command("x = = 1")
except SyntaxError as e:
    print("SyntaxError", e.msg)
interp = code.InteractiveInterpreter({"y": 2})
print(interp.runsource("z = y * 21"), interp.locals["z"], interp.runsource("for i in range(2):"))
console = code.InteractiveConsole({})
print(console.push("a = 5"), console.push("def f():"), console.push("    return a + 1"), console.push(""), console.push("print(f())"))
comp = rlcompleter.Completer({"alpha": 1, "alpine": 2, "beta": 3})
print([comp.complete("al", i) for i in range(3)], comp.complete("beta.re", 0))

# 'single' mode: an expression statement's value is shown (sys.displayhook, builtins._)
interp = code.InteractiveInterpreter({})
for src in ["1 + 1", "x = 3", "x * 2", "None", "'s'", "[x, x]", "x = 1\ny = 2", "_"]:
    try:
        exec(compile(src, "<in>", "single"), interp.locals)
    except SyntaxError as e:
        print("SyntaxError", e.msg)
console = code.InteractiveConsole()
for line in ["a = 10", "a + 5", "def g(n):", "    return n * 2", "", "g(a)"]:
    console.push(line)
for src in ["if x: y=1", "if x:\n  y=1\n", "@dec", "a = (", "x = '''a\nb", "for i in x:\n  pass\nelse:", "try:\n  pass", "1 + 1",
            "if x:\n  y=1\nz=2"]:
    try:
        r = codeop.compile_command(src)
        print(repr(src), "->", "None" if r is None else "code")
    except SyntaxError as e:
        print(repr(src), "->", type(e).__name__, e.msg)
print("done")
