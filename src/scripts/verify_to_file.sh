#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
root_dir="$(cd "$script_dir/.." && pwd)"
port="${1:-18004}"
pshome="${2:-/tmp/pshome}"
out="${3:-/tmp/results${port}.txt}"
log_file="/tmp/psirver${port}.log"

: > "$out"

cd "$root_dir"
make -s
rm -rf "$pshome" && mkdir -p "$pshome"
export PSIRVER_HOME="$pshome"

./psirver "$port" >"$log_file" 2>&1 &
pid=$!
sleep 1
trap 'kill $pid 2>/dev/null || true; wait $pid 2>/dev/null || true' EXIT

base="http://127.0.0.1:${port}"
code(){ curl -s -o /dev/null -w "%{http_code}" "$1"; }

printf "GET /health %s\n" "$(code $base/health)" >> "$out"
printf "GET /jobs %s\n" "$(code $base/jobs)" >> "$out"
printf "GET /bad %s\n" "$(code $base/nope)" >> "$out"
printf "GET /jobs/abc %s\n" "$(code $base/jobs/abc)" >> "$out"
printf "GET /jobs/1/nope %s\n" "$(code $base/jobs/1/nope)" >> "$out"
printf "GET /jobs/1/stderr/ %s\n" "$(code $base/jobs/1/stderr/)" >> "$out"
printf "GET /scripts %s\n" "$(code $base/scripts)" >> "$out"
printf "GET /scripts/abc/delete %s\n" "$(code $base/scripts/abc/delete)" >> "$out"
printf "GET /scripts/1 %s\n" "$(code $base/scripts/1)" >> "$out"
printf "GET /scripts/1/nope %s\n" "$(code $base/scripts/1/nope)" >> "$out"
printf "POST /bad %s\n" "$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "" $base/nope)" >> "$out"
printf "POST /scripts/nope %s\n" "$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "" $base/scripts/nope)" >> "$out"
printf "POST /scripts/abc/run %s\n" "$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "args=" $base/scripts/abc/run)" >> "$out"
printf "POST /scripts/1/nope %s\n" "$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "args=" $base/scripts/1/nope)" >> "$out"
printf "POST /scripts/1/run/ %s\n" "$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "args=" $base/scripts/1/run/)" >> "$out"

up_resp=$(curl -s -w "\n%{http_code}" -X POST -F "file=@testdata/test_hello.py" "$base/scripts/upload")
up_id=$(echo "$up_resp" | head -n 1)
up_code=$(echo "$up_resp" | tail -n 1)
printf "POST /scripts/upload %s id=%s\n" "$up_code" "$up_id" >> "$out"

run_resp=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "args=a,b" "$base/scripts/$up_id/run")
run_id=$(echo "$run_resp" | head -n 1)
run_code=$(echo "$run_resp" | tail -n 1)
printf "POST /scripts/%s/run %s job=%s\n" "$up_id" "$run_code" "$run_id" >> "$out"

sleep 1
printf "GET /jobs/%s %s\n" "$run_id" "$(code $base/jobs/$run_id)" >> "$out"
printf "GET /jobs/%s/stderr %s\n" "$run_id" "$(code $base/jobs/$run_id/stderr)" >> "$out"
printf "GET /jobs/%s/stdout %s\n" "$run_id" "$(code $base/jobs/$run_id/stdout)" >> "$out"
printf "GET /jobs/%s/terminate %s\n" "$run_id" "$(code $base/jobs/$run_id/terminate)" >> "$out"
printf "GET /scripts/%s/delete %s\n" "$up_id" "$(code $base/scripts/$up_id/delete)" >> "$out"
