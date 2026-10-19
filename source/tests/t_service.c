// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
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

// ---------------------------------------------------------------------------------------------
// account

/// acc:u0's two commands that pick the user with nobody asked, the old one and the one a game
/// built for 19.0.0 or later sends, each with no network account required.
static void RunAccount(void) {
    char detail[480];
    Account account;
    memset(&account, 0, sizeof(account));
    s32 users = -1;
    bool opens = false;
    if (Start(SERVICE_ACC_OPENS)) {
        char said[240];
        Result rc = AccountOpen(&account, said, sizeof(said));
        if (R_SUCCEEDED(rc)) {
            rc = serviceDispatchOut(account.service, 0, users);
        }
        opens = R_SUCCEEDED(rc) && account.service != NULL;
        snprintf(detail, sizeof(detail), "%s; GetUserCount 0x%x, %d users", said, (unsigned)rc,
                 (int)users);
        Report(SERVICE_ACC_OPENS, CHECK_NONE, opens, detail);
    }

    if (!opens) {
        Skip(SERVICE_SELECT_USER, REPORT_REASON_DEPENDS, "acc:u0 did not open");
    } else if (Start(SERVICE_SELECT_USER)) {
        ServiceSelectUser seen;
        memset(&seen, 0, sizeof(seen));
        seen.users = users;
        seen.firmware19 = hosversionAtLeast(19, 0, 0);
        const u8 network = 0;
        AccountUid uid51, uid52;
        memset(&uid51, 0, sizeof(uid51));
        memset(&uid52, 0, sizeof(uid52));
        seen.rc51 = serviceDispatchInOut(account.service, 51, network, uid51);
        seen.rc52 = serviceDispatchInOut(account.service, 52, network, uid52);
        memcpy(seen.uid51, uid51.uid, sizeof(seen.uid51));
        memcpy(seen.uid52, uid52.uid, sizeof(seen.uid52));
        const bool passed = ServiceJudgeSelectUser(&seen, detail, sizeof(detail));
        Report(SERVICE_SELECT_USER, CHECK_EXACT, passed, detail);
    }
    AccountClose(&account);
}

// ---------------------------------------------------------------------------------------------
// ldn

/// One IUserLocalCommunicationService of ldn:u's, the object a game's ldn library sends every
/// command to (CreateUserLocalCommunicationService, 0), asked without libnx's ldnInitialize so
/// that a case may ask it before Initialize.
static Result LdnCreate(Service *creator, Service *out) {
    memset(out, 0, sizeof(*out));
    return serviceDispatch(creator, 0, .out_num_objects = 1, .out_objects = out, );
}

static Result LdnGetState(Service *s, uint32_t *state) {
    *state = 0xEEu;
    return serviceDispatchOut(s, 0, *state);
}

/// Initialize as libnx's ldnInitialize sends it: 402 with the version libnx gives this firmware
/// and the process id, or 400 before 7.0.0.
static Result LdnInitialize(Service *s) {
    if (!hosversionAtLeast(7, 0, 0)) {
        const u64 reserved = 0;
        return serviceDispatchIn(s, 400, reserved, .in_send_pid = true, );
    }
    const s32 version = hosversionAtLeast(20, 0, 0)   ? 4
                        : hosversionAtLeast(19, 0, 0) ? 3
                        : hosversionAtLeast(18, 0, 0) ? 2
                                                      : 1;
    const struct {
        s32 version;
        u32 pad;
        u64 reserved;
    } in = { version, 0, 0 };
    return serviceDispatchIn(s, 402, in, .in_send_pid = true, );
}

/// CreateNetwork (202) of the network serviceref.h's SERVICE_LDN_* describe, laid out as libnx's
/// ldnCreateNetwork lays it: Product security with a passphrase of sixteen bytes, the user name,
/// and the channel left to the service.
static Result LdnCreateNetwork(Service *s) {
    struct {
        LdnSecurityConfig sec_config;
        LdnUserConfig user_config;
        u32 pad;
        LdnNetworkConfig network_config;
    } in;
    memset(&in, 0, sizeof(in));
    in.sec_config.security_mode = LdnSecurityMode_Product;
    in.sec_config.passphrase_size = 16;
    memset(in.sec_config.passphrase, 0x5A, 16);
    strcpy(in.user_config.user_name, SERVICE_LDN_USER);
    in.network_config.intent_id.local_communication_id = (s64)SERVICE_LDN_LOCAL_ID;
    in.network_config.intent_id.scene_id = SERVICE_LDN_SCENE;
    in.network_config.node_count_max = SERVICE_LDN_ROOM;
    in.network_config.local_communication_version = SERVICE_LDN_VERSION;
    return serviceDispatchIn(s, 202, in);
}

/// The state case's steps by their number in SERVICE_LDN_STEPS.
static Result LdnStep(Service *s, uint32_t step) {
    switch (step) {
    case 0:
        return LdnInitialize(s);
    case 1:
        return serviceDispatch(s, 200);
    case 2:
        return LdnCreateNetwork(s);
    case 3:
        return serviceDispatch(s, 204);
    case 4:
        return serviceDispatch(s, 201);
    case 5:
        return serviceDispatch(s, 300);
    case 6:
        return serviceDispatch(s, 301);
    default:
        return serviceDispatch(s, 401);
    }
}

static LdnNetworkInfo g_ldn_info[2];

static Result LdnAskInfo(Service *s, LdnNetworkInfo *out) {
    memset(out, 0, sizeof(*out));
    return serviceDispatch(s, 1,
                           .buffer_attrs = { SfBufferAttr_FixedSize | SfBufferAttr_HipcPointer |
                                             SfBufferAttr_Out },
                           .buffers = { { out, sizeof(*out) } }, );
}

/// GetNetworkInfoLatestUpdate (101): the network and count entries of node updates, both
/// pointer buffers as libnx's ldnGetNetworkInfoAndHistory sends them.
static Result LdnAskLatest(Service *s, LdnNetworkInfo *info, LdnNodeLatestUpdate *updates,
                           size_t count) {
    memset(info, 0, sizeof(*info));
    memset(updates, 0xEE, count * sizeof(*updates));
    return serviceDispatch(s, 101,
                           .buffer_attrs = { SfBufferAttr_FixedSize | SfBufferAttr_HipcPointer |
                                                 SfBufferAttr_Out,
                                             SfBufferAttr_HipcPointer | SfBufferAttr_Out },
                           .buffers = { { info, sizeof(*info) },
                                        { updates, count * sizeof(*updates) } }, );
}

/// Finalize (401) and close: every case leaves ldn:u as it found it.
static void LdnDone(Service *s) {
    serviceDispatch(s, 401);
    serviceClose(s);
}

/// An object of ldn:u's for the case id, or the case failed with what 0 answered.
static bool LdnMade(ServiceCaseId id, Service *creator, Service *s) {
    const Result made = LdnCreate(creator, s);
    if (R_SUCCEEDED(made)) {
        return true;
    }
    char detail[96];
    snprintf(detail, sizeof(detail), "CreateUserLocalCommunicationService returned 0x%x",
             (unsigned)made);
    Report(id, CHECK_EXACT, false, detail);
    return false;
}

/// Whether Initialize's answer lets the case go on. A refusal that says the machine has no
/// wireless to give (Disabled, AirplaneModeEnabled, NoIpAddress) skips it as unavailable, with
/// what GetState and GetIpv4Address (2) answer after it; any other answer is the case's to judge.
static bool LdnUp(ServiceCaseId id, Service *s, Result initialized) {
    if (initialized != SERVICE_LDN_DISABLED && initialized != SERVICE_LDN_AIRPLANE &&
        initialized != SERVICE_LDN_NO_ADDRESS) {
        return true;
    }
    uint32_t state = 0;
    const Result state_rc = LdnGetState(s, &state);
    struct {
        u32 address, mask;
    } ip = { 0, 0 };
    const Result ip_rc = serviceDispatchOut(s, 2, ip);
    Skip(id, REPORT_REASON_UNAVAILABLE,
         "no wireless to give: Initialize 0x%x, then GetState %u (0x%x), GetIpv4Address 0x%x "
         "(%08x, %08x)",
         (unsigned)initialized, (unsigned)state, (unsigned)state_rc, (unsigned)ip_rc,
         (unsigned)ip.address, (unsigned)ip.mask);
    return false;
}

/// An object that hosts the network serviceref.h's SERVICE_LDN_* describe, for the case id:
/// Initialize, OpenAccessPoint and CreateNetwork, the first of them refused or CreateNetwork's
/// answer left in created for the case to judge. False when the object could not be made or
/// Initialize skipped the case.
static bool LdnHost(ServiceCaseId id, Service *creator, Service *s, uint32_t *created) {
    if (!LdnMade(id, creator, s)) {
        return false;
    }
    const Result up = LdnInitialize(s);
    if (!LdnUp(id, s, up)) {
        LdnDone(s);
        return false;
    }
    *created = R_FAILED(up) ? up : serviceDispatch(s, 200);
    if (R_SUCCEEDED(*created)) {
        *created = LdnCreateNetwork(s);
    }
    return true;
}

static void LdnKeepNode(const LdnNodeInfo *in, ServiceLdnNode *out) {
    out->address = in->ip_addr.addr;
    memcpy(out->mac, in->mac_addr.addr, sizeof(out->mac));
    out->id = in->node_id;
    out->connected = in->is_connected;
    memcpy(out->name, in->user_name, sizeof(out->name));
    out->name[sizeof(out->name) - 1u] = '\0';
    out->version = in->local_communication_version;
}

/// What the network case keeps of GetNetworkInfo's network; the SSID as it came, so that the
/// judge sees where it ends.
static void LdnKeepNetwork(const LdnNetworkInfo *in, ServiceLdnNetwork *out) {
    out->local_id = (uint64_t)in->network_id.intent_id.local_communication_id;
    out->scene = in->network_id.intent_id.scene_id;
    memcpy(out->session, in->network_id.session_id.random, sizeof(out->session));
    memcpy(out->bssid, in->common.bssid.addr, sizeof(out->bssid));
    out->ssid_length = in->common.ssid.len;
    memcpy(out->ssid, in->common.ssid.str, sizeof(out->ssid));
    out->channel = in->common.channel;
    out->link_level = in->common.link_level;
    out->network_type = in->common.network_type;
    out->security_mode = in->security_mode;
    out->room = in->node_count_max;
    out->nodes = in->node_count;
    for (uint32_t i = 0; i < SERVICE_LDN_NODES; ++i) {
        LdnKeepNode(&in->nodes[i], &out->node[i]);
    }
}

static Result LdnAskAdvertise(Service *s, const void *data, size_t size) {
    return serviceDispatch(s, 206,
                           .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
                           .buffers = { { data, size } }, );
}

/// Scan (102) on the Default channel, as libnx's ldnScan sends it, into count networks.
static Result LdnAskScan(Service *s, const LdnScanFilter *filter, LdnNetworkInfo *out,
                         size_t count, s16 *found) {
    const struct {
        s16 channel;
        u8 pad[6];
        LdnScanFilter filter;
    } in = { 0, { 0 }, *filter };
    *found = -1;
    return serviceDispatchInOut(s, 102, in, *found,
                                .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
                                .buffers = { { out, count * sizeof(*out) } }, );
}

/// Connect (302) to network as libnx's ldnConnect sends it: Product security with a passphrase
/// of sixteen bytes, the user name and version of SERVICE_LDN_*, no option.
static Result LdnAskConnect(Service *s, const LdnNetworkInfo *network) {
    struct {
        LdnSecurityConfig sec_config;
        LdnUserConfig user_config;
        s32 version;
        u32 option;
    } in;
    memset(&in, 0, sizeof(in));
    in.sec_config.security_mode = LdnSecurityMode_Product;
    in.sec_config.passphrase_size = 16;
    memset(in.sec_config.passphrase, 0x5A, 16);
    strcpy(in.user_config.user_name, SERVICE_LDN_USER);
    in.version = SERVICE_LDN_VERSION;
    return serviceDispatchIn(s, 302, in,
                             .buffer_attrs = { SfBufferAttr_FixedSize | SfBufferAttr_HipcPointer |
                                               SfBufferAttr_In },
                             .buffers = { { network, sizeof(*network) } }, );
}

/// ldn:u for one console alone, with no other near and no network joined, as a game's local
/// play sets it up: the states an object walks through, its state change event, the commands it
/// refuses out of their state, the network it creates and what it carries, and a station with
/// nothing to find. Each case asks an object of its own and finalizes and closes it before it
/// ends.
static void RunLdnLocal(Service *creator) {
    char detail[480];
    if (creator == NULL) {
        Skip(SERVICE_LDN_STATES, REPORT_REASON_DEPENDS, "ldn:u did not open");
        Skip(SERVICE_LDN_EVENT, REPORT_REASON_DEPENDS, "ldn:u did not open");
        Skip(SERVICE_LDN_REFUSED, REPORT_REASON_DEPENDS, "ldn:u did not open");
        Skip(SERVICE_LDN_NETWORK, REPORT_REASON_DEPENDS, "ldn:u did not open");
        Skip(SERVICE_LDN_UPDATES, REPORT_REASON_DEPENDS, "ldn:u did not open");
        Skip(SERVICE_LDN_CONFIG, REPORT_REASON_DEPENDS, "ldn:u did not open");
        Skip(SERVICE_LDN_ADVERTISE, REPORT_REASON_DEPENDS, "ldn:u did not open");
        Skip(SERVICE_LDN_STATION, REPORT_REASON_DEPENDS, "ldn:u did not open");
        return;
    }
    Service s;
    if (Start(SERVICE_LDN_STATES) && LdnMade(SERVICE_LDN_STATES, creator, &s)) {
        ServiceLdnStates seen;
        memset(&seen, 0, sizeof(seen));
        seen.before_rc = LdnGetState(&s, &seen.before);
        seen.step_rc[0] = LdnInitialize(&s);
        if (LdnUp(SERVICE_LDN_STATES, &s, seen.step_rc[0])) {
            seen.state_rc[0] = LdnGetState(&s, &seen.state[0]);
            for (uint32_t i = 1; i < SERVICE_LDN_STEPS; ++i) {
                seen.step_rc[i] = LdnStep(&s, i);
                seen.state_rc[i] = LdnGetState(&s, &seen.state[i]);
            }
            const bool passed = ServiceJudgeLdnStates(&seen, detail, sizeof(detail));
            Report(SERVICE_LDN_STATES, CHECK_EXACT, passed, detail);
        }
        LdnDone(&s);
    }
    if (Start(SERVICE_LDN_EVENT) && LdnMade(SERVICE_LDN_EVENT, creator, &s)) {
        ServiceLdnEvent seen;
        memset(&seen, 0, sizeof(seen));
        Handle handle = INVALID_HANDLE;
        seen.attach_rc = serviceDispatch(&s, 100, .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
                                         .out_handles = &handle, );
        Event event;
        memset(&event, 0, sizeof(event));
        const bool attached = R_SUCCEEDED(seen.attach_rc) && handle != INVALID_HANDLE;
        if (attached) {
            eventLoadRemote(&event, handle, true);
        }
        bool up = true;
        for (uint32_t i = 0; i + 1u < SERVICE_LDN_STEPS && up; ++i) {
            if (attached) {
                eventClear(&event);
            }
            seen.step_rc[i] = LdnStep(&s, i);
            up = i != 0u || LdnUp(SERVICE_LDN_EVENT, &s, seen.step_rc[0]);
            // The service signals before it answers: a twentieth of a second is only for the
            // scheduler to run the waiter.
            seen.signaled[i] = up && attached && R_SUCCEEDED(eventWait(&event, 50000000ull));
        }
        if (up) {
            const bool passed = ServiceJudgeLdnEvent(&seen, detail, sizeof(detail));
            Report(SERVICE_LDN_EVENT, CHECK_EXACT, passed, detail);
        }
        if (attached) {
            eventClose(&event);
        }
        LdnDone(&s);
    }
    if (Start(SERVICE_LDN_REFUSED) && LdnMade(SERVICE_LDN_REFUSED, creator, &s)) {
        ServiceLdnRefused seen;
        memset(&seen, 0, sizeof(seen));
        seen.asked[0] = serviceDispatch(&s, 200);
        seen.asked[1] = serviceDispatch(&s, 300);
        seen.asked[2] = serviceDispatch(&s, 201);
        seen.asked[3] = serviceDispatch(&s, 301);
        seen.asked[4] = LdnAskInfo(&s, &g_ldn_info[0]);
        seen.between[0] = LdnInitialize(&s);
        if (LdnUp(SERVICE_LDN_REFUSED, &s, seen.between[0])) {
            LdnSecurityParameter security;
            LdnNetworkConfig config;
            LdnNodeLatestUpdate updates[8];
            seen.asked[5] = LdnCreateNetwork(&s);
            seen.asked[6] = LdnAskInfo(&s, &g_ldn_info[0]);
            seen.asked[7] = serviceDispatchOut(&s, 4, security);
            seen.asked[8] = serviceDispatchOut(&s, 5, config);
            seen.asked[9] = LdnAskLatest(&s, &g_ldn_info[0], updates, 8);
            seen.between[1] = serviceDispatch(&s, 200);
            seen.asked[10] = LdnAskInfo(&s, &g_ldn_info[0]);
            seen.between[2] = serviceDispatch(&s, 201);
            seen.between[3] = serviceDispatch(&s, 300);
            seen.asked[11] = LdnCreateNetwork(&s);
            seen.asked[12] = LdnAskInfo(&s, &g_ldn_info[0]);
            serviceDispatch(&s, 301);
            const bool passed = ServiceJudgeLdnRefused(&seen, detail, sizeof(detail));
            Report(SERVICE_LDN_REFUSED, CHECK_EXACT, passed, detail);
        }
        LdnDone(&s);
    }
    uint32_t created = 0;
    if (Start(SERVICE_LDN_NETWORK) && LdnHost(SERVICE_LDN_NETWORK, creator, &s, &created)) {
        ServiceLdnNetwork seen;
        memset(&seen, 0, sizeof(seen));
        seen.create_rc = created;
        seen.info_rc = LdnAskInfo(&s, &g_ldn_info[0]);
        LdnKeepNetwork(&g_ldn_info[0], &seen);
        const bool passed = ServiceJudgeLdnNetwork(&seen, detail, sizeof(detail));
        Report(SERVICE_LDN_NETWORK, CHECK_EXACT, passed, detail);
        LdnDone(&s);
    }
    if (Start(SERVICE_LDN_UPDATES) && LdnHost(SERVICE_LDN_UPDATES, creator, &s, &created)) {
        ServiceLdnUpdates seen;
        memset(&seen, 0, sizeof(seen));
        static LdnNodeLatestUpdate updates[SERVICE_LDN_NODES + 1u];
        seen.create_rc = created;
        seen.info_rc = LdnAskInfo(&s, &g_ldn_info[0]);
        seen.nine_rc = LdnAskLatest(&s, &g_ldn_info[1], updates, SERVICE_LDN_NODES + 1u);
        seen.none_rc = LdnAskLatest(&s, &g_ldn_info[1], updates, 0);
        seen.first_rc = LdnAskLatest(&s, &g_ldn_info[1], updates, SERVICE_LDN_NODES);
        seen.same = memcmp(&g_ldn_info[0], &g_ldn_info[1], sizeof(g_ldn_info[0])) == 0;
        for (uint32_t i = 0; i < SERVICE_LDN_NODES; ++i) {
            seen.first[i] = updates[i].state_change;
        }
        seen.second_rc = LdnAskLatest(&s, &g_ldn_info[1], updates, SERVICE_LDN_NODES);
        for (uint32_t i = 0; i < SERVICE_LDN_NODES; ++i) {
            seen.second[i] = updates[i].state_change;
        }
        const bool passed = ServiceJudgeLdnUpdates(&seen, detail, sizeof(detail));
        Report(SERVICE_LDN_UPDATES, CHECK_EXACT, passed, detail);
        LdnDone(&s);
    }
    if (Start(SERVICE_LDN_CONFIG) && LdnHost(SERVICE_LDN_CONFIG, creator, &s, &created)) {
        ServiceLdnConfig seen;
        memset(&seen, 0, sizeof(seen));
        seen.create_rc = created;
        seen.info_rc = LdnAskInfo(&s, &g_ldn_info[0]);
        const LdnNetworkInfo *network = &g_ldn_info[0];
        seen.node_address = network->nodes[0].ip_addr.addr;
        memcpy(seen.network_random, network->server_random, sizeof(seen.network_random));
        memcpy(seen.network_session, network->network_id.session_id.random,
               sizeof(seen.network_session));
        seen.network_channel = network->common.channel;
        struct {
            u32 address, mask;
        } ip = { 0, 0 };
        seen.address_rc = serviceDispatchOut(&s, 2, ip);
        seen.address = ip.address;
        seen.mask = ip.mask;
        LdnSecurityParameter security;
        memset(&security, 0xEE, sizeof(security));
        seen.security_rc = serviceDispatchOut(&s, 4, security);
        memcpy(seen.random, security.server_random, sizeof(seen.random));
        memcpy(seen.session, security.session_id.random, sizeof(seen.session));
        LdnNetworkConfig config;
        memset(&config, 0xEE, sizeof(config));
        seen.config_rc = serviceDispatchOut(&s, 5, config);
        seen.local_id = (uint64_t)config.intent_id.local_communication_id;
        seen.scene = config.intent_id.scene_id;
        seen.channel = config.channel;
        seen.room = config.node_count_max;
        seen.version = config.local_communication_version;
        s16 reason = 0x7EEE;
        seen.reason_rc = serviceDispatchOut(&s, 3, reason);
        seen.reason = reason;
        const bool passed = ServiceJudgeLdnConfig(&seen, detail, sizeof(detail));
        Report(SERVICE_LDN_CONFIG, CHECK_EXACT, passed, detail);
        LdnDone(&s);
    }
    if (Start(SERVICE_LDN_ADVERTISE) && LdnHost(SERVICE_LDN_ADVERTISE, creator, &s, &created)) {
        ServiceLdnAdvertise seen;
        memset(&seen, 0, sizeof(seen));
        static u8 data[SERVICE_LDN_ADVERTISE_MAX + 1u];
        for (uint32_t i = 0; i < sizeof(data); ++i) {
            data[i] = (u8)(i * 7u + 3u);
        }
        const LdnNetworkInfo *network = &g_ldn_info[0];
        seen.create_rc = created;
        seen.full_rc = LdnAskAdvertise(&s, data, SERVICE_LDN_ADVERTISE_MAX);
        seen.full_info_rc = LdnAskInfo(&s, &g_ldn_info[0]);
        seen.full_size = network->advertise_data_size;
        seen.full_same = memcmp(network->advertise_data, data, SERVICE_LDN_ADVERTISE_MAX) == 0;
        seen.over_rc = LdnAskAdvertise(&s, data, SERVICE_LDN_ADVERTISE_MAX + 1u);
        seen.over_info_rc = LdnAskInfo(&s, &g_ldn_info[0]);
        seen.over_size = network->advertise_data_size;
        seen.over_same = memcmp(network->advertise_data, data, SERVICE_LDN_ADVERTISE_MAX) == 0;
        seen.empty_rc = LdnAskAdvertise(&s, NULL, 0);
        seen.empty_info_rc = LdnAskInfo(&s, &g_ldn_info[0]);
        seen.empty_size = network->advertise_data_size;
        const bool passed = ServiceJudgeLdnAdvertise(&seen, detail, sizeof(detail));
        Report(SERVICE_LDN_ADVERTISE, CHECK_EXACT, passed, detail);
        LdnDone(&s);
    }
    if (Start(SERVICE_LDN_STATION) && LdnMade(SERVICE_LDN_STATION, creator, &s)) {
        const Result up = LdnInitialize(&s);
        if (LdnUp(SERVICE_LDN_STATION, &s, up)) {
            ServiceLdnStation seen;
            memset(&seen, 0, sizeof(seen));
            seen.open_rc = R_FAILED(up) ? up : serviceDispatch(&s, 300);
            LdnScanFilter filter;
            memset(&filter, 0, sizeof(filter));
            filter.network_id.intent_id.local_communication_id = (s64)SERVICE_LDN_LOCAL_ID;
            filter.flags = LdnScanFilterFlag_LocalCommunicationId;
            static LdnNetworkInfo found[4];
            s16 count = -1, none = -1;
            const u64 begun = armGetSystemTick();
            seen.scan_rc = LdnAskScan(&s, &filter, found, 4, &count);
            seen.ms = (uint32_t)(armTicksToNs(armGetSystemTick() - begun) / 1000000u);
            seen.found = count;
            seen.empty_rc = LdnAskScan(&s, &filter, found, 0, &none);
            // A network with nothing in it: no node to join.
            memset(&g_ldn_info[0], 0, sizeof(g_ldn_info[0]));
            seen.connect_rc = LdnAskConnect(&s, &g_ldn_info[0]);
            seen.state_rc = LdnGetState(&s, &seen.state);
            serviceDispatch(&s, 301);
            const bool passed = ServiceJudgeLdnStation(&seen, detail, sizeof(detail));
            Report(SERVICE_LDN_STATION, CHECK_EXACT, passed, detail);
        }
        LdnDone(&s);
    }
}

/// ldn:u's monitor of the client process, which libnx's ldnInitialize asks for on 18.0.0 and
/// later, then registers this process with: CreateClientProcessMonitor (1) and the monitor's
/// RegisterClient (0, a word of nought and the process id), as libnx sends them; then ldn:u for
/// one console alone.
static void RunLdn(void) {
    char detail[480];
    Service creator;
    memset(&creator, 0, sizeof(creator));
    bool opens = false;
    if (Start(SERVICE_LDN_OPENS)) {
        const Result rc = smGetService(&creator, "ldn:u");
        opens = R_SUCCEEDED(rc);
        snprintf(detail, sizeof(detail), "smGetService(\"ldn:u\") returned 0x%x", (unsigned)rc);
        Report(SERVICE_LDN_OPENS, CHECK_NONE, opens, detail);
    }

    if (!opens) {
        Skip(SERVICE_LDN_MONITOR, REPORT_REASON_DEPENDS, "ldn:u did not open");
    } else if (Start(SERVICE_LDN_MONITOR)) {
        ServiceLdnMonitor seen;
        memset(&seen, 0, sizeof(seen));
        seen.firmware18 = hosversionAtLeast(18, 0, 0);
        Service monitor;
        memset(&monitor, 0, sizeof(monitor));
        seen.create_rc =
            serviceDispatch(&creator, 1, .out_num_objects = 1, .out_objects = &monitor, );
        seen.handle = R_SUCCEEDED(seen.create_rc) ? monitor.session : 0u;
        if (seen.handle != 0u) {
            const u64 reserved = 0;
            seen.register_rc = serviceDispatchIn(&monitor, 0, reserved, .in_send_pid = true, );
            seen.registered = true;
            serviceClose(&monitor);
        }
        const bool passed = ServiceJudgeLdnMonitor(&seen, detail, sizeof(detail));
        Report(SERVICE_LDN_MONITOR, CHECK_EXACT, passed, detail);
    }
    RunLdnLocal(opens ? &creator : NULL);
    if (opens) {
        serviceClose(&creator);
    }
}

// ---------------------------------------------------------------------------------------------
// set

/// set's language codes, asked as a game asks before it picks the language of its text: the
/// list (5, 4.0.0's), the console's own (0), each index made into a code (2), the count (6).
static void RunSet(void) {
    char detail[480];
    static ServiceLanguages seen;
    memset(&seen, 0, sizeof(seen));
    seen.listed = -1;
    seen.count = -1;
    Service set;
    memset(&set, 0, sizeof(set));
    bool opens = false;
    if (Start(SERVICE_SET_OPENS)) {
        Result rc = smGetService(&set, "set");
        const bool session = R_SUCCEEDED(rc);
        if (session) {
            rc = seen.list_rc = serviceDispatchOut(
                &set, 5, seen.listed,
                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                .buffers = { { seen.codes, sizeof(seen.codes) } }, );
        }
        opens = session && R_SUCCEEDED(rc) && seen.listed > 0;
        snprintf(detail, sizeof(detail),
                 "smGetService(\"set\") %s; GetAvailableLanguageCodes2 0x%x, %d listed",
                 session ? "opened" : "refused", (unsigned)rc, (int)seen.listed);
        Report(SERVICE_SET_OPENS, CHECK_NONE, opens, detail);
        if (session && !opens) {
            serviceClose(&set);
        }
    }
    if (!opens) {
        Skip(SERVICE_SET_LANGUAGE, REPORT_REASON_DEPENDS, "set listed no language codes");
        Skip(SERVICE_SET_MAKE, REPORT_REASON_DEPENDS, "set listed no language codes");
        Skip(SERVICE_SET_COUNT, REPORT_REASON_DEPENDS, "set listed no language codes");
        return;
    }
    if (Start(SERVICE_SET_LANGUAGE)) {
        seen.language_rc = serviceDispatchOut(&set, 0, seen.language);
        const bool passed = ServiceJudgeLanguage(&seen, detail, sizeof(detail));
        Report(SERVICE_SET_LANGUAGE, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_SET_MAKE)) {
        const int32_t listed =
            seen.listed > (int32_t)SERVICE_LANGUAGES_MAX ? (int32_t)SERVICE_LANGUAGES_MAX
                                                         : seen.listed;
        for (int32_t i = 0; i < listed; ++i) {
            const u32 index = (u32)i;
            seen.made_rc[i] = serviceDispatchInOut(&set, 2, index, seen.made[i]);
        }
        const u32 past = (u32)listed;
        u64 made_past = 0;
        seen.past_rc = serviceDispatchInOut(&set, 2, past, made_past);
        const bool passed = ServiceJudgeMakeLanguage(&seen, detail, sizeof(detail));
        Report(SERVICE_SET_MAKE, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_SET_COUNT)) {
        seen.count_rc = serviceDispatchOut(&set, 6, seen.count);
        const bool passed = ServiceJudgeLanguageCount(&seen, detail, sizeof(detail));
        Report(SERVICE_SET_COUNT, CHECK_EXACT, passed, detail);
    }
    serviceClose(&set);
}

// ---------------------------------------------------------------------------------------------
// csrng

/// The random number service a game seeds from: GenerateRandomBytes (0), twice.
static void RunCsrng(void) {
    char detail[480];
    bool opens = false;
    if (Start(SERVICE_CSRNG_OPENS)) {
        const Result rc = csrngInitialize();
        opens = R_SUCCEEDED(rc);
        snprintf(detail, sizeof(detail), "csrngInitialize returned 0x%x", (unsigned)rc);
        Report(SERVICE_CSRNG_OPENS, CHECK_NONE, opens, detail);
    }
    if (!opens) {
        Skip(SERVICE_RANDOM, REPORT_REASON_DEPENDS, "csrng did not open");
        return;
    }
    if (Start(SERVICE_RANDOM)) {
        static u8 first[SERVICE_RANDOM_SIZE];
        static u8 second[SERVICE_RANDOM_SIZE];
        memset(first, SERVICE_RANDOM_FILL, sizeof(first));
        memset(second, SERVICE_RANDOM_FILL, sizeof(second));
        const Result rc1 = csrngGetRandomBytes(first, sizeof(first));
        const Result rc2 = csrngGetRandomBytes(second, sizeof(second));
        const bool passed = ServiceJudgeRandom(rc1, rc2, first, second, detail, sizeof(detail));
        Report(SERVICE_RANDOM, CHECK_EXACT, passed, detail);
    }
    csrngExit();
}

// ---------------------------------------------------------------------------------------------
// apm

/// apm and a session on it, as libnx opens them before a game asks for the performance mode.
static void RunApm(void) {
    char detail[480];
    bool opens = false;
    if (Start(SERVICE_APM_OPENS)) {
        const Result rc = apmInitialize();
        opens = R_SUCCEEDED(rc);
        snprintf(detail, sizeof(detail), "apmInitialize (apm and OpenSession) returned 0x%x",
                 (unsigned)rc);
        Report(SERVICE_APM_OPENS, CHECK_NONE, opens, detail);
    }
    if (!opens) {
        Skip(SERVICE_APM_CONFIG, REPORT_REASON_DEPENDS, "apm handed out no session");
        Skip(SERVICE_APM_BOOST, REPORT_REASON_DEPENDS, "apm handed out no session");
        return;
    }
    if (Start(SERVICE_APM_CONFIG)) {
        // GetPerformanceConfiguration (1) for Normal and Boost, SetPerformanceConfiguration (0)
        // for Normal to a configuration it does not have, the two reads, and Normal put back.
        ServiceApmConfig seen;
        memset(&seen, 0, sizeof(seen));
        seen.before_rc = apmGetPerformanceConfiguration(ApmPerformanceMode_Normal,
                                                        &seen.normal_before);
        if (R_SUCCEEDED(seen.before_rc)) {
            seen.before_rc = apmGetPerformanceConfiguration(ApmPerformanceMode_Boost,
                                                            &seen.boost_before);
        }
        seen.asked = seen.normal_before == SERVICE_APM_CONFIG1 ? SERVICE_APM_CONFIG7
                                                               : SERVICE_APM_CONFIG1;
        seen.set_rc = apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, seen.asked);
        seen.after_rc = apmGetPerformanceConfiguration(ApmPerformanceMode_Normal,
                                                       &seen.normal_after);
        if (R_SUCCEEDED(seen.after_rc)) {
            seen.after_rc = apmGetPerformanceConfiguration(ApmPerformanceMode_Boost,
                                                           &seen.boost_after);
        }
        const bool known = seen.before_rc == 0u && seen.normal_before != 0u;
        seen.restore_rc = apmSetPerformanceConfiguration(
            ApmPerformanceMode_Normal, known ? seen.normal_before : SERVICE_APM_CONFIG7);
        const bool passed = ServiceJudgeApmConfig(&seen, detail, sizeof(detail));
        Report(SERVICE_APM_CONFIG, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_APM_BOOST)) {
        // The state getter's SetCpuBoostMode (66) to FastLoad, Boost's configuration read, then
        // back to Normal, as a game does around its loading screens, and read again.
        ServiceApmBoost seen;
        memset(&seen, 0, sizeof(seen));
        seen.fast_rc = appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
        seen.fast_read_rc = apmGetPerformanceConfiguration(ApmPerformanceMode_Boost, &seen.fast);
        seen.normal_rc = appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
        seen.normal_read_rc =
            apmGetPerformanceConfiguration(ApmPerformanceMode_Boost, &seen.normal);
        const bool passed = ServiceJudgeApmBoost(&seen, detail, sizeof(detail));
        Report(SERVICE_APM_BOOST, CHECK_EXACT, passed, detail);
    }
    apmExit();
}

// ---------------------------------------------------------------------------------------------
// nfp

/// nfp for a user, opened as libnx opens it, asked to start detecting on a device by the old
/// command (3) and by the one a game built for 23.0.0 sends (25). With no device ListDevices finds,
/// both are asked for a handle of nought, and both should refuse it alike.
static void RunNfp(void) {
    char detail[480];
    bool opens = false;
    if (Start(SERVICE_NFP_OPENS)) {
        const Result rc = nfpInitialize(NfpServiceType_User);
        opens = R_SUCCEEDED(rc);
        snprintf(detail, sizeof(detail), "nfpInitialize(User) returned 0x%x", (unsigned)rc);
        Report(SERVICE_NFP_OPENS, CHECK_NONE, opens, detail);
    }
    if (!opens) {
        Skip(SERVICE_NFP_FILTER, REPORT_REASON_DEPENDS, "nfp did not open");
        return;
    }
    if (Start(SERVICE_NFP_FILTER)) {
        NfcDeviceHandle handles[4];
        memset(handles, 0, sizeof(handles));
        s32 devices = 0;
        if (R_FAILED(nfpListDevices(&devices, handles, 4))) {
            devices = 0;
        }
        const NfcDeviceHandle handle = handles[0];
        const Result rc3 = nfpStartDetection(&handle);
        if (R_SUCCEEDED(rc3)) {
            nfpStopDetection(&handle);
        }
        // The device first, where reden's one handler reads it; the filter after it, nought.
        const struct {
            NfcDeviceHandle handle;
            u32 filter;
            u32 pad;
        } in = { handle, 0, 0 };
        const Result rc25 = serviceDispatchIn(nfpGetServiceSession_Interface(), 25, in);
        if (R_SUCCEEDED(rc25)) {
            nfpStopDetection(&handle);
        }
        const bool passed = ServiceJudgeNfpFilter(rc3, rc25, devices, hosversionAtLeast(23, 0, 0),
                                                  detail, sizeof(detail));
        Report(SERVICE_NFP_FILTER, CHECK_EXACT, passed, detail);
    }
    nfpExit();
}

// ---------------------------------------------------------------------------------------------
// aoc

/// One list of aoc:u's: ListAddOnContent (3) of up to @p count indices from @p offset into the
/// @p size bytes at @p indices, the process id sent with a word for it in the request, as
/// CountAddOnContent's.
static Result AocListInto(Service *aoc, uint32_t offset, uint32_t count, s32 *indices,
                          size_t size, uint32_t *listed) {
    const struct {
        s32 offset, count;
        u64 pid;
    } in = { (s32)offset, (s32)count, 0 };
    u32 out = 0;
    const Result rc =
        serviceDispatchInOut(aoc, 3, in, out, .in_send_pid = true,
                             .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                             .buffers = { { indices, size } }, );
    *listed = out;
    return rc;
}

/// A list from @p offset into room for SERVICE_AOC_LISTED indices, all of them asked for.
static Result AocList(Service *aoc, uint32_t offset, uint32_t *listed) {
    static s32 indices[SERVICE_AOC_LISTED];
    return AocListInto(aoc, offset, SERVICE_AOC_LISTED, indices, sizeof(indices), listed);
}

/// aoc:u, asked for this program's add-on content as a game asks before it mounts one:
/// CountAddOnContent (2), then ListAddOnContent (3) from the count, the list's end, and from one
/// past it; from 0 into room for all of them, for one into room for one, and for none;
/// GetAddOnContentBaseId (5) beside the program's id; and PrepareAddOnContent (7) of an index it
/// does not have. The program has no add-on content.
static void RunAoc(void) {
    char detail[480];
    Service aoc;
    memset(&aoc, 0, sizeof(aoc));
    u32 count = 0;
    bool opens = false;
    if (Start(SERVICE_AOC_OPENS)) {
        Result rc = smGetService(&aoc, "aoc:u");
        const bool session = R_SUCCEEDED(rc);
        if (session) {
            const u64 pid = 0;
            rc = serviceDispatchInOut(&aoc, 2, pid, count, .in_send_pid = true, );
        }
        opens = session && R_SUCCEEDED(rc);
        snprintf(detail, sizeof(detail),
                 "smGetService(\"aoc:u\") %s; CountAddOnContent 0x%x, %u add-on contents",
                 session ? "opened" : "refused", (unsigned)rc, (unsigned)count);
        Report(SERVICE_AOC_OPENS, CHECK_NONE, opens, detail);
        if (session && !opens) {
            serviceClose(&aoc);
        }
    }
    if (!opens) {
        Skip(SERVICE_AOC_PAST, REPORT_REASON_DEPENDS, "aoc:u did not count its add-on content");
        Skip(SERVICE_AOC_NONE, REPORT_REASON_DEPENDS, "aoc:u did not count its add-on content");
        Skip(SERVICE_AOC_ROOM, REPORT_REASON_DEPENDS, "aoc:u did not count its add-on content");
        Skip(SERVICE_AOC_BASE, REPORT_REASON_DEPENDS, "aoc:u did not count its add-on content");
        Skip(SERVICE_AOC_PREPARE, REPORT_REASON_DEPENDS, "aoc:u did not count its add-on content");
        return;
    }
    if (Start(SERVICE_AOC_PAST)) {
        ServiceAocList seen;
        memset(&seen, 0, sizeof(seen));
        seen.count = count;
        seen.end_rc = AocList(&aoc, count, &seen.end_listed);
        uint32_t past_listed = 0;
        seen.past_rc = AocList(&aoc, count + 1u, &past_listed);
        const bool passed = ServiceJudgeAocPast(&seen, detail, sizeof(detail));
        Report(SERVICE_AOC_PAST, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_AOC_NONE)) {
        ServiceAocNone seen;
        memset(&seen, 0, sizeof(seen));
        seen.count = count;
        seen.list_rc = AocList(&aoc, 0u, &seen.listed);
        const bool passed = ServiceJudgeAocNone(&seen, detail, sizeof(detail));
        Report(SERVICE_AOC_NONE, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_AOC_ROOM)) {
        static s32 room[SERVICE_AOC_LISTED];
        for (uint32_t i = 0; i < SERVICE_AOC_LISTED; ++i) {
            room[i] = (s32)SERVICE_AOC_UNTOUCHED;
        }
        ServiceAocRoom seen;
        memset(&seen, 0, sizeof(seen));
        // One asked for, into room for one: the rest of the array is past the room.
        seen.one_rc = AocListInto(&aoc, 0u, 1u, room, sizeof(room[0]), &seen.one_listed);
        seen.past_kept = true;
        for (uint32_t i = 1; i < SERVICE_AOC_LISTED; ++i) {
            seen.past_kept = seen.past_kept && room[i] == (s32)SERVICE_AOC_UNTOUCHED;
        }
        seen.none_rc = AocListInto(&aoc, 0u, 0u, room, sizeof(room), &seen.none_listed);
        const bool passed = ServiceJudgeAocRoom(&seen, detail, sizeof(detail));
        Report(SERVICE_AOC_ROOM, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_AOC_BASE)) {
        u64 program_id = 0;
        const uint32_t info_rc =
            svcGetInfo(&program_id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
        const u64 pid = 0;
        u64 base_id = 0;
        const uint32_t rc = serviceDispatchInOut(&aoc, 5, pid, base_id, .in_send_pid = true, );
        const bool passed =
            ServiceJudgeAocBase(info_rc, program_id, rc, base_id, detail, sizeof(detail));
        Report(SERVICE_AOC_BASE, CHECK_EXACT, passed, detail);
    }
    if (count != 0u) {
        Skip(SERVICE_AOC_PREPARE, REPORT_REASON_DEPENDS,
             "%u add-on contents: index %u may be one of them", (unsigned)count,
             (unsigned)SERVICE_AOC_MISSING);
    } else if (Start(SERVICE_AOC_PREPARE)) {
        const struct {
            s32 index;
            u32 pad;
            u64 pid;
        } in = { (s32)SERVICE_AOC_MISSING, 0u, 0 };
        const uint32_t rc = serviceDispatchIn(&aoc, 7, in, .in_send_pid = true, );
        const bool passed = ServiceJudgeAocPrepare(rc, detail, sizeof(detail));
        Report(SERVICE_AOC_PREPARE, CHECK_EXACT, passed, detail);
    }
    serviceClose(&aoc);
}

// ---------------------------------------------------------------------------------------------
// time

/// One steady clock reading, GetCurrentTimePoint (0).
static Result SteadyPoint(Service *steady, ServiceTimePoint *point) {
    TimeSteadyClockTimePoint raw;
    memset(&raw, 0, sizeof(raw));
    const Result rc = serviceDispatchOut(steady, 0, raw);
    point->seconds = raw.time_point;
    memcpy(point->source, &raw.source_id, sizeof(point->source));
    return rc;
}

/// LoadTimeZoneRule (4) for @p name into @p rule.
static Result LoadRule(Service *zones, const TimeLocationName *name, TimeZoneRule *rule) {
    return serviceDispatchIn(zones, 4, *name,
                             .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                             .buffers = { { rule, sizeof(*rule) } }, );
}

/// ToCalendarTime (100) of @p time under @p rule.
static Result DateUnder(Service *zones, const TimeZoneRule *rule, int64_t time,
                        ServiceCalendarTime *date, ServiceCalendarInfo *info) {
    struct {
        ServiceCalendarTime date;
        ServiceCalendarInfo info;
    } out;
    memset(&out, 0, sizeof(out));
    const Result rc = serviceDispatchInOut(zones, 100, time, out,
                                           .buffer_attrs = { SfBufferAttr_HipcMapAlias |
                                                             SfBufferAttr_In },
                                           .buffers = { { rule, sizeof(*rule) } }, );
    *date = out.date;
    *info = out.info;
    return rc;
}

/// time:u of this suite's own, and the clocks a game reads at start: the user clock, the steady
/// clock and the time zone service. The user clock's time against its context and the steady
/// clock, the steady clock across a second and a half, and dates under the UTC rule and the
/// device's own.
static void RunTime(void) {
    char detail[480];
    Service timesrv, user, steady, zones;
    memset(&timesrv, 0, sizeof(timesrv));
    memset(&user, 0, sizeof(user));
    memset(&steady, 0, sizeof(steady));
    memset(&zones, 0, sizeof(zones));
    bool opens = false;
    if (Start(SERVICE_TIME_OPENS)) {
        Result rc = smGetService(&timesrv, "time:u");
        const bool session = R_SUCCEEDED(rc);
        Result user_rc = 0, steady_rc = 0, zones_rc = 0;
        if (session) {
            user_rc = serviceDispatch(&timesrv, 0, .out_num_objects = 1, .out_objects = &user, );
            steady_rc =
                serviceDispatch(&timesrv, 2, .out_num_objects = 1, .out_objects = &steady, );
            zones_rc = serviceDispatch(&timesrv, 3, .out_num_objects = 1, .out_objects = &zones, );
        }
        opens = session && R_SUCCEEDED(user_rc) && R_SUCCEEDED(steady_rc) &&
                R_SUCCEEDED(zones_rc) && serviceIsActive(&user) && serviceIsActive(&steady) &&
                serviceIsActive(&zones);
        snprintf(detail, sizeof(detail),
                 "smGetService(\"time:u\") 0x%x; GetStandardUserSystemClock 0x%x, "
                 "GetStandardSteadyClock 0x%x, GetTimeZoneService 0x%x",
                 (unsigned)rc, (unsigned)user_rc, (unsigned)steady_rc, (unsigned)zones_rc);
        Report(SERVICE_TIME_OPENS, CHECK_NONE, opens, detail);
    }
    if (!opens) {
        Skip(SERVICE_TIME_USER, REPORT_REASON_DEPENDS, "time:u handed out no clocks");
        Skip(SERVICE_TIME_STEADY, REPORT_REASON_DEPENDS, "time:u handed out no clocks");
        Skip(SERVICE_TIME_UTC, REPORT_REASON_DEPENDS, "time:u handed out no clocks");
        Skip(SERVICE_TIME_POSIX, REPORT_REASON_DEPENDS, "time:u handed out no clocks");
        Skip(SERVICE_TIME_MY_RULE, REPORT_REASON_DEPENDS, "time:u handed out no clocks");
    } else {
        if (Start(SERVICE_TIME_USER)) {
            ServiceUserClock seen;
            memset(&seen, 0, sizeof(seen));
            TimeSystemClockContext context;
            memset(&context, 0, sizeof(context));
            seen.context_rc = serviceDispatchOut(&user, 2, context);
            seen.offset = context.offset;
            seen.context.seconds = context.timestamp.time_point;
            memcpy(seen.context.source, &context.timestamp.source_id, 16);
            seen.steady_rc = SteadyPoint(&steady, &seen.steady);
            seen.time_rc = serviceDispatchOut(&user, 0, seen.time);
            const bool passed = ServiceJudgeUserClock(&seen, detail, sizeof(detail));
            Report(SERVICE_TIME_USER, CHECK_EXACT, passed, detail);
        }
        if (Start(SERVICE_TIME_STEADY)) {
            ServiceTimePoint first, second;
            memset(&first, 0, sizeof(first));
            memset(&second, 0, sizeof(second));
            const uint32_t first_rc = SteadyPoint(&steady, &first);
            svcSleepThread((s64)SERVICE_TIME_WAIT_NS);
            const uint32_t second_rc = SteadyPoint(&steady, &second);
            const bool passed = ServiceJudgeSteadyClock(first_rc, &first, second_rc, &second,
                                                        detail, sizeof(detail));
            Report(SERVICE_TIME_STEADY, CHECK_EXACT, passed, detail);
        }
        static TimeZoneRule rule;
        TimeLocationName utc;
        memset(&utc, 0, sizeof(utc));
        strcpy(utc.name, "UTC");
        if (Start(SERVICE_TIME_UTC)) {
            ServiceUtcDates seen;
            memset(&seen, 0, sizeof(seen));
            seen.load_rc = LoadRule(&zones, &utc, &rule);
            seen.rc[0] = DateUnder(&zones, &rule, 0, &seen.date[0], &seen.info[0]);
            seen.rc[1] = DateUnder(&zones, &rule, SERVICE_TIME_KNOWN, &seen.date[1], &seen.info[1]);
            const bool passed = ServiceJudgeUtcDates(&seen, detail, sizeof(detail));
            Report(SERVICE_TIME_UTC, CHECK_EXACT, passed, detail);
        }
        if (Start(SERVICE_TIME_POSIX)) {
            const uint32_t utc_rc = LoadRule(&zones, &utc, &rule);
            const ServiceCalendarTime date = { 2023, 11, 14, 22, 13, 20, 0 };
            s64 times[4] = { 0, 0, 0, 0 };
            s32 count = -1;
            const uint32_t rc = serviceDispatchInOut(
                &zones, 201, date, count,
                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In,
                                  SfBufferAttr_HipcPointer | SfBufferAttr_Out },
                .buffers = { { &rule, sizeof(rule) }, { times, sizeof(times) } }, );
            const bool passed =
                ServiceJudgeUtcPosix(utc_rc, rc, count, times[0], detail, sizeof(detail));
            Report(SERVICE_TIME_POSIX, CHECK_EXACT, passed, detail);
        }
        if (Start(SERVICE_TIME_MY_RULE)) {
            ServiceMyRule seen;
            memset(&seen, 0, sizeof(seen));
            TimeLocationName name;
            memset(&name, 0, sizeof(name));
            seen.name_rc = serviceDispatchOut(&zones, 0, name);
            memcpy(seen.name, name.name, sizeof(seen.name));
            seen.load_rc = LoadRule(&zones, &name, &rule);
            seen.time_rc = serviceDispatchOut(&user, 0, seen.time);
            struct {
                ServiceCalendarTime date;
                ServiceCalendarInfo info;
            } mine;
            memset(&mine, 0, sizeof(mine));
            seen.mine_rc = serviceDispatchInOut(&zones, 101, seen.time, mine);
            seen.mine_date = mine.date;
            seen.mine_info = mine.info;
            seen.loaded_rc =
                DateUnder(&zones, &rule, seen.time, &seen.loaded_date, &seen.loaded_info);
            const bool passed = ServiceJudgeMyRule(&seen, detail, sizeof(detail));
            Report(SERVICE_TIME_MY_RULE, CHECK_EXACT, passed, detail);
        }
    }
    serviceClose(&zones);
    serviceClose(&steady);
    serviceClose(&user);
    serviceClose(&timesrv);
}

// ---------------------------------------------------------------------------------------------
// pl

/// pl:u of this suite's own, its shared memory mapped as libnx's plInitialize maps it, and the six
/// shared fonts asked for one by one, as a game's text library asks before it draws, then in the
/// order of priority for the console's language.
static void RunPl(void) {
    char detail[480];
    Service pl;
    memset(&pl, 0, sizeof(pl));
    SharedMemory memory;
    memset(&memory, 0, sizeof(memory));
    const uint8_t *base = NULL;
    bool handed = false;
    bool opens = false;
    if (Start(SERVICE_PL_OPENS)) {
        Result rc = smGetService(&pl, "pl:u");
        const bool session = R_SUCCEEDED(rc);
        Handle handle = 0;
        Result handle_rc = 0, map_rc = 0;
        if (session) {
            handle_rc = serviceDispatch(&pl, 4, .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
                                        .out_handles = &handle, );
        }
        if (session && R_SUCCEEDED(handle_rc)) {
            handed = true;
            shmemLoadRemote(&memory, handle, SERVICE_FONT_MEMORY, Perm_R);
            map_rc = shmemMap(&memory);
            base = R_SUCCEEDED(map_rc) ? (const uint8_t *)shmemGetAddr(&memory) : NULL;
        }
        opens = base != NULL;
        snprintf(detail, sizeof(detail),
                 "smGetService(\"pl:u\") 0x%x; GetSharedMemoryNativeHandle 0x%x, handle %x; "
                 "mapped 0x%x",
                 (unsigned)rc, (unsigned)handle_rc, (unsigned)handle, (unsigned)map_rc);
        Report(SERVICE_PL_OPENS, CHECK_NONE, opens, detail);
    }
    if (!opens) {
        Skip(SERVICE_PL_FONTS, REPORT_REASON_DEPENDS, "pl:u's shared memory is not mapped");
        Skip(SERVICE_PL_ORDER, REPORT_REASON_DEPENDS, "pl:u's shared memory is not mapped");
    } else {
        static ServiceFont fonts[SERVICE_FONTS];
        memset(fonts, 0, sizeof(fonts));
        bool told = false;
        if (Start(SERVICE_PL_FONTS)) {
            for (u32 type = 0; type < SERVICE_FONTS; ++type) {
                ServiceFont *f = &fonts[type];
                f->request_rc = serviceDispatchIn(&pl, 0, type);
                f->state_rc = serviceDispatchInOut(&pl, 1, type, f->state);
                f->size_rc = serviceDispatchInOut(&pl, 2, type, f->size);
                f->offset_rc = serviceDispatchInOut(&pl, 3, type, f->offset);
                if (f->offset >= 8u && f->offset <= SERVICE_FONT_MEMORY - 4u) {
                    memcpy(f->header, base + f->offset - 8u, sizeof(f->header));
                    memcpy(f->start, base + f->offset, sizeof(f->start));
                }
            }
            told = true;
            const bool passed = ServiceJudgeFonts(fonts, detail, sizeof(detail));
            Report(SERVICE_PL_FONTS, CHECK_EXACT, passed, detail);
        }
        if (!told) {
            Skip(SERVICE_PL_ORDER, REPORT_REASON_DEPENDS, "the fonts were not asked one by one");
        } else if (Start(SERVICE_PL_ORDER)) {
            ServiceFontOrder seen;
            memset(&seen, 0, sizeof(seen));
            Service set;
            memset(&set, 0, sizeof(set));
            seen.language_rc = smGetService(&set, "set");
            if (R_SUCCEEDED(seen.language_rc)) {
                seen.language_rc = serviceDispatchOut(&set, 0, seen.language);
                serviceClose(&set);
            }
            struct {
                u8 loaded;
                u32 count;
            } said = { 0, 0 };
            seen.rc = serviceDispatchInOut(
                &pl, 5, seen.language, said,
                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out,
                                  SfBufferAttr_HipcMapAlias | SfBufferAttr_Out,
                                  SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                .buffers = { { seen.types, sizeof(seen.types) },
                             { seen.offsets, sizeof(seen.offsets) },
                             { seen.sizes, sizeof(seen.sizes) } }, );
            seen.loaded = said.loaded;
            seen.count = said.count;
            const bool passed = ServiceJudgeFontOrder(&seen, fonts, detail, sizeof(detail));
            Report(SERVICE_PL_ORDER, CHECK_EXACT, passed, detail);
        }
    }
    if (handed) {
        shmemClose(&memory);
    }
    serviceClose(&pl);
}

// ---------------------------------------------------------------------------------------------
// profile

/// The profile of the user a game shows in its menus, from acc:u0's GetProfile (5): its base twice,
/// its picture, and whether the user exists.
static void RunProfile(void) {
    char detail[480];
    Account account;
    memset(&account, 0, sizeof(account));
    AccountUid uid;
    memset(&uid, 0, sizeof(uid));
    Service profile;
    memset(&profile, 0, sizeof(profile));
    bool opens = false;
    if (Start(SERVICE_PROFILE_OPENS)) {
        char said[240];
        const char *how = "";
        Result rc = AccountOpen(&account, said, sizeof(said));
        Result user_rc = 0, profile_rc = 0;
        if (R_SUCCEEDED(rc)) {
            user_rc = AccountUser(&account, &uid, &how);
        }
        if (R_SUCCEEDED(rc) && accountUidIsValid(&uid)) {
            profile_rc = serviceDispatchIn(account.service, 5, uid, .out_num_objects = 1,
                                           .out_objects = &profile, );
        }
        opens = R_SUCCEEDED(rc) && accountUidIsValid(&uid) && R_SUCCEEDED(profile_rc) &&
                serviceIsActive(&profile);
        snprintf(detail, sizeof(detail), "%s; %s 0x%x, user %016llx%016llx; GetProfile 0x%x",
                 said, how, (unsigned)user_rc, (unsigned long long)uid.uid[0],
                 (unsigned long long)uid.uid[1], (unsigned)profile_rc);
        Report(SERVICE_PROFILE_OPENS, CHECK_NONE, opens, detail);
    }
    if (!opens) {
        Skip(SERVICE_PROFILE_BASE, REPORT_REASON_DEPENDS, "no profile to ask");
        Skip(SERVICE_PROFILE_IMAGE, REPORT_REASON_DEPENDS, "no profile to ask");
        Skip(SERVICE_PROFILE_EXISTS, REPORT_REASON_DEPENDS, "no user to ask about");
    } else {
        if (Start(SERVICE_PROFILE_BASE)) {
            static u8 user_data[0x80];
            ServiceProfileBase get, base;
            memset(&get, 0, sizeof(get));
            memset(&base, 0, sizeof(base));
            const uint32_t get_rc = serviceDispatchOut(
                &profile, 0, get,
                .buffer_attrs = { SfBufferAttr_FixedSize | SfBufferAttr_HipcPointer |
                                  SfBufferAttr_Out },
                .buffers = { { user_data, sizeof(user_data) } }, );
            const uint32_t base_rc = serviceDispatchOut(&profile, 1, base);
            const bool passed = ServiceJudgeProfileBase(uid.uid, get_rc, &get, base_rc, &base,
                                                        detail, sizeof(detail));
            Report(SERVICE_PROFILE_BASE, CHECK_EXACT, passed, detail);
        }
        if (Start(SERVICE_PROFILE_IMAGE)) {
            static u8 picture[SERVICE_PICTURE_MAX];
            memset(picture, 0, sizeof(picture));
            u32 size = 0, loaded = 0;
            const uint32_t size_rc = serviceDispatchOut(&profile, 10, size);
            const uint32_t load_rc = serviceDispatchOut(
                &profile, 11, loaded,
                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                .buffers = { { picture, sizeof(picture) } }, );
            const bool passed = ServiceJudgeProfileImage(size_rc, size, load_rc, loaded, picture,
                                                         detail, sizeof(detail));
            Report(SERVICE_PROFILE_IMAGE, CHECK_EXACT, passed, detail);
        }
        if (Start(SERVICE_PROFILE_EXISTS)) {
            // The user's id with both halves turned over: nobody's.
            const AccountUid made_up = { { ~uid.uid[0], ~uid.uid[1] } };
            u8 user = 0, nobody = 0;
            const uint32_t user_rc = serviceDispatchInOut(account.service, 1, uid, user);
            const uint32_t made_up_rc = serviceDispatchInOut(account.service, 1, made_up, nobody);
            const bool passed = ServiceJudgeUserExistence(user_rc, user != 0u, made_up_rc,
                                                          nobody != 0u, detail, sizeof(detail));
            Report(SERVICE_PROFILE_EXISTS, CHECK_EXACT, passed, detail);
        }
    }
    serviceClose(&profile);
    AccountClose(&account);
}

// ---------------------------------------------------------------------------------------------
// am-state

/// The application's common state getter, as libnx opened it at start, asked what a game asks
/// before its first frame: the operation mode and the resolution, the focus, the start-up logo's
/// message, 3D VR mode and the platform's region.
static void RunAmState(void) {
    char detail[480];
    Service *const getter = appletGetServiceSession_CommonStateGetter();
    if (getter == NULL || !serviceIsActive(getter)) {
        const char *const why = "this program has no ICommonStateGetter";
        Skip(SERVICE_STATE_MODE, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_STATE_FOCUS, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_STATE_LOGO, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_STATE_VR, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_STATE_REGION, REPORT_REASON_UNAVAILABLE, "%s", why);
        return;
    }
    if (Start(SERVICE_STATE_MODE)) {
        u8 mode = 0xEE;
        struct {
            s32 width, height;
        } resolution = { -1, -1 };
        const uint32_t mode_rc = serviceDispatchOut(getter, 5, mode);
        const uint32_t resolution_rc = serviceDispatchOut(getter, 60, resolution);
        const bool passed = ServiceJudgeOperationMode(mode_rc, mode, resolution_rc,
                                                      resolution.width, resolution.height, detail,
                                                      sizeof(detail));
        Report(SERVICE_STATE_MODE, CHECK_EXACT, passed, detail);
    }
    if (appletGetAppletType() != AppletType_Application) {
        Skip(SERVICE_STATE_FOCUS, REPORT_REASON_UNAVAILABLE,
             "needs to run as an application, which has the focus; this program runs as applet "
             "type %d",
             (int)appletGetAppletType());
    } else if (Start(SERVICE_STATE_FOCUS)) {
        u8 state = 0xEE;
        const uint32_t rc = serviceDispatchOut(getter, 9, state);
        const bool passed = ServiceJudgeFocus(rc, state, detail, sizeof(detail));
        Report(SERVICE_STATE_FOCUS, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_STATE_LOGO)) {
        ServiceLogoMessage seen;
        memset(&seen, 0, sizeof(seen));
        seen.firmware21 = hosversionAtLeast(21, 0, 0);
        seen.enable_rc = serviceDispatch(getter, 130);
        while (seen.count < SERVICE_AM_MESSAGES) {
            u32 message = 0;
            seen.end_rc = serviceDispatchOut(getter, 1, message);
            if (R_FAILED(seen.end_rc)) {
                break;
            }
            seen.messages[seen.count++] = message;
        }
        const bool passed = ServiceJudgeLogoMessage(&seen, detail, sizeof(detail));
        Report(SERVICE_STATE_LOGO, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_STATE_VR)) {
        ServiceVrMode seen;
        memset(&seen, 0, sizeof(seen));
        seen.firmware19 = hosversionAtLeast(19, 0, 0);
        seen.on = seen.off = 0xEE;
        seen.begin_rc = serviceDispatch(getter, 1000);
        seen.on_rc = serviceDispatchOut(getter, 1002, seen.on);
        seen.end_rc = serviceDispatch(getter, 1001);
        seen.off_rc = serviceDispatchOut(getter, 1002, seen.off);
        const bool passed = ServiceJudgeVrMode(&seen, detail, sizeof(detail));
        Report(SERVICE_STATE_VR, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_STATE_REGION)) {
        u32 region = 0xEEEEEEEEu;
        const uint32_t rc = serviceDispatchOut(getter, 300, region);
        const bool passed = ServiceJudgePlatformRegion(rc, region, hosversionAtLeast(9, 0, 0),
                                                       detail, sizeof(detail));
        Report(SERVICE_STATE_REGION, CHECK_EXACT, passed, detail);
    }
}

// ---------------------------------------------------------------------------------------------
// am-self

/// The application's self controller, as libnx opened it at start: two settings a game makes
/// while it plays, each read, set to something else, read back and put back as it was.
static void RunAmSelf(void) {
    char detail[480];
    Service *const self = appletGetServiceSession_SelfController();
    if (self == NULL || !serviceIsActive(self)) {
        Skip(SERVICE_SELF_IDLE, REPORT_REASON_UNAVAILABLE, "this program has no ISelfController");
        Skip(SERVICE_SELF_SLEEP, REPORT_REASON_UNAVAILABLE, "this program has no ISelfController");
        return;
    }
    if (Start(SERVICE_SELF_IDLE)) {
        // GetIdleTimeDetectionExtension (63) and SetIdleTimeDetectionExtension (62): None (0) and
        // Extended (1), whichever it is not.
        ServiceSelfSetting seen;
        memset(&seen, 0, sizeof(seen));
        u32 before = 0xEEEEEEEEu, after = 0xEEEEEEEEu;
        seen.before_rc = serviceDispatchOut(self, 63, before);
        seen.before = before;
        const u32 asked = before == 1u ? 0u : 1u;
        seen.asked = asked;
        seen.set_rc = serviceDispatchIn(self, 62, asked);
        seen.after_rc = serviceDispatchOut(self, 63, after);
        seen.after = after;
        const u32 back = before <= 2u ? before : 0u;
        seen.restore_rc = serviceDispatchIn(self, 62, back);
        const bool passed = ServiceJudgeSelfSetting("GetIdleTimeDetectionExtension", &seen,
                                                    detail, sizeof(detail));
        Report(SERVICE_SELF_IDLE, CHECK_EXACT, passed, detail);
    }
    if (!hosversionAtLeast(5, 0, 0)) {
        Skip(SERVICE_SELF_SLEEP, REPORT_REASON_UNAVAILABLE,
             "SetAutoSleepDisabled and IsAutoSleepDisabled came with 5.0.0; this machine says it "
             "is older");
    } else if (Start(SERVICE_SELF_SLEEP)) {
        // IsAutoSleepDisabled (69) and SetAutoSleepDisabled (68), to the other one.
        ServiceSelfSetting seen;
        memset(&seen, 0, sizeof(seen));
        u8 before = 0xEE, after = 0xEE;
        seen.before_rc = serviceDispatchOut(self, 69, before);
        seen.before = before;
        const u8 asked = before == 0u ? 1u : 0u;
        seen.asked = asked;
        seen.set_rc = serviceDispatchIn(self, 68, asked);
        seen.after_rc = serviceDispatchOut(self, 69, after);
        seen.after = after;
        const u8 back = before == 1u ? 1u : 0u;
        seen.restore_rc = serviceDispatchIn(self, 68, back);
        const bool passed =
            ServiceJudgeSelfSetting("IsAutoSleepDisabled", &seen, detail, sizeof(detail));
        Report(SERVICE_SELF_SLEEP, CHECK_EXACT, passed, detail);
    }
}

// ---------------------------------------------------------------------------------------------
// am-functions

/// The application's own functions, as libnx opened them at start, asked what a game asks before
/// its title screen: the language to show, the events it waits on, and the two channels a launch
/// may have left something in.
static void RunAmFunctions(void) {
    char detail[480];
    Service *const functions = appletGetServiceSession_Functions();
    if (appletGetAppletType() != AppletType_Application || functions == NULL ||
        !serviceIsActive(functions)) {
        char why[200];
        snprintf(why, sizeof(why),
                 "needs to run as an application, with the application's functions; this "
                 "program runs as applet type %d",
                 (int)appletGetAppletType());
        Skip(SERVICE_FUNC_LANGUAGE, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_FUNC_EVENTS, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_FUNC_CHANNELS, REPORT_REASON_UNAVAILABLE, "%s", why);
        return;
    }
    if (Start(SERVICE_FUNC_LANGUAGE)) {
        static u64 codes[SERVICE_LANGUAGES_MAX];
        memset(codes, 0, sizeof(codes));
        u64 code = 0;
        s32 listed = -1;
        const uint32_t rc = serviceDispatchOut(functions, 21, code);
        Service set;
        memset(&set, 0, sizeof(set));
        uint32_t list_rc = smGetService(&set, "set");
        if (R_SUCCEEDED(list_rc)) {
            list_rc = serviceDispatchOut(
                &set, 5, listed, .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                .buffers = { { codes, sizeof(codes) } }, );
            serviceClose(&set);
        }
        const bool passed =
            ServiceJudgeDesiredLanguage(rc, code, list_rc, listed, codes, detail, sizeof(detail));
        Report(SERVICE_FUNC_LANGUAGE, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_FUNC_EVENTS)) {
        static const u32 COMMANDS[SERVICE_START_EVENTS] = { 130, 140, 150, 160 };
        static const u32 SINCE[SERVICE_START_EVENTS] = { 8, 9, 9, 9 };
        ServiceStartEvent events[SERVICE_START_EVENTS];
        memset(events, 0, sizeof(events));
        for (u32 i = 0; i < SERVICE_START_EVENTS; ++i) {
            events[i].asked = hosversionAtLeast(SINCE[i], 0, 0);
            if (events[i].asked) {
                Handle handle = 0;
                events[i].rc = serviceDispatch(functions, COMMANDS[i],
                                               .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
                                               .out_handles = &handle, );
                events[i].handle = handle;
                if (handle != 0u) {
                    svcCloseHandle(handle);
                }
            }
        }
        const bool passed = ServiceJudgeStartEvents(events, detail, sizeof(detail));
        Report(SERVICE_FUNC_EVENTS, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_FUNC_CHANNELS)) {
        const bool friend_asked = hosversionAtLeast(9, 0, 0);
        uint32_t friend_rc = 0;
        Service storage;
        memset(&storage, 0, sizeof(storage));
        bool friend_storage = false;
        if (friend_asked) {
            friend_rc = serviceDispatch(functions, 141, .out_num_objects = 1,
                                        .out_objects = &storage, );
            friend_storage = R_SUCCEEDED(friend_rc) && serviceIsActive(&storage);
            if (friend_storage) {
                serviceClose(&storage);
            }
        }
        const u32 kind = 1; // AppletLaunchParameterKind_UserChannel
        memset(&storage, 0, sizeof(storage));
        const uint32_t user_rc = serviceDispatchIn(functions, 1, kind, .out_num_objects = 1,
                                                   .out_objects = &storage, );
        const bool user_storage = R_SUCCEEDED(user_rc) && serviceIsActive(&storage);
        if (user_storage) {
            serviceClose(&storage);
        }
        const bool passed = ServiceJudgeEmptyChannels(friend_asked, friend_rc, friend_storage,
                                                      user_rc, user_storage, detail,
                                                      sizeof(detail));
        Report(SERVICE_FUNC_CHANNELS, CHECK_EXACT, passed, detail);
    }
}

// ---------------------------------------------------------------------------------------------
// auddev

/// The audio device service a game with a renderer asks which output plays and how loud, opened
/// with libnx's auddevInitialize (audren:u's GetAudioDeviceService, 2, with this program's
/// applet resource user id).
static void RunAuddev(void) {
    char detail[480];
    bool opens = false;
    if (Start(SERVICE_AUDDEV_OPENS)) {
        const Result rc = auddevInitialize();
        opens = R_SUCCEEDED(rc);
        snprintf(detail, sizeof(detail), "auddevInitialize returned 0x%x", (unsigned)rc);
        Report(SERVICE_AUDDEV_OPENS, CHECK_NONE, opens, detail);
    }
    if (!opens) {
        Skip(SERVICE_AUDDEV_ACTIVE, REPORT_REASON_DEPENDS, "the audio device service did not open");
        Skip(SERVICE_AUDDEV_VOLUME, REPORT_REASON_DEPENDS, "the audio device service did not open");
        return;
    }
    static ServiceAudioDevices seen;
    memset(&seen, 0, sizeof(seen));
    bool active = false;
    if (Start(SERVICE_AUDDEV_ACTIVE)) {
        static AudioDeviceName names[SERVICE_AUDIO_DEVICES];
        AudioDeviceName name;
        memset(names, 0, sizeof(names));
        memset(&name, 0, sizeof(name));
        s32 listed = -1;
        seen.list_rc = auddevListAudioDeviceName(names, SERVICE_AUDIO_DEVICES, &listed);
        seen.listed = listed;
        memcpy(seen.names, names, sizeof(seen.names));
        seen.active_rc = auddevGetActiveAudioDeviceName(&name);
        memcpy(seen.active, name.name, sizeof(seen.active));
        seen.active[SERVICE_AUDIO_NAME - 1u] = '\0';
        active = R_SUCCEEDED(seen.active_rc) && seen.active[0] != '\0';
        const bool passed = ServiceJudgeAudioActive(&seen, detail, sizeof(detail));
        Report(SERVICE_AUDDEV_ACTIVE, CHECK_EXACT, passed, detail);
    }
    if (!active) {
        Skip(SERVICE_AUDDEV_VOLUME, REPORT_REASON_DEPENDS, "no active device to set a volume on");
    } else if (Start(SERVICE_AUDDEV_VOLUME)) {
        AudioDeviceName name;
        memcpy(name.name, seen.active, sizeof(name.name));
        float before = -1.0f, after = -1.0f;
        const uint32_t before_rc = auddevGetAudioDeviceOutputVolume(&name, &before);
        const float asked = before == 0.5f ? 0.25f : 0.5f;
        const uint32_t set_rc = auddevSetAudioDeviceOutputVolume(&name, asked);
        const uint32_t after_rc = auddevGetAudioDeviceOutputVolume(&name, &after);
        const uint32_t restore_rc =
            auddevSetAudioDeviceOutputVolume(&name, before >= 0.0f ? before : 1.0f);
        const bool passed = ServiceJudgeAudioVolume(before_rc, before, set_rc, asked, after_rc,
                                                    after, restore_rc, detail, sizeof(detail));
        Report(SERVICE_AUDDEV_VOLUME, CHECK_EXACT, passed, detail);
    }
    auddevExit();
}

// ---------------------------------------------------------------------------------------------
// fsdir

/// Where the directory cases work on the memory card, under the service family's own directory,
/// and the names they use there.
#define DIR_PARENT "/edenprobe/service/directory"
#define DIR_OLD DIR_PARENT "/a"
#define DIR_NEW DIR_PARENT "/b"
#define DIR_FILE "f"

/// The memory card as libnx mounted it at start, written as a game writes its own files: a
/// directory with a file renamed in its parent, listed, and deleted with what it holds. What a
/// run before left there is deleted first.
static void RunFsdir(void) {
    char detail[480];
    FsFileSystem *const card = fsdevGetDeviceFileSystem("sdmc");
    if (card == NULL) {
        const char *const why = "the memory card is not mounted";
        Skip(SERVICE_DIR_RENAME, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_DIR_LIST, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_DIR_DELETE, REPORT_REASON_UNAVAILABLE, "%s", why);
        return;
    }
    bool renamed = false;
    if (Start(SERVICE_DIR_RENAME)) {
        ServiceDirRename seen;
        memset(&seen, 0, sizeof(seen));
        fsFsCreateDirectory(card, "/edenprobe");
        fsFsCreateDirectory(card, "/edenprobe/service");
        fsFsDeleteDirectoryRecursively(card, DIR_PARENT);
        seen.make_rc = fsFsCreateDirectory(card, DIR_PARENT);
        if (R_SUCCEEDED(seen.make_rc)) {
            seen.make_rc = fsFsCreateDirectory(card, DIR_OLD);
        }
        seen.file_rc = fsFsCreateFile(card, DIR_OLD "/" DIR_FILE, SERVICE_DIR_FILE_SIZE, 0);
        seen.rename_rc = fsFsRenameDirectory(card, DIR_OLD, DIR_NEW);
        FsDirEntryType type = (FsDirEntryType)0xEE;
        seen.new_rc = fsFsGetEntryType(card, DIR_NEW, &type);
        seen.new_type = (uint32_t)type;
        seen.old_rc = fsFsGetEntryType(card, DIR_OLD, &type);
        type = (FsDirEntryType)0xEE;
        seen.inside_rc = fsFsGetEntryType(card, DIR_NEW "/" DIR_FILE, &type);
        seen.inside_type = (uint32_t)type;
        renamed = R_SUCCEEDED(seen.rename_rc) && R_SUCCEEDED(seen.new_rc);
        const bool passed = ServiceJudgeDirRename(&seen, detail, sizeof(detail));
        Report(SERVICE_DIR_RENAME, CHECK_EXACT, passed, detail);
    }
    if (!renamed) {
        Skip(SERVICE_DIR_LIST, REPORT_REASON_DEPENDS, "no renamed directory to list");
    } else if (Start(SERVICE_DIR_LIST)) {
        ServiceDirList seen;
        memset(&seen, 0, sizeof(seen));
        seen.count = seen.read = seen.again = -1;
        FsDir dir;
        memset(&dir, 0, sizeof(dir));
        seen.open_rc = fsFsOpenDirectory(card, DIR_NEW,
                                         FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir);
        if (R_SUCCEEDED(seen.open_rc)) {
            static FsDirectoryEntry entries[4];
            memset(entries, 0, sizeof(entries));
            seen.count_rc = fsDirGetEntryCount(&dir, &seen.count);
            seen.read_rc = fsDirRead(&dir, &seen.read, 4, entries);
            memcpy(seen.name, entries[0].name, sizeof(seen.name));
            seen.type = entries[0].type;
            seen.size = entries[0].file_size;
            seen.again_rc = fsDirRead(&dir, &seen.again, 4, entries);
            fsDirClose(&dir);
        }
        const bool passed = ServiceJudgeDirList(&seen, DIR_FILE, detail, sizeof(detail));
        Report(SERVICE_DIR_LIST, CHECK_EXACT, passed, detail);
    }
    if (!renamed) {
        Skip(SERVICE_DIR_DELETE, REPORT_REASON_DEPENDS, "no directory made to delete");
    } else if (Start(SERVICE_DIR_DELETE)) {
        const uint32_t delete_rc = fsFsDeleteDirectoryRecursively(card, DIR_PARENT);
        FsDirEntryType type;
        const uint32_t gone_rc = fsFsGetEntryType(card, DIR_PARENT, &type);
        const bool passed = ServiceJudgeDirDelete(delete_rc, gone_rc, detail, sizeof(detail));
        Report(SERVICE_DIR_DELETE, CHECK_EXACT, passed, detail);
    }
}

// ---------------------------------------------------------------------------------------------
// am-audio

/// The application's audio controller, as libnx opened it at start: the volumes a game expects
/// the system to play it and a library applet at, set the way a game does before it shows the
/// keyboard or an error, read back, and put back as they were.
static void RunAmAudio(void) {
    char detail[480];
    Service *const controller = appletGetServiceSession_AudioController();
    if (controller == NULL || !serviceIsActive(controller)) {
        Skip(SERVICE_AUDIOCTL_EXPECTED, REPORT_REASON_UNAVAILABLE,
             "this program has no IAudioController");
        Skip(SERVICE_AUDIOCTL_CHANGE, REPORT_REASON_UNAVAILABLE,
             "this program has no IAudioController");
        return;
    }
    if (Start(SERVICE_AUDIOCTL_EXPECTED)) {
        // GetMainAppletExpectedMasterVolume (1) and GetLibraryAppletExpectedMasterVolume (2),
        // SetExpectedMasterVolume (0) to a half and a quarter, the two reads again.
        ServiceExpectedVolume seen;
        memset(&seen, 0, sizeof(seen));
        seen.main_before = seen.library_before = -1.0f;
        seen.main_after = seen.library_after = -1.0f;
        seen.before_rc = appletGetExpectedMasterVolume(&seen.main_before, &seen.library_before);
        seen.main_asked = 0.5f;
        seen.library_asked = 0.25f;
        seen.set_rc = appletSetExpectedMasterVolume(seen.main_asked, seen.library_asked);
        seen.after_rc = appletGetExpectedMasterVolume(&seen.main_after, &seen.library_after);
        const bool known = seen.before_rc == 0u && seen.main_before >= 0.0f &&
                           seen.main_before <= 1.0f && seen.library_before >= 0.0f &&
                           seen.library_before <= 1.0f;
        seen.restore_rc = appletSetExpectedMasterVolume(known ? seen.main_before : 1.0f,
                                                        known ? seen.library_before : 1.0f);
        const bool passed = ServiceJudgeExpectedVolume(&seen, detail, sizeof(detail));
        Report(SERVICE_AUDIOCTL_EXPECTED, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_AUDIOCTL_CHANGE)) {
        // ChangeMainAppletMasterVolume (3) to three quarters with no fade, the main read (1),
        // SetTransparentVolumeRate (4) to a half, then the volumes as they were.
        ServiceMainVolume seen;
        memset(&seen, 0, sizeof(seen));
        float library = -1.0f;
        seen.before = seen.after = -1.0f;
        const uint32_t before_rc = appletGetExpectedMasterVolume(&seen.before, &library);
        seen.asked = 0.75f;
        seen.change_rc = appletChangeMainAppletMasterVolume(seen.asked, 0);
        seen.after_rc = appletGetExpectedMasterVolume(&seen.after, NULL);
        seen.rate_rc = appletSetTransparentVolumeRate(0.5f);
        const bool known = before_rc == 0u && seen.before >= 0.0f && seen.before <= 1.0f &&
                           library >= 0.0f && library <= 1.0f;
        seen.restore_rc =
            appletSetExpectedMasterVolume(known ? seen.before : 1.0f, known ? library : 1.0f);
        const bool passed = ServiceJudgeMainVolume(&seen, detail, sizeof(detail));
        Report(SERVICE_AUDIOCTL_CHANGE, CHECK_EXACT, passed, detail);
    }
}

// ---------------------------------------------------------------------------------------------
// am-channel

/// A storage of SERVICE_CHANNEL_BYTES holding @p text, unpopped to the user channel with
/// UnpopToUserChannel (122); its own handle is closed after, the channel keeps a copy.
#define SERVICE_CHANNEL_BYTES 16u
static uint32_t ChannelUnpop(Service *functions, const char *text, uint32_t *create_rc,
                             uint32_t *write_rc) {
    AppletStorage storage;
    memset(&storage, 0, sizeof(storage));
    *create_rc = appletCreateStorage(&storage, SERVICE_CHANNEL_BYTES);
    if (R_FAILED(*create_rc)) {
        return *create_rc;
    }
    *write_rc = appletStorageWrite(&storage, 0, text, SERVICE_CHANNEL_BYTES);
    const uint32_t rc = R_FAILED(*write_rc)
                            ? *write_rc
                            : serviceDispatch(functions, 122, .in_num_objects = 1,
                                              .in_objects = { &storage.s }, );
    appletStorageClose(&storage);
    return rc;
}

/// PopLaunchParameter(UserChannel) and the storage's first bytes, its handle closed after.
static uint32_t ChannelPop(char *bytes, uint32_t *read_rc) {
    AppletStorage storage;
    memset(&storage, 0, sizeof(storage));
    const uint32_t rc = appletPopLaunchParameter(&storage, AppletLaunchParameterKind_UserChannel);
    if (R_SUCCEEDED(rc)) {
        *read_rc = appletStorageRead(&storage, 0, bytes, SERVICE_CHANNEL_BYTES);
        appletStorageClose(&storage);
    }
    return rc;
}

/// The user channel, which an application's programs hand each other data through when one
/// launches the next (ExecuteProgram): a storage put back in it, two in a row, and the index of the
/// program that ran before.
static void RunAmChannel(void) {
    char detail[480];
    Service *const functions = appletGetServiceSession_Functions();
    const bool application = appletGetAppletType() == AppletType_Application && functions != NULL &&
                             serviceIsActive(functions);
    char why[200];
    snprintf(why, sizeof(why),
             "needs to run as an application, with the application's functions; this program runs "
             "as applet type %d",
             (int)appletGetAppletType());
    if (!application) {
        Skip(SERVICE_CHANNEL_UNPOP, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_CHANNEL_ORDER, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_CHANNEL_PREVIOUS, REPORT_REASON_UNAVAILABLE, "%s", why);
        return;
    }
    static const char FIRST[SERVICE_CHANNEL_BYTES] = "edenprobe first";
    static const char SECOND[SERVICE_CHANNEL_BYTES] = "edenprobe secnd";
    if (!hosversionAtLeast(6, 0, 0)) {
        Skip(SERVICE_CHANNEL_UNPOP, REPORT_REASON_UNAVAILABLE,
             "UnpopToUserChannel came with 6.0.0; this machine says it is older");
        Skip(SERVICE_CHANNEL_ORDER, REPORT_REASON_UNAVAILABLE,
             "ClearUserChannel and UnpopToUserChannel came with 6.0.0; this machine says it is "
             "older");
    } else {
        if (Start(SERVICE_CHANNEL_UNPOP)) {
            ServiceUserChannel seen;
            memset(&seen, 0, sizeof(seen));
            char bytes[SERVICE_CHANNEL_BYTES];
            memset(bytes, 0, sizeof(bytes));
            seen.unpop_rc = ChannelUnpop(functions, FIRST, &seen.create_rc, &seen.write_rc);
            seen.pop_rc = ChannelPop(bytes, &seen.read_rc);
            seen.same = seen.pop_rc == 0u && memcmp(bytes, FIRST, sizeof(bytes)) == 0;
            char again[SERVICE_CHANNEL_BYTES];
            uint32_t again_read = 0;
            seen.again_rc = ChannelPop(again, &again_read);
            const bool passed = ServiceJudgeUserChannel(&seen, detail, sizeof(detail));
            Report(SERVICE_CHANNEL_UNPOP, CHECK_EXACT, passed, detail);
        }
        if (Start(SERVICE_CHANNEL_ORDER)) {
            ServiceChannelOrder seen;
            memset(&seen, 0, sizeof(seen));
            uint32_t create_rc = 0, write_rc = 0;
            seen.first_rc = ChannelUnpop(functions, FIRST, &create_rc, &write_rc);
            seen.second_rc = ChannelUnpop(functions, SECOND, &create_rc, &write_rc);
            char bytes[SERVICE_CHANNEL_BYTES];
            memset(bytes, 0, sizeof(bytes));
            seen.pop_rc = ChannelPop(bytes, &seen.read_rc);
            seen.got = seen.pop_rc != 0u                              ? 0
                       : memcmp(bytes, SECOND, sizeof(bytes)) == 0 ? 2
                       : memcmp(bytes, FIRST, sizeof(bytes)) == 0  ? 1
                                                                   : 0;
            seen.clear_rc = serviceDispatch(functions, 121);
            uint32_t after_read = 0;
            seen.after_rc = ChannelPop(bytes, &after_read);
            const bool passed = ServiceJudgeChannelOrder(&seen, detail, sizeof(detail));
            Report(SERVICE_CHANNEL_ORDER, CHECK_EXACT, passed, detail);
        }
    }
    if (!hosversionAtLeast(5, 0, 0)) {
        Skip(SERVICE_CHANNEL_PREVIOUS, REPORT_REASON_UNAVAILABLE,
             "GetPreviousProgramIndex came with 5.0.0; this machine says it is older");
    } else if (Start(SERVICE_CHANNEL_PREVIOUS)) {
        s32 index = 0x7EEEEEEE;
        const uint32_t rc = serviceDispatchOut(functions, 123, index);
        const bool passed = ServiceJudgePreviousProgram(rc, index, detail, sizeof(detail));
        Report(SERVICE_CHANNEL_PREVIOUS, CHECK_EXACT, passed, detail);
    }
}

// ---------------------------------------------------------------------------------------------
// am-storage

/// The storages a game hands a library applet its arguments in (the keyboard's text, an error's
/// code): one created, its size, 16 bytes written and read back, and a write and a read that
/// reach 8 bytes past its end.
static void RunAmStorage(void) {
    char detail[480];
    static const u8 BYTES[16] = { 0x65, 0x64, 0x65, 0x6e, 0x70, 0x72, 0x6f, 0x62,
                                  0x65, 0x20, 0x61, 0x6d, 0x20, 0x73, 0x74, 0x6f };
    if (Start(SERVICE_STORAGE_KEEPS)) {
        ServiceStorageKeeps seen;
        memset(&seen, 0, sizeof(seen));
        AppletStorage storage;
        memset(&storage, 0, sizeof(storage));
        u8 back[16];
        memset(back, 0, sizeof(back));
        seen.size = -1;
        seen.create_rc = appletCreateStorage(&storage, SERVICE_STORAGE_SIZE);
        if (R_SUCCEEDED(seen.create_rc)) {
            s64 size = -1;
            seen.size_rc = appletStorageGetSize(&storage, &size);
            seen.size = size;
            seen.write_rc = appletStorageWrite(&storage, SERVICE_STORAGE_AT, BYTES, sizeof(BYTES));
            seen.read_rc = appletStorageRead(&storage, SERVICE_STORAGE_AT, back, sizeof(back));
            seen.same = seen.read_rc == 0u && memcmp(back, BYTES, sizeof(back)) == 0;
            appletStorageClose(&storage);
        }
        const bool passed = ServiceJudgeStorageKeeps(&seen, detail, sizeof(detail));
        Report(SERVICE_STORAGE_KEEPS, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_STORAGE_WRITE_PAST)) {
        AppletStorage storage;
        memset(&storage, 0, sizeof(storage));
        const uint32_t create_rc = appletCreateStorage(&storage, SERVICE_STORAGE_SIZE);
        uint32_t rc = 0xFFFFFFFFu;
        if (R_SUCCEEDED(create_rc)) {
            rc = appletStorageWrite(&storage, SERVICE_STORAGE_PAST, BYTES, sizeof(BYTES));
            appletStorageClose(&storage);
        }
        const bool passed =
            ServiceJudgeStoragePast("a write", create_rc, rc, detail, sizeof(detail));
        Report(SERVICE_STORAGE_WRITE_PAST, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_STORAGE_READ_PAST)) {
        AppletStorage storage;
        memset(&storage, 0, sizeof(storage));
        u8 back[16];
        const uint32_t create_rc = appletCreateStorage(&storage, SERVICE_STORAGE_SIZE);
        uint32_t rc = 0xFFFFFFFFu;
        if (R_SUCCEEDED(create_rc)) {
            rc = appletStorageRead(&storage, SERVICE_STORAGE_PAST, back, sizeof(back));
            appletStorageClose(&storage);
        }
        const bool passed =
            ServiceJudgeStoragePast("a read", create_rc, rc, detail, sizeof(detail));
        Report(SERVICE_STORAGE_READ_PAST, CHECK_EXACT, passed, detail);
    }
}

// ---------------------------------------------------------------------------------------------
// mii

/// One Mii as Get (3) gives it: the character, then where it comes from (0 the database, 1 the
/// defaults).
typedef struct {
    MiiCharInfo info;
    u32 source;
} ServiceMiiElement;

/// mii:u, as a game with Miis opens it to list the player's and the console's: the six default
/// Miis counted and given, the database's state, and a default index past the last.
static void RunMii(void) {
    char detail[480];
    const Result opened = miiInitialize(MiiServiceType_User);
    MiiDatabase db;
    memset(&db, 0, sizeof(db));
    const Result database =
        R_SUCCEEDED(opened) ? miiOpenDatabase(&db, MiiSpecialKeyCode_Normal) : opened;
    if (R_FAILED(database)) {
        char why[200];
        snprintf(why, sizeof(why), "miiInitialize(User) 0x%x, miiOpenDatabase(Normal) 0x%x",
                 (unsigned)opened, (unsigned)database);
        Skip(SERVICE_MII_COUNT, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_MII_DEFAULTS, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_MII_STATE, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_MII_PAST, REPORT_REASON_UNAVAILABLE, "%s", why);
        if (R_SUCCEEDED(opened)) {
            miiExit();
        }
        return;
    }
    if (Start(SERVICE_MII_COUNT)) {
        s32 count = -1;
        const uint32_t rc = miiDatabaseGetCount(&db, &count, MiiSourceFlag_Default);
        const bool passed = ServiceJudgeMiiCount(rc, count, detail, sizeof(detail));
        Report(SERVICE_MII_COUNT, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_MII_DEFAULTS)) {
        static ServiceMiiElement elements[SERVICE_MII_DEFAULT_COUNT + 2u];
        memset(elements, 0, sizeof(elements));
        ServiceMiiDefaults seen;
        memset(&seen, 0, sizeof(seen));
        const u32 flag = MiiSourceFlag_Default;
        s32 count = -1;
        seen.get_rc = serviceDispatchInOut(
            &db.s, 3, flag, count,
            .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
            .buffers = { { elements, sizeof(elements) } }, );
        seen.count = count;
        const u32 given = count > 0 && count <= (s32)SERVICE_MII_DEFAULT_COUNT ? (u32)count : 0u;
        for (u32 i = 0; i < given; ++i) {
            MiiCharInfo built;
            memset(&built, 0, sizeof(built));
            const uint32_t rc = serviceDispatchInOut(&db.s, 7, i, built);
            if (rc != 0u && seen.built_rc == 0u) {
                seen.built_rc = rc;
            }
            if (elements[i].source == 1u) {
                ++seen.defaults;
            }
            // The create id comes first, and is new for every build.
            if (rc == 0u && memcmp((const u8 *)&built + 16, (const u8 *)&elements[i].info + 16,
                                   sizeof(built) - 16) == 0) {
                ++seen.same;
            }
        }
        const bool passed = ServiceJudgeMiiDefaults(&seen, detail, sizeof(detail));
        Report(SERVICE_MII_DEFAULTS, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_MII_STATE)) {
        bool updated = true, full = true;
        const uint32_t updated_rc = miiDatabaseIsUpdated(&db, &updated, MiiSourceFlag_Default);
        const uint32_t full_rc = miiDatabaseIsFull(&db, &full);
        const bool passed =
            ServiceJudgeMiiState(updated_rc, updated, full_rc, full, detail, sizeof(detail));
        Report(SERVICE_MII_STATE, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_MII_PAST)) {
        MiiCharInfo built;
        memset(&built, 0, sizeof(built));
        const u32 index = SERVICE_MII_DEFAULT_COUNT;
        const uint32_t rc = serviceDispatchInOut(&db.s, 7, index, built);
        const bool passed = ServiceJudgeMiiPast(rc, detail, sizeof(detail));
        Report(SERVICE_MII_PAST, CHECK_EXACT, passed, detail);
    }
    miiDatabaseClose(&db);
    miiExit();
}

// ---------------------------------------------------------------------------------------------
// am-mode

/// The state getter's modes a game reads at start or sets in play: VR mode, which a game with a
/// VR scene turns on and off, the boot mode and the controller firmware update section.
static void RunAmMode(void) {
    char detail[480];
    if (!hosversionAtLeast(6, 0, 0)) {
        Skip(SERVICE_MODE_VR, REPORT_REASON_UNAVAILABLE,
             "libnx turns VR mode from 6.0.0; this machine says it is older");
    } else if (Start(SERVICE_MODE_VR)) {
        ServiceVrTurn seen;
        memset(&seen, 0, sizeof(seen));
        seen.before_rc = appletIsVrModeEnabled(&seen.before);
        seen.on_rc = appletSetVrModeEnabled(true);
        seen.on_read_rc = appletIsVrModeEnabled(&seen.on);
        seen.off_rc = appletSetVrModeEnabled(false);
        seen.off_read_rc = appletIsVrModeEnabled(&seen.off);
        const bool passed = ServiceJudgeVrTurn(&seen, detail, sizeof(detail));
        Report(SERVICE_MODE_VR, CHECK_EXACT, passed, detail);
    }
    if (!hosversionAtLeast(3, 0, 0)) {
        Skip(SERVICE_MODE_BOOT, REPORT_REASON_UNAVAILABLE,
             "IsInControllerFirmwareUpdateSection came with 3.0.0; this machine says it is older");
    } else if (Start(SERVICE_MODE_BOOT)) {
        PmBootMode mode = (PmBootMode)0xEE;
        bool section = true;
        const uint32_t boot_rc = appletGetBootMode(&mode);
        const uint32_t section_rc = appletIsInControllerFirmwareUpdateSection(&section);
        const bool passed = ServiceJudgeBootMode(boot_rc, (uint32_t)mode, section_rc, section,
                                                 detail, sizeof(detail));
        Report(SERVICE_MODE_BOOT, CHECK_EXACT, passed, detail);
    }
}

// ---------------------------------------------------------------------------------------------
// bsd-local

/// bsd:u with no network: what a game's socket layer is told about a socket it may not open, a
/// timeout it sets out of range or short, and an address read into a buffer smaller than it.
static void RunBsdLocal(void) {
    char detail[480];
    const Result sockets = socketInitializeDefault();
    if (R_FAILED(sockets)) {
        char why[200];
        snprintf(why, sizeof(why), "socketInitializeDefault() returned 0x%x", (unsigned)sockets);
        Skip(SERVICE_BSD_RAW, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_BSD_TIMEOUT_RANGE, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_BSD_TIMEOUT_SHORT, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_BSD_NAME_SHORT, REPORT_REASON_UNAVAILABLE, "%s", why);
        return;
    }
    if (Start(SERVICE_BSD_RAW)) {
        errno = 0;
        const int fd = socket(AF_INET, SOCK_RAW, 0);
        const int err = errno;
        if (fd >= 0) {
            close(fd);
        }
        const bool passed = ServiceJudgeRawSocket(fd, err, detail, sizeof(detail));
        Report(SERVICE_BSD_RAW, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_BSD_TIMEOUT_RANGE)) {
        ServiceTimeoutRange seen;
        memset(&seen, 0, sizeof(seen));
        seen.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (seen.socket_fd >= 0) {
            const struct timeval usec = { 0, 1000000 };
            errno = 0;
            seen.usec_ret =
                setsockopt(seen.socket_fd, SOL_SOCKET, SO_RCVTIMEO, &usec, sizeof(usec));
            seen.usec_errno = errno;
            const struct timeval sec = { -1, 0 };
            errno = 0;
            seen.sec_ret = setsockopt(seen.socket_fd, SOL_SOCKET, SO_SNDTIMEO, &sec, sizeof(sec));
            seen.sec_errno = errno;
            close(seen.socket_fd);
        }
        const bool passed = ServiceJudgeTimeoutRange(&seen, detail, sizeof(detail));
        Report(SERVICE_BSD_TIMEOUT_RANGE, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_BSD_TIMEOUT_SHORT)) {
        const int fd = socket(AF_INET, SOCK_DGRAM, 0);
        int ret = 0, err = 0;
        if (fd >= 0) {
            const u32 four = 1;
            errno = 0;
            ret = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &four, sizeof(four));
            err = errno;
            close(fd);
        }
        const bool passed = ServiceJudgeTimeoutShort(fd, ret, err, detail, sizeof(detail));
        Report(SERVICE_BSD_TIMEOUT_SHORT, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_BSD_NAME_SHORT)) {
        ServiceNameShort seen;
        memset(&seen, 0, sizeof(seen));
        seen.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (seen.socket_fd >= 0) {
            struct sockaddr_in at;
            memset(&at, 0, sizeof(at));
            at.sin_family = AF_INET;
            at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            errno = 0;
            seen.bind_ret = bind(seen.socket_fd, (const struct sockaddr *)&at, sizeof(at));
            seen.bind_errno = errno;
            u8 name[16];
            memset(name, 0xEE, sizeof(name));
            socklen_t length = 8;
            errno = 0;
            seen.name_ret = getsockname(seen.socket_fd, (struct sockaddr *)name, &length);
            seen.name_errno = errno;
            seen.length = (uint32_t)length;
            seen.past_kept = true;
            for (size_t i = 8; i < sizeof(name); ++i) {
                seen.past_kept = seen.past_kept && name[i] == 0xEE;
            }
            close(seen.socket_fd);
        }
        const bool passed = ServiceJudgeNameShort(&seen, detail, sizeof(detail));
        Report(SERVICE_BSD_NAME_SHORT, CHECK_EXACT, passed, detail);
    }
    socketExit();
}

// ---------------------------------------------------------------------------------------------
// ipc

/// A session of set opened by name, as the SDK opens each service, with nothing of libnx's around
/// it.
static uint32_t IpcOpenSet(Handle *h) {
    *h = 0;
    return smGetServiceOriginal(h, smEncodeName("set"));
}

/// What every service session answers whatever service it is, through the IpcController commands
/// a game's IPC layer sends (sf's pointer buffer, clone) and the Close it ends a session with.
static void RunIpc(void) {
    char detail[480];
    if (Start(SERVICE_IPC_POINTER)) {
        Handle h;
        const uint32_t open_rc = IpcOpenSet(&h);
        u16 size = 0;
        uint32_t rc = 0xFFFFFFFFu;
        if (open_rc == 0u) {
            rc = cmifQueryPointerBufferSize(h, &size);
            Service s;
            serviceCreate(&s, h);
            serviceClose(&s);
        }
        const bool passed =
            ServiceJudgePointerBuffer(open_rc, rc, size, detail, sizeof(detail));
        Report(SERVICE_IPC_POINTER, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_IPC_CLONE)) {
        ServiceSessionClone seen;
        memset(&seen, 0, sizeof(seen));
        Handle h;
        seen.open_rc = IpcOpenSet(&h);
        if (seen.open_rc == 0u) {
            Handle clone = 0;
            seen.clone_rc = cmifCloneCurrentObject(h, &clone);
            seen.handle = seen.clone_rc == 0u && clone != 0u;
            Service first, cloned;
            serviceCreate(&first, h);
            u64 code = 0;
            seen.first_rc = serviceDispatchOut(&first, 0, code);
            seen.first = code;
            if (seen.handle) {
                serviceCreate(&cloned, clone);
                code = 0;
                seen.clone_ask_rc = serviceDispatchOut(&cloned, 0, code);
                seen.cloned = code;
                serviceClose(&cloned);
            }
            serviceClose(&first);
        }
        const bool passed = ServiceJudgeSessionClone(&seen, detail, sizeof(detail));
        Report(SERVICE_IPC_CLONE, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_IPC_CLOSE)) {
        Handle h;
        const uint32_t open_rc = IpcOpenSet(&h);
        uint32_t close_rc = 0xFFFFFFFFu, after_rc = 0xFFFFFFFFu;
        if (open_rc == 0u) {
            cmifMakeCloseRequest(armGetTls(), 0);
            close_rc = svcSendSyncRequest(h);
            Service s;
            serviceCreate(&s, h);
            u64 code = 0;
            after_rc = serviceDispatchOut(&s, 0, code);
            svcCloseHandle(h);
        }
        const bool passed =
            ServiceJudgeSessionClose(open_rc, close_rc, after_rc, detail, sizeof(detail));
        Report(SERVICE_IPC_CLOSE, CHECK_EXACT, passed, detail);
    }
}

// ---------------------------------------------------------------------------------------------
// hid

/// One entry of a LIFO read as libnx reads one: the storage's number before and after the copy of
/// the state must agree, or the copy is tried again.
static bool HidEntry(const HidNpadCommonStateAtomicStorage *entry, u64 *storage, u64 *state) {
    for (int tries = 0; tries < 8; ++tries) {
        const u64 before = __atomic_load_n(&entry->sampling_number, __ATOMIC_ACQUIRE);
        const u64 sampled = __atomic_load_n(&entry->state.sampling_number, __ATOMIC_ACQUIRE);
        const u64 after = __atomic_load_n(&entry->sampling_number, __ATOMIC_ACQUIRE);
        if (before == after) {
            *storage = before;
            *state = sampled;
            return true;
        }
    }
    return false;
}

/// hid's shared memory as libnx mapped it at start, with no input asked: every npad LIFO the
/// system keeps writing, a controller connected or not.
static void RunHid(void) {
    char detail[480];
    HidSharedMemory *const shared = (HidSharedMemory *)hidGetSharedmemAddr();
    if (shared == NULL) {
        Skip(SERVICE_HID_SAMPLING, REPORT_REASON_UNAVAILABLE,
             "hid's shared memory is not mapped in this program");
        return;
    }
    if (Start(SERVICE_HID_SAMPLING)) {
        ServiceHidSampling seen;
        memset(&seen, 0, sizeof(seen));
        for (int i = 0; i < 10; ++i) {
            HidNpadInternalState *const npad = &shared->npad.entries[i].internal_state;
            HidNpadCommonLifo *const lifos[7] = { &npad->full_key_lifo,  &npad->handheld_lifo,
                                                  &npad->joy_dual_lifo,  &npad->joy_left_lifo,
                                                  &npad->joy_right_lifo, &npad->palma_lifo,
                                                  &npad->system_ext_lifo };
            for (int k = 0; k < 7; ++k) {
                HidNpadCommonLifo *const lifo = lifos[k];
                const u64 count = __atomic_load_n(&lifo->header.count, __ATOMIC_ACQUIRE);
                const u64 tail = __atomic_load_n(&lifo->header.tail, __ATOMIC_ACQUIRE);
                if (count < 2u || tail >= 17u) {
                    continue;
                }
                u64 storage = 0, state = 0, before_storage = 0, before_state = 0;
                if (!HidEntry(&lifo->storage[tail], &storage, &state) ||
                    !HidEntry(&lifo->storage[(tail + 16u) % 17u], &before_storage,
                              &before_state)) {
                    ++seen.torn;
                    continue;
                }
                if (seen.lifos == 0u) {
                    seen.storage = storage;
                    seen.state = state;
                }
                ++seen.lifos;
                seen.doubled += storage == state << 1 ? 1u : 0u;
                seen.consecutive += before_state + 1u == state ? 1u : 0u;
            }
        }
        const bool passed = ServiceJudgeHidSampling(&seen, detail, sizeof(detail));
        Report(SERVICE_HID_SAMPLING, CHECK_EXACT, passed, detail);
    }
}

// ---------------------------------------------------------------------------------------------
// time-snap

/// GetClockSnapshot (400) of @p type into @p snapshot.
static uint32_t SnapTake(Service *timesrv, u8 type, u8 *snapshot) {
    return serviceDispatchIn(
        timesrv, 400, type,
        .buffer_attrs = { SfBufferAttr_FixedSize | SfBufferAttr_HipcPointer | SfBufferAttr_Out },
        .buffers = { { snapshot, SERVICE_SNAP_SIZE } }, );
}

/// CalculateSpanBetween (501) of two snapshots, in nanoseconds.
static uint32_t SnapSpan(Service *timesrv, const u8 *a, const u8 *b, s64 *span) {
    return serviceDispatchOut(
        timesrv, 501, *span,
        .buffer_attrs = { SfBufferAttr_FixedSize | SfBufferAttr_HipcPointer | SfBufferAttr_In,
                          SfBufferAttr_FixedSize | SfBufferAttr_HipcPointer | SfBufferAttr_In },
        .buffers = { { a, SERVICE_SNAP_SIZE }, { b, SERVICE_SNAP_SIZE } }, );
}

/// time:u of this suite's own, and the snapshot of the clocks a game takes to date a save or
/// time a wait across a sleep: what it holds, and the span between two.
static void RunTimeSnap(void) {
    char detail[480];
    Service timesrv, user, zones;
    memset(&timesrv, 0, sizeof(timesrv));
    memset(&user, 0, sizeof(user));
    memset(&zones, 0, sizeof(zones));
    Result rc = smGetService(&timesrv, "time:u");
    Result user_rc = rc, zones_rc = rc;
    if (R_SUCCEEDED(rc)) {
        user_rc = serviceDispatch(&timesrv, 0, .out_num_objects = 1, .out_objects = &user, );
        zones_rc = serviceDispatch(&timesrv, 3, .out_num_objects = 1, .out_objects = &zones, );
    }
    if (R_FAILED(rc) || R_FAILED(user_rc) || R_FAILED(zones_rc)) {
        char why[200];
        snprintf(why, sizeof(why),
                 "smGetService(\"time:u\") 0x%x, GetStandardUserSystemClock 0x%x, "
                 "GetTimeZoneService 0x%x",
                 (unsigned)rc, (unsigned)user_rc, (unsigned)zones_rc);
        Skip(SERVICE_SNAP_COHERENT, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_SNAP_SPAN, REPORT_REASON_UNAVAILABLE, "%s", why);
    } else {
        static u8 snapshot[SERVICE_SNAP_SIZE], later[SERVICE_SNAP_SIZE];
        if (Start(SERVICE_SNAP_COHERENT)) {
            ServiceSnapshot seen;
            memset(&seen, 0, sizeof(seen));
            memset(snapshot, 0, sizeof(snapshot));
            seen.snap_rc = SnapTake(&timesrv, 0, snapshot);
            TimeSystemClockContext context;
            memset(&context, 0, sizeof(context));
            seen.context_rc = serviceDispatchOut(&user, 2, context);
            TimeLocationName name;
            memset(&name, 0, sizeof(name));
            seen.name_rc = serviceDispatchOut(&zones, 0, name);
            seen.same_context = memcmp(snapshot, &context, sizeof(context)) == 0;
            memcpy(&seen.user_time, snapshot + SERVICE_SNAP_USER_TIME, 8);
            memcpy(&seen.offset, snapshot, 8);
            memcpy(&seen.steady, snapshot + SERVICE_SNAP_STEADY, 8);
            // The context's steady time point and the snapshot's: their source ids after the time.
            seen.same_source = memcmp(snapshot + 0x10, snapshot + SERVICE_SNAP_STEADY + 8, 16) == 0;
            seen.same_name = memcmp(snapshot + SERVICE_SNAP_LOCATION, &name, sizeof(name)) == 0;
            seen.type = snapshot[SERVICE_SNAP_TYPE];
            const bool passed = ServiceJudgeSnapshot(&seen, detail, sizeof(detail));
            Report(SERVICE_SNAP_COHERENT, CHECK_EXACT, passed, detail);
        }
        if (Start(SERVICE_SNAP_SPAN)) {
            memset(snapshot, 0, sizeof(snapshot));
            const uint32_t snap_rc = SnapTake(&timesrv, 0, snapshot);
            memcpy(later, snapshot, sizeof(later));
            s64 steady = 0;
            memcpy(&steady, later + SERVICE_SNAP_STEADY, 8);
            steady += 5;
            memcpy(later + SERVICE_SNAP_STEADY, &steady, 8);
            s64 same = -1, ahead = -1;
            const uint32_t same_rc = SnapSpan(&timesrv, snapshot, snapshot, &same);
            const uint32_t later_rc = SnapSpan(&timesrv, snapshot, later, &ahead);
            const bool passed = ServiceJudgeSnapSpan(snap_rc, same_rc, same, later_rc, ahead,
                                                     detail, sizeof(detail));
            Report(SERVICE_SNAP_SPAN, CHECK_EXACT, passed, detail);
        }
    }
    serviceClose(&zones);
    serviceClose(&user);
    serviceClose(&timesrv);
}

// ---------------------------------------------------------------------------------------------
// vi

/// The application display service, which every game opens before its first frame: the displays
/// it lists, the scaling modes it converts for its layer, and the memory an indirect layer needs.
static void RunVi(void) {
    char detail[480];
    const Result opened = viInitialize(ViServiceType_Default);
    Service *const display = R_SUCCEEDED(opened) ? viGetSession_IApplicationDisplayService() : NULL;
    if (display == NULL || !serviceIsActive(display)) {
        char why[200];
        snprintf(why, sizeof(why), "viInitialize(Default) returned 0x%x", (unsigned)opened);
        Skip(SERVICE_VI_DISPLAYS, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_VI_SCALING, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_VI_INDIRECT, REPORT_REASON_UNAVAILABLE, "%s", why);
        if (R_SUCCEEDED(opened)) {
            viExit();
        }
        return;
    }
    if (Start(SERVICE_VI_DISPLAYS)) {
        static u8 infos[4][0x60];
        memset(infos, 0, sizeof(infos));
        ServiceDisplayList seen;
        memset(&seen, 0, sizeof(seen));
        s64 count = -1;
        seen.rc = serviceDispatchOut(
            display, 1000, count, .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
            .buffers = { { infos, sizeof(infos) } }, );
        seen.count = count;
        seen.named = strncmp((const char *)infos[0], "Default", 0x40) == 0;
        seen.limited = infos[0][0x40];
        memcpy(&seen.layers, infos[0] + 0x48, 8);
        memcpy(&seen.width, infos[0] + 0x50, 8);
        memcpy(&seen.height, infos[0] + 0x58, 8);
        const bool passed = ServiceJudgeDisplayList(&seen, detail, sizeof(detail));
        Report(SERVICE_VI_DISPLAYS, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_VI_SCALING)) {
        uint32_t rc[6];
        uint64_t converted[6];
        for (u32 mode = 0; mode < 6; ++mode) {
            u64 out = 0xEEEEu;
            rc[mode] = serviceDispatchInOut(display, 2102, mode, out);
            converted[mode] = out;
        }
        const bool passed = ServiceJudgeScalingModes(rc, converted, detail, sizeof(detail));
        Report(SERVICE_VI_SCALING, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_VI_INDIRECT)) {
        const struct {
            s64 width, height;
        } in = { 1280, 720 };
        struct {
            s64 size, alignment;
        } out = { -1, -1 };
        const uint32_t rc = serviceDispatchInOut(display, 2460, in, out);
        const bool passed =
            ServiceJudgeIndirectMemory(rc, out.size, out.alignment, detail, sizeof(detail));
        Report(SERVICE_VI_INDIRECT, CHECK_EXACT, passed, detail);
    }
    viExit();
}

// ---------------------------------------------------------------------------------------------
// renderer

/// A renderer setting in thousandths: a float's, or a percent's as it is.
static int32_t RendererMilli(float v) {
    return (int32_t)(v * 1000.0f + (v < 0.0f ? -0.5f : 0.5f));
}

/// An audio renderer of the program's own, opened as a game's audio library opens one at start
/// (48 kHz, 24 voices, a final mix of 2 buffers, one sink), asked what it was opened with and two
/// settings a game may change in play, each put back; closed before the suite returns.
static void RunRenderer(void) {
    char detail[480];
    const AudioRendererConfig config = {
        .output_rate = AudioRendererOutputRate_48kHz,
        .num_voices = 24,
        .num_effects = 0,
        .num_sinks = 1,
        .num_mix_objs = 1,
        .num_mix_buffers = 2,
    };
    const Result opened = audrenInitialize(&config);
    Service *const renderer = R_SUCCEEDED(opened) ? audrenGetServiceSession_AudioRenderer() : NULL;
    if (renderer == NULL || !serviceIsActive(renderer)) {
        char why[200];
        snprintf(why, sizeof(why), "audrenInitialize returned 0x%x", (unsigned)opened);
        Skip(SERVICE_RENDERER_PARAMS, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_RENDERER_LIMIT, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_RENDERER_DROP, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_RENDERER_STATE, REPORT_REASON_UNAVAILABLE, "%s", why);
        if (R_SUCCEEDED(opened)) {
            audrenExit();
        }
        return;
    }
    if (Start(SERVICE_RENDERER_PARAMS)) {
        ServiceRendererParams seen;
        memset(&seen, 0, sizeof(seen));
        u32 v = 0;
        seen.rate_rc = serviceDispatchOut(renderer, 0, v);
        seen.rate = v;
        v = 0;
        seen.count_rc = serviceDispatchOut(renderer, 1, v);
        seen.count = v;
        v = 0;
        seen.mix_rc = serviceDispatchOut(renderer, 2, v);
        seen.mix = v;
        const bool passed = ServiceJudgeRendererParams(&seen, detail, sizeof(detail));
        Report(SERVICE_RENDERER_PARAMS, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_RENDERER_LIMIT)) {
        // GetRenderingTimeLimit (9), SetRenderingTimeLimit (8) to 70 percent, 9, 8 back.
        ServiceRendererSetting seen;
        memset(&seen, 0, sizeof(seen));
        u32 before = 0xEEEEu, after = 0xEEEEu;
        seen.before_rc = serviceDispatchOut(renderer, 9, before);
        const u32 asked = 70;
        seen.set_rc = serviceDispatchIn(renderer, 8, asked);
        seen.after_rc = serviceDispatchOut(renderer, 9, after);
        const u32 back = before <= 100u ? before : 100u;
        seen.restore_rc = serviceDispatchIn(renderer, 8, back);
        seen.before = (int32_t)before;
        seen.asked = (int32_t)asked;
        seen.after = (int32_t)after;
        seen.initial = 100;
        const bool passed =
            ServiceJudgeRendererSetting("GetRenderingTimeLimit", &seen, detail, sizeof(detail));
        Report(SERVICE_RENDERER_LIMIT, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_RENDERER_DROP)) {
        // GetVoiceDropParameter (13), SetVoiceDropParameter (12) to a half, 13, 12 back.
        ServiceRendererSetting seen;
        memset(&seen, 0, sizeof(seen));
        float before = -1.0f, after = -1.0f;
        seen.before_rc = serviceDispatchOut(renderer, 13, before);
        const float asked = 0.5f;
        seen.set_rc = serviceDispatchIn(renderer, 12, asked);
        seen.after_rc = serviceDispatchOut(renderer, 13, after);
        const float back = before >= 0.0f && before <= 1.0f ? before : 1.0f;
        seen.restore_rc = serviceDispatchIn(renderer, 12, back);
        seen.before = RendererMilli(before);
        seen.asked = RendererMilli(asked);
        seen.after = RendererMilli(after);
        seen.initial = 1000;
        const bool passed =
            ServiceJudgeRendererSetting("GetVoiceDropParameter", &seen, detail, sizeof(detail));
        Report(SERVICE_RENDERER_DROP, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_RENDERER_STATE)) {
        // GetState (3), Start (5), 3, Stop (6), 3: the renderer the audio family starts with
        // audrenStartAudioRenderer, here with no update asked of it.
        ServiceRendererState seen;
        memset(&seen, 0, sizeof(seen));
        seen.before = seen.started = seen.stopped = 0xEEu;
        u32 v = 0xEEu;
        seen.before_rc = serviceDispatchOut(renderer, 3, v);
        seen.before = v;
        seen.start_rc = serviceDispatch(renderer, 5);
        v = 0xEEu;
        seen.started_rc = serviceDispatchOut(renderer, 3, v);
        seen.started = v;
        seen.stop_rc = serviceDispatch(renderer, 6);
        v = 0xEEu;
        seen.stopped_rc = serviceDispatchOut(renderer, 3, v);
        seen.stopped = v;
        const bool passed = ServiceJudgeRendererState(&seen, detail, sizeof(detail));
        Report(SERVICE_RENDERER_STATE, CHECK_EXACT, passed, detail);
    }
    audrenExit();
}

// ---------------------------------------------------------------------------------------------
// ngc

/// The text the profanity filter is asked about: a name a player could type, with no word in it
/// that a list would hold.
static const char kNgcText[] = "edenprobe player";

/// The text in a buffer of SERVICE_NGC_SIZE, the rest of it nought.
static uint8_t g_ngc_text[SERVICE_NGC_SIZE];

/// ngc:u's Check (1) or Check2 (4): the lists to look in and the filter's options (nought), the
/// text in a buffer; the words found, as flags.
static Result NgcCheck(Service *s, uint32_t cmd, uint32_t *found) {
    const struct {
        uint32_t languages;
        uint8_t option[0x20];
    } in = { SERVICE_NGC_LANGUAGES, { 0 } };
    *found = 0xEEEEu;
    return serviceDispatchInOut(s, cmd, in, *found,
                                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
                                .buffers = { { g_ngc_text, sizeof(g_ngc_text) } }, );
}

/// ngc:u's Mask (2) or Mask2 (5): as Check, with a buffer for the masked text.
static void NgcMask(Service *s, uint32_t cmd, ServiceNgcText *seen) {
    const struct {
        uint32_t languages;
        uint8_t option[0x20];
    } in = { SERVICE_NGC_LANGUAGES, { 0 } };
    memset(seen->back, 0xEE, sizeof(seen->back));
    seen->count = 0xEEEEu;
    seen->rc = serviceDispatchInOut(
        s, cmd, in, seen->count,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In,
                          SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { g_ngc_text, sizeof(g_ngc_text) }, { seen->back, sizeof(seen->back) } }, );
}

/// The profanity filter a game runs a player's name or message through before it shows it or
/// sends it: ngc:u, asked as a game asks, and ngct:u, the newer one with one word list. A clean
/// name is checked, masked and filtered.
static void RunNgc(void) {
    char detail[480];
    memset(g_ngc_text, 0, sizeof(g_ngc_text));
    memcpy(g_ngc_text, kNgcText, sizeof(kNgcText));
    Service ngc;
    memset(&ngc, 0, sizeof(ngc));
    const Result ngc_rc = smGetService(&ngc, "ngc:u");
    if (R_FAILED(ngc_rc)) {
        Skip(SERVICE_NGC_CHECK, REPORT_REASON_UNAVAILABLE, "smGetService(\"ngc:u\") returned 0x%x",
             (unsigned)ngc_rc);
        Skip(SERVICE_NGC_MASK, REPORT_REASON_UNAVAILABLE, "smGetService(\"ngc:u\") returned 0x%x",
             (unsigned)ngc_rc);
    } else {
        if (Start(SERVICE_NGC_CHECK)) {
            ServiceNgcCheck seen;
            memset(&seen, 0, sizeof(seen));
            seen.version_rc = serviceDispatchOut(&ngc, 0, seen.version);
            seen.check_rc = NgcCheck(&ngc, 1, &seen.check);
            seen.check2_rc = NgcCheck(&ngc, 4, &seen.check2);
            const bool passed = ServiceJudgeNgcCheck(&seen, detail, sizeof(detail));
            Report(SERVICE_NGC_CHECK, CHECK_EXACT, passed, detail);
        }
        if (Start(SERVICE_NGC_MASK)) {
            ServiceNgcText mask, mask2;
            NgcMask(&ngc, 2, &mask);
            NgcMask(&ngc, 5, &mask2);
            const bool passed =
                ServiceJudgeNgcMask(kNgcText, &mask, &mask2, detail, sizeof(detail));
            Report(SERVICE_NGC_MASK, CHECK_EXACT, passed, detail);
        }
        serviceClose(&ngc);
    }
    Service ngct;
    memset(&ngct, 0, sizeof(ngct));
    const Result ngct_rc = smGetService(&ngct, "ngct:u");
    if (R_FAILED(ngct_rc)) {
        Skip(SERVICE_NGCT_CLEAN, REPORT_REASON_UNAVAILABLE,
             "smGetService(\"ngct:u\") returned 0x%x", (unsigned)ngct_rc);
        return;
    }
    if (Start(SERVICE_NGCT_CLEAN)) {
        // Match (0) and Filter (1) take the text by pointer and give the filtered one by pointer.
        u8 matched = 0xEEu;
        const uint32_t match_rc = serviceDispatchOut(
            &ngct, 0, matched, .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_In },
            .buffers = { { g_ngc_text, sizeof(g_ngc_text) } }, );
        ServiceNgcText filter;
        memset(&filter, 0, sizeof(filter));
        memset(filter.back, 0xEE, sizeof(filter.back));
        filter.rc = serviceDispatch(&ngct, 1,
                                    .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_In,
                                                      SfBufferAttr_HipcPointer | SfBufferAttr_Out },
                                    .buffers = { { g_ngc_text, sizeof(g_ngc_text) },
                                                 { filter.back, sizeof(filter.back) } }, );
        const bool passed = ServiceJudgeNgctClean(kNgcText, match_rc, (matched & 1u) != 0u,
                                                  &filter, detail, sizeof(detail));
        Report(SERVICE_NGCT_CLEAN, CHECK_EXACT, passed, detail);
    }
    serviceClose(&ngct);
}

// ---------------------------------------------------------------------------------------------
// am-lock

/// IsLocked (4) on an accessor.
static Result LockIsLocked(AppletLockAccessor *a, bool *locked) {
    u8 out = 0xEEu;
    const Result rc = serviceDispatchOut(&a->s, 4, out);
    *locked = (out & 1u) != 0u;
    return rc;
}

/// The lock accessors of the state getter, which a game takes around a moment the home button
/// must wait for (a save, a purchase): the home button's, taken as libnx takes it, and a reader
/// lock's event, waited on as libnx waits.
static void RunAmLock(void) {
    char detail[480];
    if (Start(SERVICE_LOCK_TAKEN)) {
        ServiceLockTaken seen;
        memset(&seen, 0, sizeof(seen));
        AppletLockAccessor a;
        memset(&a, 0, sizeof(a));
        seen.get_rc = appletGetHomeButtonReaderLockAccessor(&a);
        if (R_SUCCEEDED(seen.get_rc)) {
            seen.before_rc = LockIsLocked(&a, &seen.before);
            seen.try_rc = appletLockAccessorTryLock(&a, &seen.took);
            seen.locked_rc = LockIsLocked(&a, &seen.locked);
            seen.again_rc = appletLockAccessorTryLock(&a, &seen.again);
            seen.unlock_rc = appletLockAccessorUnlock(&a);
            seen.after_rc = LockIsLocked(&a, &seen.after);
            appletLockAccessorClose(&a);
        }
        const bool passed = ServiceJudgeLockTaken(&seen, detail, sizeof(detail));
        Report(SERVICE_LOCK_TAKEN, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_LOCK_EVENT)) {
        ServiceLockEvent seen;
        memset(&seen, 0, sizeof(seen));
        AppletLockAccessor a;
        memset(&a, 0, sizeof(a));
        seen.get_rc = appletGetReaderLockAccessorEx(&a, 0);
        if (R_SUCCEEDED(seen.get_rc)) {
            seen.free_rc = eventWait(&a.event, 0);
            // TryLock (1) asking for no handle, as libnx's own does.
            const u8 no_handle = 0;
            u8 took = 0;
            Handle unused = INVALID_HANDLE;
            seen.try_rc = serviceDispatchInOut(&a.s, 1, no_handle, took,
                                               .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
                                               .out_handles = &unused, );
            seen.took = (took & 1u) != 0u;
            if (unused != INVALID_HANDLE) {
                svcCloseHandle(unused);
            }
            seen.clear_rc = eventClear(&a.event);
            seen.held_rc = eventWait(&a.event, 0);
            seen.unlock_rc = appletLockAccessorUnlock(&a);
            seen.unlocked_rc = eventWait(&a.event, 0);
            appletLockAccessorClose(&a);
        }
        const bool passed = ServiceJudgeLockEvent(&seen, detail, sizeof(detail));
        Report(SERVICE_LOCK_EVENT, CHECK_EXACT, passed, detail);
    }
}

// ---------------------------------------------------------------------------------------------
// set-sys

/// One of the flags the set-sys suite turns over: libnx's read and write of it.
typedef struct {
    Result (*get)(bool *out);
    Result (*set)(bool flag);
} SetsysFlag;

/// A volume in thousandths, for the line.
static int32_t SetsysMilli(float v) {
    return (int32_t)(v * 1000.0f + (v < 0.0f ? -0.5f : 0.5f));
}

/// set:sys as a homebrew program reaches it: the theme it draws with (the color set), the
/// keyboard layout its own keyboard picks, how hard a pad vibrates, and four flags, each set to
/// another value, read back, and put back as it was.
static void RunSetSys(void) {
    char detail[480];
    const Result opened = setsysInitialize();
    if (R_FAILED(opened)) {
        Skip(SERVICE_SETSYS_LOOK, REPORT_REASON_UNAVAILABLE, "setsysInitialize returned 0x%x",
             (unsigned)opened);
        Skip(SERVICE_SETSYS_VIBRATION, REPORT_REASON_UNAVAILABLE, "setsysInitialize returned 0x%x",
             (unsigned)opened);
        Skip(SERVICE_SETSYS_FLAGS, REPORT_REASON_UNAVAILABLE, "setsysInitialize returned 0x%x",
             (unsigned)opened);
        return;
    }
    if (Start(SERVICE_SETSYS_LOOK)) {
        ServiceSetsysValue color, layout;
        memset(&color, 0, sizeof(color));
        memset(&layout, 0, sizeof(layout));
        ColorSetId id = (ColorSetId)-1;
        color.before_rc = setsysGetColorSetId(&id);
        color.before = (int32_t)id;
        color.asked = color.before == ColorSetId_Dark ? ColorSetId_Light : ColorSetId_Dark;
        color.set_rc = setsysSetColorSetId((ColorSetId)color.asked);
        id = (ColorSetId)-1;
        color.after_rc = setsysGetColorSetId(&id);
        color.after = (int32_t)id;
        const bool known = color.before_rc == 0u &&
                           (color.before == ColorSetId_Light || color.before == ColorSetId_Dark);
        color.restore_rc =
            setsysSetColorSetId(known ? (ColorSetId)color.before : ColorSetId_Light);
        SetKeyboardLayout k = (SetKeyboardLayout)-1;
        layout.before_rc = setsysGetKeyboardLayout(&k);
        layout.before = (int32_t)k;
        layout.asked = layout.before == SetKeyboardLayout_EnglishUk ? SetKeyboardLayout_French
                                                                    : SetKeyboardLayout_EnglishUk;
        layout.set_rc = setsysSetKeyboardLayout((SetKeyboardLayout)layout.asked);
        k = (SetKeyboardLayout)-1;
        layout.after_rc = setsysGetKeyboardLayout(&k);
        layout.after = (int32_t)k;
        const bool layout_known = layout.before_rc == 0u && layout.before >= 0 &&
                                  layout.before < 32;
        layout.restore_rc = setsysSetKeyboardLayout(
            layout_known ? (SetKeyboardLayout)layout.before : SetKeyboardLayout_EnglishUs);
        const bool passed = ServiceJudgeSetsysLook(&color, &layout, detail, sizeof(detail));
        Report(SERVICE_SETSYS_LOOK, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_SETSYS_VIBRATION)) {
        // GetVibrationMasterVolume (35), SetVibrationMasterVolume (36) to a quarter or a half, 35
        // again, 36 back.
        ServiceSetsysValue seen;
        memset(&seen, 0, sizeof(seen));
        float before = -1.0f, after = -1.0f;
        seen.before_rc = setsysGetVibrationMasterVolume(&before);
        const float asked = before == 0.25f ? 0.5f : 0.25f;
        seen.set_rc = setsysSetVibrationMasterVolume(asked);
        seen.after_rc = setsysGetVibrationMasterVolume(&after);
        const bool known = seen.before_rc == 0u && before >= 0.0f && before <= 1.0f;
        seen.restore_rc = setsysSetVibrationMasterVolume(known ? before : 1.0f);
        seen.before = SetsysMilli(before);
        seen.asked = SetsysMilli(asked);
        seen.after = SetsysMilli(after);
        const bool passed =
            ServiceJudgeSetsysValue("vibration master volume", &seen, detail, sizeof(detail));
        Report(SERVICE_SETSYS_VIBRATION, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_SETSYS_FLAGS)) {
        static const SetsysFlag flags[SERVICE_SETSYS_FLAG_COUNT] = {
            { setsysGetLockScreenFlag, setsysSetLockScreenFlag },
            { setsysGetQuestFlag, setsysSetQuestFlag },
            { setsysGetBatteryPercentageFlag, setsysSetBatteryPercentageFlag },
            { setsysGetSpeakerAutoMuteFlag, setsysSetSpeakerAutoMuteFlag },
        };
        ServiceSetsysFlags seen;
        memset(&seen, 0, sizeof(seen));
        for (uint32_t i = 0; i < SERVICE_SETSYS_FLAG_COUNT; ++i) {
            bool before = false, after = false, back = false;
            Result rc = flags[i].get(&before);
            if (R_SUCCEEDED(rc)) {
                // Only a flag read first is written, so a refused read changes nothing.
                rc = flags[i].set(!before);
                const Result after_rc = flags[i].get(&after);
                const Result back_rc = flags[i].set(before);
                const Result read_rc = flags[i].get(&back);
                rc = R_FAILED(rc)         ? rc
                     : R_FAILED(after_rc) ? after_rc
                     : R_FAILED(back_rc)  ? back_rc
                                          : read_rc;
            }
            seen.rc[i] = rc;
            seen.before[i] = before;
            seen.after[i] = after;
            seen.back[i] = back;
        }
        const bool passed = ServiceJudgeSetsysFlags(&seen, detail, sizeof(detail));
        Report(SERVICE_SETSYS_FLAGS, CHECK_EXACT, passed, detail);
    }
    setsysExit();
}

// ---------------------------------------------------------------------------------------------
// time-zone

/// time:u's time zone service as a game's clock and calendar reach it: the location names a
/// settings screen lists, the device's location and when it was set, and a local date turned
/// back into the time it came from under the device's rule.
static void RunTimeZone(void) {
    char detail[480];
    Service timesrv, user, zones;
    memset(&timesrv, 0, sizeof(timesrv));
    memset(&user, 0, sizeof(user));
    memset(&zones, 0, sizeof(zones));
    Result rc = smGetService(&timesrv, "time:u");
    if (R_SUCCEEDED(rc)) {
        rc = serviceDispatch(&timesrv, 0, .out_num_objects = 1, .out_objects = &user, );
    }
    if (R_SUCCEEDED(rc)) {
        rc = serviceDispatch(&timesrv, 3, .out_num_objects = 1, .out_objects = &zones, );
    }
    if (R_FAILED(rc)) {
        Skip(SERVICE_TZ_LIST, REPORT_REASON_UNAVAILABLE, "time:u handed out no time zone service "
             "(0x%x)", (unsigned)rc);
        Skip(SERVICE_TZ_UPDATED, REPORT_REASON_UNAVAILABLE, "time:u handed out no time zone "
             "service (0x%x)", (unsigned)rc);
        Skip(SERVICE_TZ_BACK, REPORT_REASON_UNAVAILABLE, "time:u handed out no time zone service "
             "(0x%x)", (unsigned)rc);
        serviceClose(&user);
        serviceClose(&timesrv);
        return;
    }
    TimeLocationName device;
    memset(&device, 0, sizeof(device));
    const uint32_t device_rc = serviceDispatchOut(&zones, 0, device);
    if (Start(SERVICE_TZ_LIST)) {
        static ServiceTzList seen;
        memset(&seen, 0, sizeof(seen));
        seen.name_rc = device_rc;
        memcpy(seen.device, device.name, sizeof(device.name));
        s32 count = -1;
        seen.count_rc = serviceDispatchOut(&zones, 2, count);
        seen.count = count;
        static TimeLocationName names[SERVICE_TZ_CHUNK];
        for (uint32_t ask = 0; ask < SERVICE_TZ_ASKS && seen.count_rc == 0u; ++ask) {
            memset(names, 0, sizeof(names));
            const s32 index = seen.listed;
            s32 got = -1;
            const uint32_t list_rc = serviceDispatchInOut(
                &zones, 3, index, got,
                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                .buffers = { { names, sizeof(names) } }, );
            if (list_rc != 0u || got < 0 || got > (s32)SERVICE_TZ_CHUNK) {
                seen.list_rc = list_rc != 0u ? list_rc : 0xFFFFFFFFu;
                break;
            }
            for (s32 i = 0; i < got; ++i) {
                seen.empty += names[i].name[0] == '\0' ? 1u : 0u;
                seen.found = seen.found || (device.name[0] != '\0' &&
                                            strncmp(names[i].name, device.name,
                                                    sizeof(device.name)) == 0);
            }
            seen.listed += got;
            if (got < (s32)SERVICE_TZ_CHUNK) {
                break;
            }
        }
        const bool passed = ServiceJudgeTzList(&seen, detail, sizeof(detail));
        Report(SERVICE_TZ_LIST, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_TZ_UPDATED)) {
        ServiceTzUpdated seen;
        memset(&seen, 0, sizeof(seen));
        seen.name_rc = device_rc;
        memcpy(seen.name, device.name, sizeof(device.name));
        // The name, then the time point at the next eight-byte boundary.
        struct {
            TimeLocationName name;
            u8 pad[4];
            TimeSteadyClockTimePoint point;
        } updated;
        memset(&updated, 0, sizeof(updated));
        seen.updated_rc = serviceDispatchOut(&zones, 6, updated);
        memcpy(seen.updated, updated.name.name, sizeof(updated.name.name));
        seen.seconds = updated.point.time_point;
        memcpy(seen.source, &updated.point.source_id, sizeof(seen.source));
        const bool passed = ServiceJudgeTzUpdated(&seen, detail, sizeof(detail));
        Report(SERVICE_TZ_UPDATED, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_TZ_BACK)) {
        ServiceTzBack seen;
        memset(&seen, 0, sizeof(seen));
        seen.now_rc = serviceDispatchOut(&user, 0, seen.now);
        struct {
            ServiceCalendarTime date;
            ServiceCalendarInfo info;
        } mine;
        memset(&mine, 0, sizeof(mine));
        seen.date_rc = serviceDispatchInOut(&zones, 101, seen.now, mine);
        s32 count = -1;
        s64 times[2] = { -1, -1 };
        seen.back_rc = serviceDispatchInOut(
            &zones, 202, mine.date, count,
            .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_Out },
            .buffers = { { times, sizeof(times) } }, );
        seen.count = count;
        seen.times[0] = times[0];
        seen.times[1] = times[1];
        struct {
            char v[0x10];
        } version;
        memset(&version, 0, sizeof(version));
        seen.version_rc = serviceDispatchOut(&zones, 5, version);
        memcpy(seen.version, version.v, sizeof(version.v));
        const bool passed = ServiceJudgeTzBack(&seen, detail, sizeof(detail));
        Report(SERVICE_TZ_BACK, CHECK_EXACT, passed, detail);
    }
    serviceClose(&zones);
    serviceClose(&user);
    serviceClose(&timesrv);
}

// ---------------------------------------------------------------------------------------------
// time-perm

/// time:u, the time service an application has, asked to write what only the system may: a
/// clock, the clock settings and the device's location. Each write asks for what is there
/// already, so a machine that takes it changes nothing a later suite reads.
static void RunTimePerm(void) {
    char detail[480];
    Service timesrv, user, network, steady, zones;
    memset(&timesrv, 0, sizeof(timesrv));
    memset(&user, 0, sizeof(user));
    memset(&network, 0, sizeof(network));
    memset(&steady, 0, sizeof(steady));
    memset(&zones, 0, sizeof(zones));
    Result rc = smGetService(&timesrv, "time:u");
    if (R_SUCCEEDED(rc)) {
        rc = serviceDispatch(&timesrv, 0, .out_num_objects = 1, .out_objects = &user, );
    }
    if (R_SUCCEEDED(rc)) {
        rc = serviceDispatch(&timesrv, 1, .out_num_objects = 1, .out_objects = &network, );
    }
    if (R_SUCCEEDED(rc)) {
        rc = serviceDispatch(&timesrv, 2, .out_num_objects = 1, .out_objects = &steady, );
    }
    if (R_SUCCEEDED(rc)) {
        rc = serviceDispatch(&timesrv, 3, .out_num_objects = 1, .out_objects = &zones, );
    }
    if (R_FAILED(rc)) {
        Skip(SERVICE_TIME_PERM_CLOCKS, REPORT_REASON_UNAVAILABLE,
             "time:u handed out no clocks (0x%x)", (unsigned)rc);
        Skip(SERVICE_TIME_PERM_SETTINGS, REPORT_REASON_UNAVAILABLE,
             "time:u handed out no clocks (0x%x)", (unsigned)rc);
    } else {
        if (Start(SERVICE_TIME_PERM_CLOCKS)) {
            ServiceTimeWrites seen;
            memset(&seen, 0, sizeof(seen));
            seen.count = SERVICE_TIME_CLOCK_WRITES;
            s64 before = 0, after = 0, network_now = 0, offset = 0;
            TimeSystemClockContext context;
            memset(&context, 0, sizeof(context));
            const uint32_t read_rc = serviceDispatchOut(&user, 0, before);
            serviceDispatchOut(&user, 2, context);
            serviceDispatchOut(&network, 0, network_now);
            serviceDispatchOut(&steady, 2, offset);
            seen.rc[0] = serviceDispatchIn(&user, 1, before);
            seen.rc[1] = serviceDispatchIn(&user, 3, context);
            seen.rc[2] = serviceDispatchIn(&network, 1, network_now);
            seen.rc[3] = serviceDispatchIn(&steady, 3, offset);
            const uint32_t after_rc = serviceDispatchOut(&user, 0, after);
            seen.same = read_rc == 0u && after_rc == 0u && after >= before && after - before <= 2;
            const bool passed = ServiceJudgeTimeClocks(&seen, detail, sizeof(detail));
            Report(SERVICE_TIME_PERM_CLOCKS, CHECK_EXACT, passed, detail);
        }
        if (Start(SERVICE_TIME_PERM_SETTINGS)) {
            ServiceTimeWrites seen;
            memset(&seen, 0, sizeof(seen));
            seen.count = SERVICE_TIME_SETTING_WRITES;
            u8 automatic = 0xEEu, automatic_after = 0xEEu;
            TimeLocationName name, name_after;
            memset(&name, 0, sizeof(name));
            memset(&name_after, 0, sizeof(name_after));
            const uint32_t automatic_rc = serviceDispatchOut(&timesrv, 100, automatic);
            const uint32_t name_rc = serviceDispatchOut(&zones, 0, name);
            const u8 same_automatic = automatic & 1u;
            seen.rc[0] = serviceDispatchIn(&timesrv, 101, same_automatic);
            const s64 no_offset = 0;
            seen.rc[1] = serviceDispatchIn(&timesrv, 50, no_offset);
            seen.rc[2] = serviceDispatchIn(&zones, 1, name);
            const uint32_t automatic_after_rc = serviceDispatchOut(&timesrv, 100, automatic_after);
            const uint32_t name_after_rc = serviceDispatchOut(&zones, 0, name_after);
            seen.same = automatic_rc == 0u && name_rc == 0u && automatic_after_rc == 0u &&
                        name_after_rc == 0u && (automatic_after & 1u) == same_automatic &&
                        memcmp(&name, &name_after, sizeof(name)) == 0;
            const bool passed = ServiceJudgeTimeSettings(&seen, detail, sizeof(detail));
            Report(SERVICE_TIME_PERM_SETTINGS, CHECK_EXACT, passed, detail);
        }
    }
    serviceClose(&zones);
    serviceClose(&steady);
    serviceClose(&network);
    serviceClose(&user);
    serviceClose(&timesrv);
}

// ---------------------------------------------------------------------------------------------
// audout

/// The samples the audout suite appends: two buffers of silence, a 21 ms frame of 48 kHz stereo
/// each, page-aligned as the service maps them.
static s16 g_audout_pcm[2][0x800] __attribute__((aligned(0x1000)));
static AudioOutBuffer g_audout_buffers[2];

/// GetReleasedAudioOutBuffersAuto (8): the tags released, as many as fit.
static Result AudoutReleased(uint64_t *tags, uint32_t *count) {
    memset(tags, 0, SERVICE_AUDOUT_TAGS * sizeof(*tags));
    *count = 0xEEEEu;
    return serviceDispatchOut(audoutGetServiceSession_AudioOut(), 8, *count,
                              .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
                              .buffers = { { tags, SERVICE_AUDOUT_TAGS * sizeof(*tags) } }, );
}

/// The two silent buffers appended, as libnx appends them: each tagged with its own address.
static Result AudoutAppendBoth(void) {
    memset(g_audout_pcm, 0, sizeof(g_audout_pcm));
    Result rc = 0;
    for (uint32_t i = 0; i < 2u && R_SUCCEEDED(rc); ++i) {
        memset(&g_audout_buffers[i], 0, sizeof(g_audout_buffers[i]));
        g_audout_buffers[i].buffer = g_audout_pcm[i];
        g_audout_buffers[i].buffer_size = sizeof(g_audout_pcm[i]);
        g_audout_buffers[i].data_size = sizeof(g_audout_pcm[i]);
        rc = audoutAppendAudioOutBuffer(&g_audout_buffers[i]);
    }
    return rc;
}

/// audout, the plain PCM output a game without a renderer plays through (a homebrew, a video
/// player, many ports): the outputs listed and one opened, its volume, and the buffers appended,
/// held, flushed, played and given back around Start and Stop.
static void RunAudout(void) {
    char detail[480];
    const Result opened = audoutInitialize();
    if (R_FAILED(opened)) {
        Skip(SERVICE_AUDOUT_OPENS, REPORT_REASON_UNAVAILABLE, "audoutInitialize returned 0x%x",
             (unsigned)opened);
        Skip(SERVICE_AUDOUT_STOPPED, REPORT_REASON_UNAVAILABLE, "audoutInitialize returned 0x%x",
             (unsigned)opened);
        Skip(SERVICE_AUDOUT_STARTED, REPORT_REASON_UNAVAILABLE, "audoutInitialize returned 0x%x",
             (unsigned)opened);
        return;
    }
    if (Start(SERVICE_AUDOUT_OPENS)) {
        ServiceAudoutOpen seen;
        memset(&seen, 0, sizeof(seen));
        static char names[4][0x100];
        memset(names, 0, sizeof(names));
        seen.list_rc = audoutListAudioOuts(names[0], 4, &seen.listed);
        memcpy(seen.name, names[0], sizeof(seen.name) - 1u);
        // OpenAudioOutAuto (3) of a name audout does not have, as libnx's audoutOpenAudioOut asks.
        static char other_in[0x100], other_out[0x100];
        memset(other_in, 0, sizeof(other_in));
        memset(other_out, 0, sizeof(other_out));
        strcpy(other_in, "NoSuchOut");
        const struct {
            u32 sample_rate;
            u32 channel_count;
            u64 aruid;
        } in = { 48000, 2, appletGetAppletResourceUserId() };
        struct {
            u32 sample_rate, channel_count, format, state;
        } got;
        Service other;
        memset(&other, 0, sizeof(other));
        seen.other_rc = serviceDispatchInOut(
            audoutGetServiceSession(), 3, in, got,
            .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
                              SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
            .buffers = { { other_in, sizeof(other_in) }, { other_out, sizeof(other_out) } },
            .in_send_pid = true, .in_num_handles = 1, .in_handles = { CUR_PROCESS_HANDLE },
            .out_num_objects = 1, .out_objects = &other, );
        if (R_SUCCEEDED(seen.other_rc)) {
            serviceClose(&other);
        }
        seen.rate = audoutGetSampleRate();
        seen.channels = audoutGetChannelCount();
        seen.format = (uint32_t)audoutGetPcmFormat();
        seen.state = (uint32_t)audoutGetDeviceState();
        float volume = -1.0f, after = -1.0f;
        seen.volume_rc = audoutGetAudioOutVolume(&volume);
        seen.set_rc = audoutSetAudioOutVolume(0.5f);
        seen.after_rc = audoutGetAudioOutVolume(&after);
        audoutSetAudioOutVolume(volume >= 0.0f && volume <= 1.0f ? volume : 1.0f);
        seen.volume = (int32_t)(volume * 1000.0f + (volume < 0.0f ? -0.5f : 0.5f));
        seen.after = (int32_t)(after * 1000.0f + (after < 0.0f ? -0.5f : 0.5f));
        const bool passed = ServiceJudgeAudoutOpen(&seen, detail, sizeof(detail));
        Report(SERVICE_AUDOUT_OPENS, CHECK_EXACT, passed, detail);
    }
    bool appended = false;
    if (Start(SERVICE_AUDOUT_STOPPED)) {
        ServiceAudoutStopped seen;
        memset(&seen, 0, sizeof(seen));
        seen.append_rc = AudoutAppendBoth();
        appended = R_SUCCEEDED(seen.append_rc);
        seen.count_rc = audoutGetAudioOutBufferCount(&seen.count);
        seen.contains_rc = audoutContainsAudioOutBuffer(&g_audout_buffers[1], &seen.held);
        static AudioOutBuffer never;
        if (R_SUCCEEDED(seen.contains_rc)) {
            seen.contains_rc = audoutContainsAudioOutBuffer(&never, &seen.other);
        }
        uint64_t tags[SERVICE_AUDOUT_TAGS];
        seen.released_rc = AudoutReleased(tags, &seen.released);
        seen.flush_rc = audoutFlushAudioOutBuffers(&seen.flushed);
        seen.after_rc = audoutGetAudioOutBufferCount(&seen.after);
        const bool passed = ServiceJudgeAudoutStopped(&seen, detail, sizeof(detail));
        Report(SERVICE_AUDOUT_STOPPED, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_AUDOUT_STARTED)) {
        ServiceAudoutStarted seen;
        memset(&seen, 0, sizeof(seen));
        uint32_t count = 0;
        if (!appended || R_FAILED(audoutGetAudioOutBufferCount(&count)) || count != 2u) {
            // The case above did not leave its two buffers: a flush took them, or it did not run.
            uint64_t drop[SERVICE_AUDOUT_TAGS];
            uint32_t dropped = 0;
            AudoutReleased(drop, &dropped);
            AudoutAppendBoth();
        }
        seen.first = (uint64_t)(uintptr_t)&g_audout_buffers[0];
        seen.second = (uint64_t)(uintptr_t)&g_audout_buffers[1];
        AudioOutState state = (AudioOutState)0xEE;
        seen.start_rc = audoutStartAudioOut();
        seen.state_rc = audoutGetAudioOutState(&state);
        seen.started = (uint32_t)state;
        seen.again_rc = audoutStartAudioOut();
        seen.stop_rc = audoutStopAudioOut();
        state = (AudioOutState)0xEE;
        if (R_SUCCEEDED(seen.state_rc)) {
            seen.state_rc = audoutGetAudioOutState(&state);
        }
        seen.stopped = (uint32_t)state;
        seen.released_rc = AudoutReleased(seen.tags, &seen.released);
        audoutGetAudioOutBufferCount(&seen.count);
        const bool passed = ServiceJudgeAudoutStarted(&seen, detail, sizeof(detail));
        Report(SERVICE_AUDOUT_STARTED, CHECK_EXACT, passed, detail);
    }
    audoutStopAudioOut();
    audoutExit();
}

// ---------------------------------------------------------------------------------------------
// fs-file

#define FILE_PARENT "/edenprobe/service/file"

/// A file of SIZE bytes made at PATH on the card with the bytes given, or none. It is made at its
/// size and written inside it: a write past the end needs a file opened to append.
static Result FileMake(FsFileSystem *card, const char *path, const void *bytes, s64 size) {
    Result rc = fsFsCreateFile(card, path, size, 0);
    if (R_FAILED(rc) || size == 0) {
        return rc;
    }
    FsFile f;
    rc = fsFsOpenFile(card, path, FsOpenMode_Write, &f);
    if (R_SUCCEEDED(rc)) {
        rc = fsFileWrite(&f, 0, bytes, (u64)size, FsWriteOption_Flush);
        fsFileClose(&f);
    }
    return rc;
}

/// A game's own files on the memory card, as a save writer handles them: a file resized, a file
/// renamed into place over nothing and not over another, a directory cleaned, a file's time stamp,
/// and the card's free space. Everything is made under FILE_PARENT and deleted with it.
static void RunFsFile(void) {
    char detail[480];
    FsFileSystem *const card = fsdevGetDeviceFileSystem("sdmc");
    if (card == NULL) {
        const char *const why = "the memory card is not mounted";
        Skip(SERVICE_FILE_SIZE, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_FILE_RENAME, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_FILE_CLEAN, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_FILE_STAMP, REPORT_REASON_UNAVAILABLE, "%s", why);
        Skip(SERVICE_FILE_SPACE, REPORT_REASON_UNAVAILABLE, "%s", why);
        return;
    }
    fsFsCreateDirectory(card, "/edenprobe");
    fsFsCreateDirectory(card, "/edenprobe/service");
    fsFsDeleteDirectoryRecursively(card, FILE_PARENT);
    const Result parent_rc = fsFsCreateDirectory(card, FILE_PARENT);
    static const char bytes[4] = { 'a', 'b', 'c', 'd' };
    if (Start(SERVICE_FILE_SIZE)) {
        ServiceFileSize seen;
        memset(&seen, 0, sizeof(seen));
        seen.make_rc =
            R_FAILED(parent_rc) ? parent_rc : fsFsCreateFile(card, FILE_PARENT "/size", 4, 0);
        FsFile f;
        if (R_SUCCEEDED(seen.make_rc)) {
            seen.make_rc = fsFsOpenFile(card, FILE_PARENT "/size",
                                        FsOpenMode_Read | FsOpenMode_Write, &f);
        }
        if (R_SUCCEEDED(seen.make_rc)) {
            uint8_t back[16];
            seen.write_rc = fsFileWrite(&f, 0, bytes, sizeof(bytes), FsWriteOption_Flush);
            seen.grow_rc = fsFileSetSize(&f, 16);
            seen.grown_rc = fsFileGetSize(&f, &seen.grown);
            memset(back, 0xEE, sizeof(back));
            if (R_SUCCEEDED(seen.grown_rc)) {
                seen.grown_rc = fsFileRead(&f, 0, back, sizeof(back), FsReadOption_None,
                                           &seen.grown_read);
            }
            static const uint8_t zeros[12] = { 0 };
            seen.grown_bytes = memcmp(back, bytes, 4) == 0 && memcmp(back + 4, zeros, 12) == 0;
            seen.cut_rc = fsFileSetSize(&f, 2);
            seen.cut_size_rc = fsFileGetSize(&f, &seen.cut);
            memset(back, 0xEE, sizeof(back));
            if (R_SUCCEEDED(seen.cut_size_rc)) {
                seen.cut_size_rc = fsFileRead(&f, 0, back, sizeof(back), FsReadOption_None,
                                              &seen.cut_read);
            }
            seen.cut_bytes = memcmp(back, bytes, 2) == 0;
            fsFileClose(&f);
        }
        const bool passed = ServiceJudgeFileSize(&seen, detail, sizeof(detail));
        Report(SERVICE_FILE_SIZE, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_FILE_RENAME)) {
        ServiceFileRename seen;
        memset(&seen, 0, sizeof(seen));
        seen.make_rc =
            R_FAILED(parent_rc) ? parent_rc : FileMake(card, FILE_PARENT "/old", bytes, 4);
        if (R_SUCCEEDED(seen.make_rc)) {
            seen.make_rc = FileMake(card, FILE_PARENT "/other", NULL, 0);
        }
        seen.rename_rc = fsFsRenameFile(card, FILE_PARENT "/old", FILE_PARENT "/new");
        FsDirEntryType type = (FsDirEntryType)0xEE;
        seen.old_rc = fsFsGetEntryType(card, FILE_PARENT "/old", &type);
        type = (FsDirEntryType)0xEE;
        seen.new_rc = fsFsGetEntryType(card, FILE_PARENT "/new", &type);
        seen.new_type = (uint32_t)type;
        FsFile f;
        seen.size = -1;
        seen.size_rc = fsFsOpenFile(card, FILE_PARENT "/new", FsOpenMode_Read, &f);
        if (R_SUCCEEDED(seen.size_rc)) {
            seen.size_rc = fsFileGetSize(&f, &seen.size);
            fsFileClose(&f);
        }
        seen.onto_rc = fsFsRenameFile(card, FILE_PARENT "/other", FILE_PARENT "/new");
        type = (FsDirEntryType)0xEE;
        seen.other_rc = fsFsGetEntryType(card, FILE_PARENT "/other", &type);
        seen.other_type = (uint32_t)type;
        const bool passed = ServiceJudgeFileRename(&seen, detail, sizeof(detail));
        Report(SERVICE_FILE_RENAME, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_FILE_CLEAN)) {
        ServiceFileClean seen;
        memset(&seen, 0, sizeof(seen));
        seen.make_rc =
            R_FAILED(parent_rc) ? parent_rc : fsFsCreateDirectory(card, FILE_PARENT "/d");
        if (R_SUCCEEDED(seen.make_rc)) {
            seen.make_rc = FileMake(card, FILE_PARENT "/d/f", bytes, 4);
        }
        seen.delete_rc = fsFsDeleteDirectory(card, FILE_PARENT "/d");
        FsDirEntryType type = (FsDirEntryType)0xEE;
        seen.file_rc = fsFsGetEntryType(card, FILE_PARENT "/d/f", &type);
        seen.file_type = (uint32_t)type;
        seen.clean_rc = fsFsCleanDirectoryRecursively(card, FILE_PARENT "/d");
        type = (FsDirEntryType)0xEE;
        seen.dir_rc = fsFsGetEntryType(card, FILE_PARENT "/d", &type);
        seen.dir_type = (uint32_t)type;
        FsDir dir;
        seen.count = -1;
        seen.count_rc = fsFsOpenDirectory(card, FILE_PARENT "/d",
                                          FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir);
        if (R_SUCCEEDED(seen.count_rc)) {
            seen.count_rc = fsDirGetEntryCount(&dir, &seen.count);
            fsDirClose(&dir);
        }
        seen.again_rc = fsFsDeleteDirectory(card, FILE_PARENT "/d");
        const bool passed = ServiceJudgeFileClean(&seen, detail, sizeof(detail));
        Report(SERVICE_FILE_CLEAN, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_FILE_STAMP)) {
        ServiceFileStamp seen;
        memset(&seen, 0, sizeof(seen));
        seen.clock_rc = timeGetCurrentTime(TimeType_UserSystemClock, &seen.now);
        seen.make_rc =
            R_FAILED(parent_rc) ? parent_rc : FileMake(card, FILE_PARENT "/stamp", bytes, 4);
        FsTimeStampRaw stamp;
        memset(&stamp, 0, sizeof(stamp));
        seen.stamp_rc = fsFsGetFileTimeStampRaw(card, FILE_PARENT "/stamp", &stamp);
        seen.created = stamp.created;
        seen.modified = stamp.modified;
        seen.accessed = stamp.accessed;
        const bool passed = ServiceJudgeFileStamp(&seen, detail, sizeof(detail));
        Report(SERVICE_FILE_STAMP, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_FILE_SPACE)) {
        s64 free_size = -1, total_size = -1;
        const uint32_t free_rc = fsFsGetFreeSpace(card, "/", &free_size);
        const uint32_t total_rc = fsFsGetTotalSpace(card, "/", &total_size);
        const bool passed = ServiceJudgeFileSpace(free_rc, free_size, total_rc, total_size, detail,
                                                  sizeof(detail));
        Report(SERVICE_FILE_SPACE, CHECK_EXACT, passed, detail);
    }
    fsFsDeleteDirectoryRecursively(card, FILE_PARENT);
}

// ---------------------------------------------------------------------------------------------
// acc-app

/// The thumbnail the acc-app suite stores: a blank 256x144 RGBA frame.
static u8 g_acc_thumbnail[SERVICE_ACC_THUMBNAIL_SIZE];

/// StoreSaveDataThumbnail (110): the user, and the thumbnail by a mapped buffer.
static Result AccStoreThumbnail(Service *s, const AccountUid *uid, size_t size) {
    return serviceDispatchIn(s, 110, *uid,
                             .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
                             .buffers = { { g_acc_thumbnail, size } }, );
}

/// acc:u0 as an application's save code asks it: the users it may pick from, and the thumbnail it
/// stores with a save, at the size the account service takes and at others.
static void RunAccApp(void) {
    char detail[480];
    char said[200];
    Account a;
    const Result opened = AccountOpen(&a, said, sizeof(said));
    if (R_FAILED(opened)) {
        Skip(SERVICE_ACC_QUALIFIED, REPORT_REASON_UNAVAILABLE, "%s", said);
        Skip(SERVICE_ACC_THUMBNAIL, REPORT_REASON_UNAVAILABLE, "%s", said);
        return;
    }
    if (Start(SERVICE_ACC_QUALIFIED)) {
        ServiceAccQualified seen;
        memset(&seen, 0, sizeof(seen));
        AccountUid all[SERVICE_ACC_USERS], qualified[SERVICE_ACC_USERS];
        memset(all, 0, sizeof(all));
        memset(qualified, 0xEE, sizeof(qualified));
        seen.all_rc = serviceDispatch(
            a.service, 2, .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_Out },
            .buffers = { { all, sizeof(all) } }, );
        seen.qualified_rc = serviceDispatch(
            a.service, 141, .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_Out },
            .buffers = { { qualified, sizeof(qualified) } }, );
        for (uint32_t i = 0; i < SERVICE_ACC_USERS; ++i) {
            seen.users += accountUidIsValid(&all[i]) ? 1u : 0u;
        }
        seen.same = memcmp(all, qualified, sizeof(all)) == 0;
        const bool passed = ServiceJudgeAccQualified(&seen, detail, sizeof(detail));
        Report(SERVICE_ACC_QUALIFIED, CHECK_EXACT, passed, detail);
    }
    if (Start(SERVICE_ACC_THUMBNAIL)) {
        ServiceAccThumbnail seen;
        memset(&seen, 0, sizeof(seen));
        memset(g_acc_thumbnail, 0, sizeof(g_acc_thumbnail));
        AccountUid uid, nobody;
        memset(&nobody, 0, sizeof(nobody));
        const char *how = "";
        seen.user_rc = AccountUser(&a, &uid, &how);
        seen.stored_rc = AccStoreThumbnail(a.service, &uid, SERVICE_ACC_THUMBNAIL_SIZE);
        seen.short_rc = AccStoreThumbnail(a.service, &uid, 0x1000);
        seen.nobody_rc = AccStoreThumbnail(a.service, &nobody, SERVICE_ACC_THUMBNAIL_SIZE);
        const bool passed = ServiceJudgeAccThumbnail(&seen, detail, sizeof(detail));
        const size_t n = strlen(detail);
        snprintf(detail + n, sizeof(detail) - n, "; the user by %s", how);
        Report(SERVICE_ACC_THUMBNAIL, CHECK_EXACT, passed, detail);
    }
    AccountClose(&a);
}

void RunService(void) {
    RunHwopus();
    RunAm();
    RunFsp();
    RunAccount();
    RunLdn();
    RunSet();
    RunCsrng();
    RunApm();
    RunNfp();
    RunAoc();
    RunTime();
    RunPl();
    RunProfile();
    RunAmState();
    RunAmSelf();
    RunAmFunctions();
    RunAuddev();
    RunFsdir();
    RunAmAudio();
    RunAmChannel();
    RunAmStorage();
    RunMii();
    RunAmMode();
    RunBsdLocal();
    RunIpc();
    RunHid();
    RunTimeSnap();
    RunVi();
    RunRenderer();
    RunNgc();
    RunAmLock();
    RunSetSys();
    RunTimeZone();
    RunTimePerm();
    RunAudout();
    RunFsFile();
    RunAccApp();
}
