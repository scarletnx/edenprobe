# The hooks mode

## Index

* [What it is for](#what-it-is-for)
* [What the program exports](#what-the-program-exports)
* [Where nothing takes a function](#where-nothing-takes-a-function)
* [The start](#the-start)
* [The cases in hooks mode](#the-cases-in-hooks-mode)
* [What a run says](#what-a-run-says)

The word `hooks` in [the mode file](ModeFile.md) asks the console's services that a machine may answer in a game's own
code through the SDK's functions, and not by IPC: nn::fs, nn::time, nn::pl, nn::ngc and nn::nsd. Without it the run
is the IPC mode, as it always was: the program opens fsp-srv and time:u at its start, as libnx does, and the cases
send the services their commands.

## What it is for

A recompiler can run a game's SDK functions itself, taking each by its symbol, and serve none of the services behind
them: ScarletNX's native nnSdk (scarletnx/ScarletNX#1658) answers nn::fs, nn::time, the shared fonts, pctl, prepo,
nsd and ngc that way and stops a program that opens fsp-srv, time:u, pl:u, pctl, prepo, nsd or ngc itself, which
libnx's start does. The IPC mode is for an emulator and for a recompiler that still serves those services; the hooks
mode for one that does not.

## What the program exports

`source/sdk.h` declares the functions, each under the SDK's own symbol (`_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci`
is nn::fs::OpenFile) and with the SDK's arguments and layouts. The Makefile exports every symbol under `_ZN2nn` from
the program, as a game's module exports its SDK's, so a machine that takes a game's function by its symbol takes the
program's the same way, and the program's calls to it, direct or through a pointer, reach the machine's code. They
are:

| library | functions |
|---|---|
| nn::fs | MountSdCard, MountTemporaryStorage, Unmount, Commit; CreateFile, DeleteFile, CreateDirectory, DeleteDirectory, DeleteDirectoryRecursively, CleanDirectoryRecursively, RenameFile, RenameDirectory, GetEntryType, GetFreeSpaceSize, GetTotalSpaceSize, GetFileTimeStampForDebug; OpenFile, CloseFile, ReadFile, WriteFile, FlushFile, SetFileSize, GetFileSize; OpenDirectory, ReadDirectory, GetDirectoryEntryCount, CloseDirectory |
| nn::time | Initialize, IsInitialized; the standard user clock's GetCurrentTime and GetSystemClockContext, the network clock's GetCurrentTime, the steady clock's GetCurrentTimePoint, GetStandardUserSystemClockCurrentTimespec; GetDeviceLocationName, LoadTimeZoneRule, ToCalendarTime and ToPosixTime (under a rule and under the device's), GetTotalLocationNameCount, LoadLocationNameList, detail::GetDeviceLocationNameAndUpdatedTime, GetTimeZoneRuleVersion; ClockSnapshot::CreateWithStandardSystemClock, CalculateSpanBetween; SetStandardLocalSystemClockCurrentTime, SetStandardLocalSystemClockContext, SetStandardNetworkSystemClockCurrentTime |
| nn::pl | RequestSharedFontLoad, GetSharedFontLoadState, GetSharedFontSize, GetSharedFontAddress, GetSharedFontInOrderOfPriority |
| nn::ngc | ProfanityFilter::Initialize, Finalize, GetContentVersion, CheckProfanityWords, MaskProfanityWordsInText, with the object laid out as the SDK from 20.0.0 lays it out |
| nn::nsd | Resolve, ResolveEx, GetEnvironmentIdentifier |

## Where nothing takes a function

Each function has code of its own, which runs where no machine takes it: the request the IPC mode sends, through
libnx (`sdk_fs.c`, `sdk_time.c`, `sdk_other.c`). It is not the SDK's code: it keeps its own mount table and handles
over libnx's file system objects, opens pl:u and nsd:u at its first call and keeps them, and answers a call it has no
request for, such as a path on a mount it did not make, with libnx's NotFound (`0x1359`). So the hooks mode runs on
an emulator too, its cases asked through the SDK's functions and answered by the IPC mode's commands; and on a
machine that takes only some of them, the rest send their requests, which such a machine may refuse.

A machine that takes a function only for some SDK versions takes none of these: a homebrew program carries no SDK
label, and its version is unknown. On ScarletNX's #1658, nn::ngc's functions and nn::prepo's Save are such hooks.

## The start

The program's `__appInit` (`source/hooks.c`) is libnx's, step for step, up to the card and the clocks. Then it mounts
the memory card through nn::fs::MountSdCard as `sdmc`, and reads `sdmc:/edenprobe/mode` through nn::fs, as the runner
reads it later (the first 4096 bytes, a folder as no file). Without `hooks` it unmounts the card and goes on as
libnx: time:u, the C library's time zone, fsp-srv, its `sdmc:` device. With `hooks` the card stays mounted and becomes
the C library's `sdmc:` device, every file the program writes going through nn::fs (each write flushed, as a journal
line must be on the card before the next case may end the program), and nn::time::Initialize is called, as a game's
start calls it. Neither fsp-srv nor time:u is opened.

When the machine answered MountSdCard itself and the file has no `hooks`, the program says so on the machine's log
before it opens time:u, which such a machine may refuse:

```
edenprobe: the machine answered nn::fs::MountSdCard itself and the mode file has no hooks: the IPC mode opens time:u and fsp-srv now, which such a machine may refuse; the word hooks in sdmc:/edenprobe/mode asks through the SDK's functions instead
```

The C library's `time()` and `gettimeofday()` have no clock in hooks mode: libnx's answer to them is time:u's, which
the program does not open. No case asks them.

## The cases in hooks mode

The suites that ask those services ask the SDK's functions instead, with the same names and the same expectations,
reden's; a case says in its line which way it went where the two differ.

| family | suites | in hooks mode |
|---|---|---|
| fs | fs | the card mounted as `edenprobe-fs`, every read nn::fs::ReadFile |
| service | fsp, fsdir, fs-file | the temporary storage mounted as `edenprobe-temp`, the card as the start mounted it; a file written to is flushed before it is closed, which the SDK asks of a writer and fsp-srv does itself |
| service | time, time-snap, time-zone | nn::time's functions; Initialize stands for time:u and its objects, and a function the SDK declares void answers nought where the IPC mode puts its command's result; CalculateSpanBetween's false is `0xffffffff` there. The snapshot's type is on the line and not judged: ClockSnapshot::CreateWithStandardSystemClock picks it, and no reference says which |
| service | time-perm | the clocks through the three setters an application links, held to `ServiceJudgeTimeSdkClocks`; the settings case is skipped, since their setters answer nothing and stop the program on a refusal |
| service | pl | the fonts by their addresses: each offset counts from 8 bytes before the lowest font, the memory's start in the IPC mode's layout, since the SDK gives no base |
| service | ngc | a ProfanityFilter on a 128 KiB work buffer, checked and masked through the SDK, held to `ServiceJudgeNgcSdkCheck` and `ServiceJudgeNgcSdkMask`; the ngct case is skipped, as the program exports no function over ngct:u |
| net | dns | nsd's Resolve and GetEnvironmentIdentifier through nn::nsd |
| game | game | the card's file read through nn::fs, the steady clock through nn::time |

Every other suite is the same in both modes, and the sdk suite asks the same in both: whether a tail call to an SDK
function reaches what a call reaches ([sdk](families/Service.md#sdk)). It is the program's one tail call into those
functions: the volume layer the fs cases go through (`source/volume.c`) is built with no tail calls, and
`tools/sdk-exports.py`, run by the CI, refuses a tail branch anywhere else, so no other case depends on how a
machine treats one.

## What a run says

The support suite's first case says which way the run asks, and what answered: in hooks mode whether the machine or
the program's own code answered MountSdCard and nn::time::Initialize; in IPC mode a failure when the machine answered
MountSdCard itself, the word `hooks` asking through the SDK instead. In a hooks run, once the report is open and after
each step, a note names every function whose own code answered a call since the last one, with how many: the ones
the machine left to the program. A step a case ended runs again from its start and notes its own code once.

```json
{"type":"note","text":"hooks: in step service, the program's own code answered nn::fs::GetFileTimeStampForDebug x1"}
```
