# Build

## The NRO

```sh
export DEVKITPRO=/opt/devkitpro
make
```

`edenprobe.nro` runs unchanged on a console and in an emulator; two reports compare only when the NRO is the same.

## The APK

```sh
gradle -p android assembleDebug -Pedenprobe.out=/tmp/edenprobe-apk
```

With the Android SDK in `ANDROID_HOME` and a JDK 17 or newer in `JAVA_HOME`; what it needs and where the APK lands is
in [On Android](Android.md#build-it).

## The host build

```sh
make -f host.mk host-run
```

It builds what touches no console and runs it here: the three judge suites, the screens and their navigation (on
frames composed in memory from a fixture), the picture writer, the mode file's words and the time a case reports,
and the [libc](families/Libc.md) family's cases on this machine's C library. Then the tools' tests (`tools/perf.py`,
`tools/run.py`, `tools/validate.py`, `tools/libc-math.py`, `tools/astc-blocks.py`,
`tools/astc-images.py`, `tools/vic-expect.py`, `tools/blit2d-expect.py` and `tools/vp9-jobs.py`), which need
Python 3, the CI scripts' tests in `.github/scripts`, against a fake GitHub and a fake Discord, which also need
git (and bash and jq for the pull request comment's tests, skipped and said so without them), and the APK's runners'
(`android/tests`), against a fake adb and a fake ScarletNX driver, in bash. Writing the nvdec
suite's jobs again needs FFmpeg and libvpx's vectors: see [nvdec](families/Nvdec.md).

To see the screens without a console, name an existing folder:

```sh
EDENPROBE_FRAMES=/tmp/frames make -f host.mk host-run
```

The screen cases then write each frame they walk through there as a PAM file (`magick 1-overview.pam
1-overview.png`). Nothing reads them back, so they change no verdict.

## Commit messages

`edenprobe: what changed`, one to three lines of intent, a `Change-Id` (the hook writes one), one Signed-off-by
that is also the author and the committer, ASCII, no merges. `git config core.hooksPath hooks` turns the check on;
`sh hooks/test-commit-msg.sh` shows what it accepts, and `--history <range>` checks existing commits.
