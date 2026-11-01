@echo off
rem three languages linked: three.lua's top level runs first, then three.py's, then three.js's
pushd "%~dp0" & neos three.lua three.py three.js & popd
