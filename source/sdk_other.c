// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>
#include <switch.h>

#include "sdk.h"

// nn::pl, nn::ngc and nn::nsd's exported functions (sdk.h) and their own code: the requests to
// pl:u, ngc:u and nsd:u the IPC mode's cases send. The SDK's pl and nsd functions keep no session
// a caller sees, so the own code opens one at its first call and keeps it for the run.

/// The label in the program's read-only data (sdk.h). Nothing here reads it: the Makefile's
/// --undefined keeps it through the linker's section collection.
const char edenprobe_sdk_label[] = SDK_LABEL;

// ------------------------------------------------------------------------------------- nn::pl

static Mutex pl_lock;
static bool pl_open;

/// pl:u with its shared memory mapped, as libnx's plInitialize maps it: the session, or NULL.
static Service *Pl(void) {
    mutexLock(&pl_lock);
    if (!pl_open) {
        pl_open = R_SUCCEEDED(plInitialize(PlServiceType_User));
    }
    mutexUnlock(&pl_lock);
    return pl_open ? plGetServiceSession() : NULL;
}

SDK_FUNCTION void NnPlRequestSharedFontLoad(int32_t type) {
    SdkOwnCall(SDK_PL_REQUEST_LOAD);
    Service *const pl = Pl();
    const u32 in = (u32)type;
    if (pl != NULL) {
        (void)serviceDispatchIn(pl, 0, in);
    }
}

/// GetLoadState (1), GetSize (2) or GetSharedMemoryAddressOffset (3) of a font: its answer, nought
/// when pl:u refused.
static u32 PlAsk(u32 command, int32_t type) {
    Service *const pl = Pl();
    const u32 in = (u32)type;
    u32 out = 0;
    if (pl == NULL || R_FAILED(serviceDispatchInOut(pl, command, in, out))) {
        return 0;
    }
    return out;
}

SDK_FUNCTION int32_t NnPlGetSharedFontLoadState(int32_t type) {
    SdkOwnCall(SDK_PL_LOAD_STATE);
    return (int32_t)PlAsk(1, type);
}

SDK_FUNCTION size_t NnPlGetSharedFontSize(int32_t type) {
    SdkOwnCall(SDK_PL_SIZE);
    return PlAsk(2, type);
}

SDK_FUNCTION const void *NnPlGetSharedFontAddress(int32_t type) {
    SdkOwnCall(SDK_PL_ADDRESS);
    const u32 offset = PlAsk(3, type);
    const uint8_t *const base = Pl() != NULL ? (const uint8_t *)plGetSharedmemAddr() : NULL;
    return base != NULL ? base + offset : NULL;
}

SDK_FUNCTION bool NnPlGetSharedFontInOrderOfPriority(int32_t *count, int32_t *types,
                                                     const void **addresses, size_t *sizes,
                                                     int32_t max, NnSettingsLanguageCode language) {
    SdkOwnCall(SDK_PL_ORDER);
    Service *const pl = Pl();
    *count = 0;
    if (pl == NULL || max <= 0) {
        return false;
    }
    // GetSharedFontInOrderOfPriority (5): the language, then the types, offsets and sizes in
    // buffers of six.
    u32 got_types[NN_PL_FONTS], offsets[NN_PL_FONTS], got_sizes[NN_PL_FONTS];
    memset(got_types, 0, sizeof(got_types));
    memset(offsets, 0, sizeof(offsets));
    memset(got_sizes, 0, sizeof(got_sizes));
    u64 code = 0;
    memcpy(&code, language.code, sizeof(code));
    struct {
        u8 loaded;
        u32 count;
    } said = { 0, 0 };
    const Result rc = serviceDispatchInOut(
        pl, 5, code, said,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out,
                          SfBufferAttr_HipcMapAlias | SfBufferAttr_Out,
                          SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { got_types, sizeof(got_types) },
                     { offsets, sizeof(offsets) },
                     { got_sizes, sizeof(got_sizes) } }, );
    if (R_FAILED(rc)) {
        return false;
    }
    const uint8_t *const base = (const uint8_t *)plGetSharedmemAddr();
    uint32_t n = said.count < NN_PL_FONTS ? said.count : NN_PL_FONTS;
    n = n < (uint32_t)max ? n : (uint32_t)max;
    for (uint32_t i = 0; i < n; ++i) {
        types[i] = (int32_t)got_types[i];
        addresses[i] = base + offsets[i];
        sizes[i] = got_sizes[i];
    }
    *count = (int32_t)n;
    return said.loaded != 0u;
}

// ------------------------------------------------------------------------------------ nn::ngc

/// ngc:u's Check (1) and Mask (2) take the patterns and the filter's options (nought).
typedef struct {
    uint32_t patterns;
    uint8_t option[0x20];
} NgcIn;

SDK_FUNCTION uint32_t NnNgcInitialize(NnNgcProfanityFilter *filter, void *work, size_t size,
                                      bool language) {
    SdkOwnCall(SDK_NGC_INITIALIZE);
    if (filter == NULL || work == NULL || size < NN_NGC_WORK_MIN || filter->initialized != 0u) {
        return SDK_OWN_UNSUPPORTED;
    }
    memset(&filter->own, 0, sizeof(filter->own));
    const Result rc = smGetService(&filter->own, "ngc:u");
    if (R_SUCCEEDED(rc)) {
        filter->language = language ? 1u : 0u;
        filter->initialized = 1u;
        filter->work = (uint64_t)(uintptr_t)work;
        filter->work_size = size;
    }
    return rc;
}

SDK_FUNCTION void NnNgcFinalize(NnNgcProfanityFilter *filter) {
    SdkOwnCall(SDK_NGC_FINALIZE);
    if (filter != NULL && filter->initialized != 0u) {
        serviceClose(&filter->own);
        filter->initialized = 0u;
    }
}

SDK_FUNCTION uint32_t NnNgcGetContentVersion(NnNgcProfanityFilter *filter) {
    SdkOwnCall(SDK_NGC_VERSION);
    u32 version = 0;
    if (filter != NULL && filter->initialized != 0u) {
        (void)serviceDispatchOut(&filter->own, 0, version);
    }
    return version;
}

SDK_FUNCTION uint32_t NnNgcCheckProfanityWords(NnNgcProfanityFilter *filter, uint32_t *found,
                                               uint32_t patterns, const char **words,
                                               size_t count) {
    SdkOwnCall(SDK_NGC_CHECK);
    if (filter == NULL || filter->initialized == 0u || found == NULL || words == NULL) {
        return SDK_OWN_UNSUPPORTED;
    }
    const NgcIn in = { patterns, { 0 } };
    for (size_t i = 0; i < count; ++i) {
        u32 flags = 0;
        const Result rc = serviceDispatchInOut(
            &filter->own, 1, in, flags,
            .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
            .buffers = { { words[i], strlen(words[i]) + 1u } }, );
        if (R_FAILED(rc)) {
            return rc;
        }
        found[i] = flags;
    }
    return 0;
}

SDK_FUNCTION uint32_t NnNgcMaskProfanityWordsInText(NnNgcProfanityFilter *filter, int32_t *count,
                                                    char *text, uint32_t patterns) {
    SdkOwnCall(SDK_NGC_MASK);
    if (filter == NULL || filter->initialized == 0u || text == NULL) {
        return SDK_OWN_UNSUPPORTED;
    }
    const NgcIn in = { patterns, { 0 } };
    const size_t size = strlen(text) + 1u;
    char back[0x100];
    if (size > sizeof(back)) {
        return SDK_OWN_UNSUPPORTED;
    }
    memset(back, 0, sizeof(back));
    u32 masked = 0;
    const Result rc = serviceDispatchInOut(
        &filter->own, 2, in, masked,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In,
                          SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { text, size }, { back, size } }, );
    if (R_SUCCEEDED(rc)) {
        memcpy(text, back, size);
        if (count != NULL) {
            *count = (int32_t)masked;
        }
    }
    return rc;
}

// ------------------------------------------------------------------------------------ nn::nsd

static Mutex nsd_lock;
static Service nsd;

/// nsd:u, opened at the first call: the session, or NULL.
static Service *Nsd(void) {
    mutexLock(&nsd_lock);
    if (!serviceIsActive(&nsd) && R_FAILED(smGetService(&nsd, "nsd:u"))) {
        memset(&nsd, 0, sizeof(nsd));
    }
    mutexUnlock(&nsd_lock);
    return serviceIsActive(&nsd) ? &nsd : NULL;
}

SDK_FUNCTION uint32_t NnNsdResolve(NnNsdFqdn *out, const NnNsdFqdn *in) {
    SdkOwnCall(SDK_NSD_RESOLVE);
    Service *const s = Nsd();
    if (s == NULL) {
        return SDK_OWN_UNSUPPORTED;
    }
    // Resolve (20): a name in, a name out.
    return serviceDispatch(
        s, 20,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
                          SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = { { in->name, sizeof(in->name) }, { out->name, sizeof(out->name) } }, );
}

SDK_FUNCTION uint32_t NnNsdResolveEx(NnNsdFqdn *out, const NnNsdFqdn *in) {
    SdkOwnCall(SDK_NSD_RESOLVE_EX);
    Service *const s = Nsd();
    if (s == NULL) {
        return SDK_OWN_UNSUPPORTED;
    }
    // ResolveEx (21): the service's own result for the name beside the command's.
    u32 inner = 0;
    const Result rc = serviceDispatchOut(
        s, 21, inner,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
                          SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = { { in->name, sizeof(in->name) }, { out->name, sizeof(out->name) } }, );
    return R_FAILED(rc) ? rc : inner;
}

SDK_FUNCTION void NnNsdGetEnvironmentIdentifier(NnNsdEnvironmentIdentifier *out) {
    SdkOwnCall(SDK_NSD_ENVIRONMENT);
    Service *const s = Nsd();
    memset(out->name, 0, sizeof(out->name));
    if (s != NULL) {
        // GetEnvironmentIdentifier (11): the name in eight bytes.
        (void)serviceDispatch(s, 11,
                              .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
                              .buffers = { { out->name, sizeof(out->name) } }, );
    }
}
