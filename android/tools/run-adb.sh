#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# tools/run-adb.sh --serial SERIAL --apk APK --out DIR [--timeout SECONDS] [--stall SECONDS] [--launches N]
# Runs the probe on a device or emulator over adb and leaves it as it was: the package must not be installed (it is
# refused, never replaced), the APK is installed, launched, watched, its report pulled, and the package uninstalled
# on every way out. No tap is sent. DIR gets: device.txt, logcat.txt (from the install on), meminfo.txt, for each
# launch that died its exit-info-N.txt, and the run's report.jsonl and journal.
# A launch that ends before the report's summary, or prints no androidprobe line for --stall seconds (60), or runs
# past --timeout seconds (600), is stopped and launched again: the probe reports that case crashed and goes on, up to
# --launches (5). ADB names adb (adb on PATH when not set). Exit 0 with a whole report, 1 a run that did not end
# whole, 2 refused (usage), 3 the package is already on the device.
set -u
pkg=dev.dravee.androidprobe
serial= apk= out= timeout=600 stall=60 launches=5
usage() { sed -n '3,12s/^# \{0,1\}//p' "$0"; }
while [ $# -gt 0 ]; do
    case $1 in
    --serial) serial=${2-} ;;
    --apk) apk=${2-} ;;
    --out) out=${2-} ;;
    --timeout) timeout=${2-} ;;
    --stall) stall=${2-} ;;
    --launches) launches=${2-} ;;
    -h | --help) usage; exit 0 ;;
    *) echo "run-adb: unknown option $1 (--help)" >&2; exit 2 ;;
    esac
    [ $# -ge 2 ] || { echo "run-adb: $1 needs a value" >&2; exit 2; }
    shift 2
done
for v in serial apk out; do [ -n "${!v}" ] || { echo "run-adb: --$v is required (--help)" >&2; exit 2; }; done
for v in timeout stall launches; do
    case ${!v} in '' | *[!0-9]*) echo "run-adb: --$v ${!v} is not a whole number" >&2; exit 2 ;; esac
done
[ -f "$apk" ] || { echo "run-adb: no APK at $apk" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
adb() { "${ADB:-adb}" -s "$serial" "$@"; }
log() { echo "run-adb: $(date +%H:%M:%S) $*"; }

state=$(adb get-state 2>&1)
[ "$state" = device ] || { echo "run-adb: $serial is not ready: $state" >&2; exit 1; }
if adb shell pm list packages "$pkg" | tr -d '\r' | grep -qx "package:$pkg"; then
    echo "run-adb: $pkg is already installed on $serial: it is left as it is; uninstall it first to run" >&2
    exit 3
fi
mkdir -p "$out" || exit 1
{
    echo "serial $serial"
    echo "model $(adb shell getprop ro.product.model | tr -d '\r')"
    echo "android $(adb shell getprop ro.build.version.release | tr -d '\r') api $(adb shell getprop ro.build.version.sdk | tr -d '\r')"
    echo "abis $(adb shell getprop ro.product.cpu.abilist | tr -d '\r')"
} > "$out/device.txt"

installed=0 logcat_pid=
cleanup() {
    if [ "$installed" = 1 ]; then
        adb shell am force-stop "$pkg" > /dev/null 2>&1
        adb uninstall "$pkg" > "$out/uninstall.txt" 2>&1
        if adb shell pm list packages "$pkg" | tr -d '\r' | grep -qx "package:$pkg"; then
            log "$pkg is STILL installed on $serial: uninstall it by hand (adb -s $serial uninstall $pkg)"
        else
            log "uninstalled $pkg"
        fi
    fi
    [ -n "$logcat_pid" ] && kill "$logcat_pid" 2> /dev/null
}
trap cleanup EXIT
trap 'exit 1' INT TERM

# the log from its last line on: what the device logged before this run stays out of DIR
adb logcat -v threadtime -T 1 > "$out/logcat.txt" 2>&1 &
logcat_pid=$!
log "installing $apk on $serial"
installed=1
adb install -t "$apk" > "$out/install.txt" 2>&1 || { cat "$out/install.txt"; exit 1; }

report=
for launch in $(seq 1 "$launches"); do
    log "launch $launch"
    adb shell am start -W -n "$pkg/.ProbeActivity" > "$out/start-$launch.txt" 2>&1
    adb shell dumpsys meminfo "$pkg" > "$out/meminfo.txt" 2>&1
    start=$(date +%s) last_lines=0 last_move=$start
    while :; do
        sleep 2
        done_line=$(grep -a ' androidprobe: done: ' "$out/logcat.txt" | tail -n 1)
        if [ -n "$done_line" ]; then
            report=${done_line##* androidprobe: done: }
            report=${report%% (*}
            report=$(printf '%s' "$report" | tr -d '\r')
            break 2
        fi
        now=$(date +%s)
        lines=$(grep -ac ' androidprobe: ' "$out/logcat.txt")
        [ "$lines" != "$last_lines" ] && last_lines=$lines last_move=$now
        if [ -z "$(adb shell pidof "$pkg" | tr -d '\r')" ]; then
            log "launch $launch: the process ended before the summary"
            adb shell dumpsys activity exit-info "$pkg" > "$out/exit-info-$launch.txt" 2>&1
            break
        fi
        if [ $((now - last_move)) -ge "$stall" ] || [ $((now - start)) -ge "$timeout" ]; then
            log "launch $launch: no progress for $((now - last_move)) s ($((now - start)) s in all): stopping it"
            adb shell dumpsys activity exit-info "$pkg" > "$out/exit-info-$launch.txt" 2>&1
            adb shell am force-stop "$pkg"
            break
        fi
    done
done
[ -n "$report" ] || { log "no summary after $launches launches: the report is not pulled whole"; }
dir=${report%/report.jsonl}
[ -n "$report" ] || dir=$(grep -a ' androidprobe: run folder: ' "$out/logcat.txt" | tail -n 1 | sed 's/.* androidprobe: run folder: //' | tr -d '\r')
[ -n "$dir" ] || { log "the log names no run folder"; exit 1; }
for f in report.jsonl journal; do
    adb pull "$dir/$f" "$out/$f" > /dev/null 2>&1 || adb exec-out run-as "$pkg" cat "$dir/$f" > "$out/$f" ||
        { log "cannot pull $dir/$f"; exit 1; }
done
log "report: $out/report.jsonl"
python3 "$here/check-report.py" "$out/report.jsonl" || exit 1
[ -n "$report" ] || exit 1
