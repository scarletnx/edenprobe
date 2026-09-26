// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The audio family's signals, measurements and expected answers: no renderer, so the host build
/// checks all of it before the console build plays a sample.

/// The renderer's output rate, and the samples it renders per channel in one 5 ms frame.
#define AUDIO_RATE 48000u
#define AUDIO_FRAME 240u

/// A sine of @p hz at @p amplitude of full scale, starting at phase 0. With @p fade samples of
/// raised cosine at each end, so it starts and stops without a click; 0 for none.
void AudioRefTone(int16_t *out, size_t count, double hz, double amplitude, size_t fade);

/// White noise at @p amplitude of full scale, the same samples on every machine for one seed.
void AudioRefNoise(int16_t *out, size_t count, double amplitude, uint32_t seed, size_t fade);

/// Noise with its low end taken off: each sample the difference of two white ones, so the power
/// rises with frequency and most of it lies in the top octave. Peaks at @p amplitude.
void AudioRefHiss(int16_t *out, size_t count, double amplitude, uint32_t seed, size_t fade);

/// A sweep from @p from_hz to @p to_hz, equal time per octave.
void AudioRefSweep(int16_t *out, size_t count, double from_hz, double to_hz, double amplitude,
                   size_t fade);

/// The peak amplitude of the part of @p x at @p hz, as a fraction of full scale (32768): one bin
/// of a discrete Fourier transform over the whole run. Exact for any tone with a whole number of
/// periods in the run, and blind to every other such tone.
double AudioRefLevel16(const int16_t *x, size_t count, double hz);
double AudioRefLevel32(const int32_t *x, size_t count, double hz);

/// One channel out of frames that hold their channels one after the other, @p frame samples
/// each: the layout both the circular buffer sink and the aux effect write.
void AudioRefChannel16(const int16_t *frames, size_t frame_count, unsigned channels,
                       unsigned frame, unsigned channel, int16_t *out);
void AudioRefChannel32(const int32_t *frames, size_t frame_count, unsigned channels,
                       unsigned frame, unsigned channel, int32_t *out);

/// The limiter's rule: a detector that follows the rectified input, rising by @p attack and
/// falling by @p release of the distance each sample; above @p threshold the gain goes to
/// the threshold over the detector, by the same two rates; the output is the input delayed by
/// @p lookahead samples times that gain. Levels are fractions of full scale.
typedef struct {
    double threshold;
    double attack;
    double release;
    double input_gain;
    double output_gain;
    unsigned lookahead;
} AudioRefLimiter;

/// Runs the rule over @p count samples of @p in, writing the output as fractions of full scale.
void AudioRefLimit(const AudioRefLimiter *limiter, const int16_t *in, size_t count, double *out);

/// The steady level at @p hz that the rule gives a sine of @p amplitude: @p settle samples to
/// settle, then measured over @p window samples.
double AudioRefLimitedLevel(const AudioRefLimiter *limiter, double amplitude, double hz,
                            size_t settle, size_t window);

/// Whether @p got is within @p fraction of @p want.
int AudioRefWithin(double got, double want, double fraction);

/// A level as decibels against @p reference, for the lines a person reads.
double AudioRefDecibels(double level, double reference);

/// The largest magnitude in @p x, as a fraction of full scale.
double AudioRefPeak16(const int16_t *x, size_t count);

#ifdef __cplusplus
}
#endif
