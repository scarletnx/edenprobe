#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# tests/test-check-report.sh: tools/check-report.py takes whole reports and names each problem of broken ones.
set -u
here=$(cd "$(dirname "$0")" && pwd)
check=$here/../tools/check-report.py
tmp=$(mktemp -d)
trap 'rm -r "$tmp"' EXIT
fails=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; fails=$((fails + 1)); }

for f in ref ours; do
    if out=$(python3 -I "$check" "$here/fixtures/$f.jsonl"); then ok "the $f fixture is whole"; else bad "the $f fixture: $out"; fi
done

# each broken report: the line to write, and the words the checker must print about it
expect_bad() {
    local name=$1 words=$2
    if out=$(python3 -I "$check" "$tmp/$name.jsonl"); then
        bad "$name: accepted"
    elif [[ $out == *"$words"* ]]; then
        ok "$name: $words"
    else
        bad "$name: printed '$out', not '$words'"
    fi
}
head -n 6 "$here/fixtures/ref.jsonl" > "$tmp/no-summary.jsonl"
expect_bad no-summary "no summary: the run did not finish"
sed 's/"passed":2/"passed":3/' "$here/fixtures/ref.jsonl" > "$tmp/wrong-count.jsonl"
expect_bad wrong-count "the summary says passed 3, the case lines 2"
sed 's/"result":"pass","detail":"","id":"bionic.two"/"result":"ok","detail":"","id":"bionic.two"/' "$here/fixtures/ref.jsonl" > "$tmp/bad-result.jsonl"
expect_bad bad-result "result 'ok' is none of pass, fail, skip, crashed"
sed 's/"id":"bionic.two"/"id":"framework.one"/' "$here/fixtures/ref.jsonl" > "$tmp/same-id.jsonl"
expect_bad same-id "id framework.one again (line 3)"
sed 's/"reason":"needs API 30",//' "$here/fixtures/ref.jsonl" > "$tmp/skip-no-reason.jsonl"
expect_bad skip-no-reason "a skip without its reason"
{ cat "$here/fixtures/ref.jsonl"; echo '{"type":"note","text":"late"}'; } > "$tmp/after-summary.jsonl"
expect_bad after-summary "a line after the summary of line 7"
printf '%s' "$(cat "$here/fixtures/ref.jsonl")" > "$tmp/cut.jsonl"
expect_bad cut "the last line has no newline"
{ head -n 2 "$here/fixtures/ref.jsonl"; echo '{"type":"case","suite":"s"'; } > "$tmp/not-json.jsonl"
expect_bad not-json ":3: not JSON"

[ $fails = 0 ] && echo "test-check-report: all passed" || echo "test-check-report: $fails failed"
[ $fails = 0 ]
