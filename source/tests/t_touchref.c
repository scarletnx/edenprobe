// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../report.h"
#include "../touchref.h"
#include "tests.h"

// The touch suite's rules on states made to order: what a prompt collects frame by frame, and
// what each case decides from it, against answers and sentences worked out by hand.

static const char *const SUITE = "reference";

#define MS 1000000ull

static TouchFrame frame;

/// One state of @p count fingers, the first at @p x, @p y with @p id, the second beside it.
static const TouchFrame *State(uint32_t count, uint32_t id, uint32_t x, uint32_t y) {
    memset(&frame, 0, sizeof(frame));
    frame.count = count;
    for (uint32_t i = 0; i < count && i < TOUCH_MAX; ++i) {
        frame.points[i].id = id + i;
        frame.points[i].x = x + 200u * i;
        frame.points[i].y = y;
        frame.points[i].diameter_x = 90u;
        frame.points[i].diameter_y = 80u + i;
        frame.points[i].angle = 0u;
    }
    return &frame;
}

static void CheckPrompts(void) {
    const size_t count = TouchPromptCount();
    bool unique = true;
    bool inside = true;
    uint32_t targets = 0;
    for (size_t i = 0; i < count; ++i) {
        const TouchPrompt *const p = TouchPromptAt(i);
        for (size_t j = 0; j < i; ++j) {
            unique = unique && strcmp(TouchPromptAt(j)->name, p->name) != 0;
        }
        if (p->ask == TOUCH_ASK_TARGET) {
            ++targets;
            // The whole circle a tap may land in is on the screen.
            inside = inside && p->x >= TOUCH_TOLERANCE && p->y >= TOUCH_TOLERANCE &&
                     p->x + TOUCH_TOLERANCE < TOUCH_WIDTH && p->y + TOUCH_TOLERANCE < TOUCH_HEIGHT;
        }
    }
    const TouchPrompt *const at = TouchPromptAt(1);
    const TouchPrompt *const middle = TouchPromptAt(5);
    CHECK_TRUE(SUITE, "touch: the screen answers, then four corners and the middle, two fingers, "
                      "the drag and its lift",
               CHECK_NONE,
               count == 9u && targets == 5u && unique && inside &&
                   TouchPromptAt(0)->ask == TOUCH_ASK_READY && at->x == 96u && at->y == 96u &&
                   TouchPromptAt(4)->x == 1184u && TouchPromptAt(4)->y == 624u &&
                   middle->x == 640u && middle->y == 360u &&
                   TouchPromptAt(6)->ask == TOUCH_ASK_TWO &&
                   TouchPromptAt(7)->ask == TOUCH_ASK_DRAG &&
                   TouchPromptAt(8)->ask == TOUCH_ASK_RELEASE && TouchPromptAt(count) == NULL,
               "%u prompts, %u targets, unique %d, inside %d", (unsigned)count, (unsigned)targets,
               unique, inside);
}

static void CheckWatch(void) {
    TouchWatch w;
    // A finger down when the prompt begins is no answer, not even once it moves.
    TouchWatchBegin(&w, 1u, 0u);
    const bool held = TouchWatchStep(&w, State(1u, 4u, 500u, 300u), 10u * MS,
                                     TOUCH_PROMPT_NANOSECONDS) == TOUCH_WAITING &&
                      TouchWatchStep(&w, State(0u, 0u, 0u, 0u), 20u * MS,
                                     TOUCH_PROMPT_NANOSECONDS) == TOUCH_WAITING;
    // The next one is: down, moved, up.
    const bool down = TouchWatchStep(&w, State(1u, 5u, 100u, 110u), 30u * MS,
                                     TOUCH_PROMPT_NANOSECONDS) == TOUCH_DOWN;
    (void)TouchWatchStep(&w, State(1u, 5u, 104u, 112u), 40u * MS, TOUCH_PROMPT_NANOSECONDS);
    const bool done = TouchWatchStep(&w, State(0u, 0u, 0u, 0u), 50u * MS,
                                     TOUCH_PROMPT_NANOSECONDS) == TOUCH_DONE;
    const bool kept = w.first.x == 100u && w.first.id == 5u && w.last.x == 104u &&
                      w.last.y == 112u && w.frames == 2u && w.most == 1u &&
                      w.lifted_ns == 50u * MS && !w.held_too_long;
    // Nothing for ten seconds; a finger held twenty seconds ends the wait without a lift.
    TouchWatchBegin(&w, 0u, 0u);
    const bool late = TouchWatchStep(&w, State(0u, 0u, 0u, 0u), TOUCH_PROMPT_NANOSECONDS,
                                     TOUCH_PROMPT_NANOSECONDS) == TOUCH_TIMED_OUT;
    TouchWatchBegin(&w, 0u, 0u);
    (void)TouchWatchStep(&w, State(1u, 0u, 9u, 9u), MS, TOUCH_PROMPT_NANOSECONDS);
    const bool stuck = TouchWatchStep(&w, State(1u, 0u, 9u, 9u), MS + TOUCH_HOLD_NANOSECONDS,
                                      TOUCH_PROMPT_NANOSECONDS) == TOUCH_DONE &&
                       w.held_too_long;
    CHECK_TRUE(SUITE, "touch: a prompt waits out a finger already down, then takes the next tap",
               CHECK_NONE, held && down && done && kept && late && stuck,
               "held %d, down %d, done %d, kept %d (first %u last %u,%u states %u), late %d, "
               "stuck %d",
               held, down, done, kept, (unsigned)w.first.x, (unsigned)w.last.x,
               (unsigned)w.last.y, (unsigned)w.frames, late, stuck);
}

static void CheckReady(void) {
    TouchWatch w;
    char detail[200];
    TouchWatchBegin(&w, 0u, 0u);
    (void)TouchWatchStep(&w, State(1u, 0u, 640u, 360u), MS, TOUCH_READY_NANOSECONDS);
    (void)TouchWatchStep(&w, State(0u, 0u, 0u, 0u), 2u * MS, TOUCH_READY_NANOSECONDS);
    const bool yes = TouchReadyJudge(&w, 900u, 900u, true, detail, sizeof(detail)) ==
                         TOUCH_READY_YES &&
                     strcmp(detail, "a touch arrived: id 0 at (640,360), 1 states while down, at "
                                    "most 1 at once") == 0;
    TouchWatchBegin(&w, 0u, 0u);
    (void)TouchWatchStep(&w, State(0u, 0u, 0u, 0u), TOUCH_READY_NANOSECONDS,
                         TOUCH_READY_NANOSECONDS);
    const bool docked = TouchReadyJudge(&w, 900u, 900u, true, detail, sizeof(detail)) ==
                            TOUCH_READY_UNAVAILABLE &&
                        strstr(detail, "docked") != NULL;
    const bool none = TouchReadyJudge(&w, 0u, 0u, false, detail, sizeof(detail)) ==
                          TOUCH_READY_UNAVAILABLE &&
                      strcmp(detail, "the touch screen handed back no state within 30 s") == 0;
    const bool still = TouchReadyJudge(&w, 900u, 1u, false, detail, sizeof(detail)) ==
                           TOUCH_READY_UNAVAILABLE &&
                       strstr(detail, "never moved") != NULL;
    const bool nobody = TouchReadyJudge(&w, 900u, 880u, false, detail, sizeof(detail)) ==
                            TOUCH_READY_UNANSWERED &&
                        strcmp(detail, "nothing touched within 30 s; the touch screen sent 900 "
                                       "states, 880 of them new") == 0;
    CHECK_TRUE(SUITE, "touch: the first prompt tells a touch from a docked console, a silent "
                      "screen and nobody there",
               CHECK_NONE, yes && docked && none && still && nobody,
               "yes %d docked %d none %d still %d nobody %d: \"%s\"", yes, docked, none, still,
               nobody, detail);
}

/// A tap that came down at @p x0, @p y0 and was lifted at @p x1, @p y1.
static void Tap(TouchWatch *w, uint32_t count, uint32_t x0, uint32_t y0, uint32_t x1,
                uint32_t y1) {
    TouchWatchBegin(w, 0u, 0u);
    (void)TouchWatchStep(w, State(count, 3u, x0, y0), MS, TOUCH_PROMPT_NANOSECONDS);
    (void)TouchWatchStep(w, State(count, 3u, x1, y1), 2u * MS, TOUCH_PROMPT_NANOSECONDS);
    (void)TouchWatchStep(w, State(0u, 0u, 0u, 0u), 3u * MS, TOUCH_PROMPT_NANOSECONDS);
}

static void CheckTargets(void) {
    const TouchPrompt *const p = TouchPromptAt(1); // the top left, (96,96)
    TouchWatch w;
    char detail[240];
    Tap(&w, 1u, 90u, 90u, 96u, 96u);
    const bool on = TouchTargetJudge(p, &w, detail, sizeof(detail)) &&
                    strcmp(detail, "lifted at (96,96), +0 +0 from the target at (96,96): 0 pixels "
                                   "off, within 48; came down at (90,90), id 3, 2 states") == 0;
    char on_said[240];
    snprintf(on_said, sizeof(on_said), "%s", detail);
    Tap(&w, 1u, 96u, 96u, 144u, 96u); // exactly the tolerance
    const bool edge = TouchTargetJudge(p, &w, detail, sizeof(detail));
    Tap(&w, 1u, 96u, 96u, 125u, 135u); // 29 and 39 make 48.6, whole part 48
    const bool diagonal = TouchTargetJudge(p, &w, detail, sizeof(detail));
    Tap(&w, 1u, 96u, 96u, 145u, 96u);
    const bool past = !TouchTargetJudge(p, &w, detail, sizeof(detail)) &&
                      strstr(detail, "49 pixels off, past 48") != NULL;
    // A screen mapped upside down puts the top left tap at the bottom left.
    Tap(&w, 1u, 96u, 624u, 96u, 624u);
    const bool flipped = !TouchTargetJudge(p, &w, detail, sizeof(detail)) &&
                         strstr(detail, "+0 +528") != NULL;
    TouchWatchBegin(&w, 0u, 0u);
    (void)TouchWatchStep(&w, State(0u, 0u, 0u, 0u), TOUCH_PROMPT_NANOSECONDS,
                         TOUCH_PROMPT_NANOSECONDS);
    const bool timed_out = !TouchTargetJudge(p, &w, detail, sizeof(detail));
    CHECK_TRUE(SUITE, "touch: a tap passes within 48 pixels of its target, judged where it lifted",
               CHECK_NONE, on && edge && diagonal && past && flipped && timed_out,
               "on %d \"%s\"; edge %d diagonal %d past %d flipped %d timed out %d", on, on_said,
               edge, diagonal, past, flipped, timed_out);
}

static void CheckTwoAndDrag(void) {
    TouchWatch w;
    char detail[300];
    Tap(&w, 2u, 300u, 400u, 300u, 400u);
    const bool two = TouchTwoJudge(&w, detail, sizeof(detail)) &&
                     strcmp(detail, "two at once, ids 3 and 4, at (300,400) and (500,400); at most "
                                    "2") == 0;
    Tap(&w, 1u, 300u, 400u, 300u, 400u);
    const bool one = !TouchTwoJudge(&w, detail, sizeof(detail)) &&
                     strcmp(detail, "never two at once: the most in one state was 1, over 2 "
                                    "states") == 0;
    TouchWatchBegin(&w, 0u, 0u);
    (void)TouchWatchStep(&w, State(2u, 3u, 300u, 400u), MS, TOUCH_PROMPT_NANOSECONDS);
    w.pair[1].id = w.pair[0].id;
    const bool same = !TouchTwoJudge(&w, detail, sizeof(detail)) &&
                      strstr(detail, "both with one id") != NULL;

    // A drag: right in eight steps, one of them 16 back, under one id.
    static const uint32_t PATH[] = { 170u, 300u, 450u, 600u, 584u, 800u, 1000u, 1110u };
    TouchWatchBegin(&w, 0u, 0u);
    for (uint32_t i = 0; i < 8u; ++i) {
        (void)TouchWatchStep(&w, State(1u, 7u, PATH[i], 560u), (i + 1u) * MS,
                             TOUCH_PROMPT_NANOSECONDS);
    }
    w.first.attributes = TOUCH_ATTRIBUTE_START;
    w.last.attributes = TOUCH_ATTRIBUTE_END;
    (void)TouchWatchStep(&w, State(0u, 0u, 0u, 0u), 9u * MS, TOUCH_PROMPT_NANOSECONDS);
    const bool drag = TouchDragJudge(&w, detail, sizeof(detail)) &&
                      strcmp(detail, "from x 170 to 1110 under id 7, 8 states, back at most 16 of "
                                     "16 allowed; start attribute on the first state yes, end on "
                                     "the last yes") == 0;
    char drag_said[300];
    snprintf(drag_said, sizeof(drag_said), "%s", detail);
    const TouchWatch good = w;
    w.back = 17u;
    const bool jitter = !TouchDragJudge(&w, detail, sizeof(detail)) &&
                        strstr(detail, "went back past the jitter allowed; from x 170") == detail;
    w = good;
    w.id_changed = true;
    const bool id = !TouchDragJudge(&w, detail, sizeof(detail)) &&
                    strstr(detail, "the id changed") == detail;
    w = good;
    w.most = 2u;
    const bool fingers = !TouchDragJudge(&w, detail, sizeof(detail));
    w = good;
    w.last.x = 700u;
    const bool short_of = !TouchDragJudge(&w, detail, sizeof(detail)) &&
                          strstr(detail, "ended short of the right third; travelled under half") ==
                              detail;
    w = good;
    w.frames = 4u;
    const bool few = !TouchDragJudge(&w, detail, sizeof(detail));
    // The id changing between states is seen by the watch itself.
    TouchWatchBegin(&w, 0u, 0u);
    (void)TouchWatchStep(&w, State(1u, 1u, 200u, 560u), MS, TOUCH_PROMPT_NANOSECONDS);
    (void)TouchWatchStep(&w, State(1u, 2u, 900u, 560u), 2u * MS, TOUCH_PROMPT_NANOSECONDS);
    const bool watched = w.id_changed && w.back == 0u && w.max_x == 900u;
    CHECK_TRUE(SUITE, "touch: two fingers need two ids, and a drag one finger, one id, going right",
               CHECK_NONE,
               two && one && same && drag && jitter && id && fingers && short_of && few && watched,
               "two %d one %d same %d; drag %d \"%.150s\"; jitter %d id %d fingers %d short %d "
               "few %d watched %d",
               two, one, same, drag, drag_said, jitter, id, fingers, short_of, few, watched);
}

static void CheckRelease(void) {
    TouchRelease r;
    char detail[200];
    TouchReleaseBegin(&r, 100u * MS);
    bool measured = false;
    for (uint64_t t = 100u * MS; !measured; t += 16u * MS) {
        measured = TouchReleaseStep(&r, State(0u, 0u, 0u, 0u), t);
    }
    const bool clean = TouchReleaseJudge(&r, detail, sizeof(detail)) &&
                       strcmp(detail, "no touch in any of 32 states over the 500 ms after the "
                                      "lift") == 0;
    char clean_said[200];
    snprintf(clean_said, sizeof(clean_said), "%s", detail);
    TouchReleaseBegin(&r, 0u);
    (void)TouchReleaseStep(&r, State(0u, 0u, 0u, 0u), 16u * MS);
    (void)TouchReleaseStep(&r, State(1u, 9u, 10u, 20u), 32u * MS);
    (void)TouchReleaseStep(&r, State(0u, 0u, 0u, 0u), TOUCH_RELEASE_NANOSECONDS);
    const bool ghost = !TouchReleaseJudge(&r, detail, sizeof(detail)) &&
                       strcmp(detail, "1 of 2 states after the lift held a touch, the first 32 ms "
                                      "after it: id 9 at (10,20)") == 0;
    TouchReleaseBegin(&r, 0u);
    const bool unread = !TouchReleaseJudge(&r, detail, sizeof(detail));
    CHECK_TRUE(SUITE,
               "touch: a lifted finger is followed by states with no touch for half a second",
               CHECK_NONE, clean && ghost && unread, "clean %d \"%s\"; ghost %d; unread %d",
               clean, clean_said, ghost, unread);
}

static void CheckShape(void) {
    TouchShape shape;
    char note[200];
    TouchShapeReset(&shape);
    const bool empty = !TouchShapeNote(&shape, note, sizeof(note));
    TouchShapeAdd(&shape, State(2u, 0u, 10u, 10u));
    TouchShapeAdd(&shape, State(1u, 0u, 10u, 10u));
    TouchShapeAdd(&shape, State(1u, 5u, 10u, 10u));
    const bool said = TouchShapeNote(&shape, note, sizeof(note)) &&
                      strcmp(note, "touch: 4 finger states read; diameter 90..90 across and "
                                   "80..81 down, angle 0..0; ids 0 1 5") == 0;
    CHECK_TRUE(SUITE, "touch: the note gives every touch's diameter and angle range and the ids",
               CHECK_NONE,
               empty && said && !TouchNobodyThere(2u) && TouchNobodyThere(3u),
               "empty %d, \"%s\"", empty, note);
}

void RunReferenceTouch(void) {
    CheckPrompts();
    CheckWatch();
    CheckReady();
    CheckTargets();
    CheckTwoAndDrag();
    CheckRelease();
    CheckShape();
}
