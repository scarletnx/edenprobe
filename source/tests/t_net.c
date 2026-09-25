// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <switch.h>
#include <switch/services/sfdnsres.h>

#include "../check.h"
#include "../netref.h"
#include "../report.h"
#include "tests.h"

// The net family: the services a program reaches the network through, asked the way a game asks
// them, and judged against what the protocol says and what sdmc:/edenprobe/net says the backend
// publishes. Nothing here is compared to a previous run. Every case works its expectation out
// before it asks, and says both on its line, pass or fail.
//
// Three kinds of case, and the file decides which run:
// - the ones any machine answers the same way, with or without a backend: loopback sockets, a
//   dotted address resolving to itself, a fresh connection's defaults. Always run.
// - the ones that need a backend the file names: a game server (`server`) or an online session
//   (`session`, the contract in reden's docs/OnlineService.md). Skipped with the reason when the
//   file names none, and counted apart: a skip is not a pass.
// - a service this program cannot open at all. Every case that needed it is skipped, with the
//   libnx call that failed and what it returned.

static NetConfig config;

// The skip reasons, one sentence each, so a report reads the same way wherever it skipped.
static const char *const NO_ONLINE =
    "nothing says this machine should be online: sdmc:/edenprobe/net names no session and no "
    "server";
static const char *const NO_SESSION =
    "needs an online session: sdmc:/edenprobe/net names none (tools/net-fixture.sh writes one; "
    "on a console this would ask the platform's own servers)";
static const char *const NO_SERVER = "needs a game server: sdmc:/edenprobe/net names none";

/// A case whose line carries its expectation and its answer whether it passed or not.
static void Case(const char *suite, const char *name, CheckTier tier, bool passed,
                 const char *fmt, ...) __attribute__((format(printf, 5, 6)));
static void Case(const char *suite, const char *name, CheckTier tier, bool passed,
                 const char *fmt, ...) {
    char detail[480];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    ReportCase(suite, name, tier, passed, detail);
}

static void Skip(const char *suite, const char *name, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static void Skip(const char *suite, const char *name, const char *fmt, ...) {
    char reason[480];
    va_list args;
    va_start(args, fmt);
    vsnprintf(reason, sizeof(reason), fmt, args);
    va_end(args);
    ReportSkip(suite, name, reason);
}

static const char *ResultText(Result rc) {
    static char text[4][48];
    static unsigned next;
    char *out = text[next++ % 4u];
    NetResultText(out, sizeof(text[0]), rc);
    return out;
}

/// A string a service handed back, fit for a report line: what is not printable ASCII becomes
/// '?', so a garbled answer shows as garbled rather than as bytes that break the file's UTF-8.
static const char *Visible(const char *in, size_t max) {
    static char text[4][200];
    static unsigned next;
    char *out = text[next++ % 4u];
    size_t n = 0;
    while (in != NULL && n < max && n + 1u < sizeof(text[0]) && in[n] != '\0') {
        const unsigned char c = (unsigned char)in[n];
        out[n] = c >= 0x20u && c < 0x7Fu ? (char)c : '?';
        ++n;
    }
    out[n] = '\0';
    return out;
}

static uint64_t NowNs(void) {
    return armTicksToNs(armGetSystemTick());
}

static void SleepMs(uint32_t ms) {
    svcSleepThread((int64_t)ms * 1000000ll);
}

static const char *Dotted(const uint8_t ip[4]) {
    static char text[12][20];
    static unsigned next;
    char *out = text[next++ % 12u];
    snprintf(out, sizeof(text[0]), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return out;
}

static const uint8_t LOOPBACK[4] = { 127, 0, 0, 1 };

static void AddressBytes(const struct sockaddr_in *in, uint8_t out[4]) {
    memcpy(out, &in->sin_addr.s_addr, 4);
}

static void LoadConfig(void) {
    static char text[8192];
    FILE *file = fopen(NET_FILE_PATH, "rb");
    if (file == NULL) {
        NetConfigParse(NULL, &config);
    } else {
        const size_t length = fread(text, 1, sizeof(text) - 1u, file);
        fclose(file);
        text[length] = '\0';
        NetConfigParse(text, &config);
    }
    char said[REPORT_NOTE_MAX + 1];
    NetConfigDescribe(said, sizeof(said), &config);
    ReportNote("%s", said);
}

// ---------------------------------------------------------------------------------------------
// nifm

/// Waits for a request to leave OnHold. Asking only for its state goes to the IRequest's command
/// 0 directly, because libnx's own nifmGetRequestState asks for the result as well on the way,
/// and the question here is whether the service moves the request without being asked for that.
static NifmRequestState AwaitRequest(NifmRequest *request, bool ask_result, uint32_t budget_ms,
                                     Result *last_result) {
    const uint64_t deadline = NowNs() + (uint64_t)budget_ms * 1000000ull;
    NifmRequestState state = NifmRequestState_Invalid;
    *last_result = 0;
    for (;;) {
        if (ask_result) {
            if (R_FAILED(nifmGetRequestState(request, &state))) {
                state = NifmRequestState_Invalid;
            }
            *last_result = nifmGetResult(request);
        } else {
            u32 raw = 0;
            const Result asked = serviceDispatchOut(&request->s, 0, raw);
            state = R_SUCCEEDED(asked) ? (NifmRequestState)raw : NifmRequestState_Invalid;
        }
        if ((state == NifmRequestState_Available && (!ask_result || R_SUCCEEDED(*last_result))) ||
            NowNs() >= deadline) {
            return state;
        }
        SleepMs(20);
    }
}

static void RunNifm(void) {
    static const char *const S = "nifm";
    const Result opened = nifmInitialize(NifmServiceType_User);
    char why[200] = "";
    if (R_FAILED(opened)) {
        snprintf(why, sizeof(why),
                 "nifmInitialize(NifmServiceType_User) returned %s: nifm:u cannot be opened "
                 "from this program",
                 ResultText(opened));
        Skip(S, "nifm:u opens", "%s", why);
    } else {
        Case(S, "nifm:u opens", CHECK_NONE, true,
             "expected nifmInitialize(NifmServiceType_User) to succeed; it did");
    }

    // A request nobody submitted is in the state libnx calls Unknown1: not yet submitted.
    static const char *const FRESH = "a request nobody submitted says it was not submitted";
    if (why[0] != '\0') {
        Skip(S, FRESH, "%s", why);
    } else {
        NifmRequest request;
        const Result created = nifmCreateRequest(&request, true);
        NifmRequestState state = NifmRequestState_Invalid;
        const Result asked = R_SUCCEEDED(created) ? nifmGetRequestState(&request, &state) : created;
        Case(S, FRESH, CHECK_EXACT, R_SUCCEEDED(asked) && state == NifmRequestState_Unknown1,
             "expected state %d (not submitted); CreateRequest %s, GetRequestState %s, state %d",
             NifmRequestState_Unknown1, ResultText(created), ResultText(asked), (int)state);
        if (R_SUCCEEDED(created)) {
            nifmRequestClose(&request);
        }
    }

    const char *const online_why = why[0] != '\0'                       ? why
                                   : !NetConfigDeclaresOnline(&config) ? NO_ONLINE
                                                                       : NULL;

    static const char *const STATUS = "a machine with a backend reports its connection connected";
    if (online_why != NULL) {
        Skip(S, STATUS, "%s", online_why);
    } else {
        NifmInternetConnectionType type = 0;
        u32 strength = 0;
        NifmInternetConnectionStatus status = 0;
        const Result rc = nifmGetInternetConnectionStatus(&type, &strength, &status);
        Case(S, STATUS, CHECK_EXACT,
             R_SUCCEEDED(rc) && status == NifmInternetConnectionStatus_Connected &&
                 (type == NifmInternetConnectionType_WiFi ||
                  type == NifmInternetConnectionType_Ethernet),
             "expected status %d (connected) over Wi-Fi (1) or Ethernet (2); got %s, status %d, "
             "type %d, strength %u",
             NifmInternetConnectionStatus_Connected, ResultText(rc), (int)status, (int)type,
             (unsigned)strength);
    }

    static const char *const ADDRESS = "a machine with a backend has an address of its own";
    if (online_why != NULL) {
        Skip(S, ADDRESS, "%s", online_why);
    } else {
        u32 address = 0;
        const Result rc = nifmGetCurrentIpAddress(&address);
        uint8_t bytes[4];
        memcpy(bytes, &address, 4);
        Case(S, ADDRESS, CHECK_EXACT, R_SUCCEEDED(rc) && address != 0u,
             "expected an address other than 0.0.0.0; got %s, %s", ResultText(rc), Dotted(bytes));
    }

    // The service decides a request by itself: libnx documents the request's event as signalled
    // when its state changes, and a program is entitled to wait on that alone. Asking for the
    // result is not what moves it.
    static const char *const UNASKED = "a submitted request is accepted without being asked for "
                                       "its result";
    if (online_why != NULL) {
        Skip(S, UNASKED, "%s", online_why);
    } else {
        NifmRequest request;
        Result rc = nifmCreateRequest(&request, true);
        NifmRequestState state = NifmRequestState_Invalid;
        Result result = 0;
        if (R_SUCCEEDED(rc)) {
            // Submit (command 4) directly too: libnx's nifmRequestSubmit reads the state and
            // the result back on its way out.
            rc = serviceDispatch(&request.s, 4);
            if (R_SUCCEEDED(rc)) {
                state = AwaitRequest(&request, false, 3000, &result);
            }
            nifmRequestClose(&request);
        }
        Case(S, UNASKED, CHECK_EXACT, R_SUCCEEDED(rc) && state == NifmRequestState_Available,
             "expected state %d (available) within 3 s of Submit (command 4; the 3 s is this "
             "program's, unverified on a console), asking only GetRequestState (command 0); "
             "Submit %s, state %d",
             NifmRequestState_Available, ResultText(rc), (int)state);
    }

    // The same request asked for its result as well, which is what most programs do in a loop,
    // and while it stands, the service's own summary of whether any request is accepted.
    static const char *const ASKED = "a submitted request asked for its result is accepted";
    static const char *const ANY = "while a request is accepted, an internet request is accepted";
    if (online_why != NULL) {
        Skip(S, ASKED, "%s", online_why);
        Skip(S, ANY, "%s", online_why);
    } else {
        NifmRequest request;
        Result rc = nifmCreateRequest(&request, true);
        const bool created = R_SUCCEEDED(rc);
        NifmRequestState state = NifmRequestState_Invalid;
        Result result = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
        if (created) {
            rc = nifmRequestSubmit(&request);
            if (R_SUCCEEDED(rc)) {
                state = AwaitRequest(&request, true, 3000, &result);
            }
        }
        const bool accepted = R_SUCCEEDED(rc) && state == NifmRequestState_Available &&
                              R_SUCCEEDED(result);
        Case(S, ASKED, CHECK_EXACT, accepted,
             "expected state %d (available) and GetResult 0 within 3 s; Submit %s, state %d, "
             "GetResult %s",
             NifmRequestState_Available, ResultText(rc), (int)state, ResultText(result));
        if (!accepted) {
            Skip(S, ANY, "the request above was not accepted, so nothing is standing to ask about");
        } else {
            const bool any = nifmIsAnyInternetRequestAccepted(nifmGetClientId());
            Case(S, ANY, CHECK_EXACT, any,
                 "expected IsAnyInternetRequestAccepted to say yes; it said %s",
                 any ? "yes" : "no");
        }
        if (created) {
            nifmRequestClose(&request);
        }
    }

    if (R_SUCCEEDED(opened)) {
        nifmExit();
    }
}

// ---------------------------------------------------------------------------------------------
// dns: sfdnsres and nsd

static const char *const PLACEHOLDER_NAME = NET_PLACEHOLDER_NAME;
static const char *const PLAIN_NAME = "plain.edenprobe.invalid";

/// The fork's sign-in budget (SIGN_IN_TIMEOUT_SECONDS), which the wait below never exceeds.
#define SIGN_IN_WAIT_MS 15000u

/// Empty once the session answered; otherwise the sentence every session case appends.
static char session_late[200];

/// sfdnsres's GetHostByNameRequest, with its answer read by this program's own parser.
static bool HostByName(const char *name, NetHostEnt *out, char *said, size_t said_size) {
    static uint8_t buffer[0x1000];
    u32 h_error = 0;
    u32 error = 0;
    u32 size = 0;
    memset(buffer, 0, sizeof(buffer));
    const Result rc =
        sfdnsresGetHostByNameRequest(0, false, name, &h_error, &error, buffer, sizeof(buffer),
                                     &size);
    const bool parsed = R_SUCCEEDED(rc) && h_error == 0u && size <= sizeof(buffer) &&
                        NetParseHostEnt(buffer, size, out) && out->address_count > 0u;
    snprintf(said, said_size, "%s, h_errno %u, errno %u, %u bytes%s%s", ResultText(rc),
             (unsigned)h_error, (unsigned)error, (unsigned)size, parsed ? ", first " : "",
             parsed ? Dotted(out->addresses[0]) : "");
    return parsed;
}

/// sfdnsres's GetAddrInfoRequest, with no hints, the way libnx itself calls it for NULL hints.
static bool AddrInfo(const char *name, NetAddrList *out, char *said, size_t said_size) {
    static uint8_t buffer[0x1000];
    u32 error = 0;
    s32 ret = 0;
    u32 size = 0;
    memset(buffer, 0, sizeof(buffer));
    const Result rc = sfdnsresGetAddrInfoRequest(0, false, name, NULL, NULL, 0, buffer,
                                                 sizeof(buffer), &error, &ret, &size);
    const bool parsed = R_SUCCEEDED(rc) && ret == 0 && size <= sizeof(buffer) &&
                        NetParseAddrInfo(buffer, size, out) && out->count > 0u;
    snprintf(said, said_size, "%s, ret %d, errno %u, %u bytes%s%s", ResultText(rc), (int)ret,
             (unsigned)error, (unsigned)size, parsed ? ", first " : "",
             parsed ? Dotted(out->addresses[0]) : "");
    return parsed;
}

/// The C library's getaddrinfo, which in libnx is sfdnsres again through its own parser: a
/// second reading of the same answer, so a parser here that agreed with a wrong one cannot pass.
static bool LibcAddress(const char *name, uint8_t out[4], int *error) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    struct addrinfo *found = NULL;
    *error = getaddrinfo(name, NULL, &hints, &found);
    if (*error != 0 || found == NULL || found->ai_addr == NULL) {
        return false;
    }
    AddressBytes((const struct sockaddr_in *)found->ai_addr, out);
    freeaddrinfo(found);
    return true;
}

/// nsd Resolve, command 20: a name in, a name out, both 0x100 bytes.
static Result NsdResolve(Service *nsd, const char *name, char out[0x100]) {
    char in[0x100];
    memset(in, 0, sizeof(in));
    memset(out, 0, 0x100);
    strncpy(in, name, sizeof(in) - 1u);
    return serviceDispatch(nsd, 20,
                           .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
                                             SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
                           .buffers = { { in, sizeof(in) }, { out, 0x100 } }, );
}

static void RunDns(const char *sockets_why) {
    static const char *const S = "dns";
    char said[200];
    char said_2[200];

    // A dotted address is already an address. Nothing is looked up, so every machine answers
    // this one the same, network or not.
    static const char *const DOTTED_HOSTENT = "a dotted address resolves to itself, as a hostent";
    static const char *const DOTTED_ADDRINFO = "a dotted address resolves to itself, as addrinfo";
    static const char *const INVALID = "a name under .invalid does not resolve";
    if (sockets_why != NULL) {
        Skip(S, DOTTED_HOSTENT, "%s", sockets_why);
        Skip(S, DOTTED_ADDRINFO, "%s", sockets_why);
        Skip(S, INVALID, "%s", sockets_why);
    } else {
        uint8_t dotted[4] = { 0 };
        NetExpectAddress(&config, "127.0.0.1", NULL, dotted);
        NetHostEnt host;
        const bool got = HostByName("127.0.0.1", &host, said, sizeof(said));
        Case(S, DOTTED_HOSTENT, CHECK_EXACT, got && memcmp(host.addresses[0], dotted, 4) == 0,
             "expected %s; got %s", Dotted(dotted), said);
        NetAddrList list;
        const bool listed = AddrInfo("127.0.0.1", &list, said, sizeof(said));
        Case(S, DOTTED_ADDRINFO, CHECK_EXACT,
             listed && memcmp(list.addresses[0], dotted, 4) == 0 && list.family[0] == AF_INET,
             "expected %s, family %d; got %s, family %u", Dotted(dotted), AF_INET, said,
             (unsigned)list.family[0]);
        // RFC 6761 section 6.4: no name under .invalid ever resolves, anywhere, and a machine
        // with no network fails it too, so the expectation needs no backend.
        uint8_t address[4] = { 0 };
        int error = 0;
        const bool resolved = LibcAddress("nothing.edenprobe.invalid", address, &error);
        Case(S, INVALID, CHECK_EXACT, !resolved,
             "expected getaddrinfo to fail; it %s (error %d%s%s)",
             resolved ? "resolved" : "failed", error, resolved ? ", to " : "",
             resolved ? Dotted(address) : "");
    }

    Service nsd;
    memset(&nsd, 0, sizeof(nsd));
    const Result nsd_opened = smGetService(&nsd, "nsd:u");
    char nsd_why[200] = "";
    if (R_FAILED(nsd_opened)) {
        snprintf(nsd_why, sizeof(nsd_why),
                 "smGetService(\"nsd:u\") returned %s: nsd cannot be opened from this program",
                 ResultText(nsd_opened));
    }

    // The environment a name's placeholder stands for, as nsd reports it: GetEnvironmentIdentifier
    // fills an eight byte field with a short lower case name and a terminator.
    static const char *const ENVIRONMENT = "nsd names the environment it resolves for";
    char environment[9] = "";
    bool environment_known = false;
    if (nsd_why[0] != '\0') {
        Skip(S, ENVIRONMENT, "%s", nsd_why);
    } else {
        char field[8];
        memset(field, 0x7F, sizeof(field));
        const Result rc = serviceDispatch(
            &nsd, 11,
            .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
            .buffers = { { field, sizeof(field) } }, );
        size_t length = 0;
        while (length < sizeof(field) && field[length] != '\0') {
            ++length;
        }
        bool letters = length > 0u && length < sizeof(field);
        for (size_t i = 0; letters && i < length; ++i) {
            letters = (field[i] >= 'a' && field[i] <= 'z') || (field[i] >= '0' && field[i] <= '9');
        }
        if (letters) {
            memcpy(environment, field, length);
            environment[length] = '\0';
        }
        environment_known = R_SUCCEEDED(rc) && letters;
        Case(S, ENVIRONMENT, CHECK_EXACT, environment_known,
             "expected one to seven lower case letters or digits and a terminator; got %s, "
             "\"%s\"",
             ResultText(rc), letters ? environment : "(not a name)");
    }

    // The fork signs in in the background when the program loads, with a fifteen second budget,
    // so nothing that depends on the session is asked before it answers: nsd is asked about the
    // name the session changes until it gives the session's answer, once per run.
    if (config.has_session && nsd_why[0] == '\0') {
        static bool waited;
        if (!waited) {
            waited = true;
            const char *const probe = NetSessionProbeName(&config);
            const uint64_t begun = NowNs();
            bool answered = false;
            char answer[0x100] = "";
            while (!answered && NowNs() - begun < (uint64_t)SIGN_IN_WAIT_MS * 1000000ull) {
                answered = R_SUCCEEDED(NsdResolve(&nsd, probe, answer)) &&
                           NetSessionAnswered(&config, probe, environment, answer);
                if (!answered) {
                    SleepMs(100);
                }
            }
            const unsigned long long ms = (NowNs() - begun) / 1000000ull;
            if (answered) {
                session_late[0] = '\0';
                ReportNote("net: the session answered for \"%s\" after %llu ms", probe, ms);
            } else {
                snprintf(session_late, sizeof(session_late),
                         "; the session had not answered for \"%.60s\" after %u s, the fork's "
                         "sign-in budget, so this was asked before sign-in finished",
                         probe, SIGN_IN_WAIT_MS / 1000u);
                ReportNote("net: the session did not answer for \"%s\" within %u ms", probe,
                           SIGN_IN_WAIT_MS);
            }
        }
    }

    // nsd turns a name into a name: every % becomes the environment. The fork's contract says
    // this is what the real service does (docs/OnlineService.md, host rewriting, rule 3).
    static const char *const EXPANDED = "nsd replaces the placeholder with the environment";
    char expanded[0x100] = "";
    bool expanded_done = false;
    if (nsd_why[0] != '\0') {
        Skip(S, EXPANDED, "%s", nsd_why);
    } else if (!environment_known) {
        Skip(S, EXPANDED, "the environment is unknown, so the expansion has no expected value");
    } else {
        NetExpectNsd(&config, PLACEHOLDER_NAME, environment, expanded, sizeof(expanded));
        char got[0x100];
        const Result rc = NsdResolve(&nsd, PLACEHOLDER_NAME, got);
        got[sizeof(got) - 1u] = '\0';
        expanded_done = R_SUCCEEDED(rc) && strcmp(got, expanded) == 0;
        Case(S, EXPANDED, CHECK_EXACT, expanded_done,
             "expected \"%.100s\"; got %s, \"%.100s\"%s", expanded, ResultText(rc),
             Visible(got, 100), session_late);
    }

    // A name with no placeholder and no rewrite is nothing nsd has to change.
    static const char *const PLAIN = "nsd leaves a plain name as it is";
    if (nsd_why[0] != '\0') {
        Skip(S, PLAIN, "%s", nsd_why);
    } else {
        char want[0x100];
        NetExpectNsd(&config, PLAIN_NAME, environment, want, sizeof(want));
        char got[0x100];
        const Result rc = NsdResolve(&nsd, PLAIN_NAME, got);
        got[sizeof(got) - 1u] = '\0';
        Case(S, PLAIN, CHECK_EXACT, R_SUCCEEDED(rc) && strcmp(got, want) == 0,
             "expected \"%.80s\"; got %s, \"%.80s\"", want, ResultText(rc), Visible(got, 80));
    }

    // What the backend publishes: a rewrite for one name, which nsd hands back as the
    // destination and sfdnsres as the destination's address.
    const bool have_rewrite = config.rewrite_count > 0u;
    const NetRewrite *rewrite = &config.rewrites[0];
    static const char *const NSD_REWRITE = "nsd hands back the destination the backend publishes";
    static const char *const HOSTENT_REWRITE =
        "sfdnsres resolves a published name to its destination's address, as a hostent";
    static const char *const ADDRINFO_REWRITE =
        "sfdnsres resolves a published name to its destination's address, as addrinfo and libc";
    const char *const session_why = !config.has_session ? NO_SESSION : NULL;
    const char *const rewrite_why =
        session_why != NULL ? session_why
        : !have_rewrite     ? "the backend publishes no rewrite (no rewrite line in the net file)"
                            : NULL;
    if (nsd_why[0] != '\0' || rewrite_why != NULL) {
        Skip(S, NSD_REWRITE, "%s", nsd_why[0] != '\0' ? nsd_why : rewrite_why);
    } else {
        char want[0x100];
        NetExpectNsd(&config, rewrite->source, environment, want, sizeof(want));
        char got[0x100];
        const Result rc = NsdResolve(&nsd, rewrite->source, got);
        got[sizeof(got) - 1u] = '\0';
        Case(S, NSD_REWRITE, CHECK_EXACT, R_SUCCEEDED(rc) && strcmp(got, want) == 0,
             "expected \"%.80s\" for \"%.80s\"; got %s, \"%.80s\"%s", want, rewrite->source,
             ResultText(rc), Visible(got, 80), session_late);
    }
    const char *const resolve_why = sockets_why != NULL ? sockets_why : rewrite_why;
    if (resolve_why != NULL) {
        Skip(S, HOSTENT_REWRITE, "%s", resolve_why);
        Skip(S, ADDRINFO_REWRITE, "%s", resolve_why);
    } else {
        uint8_t want[4] = { 0 };
        NetExpectAddress(&config, rewrite->source, NULL, want);
        NetHostEnt host;
        const bool got = HostByName(rewrite->source, &host, said, sizeof(said));
        Case(S, HOSTENT_REWRITE, CHECK_EXACT, got && memcmp(host.addresses[0], want, 4) == 0,
             "expected %s for \"%.80s\"; got %s%s", Dotted(want), rewrite->source, said,
             session_late);
        NetAddrList list;
        const bool listed = AddrInfo(rewrite->source, &list, said_2, sizeof(said_2));
        uint8_t libc[4] = { 0 };
        int error = 0;
        const bool libc_got = LibcAddress(rewrite->source, libc, &error);
        Case(S, ADDRINFO_REWRITE, CHECK_EXACT,
             listed && memcmp(list.addresses[0], want, 4) == 0 && libc_got &&
                 memcmp(libc, want, 4) == 0,
             "expected %s from both; addrinfo %s; libc %s (error %d)%s", Dotted(want), said_2,
             libc_got ? Dotted(libc) : "failed", error, session_late);
    }

    // Signed in, a name carrying the placeholder, and the name nsd made of it, go to loopback so
    // a game fails to reach the host its backend stands in for (the contract's rule 4).
    static const char *const PLACEHOLDER = "a name carrying the placeholder resolves to loopback "
                                           "while signed in";
    static const char *const REMEMBERED = "the name nsd expanded resolves to loopback while "
                                          "signed in";
    const char *const intercept_why = sockets_why != NULL ? sockets_why : session_why;
    if (intercept_why != NULL) {
        Skip(S, PLACEHOLDER, "%s", intercept_why);
        Skip(S, REMEMBERED, "%s", intercept_why);
    } else {
        uint8_t want[4] = { 0 };
        NetExpectAddress(&config, PLACEHOLDER_NAME, NULL, want);
        NetHostEnt host;
        const bool got = HostByName(PLACEHOLDER_NAME, &host, said, sizeof(said));
        Case(S, PLACEHOLDER, CHECK_EXACT, got && memcmp(host.addresses[0], want, 4) == 0,
             "expected %s for \"%s\"; got %s%s", Dotted(want), PLACEHOLDER_NAME, said,
             session_late);
        if (!expanded_done) {
            Skip(S, REMEMBERED, "nsd did not expand the placeholder above, so there is no name "
                                "it handed out to ask about");
        } else {
            uint8_t want[4] = { 0 };
            NetExpectAddress(&config, expanded, expanded, want);
            NetHostEnt again;
            const bool got_again = HostByName(expanded, &again, said_2, sizeof(said_2));
            Case(S, REMEMBERED, CHECK_EXACT, got_again && memcmp(again.addresses[0], want, 4) == 0,
                 "expected %s for \"%.100s\"; got %s%s", Dotted(want), expanded, said_2,
                 session_late);
        }
    }

    if (R_SUCCEEDED(nsd_opened)) {
        serviceClose(&nsd);
    }
}

// ---------------------------------------------------------------------------------------------
// bsd

// Nothing here waits on a socket without a budget: every connect is non-blocking and polled,
// every read is polled first, and every exchange over ssl runs in non-blocking mode against a
// deadline. A socket option cannot be the bound, because the one a program would reach for is
// the thing the receive timeout case below asks about, and a machine that ignores it would
// otherwise hang the run on a server that never answers.
#define CONNECT_BUDGET_MS 3000u
#define RECEIVE_BUDGET_MS 2000u

static int OpenSocket(int type) {
    return socket(AF_INET, type, 0);
}

static struct sockaddr_in Loopback(uint16_t port) {
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    memcpy(&address.sin_addr.s_addr, LOOPBACK, 4);
    return address;
}

static bool NameOf(int fd, struct sockaddr_in *out) {
    socklen_t length = sizeof(*out);
    memset(out, 0, sizeof(*out));
    return getsockname(fd, (struct sockaddr *)out, &length) == 0;
}

static bool SameEndpoint(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static const char *Endpoint(const struct sockaddr_in *in) {
    static char text[8][28];
    static unsigned next;
    char *out = text[next++ % 8u];
    uint8_t bytes[4];
    AddressBytes(in, bytes);
    snprintf(out, sizeof(text[0]), "%s:%u", Dotted(bytes), (unsigned)ntohs(in->sin_port));
    return out;
}

/// Waits for @p events on @p fd for at most @p budget_ms: 1 ready, 0 the budget ran out, -1 an
/// error.
static int PollWithin(int fd, short events, uint32_t budget_ms, short *revents) {
    struct pollfd poller = { .fd = fd, .events = events, .revents = 0 };
    const int ready = poll(&poller, 1, (int)budget_ms);
    if (revents != NULL) {
        *revents = poller.revents;
    }
    return ready;
}

/// connect(), bounded: non-blocking, polled for writability for at most @p budget_ms, and judged
/// by the pending error, then put back to blocking. -1 with @p error set when it did not connect;
/// ETIMEDOUT when the budget ran out.
static int ConnectWithin(int fd, const struct sockaddr_in *to, uint32_t budget_ms, int *error) {
    *error = 0;
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        *error = errno;
        return -1;
    }
    errno = 0;
    int rc = connect(fd, (const struct sockaddr *)to, sizeof(*to));
    if (rc != 0 && errno == EINPROGRESS) {
        const int ready = PollWithin(fd, POLLOUT, budget_ms, NULL);
        if (ready <= 0) {
            *error = ready == 0 ? ETIMEDOUT : errno;
        } else {
            int pending = 0;
            socklen_t length = sizeof(pending);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &pending, &length) != 0) {
                *error = errno;
            } else {
                *error = pending;
                rc = pending == 0 ? 0 : -1;
            }
        }
    } else if (rc != 0) {
        *error = errno;
    }
    fcntl(fd, F_SETFL, flags);
    return *error == 0 ? 0 : -1;
}

/// recvfrom(), bounded: polled for at most @p budget_ms first. -1 with ETIMEDOUT in @p error when
/// nothing came.
static ssize_t RecvWithin(int fd, void *buffer, size_t length, struct sockaddr_in *from,
                          socklen_t *from_length, uint32_t budget_ms, int *error) {
    *error = 0;
    const int ready = PollWithin(fd, POLLIN, budget_ms, NULL);
    if (ready <= 0) {
        *error = ready == 0 ? ETIMEDOUT : errno;
        return -1;
    }
    const ssize_t got = recvfrom(fd, buffer, length, MSG_DONTWAIT, (struct sockaddr *)from,
                                 from_length);
    if (got < 0) {
        *error = errno;
    }
    return got;
}

/// @p length bytes from a stream, or what arrived before @p budget_ms ran out.
static ssize_t RecvAllWithin(int fd, uint8_t *buffer, size_t length, uint32_t budget_ms) {
    const uint64_t deadline = NowNs() + (uint64_t)budget_ms * 1000000ull;
    size_t have = 0;
    while (have < length && NowNs() < deadline) {
        const uint32_t left = (uint32_t)((deadline - NowNs()) / 1000000ull);
        int error = 0;
        const ssize_t got = RecvWithin(fd, buffer + have, length - have, NULL, NULL,
                                       left > 0u ? left : 1u, &error);
        if (got <= 0) {
            break;
        }
        have += (size_t)got;
    }
    return (ssize_t)have;
}

/// A stream pair on loopback through a listener this program opens: the client and the accepted
/// end. False with everything closed when any step fails.
static bool LoopbackStream(int *client, int *served, char *said, size_t said_size) {
    *client = -1;
    *served = -1;
    const int listener = OpenSocket(SOCK_STREAM);
    struct sockaddr_in address = Loopback(0);
    struct sockaddr_in bound;
    if (listener < 0 || bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener, 1) != 0 || !NameOf(listener, &bound)) {
        snprintf(said, said_size, "listener: errno %d", errno);
        if (listener >= 0) {
            close(listener);
        }
        return false;
    }
    *client = OpenSocket(SOCK_STREAM);
    int error = 0;
    if (*client < 0 || ConnectWithin(*client, &bound, CONNECT_BUDGET_MS, &error) != 0) {
        snprintf(said, said_size, "connect to %s: errno %d", Endpoint(&bound), error);
        if (*client >= 0) {
            close(*client);
        }
        *client = -1;
        close(listener);
        return false;
    }
    *served = PollWithin(listener, POLLIN, CONNECT_BUDGET_MS, NULL) == 1
                  ? accept(listener, NULL, NULL)
                  : -1;
    close(listener);
    if (*served < 0) {
        snprintf(said, said_size, "accept: errno %d", errno);
        close(*client);
        *client = -1;
        return false;
    }
    return true;
}

static void Pattern(uint8_t *out, size_t length, uint8_t seed) {
    for (size_t i = 0; i < length; ++i) {
        out[i] = (uint8_t)(i * 37u + seed);
    }
}

/// What the rescue thread needs: where to send, when, the socket to shut if a datagram does not
/// end the wait, and whether the receive has come back (set by the receiving thread).
typedef struct {
    struct sockaddr_in to;
    uint32_t after_ms;
    int fd;
    bool returned;
    int fired; // 0 nothing, 1 the datagram, 2 the shutdown
} Rescue;

/// Waits up to @p ms for the receive to come back, in short steps, so a machine that honours
/// the timeout is not held up by the rescue.
static bool RescueWait(Rescue *rescue, uint32_t ms) {
    for (uint32_t waited = 0; waited < ms; waited += 50) {
        if (__atomic_load_n(&rescue->returned, __ATOMIC_ACQUIRE)) {
            return true;
        }
        SleepMs(50);
    }
    return __atomic_load_n(&rescue->returned, __ATOMIC_ACQUIRE);
}

/// Ends a receive that outlives its timeout: a datagram after @p after_ms, and 3 s later a
/// shutdown of the socket, for a machine whose blocking receive does not wake for arriving data
/// either. Either one firing is itself the finding; neither can hang the run.
static void RescueThread(void *argument) {
    Rescue *rescue = argument;
    if (RescueWait(rescue, rescue->after_ms)) {
        return;
    }
    const int fd = OpenSocket(SOCK_DGRAM);
    if (fd >= 0) {
        static const char NOTE[] = "rescue";
        sendto(fd, NOTE, sizeof(NOTE), 0, (const struct sockaddr *)&rescue->to,
               sizeof(rescue->to));
        close(fd);
    }
    rescue->fired = 1;
    if (RescueWait(rescue, 3000)) {
        return;
    }
    rescue->fired = 2;
    shutdown(rescue->fd, SHUT_RDWR);
}

/// A core of the application's (0 to 2) other than this thread's. A machine can hold the
/// blocked thread's core for the whole of its service call, and a rescue queued on that core
/// never runs: Ryujinx 1.3.351 hung the run that way.
static int OtherCore(void) {
    return svcGetCurrentProcessorNumber() == 0 ? 1 : 0;
}

/// SO_RCVTIMEO of 300 ms on an empty datagram socket, then a blocking receive: POSIX says it
/// gives up with EAGAIN once the time has passed. Bounded here by a datagram sent from another
/// thread 2 s in, which, if it is what ends the wait, is itself the finding.
static void CheckReceiveTimeout(const char *suite, const char *name) {
    const int fd = OpenSocket(SOCK_DGRAM);
    struct sockaddr_in any = Loopback(0);
    static Rescue rescue;
    if (fd < 0 || bind(fd, (struct sockaddr *)&any, sizeof(any)) != 0 ||
        !NameOf(fd, &rescue.to)) {
        Case(suite, name, CHECK_EXACT, false, "expected a bound datagram socket; errno %d", errno);
        if (fd >= 0) {
            close(fd);
        }
        return;
    }
    struct timeval timeout = { 0, 300000 };
    errno = 0;
    const int set = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    const int set_error = errno;
    if (set != 0) {
        // Receiving now would wait for ever. The emulator this was written against does exactly
        // this: reden reads the option's 16 byte timeval as a u32 (hle/service/sockets/bsd.cpp,
        // SetSockOptImpl) and passes the host sizeof(u32), which Linux refuses with EINVAL
        // (internal_network/network.cpp), so no receive timeout a program sets ever applies.
        Case(suite, name, CHECK_EXACT, false,
             "expected setsockopt(SO_RCVTIMEO, 300 ms) to succeed and a receive to give up with "
             "EAGAIN between 250 and 1500 ms; setsockopt returned %d, errno %d%s",
             set, set_error,
             set_error == EINVAL ? " (an emulator that reads the timeval as a u32 answers this "
                                   "way: reden sockets/bsd.cpp SetSockOptImpl, "
                                   "internal_network/network.cpp)"
                                 : "");
        close(fd);
        return;
    }
    rescue.after_ms = 2000;
    rescue.fd = fd;
    rescue.returned = false;
    rescue.fired = 0;
    Thread thread;
    Result made = threadCreate(&thread, RescueThread, &rescue, NULL, 0x4000, 0x2C, OtherCore());
    if (R_FAILED(made)) {
        made = threadCreate(&thread, RescueThread, &rescue, NULL, 0x4000, 0x2C, -2);
    }
    const bool started = R_SUCCEEDED(made) && R_SUCCEEDED(threadStart(&thread));
    if (!started) {
        Skip(suite, name, "no thread to bound the receive with (threadCreate %s), and without "
                          "one a machine that ignores the timeout would hang the run",
             ResultText(made));
        if (R_SUCCEEDED(made)) {
            threadClose(&thread);
        }
        close(fd);
        return;
    }
    char buffer[32];
    const uint64_t begun = NowNs();
    errno = 0;
    const ssize_t got = recv(fd, buffer, sizeof(buffer), 0);
    const int error = errno;
    const unsigned long long ms = (NowNs() - begun) / 1000000ull;
    __atomic_store_n(&rescue.returned, true, __ATOMIC_RELEASE);
    threadWaitForExit(&thread);
    threadClose(&thread);
    close(fd);
    Case(suite, name, CHECK_EXACT,
         got < 0 && (error == EAGAIN || error == EWOULDBLOCK) && ms >= 250u && ms <= 1500u,
         "expected -1 with EAGAIN (%d) between 250 and 1500 ms after setsockopt(SO_RCVTIMEO, "
         "300 ms) succeeded; got %d, errno %d, after %llu ms%s",
         EAGAIN, (int)got, error, ms,
         rescue.fired == 2 ? " (a shutdown of the socket 5 s in ended the wait: the receive "
                             "ignored its timeout and the datagram sent to it)"
         : got > 0 ? " (the rescue datagram ended the wait: the timeout was accepted and ignored)"
                   : "");
}

static void RunBsd(const char *sockets_why) {
    static const char *const S = "bsd";
    static const char *const BIND = "two datagram sockets bind to loopback on ports of their own";
    static const char *const DATAGRAM = "a datagram goes to the echo and back whole, from the "
                                        "right addresses";
    static const char *const EMPTY = "an empty datagram socket has nothing to read";
    static const char *const STREAM = "a stream on loopback carries bytes both ways";
    static const char *const REFUSED = "a stream to a port nothing listens on is refused";
    static const char *const TIMEOUT = "a receive timeout gives up with EAGAIN within its time";
    static const char *const SERVER = "a stream connects to the game server";
    static const char *const NONBLOCKING = "a connect that does not block completes with no "
                                           "error";
    if (sockets_why != NULL) {
        const char *const all[] = { BIND,    DATAGRAM, EMPTY,      STREAM,
                                    REFUSED, TIMEOUT,  SERVER, NONBLOCKING };
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
            Skip(S, all[i], "%s", sockets_why);
        }
        return;
    }

    // Two datagram sockets, one the echo this suite opens itself through the same service.
    errno = 0;
    const int a = OpenSocket(SOCK_DGRAM);
    const int b = OpenSocket(SOCK_DGRAM);
    struct sockaddr_in any = Loopback(0);
    struct sockaddr_in a_name;
    struct sockaddr_in b_name;
    const bool bound = a >= 0 && b >= 0 && bind(a, (struct sockaddr *)&any, sizeof(any)) == 0 &&
                       bind(b, (struct sockaddr *)&any, sizeof(any)) == 0 && NameOf(a, &a_name) &&
                       NameOf(b, &b_name);
    const bool distinct = bound && a_name.sin_port != 0 && b_name.sin_port != 0 &&
                          a_name.sin_port != b_name.sin_port &&
                          memcmp(&a_name.sin_addr.s_addr, LOOPBACK, 4) == 0 &&
                          memcmp(&b_name.sin_addr.s_addr, LOOPBACK, 4) == 0;
    Case(S, BIND, CHECK_EXACT, distinct,
         "expected 127.0.0.1 with two different non-zero ports; got %s and %s (errno %d)",
         bound ? Endpoint(&a_name) : "-", bound ? Endpoint(&b_name) : "-", errno);

    if (!distinct) {
        Skip(S, DATAGRAM, "the two datagram sockets above did not come up");
        Skip(S, EMPTY, "the two datagram sockets above did not come up");
    } else {
        uint8_t sent[64];
        uint8_t got[128];
        Pattern(sent, sizeof(sent), 11);
        errno = 0;
        memset(got, 0, sizeof(got));
        const ssize_t wrote =
            sendto(a, sent, sizeof(sent), 0, (struct sockaddr *)&b_name, sizeof(b_name));
        struct sockaddr_in from;
        socklen_t from_length = sizeof(from);
        memset(&from, 0, sizeof(from));
        int read_error = 0;
        const ssize_t read = RecvWithin(b, got, sizeof(got), &from, &from_length,
                                        RECEIVE_BUDGET_MS, &read_error);
        // recvfrom says how long the address it wrote is; a length it left unset is not one to
        // answer with (the echo goes to the whole sockaddr_in, and the case says what came back).
        const bool from_named = NetAddressLengthUsable((size_t)from_length, sizeof(from));
        const bool there = wrote == (ssize_t)sizeof(sent) && read == (ssize_t)sizeof(sent) &&
                           from_named && memcmp(got, sent, sizeof(sent)) == 0 &&
                           SameEndpoint(&from, &a_name);
        // The echo: the second socket sends what it received back to where it came from.
        uint8_t back[128];
        memset(back, 0, sizeof(back));
        const ssize_t echoed = read > 0 && from_named
                                   ? sendto(b, got, (size_t)read, 0, (struct sockaddr *)&from,
                                            sizeof(from))
                                   : -1;
        struct sockaddr_in replier;
        socklen_t replier_length = sizeof(replier);
        memset(&replier, 0, sizeof(replier));
        int back_error = 0;
        const ssize_t returned = echoed > 0 ? RecvWithin(a, back, sizeof(back), &replier,
                                                         &replier_length, RECEIVE_BUDGET_MS,
                                                         &back_error)
                                            : -1;
        const bool home = returned == (ssize_t)sizeof(sent) &&
                          memcmp(back, sent, sizeof(sent)) == 0 &&
                          SameEndpoint(&replier, &b_name);
        Case(S, DATAGRAM, CHECK_EXACT, there && home,
             "expected the same 64 bytes at %s from %s and back at %s from %s, each within %u "
             "ms; out: sent %d, received %d from %s (%s, address length %u, errno %d); back: "
             "sent %d, received %d from %s (%s, errno %d)",
             Endpoint(&b_name), Endpoint(&a_name), Endpoint(&a_name), Endpoint(&b_name),
             RECEIVE_BUDGET_MS, (int)wrote, (int)read, Endpoint(&from),
             there ? "same" : "wrong", (unsigned)from_length, read_error, (int)echoed, (int)returned,
             Endpoint(&replier), home ? "same" : "wrong", back_error);

        errno = 0;
        const ssize_t nothing = recvfrom(b, got, sizeof(got), MSG_DONTWAIT, NULL, NULL);
        const int error = errno;
        Case(S, EMPTY, CHECK_EXACT, nothing < 0 && (error == EAGAIN || error == EWOULDBLOCK),
             "expected -1 with EAGAIN (%d); got %d, errno %d", EAGAIN, (int)nothing, error);
    }
    if (a >= 0) {
        close(a);
    }
    if (b >= 0) {
        close(b);
    }

    {
        int client = -1;
        int served = -1;
        char said[120] = "";
        if (!LoopbackStream(&client, &served, said, sizeof(said))) {
            Case(S, STREAM, CHECK_EXACT, false,
                 "expected a listener, a connect and an accept on loopback; %s", said);
        } else {
            uint8_t up[100];
            uint8_t down[100];
            uint8_t got_up[100];
            uint8_t got_down[100];
            Pattern(up, sizeof(up), 3);
            Pattern(down, sizeof(down), 200);
            memset(got_up, 0, sizeof(got_up));
            memset(got_down, 0, sizeof(got_down));
            const ssize_t sent_up = send(client, up, sizeof(up), 0);
            const ssize_t read_up = RecvAllWithin(served, got_up, sizeof(got_up),
                                                  RECEIVE_BUDGET_MS);
            const ssize_t sent_down = send(served, down, sizeof(down), 0);
            const ssize_t read_down = RecvAllWithin(client, got_down, sizeof(got_down),
                                                    RECEIVE_BUDGET_MS);
            struct sockaddr_in peer;
            socklen_t peer_length = sizeof(peer);
            struct sockaddr_in client_name;
            const bool named = getpeername(served, (struct sockaddr *)&peer, &peer_length) == 0 &&
                               NameOf(client, &client_name);
            Case(S, STREAM, CHECK_EXACT,
                 sent_up == 100 && read_up == 100 && memcmp(up, got_up, 100) == 0 &&
                     sent_down == 100 && read_down == 100 && memcmp(down, got_down, 100) == 0 &&
                     named && SameEndpoint(&peer, &client_name),
                 "expected 100 bytes each way unchanged, and the accepted end's peer to be the "
                 "client; up %d/%d %s, down %d/%d %s, peer %s, client %s",
                 (int)sent_up, (int)read_up, memcmp(up, got_up, 100) == 0 ? "same" : "different",
                 (int)sent_down, (int)read_down,
                 memcmp(down, got_down, 100) == 0 ? "same" : "different",
                 named ? Endpoint(&peer) : "-", named ? Endpoint(&client_name) : "-");
            close(client);
            close(served);
        }
    }

    {
        // A port this program just held and let go, with nothing listening on it.
        const int holder = OpenSocket(SOCK_STREAM);
        struct sockaddr_in free_port = Loopback(0);
        struct sockaddr_in held;
        const bool have = holder >= 0 &&
                          bind(holder, (struct sockaddr *)&free_port, sizeof(free_port)) == 0 &&
                          NameOf(holder, &held);
        if (holder >= 0) {
            close(holder);
        }
        const int fd = OpenSocket(SOCK_STREAM);
        int error = 0;
        const int rc = have && fd >= 0 ? ConnectWithin(fd, &held, CONNECT_BUDGET_MS, &error) : 0;
        Case(S, REFUSED, CHECK_EXACT, have && rc < 0 && error == ECONNREFUSED,
             "expected -1 with ECONNREFUSED (%d) from %s; got %d, errno %d", ECONNREFUSED,
             have ? Endpoint(&held) : "no port", rc, error);
        if (fd >= 0) {
            close(fd);
        }
    }

    CheckReceiveTimeout(S, TIMEOUT);

    if (!config.has_server) {
        Skip(S, SERVER, "%s", NO_SERVER);
        Skip(S, NONBLOCKING, "%s", NO_SERVER);
        return;
    }
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(config.server_port);
    memcpy(&server.sin_addr.s_addr, config.server_ip, 4);
    {
        const int fd = OpenSocket(SOCK_STREAM);
        int error = 0;
        const int rc = fd >= 0 ? ConnectWithin(fd, &server, CONNECT_BUDGET_MS, &error) : -1;
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        memset(&peer, 0, sizeof(peer));
        const bool named =
            rc == 0 && getpeername(fd, (struct sockaddr *)&peer, &peer_length) == 0;
        Case(S, SERVER, CHECK_EXACT, rc == 0 && named && SameEndpoint(&peer, &server),
             "expected to connect to %s within %u ms and name it as the peer; connect %d, errno "
             "%d, peer %s",
             Endpoint(&server), CONNECT_BUDGET_MS, rc, error, named ? Endpoint(&peer) : "-");
        if (fd >= 0) {
            close(fd);
        }
    }
    {
        // What a game does: a connect that returns at once, a poll for writability, and the
        // pending error, which is the answer to whether the connect worked.
        const int fd = OpenSocket(SOCK_STREAM);
        const int flags = fd >= 0 ? fcntl(fd, F_GETFL, 0) : -1;
        const bool nonblocking = flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
        errno = 0;
        const int rc = nonblocking ? connect(fd, (struct sockaddr *)&server, sizeof(server)) : -1;
        const int started = errno;
        struct pollfd poller = { .fd = fd, .events = POLLOUT, .revents = 0 };
        const int ready = nonblocking && (rc == 0 || started == EINPROGRESS)
                              ? poll(&poller, 1, 3000)
                              : -1;
        int pending = -1;
        socklen_t pending_length = sizeof(pending);
        const bool asked = ready == 1 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &pending,
                                                    &pending_length) == 0;
        Case(S, NONBLOCKING, CHECK_EXACT,
             nonblocking && (rc == 0 || started == EINPROGRESS) && ready == 1 &&
                 (poller.revents & POLLOUT) != 0 && asked && pending == 0,
             "expected connect 0 or EINPROGRESS (%d), POLLOUT within 3 s and SO_ERROR 0; "
             "connect %d errno %d, poll %d revents 0x%x, SO_ERROR %d",
             EINPROGRESS, rc, started, ready, (unsigned)poller.revents, pending);
        if (fd >= 0) {
            close(fd);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// ssl

static const char *const STATUS_PATH_NOTE = "GET /service_status_hac.json";

#define HANDSHAKE_BUDGET_MS 5000u
#define EXCHANGE_BUDGET_MS 3000u

/// A connection to the game server and the descriptors that go with it: the program's own, and
/// the one SetSocketDescriptor handed back, if any, which libnx says to close before the
/// connection is.
typedef struct {
    SslConnection connection;
    int own;
    int handed;
} Link;

/// Connects to the game server within its budget, hands the socket to a new connection, presents
/// the name, and puts the connection in non-blocking mode so every step after this one runs
/// against a deadline of this program's. With @p close_own the program's own descriptor is
/// closed straight after the hand over.
static bool LinkOpen(SslContext *context, Link *link, bool close_own, char *said,
                     size_t said_size) {
    link->own = -1;
    link->handed = -1;
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(config.server_port);
    memcpy(&server.sin_addr.s_addr, config.server_ip, 4);
    const int fd = OpenSocket(SOCK_STREAM);
    int error = 0;
    if (fd < 0 || ConnectWithin(fd, &server, CONNECT_BUDGET_MS, &error) != 0) {
        snprintf(said, said_size, "connect to %s within %u ms: errno %d", Endpoint(&server),
                 CONNECT_BUDGET_MS, fd < 0 ? errno : error);
        if (fd >= 0) {
            close(fd);
        }
        return false;
    }
    Result rc = sslContextCreateConnection(context, &link->connection);
    if (R_FAILED(rc)) {
        snprintf(said, said_size, "CreateConnection %s", ResultText(rc));
        close(fd);
        return false;
    }
    errno = 0;
    link->handed = socketSslConnectionSetSocketDescriptor(&link->connection, fd);
    error = errno;
    if (link->handed < 0 && error != ENOENT) {
        snprintf(said, said_size, "SetSocketDescriptor: errno %d", error);
        sslConnectionClose(&link->connection);
        close(fd);
        return false;
    }
    link->own = fd;
    if (close_own) {
        close(link->own);
        link->own = -1;
    }
    const char *const name = config.server_name[0] != '\0' ? config.server_name : NULL;
    rc = name != NULL ? sslConnectionSetHostName(&link->connection, name, (u32)strlen(name) + 1u)
                      : 0;
    if (R_SUCCEEDED(rc)) {
        rc = sslConnectionSetIoMode(&link->connection, SslIoMode_NonBlocking);
    }
    if (R_FAILED(rc)) {
        snprintf(said, said_size, "SetHostName or SetIoMode(NonBlocking) %s", ResultText(rc));
        if (link->handed >= 0) {
            close(link->handed);
        }
        sslConnectionClose(&link->connection);
        if (link->own >= 0) {
            close(link->own);
        }
        return false;
    }
    return true;
}

static void LinkClose(Link *link) {
    if (link->handed >= 0) {
        close(link->handed);
    }
    sslConnectionClose(&link->connection);
    if (link->own >= 0) {
        close(link->own);
    }
}

/// DoHandshake in non-blocking mode, asked again while it would block, until @p budget_ms. A
/// certificate buffer is filled on whichever call the service fills it (libnx: in non-blocking
/// mode only once), so it is kept from any of them. Returns the last answer, which is still
/// "would block" when the budget ran out.
static Result HandshakeWithin(SslConnection *connection, uint32_t budget_ms, uint8_t *certs,
                              u32 certs_capacity, u32 *certs_size, u32 *certs_count) {
    static uint8_t scratch[8192];
    const uint64_t deadline = NowNs() + (uint64_t)budget_ms * 1000000ull;
    for (;;) {
        u32 size = 0;
        u32 count = 0;
        const bool want = certs != NULL && certs_capacity <= sizeof(scratch);
        const Result rc = sslConnectionDoHandshake(connection, want ? &size : NULL,
                                                   want ? &count : NULL, want ? scratch : NULL,
                                                   want ? certs_capacity : 0u);
        if (want && size > 0u && size <= certs_capacity) {
            memcpy(certs, scratch, size);
            *certs_size = size;
            *certs_count = count;
        }
        if (!NetSslWouldBlock(rc) || NowNs() >= deadline) {
            return rc;
        }
        SleepMs(10);
    }
}

/// SkipDefaultVerify, then no verification, then the handshake: how a program talks to a server
/// whose certificate it does not check.
static Result UnverifiedHandshake(SslConnection *connection) {
    Result rc = sslConnectionSetOption(connection, SslOptionType_SkipDefaultVerify, true);
    if (R_SUCCEEDED(rc)) {
        rc = sslConnectionSetVerifyOption(connection, 0);
    }
    if (R_SUCCEEDED(rc)) {
        rc = HandshakeWithin(connection, HANDSHAKE_BUDGET_MS, NULL, 0, NULL, NULL);
    }
    return rc;
}

/// How a handshake's answer reads on a line: a result, or that it was still waiting.
static const char *HandshakeText(Result rc) {
    static char text[80];
    if (NetSslWouldBlock(rc)) {
        snprintf(text, sizeof(text), "%s, still waiting after %u ms", ResultText(rc),
                 HANDSHAKE_BUDGET_MS);
        return text;
    }
    return ResultText(rc);
}

static void RunSsl(const char *sockets_why) {
    static const char *const S = "ssl";
    static const char *const CONTEXT = "a context is created";
    static const char *const DEFAULT_VERIFY = "a new connection verifies the peer and its name "
                                              "by default";
    static const char *const HOST_NAME = "the name set on a connection reads back";
    static const char *const SOCKET = "a connection takes a socket and is set to block";
    static const char *const HANDBACK = "a connection hands a descriptor back unless told not to "
                                        "close the socket";
    static const char *const IO_MODE = "a connection set to block says it blocks";
    static const char *const REFUSE = "a verifying handshake refuses a certificate no authority "
                                      "signed";
    static const char *const TRUSTED = "a verifying handshake succeeds once the server's "
                                       "certificate is imported";
    static const char *const HANDSHAKE = "a handshake that does not verify succeeds";
    static const char *const CERTIFICATE = "the handshake hands back the server's certificate";
    static const char *const WRITE = "a request written over it goes out whole";
    static const char *const KEEPS = "a connection keeps its socket after the program closes "
                                     "its own descriptor";
    static const char *const READ = "the server answers 200 with a status document that says "
                                    "nothing is wrong";
    const char *const all[] = { CONTEXT, DEFAULT_VERIFY, HOST_NAME,   SOCKET,    HANDBACK,
                                IO_MODE, REFUSE,         TRUSTED,     CERTIFICATE, HANDSHAKE,
                                WRITE,   READ,           KEEPS };
    // The ones from here on need the game server.
    const size_t first_server_case = 6;

    char why[200] = "";
    if (sockets_why != NULL) {
        snprintf(why, sizeof(why), "%s", sockets_why);
    }
    const Result opened = why[0] == '\0' ? sslInitialize(3) : 0;
    if (why[0] == '\0' && R_FAILED(opened)) {
        snprintf(why, sizeof(why),
                 "sslInitialize(3) returned %s: ssl cannot be opened from this program",
                 ResultText(opened));
    }
    SslContext context;
    memset(&context, 0, sizeof(context));
    if (why[0] == '\0') {
        const Result rc = sslCreateContext(&context, SslVersion_Auto);
        Case(S, CONTEXT, CHECK_NONE, R_SUCCEEDED(rc),
             "expected CreateContext(Auto) to succeed; got %s", ResultText(rc));
        if (R_FAILED(rc)) {
            snprintf(why, sizeof(why), "no context: CreateContext(Auto) returned %s",
                     ResultText(rc));
        }
    }
    if (why[0] != '\0') {
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
            if (!(i == 0 && sockets_why == NULL && R_SUCCEEDED(opened))) {
                Skip(S, all[i], "%s", why);
            }
        }
        if (sockets_why == NULL && R_SUCCEEDED(opened)) {
            sslExit();
        }
        return;
    }

    // libnx, ssl.h: "The default bitmask value at the time of sslContextCreateConnection is
    // PeerCa | HostName".
    {
        SslConnection connection;
        const Result created = sslContextCreateConnection(&context, &connection);
        u32 option = 0;
        const Result rc = R_SUCCEEDED(created) ? sslConnectionGetVerifyOption(&connection, &option)
                                               : created;
        Case(S, DEFAULT_VERIFY, CHECK_EXACT,
             R_SUCCEEDED(rc) && option == (SslVerifyOption_PeerCa | SslVerifyOption_HostName),
             "expected GetVerifyOption to answer %d (PeerCa | HostName); got %s, %u",
             SslVerifyOption_PeerCa | SslVerifyOption_HostName, ResultText(rc),
             (unsigned)option);

        static const char NAME[] = "edenprobe.invalid";
        char got[64];
        memset(got, 0, sizeof(got));
        u32 got_size = 0;
        Result set = created;
        Result get = created;
        if (R_SUCCEEDED(created)) {
            set = sslConnectionSetHostName(&connection, NAME, sizeof(NAME));
            get = R_SUCCEEDED(set)
                      ? sslConnectionGetHostName(&connection, got, sizeof(got) - 1u, &got_size)
                      : set;
        }
        Case(S, HOST_NAME, CHECK_EXACT,
             R_SUCCEEDED(get) && strncmp(got, NAME, sizeof(got)) == 0,
             "expected \"%s\" back after SetHostName; SetHostName %s, GetHostName %s, \"%.60s\" "
             "(%u bytes)",
             NAME, ResultText(set), ResultText(get), Visible(got, 60), (unsigned)got_size);
        if (R_SUCCEEDED(created)) {
            sslConnectionClose(&connection);
        }
    }

    // A socket of this program's own, connected to a listener it also opened, so this needs no
    // backend; and the mode the connection was told reads back.
    {
        int client = -1;
        int served = -1;
        char said[120] = "";
        const bool paired = LoopbackStream(&client, &served, said, sizeof(said));
        SslConnection connection;
        const Result created = paired ? sslContextCreateConnection(&context, &connection)
                                      : MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
        int handed = -1;
        int error = 0;
        Result mode_set = created;
        if (R_SUCCEEDED(created)) {
            errno = 0;
            handed = socketSslConnectionSetSocketDescriptor(&connection, client);
            error = errno;
            mode_set = handed >= 0 || error == ENOENT
                           ? sslConnectionSetIoMode(&connection, SslIoMode_Blocking)
                           : MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        // Whether a descriptor comes back is the next case's question, so either answer is
        // taken here.
        Case(S, SOCKET, CHECK_EXACT,
             paired && R_SUCCEEDED(created) && (handed >= 0 || error == ENOENT) &&
                 R_SUCCEEDED(mode_set),
             "expected SetSocketDescriptor to take a connected socket and SetIoMode(Blocking) to "
             "succeed; %s, CreateConnection %s, SetSocketDescriptor %d errno %d, SetIoMode %s",
             paired ? "pair up" : said, ResultText(created), handed, error, ResultText(mode_set));
        SslIoMode mode = 0;
        const Result mode_get = R_SUCCEEDED(mode_set)
                                    ? sslConnectionGetIoMode(&connection, &mode)
                                    : mode_set;
        if (R_FAILED(mode_set)) {
            Skip(S, IO_MODE, "the connection above was not set to block");
        } else {
            Case(S, IO_MODE, CHECK_EXACT, R_SUCCEEDED(mode_get) && mode == SslIoMode_Blocking,
                 "expected GetIoMode to answer %d (Blocking); got %s, %d", SslIoMode_Blocking,
                 ResultText(mode_get), (int)mode);
        }
        if (handed >= 0) {
            close(handed);
        }
        if (R_SUCCEEDED(created)) {
            sslConnectionClose(&connection);
        }

        // libnx, ssl.h, sslConnectionSetSocketDescriptor: the descriptor handed back "must be
        // closed if it's not negative (it will be -1 if SslOptionType_DoNotCloseSocket is set)".
        // So by default one comes back, and with DoNotCloseSocket none does, which libnx's socket
        // wrapper reports as ENOENT. Unverified on a console.
        SslConnection with_option;
        const Result created_2 = paired ? sslContextCreateConnection(&context, &with_option)
                                        : MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
        Result option = created_2;
        int handed_2 = -1;
        int error_2 = 0;
        if (R_SUCCEEDED(created_2)) {
            option = sslConnectionSetOption(&with_option, SslOptionType_DoNotCloseSocket, true);
            errno = 0;
            handed_2 = R_SUCCEEDED(option)
                           ? socketSslConnectionSetSocketDescriptor(&with_option, client)
                           : -1;
            error_2 = errno;
        }
        const bool by_default = R_SUCCEEDED(created) && handed >= 0;
        const bool with_none = R_SUCCEEDED(option) && handed_2 < 0 && error_2 == ENOENT;
        Case(S, HANDBACK, CHECK_EXACT, paired && by_default && with_none,
             "expected a descriptor back by default and none (ENOENT) with DoNotCloseSocket; "
             "default %d errno %d, DoNotCloseSocket %d errno %d (SetOption %s)%s",
             handed, error, handed_2, error_2, ResultText(option),
             !by_default
                 ? "; no descriptor by default is what an emulator answers that hands the "
                   "duplicate back only when DoNotCloseSocket is set, the reverse of libnx's "
                   "reading (reden hle/service/ssl/ssl.cpp, SetSocketDescriptor)"
                 : "");
        if (handed_2 >= 0) {
            close(handed_2);
        }
        if (R_SUCCEEDED(created_2)) {
            sslConnectionClose(&with_option);
        }
        if (client >= 0) {
            close(client);
        }
        if (served >= 0) {
            close(served);
        }
    }

    if (!config.has_server) {
        for (size_t i = first_server_case; i < sizeof(all) / sizeof(all[0]); ++i) {
            Skip(S, all[i], "%s", NO_SERVER);
        }
        sslContextClose(&context);
        sslExit();
        return;
    }

    // The certificate the net file says the server holds, if it names one.
    static char pem[8192];
    static uint8_t der[4096];
    size_t der_length = 0;
    char cert_why[200] = "";
    if (config.server_cert[0] == '\0') {
        snprintf(cert_why, sizeof(cert_why), "the net file names no server-cert");
    } else {
        char path[NET_NAME_MAX + 32];
        snprintf(path, sizeof(path), "sdmc:/edenprobe/%s", config.server_cert);
        FILE *file = fopen(path, "rb");
        const size_t length = file != NULL ? fread(pem, 1, sizeof(pem) - 1u, file) : 0u;
        if (file != NULL) {
            fclose(file);
        }
        pem[length] = '\0';
        der_length = NetPemToDer(pem, der, sizeof(der));
        if (file == NULL) {
            snprintf(cert_why, sizeof(cert_why), "the net file names %.80s, which cannot be "
                                                 "opened",
                     path);
        } else if (der_length == 0u) {
            snprintf(cert_why, sizeof(cert_why), "%.80s holds no PEM certificate this program "
                                                 "can read",
                     path);
        }
    }
    bool parsed = false;
    const bool self_issued = der_length > 0u && NetCertSelfIssued(der, der_length, &parsed);

    // With the defaults left alone and nothing imported, a certificate that names itself as its
    // issuer chains to no authority the platform ships, so the handshake must fail.
    if (cert_why[0] != '\0') {
        Skip(S, REFUSE, "%s", cert_why);
    } else if (!self_issued) {
        Skip(S, REFUSE, "the server's certificate names another issuer, so whether the "
                        "platform's authorities vouch for it is not this program's to know");
    } else {
        Link link;
        char said[160] = "";
        if (!LinkOpen(&context, &link, false, said, sizeof(said))) {
            Case(S, REFUSE, CHECK_EXACT, false,
                 "expected a connection to the server to hand to a handshake; %s", said);
        } else {
            const Result rc = HandshakeWithin(&link.connection, HANDSHAKE_BUDGET_MS, NULL, 0,
                                              NULL, NULL);
            Case(S, REFUSE, CHECK_EXACT, R_FAILED(rc) && !NetSslWouldBlock(rc),
                 "expected DoHandshake to fail with PeerCa | HostName and nothing imported, "
                 "within %u ms; got %s",
                 HANDSHAKE_BUDGET_MS, HandshakeText(rc));
            LinkClose(&link);
        }
    }

    // Trusted the way a program trusts its own server: its certificate imported into a context of
    // its own, the defaults left verifying the peer and the name.
    static uint8_t certs[8192];
    u32 certs_size = 0;
    u32 certs_count = 0;
    bool trusted_shaken = false;
    if (cert_why[0] != '\0') {
        Skip(S, TRUSTED, "%s", cert_why);
    } else {
        SslContext trusting;
        memset(&trusting, 0, sizeof(trusting));
        Result rc = sslCreateContext(&trusting, SslVersion_Auto);
        const Result created = rc;
        u64 pki = 0;
        if (R_SUCCEEDED(rc)) {
            rc = sslContextImportServerPki(&trusting, pem, (u32)strlen(pem),
                                           SslCertificateFormat_Pem, &pki);
        }
        const Result imported = rc;
        Link link;
        char said[160] = "not tried";
        bool connected = false;
        if (R_SUCCEEDED(rc)) {
            connected = LinkOpen(&trusting, &link, false, said, sizeof(said));
            if (connected) {
                snprintf(said, sizeof(said), "connected");
                memset(certs, 0, sizeof(certs));
                rc = HandshakeWithin(&link.connection, HANDSHAKE_BUDGET_MS, certs, sizeof(certs),
                                     &certs_size, &certs_count);
                LinkClose(&link);
            }
        }
        trusted_shaken = connected && R_SUCCEEDED(rc);
        Case(S, TRUSTED, CHECK_EXACT, trusted_shaken,
             "expected DoHandshake to succeed with PeerCa | HostName once %.60s is imported, "
             "presenting \"%.60s\"; CreateContext %s, ImportServerPki %s, %s, DoHandshake %s",
             config.server_cert, config.server_name, ResultText(created), ResultText(imported),
             said, connected ? HandshakeText(rc) : "not reached");
        if (R_SUCCEEDED(created)) {
            sslContextClose(&trusting);
        }
    }

    if (!trusted_shaken) {
        Skip(S, CERTIFICATE, "%s", cert_why[0] != '\0' ? cert_why
                                   : "the verifying handshake above did not succeed, and one that "
                                     "does not verify is handed no certificate (libnx, ssl.h)");
    } else {
        const uint8_t *first = NULL;
        size_t first_length = 0;
        const bool found = certs_size <= sizeof(certs) &&
                           NetFirstServerCert(certs, certs_size, &first, &first_length);
        const bool same = found && first_length == der_length &&
                          memcmp(first, der, der_length) == 0;
        Case(S, CERTIFICATE, CHECK_EXACT, same,
             "expected the %zu bytes of %.60s; got %u bytes, %u certificates, first %zu bytes, "
             "%s",
             der_length, config.server_cert, (unsigned)certs_size, (unsigned)certs_count,
             first_length, same ? "the same" : "not the same");
    }

    // The way a program talks to a server whose certificate it does not check, which is what the
    // fork's games do against a local server: SkipDefaultVerify, then no verification at all.
    // This connection carries the request, so the exchange is measured whatever the imports do.
    Link link;
    char said[160] = "";
    const bool connected = LinkOpen(&context, &link, false, said, sizeof(said));
    const Result handshake = connected ? UnverifiedHandshake(&link.connection)
                                       : MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    const bool shaken = connected && R_SUCCEEDED(handshake);
    Case(S, HANDSHAKE, CHECK_EXACT, shaken,
         "expected SkipDefaultVerify, SetVerifyOption(0) and DoHandshake to succeed within %u "
         "ms, presenting \"%.60s\"; %s, %s",
         HANDSHAKE_BUDGET_MS, config.server_name, connected ? "connected" : said,
         HandshakeText(handshake));

    if (!shaken) {
        Skip(S, WRITE, "no handshake above");
        Skip(S, READ, "no handshake above");
    } else {
        char request[256];
        const size_t request_length = NetStatusRequest(
            request, sizeof(request),
            config.server_name[0] != '\0' ? config.server_name : Dotted(config.server_ip));
        u32 written = 0;
        Result wrote = 0;
        const uint64_t write_deadline = NowNs() + (uint64_t)EXCHANGE_BUDGET_MS * 1000000ull;
        while (written < request_length) {
            u32 now = 0;
            wrote = sslConnectionWrite(&link.connection, request + written,
                                       (u32)(request_length - written), &now);
            if (R_SUCCEEDED(wrote)) {
                written += now;
            } else if (!NetSslWouldBlock(wrote) || NowNs() >= write_deadline) {
                break;
            } else {
                SleepMs(10);
            }
        }
        Case(S, WRITE, CHECK_EXACT, R_SUCCEEDED(wrote) && written == request_length,
             "expected all %zu bytes of %s written within %u ms; got %s, %u", request_length,
             STATUS_PATH_NOTE, EXCHANGE_BUDGET_MS, ResultText(wrote), (unsigned)written);

        // Read until the response is whole by its Content-Length, the server closes, or the
        // budget runs out: redenex answers this path with two empty lists (its status.go),
        // which is what a service status document says when nothing is wrong.
        static char response[16384];
        size_t have = 0;
        NetHttpResponse parsed_response;
        memset(&parsed_response, 0, sizeof(parsed_response));
        Result last = 0;
        const uint64_t deadline = NowNs() + (uint64_t)EXCHANGE_BUDGET_MS * 1000000ull;
        while (R_SUCCEEDED(wrote) && have < sizeof(response) - 1u && NowNs() < deadline) {
            u32 got = 0;
            last = sslConnectionRead(&link.connection, response + have,
                                     (u32)(sizeof(response) - 1u - have), &got);
            if (NetSslWouldBlock(last)) {
                SleepMs(10);
                continue;
            }
            if (R_FAILED(last) || got == 0u) {
                break;
            }
            have += got;
            if (NetHttpParse(response, have, &parsed_response) && parsed_response.complete) {
                break;
            }
        }
        response[have] = '\0';
        const bool head = NetHttpParse(response, have, &parsed_response);
        const bool clear = head && parsed_response.status == 200 &&
                           NetStatusAllClear(response + parsed_response.body_offset,
                                             parsed_response.body_length);
        Case(S, READ, CHECK_EXACT, clear,
             "expected 200 and {\"maintenances\":[],\"incidents\":[]}; read %zu bytes (last %s), "
             "status %d, body \"%.120s\"",
             have, ResultText(last), head ? parsed_response.status : -1,
             Visible(head ? response + parsed_response.body_offset : response, 120));
    }
    if (connected) {
        LinkClose(&link);
    }

    // The service works on a duplicate of the descriptor it was handed, so the program closing
    // its own afterwards, which it is free to do, leaves the connection's alone: a duplicate is
    // a descriptor of its own (POSIX dup), and closing one does not close the other.
    {
        Link kept;
        char kept_said[160] = "";
        const bool kept_connected = LinkOpen(&context, &kept, true, kept_said, sizeof(kept_said));
        const Result rc = kept_connected ? UnverifiedHandshake(&kept.connection) : 0;
        Case(S, KEEPS, CHECK_EXACT, kept_connected && R_SUCCEEDED(rc),
             "expected DoHandshake without verification to succeed after the program closed its "
             "own descriptor (POSIX dup; unverified on a console); %s, %s",
             kept_connected ? "connected, own descriptor closed" : kept_said,
             kept_connected ? HandshakeText(rc) : "not reached");
        if (kept_connected) {
            LinkClose(&kept);
        }
    }
    sslContextClose(&context);
    sslExit();
}

// ---------------------------------------------------------------------------------------------
// acc

/// nn::account's limit on an id token, and the buffer a program hands for one.
#define ID_TOKEN_BUFFER 0xC00u

static Result LoadIdToken(Service *manager, uint8_t *buffer, u32 *size) {
    memset(buffer, 0xA5, ID_TOKEN_BUFFER);
    return serviceDispatchOut(manager, 3, *size,
                              .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                              .buffers = { { buffer, ID_TOKEN_BUFFER } }, );
}

/// acc:u0, the way a program reaches it. libnx's accountInitialize asks InitializeApplicationInfo
/// first, which a loader that registered no launch property for this program refuses; the
/// commands this suite needs do not read what that call records, so the service is then opened
/// by name without it and the line says so.
typedef struct {
    Service *service;
    Service raw;
    bool libnx;
} AccountSession;

static Result OpenAccount(AccountSession *session, AccountUid *uid, char *said,
                          size_t said_size) {
    memset(session, 0, sizeof(*session));
    memset(uid, 0, sizeof(*uid));
    const Result initialized = accountInitialize(AccountServiceType_Application);
    Result rc = initialized;
    if (R_SUCCEEDED(initialized)) {
        session->libnx = true;
        session->service = accountGetServiceSession();
    } else {
        rc = smGetService(&session->raw, "acc:u0");
        if (R_FAILED(rc)) {
            snprintf(said, said_size,
                     "accountInitialize(AccountServiceType_Application) returned %s and "
                     "smGetService(\"acc:u0\") %s: acc:u0 cannot be opened from this program",
                     ResultText(initialized), ResultText(rc));
            return rc;
        }
        session->service = &session->raw;
    }
    // GetLastOpenedUser (4), then the first of ListAllUsers (2) when nobody was opened last.
    Result named = serviceDispatchOut(session->service, 4, *uid);
    const char *how = "GetLastOpenedUser";
    if (R_FAILED(named) || !accountUidIsValid(uid)) {
        AccountUid users[8];
        memset(users, 0, sizeof(users));
        named = serviceDispatch(session->service, 2,
                                .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_Out },
                                .buffers = { { users, sizeof(users) } }, );
        *uid = users[0];
        how = "ListAllUsers";
    }
    snprintf(said, said_size, "%s%s; %s %s, user %016llx%016llx",
             session->libnx ? "accountInitialize(Application) succeeded"
                            : "accountInitialize(Application) returned ",
             session->libnx ? "" : ResultText(initialized), how, ResultText(named),
             (unsigned long long)uid->uid[0], (unsigned long long)uid->uid[1]);
    if (!session->libnx) {
        const size_t used = strlen(said);
        snprintf(said + used, said_size - used,
                 " (acc:u0 opened by name, without InitializeApplicationInfo)");
    }
    return 0;
}

static void CloseAccount(AccountSession *session) {
    if (session->libnx) {
        accountExit();
    } else if (session->service != NULL) {
        serviceClose(&session->raw);
    }
    session->service = NULL;
}

static void RunAcc(void) {
    static const char *const S = "acc";
    static const char *const OPENS = "acc:u0 opens and names a user";
    static const char *const MANAGER = "the network account manager for that user opens";
    static const char *const ACCOUNT = "the network account id is the one the backend publishes";
    static const char *const REFRESH = "the id token refresh completes and succeeds";
    static const char *const TOKEN = "the id token is a bearer token, terminated inside its buffer";
    static const char *const CACHED = "the cached id token reads back the same";

    char why[200] = "";
    AccountSession account_session;
    AccountUid uid;
    char said[240];
    const Result opened = OpenAccount(&account_session, &uid, said, sizeof(said));
    if (R_FAILED(opened)) {
        snprintf(why, sizeof(why), "%.190s", said);
        Skip(S, OPENS, "%s", why);
    } else {
        const bool have = accountUidIsValid(&uid);
        Case(S, OPENS, CHECK_NONE, have, "expected acc:u0 and a user; %s", said);
        if (!have) {
            snprintf(why, sizeof(why), "no user to ask about");
        }
    }

    Service manager;
    memset(&manager, 0, sizeof(manager));
    if (why[0] != '\0') {
        Skip(S, MANAGER, "%s", why);
    } else {
        const Result rc = serviceDispatchIn(account_session.service, 101, uid,
                                            .out_num_objects = 1, .out_objects = &manager, );
        Case(S, MANAGER, CHECK_NONE, R_SUCCEEDED(rc),
             "expected GetBaasAccountManagerForApplication (101) to open one; got %s",
             ResultText(rc));
        if (R_FAILED(rc)) {
            snprintf(why, sizeof(why), "GetBaasAccountManagerForApplication returned %s",
                     ResultText(rc));
        }
    }

    const char *const session_why = why[0] != '\0' ? why : !config.has_session ? NO_SESSION : NULL;
    if (session_why != NULL) {
        Skip(S, ACCOUNT, "%s", session_why);
    } else if (!config.has_account) {
        Skip(S, ACCOUNT, "the net file names no account, so there is nothing to expect");
    } else {
        u64 account = 0;
        const Result rc = serviceDispatchOut(&manager, 1, account);
        Case(S, ACCOUNT, CHECK_EXACT, R_SUCCEEDED(rc) && account == config.account,
             "expected %016llx; got %s, %016llx%s", (unsigned long long)config.account,
             ResultText(rc), (unsigned long long)account, session_late);
    }

    static uint8_t first[ID_TOKEN_BUFFER];
    static uint8_t second[ID_TOKEN_BUFFER];
    bool refreshed = false;
    if (session_why != NULL) {
        Skip(S, REFRESH, "%s", session_why);
    } else {
        Service context;
        const Result started = serviceDispatch(&manager, 2, .out_num_objects = 1,
                                               .out_objects = &context, );
        u8 done = 0;
        Result result = started;
        const uint64_t begun = NowNs();
        if (R_SUCCEEDED(started)) {
            // The client's sign-in budget is fifteen seconds (SIGN_IN_TIMEOUT_SECONDS); five more
            // for the service to notice.
            while (NowNs() - begun < 20000000000ull) {
                if (R_FAILED(serviceDispatchOut(&context, 2, done)) || done != 0u) {
                    break;
                }
                SleepMs(50);
            }
            result = done != 0u ? serviceDispatch(&context, 3) : result;
            serviceClose(&context);
        }
        // A refresh that reports success has to have left a token behind: success with an empty
        // cache is what a service that never asked anybody answers.
        u32 cached = 0;
        const Result loaded = done != 0u && R_SUCCEEDED(result)
                                  ? LoadIdToken(&manager, first, &cached)
                                  : result;
        refreshed = R_SUCCEEDED(started) && done != 0u && R_SUCCEEDED(result) &&
                    R_SUCCEEDED(loaded) && cached > 0u;
        Case(S, REFRESH, CHECK_EXACT, refreshed,
             "expected HasDone within 20 s, GetResult 0 and a token in the cache after it; "
             "EnsureIdTokenCacheAsync %s, done %u after %llu ms, GetResult %s, cache %u bytes%s",
             ResultText(started), (unsigned)done,
             (unsigned long long)((NowNs() - begun) / 1000000ull), ResultText(result),
             (unsigned)cached, session_late);
    }

    u32 size = 0;
    if (session_why != NULL) {
        Skip(S, TOKEN, "%s", session_why);
        Skip(S, CACHED, "%s", session_why);
    } else if (!refreshed) {
        Skip(S, TOKEN, "the refresh above did not succeed, so there is no token to expect");
        Skip(S, CACHED, "the refresh above did not succeed, so there is no token to expect");
    } else {
        // It travels as a bearer credential, so RFC 6750 fixes its alphabet; the guest reads it
        // as a C string, so a terminator has to follow it inside the buffer.
        const Result rc = LoadIdToken(&manager, first, &size);
        const bool sized = R_SUCCEEDED(rc) && size > 0u && size < ID_TOKEN_BUFFER;
        const bool token = sized && NetIsB64Token(first, size) && first[size] == 0u;
        Case(S, TOKEN, CHECK_EXACT, token,
             "expected 1 to %u bytes of b64token and a zero after them; got %s, %u bytes, %s, "
             "next byte 0x%02x%s",
             ID_TOKEN_BUFFER - 1u, ResultText(rc), (unsigned)size,
             sized && NetIsB64Token(first, size) ? "b64token" : "not b64token",
             sized ? first[size] : 0u, session_late);
        u32 size_again = 0;
        const Result again = LoadIdToken(&manager, second, &size_again);
        Case(S, CACHED, CHECK_EXACT,
             sized && R_SUCCEEDED(again) && size_again == size && memcmp(first, second, size) == 0,
             "expected the same %u bytes again; got %s, %u bytes, %s%s", (unsigned)size,
             ResultText(again), (unsigned)size_again,
             size_again == size && memcmp(first, second, size) == 0 ? "the same" : "different",
             session_late);
    }

    if (manager.session != 0) {
        serviceClose(&manager);
    }
    CloseAccount(&account_session);
}

// ---------------------------------------------------------------------------------------------
// friend

static void RunFriend(void) {
    static const char *const S = "friend";
    static const char *const OPENS = "friend:u opens a friend service";
    static const char *const BLOCKED = "the blocked user list is the backend's";
    static const char *const KNOWN = "a profile the backend knows comes back valid with its name "
                                     "and picture";
    static const char *const UNKNOWN = "a profile the backend does not know comes back invalid, "
                                       "in its own slot";
    static const char *const PICTURE = "a picture url comes back with every {size} replaced";

    char why[200] = "";
    const Result opened = friendsInitialize(FriendsServiceType_User);
    if (R_FAILED(opened)) {
        snprintf(why, sizeof(why),
                 "friendsInitialize(FriendsServiceType_User) returned %s: friend:u cannot be "
                 "opened from this program",
                 ResultText(opened));
        Skip(S, OPENS, "%s", why);
    } else {
        Case(S, OPENS, CHECK_NONE, true,
             "expected friendsInitialize(FriendsServiceType_User) to succeed; it did");
    }

    // The same user acc names, which is who the lists belong to.
    AccountUid uid;
    memset(&uid, 0, sizeof(uid));
    if (why[0] == '\0') {
        AccountSession account_session;
        char said[240];
        if (R_SUCCEEDED(OpenAccount(&account_session, &uid, said, sizeof(said)))) {
            CloseAccount(&account_session);
        }
    }
    const char *const session_why = why[0] != '\0' ? why : !config.has_session ? NO_SESSION : NULL;
    Service *const service = why[0] == '\0' ? friendsGetServiceSession_IFriendsService() : NULL;

    if (session_why != NULL) {
        Skip(S, BLOCKED, "%s", session_why);
    } else {
        struct {
            s32 offset;
            u32 pad;
            AccountUid uid;
        } in = { 0, 0, uid };
        u64 ids[NET_BLOCKED_MAX + 4u];
        memset(ids, 0, sizeof(ids));
        s32 count = -1;
        const Result rc = serviceDispatchInOut(
            service, 10400, in, count,
            .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_Out },
            .buffers = { { ids, sizeof(ids) } }, );
        const bool same = R_SUCCEEDED(rc) && count == (s32)config.blocked_count &&
                          NetSameIds(ids, (size_t)config.blocked_count, config.blocked,
                                     config.blocked_count);
        Case(S, BLOCKED, CHECK_EXACT, same,
             "expected the %u ids the backend lists, first %016llx; got %s, %d ids, first "
             "%016llx%s",
             (unsigned)config.blocked_count,
             (unsigned long long)(config.blocked_count > 0u ? config.blocked[0] : 0u),
             ResultText(rc), (int)count, (unsigned long long)ids[0], session_late);
    }

    const NetProfile *known = NULL;
    for (uint32_t i = 0; i < config.profile_count && config.has_account; ++i) {
        if (config.profiles[i].account == config.account) {
            known = &config.profiles[i];
        }
    }
    if (session_why != NULL) {
        Skip(S, KNOWN, "%s", session_why);
        Skip(S, UNKNOWN, "%s", session_why);
    } else if (known == NULL) {
        Skip(S, KNOWN, "the net file names no profile for its account");
        Skip(S, UNKNOWN, "the net file names no profile for its account");
    } else {
        // One id it knows and one it cannot: one slot each, in the order asked.
        const u64 ids[2] = { config.account, NetUnknownAccount(&config) };
        static uint8_t raw[2][NET_FRIEND_PROFILE_SIZE];
        memset(raw, 0xEE, sizeof(raw));
        const Result rc = serviceDispatchIn(
            service, 10500, uid,
            .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_In,
                              SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
            .buffers = { { ids, sizeof(ids) }, { raw, sizeof(raw) } }, );
        NetFriendProfile first;
        NetFriendProfile second;
        NetFriendProfileDecode(raw[0], &first);
        NetFriendProfileDecode(raw[1], &second);
        Case(S, KNOWN, CHECK_EXACT, R_SUCCEEDED(rc) && NetProfileMatches(known, &first),
             "expected %016llx valid, \"%.32s\", \"%.100s\"; got %s, %016llx valid %u, \"%.32s\", "
             "\"%.100s\"%s",
             (unsigned long long)known->account, Visible(known->name, 32), Visible(known->url, 100),
             ResultText(rc),
             (unsigned long long)first.account, (unsigned)first.valid, Visible(first.name, 32),
             Visible(first.url, 100), session_late);
        Case(S, UNKNOWN, CHECK_EXACT, R_SUCCEEDED(rc) && NetProfileIsUnknown(ids[1], &second),
             "expected slot two to answer for %016llx (the id asked: the fork's reading, "
             "unverified on a console), invalid; got %s, %016llx valid %u%s",
             (unsigned long long)ids[1], ResultText(rc), (unsigned long long)second.account,
             (unsigned)second.valid, session_late);
    }

    // GetProfileImageUrl (11000): a picture url in, the same url with every {size} replaced by
    // the size asked for out (the contract: avatar_url may contain {size}). Two placeholders, so
    // replacing only the first is a wrong answer.
    if (session_why != NULL) {
        Skip(S, PICTURE, "%s", session_why);
    } else {
        static const char URL[] = "http://edenprobe.invalid/{size}/avatar-{size}.png";
        struct {
            char url[NET_PROFILE_URL_MAX];
            u32 size;
        } in;
        memset(&in, 0, sizeof(in));
        memcpy(in.url, URL, sizeof(URL));
        in.size = 256;
        char want[NET_PROFILE_URL_MAX];
        NetExpandSize(want, sizeof(want), URL, in.size);
        char out[NET_PROFILE_URL_MAX];
        memset(out, 0, sizeof(out));
        const Result rc = serviceDispatchInOut(service, 11000, in, out);
        out[sizeof(out) - 1u] = '\0';
        Case(S, PICTURE, CHECK_EXACT, R_SUCCEEDED(rc) && strcmp(out, want) == 0,
             "expected \"%.100s\"; got %s, \"%.100s\"", want, ResultText(rc),
             Visible(out, 100));
    }

    if (R_SUCCEEDED(opened)) {
        friendsExit();
    }
}

void RunNet(void) {
    LoadConfig();

    RunNifm();

    // One socket driver for dns, bsd and ssl, since libnx starts sfdnsres beside bsd.
    const Result sockets = socketInitializeDefault();
    char sockets_why[200] = "";
    if (R_FAILED(sockets)) {
        snprintf(sockets_why, sizeof(sockets_why),
                 "socketInitializeDefault() returned %s: bsd cannot be opened from this program",
                 ResultText(sockets));
    }
    const char *const why = sockets_why[0] != '\0' ? sockets_why : NULL;
    RunDns(why);
    RunBsd(why);
    RunSsl(why);
    if (R_SUCCEEDED(sockets)) {
        socketExit();
    }

    RunAcc();
    RunFriend();
}
