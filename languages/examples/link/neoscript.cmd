@echo off
rem neoscript linked with Python and JavaScript: neoscript.py's top level runs first, then neoscript.neo's (its main), then neoscript.js's
pushd "%~dp0" & neos neoscript.py,neoscript.neo,neoscript.js & popd
