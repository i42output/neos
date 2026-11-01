#!/bin/sh
# three languages linked: three.lua's top level runs first, then three.py's, then three.js's
cd "$(dirname "$0")" && neos three.lua three.py three.js
