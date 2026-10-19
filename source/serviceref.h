// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The service family's judge: what each case saw a service answer, made into a verdict and the
/// line that says it. No service here, so both builds run it and the reference suite pins it on
/// answers made to order.
///
/// Every expectation is reden's (the commit is named beside it), since no console has run these
/// cases yet; the open question each one leaves is in docs/families/Service.md.

/// A result as the console writes it: the module, and the description nine bits up.
#define SERVICE_RESULT(module, description) ((uint32_t)(module) | ((uint32_t)(description) << 9))

/// hwopus refusing a decoder past its limit: ResultOutOfOpusDecoders, HwOpus (111) 385. reden
/// 00a1c392e7 read the limit, 24 live decoders, and the result from the firmware's
/// nn::codec::InitializeHardwareOpusDecoder, which falls back to the software decoder on it.
#define SERVICE_OPUS_REFUSED SERVICE_RESULT(111, 385)
#define SERVICE_OPUS_LIMIT 24u
/// Two past the limit, so a machine with no limit at all shows it with room to spare.
#define SERVICE_OPUS_TRIES (SERVICE_OPUS_LIMIT + 2u)

/// What the save size case extends both parts of the account save by, on top of what it reads
/// first: a whole number of the save's 16 KiB blocks, small enough for any memory card.
#define SERVICE_SAVE_STEP 0x100000u

/// The file system's answer for a path that is not there: Fs (2) 1, PathNotFound.
#define SERVICE_PATH_NOT_FOUND SERVICE_RESULT(2, 1)

/// apm's performance configurations the apm suite sets and expects (reden `apm_controller.h`):
/// Config7, both modes' at first; Config1, one the session case sets; Config13, the one
/// SetCpuBoostMode with FastLoad gives Boost mode (`apm_controller.cpp`, `SetFromCpuBoostMode`).
#define SERVICE_APM_CONFIG7 0x00020003u
#define SERVICE_APM_CONFIG1 0x00010000u
#define SERVICE_APM_CONFIG13 0x92220009u

/// set refusing a language past its table: Settings (105) 625, ResultInvalidLanguage (reden
/// settings_server.cpp). The most codes GetAvailableLanguageCodes2 can list, 0x40, is reden's
/// POST_4_0_0_MAX_ENTRIES.
#define SERVICE_INVALID_LANGUAGE SERVICE_RESULT(105, 625)
#define SERVICE_LANGUAGES_MAX 0x40u

/// How many random bytes the csrng case asks for each time, and what it fills them with first.
#define SERVICE_RANDOM_SIZE 64u
#define SERVICE_RANDOM_FILL 0xA5u

/// aoc:u listing from past the end of its list: ResultApplicationRecordNotFound, AM (128) 37,
/// which reden 54cd5fb8eb answers where it had answered ResultUnknown. The most indices the aoc
/// case has room for in one list.
#define SERVICE_AOC_PAST_END SERVICE_RESULT(128, 37)
#define SERVICE_AOC_LISTED 64u
/// The add-on content base id reden gives a program with no control data: the program's base,
/// its id with the low 13 bits cleared, and 0x1000 added (`file_sys/common_funcs.h`,
/// `GetAOCBaseTitleID`). What the room case fills its array with first, to see a write past the
/// room it names.
#define SERVICE_AOC_BASE_MASK 0xFFFFFFFFFFFFE000ull
#define SERVICE_AOC_BASE_OFFSET 0x1000ull
#define SERVICE_AOC_UNTOUCHED 0x5A5A5A5Au
/// The index the prepare case asks aoc:u to prepare, which a program with no add-on content does
/// not have: the first an add-on content can take.
#define SERVICE_AOC_MISSING 1u

/// The time the time cases date, 2023-11-14 22:13:20 in UTC, a Tuesday and the year's 318th day;
/// and how long the steady clock case waits between its two readings, a second and a half, so a
/// clock that counts seconds has moved by one or two.
#define SERVICE_TIME_KNOWN 1700000000
#define SERVICE_TIME_WAIT_NS 1500000000ull

/// pl:u's shared fonts: six of them in one shared memory of 0x1100000 bytes, each behind an
/// 8-byte header whose first word reads 0x18029a7f (reden's DecryptSharedFont writes the
/// decrypted magic 0x7f9a0218 big-endian, Ryujinx's WriteMagicAndSize the same bytes) and whose
/// second is the size XORed with 0x49621806, the key of the .bfttf the fonts come from.
#define SERVICE_FONTS 6u
#define SERVICE_FONT_MEMORY 0x1100000u
#define SERVICE_FONT_MAGIC 0x18029a7fu
#define SERVICE_FONT_KEY 0x49621806u

/// The user's picture as acc hands it to a game: a JPEG of 256 by 256, at most 0x20000 bytes
/// (reden profile_picture.h, PROFILE_PICTURE_SIDE and PROFILE_PICTURE_MAX_SIZE).
#define SERVICE_PICTURE_SIDE 256u
#define SERVICE_PICTURE_MAX 0x20000u

/// The applet manager's results: no message left to receive, ResultNoMessages, AM (128) 3; and
/// a channel with nothing in it, ResultNoDataInChannel, AM 2 (reden am_results.h). The message a
/// program that enabled it gets when the start-up logo is gone: StartupLogoDisappeared, 95
/// (reden am_types.h). The most messages the logo case receives looking for it.
#define SERVICE_AM_NO_MESSAGES SERVICE_RESULT(128, 3)
#define SERVICE_AM_NO_DATA SERVICE_RESULT(128, 2)
#define SERVICE_AM_LOGO_GONE 95u
#define SERVICE_AM_MESSAGES 16u

/// The events an application's functions hand out that a game asks for at start, by command:
/// GetGpuErrorDetectedSystemEvent (130, 8.0.0+), GetFriendInvitationStorageChannelEvent (140),
/// GetNotificationStorageChannelEvent (150) and GetHealthWarningDisappearedSystemEvent (160), the
/// last three 9.0.0+ (libnx applet.c).
#define SERVICE_START_EVENTS 4u

/// The most output devices the audio device case lists, and the length of a device's name
/// (nn::audio::AudioDeviceName, 0x100 bytes).
#define SERVICE_AUDIO_DEVICES 8u
#define SERVICE_AUDIO_NAME 0x100u

/// The file the directory cases make inside the directory they rename, and its size.
#define SERVICE_DIR_FILE_SIZE 16

/// am's InvalidOffset: a storage access past its end (`hle/service/am/am_results.h`).
#define SERVICE_AM_INVALID_OFFSET SERVICE_RESULT(128, 503)
/// The storage the am-storage suite creates, and where it writes in it.
#define SERVICE_STORAGE_SIZE 0x40
#define SERVICE_STORAGE_AT 0x20
#define SERVICE_STORAGE_PAST 0x38

/// mii's InvalidArgument (`hle/service/mii/mii_result.h`), and the default Miis reden builds
/// (RawData::DefaultMii).
#define SERVICE_MII_INVALID_ARGUMENT SERVICE_RESULT(126, 1)
#define SERVICE_MII_DEFAULT_COUNT 6u

/// The errnos bsd answers, in the numbering it and newlib share.
#define SERVICE_EINVAL 22
#define SERVICE_EDOM 33

/// The kernel's SessionClosed: a request on a session whose server closed it.
#define SERVICE_SESSION_CLOSED 0xF601u

/// nn::time::sf::ClockSnapshot, 0xD0 bytes: where the parts the time-snap suite reads are.
#define SERVICE_SNAP_SIZE 0xD0
#define SERVICE_SNAP_USER_TIME 0x40
#define SERVICE_SNAP_STEADY 0x90
#define SERVICE_SNAP_LOCATION 0xA8
#define SERVICE_SNAP_TYPE 0xCD

/// vi's OperationFailed (`hle/service/vi/vi_results.h`).
#define SERVICE_VI_OPERATION_FAILED SERVICE_RESULT(114, 1)

/// The buffers the ngc suite hands the profanity filter: the text in one, what comes back in the
/// other, filled with 0xEE before the call so a buffer left as it was shows.
#define SERVICE_NGC_SIZE 0x40u
/// The word lists ngc:u is asked to look in, as nn::ngc's pattern list flags: American English
/// (bit 1). reden reads none of it.
#define SERVICE_NGC_LANGUAGES 0x2u

/// A wait that timed out: the kernel's TimedOut, Kernel (1) 117, what an event not signaled answers
/// a wait of nought.
#define SERVICE_TIMED_OUT SERVICE_RESULT(1, 117)

/// The flags the set-sys suite turns over and puts back, in the order its line names them: the
/// lock screen, the quest (kiosk) flag, the battery percentage and the speaker's auto mute.
#define SERVICE_SETSYS_FLAG_COUNT 4u

/// The location names the time-zone suite asks for at a time, and the most asks it makes: well
/// past the six hundred or so names a time zone database holds.
#define SERVICE_TZ_CHUNK 16u
#define SERVICE_TZ_ASKS 128u

/// time refusing a write a session may not make: PermissionDenied, Time (116) 1 (reden
/// `psc/time/errors.h`).
#define SERVICE_TIME_DENIED SERVICE_RESULT(116, 1)
/// The writes each time-perm case asks of time:u, in the order its line names them.
#define SERVICE_TIME_CLOCK_WRITES 4u
#define SERVICE_TIME_SETTING_WRITES 3u

/// audout refusing a device name it does not have, NotFound, and a Start of an out already
/// started, OperationFailed: Audio (153) 1 and 2 (reden `audio/errors.h`).
#define SERVICE_AUDIO_NOT_FOUND SERVICE_RESULT(153, 1)
#define SERVICE_AUDIO_FAILED SERVICE_RESULT(153, 2)
/// The released tags the started case reads at once: room past the two it appends.
#define SERVICE_AUDOUT_TAGS 4u

/// The file system refusing a path already taken: Fs (2) 2, PathAlreadyExists.
#define SERVICE_PATH_TAKEN SERVICE_RESULT(2, 2)
/// How far the fs-file suite lets a file's time stamps be from the user clock: two minutes.
#define SERVICE_STAMP_SLACK 120u

/// acc refusing a user id of nought, InvalidUserId, and a thumbnail of another size,
/// InvalidArrayLength: Account (124) 20 and 32 (reden `acc/errors.h`). A save's thumbnail is
/// 256x144 RGBA: 0x24000 bytes (reden `acc.cpp`, THUMBNAIL_SIZE).
#define SERVICE_ACC_INVALID_USER SERVICE_RESULT(124, 20)
#define SERVICE_ACC_INVALID_LENGTH SERVICE_RESULT(124, 32)
#define SERVICE_ACC_THUMBNAIL_SIZE 0x24000u
/// The users the acc-app suite lists at once: as many as a console has.
#define SERVICE_ACC_USERS 8u

typedef enum {
    SERVICE_OPUS_OPENS,   ///< hwopus: one decoder, 48 kHz stereo (none tier)
    SERVICE_OPUS_LIMITED, ///< hwopus: 24 live, the 25th refused
    SERVICE_OPUS_FREED,   ///< hwopus: one closed, the next one opens
    SERVICE_AM_OPENS,     ///< am: the application's functions and a user (none tier)
    SERVICE_SAVE_SIZE,    ///< am: ExtendSaveData, then GetSaveDataSize reads it back
    SERVICE_TEMP_OPENS,   ///< fsp: the temporary storage opens (none tier)
    SERVICE_TEMP_EMPTY,   ///< fsp: what the launch before left there is gone
    SERVICE_ACC_OPENS,    ///< account: acc:u0 and its user count (none tier)
    SERVICE_SELECT_USER,  ///< account: 52 answers as 51 does
    SERVICE_LDN_OPENS,    ///< ldn: ldn:u opens (none tier)
    SERVICE_LDN_MONITOR,  ///< ldn: CreateClientProcessMonitor and its RegisterClient
    SERVICE_LDN_STATES,   ///< ldn: the state after each step, from Initialize to Finalize
    SERVICE_LDN_EVENT,    ///< ldn: the state change event, when a network comes and goes
    SERVICE_LDN_REFUSED,  ///< ldn: commands out of their state refused as BadState
    SERVICE_SET_OPENS,    ///< set: set opens and lists its language codes (none tier)
    SERVICE_SET_LANGUAGE, ///< set: GetLanguageCode is one of them
    SERVICE_SET_MAKE,     ///< set: MakeLanguageCode gives the list's code at each index
    SERVICE_SET_COUNT,    ///< set: GetAvailableLanguageCodeCount2 leaves room for the list
    SERVICE_CSRNG_OPENS,  ///< csrng: csrng opens (none tier)
    SERVICE_RANDOM,       ///< csrng: two asks fill their buffers, differently
    SERVICE_APM_OPENS,    ///< apm: apm and a session (none tier)
    SERVICE_APM_CONFIG,   ///< apm: a configuration set is read back for its mode alone
    SERVICE_APM_BOOST,    ///< apm: SetCpuBoostMode picks the boost mode's configuration
    SERVICE_NFP_OPENS,    ///< nfp: libnx's nfpInitialize for a user (none tier)
    SERVICE_NFP_FILTER,   ///< nfp: StartDetectionWithFilter answers as StartDetection
    SERVICE_AOC_OPENS,    ///< aoc: aoc:u opens and counts its add-on content (none tier)
    SERVICE_AOC_PAST,     ///< aoc: the list's end lists nothing, one past it is refused
    SERVICE_AOC_NONE,     ///< aoc: no add-on content counted, none listed from 0
    SERVICE_AOC_ROOM,     ///< aoc: a list for one into room for one, or for none, lists none
    SERVICE_AOC_BASE,     ///< aoc: the base id is the program's base with 0x1000 added
    SERVICE_AOC_PREPARE,  ///< aoc: PrepareAddOnContent of an index not installed succeeds
    SERVICE_TIME_OPENS,   ///< time: time:u and its clocks (none tier)
    SERVICE_TIME_USER,    ///< time: the user clock is its context's offset past the steady clock
    SERVICE_TIME_STEADY,  ///< time: the steady clock counts seconds on one source
    SERVICE_TIME_UTC,     ///< time: ToCalendarTime under UTC dates two known times
    SERVICE_TIME_POSIX,   ///< time: ToPosixTime under UTC turns the date back
    SERVICE_TIME_MY_RULE, ///< time: the device's location loads, and its rule is the service's own
    SERVICE_PL_OPENS,     ///< pl: pl:u and its shared memory, mapped (none tier)
    SERVICE_PL_FONTS,     ///< pl: six fonts loaded, inside the memory, behind their headers
    SERVICE_PL_ORDER,     ///< pl: GetSharedFontInOrderOfPriority lists the six
    SERVICE_PROFILE_OPENS, ///< profile: acc:u0 hands out a user's profile (none tier)
    SERVICE_PROFILE_BASE,  ///< profile: Get and GetBase tell the same user, by a name
    SERVICE_PROFILE_IMAGE, ///< profile: the picture is a 256x256 JPEG of the size told
    SERVICE_PROFILE_EXISTS, ///< profile: GetUserExistence finds the user, not one made up
    SERVICE_STATE_MODE,   ///< am-state: the operation mode and the default resolution agree
    SERVICE_STATE_FOCUS,  ///< am-state: a running application is in focus
    SERVICE_STATE_LOGO,   ///< am-state: 130 enqueues StartupLogoDisappeared
    SERVICE_STATE_VR,     ///< am-state: BeginVrMode3d and EndVrMode3d, read back
    SERVICE_STATE_REGION, ///< am-state: the settings platform region is Global
    SERVICE_SELF_IDLE,    ///< am-self: the idle time detection extension reads back
    SERVICE_SELF_SLEEP,   ///< am-self: auto sleep disabled reads back
    SERVICE_FUNC_LANGUAGE, ///< am-functions: GetDesiredLanguage is a code set lists
    SERVICE_FUNC_EVENTS,   ///< am-functions: the events asked for at start are handed out
    SERVICE_FUNC_CHANNELS, ///< am-functions: the friend invitation and user channels are empty
    SERVICE_AUDDEV_OPENS,  ///< auddev: audren:u hands out the audio device service (none tier)
    SERVICE_AUDDEV_ACTIVE, ///< auddev: the active output is one of the devices listed
    SERVICE_AUDDEV_VOLUME, ///< auddev: a volume set on the active device reads back
    SERVICE_DIR_RENAME,   ///< fsdir: a directory renamed takes what it holds
    SERVICE_DIR_LIST,     ///< fsdir: the directory lists its one file, once
    SERVICE_DIR_DELETE,   ///< fsdir: DeleteDirectoryRecursively takes it all
    SERVICE_AUDIOCTL_EXPECTED, ///< am-audio: the expected master volumes set are read back
    SERVICE_AUDIOCTL_CHANGE,   ///< am-audio: the main applet volume changed is read back
    SERVICE_CHANNEL_UNPOP,    ///< am-channel: a storage unpopped to the user channel pops back
    SERVICE_CHANNEL_ORDER,    ///< am-channel: the last one unpopped pops first; Clear empties it
    SERVICE_CHANNEL_PREVIOUS, ///< am-channel: no program ran before this one
    SERVICE_STORAGE_KEEPS,      ///< am-storage: a storage has its size and keeps what is written
    SERVICE_STORAGE_WRITE_PAST, ///< am-storage: a write past the end is refused
    SERVICE_STORAGE_READ_PAST,  ///< am-storage: a read past the end is refused
    SERVICE_MII_COUNT,    ///< mii: six default Miis counted
    SERVICE_MII_DEFAULTS, ///< mii: Get of the default source gives the six BuildDefault builds
    SERVICE_MII_STATE,    ///< mii: the database is neither updated nor full
    SERVICE_MII_PAST,     ///< mii: BuildDefault past the six is refused
    SERVICE_MODE_VR,   ///< am-mode: VR mode turned on and off reads back
    SERVICE_MODE_BOOT, ///< am-mode: a normal boot, outside a controller firmware update
    SERVICE_BSD_RAW,           ///< bsd-local: a raw socket on bsd:u is refused with EINVAL
    SERVICE_BSD_TIMEOUT_RANGE, ///< bsd-local: a timeout out of range is EDOM
    SERVICE_BSD_TIMEOUT_SHORT, ///< bsd-local: a timeout shorter than a timeval is EINVAL
    SERVICE_BSD_NAME_SHORT,    ///< bsd-local: getsockname into 8 bytes says 8
    SERVICE_IPC_POINTER, ///< ipc: a session's pointer buffer has room
    SERVICE_IPC_CLONE,   ///< ipc: a clone of a session answers as the session
    SERVICE_IPC_CLOSE,   ///< ipc: a Close is answered SessionClosed, and so is what follows
    SERVICE_HID_SAMPLING, ///< hid: npad LIFO entries number twice their state, states one apart
    SERVICE_SNAP_COHERENT, ///< time-snap: a snapshot holds the user clock and the location
    SERVICE_SNAP_SPAN,     ///< time-snap: the span between snapshots is their steady difference
    SERVICE_VI_DISPLAYS, ///< vi: ListDisplays lists the default display alone
    SERVICE_VI_SCALING,  ///< vi: ConvertScalingMode's table, and the mode past it refused
    SERVICE_VI_INDIRECT, ///< vi: the memory an indirect layer's image needs
    SERVICE_RENDERER_PARAMS, ///< renderer: the rate, frame and mix buffers it was opened with
    SERVICE_RENDERER_LIMIT,  ///< renderer: the rendering time limit set reads back
    SERVICE_RENDERER_DROP,   ///< renderer: the voice drop parameter set reads back
    SERVICE_RENDERER_STATE,  ///< renderer: stopped until started, and stopped again
    SERVICE_NGC_CHECK,  ///< ngc: a version, and nothing found in a clean name
    SERVICE_NGC_MASK,   ///< ngc: a clean name masked comes back as it was sent
    SERVICE_NGCT_CLEAN, ///< ngc: ngct:u matches nothing and filters the name back unchanged
    SERVICE_LOCK_TAKEN, ///< am-lock: a free lock is taken as libnx takes it, held until unlocked
    SERVICE_LOCK_EVENT, ///< am-lock: a lock's event is signaled while free and when unlocked
    SERVICE_SETSYS_LOOK,      ///< set-sys: the color set and keyboard layout set are read back
    SERVICE_SETSYS_VIBRATION, ///< set-sys: the vibration master volume set is read back
    SERVICE_SETSYS_FLAGS,     ///< set-sys: four flags turned over are read back, then put back
    SERVICE_TZ_LIST,    ///< time-zone: as many names listed as counted, the device's among them
    SERVICE_TZ_UPDATED, ///< time-zone: the device's location comes with when it was set
    SERVICE_TZ_BACK,    ///< time-zone: a local date turned back gives the time it came from
    SERVICE_TIME_PERM_CLOCKS,   ///< time-perm: time:u may not set a clock
    SERVICE_TIME_PERM_SETTINGS, ///< time-perm: time:u may not set correction, offset or location
    SERVICE_AUDOUT_OPENS,   ///< audout: one output, opened at 48 kHz stereo, stopped, volume kept
    SERVICE_AUDOUT_STOPPED, ///< audout: buffers on a stopped out wait, held and not flushed
    SERVICE_AUDOUT_STARTED, ///< audout: an out starts once, and its Stop gives every buffer back
    SERVICE_FILE_SIZE,   ///< fs-file: a file grown reads nought past its bytes, shrunk ends there
    SERVICE_FILE_RENAME, ///< fs-file: a file renamed is found by its new name alone
    SERVICE_FILE_CLEAN,  ///< fs-file: a directory with a file not deleted; cleaned, kept empty
    SERVICE_FILE_STAMP,  ///< fs-file: a file just written is dated by the clock
    SERVICE_FILE_SPACE,  ///< fs-file: the memory card's free space is within its total
    SERVICE_ACC_QUALIFIED, ///< acc-app: the qualified users are the users there are
    SERVICE_ACC_THUMBNAIL, ///< acc-app: a save thumbnail is taken at its size, for a user
    SERVICE_CASES
} ServiceCaseId;

/// The suite a case belongs to and its name, as its line carries them.
const char *ServiceCaseSuite(ServiceCaseId id);
const char *ServiceCaseName(ServiceCaseId id);

/// The decoders the limit case opened, in order: @p results[i] is what the (i + 1)th open
/// answered, @p tried how many it asked for, stopping at the first refusal or at
/// SERVICE_OPUS_TRIES. Passes when the first 24 opened and the 25th was refused with
/// SERVICE_OPUS_REFUSED.
bool ServiceJudgeOpusLimit(const uint32_t *results, uint32_t tried, char *out, size_t out_size);

/// After the limit case's refusal and one live decoder closed: @p reopened is what the next open
/// answered. Passes when it opened.
bool ServiceJudgeOpusFreed(uint32_t reopened, char *out, size_t out_size);

/// ExtendSaveData (25) and GetSaveDataSize (26) of IApplicationFunctions, both for the account
/// save of one user: what 26 said before, what 25 was asked and answered, and what 26 said after.
/// Passes when 25 succeeded and 26 then reads back the sizes 25 was asked for, reden d2279cbf8d's
/// pair (the user read at byte 8 of both requests, the size kept); the required size 25 hands
/// back is on the line, not judged.
typedef struct {
    uint32_t before_rc, extend_rc, after_rc;
    uint64_t before_normal, before_journal;
    uint64_t asked_normal, asked_journal;
    uint64_t required;
    uint64_t after_normal, after_journal;
} ServiceSaveSize;
bool ServiceJudgeSaveSize(const ServiceSaveSize *seen, char *out, size_t out_size);

/// The temporary storage at the start of a launch. @p promised is the marker the launch before
/// wrote there, as it said on the memory card (never nought: with no promise there is nothing to
/// look for, and the caller skips the case); @p found_rc what opening the marker answered and
/// @p found what it held when it opened. Passes when the marker is not there, PathNotFound, which
/// is reden f3af5d0c25's rule: the storage is emptied once at each launch of the application.
bool ServiceJudgeTempEmpty(uint64_t promised, uint32_t found_rc, uint64_t found, char *out,
                           size_t out_size);

/// acc:u0's TrySelectUserWithoutInteractionDeprecated (51) and TrySelectUserWithoutInteraction
/// (52, 19.0.0+), both asked with no network account required, and how many users there are.
/// Passes when 52 answers what 51 answers, reden 87e5d0b6e4's two handlers: the user when there
/// is exactly one, an id of nought otherwise. A machine that says it is older than 19.0.0 may
/// refuse 52 instead, as a console of that age would, and passes that way too.
typedef struct {
    uint32_t rc51, rc52;
    uint64_t uid51[2], uid52[2];
    int32_t users;
    bool firmware19;
} ServiceSelectUser;
bool ServiceJudgeSelectUser(const ServiceSelectUser *seen, char *out, size_t out_size);

/// ldn:u's CreateClientProcessMonitor (1, 18.0.0+): what it answered and the handle of the object
/// it handed out, and what that object's RegisterClient (0, with this process's id) answered.
/// Passes when both succeeded on a handle that is not nought, which is what libnx's ldnInitialize
/// asks on 18.0.0 and later and reden 23585820bc answers; a machine that says it is older than
/// 18.0.0 may refuse 1 instead.
typedef struct {
    uint32_t create_rc, register_rc;
    uint32_t handle;
    bool registered; ///< RegisterClient was asked
    bool firmware18;
} ServiceLdnMonitor;
bool ServiceJudgeLdnMonitor(const ServiceLdnMonitor *seen, char *out, size_t out_size);

/// ldn:u's answers the ldn cases expect, LDN (203) in reden's `ldn_results.h`: BadState (32), a
/// command out of the state it needs. Disabled (22), AirplaneModeEnabled (23) and NoIpAddress (33)
/// are an Initialize refused by a machine with no wireless to give, which skips the cases.
#define SERVICE_LDN_WRONG_STATE SERVICE_RESULT(203, 32)
#define SERVICE_LDN_DISABLED SERVICE_RESULT(203, 22)
#define SERVICE_LDN_AIRPLANE SERVICE_RESULT(203, 23)
#define SERVICE_LDN_NO_ADDRESS SERVICE_RESULT(203, 33)
/// The network every ldn case that hosts creates, as a game's CreateNetwork (202) carries it: the
/// local communication id and scene of its intent, room for four nodes, and node 0's user name
/// and local communication version.
#define SERVICE_LDN_LOCAL_ID 0x01000000000ED000ull
#define SERVICE_LDN_SCENE 0x5Au
#define SERVICE_LDN_ROOM 4u
#define SERVICE_LDN_USER "edenprobe"
#define SERVICE_LDN_VERSION 3
/// The steps the state case walks, in order: Initialize, OpenAccessPoint (200), CreateNetwork,
/// DestroyNetwork (204), CloseAccessPoint (201), OpenStation (300), CloseStation (301), Finalize
/// (401). The event case walks all of them but Finalize.
#define SERVICE_LDN_STEPS 8u
/// The commands the BadState case asks out of their state, and the steps it takes between them.
#define SERVICE_LDN_ASKS 13u
#define SERVICE_LDN_BETWEEN 4u

/// ldn:u's state (GetState, 0) on an object of its own (CreateUserLocalCommunicationService, 0)
/// before Initialize and after each step of SERVICE_LDN_STEPS, with what each step and each
/// GetState answered. Passes when every one succeeds, the states are Initialized (1),
/// AccessPointOpened (2), AccessPointCreated (3), 2, 1, StationOpened (4) and 1 after the first
/// seven, and None (0) or Error (6) before Initialize and after Finalize: reden walks the seven
/// (`lan_discovery.cpp`) and answers Error to an object not initialized
/// (`user_local_communication_service.cpp`, `GetState`), where Ryujinx answers None.
typedef struct {
    uint32_t before_rc, before;
    uint32_t step_rc[SERVICE_LDN_STEPS];
    uint32_t state_rc[SERVICE_LDN_STEPS];
    uint32_t state[SERVICE_LDN_STEPS];
} ServiceLdnStates;
bool ServiceJudgeLdnStates(const ServiceLdnStates *seen, char *out, size_t out_size);

/// The state change event (AttachStateChangeEvent, 100) of an object of its own, cleared before
/// each of the state case's steps but Finalize and waited on for a moment after it: what 100 and
/// each step answered and whether the event was signaled after it. Passes when all of them
/// succeed and it was signaled after CreateNetwork and after DestroyNetwork: reden signals it
/// when node 0 joins the network it creates and when the network goes (`lan_discovery.cpp`,
/// `UpdateNodes`, `DestroyNetwork`) and at no other step, where Ryujinx signals each change.
typedef struct {
    uint32_t attach_rc;
    uint32_t step_rc[SERVICE_LDN_STEPS - 1u];
    bool signaled[SERVICE_LDN_STEPS - 1u];
} ServiceLdnEvent;
bool ServiceJudgeLdnEvent(const ServiceLdnEvent *seen, char *out, size_t out_size);

/// Commands asked out of the state they need, on an object of its own: OpenAccessPoint,
/// OpenStation, CloseAccessPoint, CloseStation and GetNetworkInfo (1) before Initialize;
/// CreateNetwork, GetNetworkInfo, GetSecurityParameter (4), GetNetworkConfig (5) and
/// GetNetworkInfoLatestUpdate (101, eight entries) once initialized; GetNetworkInfo with the
/// access point open; CreateNetwork and GetNetworkInfo as a station. between holds Initialize,
/// OpenAccessPoint, CloseAccessPoint and OpenStation, asked in between. Passes when those
/// succeed and every command asked is refused as SERVICE_LDN_WRONG_STATE, as reden's
/// lan_discovery and Ryujinx both refuse them.
typedef struct {
    uint32_t asked[SERVICE_LDN_ASKS];
    uint32_t between[SERVICE_LDN_BETWEEN];
} ServiceLdnRefused;
bool ServiceJudgeLdnRefused(const ServiceLdnRefused *seen, char *out, size_t out_size);

/// set's language codes: what GetAvailableLanguageCodes2 (5) listed, GetLanguageCode (0), what
/// MakeLanguageCode (2) made of each index of the list and of the first index past it, and
/// GetAvailableLanguageCodeCount2 (6). reden lists its table, answers the console's language from
/// it, makes each index into the table's code and refuses one past it with
/// SERVICE_INVALID_LANGUAGE, and counts the most the list can hold.
typedef struct {
    uint32_t list_rc, language_rc, past_rc, count_rc;
    int32_t listed;
    uint64_t codes[SERVICE_LANGUAGES_MAX];
    uint64_t language;
    uint32_t made_rc[SERVICE_LANGUAGES_MAX];
    uint64_t made[SERVICE_LANGUAGES_MAX];
    int32_t count;
} ServiceLanguages;
/// The code as text: up to eight characters, '?' for a byte that is not printable.
void ServiceLanguageText(uint64_t code, char out[9]);
bool ServiceJudgeLanguage(const ServiceLanguages *seen, char *out, size_t out_size);
bool ServiceJudgeMakeLanguage(const ServiceLanguages *seen, char *out, size_t out_size);
bool ServiceJudgeLanguageCount(const ServiceLanguages *seen, char *out, size_t out_size);

/// csrng's GenerateRandomBytes (0), asked twice for SERVICE_RANDOM_SIZE bytes, each buffer filled
/// with SERVICE_RANDOM_FILL first. Passes when both succeeded, neither buffer's last eight bytes
/// still hold the fill (reden writes the whole buffer it is given), neither is all nought, and the
/// two differ.
bool ServiceJudgeRandom(uint32_t rc1, uint32_t rc2, const uint8_t *first, const uint8_t *second,
                        char *out, size_t out_size);

/// nfp's StartDetection (3) and StartDetectionWithFilter (25, 23.0.0), each asked for the same
/// device, and how many devices ListDevices (2) found. Passes when 25 answers what 3 answers,
/// reden 42642f8bad's one handler for both; a machine that says it is older than 23.0.0 may
/// refuse 25 instead.
bool ServiceJudgeNfpFilter(uint32_t rc3, uint32_t rc25, int32_t devices, bool firmware23,
                           char *out, size_t out_size);

/// aoc:u's CountAddOnContent (2) and ListAddOnContent (3) for this program: the count, and the
/// list asked from the count as its offset, which is the list's end, and from one past it. Passes
/// when the end lists nothing with success and one past it is refused with SERVICE_AOC_PAST_END,
/// reden 54cd5fb8eb's ListAddOnContent.
typedef struct {
    uint32_t count;
    uint32_t end_rc, end_listed;
    uint32_t past_rc;
} ServiceAocList;
bool ServiceJudgeAocPast(const ServiceAocList *seen, char *out, size_t out_size);

/// aoc:u's count and its list from 0 for this program, which has no add-on content: the count
/// CountAddOnContent (2) gave, what ListAddOnContent (3) from 0 with room for SERVICE_AOC_LISTED
/// answered, and how many it listed. Passes when none is counted and the list succeeds with none:
/// reden counts the add-on contents installed under the program's base id and lists their
/// indices from the offset (`addon_content_manager.cpp`, `CountAddOnContent`, `ListAddOnContent`).
typedef struct {
    uint32_t count;
    uint32_t list_rc, listed;
} ServiceAocNone;
bool ServiceJudgeAocNone(const ServiceAocNone *seen, char *out, size_t out_size);

/// Two lists from 0 that fit their room: one index into room for one, and none into room for
/// SERVICE_AOC_LISTED; what each answered, how many each listed, and whether the words past the
/// room for one kept SERVICE_AOC_UNTOUCHED. Passes when both succeed with none listed and nothing
/// past the room is written: reden lists the fewer of what it has past the offset and what is
/// asked, here none. A list asked for more than its room is not asked: reden answers it as these,
/// Ryujinx refuses it with InvalidBufferSize (166, 200).
typedef struct {
    uint32_t one_rc, one_listed;
    uint32_t none_rc, none_listed;
    bool past_kept;
} ServiceAocRoom;
bool ServiceJudgeAocRoom(const ServiceAocRoom *seen, char *out, size_t out_size);

/// The program's id (svcGetInfo, InfoType_ProgramId) and GetAddOnContentBaseId (5): passes when
/// both answer and the base id is the program's base with 0x1000 added, as reden answers a
/// program with no control data (`addon_content_manager.cpp`, `GetAddOnContentBaseId`).
bool ServiceJudgeAocBase(uint32_t info_rc, uint64_t program_id, uint32_t rc, uint64_t base_id,
                         char *out, size_t out_size);

/// PrepareAddOnContent (7) of SERVICE_AOC_MISSING, which this program does not have: passes when
/// it succeeds, as reden's, which prepares nothing and answers success for any index
/// (`addon_content_manager.cpp`, `PrepareAddOnContent`, a stub there).
bool ServiceJudgeAocPrepare(uint32_t rc, char *out, size_t out_size);

/// A steady clock time point as the time service writes it: seconds, and the clock's source.
typedef struct {
    int64_t seconds;
    uint8_t source[16];
} ServiceTimePoint;

/// The user clock (time:u GetStandardUserSystemClock, 0): its context, GetSystemClockContext (2),
/// then the steady clock's time point (GetStandardSteadyClock, 2, GetCurrentTimePoint, 0), then
/// the user clock's GetCurrentTime (0), in that order. Passes when all three succeed, the context
/// is on the steady clock's source, which is not nought, and the time is the context's offset
/// past the time point, or a second more for one that went by between the two: reden's
/// SystemClockCore::GetCurrentTime.
typedef struct {
    uint32_t context_rc, steady_rc, time_rc;
    int64_t offset;
    ServiceTimePoint context, steady;
    int64_t time;
} ServiceUserClock;
bool ServiceJudgeUserClock(const ServiceUserClock *seen, char *out, size_t out_size);

/// Two readings of the steady clock SERVICE_TIME_WAIT_NS apart. Passes when both succeed on the
/// same source, not nought, the second one or two seconds past the first.
bool ServiceJudgeSteadyClock(uint32_t first_rc, const ServiceTimePoint *first, uint32_t second_rc,
                             const ServiceTimePoint *second, char *out, size_t out_size);

/// A date as ToCalendarTime writes it (nn::time::CalendarTime and CalendarAdditionalInfo), in the
/// console's layout.
typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second, pad;
} ServiceCalendarTime;
typedef struct {
    uint32_t day_of_week, day_of_year;
    char zone[8];
    uint32_t dst;
    int32_t utc_offset;
} ServiceCalendarInfo;

/// LoadTimeZoneRule (4) for "UTC", then ToCalendarTime (100) under it of 0 and of
/// SERVICE_TIME_KNOWN. Passes when all succeed with 1970-01-01 00:00:00, a Thursday, the year's
/// first day, and 2023-11-14 22:13:20, a Tuesday, its 318th, both named UTC, no daylight saving
/// and no offset.
typedef struct {
    uint32_t load_rc;
    uint32_t rc[2];
    ServiceCalendarTime date[2];
    ServiceCalendarInfo info[2];
} ServiceUtcDates;
bool ServiceJudgeUtcDates(const ServiceUtcDates *seen, char *out, size_t out_size);

/// ToPosixTime (201) under the UTC rule of 2023-11-14 22:13:20: what it answered, how many times
/// it wrote and the first. Passes when it succeeds with one, SERVICE_TIME_KNOWN.
bool ServiceJudgeUtcPosix(uint32_t load_rc, uint32_t rc, int32_t count, int64_t first, char *out,
                          size_t out_size);

/// The device's location, GetDeviceLocationName (0), loaded as a rule with LoadTimeZoneRule (4);
/// then the user clock's time dated twice, ToCalendarTimeWithMyRule (101) and ToCalendarTime (100)
/// under the loaded rule. Passes when all succeed, the name is not empty, and the two dates and
/// their additional information agree: the service's own rule is the device location's.
typedef struct {
    uint32_t name_rc, load_rc, time_rc, mine_rc, loaded_rc;
    char name[0x24];
    int64_t time;
    ServiceCalendarTime mine_date, loaded_date;
    ServiceCalendarInfo mine_info, loaded_info;
} ServiceMyRule;
bool ServiceJudgeMyRule(const ServiceMyRule *seen, char *out, size_t out_size);

/// One shared font as pl:u tells it: RequestLoad (0), GetLoadState (1), GetSize (2) and
/// GetSharedMemoryAddressOffset (3) for its type, with the 8 bytes before the offset and the
/// font's first four, read from the mapped memory (nought when the offset puts them outside it).
typedef struct {
    uint32_t request_rc, state_rc, size_rc, offset_rc;
    uint32_t state, size, offset;
    uint8_t header[8];
    uint8_t start[4];
} ServiceFont;
/// Passes when, for every type, all four succeed, the state is Loaded (1), the size is not
/// nought, the font lies inside the memory with room for its header before it, the header's
/// first word is SERVICE_FONT_MAGIC and the font starts as a TrueType or OpenType file. The
/// header's second word goes on the line, as reden reads it (little-endian) and as Ryujinx
/// writes it (big-endian), and is not judged: the two disagree (an open question in Service.md).
bool ServiceJudgeFonts(const ServiceFont fonts[SERVICE_FONTS], char *out, size_t out_size);

/// GetSharedFontInOrderOfPriority (5) for the language set's GetLanguageCode (0) gives:
/// whether the fonts are loaded, how many it lists, and their types, offsets and sizes. Passes
/// when both succeed, the fonts are loaded, all six are listed once each, and each one's offset
/// and size are the ones its type was told in @p fonts.
typedef struct {
    uint32_t language_rc, rc;
    uint64_t language;
    uint8_t loaded;
    uint32_t count;
    uint32_t types[SERVICE_FONTS], offsets[SERVICE_FONTS], sizes[SERVICE_FONTS];
} ServiceFontOrder;
bool ServiceJudgeFontOrder(const ServiceFontOrder *seen, const ServiceFont fonts[SERVICE_FONTS],
                           char *out, size_t out_size);

/// A user's profile base (nn::account::profile::ProfileBase) in the console's layout.
typedef struct {
    uint64_t uid[2];
    uint64_t edited;
    char nickname[0x20];
} ServiceProfileBase;

/// IProfile's Get (0) and GetBase (1) for the user acc:u0's GetProfile (5) was asked about.
/// Passes when both succeed and tell that user, the same base twice, with a nickname that is not
/// empty and ends inside its 0x20 bytes.
bool ServiceJudgeProfileBase(const uint64_t asked[2], uint32_t get_rc,
                             const ServiceProfileBase *get, uint32_t base_rc,
                             const ServiceProfileBase *base, char *out, size_t out_size);

/// IProfile's GetImageSize (10) and LoadImage (11, into a buffer of SERVICE_PICTURE_MAX): what each
/// answered, the sizes they told and the bytes loaded. Passes when both succeed with the same size,
/// not nought and within SERVICE_PICTURE_MAX, and the bytes are a JPEG, FF D8 to FF D9, whose frame
/// is SERVICE_PICTURE_SIDE square: reden's GetProfilePictureForGame (4455b6a3ef, d88b4c3e4e).
bool ServiceJudgeProfileImage(uint32_t size_rc, uint32_t size, uint32_t load_rc, uint32_t loaded,
                              const uint8_t *bytes, char *out, size_t out_size);

/// acc:u0's GetUserExistence (1) for the user and for an id nobody has. Passes when both succeed,
/// the first true and the second false.
bool ServiceJudgeUserExistence(uint32_t user_rc, bool user, uint32_t made_up_rc, bool made_up,
                               char *out, size_t out_size);

/// ICommonStateGetter's GetOperationMode (5) and GetDefaultDisplayResolution (60). Passes when
/// both succeed and agree: Handheld (0) with 1280x720, Console (1) with 1920x1080, reden's
/// GetDefaultDisplayResolution.
bool ServiceJudgeOperationMode(uint32_t mode_rc, uint8_t mode, uint32_t resolution_rc,
                               int32_t width, int32_t height, char *out, size_t out_size);

/// GetCurrentFocusState (9) of a program running as an application. Passes when it succeeds
/// with InFocus (1).
bool ServiceJudgeFocus(uint32_t rc, uint8_t state, char *out, size_t out_size);

/// EnableStartupLogoDisappearedMessage (130, 21.0.0+), then ReceiveMessage (1) until it refuses
/// or SERVICE_AM_MESSAGES came: what 130 answered, the messages and what ended them. Passes when
/// 130 succeeds and SERVICE_AM_LOGO_GONE is among them, reden 87e5d0b6e4's handler; a machine
/// that says it is older than 21.0.0 may refuse 130 instead.
typedef struct {
    uint32_t enable_rc, end_rc;
    uint32_t count;
    uint32_t messages[SERVICE_AM_MESSAGES];
    bool firmware21;
} ServiceLogoMessage;
bool ServiceJudgeLogoMessage(const ServiceLogoMessage *seen, char *out, size_t out_size);

/// BeginVrMode3d (1000, 19.0.0+), IsVrModeEnabled3d (1002), EndVrMode3d (1001) and 1002 again.
/// Passes when all succeed, on after the beginning and off after the end, reden 87e5d0b6e4's
/// three handlers; a machine that says it is older than 19.0.0 may refuse 1000 instead.
typedef struct {
    uint32_t begin_rc, on_rc, end_rc, off_rc;
    uint8_t on, off;
    bool firmware19;
} ServiceVrMode;
bool ServiceJudgeVrMode(const ServiceVrMode *seen, char *out, size_t out_size);

/// GetSettingsPlatformRegion (300, 9.0.0+). Passes when it succeeds with Global (1), reden's
/// answer; a machine that says it is older than 9.0.0 may refuse it instead.
bool ServiceJudgePlatformRegion(uint32_t rc, uint32_t region, bool firmware9, char *out,
                                size_t out_size);

/// A setting of ISelfController's written and read back: what it was, what the write asked for,
/// what it read after, and what the four commands answered (the first read, the write, the read
/// after, the write that puts it back). Passes when all four succeed and the read after the write
/// gives what it asked for: reden keeps both settings it is given (`self_controller.cpp`,
/// SetIdleTimeDetectionExtension and GetIdleTimeDetectionExtension, SetAutoSleepDisabled and
/// IsAutoSleepDisabled). @p what names the setting on the line.
typedef struct {
    uint32_t before_rc, set_rc, after_rc, restore_rc;
    uint32_t before, asked, after;
} ServiceSelfSetting;
bool ServiceJudgeSelfSetting(const char *what, const ServiceSelfSetting *seen, char *out,
                             size_t out_size);

/// IApplicationFunctions' GetDesiredLanguage (21), and the codes set lists
/// (GetAvailableLanguageCodes2, 5). Passes when both succeed and the code is one of the list's,
/// not nought: reden converts the application language it picks into a settings code.
bool ServiceJudgeDesiredLanguage(uint32_t rc, uint64_t code, uint32_t list_rc, int32_t listed,
                                 const uint64_t *codes, char *out, size_t out_size);

/// One start event: whether it was asked (the machine's firmware has the command), what the
/// command answered and the handle it gave.
typedef struct {
    bool asked;
    uint32_t rc, handle;
} ServiceStartEvent;
/// Passes when every event asked was handed out, a success with a handle that is not nought, and
/// at least one was asked; an unknown command's success gives no handle and fails.
bool ServiceJudgeStartEvents(const ServiceStartEvent events[SERVICE_START_EVENTS], char *out,
                             size_t out_size);

/// TryPopFromFriendInvitationStorageChannel (141, 9.0.0+) and PopLaunchParameter (1) of the user
/// channel (1), with nothing pushed to either: what each answered and whether it gave a storage.
/// Passes when each one asked is refused with SERVICE_AM_NO_DATA, reden's answer for an empty
/// channel.
bool ServiceJudgeEmptyChannels(bool friend_asked, uint32_t friend_rc, bool friend_storage,
                               uint32_t user_rc, bool user_storage, char *out, size_t out_size);

/// IAudioDevice's ListAudioDeviceName and GetActiveAudioDeviceName (the Auto commands from
/// 3.0.0, as libnx sends them): what each answered, how many were listed, the names, and the
/// active one. Passes when both succeed, at least one is listed and the active name is one of
/// them, not empty: reden lists its outputs and names AudioTvOutput the active one.
typedef struct {
    uint32_t list_rc, active_rc;
    int32_t listed;
    char names[SERVICE_AUDIO_DEVICES][SERVICE_AUDIO_NAME];
    char active[SERVICE_AUDIO_NAME];
} ServiceAudioDevices;
bool ServiceJudgeAudioActive(const ServiceAudioDevices *seen, char *out, size_t out_size);

/// GetAudioDeviceOutputVolume of the active device, SetAudioDeviceOutputVolume to @p asked, the
/// volume read again and the first one set back. Passes when all four succeed and the read after
/// the set is @p asked, reden's SetDeviceVolumes for the device it calls active.
bool ServiceJudgeAudioVolume(uint32_t before_rc, float before, uint32_t set_rc, float asked,
                             uint32_t after_rc, float after, uint32_t restore_rc, char *out,
                             size_t out_size);

/// On the memory card: CreateDirectory of a directory and CreateFile of a file in it, then
/// RenameDirectory to a new name in the same parent, and GetEntryType of the new name, the old
/// one and the file under the new one. Passes when the three succeed, the new name is a
/// directory, the old one is not there (SERVICE_PATH_NOT_FOUND) and the file is under the new
/// name, reden 1f03dee126's RenameDirectory in one parent.
typedef struct {
    uint32_t make_rc, file_rc, rename_rc;
    uint32_t new_rc, old_rc, inside_rc;
    uint32_t new_type, inside_type;
} ServiceDirRename;
bool ServiceJudgeDirRename(const ServiceDirRename *seen, char *out, size_t out_size);

/// OpenDirectory of the renamed directory, GetEntryCount, Read with room for four and Read again.
/// Passes when all succeed, the count is one, the first read gives the file by its name, as a
/// file of SERVICE_DIR_FILE_SIZE bytes, and the second gives nothing.
typedef struct {
    uint32_t open_rc, count_rc, read_rc, again_rc;
    int64_t count, read, again;
    char name[16];
    int32_t type;
    int64_t size;
} ServiceDirList;
bool ServiceJudgeDirList(const ServiceDirList *seen, const char *file, char *out,
                         size_t out_size);

/// DeleteDirectoryRecursively of the parent, then GetEntryType of it. Passes when the delete
/// succeeds and the parent is not there.
bool ServiceJudgeDirDelete(uint32_t delete_rc, uint32_t gone_rc, char *out, size_t out_size);

/// IAudioController's expected master volumes, set and read back: what they were, what the write
/// asked for and what they read after, with what each step answered (the first read, the write,
/// the read after, the write that puts them back). Passes when every step succeeds and both
/// volumes read after are the ones asked: reden keeps them on the controller
/// (`audio_controller.cpp`, SetExpectedMasterVolume and the two Get commands).
typedef struct {
    uint32_t before_rc, set_rc, after_rc, restore_rc;
    float main_before, library_before, main_asked, library_asked, main_after, library_after;
} ServiceExpectedVolume;
bool ServiceJudgeExpectedVolume(const ServiceExpectedVolume *seen, char *out, size_t out_size);

/// ChangeMainAppletMasterVolume (3) read back with GetMainAppletExpectedMasterVolume (1), and
/// SetTransparentVolumeRate (4) accepted. Passes when every step succeeds and the main volume
/// read after is the one asked: reden sets the main volume it is given and keeps the rate.
typedef struct {
    uint32_t change_rc, after_rc, rate_rc, restore_rc;
    float before, asked, after;
} ServiceMainVolume;
bool ServiceJudgeMainVolume(const ServiceMainVolume *seen, char *out, size_t out_size);

/// A storage put in the user channel with UnpopToUserChannel (122) and taken out with
/// PopLaunchParameter(UserChannel) (1): what each step answered, whether the storage popped
/// held the bytes written, and what a second pop answered. Passes when every step succeeds, the
/// bytes are the same and the second pop finds the channel empty (NoDataInChannel): reden keeps
/// what is unpopped for the next pop (`application_functions.cpp`, UnpopToUserChannel and
/// PopLaunchParameter).
typedef struct {
    uint32_t create_rc, write_rc, unpop_rc, pop_rc, read_rc, again_rc;
    bool same;
} ServiceUserChannel;
bool ServiceJudgeUserChannel(const ServiceUserChannel *seen, char *out, size_t out_size);

/// Two storages unpopped, one pop, ClearUserChannel (121) and one more pop: which one the first
/// pop gave (1 the first unpopped, 2 the second, 0 neither) and what each step answered. Passes
/// when the second comes out first and the pop after the clear finds nothing: reden's channel is
/// a stack and Clear empties it.
typedef struct {
    uint32_t first_rc, second_rc, pop_rc, read_rc, clear_rc, after_rc;
    int got;
} ServiceChannelOrder;
bool ServiceJudgeChannelOrder(const ServiceChannelOrder *seen, char *out, size_t out_size);

/// GetPreviousProgramIndex (123): -1 for a program no other program of the application launched,
/// as reden starts every application (`applet.h`, previous_program_index).
bool ServiceJudgePreviousProgram(uint32_t rc, int32_t index, char *out, size_t out_size);

/// A storage from CreateStorage (ILibraryAppletCreator 10): what the creation, GetSize, a write
/// of 16 bytes at SERVICE_STORAGE_AT and the read of them back answered, the size it gave, and
/// whether the bytes read are the ones written. Passes when every step succeeds, the size is the
/// one asked and the bytes match: reden's storage is a buffer of that size
/// (`library_applet_storage.cpp`).
typedef struct {
    uint32_t create_rc, size_rc, write_rc, read_rc;
    int64_t size;
    bool same;
} ServiceStorageKeeps;
bool ServiceJudgeStorageKeeps(const ServiceStorageKeeps *seen, char *out, size_t out_size);

/// An access of 16 bytes at SERVICE_STORAGE_PAST in a storage of SERVICE_STORAGE_SIZE, which ends
/// 8 bytes past it: InvalidOffset (128, 503), as reden's ValidateOffset answers a write or a read
/// whose end is past the storage's. @p what names the access on the line.
bool ServiceJudgeStoragePast(const char *what, uint32_t create_rc, uint32_t rc, char *out,
                             size_t out_size);

/// GetCount (2) of the default source: reden counts its six default Miis
/// (`mii_manager.cpp`, GetCount and DefaultMiiCount).
bool ServiceJudgeMiiCount(uint32_t rc, int32_t count, char *out, size_t out_size);

/// Get (3) of the default source against BuildDefault (7) of each index: what Get answered and
/// how many it gave, how many of those came with source Default (1), what the builds answered
/// and how many of the Miis given are the same build past their create id, which reden makes new
/// for each (`mii_manager.cpp`, Get and BuildDefault). Passes when both succeed and all six are
/// Default and the same.
typedef struct {
    uint32_t get_rc, built_rc;
    int32_t count;
    uint32_t defaults, same;
} ServiceMiiDefaults;
bool ServiceJudgeMiiDefaults(const ServiceMiiDefaults *seen, char *out, size_t out_size);

/// IsUpdated (0) of the default source and IsFullDatabase (1): reden answers false to both for a
/// session that asked the default source only and a database with room.
bool ServiceJudgeMiiState(uint32_t updated_rc, bool updated, uint32_t full_rc, bool full,
                          char *out, size_t out_size);

/// BuildDefault (7) of index 6, one past the default Miis: InvalidArgument (126, 1), as reden
/// checks the index against RawData::DefaultMii.
bool ServiceJudgeMiiPast(uint32_t rc, char *out, size_t out_size);

/// VR mode read (IsVrModeEnabled, 50), turned on, read, turned off and read: libnx turns it with
/// BeginVrModeEx (53) and EndVrModeEx (54) from 7.0.0, SetVrModeEnabled (51) before. Passes when
/// every step succeeds and the reads say on after on and off after off: reden keeps the mode on
/// the applet (`common_state_getter.cpp`, vr_mode_enabled).
typedef struct {
    uint32_t before_rc, on_rc, on_read_rc, off_rc, off_read_rc;
    bool before, on, off;
} ServiceVrTurn;
bool ServiceJudgeVrTurn(const ServiceVrTurn *seen, char *out, size_t out_size);

/// GetBootMode (8) and IsInControllerFirmwareUpdateSection (55): reden answers a normal boot (0)
/// and false.
bool ServiceJudgeBootMode(uint32_t boot_rc, uint32_t mode, uint32_t section_rc, bool section,
                          char *out, size_t out_size);

/// socket(AF_INET, SOCK_RAW, 0) on bsd:u: -1 with EINVAL, as reden refuses a raw socket that
/// is not ICMP on the user's service (a41a98028a, `bsd.cpp` SocketImpl).
bool ServiceJudgeRawSocket(int fd, int err, char *out, size_t out_size);

/// setsockopt of SO_RCVTIMEO with tv_usec 1000000 and of SO_SNDTIMEO with tv_sec -1, on a UDP
/// socket: each -1 with EDOM, as a BSD stack answers a timeval out of range and reden does
/// (afe7a00cde, `bsd.cpp` TimeoutMilliseconds).
typedef struct {
    int socket_fd, usec_ret, usec_errno, sec_ret, sec_errno;
} ServiceTimeoutRange;
bool ServiceJudgeTimeoutRange(const ServiceTimeoutRange *seen, char *out, size_t out_size);

/// setsockopt of SO_RCVTIMEO with 4 bytes: -1 with EINVAL, a buffer too short for a timeval.
bool ServiceJudgeTimeoutShort(int socket_fd, int ret, int err, char *out, size_t out_size);

/// getsockname of a UDP socket bound to the loopback, into a buffer of 8 bytes: it succeeds,
/// says 8, the length it wrote, and writes nothing past them, as reden cuts the address to the
/// buffer and answers the length cut (1b7f7eedcb).
typedef struct {
    int socket_fd, bind_ret, bind_errno, name_ret, name_errno;
    uint32_t length;
    bool past_kept;
} ServiceNameShort;
bool ServiceJudgeNameShort(const ServiceNameShort *seen, char *out, size_t out_size);

/// QueryPointerBufferSize (IpcController 3) on a session of set: a size above nought, since set
/// takes its language codes in a pointer buffer; reden answers the program's (0x8000 for an NRO,
/// `sm_controller.cpp`).
bool ServiceJudgePointerBuffer(uint32_t open_rc, uint32_t rc, uint32_t size, char *out,
                               size_t out_size);

/// CloneCurrentObject (IpcController 2) of a session of set, and GetLanguageCode (0) asked on both:
/// what each step answered, whether the clone is a handle, and the two codes. Passes when all
/// succeed and the codes are the same and not nought, as reden's clone serves the same object.
typedef struct {
    uint32_t open_rc, clone_rc, first_rc, clone_ask_rc;
    bool handle;
    uint64_t first, cloned;
} ServiceSessionClone;
bool ServiceJudgeSessionClone(const ServiceSessionClone *seen, char *out, size_t out_size);

/// A CMIF Close sent with svcSendSyncRequest on a session of set, then GetLanguageCode on the same
/// handle: SessionClosed (0xF601) for both, as reden's server closes the session and answers the
/// client so (28ab4a1a01).
bool ServiceJudgeSessionClose(uint32_t open_rc, uint32_t close_rc, uint32_t after_rc, char *out,
                              size_t out_size);

/// The npad LIFOs of hid's shared memory (ten npads, seven LIFOs each) that hold two entries or
/// more: how many there are, in how many the newest entry's storage number is twice its state's
/// sampling number, in how many the state before it is one less, and how many reads moved under
/// the copy every try. Passes when there is one at least and every one is doubled and one
/// apart: reden's ring LIFO numbers the storage `state.sampling_number << 1` and each npad's
/// state counts one per entry, an empty one too since 07f40d5cac (`ring_lifo.h:50`).
typedef struct {
    uint32_t lifos, doubled, consecutive, torn;
    uint64_t storage, state;
} ServiceHidSampling;
bool ServiceJudgeHidSampling(const ServiceHidSampling *seen, char *out, size_t out_size);

/// GetClockSnapshot (time:u 400) of the user clock (type 0), against the user clock's own context
/// (ISystemClock 2) and the device's location (ITimeZoneService 0): what each answered, whether the
/// snapshot's user context is the clock's, its user time against the context's offset plus the
/// snapshot's steady time point (both of one source), whether its location is the device's, and its
/// type. Passes when all succeed and agree: reden builds the snapshot from those
/// (`psc/time/static.cpp`, GetClockSnapshotImpl).
typedef struct {
    uint32_t snap_rc, context_rc, name_rc;
    bool same_context, same_source, same_name;
    int64_t user_time, offset, steady;
    uint32_t type;
} ServiceSnapshot;
bool ServiceJudgeSnapshot(const ServiceSnapshot *seen, char *out, size_t out_size);

/// CalculateSpanBetween (501) of a snapshot and itself, and of it and a copy whose steady time
/// point is five seconds later: nought and five seconds, in nanoseconds, as reden takes the span
/// from the steady time points of one source.
bool ServiceJudgeSnapSpan(uint32_t snap_rc, uint32_t same_rc, int64_t same, uint32_t later_rc,
                          int64_t later, char *out, size_t out_size);

/// ListDisplays (1000) into room for four: what it answered, how many it listed, and the first
/// one's name, layer limit, layer count and size. Passes when it lists one, "Default", with a
/// limited layer count of one and 1920x1080, reden's DisplayInfo (`vi_types.h`).
typedef struct {
    uint32_t rc;
    int64_t count;
    bool named;
    uint32_t limited;
    uint64_t layers, width, height;
} ServiceDisplayList;
bool ServiceJudgeDisplayList(const ServiceDisplayList *seen, char *out, size_t out_size);

/// ConvertScalingMode (2102) of the modes 0 to 5 a game names (none, freeze, scale to window,
/// scale and crop, preserve aspect ratio, then one past): what each answered and gave. Passes when
/// the five give 3, 0, 1, 2 and 4, the window's own numbering, and the sixth is OperationFailed, as
/// reden's table (`application_display_service.cpp`, ConvertScalingMode).
bool ServiceJudgeScalingModes(const uint32_t rc[6], const uint64_t converted[6], char *out,
                              size_t out_size);

/// GetIndirectLayerImageRequiredMemoryInfo (2460) of 1280x720: 0x3A0000, its RGBA bytes rounded up
/// to 0x20000, with an alignment of 0x1000, as reden reckons it.
bool ServiceJudgeIndirectMemory(uint32_t rc, int64_t size, int64_t alignment, char *out,
                                size_t out_size);

/// GetSampleRate (0), GetSampleCount (1) and GetMixBufferCount (2) of a renderer opened at
/// 48 kHz with 2 mix buffers: what each answered and gave. Passes when they give 48000, 240 (a
/// 5 ms frame) and 2, the parameters reden's renderer keeps (`audio_renderer.cpp`).
typedef struct {
    uint32_t rate_rc, count_rc, mix_rc;
    uint32_t rate, count, mix;
} ServiceRendererParams;
bool ServiceJudgeRendererParams(const ServiceRendererParams *seen, char *out, size_t out_size);

/// A renderer setting read, set, read back and put back: the rendering time limit (9 and 8, in
/// percent, 100 at first) or the voice drop parameter (13 and 12, a float, 1 at first). Values in
/// thousandths of the setting. Passes when every step succeeds, the first read is the default and
/// the read after the write is what was set, as reden keeps both on the renderer.
typedef struct {
    uint32_t before_rc, set_rc, after_rc, restore_rc;
    int32_t before, asked, after, initial;
} ServiceRendererSetting;
bool ServiceJudgeRendererSetting(const char *what, const ServiceRendererSetting *seen, char *out,
                                 size_t out_size);

/// GetState (3) before Start (5), after it and after Stop (6), with what each answered. Passes
/// when every call succeeds and the state is 1, then 0, then 1: reden answers whether the renderer
/// is not active (`audio_renderer.cpp`, `GetState`).
typedef struct {
    uint32_t before_rc, start_rc, started_rc, stop_rc, stopped_rc;
    uint32_t before, started, stopped;
} ServiceRendererState;
bool ServiceJudgeRendererState(const ServiceRendererState *seen, char *out, size_t out_size);

/// ngc:u's version of its word lists (GetContentVersion, 0) and what Check (1) and Check2 (4)
/// found in a clean name, with what each answered. Passes when every call succeeds, the version is
/// above nought and neither check found a word: reden answers version 1 and finds nothing
/// (`ngc/ngc.cpp`, NgcServiceImpl).
typedef struct {
    uint32_t version_rc, check_rc, check2_rc;
    uint32_t version, check, check2;
} ServiceNgcCheck;
bool ServiceJudgeNgcCheck(const ServiceNgcCheck *seen, char *out, size_t out_size);

/// A text the profanity filter gave back: what the call answered, the words it said it masked
/// (0xEEEE when it wrote none) and the buffer the text came back in.
typedef struct {
    uint32_t rc, count;
    uint8_t back[SERVICE_NGC_SIZE];
} ServiceNgcText;

/// Mask (2) and Mask2 (5) of the clean name TEXT. Passes when both succeed, mask no word and give
/// the text and its terminator back byte for byte: reden writes the text it was sent into the
/// buffer it hands back.
bool ServiceJudgeNgcMask(const char *text, const ServiceNgcText *mask, const ServiceNgcText *mask2,
                         char *out, size_t out_size);

/// ngct:u's Match (0) and Filter (1) of the clean name TEXT: Match's answer and verdict, and the
/// filtered text. Passes when both succeed, Match finds nothing and Filter gives the text back byte
/// for byte, as reden's IService does.
bool ServiceJudgeNgctClean(const char *text, uint32_t match_rc, bool matched,
                           const ServiceNgcText *filter, char *out, size_t out_size);

/// A session's configurations: both modes' at first, the one asked for Normal mode, both read
/// after, with what each step answered (the two first reads, the write, the two reads after, the
/// write that puts Normal back). Passes when every step succeeds, Normal reads what was asked and
/// Boost what it read at first: reden keeps one configuration per mode (`apm_controller.cpp`,
/// `SetPerformanceConfiguration`, `GetCurrentPerformanceConfiguration`).
typedef struct {
    uint32_t before_rc, set_rc, after_rc, restore_rc;
    uint32_t normal_before, boost_before, asked, normal_after, boost_after;
} ServiceApmConfig;
bool ServiceJudgeApmConfig(const ServiceApmConfig *seen, char *out, size_t out_size);

/// Boost mode's configuration after the state getter's SetCpuBoostMode (66) with FastLoad and then
/// with Normal, each read on an apm session, with what each step answered. Passes when every step
/// succeeds, FastLoad gives Config13 and Normal Config7: reden hands the mode to apm, which sets
/// Boost's configuration from it (`common_state_getter.cpp`, `SetCpuBoostMode`).
typedef struct {
    uint32_t fast_rc, fast_read_rc, normal_rc, normal_read_rc;
    uint32_t fast, normal;
} ServiceApmBoost;
bool ServiceJudgeApmBoost(const ServiceApmBoost *seen, char *out, size_t out_size);

/// A home button lock (GetHomeButtonReaderLockAccessor, 30) taken as libnx takes it, which waits
/// on the lock's event for nought before TryLock (1), then IsLocked (4) and a second take, and the
/// Unlock (2): what each step answered, and what IsLocked and the two takes said. Passes when
/// every step succeeds, the lock is free, taken, held, not taken twice and free again: reden
/// signals a lock's event when it makes it and keeps it signaled (`lock_accessor.cpp`).
typedef struct {
    uint32_t get_rc, before_rc, try_rc, locked_rc, again_rc, unlock_rc, after_rc;
    bool before, took, locked, again, after;
} ServiceLockTaken;
bool ServiceJudgeLockTaken(const ServiceLockTaken *seen, char *out, size_t out_size);

/// A reader lock (GetReaderLockAccessorEx, 31) and its event, each wait of nought: on the free
/// lock, after a TryLock (1) and the event cleared, and after the Unlock (2). Passes when the free
/// lock's wait succeeds, the held one's times out (SERVICE_TIMED_OUT) and the unlocked one's
/// succeeds again, with every call succeeding: reden signals the event at the start and on every
/// Unlock.
typedef struct {
    uint32_t get_rc, free_rc, try_rc, clear_rc, held_rc, unlock_rc, unlocked_rc;
    bool took;
} ServiceLockEvent;
bool ServiceJudgeLockEvent(const ServiceLockEvent *seen, char *out, size_t out_size);

/// A system setting set and read back: what it was, what the write asked, what it read after,
/// and what each step answered (the first read, the write, the read after, the write that puts it
/// back). A volume is in thousandths. Passes when every step succeeds and the read after is what
/// was asked: reden keeps the system settings it is given (`system_settings_server.cpp`).
typedef struct {
    uint32_t before_rc, set_rc, after_rc, restore_rc;
    int32_t before, asked, after;
} ServiceSetsysValue;
bool ServiceJudgeSetsysValue(const char *what, const ServiceSetsysValue *seen, char *out,
                             size_t out_size);
/// The color set (GetColorSetId, 23, and SetColorSetId, 24) and the keyboard layout
/// (GetKeyboardLayout, 136, and SetKeyboardLayout, 137), each as ServiceJudgeSetsysValue.
bool ServiceJudgeSetsysLook(const ServiceSetsysValue *color, const ServiceSetsysValue *layout,
                            char *out, size_t out_size);

/// Four flags turned over and put back (SERVICE_SETSYS_FLAG_COUNT, in this order: 7 and 8, 47
/// and 48, 99 and 100, 45 and 46): the first failing step's answer for each, or nought, and each
/// flag as read first, after the write and after the write back. Passes when every step succeeds
/// and each flag reads turned over, then as it was.
typedef struct {
    uint32_t rc[SERVICE_SETSYS_FLAG_COUNT];
    bool before[SERVICE_SETSYS_FLAG_COUNT], after[SERVICE_SETSYS_FLAG_COUNT];
    bool back[SERVICE_SETSYS_FLAG_COUNT];
} ServiceSetsysFlags;
bool ServiceJudgeSetsysFlags(const ServiceSetsysFlags *seen, char *out, size_t out_size);

/// The time zone service's location names: the device's (GetDeviceLocationName, 0), the count
/// (GetTotalLocationNameCount, 2), and the names LoadLocationNameList (3) gives SERVICE_TZ_CHUNK
/// at a time from 0 until it gives fewer, with the first failing call's answer, how many it
/// listed, how many were empty and whether the device's was among them. Passes when every call
/// succeeds, the count is above nought, as many are listed as counted, none is empty and the
/// device's is listed: reden lists the names of its time zone binary
/// (`glue/time/time_zone_binary.cpp`).
typedef struct {
    uint32_t name_rc, count_rc, list_rc;
    int32_t count, listed;
    uint32_t empty;
    bool found;
    char device[0x25];
} ServiceTzList;
bool ServiceJudgeTzList(const ServiceTzList *seen, char *out, size_t out_size);

/// GetDeviceLocationNameAndUpdatedTime (6) against GetDeviceLocationName (0): what each answered,
/// the two names, and the time point 6 gave. Passes when both succeed with the same name, not an
/// empty one: reden answers the device's location and the steady clock time it was set at
/// (`psc/time/time_zone_service.cpp`).
typedef struct {
    uint32_t name_rc, updated_rc;
    char name[0x25], updated[0x25];
    int64_t seconds;
    uint8_t source[16];
} ServiceTzUpdated;
bool ServiceJudgeTzUpdated(const ServiceTzUpdated *seen, char *out, size_t out_size);

/// The user clock's time now dated by the device's rule (ToCalendarTimeWithMyRule, 101) and the
/// date turned back (ToPosixTimeWithMyRule, 202), with the rule's version (GetTimeZoneRuleVersion,
/// 5) for the line: what each answered, the time, the times 202 gave and their count, and the
/// version. Passes when every call succeeds and 202 gives one or two times with the first among
/// them: reden gives back every time a local date can be (`psc/time/time_zone.cpp`). The version
/// is not judged: reden's is its binary's date with a line break ("230326\n"), and what a
/// console's is has not been seen.
typedef struct {
    uint32_t now_rc, date_rc, back_rc, version_rc;
    int64_t now;
    int32_t count;
    int64_t times[2];
    char version[0x11];
} ServiceTzBack;
bool ServiceJudgeTzBack(const ServiceTzBack *seen, char *out, size_t out_size);

/// Writes time:u was asked to make, each with what it answered, and whether what they would have
/// changed reads the same after (a clock within two seconds of its first reading, a setting as it
/// was). Passes when every write is refused as SERVICE_TIME_DENIED and nothing changed: reden's
/// time:u may write no clock, no setting and no location (`glue/glue.cpp`, its setup info all
/// nought, and the checks in `psc/time/system_clock.cpp`, `steady_clock.cpp`, `static.cpp` and
/// `glue/time/time_zone.cpp`).
typedef struct {
    uint32_t count;
    uint32_t rc[SERVICE_TIME_CLOCK_WRITES];
    bool same;
} ServiceTimeWrites;
/// The clocks: SetCurrentTime (1) and SetSystemClockContext (3) on the user clock, SetCurrentTime
/// on the network clock, and the steady clock's SetTestOffset (3).
bool ServiceJudgeTimeClocks(const ServiceTimeWrites *seen, char *out, size_t out_size);
/// The settings: SetStandardUserSystemClockAutomaticCorrectionEnabled (101),
/// SetStandardSteadyClockInternalOffset (50) and the time zone service's SetDeviceLocationName (1).
bool ServiceJudgeTimeSettings(const ServiceTimeWrites *seen, char *out, size_t out_size);

/// audout:u and the out libnx's audoutInitialize opened: ListAudioOuts (0), an OpenAudioOutAuto
/// (3) with a name it does not have, what the open gave (rate, channels, PCM format, state), and
/// the volume (GetAudioOutVolume, 13) at first and after SetAudioOutVolume (12) to a half, in
/// thousandths. Passes when it lists one output, "DeviceOut", refuses the other name as
/// SERVICE_AUDIO_NOT_FOUND, opened 48 kHz stereo PCM16 stopped at full volume, and reads the half
/// back: reden's audio out (`audio_core/out/audio_out_system.cpp`).
typedef struct {
    uint32_t list_rc, other_rc, volume_rc, set_rc, after_rc;
    uint32_t listed;
    char name[16];
    uint32_t rate, channels, format, state;
    int32_t volume, after;
} ServiceAudoutOpen;
bool ServiceJudgeAudoutOpen(const ServiceAudoutOpen *seen, char *out, size_t out_size);

/// Two buffers appended (AppendAudioOutBufferAuto, 7) to the out before it is started, then
/// GetAudioOutBufferCount (9), ContainsAudioOutBuffer (6) of one of them and of another tag,
/// GetReleasedAudioOutBuffersAuto (8), FlushAudioOutBuffers (11) and the count again. Passes when
/// every call succeeds, both are counted, the one appended is held and the other tag not, none is
/// released, the flush says it flushed nothing and both are still counted: reden flushes only a
/// started out (`audio_out_system.cpp`, `FlushAudioOutBuffers`).
typedef struct {
    uint32_t append_rc, count_rc, contains_rc, released_rc, flush_rc, after_rc;
    uint32_t count, released, after;
    bool held, other, flushed;
} ServiceAudoutStopped;
bool ServiceJudgeAudoutStopped(const ServiceAudoutStopped *seen, char *out, size_t out_size);

/// Start (1), GetAudioOutState (0), Start again, Stop (2), the state, the released tags and the
/// count: what each answered and gave, with the two tags appended. Passes when the first Start
/// succeeds and the state is Started (0), the second is refused as SERVICE_AUDIO_FAILED, the Stop
/// succeeds with the state Stopped (1), both tags come back released in the order appended and
/// none is counted: reden refuses a Start of an out not stopped and releases every buffer when it
/// stops (`audio_out_system.cpp`, `Start`, `Stop`).
typedef struct {
    uint32_t start_rc, again_rc, stop_rc, state_rc, released_rc;
    uint32_t started, stopped, count, released;
    uint64_t tags[SERVICE_AUDOUT_TAGS];
    uint64_t first, second;
} ServiceAudoutStarted;
bool ServiceJudgeAudoutStarted(const ServiceAudoutStarted *seen, char *out, size_t out_size);

/// A file made of four bytes, grown to 16 (IFile SetSize, 3) and cut to 2: what each step answered,
/// the sizes GetSize (4) gave after each, how much each read of 16 from 0 read, and whether the
/// bytes were the four with nought after them and then the first two. Passes when every step
/// succeeds with those sizes and bytes: reden resizes the host file (`fsp/fs_i_file.cpp`).
typedef struct {
    uint32_t make_rc, write_rc, grow_rc, grown_rc, cut_rc, cut_size_rc;
    int64_t grown, cut;
    uint64_t grown_read, cut_read;
    bool grown_bytes, cut_bytes;
} ServiceFileSize;
bool ServiceJudgeFileSize(const ServiceFileSize *seen, char *out, size_t out_size);

/// RenameFile (5) of a file of four bytes to a free name, then of another file onto that name:
/// what each answered, GetEntryType (7) of the old name and of the new one, the size under the
/// new name, and the other file's type after its refused rename. Passes when the first rename
/// succeeds, the old name is gone (SERVICE_PATH_NOT_FOUND), the new one is a file of four bytes,
/// and the second rename is refused as SERVICE_PATH_TAKEN with the other file still there: reden
/// (`filesystem.cpp`, `VfsDirectoryServiceWrapper::RenameFile`).
typedef struct {
    uint32_t make_rc, rename_rc, old_rc, new_rc, size_rc, onto_rc, other_rc;
    uint32_t new_type, other_type;
    int64_t size;
} ServiceFileRename;
bool ServiceJudgeFileRename(const ServiceFileRename *seen, char *out, size_t out_size);

/// DeleteDirectory (3) of a directory holding a file, CleanDirectoryRecursively (13) of it, and
/// DeleteDirectory again: what each answered, the file's type after the refused delete, the
/// directory's type and its entry count after the clean. Passes when the first delete is refused
/// (any refusal: reden answers ResultUnknown) with the file still there, the clean succeeds and
/// leaves the directory with nothing in it, and the second delete succeeds.
typedef struct {
    uint32_t make_rc, delete_rc, file_rc, clean_rc, dir_rc, count_rc, again_rc;
    uint32_t file_type, dir_type;
    int64_t count;
} ServiceFileClean;
bool ServiceJudgeFileClean(const ServiceFileClean *seen, char *out, size_t out_size);

/// GetFileTimeStampRaw (14) of a file just written, against the user clock read before it: what
/// each answered and the three times. Passes when both succeed and the created, modified and
/// accessed times are each within SERVICE_STAMP_SLACK seconds of the clock: reden dates a file by
/// the host's stat of it (`vfs_real.cpp`, `GetFileTimeStamp`).
typedef struct {
    uint32_t clock_rc, make_rc, stamp_rc;
    uint64_t now, created, modified, accessed;
} ServiceFileStamp;
bool ServiceJudgeFileStamp(const ServiceFileStamp *seen, char *out, size_t out_size);

/// GetFreeSpaceSize (11) and GetTotalSpaceSize (12) of the memory card's root. Passes when both
/// succeed and the free space is above nought and no more than the total.
bool ServiceJudgeFileSpace(uint32_t free_rc, int64_t free_size, uint32_t total_rc,
                           int64_t total_size, char *out, size_t out_size);

/// ListAllUsers (2) and ListQualifiedUsers (141), each with what it answered, how many users the
/// first listed, and whether the second listed the same ones. Passes when both succeed with at
/// least one user and the same list: reden qualifies every user, with no parental control
/// (`acc.cpp`, `ListQualifiedUsers`).
typedef struct {
    uint32_t all_rc, qualified_rc;
    uint32_t users;
    bool same;
} ServiceAccQualified;
bool ServiceJudgeAccQualified(const ServiceAccQualified *seen, char *out, size_t out_size);

/// StoreSaveDataThumbnail (110) for the user with a thumbnail of SERVICE_ACC_THUMBNAIL_SIZE
/// bytes, with one of 0x1000, and for a user id of nought: what each answered, with how the user
/// was picked. Passes when the first is taken, the short one is refused as
/// SERVICE_ACC_INVALID_LENGTH and the nought user as SERVICE_ACC_INVALID_USER: reden checks the
/// user, then the size (`acc.cpp`, `StoreSaveDataThumbnail`).
typedef struct {
    uint32_t user_rc, stored_rc, short_rc, nobody_rc;
} ServiceAccThumbnail;
bool ServiceJudgeAccThumbnail(const ServiceAccThumbnail *seen, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
