#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# tests/test-edenprobe-tools.sh TOOLS: the ScarletNX workspace's edenprobe-diff.py and edenprobe-table.py (in TOOLS)
# read androidprobe's reports as they are: the summary of each, a failure a reference passes, a crashed case, times.
set -u
here=$(cd "$(dirname "$0")" && pwd)
tools=${1:?usage: tests/test-edenprobe-tools.sh TOOLS (the folder of edenprobe-diff.py and edenprobe-table.py)}
fails=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; fails=$((fails + 1)); }
has() { if [[ $out == *"$2"* ]]; then ok "$1"; else bad "$1: no '$2' in:"; printf '%s\n' "$out"; fi; }

out=$(python3 -I "$tools/edenprobe-diff.py" "$here/fixtures/ours.jsonl" "$here/fixtures/ref.jsonl" --cases 2>&1)
has "diff: our summary" "{'passed': 1, 'failed': 1, 'skipped': 0, 'crashed': 1}"
has "diff: the mode note" "mode: every case, 3 of them"
has "diff: one failure a reference passes" "our failures: 1; a reference passes 1, none passes 0"
has "diff: the case and its detail" "bionic: case two
      got Resource temporarily unavailable, expected Try again"

out=$(python3 -I "$tools/edenprobe-table.py" "android=$here/fixtures/ref.jsonl" "layer=$here/fixtures/ours.jsonl" 2>&1)
has "table: results and times side by side" "pass        0.0       fail        0.0  bionic/case two"
has "table: a crashed case" "crashed"
has "table: the counts" "layer: crashed 1, fail 1, pass 1"

[ $fails = 0 ] && echo "test-edenprobe-tools: all passed" || echo "test-edenprobe-tools: $fails failed"
[ $fails = 0 ]
