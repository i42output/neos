#!/bin/sh
# objects that cross languages: objects.js's top level runs first, then objects.py's
cd "$(dirname "$0")" && neos objects.js,objects.py
