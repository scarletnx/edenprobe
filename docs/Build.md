# Building

For the console, with the toolchain installed:

```sh
export DEVKITPRO=/opt/devkitpro
make
```

That produces `edenprobe.nro`. The same file runs on hardware and in an emulator, unchanged: two
reports are only comparable if the binary is identical.

The host build compiles the parts that touch no console and runs them on the machine you are sitting
at:

```sh
make -f host.mk host-run
```

It runs the three judge suites, so a broken comparator shows in a second instead of after a boot,
along with what is written and drawn around a verdict: the picture writer against a fixture it
parses back, the screens and the navigation between them against frames composed in ordinary memory
from a fixture shaped like a real run, the store the screens read, the mode file's words, and the
time a case reports. Last it runs the tests of the tools, `tools/perf.py`, `tools/perf-self.py`,
`tools/run.py` (with a script standing in for the emulator, crashing, hanging and finishing as each
test asks),
`tools/validate.py`, `tools/astc-blocks.py`, `tools/vic-expect.py` and `tools/blit2d-expect.py`
(each against the table its reference cases hold) and `tools/vp9-jobs.py` (the nvdec suite's jobs
checked against the frames they carry, with neither FFmpeg nor the vectors), which need Python 3
and nothing else. Writing those jobs again needs both: see [The nvdec suite](families/Nvdec.md).
Then the tests of the CI's scripts in `.github/scripts`, against a fake GitHub and a fake Discord,
which also want git, and bash and jq for the one that comments on a pull request (without those
two its tests are skipped, and say so).

To look at the screens without a console, name an existing directory in `EDENPROBE_FRAMES`:

```sh
EDENPROBE_FRAMES=/tmp/frames make -f host.mk host-run
```

The screen cases then also write each frame of their walk there as a PAM file: the overview, a
failing graphics group, one of its cases on its pictures and alone on `judged`, the group again, a
net case's detail page, a net group, the start screen with its menu, the screen that asks whether
to go on with a run and the running screen; `magick 1-overview.pam 1-overview.png` makes a PNG of one.
The frames are never read, so they change no verdict.

## Commit messages

Commit messages have a convention too: `edenprobe: what changed`, one to three lines of intent, a
`Change-Id` (the hook writes one when there is none), one Signed-off-by that is also the author and
the committer, ASCII, no merges.
`git config core.hooksPath hooks` once per clone turns the check on; `sh hooks/test-commit-msg.sh`
shows what it accepts and `--history <range>` runs it over existing commits.
