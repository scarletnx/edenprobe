# Under an emulator

```sh
make run EMULATOR=/path/to/emulator
```

The report and its pictures go in the run's folder under `sdmc:/edenprobe/runs` ([the report](Report.md#where-a-run-leaves-its-report)).
`make run` points the emulator's `XDG_DATA_HOME`, `XDG_CONFIG_HOME` and `XDG_CACHE_HOME` at `build/xdg/` in this tree,
so it starts from the emulator's defaults every time and never reads or rewrites your settings; the report is then
at `build/xdg/data/eden/sdmc/edenprobe/runs/<hash>/report.jsonl`, which the target's last line prints, and `make
clean` removes it all. `XDG=/elsewhere` moves the tree.

The target runs `tools/run.py`. It first deletes the reports and pictures of every run folder, `last-run` and the
support suite's scratch folder, so no old report passes for the new one; the journals stay, so a run the program
left in the middle goes on.

**After a crash it starts the emulator again.** A launch that ends without a summary, crashed or past `TIMEOUT` (60
seconds, then killed ten later), is followed by another that goes on after the case the last one ended inside
([When the program ends in the middle](Crashes.md)), up to `RELAUNCHES` more (16). Each launch gets a line:

```
make run: launch 1 ended by signal 6 (SIGABRT) inside bcn case "every layer of a six-layer BC4 array keeps its own 2x2 and 1x1 levels" (the warm-up); launching again (2 of at most 17)
```

A launch that adds nothing to the journal but its own start ends the relaunching, and so does `nojournal`. At the
end the target prints the counts, the cases skipped as crashed, the ones this run ended inside, and the launches. A
missing emulator, or a run that never reaches its summary, ends it with a line saying which and a non-zero exit.
Stopping it by hand writes `stopped` into the journal, so the case under way is not taken for a crash.

**The crash list is per emulator binary**, in `build/xdg/crash-lists/`, put on the card before the first launch and
taken back after the last; a binary of another size or time starts its own. To try a case again, delete its line
there.

`make run` writes `quick` into the mode file. `MODE` sets it: `MODE="quick images"` keeps every picture,
`MODE="quick fatal"` runs the cases that may crash, and `MODE=` writes no file, so the start and result screens come
up, each counting down ten seconds.

**On a private display.** A closed window stops the emulator, which reads like a crash inside the case under way.
A run nobody watches can use Xvfb. There Vulkan finds no queue that can present, so use OpenGL on the software
rasteriser: `backend=0` under `[Renderer]` in `sdl2-config.ini`, in an `XDG` of its own. Not for the graphics family,
which needs a real backend.

```sh
Xvfb -displayfd 1 -screen 0 1280x720x24 -nolisten tcp > display &   # it picks a free number
while [ ! -s display ]; do sleep 0.2; done
DISPLAY=:$(cat display) make run MODE="quick audio" XDG=/somewhere/xvfb-xdg
kill %1
```

**On a null backend** every drawing case fails with every pixel zero: the backend draws nothing, as asked. Such a
run still checks the commands the graphics library sends, since that library runs on the emulated processor: a run
that reaches its summary loaded every shader, made every copy and cleared every rectangle.
