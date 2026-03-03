#!/usr/bin/env bash
set -u
target="${1:-/tmp/results18004.txt}"
ls -l "$target" || true
cat "$target" || true
echo DONE
