# Running it under an emulator

```sh
make run
```

`EMULATOR` points at the binary to test: `make run EMULATOR=/path/to/emulator`. With nothing given
it is an eden-cli build kept in `~/WorkDir/accord/bin`.

The run leaves `report.jsonl`, and `images/` beside it, in its folder under `sdmc:/edenprobe/runs`
(see [Where a run leaves its report](Report.md#where-a-run-leaves-its-report)): the memory card on a
console, and on a desktop a directory the emulator keeps under `$XDG_DATA_HOME`, beside its settings
and caches under `$XDG_CONFIG_HOME` and `$XDG_CACHE_HOME`. `make run` points all three at
`build/xdg/` inside this tree, so the report is at
`build/xdg/data/eden/sdmc/edenprobe/runs/<hash>/report.jsonl` (the target's last line prints it) and
`make clean` takes the lot away. That is not tidiness: a frontend saves its settings when it exits,
so a run pointed at your own directories reads your settings and may rewrite them, while one pointed
here starts from the emulator's defaults every time, which also makes two runs comparable. An
emulator with a different layout puts the same names under whatever it calls its memory card.

The target runs `tools/run.py`, which first deletes the report and the pictures in every run
folder, `last-run` and the support suite's scratch directory, so a run that never started cannot
leave an old report looking like its own, and a green run cannot sit beside the pictures of an
earlier failing one. It reads the report and the journal in the folder the program then names in
`last-run`. The journals stay: the program reads its own, starts a new run because the report is
gone, and lists a case the last launch of an earlier run ended inside.

**It starts the emulator again after a crash.** A launch that ends with no summary in the report,
crashed or stopped at its time limit (`TIMEOUT`, 60 seconds, then killed ten after that), is
followed by another, which goes on with the run after the case the last one ended inside (see [When
the program ends in the middle](Crashes.md)), up to `RELAUNCHES` more (16). Each launch gets a line:

```
make run: launch 1 ended by signal 6 (SIGABRT) inside bcn case "every layer of a six-layer BC4 array keeps its own 2x2 and 1x1 levels" (the warm-up); launching again (2 of at most 17)
```

A launch that leaves the journal as it was, or adds nothing to it but its own start, ends the
relaunching, since the next would do the same; so does a run with no journal (`nojournal`). At the
end the target prints the counts, then, on a line of its own, the cases skipped as crashed and the
ones this run ended inside, and the number of launches. A missing or non-executable emulator, or a
run that never gets its summary, stops the target with a line saying which and a non-zero exit;
stopping it by hand stops the emulator and writes `stopped` into the journal, so the case under
way is not taken for a crash.

**The crash list is kept per emulator binary**, under `build/xdg/crash-lists/`, and put on the card
before the first launch and taken back after the last, so a case one build crashes on is not
skipped on another's run. A rebuild, a binary of another size or time, starts a list of its own.
To try one case again under `make run`, delete its line in the list kept there, named after the
binary: the card's copy is replaced from it before the first launch.

Before the emulator starts, the target writes `quick` into the mode file and says so, because the
detailed screen is for a person. `MODE` is a variable too: `make run MODE="quick images"` keeps
every picture, `make run MODE="quick fatal"` runs the cases that may crash, and `make run MODE=`
writes no file, so the start screen and the results come up, each counting down ten seconds, and
the program leaves by itself, which is the way to see them from here. `XDG` moves the whole tree:
`make run XDG=/elsewhere` starts from an empty card there.

**On a display nobody is using.** A closed window stops the emulator, and the run then says the
launch exited 0 inside whatever case was running, which reads like a crash and is not one; a run
nobody needs to watch can go on a private X server instead, where no window comes up. Under Xvfb the
Vulkan backend finds no queue that can present and the emulator stops before the program starts; the
OpenGL one runs on the software rasteriser, which the audio and pad families were run with (a pad
run then waits out its first screen, nobody being there). Set `backend=0` under `[Renderer]` in the
`sdl2-config.ini` of an `XDG` of its own, then:

```sh
Xvfb :79 -screen 0 1280x720x24 -nolisten tcp &
DISPLAY=:79 make run MODE="quick audio" XDG=/somewhere/xvfb-xdg
```

The graphics family is not for this: its cases want a real rendering backend, as the next paragraph
says.

The run needs a real rendering backend. On a null or headless backend the selfcheck cases still
report and the device still opens, then every drawing case fails with every pixel reading zero,
because a backend that draws nothing is doing exactly as asked: a property of the run, not a defect
found. Such a run is still worth doing. The graphics library runs on the emulated processor, so
everything it checks about the commands it is given is checked for real: a shader module it will not
load, a copy it will not make or a rectangle it will not clear ends the program rather than failing
a case. A headless run that reaches its summary has shown none of that happened, which is most of
what can go wrong before a pixel is involved.
