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
| apm | 3 | apm and a session on it, which a game opens before it asks for the performance mode, and the configurations a session and SetCpuBoostMode set (`hle/service/apm/apm_interface.cpp`, `APM::OpenSession`, `apm_controller.cpp`) |
| nfp | 2 | the detection a game built for 23.0.0 starts (`hle/service/nfp/nfp.cpp`, the IUser table) |
| aoc | 6 | the add-on content a game counts, lists and prepares before it mounts one: none here, a list past its end, a list that fits its room, the base id, a prepare of an index not installed (`hle/service/aoc/addon_content_manager.cpp`, `CountAddOnContent`, `ListAddOnContent`, `GetAddOnContentBaseId`, `PrepareAddOnContent`) |
| time | 6 | the clocks and dates a game reads at start: the user clock against its context and the steady clock, the steady clock's seconds, dates under UTC and under the device's rule (`hle/service/psc/time/clocks/system_clock_core.cpp`, `GetCurrentTime`, and `hle/service/glue/time/time_zone.cpp`) |
| pl | 3 | the shared fonts a game's text is drawn with: loaded, inside the shared memory behind their headers, and in order of priority (`hle/service/ns/platform_service_manager.cpp`, `DecryptSharedFont`, `GetSharedFontInOrderOfPriority`) |
| profile | 4 | the profile a game shows in its menus: one base twice, the picture a 256x256 JPEG of the size told, and the user's existence (`hle/service/acc/acc.cpp`, `IProfileCommon`, and `acc/profile_picture.cpp`, `GetProfilePictureForGame`) |
| am-state | 5 | what the application's state getter tells a game before its first frame: the mode and its resolution, the focus, the start-up logo's message, 3D VR mode, the platform region (`hle/service/am/service/common_state_getter.cpp`) |
| am-self | 2 | two settings a game makes on its self controller, read back: the idle time detection extension and auto sleep (`hle/service/am/service/self_controller.cpp`) |
| am-functions | 3 | what an application's functions tell a game before its title screen: the language, the events it waits on, the channels a launch may fill (`hle/service/am/service/application_functions.cpp`) |
| auddev | 3 | the audio device service a game with a renderer asks: the active output among those listed, and its volume read back as set (`hle/service/audio/audio_device.cpp`, `audio_core/renderer/audio_device.cpp`) |
| fsdir | 3 | a game's own files on the memory card: a directory renamed with what it holds, listed, and deleted with it (`hle/service/filesystem/fsp/fs_i_filesystem.cpp`, `RenameDirectory`, and `hle/service/filesystem/filesystem.cpp`) |
| am-audio | 2 | the volumes a game expects for itself and a library applet, set and read back (`hle/service/am/service/audio_controller.cpp`) |
| am-channel | 3 | the user channel an application's programs hand data through, and the program that ran before (`hle/service/am/service/application_functions.cpp`) |
| am-storage | 3 | the storages a game hands a library applet its arguments in: size, bytes, and the end (`hle/service/am/library_applet_storage.cpp`) |
| mii | 4 | the console's six default Miis a game lists with the player's, and the database's state (`hle/service/mii/mii_manager.cpp`) |
| am-mode | 2 | the state getter's modes: VR mode turned on and off, the boot mode, the controller update section (`hle/service/am/service/common_state_getter.cpp`) |
| bsd-local | 4 | bsd:u with no network: a raw socket, a timeout out of range or short, an address into a short buffer (`hle/service/sockets/bsd.cpp`) |
| ipc | 3 | what every session answers: its pointer buffer, a clone, a Close (`hle/service/sm/sm_controller.cpp`, `hle/service/service.cpp`) |
| hid | 1 | the npad LIFOs of hid's shared memory, which every game reads its controllers from (`hid_core/resources/ring_lifo.h`) |
| time-snap | 2 | the clock snapshot a game dates a save or times a wait across a sleep with, and the span between two (`hle/service/psc/time/static.cpp`) |
| vi | 3 | the application display service every game opens first: the displays listed, the scaling modes converted, an indirect layer's memory (`hle/service/vi/application_display_service.cpp`) |
| renderer | 3 | an audio renderer of the program's own: what it was opened with, the rendering time limit and the voice drop parameter (`hle/service/audio/audio_renderer.cpp`) |
| ngc | 3 | the profanity filter a game runs a player's name through: ngc:u's version, check and mask, and ngct:u's match and filter (`hle/service/ngc/ngc.cpp`) |
| am-lock | 2 | the lock accessors a game holds the home button off with: taken as libnx takes it, held, and their event (`hle/service/am/service/lock_accessor.cpp`) |
| set-sys | 3 | the system settings a homebrew program reads and writes on set:sys: the color set, the keyboard layout, the vibration volume and four flags (`hle/service/set/system_settings_server.cpp`) |
| time-zone | 3 | time:u's time zone service: every location name it counts listed with the device's, the device's location with when it was set, and a local date turned back (`hle/service/glue/time/time_zone.cpp`, `psc/time/time_zone_service.cpp`) |
| time-perm | 2 | what time:u, an application's time service, may not write: a clock, automatic correction, the steady clock's offset, the device's location (`hle/service/psc/time/system_clock.cpp`, `steady_clock.cpp`, `static.cpp`, `glue/time/time_zone.cpp`) |
| audout | 3 | the plain PCM output: one output listed and opened as asked, its volume kept, buffers held while stopped and given back when it stops (`audio_core/out/audio_out_system.cpp`) |
| fs-file | 5 | a game's own files on the memory card, as a save writer handles them: resized, renamed into place, a directory cleaned, a file's time stamp, the free space (`hle/service/filesystem/fsp/fs_i_file.cpp`, `fs_i_filesystem.cpp`, `filesystem.cpp`) |

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
| a configuration set is read back for its mode alone | GetPerformanceConfiguration (1) for both modes, SetPerformanceConfiguration (0) for Normal, 1 for both, 0 back | Normal reads what was set and Boost what it read first | exact |
| SetCpuBoostMode gives boost mode the configuration of its clocks | the state getter's SetCpuBoostMode (66) with FastLoad, 1 for Boost, 66 with Normal, 1 for Boost | FastLoad gives `0x92220009` (Config13) and Normal `0x00020003` (Config7) | exact |

reden keeps one configuration per mode, Config7 for both at first, and its state getter's
SetCpuBoostMode hands the mode to apm, which gives Boost mode Config7, Config13 or Config15 by it
(`apm_controller.cpp` at 75c12299a6, `SetFromCpuBoostMode`). Both cases skip as `depends` when apm
opened no session; the first puts Normal's configuration back, the second ends at Normal.

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

The program has no add-on content: reden counts the contents installed under the program's base
id and lists their indices from the offset, the fewer of what it has and what is asked, so a list
from 0 is empty however much it asks for (`addon_content_manager.cpp:96-142` at 75c12299a6).
GetAddOnContentBaseId (5) gives the base id from the program's control data, and to a program
with none, as this one, the program's id with its low 13 bits cleared and 0x1000 added
(`GetAOCBaseTitleID`, `file_sys/common_funcs.h`); the case reads the program's id with
`svcGetInfo` (ProgramId). A list asked for more indices than its buffer holds has no case:
reden answers it as any other, Ryujinx refuses it with InvalidBufferSize, (166, 200), `0x190A6`.
reden's PrepareAddOnContent (7) is a stub that answers success for any index, as Ryujinx's
does. The commands by application id (0, 1, 4 and 6) have no handler in reden, so no case sends
them.

| case | what it asks | passes when | tier |
|---|---|---|---|
| aoc:u opens and counts this program's add-on content | aoc:u and 2 | both answer | none |
| ListAddOnContent lists nothing at the end and refuses one past it | 3 from the count, then from the count and one | the first succeeds with none listed, the second is refused with `0x4A80` | exact |
| aoc:u counts no add-on content and lists none from 0 | 2, then 3 from 0 with room for 64 | none counted, and the list succeeds with none | exact |
| a list for one into room for one, or for none, lists none | 3 from 0 for one into room for one, then for none into room for 64 | both succeed with none listed, and the 63 words past the room for one keep `0x5A5A5A5A` | exact |
| the add-on content base id is the program's base and 0x1000 | `svcGetInfo` (ProgramId), then 5 | both succeed, and 5 gives the program's id with its low 13 bits cleared and 0x1000 added | exact |
| PrepareAddOnContent of an index not installed succeeds | 7 for index 1, when none is counted | it succeeds | exact |

Each line carries what was asked, the answers and how many were listed. Nothing is mounted.

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

## profile

A game that shows who is playing asks acc:u0 for the user's profile, GetProfile (5), and reads its
base, Get (0) with the user data or GetBase (1) without, and its picture, GetImageSize (10) and
LoadImage (11). reden hands a game the user's stored picture as a JPEG of 256 by 256, at most
0x20000 bytes, made one on the fly when it is not (a PNG, a picture of another size), and its own
default when there is none; GetImageSize tells the size of the same picture LoadImage writes
(4455b6a3ef, 9c5d9e606e, d88b4c3e4e; `profile_picture.h:17-19`, `acc.cpp:693-711` at 75c12299a6).
Before them both commands answered a size of nought and nothing.

The user is the one the am suite asks about, from acc:u0 opened the same way.

| case | what it asks | passes when | tier |
|---|---|---|---|
| acc:u0 hands out the profile of a user | the user, then 5 | the profile comes | none |
| Get and GetBase tell the same user, by a name | 0 with 0x80 bytes of user data, then 1 | both the asked user, the same base twice, a nickname not empty that ends inside its 0x20 bytes | exact |
| the user's picture is a 256x256 JPEG of the size GetImageSize tells | 10, then 11 into 0x20000 bytes | the same size twice, FF D8 to FF D9, a frame of 256 by 256 | exact |
| GetUserExistence finds the user and not one made up | acc:u0 1 for the user and for its id turned over | the first true, the second false | exact |

The picture's line carries both sizes, its first and last two bytes and the frame's size; the
nickname is printed with control characters and quotes as `?`.

## am-state

Before its first frame a game asks the application's common state getter, ICommonStateGetter,
how to lay itself out and when to start: GetOperationMode (5) and GetDefaultDisplayResolution (60),
GetCurrentFocusState (9), and from 21.0.0 EnableStartupLogoDisappearedMessage (130), after which a
message says the start-up logo is gone. reden (`common_state_getter.cpp` at 75c12299a6) answers the
resolution of the mode it is in, 1280x720 handheld and 1920x1080 docked; enqueues
StartupLogoDisappeared (95) on 130 (87e5d0b6e4, #4343); keeps 3D VR mode across BeginVrMode3d
(1000), EndVrMode3d (1001) and IsVrModeEnabled3d (1002), from 19.0.0 (the same commit); and answers
GetSettingsPlatformRegion (300, from 9.0.0) Global (1). A machine without those commands answers
an unknown command's success: no message, VR mode never on, a region of nought.

The getter is the one libnx opened at start. A command a machine says is newer than its firmware
may be refused, and passes that way; answered with nothing done, it fails at any firmware.

| case | what it asks | passes when | tier |
|---|---|---|---|
| the operation mode and the default resolution agree | 5 and 60 | Handheld with 1280x720, or Console with 1920x1080 | exact |
| a running application is in focus | 9 | InFocus (1); skipped as `unavailable` for a program that is not an application | exact |
| EnableStartupLogoDisappearedMessage brings message 95 | 130, then ReceiveMessage (1) until it refuses, at most 16 | 95 among the messages; or 130 refused by a machine older than 21.0.0 | exact |
| BeginVrMode3d turns 3D VR mode on and EndVrMode3d off | 1000, 1002, 1001, 1002 | on, then off; or 1000 refused by a machine older than 19.0.0 | exact |
| the settings platform region is Global | 300 | Global (1); or refused by a machine older than 9.0.0 | exact |

The logo case's line carries every message it received and what ended them (ResultNoMessages,
AM 3, `0x680`, when none was left). Messages left from before go with them: the program's own
loop does not need them.

## am-self

A game that plays a long scene without input, a film or a race watched, tells its self controller,
ISelfController, not to dim the screen or sleep: SetIdleTimeDetectionExtension (62) and, from
5.0.0, SetAutoSleepDisabled (68), and reads them back with GetIdleTimeDetectionExtension (63) and
IsAutoSleepDisabled (69). reden keeps both (`self_controller.cpp:298-349` at 75c12299a6); a
machine that answers the reads with a stub's nought makes a game that checks believe its request
did nothing.

| case | what it asks | passes when | tier |
|---|---|---|---|
| the idle time detection extension set is the one read back | 63, 62 to Extended (1) or None (0), whichever it was not, 63, 62 back | the read after the write is what was set | exact |
| auto sleep disabled is read back as set | 69, 68 to the other, 69, 68 back; skipped as `unavailable` before 5.0.0 | the read after the write is what was set | exact |

Both settings are put back as they were before the case returns; the line carries all four
answers.

## am-functions

Before its title screen a game asks its own functions, IApplicationFunctions, which language to
show, GetDesiredLanguage (21); the events it will wait on, GetGpuErrorDetectedSystemEvent (130, from 8.0.0),
GetFriendInvitationStorageChannelEvent (140), GetNotificationStorageChannelEvent (150) and
GetHealthWarningDisappearedSystemEvent (160), the last three from 9.0.0 (libnx `applet.c`); and
what a launch left for it, TryPopFromFriendInvitationStorageChannel (141) and PopLaunchParameter
(1). reden (`application_functions.cpp` at 75c12299a6) converts the language it picks into a
settings code; hands out an event for each of the four; and refuses an empty channel with ResultNoDataInChannel, AM 2, `0x480`.

The suite needs the application's functions, which only a program running as an application has;
anywhere else every case is skipped as `unavailable`.

| case | what it asks | passes when | tier |
|---|---|---|---|
| GetDesiredLanguage is one of the codes set lists | 21, and set's 5 | a code that is not nought, in set's list | exact |
| the events an application asks for at start are handed out | 130, 140, 150 and 160, each the firmware has | each one a success with a handle | exact |
| nothing waits in the friend invitation channel or the user channel | 141, then 1 with the user channel (1) | both refused with `0x480`, 141 only when the firmware has it | exact |

Each event handed out is closed at once; a storage handed out is closed without being read.

## auddev

A game that plays through the audio renderer asks the audio device service which outputs there
are and which one plays, and sets that one's volume: audren:u's GetAudioDeviceService (2), then
ListAudioDeviceName, GetActiveAudioDeviceName and Get and SetAudioDeviceOutputVolume, the Auto
commands from 3.0.0 (6, 10, 8, 7) as libnx's `auddev` sends them. reden lists its outputs, names
AudioTvOutput the active one, and keeps the volume set on that one (`audio_device.cpp:57-127` at
75c12299a6, and `audio_core/renderer/audio_device.cpp` for the names).

| case | what it asks | passes when | tier |
|---|---|---|---|
| audren:u hands out the audio device service | libnx's `auddevInitialize` | it succeeds | none |
| the active output is one of the devices listed | the list, room for 8, and the active name | at least one listed, the active name not empty and among them | exact |
| a volume set on the active device reads back | its volume, set to 0.5 (0.25 when it was 0.5), read again, set back | the read after the set is what was set | exact |

The volume is put back as it was before the case returns; the line carries all four answers.

## fsdir

A game that keeps its own files moves a directory to replace an old one, lists what a directory
holds, and deletes one with what is in it: IFileSystem's RenameDirectory (6), OpenDirectory (9)
with IDirectory's GetEntryCount (1) and Read (0), and DeleteDirectoryRecursively (4). reden
1f03dee126 (#4312) made a directory renamed inside its own parent keep what it holds, where the
rename had left a save without its files.

The suite works on the memory card libnx mounted at start, under `/edenprobe/service/directory`,
which it deletes first, so a run left half done does not change the next.

| case | what it asks | passes when | tier |
|---|---|---|---|
| a directory renamed takes what it holds | CreateDirectory `directory` and `directory/a`, CreateFile `a/f` of 16 bytes, RenameDirectory `a` to `b`, then GetEntryType of `b`, `a` and `b/f` | `b` a directory, `a` not there (`0x202`), `b/f` a file | exact |
| the renamed directory lists its one file, once | OpenDirectory `b`, GetEntryCount, Read with room for four, Read again | a count of one, `f` as a file of 16 bytes, then nothing | exact |
| DeleteDirectoryRecursively takes a directory and what it holds | 4 of `directory`, then GetEntryType of it | it succeeds and the directory is not there | exact |

The second and third cases are skipped as `depends` when the rename did not come back as a
directory.

## am-audio

Before a game shows the keyboard, an error or another library applet, it tells its audio
controller, IAudioController, how loud it and the applet should be (SetExpectedMasterVolume, 0),
and after, reads what it set to put it back (GetMainAppletExpectedMasterVolume, 1, and
GetLibraryAppletExpectedMasterVolume, 2). reden keeps both volumes on the controller, a quarter
and one at first, and ChangeMainAppletMasterVolume (3) sets the main one too
(`audio_controller.cpp` at 75c12299a6); SetTransparentVolumeRate (4) is kept with nothing to read
it back.

| case | what it asks | passes when | tier |
|---|---|---|---|
| the expected master volumes set are the ones read back | 1 and 2, 0 with a half and a quarter, 1 and 2 again, 0 back | both reads after the write are what was set | exact |
| a main applet volume changed is read back | 1 and 2, 3 to three quarters with no fade, 1, 4 with a half, 0 back | every step succeeds and the main read is what was changed | exact |

Both cases put the volumes back as they read them, or at one each when the first read failed; the
line carries every answer, the volumes in thousandths.

## am-channel

An application made of several programs (a collection, a game with a separate launcher) starts
the next one with ExecuteProgram and hands it data through the user channel: it clears the
channel (ClearUserChannel, 121), puts a storage in it (UnpopToUserChannel, 122), and the program
started takes it out with PopLaunchParameter(UserChannel) (1); GetPreviousProgramIndex (123)
tells a program which one launched it. reden keeps what is unpopped for the next pop, the last
one first, and starts every application with -1 as the previous program (`application_functions.cpp`
and `applet.h` at 75c12299a6).

| case | what it asks | passes when | tier |
|---|---|---|---|
| a storage unpopped to the user channel pops back with its bytes | a storage of 16 bytes written, 122, 1 with UserChannel, the bytes read, 1 again; skipped as `unavailable` before 6.0.0 | the bytes are the ones written and the second pop answers NoDataInChannel | exact |
| the last storage unpopped pops first and a clear empties the channel | two storages unpopped, 1, 121, 1 again; skipped as `unavailable` before 6.0.0 | the pop gives the second one and the pop after the clear answers NoDataInChannel | exact |
| the previous program index is -1 for the first program | 123; skipped as `unavailable` before 5.0.0 | it succeeds with -1 | exact |

All three skip as `unavailable` in a program that is not an application. The channel is left
empty, as the am-functions suite found it.

## am-storage

A game that shows the keyboard, an error or any other library applet writes the applet's
arguments into storages it creates with CreateStorage (ILibraryAppletCreator 10) and opens with
Open (IStorage 0): GetSize (IStorageAccessor 0), Write (10) and Read (11). reden's storage is a
buffer of the size asked, and a write or a read whose end is past the storage's is refused with
InvalidOffset (`library_applet_storage.cpp:17-24` at 75c12299a6), before any byte moves.

| case | what it asks | passes when | tier |
|---|---|---|---|
| a storage has the size asked and gives back what is written | a storage of 0x40, GetSize, 16 bytes written at 0x20 and read back | every step succeeds, the size is 0x40 and the bytes match | exact |
| a write past a storage's end is refused | 16 bytes written at 0x38 in a storage of 0x40 | InvalidOffset (128, 503) | exact |
| a read past a storage's end is refused | 16 bytes read at 0x38 in a storage of 0x40 | InvalidOffset (128, 503) | exact |

Each case closes its storage before it returns.

## mii

A game with Miis (a racer, a fighter, a party game) opens mii:u, its database service
(GetDatabaseService, 0), and lists the Miis it may show: the player's from the database and the
console's six default ones. reden counts and gives the six for the default source, each the
build BuildDefault makes of its index with a create id of its own, answers false to IsUpdated for
a session that never asked the database and to IsFullDatabase for a database with room, and
refuses a default index past the sixth with InvalidArgument (`mii_manager.cpp:55-72, 308-430` and
`mii.cpp:71-142` at 75c12299a6).

| case | what it asks | passes when | tier |
|---|---|---|---|
| the default source counts six Miis | GetCount (2) of the default source | it succeeds with 6 | exact |
| the default source gives the six default Miis BuildDefault builds | Get (3) of the default source into room for eight, then BuildDefault (7) of each index | six Miis, each of source Default (1) and the same build past its create id | exact |
| the database is neither updated nor full | IsUpdated (0) of the default source, IsFullDatabase (1) | both succeed with false | exact |
| a default Mii past the sixth is refused | BuildDefault of index 6 | InvalidArgument (126, 1) | exact |

The four skip as `unavailable` when mii:u or its database does not open; the session is closed
after.

## am-mode

A game with a VR scene turns VR mode on as it enters it and off as it leaves, and reads it back
(IsVrModeEnabled, 50); libnx turns it with BeginVrModeEx (53) and EndVrModeEx (54) from 7.0.0 and
SetVrModeEnabled (51) before. reden keeps the mode on the applet, answers GetBootMode (8) with a
normal boot and IsInControllerFirmwareUpdateSection (55) with false (`common_state_getter.cpp` at
75c12299a6).

| case | what it asks | passes when | tier |
|---|---|---|---|
| VR mode turned on reads on, and off once turned off | 50, on, 50, off, 50; skipped as `unavailable` before 6.0.0 | every step succeeds, on after on and off after off | exact |
| the boot is normal and no controller firmware update is running | 8 and 55; skipped as `unavailable` before 3.0.0 | both succeed, a boot mode of 0 and false | exact |

VR mode is left off.

## bsd-local

A game's socket layer (the SDK's, libnx's here) opens bsd:u and expects the answers of the BSD
stack behind it, before any network is up: no raw socket other than ICMP on the user's service,
EDOM for a timeval out of range and EINVAL for one too short in SO_RCVTIMEO and SO_SNDTIMEO, and an
address cut to the buffer it is read into, with the length cut. reden answers so since a41a98028a
(the raw socket), afe7a00cde (the timeouts) and 1b7f7eedcb (the address), in `bsd.cpp` at
75c12299a6. The net family asks bsd with a network; these four need none.

| case | what it asks | passes when | tier |
|---|---|---|---|
| a raw socket on bsd:u is refused with EINVAL | socket(AF_INET, SOCK_RAW, 0) | -1, errno EINVAL | exact |
| a socket timeout out of range is EDOM | on a UDP socket, SO_RCVTIMEO {0, 1000000}, SO_SNDTIMEO {-1, 0} | each -1, errno EDOM | exact |
| a socket timeout shorter than a timeval is EINVAL | SO_RCVTIMEO with 4 bytes | -1, errno EINVAL | exact |
| getsockname into 8 bytes writes 8 and says 8 | a UDP socket bound to 127.0.0.1 port 0, getsockname with a length of 8 into 16 bytes | it succeeds, says 8, and the 8 bytes past stay as they were | exact |

The four skip as `unavailable` when socketInitializeDefault fails; every socket is closed and the
driver let go before the suite returns.

## ipc

Every service session answers the IpcController's commands besides its own: a game's IPC layer
asks QueryPointerBufferSize (3) before it sends a pointer buffer and clones a session with
CloneCurrentObject (2) to call it from another thread, and every session ends with a Close
(CMIF message type 2). reden answers the program's pointer buffer, 0x8000 for an NRO, serves the
same object on a clone, and answers a Close by closing the session, so the client reads
SessionClosed (0xF601) for it and for anything sent after (`sm_controller.cpp` and, from
28ab4a1a01, `service.cpp:128-133` at 75c12299a6). The three ask set, opened by name with
smGetServiceOriginal, outside of libnx's own session to it.

| case | what it asks | passes when | tier |
|---|---|---|---|
| a session of set has a pointer buffer | QueryPointerBufferSize | it succeeds with a size above nought | exact |
| a clone of a session answers as the session does | CloneCurrentObject, GetLanguageCode (0) on both | all succeed, the clone is a handle, and the two codes are the same and not nought | exact |
| a Close is answered SessionClosed, and so is the next request | a Close (cmifMakeCloseRequest) with svcSendSyncRequest, then GetLanguageCode on the handle | 0xF601 for both | exact |

Every handle is closed before the suite returns.

## hid

Every game reads its controllers from hid's shared memory: per npad, seven LIFOs of seventeen
entries, each entry a storage number before the state it holds. reden numbers the storage twice
the state's sampling number (`state.sampling_number << 1`, `ring_lifo.h:50` at 75c12299a6) and
counts each npad's states one per entry, an empty entry of a controller not connected too, since
07f40d5cac (Eden #4442) stopped the empty one from taking the storage's doubled number. libnx
reads an entry again while its storage number moves under the copy, and drops states that are not
one apart.

| case | what it asks | passes when | tier |
|---|---|---|---|
| each npad entry numbers twice its state, the states one apart | the newest entry and the one before it of every npad LIFO with two entries or more, with no input asked | there is one at least, and in each the storage number is twice the state's and the state before is one less | exact |

An entry whose storage number moves under eight copies in a row is counted as torn and left out.

## time-snap

A game that dates a save or times something across a sleep (a plant that grows, a stamina that
fills) takes a clock snapshot (GetClockSnapshot, time:u 400) and later the span between two
(CalculateSpanBetween, 501). reden fills the snapshot from the user clock's context, the steady
clock and the device's location, the user time being the context's offset plus the steady time
point when both are of one source, and takes the span from the two steady time points
(`psc/time/static.cpp:232-341` at 75c12299a6).

| case | what it asks | passes when | tier |
|---|---|---|---|
| a clock snapshot holds the user clock, its time and the location | 400 of the user clock (type 0), ISystemClock 2 of the user clock, ITimeZoneService 0 | the snapshot's user context is the clock's, its user time the offset plus its steady time point of one source, its location the device's, its type 0 | exact |
| the span between two snapshots is their steady clocks' difference | 400, then 501 of the snapshot and itself, and of it and a copy five seconds later on the steady clock | nought, then five seconds in nanoseconds | exact |

The suite opens time:u of its own and closes it before it returns; the two skip as `unavailable`
when it or its clocks do not open.

## vi

Every game opens the application display service before its first frame, through vi:u, and asks
it about the display and its layer. reden lists one display, "Default", with a limited layer
count of one and 1920x1080 (`vi_types.h`, DisplayInfo), converts the five scaling modes a game
names (none, freeze, scale to window, scale and crop, preserve aspect ratio) to the window's own
numbering, 3, 0, 1, 2 and 4, refusing the next with OperationFailed, and gives an indirect layer's
image memory as its RGBA bytes rounded up to 0x20000, aligned to 0x1000
(`application_display_service.cpp` at 75c12299a6).

| case | what it asks | passes when | tier |
|---|---|---|---|
| ListDisplays lists the default display alone | ListDisplays (1000) into room for four | one display, "Default", limited to one layer, 1920x1080 | exact |
| each scaling mode converts to the window's, and one past it is refused | ConvertScalingMode (2102) of 0 to 5 | 3, 0, 1, 2 and 4, then OperationFailed (114, 1) | exact |
| an indirect layer's image needs its bytes rounded to 0x20000 | GetIndirectLayerImageRequiredMemoryInfo (2460) of 1280x720 | 0x3A0000, alignment 0x1000 | exact |

The suite takes vi through libnx's viInitialize, which only counts one more user when the program
already has it, and lets go with viExit.

## renderer

A game's audio library opens its renderer at start and may tune it in play: the share of the
frame the renderer may take (SetRenderingTimeLimit, 8) and how readily it drops voices that would
take longer (SetVoiceDropParameter, 12). reden keeps what the renderer was opened with, answers
GetSampleRate (0), GetSampleCount (1) and GetMixBufferCount (2) from it, starts the limit at 100
percent and the drop parameter at 1, and reads back what was set (`audio_renderer.cpp` and
`audio_core/renderer/system.cpp` at 75c12299a6). The audio family plays through a renderer; this
suite opens one of its own, at 48 kHz with a final mix of 2 buffers, and closes it.

| case | what it asks | passes when | tier |
|---|---|---|---|
| a renderer gives the rate, frame and mix buffers it opened with | 0, 1 and 2 | 48000, 240 and 2 | exact |
| the rendering time limit set is read back | 9, 8 to 70, 9, 8 back | 100 at first, then 70 | exact |
| the voice drop parameter set is read back | 13, 12 to a half, 13, 12 back | 1 at first, then a half (in thousandths on the line) | exact |

The three skip as `unavailable` when audrenInitialize fails.

## ngc

A game runs a name or a message a player typed through the profanity filter before it shows or
sends it: ngc:u checks the text against the word lists of the languages it is given (Check, 1, and
Check2, 4) and masks what it finds (Mask, 2, and Mask2, 5), and ngct:u, from 9.0.0, matches and
filters with one list (Match, 0, and Filter, 1). reden finds nothing and hands every text back as
it was sent, and answers version 1 for its lists (`ngc/ngc.cpp` at 75c12299a6); a clean name is
what a console would give back too.

The text is "edenprobe player" in a buffer of 0x40 bytes; ngc:u takes it by a mapped buffer, ngct:u
by pointer, and the buffer it comes back in is filled with 0xEE first.

| case | what it asks | passes when | tier |
|---|---|---|---|
| ngc:u has a version and finds no word in a clean name | 0, 1 and 4 with the American English list | each succeeds, the version is above nought, and no flag is set | exact |
| a clean name masked comes back as it was sent | 2 and 5 | each succeeds, masks no word, and gives the text and its terminator back byte for byte | exact |
| ngct:u matches nothing in a clean name and filters it back as sent | 0 and 1 | each succeeds, nothing matches, and the text comes back byte for byte | exact |

Each case is skipped as `unavailable` when sm hands out no session to its service.

## am-lock

A game that must not be sent to the home menu for a moment, through a save or a purchase, takes a
lock from its state getter: the home button's (GetHomeButtonReaderLockAccessor, 30) or a reader or
writer lock (31 and 32). libnx's `appletLockAccessorTryLock` waits for nought on the lock's event
before it asks TryLock (1), and `appletLockAccessorLock` waits on it for good, so a lock whose
event is never signaled cannot be taken. reden signals the event when it makes the lock and on
every Unlock (2), and IsLocked (4) says whether it is held (`lock_accessor.cpp` at 75c12299a6).

| case | what it asks | passes when | tier |
|---|---|---|---|
| a free lock is taken as libnx takes it, and held until unlocked | 30; 4; libnx's TryLock; 4; libnx's TryLock again; 2; 4 | every step succeeds; free, taken, held, not taken again, free after the unlock | exact |
| a lock's event is signaled while free and when unlocked | 31 for button 0; a wait of nought; 1 with no handle; the event cleared; a wait; 2; a wait | the free and unlocked waits succeed and the held one times out (`0xEA01`) | exact |

The line carries every step's answer; both locks are unlocked and closed before the suite returns.

## set-sys

A homebrew program reaches set:sys, which an application cannot: it draws in the console's theme
(GetColorSetId, 23), picks its own keyboard's layout (GetKeyboardLayout, 136), and reads how hard a
pad vibrates (GetVibrationMasterVolume, 35) and the flags of the settings menu. reden keeps every
system setting it is given and saves them (`system_settings_server.cpp` at 75c12299a6), so each one
the suite sets reads back as set.

| case | what it asks | passes when | tier |
|---|---|---|---|
| the color set and the keyboard layout set are the ones read back | 23, 24 with the other theme, 23, 24 back; 136, 137 with English (UK) or French, 136, 137 back | every step succeeds and both reads after are what was set | exact |
| the vibration master volume set is the one read back | 35, 36 with a quarter or a half, 35, 36 back | every step succeeds and the read after is what was set | exact |
| four flags turned over are read back, and put back | the lock screen (7, 8), quest (47, 48), battery percentage (99, 100) and speaker auto mute (45, 46) flags: read, turned over, read, put back, read | every step succeeds, each flag reads turned over and then as it was | exact |

Every setting is put back as it was read, or to the console's default when the first read failed,
so a run leaves the settings as it found them; the line carries every answer, the volume in
thousandths. All three are skipped as `unavailable` when set:sys does not open.

## time-zone

A game's settings or calendar lists the locations a console can be set to (GetTotalLocationNameCount,
2, and LoadLocationNameList, 3), reads where the device is and when that was set
(GetDeviceLocationNameAndUpdatedTime, 6), and turns a local date back into the time it came from
under the device's rule (ToPosixTimeWithMyRule, 202). reden lists the names of its time zone binary,
answers the device's location with the steady clock time it was set at, and gives back every time a
local date can be, one or two around a change of offset (`glue/time/time_zone.cpp`,
`psc/time/time_zone_service.cpp` at 75c12299a6). The rule's version (5) goes on the line unjudged:
reden's is its binary's date with a line break, `230326\n`.

| case | what it asks | passes when | tier |
|---|---|---|---|
| as many location names are listed as counted, the device's among them | 0; 2; 3 sixteen at a time from 0 until it gives fewer | every call succeeds, as many are listed as counted, none is empty, the device's is listed | exact |
| the device's location comes with when it was set | 0 and 6 | both succeed with the same name, not an empty one | exact |
| a local date turned back gives the time it came from | the user clock's time (0), 101 with it, 202 with the date, and 5 for the line | every call succeeds and 202 gives one or two times with the clock's among them | exact |

The names are asked for in at most 128 calls, past the six hundred or so a database holds. The
suite opens time:u itself, as the time suite does; all three are skipped as `unavailable` when it
hands out no time zone service.

## time-perm

An application gets time:u, which reads the clocks and the time zone and writes none of them: the
user and network clocks' SetCurrentTime (1) and SetSystemClockContext (3), the steady clock's
SetTestOffset (3), SetStandardUserSystemClockAutomaticCorrectionEnabled (101),
SetStandardSteadyClockInternalOffset (50) and the time zone service's SetDeviceLocationName (1) are
the system settings' and time:a's. reden's time:u has every write permission off and answers each
of them PermissionDenied, Time (116) 1: `0x274` (`glue/glue.cpp` and the checks of
`psc/time/system_clock.cpp`, `steady_clock.cpp`, `static.cpp` and `glue/time/time_zone.cpp` at
75c12299a6).

| case | what it asks | passes when | tier |
|---|---|---|---|
| time:u may not set a clock | the user clock's 1 with its time and 3 with its context, the network clock's 1 with its time, the steady clock's 3 with its offset | each is refused with `0x274` and the user clock reads within two seconds of before | exact |
| time:u may not set automatic correction, the steady clock's offset or the device's location | 101 with the flag 100 reads, 50 with nought, the time zone service's 1 with the name 0 reads | each is refused with `0x274` and 100 and 0 read the same after | exact |

Every write asks for what is there already, so a machine that takes one changes nothing; the
suite opens time:u itself and skips both cases as `unavailable` when it hands out no clocks.

## audout

A program without an audio renderer plays through audout:u: libnx's `audoutInitialize` opens the
default output with OpenAudioOutAuto (3), and the program appends buffers (7) to it, starts it (1),
takes back what was played (8) and stops it (2). reden's output is "DeviceOut", opened at 48 kHz
stereo, PCM16, stopped, at full volume; it holds what is appended while stopped and flushes (11)
only a started out, refuses to start an out already started with OperationFailed (`0x499`), and
gives every buffer back when it stops (`audio_core/out/audio_out_system.cpp` at 75c12299a6).

| case | what it asks | passes when | tier |
|---|---|---|---|
| one output, opened at 48 kHz stereo, stopped, its volume kept | ListAudioOuts (0); 3 with "NoSuchOut"; what `audoutInitialize`'s open gave; GetAudioOutVolume (13), SetAudioOutVolume (12) to a half, 13 | one output, "DeviceOut"; the other name refused with `0x299`; 48000 Hz, 2 channels, format 2, state 1; volume 1 and then a half | exact |
| buffers appended to a stopped out wait, held and not flushed | two buffers of silence appended (7); 9; 6 for one of them and for another address; 8; 11; 9 | every call succeeds; both counted, the one held and the other not, none released; 11 says nothing was flushed and both are still counted | exact |
| an out starts once, and its Stop gives every buffer back | 1; GetAudioOutState (0); 1 again; 2; 0; 8; 9 | Started (0), then `0x499`; Stopped (1) with both tags released in the order appended and none counted | exact |

The third case appends its own two buffers when the second did not leave them. The volume is put
back as it was read, the out is stopped and closed before the suite returns, and all three are
skipped as `unavailable` when `audoutInitialize` fails.

## fs-file

A game that writes a save grows or cuts a file to the size of what it writes (IFile SetSize, 3),
writes a new copy beside the old one and renames it into place (RenameFile, 5), clears a directory
of what it held (CleanDirectoryRecursively, 13), reads when a file was written
(GetFileTimeStampRaw, 14), and checks the room left first (GetFreeSpaceSize, 11, and
GetTotalSpaceSize, 12). reden does each on the host's files (`fsp/fs_i_file.cpp`,
`fsp/fs_i_filesystem.cpp`, `filesystem.cpp` and `vfs_real.cpp` at 75c12299a6): a file grown reads
nought past its bytes, a rename onto a name taken is PathAlreadyExists (`0x402`), a directory that
holds a file is not deleted (reden's answer is ResultUnknown, `0xFFFFFFFF`, where a console's
would be DirectoryNotEmpty, so the case takes any refusal), and a file is dated by the host's clock.

| case | what it asks | passes when | tier |
|---|---|---|---|
| a file grown reads nought past what it held, and shrunk ends there | four bytes written, 3 to 16, 4, a read of 16; 3 to 2, 4, a read of 16 | sizes 16 and 2; the four bytes then nought, then the first two alone | exact |
| a file renamed is found by its new name alone, not onto another | 5 of a four-byte file to a free name; 7 of both names; its size; 5 of another file onto it; 7 of that one | the old name is `0x202`, the new one a file of four bytes; the second 5 is `0x402` and the other file still there | exact |
| a directory with a file is not deleted, and cleaned is left empty | DeleteDirectory (3) of it; 7 of the file; 13; 7 and the entry count of the directory; 3 again | the first 3 refused with the file kept; the directory there and empty; the second 3 succeeds | exact |
| a file just written is dated by the clock | the user clock; a file written; 14 | its created, modified and accessed times each within two minutes of the clock | exact |
| the memory card's free space is within its total | 11 and 12 of the root | both succeed, the free space above nought and no more than the total | exact |

Each file is made at its size and written inside it: a write past the end of a file not opened to
append is refused on a console (FileExtensionWithoutOpenModeAllowAppend, `0x307202`), which reden
does not check. Everything is made under `/edenprobe/service/file` on the card, which the suite
deletes before and after; all five are skipped as `unavailable` when the card is not mounted.

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
- **Q11.** What a console hands a game as the picture of a user who never chose one: reden its
  own default JPEG; and whether a console's Get with user data and GetBase can ever differ.
- **Q12.** Whether a console of 21.0.0 sends message 95 at once on 130 when the logo is long
  gone, as reden does, or only when it goes; whether its 3D VR mode shows on the screen while the
  case has it on; and the region a console sold in China answers (reden always says Global).
- **Q13.** Whether a console lets an application set the idle time detection extension to None
  after it was Extended, and keeps it until the application exits, as reden does.
- **Q14.** Whether a console's user channel is ever filled for a program nobody launched with
  one.
- **Q15.** Which output a console names active when it is docked, handheld and with
  headphones (reden always AudioTvOutput), and whether it keeps a volume set on an output that
  is not the active one.
- **Q16.** Whether a console's memory card keeps a directory's entries in the order they were
  made when it lists them (the case makes one), and the result it gives a rename whose new name
  is already there.
- **Q17.** Whether a console keeps the expected master volumes an application sets and gives
  them back, as reden does, and whether it clamps a volume past one or refuses it.
- **Q18.** Whether a console's user channel gives the last storage unpopped first, as reden's
  does, and whether an application launched from the home menu reads -1 as its previous program.
- **Q19.** Whether a console refuses a read that runs past a storage's end with InvalidOffset, as
  reden does, or reads what is there.
- **Q20.** Whether a console's default Miis come out of Get in BuildDefault's order, as reden
  gives them, and whether each Get makes their create ids anew.
- **Q21.** Whether a console lets an application turn VR mode on and back off, and reads it back
  each time, as reden does.
- **Q22.** Whether a console's bsd:u answers getsockname into a short buffer with the length
  cut, as reden does and a BSD stack does, or the address's whole length, as Linux does.
- **Q23.** Whether a console's set answers a Close with SessionClosed, as reden does and as libnx's
  serviceClose ignores, and how large its pointer buffer is.
- **Q24.** Whether a console numbers an npad entry's storage twice its state, as reden does, and
  keeps writing the LIFOs of an npad with nothing connected.
- **Q25.** Whether a console's snapshot gives the user time as the context's offset plus its own
  steady time point, as reden does, and the span in nanoseconds.
- **Q26.** Whether a console lists only the default display to an application, as reden does, and
  reckons an indirect layer's memory in blocks of 0x20000.
- **Q27.** Whether a console starts a renderer's voice drop parameter at 1 and its rendering time
  limit at 100, as reden does, and reads both back as set.
- **Q28.** Whether a console refuses a list asked for more indices than its buffer holds, as
  Ryujinx does with InvalidBufferSize (166, 200), or answers it, as reden does; and whether its
  GetAddOnContentBaseId is the program's base with 0x1000 added or its control data's own value,
  where the two differ. On a console the aoc cases need a title with no add-on content.
- **Q29.** Whether a console refuses PrepareAddOnContent of an index the program does not have,
  and with what: reden answers success for any index, from a stub.
- **Q30.** Which version a console's ngc:u answers, which buffer types its Check and Mask take
  (the suite sends mapped ones), and whether the pattern list's bit 1 is American English.
- **Q31.** Whether a console's apm session keeps a configuration per mode as reden does, and
  whether its SetCpuBoostMode with FastLoad gives Boost mode Config13 (`0x92220009`).
- **Q32.** Whether a console signals a lock accessor's event from the start, as reden does
  and as libnx's TryLock needs, and whether a reader lock and a writer lock of one button share it.
- **Q33.** Whether a console's set:sys takes each of these writes from a homebrew program and
  gives it back at once, as reden does.
- **Q34.** How many location names a console counts, what its GetTimeZoneRuleVersion answers
  (reden's is `230326\n`), and whether GetDeviceLocationNameAndUpdatedTime dates the location by
  the steady clock.
- **Q35.** Whether a console's time:u refuses each of these writes with PermissionDenied
  (`0x274`), as reden's does.
- **Q36.** Whether a console's audout refuses a second Start with OperationFailed and
  flushes nothing while stopped, as reden and Ryujinx do.
- **Q37.** What a console answers DeleteDirectory of a directory that holds a file (reden's
  ResultUnknown), and whether its GetFileTimeStampRaw lays out created, modified and accessed in
  that order and sets the flag after them.

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
With no add-on content, aoc passes none counted and none listed, both lists that fit their room
answered with none and nothing past it, and the base id of a program of id nought, of an
application's program 3 and of an id with bit 12 set; one counted, either list refused, one
listed, a word written past the room, the program's own id given back, its program index kept, a
refusal and no program id each fail.
A prepare of a missing index passes answered with success, and fails refused.
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
profile passes one base twice, of the user, by a name, and fails two bases, another
user, no name, a name with no end and a stand-in; a 256x256 JPEG of the size told, and not
another size told, a JPEG of 256 by 128, one cut before its end or a stand-in's nothing; and the
user found with nobody made up, and not everyone found or nobody.
am-state passes Handheld at 1280x720 and Console at 1920x1080, and fails a mode with
the other's resolution and a stand-in; InFocus, and not out of focus or in the background;
message 95 among the messages after 130, and fails 130 answered without it at any firmware and
130 refused on 21.0.0, while passing 130 refused on an older one (`0x680` and `0x480` worked out
by hand: 128 + 3 x 512, 128 + 2 x 512); VR mode on then off, and not never on, never off or a
stand-in, with 1000 refused passing only before 19.0.0; and the region Global, with 300 refused
passing only before 9.0.0.
am-self passes a setting read back as set, and fails a stub's read that stays as before,
another value read back, and a refusal.
am-functions passes a desired language set lists, and fails one it does not list and
a code of nought; every start event handed out, also with the three of 9.0.0 newer than the
firmware, and fails an unknown command's success with no handle and nothing asked; and two
empty channels refused with `0x480`, the friend one only when asked, failing a stub's success,
a storage in the user channel and another result.
auddev passes reden's three outputs with AudioTvOutput active, and fails an active one
not listed, no active name and a stand-in's empty list; and a volume read back as set, failing
the volume before read back, another and a refusal.
fsdir passes a directory renamed with what it holds, and fails the old one left, the
file lost or found as a directory and a refused rename; the one file listed once, failing it
listed twice, as a
directory, with no size and a count of two; and a directory gone with what it held, failing
one still there and a refused delete.
am-audio passes volumes read back as set, and fails a write the reads do not see, half of it
kept, and a refusal.
am-channel passes a user channel that keeps what is unpopped, and fails an unpop that keeps
nothing, other bytes, a channel not emptied by the pop, a queue, a clear that leaves one, and an
index of 0.
am-storage passes a storage that keeps its size and its end, and fails another size, other
bytes read back, a read cut at the end and answered as a success, and a refusal of another kind.
mii passes the six default Miis, and fails a count of nought, an empty or short list, a Mii of
another source or build, a full database, and a seventh default built.
am-mode passes VR mode as turned and a normal boot, and fails a stub's read that stays off,
a mode stuck on, a maintenance boot and an update running.
bsd-local passes the answers of a BSD stack, and fails a raw socket opened, a timeout out of
range taken or refused as EINVAL, a short timeout taken, the whole length said for a cut address,
and a write past the buffer.
ipc passes the controller's answers and a Close, and fails no pointer buffer, a clone that
answers otherwise or gives no handle, a Close answered as a success, and a session alive after
it.
hid passes npad entries doubled and one apart, and fails an entry numbered as its state, states
not one apart, and no LIFO to read.
time-snap passes a snapshot of the user clock and its span, and fails another user time,
context or location, a span in seconds, and a span to itself above nought.
vi passes the default display, the scaling table and the rounding, and fails five displays
listed, a smaller default display, the game's numbering given back, a sixth mode taken, and an
unrounded size.
renderer passes its parameters and two settings kept, and fails another frame or mix count, a
stub's nought read back, and a first read that is not the default.
ngc passes a version and no word found, and fails a version of nought, a word found and a refusal;
a text given back passes, and a buffer left as it was, a text without its terminator, a word
masked, a match and a filter that empties the text each fail.
apm passes a configuration read back for its mode alone and fails a stub's fixed one, a write
both modes take and a refusal; it passes Config13 for FastLoad and Config7 back, and fails Config7
for FastLoad, Config13 kept after Normal and SetCpuBoostMode refused.
am-lock passes a lock taken, held and freed, and fails a TryLock that timed out on its event,
a lock taken twice, one held after its unlock and one not held; it passes an event signaled free
and unlocked and fails one not signaled free, one the clear did not clear and an unlock that
signals nothing.
set-sys passes two settings read back as set, and fails a stub's nought, the value before and a
refusal; it passes a volume read back and fails one of nought; it passes four flags turned over and
put back, and fails a flag not turned over, one not put back and a refusal.
time-zone passes every name listed with the device's, and fails a list cut short, empty names, the
device's missing and none counted; it passes the device's location twice and fails an empty one and
another; it passes the time back, first or second of two, and fails a time an hour off, no time back and
202 refused.
time-perm passes every write refused with `0x274` and nothing changed, and fails a write taken,
another refusal, a clock moved, a location set and no write asked.
audout passes one output opened as asked with its volume kept, and fails another name opened, a
mono out, a stub's volume and two outputs; it passes buffers held while stopped and fails a flush
that takes them, a tag held that was never appended and one released early; it passes a single
Start and every buffer back, and fails a second Start taken, the tags back swapped and one still
counted.
fs-file passes a file grown with nought and cut, and fails a growth not nought, a cut that keeps the
size and SetSize refused; it passes a rename to a free name refused onto a taken one, and fails a
copy that keeps the old name, a rename onto another and an emptied file; it passes a full
directory kept and cleaned empty, and fails a delete that takes the file, a clean that leaves it
and one that takes the directory; it passes stamps within two minutes of the clock and fails a
stub's noughts, a day off and no clock; and it passes the free space within the total and fails
more than the total and none.
