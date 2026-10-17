// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "audio.h"
#include "audioref.h"

// The renderer's parameters as the renderer reads them, field for field. libnx's structures
// stop at what its driver uses; these carry the rest.

typedef struct {
    uint32_t revision;
    uint32_t behaviour_size;
    uint32_t pool_size;
    uint32_t voice_size;
    uint32_t resource_size;
    uint32_t effect_size;
    uint32_t mix_size;
    uint32_t sink_size;
    uint32_t performance_size;
    uint32_t splitter_size;
    uint32_t render_info_size;
    uint32_t reserved[4];
    uint32_t total_size;
} UpdateHeader;

typedef struct {
    uint32_t revision;
    uint32_t reserved;
    uint64_t flags;
} BehaviourIn;

typedef struct {
    uint32_t code;
    uint32_t reserved;
    uint64_t address;
} ErrorOut;

typedef struct {
    ErrorOut errors[10];
    uint32_t count;
    uint8_t reserved[12];
} BehaviourOut;

typedef struct {
    uint64_t address;
    uint64_t size;
    uint32_t state;
    uint8_t in_use;
    uint8_t reserved[11];
} PoolIn;

typedef struct {
    uint32_t state;
    uint8_t reserved[12];
} PoolOut;

typedef struct {
    uint32_t id;
    float volumes[24];
    uint8_t in_use;
    uint8_t reserved[11];
} ResourceIn;

typedef struct {
    uint64_t address;
    uint64_t size;
    int32_t start;
    int32_t end;
    uint8_t loop;
    uint8_t ended;
    uint8_t sent;
    uint8_t reserved;
    int32_t loop_count;
    uint64_t context;
    uint64_t context_size;
    uint32_t loop_start;
    uint32_t loop_end;
} WaveBufferIn;

typedef struct {
    uint32_t id;
    uint32_t node_id;
    uint8_t is_new;
    uint8_t in_use;
    uint8_t play_state;
    uint8_t format;
    uint32_t sample_rate;
    int32_t priority;
    int32_t sort_order;
    uint32_t channel_count;
    float pitch;
    float volume;
    uint8_t biquads[24];
    uint32_t wave_buffer_count;
    uint16_t wave_buffer_index;
    uint8_t reserved0[6];
    uint64_t data_address;
    uint64_t data_size;
    uint32_t mix_id;
    uint32_t splitter_id;
    WaveBufferIn wave_buffers[4];
    uint32_t resources[6];
    uint8_t clear_drop;
    uint8_t flush_count;
    uint8_t reserved1[2];
    uint8_t flags;
    uint8_t reserved2;
    uint8_t quality;
    uint8_t reserved3[0x11];
} VoiceIn;

typedef struct {
    uint64_t played;
    uint32_t consumed;
    uint8_t dropped;
    uint8_t reserved[3];
} VoiceOut;

typedef struct {
    uint8_t type;
    uint8_t is_new;
    uint8_t enabled;
    uint8_t reserved0;
    uint32_t mix_id;
    uint64_t workbuffer;
    uint64_t workbuffer_size;
    uint32_t process_order;
    uint32_t reserved1;
    uint8_t specific[0xA0];
} EffectIn;

typedef struct {
    uint8_t state;
    uint8_t reserved[15];
} EffectOut;

typedef struct {
    int8_t inputs[24];
    int8_t outputs[24];
    uint32_t count;
    uint32_t sample_rate;
    uint32_t count_max;
    uint32_t count_limit;
    uint64_t send_info;
    uint64_t send;
    uint64_t return_info;
    uint64_t return_;
    uint32_t sample_size;
    uint32_t sample_count;
    uint32_t mix_sample_count;
} AuxSpecific;

typedef struct {
    int8_t inputs[6];
    int8_t outputs[6];
    uint16_t channel_count_max;
    uint16_t channel_count;
    uint32_t sample_rate;
    int32_t look_ahead_time_max;
    int32_t attack_time;
    int32_t release_time;
    int32_t look_ahead_time;
    float attack;
    float release;
    float threshold;
    float input_gain;
    float output_gain;
    int32_t look_ahead_min;
    int32_t look_ahead_max;
    uint8_t state;
    uint8_t statistics;
    uint8_t statistics_reset;
    uint8_t mode;
} LimiterSpecific;

typedef struct {
    float volume;
    uint32_t sample_rate;
    uint32_t buffer_count;
    uint8_t in_use;
    uint8_t dirty;
    uint8_t reserved0[2];
    int32_t mix_id;
    uint32_t effect_count;
    uint32_t node_id;
    uint8_t reserved1[8];
    float volumes[24][24];
    int32_t destination;
    uint32_t splitter;
    uint8_t reserved2[4];
} MixIn;

typedef struct {
    char name[0x100];
    uint32_t input_count;
    int8_t inputs[6];
    uint8_t reserved;
    uint8_t downmix;
    float downmix_coefficients[4];
} DeviceSpecific;

// The sample format is one byte; the three after it are reserved, and the channels start at
// 0x1C. A renderer that reads them from 0x19 is what the cases below are about.
typedef struct {
    uint64_t address;
    uint32_t size;
    uint32_t input_count;
    uint32_t sample_count;
    uint32_t previous;
    uint8_t format;
    uint8_t reserved0[3];
    int8_t inputs[6];
    uint8_t reserved1[6];
} CircularSpecific;

typedef struct {
    uint8_t type;
    uint8_t in_use;
    uint8_t reserved0[2];
    uint32_t node_id;
    uint8_t reserved1[0x18];
    union {
        DeviceSpecific device;
        CircularSpecific circular;
        uint8_t raw[0x120];
    };
} SinkIn;

typedef struct {
    uint32_t write_offset;
    uint8_t reserved[0x1C];
} SinkOut;

typedef struct {
    uint32_t node_id;
    uint8_t reserved[12];
} PerformanceIn;

typedef struct {
    uint32_t history_size;
    uint8_t reserved[12];
} PerformanceOut;

_Static_assert(sizeof(UpdateHeader) == 0x40, "update header");
_Static_assert(sizeof(BehaviourIn) == 0x10 && sizeof(BehaviourOut) == 0xB0, "behaviour");
_Static_assert(sizeof(PoolIn) == 0x20 && sizeof(PoolOut) == 0x10, "memory pool");
_Static_assert(sizeof(ResourceIn) == 0x70, "voice channel resource");
_Static_assert(sizeof(WaveBufferIn) == 0x38, "wave buffer");
_Static_assert(sizeof(VoiceIn) == 0x170 && sizeof(VoiceOut) == 0x10, "voice");
_Static_assert(offsetof(VoiceIn, wave_buffers) == 0x60 && offsetof(VoiceIn, quality) == 0x15E,
               "voice fields");
_Static_assert(sizeof(EffectIn) == 0xC0 && sizeof(EffectOut) == 0x10, "effect");
_Static_assert(sizeof(AuxSpecific) <= 0xA0 && offsetof(AuxSpecific, send_info) == 0x40, "aux");
_Static_assert(sizeof(LimiterSpecific) == 0x44 && offsetof(LimiterSpecific, mode) == 0x43,
               "limiter");
_Static_assert(sizeof(MixIn) == 0x930 && offsetof(MixIn, destination) == 0x924, "mix");
_Static_assert(offsetof(DeviceSpecific, downmix) == 0x10B, "device sink");
_Static_assert(offsetof(CircularSpecific, format) == 0x18 &&
                   offsetof(CircularSpecific, inputs) == 0x1C,
               "circular buffer sink");
_Static_assert(sizeof(SinkIn) == 0x140 && sizeof(SinkOut) == 0x20, "sink");
_Static_assert(sizeof(PerformanceIn) == 0x10 && sizeof(PerformanceOut) == 0x10, "performance");

enum {
    EFFECTS = 2,
    SINKS = 3,
    MIXES = 2,
    MIX_BUFFERS = 6,
    POOLS = EFFECTS + 4 * AUDIO_VOICES,
    EFFECT_LIMITER = 0,
    EFFECT_AUX = 1,
    EFFECT_TYPE_AUX = 2,
    EFFECT_TYPE_LIMITER = 7,
    POOL_BYTES = 8 << 20,
    // The aux and case rings hold 40 frames a channel: 0.2 s, a whole number of periods of every
    // probe tone in each frame.
    CAPTURE_FRAMES = 40,
    AUX_COUNT = CAPTURE_FRAMES * 2 * AUDIO_FRAME,
    // The listening sink holds 2.5 s, longer than the phrase it records.
    LISTEN_FRAMES = 500,
};

static const uint32_t UNUSED_MIX = 0x7FFFFFFF;
static const uint32_t UNUSED_SPLITTER = 0xFFFFFFFF;

static struct {
    bool open;
    unsigned revision;
    uint8_t *pool;
    size_t pool_used;
    void *in;
    size_t in_size;
    void *out;
    size_t out_size;
    UpdateHeader *header;
    BehaviourIn *behaviour;
    PoolIn *pools;
    ResourceIn *resources;
    VoiceIn *voices;
    EffectIn *effects;
    MixIn *mixes;
    SinkIn *sinks;
    PerformanceIn *performance;
    uint16_t node_counter[AUDIO_VOICES];
    uint64_t played[AUDIO_VOICES];
    uint32_t consumed[AUDIO_VOICES];
    uint32_t sink_offset[SINKS];
    uint32_t sink_offset_seen[SINKS];
    uint8_t *aux_send;
    uint8_t *aux_return;
    int16_t *rings[SINKS];
    uint32_t ring_bytes[SINKS];
    uint8_t *limiter_work;
    char error[160];
} r;

static void Fail(const char *what) {
    if (r.error[0] == '\0') {
        snprintf(r.error, sizeof(r.error), "%s", what);
    }
}

uint64_t AudioNowMs(void) {
    return armTicksToNs(armGetSystemTick()) / 1000000ull;
}

unsigned AudioRevision(void) {
    return (r.revision >> 24) - '0';
}

const char *AudioError(void) {
    return r.error;
}

static void *Take(size_t bytes, size_t align) {
    const size_t at = (r.pool_used + align - 1u) & ~(align - 1u);
    if (r.pool == NULL || at + bytes > (size_t)POOL_BYTES) {
        return NULL;
    }
    r.pool_used = at + bytes;
    memset(r.pool + at, 0, bytes);
    return r.pool + at;
}

int16_t *AudioAlloc(size_t samples) {
    return Take(samples * sizeof(int16_t), 64u);
}

void AudioSync(const void *address, size_t bytes) {
    armDCacheFlush((void *)address, bytes);
}

/// Lays the input out once. Every section stays where it is; each update rewrites only fields.
static void Layout(void) {
    uint8_t *at = r.in;
    r.header = (UpdateHeader *)at;
    at += sizeof(UpdateHeader);
    r.behaviour = (BehaviourIn *)at;
    at += sizeof(BehaviourIn);
    r.pools = (PoolIn *)at;
    at += sizeof(PoolIn) * POOLS;
    r.resources = (ResourceIn *)at;
    at += sizeof(ResourceIn) * AUDIO_VOICES;
    r.voices = (VoiceIn *)at;
    at += sizeof(VoiceIn) * AUDIO_VOICES;
    r.effects = (EffectIn *)at;
    at += sizeof(EffectIn) * EFFECTS;
    r.mixes = (MixIn *)at;
    at += sizeof(MixIn) * MIXES;
    r.sinks = (SinkIn *)at;
    at += sizeof(SinkIn) * SINKS;
    r.performance = (PerformanceIn *)at;

    r.header->revision = r.revision;
    r.header->behaviour_size = sizeof(BehaviourIn);
    r.header->pool_size = sizeof(PoolIn) * POOLS;
    r.header->resource_size = sizeof(ResourceIn) * AUDIO_VOICES;
    r.header->voice_size = sizeof(VoiceIn) * AUDIO_VOICES;
    r.header->effect_size = sizeof(EffectIn) * EFFECTS;
    r.header->mix_size = sizeof(MixIn) * MIXES;
    r.header->sink_size = sizeof(SinkIn) * SINKS;
    r.header->performance_size = sizeof(PerformanceIn);
    r.header->total_size = (uint32_t)r.in_size;
    r.behaviour->revision = r.revision;

    for (unsigned i = 0; i < POOLS; ++i) {
        r.pools[i].state = 6; // released
    }
    r.pools[0].address = (uint64_t)(uintptr_t)r.pool;
    r.pools[0].size = POOL_BYTES;
    r.pools[0].state = 4; // asked to attach

    for (unsigned i = 0; i < AUDIO_VOICES; ++i) {
        r.resources[i].id = i;
        r.voices[i].id = i;
    }

    for (unsigned m = 0; m < MIXES; ++m) {
        MixIn *mix = &r.mixes[m];
        mix->volume = 1.0f;
        mix->sample_rate = AUDIO_RATE;
        mix->buffer_count = m == AUDIO_FINAL_MIX ? 2u : 4u;
        mix->in_use = 1;
        mix->mix_id = (int32_t)m;
        mix->node_id = (2u << 28) | (m << 16);
        mix->destination = m == AUDIO_FINAL_MIX ? (int32_t)UNUSED_MIX : AUDIO_FINAL_MIX;
        mix->splitter = UNUSED_SPLITTER;
    }

    // The limiter, off until a case turns it on, and the aux after it.
    EffectIn *limiter = &r.effects[EFFECT_LIMITER];
    limiter->type = EFFECT_TYPE_LIMITER;
    limiter->is_new = 1;
    limiter->mix_id = AUDIO_SUBMIX;
    limiter->workbuffer = (uint64_t)(uintptr_t)r.limiter_work;
    limiter->workbuffer_size = 0x1000;
    limiter->process_order = 0;
    LimiterSpecific *l = (LimiterSpecific *)limiter->specific;
    l->inputs[0] = 0;
    l->inputs[1] = 1;
    l->outputs[0] = 0;
    l->outputs[1] = 1;
    l->channel_count_max = 2;
    l->channel_count = 2;
    l->sample_rate = AUDIO_RATE;
    l->look_ahead_min = 48;
    l->look_ahead_max = 48;
    l->input_gain = 1.0f;
    l->output_gain = 1.0f;
    l->threshold = 1.0f;
    l->attack = 0.001f;
    l->release = 0.001f;
    // Updated, so the limiter keeps its state from one update to the next; the renderer marks it
    // initialised itself on the update that sets is_new.
    l->state = 2;

    EffectIn *aux = &r.effects[EFFECT_AUX];
    aux->type = EFFECT_TYPE_AUX;
    aux->is_new = 1;
    aux->mix_id = AUDIO_SUBMIX;
    aux->process_order = 1;
    AuxSpecific *a = (AuxSpecific *)aux->specific;
    a->inputs[0] = 0;
    a->inputs[1] = 1;
    a->outputs[0] = 2;
    a->outputs[1] = 3;
    a->count = 2;
    a->sample_rate = AUDIO_RATE;
    a->count_max = AUX_COUNT;
    a->count_limit = 2;
    a->send_info = (uint64_t)(uintptr_t)r.aux_send;
    a->send = (uint64_t)(uintptr_t)(r.aux_send + 0x80);
    a->return_info = (uint64_t)(uintptr_t)r.aux_return;
    a->return_ = (uint64_t)(uintptr_t)(r.aux_return + 0x80);
    a->sample_size = sizeof(int32_t);
    a->sample_count = AUDIO_FRAME;
    a->mix_sample_count = AUDIO_FRAME;

    SinkIn *device = &r.sinks[0];
    device->type = 1;
    device->in_use = 1;
    device->node_id = 3u << 28;
    snprintf(device->device.name, sizeof(device->device.name), "MainAudioOut");
    device->device.input_count = 2;
    device->device.inputs[0] = 0;
    device->device.inputs[1] = 1;
    for (unsigned s = 1; s < SINKS; ++s) {
        SinkIn *sink = &r.sinks[s];
        sink->type = 2;
        sink->node_id = (3u << 28) | (s << 16);
        sink->circular.address = (uint64_t)(uintptr_t)r.rings[s];
        sink->circular.size = r.ring_bytes[s];
        sink->circular.input_count = 2;
        sink->circular.sample_count = AUDIO_FRAME;
        sink->circular.format = 2; // 16-bit
        sink->circular.inputs[0] = 0;
        sink->circular.inputs[1] = 1;
    }
    r.performance->node_id = 15u << 28;
}

static bool Update(void);

bool AudioOpen(char *error, size_t error_size) {
    memset(&r, 0, sizeof(r));
    const AudioRendererConfig config = {
        .output_rate = AudioRendererOutputRate_48kHz,
        .num_voices = AUDIO_VOICES,
        .num_effects = EFFECTS,
        .num_sinks = SINKS,
        .num_mix_objs = MIXES,
        .num_mix_buffers = MIX_BUFFERS,
    };
    Result rc = audrenInitialize(&config);
    if (R_FAILED(rc)) {
        snprintf(error, error_size, "audrenInitialize returned 0x%x", (unsigned)rc);
        return false;
    }
    r.revision = audrenGetRevision();
    // The output grows a renderer information section from revision 5.
    const unsigned number = (r.revision >> 24) - '0';
    r.in_size = sizeof(UpdateHeader) + sizeof(BehaviourIn) + sizeof(PoolIn) * POOLS +
                sizeof(ResourceIn) * AUDIO_VOICES + sizeof(VoiceIn) * AUDIO_VOICES +
                sizeof(EffectIn) * EFFECTS + sizeof(MixIn) * MIXES + sizeof(SinkIn) * SINKS +
                sizeof(PerformanceIn);
    r.out_size = sizeof(UpdateHeader) + sizeof(PoolOut) * POOLS + sizeof(VoiceOut) * AUDIO_VOICES +
                 sizeof(EffectOut) * EFFECTS + sizeof(SinkOut) * SINKS + sizeof(PerformanceOut) +
                 sizeof(BehaviourOut) + (number >= 5u ? 0x10u : 0u);
    r.in = memalign(AUDREN_INPUT_PARAM_ALIGNMENT, (r.in_size + 0xFFFu) & ~(size_t)0xFFF);
    r.out = memalign(AUDREN_OUTPUT_PARAM_ALIGNMENT, r.out_size);
    r.pool = memalign(AUDREN_MEMPOOL_ALIGNMENT, POOL_BYTES);
    if (r.in == NULL || r.out == NULL || r.pool == NULL) {
        snprintf(error, error_size, "no memory for the renderer's buffers");
        AudioClose();
        return false;
    }
    memset(r.in, 0, r.in_size);
    memset(r.pool, 0, POOL_BYTES);

    r.limiter_work = Take(0x1000, 0x1000);
    r.aux_send = Take(0x80 + AUX_COUNT * sizeof(int32_t), 64u);
    r.aux_return = Take(0x80 + AUX_COUNT * sizeof(int32_t), 64u);
    r.ring_bytes[AUDIO_SINK_LISTEN] = LISTEN_FRAMES * 2u * AUDIO_FRAME * sizeof(int16_t);
    r.ring_bytes[AUDIO_SINK_CASES] = CAPTURE_FRAMES * 2u * AUDIO_FRAME * sizeof(int16_t);
    r.rings[AUDIO_SINK_LISTEN] = Take(r.ring_bytes[AUDIO_SINK_LISTEN], 64u);
    r.rings[AUDIO_SINK_CASES] = Take(r.ring_bytes[AUDIO_SINK_CASES], 64u);
    Layout();
    AudioSync(r.pool, POOL_BYTES);
    r.open = true;

    // The first update attaches the pool, before the renderer starts.
    if (!Update()) {
        snprintf(error, error_size, "the first update failed: %s", r.error);
        AudioClose();
        return false;
    }
    if (r.pools[0].state != 5) {
        snprintf(error, error_size, "the memory pool did not attach (state %u)",
                 (unsigned)r.pools[0].state);
        AudioClose();
        return false;
    }
    rc = audrenStartAudioRenderer();
    if (R_FAILED(rc)) {
        snprintf(error, error_size, "audrenStartAudioRenderer returned 0x%x", (unsigned)rc);
        AudioClose();
        return false;
    }
    return true;
}

void AudioClose(void) {
    if (r.open) {
        audrenStopAudioRenderer();
    }
    audrenExit();
    free(r.in);
    free(r.out);
    free(r.pool);
    memset(&r, 0, sizeof(r));
}

void AudioPlay(unsigned voice, const int16_t *data, size_t samples, bool loop,
               AudioQuality quality, unsigned mix, float left, float right) {
    AudioPlayAt(voice, data, samples, loop, quality, mix, left, right, AUDIO_RATE);
}

void AudioPlayAt(unsigned voice, const int16_t *data, size_t samples, bool loop,
                 AudioQuality quality, unsigned mix, float left, float right, uint32_t rate) {
    VoiceIn *v = &r.voices[voice];
    ResourceIn *resource = &r.resources[voice];
    memset(v, 0, sizeof(*v));
    v->id = voice;
    v->node_id = (1u << 28) | (voice << 16) | r.node_counter[voice]++;
    v->is_new = 1;
    v->in_use = 1;
    v->play_state = 0; // started
    v->format = 2;     // 16-bit
    v->sample_rate = rate;
    v->sort_order = (int32_t)voice;
    v->channel_count = 1;
    v->pitch = 1.0f;
    v->volume = 1.0f;
    v->wave_buffer_count = 1;
    v->mix_id = mix;
    v->splitter_id = UNUSED_SPLITTER;
    for (unsigned i = 0; i < 4u; ++i) {
        v->wave_buffers[i].sent = 1;
    }
    WaveBufferIn *w = &v->wave_buffers[0];
    w->address = (uint64_t)(uintptr_t)data;
    w->size = samples * sizeof(int16_t);
    w->end = (int32_t)samples;
    w->loop = loop;
    w->sent = 0;
    // Forever, over the whole buffer. A renderer that reads only the loop flag ignores both.
    w->loop_count = loop ? -1 : 0;
    w->loop_end = loop ? (uint32_t)samples : 0u;
    v->resources[0] = voice;
    v->quality = (uint8_t)quality;
    memset(resource->volumes, 0, sizeof(resource->volumes));
    resource->volumes[0] = left;
    resource->volumes[1] = right;
    resource->in_use = 1;
    r.played[voice] = 0;
    r.consumed[voice] = 0;
}

void AudioStop(unsigned voice) {
    r.voices[voice].in_use = 0;
    r.resources[voice].in_use = 0;
}

bool AudioVoiceDone(unsigned voice) {
    return r.consumed[voice] > 0u;
}

uint64_t AudioVoicePlayed(unsigned voice) {
    return r.played[voice];
}

void AudioSubmixGain(float gain) {
    r.mixes[AUDIO_SUBMIX].volumes[0][0] = gain;
    r.mixes[AUDIO_SUBMIX].volumes[1][1] = gain;
}

void AudioLimiter(const AudioLimiterSettings *settings) {
    EffectIn *limiter = &r.effects[EFFECT_LIMITER];
    LimiterSpecific *l = (LimiterSpecific *)limiter->specific;
    limiter->enabled = settings->enabled;
    limiter->is_new = 1;
    l->mode = settings->mode;
    l->threshold = settings->threshold;
    l->attack = settings->attack;
    l->release = settings->release;
    l->look_ahead_min = (int32_t)settings->lookahead;
    l->look_ahead_max = (int32_t)settings->lookahead;
}

void AudioCapture(bool on) {
    r.effects[EFFECT_AUX].enabled = on;
}

const int32_t *AudioCaptureRing(size_t *frames) {
    const int32_t *ring = (const int32_t *)(r.aux_send + 0x80);
    AudioSync(ring, AUX_COUNT * sizeof(int32_t));
    *frames = CAPTURE_FRAMES;
    return ring;
}

void AudioRecord(unsigned sink, bool on) {
    r.sinks[sink].in_use = on;
    if (on) {
        r.sink_offset_seen[sink] = 0;
    }
}

const int16_t *AudioRecordRing(unsigned sink, size_t *frames) {
    AudioSync(r.rings[sink], r.ring_bytes[sink]);
    *frames = r.ring_bytes[sink] / (2u * AUDIO_FRAME * sizeof(int16_t));
    return r.rings[sink];
}

uint32_t AudioRecordOffset(unsigned sink) {
    return r.sink_offset[sink];
}

uint32_t AudioRecordOffsetSeen(unsigned sink) {
    return r.sink_offset_seen[sink];
}

/// One update: the parameters out, the renderer's answers in.
static bool Update(void) {
    uint64_t order = 0;
    for (unsigned i = 0; i < AUDIO_VOICES; ++i) {
        if (r.voices[i].in_use) {
            r.voices[i].sort_order = (int32_t)order++;
        }
    }
    const Result rc = audrenRequestUpdateAudioRenderer(r.in, r.in_size, r.out, r.out_size, NULL, 0);
    if (R_FAILED(rc)) {
        char what[64];
        snprintf(what, sizeof(what), "an update returned 0x%x", (unsigned)rc);
        Fail(what);
        return false;
    }

    const uint8_t *at = (const uint8_t *)r.out + sizeof(UpdateHeader);
    const PoolOut *pools = (const PoolOut *)at;
    for (unsigned i = 0; i < POOLS; ++i) {
        if (pools[i].state != 0u) {
            r.pools[i].state = pools[i].state;
        }
    }
    at += sizeof(PoolOut) * POOLS;
    const VoiceOut *voices = (const VoiceOut *)at;
    for (unsigned i = 0; i < AUDIO_VOICES; ++i) {
        if (r.voices[i].in_use) {
            r.played[i] = voices[i].played;
            r.consumed[i] = voices[i].consumed;
        }
        r.voices[i].is_new = 0;
        for (unsigned w = 0; w < 4u; ++w) {
            r.voices[i].wave_buffers[w].sent = 1;
        }
    }
    at += sizeof(VoiceOut) * AUDIO_VOICES + sizeof(EffectOut) * EFFECTS;
    for (unsigned i = 0; i < EFFECTS; ++i) {
        r.effects[i].is_new = 0;
    }
    const SinkOut *sinks = (const SinkOut *)at;
    for (unsigned i = 0; i < SINKS; ++i) {
        r.sink_offset[i] = sinks[i].write_offset;
        if (sinks[i].write_offset > r.sink_offset_seen[i]) {
            r.sink_offset_seen[i] = sinks[i].write_offset;
        }
    }
    at += sizeof(SinkOut) * SINKS + sizeof(PerformanceOut);
    const BehaviourOut *behaviour = (const BehaviourOut *)at;
    if (behaviour->count > 0u) {
        char what[96];
        snprintf(what, sizeof(what), "the renderer reported %u errors, the first 0x%x at 0x%llx",
                 (unsigned)behaviour->count, (unsigned)behaviour->errors[0].code,
                 (unsigned long long)behaviour->errors[0].address);
        Fail(what);
    }

    return true;
}

bool AudioStep(void) {
    if (!Update()) {
        return false;
    }
    // An update clears the frame event, so this waits for a frame rendered from this update's
    // parameters. The limiter depends on it: the update that restarts it has to reach a frame.
    // With a real output the first frames wait for the host's stream to open, which can take
    // seconds; a null output never waits. So the limit is generous, not a frame period.
    if (R_FAILED(eventWait(audrenGetFrameEvent(), 5000000000ull))) {
        Fail("no frame was rendered for 5 s");
        return false;
    }
    return true;
}
