#!/bin/bash
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# android/tests/test-run-adb.sh: android/tools/run-adb.sh on a fake device (fake-adb): a whole run, a launch that dies,
# one that hangs, a run that never ends whole, and a device that already has the package. Each leaves the device as
# it was: the package uninstalled, or never installed.
set -u
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -r "$tmp"' EXIT
fails=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; fails=$((fails + 1)); }
touch "$tmp/app.apk"
root=/storage/emulated/0/Android/data/org.scarletnx.edenprobe/files/edenprobe

scenario() { # NAME SCENARIO [OPTIONS...]: runs it in $tmp/NAME, sets rc
    local name=$1 sc=$2
    shift 2
    mkdir -p "$tmp/$name/dev"
    [ "$sc" = installed ] && touch "$tmp/$name/dev/installed"
    FAKE_ADB=$tmp/$name/dev FAKE_SCENARIO=$sc FAKE_REPORT=$here/fixtures/report.jsonl ADB=$here/fake-adb \
        "$here/../tools/run-adb.sh" --serial fake --apk "$tmp/app.apk" --out "$tmp/$name/out" "$@" > "$tmp/$name/log" 2>&1
    rc=$?
}
calls() { grep -c -- "$2" "$tmp/$1/dev/calls"; }

scenario whole ok --mode "quick libc"
[ $rc = 0 ] && ok "whole: exit 0" || bad "whole: exit $rc: $(cat "$tmp/whole/log")"
cmp -s "$tmp/whole/out/report.jsonl" "$here/fixtures/report.jsonl" && ok "whole: the report pulled as it is" || bad "whole: the report differs"
[ "$(calls whole 'install -t')" = 1 ] && [ "$(calls whole "uninstall org.scarletnx.edenprobe")" = 1 ] && [ ! -e "$tmp/whole/dev/installed" ] &&
    ok "whole: installed once, uninstalled at the end" || bad "whole: install and uninstall: $(cat "$tmp/whole/dev/calls")"
[ "$(cat "$tmp/whole/dev/device$root/mode" 2> /dev/null)" = "quick libc machine=android@fake" ] &&
    ok "whole: the mode file holds --mode's words and the machine, with its Android release" ||
    bad "whole: mode file \"$(cat "$tmp/whole/dev/device$root/mode" 2> /dev/null)\""
grep -q "EDENPROBE DONE passed=1 failed=1" "$tmp/whole/log" && ok "whole: says the engine's counts" || bad "whole: $(cat "$tmp/whole/log")"
grep -q "meminfo of" "$tmp/whole/out/meminfo.txt" && ok "whole: meminfo saved" || bad "whole: no meminfo"

scenario dies die-once
[ $rc = 0 ] && [ "$(cat "$tmp/dies/dev/launches")" = 2 ] && ok "dies: launched again, exit 0" || bad "dies: exit $rc, launches $(cat "$tmp/dies/dev/launches")"
[ -s "$tmp/dies/out/exit-info-1.txt" ] && ok "dies: exit-info of launch 1 saved" || bad "dies: no exit-info-1.txt"
[ "$(cat "$tmp/dies/dev/device$root/mode" 2> /dev/null)" = "quick machine=android@fake" ] &&
    ok "dies: the mode file says quick without --mode, and the machine" || bad "dies: no quick mode file"
[ ! -e "$tmp/dies/dev/installed" ] && ok "dies: uninstalled" || bad "dies: still installed"

scenario hangs stall --stall 3
[ $rc = 0 ] && [ "$(calls hangs 'am force-stop')" -ge 1 ] && [ "$(cat "$tmp/hangs/dev/launches")" = 2 ] &&
    ok "hangs: stopped after 3 s silent and launched again" || bad "hangs: exit $rc, $(cat "$tmp/hangs/log")"

scenario nostorage nostorage-once --stall 60
[ $rc = 0 ] && [ "$(cat "$tmp/nostorage/dev/launches")" = 2 ] && ok "nostorage: launched again at once, exit 0" ||
    bad "nostorage: exit $rc, $(cat "$tmp/nostorage/log")"
grep -q "launch 1: the run did not start: getExternalFilesDir" "$tmp/nostorage/log" && ok "nostorage: says why" ||
    bad "nostorage: $(cat "$tmp/nostorage/log")"

scenario never never --launches 2
[ $rc = 1 ] && ok "never: exit 1" || bad "never: exit $rc"
grep -q "no EDENPROBE DONE after 2 launches" "$tmp/never/log" && ok "never: says so" || bad "never: $(cat "$tmp/never/log")"
[ "$(grep -c . "$tmp/never/out/report.jsonl" 2> /dev/null)" = 4 ] && ok "never: the partial report pulled" || bad "never: no partial report"
[ ! -e "$tmp/never/dev/installed" ] && ok "never: uninstalled" || bad "never: still installed"

scenario taken installed
[ $rc = 3 ] && ok "taken: exit 3" || bad "taken: exit $rc"
[ "$(calls taken 'install -t')" = 0 ] && [ "$(calls taken 'uninstall')" = 0 ] && [ -e "$tmp/taken/dev/installed" ] &&
    ok "taken: the installed package left alone" || bad "taken: $(cat "$tmp/taken/dev/calls")"

scenario named ok --mode "quick machine=console"
[ "$(cat "$tmp/named/dev/device$root/mode" 2> /dev/null)" = "quick machine=console" ] &&
    ok "named: words that name a machine keep it" || bad "named: mode file \"$(cat "$tmp/named/dev/device$root/mode" 2> /dev/null)\""
scenario quoted ok --mode "it's"
[ $rc = 2 ] && [ ! -e "$tmp/quoted/dev/calls" ] && ok "quoted: a mode with a quote refused before adb" || bad "quoted: exit $rc"

[ $fails = 0 ] && echo "test-run-adb: all passed" || echo "test-run-adb: $fails failed"
[ $fails = 0 ]
