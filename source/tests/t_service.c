// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "../check.h"
#include "../report.h"
#include "../serviceref.h"
#include "tests.h"

// The service family: commands a game sends to the system's services, asked the way a game asks
// them, each answer judged by serviceref.c against the one reden gives. One suite per service,
// in the order the family runs them; a case that needs one before it is skipped as `depends`
// when that one did not leave what it needs.

static void Report(ServiceCaseId id, CheckTier tier, bool passed, const char *detail) {
    ReportCase(ServiceCaseSuite(id), ServiceCaseName(id), tier, passed, detail);
}

static void Skip(ServiceCaseId id, ReportReason reason, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static void Skip(ServiceCaseId id, ReportReason reason, const char *fmt, ...) {
    char detail[480];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    ReportSkip(ServiceCaseSuite(id), ServiceCaseName(id), reason, detail);
}

static bool Start(ServiceCaseId id) {
    return ReportStart(ServiceCaseSuite(id), ServiceCaseName(id));
}

// ---------------------------------------------------------------------------------------------
// hwopus

/// Opens decoders of 48 kHz stereo, the way a game's codec library does, until one is refused or
/// two past reden's limit are open; closes one and opens again; closes them all. Each decoder is
/// an object of its own, with a work buffer handed over as transfer memory.
static void RunHwopus(void) {
    static HwopusDecoder decoders[SERVICE_OPUS_TRIES];
    static uint32_t results[SERVICE_OPUS_TRIES];
    memset(decoders, 0, sizeof(decoders));
    memset(results, 0, sizeof(results));
    char detail[480];

    bool opens = false;
    if (Start(SERVICE_OPUS_OPENS)) {
        results[0] = hwopusDecoderInitialize(&decoders[0], 48000, 2);
        opens = R_SUCCEEDED(results[0]);
        snprintf(detail, sizeof(detail), "hwopusDecoderInitialize(48000, 2) returned 0x%x",
                 (unsigned)results[0]);
        Report(SERVICE_OPUS_OPENS, CHECK_NONE, opens, detail);
    }

    bool refused = false;
    if (!opens) {
        Skip(SERVICE_OPUS_LIMITED, REPORT_REASON_DEPENDS, "no decoder opened to count from");
    } else if (Start(SERVICE_OPUS_LIMITED)) {
        uint32_t tried = 1;
        while (tried < SERVICE_OPUS_TRIES) {
            results[tried] = hwopusDecoderInitialize(&decoders[tried], 48000, 2);
            ++tried;
            if (R_FAILED(results[tried - 1u])) {
                refused = true;
                break;
            }
        }
        const bool passed = ServiceJudgeOpusLimit(results, tried, detail, sizeof(detail));
        Report(SERVICE_OPUS_LIMITED, CHECK_EXACT, passed, detail);
    }

    if (!refused) {
        Skip(SERVICE_OPUS_FREED, REPORT_REASON_DEPENDS,
             "the case above refused no decoder, so there is no room to free");
    } else if (Start(SERVICE_OPUS_FREED)) {
        // The first one closed; the one the limit case was refused is asked for again.
        hwopusDecoderExit(&decoders[0]);
        const uint32_t reopened = hwopusDecoderInitialize(&decoders[0], 48000, 2);
        const bool passed = ServiceJudgeOpusFreed(reopened, detail, sizeof(detail));
        Report(SERVICE_OPUS_FREED, CHECK_EXACT, passed, detail);
    }

    for (uint32_t i = 0; i < SERVICE_OPUS_TRIES; ++i) {
        hwopusDecoderExit(&decoders[i]);
    }
}

// ---------------------------------------------------------------------------------------------
// acc:u0, for the suites that need a user

/// acc:u0, the way a program reaches it. libnx's accountInitialize asks InitializeApplicationInfo
/// first, which a loader that registered no launch property for this program refuses; the
/// commands asked here do not read what that call records, so the service is then opened by name
/// without it and the line says so, as the net family's acc suite does.
typedef struct {
    Service *service;
    Service raw;
    bool libnx;
} Account;

static Result AccountOpen(Account *a, char *said, size_t said_size) {
    memset(a, 0, sizeof(*a));
    const Result initialized = accountInitialize(AccountServiceType_Application);
    if (R_SUCCEEDED(initialized)) {
        a->libnx = true;
        a->service = accountGetServiceSession();
        snprintf(said, said_size, "accountInitialize(Application) succeeded");
        return 0;
    }
    const Result rc = smGetService(&a->raw, "acc:u0");
    if (R_FAILED(rc)) {
        snprintf(said, said_size,
                 "accountInitialize(Application) returned 0x%x and smGetService(\"acc:u0\") 0x%x",
                 (unsigned)initialized, (unsigned)rc);
        return rc;
    }
    a->service = &a->raw;
    snprintf(said, said_size,
             "accountInitialize(Application) returned 0x%x, acc:u0 opened by name without "
             "InitializeApplicationInfo",
             (unsigned)initialized);
    return 0;
}

static void AccountClose(Account *a) {
    if (a->libnx) {
        accountExit();
    } else if (a->service != NULL) {
        serviceClose(&a->raw);
    }
    memset(a, 0, sizeof(*a));
}

/// GetLastOpenedUser (4), then the first of ListAllUsers (2) when nobody was opened last.
static Result AccountUser(Account *a, AccountUid *uid, const char **how) {
    memset(uid, 0, sizeof(*uid));
    Result rc = serviceDispatchOut(a->service, 4, *uid);
    *how = "GetLastOpenedUser";
    if (R_FAILED(rc) || !accountUidIsValid(uid)) {
        AccountUid users[8];
        memset(users, 0, sizeof(users));
        rc = serviceDispatch(a->service, 2,
                             .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_Out },
                             .buffers = { { users, sizeof(users) } }, );
        *uid = users[0];
        *how = "ListAllUsers";
    }
    return rc;
}

// ---------------------------------------------------------------------------------------------
// am

/// The account save's size, as IApplicationFunctions takes it: the save's type, seven bytes of
/// padding and the user, which is 8-aligned (reden d2279cbf8d).
typedef struct {
    u8 type;
    u8 pad[7];
    AccountUid uid;
} SaveTarget;

static Result SaveSize(Service *functions, const AccountUid *uid, u64 *normal, u64 *journal) {
    const SaveTarget in = { FsSaveDataType_Account, { 0 }, *uid };
    struct {
        u64 normal, journal;
    } out = { 0, 0 };
    const Result rc = serviceDispatchInOut(functions, 26, in, out);
    *normal = out.normal;
    *journal = out.journal;
    return rc;
}

/// The application's own functions, asked about the account save of a user: EnsureSaveData (20)
/// first, as a game does, then the size, ExtendSaveData (25) by one step on both parts, and the
/// size again.
static void RunAm(void) {
    char detail[480];
    Account account;
    memset(&account, 0, sizeof(account));
    AccountUid uid;
    memset(&uid, 0, sizeof(uid));
    Service *functions = NULL;

    bool opens = false;
    char why[240] = "";
    ReportReason why_reason = REPORT_REASON_DEPENDS;
    if (appletGetAppletType() != AppletType_Application) {
        snprintf(why, sizeof(why),
                 "needs to run as an application, with the application's functions; this program "
                 "runs as applet type %d",
                 (int)appletGetAppletType());
        why_reason = REPORT_REASON_UNAVAILABLE;
        Skip(SERVICE_AM_OPENS, REPORT_REASON_UNAVAILABLE, "%s", why);
    } else if (Start(SERVICE_AM_OPENS)) {
        functions = appletGetServiceSession_Functions();
        char said[240];
        const char *how = "";
        Result rc = AccountOpen(&account, said, sizeof(said));
        if (R_SUCCEEDED(rc)) {
            rc = AccountUser(&account, &uid, &how);
        }
        opens = functions != NULL && serviceIsActive(functions) && accountUidIsValid(&uid);
        snprintf(detail, sizeof(detail), "functions %s; %s; %s 0x%x, user %016llx%016llx",
                 functions != NULL && serviceIsActive(functions) ? "open" : "not open", said, how,
                 (unsigned)rc, (unsigned long long)uid.uid[0], (unsigned long long)uid.uid[1]);
        Report(SERVICE_AM_OPENS, CHECK_NONE, opens, detail);
        if (!opens) {
            snprintf(why, sizeof(why), "no application functions or no user to ask about");
        }
    } else {
        snprintf(why, sizeof(why), "the case that opens the functions did not run");
    }

    if (!opens) {
        Skip(SERVICE_SAVE_SIZE, why_reason, "%s", why);
    } else if (Start(SERVICE_SAVE_SIZE)) {
        u64 ensured = 0;
        const Result ensure = serviceDispatchInOut(functions, 20, uid, ensured);
        ServiceSaveSize seen;
        memset(&seen, 0, sizeof(seen));
        seen.before_rc = SaveSize(functions, &uid, &seen.before_normal, &seen.before_journal);
        seen.asked_normal = seen.before_normal + SERVICE_SAVE_STEP;
        seen.asked_journal = seen.before_journal + SERVICE_SAVE_STEP;
        const struct {
            SaveTarget target;
            u64 normal, journal;
        } in = { { FsSaveDataType_Account, { 0 }, uid }, seen.asked_normal, seen.asked_journal };
        u64 required = 0;
        seen.extend_rc = serviceDispatchInOut(functions, 25, in, required);
        seen.required = required;
        seen.after_rc = SaveSize(functions, &uid, &seen.after_normal, &seen.after_journal);
        char judged[400];
        const bool passed = ServiceJudgeSaveSize(&seen, judged, sizeof(judged));
        snprintf(detail, sizeof(detail), "EnsureSaveData 0x%x; %s", (unsigned)ensure, judged);
        Report(SERVICE_SAVE_SIZE, CHECK_EXACT, passed, detail);
    }
    AccountClose(&account);
}

// ---------------------------------------------------------------------------------------------
// fsp

/// Where a launch says on the memory card which marker it left in the temporary storage, so the
/// next launch knows what to look for; and the marker's place in the storage.
#define SERVICE_DIRECTORY "sdmc:/edenprobe/service"
static const char *const TEMP_PROMISE = SERVICE_DIRECTORY "/temporary";
static const char *const TEMP_MARKER = "/edenprobe-launch";

static uint64_t ReadPromise(void) {
    unsigned long long promised = 0;
    FILE *file = fopen(TEMP_PROMISE, "rb");
    if (file != NULL) {
        if (fscanf(file, "%llx", &promised) != 1) {
            promised = 0;
        }
        fclose(file);
    }
    return promised;
}

/// Leaves a marker of its own in the storage and says so on the card, for the next launch.
static bool LeaveMarker(FsFileSystem *fs, char *said, size_t said_size) {
    const uint64_t marker = armGetSystemTick() | 1u;
    fsFsDeleteFile(fs, TEMP_MARKER);
    Result rc = fsFsCreateFile(fs, TEMP_MARKER, sizeof(marker), 0);
    FsFile file;
    if (R_SUCCEEDED(rc)) {
        rc = fsFsOpenFile(fs, TEMP_MARKER, FsOpenMode_Write, &file);
    }
    if (R_SUCCEEDED(rc)) {
        rc = fsFileWrite(&file, 0, &marker, sizeof(marker), FsWriteOption_Flush);
        fsFileClose(&file);
    }
    if (R_FAILED(rc)) {
        snprintf(said, said_size, "the marker for the next launch was not written: 0x%x",
                 (unsigned)rc);
        return false;
    }
    fsFsCommit(fs);
    mkdir(SERVICE_DIRECTORY, 0777);
    FILE *promise = fopen(TEMP_PROMISE, "wb");
    const bool written = promise != NULL &&
                         fprintf(promise, "%016llx\n", (unsigned long long)marker) == 17 &&
                         fclose(promise) == 0;
    snprintf(said, said_size, written ? "marker %016llx left for the next launch"
                                      : "marker %016llx left, but %s was not written",
             (unsigned long long)marker, TEMP_PROMISE);
    return written;
}

/// The application's temporary storage, which a console empties at each launch of the
/// application. The first case opens it, looks for the marker the launch before left and leaves
/// one of its own, so every request to the service is made inside a case that started; the
/// second judges what the first found against what the launch before said it left.
static void RunFsp(void) {
    char detail[480];
    const uint64_t promised = ReadPromise();
    uint32_t found_rc = 0;
    uint64_t found = 0;
    bool opens = false;
    if (Start(SERVICE_TEMP_OPENS)) {
        FsFileSystem fs;
        memset(&fs, 0, sizeof(fs));
        const Result rc = fsOpen_TemporaryStorage(&fs);
        opens = R_SUCCEEDED(rc);
        char left[200] = "";
        if (opens) {
            FsFile file;
            found_rc = fsFsOpenFile(&fs, TEMP_MARKER, FsOpenMode_Read, &file);
            if (R_SUCCEEDED(found_rc)) {
                u64 read = 0;
                fsFileRead(&file, 0, &found, sizeof(found), FsReadOption_None, &read);
                fsFileClose(&file);
            }
            LeaveMarker(&fs, left, sizeof(left));
            fsFsClose(&fs);
        }
        snprintf(detail, sizeof(detail), "fsOpen_TemporaryStorage returned 0x%x%s%s",
                 (unsigned)rc, opens ? "; " : "", left);
        Report(SERVICE_TEMP_OPENS, CHECK_NONE, opens, detail);
    }

    if (!opens) {
        Skip(SERVICE_TEMP_EMPTY, REPORT_REASON_DEPENDS, "the temporary storage did not open");
    } else if (promised == 0u) {
        Skip(SERVICE_TEMP_EMPTY, REPORT_REASON_DEPENDS,
             "no launch before this one said it left a marker (%s): this launch left one, and "
             "the next one asks",
             TEMP_PROMISE);
    } else if (Start(SERVICE_TEMP_EMPTY)) {
        const bool passed =
            ServiceJudgeTempEmpty(promised, found_rc, found, detail, sizeof(detail));
        Report(SERVICE_TEMP_EMPTY, CHECK_EXACT, passed, detail);
    }
}

void RunService(void) {
    RunHwopus();
    RunAm();
    RunFsp();
}
