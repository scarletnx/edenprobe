# On Android

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
