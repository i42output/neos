#!/bin/sh
# JavaScript calling Python: the translation units are linked (calls.py's top level runs first, then calls.js's)
cd "$(dirname "$0")" && neos calls.py calls.js
