// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../padref.h"
#include "../report.h"
#include "tests.h"

// The pad family's rules against answers worked out by hand: what a press is, what each case
// decides, and the words its lines carry. The distances were worked out with Python's isqrt.

static const char *const SUITE = "reference";

#define MS 1000000ull

static void CheckPrompts(void) {
    const size_t count = PadPromptCount();
    uint64_t seen = 0;
    size_t buttons = 0;
    bool one_bit = true;
    bool named = true;
    bool unique = true;
    size_t style_at = 0;
    for (size_t i = 0; i < count; ++i) {
        const PadPrompt *const p = PadPromptAt(i);
        named = named && p->name != NULL && p->name[0] != '\0' && p->big != NULL && p->how != NULL;
        for (size_t j = 0; j < i; ++j) {
            unique = unique && strcmp(PadPromptAt(j)->name, p->name) != 0;
        }
        if (p->ask == PAD_ASK_BUTTON) {
            one_bit = one_bit && p->button != 0u && (p->button & (p->button - 1u)) == 0u &&
                      (p->button & seen) == 0u && (p->button & PAD_STICK_DIRECTIONS) == 0u;
            seen |= p->button;
            ++buttons;
        }
        if (p->ask == PAD_ASK_STYLE) {
            style_at = i;
        }
    }
    CHECK_TRUE(SUITE, "pad: sixteen buttons are asked for, each once and by one bit", CHECK_NONE,
               count == 26u && buttons == 16u && seen == 0xFFFFu && one_bit && named && unique &&
                   PadPromptAt(0)->ask == PAD_ASK_READY &&
                   PadPromptAt(count) == NULL,
               "%u prompts, %u buttons, bits 0x%llx", (unsigned)count, (unsigned)buttons,
               (unsigned long long)seen);
    // The buttons come first, since the style case asks which controller they came from.
    CHECK_TRUE(SUITE, "pad: the controller answers first, the style case after every button",
               CHECK_NONE,
               style_at == 17u && PadPromptAt(18)->ask == PAD_ASK_SLOTS &&
                   PadPromptAt(19)->ask == PAD_ASK_EACH && PadPromptAt(23)->amplitude == 0.5f &&
                   PadPromptAt(24)->amplitude == 1.0f && PadPromptAt(25)->ask == PAD_ASK_STOPPED,
               "style at %u", (unsigned)style_at);
}

static void CheckWords(void) {
    char text[64];
    PadButtonWords(text, sizeof(text), PAD_A | PAD_ZR);
    bool ok = strcmp(text, "A ZR") == 0;
    PadButtonWords(text, sizeof(text), 0u);
    ok = ok && strcmp(text, "nothing") == 0;
    PadButtonWords(text, sizeof(text), (1ull << 16) | (1ull << 40));
    ok = ok && strcmp(text, "LS-left bit40") == 0;
    char small[6];
    PadButtonWords(small, sizeof(small), PAD_A | PAD_B | PAD_X | PAD_Y);
    ok = ok && strcmp(small, "A B X") == 0;
    CHECK_TRUE(SUITE, "pad: the bits are named low first, and a short buffer stays whole",
               CHECK_NONE, ok, "\"%s\" \"%s\"", text, small);

    char styles[64];
    PadStyleWords(styles, sizeof(styles), PAD_STYLE_FULL_KEY | PAD_STYLE_JOY_DUAL | (1u << 20));
    char none[16];
    PadStyleWords(none, sizeof(none), 0u);
    CHECK_TRUE(SUITE, "pad: the styles and the ids are named as the service names them",
               CHECK_NONE,
               strcmp(styles, "FullKey JoyDual 0x100000") == 0 && strcmp(none, "none") == 0 &&
                   strcmp(PadIdWord(0u), "No1") == 0 && strcmp(PadIdWord(7u), "No8") == 0 &&
                   strcmp(PadIdWord(PAD_ID_HANDHELD), "Handheld") == 0 &&
                   strcmp(PadIdWord(PAD_ID_OTHER), "Other") == 0 &&
                   strcmp(PadIdWord(8u), "unknown") == 0,
               "\"%s\" \"%s\"", styles, none);
}

static void CheckPress(void) {
    // B still down from the prompt before is not a press; A, then ZR inside the window, are.
    PadPress press;
    PadPressBegin(&press, PAD_B, 0u);
    bool ok = PadPressStep(&press, PAD_B, 500u * MS) == PAD_PRESS_WAITING;
    ok = ok && PadPressStep(&press, PAD_B | PAD_A, 1000u * MS) == PAD_PRESS_COLLECTING;
    ok = ok && PadPressStep(&press, PAD_A | PAD_ZR, 1050u * MS) == PAD_PRESS_COLLECTING;
    ok = ok && PadPressStep(&press, PAD_A | PAD_ZR, 1100u * MS) == PAD_PRESS_DONE;
    ok = ok && press.arrived == (PAD_A | PAD_ZR);
    char detail[128];
    const bool judged = PadPressJudge(PAD_A, press.arrived, detail, sizeof(detail));
    CHECK_TRUE(SUITE, "pad: a press is what goes down in its first tenth of a second",
               CHECK_NONE,
               ok && !judged && strcmp(detail, "asked for A, arrived A ZR") == 0,
               "arrived 0x%llx, \"%s\"", (unsigned long long)press.arrived, detail);

    // Let go and pressed again, the held button counts; a bit at the window's end does not.
    PadPressBegin(&press, PAD_B, 0u);
    PadPressStep(&press, 0u, 16u * MS);
    ok = PadPressStep(&press, PAD_B, 32u * MS) == PAD_PRESS_COLLECTING;
    ok = ok && PadPressStep(&press, PAD_B | PAD_Y, 132u * MS) == PAD_PRESS_DONE;
    const bool alone = PadPressJudge(PAD_B, press.arrived, detail, sizeof(detail));
    CHECK_TRUE(SUITE, "pad: a button pressed again counts, and the window closes on time",
               CHECK_NONE, ok && alone && strcmp(detail, "asked for B, arrived B") == 0,
               "arrived 0x%llx, \"%s\"", (unsigned long long)press.arrived, detail);

    // A stick pushed is a direction, never a press, and the prompt runs out at ten seconds.
    PadPressBegin(&press, 0u, 0u);
    ok = PadPressStep(&press, 1ull << 16, 100u * MS) == PAD_PRESS_WAITING;
    ok = ok && PadPressStep(&press, (1ull << 16) | (1ull << 21), 9999u * MS) == PAD_PRESS_WAITING;
    ok = ok && PadPressStep(&press, 0u, 10000u * MS) == PAD_PRESS_TIMED_OUT;
    ok = ok && PadPressStep(&press, PAD_A, 10016u * MS) == PAD_PRESS_TIMED_OUT;
    CHECK_TRUE(SUITE, "pad: a stick direction is not a press, and nothing for 10 s times out",
               CHECK_NONE, ok && press.arrived == 0u, "arrived 0x%llx",
               (unsigned long long)press.arrived);

    CHECK_TRUE(SUITE, "pad: up answers yes, down no, both or neither nothing", CHECK_NONE,
               PadAnswerOf(PAD_UP) == PAD_ANSWER_YES && PadAnswerOf(PAD_DOWN) == PAD_ANSWER_NO &&
                   PadAnswerOf(PAD_UP | PAD_DOWN) == PAD_ANSWER_NONE &&
                   PadAnswerOf(PAD_A) == PAD_ANSWER_NONE &&
                   PadAnswerOf(PadEdges(PAD_UP, PAD_UP)) == PAD_ANSWER_NONE &&
                   PadAnswerOf(PadEdges(0u, 1ull << 17)) == PAD_ANSWER_NONE,
               "the answers were misread");
}

static void CheckSticks(void) {
    CHECK_TRUE(SUITE, "pad: a stick's distance is the integer square root", CHECK_NONE,
               PadRadius(3, 4) == 5u && PadRadius(-3, -4) == 5u &&
                   PadRadius(32767, 32767) == 46339u && PadRadius(-32768, 0) == 32768u &&
                   PadRadius(0, 0) == 0u && PadRadius(16384, 16384) == 23170u &&
                   !PadStickStarted(16383, 0) && PadStickStarted(0, -16384),
               "%u %u %u", (unsigned)PadRadius(3, 4), (unsigned)PadRadius(32767, 32767),
               (unsigned)PadRadius(-32768, 0));

    PadStick left;
    PadStick right;
    PadStickReset(&left);
    PadStickReset(&right);
    for (int i = 0; i < 60; ++i) {
        PadStickAdd(&left, 0, 0);
        PadStickAdd(&right, 0, 0);
    }
    char detail[160];
    const bool rest = PadRestJudge(&left, &right, detail, sizeof(detail));
    PadStickAdd(&right, 0, -12);
    char noisy[160];
    const bool rest_noisy = PadRestJudge(&left, &right, noisy, sizeof(noisy));
    CHECK_TRUE(SUITE, "pad: sticks at rest pass only at nought", CHECK_NONE,
               rest && !rest_noisy &&
                   strcmp(noisy, "at most: left x 0 y 0, right x 0 y 12, over 60 frames") == 0,
               "\"%s\"", noisy);

    // 2% short of full scale passes, one unit more short fails.
    PadStick circle;
    PadStickReset(&circle);
    PadStickAdd(&circle, 32767, 0);
    PadStickAdd(&circle, 0, -32112);
    PadStickAdd(&circle, 23170, 23170);
    const bool round = PadEdgeJudge("left", &circle, detail, sizeof(detail));
    const bool round_said =
        strcmp(detail, "left stick at most: x 32767, y 32112, radius 32767 over 3 frames; the "
                       "edge is a circle") == 0;
    PadStick shy;
    PadStickReset(&shy);
    PadStickAdd(&shy, 32767, 0);
    PadStickAdd(&shy, 0, 32111);
    PadStick square;
    PadStickReset(&square);
    PadStickAdd(&square, 32767, 32767);
    PadStick between;
    PadStickReset(&between);
    PadStickAdd(&between, 32767, 0);
    PadStickAdd(&between, 27577, 27577);
    PadStick empty;
    PadStickReset(&empty);
    CHECK_TRUE(SUITE, "pad: a stick's edge is full scale within 2%, and its shape is named",
               CHECK_NONE,
               round && round_said && !PadEdgeJudge("right", &shy, NULL, 0u) &&
                   PadEdgeJudge("right", &square, NULL, 0u) &&
                   strcmp(PadEdgeShape(&square), "a square") == 0 &&
                   strcmp(PadEdgeShape(&between), "between a circle and a square") == 0 &&
                   !PadEdgeJudge("left", &empty, NULL, 0u) &&
                   strcmp(PadEdgeShape(&empty), "not reached") == 0,
               "\"%s\", between radius %u", detail, (unsigned)between.max_radius);
}

static void CheckStyle(void) {
    char detail[128];
    const PadStyleVerdict first = PadStyleJudge(0u, PAD_STYLE_FULL_KEY, detail, sizeof(detail));
    const bool first_said = strcmp(detail, "the buttons arrived from No1, style FullKey") == 0;
    CHECK_TRUE(SUITE, "pad: player 1 with the full key style passes, and nothing else does",
               CHECK_NONE,
               first == PAD_STYLE_PASS && first_said &&
                   PadStyleJudge(1u, PAD_STYLE_FULL_KEY, NULL, 0u) == PAD_STYLE_FAIL &&
                   PadStyleJudge(0u, PAD_STYLE_JOY_DUAL, NULL, 0u) == PAD_STYLE_FAIL &&
                   PadStyleJudge(PAD_ID_HANDHELD, PAD_STYLE_HANDHELD, NULL, 0u) ==
                       PAD_STYLE_HANDHELD_ONLY &&
                   !PadNobodyThere(2u) && PadNobodyThere(3u),
               "\"%s\"", detail);
}

static void CheckSlots(void) {
    bool connected[PAD_SLOTS] = { false };
    uint32_t styles[PAD_SLOTS] = { 0 };
    connected[0] = true;
    styles[0] = PAD_STYLE_FULL_KEY;
    connected[1] = true;
    styles[1] = PAD_STYLE_JOY_DUAL;
    char detail[256];
    const bool two = PadSlotsJudge(connected, styles, detail, sizeof(detail));
    const bool two_said =
        strcmp(detail, "No1 FullKey, No2 JoyDual, No3 -, No4 -, No5 -, No6 -, No7 -, No8 -, "
                       "Handheld -") == 0;
    styles[1] = 0u;
    const bool styleless = PadSlotsJudge(connected, styles, NULL, 0u);
    bool none[PAD_SLOTS] = { false };
    CHECK_TRUE(SUITE, "pad: every connected slot needs a style, and one must be connected",
               CHECK_NONE,
               two && two_said && !styleless && !PadSlotsJudge(none, styles, NULL, 0u) &&
                   PadSlotId(0u) == 0u && PadSlotId(7u) == 7u && PadSlotId(8u) == PAD_ID_HANDHELD,
               "\"%s\"", detail);

    // No1, then No2 and No3 within a tenth of a second (one controller two slots read), then
    // No1 again; five quiet seconds end it.
    PadEach each;
    PadEachBegin(&each, 0u);
    bool ok = PadEachStep(&each, 0u, 1000u * MS) == PAD_PRESS_WAITING;
    ok = ok && PadEachStep(&each, 1u << 0, 2000u * MS) == PAD_PRESS_COLLECTING;
    ok = ok && PadEachStep(&each, 1u << 1, 3000u * MS) == PAD_PRESS_COLLECTING;
    ok = ok && PadEachStep(&each, 1u << 2, 3050u * MS) == PAD_PRESS_COLLECTING;
    ok = ok && PadEachStep(&each, 1u << 0, 4000u * MS) == PAD_PRESS_COLLECTING;
    ok = ok && PadEachStep(&each, 0u, 8999u * MS) == PAD_PRESS_COLLECTING;
    ok = ok && PadEachStep(&each, 0u, 9000u * MS) == PAD_PRESS_DONE;
    const bool judged = PadEachJudge(&each, detail, sizeof(detail));
    const bool said = strcmp(detail, "3 presses, from No1, No2 and No3, No1") == 0;
    PadEach alone;
    PadEachBegin(&alone, 0u);
    PadEachStep(&alone, 1u << 8, 100u * MS);
    PadEachStep(&alone, 0u, 5100u * MS);
    char one[64];
    const bool alone_ok = PadEachJudge(&alone, one, sizeof(one));
    PadEach nobody;
    PadEachBegin(&nobody, 0u);
    const bool waits = PadEachStep(&nobody, 0u, 9999u * MS) == PAD_PRESS_WAITING;
    const bool gives_up = PadEachStep(&nobody, 0u, 10000u * MS) == PAD_PRESS_TIMED_OUT;
    CHECK_TRUE(SUITE, "pad: each controller's press is the slots that went down together",
               CHECK_NONE,
               ok && !judged && said && each.count == 3u && alone.state == PAD_PRESS_DONE &&
                   alone_ok && strcmp(one, "1 press, from Handheld") == 0 && waits && gives_up &&
                   !PadEachJudge(&nobody, NULL, 0u),
               "\"%s\" \"%s\"", detail, one);
}

void RunReferencePad(void) {
    CheckPrompts();
    CheckWords();
    CheckPress();
    CheckSticks();
    CheckStyle();
    CheckSlots();
}
