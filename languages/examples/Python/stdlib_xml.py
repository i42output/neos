# The standard library's XML: pyexpat (Expat's parser: its handlers, buffering, namespaces, errors, encodings and external entities),
# xml.etree.ElementTree, xml.dom.minidom and pulldom, xml.sax, plistlib, and xmlrpc (a client and a server on the loopback interface)
import pyexpat, io
from pyexpat import errors, model
print(pyexpat.EXPAT_VERSION.startswith("expat_"), len(pyexpat.version_info), pyexpat.native_encoding, type(pyexpat.features), pyexpat.XML_PARAM_ENTITY_PARSING_ALWAYS)
print(errors.XML_ERROR_SYNTAX, errors.codes["mismatched tag"], errors.messages[7], model.XML_CTYPE_MIXED, model.XML_CQUANT_PLUS)
print(pyexpat.ErrorString(3), pyexpat.ExpatError is pyexpat.error, pyexpat.ExpatError.__module__, pyexpat.ExpatError.__name__)

events = []
p = pyexpat.ParserCreate()
p.StartElementHandler = lambda name, attrs: events.append(("start", name, attrs))
p.EndElementHandler = lambda name: events.append(("end", name))
p.CharacterDataHandler = lambda data: events.append(("data", data))
p.CommentHandler = lambda data: events.append(("comment", data))
p.ProcessingInstructionHandler = lambda t, d: events.append(("pi", t, d))
p.XmlDeclHandler = lambda v, e, s: events.append(("xmldecl", v, e, s))
p.StartDoctypeDeclHandler = lambda *a: events.append(("doctype",) + a)
p.EndDoctypeDeclHandler = lambda: events.append(("enddoctype",))
p.EntityDeclHandler = lambda *a: events.append(("entity",) + a)
p.ElementDeclHandler = lambda *a: events.append(("elementdecl",) + a)
p.AttlistDeclHandler = lambda *a: events.append(("attlist",) + a)
p.StartCdataSectionHandler = lambda: events.append(("cdata",))
p.EndCdataSectionHandler = lambda: events.append(("endcdata",))
doc = b"""<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE root [
  <!ELEMENT root (item+)>
  <!ELEMENT item (#PCDATA)>
  <!ATTLIST item id CDATA #REQUIRED kind (a|b) "a">
  <!ENTITY who "world">
]>
<root>
  <!-- a comment -->
  <?target some data?>
  <item id="1">hello &who; &amp; &#65;&#x42;</item>
  <item id="2" kind="b"><![CDATA[<raw> & stuff]]></item>
  <item id="3">caf\xc3\xa9 \xe2\x82\xac</item>
</root>"""
print(p.Parse(doc, True))
for e in events:
    print(e)
print(p.CurrentLineNumber, p.ErrorCode, p.buffer_text, p.buffer_size, p.ordered_attributes, p.intern is not None, len(p.intern) > 0)

# buffer_text, ordered_attributes, specified_attributes, incremental parsing
out = []
p = pyexpat.ParserCreate()
p.buffer_text = True
p.ordered_attributes = True
p.specified_attributes = True
p.StartElementHandler = lambda n, a: out.append((n, a))
p.CharacterDataHandler = out.append
for chunk in [b"<a x='1' y='2'>par", b"tial te", b"xt &amp; more</a>"]:
    p.Parse(chunk)
p.Parse(b"", True)
print(out, p.buffer_used)

# namespaces
ns = []
p = pyexpat.ParserCreate(namespace_separator="}")
p.StartNamespaceDeclHandler = lambda pre, uri: ns.append(("ns", pre, uri))
p.EndNamespaceDeclHandler = lambda pre: ns.append(("endns", pre))
p.StartElementHandler = lambda n, a: ns.append((n, a))
p.Parse('<r xmlns="http://d/" xmlns:x="http://x/"><x:e x:a="1" b="2"/></r>', True)
print(ns)
p = pyexpat.ParserCreate(namespace_separator=" ")
p.namespace_prefixes = True
p.StartElementHandler = lambda n, a: print("triplet", n, a)
p.Parse('<x:e xmlns:x="http://x/" x:a="1"/>', True)

# errors
for bad in [b"<a><b></a>", b"<a>", b"<a/><b/>", b"<a x='1' x='2'/>", b"<a>&undefined;</a>", b"", b"<a>\xff</a>", b"<x:a/>"]:
    p = pyexpat.ParserCreate(namespace_separator="!") if b"x:" in bad else pyexpat.ParserCreate()
    try:
        p.Parse(bad, True)
        print("ok?", bad)
    except pyexpat.ExpatError as e:
        print("ExpatError", e, e.code, e.lineno, e.offset, errors.messages[e.code], p.ErrorLineNumber, p.ErrorColumnNumber, p.ErrorByteIndex)

# an exception in a handler
p = pyexpat.ParserCreate()
def boom(name, attrs):
    raise ValueError("handler failed " + name)
p.StartElementHandler = boom
try:
    p.Parse(b"<a><b/></a>", True)
except ValueError as e:
    print("ValueError", e)

# ParseFile, encodings, str input
p = pyexpat.ParserCreate()
got = []
p.CharacterDataHandler = got.append
p.ParseFile(io.BytesIO('<?xml version="1.0" encoding="iso-8859-1"?><a>caf\xe9</a>'.encode("latin-1")))
print(got)
p = pyexpat.ParserCreate("utf-16")
got = []
p.CharacterDataHandler = got.append
p.Parse("<a>wide</a>".encode("utf-16"), True)
print(got)
p = pyexpat.ParserCreate()
got = []
p.CharacterDataHandler = got.append
p.Parse('<?xml version="1.0" encoding="latin-1"?><a>unicode €</a>', True)
print(got)
p = pyexpat.ParserCreate()
got = []
p.CharacterDataHandler = got.append
p.Parse('<?xml version="1.0" encoding="cp1252"?><a>'.encode() + b"\x80\x93" + b"</a>", True)
print(got)

# attribute errors and handlers' values
p = pyexpat.ParserCreate()
print(p.StartElementHandler, type(p).__name__, isinstance(p, pyexpat.XMLParserType))
try:
    p.nonsense = 1
except AttributeError as e:
    print("AttributeError", e)
try:
    del p.StartElementHandler
except RuntimeError as e:
    print("RuntimeError", e)
try:
    p.buffer_size = 0
except ValueError as e:
    print("ValueError", e)
try:
    pyexpat.ParserCreate(namespace_separator="ab")
except ValueError as e:
    print("ValueError", e)
try:
    pyexpat.ParserCreate(intern=[])
except TypeError as e:
    print("TypeError", e)
try:
    pyexpat.XMLParserType()
except TypeError as e:
    print("TypeError", e)
p.Parse(b"<a/>", True)
try:
    p.Parse(b"<b/>", True)
except pyexpat.ExpatError as e:
    print("ExpatError", e)
print(p.GetReparseDeferralEnabled(), p.SetParamEntityParsing(pyexpat.XML_PARAM_ENTITY_PARSING_NEVER))
p = pyexpat.ParserCreate()
p.SetBase("http://base/")
print(p.GetBase(), p.GetInputContext())
ctx = []
p.StartElementHandler = lambda n, a: ctx.append(p.GetInputContext()[:10])
p.Parse(b"<root><child/></root>", True)
print(ctx)

# external entities
p = pyexpat.ParserCreate()
p.SetParamEntityParsing(pyexpat.XML_PARAM_ENTITY_PARSING_ALWAYS)
seen = []
def ext(context, base, system_id, public_id):
    seen.append((context, base, system_id, public_id))
    sub = p.ExternalEntityParserCreate(context)
    sub.CharacterDataHandler = lambda d: seen.append(("sub", d))
    sub.Parse(b"external text", True)
    return 1
p.ExternalEntityRefHandler = ext
p.CharacterDataHandler = lambda d: seen.append(("main", d))
p.Parse(b'<!DOCTYPE r [<!ENTITY e SYSTEM "e.txt">]><r>&e;</r>', True)
print(seen)

import io, sys, datetime, threading
import xml.etree.ElementTree as ET
from xml.dom import minidom, pulldom
import xml.sax, xml.sax.handler, xml.sax.saxutils
from xml.parsers import expat
import plistlib
import xmlrpc.client, xmlrpc.server

DOC = """<?xml version="1.0"?>
<catalog xmlns:h="http://example.com/h">
  <!-- books -->
  <book id="b1" lang="en"><title>Python &amp; XML</title><price>29.99</price><h:note>first</h:note></book>
  <book id="b2"><title>Café Society</title><price>9.50</price></book>
  <magazine id="m1"><title>Monthly</title></magazine>
</catalog>"""

# --- ElementTree
root = ET.fromstring(DOC)
print(root.tag, len(root), [c.tag for c in root], root[0].attrib, root[0].get("lang"), root[1].get("lang", "?"))
print([t.text for t in root.iter("title")], root.find("book/price").text, root.findtext("magazine/title"))
print([b.get("id") for b in root.findall(".//book[@id]")], [b.get("id") for b in root.findall("book[price='9.50']")])
print(root.find("book/{http://example.com/h}note").text, [e.tag for e in root.iterfind(".//*")][:6])
new = ET.SubElement(root, "book", id="b3")
ET.SubElement(new, "title").text = "<New>"
new.set("lang", 'fr "q"')
root.remove(root.find("magazine"))
ET.indent(root, space=" ")
print(ET.tostring(root, encoding="unicode"))
print(ET.tostring(root, encoding="utf-8")[:60], ET.tostring(ET.Element("e", a="1"), xml_declaration=True, encoding="us-ascii"))
tree = ET.ElementTree(root)
buf = io.BytesIO()
tree.write(buf, encoding="utf-8", xml_declaration=True)
print(buf.getvalue()[:50])
print(ET.tostring(ET.fromstring("<a><b/>text<c>x</c>tail</a>"), method="text"))
print(ET.canonicalize("<a b='2' a='1'><c/></a>"), ET.tostring(ET.XML("<a><?pi data?><!--c--></a>")))
events = [(ev, el.tag) for ev, el in ET.iterparse(io.BytesIO(DOC.encode()), events=("start", "end"))]
print(events[:5], len(events))
pp = ET.XMLPullParser(events=("start", "end", "start-ns"))
pp.feed("<r xmlns:x='u'><x:i/>")
pp.feed("</r>")
print([(e, getattr(x, "tag", x)) for e, x in pp.read_events()])
try:
    ET.fromstring("<a><b></a>")
except ET.ParseError as e:
    print("ParseError", e, e.code, e.position)
tb = ET.TreeBuilder()
tb.start("x", {"k": "v"}); tb.data("hi"); tb.end("x")
print(ET.tostring(tb.close()))
print(ET.dump(ET.fromstring("<d><e/></d>")))
ns = {"h": "http://example.com/h"}
print(ET.fromstring(DOC).find(".//h:note", ns).text, ET.register_namespace("hh", "http://example.com/h"),
      ET.tostring(ET.fromstring(DOC).find(".//{http://example.com/h}note")))

# --- minidom
dom = minidom.parseString(DOC)
print(dom.documentElement.tagName, [n.getAttribute("id") for n in dom.getElementsByTagName("book")],
      dom.getElementsByTagName("title")[1].firstChild.data, dom.documentElement.childNodes[1].nodeType)
d2 = minidom.Document()
r = d2.createElement("root"); d2.appendChild(r)
c = d2.createElement("child"); c.setAttribute("n", "1 & 2"); r.appendChild(c)
c.appendChild(d2.createTextNode("text <here>"))
r.appendChild(d2.createComment("note"))
print(d2.toxml())
print(d2.toprettyxml(indent="  "))
print(minidom.parseString("<a><b>1</b><b>2</b></a>").documentElement.toxml(), dom.documentElement.hasAttribute("x"))
for event, node in pulldom.parseString("<x><y>1</y><y>2</y></x>"):
    if event == pulldom.START_ELEMENT:
        print("pulldom", node.tagName)

# --- SAX
class H(xml.sax.handler.ContentHandler):
    def __init__(self):
        super().__init__(); self.out = []
    def startElement(self, name, attrs):
        self.out.append(("start", name, dict(attrs)))
    def endElement(self, name):
        self.out.append(("end", name))
    def characters(self, content):
        if content.strip():
            self.out.append(("chars", content))
h = H()
xml.sax.parseString(DOC.encode(), h)
print(h.out[:8])
out = io.StringIO()
g = xml.sax.saxutils.XMLGenerator(out, encoding="utf-8")
g.startDocument(); g.startElement("a", {"x": "1"}); g.characters("<&>"); g.endElement("a"); g.endDocument()
print(out.getvalue())
print(xml.sax.saxutils.escape("a<b>&c", {'"': "&quot;"}), xml.sax.saxutils.unescape("&lt;x&gt;"), xml.sax.saxutils.quoteattr('say "hi"'))
try:
    xml.sax.parseString(b"<a><b></a>", xml.sax.handler.ContentHandler())
except xml.sax.SAXParseException as e:
    print("SAXParseException", e.getMessage(), e.getLineNumber(), e.getColumnNumber())
print(expat.ParserCreate().Parse("<x/>", True), expat.errors.XML_ERROR_TAG_MISMATCH)

# --- plistlib
data = {"name": "neos", "version": 3, "pi": 3.25, "flags": [True, False], "blob": b"\x00\x01", "when": datetime.datetime(2026, 10, 24, 1, 2, 3),
        "nested": {"a": [1, 2, {"b": "c"}]}}
x = plistlib.dumps(data)
print(x.decode()[:120])
print(plistlib.loads(x) == data, plistlib.loads(plistlib.dumps(data, fmt=plistlib.FMT_BINARY)) == data, plistlib.FMT_BINARY, plistlib.dumps([1], fmt=plistlib.FMT_BINARY)[:8])
uid = plistlib.UID(5)
print(plistlib.loads(plistlib.dumps({"u": uid}, fmt=plistlib.FMT_BINARY)), repr(uid))

# --- xmlrpc
req = xmlrpc.client.dumps((1, "two", [3.0, True], {"k": None}), "method.name", allow_none=True)
print(req)
print(xmlrpc.client.loads(req, use_builtin_types=True))
print(xmlrpc.client.dumps(xmlrpc.client.Fault(4, "bad"), methodresponse=True).replace("\n", ""))
srv = xmlrpc.server.SimpleXMLRPCServer(("127.0.0.1", 0), logRequests=False, allow_none=True)
srv.register_function(lambda a, b: a + b, "add")
srv.register_function(lambda: None, "nothing")
srv.register_introspection_functions()
srv.register_multicall_functions()
t = threading.Thread(target=srv.serve_forever, daemon=True); t.start()
proxy = xmlrpc.client.ServerProxy("http://127.0.0.1:%d/" % srv.server_address[1], allow_none=True)
print(proxy.add(2, 3), proxy.add("a", "b"), proxy.nothing(), sorted(proxy.system.listMethods()))
mc = xmlrpc.client.MultiCall(proxy)
mc.add(1, 1); mc.add([1], [2])
print(list(mc()))
try:
    proxy.missing()
except xmlrpc.client.Fault as f:
    print("Fault", f.faultCode, f.faultString)
srv.shutdown(); srv.server_close()
print(xmlrpc.client.DateTime(datetime.datetime(2026, 1, 2, 3, 4, 5)), xmlrpc.client.Binary(b"hi").data)
print("done")
