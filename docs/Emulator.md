# Under an emulator

```sh
make run EMULATOR=/path/to/emulator
```

The report and its pictures go in the run's folder under `sdmc:/edenprobe/runs` ([the report](Report.md#where-a-run-leaves-its-report)).
`make run` points the emulator's `XDG_DATA_HOME`, `XDG_CONFIG_HOME` and `XDG_CACHE_HOME` at `build/xdg/` in this tree,
so it never reads or rewrites your settings; the report is then at
`build/xdg/data/eden/sdmc/edenprobe/runs/<hash>/report.jsonl`, which the target's last line prints, and `make clean`
removes it all. `XDG=/elsewhere` moves the tree.

**The settings are per emulator binary**, in `build/xdg/settings/<binary>/`, its `XDG_CONFIG_HOME`. A frontend writes
its settings on the way out, so with one folder for every build the second build would start from what the first
wrote, not from its own defaults. A binary's first run makes its folder empty and the emulator starts from its
defaults; its later runs keep what it, or you, wrote there. A rebuilt binary, another size or modification time, starts
a folder of its own, as its crash list does. The run's first lines name the folder:

```
make run: settings: .../build/xdg/settings/eden-cli-5c1e0a9f3b21, this binary's own, made empty: the emulator starts from its defaults
```

`build/xdg/config`, every binary's settings before, is no longer read and stays where it is; the run says so while it
is there.

The target runs `tools/run.py`. It first deletes the reports, pictures and emulator logs of every run folder,
`last-run` and the support suite's scratch folder, so no old report passes for the new one; the journals stay, so a
run the program left in the middle goes on.

**After a crash it starts the emulator again.** A launch that ends without a summary, crashed or past `TIMEOUT` (60
seconds, then killed ten later), is followed by another that goes on after the case the last one ended inside
([When the program ends in the middle](Crashes.md)), up to `RELAUNCHES` more (16). Each launch gets a line:

```
make run: launch 1 ended by signal 6 (SIGABRT) inside bcn case "every layer of a six-layer BC4 array keeps its own 2x2 and 1x1 levels" (the warm-up); launching again (2 of at most 17)
```

A launch stopped at `TIMEOUT` inside a case is said in the journal, so the next launch skips that case as `timeout`,
not as `crashed`, and keeps it off the crash list ([a launch stopped at its time
limit](Crashes.md#a-launch-stopped-at-its-time-limit)):

```
make run: launch 1 was stopped after 60 s inside tex8k case "a 256x256 region of the 8192x8192 BC7 texture copied out to a buffer holds its bytes", 58 s after the journal last grew: the journal says timeout 60 58, so the next launch skips that case as timeout, not as crashed; launching again (2 of at most 17)
```

A launch that adds nothing to the journal but its own start ends the relaunching, and so does `nojournal`. At the
end the target prints the counts, the cases skipped as crashed, the ones this run ended inside, the ones a launch was
stopped inside at its limit, and the launches. A
missing emulator, or a run that never reaches its summary, ends it with a line saying which and a non-zero exit.
Stopping it by hand writes `stopped` into the journal, so the case under way is not taken for a crash.

**A lost graphics device ends the run.** When the emulator's own log says the host's driver dropped its device (Eden's
`Device loss occurred!`, the Vulkan result named `DEVICE_LOST` or `DeviceLost`), `make run` stops it there and
launches nothing more: what an emulator draws after a loss is not its own work, and on some machines the next launch
finds the display itself reset. It prints the line and how many case lines came after it, which are no results, adds
the same as the report's last note, and exits 1 with no summary. The probe's own lines never count, so a case whose
detail quotes a loss ends nothing.

```
make run: launch 1: the emulator said its graphics device was lost inside tex8k case "the copy engine uploads the 8192x8192 ASTC 4x4 texture, and a draw fetches 4096 texels across it": "[  41.107514] Render.Vulkan <Critical> video_core/vulkan_common/vulkan_device.cpp:898:ReportLoss: Device loss occurred!"; stopped there and not launched again, since what an emulator draws after a device loss is not its own work: 2 case lines came after that line and are no results; the report's last line says so; that case goes on the crash list at the next run
```

The case the launch was inside goes on the crash list when the next run starts, as any case a launch ended inside, so
that run skips it as `crashed`; `MODE="quick retry"` runs it again.

**What the emulator said is kept beside the report.** A failure can have its cause only in the emulator's log: every
pixel of a case wrong because a shader was never translated, a video job that wrote nothing because it never ran. So
`make run` keeps the emulator's output in the run's folder, colours taken off:

```
runs/5fd6ec1/
  report.jsonl
  emulator.log                 everything the emulator printed, a launch after another
  logs/<suite>/<case>.log      what it said while that case ran, for each case that did not pass
```

`logs/intmath/a-left-shift-by-31-32-and-33-in-a-register-clamps-the-count-to-32.log`, from a `notime` run under Eden:

```
intmath: a left shift by 31, 32 and 33 in a register clamps the count to 32: fail
== launch 1, up to its line in the report
 [  45.629048] Render.Vulkan <Error> video_core/renderer_vulkan/vk_pipeline_cache.cpp:930:CreateGraphicsPipeline: Instruction IMUL32I is not implemented is not implemented
```

A case's file is named as its pictures are ([Failure pictures](FailurePictures.md)) and holds the emulator's own lines,
never the probe's, under a heading for each pass and launch they came in. The probe's lines in the output are what
cuts it: a case's report line closes what came before it; in the passes a timed suite makes before it reports, the
breadcrumb before each case does ([Time per case](TimePerCase.md#when-a-rehearsal-hangs)), the n-th of a pass being
the suite's n-th case; and what a launch said after the probe's last line belongs to the case the journal says it
ended inside, which is where a crash's reason is. A case the emulator said nothing during gets no file, and neither
does one that passed: `emulator.log` has it all. An emulator says some things once: a shader it cannot translate when
the first case draws with it, and nothing for the cases after that fail the same way, so a suite's failures with no
file are read beside the ones that have one. What a suite's first case gets includes the device before it closing.
An emulator that does not log the debug channel leaves nothing to cut by, which `make run` says; `--log FILE` on
`tools/run.py` still keeps every line as it came, colours and all.

**The crash list is per emulator binary**, in `build/xdg/crash-lists/`, put on the card before the first launch and
taken back after the last; a binary of another size or time starts its own. To try a case again, delete its line
there. To keep a whole suite out instead, `MODE="quick skip=tex8k"` ([Leaving a suite out](Crashes.md#leaving-a-suite-out)):
the list is not for a person to fill.

`make run` writes `quick` into the mode file. `MODE` sets it: `MODE="quick images"` keeps every picture, `MODE="quick
retry"` runs the crash list's cases again, and `MODE=` writes no file, so the start and result screens come up, each
counting down ten seconds. Beside `MODE` it writes the machine for the [ban list](Crashes.md#the-ban-list),
`machine=eden@<commit>` from `eden-cli --version` when the emulator's name starts with eden, `machine=ryujinx` for
Ryujinx; `MACHINE=<name>[@<version>]` names another, and an emulator of another name gets none, which `make run` says.

After the machine it writes `vram=<MiB>`, the graphics memory this computer's card says it has of its own: the largest
`mem_info_vram_total` under `/sys/class/drm`, a file the amdgpu driver writes, and where no card writes one, the largest
`memory.total` `nvidia-smi` gives, since NVIDIA's own driver writes no such file. With less than 4096 the program does not
try [tex8k](families/Graphics.md#tex8k)'s 8192x8192 textures, which on a handheld took the graphics device and the
display with it. `MODE="quick vram=8192"` says another number and is kept; where neither says one (another driver,
another system), no word is written, `make run` says so, and the textures are tried.

**Several rounds, for the times.** `make run ROUNDS=5` makes the whole run five times and prints how far apart each
suite's times came, with every case's median and spread in `rounds/times.tsv` in the run's folder and each round's
report beside it ([Time per case](TimePerCase.md#several-rounds-of-one-build)).

**On a private display.** A closed window stops the emulator, which reads like a crash inside the case under way.
A run nobody watches can use Xvfb. There Vulkan finds no queue that can present, so use OpenGL on the software
rasteriser: `backend=0` under `[Renderer]` in the binary's `eden/sdl2-config.ini`, in an `XDG` of its own, under
`settings/<binary>/`, the folder the run names (a first run there makes it). Not for the graphics family, which needs
a real backend.

```sh
Xvfb -displayfd 1 -screen 0 1280x720x24 -nolisten tcp > display &   # it picks a free number
while [ ! -s display ]; do sleep 0.2; done
DISPLAY=:$(cat display) make run MODE="quick audio" XDG=/somewhere/xvfb-xdg
kill %1
```

**On a null backend** every drawing case fails with every pixel zero: the backend draws nothing, as asked. Such a
run still checks the commands the graphics library sends, since that library runs on the emulated processor: a run
that reaches its summary loaded every shader, made every copy and cleared every rectangle.
