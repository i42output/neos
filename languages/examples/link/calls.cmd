@echo off
rem JavaScript calling Python: the translation units are linked (calls.py's top level runs first, then calls.js's)
pushd "%~dp0" & neos calls.py calls.js & popd
