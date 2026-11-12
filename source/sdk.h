// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The SDK's functions for the services a machine may answer in a game's own code (hooks mode,
/// docs/ModeFile.md): nn::fs, nn::time, nn::pl, nn::ngc, nn::nsd, nn::pctl, nn::audio and
/// nn::codec, under the names and with the arguments a game's SDK exports them with. The program
/// exports each by that name (the Makefile's --export-dynamic-symbol), so a machine that takes a
/// game's function by its symbol takes this one the same way, and none of the code below runs.
/// Where nothing takes it, its own code runs: the request to the service that the IPC mode sends,
/// through libnx, with the layouts the SDK's caller sees. The own code is not the SDK's: it
/// answers what the probe's cases ask, and a request it does not know how to send answers
/// SDK_OWN_UNSUPPORTED.
///
/// Every function here counts the calls its own code answered (SdkOwnCalls), so a run says which
/// of them the machine took and which it left to the program. The functions a machine calls back
/// instead, as a game's SDK has them for it (the audio renderer's managers and
/// nn::os::AttachReadableHandleToSystemEvent), are the program's code on every machine and count
/// nothing.

/// Each exported function's name in C, its symbol, and attributes no compiler may see through:
/// a call from a case stays a call to the symbol, which is where a machine's hook takes it.
#define SDK_EXPORT(symbol) __asm__(symbol)
#define SDK_FUNCTION __attribute__((noipa, used, visibility("default")))

/// The SDK's own label, as a game's SDK module carries it in its read-only data: the SDK version
/// these functions' arguments and objects follow. A machine that takes a function only for some
/// SDK versions reads a module's version from this label, and takes none of a module without one.
#define SDK_LABEL "SDK MW+Nintendo+NintendoSdk_nnSdk-20_5_0"
extern const char edenprobe_sdk_label[];

/// What the own code answers when it has no request to send for the call: a libnx result, so it
/// can be told from any service's.
#define SDK_OWN_UNSUPPORTED MAKERESULT(Module_Libnx, LibnxError_NotFound)

// ------------------------------------------------------------------------------------- nn::fs

/// nn::fs::FileHandle and nn::fs::DirectoryHandle: one word, passed in a register.
typedef struct {
    uint64_t handle;
} NnFsFileHandle;
typedef struct {
    uint64_t handle;
} NnFsDirectoryHandle;

/// nn::fs::WriteOption: its flags, NN_FS_WRITE_FLUSH to flush the file with the write.
typedef struct {
    int32_t flags;
} NnFsWriteOption;
#define NN_FS_WRITE_FLUSH 1

/// nn::fs::OpenMode and OpenDirectoryMode, the bits libnx's FsOpenMode and FsDirOpenMode have.
#define NN_FS_OPEN_READ 1
#define NN_FS_OPEN_WRITE 2
#define NN_FS_OPEN_APPEND 4
#define NN_FS_DIRECTORY_DIRECTORIES 1
#define NN_FS_DIRECTORY_FILES 2

/// nn::fs::DirectoryEntryType: a directory or a file, as libnx's FsDirEntryType.
#define NN_FS_ENTRY_DIRECTORY 0
#define NN_FS_ENTRY_FILE 1

/// nn::fs::DirectoryEntry is libnx's FsDirectoryEntry (0x310 bytes), and nn::fs::FileTimeStamp its
/// FsTimeStampRaw (0x20 bytes).
typedef FsDirectoryEntry NnFsDirectoryEntry;
typedef FsTimeStampRaw NnFsFileTimeStamp;

/// The fs module's number, which libnx names no constant for: an fs result is 2 plus its
/// description times 512.
#define NN_FS_MODULE 2

/// The longest mount name the SDK takes.
#define NN_FS_MOUNT_NAME_MAX 15u

uint32_t NnFsMountSdCard(const char *name) SDK_EXPORT("_ZN2nn2fs11MountSdCardEPKc");
uint32_t NnFsMountTemporaryStorage(const char *name)
    SDK_EXPORT("_ZN2nn2fs21MountTemporaryStorageEPKc");
void NnFsUnmount(const char *name) SDK_EXPORT("_ZN2nn2fs7UnmountEPKc");
uint32_t NnFsCommit(const char *name) SDK_EXPORT("_ZN2nn2fs6CommitEPKc");
uint32_t NnFsCreateFile(const char *path, int64_t size) SDK_EXPORT("_ZN2nn2fs10CreateFileEPKcl");
uint32_t NnFsDeleteFile(const char *path) SDK_EXPORT("_ZN2nn2fs10DeleteFileEPKc");
uint32_t NnFsCreateDirectory(const char *path) SDK_EXPORT("_ZN2nn2fs15CreateDirectoryEPKc");
uint32_t NnFsDeleteDirectory(const char *path) SDK_EXPORT("_ZN2nn2fs15DeleteDirectoryEPKc");
uint32_t NnFsDeleteDirectoryRecursively(const char *path)
    SDK_EXPORT("_ZN2nn2fs26DeleteDirectoryRecursivelyEPKc");
uint32_t NnFsCleanDirectoryRecursively(const char *path)
    SDK_EXPORT("_ZN2nn2fs25CleanDirectoryRecursivelyEPKc");
uint32_t NnFsRenameFile(const char *from, const char *to)
    SDK_EXPORT("_ZN2nn2fs10RenameFileEPKcS2_");
uint32_t NnFsRenameDirectory(const char *from, const char *to)
    SDK_EXPORT("_ZN2nn2fs15RenameDirectoryEPKcS2_");
uint32_t NnFsGetEntryType(int32_t *type, const char *path)
    SDK_EXPORT("_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc");
uint32_t NnFsGetFreeSpaceSize(int64_t *size, const char *path)
    SDK_EXPORT("_ZN2nn2fs16GetFreeSpaceSizeEPlPKc");
uint32_t NnFsGetTotalSpaceSize(int64_t *size, const char *path)
    SDK_EXPORT("_ZN2nn2fs17GetTotalSpaceSizeEPlPKc");
uint32_t NnFsGetFileTimeStampForDebug(NnFsFileTimeStamp *stamp, const char *path)
    SDK_EXPORT("_ZN2nn2fs24GetFileTimeStampForDebugEPNS0_13FileTimeStampEPKc");
uint32_t NnFsOpenFile(NnFsFileHandle *out, const char *path, int mode)
    SDK_EXPORT("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci");
void NnFsCloseFile(NnFsFileHandle file) SDK_EXPORT("_ZN2nn2fs9CloseFileENS0_10FileHandleE");
uint32_t NnFsReadFile(size_t *read, NnFsFileHandle file, int64_t offset, void *buffer,
                      size_t size) SDK_EXPORT("_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm");
uint32_t NnFsWriteFile(NnFsFileHandle file, int64_t offset, const void *buffer, size_t size,
                       const NnFsWriteOption *option)
    SDK_EXPORT("_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE");
uint32_t NnFsFlushFile(NnFsFileHandle file) SDK_EXPORT("_ZN2nn2fs9FlushFileENS0_10FileHandleE");
uint32_t NnFsSetFileSize(NnFsFileHandle file, int64_t size)
    SDK_EXPORT("_ZN2nn2fs11SetFileSizeENS0_10FileHandleEl");
uint32_t NnFsGetFileSize(int64_t *size, NnFsFileHandle file)
    SDK_EXPORT("_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE");
uint32_t NnFsOpenDirectory(NnFsDirectoryHandle *out, const char *path, int mode)
    SDK_EXPORT("_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci");
uint32_t NnFsReadDirectory(int64_t *count, NnFsDirectoryEntry *entries,
                           NnFsDirectoryHandle directory, int64_t max)
    SDK_EXPORT("_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl");
uint32_t NnFsGetDirectoryEntryCount(int64_t *count, NnFsDirectoryHandle directory)
    SDK_EXPORT("_ZN2nn2fs22GetDirectoryEntryCountEPlNS0_15DirectoryHandleE");
void NnFsCloseDirectory(NnFsDirectoryHandle directory)
    SDK_EXPORT("_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE");

/// nn::time::IsInitialized reached by a tail call, a branch to its entry, as a game's code reaches
/// a function whose answer it returns. In assembly, so no compiler setting makes it a call.
bool SdkTailIsInitialized(void);

/// Whether the program's own code made the mount @p name, which says the machine took
/// MountSdCard or MountTemporaryStorage when the call that made it succeeded.
bool SdkFsOwnMount(const char *name);

// ----------------------------------------------------------------------------------- nn::time

/// nn::time's types are libnx's, which have the SDK's layouts: PosixTime is an s64,
/// SystemClockContext TimeSystemClockContext (0x20), SteadyClockTimePoint TimeSteadyClockTimePoint
/// (0x18), LocationName TimeLocationName (0x24), TimeZoneRule (0x4000), CalendarTime
/// TimeCalendarTime (8) and CalendarAdditionalInfo TimeCalendarAdditionalInfo (0x18). A
/// ClockSnapshot is 0xD0 bytes, a TimeSpan an s64 of nanoseconds.
#define NN_TIME_SNAPSHOT_SIZE 0xD0u
typedef struct {
    uint8_t bytes[NN_TIME_SNAPSHOT_SIZE];
} __attribute__((aligned(8))) NnTimeClockSnapshot;

uint32_t NnTimeInitialize(void) SDK_EXPORT("_ZN2nn4time10InitializeEv");
bool NnTimeIsInitialized(void) SDK_EXPORT("_ZN2nn4time13IsInitializedEv");
uint32_t NnTimeUserGetCurrentTime(int64_t *out)
    SDK_EXPORT("_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE");
uint32_t NnTimeUserGetContext(TimeSystemClockContext *out)
    SDK_EXPORT("_ZN2nn4time23StandardUserSystemClock21GetSystemClockContextEPNS0_"
               "18SystemClockContextE");
uint32_t NnTimeNetworkGetCurrentTime(int64_t *out)
    SDK_EXPORT("_ZN2nn4time26StandardNetworkSystemClock14GetCurrentTimeEPNS0_9PosixTimeE");
uint32_t NnTimeSteadyGetCurrentTimePoint(TimeSteadyClockTimePoint *out)
    SDK_EXPORT("_ZN2nn4time19StandardSteadyClock19GetCurrentTimePointEPNS0_"
               "20SteadyClockTimePointE");
uint32_t NnTimeUserGetCurrentTimespec(struct timespec *out)
    SDK_EXPORT("_ZN2nn4time41GetStandardUserSystemClockCurrentTimespecEP8timespec");
void NnTimeGetDeviceLocationName(TimeLocationName *out)
    SDK_EXPORT("_ZN2nn4time21GetDeviceLocationNameEPNS0_12LocationNameE");
uint32_t NnTimeLoadTimeZoneRule(TimeZoneRule *rule, const TimeLocationName *name)
    SDK_EXPORT("_ZN2nn4time16LoadTimeZoneRuleEPNS0_12TimeZoneRuleERKNS0_12LocationNameE");
uint32_t NnTimeToCalendarTimeMine(TimeCalendarTime *date, TimeCalendarAdditionalInfo *info,
                                  const int64_t *time)
    SDK_EXPORT("_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_"
               "22CalendarAdditionalInfoERKNS0_9PosixTimeE");
uint32_t NnTimeToCalendarTime(TimeCalendarTime *date, TimeCalendarAdditionalInfo *info,
                              const int64_t *time, const TimeZoneRule *rule)
    SDK_EXPORT("_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_"
               "22CalendarAdditionalInfoERKNS0_9PosixTimeERKNS0_12TimeZoneRuleE");
uint32_t NnTimeToPosixTimeMine(int32_t *count, int64_t *out, int32_t max,
                               const TimeCalendarTime *date)
    SDK_EXPORT("_ZN2nn4time11ToPosixTimeEPiPNS0_9PosixTimeEiRKNS0_12CalendarTimeE");
uint32_t NnTimeToPosixTime(int32_t *count, int64_t *out, int32_t max,
                           const TimeCalendarTime *date, const TimeZoneRule *rule)
    SDK_EXPORT("_ZN2nn4time11ToPosixTimeEPiPNS0_9PosixTimeEiRKNS0_12CalendarTimeERKNS0_"
               "12TimeZoneRuleE");
int32_t NnTimeGetTotalLocationNameCount(void)
    SDK_EXPORT("_ZN2nn4time25GetTotalLocationNameCountEv");
void NnTimeLoadLocationNameList(int32_t *count, TimeLocationName *out, int32_t max,
                                int32_t index)
    SDK_EXPORT("_ZN2nn4time20LoadLocationNameListEPiPNS0_12LocationNameEii");
void NnTimeGetDeviceLocationNameAndUpdatedTime(TimeLocationName *name,
                                               TimeSteadyClockTimePoint *point)
    SDK_EXPORT("_ZN2nn4time6detail35GetDeviceLocationNameAndUpdatedTimeEPNS0_12LocationNameEPNS0_"
               "20SteadyClockTimePointE");
/// nn::time::TimeZoneRuleVersion: a string in 0x10 bytes.
void NnTimeGetTimeZoneRuleVersion(char out[0x10])
    SDK_EXPORT("_ZN2nn4time22GetTimeZoneRuleVersionEPNS0_19TimeZoneRuleVersionE");
void NnTimeSnapshotCreate(NnTimeClockSnapshot *out)
    SDK_EXPORT("_ZN2nn4time13ClockSnapshot29CreateWithStandardSystemClockEPS1_");
bool NnTimeCalculateSpanBetween(int64_t *span, const NnTimeClockSnapshot *a,
                                const NnTimeClockSnapshot *b)
    SDK_EXPORT("_ZN2nn4time20CalculateSpanBetweenEPNS_8TimeSpanERKNS0_13ClockSnapshotES5_");
uint32_t NnTimeSetLocalCurrentTime(const int64_t *time)
    SDK_EXPORT("_ZN2nn4time38SetStandardLocalSystemClockCurrentTimeERKNS0_9PosixTimeE");
uint32_t NnTimeSetLocalContext(const TimeSystemClockContext *context)
    SDK_EXPORT("_ZN2nn4time34SetStandardLocalSystemClockContextERKNS0_18SystemClockContextE");
uint32_t NnTimeSetNetworkCurrentTime(const int64_t *time)
    SDK_EXPORT("_ZN2nn4time40SetStandardNetworkSystemClockCurrentTimeERKNS0_9PosixTimeE");

// ------------------------------------------------------------------------------------- nn::pl

/// nn::pl::SharedFontType 0 to 5 and nn::pl::LoadState's loaded; nn::settings::LanguageCode, eight
/// characters in one register.
#define NN_PL_FONTS 6u
#define NN_PL_LOADED 1
typedef struct {
    char code[8];
} NnSettingsLanguageCode;

void NnPlRequestSharedFontLoad(int32_t type)
    SDK_EXPORT("_ZN2nn2pl21RequestSharedFontLoadENS0_14SharedFontTypeE");
int32_t NnPlGetSharedFontLoadState(int32_t type)
    SDK_EXPORT("_ZN2nn2pl22GetSharedFontLoadStateENS0_14SharedFontTypeE");
size_t NnPlGetSharedFontSize(int32_t type)
    SDK_EXPORT("_ZN2nn2pl17GetSharedFontSizeENS0_14SharedFontTypeE");
const void *NnPlGetSharedFontAddress(int32_t type)
    SDK_EXPORT("_ZN2nn2pl20GetSharedFontAddressENS0_14SharedFontTypeE");
bool NnPlGetSharedFontInOrderOfPriority(int32_t *count, int32_t *types, const void **addresses,
                                        size_t *sizes, int32_t max,
                                        NnSettingsLanguageCode language)
    SDK_EXPORT("_ZN2nn2pl30GetSharedFontInOrderOfPriorityEPiPNS0_14SharedFontTypeEPPKvPmiNS_"
               "8settings12LanguageCodeE");

// ------------------------------------------------------------------------------------ nn::ngc

/// nn::ngc::ProfanityFilter as the SDK from 20.0.0 lays it out: the flag of the language it was
/// built for at 8, whether it is initialized at 9, the skip, mask and normalization modes at 0xc,
/// 0x10 and 0x14, the work buffer and its size at 0x18 and 0x20, the heap the SDK builds there at
/// 0x28. The probe's own code keeps its session after them.
typedef struct {
    uint8_t reserved[8];
    uint8_t language;
    uint8_t initialized;
    uint8_t pad[2];
    uint32_t modes[3];
    uint64_t work;
    uint64_t work_size;
    uint64_t heap;
    Service own; ///< the probe's own code's ngc:u session, after the SDK's fields
} NnNgcProfanityFilter;
_Static_assert(offsetof(NnNgcProfanityFilter, initialized) == 9 &&
                   offsetof(NnNgcProfanityFilter, work) == 0x18 &&
                   offsetof(NnNgcProfanityFilter, heap) == 0x28,
               "the ProfanityFilter's fields are where the SDK has them");
/// The smallest work buffer the SDK takes.
#define NN_NGC_WORK_MIN 0x20000u

uint32_t NnNgcInitialize(NnNgcProfanityFilter *filter, void *work, size_t size, bool language)
    SDK_EXPORT("_ZN2nn3ngc15ProfanityFilter10InitializeEPvmb");
void NnNgcFinalize(NnNgcProfanityFilter *filter)
    SDK_EXPORT("_ZN2nn3ngc15ProfanityFilter8FinalizeEv");
uint32_t NnNgcGetContentVersion(NnNgcProfanityFilter *filter)
    SDK_EXPORT("_ZN2nn3ngc15ProfanityFilter17GetContentVersionEv");
uint32_t NnNgcCheckProfanityWords(NnNgcProfanityFilter *filter, uint32_t *found,
                                  uint32_t patterns, const char **words, size_t count)
    SDK_EXPORT("_ZN2nn3ngc15ProfanityFilter19CheckProfanityWordsEPjjPPKcm");
uint32_t NnNgcMaskProfanityWordsInText(NnNgcProfanityFilter *filter, int32_t *count, char *text,
                                       uint32_t patterns)
    SDK_EXPORT("_ZN2nn3ngc15ProfanityFilter24MaskProfanityWordsInTextEPiPcj");

// ------------------------------------------------------------------------------------ nn::nsd

/// nn::nsd::Fqdn: a name in 0x100 bytes; nn::nsd::EnvironmentIdentifier: one in 8.
#define NN_NSD_FQDN 0x100u
typedef struct {
    char name[NN_NSD_FQDN];
} NnNsdFqdn;
typedef struct {
    char name[8];
} NnNsdEnvironmentIdentifier;

uint32_t NnNsdResolve(NnNsdFqdn *out, const NnNsdFqdn *in)
    SDK_EXPORT("_ZN2nn3nsd7ResolveEPNS0_4FqdnERKS1_");
uint32_t NnNsdResolveEx(NnNsdFqdn *out, const NnNsdFqdn *in)
    SDK_EXPORT("_ZN2nn3nsd9ResolveExEPNS0_4FqdnERKS1_");
void NnNsdGetEnvironmentIdentifier(NnNsdEnvironmentIdentifier *out)
    SDK_EXPORT("_ZN2nn3nsd24GetEnvironmentIdentifierEPNS0_21EnvironmentIdentifierE");

// ------------------------------------------------------------------------------------ nn::pctl

/// nn::pctl, the parental controls a game asks before it lets a player talk to others: Initialize,
/// which the SDK's start calls, whether a restriction is set, whether free communication is
/// permitted (@p show_error: whether the SDK shows the system's error when it is not), and the
/// safety level.
uint32_t NnPctlInitialize(void) SDK_EXPORT("_ZN2nn4pctl10InitializeEv");
bool NnPctlIsRestrictionEnabled(void) SDK_EXPORT("_ZN2nn4pctl20IsRestrictionEnabledEv");
bool NnPctlCheckFreeCommunicationPermission(bool show_error)
    SDK_EXPORT("_ZN2nn4pctl32CheckFreeCommunicationPermissionEb");
uint32_t NnPctlGetSafetyLevel(void) SDK_EXPORT("_ZN2nn4pctl14GetSafetyLevelEv");

// ------------------------------------------------------------------------------------- nn::os

/// nn::os::SystemEvent as the program keeps it: the readable handle attached to it and how it
/// clears. The SDK's functions that hand a game an event attach a handle to the game's
/// SystemEvent through nn::os::AttachReadableHandleToSystemEvent, which a machine that answers
/// them calls in the game's module by its symbol: this program's, below. Nothing else reads the
/// fields, so they are the program's own, in the room the SDK's SystemEventType takes.
#define NN_OS_SYSTEM_EVENT_SIZE 0x28u
#define NN_OS_CLEAR_AUTO 1
typedef struct {
    uint32_t handle;
    uint8_t attached;
    uint8_t managed;    ///< the event closes the handle when it is let go
    uint8_t clear_mode; ///< nn::os::EventClearMode: NN_OS_CLEAR_AUTO clears it as a wait ends
    uint8_t reserved[NN_OS_SYSTEM_EVENT_SIZE - 7u];
} __attribute__((aligned(8))) NnOsSystemEvent;

void NnOsAttachReadableHandleToSystemEvent(NnOsSystemEvent *event, uint32_t handle, bool managed,
                                           int32_t clear_mode)
    SDK_EXPORT("_ZN2nn2os33AttachReadableHandleToSystemEventEPNS0_15SystemEventTypeEjbNS0_"
               "14EventClearModeE");

/// Waits up to @p ns for @p event's handle, clearing it after the wait when it clears by itself;
/// false when nothing is attached or the wait timed out. The program's, not the SDK's.
bool SdkEventWait(NnOsSystemEvent *event, uint64_t ns);
/// Clears @p event's handle, so the next wait waits for a signal after this.
void SdkEventClear(NnOsSystemEvent *event);
/// Lets @p event's handle go, closing it when the event manages it.
void SdkEventClose(NnOsSystemEvent *event);

// ---------------------------------------------------------------------------------- nn::audio

/// nn::audio's names: an AudioDeviceName, AudioOutInfo and AudioInInfo are each a name in 0x100
/// bytes.
#define NN_AUDIO_NAME 0x100u
typedef struct {
    char name[NN_AUDIO_NAME];
} NnAudioName;

/// nn::audio::AudioOutParameter: the rate, which an out ignores (it plays at 48 kHz), and the
/// channels in its first 16 bits; the SDK's InitializeAudioOutParameter writes a word after them
/// that a machine reads nothing of.
typedef struct {
    int32_t sample_rate;
    uint16_t channel_count;
    uint16_t reserved;
    uint32_t magic;
} NnAudioOutParameter;

/// nn::audio::AudioOut as every SDK from 0.12 to 20.5 lays out what its own accessors read: the
/// rate, channels, sample format and state OpenAudioOut writes, and the out's name. The program's
/// own code keeps its IAudioOut session after them.
typedef struct {
    uint32_t sample_rate;
    uint32_t channel_count;
    uint32_t sample_format;
    uint32_t state;
    char name[NN_AUDIO_NAME];
    Service own;
} NnAudioOut;
_Static_assert(offsetof(NnAudioOut, state) == 0xC && offsetof(NnAudioOut, own) == 0x110,
               "the AudioOut's fields are where the SDK has them");
/// nn::audio::AudioOutState: started, stopped.
#define NN_AUDIO_OUT_STARTED 0u
#define NN_AUDIO_OUT_STOPPED 1u

/// nn::audio::AudioOutBuffer: the next one, the samples, their capacity and size and the offset
/// into them, 8 bytes each: libnx's AudioOutBuffer, which the session takes whole.
typedef AudioOutBuffer NnAudioOutBuffer;
_Static_assert(sizeof(NnAudioOutBuffer) == 0x28, "an AudioOutBuffer is 0x28 bytes");

uint32_t NnAudioOpenAudioOut(NnAudioOut *out, const char *name, const NnAudioOutParameter *param)
    SDK_EXPORT("_ZN2nn5audio12OpenAudioOutEPNS0_8AudioOutEPKcRKNS0_17AudioOutParameterE");
uint32_t NnAudioOpenDefaultAudioOut(NnAudioOut *out, const NnAudioOutParameter *param)
    SDK_EXPORT("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutERKNS0_17AudioOutParameterE");
void NnAudioCloseAudioOut(NnAudioOut *out)
    SDK_EXPORT("_ZN2nn5audio13CloseAudioOutEPNS0_8AudioOutE");
uint32_t NnAudioGetAudioOutState(const NnAudioOut *out)
    SDK_EXPORT("_ZN2nn5audio16GetAudioOutStateEPKNS0_8AudioOutE");
uint32_t NnAudioStartAudioOut(NnAudioOut *out)
    SDK_EXPORT("_ZN2nn5audio13StartAudioOutEPNS0_8AudioOutE");
void NnAudioStopAudioOut(NnAudioOut *out) SDK_EXPORT("_ZN2nn5audio12StopAudioOutEPNS0_8AudioOutE");
bool NnAudioAppendAudioOutBuffer(NnAudioOut *out, NnAudioOutBuffer *buffer)
    SDK_EXPORT("_ZN2nn5audio20AppendAudioOutBufferEPNS0_8AudioOutEPNS0_14AudioOutBufferE");
NnAudioOutBuffer *NnAudioGetReleasedAudioOutBuffer(NnAudioOut *out)
    SDK_EXPORT("_ZN2nn5audio25GetReleasedAudioOutBufferEPNS0_8AudioOutE");
bool NnAudioContainsAudioOutBuffer(const NnAudioOut *out, const NnAudioOutBuffer *buffer)
    SDK_EXPORT("_ZN2nn5audio22ContainsAudioOutBufferEPKNS0_8AudioOutEPKNS0_14AudioOutBufferE");
int32_t NnAudioGetAudioOutBufferCount(const NnAudioOut *out)
    SDK_EXPORT("_ZN2nn5audio22GetAudioOutBufferCountEPKNS0_8AudioOutE");
bool NnAudioFlushAudioOutBuffers(NnAudioOut *out)
    SDK_EXPORT("_ZN2nn5audio20FlushAudioOutBuffersEPNS0_8AudioOutE");
void NnAudioSetAudioOutVolume(NnAudioOut *out, float volume)
    SDK_EXPORT("_ZN2nn5audio17SetAudioOutVolumeEPNS0_8AudioOutEf");
float NnAudioGetAudioOutVolume(const NnAudioOut *out)
    SDK_EXPORT("_ZN2nn5audio17GetAudioOutVolumeEPKNS0_8AudioOutE");
int32_t NnAudioListAudioOuts(NnAudioName *infos, int32_t count)
    SDK_EXPORT("_ZN2nn5audio13ListAudioOutsEPNS0_12AudioOutInfoEi");

/// The audio device: the outputs by name, the active one, and an output's volume.
int32_t NnAudioListAudioDeviceName(NnAudioName *names, int32_t count)
    SDK_EXPORT("_ZN2nn5audio19ListAudioDeviceNameEPNS0_15AudioDeviceNameEi");
void NnAudioGetActiveAudioDeviceName(NnAudioName *name)
    SDK_EXPORT("_ZN2nn5audio24GetActiveAudioDeviceNameEPNS0_15AudioDeviceNameE");
void NnAudioSetAudioDeviceOutputVolume(const NnAudioName *name, float volume)
    SDK_EXPORT("_ZN2nn5audio26SetAudioDeviceOutputVolumeEPKNS0_15AudioDeviceNameEf");
float NnAudioGetAudioDeviceOutputVolume(const NnAudioName *name)
    SDK_EXPORT("_ZN2nn5audio26GetAudioDeviceOutputVolumeEPKNS0_15AudioDeviceNameE");

/// The audio ins: their names.
int32_t NnAudioListAudioIns(NnAudioName *infos, int32_t count)
    SDK_EXPORT("_ZN2nn5audio12ListAudioInsEPNS0_11AudioInInfoEi");

/// The result of the own code's last request for a function whose SDK signature carries none (a
/// device's volume, an out's count, a renderer's rate, pctl's flags), so a case can say what
/// refused it: nought when it was answered. Reading it clears it, and a call a machine took writes
/// none.
uint32_t SdkOwnResult(void);
/// Keeps @p rc as the own code's last request's result (SdkOwnResult).
void SdkOwnNote(Result rc);

/// nn::audio::AudioRendererParameter as the SDK from 9.0.0 lays it out (0x38 bytes): the counts,
/// the voice drop flag, the splitters, the rendering device and execution mode a word each, and
/// the revision last.
typedef struct {
    uint32_t sample_rate;
    uint32_t sample_count;
    uint32_t mix_buffer_count;
    uint32_t sub_mix_count;
    uint32_t voice_count;
    uint32_t sink_count;
    uint32_t effect_count;
    uint32_t performance_frame_count;
    uint8_t voice_drop;
    uint8_t reserved[3];
    uint32_t splitter_count;
    uint32_t splitter_send_channel_count;
    uint32_t rendering_device;
    uint32_t execution_mode;
    uint32_t revision;
} NnAudioRendererParameter;
_Static_assert(sizeof(NnAudioRendererParameter) == 0x38 &&
                   offsetof(NnAudioRendererParameter, revision) == 0x34,
               "the AudioRendererParameter's fields are where the SDK has them");

/// nn::audio::AudioRendererHandle: two words, passed in two registers. The own code's session is
/// the first.
typedef struct {
    void *session;
    void *context;
} NnAudioRendererHandle;

/// One of the renderer's managers, as the program's managers below take it: the sections it writes
/// into an update (the voice manager's channel resources first, then its voices), the answer's
/// section it reads, and for the behaviour manager the revision. A machine never reads one: it
/// hands it back to the manager's functions.
typedef struct {
    const void *in;
    size_t in_size;
    const void *first;
    size_t first_size;
    size_t out_size;
    uint32_t revision;
} NnAudioManager;

/// nn::audio::AudioRendererConfig as the SDK from 9.0.0 lays it out (0x68 bytes): its eight
/// managers, where an update leaves the frames rendered so far, and the update's two buffers.
/// RequestUpdateAudioRenderer writes the update's header and runs each manager on the first, hands
/// it to the renderer, and runs the managers on its answer in the second.
typedef struct {
    NnAudioManager *voices;
    NnAudioManager *mixes;
    NnAudioManager *effects;
    NnAudioManager *sinks;
    NnAudioManager *performance;
    NnAudioManager *pools;
    NnAudioManager *behaviour;
    NnAudioManager *splitters;
    uint64_t *frames;
    void *in;
    uint64_t in_size;
    void *out;
    uint64_t out_size;
} NnAudioRendererConfig;
_Static_assert(sizeof(NnAudioRendererConfig) == 0x68 &&
                   offsetof(NnAudioRendererConfig, frames) == 0x40 &&
                   offsetof(NnAudioRendererConfig, in) == 0x48 &&
                   offsetof(NnAudioRendererConfig, out_size) == 0x60,
               "the AudioRendererConfig's fields are where the SDK has them");
/// An update's header and the size of each section in it, as the renderer reads them.
#define NN_AUDIO_UPDATE_HEADER 0x40u
/// The renderer's revision 5, "REV5": the first whose answer ends with the frames rendered so far,
/// which RequestUpdateAudioRenderer of an SDK from 6.0.0 reads back into the config.
#define NN_AUDIO_REVISION_5 0x35564552u

size_t NnAudioGetAudioRendererWorkBufferSize(const NnAudioRendererParameter *param)
    SDK_EXPORT("_ZN2nn5audio30GetAudioRendererWorkBufferSizeERKNS0_22AudioRendererParameterE");
uint32_t NnAudioOpenAudioRenderer(NnAudioRendererHandle *handle, NnOsSystemEvent *event,
                                  const NnAudioRendererParameter *param, void *work, size_t size)
    SDK_EXPORT("_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleEPNS_2os11SystemEvent"
               "ERKNS0_22AudioRendererParameterEPvm");
void NnAudioCloseAudioRenderer(NnAudioRendererHandle handle)
    SDK_EXPORT("_ZN2nn5audio18CloseAudioRendererENS0_19AudioRendererHandleE");
uint32_t NnAudioStartAudioRenderer(NnAudioRendererHandle handle)
    SDK_EXPORT("_ZN2nn5audio18StartAudioRendererENS0_19AudioRendererHandleE");
void NnAudioStopAudioRenderer(NnAudioRendererHandle handle)
    SDK_EXPORT("_ZN2nn5audio17StopAudioRendererENS0_19AudioRendererHandleE");
uint32_t NnAudioGetAudioRendererState(NnAudioRendererHandle handle)
    SDK_EXPORT("_ZN2nn5audio21GetAudioRendererStateENS0_19AudioRendererHandleE");
int32_t NnAudioGetAudioRendererSampleRate(NnAudioRendererHandle handle)
    SDK_EXPORT("_ZN2nn5audio26GetAudioRendererSampleRateENS0_19AudioRendererHandleE");
int32_t NnAudioGetAudioRendererSampleCount(NnAudioRendererHandle handle)
    SDK_EXPORT("_ZN2nn5audio27GetAudioRendererSampleCountENS0_19AudioRendererHandleE");
int32_t NnAudioGetAudioRendererMixBufferCount(NnAudioRendererHandle handle)
    SDK_EXPORT("_ZN2nn5audio30GetAudioRendererMixBufferCountENS0_19AudioRendererHandleE");
void NnAudioSetAudioRendererRenderingTimeLimit(NnAudioRendererHandle handle, int32_t percent)
    SDK_EXPORT("_ZN2nn5audio34SetAudioRendererRenderingTimeLimitENS0_19AudioRendererHandleEi");
int32_t NnAudioGetAudioRendererRenderingTimeLimit(NnAudioRendererHandle handle)
    SDK_EXPORT("_ZN2nn5audio34GetAudioRendererRenderingTimeLimitENS0_19AudioRendererHandleE");
void NnAudioSetAudioRendererVoiceDropParameter(NnAudioRendererHandle handle, float parameter)
    SDK_EXPORT("_ZN2nn5audio34SetAudioRendererVoiceDropParameterENS0_19AudioRendererHandleEf");
float NnAudioGetAudioRendererVoiceDropParameter(NnAudioRendererHandle handle)
    SDK_EXPORT("_ZN2nn5audio34GetAudioRendererVoiceDropParameterENS0_19AudioRendererHandleE");
uint32_t NnAudioRequestUpdateAudioRenderer(NnAudioRendererHandle handle,
                                           const NnAudioRendererConfig *config)
    SDK_EXPORT("_ZN2nn5audio26RequestUpdateAudioRendererENS0_19AudioRendererHandleEPKNS0_"
               "19AudioRendererConfigE");

/// The managers' functions RequestUpdateAudioRenderer runs, under SDK 20.5's symbols: each that
/// writes a section copies its manager's into the update and gives its size; each that reads one
/// gives the size of its manager's; the voice manager writes its channel resources, then its
/// voices, and gives each size through a pointer.
size_t NnAudioBehaviourUpdateIn(const NnAudioManager *m, void *at)
    SDK_EXPORT("_ZNK2nn5audio15BehaviorManager17UpdateInParameterEPNS0_8renderer3ipc8Behavior10"
               "UpdateDataILi0EEE");
uint32_t NnAudioBehaviourGetRevision(const NnAudioManager *m)
    SDK_EXPORT("_ZNK2nn5audio15BehaviorManager11GetRevisionEv");
uint32_t NnAudioBehaviourCheckErrors(NnAudioManager *m)
    SDK_EXPORT("_ZN2nn5audio15BehaviorManager11CheckErrorsEv");
size_t NnAudioBehaviourUpdateOut(NnAudioManager *m, const void *at)
    SDK_EXPORT("_ZN2nn5audio15BehaviorManager15UpdateOutStatusEPKNS0_8renderer3ipc8Behavior10"
               "StatusDataILi0EEE");
size_t NnAudioPoolUpdateIn(NnAudioManager *m, void *at)
    SDK_EXPORT("_ZN2nn5audio17MemoryPoolManager17UpdateInParameterEPNS0_8renderer3ipc10MemoryPool10"
               "UpdateDataILi0EEE");
size_t NnAudioPoolUpdateOut(NnAudioManager *m, const void *at)
    SDK_EXPORT("_ZN2nn5audio17MemoryPoolManager15UpdateOutStatusEPKNS0_8renderer3ipc10MemoryPool10"
               "StatusDataILi0EEE");
size_t NnAudioVoiceUpdateIn(NnAudioManager *m, void *at, size_t *voices, size_t *resources)
    SDK_EXPORT("_ZN2nn5audio16VoiceInfoManager28UpdateVoiceRelatedParametersEPvPmS3_");
size_t NnAudioVoiceUpdateOut(NnAudioManager *m, const void *at)
    SDK_EXPORT("_ZN2nn5audio16VoiceInfoManager20UpdateVoiceOutStatusEPKNS0_8renderer3ipc5Voice10"
               "StatusDataILi0EEE");
size_t NnAudioEffectUpdateIn(NnAudioManager *m, void *at)
    SDK_EXPORT("_ZN2nn5audio13EffectManager24UpdateEffectsInParameterEPNS0_8renderer3ipc6Effect10"
               "UpdateDataILi2EEE");
size_t NnAudioEffectUpdateOut(NnAudioManager *m, const void *at)
    SDK_EXPORT("_ZN2nn5audio13EffectManager21UpdateEffectOutStatusEPKNS0_8renderer3ipc6Effect10"
               "StatusDataILi1EEE");
size_t NnAudioSplitterUpdateIn(NnAudioManager *m, void *at)
    SDK_EXPORT("_ZN2nn5audio19SplitterInfoManager17UpdateInParameterEPv");
size_t NnAudioMixUpdateIn(NnAudioManager *m, void *at)
    SDK_EXPORT("_ZN2nn5audio10MixManager20UpdateMixInParameterEPv");
size_t NnAudioSinkUpdateIn(NnAudioManager *m, void *at)
    SDK_EXPORT("_ZN2nn5audio11SinkManager21UpdateSinkInParameterEPNS0_8renderer3ipc4Sink10"
               "UpdateDataILi0EEE");
size_t NnAudioSinkUpdateOut(NnAudioManager *m, const void *at)
    SDK_EXPORT("_ZN2nn5audio11SinkManager19UpdateSinkOutStatusEPKNS0_8renderer3ipc4Sink10"
               "StatusDataILi0EEE");
size_t NnAudioPerformanceUpdateIn(NnAudioManager *m, void *at)
    SDK_EXPORT("_ZN2nn5audio24PerformanceBufferManager17UpdateInParameterEPNS0_8renderer3ipc18"
               "PerformanceMetrics10UpdateDataILi0EEE");
size_t NnAudioPerformanceUpdateOut(NnAudioManager *m, const void *at)
    SDK_EXPORT("_ZN2nn5audio24PerformanceBufferManager15UpdateOutStatusEPKNS0_8renderer3ipc18"
               "PerformanceMetrics10StatusDataILi0EEE");

// ---------------------------------------------------------------------------------- nn::codec

/// nn::codec::HardwareOpusDecoderType as the SDK from 6.0.0 lays out what its own accessors read:
/// the session's object, the rate and the channels, the last decode's time in microseconds, and
/// the flag ResetHardwareOpusDecoder sets for the next decode. The program's own code keeps its
/// hwopus decoder after them.
typedef struct {
    uint64_t session;
    uint8_t reserved0[0x10];
    int32_t sample_rate;
    int32_t channel_count;
    int64_t decode_us;
    uint8_t reset;
    uint8_t reserved1[7];
    HwopusDecoder own;
} NnCodecOpusDecoder;
_Static_assert(offsetof(NnCodecOpusDecoder, sample_rate) == 0x18 &&
                   offsetof(NnCodecOpusDecoder, reset) == 0x28,
               "the HardwareOpusDecoderType's fields are where the SDK has them");
/// nn::codec::OpusResult, which the decoder's functions give where other SDK functions give an
/// nn::Result: success, a packet the decoder refuses, an input shorter than its header says, an
/// output with no room for the packet, and no decoder left.
#define NN_CODEC_OPUS_OK 0
#define NN_CODEC_OPUS_BROKEN_DATA 4
#define NN_CODEC_OPUS_SHORT_INPUT 6
#define NN_CODEC_OPUS_SHORT_OUTPUT 7
#define NN_CODEC_OPUS_OUT_OF_RESOURCE 8

size_t NnCodecGetOpusWorkBufferSize(int32_t rate, int32_t channels, int32_t option)
    SDK_EXPORT("_ZN2nn5codec36GetHardwareOpusDecoderWorkBufferSizeEiii");
int32_t NnCodecInitializeOpusDecoder(NnCodecOpusDecoder *decoder, int32_t rate, int32_t channels,
                                     void *work, size_t size, int32_t option)
    SDK_EXPORT("_ZN2nn5codec29InitializeHardwareOpusDecoderEPNS0_23HardwareOpusDecoderTypeEiiPvmi");
int32_t NnCodecDecodeOpus(NnCodecOpusDecoder *decoder, size_t *consumed, int32_t *samples,
                          int16_t *out, size_t out_size, const void *in, size_t in_size)
    SDK_EXPORT("_ZN2nn5codec33DecodeOpusInterleavedWithHardwareEPNS0_23HardwareOpusDecoderTypeEPmPi"
               "PsmPKvm");
void NnCodecFinalizeOpusDecoder(NnCodecOpusDecoder *decoder)
    SDK_EXPORT("_ZN2nn5codec27FinalizeHardwareOpusDecoderEPNS0_23HardwareOpusDecoderTypeE");

// ------------------------------------------------------------------------ what the own code did

/// Every exported function, for the count of the calls its own code answered.
#define SDK_FUNCTIONS(X)                                                                          \
    X(FS_MOUNT_SD_CARD, "nn::fs::MountSdCard")                                                    \
    X(FS_MOUNT_TEMPORARY, "nn::fs::MountTemporaryStorage")                                        \
    X(FS_UNMOUNT, "nn::fs::Unmount")                                                              \
    X(FS_COMMIT, "nn::fs::Commit")                                                                \
    X(FS_CREATE_FILE, "nn::fs::CreateFile")                                                       \
    X(FS_DELETE_FILE, "nn::fs::DeleteFile")                                                       \
    X(FS_CREATE_DIRECTORY, "nn::fs::CreateDirectory")                                             \
    X(FS_DELETE_DIRECTORY, "nn::fs::DeleteDirectory")                                             \
    X(FS_DELETE_DIRECTORY_RECURSIVELY, "nn::fs::DeleteDirectoryRecursively")                      \
    X(FS_CLEAN_DIRECTORY_RECURSIVELY, "nn::fs::CleanDirectoryRecursively")                        \
    X(FS_RENAME_FILE, "nn::fs::RenameFile")                                                       \
    X(FS_RENAME_DIRECTORY, "nn::fs::RenameDirectory")                                             \
    X(FS_GET_ENTRY_TYPE, "nn::fs::GetEntryType")                                                  \
    X(FS_GET_FREE_SPACE, "nn::fs::GetFreeSpaceSize")                                              \
    X(FS_GET_TOTAL_SPACE, "nn::fs::GetTotalSpaceSize")                                            \
    X(FS_GET_TIME_STAMP, "nn::fs::GetFileTimeStampForDebug")                                      \
    X(FS_OPEN_FILE, "nn::fs::OpenFile")                                                           \
    X(FS_CLOSE_FILE, "nn::fs::CloseFile")                                                         \
    X(FS_READ_FILE, "nn::fs::ReadFile")                                                           \
    X(FS_WRITE_FILE, "nn::fs::WriteFile")                                                         \
    X(FS_FLUSH_FILE, "nn::fs::FlushFile")                                                         \
    X(FS_SET_FILE_SIZE, "nn::fs::SetFileSize")                                                    \
    X(FS_GET_FILE_SIZE, "nn::fs::GetFileSize")                                                    \
    X(FS_OPEN_DIRECTORY, "nn::fs::OpenDirectory")                                                 \
    X(FS_READ_DIRECTORY, "nn::fs::ReadDirectory")                                                 \
    X(FS_GET_DIRECTORY_ENTRY_COUNT, "nn::fs::GetDirectoryEntryCount")                             \
    X(FS_CLOSE_DIRECTORY, "nn::fs::CloseDirectory")                                               \
    X(TIME_INITIALIZE, "nn::time::Initialize")                                                    \
    X(TIME_IS_INITIALIZED, "nn::time::IsInitialized")                                             \
    X(TIME_USER_TIME, "nn::time::StandardUserSystemClock::GetCurrentTime")                        \
    X(TIME_USER_CONTEXT, "nn::time::StandardUserSystemClock::GetSystemClockContext")              \
    X(TIME_NETWORK_TIME, "nn::time::StandardNetworkSystemClock::GetCurrentTime")                  \
    X(TIME_STEADY_POINT, "nn::time::StandardSteadyClock::GetCurrentTimePoint")                    \
    X(TIME_USER_TIMESPEC, "nn::time::GetStandardUserSystemClockCurrentTimespec")                  \
    X(TIME_DEVICE_LOCATION, "nn::time::GetDeviceLocationName")                                    \
    X(TIME_LOAD_RULE, "nn::time::LoadTimeZoneRule")                                               \
    X(TIME_TO_CALENDAR_MINE, "nn::time::ToCalendarTime (the device's rule)")                      \
    X(TIME_TO_CALENDAR, "nn::time::ToCalendarTime")                                               \
    X(TIME_TO_POSIX_MINE, "nn::time::ToPosixTime (the device's rule)")                            \
    X(TIME_TO_POSIX, "nn::time::ToPosixTime")                                                     \
    X(TIME_LOCATION_COUNT, "nn::time::GetTotalLocationNameCount")                                 \
    X(TIME_LOCATION_LIST, "nn::time::LoadLocationNameList")                                       \
    X(TIME_LOCATION_UPDATED, "nn::time::detail::GetDeviceLocationNameAndUpdatedTime")             \
    X(TIME_RULE_VERSION, "nn::time::GetTimeZoneRuleVersion")                                      \
    X(TIME_SNAPSHOT, "nn::time::ClockSnapshot::CreateWithStandardSystemClock")                    \
    X(TIME_SPAN_BETWEEN, "nn::time::CalculateSpanBetween")                                        \
    X(TIME_SET_LOCAL_TIME, "nn::time::SetStandardLocalSystemClockCurrentTime")                    \
    X(TIME_SET_LOCAL_CONTEXT, "nn::time::SetStandardLocalSystemClockContext")                     \
    X(TIME_SET_NETWORK_TIME, "nn::time::SetStandardNetworkSystemClockCurrentTime")                \
    X(PL_REQUEST_LOAD, "nn::pl::RequestSharedFontLoad")                                           \
    X(PL_LOAD_STATE, "nn::pl::GetSharedFontLoadState")                                            \
    X(PL_SIZE, "nn::pl::GetSharedFontSize")                                                       \
    X(PL_ADDRESS, "nn::pl::GetSharedFontAddress")                                                 \
    X(PL_ORDER, "nn::pl::GetSharedFontInOrderOfPriority")                                         \
    X(NGC_INITIALIZE, "nn::ngc::ProfanityFilter::Initialize")                                     \
    X(NGC_FINALIZE, "nn::ngc::ProfanityFilter::Finalize")                                         \
    X(NGC_VERSION, "nn::ngc::ProfanityFilter::GetContentVersion")                                 \
    X(NGC_CHECK, "nn::ngc::ProfanityFilter::CheckProfanityWords")                                 \
    X(NGC_MASK, "nn::ngc::ProfanityFilter::MaskProfanityWordsInText")                             \
    X(NSD_RESOLVE, "nn::nsd::Resolve")                                                            \
    X(NSD_RESOLVE_EX, "nn::nsd::ResolveEx")                                                       \
    X(NSD_ENVIRONMENT, "nn::nsd::GetEnvironmentIdentifier")                                       \
    X(PCTL_INITIALIZE, "nn::pctl::Initialize")                                                    \
    X(PCTL_RESTRICTION, "nn::pctl::IsRestrictionEnabled")                                         \
    X(PCTL_FREE_COMMUNICATION, "nn::pctl::CheckFreeCommunicationPermission")                      \
    X(PCTL_SAFETY_LEVEL, "nn::pctl::GetSafetyLevel")                                              \
    X(AUDIO_OPEN_OUT, "nn::audio::OpenAudioOut")                                                  \
    X(AUDIO_OPEN_DEFAULT_OUT, "nn::audio::OpenDefaultAudioOut")                                   \
    X(AUDIO_CLOSE_OUT, "nn::audio::CloseAudioOut")                                                \
    X(AUDIO_OUT_STATE, "nn::audio::GetAudioOutState")                                             \
    X(AUDIO_START_OUT, "nn::audio::StartAudioOut")                                                \
    X(AUDIO_STOP_OUT, "nn::audio::StopAudioOut")                                                  \
    X(AUDIO_APPEND_OUT, "nn::audio::AppendAudioOutBuffer")                                        \
    X(AUDIO_RELEASED_OUT, "nn::audio::GetReleasedAudioOutBuffer")                                 \
    X(AUDIO_CONTAINS_OUT, "nn::audio::ContainsAudioOutBuffer")                                    \
    X(AUDIO_OUT_COUNT, "nn::audio::GetAudioOutBufferCount")                                       \
    X(AUDIO_FLUSH_OUT, "nn::audio::FlushAudioOutBuffers")                                         \
    X(AUDIO_SET_OUT_VOLUME, "nn::audio::SetAudioOutVolume")                                       \
    X(AUDIO_OUT_VOLUME, "nn::audio::GetAudioOutVolume")                                           \
    X(AUDIO_LIST_OUTS, "nn::audio::ListAudioOuts")                                                \
    X(AUDIO_LIST_DEVICES, "nn::audio::ListAudioDeviceName")                                       \
    X(AUDIO_ACTIVE_DEVICE, "nn::audio::GetActiveAudioDeviceName")                                 \
    X(AUDIO_SET_DEVICE_VOLUME, "nn::audio::SetAudioDeviceOutputVolume")                           \
    X(AUDIO_DEVICE_VOLUME, "nn::audio::GetAudioDeviceOutputVolume")                               \
    X(AUDIO_LIST_INS, "nn::audio::ListAudioIns")                                                  \
    X(AUDIO_RENDERER_WORK, "nn::audio::GetAudioRendererWorkBufferSize")                           \
    X(AUDIO_OPEN_RENDERER, "nn::audio::OpenAudioRenderer")                                        \
    X(AUDIO_CLOSE_RENDERER, "nn::audio::CloseAudioRenderer")                                      \
    X(AUDIO_START_RENDERER, "nn::audio::StartAudioRenderer")                                      \
    X(AUDIO_STOP_RENDERER, "nn::audio::StopAudioRenderer")                                        \
    X(AUDIO_RENDERER_STATE, "nn::audio::GetAudioRendererState")                                   \
    X(AUDIO_RENDERER_RATE, "nn::audio::GetAudioRendererSampleRate")                               \
    X(AUDIO_RENDERER_COUNT, "nn::audio::GetAudioRendererSampleCount")                             \
    X(AUDIO_RENDERER_MIX, "nn::audio::GetAudioRendererMixBufferCount")                            \
    X(AUDIO_SET_RENDERER_LIMIT, "nn::audio::SetAudioRendererRenderingTimeLimit")                  \
    X(AUDIO_RENDERER_LIMIT, "nn::audio::GetAudioRendererRenderingTimeLimit")                      \
    X(AUDIO_SET_RENDERER_DROP, "nn::audio::SetAudioRendererVoiceDropParameter")                   \
    X(AUDIO_RENDERER_DROP, "nn::audio::GetAudioRendererVoiceDropParameter")                       \
    X(AUDIO_UPDATE_RENDERER, "nn::audio::RequestUpdateAudioRenderer")                             \
    X(CODEC_OPUS_WORK, "nn::codec::GetHardwareOpusDecoderWorkBufferSize")                         \
    X(CODEC_OPUS_INITIALIZE, "nn::codec::InitializeHardwareOpusDecoder")                          \
    X(CODEC_OPUS_DECODE, "nn::codec::DecodeOpusInterleavedWithHardware")                          \
    X(CODEC_OPUS_FINALIZE, "nn::codec::FinalizeHardwareOpusDecoder")

typedef enum {
#define SDK_ENUM(id, name) SDK_##id,
    SDK_FUNCTIONS(SDK_ENUM)
#undef SDK_ENUM
        SDK_FUNCTION_COUNT
} SdkFunction;

/// Counts a call the own code of @p function answered.
void SdkOwnCall(SdkFunction function);

/// The calls each function's own code answered so far.
uint32_t SdkOwnCalls(SdkFunction function);

/// The functions whose own code answered a call since @p noted's counts, each with its count, comma
/// separated, in @p out, and @p noted brought up to now. Returns the length: nought, with an empty
/// string, when none did.
size_t SdkOwnDescribeSince(char *out, size_t out_size, uint32_t noted[SDK_FUNCTION_COUNT]);

#ifdef __cplusplus
}
#endif
