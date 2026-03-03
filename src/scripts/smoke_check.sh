#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
root_dir="$(cd "$script_dir/.." && pwd)"
port="${1:-18020}"
pshome="${2:-/tmp/pshome_smoke_${port}}"
log_file="/tmp/psirver_smoke_${port}.log"

pass_count=0
fail_count=0

pass() {
  echo "PASS $1"
  pass_count=$((pass_count + 1))
}

fail() {
  echo "FAIL $1"
  fail_count=$((fail_count + 1))
}

check_eq() {
  local name="$1"
  local got="$2"
  local expected="$3"
  if [[ "$got" == "$expected" ]]; then
    pass "$name"
  else
    fail "$name expected='$expected' got='$got'"
  fi
}

check_nonempty() {
  local name="$1"
  local got="$2"
  if [[ -n "$got" ]]; then
    pass "$name"
  else
    fail "$name expected non-empty"
  fi
}

check_contains() {
  local name="$1"
  local haystack="$2"
  local needle="$3"
  if grep -Fq "$needle" <<<"$haystack"; then
    pass "$name"
  else
    fail "$name missing '$needle'"
  fi
}

check_not_contains() {
  local name="$1"
  local haystack="$2"
  local needle="$3"
  if grep -Fq "$needle" <<<"$haystack"; then
    fail "$name unexpectedly contains '$needle'"
  else
    pass "$name"
  fi
}

cd "$root_dir"

echo "Building..."
make clean >/dev/null
make >/dev/null

echo "Preparing PSIRVER_HOME=$pshome"
chmod -R u+rwX "$pshome" 2>/dev/null || true
rm -rf "$pshome"
mkdir -p "$pshome"
export PSIRVER_HOME="$pshome"

echo "Starting server on port $port"
./psirver "$port" >"$log_file" 2>&1 &
pid=$!

cleanup() {
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}
trap cleanup EXIT

sleep 1
if ! kill -0 "$pid" 2>/dev/null; then
  echo "FAIL server_start"
  cat "$log_file" || true
  exit 1
fi

base="http://127.0.0.1:${port}"

# 1) /health and /teapot
health_code="$(curl -s -o /tmp/smoke_health.body -w "%{http_code}" "$base/health")"
health_body="$(cat /tmp/smoke_health.body)"
check_eq "health_status" "$health_code" "200"
check_eq "health_body" "$health_body" "Running"

teapot_code="$(curl -s -o /tmp/smoke_teapot.body -w "%{http_code}" "$base/teapot")"
teapot_body="$(cat /tmp/smoke_teapot.body)"
check_eq "teapot_status" "$teapot_code" "418"
check_eq "teapot_body" "$teapot_body" "Running"

# 2) empty /scripts
scripts_empty_code="$(curl -s -o /tmp/smoke_scripts_empty.body -w "%{http_code}" "$base/scripts")"
scripts_empty_body="$(cat /tmp/smoke_scripts_empty.body)"
check_eq "scripts_empty_status" "$scripts_empty_code" "200"
check_eq "scripts_empty_body" "$scripts_empty_body" ""

# 3) upload two scripts
id1="$(curl -s -X POST -F "script=@testdata/test_hello.py" "$base/scripts/upload")"
id2="$(curl -s -X POST -F "script=@testdata/test_script.py" "$base/scripts/upload")"
check_nonempty "upload_id1_nonempty" "$id1"
check_nonempty "upload_id2_nonempty" "$id2"
if [[ "$id1" =~ ^[0-9]+$ ]]; then pass "upload_id1_numeric"; else fail "upload_id1_numeric got='$id1'"; fi
if [[ "$id2" =~ ^[0-9]+$ ]]; then pass "upload_id2_numeric"; else fail "upload_id2_numeric got='$id2'"; fi

# 4) non-empty /scripts
scripts_list_code="$(curl -s -o /tmp/smoke_scripts_list.body -w "%{http_code}" "$base/scripts")"
scripts_list_body="$(cat /tmp/smoke_scripts_list.body)"
check_eq "scripts_list_status" "$scripts_list_code" "200"
check_nonempty "scripts_list_nonempty" "$scripts_list_body"
check_contains "scripts_contains_id1" "$scripts_list_body" "${id1},"
check_contains "scripts_contains_id2" "$scripts_list_body" "${id2},"

# 5) delete one script by id
delete_code="$(curl -s -o /tmp/smoke_delete.body -w "%{http_code}" "$base/scripts/$id1/delete")"
delete_body="$(cat /tmp/smoke_delete.body)"
check_eq "delete_status" "$delete_code" "200"
check_eq "delete_body_id" "$delete_body" "$id1"

# 6) updated listing
scripts_after_code="$(curl -s -o /tmp/smoke_scripts_after.body -w "%{http_code}" "$base/scripts")"
scripts_after_body="$(cat /tmp/smoke_scripts_after.body)"
check_eq "scripts_after_status" "$scripts_after_code" "200"
check_not_contains "scripts_after_removed_id1" "$scripts_after_body" "${id1},"
check_contains "scripts_after_keeps_id2" "$scripts_after_body" "${id2},"

echo "----"
echo "Smoke summary: PASS=$pass_count FAIL=$fail_count"

if [[ "$fail_count" -gt 0 ]]; then
  exit 1
fi

exit 0
