#!/bin/sh
# neoscript linked with Python and JavaScript: neoscript.py's top level runs first, then neoscript.neo's (its main), then neoscript.js's
cd "$(dirname "$0")" && neos neoscript.py,neoscript.neo,neoscript.js
