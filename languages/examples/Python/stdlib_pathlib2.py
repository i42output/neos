from pathlib import Path, PurePosixPath, PurePath
import os
class MyPath(type(Path())):
    def shout(self):
        return str(self).upper()
m = MyPath('a', 'b')
print(m, repr(m), m.shout(), type(m / 'c').__name__, type(m.parent).__name__, isinstance(m, Path))
print(Path(Path('a'), 'b', PurePosixPath('c')), os.path.join(Path('x'), 'y'), os.listdir(Path('.')) == os.listdir('.'), Path('a/b').parts, {Path('a'): 1}[Path('a')])
print(Path('setup.py').with_suffix('.cfg').name, [p.name for p in Path('/a/b/c').parents], Path('a') in [Path('a')], str(Path.cwd()) == os.getcwd(), PurePath('a/b') == PurePosixPath('a/b'))
p = Path('x.txt')
print(f'{p}', '%s' % p, p.stem, p.suffix, str(p), p.exists(), Path('/').is_dir(), Path('/').parent, Path('/').parts)
