# Frontends

## Index

* [What the engine is](#what-the-engine-is)
* [What a frontend gives it](#what-a-frontend-gives-it)
* [The frontends there are](#the-frontends-there-are)
* [A new frontend](#a-new-frontend)
* [The ones to come](#the-ones-to-come)
  * [3DS](#3ds)
  * [Vita](#vita)
  * [GBA](#gba)

One program, built for each machine it asks: the NRO for the console, the APK for Android, the host build for a
PC. Each build is a frontend around one engine, and every frontend writes the same report, so a report from a
machine and one from its emulator compare line by line ([The report](Report.md)).

## What the engine is

Portable C, the same sources in every build (`source/portable.txt` lists them):

- **the report** (`report.c`): each case's line, the notes, the summary; every line to a file and to the machine's
  log, and synced before the next case starts;
- **the journal and the crash list** (`journal.c`): where a run is, so a crash costs one case and the next launch
  goes on after it ([When the program ends in the middle](Crashes.md));
- **the mode file** (`mode.c`): the words that choose the families ([The mode file](ModeFile.md));
- **the run** (`runner.c`): the mode file read, the run's folder made, the journal of the run before read and the
  plan made from it, the steps run in their order with the journal written between them, the summary, and
  `EDENPROBE DONE` on the log;
- **the judge** (selfcheck, support, reference) and the references the cases are held to;
- **the cases every frontend runs**: the libc family's suites, the C library and what it asks of the system
  ([libc](families/Libc.md)).

The engine uses no thread, no drawing and no network of its own, and the heap only to keep a detailed run's cases
for its screens. It writes files with stdio; a machine with no file system gets its report on the log alone.

## What a frontend gives it

`source/platform.h` is all the engine asks of a machine beyond its C library:

| What | Required | Console | Android | Host |
|---|---|---|---|---|
| `PlatformLog`, the machine's log | yes | the debug string service | logcat, tag `edenprobe` | standard output |
| `PlatformLogIsStandardOutput`, whether a case can read the log back | yes | no | no | yes |
| `PlatformSocketsOpen`, for the socket suite | when it runs that suite | bsd (`socketInitializeDefault`) | nothing to bring up | nothing to bring up |
| `PlatformSurface*`, a surface to draw into and read back | when it runs a drawing suite of the engine's | no: its graphics family is deko3d's own | not yet | no |

Files, time and threads come from the C library itself: stdio, `clock_gettime` and pthreads, which newlib on the
consoles, bionic and glibc all have. A suite that needs one of them is a step only a frontend whose machine has it
lists: the system suite needs threads and a clock, the socket suite sockets.

The rest is the frontend's own code, around the runner's calls (`runner.h`):

- a `RunnerFrontend`: the root folder (the mode file, the crash list, `runs/` and `baseline/` go under it), the
  version, the build and the hash, its steps in their order, the families they make up, and of those the ones that
  run only when the mode file names them, and what it writes into the report after each step (the console notes
  what the SDK's own code answered in it, [The hooks mode](Hooks.md));
- its own suites: the console's are its processor, deko3d, its services; Android's its Java framework, JNI, GLES and
  bionic's own;
- its screens, when it has any: the console asks whether to go on with a run left in the middle, shows the start
  screen and the running screen, and the results after the report is closed. A frontend without screens runs as a
  quick run does.

## The frontends there are

| Frontend | Source | Root | Families |
|---|---|---|---|
| The console (NRO) | `source/main.c`, `source/platform_switch.c` | `sdmc:/edenprobe` | judge and cpu always; graphics, fs, net when no word names one; pad, kernel, display, video, audio, service, game and libc when named |
| Android (APK) | `android/src/main/cpp/frontend.c`, `platform_android.c`, the Java in `android/src/main/java` | the app's external files, `edenprobe/` | judge always; libc, graphics, net, jni and framework when no word names one ([On Android](Android.md)) |
| Host | `source/host_main.c`, `source/platform_host.c` | none: `edenprobe-host-report.jsonl` where it runs | the judge and the libc family, every time ([Build](Build.md#the-host-build)) |

The console runs the libc family only when a word names it, so a run that names no family reports what it did
before the family existed.

## A new frontend

1. A build that compiles `source/portable.txt` with its compiler, `-std=c11 -Wall -Wextra -Werror`, and its own
   sources beside them, and names its build as the Makefile does (`EDENPROBE_VERSION`, `EDENPROBE_BUILD`,
   `EDENPROBE_HASH`, [Versions](Versions.md)).
2. `platform_<name>.c` with `PlatformLog`, `PlatformError`, and the sockets and the surface for the suites it runs.
3. A `RunnerFrontend` and an entry that calls the runner in order: `RunnerInit`, `RunnerModeFromFile`,
   `RunnerPlan`, `RunnerModeAtStart`, `RunnerOpen`, `RunnerBeginJournal` for a new run, `RunnerApplyMode`,
   `RunnerSteps`, `RunnerFinish`. Android's `frontend.c` is the shortest one to copy.
4. Its steps: the judge's three first, then the libc family's suites its machine can run, then its own.
5. A job in `.github/workflows/build.yml` that builds it, and its line in this document and in
   [Releases](Releases.md).

## The ones to come

The 3DS, the Vita and the GBA run under ScarletNX too, and each would be a frontend of its own. None is built yet;
what each needs:

### 3DS

- **Toolchain:** devkitARM with libctru and citro3d, from devkitPro's pacman (`dkp-pacman -S 3ds-dev`), or the
  `devkitpro/devkitarm` container as the NRO's job uses `devkitpro/devkita64`. This machine has devkitA64 and libnx
  only; devkitPro's installer writes `/opt/devkitpro`, which takes root.
- **Platform:** `PlatformLog` through `svcOutputDebugString`, which Citra and its forks log; sockets through
  `socInit` with a buffer of its own; files on the SD card through libctru's sdmc, so the root is `sdmc:/edenprobe`
  as on the Switch.
- **Shared cases:** libc whole (newlib, as the Switch's); system (libctru's pthreads and `clock_gettime`); socket
  over `soc:U` on loopback.

### Vita

- **Toolchain:** VitaSDK, which installs in a folder of the user's (`vdpm`, `VITASDK=$HOME/vitasdk`), no root.
- **Platform:** `PlatformLog` through `sceClibPrintf`, which Vita3K logs; sockets through `sceNetInit` and
  `sceNetCtlInit`; files under `ux0:data/edenprobe`.
- **Shared cases:** libc whole (VitaSDK's newlib); system (its pthreads, `clock_gettime`); socket on loopback once
  the network module is up.

### GBA

- **Toolchain:** devkitARM with libgba or libtonc (`dkp-pacman -S gba-dev`), the same devkitARM the 3DS needs.
- **What it is:** an ARM7TDMI with no operating system, 256 KiB of work RAM, no threads, no file system and no
  network. The runner's journal and crash list need files, so a GBA frontend calls the report alone, with its mode
  compiled in: `ReportOpen` of a path its C library cannot open, which leaves the whole run on the log as on a
  console with no card, its steps, `ReportSummary`.
- **The report out:** `PlatformLog` writes each line where the emulator reads it: mGBA's debug registers (0xC0DE
  written at `0x4FFF780` turns them on, the text goes at `0x4FFF600` and its level at `0x4FFF700`), which mGBA
  prints and logs; no$gba's debug message; or the serial port. The last lines, the summary among them, also go to
  SRAM, which every emulator saves as a file, for a run whose log nobody reads.
- **Shared cases:** the libc suite, on newlib as the consoles have it; no system suite (no threads, no clock) and no
  socket suite. The code runs from the cartridge's 32 MiB, and the engine's static data has to fit the 256 KiB of
  work RAM beside the cases': what the judge's references and the baseline's table take is measured when the
  frontend is built, and the ones that do not fit stay out of its steps. Floats are software on that processor, so
  libm and the formatting cases take far longer than elsewhere.
