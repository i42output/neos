import gzip, io, sched, time, cmd, quopri, mimetypes, netrc, tempfile, os, shutil
data = b"hello world " * 100
c = gzip.compress(data, mtime=0); print(len(c), gzip.decompress(c) == data, c[:4])
buf = io.BytesIO()
with gzip.GzipFile(fileobj=buf, mode="wb", mtime=0, filename="x.txt") as f: f.write(b"line1\nline2\n")
buf.seek(0)
with gzip.GzipFile(fileobj=buf) as f: print(f.read(), f.name if hasattr(f, "name") else None)
d = tempfile.mkdtemp(); p = os.path.join(d, "t.gz")
with gzip.open(p, "wt", encoding="utf-8") as f: f.write("text é\n")
with gzip.open(p, "rt", encoding="utf-8") as f: print(repr(f.read()))
with gzip.open(p) as f: print(f.readline(), f.tell())
try: gzip.decompress(b"notgzip")
except gzip.BadGzipFile as e: print("bad", e)
s = sched.scheduler(time.monotonic, lambda x: None); out = []
s.enter(0.02, 2, out.append, ("b",)); s.enter(0.01, 1, out.append, ("a",)); e = s.enter(0.03, 1, out.append, ("c",)); s.cancel(e)
print(len(s.queue)); s.run(); print(out, s.empty())
class Shell(cmd.Cmd):
    prompt = "> "
    def do_greet(self, arg): print("hi", arg)
    def do_quit(self, arg): return True
    def help_greet(self): print("greets")
sh = Shell(stdin=io.StringIO("greet bob\nunknown\nhelp greet\nquit\n"), stdout=io.StringIO()); sh.use_rawinput = False
sh.cmdloop(); print(repr(sh.stdout.getvalue()))
print(sh.parseline("greet  x y"), sh.completenames("gr"))
print(quopri.encodestring(b"caf\xe9 = ok\n"), quopri.decodestring(b"caf=E9 =3D ok"), quopri.encodestring(b"a b\t\n", quotetabs=True))
print(mimetypes.guess_type("a.html"), mimetypes.guess_type("x.tar.gz"), mimetypes.guess_extension("image/png"), mimetypes.guess_type("http://x/y.json"))
print(mimetypes.types_map[".txt"], ".txt" in mimetypes.guess_all_extensions("text/plain"), mimetypes.guess_type("noext"))
n = os.path.join(d, "netrc"); open(n, "w").write("machine example.com login me password secret\ndefault login anon password x\n"); os.chmod(n, 0o600)
nr = netrc.netrc(n); print(nr.authenticators("example.com"), nr.authenticators("other"), sorted(nr.hosts))
shutil.rmtree(d)
