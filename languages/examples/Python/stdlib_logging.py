# logging: loggers in a hierarchy, handlers, formatters, filters, adapters and custom levels
import io
import logging
import sys

# basicConfig() sets up the root logger (here on stdout, so the output stays in order)
logging.basicConfig(stream=sys.stdout, level=logging.INFO, format='%(levelname)s:%(name)s:%(message)s')
logging.info('configured the root logger')
logging.debug('debug messages are below the root level and are not shown')
logging.warning('%d %s', 3, 'warnings')

# loggers form a hierarchy by name and pass records up to their parents' handlers
app = logging.getLogger('app')
db = logging.getLogger('app.db')
print('hierarchy:', db.parent is app, app.parent is logging.root, db.name, db.getEffectiveLevel(), logging.getLevelName(db.getEffectiveLevel()))
db.info('connected to %s', 'inventory')
print('same object:', logging.getLogger('app.db') is db, app.getChild('db') is db)

# a handler of its own, with a formatter showing where a record came from
captured = io.StringIO()
handler = logging.StreamHandler(captured)
handler.setFormatter(logging.Formatter('[%(levelname)-8s] %(name)s.%(funcName)s:%(lineno)d %(message)s'))
handler.setLevel(logging.DEBUG)
db.addHandler(handler)
db.setLevel(logging.DEBUG)
db.propagate = False

def query(table):
    db.debug('select * from %s', table)
    db.info('%(rows)d rows from %(table)s', {'rows': 42, 'table': table})

query('parts')
print(captured.getvalue(), end='')

# exceptions: exception() adds the traceback
captured.seek(0)
captured.truncate()
try:
    {}['missing']
except KeyError:
    db.exception('lookup failed')
lines = captured.getvalue().splitlines()
print(lines[0], '|', lines[1], '|', lines[-1])

# the three formatting styles
record = logging.makeLogRecord({'name': 'demo', 'levelno': logging.ERROR, 'levelname': 'ERROR', 'msg': 'disk %s is %d%% full', 'args': ('C:', 97)})
for formatter in [logging.Formatter('%(levelname)s %(name)s: %(message)s'), logging.Formatter('{levelname} {name}: {message}', style='{'),
                  logging.Formatter('$levelname $name: $message', style='$')]:
    print('style', formatter._style.__class__.__name__ + ':', formatter.format(record))
try:
    logging.Formatter('%(levelname', validate=True)
except ValueError as e:
    print('ValueError:', e)

# filters: objects with a filter() method, plain callables, and name-based filters
class NoSecrets(logging.Filter):
    def filter(self, record):
        return 'password' not in record.getMessage()

audit = logging.getLogger('audit')
audit.propagate = False
audit_out = io.StringIO()
audit_handler = logging.StreamHandler(audit_out)
audit_handler.setFormatter(logging.Formatter('%(message)s'))
audit.addHandler(audit_handler)
audit.addFilter(NoSecrets())
audit_handler.addFilter(lambda record: record.levelno >= logging.WARNING)
audit.warning('user bob logged in')
audit.warning('password for bob is hunter2')
audit.info('below the handler filter')
audit.error('user bob locked out')
print('filtered:', audit_out.getvalue().splitlines())
name_filter = logging.Filter('app.db')
print('name filter:', [name_filter.filter(logging.makeLogRecord({'name': n})) for n in ['app.db', 'app.db.pool', 'app.dbx', 'app']])

# adapters add context to every record
class RequestAdapter(logging.LoggerAdapter):
    def process(self, msg, kwargs):
        kwargs['extra'] = self.extra
        return '[request %s] %s' % (self.extra['request'], msg), kwargs

web = logging.getLogger('web')
web.propagate = False
web_out = io.StringIO()
web_handler = logging.StreamHandler(web_out)
web_handler.setFormatter(logging.Formatter('%(user)s %(message)s'))
web.addHandler(web_handler)
web.setLevel(logging.INFO)
plain = logging.LoggerAdapter(web, {'user': 'alice'})
plain.info('viewed the dashboard')
request = RequestAdapter(web, {'request': 17, 'user': 'carol'})
request.warning('slow response')
print('adapters:', web_out.getvalue().splitlines())

# a custom handler that keeps records, and a custom level
class ListHandler(logging.Handler):
    def __init__(self):
        super().__init__()
        self.records = []
    def emit(self, record):
        self.records.append((record.levelname, self.format(record)))

NOTICE = 25
logging.addLevelName(NOTICE, 'NOTICE')
jobs = logging.getLogger('jobs')
jobs.propagate = False
keeper = ListHandler()
jobs.addHandler(keeper)
jobs.setLevel('INFO')
jobs.log(NOTICE, 'nightly job %s finished', 'backup')
jobs.info('cleaned %d temporary files', 12)
jobs.debug('not kept')
print('kept:', keeper.records)
print('levels:', logging.getLevelName(NOTICE), logging.getLevelName('NOTICE'), logging.getLevelName(logging.CRITICAL), logging.getLevelName(7))
print('enabled:', jobs.isEnabledFor(logging.DEBUG), jobs.isEnabledFor(NOTICE), jobs.level, jobs.hasHandlers())

# disable() switches off everything at or below a level
logging.disable(logging.WARNING)
jobs.warning('suppressed')
jobs.error('still shown')
logging.disable(logging.NOTSET)
print('after disable:', [message for level, message in keeper.records[2:]])

# removing handlers and bad levels
jobs.removeHandler(keeper)
print('handlers:', jobs.handlers, jobs.hasHandlers())
for bad in ['LOUD', 1.5]:
    try:
        jobs.setLevel(bad)
    except (ValueError, TypeError) as e:
        print(type(e).__name__ + ':', e)
print(repr(app), repr(handler), repr(logging.root))
logging.shutdown()
