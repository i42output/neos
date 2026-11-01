@echo off
rem errors that cross languages: errors.py's top level runs first, then errors.js's
pushd "%~dp0" & neos errors.py errors.js & popd
