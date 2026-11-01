@echo off
rem more than one translation unit of a language: each after the first of its language is a program of its own, linked as units of different
rem languages are (their top levels run in turn: units.py's, units.js's, units2.py's, units2.js's, units.lua's, then units2.lua's); one and two are the program's arguments
pushd "%~dp0" & neos units.py,units.js,units2.py,units2.js,units.lua,units2.lua one two & popd
