import os
import subprocess
import sys

# (commands that both a POSIX shell and cmd.exe understand; text mode makes their line endings "\n")
windows = os.name == "nt"
sleep = "ping -n 4 127.0.0.1 > nul" if windows else "sleep 3"

r = subprocess.run("echo hello", shell=True, capture_output=True, text=True)
print(r.returncode, repr(r.stdout), repr(r.stderr), type(r).__name__)
print(subprocess.run("exit 3", shell=True).returncode)
print(repr(subprocess.check_output("echo out", shell=True, text=True)))
r = subprocess.run("sort", shell=True, input="b\na\nc\n", capture_output=True, text=True)
print(repr(r.stdout))
try:
    subprocess.run("exit 2", shell=True, check=True)
except subprocess.CalledProcessError as e:
    print("CalledProcessError", e.returncode, e)
try:
    subprocess.run(sleep, shell=True, timeout=0.2)
except subprocess.TimeoutExpired as e:
    print("TimeoutExpired", e.timeout, e)
with subprocess.Popen("echo popen", shell=True, stdout=subprocess.PIPE, text=True) as p:
    out, err = p.communicate()
    print(repr(out), err, p.returncode)
p = subprocess.Popen(sleep, shell=True)
print(p.poll())
p.kill()
print(p.wait() != 0)
print(subprocess.call("exit 0", shell=True), subprocess.check_call("exit 0", shell=True))
print(subprocess.getstatusoutput("echo status"), subprocess.getoutput("echo output"))
r = subprocess.run("echo devnull", shell=True, stdout=subprocess.DEVNULL)
print(r.stdout, r.returncode)
r = subprocess.run("echo merged", shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
print(repr(r.stdout))
try:
    subprocess.run(["no_such_program_neos"])
except FileNotFoundError as e:
    print("FileNotFoundError", e.filename)
print(subprocess.list2cmdline(["a b", 'c"d', "e"]))
print(repr(subprocess.CompletedProcess(["x"], 0, "o", None)))
print(subprocess.PIPE, subprocess.STDOUT, subprocess.DEVNULL, issubclass(subprocess.TimeoutExpired, subprocess.SubprocessError))
try:
    subprocess.run("exit 0", shell=True, input="x", stdin=subprocess.PIPE)
except ValueError as e:
    print(e)
env = dict(os.environ, NEOS_SUBPROCESS_VAR="from env")
print(repr(subprocess.run("echo %NEOS_SUBPROCESS_VAR%" if windows else "echo $NEOS_SUBPROCESS_VAR", shell=True, env=env, capture_output=True, text=True).stdout))
