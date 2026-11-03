#!/usr/bin/env bash
#
# mayhem/test.sh — RUNS the known-answer self test built by mayhem/build.sh (tar header parsing,
# COCO JSON lookahead parsing, URI splitting, WAV decoding) and reports CTRF counts. A binary that
# exits early or prints fewer PASS lines than EXPECTED fails the suite.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

EXPECTED=22
RUNNER="$SRC/dali_selftest"
if [ ! -x "$RUNNER" ]; then
  echo "missing $RUNNER (mayhem/build.sh should have built it)" >&2
  emit_ctrf "dali-selftest" 0 1 0
  exit 1
fi

out="$("$RUNNER" 2>&1)"
echo "$out"
passed=$(printf '%s\n' "$out" | grep -c '^PASS ')
failed=$(printf '%s\n' "$out" | grep -c '^FAIL ')
if ! printf '%s\n' "$out" | grep -q '^SELFTEST DONE failed=0$'; then
  failed=$(( failed + 1 ))
fi
if [ "$passed" -lt "$EXPECTED" ]; then
  failed=$(( failed + EXPECTED - passed ))
fi
emit_ctrf "dali-selftest" "$passed" "$failed" 0
