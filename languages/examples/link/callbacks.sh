#!/bin/sh
# Python calling JavaScript (and JavaScript calling Python back): callbacks.js's top level runs first, then callbacks.py's
cd "$(dirname "$0")" && neos callbacks.js,callbacks.py
