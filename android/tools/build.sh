#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# tools/build.sh OUT [TASK...]: builds the probe with Gradle, everything it writes under OUT (the default task is
# assembleDebug: OUT/gradle/outputs/apk/debug/androidprobe-debug.apk, signed with this machine's debug key).
# ANDROID_HOME names the SDK (with NDK 28.2.13676358 and CMake 3.31.6), JAVA_HOME a JDK 21 or newer, GRADLE a
# Gradle 9 (gradle on PATH when not set).
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
if [ $# -lt 1 ] || [ "$1" = -h ] || [ "$1" = --help ]; then
    sed -n '2,6s/^# \{0,1\}//p' "$0"
    [ $# -ge 1 ] && exit 0
    exit 2
fi
out=$1
shift
[ $# -gt 0 ] || set -- assembleDebug
: "${ANDROID_HOME:?ANDROID_HOME must name the Android SDK}"
: "${JAVA_HOME:?JAVA_HOME must name a JDK 21 or newer}"
mkdir -p "$out"
export ANDROID_HOME JAVA_HOME
"${GRADLE:-gradle}" -p "$here" --project-cache-dir "$out/gradle-cache" -Pandroidprobe.out="$out" "$@"
case " $* " in
*" assembleDebug "*) echo "apk: $out/gradle/outputs/apk/debug/androidprobe-debug.apk" ;;
esac
