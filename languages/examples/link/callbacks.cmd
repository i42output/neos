@echo off
rem Python calling JavaScript (and JavaScript calling Python back): callbacks.js's top level runs first, then callbacks.py's
pushd "%~dp0" & neos callbacks.js,callbacks.py & popd
