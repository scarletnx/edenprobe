# Running it on Android

The same `.nro` runs under the Android build of an emulator; the report is the same file under
the app's own memory card. For Eden, whose package is `dev.eden.eden_emulator` (a suffix such as
`.relWithDebInfo` on other build types; `adb shell pm list packages | grep eden` names it), one
run is:

```sh
adb install -r Eden.apk && adb push edenprobe.nro /sdcard/edenprobe.nro && \
adb shell 'd=/sdcard/Android/data/dev.eden.eden_emulator/files/sdmc/edenprobe; mkdir -p $d && rm -f $d/runs/*/report.jsonl && echo "quick label=phone" > $d/mode'
# open /sdcard/edenprobe.nro from the app, wait for it to leave by itself, then:
d=/sdcard/Android/data/dev.eden.eden_emulator/files/sdmc/edenprobe
adb pull "$d/$(adb shell cat $d/last-run | tr -d '\r')/report.jsonl" report-android.jsonl
```

Deleting the reports makes the next launch start a run rather than go on with one left in the
middle. Two builds of one package share that directory: each build of the probe reports in its own
folder, and a label per emulator build (`label=eden-nightly`) keeps two of those apart too. When the
app stops in the middle of a run, open the program again: it goes on with the run (see [When the
program ends in the middle](Crashes.md)), and the report is whole once it ends on its summary. The
device and driver are in the report's first lines only as far as the emulator logs them; say which
phone and which driver next to the file.
