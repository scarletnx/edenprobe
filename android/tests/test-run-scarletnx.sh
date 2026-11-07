#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# tests/test-run-scarletnx.sh: tools/run-scarletnx.sh with a fake driver (tests/fake-driver): headless, no display,
# an empty home inside the run folder, the driver's options passed, a launch that dies and one that hangs launched
# again, its crash files in the run folder, relative paths taken from where it was called, and the report copied out.
set -u
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -r "$tmp"' EXIT
fails=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; fails=$((fails + 1)); }
touch "$tmp/app.apk"

scenario() { # NAME SCENARIO [OPTIONS...]
    local name=$1 sc=$2
    shift 2
    mkdir -p "$tmp/$name/state"
    (cd "$tmp" && DISPLAY=:99 WAYLAND_DISPLAY=wayland-9 FAKE_STATE=$tmp/$name/state FAKE_SCENARIO=$sc FAKE_REPORT=$here/fixtures/ref.jsonl \
        "$here/../tools/run-scarletnx.sh" --driver "$here/fake-driver" --apk app.apk --out "$name/out" "$@") \
        > "$tmp/$name/log" 2>&1
    rc=$?
}

scenario whole ok --java-home /jdk --framework /fw.jar -- --log-level=I
[ $rc = 0 ] && ok "whole: exit 0" || bad "whole: exit $rc: $(cat "$tmp/whole/log")"
cmp -s "$tmp/whole/out/report.jsonl" "$here/fixtures/ref.jsonl" && ok "whole: the report copied as it is" || bad "whole: the report differs"
grep -qx "args --headless --java-home=/jdk --framework=/fw.jar --log-level=I $tmp/app.apk" "$tmp/whole/state/seen-1" &&
    ok "whole: the driver's options" || bad "whole: $(head -n 1 "$tmp/whole/state/seen-1")"
grep -qx "DISPLAY=unset WAYLAND_DISPLAY=unset" "$tmp/whole/state/seen-1" && ok "whole: no display" || bad "whole: $(sed -n 2p "$tmp/whole/state/seen-1")"
grep -qx "XDG_DATA_HOME=$tmp/whole/out/home/data" "$tmp/whole/state/seen-1" && ok "whole: the run's own home" || bad "whole: $(sed -n 3p "$tmp/whole/state/seen-1")"

scenario dies die-once
[ $rc = 0 ] && [ "$(cat "$tmp/dies/state/launches")" = 2 ] && ok "dies: launched again, exit 0" || bad "dies: exit $rc: $(cat "$tmp/dies/log")"
grep -q "FATAL EXCEPTION" "$tmp/dies/out/launch-1.log" && ok "dies: launch 1's log kept" || bad "dies: no launch-1.log"
ls "$tmp/dies/out"/hs_err_pid*.log > /dev/null 2>&1 && ! ls "$tmp"/hs_err_pid*.log > /dev/null 2>&1 &&
    ok "dies: the crash's hs_err file in the run folder" || bad "dies: hs_err not in the run folder: $(ls "$tmp" "$tmp/dies/out")"

scenario hangs stall --stall 3
[ $rc = 0 ] && [ "$(cat "$tmp/hangs/state/launches")" = 2 ] && ok "hangs: stopped after 3 s silent and launched again" ||
    bad "hangs: exit $rc: $(cat "$tmp/hangs/log")"

[ $fails = 0 ] && echo "test-run-scarletnx: all passed" || echo "test-run-scarletnx: $fails failed"
[ $fails = 0 ]
