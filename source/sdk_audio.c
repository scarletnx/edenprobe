// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>
#include <switch.h>

#include "sdk.h"

// nn::os's handle attach, nn::audio's and nn::codec's exported functions (sdk.h) and their own
// code: the requests the IPC mode's cases send to audout:u, audren:u and its IAudioDevice,
// audin:u and hwopus. Each AudioOut, renderer and Opus decoder the own code opens keeps its own
// session, in the caller's object (an AudioOut's or a decoder's room after the SDK's fields) or in
// a slot the handle names (a renderer's), as the SDK's keep one each.

static void Note(Result rc) {
    SdkOwnNote(rc);
}

// ------------------------------------------------------------------------------------- nn::os

SDK_FUNCTION void NnOsAttachReadableHandleToSystemEvent(NnOsSystemEvent *event, uint32_t handle,
                                                        bool managed, int32_t clear_mode) {
    memset(event, 0, sizeof(*event));
    event->handle = handle;
    event->attached = 1;
    event->managed = managed ? 1u : 0u;
    event->clear_mode = (uint8_t)clear_mode;
}

bool SdkEventWait(NnOsSystemEvent *event, uint64_t ns) {
    if (!event->attached || R_FAILED(svcWaitSynchronizationSingle(event->handle, ns))) {
        return false;
    }
    if (event->clear_mode == NN_OS_CLEAR_AUTO) {
        svcClearEvent(event->handle);
    }
    return true;
}

void SdkEventClear(NnOsSystemEvent *event) {
    if (event->attached) {
        svcClearEvent(event->handle);
    }
}

void SdkEventClose(NnOsSystemEvent *event) {
    if (event->attached && event->managed) {
        svcCloseHandle(event->handle);
    }
    memset(event, 0, sizeof(*event));
}

// --------------------------------------------------------------------------- nn::audio's outs

/// audout:u, opened at the first call and kept for the run.
static Mutex manager_lock;
static Service audout_manager;

static Service *AudoutManager(Result *rc) {
    mutexLock(&manager_lock);
    *rc = serviceIsActive(&audout_manager) ? 0 : smGetService(&audout_manager, "audout:u");
    mutexUnlock(&manager_lock);
    return R_SUCCEEDED(*rc) ? &audout_manager : NULL;
}

/// OpenAudioOutAuto (3): the name asked for ("" for the default), the rate and channels, the
/// process; the out's rate, channels, format and state, its name, and its session.
static uint32_t OpenOut(NnAudioOut *out, const char *name, const NnAudioOutParameter *param) {
    Result rc;
    Service *const manager = AudoutManager(&rc);
    if (manager == NULL) {
        return rc;
    }
    static char asked[NN_AUDIO_NAME];
    char given[NN_AUDIO_NAME];
    mutexLock(&manager_lock);
    memset(asked, 0, sizeof(asked));
    memset(given, 0, sizeof(given));
    if (name != NULL) {
        strncpy(asked, name, sizeof(asked) - 1u);
    }
    const struct {
        uint32_t sample_rate;
        uint16_t channel_count;
        uint16_t reserved;
        uint64_t aruid;
    } in = { (uint32_t)param->sample_rate, param->channel_count, 0,
             appletGetAppletResourceUserId() };
    struct {
        uint32_t sample_rate, channel_count, format, state;
    } got = { 0, 0, 0, 0 };
    Service session;
    memset(&session, 0, sizeof(session));
    rc = serviceDispatchInOut(
        manager, 3, in, got,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
                          SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = { { asked, sizeof(asked) }, { given, sizeof(given) } }, .in_send_pid = true,
        .in_num_handles = 1, .in_handles = { CUR_PROCESS_HANDLE }, .out_num_objects = 1,
        .out_objects = &session, );
    mutexUnlock(&manager_lock);
    if (R_FAILED(rc)) {
        return rc;
    }
    out->sample_rate = got.sample_rate;
    out->channel_count = got.channel_count;
    out->sample_format = got.format;
    out->state = got.state;
    memcpy(out->name, given, sizeof(out->name));
    out->name[sizeof(out->name) - 1u] = '\0';
    out->own = session;
    return 0;
}

SDK_FUNCTION uint32_t NnAudioOpenAudioOut(NnAudioOut *out, const char *name,
                                          const NnAudioOutParameter *param) {
    SdkOwnCall(SDK_AUDIO_OPEN_OUT);
    return OpenOut(out, name, param);
}

SDK_FUNCTION uint32_t NnAudioOpenDefaultAudioOut(NnAudioOut *out,
                                                 const NnAudioOutParameter *param) {
    SdkOwnCall(SDK_AUDIO_OPEN_DEFAULT_OUT);
    return OpenOut(out, NULL, param);
}

SDK_FUNCTION void NnAudioCloseAudioOut(NnAudioOut *out) {
    SdkOwnCall(SDK_AUDIO_CLOSE_OUT);
    serviceClose(&out->own);
    out->state = NN_AUDIO_OUT_STOPPED;
}

SDK_FUNCTION uint32_t NnAudioGetAudioOutState(const NnAudioOut *out) {
    SdkOwnCall(SDK_AUDIO_OUT_STATE);
    uint32_t state = out->state;
    Note(serviceDispatchOut((Service *)&out->own, 0, state));
    return state;
}

SDK_FUNCTION uint32_t NnAudioStartAudioOut(NnAudioOut *out) {
    SdkOwnCall(SDK_AUDIO_START_OUT);
    return serviceDispatch(&out->own, 1);
}

SDK_FUNCTION void NnAudioStopAudioOut(NnAudioOut *out) {
    SdkOwnCall(SDK_AUDIO_STOP_OUT);
    Note(serviceDispatch(&out->own, 2));
}

/// AppendAudioOutBufferAuto (7): the buffer whole, its address the tag it comes back as.
SDK_FUNCTION bool NnAudioAppendAudioOutBuffer(NnAudioOut *out, NnAudioOutBuffer *buffer) {
    SdkOwnCall(SDK_AUDIO_APPEND_OUT);
    const uint64_t tag = (uint64_t)(uintptr_t)buffer;
    const Result rc = serviceDispatchIn(
        &out->own, 7, tag, .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
        .buffers = { { buffer, sizeof(*buffer) } }, );
    Note(rc);
    return R_SUCCEEDED(rc);
}

/// GetReleasedAudioOutBuffersAuto (8) for one tag, as the SDK takes them back one a call.
SDK_FUNCTION NnAudioOutBuffer *NnAudioGetReleasedAudioOutBuffer(NnAudioOut *out) {
    SdkOwnCall(SDK_AUDIO_RELEASED_OUT);
    uint64_t tag = 0;
    uint32_t count = 0;
    const Result rc =
        serviceDispatchOut(&out->own, 8, count,
                           .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
                           .buffers = { { &tag, sizeof(tag) } }, );
    Note(rc);
    return R_SUCCEEDED(rc) && count > 0u ? (NnAudioOutBuffer *)(uintptr_t)tag : NULL;
}

SDK_FUNCTION bool NnAudioContainsAudioOutBuffer(const NnAudioOut *out,
                                                const NnAudioOutBuffer *buffer) {
    SdkOwnCall(SDK_AUDIO_CONTAINS_OUT);
    const uint64_t tag = (uint64_t)(uintptr_t)buffer;
    uint8_t held = 0;
    const Result rc = serviceDispatchInOut((Service *)&out->own, 6, tag, held);
    Note(rc);
    return R_SUCCEEDED(rc) && held != 0u;
}

SDK_FUNCTION int32_t NnAudioGetAudioOutBufferCount(const NnAudioOut *out) {
    SdkOwnCall(SDK_AUDIO_OUT_COUNT);
    uint32_t count = 0;
    Note(serviceDispatchOut((Service *)&out->own, 9, count));
    return (int32_t)count;
}

SDK_FUNCTION bool NnAudioFlushAudioOutBuffers(NnAudioOut *out) {
    SdkOwnCall(SDK_AUDIO_FLUSH_OUT);
    uint8_t flushed = 0;
    const Result rc = serviceDispatchOut(&out->own, 11, flushed);
    Note(rc);
    return R_SUCCEEDED(rc) && flushed != 0u;
}

SDK_FUNCTION void NnAudioSetAudioOutVolume(NnAudioOut *out, float volume) {
    SdkOwnCall(SDK_AUDIO_SET_OUT_VOLUME);
    Note(serviceDispatchIn(&out->own, 12, volume));
}

SDK_FUNCTION float NnAudioGetAudioOutVolume(const NnAudioOut *out) {
    SdkOwnCall(SDK_AUDIO_OUT_VOLUME);
    float volume = -1.0f;
    Note(serviceDispatchOut((Service *)&out->own, 13, volume));
    return volume;
}

/// ListAudioOutsAuto (2): the names into @p infos, how many.
SDK_FUNCTION int32_t NnAudioListAudioOuts(NnAudioName *infos, int32_t count) {
    SdkOwnCall(SDK_AUDIO_LIST_OUTS);
    Result rc;
    Service *const manager = AudoutManager(&rc);
    uint32_t listed = 0;
    if (manager != NULL && count > 0) {
        rc = serviceDispatchOut(
            manager, 2, listed, .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
            .buffers = { { infos, sizeof(*infos) * (size_t)count } }, );
    }
    Note(rc);
    return R_SUCCEEDED(rc) ? (int32_t)listed : 0;
}

// ------------------------------------------------------------------------- nn::audio's device

/// The audio device through libnx's auddev, the commands the IPC mode's case sends: opened at the
/// first call and kept for the run.
static Result Device(void) {
    static Result opened = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    mutexLock(&manager_lock);
    if (R_FAILED(opened)) {
        opened = auddevInitialize();
    }
    const Result rc = opened;
    mutexUnlock(&manager_lock);
    return rc;
}

SDK_FUNCTION int32_t NnAudioListAudioDeviceName(NnAudioName *names, int32_t count) {
    SdkOwnCall(SDK_AUDIO_LIST_DEVICES);
    s32 listed = 0;
    Result rc = Device();
    if (R_SUCCEEDED(rc)) {
        rc = auddevListAudioDeviceName((AudioDeviceName *)names, count, &listed);
    }
    Note(rc);
    return R_SUCCEEDED(rc) ? listed : 0;
}

SDK_FUNCTION void NnAudioGetActiveAudioDeviceName(NnAudioName *name) {
    SdkOwnCall(SDK_AUDIO_ACTIVE_DEVICE);
    Result rc = Device();
    if (R_SUCCEEDED(rc)) {
        rc = auddevGetActiveAudioDeviceName((AudioDeviceName *)name);
    }
    Note(rc);
}

SDK_FUNCTION void NnAudioSetAudioDeviceOutputVolume(const NnAudioName *name, float volume) {
    SdkOwnCall(SDK_AUDIO_SET_DEVICE_VOLUME);
    Result rc = Device();
    if (R_SUCCEEDED(rc)) {
        rc = auddevSetAudioDeviceOutputVolume((const AudioDeviceName *)name, volume);
    }
    Note(rc);
}

SDK_FUNCTION float NnAudioGetAudioDeviceOutputVolume(const NnAudioName *name) {
    SdkOwnCall(SDK_AUDIO_DEVICE_VOLUME);
    float volume = -1.0f;
    Result rc = Device();
    if (R_SUCCEEDED(rc)) {
        rc = auddevGetAudioDeviceOutputVolume((const AudioDeviceName *)name, &volume);
    }
    Note(rc);
    return volume;
}

// ---------------------------------------------------------------------------- nn::audio's ins

/// ListAudioIns (0) on audin:u, opened at the first call and kept for the run.
SDK_FUNCTION int32_t NnAudioListAudioIns(NnAudioName *infos, int32_t count) {
    SdkOwnCall(SDK_AUDIO_LIST_INS);
    static Service audin;
    mutexLock(&manager_lock);
    Result rc = serviceIsActive(&audin) ? 0 : smGetService(&audin, "audin:u");
    uint32_t listed = 0;
    if (R_SUCCEEDED(rc) && count > 0) {
        rc = serviceDispatchOut(
            &audin, 0, listed, .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
            .buffers = { { infos, sizeof(*infos) * (size_t)count } }, );
    }
    mutexUnlock(&manager_lock);
    Note(rc);
    return R_SUCCEEDED(rc) ? (int32_t)listed : 0;
}

// ------------------------------------------------------------------------ nn::audio's renderer

/// The renderers the own code opened: a session each, and its work buffer as transfer memory. The
/// SDK hands audren:u the caller's buffer; the own code makes one of the size the caller's has and
/// lets it go when the renderer closes, as libnx's audrenInitialize and audrenExit do, so the
/// caller's is never out of its hands.
typedef struct {
    bool used;
    Service session;
    TransferMemory work;
} OwnRenderer;
#define OWN_RENDERERS 4u
static OwnRenderer renderers[OWN_RENDERERS];

/// audren:u, opened at the first call and kept for the run.
static Service audren_manager;

static Service *AudrenManager(Result *rc) {
    mutexLock(&manager_lock);
    *rc = serviceIsActive(&audren_manager) ? 0 : smGetService(&audren_manager, "audren:u");
    mutexUnlock(&manager_lock);
    return R_SUCCEEDED(*rc) ? &audren_manager : NULL;
}

/// The parameter as audren:u takes it (0x34 bytes): the SDK's first 32 bytes, the voice drop flag,
/// a byte each for the rendering device and the execution mode, the splitters, an external
/// context of nought and the revision.
typedef struct {
    uint8_t bytes[0x34];
} RendererParameterInternal;

static RendererParameterInternal Internal(const NnAudioRendererParameter *p) {
    RendererParameterInternal in;
    memset(&in, 0, sizeof(in));
    memcpy(in.bytes, p, 0x20);
    in.bytes[0x20] = p->voice_drop;
    in.bytes[0x22] = (uint8_t)p->rendering_device;
    in.bytes[0x23] = (uint8_t)p->execution_mode;
    memcpy(in.bytes + 0x24, &p->splitter_count, 4);
    memcpy(in.bytes + 0x28, &p->splitter_send_channel_count, 4);
    memcpy(in.bytes + 0x30, &p->revision, 4);
    return in;
}

/// GetWorkBufferSize (1).
SDK_FUNCTION size_t NnAudioGetAudioRendererWorkBufferSize(const NnAudioRendererParameter *param) {
    SdkOwnCall(SDK_AUDIO_RENDERER_WORK);
    Result rc;
    Service *const manager = AudrenManager(&rc);
    uint64_t size = 0;
    if (manager != NULL) {
        const RendererParameterInternal in = Internal(param);
        rc = serviceDispatchInOut(manager, 1, in, size);
    }
    Note(rc);
    return R_SUCCEEDED(rc) ? (size_t)size : 0u;
}

/// OpenAudioRenderer (0): the parameter, the work buffer's size, the process and a work buffer of
/// that size as transfer memory; then the renderer's frame event (QuerySystemEvent, 7) attached to
/// @p event.
SDK_FUNCTION uint32_t NnAudioOpenAudioRenderer(NnAudioRendererHandle *handle,
                                               NnOsSystemEvent *event,
                                               const NnAudioRendererParameter *param, void *work,
                                               size_t size) {
    SdkOwnCall(SDK_AUDIO_OPEN_RENDERER);
    Result rc;
    Service *const manager = AudrenManager(&rc);
    if (manager == NULL) {
        return rc;
    }
    OwnRenderer *own = NULL;
    mutexLock(&manager_lock);
    for (uint32_t i = 0; i < OWN_RENDERERS && own == NULL; ++i) {
        if (!renderers[i].used) {
            own = &renderers[i];
            memset(own, 0, sizeof(*own));
            own->used = true;
        }
    }
    mutexUnlock(&manager_lock);
    if (own == NULL) {
        return SDK_OWN_UNSUPPORTED;
    }
    (void)work;
    rc = tmemCreate(&own->work, (size + 0xFFFu) & ~(size_t)0xFFF, Perm_None);
    if (R_SUCCEEDED(rc)) {
        const struct {
            RendererParameterInternal param;
            uint32_t pad;
            uint64_t work_size;
            uint64_t aruid;
        } in = { Internal(param), 0, size, appletGetAppletResourceUserId() };
        rc = serviceDispatchIn(manager, 0, in, .in_send_pid = true, .in_num_handles = 2,
                               .in_handles = { own->work.handle, CUR_PROCESS_HANDLE },
                               .out_num_objects = 1, .out_objects = &own->session, );
        if (R_FAILED(rc)) {
            tmemClose(&own->work);
        }
    }
    if (R_SUCCEEDED(rc) && event != NULL) {
        Handle frame = INVALID_HANDLE;
        rc = serviceDispatch(&own->session, 7, .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
                             .out_handles = &frame, );
        if (R_SUCCEEDED(rc)) {
            NnOsAttachReadableHandleToSystemEvent(event, frame, true, NN_OS_CLEAR_AUTO);
        } else {
            serviceClose(&own->session);
            tmemClose(&own->work);
        }
    }
    if (R_FAILED(rc)) {
        own->used = false;
        return rc;
    }
    handle->session = own;
    handle->context = NULL;
    return 0;
}

static OwnRenderer *Own(NnAudioRendererHandle handle) {
    OwnRenderer *const own = (OwnRenderer *)handle.session;
    return own != NULL && own->used ? own : NULL;
}

/// The renderer's session goes, then its work buffer.
SDK_FUNCTION void NnAudioCloseAudioRenderer(NnAudioRendererHandle handle) {
    SdkOwnCall(SDK_AUDIO_CLOSE_RENDERER);
    OwnRenderer *const own = Own(handle);
    if (own != NULL) {
        serviceClose(&own->session);
        tmemClose(&own->work);
        own->used = false;
    }
}

/// A command with no arguments on a renderer's session: its result.
static Result Ask(NnAudioRendererHandle handle, uint32_t command) {
    OwnRenderer *const own = Own(handle);
    return own != NULL ? serviceDispatch(&own->session, command) : SDK_OWN_UNSUPPORTED;
}

/// A command that answers a word: the word, nought when it was refused.
static uint32_t AskWord(NnAudioRendererHandle handle, uint32_t command) {
    OwnRenderer *const own = Own(handle);
    uint32_t word = 0;
    const Result rc =
        own != NULL ? serviceDispatchOut(&own->session, command, word) : SDK_OWN_UNSUPPORTED;
    Note(rc);
    return R_SUCCEEDED(rc) ? word : 0u;
}

SDK_FUNCTION uint32_t NnAudioStartAudioRenderer(NnAudioRendererHandle handle) {
    SdkOwnCall(SDK_AUDIO_START_RENDERER);
    return Ask(handle, 5);
}

SDK_FUNCTION void NnAudioStopAudioRenderer(NnAudioRendererHandle handle) {
    SdkOwnCall(SDK_AUDIO_STOP_RENDERER);
    Note(Ask(handle, 6));
}

SDK_FUNCTION uint32_t NnAudioGetAudioRendererState(NnAudioRendererHandle handle) {
    SdkOwnCall(SDK_AUDIO_RENDERER_STATE);
    return AskWord(handle, 3);
}

SDK_FUNCTION int32_t NnAudioGetAudioRendererSampleRate(NnAudioRendererHandle handle) {
    SdkOwnCall(SDK_AUDIO_RENDERER_RATE);
    return (int32_t)AskWord(handle, 0);
}

SDK_FUNCTION int32_t NnAudioGetAudioRendererSampleCount(NnAudioRendererHandle handle) {
    SdkOwnCall(SDK_AUDIO_RENDERER_COUNT);
    return (int32_t)AskWord(handle, 1);
}

SDK_FUNCTION int32_t NnAudioGetAudioRendererMixBufferCount(NnAudioRendererHandle handle) {
    SdkOwnCall(SDK_AUDIO_RENDERER_MIX);
    return (int32_t)AskWord(handle, 2);
}

SDK_FUNCTION void NnAudioSetAudioRendererRenderingTimeLimit(NnAudioRendererHandle handle,
                                                           int32_t percent) {
    SdkOwnCall(SDK_AUDIO_SET_RENDERER_LIMIT);
    OwnRenderer *const own = Own(handle);
    const uint32_t in = (uint32_t)percent;
    Note(own != NULL ? serviceDispatchIn(&own->session, 8, in) : SDK_OWN_UNSUPPORTED);
}

SDK_FUNCTION int32_t NnAudioGetAudioRendererRenderingTimeLimit(NnAudioRendererHandle handle) {
    SdkOwnCall(SDK_AUDIO_RENDERER_LIMIT);
    return (int32_t)AskWord(handle, 9);
}

SDK_FUNCTION void NnAudioSetAudioRendererVoiceDropParameter(NnAudioRendererHandle handle,
                                                           float parameter) {
    SdkOwnCall(SDK_AUDIO_SET_RENDERER_DROP);
    OwnRenderer *const own = Own(handle);
    Note(own != NULL ? serviceDispatchIn(&own->session, 12, parameter) : SDK_OWN_UNSUPPORTED);
}

SDK_FUNCTION float NnAudioGetAudioRendererVoiceDropParameter(NnAudioRendererHandle handle) {
    SdkOwnCall(SDK_AUDIO_RENDERER_DROP);
    OwnRenderer *const own = Own(handle);
    float parameter = -1.0f;
    Note(own != NULL ? serviceDispatchOut(&own->session, 13, parameter) : SDK_OWN_UNSUPPORTED);
    return parameter;
}

/// audren:u's InvalidUpdateInfo (audio 41): an update or an answer whose sections are not the
/// sizes its header says.
#define AUDIO_INVALID_UPDATE_INFO MAKERESULT(153, 41)

/// The update's header words, by what each holds the size of.
enum {
    HEADER_REVISION = 0,
    HEADER_BEHAVIOUR = 1,
    HEADER_POOLS = 2,
    HEADER_VOICES = 3,
    HEADER_RESOURCES = 4,
    HEADER_EFFECTS = 5,
    HEADER_MIXES = 6,
    HEADER_SINKS = 7,
    HEADER_PERFORMANCE = 8,
    HEADER_SPLITTERS = 9,
    HEADER_RENDER_INFO = 10,
    HEADER_TOTAL = 15,
};

/// The SDK's RequestUpdateAudioRenderer, in its order: the header, the managers' sections into the
/// config's first buffer, RequestUpdate (4) with it and the second, the managers on the answer,
/// where it says the frames rendered so far, and the behaviour manager's errors last.
SDK_FUNCTION uint32_t NnAudioRequestUpdateAudioRenderer(NnAudioRendererHandle handle,
                                                        const NnAudioRendererConfig *config) {
    SdkOwnCall(SDK_AUDIO_UPDATE_RENDERER);
    OwnRenderer *const own = Own(handle);
    if (own == NULL) {
        return SDK_OWN_UNSUPPORTED;
    }
    uint8_t *const in = (uint8_t *)config->in;
    uint32_t header[NN_AUDIO_UPDATE_HEADER / 4u];
    memset(header, 0, sizeof(header));
    header[HEADER_REVISION] = NnAudioBehaviourGetRevision(config->behaviour);
    size_t at = NN_AUDIO_UPDATE_HEADER;
    size_t n = NnAudioBehaviourUpdateIn(config->behaviour, in + at);
    header[HEADER_BEHAVIOUR] = (uint32_t)n;
    at += n;
    at += header[HEADER_POOLS] = (uint32_t)NnAudioPoolUpdateIn(config->pools, in + at);
    size_t voices = 0, resources = 0;
    NnAudioVoiceUpdateIn(config->voices, in + at, &voices, &resources);
    header[HEADER_VOICES] = (uint32_t)voices;
    header[HEADER_RESOURCES] = (uint32_t)resources;
    at += voices + resources;
    at += header[HEADER_EFFECTS] = (uint32_t)NnAudioEffectUpdateIn(config->effects, in + at);
    at += header[HEADER_SPLITTERS] = (uint32_t)NnAudioSplitterUpdateIn(config->splitters, in + at);
    at += header[HEADER_MIXES] = (uint32_t)NnAudioMixUpdateIn(config->mixes, in + at);
    at += header[HEADER_SINKS] = (uint32_t)NnAudioSinkUpdateIn(config->sinks, in + at);
    at += header[HEADER_PERFORMANCE] =
        (uint32_t)NnAudioPerformanceUpdateIn(config->performance, in + at);
    header[HEADER_TOTAL] = (uint32_t)at;
    memcpy(in, header, sizeof(header));
    if (at > config->in_size) {
        return AUDIO_INVALID_UPDATE_INFO;
    }
    static uint8_t performance[0x10];
    Result rc = serviceDispatch(
        &own->session, 4,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In,
                          SfBufferAttr_HipcMapAlias | SfBufferAttr_Out,
                          SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { in, at }, { config->out, config->out_size }, { performance, 0 } }, );
    if (R_FAILED(rc)) {
        if (rc == AUDIO_INVALID_UPDATE_INFO) {
            NnAudioBehaviourCheckErrors(config->behaviour);
        }
        return rc;
    }
    const uint8_t *const out = (const uint8_t *)config->out;
    uint32_t answer[NN_AUDIO_UPDATE_HEADER / 4u];
    memcpy(answer, out, sizeof(answer));
    at = NN_AUDIO_UPDATE_HEADER;
    const struct {
        size_t (*read)(NnAudioManager *, const void *);
        NnAudioManager *manager;
        uint32_t word;
    } sections[] = {
        { NnAudioPoolUpdateOut, config->pools, HEADER_POOLS },
        { NnAudioVoiceUpdateOut, config->voices, HEADER_VOICES },
        { NnAudioEffectUpdateOut, config->effects, HEADER_EFFECTS },
        { NnAudioSinkUpdateOut, config->sinks, HEADER_SINKS },
        { NnAudioPerformanceUpdateOut, config->performance, HEADER_PERFORMANCE },
        { NnAudioBehaviourUpdateOut, config->behaviour, HEADER_BEHAVIOUR },
    };
    for (size_t i = 0; i < sizeof(sections) / sizeof(sections[0]); ++i) {
        n = sections[i].read(sections[i].manager, out + at);
        if (n != answer[sections[i].word]) {
            return AUDIO_INVALID_UPDATE_INFO;
        }
        at += n;
    }
    if (config->frames != NULL) {
        if (answer[HEADER_RENDER_INFO] != 0x10u) {
            return AUDIO_INVALID_UPDATE_INFO;
        }
        memcpy(config->frames, out + at, sizeof(*config->frames));
    }
    if (answer[HEADER_TOTAL] != config->out_size) {
        return AUDIO_INVALID_UPDATE_INFO;
    }
    return NnAudioBehaviourCheckErrors(config->behaviour);
}

// -------------------------------------------------------------- the renderer's managers' code

/// What a manager writes into an update: its first section and its own, one after the other.
static size_t Write(const NnAudioManager *m, void *at) {
    uint8_t *const to = (uint8_t *)at;
    if (m->first_size != 0u) {
        memcpy(to, m->first, m->first_size);
    }
    if (m->in_size != 0u) {
        memcpy(to + m->first_size, m->in, m->in_size);
    }
    return m->first_size + m->in_size;
}

SDK_FUNCTION size_t NnAudioBehaviourUpdateIn(const NnAudioManager *m, void *at) {
    return Write(m, at);
}

SDK_FUNCTION uint32_t NnAudioBehaviourGetRevision(const NnAudioManager *m) {
    return m->revision;
}

/// The errors the renderer reported are the probe's to read (audio.c): here none stops it.
SDK_FUNCTION uint32_t NnAudioBehaviourCheckErrors(NnAudioManager *m) {
    (void)m;
    return 0;
}

SDK_FUNCTION size_t NnAudioBehaviourUpdateOut(NnAudioManager *m, const void *at) {
    (void)at;
    return m->out_size;
}

SDK_FUNCTION size_t NnAudioPoolUpdateIn(NnAudioManager *m, void *at) {
    return Write(m, at);
}

SDK_FUNCTION size_t NnAudioPoolUpdateOut(NnAudioManager *m, const void *at) {
    (void)at;
    return m->out_size;
}

SDK_FUNCTION size_t NnAudioVoiceUpdateIn(NnAudioManager *m, void *at, size_t *voices,
                                         size_t *resources) {
    *resources = m->first_size;
    *voices = m->in_size;
    return Write(m, at);
}

SDK_FUNCTION size_t NnAudioVoiceUpdateOut(NnAudioManager *m, const void *at) {
    (void)at;
    return m->out_size;
}

SDK_FUNCTION size_t NnAudioEffectUpdateIn(NnAudioManager *m, void *at) {
    return Write(m, at);
}

SDK_FUNCTION size_t NnAudioEffectUpdateOut(NnAudioManager *m, const void *at) {
    (void)at;
    return m->out_size;
}

SDK_FUNCTION size_t NnAudioSplitterUpdateIn(NnAudioManager *m, void *at) {
    return Write(m, at);
}

SDK_FUNCTION size_t NnAudioMixUpdateIn(NnAudioManager *m, void *at) {
    return Write(m, at);
}

SDK_FUNCTION size_t NnAudioSinkUpdateIn(NnAudioManager *m, void *at) {
    return Write(m, at);
}

SDK_FUNCTION size_t NnAudioSinkUpdateOut(NnAudioManager *m, const void *at) {
    (void)at;
    return m->out_size;
}

SDK_FUNCTION size_t NnAudioPerformanceUpdateIn(NnAudioManager *m, void *at) {
    return Write(m, at);
}

SDK_FUNCTION size_t NnAudioPerformanceUpdateOut(NnAudioManager *m, const void *at) {
    (void)at;
    return m->out_size;
}

// --------------------------------------------------------------------------------- nn::codec

/// hwopus's OutOfOpusDecoders (HwOpus 385), which the SDK gives as OpusResult OutOfResource; its
/// InputDataTooSmall (8), BufferTooSmall (3) and LibOpusInvalidPacket (17) as a short input, a
/// short output and broken data. Any other result is given as it is, so a case names it.
static int32_t OpusResultOf(Result rc) {
    if (R_SUCCEEDED(rc)) {
        return NN_CODEC_OPUS_OK;
    }
    if (R_MODULE(rc) == 111) {
        switch (R_DESCRIPTION(rc)) {
        case 385:
            return NN_CODEC_OPUS_OUT_OF_RESOURCE;
        case 8:
            return NN_CODEC_OPUS_SHORT_INPUT;
        case 3:
            return NN_CODEC_OPUS_SHORT_OUTPUT;
        case 17:
            return NN_CODEC_OPUS_BROKEN_DATA;
        default:
            break;
        }
    }
    return (int32_t)rc;
}

/// GetWorkBufferSize (1) on hwopus, opened for the call.
SDK_FUNCTION size_t NnCodecGetOpusWorkBufferSize(int32_t rate, int32_t channels, int32_t option) {
    SdkOwnCall(SDK_CODEC_OPUS_WORK);
    (void)option;
    Service hwopus;
    Result rc = smGetService(&hwopus, "hwopus");
    uint32_t size = 0;
    if (R_SUCCEEDED(rc)) {
        const struct {
            int32_t rate;
            int32_t channels;
        } in = { rate, channels };
        rc = serviceDispatchInOut(&hwopus, 1, in, size);
        serviceClose(&hwopus);
    }
    Note(rc);
    return R_SUCCEEDED(rc) ? size : 0u;
}

/// libnx's decoder, which asks for its own work buffer: the caller's is left as it is.
SDK_FUNCTION int32_t NnCodecInitializeOpusDecoder(NnCodecOpusDecoder *decoder, int32_t rate,
                                                  int32_t channels, void *work, size_t size,
                                                  int32_t option) {
    SdkOwnCall(SDK_CODEC_OPUS_INITIALIZE);
    (void)work, (void)size, (void)option;
    memset(&decoder->own, 0, sizeof(decoder->own));
    const Result rc = hwopusDecoderInitialize(&decoder->own, rate, channels);
    if (R_SUCCEEDED(rc)) {
        decoder->session = 1;
        decoder->sample_rate = rate;
        decoder->channel_count = channels;
        decoder->decode_us = 0;
        decoder->reset = 0;
    }
    return OpusResultOf(rc);
}

SDK_FUNCTION int32_t NnCodecDecodeOpus(NnCodecOpusDecoder *decoder, size_t *consumed,
                                       int32_t *samples, int16_t *out, size_t out_size,
                                       const void *in, size_t in_size) {
    SdkOwnCall(SDK_CODEC_OPUS_DECODE);
    s32 used = 0, got = 0;
    const Result rc =
        hwopusDecodeInterleaved(&decoder->own, &used, &got, in, in_size, out, out_size);
    if (R_SUCCEEDED(rc)) {
        *consumed = (size_t)used;
        *samples = got;
    }
    return OpusResultOf(rc);
}

SDK_FUNCTION void NnCodecFinalizeOpusDecoder(NnCodecOpusDecoder *decoder) {
    SdkOwnCall(SDK_CODEC_OPUS_FINALIZE);
    if (decoder->session != 0u) {
        hwopusDecoderExit(&decoder->own);
    }
    memset(decoder, 0, offsetof(NnCodecOpusDecoder, own));
}
