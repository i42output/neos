#!/bin/sh
# errors that cross languages: errors.py's top level runs first, then errors.js's
cd "$(dirname "$0")" && neos errors.py,errors.js
