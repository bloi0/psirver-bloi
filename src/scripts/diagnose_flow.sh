#!/usr/bin/env bash
set -u

port="${1:-18001}"
home_dir="${2:-/tmp/pshome}"
log_file="/tmp/psirver${port}.log"

echo "=== Processes (psirver) ==="
pgrep -af psirver || true
echo

echo "=== Log: $log_file ==="
cat "$log_file" || true
echo

echo "=== Home: $home_dir ==="
ls -la "$home_dir" || true
