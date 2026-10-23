import email, email.utils, email.policy
from email.message import EmailMessage, Message
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from email.mime.application import MIMEApplication
from email.header import Header, decode_header, make_header
from email.parser import Parser, BytesParser
from email import encoders
msg = EmailMessage()
msg["Subject"] = "Héllo wörld"
msg["From"] = "Alice <alice@example.com>"
msg["To"] = "bob@example.com, Carol <carol@example.org>"
msg["Date"] = email.utils.format_datetime(email.utils.parsedate_to_datetime("Mon, 20 Nov 1995 19:12:08 -0500"))
msg["Message-ID"] = "<id1@example.com>"
msg.set_content("Plain body\nline 2\n")
msg.add_alternative("<p>HTML</p>", subtype="html")
msg.add_attachment(b"\x00\x01binary", maintype="application", subtype="octet-stream", filename="a.bin")
s = msg.as_string()
print(s.count("\n"), msg.is_multipart(), msg.get_content_type(), [p.get_content_type() for p in msg.walk()])
m2 = email.message_from_string(s, policy=email.policy.default)
print(m2["subject"], m2["to"].addresses[1].display_name, m2["from"].addresses[0].username, m2["date"].datetime.year)
print(m2.get_body(preferencelist=("plain",)).get_content(), [a.get_filename() for a in m2.iter_attachments()], m2.get_body(("html",)).get_content().strip())
print(email.utils.parseaddr("Name <x@y.z>"), email.utils.formataddr(("Ann", "a@b.c")), email.utils.getaddresses(["a@b, C <c@d>"]))
print(email.utils.quote('a"b'), email.utils.unquote('"x"'), email.utils.parsedate("Mon, 20 Nov 1995 19:12:08 -0500")[:6])
print(decode_header("=?utf-8?q?caf=C3=A9?= ok"), str(make_header(decode_header("=?utf-8?b?w6k=?="))), Header("ü", "utf-8").encode())
old = Message(); old["X"] = "1"; old.set_payload("hi"); old.set_charset("utf-8"); print(old.as_string().splitlines())
mm = MIMEMultipart(); mm["Subject"] = "mp"; mm.attach(MIMEText("t", "plain")); ap = MIMEApplication(b"data"); mm.attach(ap)
mm.set_boundary("BOUNDARY"); print(mm.as_string().splitlines()[:12])
raw = b"From: a@b\r\nSubject: =?iso-8859-1?q?r=E9sum=E9?=\r\nContent-Type: text/plain; charset=latin-1\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\nh=E9llo\r\n"
bm = BytesParser(policy=email.policy.default).parsebytes(raw); print(bm["subject"], repr(bm.get_content()), bm.get_content_charset())
p = Parser().parsestr("A: 1\nB: 2\n\nbody"); print(p.keys(), p.get_payload(), p.get_all("A"), p.items())
print(email.utils.make_msgid(domain="x.y").endswith("@x.y>"), email.utils.localtime().tzinfo is not None, email.utils.encode_rfc2231("x y", "utf-8"))
import io
from email.generator import Generator, BytesGenerator
from email import iterators, errors, headerregistry
m = EmailMessage(policy=email.policy.SMTP)
m["Subject"] = "A very long subject line " * 6
m["To"] = ", ".join("person%d <p%d@example.com>" % (i, i) for i in range(6))
m.set_content("x" * 100 + "\n" + "é" * 10 + "\n")
out = io.StringIO(); Generator(out, maxheaderlen=60).flatten(m); s = out.getvalue()
print(max(len(l) for l in s.split("\r\n")[:10]), s.count("\r\n"), s.split("\r\n")[0:3])
b = io.BytesIO(); BytesGenerator(b).flatten(m); print(len(b.getvalue()) > 100, b.getvalue()[:20])
print(email.utils.formatdate(0, usegmt=True), email.utils.formatdate(86400 * 365, localtime=False))
raw = "MIME-Version: 1.0\nContent-Type: multipart/mixed; boundary=XX\n\n--XX\nContent-Type: text/plain\n\none\n--XX\nContent-Type: text/html\n\n<b>two</b>\n--XX--\n"
mm = email.message_from_string(raw)
print([p.get_content_type() for p in mm.walk()], [p.get_payload() for p in iterators.typed_subpart_iterator(mm, "text", "html")])
print(list(iterators.body_line_iterator(mm)))
out = io.StringIO(); iterators._structure(mm, out); print(out.getvalue())
try:
    email.message_from_string("Subject: x\n\nb", policy=email.policy.strict).__setitem__("To", "bad <<")
except Exception as e: print(type(e).__name__)
h = headerregistry.HeaderRegistry()("To", "Ann <a@b.c>, Bob <bob@x>"); print(h, [a.addr_spec for a in h.addresses], type(h).__name__)
a = headerregistry.Address("Ann Q", "ann", "example.com"); print(a, repr(a), a.domain)
g = headerregistry.Group("grp", [a]); print(g)
msg = Message(); msg["Content-Type"] = 'text/plain; charset="utf-8"; format=flowed'
print(msg.get_params(), msg.get_param("format"), msg.get_content_charset()); msg.set_param("charset", "latin-1"); print(msg["Content-Type"])
msg.del_param("format"); print(msg["Content-Type"]); msg.replace_header("Content-Type", "text/html"); print(msg.get_content_type())
print(email.message_from_string("Subject: =?utf-8?q?=E2=9C=93?=\n\nx", policy=email.policy.default)["Subject"])
print(email.utils.collapse_rfc2231_value(("utf-8", "", "caf%C3%A9")), email.utils.decode_rfc2231("utf-8''x%20y"))
