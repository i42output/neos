import zipfile, io, os, tempfile, warnings, shutil
d = tempfile.mkdtemp()
src = os.path.join(d, "src.txt"); open(src, "w").write("source file\n" * 10)
p = os.path.join(d, "a.zip")
with zipfile.ZipFile(p, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    z.comment = b"the comment"
    z.write(src, "s/src.txt")
    with z.open("streamed.txt", "w") as f:
        f.write(b"streamed data\n" * 3)
    z.mkdir("emptydir")
    i = zipfile.ZipInfo.from_file(src, "again.txt")
    print(i.filename, i.file_size, i.is_dir())
with zipfile.ZipFile(p) as z:
    print(z.comment, [(i.filename, i.is_dir(), i.compress_type) for i in z.infolist()])
    print(repr(z).replace(d, "<dir>"), repr(z.getinfo("s/src.txt")).split(" ")[:3])
    print(z.read("streamed.txt"))
    with z.open("s/src.txt") as f:
        print(f.readline(), f.read(5), f.seekable(), f.tell()); f.seek(0); print(f.read(6))
    print(z.extract("s/src.txt", os.path.join(d, "x")).endswith(os.path.join("x", "s", "src.txt")))
    try: z.getinfo("missing")
    except KeyError as e: print("KeyError", e)
    print(z.printdir() is None)
zp = zipfile.Path(p)
print(sorted(str(x).replace(d, "<dir>") for x in zp.glob("**/*.txt")), zp.joinpath("s").is_dir(), (zp / "s" / "src.txt").exists(), zp.name)
print((zp / "s/src.txt").read_bytes()[:6], (zp / "s/src.txt").suffix, (zp / "s/src.txt").stem)
buf = io.BytesIO()
with zipfile.ZipFile(buf, "w") as z:
    z.writestr("dup", "1")
    with warnings.catch_warnings(record=True) as w:
        warnings.simplefilter("always")
        z.writestr("dup", "2")
        print([str(x.message) for x in w])
with zipfile.ZipFile(buf, "a") as z:
    try: z.open("nope", "x")
    except ValueError as e: print("ValueError", e)
print(zipfile.ZipFile(buf).namelist())
try: zipfile.ZipFile(buf, "q")
except ValueError as e: print(e)
z = zipfile.ZipFile(io.BytesIO(), "w"); z.close(); 
try: z.writestr("a", "b")
except ValueError as e: print(e)
print(zipfile.ZIP_STORED, zipfile.ZIP_BZIP2, zipfile.ZIP_LZMA, zipfile.ZipFile.__mro__[1:])
shutil.rmtree(d)
