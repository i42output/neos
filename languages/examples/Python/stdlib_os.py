import os
import os.path
import posixpath
import ntpath
import stat
import errno

print(posixpath.join('a', 'b', 'c'), posixpath.join('/a', '/b', 'c'), posixpath.join('a/', 'b'), posixpath.join('', 'x'))
print(ntpath.join('C:\\a', 'b'), ntpath.join('C:', 'x'), ntpath.join('c:\\a', 'D:\\b'), ntpath.join('a', '\\b'), ntpath.join('\\\\srv\\share', 'x'))
for p in ['/usr/lib/file.tar.gz', 'file', '/', '//a/b', 'a/b/', '.bashrc', 'a/.b.c', '']:
    print(repr(p), posixpath.split(p), posixpath.splitext(p), posixpath.basename(p), posixpath.dirname(p), posixpath.isabs(p))
for p in ['C:\\Users\\x\\f.txt', 'C:f', '\\\\server\\share\\dir\\f', 'd/e\\f', 'c:\\', '\\\\?\\UNC\\s\\sh\\x']:
    print(repr(p), ntpath.split(p), ntpath.splitext(p), ntpath.splitdrive(p), ntpath.splitroot(p), ntpath.isabs(p))
for p in ['a/./b/../c', '/../a//b/', '//x/../y', '', '../..//a', 'a/b/../../..']:
    print(repr(p), posixpath.normpath(p), ntpath.normpath(p))
print(posixpath.splitroot('//a/b'), posixpath.splitroot('///a'), posixpath.splitroot('x/y'))
print(posixpath.relpath('/a/b/c', '/a/x'), posixpath.relpath('/a', '/a'), ntpath.relpath('C:\\a\\b', 'C:\\a\\c\\d'))
print(posixpath.commonpath(['/usr/lib', '/usr/local/lib']), posixpath.commonprefix(['interstellar', 'internet', 'interval']), ntpath.commonpath(['C:\\a\\b', 'c:/a/c']))
print(ntpath.normcase('A/B\\C'), posixpath.normcase('A/B'))
os.environ['NEOS_TEST_VAR'] = 'value'
print(posixpath.expandvars('$NEOS_TEST_VAR/x ${NEOS_TEST_VAR} $UNDEFINED_NEOS'), ntpath.expandvars('%NEOS_TEST_VAR% $NEOS_TEST_VAR'))
print(os.getenv('NEOS_TEST_VAR'), os.getenv('NEOS_UNDEFINED_VAR'), os.getenv('NEOS_UNDEFINED_VAR', 'dflt'), 'PATH' in os.environ)
print(os.sep, os.curdir, os.pardir, os.extsep, os.path is posixpath, os.name)

base = 'neos_os_test_dir'
if os.path.exists(base):
    for root, dirs, files in os.walk(base, topdown=False):
        for f in files:
            os.remove(os.path.join(root, f))
        for d in dirs:
            os.rmdir(os.path.join(root, d))
    os.rmdir(base)
os.mkdir(base)
os.makedirs(os.path.join(base, 'a', 'b'))
os.makedirs(os.path.join(base, 'a', 'b'), exist_ok=True)
try:
    os.makedirs(os.path.join(base, 'a'))
except FileExistsError as e:
    print('FileExistsError', e.errno == errno.EEXIST, e.filename)
for name in ['one.txt', 'a/two.txt', 'a/b/three.txt']:
    with open(os.path.join(base, name), 'w') as f:
        f.write(name * 2)
print(sorted(os.listdir(base)), os.path.isdir(base), os.path.isfile(base), os.path.exists(os.path.join(base, 'one.txt')))
st = os.stat(os.path.join(base, 'one.txt'))
print(st.st_size, stat.S_ISREG(st.st_mode), stat.S_ISDIR(st.st_mode), st[6], len(st), type(st.st_mtime).__name__, type(st[8]).__name__, stat.filemode(st.st_mode)[0])
print(os.path.getsize(os.path.join(base, 'a', 'two.txt')))
walked = []
for root, dirs, files in os.walk(base):
    dirs.sort()
    walked.append((root, sorted(dirs), sorted(files)))
print(walked)
print([r for r, d, f in os.walk(base, topdown=False)])
for root, dirs, files in os.walk(base):
    dirs.clear()
    print('top only', root, sorted(files))
with os.scandir(base) as it:
    entries = sorted(it, key=lambda e: e.name)
print([(e.name, e.is_dir(), e.is_file()) for e in entries], entries[0].path)
os.rename(os.path.join(base, 'one.txt'), os.path.join(base, 'uno.txt'))
os.replace(os.path.join(base, 'uno.txt'), os.path.join(base, 'a', 'two.txt'))
print(sorted(os.listdir(base)), open(os.path.join(base, 'a', 'two.txt')).read())
for bad in [lambda: os.remove(os.path.join(base, 'missing')), lambda: os.rmdir(os.path.join(base, 'a')), lambda: os.listdir('neos_missing_dir'),
            lambda: os.stat('neos_missing'), lambda: os.mkdir(base), lambda: open('neos_missing/x'), lambda: os.path.getsize('neos_missing')]:
    try:
        bad()
    except OSError as e:
        print(type(e).__name__, e.errno, e.strerror, e.filename, isinstance(e, OSError))
cwd = os.getcwd()
os.chdir(base)
print(os.path.basename(os.getcwd()), sorted(os.listdir()))
os.chdir('..')
print(os.getcwd() == cwd, os.path.abspath('x') == os.path.join(cwd, 'x'), os.path.isabs(os.path.abspath('.')))
for root, dirs, files in os.walk(base, topdown=False):
    for f in files:
        os.unlink(os.path.join(root, f))
    for d in dirs:
        os.rmdir(os.path.join(root, d))
os.rmdir(base)
print(os.path.exists(base), errno.ENOENT, errno.errorcode[errno.ENOENT], os.strerror(errno.ENOENT))
print(os.fspath('p'), len(os.urandom(8)), type(os.getpid()).__name__, os.cpu_count() > 0)
class P:
    def __fspath__(self):
        return 'from_fspath'
print(os.fspath(P()), posixpath.join(P(), 'x'))
