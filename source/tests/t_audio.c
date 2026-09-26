// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "../audio.h"
#include "../audioref.h"
#include "../check.h"
#include "../report.h"
#include "tests.h"

// The audio family: the audio renderer asked the way a game asks it, and judged by recording
// what it rendered. The measuring voices go through a submix that reaches the speakers at a
// gain of nought, and an aux effect on that submix records them; the circular buffer sink case
// plays two tones 46 dB below full scale, which is as quiet as a case that goes through the
// final mix can be. Every tone fills a 240-sample frame with whole periods and loops, so any
// frame of the recording is the same steady wave and the order it is read in does not matter.
//
// With the mode word `listen` the family first plays a sequence meant for a person to hear the
// same three things by ear; see RunListening below.

static const char *const SUITE = "audio";

static bool listening = false;

void AudioSetListen(bool on) {
    listening = on;
}

static const char *const RESAMPLE_HIGH =
    "an 11 kHz voice at the output rate keeps its level at high quality";
static const char *const RESAMPLE_DEFAULT =
    "an 11 kHz voice at the output rate keeps its level at default quality";
static const char *const CIRCULAR_USE =
    "a stereo circular buffer sink comes into use and records the final mix";
static const char *const CIRCULAR_CHANNELS =
    "a circular buffer sink records the left channel first and the right second";
static const char *const LIMITER_OFF = "a tone through the limiter switched off keeps its level";
static const char *const LIMITER_MODE1 =
    "a tone through the limiter in mode 1 comes out at the level of its rule";

// Steady tones play this long before a recording is read: 100 frames, the aux ring holds 40.
#define SETTLE 24000u
#define LOOP 48000u

/// A case whose line carries what was measured, pass or fail.
static void Case(const char *name, bool passed, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static void Case(const char *name, bool passed, const char *fmt, ...) {
    char detail[480];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    ReportCase(SUITE, name, CHECK_LEVEL, passed, detail);
}

/// A line for the person listening: the emulator's log shows it as the part it names starts.
static void Say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void Say(const char *fmt, ...) {
    char line[200] = "EDENPROBE LISTEN ";
    const size_t used = strlen(line);
    va_list args;
    va_start(args, fmt);
    vsnprintf(line + used, sizeof(line) - used, fmt, args);
    va_end(args);
    svcOutputDebugString(line, strlen(line));
}

/// Steps until @p voice has played @p samples, or @p limit_ms pass.
static bool PlayUntil(unsigned voice, uint64_t samples, uint64_t limit_ms) {
    const uint64_t start = AudioNowMs();
    while (AudioVoicePlayed(voice) < samples) {
        if (!AudioStep() || AudioNowMs() - start > limit_ms) {
            return false;
        }
    }
    return true;
}

/// Steps until @p voice has come to the end of its buffer.
static bool PlayToEnd(unsigned voice, size_t samples) {
    const uint64_t start = AudioNowMs();
    const uint64_t limit = samples * 1000u / AUDIO_RATE + 3000u;
    while (!AudioVoiceDone(voice)) {
        if (!AudioStep() || AudioNowMs() - start > limit) {
            return false;
        }
    }
    return true;
}

static bool Play(unsigned voice, const int16_t *data, size_t samples, AudioQuality quality,
                 unsigned mix, float left, float right) {
    AudioPlay(voice, data, samples, false, quality, mix, left, right);
    return PlayToEnd(voice, samples);
}

static int32_t captured[40 * AUDIO_FRAME];
static int16_t recorded[40 * AUDIO_FRAME];

/// Loops @p tone on voice 0 into the submix's buffer 0 until the aux ring holds nothing else,
/// and gives the level at @p hz in what the aux recorded; -1 when the renderer stopped first.
static double SubmixLevel(const int16_t *tone, double hz, AudioQuality quality) {
    AudioPlay(0, tone, LOOP, true, quality, AUDIO_SUBMIX, 1.0f, 0.0f);
    const bool played = PlayUntil(0, SETTLE, 5000u);
    size_t frames = 0;
    const int32_t *ring = AudioCaptureRing(&frames);
    AudioRefChannel32(ring, frames, 2u, AUDIO_FRAME, 0u, captured);
    const double level = AudioRefLevel32(captured, frames * AUDIO_FRAME, hz);
    AudioStop(0);
    AudioStep();
    return played ? level : -1.0;
}

/// Both cases of a pair skipped for want of room in the renderer's memory pool.
static void NoRoom(const char *first, const char *second) {
    static const char *const WHY = "no room in the renderer's memory pool";
    ReportSkip(SUITE, first, REPORT_REASON_UNAVAILABLE, WHY);
    ReportSkip(SUITE, second, REPORT_REASON_UNAVAILABLE, WHY);
}

static void CaseResampler(void) {
    int16_t *tone = AudioAlloc(LOOP);
    if (tone == NULL) {
        NoRoom(RESAMPLE_HIGH, RESAMPLE_DEFAULT);
        return;
    }
    AudioRefTone(tone, LOOP, 11000.0, 0.5, 0u);
    AudioSync(tone, LOOP * sizeof(int16_t));

    if (!ReportStart(SUITE, RESAMPLE_HIGH)) {
        ReportSkip(SUITE, RESAMPLE_DEFAULT, REPORT_REASON_DEPENDS,
                   "judged against the level of the case before it, which did not run");
        return;
    }
    const double high = SubmixLevel(tone, 11000.0, AUDIO_QUALITY_HIGH);
    Case(RESAMPLE_HIGH, AudioRefWithin(high, 0.5, 0.02),
         "11 kHz at 0.5 of full scale came out at %.4f (%+.2f dB)", high,
         AudioRefDecibels(high, 0.5));
    if (!ReportStart(SUITE, RESAMPLE_DEFAULT)) {
        return;
    }
    const double normal = SubmixLevel(tone, 11000.0, AUDIO_QUALITY_DEFAULT);
    Case(RESAMPLE_DEFAULT, AudioRefWithin(normal, 0.5, 0.02) && AudioRefWithin(normal, high, 0.02),
         "11 kHz at 0.5 of full scale came out at %.4f (%+.2f dB), at high quality %.4f; a "
         "renderer that takes the smoothing table at a ratio of one gives about 0.41",
         normal, AudioRefDecibels(normal, 0.5), high);
}

static void CaseCircular(void) {
    int16_t *left = AudioAlloc(LOOP);
    int16_t *right = AudioAlloc(LOOP);
    if (left == NULL || right == NULL) {
        NoRoom(CIRCULAR_USE, CIRCULAR_CHANNELS);
        return;
    }
    const double level = 0.005;
    AudioRefTone(left, LOOP, 1000.0, level, 0u);
    AudioRefTone(right, LOOP, 1600.0, level, 0u);
    AudioSync(left, LOOP * sizeof(int16_t));
    AudioSync(right, LOOP * sizeof(int16_t));

    if (!ReportStart(SUITE, CIRCULAR_USE)) {
        ReportSkip(SUITE, CIRCULAR_CHANNELS, REPORT_REASON_DEPENDS,
                   "judged from the recording of the case before it, which did not run");
        return;
    }
    AudioRecord(AUDIO_SINK_CASES, true);
    AudioPlay(0, left, LOOP, true, AUDIO_QUALITY_HIGH, AUDIO_FINAL_MIX, 1.0f, 0.0f);
    AudioPlay(1, right, LOOP, true, AUDIO_QUALITY_HIGH, AUDIO_FINAL_MIX, 0.0f, 1.0f);
    const bool played = PlayUntil(0, SETTLE, 5000u);
    size_t frames = 0;
    const int16_t *ring = AudioRecordRing(AUDIO_SINK_CASES, &frames);
    const size_t samples = frames * AUDIO_FRAME;
    const double peak = AudioRefPeak16(ring, frames * 2u * AUDIO_FRAME);
    const uint32_t offset = AudioRecordOffsetSeen(AUDIO_SINK_CASES);
    AudioRefChannel16(ring, frames, 2u, AUDIO_FRAME, 0u, recorded);
    const double l_left = AudioRefLevel16(recorded, samples, 1000.0);
    const double l_right = AudioRefLevel16(recorded, samples, 1600.0);
    AudioRefChannel16(ring, frames, 2u, AUDIO_FRAME, 1u, recorded);
    const double r_left = AudioRefLevel16(recorded, samples, 1000.0);
    const double r_right = AudioRefLevel16(recorded, samples, 1600.0);
    AudioStop(0);
    AudioStop(1);
    AudioRecord(AUDIO_SINK_CASES, false);
    AudioStep();

    Case(CIRCULAR_USE, played && peak > level / 2.0 && offset > 0u,
         "loudest recorded sample %.5f of full scale, largest write offset reported %u; the "
         "final mix held 1 kHz on the left and 1.6 kHz on the right at %.3f",
         peak, (unsigned)offset, level);
    if (!ReportStart(SUITE, CIRCULAR_CHANNELS)) {
        return;
    }
    Case(CIRCULAR_CHANNELS,
         played && AudioRefWithin(l_left, level, 0.02) && AudioRefWithin(r_right, level, 0.02) &&
             l_right < level * 0.05 && r_left < level * 0.05,
         "channel 0 held 1 kHz at %.5f and 1.6 kHz at %.5f; channel 1 held 1 kHz at %.5f and "
         "1.6 kHz at %.5f; expected %.3f of its own tone in each and none of the other",
         l_left, l_right, r_left, r_right, level);
}

static void CaseLimiter(void) {
    int16_t *tone = AudioAlloc(LOOP);
    if (tone == NULL) {
        NoRoom(LIMITER_OFF, LIMITER_MODE1);
        return;
    }
    const double amplitude = 0.25;
    AudioRefTone(tone, LOOP, 1000.0, amplitude, 0u);
    AudioSync(tone, LOOP * sizeof(int16_t));

    // Rates of a hundredth: a renderer that keeps the detector in 15 fractional bits drops the
    // steps a rate of a thousandth makes near the average, and lands 9% above the rule.
    AudioLimiterSettings settings = {
        .enabled = false, .mode = 1, .threshold = 0.1f,
        .attack = 0.01f, .release = 0.01f, .lookahead = 48u,
    };
    if (ReportStart(SUITE, LIMITER_OFF)) {
        AudioLimiter(&settings);
        const double off = SubmixLevel(tone, 1000.0, AUDIO_QUALITY_HIGH);
        Case(LIMITER_OFF, AudioRefWithin(off, amplitude, 0.01),
             "1 kHz at %.2f of full scale came out at %.4f", amplitude, off);
    }
    if (ReportStart(SUITE, LIMITER_MODE1)) {
        settings.enabled = true;
        AudioLimiter(&settings);
        const double mode1 = SubmixLevel(tone, 1000.0, AUDIO_QUALITY_HIGH);
        settings.mode = 0;
        AudioLimiter(&settings);
        const double mode0 = SubmixLevel(tone, 1000.0, AUDIO_QUALITY_HIGH);
        settings.enabled = false;
        AudioLimiter(&settings);
        AudioStep();

        const AudioRefLimiter rule = {
            .threshold = 0.1, .attack = 0.01, .release = 0.01,
            .input_gain = 1.0, .output_gain = 1.0, .lookahead = 48u,
        };
        const double expected =
            AudioRefLimitedLevel(&rule, amplitude, 1000.0, SETTLE, 40u * AUDIO_FRAME);
        Case(LIMITER_MODE1, AudioRefWithin(mode1, expected, 0.02),
             "1 kHz at %.2f, threshold 0.1: came out at %.4f, the rule gives %.4f (%+.2f dB); in "
             "mode 0 it came out at %.4f",
             amplitude, mode1, expected, AudioRefDecibels(mode1, expected), mode0);
        ReportNote("audio: the limiter in mode 0 brought 1 kHz at %.2f to %.4f and in mode 1 to "
                   "%.4f; the rule, threshold over the detector, gives %.4f",
                   amplitude, mode0, mode1, expected);
    }
}

// The listening sequence. Every part starts with a beep pattern and a log line naming it:
// one, two or three beeps at 880 Hz open parts a, b and c; within a part, a low beep (440 Hz)
// comes before one take and a high beep (1760 Hz) before the take it is compared with.

#define MS(n) ((size_t)(n) * AUDIO_RATE / 1000u)

static void Beep(int16_t *out, double hz, size_t length) {
    AudioRefTone(out, length, hz, 0.12, MS(5));
}

/// @p beeps beeps at @p hz, 120 ms each with 100 ms between, then @p rest ms of silence.
static int16_t *Marker(unsigned beeps, double hz, unsigned rest, size_t *samples) {
    *samples = beeps * MS(220) + MS(rest);
    int16_t *out = AudioAlloc(*samples);
    if (out != NULL) {
        for (unsigned i = 0; i < beeps; ++i) {
            Beep(out + i * MS(220), hz, MS(120));
        }
        AudioSync(out, *samples * sizeof(int16_t));
    }
    return out;
}

/// Notes of @p ms each with 50 ms between, starting @p at samples in.
static void Notes(int16_t *out, size_t at, const double *hz, unsigned count, unsigned ms,
                  double amplitude) {
    for (unsigned i = 0; i < count; ++i) {
        AudioRefTone(out + at + i * MS(ms + 50u), MS(ms), hz[i], amplitude, MS(8));
    }
}

static bool RunListening(void) {
    size_t n_one, n_two, n_three, n_low, n_high;
    int16_t *one = Marker(1, 880.0, 500, &n_one);
    int16_t *two = Marker(2, 880.0, 500, &n_two);
    int16_t *three = Marker(3, 880.0, 500, &n_three);
    int16_t *low = Marker(1, 440.0, 250, &n_low);
    int16_t *high = Marker(1, 1760.0, 250, &n_high);

    // Part a: noise bursts, a sweep from 4 to 16 kHz and a second of hiss, the same every take.
    const size_t bright_n = MS(1000) + MS(200) + MS(2000) + MS(200) + MS(1000) + MS(300);
    int16_t *bright = AudioAlloc(bright_n);
    // Part b: low notes on the left, then high notes on the right.
    const size_t phrase_n = MS(1500);
    int16_t *phrase_l = AudioAlloc(phrase_n);
    int16_t *phrase_r = AudioAlloc(phrase_n);
    const size_t echo_n = MS(1650);
    int16_t *echo_l = AudioAlloc(echo_n);
    int16_t *echo_r = AudioAlloc(echo_n);
    // Part c: three notes for the limiter.
    const size_t motif_n = MS(1350) + MS(200);
    int16_t *motif = AudioAlloc(motif_n);
    // The start and the end: three notes up, three down.
    const size_t chime_n = MS(3 * 150) + MS(300);
    int16_t *up = AudioAlloc(chime_n);
    int16_t *down = AudioAlloc(chime_n);
    if (one == NULL || two == NULL || three == NULL || low == NULL || high == NULL ||
        bright == NULL || phrase_l == NULL || phrase_r == NULL || echo_l == NULL ||
        echo_r == NULL || motif == NULL || up == NULL || down == NULL) {
        ReportNote("audio: no room in the renderer's memory pool for the listening sequence");
        return false;
    }
    for (unsigned i = 0; i < 4u; ++i) {
        AudioRefNoise(bright + i * MS(250), MS(150), 0.2, 11u + i, MS(3));
    }
    AudioRefSweep(bright + MS(1200), MS(2000), 4000.0, 16000.0, 0.2, MS(20));
    AudioRefHiss(bright + MS(3400), MS(1000), 0.2, 23u, MS(20));
    static const double LEFT_NOTES[3] = {261.63, 329.63, 392.0};
    static const double RIGHT_NOTES[3] = {783.99, 659.26, 523.25};
    Notes(phrase_l, 0, LEFT_NOTES, 3u, 200u, 0.15);
    Notes(phrase_r, MS(750), RIGHT_NOTES, 3u, 200u, 0.15);
    static const double MOTIF[3] = {523.25, 659.26, 783.99};
    Notes(motif, 0, MOTIF, 3u, 400u, 0.4);
    static const double UP[3] = {523.25, 659.26, 783.99};
    static const double DOWN[3] = {783.99, 659.26, 523.25};
    Notes(up, 0, UP, 3u, 100u, 0.12);
    Notes(down, 0, DOWN, 3u, 100u, 0.12);
    AudioSync(bright, bright_n * sizeof(int16_t));
    AudioSync(phrase_l, phrase_n * sizeof(int16_t));
    AudioSync(phrase_r, phrase_n * sizeof(int16_t));
    AudioSync(motif, motif_n * sizeof(int16_t));
    AudioSync(up, chime_n * sizeof(int16_t));
    AudioSync(down, chime_n * sizeof(int16_t));

    const AudioQuality hq = AUDIO_QUALITY_HIGH;
    const unsigned final = AUDIO_FINAL_MIX;
    bool ok = true;
    Say("start: three rising notes, then parts a, b and c, about 40 seconds");
    ok = ok && Play(0, up, chime_n, hq, final, 1.0f, 1.0f);

    Say("a: resampler, one beep; four takes of noise bursts, a sweep to 16 kHz and hiss");
    ok = ok && Play(0, one, n_one, hq, final, 1.0f, 1.0f);
    for (unsigned take = 0; ok && take < 4u; ++take) {
        const bool is_high = (take % 2u) == 1u;
        Say("a%u: %s", take + 1u,
            is_high ? "high beep, high quality" : "low beep, default quality");
        ok = Play(0, is_high ? high : low, is_high ? n_high : n_low, hq, final, 1.0f, 1.0f) &&
             Play(1, bright, bright_n, is_high ? AUDIO_QUALITY_HIGH : AUDIO_QUALITY_DEFAULT,
                  final, 1.0f, 1.0f);
    }

    Say("b: circular buffer sink, two beeps; low notes left, high notes right, recorded");
    ok = ok && Play(0, two, n_two, hq, final, 1.0f, 1.0f);
    if (ok) {
        AudioRecord(AUDIO_SINK_LISTEN, true);
        AudioPlay(1, phrase_l, phrase_n, false, hq, final, 1.0f, 0.0f);
        AudioPlay(2, phrase_r, phrase_n, false, hq, final, 0.0f, 1.0f);
        ok = PlayToEnd(1, phrase_n) && PlayToEnd(2, phrase_n);
        for (unsigned i = 0; ok && i < 30u; ++i) {
            ok = AudioStep();
        }
        AudioRecord(AUDIO_SINK_LISTEN, false);
        ok = ok && AudioStep();
    }
    if (ok) {
        size_t frames = 0;
        const int16_t *ring = AudioRecordRing(AUDIO_SINK_LISTEN, &frames);
        const size_t take = echo_n / AUDIO_FRAME < frames ? echo_n / AUDIO_FRAME : frames;
        AudioRefChannel16(ring, take, 2u, AUDIO_FRAME, 0u, echo_l);
        AudioRefChannel16(ring, take, 2u, AUDIO_FRAME, 1u, echo_r);
        AudioSync(echo_l, echo_n * sizeof(int16_t));
        AudioSync(echo_r, echo_n * sizeof(int16_t));
        const double peak_l = AudioRefPeak16(echo_l, echo_n);
        const double peak_r = AudioRefPeak16(echo_r, echo_n);
        Say("b: the recording peaks at %.3f on the left and %.3f on the right (0 is silence)",
            peak_l, peak_r);
        ReportNote("audio: the listening sequence recorded its phrase at a peak of %.3f on the "
                   "left and %.3f on the right, played at 0.15",
                   peak_l, peak_r);
        Say("b2: low beep, the recording's left channel on the left");
        ok = Play(0, low, n_low, hq, final, 1.0f, 1.0f) &&
             Play(3, echo_l, echo_n, hq, final, 1.0f, 0.0f);
        Say("b3: high beep, the recording's right channel on the right");
        ok = ok && Play(0, high, n_high, hq, final, 1.0f, 1.0f) &&
             Play(3, echo_r, echo_n, hq, final, 0.0f, 1.0f);
    }

    Say("c: limiter, three beeps; four takes of three notes, threshold 0.2");
    ok = ok && Play(0, three, n_three, hq, final, 1.0f, 1.0f);
    AudioSubmixGain(1.0f);
    for (unsigned take = 0; ok && take < 4u; ++take) {
        const uint8_t mode = (uint8_t)(take % 2u);
        Say("c%u: %s", take + 1u, mode ? "high beep, limiter mode 1" : "low beep, limiter mode 0");
        ok = Play(0, mode ? high : low, mode ? n_high : n_low, hq, final, 1.0f, 1.0f);
        const AudioLimiterSettings settings = {
            .enabled = true, .mode = mode, .threshold = 0.2f,
            .attack = 0.01f, .release = 0.01f, .lookahead = 48u,
        };
        AudioLimiter(&settings);
        ok = ok && Play(1, motif, motif_n, hq, AUDIO_SUBMIX, 1.0f, 1.0f);
    }
    AudioSubmixGain(0.0f);
    const AudioLimiterSettings off = {
        .enabled = false, .mode = 1, .threshold = 1.0f,
        .attack = 0.001f, .release = 0.001f, .lookahead = 48u,
    };
    AudioLimiter(&off);

    Say("end: three falling notes; the cases that follow are nearly silent");
    ok = ok && Play(0, down, chime_n, hq, final, 1.0f, 1.0f);
    if (!ok) {
        ReportNote("audio: the listening sequence stopped early: %s",
                   AudioError()[0] != '\0' ? AudioError() : "a part did not end in time");
    }
    return ok;
}

void RunAudio(void) {
    char error[160] = "";
    if (!AudioOpen(error, sizeof(error))) {
        const char *const names[] = {RESAMPLE_HIGH, RESAMPLE_DEFAULT, CIRCULAR_USE,
                                     CIRCULAR_CHANNELS, LIMITER_OFF, LIMITER_MODE1};
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            ReportSkip(SUITE, names[i], REPORT_REASON_UNAVAILABLE, error);
        }
        return;
    }
    ReportNote("audio: renderer revision %u at 48 kHz; measuring through an aux effect on a "
               "submix at gain 0",
               AudioRevision());
    AudioSubmixGain(0.0f);
    if (listening) {
        RunListening();
    }
    AudioCapture(true);
    CaseResampler();
    CaseCircular();
    CaseLimiter();
    AudioCapture(false);
    AudioStep();
    if (AudioError()[0] != '\0') {
        ReportNote("audio: the renderer said: %s", AudioError());
    }
    AudioClose();
}
