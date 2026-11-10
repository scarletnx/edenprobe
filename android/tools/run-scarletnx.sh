#!/bin/bash
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# android/tools/run-scarletnx.sh --driver PROGRAM --apk APK --out DIR [--mode WORDS] [--java-home DIR] [--framework JAR]
#                                [--timeout SECONDS] [--stall SECONDS] [--launches N] [-- DRIVER OPTION...]
# Runs edenprobe's APK on ScarletNX's Android layer: PROGRAM is its scarletnx-android, run --headless with nothing on
# the display (DISPLAY and WAYLAND_DISPLAY unset) and an empty home of the run's own (XDG_DATA_HOME, XDG_CONFIG_HOME,
# XDG_STATE_HOME and XDG_CACHE_HOME in DIR/home), so the user's ScarletNX data is never read or written; the app's
# mode file there holds WORDS ("quick" when not given) and machine=scarletnx-android, unless WORDS name a machine.
# --java-home and --framework go to the driver as its options. The driver runs in DIR, so what a crash leaves (the
# JVM's hs_err_pid files) lands there with each launch's log (launch-N.log) and the run's report.jsonl and journal. A
# launch that ends before EDENPROBE DONE, prints no edenprobe line for --stall seconds (120), or runs past --timeout
# seconds (900), is stopped and launched again: the engine reports the case it ended inside as crashed and goes on, up
# to --launches (5). A launch this script stopped is said in the journal, `timeout <limit> <quiet>`, the limit it ran
# into and the seconds since the journal's last record, so the engine reports that case as timeout instead, off the
# crash list (docs/Crashes.md). Exit 0 with a whole report, 1 a run that did not end whole, 2 refused (usage).
set -u
driver= apk= out= mode=quick java_home= framework= timeout=900 stall=120 launches=5
usage() { sed -n '4,19s/^# \{0,1\}//p' "$0"; }
while [ $# -gt 0 ]; do
    case $1 in
    --) shift; break ;;
    --driver) driver=${2-} ;;
    --apk) apk=${2-} ;;
    --out) out=${2-} ;;
    --mode) mode=${2-} ;;
    --java-home) java_home=${2-} ;;
    --framework) framework=${2-} ;;
    --timeout) timeout=${2-} ;;
    --stall) stall=${2-} ;;
    --launches) launches=${2-} ;;
    -h | --help) usage; exit 0 ;;
    *) echo "run-scarletnx: unknown option $1 (--help)" >&2; exit 2 ;;
    esac
    [ $# -ge 2 ] || { echo "run-scarletnx: $1 needs a value" >&2; exit 2; }
    shift 2
done
for v in driver apk out; do [ -n "${!v}" ] || { echo "run-scarletnx: --$v is required (--help)" >&2; exit 2; }; done
for v in timeout stall launches; do
    case ${!v} in '' | *[!0-9]*) echo "run-scarletnx: --$v ${!v} is not a whole number" >&2; exit 2 ;; esac
done
[ -x "$driver" ] || { echo "run-scarletnx: no program at $driver" >&2; exit 2; }
[ -f "$apk" ] || { echo "run-scarletnx: no APK at $apk" >&2; exit 2; }
log() { echo "run-scarletnx: $(date +%H:%M:%S) $*"; }
mkdir -p "$out/home" || exit 1
out=$(cd "$out" && pwd)
# where the layer keeps the app's external files, getExternalFilesDir's: the engine's root under it
root=$out/home/data/scarletnx/android/data/org.scarletnx.edenprobe/external/files/edenprobe
# the machine for the ban list (docs/Crashes.md), unless the words name one: the driver says its build only once it runs,
# so no version
case " $mode " in
*" machine="*) ;;
*) mode="$mode machine=scarletnx-android" ;;
esac
mkdir -p "$root" && printf '%s\n' "$mode" > "$root/mode" || exit 1
# absolute, without resolving a link: the driver is a link whose name picks the program
driver=$(cd "$(dirname "$driver")" && pwd)/$(basename "$driver")
apk=$(cd "$(dirname "$apk")" && pwd)/$(basename "$apk")
args=(--headless)
[ -n "$java_home" ] && args+=("--java-home=$java_home")
[ -n "$framework" ] && args+=("--framework=$framework")
args+=("$@" "$apk")
echo "${args[*]}" > "$out/driver-args.txt"

done_line= pid=
stop() {
    [ -n "$pid" ] || return
    kill "$pid" 2> /dev/null
    for _ in 1 2 3 4 5; do kill -0 "$pid" 2> /dev/null || break; sleep 1; done
    kill -9 "$pid" 2> /dev/null
    wait "$pid" 2> /dev/null
    pid=
}
trap stop EXIT
trap 'exit 1' INT TERM
# Says in the journal of the folder last-run names that the launch was stopped at LIMIT seconds, on a line of its own:
# the engine then skips the case under way as timeout. Nothing when the run has no journal yet.
mark_timeout() { # LIMIT NOW
    local folder journal written
    folder=$(cat "$root/last-run" 2> /dev/null)
    case $folder in runs/?*) ;; *) return ;; esac
    journal=$root/$folder/journal
    [ -f "$journal" ] || return
    written=$(stat -c %Y "$journal" 2> /dev/null || stat -f %m "$journal")
    [ -z "$(tail -c 1 "$journal")" ] || echo >> "$journal"
    printf 'timeout %s %s\t\n' "$1" "$(($2 - written))" >> "$journal"
    log "launch $launch: the journal says timeout $1 $(($2 - written)), so the case under way is skipped as timeout, not as crashed"
}

for launch in $(seq 1 "$launches"); do
    l="$out/launch-$launch.log"
    log "launch $launch: $driver ${args[*]}"
    (cd "$out" && exec env -u DISPLAY -u WAYLAND_DISPLAY XDG_DATA_HOME="$out/home/data" XDG_CONFIG_HOME="$out/home/config" \
        XDG_STATE_HOME="$out/home/state" XDG_CACHE_HOME="$out/home/cache" "$driver" "${args[@]}") > "$l" 2>&1 &
    pid=$!
    start=$(date +%s) last_lines=0 last_move=$start
    while :; do
        sleep 2
        done_line=$(grep -a 'EDENPROBE DONE ' "$l" | tail -n 1)
        if [ -n "$done_line" ]; then
            # the activity finishes once the report is closed, which ends the process; give it 15 s
            for _ in $(seq 1 15); do kill -0 "$pid" 2> /dev/null || break; sleep 1; done
            if kill -0 "$pid" 2> /dev/null; then
                log "launch $launch: the process was still running 15 s after EDENPROBE DONE: stopping it"
                stop
            else
                wait "$pid"
                log "launch $launch: the process ended with status $?"
                pid=
            fi
            break 2
        fi
        now=$(date +%s)
        lines=$(grep -ac 'edenprobe' "$l")
        [ "$lines" != "$last_lines" ] && last_lines=$lines last_move=$now
        if ! kill -0 "$pid" 2> /dev/null; then
            wait "$pid"
            log "launch $launch: the process ended with status $? before EDENPROBE DONE; its last lines:"
            pid=
            tail -n 5 "$l" | sed 's/^/    /'
            break
        fi
        if [ $((now - last_move)) -ge "$stall" ] || [ $((now - start)) -ge "$timeout" ]; then
            log "launch $launch: no progress for $((now - last_move)) s ($((now - start)) s in all): stopping it"
            stop
            if [ $((now - last_move)) -ge "$stall" ]; then mark_timeout "$stall" "$now"; else mark_timeout "$timeout" "$now"; fi
            break
        fi
    done
done
if [ -n "$done_line" ]; then
    log "${done_line##*EDENPROBE DONE }"
else
    log "no EDENPROBE DONE after $launches launches: the report is not whole"
fi
# last-run names the folder the latest launch reported in, under the root
folder=$(cat "$root/last-run" 2> /dev/null)
case $folder in runs/?*) ;; *) log "$root/last-run names no run folder: \"$folder\""; exit 1 ;; esac
cp "$root/$folder/report.jsonl" "$out/report.jsonl" && cp "$root/$folder/journal" "$out/journal" || exit 1
log "report: $out/report.jsonl (from $root/$folder)"
[ -n "$done_line" ] || exit 1
tail -n 1 "$out/report.jsonl" | grep -q '"type":"summary"' || { log "the report does not end on its summary"; exit 1; }
