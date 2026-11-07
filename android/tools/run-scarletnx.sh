#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# tools/run-scarletnx.sh --driver PROGRAM --apk APK --out DIR [--java-home DIR] [--framework JAR] [--timeout SECONDS]
#                        [--stall SECONDS] [--launches N] [-- DRIVER OPTION...]
# Runs the probe on ScarletNX's Android layer: PROGRAM is its scarletnx-android, run --headless with nothing on the
# display (DISPLAY and WAYLAND_DISPLAY unset) and an empty home of the run's own (XDG_DATA_HOME, XDG_CONFIG_HOME,
# XDG_STATE_HOME and XDG_CACHE_HOME in DIR/home), so the user's ScarletNX data is never read or written. --java-home and
# --framework go to the driver as its options. DIR gets each launch's log (launch-N.log) and the run's report.jsonl
# and journal. A launch that ends before the report's summary, prints no androidprobe line for --stall seconds (120),
# or runs past --timeout seconds (900), is stopped and launched again: the probe reports that case crashed and goes
# on, up to --launches (5). Exit 0 with a whole report, 1 a run that did not end whole, 2 refused (usage).
set -u
driver= apk= out= java_home= framework= timeout=900 stall=120 launches=5
usage() { sed -n '3,13s/^# \{0,1\}//p' "$0"; }
while [ $# -gt 0 ]; do
    case $1 in
    --) shift; break ;;
    --driver) driver=${2-} ;;
    --apk) apk=${2-} ;;
    --out) out=${2-} ;;
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
here=$(cd "$(dirname "$0")" && pwd)
log() { echo "run-scarletnx: $(date +%H:%M:%S) $*"; }
mkdir -p "$out/home" || exit 1
out=$(cd "$out" && pwd)
args=(--headless)
[ -n "$java_home" ] && args+=("--java-home=$java_home")
[ -n "$framework" ] && args+=("--framework=$framework")
args+=("$@" "$apk")
echo "${args[*]}" > "$out/driver-args.txt"

report= pid=
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

for launch in $(seq 1 "$launches"); do
    l="$out/launch-$launch.log"
    log "launch $launch: $driver ${args[*]}"
    env -u DISPLAY -u WAYLAND_DISPLAY XDG_DATA_HOME="$out/home/data" XDG_CONFIG_HOME="$out/home/config" \
        XDG_STATE_HOME="$out/home/state" XDG_CACHE_HOME="$out/home/cache" "$driver" "${args[@]}" > "$l" 2>&1 &
    pid=$!
    start=$(date +%s) last_lines=0 last_move=$start
    while :; do
        sleep 2
        done_line=$(grep -a 'androidprobe: done: ' "$l" | tail -n 1)
        if [ -n "$done_line" ]; then
            report=${done_line##*androidprobe: done: }
            report=${report%% (*}
            # the probe finishes its activity, which ends the process; give it 15 s
            for _ in $(seq 1 15); do kill -0 "$pid" 2> /dev/null || break; sleep 1; done
            if kill -0 "$pid" 2> /dev/null; then
                log "launch $launch: the process was still running 15 s after the run ended: stopping it"
                stop
            else
                wait "$pid"
                log "launch $launch: the process ended with status $?"
                pid=
            fi
            break 2
        fi
        now=$(date +%s)
        lines=$(grep -ac 'androidprobe: ' "$l")
        [ "$lines" != "$last_lines" ] && last_lines=$lines last_move=$now
        if ! kill -0 "$pid" 2> /dev/null; then
            wait "$pid"
            log "launch $launch: the process ended with status $? before the summary; its last lines:"
            pid=
            tail -n 5 "$l" | sed 's/^/    /'
            break
        fi
        if [ $((now - last_move)) -ge "$stall" ] || [ $((now - start)) -ge "$timeout" ]; then
            log "launch $launch: no progress for $((now - last_move)) s ($((now - start)) s in all): stopping it"
            stop
            break
        fi
    done
done
if [ -z "$report" ]; then
    log "no summary after $launches launches"
    report=$(find "$out/home" -path '*/runs/*/report.jsonl' | head -n 1)
fi
[ -n "$report" ] && [ -f "$report" ] || { log "no report.jsonl under $out/home"; exit 1; }
cp "$report" "$out/report.jsonl" && cp "$(dirname "$report")/journal" "$out/journal" || exit 1
log "report: $out/report.jsonl (from $report)"
python3 "$here/check-report.py" "$out/report.jsonl" || exit 1
grep -q '"type":"summary"' "$out/report.jsonl"
