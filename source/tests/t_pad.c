// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "../padref.h"
#include "../report.h"
#include "../ui.h"
#include "../viewer.h"
#include "tests.h"

// The pad family: what a game receives from a controller in a person's hands. The screen names
// one button at a time by the label printed on it, and the line says what arrived; the same
// controller on two machines gives two reports that compare line by line. Then the controller
// the buttons came from, the sticks let go and pushed to their edges, and a vibration the person
// says they felt. Every rule is in padref.c; this file reads the controller and draws the screen.
// The library calls are libnx's (ISC licence).

static const char *const SUITE = "pad";

_Static_assert(PAD_A == HidNpadButton_A && PAD_B == HidNpadButton_B &&
                   PAD_X == HidNpadButton_X && PAD_Y == HidNpadButton_Y &&
                   PAD_STICK_L == HidNpadButton_StickL && PAD_STICK_R == HidNpadButton_StickR &&
                   PAD_L == HidNpadButton_L && PAD_R == HidNpadButton_R &&
                   PAD_ZL == HidNpadButton_ZL && PAD_ZR == HidNpadButton_ZR &&
                   PAD_PLUS == HidNpadButton_Plus && PAD_MINUS == HidNpadButton_Minus &&
                   PAD_LEFT == HidNpadButton_Left && PAD_UP == HidNpadButton_Up &&
                   PAD_RIGHT == HidNpadButton_Right && PAD_DOWN == HidNpadButton_Down,
               "padref.h numbers the buttons as libnx does");
_Static_assert(PAD_STICK_DIRECTIONS ==
                   (uint64_t)(HidNpadButton_StickLLeft | HidNpadButton_StickLUp |
                              HidNpadButton_StickLRight | HidNpadButton_StickLDown |
                              HidNpadButton_StickRLeft | HidNpadButton_StickRUp |
                              HidNpadButton_StickRRight | HidNpadButton_StickRDown),
               "padref.h's stick directions are libnx's");
_Static_assert(PAD_SLOTS == 9u && PAD_ID_HANDHELD == HidNpadIdType_Handheld &&
                   PAD_ID_OTHER == HidNpadIdType_Other &&
                   PAD_STYLE_FULL_KEY == HidNpadStyleTag_NpadFullKey &&
                   PAD_STYLE_HANDHELD == HidNpadStyleTag_NpadHandheld &&
                   PAD_STYLE_JOY_DUAL == HidNpadStyleTag_NpadJoyDual &&
                   PAD_STYLE_JOY_LEFT == HidNpadStyleTag_NpadJoyLeft &&
                   PAD_STYLE_JOY_RIGHT == HidNpadStyleTag_NpadJoyRight,
               "padref.h's ids and styles are libnx's");

#define FRAME_NANOSECONDS 16000000ull
// The screen is drawn this often at most: often enough to see a stick move, rarely enough that
// a short press is read between two draws.
#define DRAW_NANOSECONDS 100000000ull
// How long a prompt's result stays up before the next prompt.
#define RESULT_NANOSECONDS 700000000ull
// After a vibration starts, when the value the system holds for it is read back.
#define ACTUAL_NANOSECONDS 100000000ull

// Each slot read on its own, so a press says which controller it came from.
static const HidNpadIdType SLOTS[] = {
    HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4,
    HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7, HidNpadIdType_No8,
    HidNpadIdType_Handheld,
};
#define SLOT_COUNT (sizeof(SLOTS) / sizeof(SLOTS[0]))

static PadState pads[SLOT_COUNT];
static uint64_t previous[SLOT_COUNT];
// The slot the first press came from, which the style case asks about and the sticks and the
// vibration use; -1 until a press arrives.
static int answered = -1;

static const char *heading = "edenprobe";

void PadSetHeading(const char *text) {
    heading = text != NULL ? text : "edenprobe";
}

static uint64_t Now(void) {
    return armTicksToNs(armGetSystemTick());
}

/// A line for whoever watches the emulator's log rather than the screen.
static void Say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void Say(const char *fmt, ...) {
    char line[200] = "EDENPROBE PAD ";
    const size_t used = strlen(line);
    va_list args;
    va_start(args, fmt);
    vsnprintf(line + used, sizeof(line) - used, fmt, args);
    va_end(args);
    svcOutputDebugString(line, strlen(line));
}

// The slots with a new press in the last ReadPads, bit n for slot n.
static uint32_t slots_pressed;

/// One look at every slot: the buttons held across them, and the first slot with a new press
/// in this frame, or -1.
static uint64_t ReadPads(int *pressed_slot) {
    uint64_t held = 0;
    *pressed_slot = -1;
    slots_pressed = 0;
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        padUpdate(&pads[i]);
        if (!padIsConnected(&pads[i])) {
            previous[i] = 0;
            continue;
        }
        const uint64_t now = padGetButtons(&pads[i]);
        if (PadEdges(previous[i], now) != 0u) {
            slots_pressed |= 1u << i;
            if (*pressed_slot < 0) {
                *pressed_slot = (int)i;
            }
        }
        previous[i] = now;
        held |= now;
    }
    return held;
}

/// The slot whose sticks the screen shows and the stick cases measure: the one that answered,
/// or the first connected.
static int StickSlot(void) {
    if (answered >= 0) {
        return answered;
    }
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        if (padIsConnected(&pads[i])) {
            return (int)i;
        }
    }
    return -1;
}

static void Sticks(int32_t out[4]) {
    memset(out, 0, 4u * sizeof(out[0]));
    const int slot = StickSlot();
    if (slot < 0) {
        return;
    }
    const HidAnalogStickState left = padGetStickPos(&pads[slot], 0);
    const HidAnalogStickState right = padGetStickPos(&pads[slot], 1);
    out[0] = left.x;
    out[1] = left.y;
    out[2] = right.x;
    out[3] = right.y;
}

/// The connected slots with their styles, for the screen and the note.
static void Connected(char *out, size_t out_size) {
    out[0] = '\0';
    size_t used = 0;
    for (size_t i = 0; i < SLOT_COUNT && used < out_size; ++i) {
        if (!padIsConnected(&pads[i])) {
            continue;
        }
        char styles[96];
        PadStyleWords(styles, sizeof(styles), padGetStyleSet(&pads[i]));
        const int wrote = snprintf(out + used, out_size - used, "%s%s %s", used > 0u ? ", " : "",
                                   PadIdWord((uint32_t)SLOTS[i]), styles);
        used = wrote < 0 ? out_size : used + (size_t)wrote;
    }
    if (out[0] == '\0') {
        snprintf(out, out_size, "nothing");
    }
}

// The screen as it stands, redrawn by Draw when it is due.
static UiPadScreen screen;
static bool screen_up = true;
static uint64_t drawn_ns;
static char state_text[96];
static char last_text[200];
static char connected_text[200];

static void Draw(uint64_t held, bool now_regardless) {
    const uint64_t now = Now();
    if (!screen_up || (!now_regardless && now - drawn_ns < DRAW_NANOSECONDS)) {
        return;
    }
    int32_t sticks[4];
    Sticks(sticks);
    Connected(connected_text, sizeof(connected_text));
    screen.heading = heading;
    screen.state = state_text;
    screen.held = held;
    screen.left_x = sticks[0];
    screen.left_y = sticks[1];
    screen.right_x = sticks[2];
    screen.right_y = sticks[3];
    screen.controllers = connected_text;
    // A machine with no screen runs the prompts all the same; the log says each one.
    screen_up = ViewerPadShow(&screen);
    drawn_ns = now;
}

static void Begin(size_t index, const PadPrompt *prompt) {
    screen.prompt = (uint32_t)index + 1u;
    screen.prompts = (uint32_t)PadPromptCount();
    screen.big = prompt->big;
    screen.how = prompt->how;
    state_text[0] = '\0';
    Say("prompt %u of %u: %s", (unsigned)screen.prompt, (unsigned)screen.prompts, prompt->how);
}

static void SecondsLeft(const char *what, uint64_t began, uint64_t length) {
    const uint64_t spent = Now() - began;
    const uint64_t left = spent < length ? length - spent : 0u;
    snprintf(state_text, sizeof(state_text), "%s: %u s left", what,
             (unsigned)((left + 999999999ull) / 1000000000ull));
}

/// Keeps the result up a moment, so a person sees what came of the press before the next one.
/// The case's line is already out: a program the system closes meanwhile stops lingering, and the
/// next prompt finds it closing.
static void Linger(ReportResult result, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void Linger(ReportResult result, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(last_text, sizeof(last_text), fmt, args);
    va_end(args);
    screen.last = last_text;
    screen.last_result = result;
    snprintf(state_text, sizeof(state_text), "done");
    const uint64_t began = Now();
    int slot = -1;
    while (Now() - began < RESULT_NANOSECONDS && appletMainLoop()) {
        Draw(ReadPads(&slot), false);
        svcSleepThread(FRAME_NANOSECONDS);
    }
}

/// What became of a prompt that asks a person.
typedef enum {
    ASKED_ANSWERED,
    ASKED_UNANSWERED,
    /// The system is closing the program, and the case has no line yet.
    ASKED_CLOSED,
} Asked;

static Asked AskButton(size_t index, const PadPrompt *prompt) {
    Begin(index, prompt);
    int slot = -1;
    PadPress press;
    PadPressBegin(&press, ReadPads(&slot), Now());
    int from = -1;
    for (;;) {
        if (!appletMainLoop()) {
            return ASKED_CLOSED;
        }
        const uint64_t held = ReadPads(&slot);
        const PadPressState was = press.state;
        const PadPressState state = PadPressStep(&press, held, Now());
        if (was == PAD_PRESS_WAITING && state == PAD_PRESS_COLLECTING) {
            from = slot;
            if (answered < 0) {
                answered = slot;
            }
        }
        if (state == PAD_PRESS_DONE || state == PAD_PRESS_TIMED_OUT) {
            break;
        }
        if (state == PAD_PRESS_WAITING) {
            SecondsLeft("waiting", press.began_ns, PAD_PROMPT_NANOSECONDS);
        }
        Draw(held, false);
        svcSleepThread(FRAME_NANOSECONDS);
    }
    if (press.state == PAD_PRESS_TIMED_OUT) {
        ReportSkip(SUITE, prompt->name, REPORT_REASON_UNANSWERED,
                   "nothing was pressed within 10 s");
        Linger(REPORT_SKIP, "%s: nothing pressed", prompt->big);
    return ASKED_UNANSWERED;
    }
    char judged[300];
    const bool passed = PadPressJudge(prompt->button, press.arrived, judged, sizeof(judged));
    char detail[360];
    snprintf(detail, sizeof(detail), "%s, from %s", judged,
             from >= 0 ? PadIdWord((uint32_t)SLOTS[from]) : "a slot this read missed");
    ReportCase(SUITE, prompt->name, CHECK_EXACT, passed, detail);
    Linger(passed ? REPORT_PASS : REPORT_FAIL, "%s", judged);
    return ASKED_ANSWERED;
}

/// Any button: the controller the person holds answers, and the slot it is in is the one every
/// case after this reads. A press that never comes is an emulator that does not see the
/// controller, or nobody there, and nothing after it can ask anything.
static Asked AskReady(size_t index, const PadPrompt *prompt) {
    Begin(index, prompt);
    int slot = -1;
    const uint64_t began = Now();
    (void)ReadPads(&slot);
    for (;;) {
        if (!appletMainLoop()) {
            return ASKED_CLOSED;
        }
        const uint64_t held = ReadPads(&slot);
        if (slot >= 0) {
            answered = slot;
            break;
        }
        if (Now() - began >= PAD_READY_NANOSECONDS) {
            char connected[200];
            Connected(connected, sizeof(connected));
            char detail[420];
            snprintf(detail, sizeof(detail),
                     "nothing was pressed within 30 s; connected: %s. An emulator shows a slot "
                     "as connected whether or not it sees a controller for it",
                     connected);
            ReportSkip(SUITE, prompt->name, REPORT_REASON_UNANSWERED, detail);
            Linger(REPORT_SKIP, "no controller answered");
            return ASKED_UNANSWERED;
        }
        SecondsLeft("waiting for the controller", began, PAD_READY_NANOSECONDS);
        Draw(held, false);
        svcSleepThread(FRAME_NANOSECONDS);
    }
    char pressed[128];
    PadButtonWords(pressed, sizeof(pressed), PadEdges(0u, previous[answered]));
    char styles[96];
    PadStyleWords(styles, sizeof(styles), padGetStyleSet(&pads[answered]));
    char detail[300];
    snprintf(detail, sizeof(detail), "%s answered, style %s, with %s",
             PadIdWord((uint32_t)SLOTS[answered]), styles, pressed);
    ReportCase(SUITE, prompt->name, CHECK_NONE, true, detail);
    Linger(REPORT_PASS, "%s", detail);
    return ASKED_ANSWERED;
}

static void AskStyle(const PadPrompt *prompt) {
    if (answered < 0) {
        ReportSkip(SUITE, prompt->name, REPORT_REASON_DEPENDS,
                   "no button arrived, so no controller answered");
        return;
    }
    char detail[200];
    const PadStyleVerdict verdict = PadStyleJudge(
        (uint32_t)SLOTS[answered], padGetStyleSet(&pads[answered]), detail, sizeof(detail));
    if (verdict == PAD_STYLE_HANDHELD_ONLY) {
        const size_t used = strlen(detail);
        snprintf(detail + used, sizeof(detail) - used,
                 "; the case asks about an external controller");
        ReportSkip(SUITE, prompt->name, REPORT_REASON_UNAVAILABLE, detail);
        return;
    }
    ReportCase(SUITE, prompt->name, CHECK_EXACT, verdict == PAD_STYLE_PASS, detail);
}

static void AskSlots(const PadPrompt *prompt) {
    bool connected[PAD_SLOTS];
    uint32_t styles[PAD_SLOTS];
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        connected[i] = padIsConnected(&pads[i]);
        styles[i] = connected[i] ? padGetStyleSet(&pads[i]) : 0u;
    }
    char detail[300];
    const bool passed = PadSlotsJudge(connected, styles, detail, sizeof(detail));
    ReportCase(SUITE, prompt->name, CHECK_EXACT, passed, detail);
}

static Asked AskEach(size_t index, const PadPrompt *prompt) {
    Begin(index, prompt);
    int slot = -1;
    (void)ReadPads(&slot);
    PadEach each;
    PadEachBegin(&each, Now());
    for (;;) {
        if (!appletMainLoop()) {
            return ASKED_CLOSED;
        }
        const uint64_t held = ReadPads(&slot);
        const PadPressState state = PadEachStep(&each, slots_pressed, Now());
        if (state == PAD_PRESS_DONE || state == PAD_PRESS_TIMED_OUT) {
            break;
        }
        if (each.count == 0u) {
            SecondsLeft("waiting", each.began_ns, PAD_PROMPT_NANOSECONDS);
        } else {
            snprintf(state_text, sizeof(state_text), "%u so far; 5 s with nothing ends it",
                     (unsigned)each.count);
        }
        Draw(held, false);
        svcSleepThread(FRAME_NANOSECONDS);
    }
    if (each.state == PAD_PRESS_TIMED_OUT) {
        ReportSkip(SUITE, prompt->name, REPORT_REASON_UNANSWERED,
                   "nothing was pressed within 10 s");
        Linger(REPORT_SKIP, "%s: nothing pressed", prompt->big);
        return ASKED_UNANSWERED;
    }
    char detail[300];
    const bool passed = PadEachJudge(&each, detail, sizeof(detail));
    ReportCase(SUITE, prompt->name, CHECK_EXACT, passed, detail);
    Linger(passed ? REPORT_PASS : REPORT_FAIL, "%s", detail);
    return ASKED_ANSWERED;
}

static Asked AskRest(size_t index, const PadPrompt *prompt) {
    Begin(index, prompt);
    if (StickSlot() < 0) {
        ReportSkip(SUITE, prompt->name, REPORT_REASON_UNAVAILABLE, "no controller is connected");
        return ASKED_ANSWERED;
    }
    PadStick left;
    PadStick right;
    PadStickReset(&left);
    PadStickReset(&right);
    const uint64_t began = Now();
    int slot = -1;
    for (;;) {
        if (!appletMainLoop()) {
            return ASKED_CLOSED;
        }
        const uint64_t held = ReadPads(&slot);
        const uint64_t spent = Now() - began;
        if (spent >= PAD_REST_SETTLE_NANOSECONDS + PAD_REST_NANOSECONDS) {
            break;
        }
        if (spent >= PAD_REST_SETTLE_NANOSECONDS) {
            int32_t sticks[4];
            Sticks(sticks);
            PadStickAdd(&left, sticks[0], sticks[1]);
            PadStickAdd(&right, sticks[2], sticks[3]);
            SecondsLeft("measuring", began + PAD_REST_SETTLE_NANOSECONDS, PAD_REST_NANOSECONDS);
        } else {
            SecondsLeft("measuring in", began, PAD_REST_SETTLE_NANOSECONDS);
        }
        Draw(held, false);
        svcSleepThread(FRAME_NANOSECONDS);
    }
    char detail[200];
    const bool passed = PadRestJudge(&left, &right, detail, sizeof(detail));
    ReportCase(SUITE, prompt->name, CHECK_EXACT, passed, detail);
    Linger(passed ? REPORT_PASS : REPORT_FAIL, "%s", detail);
    return ASKED_ANSWERED;
}

static Asked AskEdge(size_t index, const PadPrompt *prompt) {
    Begin(index, prompt);
    const bool left_stick = prompt->ask == PAD_ASK_EDGE_LEFT;
    if (StickSlot() < 0) {
        ReportSkip(SUITE, prompt->name, REPORT_REASON_UNAVAILABLE, "no controller is connected");
        return ASKED_ANSWERED;
    }
    PadStick stick;
    PadStickReset(&stick);
    const uint64_t began = Now();
    uint64_t started = 0;
    int slot = -1;
    for (;;) {
        if (!appletMainLoop()) {
            return ASKED_CLOSED;
        }
        const uint64_t held = ReadPads(&slot);
        int32_t sticks[4];
        Sticks(sticks);
        const int32_t x = left_stick ? sticks[0] : sticks[2];
        const int32_t y = left_stick ? sticks[1] : sticks[3];
        const uint64_t now = Now();
        if (started == 0u && PadStickStarted(x, y)) {
            started = now;
        }
        if (started != 0u) {
            if (now - started >= PAD_EDGE_NANOSECONDS) {
                break;
            }
            PadStickAdd(&stick, x, y);
            SecondsLeft("measuring, keep turning", started, PAD_EDGE_NANOSECONDS);
        } else if (now - began >= PAD_PROMPT_NANOSECONDS) {
            break;
        } else {
            SecondsLeft("waiting", began, PAD_PROMPT_NANOSECONDS);
        }
        Draw(held, false);
        svcSleepThread(FRAME_NANOSECONDS);
    }
    if (started == 0u) {
        ReportSkip(SUITE, prompt->name, REPORT_REASON_UNANSWERED,
                   "the stick did not go past half way within 10 s");
        Linger(REPORT_SKIP, "%s: not pushed", prompt->big);
    return ASKED_UNANSWERED;
    }
    char detail[200];
    const bool passed =
        PadEdgeJudge(left_stick ? "left" : "right", &stick, detail, sizeof(detail));
    ReportCase(SUITE, prompt->name, CHECK_LEVEL, passed, detail);
    Linger(passed ? REPORT_PASS : REPORT_FAIL, "%s", detail);
    return ASKED_ANSWERED;
}

// The vibration devices of the controller that answered, made once.
static HidVibrationDeviceHandle vibration[2];
static int vibration_count = -1;
static char vibration_why[96];

static bool VibrationReady(void) {
    if (vibration_count >= 0) {
        return vibration_count > 0;
    }
    vibration_count = 0;
    const int slot = StickSlot();
    if (slot < 0) {
        snprintf(vibration_why, sizeof(vibration_why), "no controller is connected");
        return false;
    }
    const uint32_t styles = padGetStyleSet(&pads[slot]);
    // The controller's own style, and the two devices a pair of hands has, or the one a single
    // Joy-Con has.
    static const struct {
        uint32_t style;
        int devices;
    } KINDS[] = {
        { PAD_STYLE_FULL_KEY, 2 }, { PAD_STYLE_HANDHELD, 2 }, { PAD_STYLE_JOY_DUAL, 2 },
        { PAD_STYLE_JOY_LEFT, 1 }, { PAD_STYLE_JOY_RIGHT, 1 },
    };
    for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); ++i) {
        if ((styles & KINDS[i].style) == 0u) {
            continue;
        }
        const Result rc = hidInitializeVibrationDevices(vibration, KINDS[i].devices, SLOTS[slot],
                                                        (HidNpadStyleTag)KINDS[i].style);
        if (R_FAILED(rc)) {
            snprintf(vibration_why, sizeof(vibration_why),
                     "the vibration devices of %s did not initialise: 0x%x",
                     PadIdWord((uint32_t)SLOTS[slot]), (unsigned)rc);
            return false;
        }
        vibration_count = KINDS[i].devices;
        return true;
    }
    snprintf(vibration_why, sizeof(vibration_why), "%s has no style with a vibration device",
             PadIdWord((uint32_t)SLOTS[slot]));
    return false;
}

static void Vibrate(float amplitude) {
    HidVibrationValue values[2];
    for (int i = 0; i < 2; ++i) {
        values[i].amp_low = amplitude;
        values[i].freq_low = PAD_VIBRATE_LOW_HZ;
        values[i].amp_high = amplitude;
        values[i].freq_high = PAD_VIBRATE_HIGH_HZ;
    }
    (void)hidSendVibrationValues(vibration, values, vibration_count);
}

/// The value the system holds for the first device, as the line says it.
static void Actual(char *out, size_t out_size) {
    HidVibrationValue value;
    const Result rc = hidGetActualVibrationValue(vibration[0], &value);
    if (R_FAILED(rc)) {
        snprintf(out, out_size, "the value the system holds could not be read: 0x%x",
                 (unsigned)rc);
        return;
    }
    snprintf(out, out_size, "the system holds %.2f at %.0f Hz and %.2f at %.0f Hz",
             (double)value.amp_low, (double)value.freq_low, (double)value.amp_high,
             (double)value.freq_high);
}

/// Up for yes, down for no, or a tap on the bar's yes or no, within the prompt's ten seconds.
static Asked AskYesNo(PadAnswer *answer, bool *by_touch) {
    *answer = PAD_ANSWER_NONE;
    *by_touch = false;
    int slot = -1;
    uint64_t before = ReadPads(&slot);
    const uint64_t began = Now();
    ViewerPadTapBegin();
    screen.asks_yes_no = true;
    Asked asked = ASKED_UNANSWERED;
    for (;;) {
        if (!appletMainLoop()) {
            asked = ASKED_CLOSED;
            break;
        }
        const uint64_t held = ReadPads(&slot);
        *answer = PadAnswerOf(PadEdges(before, held));
        before = held;
        if (*answer == PAD_ANSWER_NONE) {
            *answer = ViewerPadTap();
            *by_touch = *answer != PAD_ANSWER_NONE;
        }
        if (*answer != PAD_ANSWER_NONE) {
            asked = ASKED_ANSWERED;
            break;
        }
        if (Now() - began >= PAD_PROMPT_NANOSECONDS) {
            break;
        }
        SecondsLeft("up: yes, down: no, or tap", began, PAD_PROMPT_NANOSECONDS);
        Draw(held, false);
        svcSleepThread(FRAME_NANOSECONDS);
    }
    screen.asks_yes_no = false;
    return asked;
}

// What the last vibration left: the value held while it ran, for the stopped case's line.
static char after_stop[120];

static Asked AskVibration(size_t index, const PadPrompt *prompt) {
    Begin(index, prompt);
    if (prompt->ask == PAD_ASK_VIBRATION && !VibrationReady()) {
        ReportSkip(SUITE, prompt->name, REPORT_REASON_UNAVAILABLE, vibration_why);
        return ASKED_ANSWERED;
    }
    if (prompt->ask == PAD_ASK_STOPPED && vibration_count <= 0) {
        ReportSkip(SUITE, prompt->name, REPORT_REASON_DEPENDS, "no vibration was sent");
        return ASKED_ANSWERED;
    }
    char held_value[120] = "";
    if (prompt->ask == PAD_ASK_VIBRATION) {
        // The second the controller vibrates, then nought, then the question.
        Vibrate(prompt->amplitude);
        const uint64_t began = Now();
        bool read_back = false;
        int slot = -1;
        while (Now() - began < PAD_VIBRATE_NANOSECONDS) {
            if (!appletMainLoop()) {
                Vibrate(0.0f);
                return ASKED_CLOSED;
            }
            if (!read_back && Now() - began >= ACTUAL_NANOSECONDS) {
                Actual(held_value, sizeof(held_value));
                read_back = true;
            }
            SecondsLeft("vibrating", began, PAD_VIBRATE_NANOSECONDS);
            Draw(ReadPads(&slot), false);
            svcSleepThread(FRAME_NANOSECONDS);
        }
        Vibrate(0.0f);
        svcSleepThread(ACTUAL_NANOSECONDS);
        Actual(after_stop, sizeof(after_stop));
    }
    PadAnswer answer = PAD_ANSWER_NONE;
    bool by_touch = false;
    const Asked asked = AskYesNo(&answer, &by_touch);
    if (asked == ASKED_CLOSED) {
        return ASKED_CLOSED;
    }
    if (asked == ASKED_UNANSWERED) {
        ReportSkip(SUITE, prompt->name, REPORT_REASON_UNANSWERED,
                   "neither up nor down was pressed, nor yes or no tapped, within 10 s");
        Linger(REPORT_SKIP, "%s: no answer", prompt->big);
    return ASKED_UNANSWERED;
    }
    const bool yes = answer == PAD_ANSWER_YES;
    char detail[300];
    const char *const said = by_touch ? (yes ? "yes, tapped" : "no, tapped") : yes ? "yes" : "no";
    if (prompt->ask == PAD_ASK_VIBRATION) {
        snprintf(detail, sizeof(detail), "felt: %s; while it ran %s", said, held_value);
    } else {
        snprintf(detail, sizeof(detail), "stopped: %s; after nought %s", said, after_stop);
    }
    ReportCase(SUITE, prompt->name, CHECK_LEVEL, yes, detail);
    Linger(yes ? REPORT_PASS : REPORT_FAIL, "%s", detail);
    return ASKED_ANSWERED;
}

void RunPad(void) {
    answered = -1;
    vibration_count = -1;
    screen_up = true;
    drawn_ns = 0;
    memset(&screen, 0, sizeof(screen));
    memset(previous, 0, sizeof(previous));
    // Every player and the handheld controls, each read on its own.
    padConfigureInput(PAD_ID_PLAYERS, HidNpadStyleSet_NpadStandard);
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        padInitializeWithMask(&pads[i], 1ull << SLOTS[i]);
    }
    // The first frames of a controller that arrives with the window are its whole mask at once,
    // which is not a person; read past them before anything is asked.
    int slot = -1;
    for (uint32_t i = 0; i < UI_SETTLING_FRAMES; ++i) {
        (void)ReadPads(&slot);
        svcSleepThread(FRAME_NANOSECONDS);
    }

    const size_t count = PadPromptCount();
    uint32_t unanswered = 0;
    bool closed = false;
    bool ready = true;
    for (size_t i = 0; i < count; ++i) {
        const PadPrompt *const prompt = PadPromptAt(i);
        if (closed) {
            ReportSkip(SUITE, prompt->name, REPORT_REASON_UNAVAILABLE,
                       "the system was closing the program");
            continue;
        }
        if (!ready) {
            ReportSkip(SUITE, prompt->name, REPORT_REASON_DEPENDS,
                       "no controller answered the first screen");
            continue;
        }
        const bool asks_nobody = prompt->ask == PAD_ASK_STYLE || prompt->ask == PAD_ASK_SLOTS;
        if (PadNobodyThere(unanswered) && !asks_nobody) {
            ReportSkip(SUITE, prompt->name, REPORT_REASON_UNANSWERED,
                       "nobody answered the three prompts before it");
            continue;
        }
        if (!ReportStart(SUITE, prompt->name)) {
            continue;
        }
        Asked asked = ASKED_ANSWERED;
        switch (prompt->ask) {
        case PAD_ASK_READY:
            asked = AskReady(i, prompt);
            ready = asked == ASKED_ANSWERED;
            break;
        case PAD_ASK_BUTTON:
            asked = AskButton(i, prompt);
            break;
        case PAD_ASK_STYLE:
            AskStyle(prompt);
            break;
        case PAD_ASK_SLOTS:
            AskSlots(prompt);
            break;
        case PAD_ASK_EACH:
            asked = AskEach(i, prompt);
            break;
        case PAD_ASK_REST:
            asked = AskRest(i, prompt);
            break;
        case PAD_ASK_EDGE_LEFT:
        case PAD_ASK_EDGE_RIGHT:
            asked = AskEdge(i, prompt);
            break;
        case PAD_ASK_VIBRATION:
        case PAD_ASK_STOPPED:
            asked = AskVibration(i, prompt);
            break;
        }
        if (asked == ASKED_CLOSED) {
            ReportSkip(SUITE, prompt->name, REPORT_REASON_UNAVAILABLE,
                       "the system was closing the program");
            closed = true;
        }
        // The two that ask nobody say nothing about whether anybody is there.
        if (!asks_nobody) {
            unanswered = asked == ASKED_UNANSWERED ? unanswered + 1u : 0u;
        }
    }
    if (vibration_count > 0) {
        Vibrate(0.0f);
    }
    ViewerPadClose();
}
