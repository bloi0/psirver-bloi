#!/usr/bin/env bash
set -euo pipefail

cd /mnt/c/Users/Brennan/psirver-bloi/src
make -s

rm -rf /tmp/pshome
mkdir -p /tmp/pshome
export PSIRVER_HOME=/tmp/pshome

./psirver 18001 >/tmp/psirver18001.log 2>&1 &
pid=$!
sleep 1
if ! kill -0 "$pid" 2>/dev/null; then
  echo "START_FAIL"
  cat /tmp/psirver18001.log || true
  exit 1
fi

cleanup() {
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}
trap cleanup EXIT

base=http://127.0.0.1:18001

script_id=$(curl -s -X POST -F "file=@test_hello.py" "$base/scripts/upload")
run_code=$(curl -s -o /tmp/run.out -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "args=a,b" "$base/scripts/$script_id/run")
job_id=$(cat /tmp/run.out)
sleep 1

code() { curl -s -o /dev/null -w "%{http_code}" "$1"; }

echo "POST /scripts/upload => 200 (id=$script_id)"
echo "POST /scripts/<id>/run => $run_code (job=$job_id)"
echo "GET /jobs/<id> => $(code $base/jobs/$job_id)"
echo "GET /jobs/<id>/stdout => $(code $base/jobs/$job_id/stdout)"
echo "GET /jobs/<id>/stderr => $(code $base/jobs/$job_id/stderr)"
echo "GET /jobs/<id>/terminate => $(code $base/jobs/$job_id/terminate)"
echo "GET /scripts/<id>/delete => $(code $base/scripts/$script_id/delete)"
