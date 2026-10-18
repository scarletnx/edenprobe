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
    SERVICE_SET_OPENS,    ///< set: set opens and lists its language codes (none tier)
    SERVICE_SET_LANGUAGE, ///< set: GetLanguageCode is one of them
    SERVICE_SET_MAKE,     ///< set: MakeLanguageCode gives the list's code at each index
    SERVICE_SET_COUNT,    ///< set: GetAvailableLanguageCodeCount2 leaves room for the list
    SERVICE_CSRNG_OPENS,  ///< csrng: csrng opens (none tier)
    SERVICE_RANDOM,       ///< csrng: two asks fill their buffers, differently
    SERVICE_APM_OPENS,    ///< apm: apm and a session (none tier)
    SERVICE_NFP_OPENS,    ///< nfp: libnx's nfpInitialize for a user (none tier)
    SERVICE_NFP_FILTER,   ///< nfp: StartDetectionWithFilter answers as StartDetection
    SERVICE_AOC_OPENS,    ///< aoc: aoc:u opens and counts its add-on content (none tier)
    SERVICE_AOC_PAST,     ///< aoc: the list's end lists nothing, one past it is refused
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

#ifdef __cplusplus
}
#endif
