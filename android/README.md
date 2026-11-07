<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# androidprobe

One APK of short cases, each a behaviour of Android that an app relies on: the framework's Java API, resources,
bionic, JNI, EGL and GLES, sockets. It runs on a real Android device or emulator, the reference, and on
[ScarletNX](https://github.com/scarletnx/ScarletNX)'s Android layer (`scarletnx-android`). Both write the same report,
in edenprobe's format, so a case that passes on Android and fails on the layer names something the layer does
differently.

## Build

    ANDROID_HOME=~/Android/Sdk JAVA_HOME=/opt/android-studio/jbr GRADLE=gradle tools/build.sh OUT

The SDK needs platform 36, NDK 28.2.13676358 and CMake 3.31.6; Gradle 9 with JDK 21 or newer. Everything the build
writes goes under `OUT`; the APK is `OUT/gradle/outputs/apk/debug/androidprobe-debug.apk`, debuggable and signed with
the machine's debug key. It holds `arm64-v8a` (phones, the layer) and `x86_64` (the emulator) native code. Java and C
only: no Kotlin, no C++ runtime, no library.

## Run

On a device or an emulator, over adb:

    tools/run-adb.sh --serial SERIAL --apk APK --out DIR

The package must not be on the device yet. The script installs it, launches it, watches logcat, pulls the report
and uninstalls it, also when it fails. It sends no input to the device.

On ScarletNX's layer:

    tools/run-scarletnx.sh --driver BUILD/scarletnx-android --apk APK --out DIR [--java-home JDK] [--framework JAR]

It runs the driver headless, with an empty home of the run's own in `DIR/home`.

The probe runs every case as it starts and finishes its activity. A case that ends the process (a native crash, or a
hang the scripts stop) is reported `crashed` at the next launch, which goes on after it; both scripts launch again
until the report has its summary, 5 launches at most. `--help` gives each script's options.

## The report

`report.jsonl` in the app's external files (`runs/REVISION/`), one JSON object a line, as edenprobe writes it:
notes, a line per case, the summary last. A case line has edenprobe's `suite`, `name`, `tier`, `result` (`pass`,
`fail`, `skip`, `crashed`) and `detail`, and androidprobe's own: `id`, a key that never changes, `got`, what the case
measured (a return, an exception's class, pixels), `want`, what Android gives, and `us`, its time. From the ScarletNX
workspace:

    tools/edenprobe-diff.py LAYER/report.jsonl ANDROID/report.jsonl --cases
    tools/edenprobe-table.py android=ANDROID/report.jsonl layer=LAYER/report.jsonl

`tools/check-report.py REPORT` checks a report's format; both run scripts run it.

## Cases

`src/main/java/dev/dravee/androidprobe/Cases.java` lists them in the order a run takes them, a suite at a time:
`framework`, `resources`, `net`, `jni`, `bionic`, `gles`. Each case is a class of its own, loaded only when it runs,
so a class the layer cannot load fails that case alone. The native side is `src/main/cpp/`. The math case compares
libm with correctly rounded values that `tools/math-expected.py` computes.

## Tests

    tests/test-check-report.sh
    tests/test-run-adb.sh
    tests/test-run-scarletnx.sh
    tests/test-math-expected.sh
    tests/test-edenprobe-tools.sh TOOLS
    tools/build.sh OUT testDebugUnitTest

`test-run-adb.sh` and `test-run-scarletnx.sh` drive the scripts with a fake adb and a fake driver.
`test-edenprobe-tools.sh` runs the workspace's `edenprobe-diff.py` and `edenprobe-table.py` (in `TOOLS`) on the
fixture reports. The unit tests cover the report writer and its resume after a crash.

## License

GPL-3.0-or-later, as ScarletNX ([LICENSE](LICENSE)).
