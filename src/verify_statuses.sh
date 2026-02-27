#!/usr/bin/env bash
set -euo pipefail

cd /mnt/c/Users/Brennan/psirver-bloi/src
make -s

rm -rf /tmp/pshome
mkdir -p /tmp/pshome
export PSIRVER_HOME=/tmp/pshome

./psirver 18000 >/tmp/psirver18000.log 2>&1 &
pid=$!
sleep 1
if ! kill -0 "$pid" 2>/dev/null; then
  echo "START_FAIL"
  cat /tmp/psirver18000.log || true
  exit 1
fi

code() {
  curl -s -o /dev/null -w "%{http_code}" "$1"
}

echo "GET /health $(code http://127.0.0.1:18000/health)"
echo "GET /jobs $(code http://127.0.0.1:18000/jobs)"
echo "GET /bad $(code http://127.0.0.1:18000/nope)"
echo "GET /jobs/abc $(code http://127.0.0.1:18000/jobs/abc)"
echo "GET /jobs/1/stderr/ $(code http://127.0.0.1:18000/jobs/1/stderr/)"
echo "GET /scripts $(code http://127.0.0.1:18000/scripts)"
echo "GET /scripts/abc/delete $(code http://127.0.0.1:18000/scripts/abc/delete)"

echo "POST /bad $(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "" http://127.0.0.1:18000/nope)"
echo "POST /scripts/abc/run $(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "args=" http://127.0.0.1:18000/scripts/abc/run)"
echo "POST /scripts/1/run/ $(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "args=" http://127.0.0.1:18000/scripts/1/run/)"

kill "$pid"
wait "$pid" 2>/dev/null || true
