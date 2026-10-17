// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../audioref.h"
#include "../check.h"
#include "../report.h"
#include "tests.h"

// The audio family's measurements and its limiter rule, checked against answers worked out by
// hand, so a level the family reports from a renderer is a level and not the measuring code's
// own error.

static const char *const SUITE = "reference";

#define RUN 9600u

static int16_t a[RUN];
static int16_t b[RUN];

void RunReferenceAudio(void) {
    // The capture reads a ring whose frames it may catch in any order, so each tone must fill a
    // frame with whole periods; 11 kHz does, 55 of them.
    static const double TONES[] = {1000.0, 1600.0, 11000.0};
    bool whole = true;
    for (size_t i = 0; i < sizeof(TONES) / sizeof(TONES[0]); ++i) {
        const double periods = TONES[i] * AUDIO_FRAME / AUDIO_RATE;
        whole = whole && periods == floor(periods);
    }
    CHECK_TRUE(SUITE, "audio: each probe tone fills a frame with whole periods", CHECK_NONE,
               whole, "a tone leaves part of a period at the end of a 240-sample frame");

    AudioRefTone(a, RUN, 1000.0, 0.5, 0u);
    AudioRefTone(b, RUN, 11000.0, 0.25, 0u);
    const double la = AudioRefLevel16(a, RUN, 1000.0);
    const double lb = AudioRefLevel16(b, RUN, 11000.0);
    CHECK_TRUE(SUITE, "audio: a sine is read back at its amplitude", CHECK_NONE,
               fabs(la - 0.5) < 2e-4 && fabs(lb - 0.25) < 2e-4,
               "1 kHz at 0.5 read %.6f, 11 kHz at 0.25 read %.6f", la, lb);

    const double cross = AudioRefLevel16(a, RUN, 1600.0);
    const double high = AudioRefLevel16(b, RUN, 1000.0);
    CHECK_TRUE(SUITE, "audio: a tone reads as nothing at another tone's frequency", CHECK_NONE,
               cross < 1e-4 && high < 1e-4, "1 kHz read %.6f at 1.6 kHz, 11 kHz read %.6f at 1 kHz",
               cross, high);

    AudioRefTone(a, RUN, 1000.0, 0.3, 0u);
    AudioRefTone(b, RUN, 1600.0, 0.2, 0u);
    for (size_t n = 0; n < RUN; ++n) {
        a[n] = (int16_t)(a[n] + b[n]);
    }
    const double m1 = AudioRefLevel16(a, RUN, 1000.0);
    const double m2 = AudioRefLevel16(a, RUN, 1600.0);
    CHECK_TRUE(SUITE, "audio: two tones mixed read back each at its own level", CHECK_NONE,
               fabs(m1 - 0.3) < 2e-4 && fabs(m2 - 0.2) < 2e-4,
               "0.3 at 1 kHz read %.6f, 0.2 at 1.6 kHz read %.6f", m1, m2);

    // Two frames of two channels, each sample saying where it belongs.
    int16_t frames[4 * AUDIO_FRAME];
    for (unsigned f = 0; f < 2u; ++f) {
        for (unsigned c = 0; c < 2u; ++c) {
            for (unsigned s = 0; s < AUDIO_FRAME; ++s) {
                frames[(f * 2u + c) * AUDIO_FRAME + s] = (int16_t)(c * 10000 + f * 1000 + s);
            }
        }
    }
    int16_t right[2 * AUDIO_FRAME];
    AudioRefChannel16(frames, 2u, 2u, AUDIO_FRAME, 1u, right);
    int32_t frames32[4 * AUDIO_FRAME];
    for (unsigned i = 0; i < 4u * AUDIO_FRAME; ++i) {
        frames32[i] = frames[i];
    }
    int32_t left[2 * AUDIO_FRAME];
    AudioRefChannel32(frames32, 2u, 2u, AUDIO_FRAME, 0u, left);
    bool split = true;
    for (unsigned f = 0; f < 2u; ++f) {
        for (unsigned s = 0; s < AUDIO_FRAME; ++s) {
            split = split && right[f * AUDIO_FRAME + s] == (int16_t)(10000 + f * 1000 + s) &&
                    left[f * AUDIO_FRAME + s] == (int32_t)(f * 1000 + s);
        }
    }
    CHECK_TRUE(SUITE, "audio: frames split back into their channels", CHECK_NONE, split,
               "a sample landed in the wrong channel or the wrong place");

    // By hand: a steady 0.5 settles the detector at 0.5, the gain at 0.1 / 0.5 and the output at
    // the threshold.
    const AudioRefLimiter limiter = {
        .threshold = 0.1, .attack = 0.001, .release = 0.001,
        .input_gain = 1.0, .output_gain = 1.0, .lookahead = 48u,
    };
    // One second, fifty time constants of the detector and of the gain.
    const size_t steady = AUDIO_RATE;
    int16_t *level = malloc(steady * sizeof(int16_t));
    double *out = malloc(steady * sizeof(double));
    double held = -1.0;
    if (level != NULL && out != NULL) {
        for (size_t n = 0; n < steady; ++n) {
            level[n] = 16384;
        }
        AudioRefLimit(&limiter, level, steady, out);
        held = out[steady - 1u];
    }
    free(level);
    free(out);
    CHECK_TRUE(SUITE, "audio: the limiter rule holds a steady input at its threshold", CHECK_NONE,
               fabs(held - 0.1) < 1e-4, "0.5 held at %.6f, threshold 0.1", held);

    // By hand: the detector of a sine of amplitude A settles at its rectified mean, 2A / pi, so
    // the gain is the threshold over that and the output amplitude the threshold times pi / 2,
    // whatever A is; the detector's ripple moves it by well under one per cent at these rates.
    const double sine = AudioRefLimitedLevel(&limiter, 0.25, 1000.0, 24000u, RUN);
    const double louder = AudioRefLimitedLevel(&limiter, 0.5, 1000.0, 24000u, RUN);
    const double quiet = AudioRefLimitedLevel(&limiter, 0.05, 1000.0, 24000u, RUN);
    const double expected = 0.1 * 3.14159265358979323846 / 2.0;
    CHECK_TRUE(SUITE, "audio: the limiter rule brings a sine to the threshold times pi over two",
               CHECK_NONE,
               AudioRefWithin(sine, expected, 0.01) && AudioRefWithin(louder, expected, 0.01) &&
                   AudioRefWithin(quiet, 0.05, 0.001),
               "0.25 came to %.5f and 0.5 to %.5f, expected %.5f; 0.05, below the threshold, "
               "came to %.5f", sine, louder, expected, quiet);

    // The listening signals: inside their amplitude, and at rest at both ends.
    AudioRefNoise(a, RUN, 0.2, 7u, 240u);
    AudioRefSweep(b, RUN, 2000.0, 16000.0, 0.2, 240u);
    const bool noise_ok = AudioRefPeak16(a, RUN) <= 0.2 + 1.0 / 32768.0 && a[0] == 0 &&
                          a[RUN - 1u] == 0 && AudioRefPeak16(a, RUN) > 0.19;
    const bool sweep_ok = AudioRefPeak16(b, RUN) <= 0.2 + 1.0 / 32768.0 && b[0] == 0 &&
                          b[RUN - 1u] == 0 && AudioRefPeak16(b, RUN) > 0.19;
    CHECK_TRUE(SUITE, "audio: the listening signals stay inside their level and start at rest",
               CHECK_NONE, noise_ok && sweep_ok,
               "noise peak %.5f ends %d %d; sweep peak %.5f ends %d %d", AudioRefPeak16(a, RUN),
               a[0], a[RUN - 1u], AudioRefPeak16(b, RUN), b[0], b[RUN - 1u]);

    // The hiss is the difference of neighbouring white samples, so each sample shares one term,
    // negated, with the next: the correlation at a lag of one is minus half the power, where
    // white noise has none.
    AudioRefHiss(a, RUN, 0.2, 5u, 0u);
    double power = 0.0;
    double lag = 0.0;
    for (size_t n = 1; n < RUN; ++n) {
        power += (double)a[n] * a[n];
        lag += (double)a[n] * a[n - 1u];
    }
    const double correlation = lag / power;
    CHECK_TRUE(SUITE, "audio: the hiss is white noise with its low end taken off", CHECK_NONE,
               correlation > -0.55 && correlation < -0.45 &&
                   AudioRefPeak16(a, RUN) <= 0.2 + 1.0 / 32768.0,
               "correlation at a lag of one %.3f of the power, expected -0.5; peak %.5f",
               correlation, AudioRefPeak16(a, RUN));

    // A sweep of equal time per octave: its first and last hundredths of a second cross zero at
    // about twice the frequencies it starts and ends at.
    int first = 0;
    int last = 0;
    for (size_t n = 241; n < 241 + 480; ++n) {
        first += (b[n - 1u] < 0) != (b[n] < 0);
    }
    for (size_t n = RUN - 720u; n < RUN - 240u; ++n) {
        last += (b[n - 1u] < 0) != (b[n] < 0);
    }
    const double start_hz = first * 100.0 / 2.0;
    const double end_hz = last * 100.0 / 2.0;
    CHECK_TRUE(SUITE, "audio: the sweep starts and ends at its frequencies", CHECK_NONE,
               start_hz > 1800.0 && start_hz < 2400.0 && end_hz > 14000.0 && end_hz < 16500.0,
               "starts near %.0f Hz and ends near %.0f Hz, expected 2000 and 16000", start_hz,
               end_hz);

    // The conversion's tables by their formula, against values the renderer's own tables hold
    // (a few of each, as units of 1/32768): within two units, as the fit is.
    static const struct {
        unsigned taps;
        double ratio;
        unsigned index;
        int value;
    } TABLED[] = {
        { 4u, 1.0, 0u, -68 },    { 4u, 1.0, 1u, 32639 },  { 4u, 1.0, 2u, 69 },
        { 4u, 1.0, 3u, -5 },     { 4u, 1.0, 256u, -1459 }, { 4u, 1.0, 257u, 17787 },
        { 4u, 1.0, 258u, 18151 }, { 4u, 1.0, 259u, -1495 }, { 4u, 2.0, 0u, 6600 },
        { 4u, 2.0, 1u, 19426 },  { 4u, 2.0, 2u, 6722 },   { 4u, 2.0, 3u, 3 },
        { 4u, 1.2, 0u, 3195 },   { 4u, 1.2, 1u, 26287 },  { 4u, 1.2, 2u, 3329 },
        { 4u, 1.2, 3u, -32 },    { 8u, 1.0, 0u, -12 },    { 8u, 1.0, 3u, 32767 },
        { 8u, 1.0, 4u, 81 },     { 8u, 1.0, 7u, 0 },      { 8u, 1.0, 512u, -207 },
        { 8u, 1.0, 515u, 19809 }, { 8u, 2.0, 0u, -582 },  { 8u, 2.0, 3u, 16386 },
        { 8u, 2.0, 4u, 8833 },   { 8u, 2.0, 7u, 0 },
    };
    static double filter[128u * 8u];
    unsigned wrong = 0;
    int first_got = 0;
    int first_want = 0;
    for (size_t i = 0; i < sizeof(TABLED) / sizeof(TABLED[0]); ++i) {
        AudioRefResampleTable(TABLED[i].taps, TABLED[i].ratio, filter);
        const int got = (int)lround(filter[TABLED[i].index] * 32768.0);
        if (abs(got - TABLED[i].value) > 2 && wrong++ == 0u) {
            first_got = got;
            first_want = TABLED[i].value;
        }
    }
    CHECK_TRUE(SUITE, "audio: the conversion tables' formula gives the renderer's values",
               CHECK_NONE, wrong == 0u && !AudioRefResampleTable(6u, 1.0, filter),
               "%u values off by more than 2, the first %d where the table holds %d", wrong,
               first_got, first_want);

    // A tone through each quality at three ratios, one per table: the levels a separate Python
    // model of the same tables, phases and measure gives.
    static const struct {
        unsigned taps;
        double ratio;
        double hz;
        double level;
    } THROUGH[] = {
        { 4u, 0.5, 10000.0, 0.3359 }, { 4u, 1.25, 15000.0, 0.3850 }, { 4u, 2.0, 18000.0, 0.3742 },
        { 8u, 0.5, 10000.0, 0.4023 }, { 8u, 1.25, 15000.0, 0.4533 }, { 8u, 2.0, 18000.0, 0.3693 },
    };
    unsigned off = 0;
    double off_got = 0.0;
    double off_want = 0.0;
    for (size_t i = 0; i < sizeof(THROUGH) / sizeof(THROUGH[0]); ++i) {
        const double got =
            AudioRefResampledLevel(THROUGH[i].taps, THROUGH[i].ratio, THROUGH[i].hz, 0.5);
        if (fabs(got - THROUGH[i].level) > 5e-4 && off++ == 0u) {
            off_got = got;
            off_want = THROUGH[i].level;
        }
    }
    CHECK_TRUE(SUITE, "audio: a tone keeps the level the conversion's table gives it",
               CHECK_NONE, off == 0u, "%u levels off, the first %.4f where %.4f is expected", off,
               off_got, off_want);

    // Mode 0's second answer, its estimate and its two steps, against values worked out by
    // hand: at a detector of 0.159 the steps give 1.8408 where one over it is 6.2893.
    const double estimate = AudioRefReciprocalEstimate(0.159);
    const double stepped = 2.0 - 0.159 * (2.0 - 0.159 * estimate);
    CHECK_TRUE(SUITE, "audio: the two-step reciprocal's estimate and its steps", CHECK_NONE,
               estimate == 6.28125 && fabs(stepped - 1.840796) < 1e-6 &&
                   AudioRefReciprocalEstimate(0.1001) == 9.94140625 &&
                   AudioRefReciprocalEstimate(0.3) == 3.3359375 &&
                   AudioRefReciprocalEstimate(0.5) == 1.99609375,
               "estimate %.8f, stepped %.6f", estimate, stepped);

    // The audio suite's mode 0 case, 1 kHz at 0.25 through threshold 0.1 at rates of a
    // hundredth: the rule gives 0.1574 and the two-step reciprocal 0.0460, worked out apart in
    // Python with the same tone, rule and measure.
    AudioRefLimiter rule = {
        .threshold = 0.1, .attack = 0.01, .release = 0.01,
        .input_gain = 1.0, .output_gain = 1.0, .lookahead = 48u,
    };
    const double by_rule = AudioRefLimitedLevel(&rule, 0.25, 1000.0, 24000u, 40u * AUDIO_FRAME);
    rule.two_step = 1;
    const double by_steps = AudioRefLimitedLevel(&rule, 0.25, 1000.0, 24000u, 40u * AUDIO_FRAME);
    CHECK_TRUE(SUITE, "audio: mode 0's two answers are the rule's level and the two-step one",
               CHECK_NONE,
               fabs(by_rule - 0.1574) < 5e-4 && fabs(by_steps - 0.0460) < 5e-4 &&
                   strcmp(AudioRefModeZeroReading(0.0461, by_rule, by_steps),
                          "the two-step reciprocal") == 0 &&
                   strcmp(AudioRefModeZeroReading(0.1560, by_rule, by_steps), "the rule") == 0 &&
                   strcmp(AudioRefModeZeroReading(0.1000, by_rule, by_steps), "neither") == 0,
               "rule %.4f, two-step %.4f", by_rule, by_steps);
}
