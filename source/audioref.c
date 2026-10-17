// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdlib.h>

#include "audioref.h"

static const double PI = 3.14159265358979323846;
static const double FULL_SCALE = 32768.0;

/// The phase of @p hz at sample @p n, reduced before it is scaled, so a long run keeps its
/// precision.
static double Phase(double hz, size_t n) {
    return 2.0 * PI * fmod(hz * (double)n, (double)AUDIO_RATE) / (double)AUDIO_RATE;
}

static double Envelope(size_t n, size_t count, size_t fade) {
    if (fade == 0u || count < 2u * fade) {
        return 1.0;
    }
    if (n < fade) {
        return 0.5 - 0.5 * cos(PI * (double)n / (double)fade);
    }
    if (n >= count - fade) {
        return 0.5 - 0.5 * cos(PI * (double)(count - 1u - n) / (double)fade);
    }
    return 1.0;
}

static int16_t Sample(double value) {
    const double scaled = floor(value * FULL_SCALE + 0.5);
    if (scaled > 32767.0) {
        return 32767;
    }
    if (scaled < -32768.0) {
        return -32768;
    }
    return (int16_t)scaled;
}

void AudioRefTone(int16_t *out, size_t count, double hz, double amplitude, size_t fade) {
    for (size_t n = 0; n < count; ++n) {
        out[n] = Sample(amplitude * sin(Phase(hz, n)) * Envelope(n, count, fade));
    }
}

/// The next white sample, uniform over [-1, 1).
static double White(uint32_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return (double)*state / 2147483648.0 - 1.0;
}

void AudioRefNoise(int16_t *out, size_t count, double amplitude, uint32_t seed, size_t fade) {
    uint32_t state = seed != 0u ? seed : 1u;
    for (size_t n = 0; n < count; ++n) {
        out[n] = Sample(amplitude * White(&state) * Envelope(n, count, fade));
    }
}

void AudioRefHiss(int16_t *out, size_t count, double amplitude, uint32_t seed, size_t fade) {
    uint32_t state = seed != 0u ? seed : 1u;
    double previous = White(&state);
    for (size_t n = 0; n < count; ++n) {
        const double current = White(&state);
        out[n] = Sample(amplitude * 0.5 * (current - previous) * Envelope(n, count, fade));
        previous = current;
    }
}

void AudioRefSweep(int16_t *out, size_t count, double from_hz, double to_hz, double amplitude,
                   size_t fade) {
    const double seconds = (double)count / (double)AUDIO_RATE;
    const double k = log(to_hz / from_hz);
    for (size_t n = 0; n < count; ++n) {
        const double t = (double)n / (double)AUDIO_RATE;
        const double phase = 2.0 * PI * from_hz * seconds / k * (exp(k * t / seconds) - 1.0);
        out[n] = Sample(amplitude * sin(fmod(phase, 2.0 * PI)) * Envelope(n, count, fade));
    }
}

static double Level(const void *x, size_t count, double hz, int wide) {
    if (count == 0u) {
        return 0.0;
    }
    double re = 0.0;
    double im = 0.0;
    for (size_t n = 0; n < count; ++n) {
        const double v =
            wide == 2 ? ((const double *)x)[n]
                      : (wide ? (double)((const int32_t *)x)[n] / FULL_SCALE
                              : (double)((const int16_t *)x)[n] / FULL_SCALE);
        const double phase = Phase(hz, n);
        re += v * cos(phase);
        im += v * sin(phase);
    }
    return 2.0 * sqrt(re * re + im * im) / (double)count;
}

double AudioRefLevel16(const int16_t *x, size_t count, double hz) {
    return Level(x, count, hz, 0);
}

double AudioRefLevel32(const int32_t *x, size_t count, double hz) {
    return Level(x, count, hz, 1);
}

void AudioRefChannel16(const int16_t *frames, size_t frame_count, unsigned channels,
                       unsigned frame, unsigned channel, int16_t *out) {
    for (size_t f = 0; f < frame_count; ++f) {
        const int16_t *from = frames + (f * channels + channel) * frame;
        for (unsigned s = 0; s < frame; ++s) {
            out[f * frame + s] = from[s];
        }
    }
}

void AudioRefChannel32(const int32_t *frames, size_t frame_count, unsigned channels,
                       unsigned frame, unsigned channel, int32_t *out) {
    for (size_t f = 0; f < frame_count; ++f) {
        const int32_t *from = frames + (f * channels + channel) * frame;
        for (unsigned s = 0; s < frame; ++s) {
            out[f * frame + s] = from[s];
        }
    }
}

double AudioRefReciprocalEstimate(double a) {
    const int32_t q = (int32_t)(a * 512.0);
    const double r = 1.0 / (((double)q + 0.5) / 512.0);
    const int32_t s = (int32_t)(256.0 * r + 0.5);
    return (double)s / 256.0;
}

const char *AudioRefModeZeroReading(double level, double rule, double two_step) {
    if (AudioRefWithin(level, rule, 0.05)) {
        return "the rule";
    }
    if (AudioRefWithin(level, two_step, 0.05)) {
        return "the two-step reciprocal";
    }
    return "neither";
}

void AudioRefLimit(const AudioRefLimiter *limiter, const int16_t *in, size_t count, double *out) {
    double detector = 0.0;
    double gain = 1.0;
    const unsigned delay = limiter->lookahead > 0u ? limiter->lookahead : 1u;
    double *line = calloc(delay, sizeof(double));
    if (line == NULL) {
        for (size_t n = 0; n < count; ++n) {
            out[n] = 0.0;
        }
        return;
    }
    unsigned at = 0;
    for (size_t n = 0; n < count; ++n) {
        const double sample = (double)in[n] / FULL_SCALE * limiter->input_gain;
        const double rectified = fabs(sample);
        detector += (rectified - detector) *
                    (rectified > detector ? limiter->attack : limiter->release);
        double target = 1.0;
        if (detector > limiter->threshold) {
            if (limiter->two_step) {
                const double r0 = AudioRefReciprocalEstimate(detector);
                target = limiter->threshold * (2.0 - detector * (2.0 - detector * r0));
            } else {
                target = limiter->threshold / detector;
            }
        }
        gain += (target - gain) * (target < gain ? limiter->attack : limiter->release);
        out[n] = line[at] * gain * limiter->output_gain;
        line[at] = sample;
        at = (at + 1u) % delay;
    }
    free(line);
}

double AudioRefLimitedLevel(const AudioRefLimiter *limiter, double amplitude, double hz,
                            size_t settle, size_t window) {
    const size_t count = settle + window;
    int16_t *in = malloc(count * sizeof(int16_t));
    double *out = malloc(count * sizeof(double));
    double level = -1.0;
    if (in != NULL && out != NULL) {
        AudioRefTone(in, count, hz, amplitude, 0u);
        AudioRefLimit(limiter, in, count, out);
        level = Level(out + settle, window, hz, 2);
    }
    free(in);
    free(out);
    return level;
}

int AudioRefWithin(double got, double want, double fraction) {
    return fabs(got - want) <= fraction * fabs(want);
}

double AudioRefDecibels(double level, double reference) {
    if (level <= 0.0 || reference <= 0.0) {
        return -999.0;
    }
    return 20.0 * log10(level / reference);
}

double AudioRefPeak16(const int16_t *x, size_t count) {
    int peak = 0;
    for (size_t n = 0; n < count; ++n) {
        const int magnitude = x[n] < 0 ? -(int)x[n] : (int)x[n];
        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    return (double)peak / FULL_SCALE;
}
