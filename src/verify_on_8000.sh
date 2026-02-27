#!/usr/bin/env bash
set -euo pipefail

cd /mnt/c/Users/Brennan/psirver-bloi/src
make -s

# Prevent stale results: stop any old server still bound to 8000.
pkill -x psirver || true
sleep 1

rm -rf /tmp/pshome8000
mkdir -p /tmp/pshome8000
export PSIRVER_HOME=/tmp/pshome8000

./psirver 8000 >/tmp/psirver8000.log 2>&1 &
pid=$!

cleanup() {
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}
trap cleanup EXIT

sleep 1
if ! kill -0 "$pid" 2>/dev/null; then
  echo "START_FAIL"
  cat /tmp/psirver8000.log || true
  exit 1
fi

status_get() {
  curl -s -o /dev/null -w "%{http_code}" "$1"
}

status_post_form() {
  curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "$2" "$1"
}

base=http://127.0.0.1:8000

echo "GET /<bad_command> => $(status_get $base/not_a_route)"
echo "GET /health => $(status_get $base/health)"
echo "GET /jobs => $(status_get $base/jobs)"
echo "GET /jobs/<bad_id> => $(status_get $base/jobs/abc)"
echo "GET /jobs/<id>/<bad_command> => $(status_get $base/jobs/1/nope)"
echo "GET /jobs/<id>/stderr/ => $(status_get $base/jobs/1/stderr/)"
echo "GET /scripts => $(status_get $base/scripts)"
echo "GET /scripts/<bad_id>/delete => $(status_get $base/scripts/abc/delete)"
echo "GET /scripts/<id> => $(status_get $base/scripts/1)"
echo "GET /scripts/<id>/<bad_command> => $(status_get $base/scripts/1/nope)"
echo "POST /<bad_command> => $(status_post_form $base/not_a_route "")"
echo "POST /scripts/<bad_command> => $(status_post_form $base/scripts/nope "")"
echo "POST /scripts/<bad_id>/run => $(status_post_form $base/scripts/abc/run "args=")"
echo "POST /scripts/<id>/<bad_command> => $(status_post_form $base/scripts/1/nope "args=")"
echo "POST /scripts/<id>/run/ => $(status_post_form $base/scripts/1/run/ "args=")"

upload_resp=$(curl -s -w "\n%{http_code}" -X POST -F "file=@test_hello.py" "$base/scripts/upload")
upload_id=$(echo "$upload_resp" | head -n 1)
upload_code=$(echo "$upload_resp" | tail -n 1)
echo "POST /scripts/upload => $upload_code (id=$upload_id)"

if echo "$upload_id" | grep -Eq '^[0-9]+$'; then
  run_resp=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "args=a,b" "$base/scripts/$upload_id/run")
  run_id=$(echo "$run_resp" | head -n 1)
  run_code=$(echo "$run_resp" | tail -n 1)
  echo "POST /scripts/<id>/run => $run_code (job=$run_id)"

  if echo "$run_id" | grep -Eq '^[0-9]+$'; then
    sleep 1
    echo "GET /jobs/<id> => $(status_get $base/jobs/$run_id)"
    echo "GET /jobs/<id>/stderr => $(status_get $base/jobs/$run_id/stderr)"
    echo "GET /jobs/<id>/stdout => $(status_get $base/jobs/$run_id/stdout)"
    echo "GET /jobs/<id>/terminate => $(status_get $base/jobs/$run_id/terminate)"
  fi

  echo "GET /scripts/<id>/delete => $(status_get $base/scripts/$upload_id/delete)"
fi
