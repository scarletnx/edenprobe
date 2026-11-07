#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# tests/test-run-adb.sh: tools/run-adb.sh on a fake device (tests/fake-adb): a whole run, a launch that dies, one
# that hangs, a run that never ends whole, and a device that already has the package. Each leaves the device as it
# was: the package uninstalled, or never installed.
set -u
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -r "$tmp"' EXIT
fails=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; fails=$((fails + 1)); }
touch "$tmp/app.apk"

scenario() { # NAME SCENARIO [OPTIONS...]: runs it in $tmp/NAME, sets rc
    local name=$1 sc=$2
    shift 2
    mkdir -p "$tmp/$name/dev"
    [ "$sc" = installed ] && touch "$tmp/$name/dev/installed"
    FAKE_ADB=$tmp/$name/dev FAKE_SCENARIO=$sc FAKE_REPORT=$here/fixtures/ref.jsonl ADB=$here/fake-adb \
        "$here/../tools/run-adb.sh" --serial fake --apk "$tmp/app.apk" --out "$tmp/$name/out" "$@" > "$tmp/$name/log" 2>&1
    rc=$?
}
calls() { grep -c -- "$2" "$tmp/$1/dev/calls"; }

scenario whole ok
[ $rc = 0 ] && ok "whole: exit 0" || bad "whole: exit $rc: $(cat "$tmp/whole/log")"
cmp -s "$tmp/whole/out/report.jsonl" "$here/fixtures/ref.jsonl" && ok "whole: the report pulled as it is" || bad "whole: the report differs"
[ "$(calls whole 'install -t')" = 1 ] && [ "$(calls whole "uninstall dev.dravee.androidprobe")" = 1 ] && [ ! -e "$tmp/whole/dev/installed" ] &&
    ok "whole: installed once, uninstalled at the end" || bad "whole: install and uninstall: $(cat "$tmp/whole/dev/calls")"
grep -q "meminfo of" "$tmp/whole/out/meminfo.txt" && ok "whole: meminfo saved" || bad "whole: no meminfo"

scenario dies die-once
[ $rc = 0 ] && [ "$(cat "$tmp/dies/dev/launches")" = 2 ] && ok "dies: launched again, exit 0" || bad "dies: exit $rc, launches $(cat "$tmp/dies/dev/launches")"
[ -s "$tmp/dies/out/exit-info-1.txt" ] && ok "dies: exit-info of launch 1 saved" || bad "dies: no exit-info-1.txt"
[ ! -e "$tmp/dies/dev/installed" ] && ok "dies: uninstalled" || bad "dies: still installed"

scenario hangs stall --stall 3
[ $rc = 0 ] && [ "$(calls hangs 'am force-stop')" -ge 1 ] && [ "$(cat "$tmp/hangs/dev/launches")" = 2 ] &&
    ok "hangs: stopped after 3 s silent and launched again" || bad "hangs: exit $rc, $(cat "$tmp/hangs/log")"

scenario never never --launches 2
[ $rc = 1 ] && ok "never: exit 1" || bad "never: exit $rc"
grep -q "no summary after 2 launches" "$tmp/never/log" && ok "never: says so" || bad "never: $(cat "$tmp/never/log")"
[ "$(grep -c . "$tmp/never/out/report.jsonl" 2> /dev/null)" = 3 ] && ok "never: the partial report pulled" || bad "never: no partial report"
[ ! -e "$tmp/never/dev/installed" ] && ok "never: uninstalled" || bad "never: still installed"

scenario taken installed
[ $rc = 3 ] && ok "taken: exit 3" || bad "taken: exit $rc"
[ "$(calls taken 'install -t')" = 0 ] && [ "$(calls taken 'uninstall')" = 0 ] && [ -e "$tmp/taken/dev/installed" ] &&
    ok "taken: the installed package left alone" || bad "taken: $(cat "$tmp/taken/dev/calls")"

[ $fails = 0 ] && echo "test-run-adb: all passed" || echo "test-run-adb: $fails failed"
[ $fails = 0 ]
