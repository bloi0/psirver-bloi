#!/usr/bin/env bash
set -u

script_dir="$(cd "$(dirname "$0")" && pwd)"
root_dir="$(cd "$script_dir/.." && pwd)"
port="${1:-18002}"
pshome="${2:-/tmp/pshome}"
log_file="/tmp/psirver${port}.log"

cd "$root_dir" || exit 1
make -s || exit 1

rm -rf "$pshome"
mkdir -p "$pshome"
export PSIRVER_HOME="$pshome"

./psirver "$port" >"$log_file" 2>&1 &
pid=$!
sleep 1
if ! kill -0 "$pid" 2>/dev/null; then
  echo "START_FAIL"
  cat "$log_file" || true
  exit 1
fi

cleanup() {
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}
trap cleanup EXIT

base="http://127.0.0.1:${port}"
status_get() { curl -s -o /dev/null -w "%{http_code}" "$1"; }
body_get() { curl -s "$1"; }
status_post_form() { curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "$2" "$1"; }

echo "GET /bad => $(status_get $base/not_a_route)"
health_body=$(body_get $base/health)
teapot_body=$(body_get $base/teapot)
echo "GET /health => $(status_get $base/health) body=$health_body"
echo "GET /teapot => $(status_get $base/teapot) body=$teapot_body"
if [ "$health_body" != "Running" ]; then
  echo "BODY_MISMATCH /health expected=Running got=$health_body"
fi
if [ "$teapot_body" != "Running" ]; then
  echo "BODY_MISMATCH /teapot expected=Running got=$teapot_body"
fi
echo "GET /jobs => $(status_get $base/jobs)"
echo "GET /jobs/<bad_id> => $(status_get $base/jobs/abc)"
echo "GET /jobs/<id>/<bad_command> => $(status_get $base/jobs/1/nope)"
echo "GET /jobs/<id>/stderr/ => $(status_get $base/jobs/1/stderr/)"
echo "GET /scripts => $(status_get $base/scripts)"
echo "GET /scripts/<bad_id>/delete => $(status_get $base/scripts/abc/delete)"
echo "GET /scripts/<id> => $(status_get $base/scripts/1)"
echo "GET /scripts/<id>/<bad_command> => $(status_get $base/scripts/1/nope)"
echo "POST /bad => $(status_post_form $base/not_a_route "")"
echo "POST /scripts/<bad_command> => $(status_post_form $base/scripts/nope "")"
echo "POST /scripts/<bad_id>/run => $(status_post_form $base/scripts/abc/run "args=")"
echo "POST /scripts/<id>/<bad_command> => $(status_post_form $base/scripts/1/nope "args=")"
echo "POST /scripts/<id>/run/ => $(status_post_form $base/scripts/1/run/ "args=")"

up_resp=$(curl -s -w "\n%{http_code}" -X POST -F "file=@testdata/test_hello.py" "$base/scripts/upload")
up_body=$(echo "$up_resp" | head -n 1)
up_code=$(echo "$up_resp" | tail -n 1)
echo "POST /scripts/upload => $up_code (body=$up_body)"

if echo "$up_body" | grep -Eq '^[0-9]+$'; then
  script_id="$up_body"
  run_resp=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "args=a,b" "$base/scripts/$script_id/run")
  run_body=$(echo "$run_resp" | head -n 1)
  run_code=$(echo "$run_resp" | tail -n 1)
  echo "POST /scripts/<id>/run => $run_code (body=$run_body)"

  if echo "$run_body" | grep -Eq '^[0-9]+$'; then
    job_id="$run_body"
    sleep 1
    echo "GET /jobs/<id> => $(status_get $base/jobs/$job_id)"
    echo "GET /jobs/<id>/stderr => $(status_get $base/jobs/$job_id/stderr)"
    echo "GET /jobs/<id>/stdout => $(status_get $base/jobs/$job_id/stdout)"
    echo "GET /jobs/<id>/terminate => $(status_get $base/jobs/$job_id/terminate)"
  fi

  echo "GET /scripts/<id>/delete => $(status_get $base/scripts/$script_id/delete)"
fi
