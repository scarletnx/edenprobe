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
#include "../viewer.h"
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
        AudioRefLimiter two_step = rule;
        two_step.two_step = 1;
        const double stepped =
            AudioRefLimitedLevel(&two_step, amplitude, 1000.0, SETTLE, 40u * AUDIO_FRAME);
        Case(LIMITER_MODE1, AudioRefWithin(mode1, expected, 0.02),
             "1 kHz at %.2f, threshold 0.1: came out at %.4f, the rule gives %.4f (%+.2f dB); in "
             "mode 0 it came out at %.4f",
             amplitude, mode1, expected, AudioRefDecibels(mode1, expected), mode0);
        // Mode 0 has two answers and no console has said which: the note says which one this
        // machine's level is.
        ReportNote("audio: the limiter in mode 0 brought 1 kHz at %.2f to %.4f and in mode 1 to "
                   "%.4f; the rule, threshold over the detector, gives %.4f, the two-step "
                   "reciprocal %.4f; mode 0 here is %s",
                   amplitude, mode0, mode1, expected, stepped,
                   AudioRefModeZeroReading(mode0, expected, stepped));
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

/// The clips the sequence plays, and where a repeated recording has to be read from. The sink's
/// write cursor only moves forward, so a take played again is read from where this one started.
typedef struct {
    int16_t *up;
    size_t n_up;
    int16_t *down;
    size_t n_down;
    int16_t *one;
    size_t n_one;
    int16_t *two;
    size_t n_two;
    int16_t *three;
    size_t n_three;
    int16_t *low;
    size_t n_low;
    int16_t *high;
    size_t n_high;
    int16_t *bright;
    size_t n_bright;
    int16_t *phrase_l;
    int16_t *phrase_r;
    size_t n_phrase;
    int16_t *echo_l;
    int16_t *echo_r;
    size_t n_echo;
    int16_t *motif;
    size_t n_motif;
    bool ok;
    /// Part b's echoes are said only after the recording finished. A failed beep before that
    /// stays quiet; a failed left echo still announces the right one.
    bool echoes;
    bool settled;
    double peak_l;
    double peak_r;
    size_t record_at;
} Listen;

/// Stops whatever a button cut off, and one step so the renderer drops it before the next take.
/// @p recording also takes the listening sink back out of use.
static ViewerListenAction StopHear(ViewerListenAction act, bool recording) {
    for (unsigned voice = 0; voice < 4u; ++voice) {
        AudioStop(voice);
    }
    if (recording) {
        AudioRecord(AUDIO_SINK_LISTEN, false);
    }
    return AudioStep() ? act : VIEWER_LISTEN_FAIL;
}

/// Plays one clip to its end. A button stops it; running out of time does not, which is how a
/// take that never ends already behaved.
static ViewerListenAction PlayHear(unsigned voice, const int16_t *data, size_t samples,
                                   AudioQuality quality, unsigned mix, float left, float right,
                                   size_t *wrote) {
    AudioPlay(voice, data, samples, false, quality, mix, left, right);
    const uint64_t start = AudioNowMs();
    const uint64_t limit = samples * 1000u / AUDIO_RATE + 3000u;
    while (!AudioVoiceDone(voice)) {
        const ViewerListenAction act = ViewerListenPoll();
        if (act != VIEWER_LISTEN_NONE) {
            return StopHear(act, false);
        }
        if (!AudioStep()) {
            return VIEWER_LISTEN_FAIL;
        }
        if (wrote != NULL) {
            ++*wrote;
        }
        if (AudioNowMs() - start > limit) {
            return VIEWER_LISTEN_FAIL;
        }
    }
    return VIEWER_LISTEN_NONE;
}

static ViewerListenAction Finish(Listen *listen, ViewerListenAction act) {
    if (act == VIEWER_LISTEN_FAIL) {
        listen->ok = false;
        return VIEWER_LISTEN_NONE;
    }
    return act;
}

static void LimiterOff(void) {
    AudioSubmixGain(0.0f);
    const AudioLimiterSettings off = {
        .enabled = false, .mode = 1, .threshold = 1.0f,
        .attack = 0.001f, .release = 0.001f, .lookahead = 48u,
    };
    AudioLimiter(&off);
}

/// One channel of a recording that did not start at the front of the ring.
static void ChannelFrom(const int16_t *ring, size_t capacity, size_t origin, size_t count,
                        unsigned channel, int16_t *out) {
    for (size_t frame = 0; frame < count; ++frame) {
        const size_t at = capacity == 0u ? 0u : (origin + frame) % capacity;
        memcpy(out + frame * AUDIO_FRAME, ring + (at * 2u + channel) * AUDIO_FRAME,
               AUDIO_FRAME * sizeof(int16_t));
    }
}

static void AdvanceRecord(Listen *listen, size_t origin, size_t wrote) {
    size_t capacity = 0;
    AudioRecordRing(AUDIO_SINK_LISTEN, &capacity);
    if (capacity > 0u) {
        listen->record_at = (origin + wrote) % capacity;
    }
}

/// The phrase, then the thirty silent frames the echo's window was cut to. A button leaves the
/// sink where the cursor got to, so playing the phrase again reads the new take.
static ViewerListenAction RecordPhrase(Listen *listen) {
    size_t wrote = 0;
    const size_t origin = listen->record_at;
    const AudioQuality hq = AUDIO_QUALITY_HIGH;
    const unsigned final = AUDIO_FINAL_MIX;
    AudioRecord(AUDIO_SINK_LISTEN, true);
    AudioPlay(1, listen->phrase_l, listen->n_phrase, false, hq, final, 1.0f, 0.0f);
    AudioPlay(2, listen->phrase_r, listen->n_phrase, false, hq, final, 0.0f, 1.0f);
    const uint64_t start = AudioNowMs();
    const uint64_t limit = listen->n_phrase * 1000u / AUDIO_RATE + 3000u;
    ViewerListenAction act = VIEWER_LISTEN_NONE;
    while (listen->ok && (!AudioVoiceDone(1) || !AudioVoiceDone(2))) {
        act = ViewerListenPoll();
        if (act != VIEWER_LISTEN_NONE) {
            break;
        }
        if (!AudioStep()) {
            listen->ok = false;
            break;
        }
        ++wrote;
        if (AudioNowMs() - start > limit) {
            listen->ok = false;
            break;
        }
    }
    if (act == VIEWER_LISTEN_NONE && listen->ok) {
        for (unsigned i = 0; i < 30u; ++i) {
            act = ViewerListenPoll();
            if (act != VIEWER_LISTEN_NONE) {
                break;
            }
            if (!AudioStep()) {
                listen->ok = false;
                break;
            }
            ++wrote;
        }
    }
    if (act != VIEWER_LISTEN_NONE) {
        act = StopHear(act, true);
        AdvanceRecord(listen, origin, wrote);
        return Finish(listen, act);
    }
    AudioRecord(AUDIO_SINK_LISTEN, false);
    if (listen->ok) {
        listen->ok = AudioStep();
    }
    if (!listen->ok) {
        AdvanceRecord(listen, origin, wrote);
        return VIEWER_LISTEN_NONE;
    }

    size_t frames = 0;
    const int16_t *ring = AudioRecordRing(AUDIO_SINK_LISTEN, &frames);
    const size_t take =
        listen->n_echo / AUDIO_FRAME < frames ? listen->n_echo / AUDIO_FRAME : frames;
    if (origin == 0u) {
        AudioRefChannel16(ring, take, 2u, AUDIO_FRAME, 0u, listen->echo_l);
        AudioRefChannel16(ring, take, 2u, AUDIO_FRAME, 1u, listen->echo_r);
    } else {
        ChannelFrom(ring, frames, origin, take, 0u, listen->echo_l);
        ChannelFrom(ring, frames, origin, take, 1u, listen->echo_r);
    }
    AudioSync(listen->echo_l, listen->n_echo * sizeof(int16_t));
    AudioSync(listen->echo_r, listen->n_echo * sizeof(int16_t));
    listen->peak_l = AudioRefPeak16(listen->echo_l, listen->n_echo);
    listen->peak_r = AudioRefPeak16(listen->echo_r, listen->n_echo);
    Say("b: the recording peaks at %.3f on the left and %.3f on the right (0 is silence)",
        listen->peak_l, listen->peak_r);
    ReportNote("audio: the listening sequence recorded its phrase at a peak of %.3f on the "
               "left and %.3f on the right, played at 0.15",
               listen->peak_l, listen->peak_r);
    if (frames > 0u) {
        listen->record_at = (origin + wrote) % frames;
    }
    listen->echoes = true;
    return VIEWER_LISTEN_NONE;
}

static ViewerListenAction PlayScreen(UiListenScreen screen, Listen *listen) {
    const AudioQuality hq = AUDIO_QUALITY_HIGH;
    const unsigned final = AUDIO_FINAL_MIX;
    switch (screen) {
    case UI_LISTEN_OPENING:
        ViewerListenShow(screen, 0.0, 0.0);
        Say("start: three rising notes, then parts a, b and c, about 40 seconds");
        if (!listen->ok) {
            return VIEWER_LISTEN_NONE;
        }
        return Finish(listen, PlayHear(0, listen->up, listen->n_up, hq, final, 1.0f, 1.0f, NULL));
    case UI_LISTEN_A_BEEP:
        ViewerListenShow(screen, 0.0, 0.0);
        Say("a: resampler, one beep; four takes of noise bursts, a sweep to 16 kHz and hiss");
        if (!listen->ok) {
            return VIEWER_LISTEN_NONE;
        }
        return Finish(listen,
                      PlayHear(0, listen->one, listen->n_one, hq, final, 1.0f, 1.0f, NULL));
    case UI_LISTEN_A1:
    case UI_LISTEN_A2:
    case UI_LISTEN_A3:
    case UI_LISTEN_A4: {
        if (!listen->ok) {
            return VIEWER_LISTEN_NONE;
        }
        const unsigned take = (unsigned)(screen - UI_LISTEN_A1);
        const bool is_high = (take % 2u) == 1u;
        ViewerListenShow(screen, 0.0, 0.0);
        Say("a%u: %s", take + 1u,
            is_high ? "high beep, high quality" : "low beep, default quality");
        const int16_t *cue = is_high ? listen->high : listen->low;
        const size_t n_cue = is_high ? listen->n_high : listen->n_low;
        ViewerListenAction act = PlayHear(0, cue, n_cue, hq, final, 1.0f, 1.0f, NULL);
        if (act == VIEWER_LISTEN_NONE) {
            act = PlayHear(1, listen->bright, listen->n_bright,
                           is_high ? AUDIO_QUALITY_HIGH : AUDIO_QUALITY_DEFAULT, final, 1.0f,
                           1.0f, NULL);
        }
        return Finish(listen, act);
    }
    case UI_LISTEN_B_BEEP:
        ViewerListenShow(screen, 0.0, 0.0);
        Say("b: circular buffer sink, two beeps; low notes left, high notes right, recorded");
        if (!listen->ok) {
            return VIEWER_LISTEN_NONE;
        }
        return Finish(listen,
                      PlayHear(0, listen->two, listen->n_two, hq, final, 1.0f, 1.0f, NULL));
    case UI_LISTEN_B_RECORD:
        if (!listen->ok) {
            return VIEWER_LISTEN_NONE;
        }
        ViewerListenShow(screen, 0.0, 0.0);
        return RecordPhrase(listen);
    case UI_LISTEN_B_LEFT:
        if (!listen->echoes) {
            return VIEWER_LISTEN_NONE;
        }
        ViewerListenShow(screen, listen->peak_l, listen->peak_r);
        Say("b2: low beep, the recording's left channel on the left");
        if (!listen->ok) {
            return VIEWER_LISTEN_NONE;
        }
        {
            ViewerListenAction act =
                PlayHear(0, listen->low, listen->n_low, hq, final, 1.0f, 1.0f, NULL);
            if (act == VIEWER_LISTEN_NONE) {
                act = PlayHear(3, listen->echo_l, listen->n_echo, hq, final, 1.0f, 0.0f, NULL);
            }
            return Finish(listen, act);
        }
    case UI_LISTEN_B_RIGHT:
        if (!listen->echoes) {
            return VIEWER_LISTEN_NONE;
        }
        ViewerListenShow(screen, listen->peak_l, listen->peak_r);
        Say("b3: high beep, the recording's right channel on the right");
        if (!listen->ok) {
            return VIEWER_LISTEN_NONE;
        }
        {
            ViewerListenAction act =
                PlayHear(0, listen->high, listen->n_high, hq, final, 1.0f, 1.0f, NULL);
            if (act == VIEWER_LISTEN_NONE) {
                act = PlayHear(3, listen->echo_r, listen->n_echo, hq, final, 0.0f, 1.0f, NULL);
            }
            return Finish(listen, act);
        }
    case UI_LISTEN_C_BEEP: {
        ViewerListenShow(screen, 0.0, 0.0);
        Say("c: limiter, three beeps; four takes of three notes, threshold 0.2");
        ViewerListenAction act = VIEWER_LISTEN_NONE;
        if (listen->ok) {
            act = PlayHear(0, listen->three, listen->n_three, hq, final, 1.0f, 1.0f, NULL);
        }
        act = Finish(listen, act);
        // Even when the beeps did not finish: the takes that follow, if any, go through it,
        // and a run that plays none of them sets it back before the closing notes.
        if (act == VIEWER_LISTEN_NONE) {
            AudioSubmixGain(1.0f);
        }
        return act;
    }
    case UI_LISTEN_C1:
    case UI_LISTEN_C2:
    case UI_LISTEN_C3:
    case UI_LISTEN_C4: {
        if (!listen->ok) {
            return VIEWER_LISTEN_NONE;
        }
        const unsigned take = (unsigned)(screen - UI_LISTEN_C1);
        const uint8_t mode = (uint8_t)(take % 2u);
        ViewerListenShow(screen, 0.0, 0.0);
        Say("c%u: %s", take + 1u,
            mode ? "high beep, limiter mode 1" : "low beep, limiter mode 0");
        const int16_t *cue = mode ? listen->high : listen->low;
        const size_t n_cue = mode ? listen->n_high : listen->n_low;
        ViewerListenAction act = PlayHear(0, cue, n_cue, hq, final, 1.0f, 1.0f, NULL);
        const AudioLimiterSettings settings = {
            .enabled = true, .mode = mode, .threshold = 0.2f,
            .attack = 0.01f, .release = 0.01f, .lookahead = 48u,
        };
        AudioLimiter(&settings);
        if (act == VIEWER_LISTEN_NONE) {
            act = PlayHear(1, listen->motif, listen->n_motif, hq, AUDIO_SUBMIX, 1.0f, 1.0f, NULL);
        }
        return Finish(listen, act);
    }
    case UI_LISTEN_CLOSING:
        LimiterOff();
        listen->settled = true;
        ViewerListenShow(screen, 0.0, 0.0);
        Say("end: three falling notes; the cases that follow are nearly silent");
        if (!listen->ok) {
            return VIEWER_LISTEN_NONE;
        }
        return Finish(listen,
                      PlayHear(0, listen->down, listen->n_down, hq, final, 1.0f, 1.0f, NULL));
    case UI_LISTEN_DONE:
        break;
    }
    return VIEWER_LISTEN_NONE;
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

    Listen listen = {
        .up = up, .n_up = chime_n, .down = down, .n_down = chime_n,
        .one = one, .n_one = n_one, .two = two, .n_two = n_two,
        .three = three, .n_three = n_three, .low = low, .n_low = n_low,
        .high = high, .n_high = n_high, .bright = bright, .n_bright = bright_n,
        .phrase_l = phrase_l, .phrase_r = phrase_r, .n_phrase = phrase_n,
        .echo_l = echo_l, .echo_r = echo_r, .n_echo = echo_n,
        .motif = motif, .n_motif = motif_n, .ok = true, .echoes = false, .settled = false,
        .peak_l = 0.0, .peak_r = 0.0, .record_at = 0,
    };
    // No press walks the screens in order. Y stays on the one that is playing, X jumps to the
    // next part, and either way the log line for a screen is the one it already had.
    for (UiListenScreen screen = UI_LISTEN_OPENING; screen != UI_LISTEN_DONE;) {
        const ViewerListenAction act = PlayScreen(screen, &listen);
        if (act == VIEWER_LISTEN_CLOSED) {
            break;
        }
        if (act == VIEWER_LISTEN_REPEAT) {
            continue;
        }
        screen = act == VIEWER_LISTEN_SKIP ? UiListenSkip(screen) : UiListenNext(screen);
    }
    if (!listen.settled) {
        LimiterOff();
    }
    if (!listen.ok) {
        ReportNote("audio: the listening sequence stopped early: %s",
                   AudioError()[0] != '\0' ? AudioError() : "a part did not end in time");
    }
    return listen.ok;
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
    // After the cases, so the closing screen stays up while they run.
    ViewerListenClose();
    AudioClose();
}
