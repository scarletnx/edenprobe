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
| ldn | 2 | the client process monitor a game asks ldn:u for on 18.0.0 (`hle/service/ldn/ldn.cpp`, `IUserServiceCreator`, `IClientProcessMonitor`) |
| set | 4 | the language codes a game picks its text by (`hle/service/set/settings_server.cpp`, `available_language_codes`) |
| csrng | 2 | the random bytes a game seeds from (`hle/service/spl/spl_module.cpp`, `GenerateRandomBytes`) |
| apm | 1 | apm and a session on it, which a game opens before it asks for the performance mode (`hle/service/apm/apm_interface.cpp`, `APM::OpenSession`) |
| nfp | 2 | the detection a game built for 23.0.0 starts (`hle/service/nfp/nfp.cpp`, the IUser table) |
| aoc | 2 | the add-on content list a game reads before it mounts one, past its end (`hle/service/aoc/addon_content_manager.cpp`, `ListAddOnContent`) |
| time | 6 | the clocks and dates a game reads at start: the user clock against its context and the steady clock, the steady clock's seconds, dates under UTC and under the device's rule (`hle/service/psc/time/clocks/system_clock_core.cpp`, `GetCurrentTime`, and `hle/service/glue/time/time_zone.cpp`) |
| pl | 3 | the shared fonts a game's text is drawn with: loaded, inside the shared memory behind their headers, and in order of priority (`hle/service/ns/platform_service_manager.cpp`, `DecryptSharedFont`, `GetSharedFontInOrderOfPriority`) |

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

## ldn

From 18.0.0, libnx's `ldnInitialize` asks ldn:u for a monitor of the client process,
CreateClientProcessMonitor (1), and registers this process with it, the monitor's RegisterClient (0,
a word of nought and the process id). reden 23585820bc (#4376) added the command and the object;
without it, an unknown command's success hands back no object and the registration goes to a
handle that is none.

| case | what it asks | passes when | tier |
|---|---|---|---|
| ldn:u opens | ldn:u | it opens | none |
| CreateClientProcessMonitor hands out a monitor this process joins | 1, then RegisterClient on what it handed out | both succeed, on a handle that is not nought; or 1 is refused by a machine that says it is older than 18.0.0 | exact |

The line carries both results, the handle and the firmware the machine says it runs. The monitor
and ldn:u are closed before the suite returns; nothing else of ldn is asked.

## set

A game picks the language of its text from set: the codes the console has,
GetAvailableLanguageCodes2 (5, from 4.0.0), the console's own, GetLanguageCode (0), and
MakeLanguageCode (2), which makes an index of the language enumeration into its code. reden answers
all three from one table, refuses an index past it with ResultInvalidLanguage, Settings (105) 625,
`0x4E269`, and counts with GetAvailableLanguageCodeCount2 (6) the most the list can hold, 0x40.

| case | what it asks | passes when | tier |
|---|---|---|---|
| set opens and lists its language codes | set, and 5 with room for 0x40 codes | it lists at least one | none |
| GetLanguageCode is one of the codes set lists | 0 | a code that is not nought, in the list | exact |
| MakeLanguageCode makes each index into the code listed there | 2 for every index the list has, and for the next | each the code at that index, the next refused with `0x4E269` | exact |
| GetAvailableLanguageCodeCount2 leaves room for every code listed | 6 | at least as many as the list holds | exact |

The count is held to room, not to the list's length: a console's count is the list's, reden's the
buffer's, and a game sizes its buffer by it either way. Each line names the codes as text.

## csrng

csrng's GenerateRandomBytes (0) writes as many random bytes as the buffer holds (reden's
`spl_module.cpp`, the size of the buffer it is handed). The suite asks twice for 64 bytes, each
buffer filled with `0xA5` first, through libnx's `csrngGetRandomBytes`.

| case | what it asks | passes when | tier |
|---|---|---|---|
| csrng opens | `csrngInitialize` | it succeeds | none |
| GenerateRandomBytes fills the buffer, and differently each time | 0, twice | both succeed, neither buffer's last eight bytes still the fill, neither all nought, the two apart | exact |

A correct machine fails the case only if 64 random bytes repeat, or eight end on the fill, by
chance: one in 2^64. The line carries the first four bytes and the last two of each.

## apm

A game opens apm, and a session on it, before it asks for the performance mode, Normal or Boost,
that tells it whether it runs docked. libnx's `apmInitialize` does both: apm, then OpenSession (0),
which reden answers with an ISession (`apm_interface.cpp`, `APM::OpenSession`).

| case | what it asks | passes when | tier |
|---|---|---|---|
| apm opens, with a session | `apmInitialize` (apm and OpenSession, 0) | it succeeds | none |

## nfp

A game built for 23.0.0 starts looking for a figure with StartDetectionWithFilter (25) where older
ones send StartDetection (3). reden 42642f8bad (#4425) answers 25 with 3's handler in IUser, ISystem
and IDebug. With nobody holding a figure to the reader the question is still asked: both commands
for the same device, the first ListDevices (2) finds or a handle of nought, and the two answers must
agree. Nothing here needs a person.

| case | what it asks | passes when | tier |
|---|---|---|---|
| nfp opens and initialises for a user | `nfpInitialize(User)` (nfp:user, CreateUserInterface, Initialize) | it succeeds | none |
| StartDetectionWithFilter answers what StartDetection answers | 2, then 3 and 25 for one device, each stopped when it started | the same result; or 25 refused by a machine that says it is older than 23.0.0 | exact |

25 is sent with the device first, where reden's handler reads it, and a filter word of nought after.

## aoc

A game asks aoc:u how many add-on contents it has, CountAddOnContent (2), and lists their indices,
ListAddOnContent (3), from an offset, before it mounts one. reden 54cd5fb8eb refuses a list asked
from past its end with ResultApplicationRecordNotFound, AM (128) 37, `0x4A80`, where it had
answered ResultUnknown; asked from the end itself, the list is empty and succeeds. libnx has no
client for aoc:u, so both commands are sent raw: the offset and the most indices wanted, then a
word for the process id, which goes with the request.

| case | what it asks | passes when | tier |
|---|---|---|---|
| aoc:u opens and counts this program's add-on content | aoc:u and 2 | both answer | none |
| ListAddOnContent lists nothing at the end and refuses one past it | 3 from the count, then from the count and one | the first succeeds with none listed, the second is refused with `0x4A80` | exact |

The line carries the count, both answers and how many the first listed. Nothing is mounted.

## time

A game reads the time at start, for its clock, its daily events and the dates on its saves, from
time:u: the user's clock, GetStandardUserSystemClock (0), the steady clock, GetStandardSteadyClock
(2), and the time zones, GetTimeZoneService (3). reden's user clock tells its time as the
context's offset past the steady clock's time point, and refuses with ResultClockMismatch when the
context was set on another source of the steady clock (`SystemClockCore::GetCurrentTime`,
`system_clock_core.cpp:23-35` at 75c12299a6). The time zone service dates a time under a
rule it loads by the location's name, LoadTimeZoneRule (4), ToCalendarTime (100) and ToPosixTime
(201), or under the device's own, ToCalendarTimeWithMyRule (101) (`glue/time/time_zone.cpp`).

The suite opens time:u for itself and the three objects; libnx's own, opened at start, are not
asked. The dates are worked out by hand: 0 is 1970-01-01 00:00:00, a Thursday (4), the year's
first day (0); 1700000000 is 19675 days and 80000 seconds later, 2023-11-14 22:13:20, a Tuesday
(2), the year's 318th day (317, as the service counts from nought).

| case | what it asks | passes when | tier |
|---|---|---|---|
| time:u hands out the user clock, the steady clock and the time zones | time:u, then 0, 2 and 3 | all three objects come | none |
| the user clock's time is its context's offset past the steady clock | the user clock's GetSystemClockContext (2), the steady clock's GetCurrentTimePoint (0), the user clock's GetCurrentTime (0) | the context on the steady clock's source, not nought, and the time its offset past the time point, or one second more | exact |
| the steady clock counts seconds on one source | GetCurrentTimePoint twice, a second and a half apart | the same source, not nought, one or two seconds on | exact |
| ToCalendarTime under UTC dates the epoch and a known time | LoadTimeZoneRule("UTC"), then 100 of 0 and of 1700000000 | both dates, weekdays and days of the year as worked out, named UTC, no daylight saving, no offset | exact |
| ToPosixTime under UTC turns the known date back into its time | LoadTimeZoneRule("UTC"), then 201 of 2023-11-14 22:13:20 | one time, 1700000000 | exact |
| the device's location loads as the rule the service dates with | GetDeviceLocationName (0), 4 of that name, the user clock's time, then 101 and 100 under the loaded rule | the name not empty, and both dates and their information the same | exact |

Each line carries what each command answered and what it wrote, the dates as text. Nothing is
set: the clocks and the location are only read.

## pl

A game that draws its text with the system's fonts asks pl:u for them before it draws:
GetSharedMemoryNativeHandle (4) for the memory they are in, then for each type RequestLoad (0),
GetLoadState (1), GetSize (2) and GetSharedMemoryAddressOffset (3), or all of them at once in the
order of priority for a language, GetSharedFontInOrderOfPriority (5). reden puts the six fonts in
one memory of 0x1100000 bytes, each behind 8 bytes: the magic the .bfttf decrypts to, 0x7f9a0218,
big-endian, so that the word reads 0x18029a7f, and the size XORed with the key, 0x49621806, the
.bfttf's own magic 0x36f81a1e XORed with the decrypted one; the font itself follows decrypted, a
TrueType file (`DecryptSharedFont`, `platform_service_manager.cpp:38-53` at 75c12299a6). Ryujinx
writes the same first word (`SharedFontManager.cs`, `WriteMagicAndSize`), and the second
big-endian where reden leaves it little-endian, so the suite judges the first and only prints the
second, both ways.

The suite opens pl:u for itself and maps the memory read-only, as libnx's `plInitialize` does.

| case | what it asks | passes when | tier |
|---|---|---|---|
| pl:u opens and maps its shared fonts | pl:u and 4, the memory mapped | it maps | none |
| the six shared fonts are loaded, each inside behind its header | 0 to 3 for each type, and the 8 bytes before the offset | every type Loaded (1), not empty, inside the memory, behind 0x18029a7f, and starting as a TrueType or OpenType file | exact |
| the fonts in order of priority are the six, as told one by one | set's GetLanguageCode (0), then 5 for that code | loaded, six listed once each, every offset and size the one its type was told | exact |

The order itself is not judged: reden lists the types in their own order, a console by the
language. The second case's line carries, for every type, the four results ORed, the state, the
size at the offset, the header's first word and its second decoded both ways, and the font's first
word.

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
- **Q5.** What a console's RegisterClient answers a process that never initialised ldn (the suite
  does not ask Initialize first, as libnx does): reden answers success to any process.
- **Q6.** The result a console gives MakeLanguageCode past its table (reden's `0x4E269`), and
  whether its count (6) is the list's length or, as reden's, the most the list can hold.
- **Q7.** The layout of 25's filter on a console: reden reads the device handle and nothing else.
- **Q8.** Whether a console answers aoc:u for a program that is not an installed application, and
  the result it gives a list from past the end: the expectation is reden's `0x4A80`.
- **Q9.** Whether a console's steady clock moves by whole seconds between two readings taken a
  second and a half apart, as reden's does (it rounds nanoseconds down to seconds), and whether the
  user clock's context is ever on another source than the steady clock's on a console that keeps
  its time (reden's never is).
- **Q10.** The byte order of the size in a shared font's header on a console: reden leaves the
  .bfttf's word little-endian, Ryujinx writes it big-endian; and whether a console's
  GetSharedFontInOrderOfPriority puts a language's own font first.

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
19.0.0 machine and 51 itself refused each fail. The ldn monitor passes when it comes and takes the
process, and when 1 is refused on a machine older than 18.0.0; success with no object, a
registration the kernel refuses (`0xE401`, InvalidHandle) and 1 refused on an 18.0.0 machine each
fail. set passes reden's table (the codes spelled as text) and fails a language not listed, a code
of nought, an index made into another index's code, one past the table made into anything, and a
count below the list. nfp passes 25 refusing as 3 refuses, and 25 refused on
a machine older than 23.0.0; an unknown command's success where 3 refuses fails, at any firmware. csrng passes two buffers written whole and
apart, and fails the same bytes twice, a buffer whose end was not written, a buffer of nought and a
refusal. aoc passes an empty end and `0x4A80` past it, for none and for three add-on contents (the
code worked out by hand: 128 + 37 x 512); a success past the end, another refusal there (reden's
ResultUnknown before 54cd5fb8eb), the end refused and an end that lists one each fail.
time passes a user clock its context's offset past the steady clock, and a second
more, and fails two seconds more, a context on another source and a stand-in's noughts; a steady
clock one or two seconds on, and not one standing still, one counting nanoseconds or two sources;
the epoch and 1700000000 dated as worked out by hand, and not a day of the year counted from one,
another weekday, an offset or a stand-in; 1700000000 back from its date, and not no time or a time
three hours off; and the device's rule dating as the service's own, but not two offsets or no
name.
pl passes six loaded fonts behind the decrypted magic, with the size written either
way, and fails the .bfttf's own magic in its place, a font not loaded, no room for a header, a
font past the memory, bytes that are not a font and a stand-in's noughts; it passes the six in
another order, each where its type said, and fails one listed twice, an offset moved, fonts not
loaded and a stand-in.
