// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The touch suite's rules, with nothing console specific in them: the prompts and where their
/// targets are, what a prompt collects from the touch screen frame by frame, and what each case
/// decides from it. The suite in tests/t_touch.c reads the touch screen and draws; everything it
/// judges is here, where the host build checks it.

/// The touch screen's coordinates: one per pixel of the 1280 by 720 frame, as libnx's hid.h
/// gives the touch states.
#define TOUCH_WIDTH 1280u
#define TOUCH_HEIGHT 720u
/// A corner target's distance from both of its edges, far enough in that a fingertip covers it.
#define TOUCH_INSET 96u
/// How far from a target's middle a tap may land and still be on it: about five millimetres on
/// the console's panel, the size of a fingertip aimed at a cross.
#define TOUCH_TOLERANCE 48u
/// The touches one state holds (hid.h's HidTouchScreenState).
#define TOUCH_MAX 16u

/// How long a prompt waits for a finger; the first one waits longer, as the pad's does.
#define TOUCH_PROMPT_NANOSECONDS 10000000000ull
#define TOUCH_READY_NANOSECONDS 30000000000ull
/// Once a finger is down, how long the prompt waits for every finger to come up again.
#define TOUCH_HOLD_NANOSECONDS 20000000000ull
/// After a drag is lifted, how long every state must hold no touch.
#define TOUCH_RELEASE_NANOSECONDS 500000000ull
/// So many unanswered in a row and the prompts left are skipped too: nobody is there.
#define TOUCH_UNANSWERED_LIMIT 3u

/// The drag's two marks, on one row, and what it must do between them: start within a third of
/// the screen of the left mark's side, end in the right third, travel at least half the screen,
/// never go back by more than TOUCH_DRAG_BACK, and be seen in TOUCH_DRAG_FRAMES states or more.
#define TOUCH_DRAG_Y 560u
#define TOUCH_DRAG_FROM_X 160u
#define TOUCH_DRAG_TO_X 1120u
#define TOUCH_DRAG_TRAVEL (TOUCH_WIDTH / 2u)
#define TOUCH_DRAG_BACK 16u
#define TOUCH_DRAG_FRAMES 5u

/// The attribute bits of a touch state (hid.h HidTouchAttribute): its first state, its last.
#define TOUCH_ATTRIBUTE_START 1u
#define TOUCH_ATTRIBUTE_END 2u

/// One finger in one state, as hid.h's HidTouchState carries it.
typedef struct {
    uint32_t id;
    uint32_t x;
    uint32_t y;
    uint32_t diameter_x;
    uint32_t diameter_y;
    uint32_t angle;
    uint32_t attributes;
} TouchPoint;

/// One state: how many fingers, and each of them.
typedef struct {
    uint32_t count;
    TouchPoint points[TOUCH_MAX];
} TouchFrame;

/// What a prompt asks.
typedef enum {
    /// Anything at all: the touch screen answers, and nothing after it runs without it.
    TOUCH_ASK_READY,
    /// One tap on the target at the prompt's x, y.
    TOUCH_ASK_TARGET,
    /// Two fingers at once.
    TOUCH_ASK_TWO,
    /// One finger dragged from the left mark to the right one.
    TOUCH_ASK_DRAG,
    /// The drag's lift, measured after it. Asks the person nothing.
    TOUCH_ASK_RELEASE,
} TouchAsk;

typedef struct {
    /// The case's name on its line.
    const char *name;
    TouchAsk ask;
    /// What the screen shows large, and the sentence under it.
    const char *big;
    const char *how;
    /// For TOUCH_ASK_TARGET, where the target's middle is.
    uint32_t x;
    uint32_t y;
} TouchPrompt;

/// The prompts, in the order a person is given them: the screen answers, the four corners and
/// the middle, two fingers, then the drag and its lift.
size_t TouchPromptCount(void);
const TouchPrompt *TouchPromptAt(size_t index);

typedef enum {
    TOUCH_WAITING,
    /// A finger is down; the prompt waits for every finger to come up.
    TOUCH_DOWN,
    TOUCH_DONE,
    TOUCH_TIMED_OUT,
} TouchState;

/// What a prompt collects from the states between its start and the last finger coming up.
typedef struct {
    TouchState state;
    /// A finger was down when the prompt began: nothing counts until it is lifted.
    bool blocked;
    uint64_t began_ns;
    uint64_t down_ns;
    uint64_t lifted_ns;
    /// Held so long that the prompt stopped waiting for the lift.
    bool held_too_long;
    /// The most touches in one state, and how many states had one.
    uint32_t most;
    uint32_t frames;
    /// The first touch of the first state that had one, and of the last.
    TouchPoint first;
    TouchPoint last;
    /// The first state with two touches or more: its first two.
    bool two;
    TouchPoint pair[2];
    /// The first touch's id changed from one state to the next.
    bool id_changed;
    /// The farthest right the first touch got, and the farthest it went back from there.
    uint32_t max_x;
    uint32_t back;
} TouchWatch;

/// Begins with @p count_now fingers on the screen, which are not an answer.
void TouchWatchBegin(TouchWatch *watch, uint32_t count_now, uint64_t now_ns);

/// One state. Timed out after @p timeout_ns with no finger; done once every finger is up again,
/// or TOUCH_HOLD_NANOSECONDS after the first came down.
TouchState TouchWatchStep(TouchWatch *watch, const TouchFrame *frame, uint64_t now_ns,
                          uint64_t timeout_ns);

/// The first prompt: a touch arrived, or why none could.
typedef enum {
    TOUCH_READY_YES,
    /// No touch screen to touch: a docked console, or one that sends no state.
    TOUCH_READY_UNAVAILABLE,
    /// A touch screen that sends states, and nobody touched it.
    TOUCH_READY_UNANSWERED,
} TouchReady;

/// @p states is how many reads handed back a state, @p samplings how many different sampling
/// numbers they carried; a sensor that runs hands back a new one every few milliseconds.
TouchReady TouchReadyJudge(const TouchWatch *watch, uint64_t states, uint64_t samplings,
                           bool docked, char *detail, size_t detail_size);

/// The tap landed within TOUCH_TOLERANCE of the target, judged where the finger was last before
/// it came up.
bool TouchTargetJudge(const TouchPrompt *prompt, const TouchWatch *watch, char *detail,
                      size_t detail_size);

/// Two touches arrived in one state, with different ids.
bool TouchTwoJudge(const TouchWatch *watch, char *detail, size_t detail_size);

/// One finger, one id, moving right from the left of the screen to the right of it.
bool TouchDragJudge(const TouchWatch *watch, char *detail, size_t detail_size);

/// The states after the drag's lift.
typedef struct {
    uint64_t lifted_ns;
    uint32_t frames;
    uint32_t touched;
    TouchPoint seen;
    uint64_t seen_ns;
} TouchRelease;

void TouchReleaseBegin(TouchRelease *release, uint64_t lifted_ns);

/// One state; true once TOUCH_RELEASE_NANOSECONDS have passed since the lift.
bool TouchReleaseStep(TouchRelease *release, const TouchFrame *frame, uint64_t now_ns);

/// Every state after the lift held no touch.
bool TouchReleaseJudge(const TouchRelease *release, char *detail, size_t detail_size);

/// What the fingers' shapes were, over every finger in every state the suite read: for the note.
typedef struct {
    uint32_t touches;
    uint32_t min_diameter_x;
    uint32_t max_diameter_x;
    uint32_t min_diameter_y;
    uint32_t max_diameter_y;
    uint32_t min_angle;
    uint32_t max_angle;
    /// The ids seen, in the order they first came, at most eight.
    uint32_t ids[8];
    uint32_t id_count;
} TouchShape;

void TouchShapeReset(TouchShape *shape);
void TouchShapeAdd(TouchShape *shape, const TouchFrame *frame);

/// The note's text, or false with nothing written when no touch was read.
bool TouchShapeNote(const TouchShape *shape, char *out, size_t out_size);

/// Whether the prompts left are skipped because nobody answered the last
/// TOUCH_UNANSWERED_LIMIT.
bool TouchNobodyThere(uint32_t unanswered_in_a_row);

#ifdef __cplusplus
}
#endif
