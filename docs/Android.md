# On Android

## Index

* [The APK](#the-apk)
  * [Build it](#build-it)
  * [Run it on a device or the emulator](#run-it-on-a-device-or-the-emulator)
  * [Run it on ScarletNX's Android layer](#run-it-on-scarletnxs-android-layer)
  * [What it runs](#what-it-runs)
* [The NRO in an emulator's Android build](#the-nro-in-an-emulators-android-build)

Two ways: edenprobe's own APK asks Android itself, and the NRO asks an emulator that runs on Android.

## The APK

`android/` is edenprobe's Android frontend ([Frontends](Frontends.md)): one APK, package `org.scarletnx.edenprobe`,
whose native library holds the engine and the libc family's cases, the same C the NRO runs, beside Android's own
cases. It runs on a device or the emulator, the reference, and on ScarletNX's Android layer (`scarletnx-android`),
and both write the same report, so a case that passes on Android and fails on the layer names something the layer
does differently.

### Build it

```sh
ANDROID_HOME=~/Android/Sdk JAVA_HOME=/path/to/jdk gradle -p android assembleDebug -Pedenprobe.out=/tmp/edenprobe-apk
```

The SDK needs platform 36, NDK 28.2.13676358 and CMake 3.31.6; Gradle 9 on JDK 17 or newer. `-Pedenprobe.out` puts
everything the build writes under that folder (without it, `android/build`); the APK is
`gradle/outputs/apk/debug/edenprobe-debug.apk` there, debuggable and signed with the machine's debug key, with the
native library for `arm64-v8a` (phones, the layer) and `x86_64` (the emulator). Java and C only: no Kotlin, no C++
runtime, no library. The build names itself from `VERSION` and git as the NRO's does ([Versions](Versions.md)), so a
report's folder is the commit's.

### Run it on a device or the emulator

```sh
android/tools/run-adb.sh --serial SERIAL --apk edenprobe-debug.apk --out DIR [--mode "quick libc"]
```

The package must not be on the device yet: the script installs it, launches it with the words for its mode file
(`quick` when `--mode` is not given) and `machine=android@<its Android release>` for the [ban
list](Crashes.md#the-ban-list), unless the words name a machine, watches logcat, pulls the report and the journal and
uninstalls it, also when it fails. It sends no input. A launch that ends before `EDENPROBE DONE`, goes silent, or says
the run did not start (a launch right after a boot can, before the app has its storage) is launched again, and the
engine goes on after the case it ended inside, as on the console ([When the program ends in the middle](Crashes.md)).
One it stopped for going silent (`--stall`) or running past `--timeout` is said in the run's journal on the device,
so that case is skipped as a timeout rather than a crash ([A launch stopped at its time
limit](Crashes.md#a-launch-stopped-at-its-time-limit)).

By hand: `adb shell am start -n org.scarletnx.edenprobe/.ProbeActivity --es mode "quick libc"` launches it with
those words, which the app writes to its mode file first (adb's shell cannot write the app's folder before the app
has run); without the extra it reads the file as it is. The root is the app's external files,
`/storage/emulated/0/Android/data/org.scarletnx.edenprobe/files/edenprobe/`, with `mode` in it and the report under
`runs/` as on the console's card ([The report](Report.md)); `last-run` names the folder. Every line also goes to logcat under the tag `edenprobe`, the last one
`EDENPROBE DONE` with the counts.

### Run it on ScarletNX's Android layer

```sh
android/tools/run-scarletnx.sh --driver BUILD/scarletnx-android --apk edenprobe-debug.apk --out DIR [--mode WORDS]
```

It runs the driver headless with an empty home of the run's own in `DIR/home`, the mode file in the app's place there,
with `machine=scarletnx-android` beside the words unless they name a machine, launches it again after a crash as
`run-adb.sh` does, and copies the report out. `--java-home` and `--framework` go to the driver.

### What it runs

The judge first, as everywhere; then, in a run whose mode file names no family, every family Android has, since
none needs a person, a server or sound:

| family | suites | what they ask |
|---|---|---|
| `libc` | libc, system, socket ([libc](families/Libc.md)); bionic | the C library as every frontend has it; then bionic's own words for what the standard leaves open, the loader, `mmap`, `/proc/self`, signals, properties |
| `graphics` | gles | EGL, and GLES 1.1, 2 and 3 drawing read back by pixels |
| `net` | net | Java sockets on loopback, and names that never resolve, in Java and through bionic's resolver |
| `jni` | jni | `JNI_OnLoad`, both bindings, `FindClass` from two kinds of thread, strings, exceptions, arrays, calls |
| `framework` | framework, resources | the framework's Java API an app calls, and its resources: strings, plurals, typed values, binary XML, a layout, assets |

The Java cases report through the engine: each says it starts before its work and how it ended after, so a case
that ends the process is the one the next launch skips. Android has no screens: a run whose mode file is not quick
says so in a note and leaves once its report is closed.

## The NRO in an emulator's Android build

The same NRO runs in an emulator's Android build, and the report is under the app's own SD card. For Eden (package
`dev.eden.eden_emulator`; other build types add a suffix, which `adb shell pm list packages | grep eden` shows):

```sh
adb install -r Eden.apk && adb push edenprobe.nro /sdcard/edenprobe.nro && \
adb shell 'd=/sdcard/Android/data/dev.eden.eden_emulator/files/sdmc/edenprobe; mkdir -p $d && rm -f $d/runs/*/report.jsonl && echo "quick label=phone" > $d/mode'
# open /sdcard/edenprobe.nro from the app, wait for it to leave by itself, then:
d=/sdcard/Android/data/dev.eden.eden_emulator/files/sdmc/edenprobe
adb pull "$d/$(adb shell cat $d/last-run | tr -d '\r')/report.jsonl" report-android.jsonl
```

Deleting the reports makes the next launch start a run instead of going on with one. Two builds of one package
share the folder: a label per emulator build (`label=eden-nightly`) keeps their reports apart. When the app stops in
the middle of a run, open the program again and it goes on ([When the program ends in the middle](Crashes.md)); the
report is whole once it ends on its summary. Write down the phone and the driver beside the file: the report has
them only as far as the emulator logs them.
