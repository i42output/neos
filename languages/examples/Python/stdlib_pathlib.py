import os
import pathlib
from pathlib import Path, PurePath, PurePosixPath, PureWindowsPath

# pure paths
p = PurePosixPath('/usr/local/lib/python3.13/site-packages/pkg.tar.gz')
print(p, repr(p), p.parts, p.drive, p.root, p.anchor, p.name, p.suffix, p.suffixes, p.stem)
print(p.parent, p.parent.parent, list(p.parents)[:3], len(p.parents), p.parents[-1], p.is_absolute(), p.as_posix(), p.as_uri())
print(PurePosixPath('a/b/../c//d/./e'), PurePosixPath(''), PurePosixPath('.'), PurePosixPath('//x/y'), PurePosixPath('///x'), PurePosixPath('a', '/b', 'c'), PurePosixPath('a', 'b', 'c'))
q = PurePosixPath('docs')
print(q / 'readme.md', 'root' / q, q / PurePosixPath('x/y'), q.joinpath('a', 'b'), q.with_name('src'), (q / 'a.txt').with_suffix('.md'), (q / 'a.txt').with_stem('b'), (q / 'a.tar.gz').with_suffix(''))
print(PurePosixPath('/a/b/c').relative_to('/a'), PurePosixPath('/a/b/c').relative_to(PurePosixPath('/a/b/c')), PurePosixPath('/a/b').relative_to('/a/c', walk_up=True), PurePosixPath('a/b').is_relative_to('a'), PurePosixPath('a/b').is_relative_to('b'))
print(PurePosixPath('a/b.py').match('*.py'), PurePosixPath('/a/b/c.py').match('b/*.py'), PurePosixPath('/a/b/c.py').match('/*.py'), PurePosixPath('a/b/c.py').full_match('a/**'), PurePosixPath('a/b/c.py').full_match('**/*.py'), PurePosixPath('a/b').match('A/B'))
print(PurePosixPath('a') == PurePosixPath('a'), PurePosixPath('a') == PurePosixPath('b'), PurePosixPath('a') == 'a', PurePosixPath('a') < PurePosixPath('b'), sorted([PurePosixPath('b'), PurePosixPath('a/c'), PurePosixPath('a')]), hash(PurePosixPath('x')) == hash(PurePosixPath('x')))
print(PurePosixPath('.hidden').suffix, PurePosixPath('.hidden').stem, PurePosixPath('a.').suffix, PurePosixPath('a.').suffixes, PurePosixPath('..').name, PurePosixPath('/').name, PurePosixPath('/').parent, PurePosixPath('x.tar.gz').suffixes, PurePosixPath('.a.b').suffixes)
w = PureWindowsPath('C:/Users/Me\\Documents/file.TXT')
print(w, repr(w), w.parts, w.drive, w.root, w.anchor, w.name, w.suffix, w.parent, w.as_posix(), w.is_absolute(), w.as_uri())
print(PureWindowsPath('//server/share/dir/f'), PureWindowsPath('//server/share/dir/f').drive, PureWindowsPath('c:a/b'), PureWindowsPath('c:a/b').is_absolute(), PureWindowsPath('a') == PureWindowsPath('A'), PureWindowsPath('C:/x').relative_to('c:/'))
print(str(PurePath('x')), type(PurePath('x')).__name__, isinstance(PurePosixPath('x'), PurePath), isinstance(Path('x'), PurePath), type(Path('x')).__name__, isinstance(Path('x'), PurePosixPath))
print(os.fspath(PurePosixPath('a/b')), bytes(PurePosixPath('a/b')), Path('~').expanduser() == Path.home(), Path('a').absolute() == Path.cwd() / 'a', Path('/x/y').absolute())
for f in [lambda: PurePosixPath('/a/b').relative_to('/c'), lambda: PurePosixPath('a').relative_to('/a'), lambda: PurePosixPath('').with_name('x'), lambda: PurePosixPath('a').with_name('b/c'),
          lambda: PurePosixPath('a').with_suffix('txt'), lambda: PurePosixPath('a').as_uri(), lambda: PurePosixPath(5), lambda: PurePosixPath('a') < PureWindowsPath('a'), lambda: PurePosixPath('a').match('')]:
    try:
        print(f())
    except Exception as e:
        print(type(e).__name__, e)

# concrete paths
base = Path('pathlib_test_dir')
if base.exists():
    for p in sorted(base.rglob('*'), reverse=True):
        p.rmdir() if p.is_dir() else p.unlink()
    base.rmdir()
base.mkdir()
print(base.exists(), base.is_dir(), base.is_file(), base.is_symlink())
(base / 'sub' / 'deeper').mkdir(parents=True)
(base / 'sub').mkdir(exist_ok=True)
n = (base / 'notes.txt').write_text('hello\nworld\n')
print(n, (base / 'notes.txt').read_text(), (base / 'notes.txt').read_text().splitlines(), (base / 'notes.txt').stat().st_size)
print((base / 'data.bin').write_bytes(b'\x00\x01\x02'), (base / 'data.bin').read_bytes())
(base / 'sub' / 'a.py').write_text('print(1)\n')
(base / 'sub' / 'deeper' / 'b.py').write_text('print(2)\n')
(base / 'sub' / 'deeper' / 'c.txt').touch()
(base / '.hidden').touch()
print(sorted(base.iterdir()), sorted(x.name for x in base.iterdir()))
print(sorted(base.glob('*.txt')), sorted(base.glob('**/*.py')), sorted(base.rglob('*.py')), sorted(base.glob('sub/*')), sorted(base.glob('*/')), sorted(base.glob('**')))
print(sorted(base.glob('s?b/deeper/[bc].*')), sorted(base.rglob('*.TXT', case_sensitive=False)), list(base.glob('nothing*')), sorted(base.glob('.*')))
with (base / 'notes.txt').open() as f:
    print(f.readline().strip())
with open(base / 'notes.txt', 'a') as f:
    f.write('more\n')
print((base / 'notes.txt').read_text().count('\n'), (base / 'notes.txt').samefile(str(base / 'notes.txt')))
r = (base / 'notes.txt').rename(base / 'renamed.txt')
print(r, r.exists(), (base / 'notes.txt').exists(), (base / 'renamed.txt').replace(base / 'final.txt'))
for root, dirs, files in base.walk():
    print(root, sorted(dirs), sorted(files))
print(Path('pathlib_test_dir/sub/../sub/a.py').resolve() == (Path.cwd() / 'pathlib_test_dir/sub/a.py'), (base / 'sub').resolve().is_absolute())
for f in [lambda: base.mkdir(), lambda: (base / 'missing').rmdir(), lambda: (base / 'missing.txt').read_text(), lambda: (base / 'x' / 'y').mkdir(), lambda: (base / 'final.txt').write_text(5),
          lambda: (base / 'final.txt').touch(exist_ok=False), lambda: (base / 'missing.txt').unlink(), lambda: (base / 'missing.txt').unlink(missing_ok=True), lambda: (base / 'missing').stat()]:
    try:
        print(f())
    except Exception as e:
        print(type(e).__name__, e)
for p in sorted(base.rglob('*'), reverse=True):
    p.rmdir() if p.is_dir() else p.unlink()
base.rmdir()
print(base.exists())
