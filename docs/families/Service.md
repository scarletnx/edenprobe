# The service family

Commands a game sends to the system's services, asked the way a game asks them, each answer held to
the one reden gives. Only the word `service` runs it (see [The mode file](../ModeFile.md)), since no
console has yet shown what it answers; the start screen has no row for it, and a file that names it
keeps it whatever the screen changes. One suite per service, each named after it.

## The suites

| suite | cases | what a failure points at |
|---|---|---|
| hwopus | 3 | the hardware Opus decoder's limit on live decoders and the result past it (`hle/service/audio/hardware_opus_decoder_manager.cpp`, `audio_core/opus/hardware_opus.cpp`, `RegisterDecoder`) |
| am | 2 | the size of an application's account save: where the request carries the user, and whether the size is kept (`hle/service/am/service/application_functions.cpp`, `ExtendSaveData`, `GetSaveDataSize`) |
| fsp | 2 | the temporary storage left as the launch before left it (`core.cpp`, `Load`, and `hle/service/filesystem/filesystem.cpp`, `InitTempStorage`) |
| account | 2 | the user picked with nobody asked, by the command a game built for 19.0.0 sends (`hle/service/acc/acc.cpp`, `TrySelectUserWithoutInteraction`, and the acc:u0 table) |

Every expectation is reden's, at 75c12299a6, with the commit that set it; the line of every case
says what it expected and what came back, passed or not. A case whose question needs one before it
is skipped as `depends` when that one did not leave what it needs. The tier is exact wherever a
value is compared and none on the line that only says a service opened.

## hwopus

A game's codec library opens hardware Opus decoders until the service refuses one, and on the
refusal falls back to a software decoder (reden 00a1c392e7, #4384, read from the firmware's
`nn::codec::InitializeHardwareOpusDecoder` after a game failed to load its save with a decoder
refused the wrong way). The service keeps 24 live decoders and answers the 25th with
`ResultOutOfOpusDecoders`, module HwOpus (111), description 385: `0x3026F`.

The suite opens decoders of 48 kHz stereo with libnx's `hwopusDecoderInitialize`, which asks
`GetWorkBufferSize` (1) and then `OpenHardwareOpusDecoder` (0) with the work buffer as transfer
memory, each decoder an object of its own.

| case | what it asks | passes when | tier |
|---|---|---|---|
| hwopus opens a decoder for 48 kHz stereo | one decoder | it opens | none |
| hwopus keeps 24 decoders and refuses the 25th as out of decoders | decoders until one is refused, at most 26 | 24 open and the 25th is refused with `0x3026F` | exact |
| a decoder closed makes room for the next one | after the refusal, close the first and open again | it opens | exact |

The second case stops at the first refusal, so a machine with no limit opens all 26 and says so,
and a limit of another size or a refusal with another result says which. The third is skipped when
the second refused nothing. Every decoder is closed before the suite returns.

## am

A game that grows its save asks the application's functions to extend it, ExtendSaveData (25),
and reads the size back with GetSaveDataSize (26). Both take the save's type and the user, the
user 8-aligned after seven bytes of padding; reden d2279cbf8d read it from byte 8 in both, where
it had been read from byte 1, and keeps the size the extend asked for, which the next 26 hands
back (`application_functions.cpp:234-261` at 75c12299a6).

The suite needs the application's functions, which only a program running as an application has
(`appletGetAppletType`); anywhere else both cases are skipped as `unavailable`. The user comes from
acc:u0, GetLastOpenedUser (4), else the first of ListAllUsers (2), with acc:u0 opened by libnx's
`accountInitialize` or, when the loader refuses InitializeApplicationInfo, by name.

| case | what it asks | passes when | tier |
|---|---|---|---|
| an application's functions open, with a user to ask about | the functions and a user | both are there | none |
| GetSaveDataSize reads back the size ExtendSaveData gave the save | EnsureSaveData (20), then 26, then 25 with both parts one MiB larger, then 26 | 25 succeeds and 26 reads back what it asked | exact |

The line carries the sizes before, the ones asked for, the required size 25 hands back (unjudged)
and the sizes after, so a machine that keeps nothing says the size before reads back.

## fsp

An application's temporary storage starts empty: "On hardware, it is expected that temporary
storage be empty at first use", the comment reden carried from yuzu, and reden f3af5d0c25 (#4375)
empties it once at each launch of the application rather than each time a program of it opens its
saves, which had deleted a file a game made a moment before.

The suite opens the storage with libnx's `fsOpen_TemporaryStorage` (OpenSaveDataFileSystem, 51,
space and type Temporary) and leaves a marker in it, `/edenprobe-launch`, eight bytes, saying on the
memory card which one (`sdmc:/edenprobe/service/temporary`). The next launch looks for it before
leaving its own: the first launch on a card has nothing to look for, so its second case is skipped
as `depends`, and a run is only a question from the second launch on.

| case | what it asks | passes when | tier |
|---|---|---|---|
| the temporary storage opens | the storage, the marker the launch before left, and one left for the next | it opens | none |
| the temporary storage starts each launch empty | what the first case found | the marker is gone: opening it answers PathNotFound (`0x202`) | exact |

Every request to the service is made in the first case, so a program that ends inside one is
recorded against a case that started. A marker that opens fails the case with what it holds, and
says when it is not the one the card names (left by a launch whose card line was lost).

## account

A game picks its user without asking anybody when the console has exactly one:
TrySelectUserWithoutInteractionDeprecated (51), or from 19.0.0 TrySelectUserWithoutInteraction
(52), which a game built for that firmware sends instead. reden 87e5d0b6e4 put 52 in acc:u0's
table (b66ebe638b had it in acc:u0 and acc:su) with 51's answer: the user when there is exactly
one, an id of nought otherwise. Without 52 a game built for 19.0.0 gets whatever an unknown
command answers, and an id of nought sends it to the user selector.

acc:u0 is opened as the am suite opens it. Both commands are asked with no network account
required (a byte of nought).

| case | what it asks | passes when | tier |
|---|---|---|---|
| acc:u0 opens and counts its users | acc:u0 and GetUserCount (0) | both answer | none |
| TrySelectUserWithoutInteraction answers as its deprecated twin | 51, then 52 | both succeed with the same id; or 52 is refused by a machine that says it is older than 19.0.0 | exact |

The line carries the user count, both answers and both ids, and the firmware the machine says it
runs (`hosversionAtLeast(19, 0, 0)`).

## The open questions

- **Q1.** The console's limit and result: reden's 24 and `0x3026F` are read from the firmware, not
  from a console run. Whether the limit is per process or for the whole machine is not known
  either; the suite opens all its decoders from one process.
- **Q2.** Whether a console extends the save of a program whose control data declares no save
  size: the expectation is reden's, which keeps any size it is asked for; a console may refuse.
- **Q3.** Whether a console gives a program whose control data declares no temporary storage size
  a storage at all, and empties it at each launch rather than at each exit; reden empties it when
  the application is launched.
- **Q4.** What a console of 19.0.0 or later answers 52 with more than one user, and with one:
  reden answers as it answers 51.

## The judge, pinned

The `reference` suite holds `source/serviceref.c` on answers made to order, in both builds: every
case under a suite the screens put in the service family, and no name twice; reden's 24 and
`0x3026F` pass, and so does a reopen that succeeds, while no limit, a limit of 25 or of 3, a
refusal with another result (`0xE01`, the session limit) and a reopen still refused each fail with
the line that names it. The result code is worked out by hand: 111 + 385 x 512 = 0x3026F. A save
size that reads back as asked passes; the size before reading back, one part kept and the other
not, and either command refusing each fail with the line that says which. A temporary marker that
is gone passes; one that opens, holding the marker promised or an older one, and an open refused
with anything but PathNotFound each fail. 52 passes with 51's answer for one user and for two, and
refused only on a machine older than 19.0.0; a stub's id of nought for one user, 52 refused on a
19.0.0 machine and 51 itself refused each fail.
