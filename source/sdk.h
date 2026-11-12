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
/// docs/ModeFile.md): nn::fs, nn::time, nn::pl, nn::ngc and nn::nsd, under the names and with the
/// arguments a game's SDK exports them with. The program exports each by that name (the
/// Makefile's --export-dynamic-symbol), so a machine that takes a game's function by its symbol
/// takes this one the same way, and none of the code below runs. Where nothing takes it, its own
/// code runs: the request to the service that the IPC mode sends, through libnx, with the layouts
/// the SDK's caller sees. The own code is not the SDK's: it answers what the probe's cases ask,
/// and a request it does not know how to send answers SDK_OWN_UNSUPPORTED.
///
/// Every function here counts the calls its own code answered (SdkOwnCalls), so a run says which
/// of them the machine took and which it left to the program.

/// Each exported function's name in C, its symbol, and attributes no compiler may see through:
/// a call from a case stays a call to the symbol, which is where a machine's hook takes it.
#define SDK_EXPORT(symbol) __asm__(symbol)
#define SDK_FUNCTION __attribute__((noipa, used, visibility("default")))

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
    X(NSD_ENVIRONMENT, "nn::nsd::GetEnvironmentIdentifier")

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

/// The functions whose own code answered a call, each with its count, comma separated, in
/// @p out; "none" when no own code ran. Returns the length.
size_t SdkOwnDescribe(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
