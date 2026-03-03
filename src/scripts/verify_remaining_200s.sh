#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
root_dir="$(cd "$script_dir/.." && pwd)"
port="${1:-18003}"
pshome="${2:-/tmp/pshome}"
log_file="/tmp/psirver${port}.log"

cd "$root_dir"
make -s
rm -rf "$pshome" && mkdir -p "$pshome"
export PSIRVER_HOME="$pshome"

./psirver "$port" >"$log_file" 2>&1 &
pid=$!
sleep 1
trap 'kill $pid 2>/dev/null || true; wait $pid 2>/dev/null || true' EXIT

base="http://127.0.0.1:${port}"
script_id=$(curl -s -X POST -F "file=@testdata/test_hello.py" "$base/scripts/upload")
job_id=$(curl -s -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "args=a,b" "$base/scripts/$script_id/run")
sleep 1

code(){ curl -s -o /dev/null -w "%{http_code}" "$1"; }

echo "GET /jobs/<id> => $(code $base/jobs/$job_id)"
echo "GET /jobs/<id>/stderr => $(code $base/jobs/$job_id/stderr)"
echo "GET /jobs/<id>/stdout => $(code $base/jobs/$job_id/stdout)"
echo "GET /jobs/<id>/terminate => $(code $base/jobs/$job_id/terminate)"
echo "GET /scripts/<id>/delete => $(code $base/scripts/$script_id/delete)"
