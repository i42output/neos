@echo off
rem objects that cross languages: objects.js's top level runs first, then objects.py's
pushd "%~dp0" & neos objects.js,objects.py & popd
