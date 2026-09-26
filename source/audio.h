// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// A client of the audio renderer that writes the renderer's parameters itself. libnx's driver
/// has no field for a voice's sample rate conversion quality, no effects and no circular buffer
/// sink, and those are what the audio family asks about.
///
/// One renderer, laid out once: a final mix of two buffers feeding the device sink and two
/// circular buffer sinks; a submix of four buffers feeding the final mix at a gain the caller
/// sets, with a limiter and then an aux effect on it. The aux takes the submix's first two
/// buffers into a ring the program reads, and returns into the other two, which go nowhere.

enum {
    AUDIO_VOICES = 8,
    AUDIO_FINAL_MIX = 0,
    AUDIO_SUBMIX = 1,
    /// The circular buffer sinks: one for the listening sequence, one for the cases.
    AUDIO_SINK_LISTEN = 1,
    AUDIO_SINK_CASES = 2,
};

/// A voice's sample rate conversion quality, as the renderer numbers it.
typedef enum {
    AUDIO_QUALITY_DEFAULT = 0,
    AUDIO_QUALITY_HIGH = 1,
    AUDIO_QUALITY_LOW = 2,
} AudioQuality;

/// Opens the renderer at 48 kHz and attaches its memory pool. On failure @p error says which
/// step failed and with what.
bool AudioOpen(char *error, size_t error_size);
void AudioClose(void);

/// The renderer revision the update carries, as a number.
unsigned AudioRevision(void);

/// Room for @p samples 16-bit samples in the renderer's memory pool, 64-byte aligned, zeroed;
/// NULL when the pool is full. Everything a voice plays has to come from here.
int16_t *AudioAlloc(size_t samples);

/// Makes what the program wrote into the pool visible to the renderer, and what the renderer
/// wrote visible to the program.
void AudioSync(const void *address, size_t bytes);

/// Starts @p voice on @p samples of mono 16-bit audio at 48 kHz, into buffers 0 and 1 of
/// @p mix at @p left and @p right.
void AudioPlay(unsigned voice, const int16_t *data, size_t samples, bool loop,
               AudioQuality quality, unsigned mix, float left, float right);
void AudioStop(unsigned voice);
/// Whether the buffer a voice was started on has ended; a looping one never does.
bool AudioVoiceDone(unsigned voice);
/// Samples the voice has played since it started.
uint64_t AudioVoicePlayed(unsigned voice);

/// The gain from each of the submix's first two buffers to the final mix's.
void AudioSubmixGain(float gain);

/// The limiter on the submix's first two buffers.
typedef struct {
    bool enabled;
    uint8_t mode;
    float threshold;
    float attack;
    float release;
    unsigned lookahead;
} AudioLimiterSettings;
/// Sets the limiter and starts it from rest on the next update.
void AudioLimiter(const AudioLimiterSettings *settings);

/// Starts the aux effect recording the submix into its ring from the start of the ring, or
/// stops it.
void AudioCapture(bool on);
/// The aux ring: frames of 240 samples of the submix's buffer 0, then 240 of buffer 1.
const int32_t *AudioCaptureRing(size_t *frames);

/// Puts a circular buffer sink in or out of use.
void AudioRecord(unsigned sink, bool on);
/// A circular buffer sink's ring: frames of 240 samples of the final mix's buffer 0, then 240
/// of buffer 1.
const int16_t *AudioRecordRing(unsigned sink, size_t *frames);
/// The write offset the sink last reported, and the largest it has reported since it was put
/// in use.
uint32_t AudioRecordOffset(unsigned sink);
uint32_t AudioRecordOffsetSeen(unsigned sink);

/// One update, then a wait for the next rendered frame (at most 100 ms). False when the update
/// failed; the reason is kept for AudioError.
bool AudioStep(void);
/// The first failure since the renderer opened, empty when none: a failed update or an error
/// the renderer reported for a parameter.
const char *AudioError(void);

/// Milliseconds since some fixed moment, for the timeouts around steps.
uint64_t AudioNowMs(void);

#ifdef __cplusplus
}
#endif
