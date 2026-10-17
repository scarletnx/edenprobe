// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "../report.h"
#include "../touchref.h"
#include "../ui.h"
#include "../viewer.h"
#include "tests.h"

// The touch screen as a game reads it: a person taps targets drawn at known points, puts two
// fingers down, drags one across, and the line says what arrived. Part of the pad family, after
// the controller's prompts, since it needs a person too. Every rule is in touchref.c; this file
// reads the touch screen (libnx's hidGetTouchScreenStates, ISC licence, once the viewer has
// activated it) and draws the screen.

static const char *const SUITE = "touch";

_Static_assert(TOUCH_ATTRIBUTE_START == HidTouchAttribute_Start &&
                   TOUCH_ATTRIBUTE_END == HidTouchAttribute_End &&
                   TOUCH_MAX == sizeof(((HidTouchScreenState *)0)->touches) /
                                    sizeof(((HidTouchScreenState *)0)->touches[0]),
               "touchref.h numbers the attributes and sizes a state as libnx does");

#define FRAME_NANOSECONDS 16000000ull
// The screen is drawn this often at most, as the pad's.
#define DRAW_NANOSECONDS 100000000ull
#define RESULT_NANOSECONDS 700000000ull

static const char *heading = "edenprobe";

void TouchSetHeading(const char *text) {
    heading = text != NULL ? text : "edenprobe";
}

static uint64_t Now(void) {
    return armTicksToNs(armGetSystemTick());
}

/// A line for whoever watches the emulator's log rather than the screen.
static void Say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void Say(const char *fmt, ...) {
    char line[200] = "EDENPROBE TOUCH ";
    const size_t used = strlen(line);
    va_list args;
    va_start(args, fmt);
    vsnprintf(line + used, sizeof(line) - used, fmt, args);
    va_end(args);
    svcOutputDebugString(line, strlen(line));
}

// The latest state, and what the reads have shown of the sensor.
static TouchFrame frame;
static uint64_t states;
static uint64_t samplings;
static uint64_t last_sampling;
static TouchShape shape;

static void ReadTouch(void) {
    HidTouchScreenState state;
    memset(&state, 0, sizeof(state));
    memset(&frame, 0, sizeof(frame));
    if (hidGetTouchScreenStates(&state, 1) == 0u) {
        return;
    }
    if (states == 0u || state.sampling_number != last_sampling) {
        ++samplings;
        last_sampling = state.sampling_number;
    }
    ++states;
    frame.count = state.count < 0 ? 0u
                  : (uint32_t)state.count > TOUCH_MAX ? TOUCH_MAX
                                                      : (uint32_t)state.count;
    for (uint32_t i = 0; i < frame.count; ++i) {
        const HidTouchState *const t = &state.touches[i];
        const TouchPoint point = { t->finger_id, t->x, t->y, t->diameter_x, t->diameter_y,
                                   t->rotation_angle, t->attributes };
        frame.points[i] = point;
    }
    TouchShapeAdd(&shape, &frame);
}

// The screen as it stands, redrawn by Draw when it is due.
static UiTouchScreen screen;
static bool screen_up = true;
static uint64_t drawn_ns;
static char state_text[120];
static char now_text[160];
static char last_text[240];

static void Draw(bool now_regardless) {
    const uint64_t now = Now();
    if (!screen_up || (!now_regardless && now - drawn_ns < DRAW_NANOSECONDS)) {
        return;
    }
    screen.touches = frame.count < 4u ? frame.count : 4u;
    for (uint32_t i = 0; i < screen.touches; ++i) {
        screen.touch_x[i] = frame.points[i].x;
        screen.touch_y[i] = frame.points[i].y;
    }
    if (frame.count == 0u) {
        snprintf(now_text, sizeof(now_text), "touching now: nothing");
    } else {
        const TouchPoint *const p = &frame.points[0];
        snprintf(now_text, sizeof(now_text),
                 "touching now: %u, id %u at (%u,%u), %u by %u, angle %u", (unsigned)frame.count,
                 (unsigned)p->id, (unsigned)p->x, (unsigned)p->y, (unsigned)p->diameter_x,
                 (unsigned)p->diameter_y, (unsigned)p->angle);
    }
    screen.heading = heading;
    screen.state = state_text;
    screen.now = now_text;
    // A machine with no screen runs the prompts all the same; the log says each one.
    screen_up = ViewerTouchShow(&screen);
    drawn_ns = now;
}

static void Begin(size_t index, const TouchPrompt *prompt) {
    screen.prompt = (uint32_t)index + 1u;
    screen.prompts = (uint32_t)TouchPromptCount();
    screen.big = prompt->big;
    screen.how = prompt->how;
    screen.marks = 0u;
    if (prompt->ask == TOUCH_ASK_TARGET) {
        screen.marks = 1u;
        screen.mark_x[0] = prompt->x;
        screen.mark_y[0] = prompt->y;
    } else if (prompt->ask == TOUCH_ASK_DRAG) {
        screen.marks = 2u;
        screen.mark_x[0] = TOUCH_DRAG_FROM_X;
        screen.mark_x[1] = TOUCH_DRAG_TO_X;
        screen.mark_y[0] = TOUCH_DRAG_Y;
        screen.mark_y[1] = TOUCH_DRAG_Y;
    }
    state_text[0] = '\0';
    Say("prompt %u of %u: %s", (unsigned)screen.prompt, (unsigned)screen.prompts, prompt->how);
}

static void SecondsLeft(const char *what, uint64_t began, uint64_t length) {
    const uint64_t spent = Now() - began;
    const uint64_t left = spent < length ? length - spent : 0u;
    snprintf(state_text, sizeof(state_text), "%s: %u s left", what,
             (unsigned)((left + 999999999ull) / 1000000000ull));
}

/// Keeps the result up a moment, as the pad's prompts do.
static void Linger(ReportResult result, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void Linger(ReportResult result, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(last_text, sizeof(last_text), fmt, args);
    va_end(args);
    screen.last = last_text;
    screen.last_result = result;
    screen.marks = 0u;
    snprintf(state_text, sizeof(state_text), "done");
    const uint64_t began = Now();
    while (Now() - began < RESULT_NANOSECONDS && appletMainLoop()) {
        ReadTouch();
        Draw(false);
        svcSleepThread(FRAME_NANOSECONDS);
    }
}

typedef enum {
    ASKED_ANSWERED,
    ASKED_UNANSWERED,
    ASKED_UNAVAILABLE,
    ASKED_CLOSED,
} Asked;

/// Waits on @p watch until a finger has come down and every finger is up again, or the time runs
/// out, saying @p waiting on the screen meanwhile.
static Asked Watch(TouchWatch *watch, uint64_t timeout, const char *waiting) {
    ReadTouch();
    TouchWatchBegin(watch, frame.count, Now());
    for (;;) {
        if (!appletMainLoop()) {
            return ASKED_CLOSED;
        }
        ReadTouch();
        const TouchState state = TouchWatchStep(watch, &frame, Now(), timeout);
        if (state == TOUCH_DONE) {
            return ASKED_ANSWERED;
        }
        if (state == TOUCH_TIMED_OUT) {
            return ASKED_UNANSWERED;
        }
        if (state == TOUCH_DOWN) {
            snprintf(state_text, sizeof(state_text), "down: lift to finish");
        } else {
            SecondsLeft(watch->blocked ? "lift first, then" : waiting, watch->began_ns, timeout);
        }
        Draw(false);
        svcSleepThread(FRAME_NANOSECONDS);
    }
}

static char unavailable[200];

static Asked AskReady(size_t index, const TouchPrompt *prompt) {
    Begin(index, prompt);
    const bool docked = appletGetOperationMode() == AppletOperationMode_Console;
    TouchWatch watch;
    const Asked asked = Watch(&watch, TOUCH_READY_NANOSECONDS,
                              docked ? "the console says it is docked: undock it, or wait"
                                     : "waiting for a touch");
    if (asked == ASKED_CLOSED) {
        return ASKED_CLOSED;
    }
    char detail[200];
    const TouchReady ready = TouchReadyJudge(&watch, states, samplings, docked, detail,
                                             sizeof(detail));
    if (ready == TOUCH_READY_YES) {
        ReportCase(SUITE, prompt->name, CHECK_NONE, true, detail);
        Linger(REPORT_PASS, "%s", detail);
        return ASKED_ANSWERED;
    }
    if (ready == TOUCH_READY_UNAVAILABLE) {
        ReportSkip(SUITE, prompt->name, REPORT_REASON_UNAVAILABLE, detail);
        snprintf(unavailable, sizeof(unavailable), "%s", detail);
        Linger(REPORT_SKIP, "%s", detail);
        return ASKED_UNAVAILABLE;
    }
    ReportSkip(SUITE, prompt->name, REPORT_REASON_UNANSWERED, detail);
    Linger(REPORT_SKIP, "nothing touched");
    return ASKED_UNANSWERED;
}

// What the drag's lift left, measured as soon as it happened and reported by the prompt after.
static bool released;
static bool release_passed;
static char release_detail[200];

static void MeasureRelease(uint64_t lifted_ns) {
    TouchRelease release;
    TouchReleaseBegin(&release, lifted_ns);
    snprintf(state_text, sizeof(state_text), "keep every finger off the screen");
    for (;;) {
        ReadTouch();
        if (TouchReleaseStep(&release, &frame, Now()) || !appletMainLoop()) {
            break;
        }
        Draw(false);
        svcSleepThread(FRAME_NANOSECONDS);
    }
    released = true;
    release_passed = TouchReleaseJudge(&release, release_detail, sizeof(release_detail));
}

static Asked AskTouches(size_t index, const TouchPrompt *prompt) {
    Begin(index, prompt);
    TouchWatch watch;
    const Asked asked = Watch(&watch, TOUCH_PROMPT_NANOSECONDS, "waiting");
    if (asked == ASKED_CLOSED) {
        return ASKED_CLOSED;
    }
    if (asked == ASKED_UNANSWERED) {
        ReportSkip(SUITE, prompt->name, REPORT_REASON_UNANSWERED,
                   watch.blocked ? "a finger stayed down from before for 10 s"
                                 : "nothing touched within 10 s");
        Linger(REPORT_SKIP, "%s: nothing touched", prompt->big);
        return ASKED_UNANSWERED;
    }
    if (prompt->ask == TOUCH_ASK_DRAG) {
        MeasureRelease(watch.lifted_ns);
    }
    char detail[320];
    bool passed = false;
    CheckTier tier = CHECK_EXACT;
    switch (prompt->ask) {
    case TOUCH_ASK_TARGET:
        passed = TouchTargetJudge(prompt, &watch, detail, sizeof(detail));
        tier = CHECK_POSITION;
        break;
    case TOUCH_ASK_TWO:
        passed = TouchTwoJudge(&watch, detail, sizeof(detail));
        break;
    default:
        passed = TouchDragJudge(&watch, detail, sizeof(detail));
        tier = CHECK_POSITION;
        break;
    }
    if (watch.held_too_long) {
        const size_t used = strlen(detail);
        snprintf(detail + used, sizeof(detail) - used, "; still down after 20 s");
    }
    ReportCase(SUITE, prompt->name, tier, passed, detail);
    Linger(passed ? REPORT_PASS : REPORT_FAIL, "%s", detail);
    return ASKED_ANSWERED;
}

static void AskRelease(const TouchPrompt *prompt) {
    if (!released) {
        ReportSkip(SUITE, prompt->name, REPORT_REASON_DEPENDS, "no drag was lifted to measure");
        return;
    }
    ReportCase(SUITE, prompt->name, CHECK_EXACT, release_passed, release_detail);
}

void RunTouch(void) {
    screen_up = true;
    drawn_ns = 0;
    states = 0;
    samplings = 0;
    last_sampling = 0;
    released = false;
    unavailable[0] = '\0';
    memset(&screen, 0, sizeof(screen));
    TouchShapeReset(&shape);
    uint32_t refused = 0;
    if (!ViewerTouchInit(&refused)) {
        snprintf(unavailable, sizeof(unavailable),
                 "the touch screen could not be activated (result 0x%08x)", (unsigned)refused);
    }

    const size_t count = TouchPromptCount();
    uint32_t unanswered = 0;
    bool closed = false;
    bool ready = true;
    for (size_t i = 0; i < count; ++i) {
        const TouchPrompt *const prompt = TouchPromptAt(i);
        const bool asks_nobody = prompt->ask == TOUCH_ASK_RELEASE;
        if (closed) {
            ReportSkip(SUITE, prompt->name, REPORT_REASON_UNAVAILABLE,
                       "the system was closing the program");
            continue;
        }
        if (unavailable[0] != '\0') {
            ReportSkip(SUITE, prompt->name, REPORT_REASON_UNAVAILABLE, unavailable);
            continue;
        }
        if (!ready) {
            ReportSkip(SUITE, prompt->name, REPORT_REASON_DEPENDS,
                       "nobody touched the first screen");
            continue;
        }
        if (TouchNobodyThere(unanswered) && !asks_nobody) {
            ReportSkip(SUITE, prompt->name, REPORT_REASON_UNANSWERED,
                       "nobody answered the three prompts before it");
            continue;
        }
        if (!ReportStart(SUITE, prompt->name)) {
            continue;
        }
        Asked asked = ASKED_ANSWERED;
        switch (prompt->ask) {
        case TOUCH_ASK_READY:
            asked = AskReady(i, prompt);
            ready = asked == ASKED_ANSWERED;
            break;
        case TOUCH_ASK_RELEASE:
            AskRelease(prompt);
            break;
        default:
            asked = AskTouches(i, prompt);
            break;
        }
        if (asked == ASKED_CLOSED) {
            ReportSkip(SUITE, prompt->name, REPORT_REASON_UNAVAILABLE,
                       "the system was closing the program");
            closed = true;
        }
        if (!asks_nobody) {
            unanswered = asked == ASKED_UNANSWERED ? unanswered + 1u : 0u;
        }
    }
    char note[REPORT_NOTE_MAX];
    if (TouchShapeNote(&shape, note, sizeof(note))) {
        ReportNote("%s", note);
    }
    ViewerPadClose();
}
