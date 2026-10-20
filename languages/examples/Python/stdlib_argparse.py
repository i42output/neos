# argparse: a command-line interface with options, positionals, sub-commands, groups, help and errors
import argparse
import os
import sys

os.environ['COLUMNS'] = '100'   # (help is wrapped to the terminal width; fix it so the output is the same everywhere)
sys.stderr = sys.stdout         # (usage errors go to stderr; keep them in order with everything else)

def port(text):
    value = int(text)
    if not 0 < value < 65536:
        raise argparse.ArgumentTypeError(f'{value} is not a port number')
    return value

parser = argparse.ArgumentParser(prog='notes', description='Keep notes from the command line.', epilog='Notes are kept in memory only.')
parser.add_argument('-v', '--verbose', action='count', default=0, help='say more (repeatable)')
parser.add_argument('--version', action='version', version='%(prog)s 1.0')
output = parser.add_mutually_exclusive_group()
output.add_argument('--json', action='store_true', help='print JSON')
output.add_argument('--csv', action='store_true', help='print CSV')
server = parser.add_argument_group('server', 'where notes are synchronised')
server.add_argument('--host', default='localhost', metavar='NAME')
server.add_argument('--port', type=port, default=8080)
commands = parser.add_subparsers(dest='command', required=True, title='commands', metavar='COMMAND')

add = commands.add_parser('add', help='add a note', aliases=['a'])
add.add_argument('text', nargs='+', help='the words of the note')
add.add_argument('-t', '--tag', action='append', default=[], dest='tags', help='a tag (repeatable)')
add.add_argument('--priority', choices=['low', 'normal', 'high'], default='normal')

listing = commands.add_parser('list', help='list notes')
listing.add_argument('--limit', type=int, default=10)
listing.add_argument('--since', metavar='DATE')
listing.add_argument('--reverse', action=argparse.BooleanOptionalAction, default=False)

remove = commands.add_parser('remove', help='remove notes')
remove.add_argument('ids', nargs='*', type=int, metavar='ID')
remove.add_argument('--all', action='store_true')
remove.set_defaults(confirm=True)

def show(argv):
    print('$ notes', ' '.join(argv))
    try:
        namespace = parser.parse_args(argv)
    except SystemExit as e:
        print('  exit status', e.code)
        return
    print(' ', ', '.join(f'{key}={value!r}' for key, value in sorted(vars(namespace).items())))

show(['add', 'buy', 'milk', '-t', 'shopping', '-t', 'home', '--priority', 'high'])
show(['-vv', '--json', 'a', 'call', 'the', 'garage'])
show(['--host', 'example.org', '--port', '9000', 'list', '--limit', '3', '--reverse'])
show(['list', '--no-reverse', '--since=2026-10-01'])
show(['remove', '3', '5', '8'])
show(['remove', '--all'])
show(['--', 'add', '--not-an-option'][1:])

# errors: each prints the usage and a message, then exits with status 2
show([])
show(['add'])
show(['add', 'x', '--priority', 'urgent'])
show(['list', '--limit', 'ten'])
show(['--port', '70000', 'list'])
show(['--json', '--csv', 'list'])
show(['frobnicate'])
show(['list', '--unknown'])

# --version and help exit with status 0
show(['--version'])
print(parser.format_help())
print(add.format_help())
print(parser.format_usage(), end='')

# parse_known_args() leaves what it doesn't recognise
namespace, extra = parser.parse_known_args(['list', '--limit', '2', '--colour', 'blue'])
print('known:', namespace.command, namespace.limit, 'extra:', extra)

# a parser that raises instead of exiting
strict = argparse.ArgumentParser(prog='strict', exit_on_error=False)
strict.add_argument('--count', type=int)
try:
    strict.parse_args(['--count', 'many'])
except argparse.ArgumentError as e:
    print('ArgumentError:', e, '| argument:', e.argument_name)

# defaults, Namespace and prefix matching
print(argparse.Namespace(a=1, b='two'), argparse.Namespace(a=1) == argparse.Namespace(a=1), 'a' in argparse.Namespace(a=1))
prefix = argparse.ArgumentParser(prog='prefix')
prefix.add_argument('--verbosity', type=int, default=1)
prefix.add_argument('--name', default=argparse.SUPPRESS)
print(prefix.parse_args(['--verb', '3']), prefix.parse_args([]), prefix.get_default('verbosity'))
