#!/usr/bin/env bash
set -uE -o pipefail

# Enable extended globbing so we can trim trailing whitespace
# efficiently
shopt -s extglob

# The server stores its runtime state under PSIRVER_HOME.
# Refuse to run if the variable is missing.
[[ -n "${PSIRVER_HOME:-}" ]] || {
  printf 'Error: PSIRVER_HOME is not set\n' >&2
  exit 1
}

# Fixed paths and endpoint prefixes used throughout the test suite
readonly SERVER="http://localhost:8000"
readonly SCRIPTS_DIR="$PSIRVER_HOME/scripts"
readonly SCRIPTS="$SERVER/scripts"
readonly JOBS="$SERVER/jobs"
readonly PID_FILE="$PSIRVER_HOME/psirver.pid"

# Terminal colors for readable pass/fail output
readonly GREEN=$'\033[0;32m'
readonly RED=$'\033[0;31m'
readonly RESET=$'\033[0m'

# The PID file is used here as a simple indication that psirver is
# running
[[ -f "$PID_FILE" ]] || {
  printf 'Error: Psirver is not running\n' >&2
  exit 1
}

# These globals are updated by check_endpoint() so later checks can
# inspect both the HTTP status code and the response body
http_code=''
http_response=''

# Remove trailing whitespace from a string
trim_trailing_ws() {
  local s="$1"
  s="${s%%*([[:space:]])}"
  printf '%s' "$s"
}

# Compare actual and expected values and print a colored result
pass_fail() {
  local actual="$1"
  local expected="$2"

  if [[ "$actual" == "$expected" ]]; then
    printf '%sPASSED%s\n' "$GREEN" "$RESET"
  else
    printf '%sFAILED%s (expected "%s", got "%s")\n' \
      "$RED" "$RESET" "$expected" "$actual"
  fi
}

# Print a failure message for side effects that should have
# happened in the filesystem.
note_failure() {
  printf '  %sYET FAILED%s: %s\n' "$RED" "$RESET" "$1"
}

# Verify that a directory was created
require_dir() {
  local path="$1"
  [[ -d "$path" ]] || note_failure "directory not created: $path"
}

# Verify that a file was created
require_file() {
  local path="$1"
  [[ -f "$path" ]] || note_failure "file not created: $path"
}

# Curl helper: return only the HTTP status code
curl_code_only() {
  curl --silent -o /dev/null -w '%{http_code}\n' "$@"
}

# Curl helper: return the response body followed by the HTTP status code.
# This allows a single call to check both
curl_body_and_code() {
  curl --silent -w ' %{http_code}\n' "$@"
}

# Run one endpoint test:
# 1. Print a short label
# 2. Execute the supplied curl helper
# 3. Split its output into body and HTTP code
# 4. Compare the code with the expected code
check_endpoint() {
  local msg="$1"
  local expected="$2"
  shift 2

  printf '%s ' "$msg"

  local status
  status="$("$@")"
  status="$(trim_trailing_ws "$status")"

  http_code="${status: -3}"
  http_response="${status%???}"
  http_response="$(trim_trailing_ws "$http_response")"

  pass_fail "$http_code" "$expected"
}

# Start each full run with a clean scripts directory.
# If old script directories exist, remove them.
cleanup_scripts_dir() {
  [[ -d "$SCRIPTS_DIR" ]] || return 0

  if compgen -G "$SCRIPTS_DIR/*" > /dev/null; then
    printf '%s\n' '--- Warning: Non-empty directory ---'
    chmod a+w -- "$SCRIPTS_DIR"/* 2>/dev/null || true
    rm -rf -- "$SCRIPTS_DIR"/*
  fi
}

# Upload a script and return its numeric script ID through a nameref.
# After upload, also verify the script directory and file were created.
upload_script() {
  local filename="$1"
  local -n out_id="$2"

  check_endpoint "POST /scripts/upload" "200" \
    curl_body_and_code -F "file=@scripts/$filename" "$SCRIPTS/upload"

  out_id="$http_response"
  printf '...Uploaded as %s\n' "$out_id"

  require_dir "$SCRIPTS_DIR/$out_id"
  require_file "$SCRIPTS_DIR/$out_id/$filename"
}

# Run a previously uploaded script.
# The server is expected to respond with a redirect whose Location header/body
# contains the new job ID. Extract that job ID and return it through a nameref.
run_script() {
  local script_id="$1"
  local args="$2"
  local -n out_job_id="$3"

  check_endpoint "POST /scripts/$script_id/run $args" "303" \
    curl_body_and_code "$SCRIPTS/$script_id/run" -d "args=$args"

  out_job_id="${http_response##*/}"
  printf '...Run as %s\n' "$out_job_id"
}

# Query the  status of a  job and compare both  the HTTP code  and the
# response body
get_status() {
  local job_id="$1"
  local expected_code="$2"
  local expected_message="$3"

  check_endpoint "GET /jobs/$job_id" "$expected_code" \
    curl_body_and_code "$JOBS/$job_id"
  pass_fail "$http_response" "$expected_message"
}

# Reduce the /scripts listing to just the script IDs.
# The endpoint returns CSV-like lines; for this check we only care about
# the first field on each line.
scripts_listing_ids() {
  local line id out=''

  while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    id="${line%%,*}"
    out+="$id "
  done <<< "$1"

  printf '%s' "$out"
}

# Check that the /scripts listing contains the expected IDs in order
check_scripts_listing() {
  local expected="$1"
  local output

  check_endpoint "GET /scripts" "200" curl_body_and_code "$SCRIPTS"
  output="$(scripts_listing_ids "$http_response")"
  pass_fail "$output" "$expected"
}

# Purge a set of finished jobs one by one.
delete_job_list() {
  local job
  for job in "$@"; do
    check_endpoint "GET /jobs/$job/purge" "200" \
      curl_code_only "$JOBS/$job/purge"
  done
}

# Delete a script by ID
delete_script() {
  local script_id="$1"
  local expected="$2"
  check_endpoint "GET /scripts/$script_id/delete" "$expected" \
    curl_code_only "$SCRIPTS/$script_id/delete"
}

# -------------------------------------------------------------------------
# 1. BASIC SERVER LIVENESS
# -------------------------------------------------------------------------
# Only after bad-route tests do we verify the normal public endpoints.
# These checks confirm that the server is alive and that the "teapot" endpoint
# preserves its special status code.

printf '\n%s\n' '--- Testing health...'
check_endpoint "GET /health" "200" curl_code_only "$SERVER/health"
check_endpoint "GET /teapot" "418" curl_code_only "$SERVER/teapot"

# -------------------------------------------------------------------------
# 2. METHOD VALIDATION
# -------------------------------------------------------------------------
# First, verify that unsupported HTTP methods are rejected on /scripts.
# This checks whether the server distinguishes allowed and disallowed methods.

printf '%s\n' '--- Testing bad methods...'
check_endpoint "DELETE" "405" curl_code_only -X DELETE "$SCRIPTS"
check_endpoint "HEAD" "405" curl_code_only -X HEAD -I "$SCRIPTS"

# -------------------------------------------------------------------------
# 3. ROUTE AND ARGUMENT VALIDATION
# -------------------------------------------------------------------------
# Next, send malformed routes and malformed identifiers. These tests
# make sure the server rejects invalid paths consistently and does not
# accidentally interpret bad input as a valid command.

printf '\n%s\n' '--- Testing bad commands...'
fake_script="355"
check_endpoint "GET /<bad_command>" "400" \
  curl_code_only "$SERVER/foobar"
check_endpoint "POST /<bad_command>" "400" \
  curl_code_only -F "file=@Makefile" "$SERVER/foobar"
check_endpoint "GET /scripts/<bad_id>/delete" "400" \
  curl_code_only "$SCRIPTS/bad_id/delete"
check_endpoint "GET /scripts/$fake_script" "400" \
  curl_code_only "$SCRIPTS/$fake_script"
check_endpoint "GET /scripts/$fake_script/<bad_command>" "400" \
  curl_code_only "$SCRIPTS/$fake_script/foobar"
check_endpoint "POST /scripts/<bad_command>" "400" \
  curl_code_only -F "file=@Makefile" "$SCRIPTS/foobar"
check_endpoint "POST /scripts/<bad_id>/run" "400" \
  curl_code_only -d 'args=val1,val2' "$SCRIPTS/bad_id/run"
check_endpoint "POST /scripts/$fake_script/<bad_command>" "400" \
  curl_code_only -d 'args=val1,val2' "$SCRIPTS/$fake_script/foobar"
check_endpoint "POST /scripts/$fake_script/run/" "400" \
  curl_code_only -d 'args=arg1,arg2,arg3' "$SCRIPTS/$fake_script/run/"
check_endpoint "GET /jobs/$fake_script/<bad_command>" "400" \
  curl_code_only "$JOBS/$fake_script/foobar"
check_endpoint "GET /jobs/<bad_id>" "400" \
  curl_code_only "$JOBS/bad_id"
check_endpoint "GET /jobs/$fake_script/stderr/" "400" \
  curl_code_only "$JOBS/$fake_script/stderr/"
check_endpoint "GET /jobs/$fake_script/stdout/" "400" \
  curl_code_only "$JOBS/$fake_script/stdout/"

# -------------------------------------------------------------------------
# 4. SCRIPT MANAGEMENT
# -------------------------------------------------------------------------
# Clean the scripts area, then exercise the script lifecycle:
# - list scripts
# - upload scripts
# - list again
# - delete scripts
# - verify that the listing changes accordingly

printf '\n%s\n' '--- Testing script management...'

cleanup_scripts_dir

# Seed the scripts directory with a malformed non-numeric entry and an
# empty numeric slot to see whether the server handles directory
# scanning correctly.

s0="1"
mkdir -p "$PSIRVER_HOME/scripts/foobar"
check_endpoint "GET /scripts" "200" curl_body_and_code "$SCRIPTS"
check_endpoint "GET /scripts/$fake_script/delete" "404" \
  curl_code_only "$SCRIPTS/$fake_script/delete"

rm -rf -- "$SCRIPTS_DIR/foobar"
mkdir -p "$SCRIPTS_DIR/$s0"

upload_script "copious.py" copious_script
upload_script "ok.py" just_script

check_scripts_listing "$copious_script $just_script "
delete_script "$copious_script" 200
check_scripts_listing "$just_script "
delete_script "$just_script" 200
check_scripts_listing ""

# -------------------------------------------------------------------------
# 5. JOB CREATION
# -------------------------------------------------------------------------
# Try to run a nonexistent script first, then upload a set of scripts with
# different behaviors and launch jobs from them:
# - normal completion
# - slow execution
# - slow execution with arguments
# - output-limited execution
# - abnormal termination

printf '\n%s\n' '--- Testing job creation...'

check_endpoint "POST /scripts/$fake_script/run" "404" \
  curl_code_only -d 'args=arg1,arg2,arg3' "$SCRIPTS/$fake_script/run"

upload_script "ok.py" just_script
upload_script "copious.py" copious_script
upload_script "abort.py" abort_script
upload_script "slow.py" slow_script

run_script "$just_script" "" just_job
run_script "$slow_script" "" slow_job
run_script "$slow_script" "one,two" another_slow_job
run_script "$copious_script" "one" copious_job
run_script "$abort_script" "one" job_with_abort

# -------------------------------------------------------------------------
# 6. INVALID JOB CONTROL
# -------------------------------------------------------------------------
# Before checking real jobs, confirm that all job-control endpoints return 404
# when asked about a nonexistent job ID.

printf '\n%s\n' '--- Testing invalid job control...'
check_endpoint "GET /jobs/$fake_script" "404" \
	       curl_code_only "$JOBS/$fake_script"
check_endpoint "GET /jobs/$fake_script/stderr" "404" \
	       curl_code_only "$JOBS/$fake_script/stderr"
check_endpoint "GET /jobs/$fake_script/stdout" "404" \
	       curl_code_only "$JOBS/$fake_script/stdout"
check_endpoint "GET /jobs/$fake_script/terminate" "404" \
	       curl_code_only "$JOBS/$fake_script/terminate"
check_endpoint "GET /jobs/$fake_script/purge" "404" \
	       curl_code_only "$JOBS/$fake_script/purge"

# -------------------------------------------------------------------------
# 7. TERMINATION
# -------------------------------------------------------------------------
# Explicitly terminate one of the running slow jobs and verify that its state
# changes to TERMINATED.

printf '\n%s\n' '--- Testing termination...'
check_endpoint "GET /jobs/$another_slow_job/terminate" "200" \
  curl_code_only "$JOBS/$another_slow_job/terminate"
check_endpoint "GET /jobs/$another_slow_job" "200" \
  curl_body_and_code "$JOBS/$another_slow_job"
pass_fail "$http_response" "$slow_script,TERMINATED"

# -------------------------------------------------------------------------
# 8. STDOUT / STDERR / STATUS EVOLUTION
# -------------------------------------------------------------------------
# Inspect finished-job output streams, then query several job states:
# - completed job
# - still-running job
# - output-limited job
# - aborted job
# Finally wait long enough for the remaining slow job to time out.

printf '\n%s\n' '--- Testing stdin/stdout...'

check_endpoint "GET /jobs/$just_job/stdout" "200" \
  curl_body_and_code "$JOBS/$just_job/stdout"
pass_fail "$http_response" "**** Hello, world! ****
--- scripts/1/ok.py"

check_endpoint "GET /jobs/$just_job/stderr" "200" \
  curl_body_and_code "$JOBS/$just_job/stderr"
pass_fail "$http_response" "No errors!
Clean output"

# Here, 24 is the expected return value of the completed job
get_status "$just_job" "200" "$just_script,24"
get_status "$slow_job" "200" "$slow_script,RUNNING"
get_status "$copious_job" "200" "$copious_script,OUTPUT_LIMITED"

printf '%s\n' 'Sleeping...'
sleep 1
get_status "$job_with_abort" "200" "$abort_script,TERMINATED"

printf '%s\n' 'Sleeping...'
sleep 5
get_status "$slow_job" "200" "$slow_script,TIMED_OUT"

# -------------------------------------------------------------------------
# 9. JOB LISTING
# -------------------------------------------------------------------------
# Once the jobs have stabilized, verify that /jobs returns the expected set
# of job records in the expected format.

jobs_output="$just_job,$just_script,ok.py
$slow_job,$slow_script,slow.py
$another_slow_job,$slow_script,slow.py
$copious_job,$copious_script,copious.py
$job_with_abort,$abort_script,abort.py"

check_endpoint "GET /jobs" "200" curl_body_and_code "$JOBS"
pass_fail "$http_response" "$jobs_output"


# -------------------------------------------------------------------------
# 10. JOB PURGE
# -------------------------------------------------------------------------
# Remove all jobs, then verify that the job list becomes empty.

# Cannot delete: the script is still in use!
delete_script "$just_script" 425

delete_job_list \
  "$just_job" \
  "$slow_job" \
  "$another_slow_job" \
  "$copious_job" \
  "$job_with_abort"

check_endpoint "GET /jobs" "200" curl_body_and_code "$JOBS"
pass_fail "$http_response" ""

# -------------------------------------------------------------------------
# 11. FINAL SCRIPT CLEANUP
# -------------------------------------------------------------------------
# Delete the uploaded scripts at the end so repeated runs start cleanly.

delete_script "$just_script" 200
delete_script "$copious_script" 200
delete_script "$abort_script" 200
