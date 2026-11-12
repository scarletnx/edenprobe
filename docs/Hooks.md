# The hooks mode

## Index

* [What it is for](#what-it-is-for)
* [What the program exports](#what-the-program-exports)
* [Where nothing takes a function](#where-nothing-takes-a-function)
* [The start](#the-start)
* [The renderer](#the-renderer)
* [The cases in hooks mode](#the-cases-in-hooks-mode)
* [What a run says](#what-a-run-says)
* [What the machines answered](#what-the-machines-answered)

The word `hooks` in [the mode file](ModeFile.md) asks the console's services that a machine may answer in a game's own
code through the SDK's functions, and not by IPC: nn::fs, nn::time, nn::pl, nn::ngc, nn::nsd, nn::pctl, nn::audio
(the audio outs, the renderer, the audio device and the audio ins) and nn::codec (the hardware Opus decoder). Without
it the run is the IPC mode, as it always was: the program opens fsp-srv and time:u at its start, as libnx does, and
the cases send the services their commands.

## What it is for

A recompiler can run a game's SDK functions itself, taking each by its symbol, and serve none of the services behind
them: ScarletNX's native nnSdk (scarletnx/ScarletNX#1658, carried onto its main from #1779) answers nn::fs, nn::time,
the shared fonts, pctl, prepo, nsd and ngc that way, and from #1804 to #1860 the audio outs, the audio device, the
renderer, the hardware Opus decoder and the audio ins, and stops a program that opens fsp-srv, time:u, pl:u, pctl,
prepo, nsd, ngc, audout:u, audren:u, hwopus or audin:u itself, as libnx's start and its audio drivers do. The IPC
mode is for an emulator and for a recompiler that still serves those services; the hooks mode for one that does not.

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
| nn::pctl | Initialize, IsRestrictionEnabled, CheckFreeCommunicationPermission, GetSafetyLevel |
| nn::audio, outs | OpenAudioOut, OpenDefaultAudioOut, CloseAudioOut, GetAudioOutState, StartAudioOut, StopAudioOut, AppendAudioOutBuffer, GetReleasedAudioOutBuffer, ContainsAudioOutBuffer, GetAudioOutBufferCount, FlushAudioOutBuffers, SetAudioOutVolume, GetAudioOutVolume, ListAudioOuts, with the AudioOut laid out as every SDK from 0.12 to 20.5 keeps the rate, channels, format, state and name its accessors read |
| nn::audio, device | ListAudioDeviceName, GetActiveAudioDeviceName, SetAudioDeviceOutputVolume, GetAudioDeviceOutputVolume |
| nn::audio, ins | ListAudioIns |
| nn::audio, renderer | GetAudioRendererWorkBufferSize, OpenAudioRenderer (with its SystemEvent), CloseAudioRenderer, StartAudioRenderer, StopAudioRenderer, GetAudioRendererState, its sample rate, sample count and mix buffer count, the rendering time limit and the voice drop parameter, set and read, and RequestUpdateAudioRenderer, with the AudioRendererParameter and AudioRendererConfig laid out as the SDK from 9.0.0 does |
| nn::codec | GetHardwareOpusDecoderWorkBufferSize, InitializeHardwareOpusDecoder, DecodeOpusInterleavedWithHardware, FinalizeHardwareOpusDecoder, with the HardwareOpusDecoderType laid out as the SDK from 6.0.0 does |

And the functions a machine that answers those calls back in the game's module, as the game's SDK has them for it:
the renderer's managers, whose functions write and read an update's sections (BehaviorManager's UpdateInParameter,
GetRevision, UpdateOutStatus and CheckErrors; the UpdateInParameter and UpdateOutStatus of MemoryPoolManager,
VoiceInfoManager (UpdateVoiceRelatedParameters), EffectManager, SinkManager and PerformanceBufferManager; those of
SplitterInfoManager and MixManager that write), under SDK 20.5's symbols, a const method's (`_ZNK2nn`) among them,
which the Makefile exports too; and nn::os::AttachReadableHandleToSystemEvent, through which a machine hands a game the
renderer's frame event. These are the program's code on every machine (see [The renderer](#the-renderer)).

## Where nothing takes a function

Each function has code of its own, which runs where no machine takes it: the request the IPC mode sends, through
libnx (`sdk_fs.c`, `sdk_time.c`, `sdk_other.c`). It is not the SDK's code: it keeps its own mount table and handles
over libnx's file system objects, opens pl:u and nsd:u at its first call and keeps them, and answers a call it has no
request for, such as a path on a mount it did not make, with libnx's NotFound (`0x1359`). So the hooks mode runs on
an emulator too, its cases asked through the SDK's functions and answered by the IPC mode's commands; and on a
machine that takes only some of them, the rest send their requests, which such a machine may refuse.

nn::audio's and nn::codec's own code (`sdk_audio.c`) keeps a session for each object it opens, as the SDK does: an
AudioOut's IAudioOut in the AudioOut's room after the SDK's fields, a decoder's in the HardwareOpusDecoderType's, a
renderer's in a slot its handle names. Where the SDK hands the service the caller's work buffer, the own code makes
one of that size, as libnx's drivers do, and leaves the caller's alone. The
device's functions go through libnx's auddev, the commands the IPC mode's case sends. A function the SDK declares with
no result, such as GetAudioOutVolume, keeps the result of its own code's request for the case to name
(`SdkOwnResult`), nought when a machine took it.

A machine may take a function only for some SDK versions, reading a module's version from the SDK's label in its
read-only data, which a homebrew program lacks. So the program carries one (`SDK_LABEL` in `sdk.h`,
`SDK MW+Nintendo+NintendoSdk_nnSdk-20_5_0`): the SDK whose arguments and objects its functions follow, as
nn::ngc's ProfanityFilter is laid out from SDK 20. `tools/sdk-exports.py` checks the program holds it. On ScarletNX's
native nnSdk, nn::ngc's functions and nn::prepo's Save are such hooks.

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

## The renderer

An SDK's RequestUpdateAudioRenderer writes the update itself: it runs the managers the game's AudioRendererConfig
holds, each writing its section, hands the update to the renderer, and runs them again on the answer. A machine that
answers RequestUpdateAudioRenderer in the game's code does the same work with no session, calling the managers'
functions in the game's module by their symbols, and so do this program's: the config (`NnAudioRendererConfig`,
`sdk.h`) holds a manager for each section of the update the audio family lays out (`source/audio.c`), and each
manager's function copies its section into the update, or gives the size of its section of the answer. The update a
machine builds that way is byte for byte the one the IPC mode sends, and the program reads the answer where it always
did. The program's own RequestUpdateAudioRenderer does the same, in the same order, and sends RequestUpdate (4).

An SDK from 6.0.0 keeps in the config where the frames rendered so far go, and reads them from the answer's last
section, the render information a renderer adds from revision 5: in hooks mode the renderer is opened at revision 5,
where libnx asks 4 on any firmware from 4.0.0. Of what 5 adds, nothing the cases ask changes: a looping voice's
played count starts over at its loop point, and every case that waits on one waits for less than a loop. The audio
family's first note says which revision a run asked.

The renderer's frame event comes to the program through nn::os::AttachReadableHandleToSystemEvent, which the program
exports and a machine calls with the handle; the audio family clears it before each update and waits on it after, as
it waits on libnx's in IPC mode.

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
| service | hwopus | nn::codec's decoders, each on a work buffer of the size GetHardwareOpusDecoderWorkBufferSize gives; the SDK's OpusResult where hwopus gives a result, so the 25th decoder is held to OutOfResource (8), what the SDK makes of hwopus's `0x3026F` (`ServiceJudgeOpusLimitSdk`) |
| service | auddev | nn::audio's device functions, which need nothing opened: the opens case is the list |
| service | renderer | a renderer opened through nn::audio at revision 5, its rate, sample count and mix buffers, time limit, voice drop parameter and state through the SDK's getters and setters |
| service | audout | the out OpenDefaultAudioOut opens, its rate, channels, format and state read where the SDK keeps them in the AudioOut, buffers appended and taken back one a call through the SDK; a function the SDK declares bool reads 1 where the IPC mode puts its command's result, for false |
| service | audin | nn::audio::ListAudioIns |
| service | pctl | nn::pctl's Initialize, IsRestrictionEnabled, CheckFreeCommunicationPermission and GetSafetyLevel; a false permission reads 1 where the IPC mode puts its command's result |
| audio | audio | the renderer through nn::audio, at revision 5 (see [The renderer](#the-renderer)) |
| net | dns | nsd's Resolve and GetEnvironmentIdentifier through nn::nsd |
| game | game | the card's file read through nn::fs, the steady clock through nn::time, the audio thread's renderer through nn::audio |

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
{"type":"note","text":"hooks: in step service, the program's own code answered nn::fs::GetFileTimeStampForDebug x1, nn::time::IsInitialized x1"}
```

## What the machines answered

Each machine ran this build once in each mode, `quick notime fs service net` with and without `hooks`, and the game
family apart, `quick notime game frames=120`: ScarletNX's #1658 at 3acdfd2 (its native nnSdk), ScarletNX main at
9cc5ec2, both recompiling the program at opt 0 and running it headless, and Eden 6a9235c22d (eden-cli on a display of
its own). In IPC mode, v0.12.0 and this build give every case they share the same result on ScarletNX main and on
Eden, and the six cases this build adds pass on both. On #1658 the IPC mode ends at the start, as it should: the
program says the line above, and sm stops it at time:u.

The cases of the suites that ask those services (fs, fsp, fsdir, fs-file, time, time-snap, time-zone, time-perm, pl,
ngc, dns) and sdk, where any two columns differ; of their 56 cases, 42 pass on every column and five of dns are skipped
on every one, as no net file names a backend:

| case | #1658 hooks | main IPC | main hooks | Eden IPC | Eden hooks |
|---|---|---|---|---|---|
| fsp: the temporary storage starts each launch empty | pass | skip, depends | skip, depends | skip, depends | skip, depends |
| ngc: ngc:u has a version and finds no word in a clean name | crashed | pass | pass | pass | pass |
| ngc: a clean name masked comes back as it was sent | crashed | pass | pass | pass | pass |
| ngc: ngct:u matches nothing in a clean name and filters it back as sent | skip | pass | skip | pass | skip |
| time-perm: time:u may not set automatic correction, the steady clock's offset or the device's location | skip | pass | skip | pass | skip |
| fs-file: a file just written is dated by the clock | fail | pass | pass | pass | pass |
| sdk: a tail call to an SDK function reaches what a call reaches | fail | pass | pass | pass | pass |
| dns: nsd names the environment it resolves for | crashed | pass | pass | pass | pass |
| dns: nsd replaces the placeholder with the environment | skip, depends | pass | pass | fail | fail |

The fsp case needs the launch before to have left its marker: #1658's run took four launches, the others one. What
#1658 leaves to the program, each its own:

- nn::ngc: its hooks hold for SDK 20 and later, and a homebrew program's SDK version is unknown, so none takes the
  program's ProfanityFilter; its own code opens ngc:u, and sm stops the program, once for each case.
- nn::nsd::GetEnvironmentIdentifier and nn::fs::GetFileTimeStampForDebug have no hook: the first opens nsd:u and sm
  stops the program, the second finds no mount of the program's own and answers `0x1359`.
- A tail call: #1658 takes `bl` and a call through a pointer, and a `b` to a hooked function's entry runs the
  function's own code; a game's code makes such branches.

With the SDK label the program carries since, and ScarletNX's native nnSdk carried onto its main with its fixes
(ScarletNX#1779 to #1783, at b879c23), `quick notime fs service net sdk hooks` ends in one launch with no case
crashed: nn::ngc's two cases, nsd's environment and the tail call pass, and of the table's cases only the file's time
stamp still fails, nn::fs::GetFileTimeStampForDebug having no hook there.

The game family's six cases pass on every column.
