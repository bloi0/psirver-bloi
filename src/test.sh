#!/bin/bash
CURLFLAGS="-s -o /dev/null -w %{http_code}"
SERVER=http://localhost:8000

[[ -f "$PSIRVER_HOME/psirver.pid" ]] || { echo "Error: Psirver is not running" >&2; exit 1; }

check_endpoint() {
  local msg="$1"
  local cmd="$2"
  local expected="$3"
  
  # ANSI colors
  local GREEN=$'\033[0;32m'
  local RED=$'\033[0;31m'
  local RESET=$'\033[0m'
  
  echo -n "${msg} "
  local status="$(eval "$cmd")"

  if [[ "$status" == "$expected" ]]; then echo "${GREEN}PASSED${RESET}";
  else echo "${RED}FAILED${RESET} (expected $expected, got $status)"; fi
}

# Bad methods
check_endpoint "DELETE" "curl -X DELETE $CURLFLAGS $SERVER/scripts" "405"
check_endpoint "HEAD" "curl -X HEAD -I $CURLFLAGS $SERVER/scripts" "405"

# GET requests
check_endpoint "GET /<bad_command>" "curl $CURLFLAGS $SERVER/foobar" "400"
check_endpoint "GET /health" "curl $CURLFLAGS $SERVER/health" "200"
check_endpoint "GET /jobs" "curl $CURLFLAGS $SERVER/jobs" "200"
check_endpoint "GET /jobs/<bad_id>" "curl $CURLFLAGS $SERVER/jobs/bad_id" "400"
check_endpoint "GET /jobs/<id>" "curl $CURLFLAGS $SERVER/jobs/355" "200"
check_endpoint "GET /jobs/<id>/<bad_command>" "curl $CURLFLAGS $SERVER/jobs/355/foobar" "400"
check_endpoint "GET /jobs/<id>/stderr" "curl $CURLFLAGS $SERVER/jobs/355/stderr" "200"
check_endpoint "GET /jobs/<id>/stderr/" "curl $CURLFLAGS $SERVER/jobs/355/stderr/" "400"
check_endpoint "GET /jobs/<id>/stdout" "curl $CURLFLAGS $SERVER/jobs/355/stdout" "200"
check_endpoint "GET /jobs/<id>/terminate" "curl $CURLFLAGS $SERVER/jobs/355/terminate" "200"
check_endpoint "GET /scripts" "curl $CURLFLAGS $SERVER/scripts" "200"
check_endpoint "GET /scripts/<bad_id>/delete" "curl $CURLFLAGS $SERVER/scripts/bad_id/delete" "400"
check_endpoint "GET /scripts/<id>" "curl $CURLFLAGS $SERVER/scripts/355" "400"
check_endpoint "GET /scripts/<id>/<bad_command>" "curl $CURLFLAGS $SERVER/scripts/355/foobar" "400"
check_endpoint "GET /scripts/<id>/delete" "curl $CURLFLAGS $SERVER/scripts/355/delete" "200"
check_endpoint "GET /teapot" "curl $CURLFLAGS $SERVER/teapot" "200"

# POST requests
check_endpoint "POST /<bad_command>" "curl -F \"file=@Makefile\" $CURLFLAGS $SERVER/foobar" "400"
check_endpoint "POST /scripts/<bad_command>" "curl -F \"file=@Makefile\" $CURLFLAGS $SERVER/scripts/foobar" "400"
check_endpoint "POST /scripts/<bad_id>/run" "curl -d \"args=val1,val2\" $CURLFLAGS $SERVER/scripts/bad_id/run" "400"
check_endpoint "POST /scripts/<id>/<bad_command>" "curl -d \"args=val1,val2\" $CURLFLAGS $SERVER/scripts/355/foobar" "400"
check_endpoint "POST /scripts/<id>/run" "curl -d \"args=arg1,arg2,arg3\" $CURLFLAGS $SERVER/scripts/355/run" "200"
check_endpoint "POST /scripts/<id>/run/" "curl -d \"args=arg1,arg2,arg3\" $CURLFLAGS $SERVER/scripts/355/run/" "400"
check_endpoint "POST /scripts/upload" "curl -F \"file=@scripts/test0.py\" $CURLFLAGS $SERVER/scripts/upload" "200"

