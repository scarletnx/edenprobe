// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The pad family's rules, with nothing console specific in them: which prompts a person is
/// given, what counts as a press, and what each case decides from what arrived. The suite in
/// tests/t_pad.c reads the controller and draws the screen; everything it judges is here, where
/// the host build checks it.

/// The button bits as the console's input service numbers them, the order libnx's HidNpadButton
/// gives (hid.h, ISC licence). The console build checks every one against libnx's own.
#define PAD_A (1ull << 0)
#define PAD_B (1ull << 1)
#define PAD_X (1ull << 2)
#define PAD_Y (1ull << 3)
#define PAD_STICK_L (1ull << 4)
#define PAD_STICK_R (1ull << 5)
#define PAD_L (1ull << 6)
#define PAD_R (1ull << 7)
#define PAD_ZL (1ull << 8)
#define PAD_ZR (1ull << 9)
#define PAD_PLUS (1ull << 10)
#define PAD_MINUS (1ull << 11)
#define PAD_LEFT (1ull << 12)
#define PAD_UP (1ull << 13)
#define PAD_RIGHT (1ull << 14)
#define PAD_DOWN (1ull << 15)
/// Bits 16 to 23: a stick pushed past the service's own threshold, which is a direction and not
/// a button, so no press is ever judged on them.
#define PAD_STICK_DIRECTIONS (0xFFull << 16)

/// The controller ids, as the service numbers them: players 1 to 8, then the handheld controls.
#define PAD_ID_PLAYERS 8u
#define PAD_ID_OTHER 0x10u
#define PAD_ID_HANDHELD 0x20u

/// The slots the family reads, each on its own: the eight players, then the handheld controls.
#define PAD_SLOTS 9u

/// The style bits, as the service numbers them (libnx HidNpadStyleTag).
#define PAD_STYLE_FULL_KEY (1u << 0)
#define PAD_STYLE_HANDHELD (1u << 1)
#define PAD_STYLE_JOY_DUAL (1u << 2)
#define PAD_STYLE_JOY_LEFT (1u << 3)
#define PAD_STYLE_JOY_RIGHT (1u << 4)

/// The full scale of a stick axis, either way.
#define PAD_STICK_FULL 32767

/// How long a prompt waits for its answer; nothing by then and the case is skipped as
/// unanswered.
#define PAD_PROMPT_NANOSECONDS 10000000000ull
/// The first screen waits longer for the controller to answer: the person may still be picking
/// it up, or finding that the emulator does not see it.
#define PAD_READY_NANOSECONDS 30000000000ull
/// From the first press, how long the bits that arrive count as that press: one button that
/// arrives as two bits sets both inside this, a person pressing two in turn does not.
#define PAD_WINDOW_NANOSECONDS 100000000ull
/// So many unanswered in a row and the prompts left are skipped too: nobody is there.
#define PAD_UNANSWERED_LIMIT 3u
/// Pressing each controller in turn ends after this long with nothing pressed.
#define PAD_EACH_QUIET_NANOSECONDS 5000000000ull
/// The most presses that prompt keeps.
#define PAD_EACH_MAX 16u
/// Sticks let go: the wait before measuring, and the measure.
#define PAD_REST_SETTLE_NANOSECONDS 2000000000ull
#define PAD_REST_NANOSECONDS 1000000000ull
/// A stick at its edge: measured this long from the moment it first goes past half way.
#define PAD_EDGE_NANOSECONDS 3000000000ull
/// One vibration, and the two bands it is sent on.
#define PAD_VIBRATE_NANOSECONDS 1000000000ull
#define PAD_VIBRATE_LOW_HZ 160.0f
#define PAD_VIBRATE_HIGH_HZ 320.0f

/// What a prompt asks.
typedef enum {
    /// Any button on the controller the person will use: the controller answers, and the run
    /// learns which slot it is in. Nothing after it runs without it.
    PAD_ASK_READY,
    /// One button, by the name printed on it.
    PAD_ASK_BUTTON,
    /// The controller the buttons arrived from: its id and style. Asks the person nothing.
    PAD_ASK_STYLE,
    /// Every slot, connected or not, and its style. Asks the person nothing.
    PAD_ASK_SLOTS,
    /// A on each controller in turn, and the slot each press arrived in.
    PAD_ASK_EACH,
    /// Both sticks let go.
    PAD_ASK_REST,
    /// One stick pushed to its edge and turned.
    PAD_ASK_EDGE_LEFT,
    PAD_ASK_EDGE_RIGHT,
    /// A vibration, then whether it was felt.
    PAD_ASK_VIBRATION,
    /// Whether the last vibration stopped when it was sent nought.
    PAD_ASK_STOPPED,
} PadAsk;

typedef struct {
    /// The case's name on its line.
    const char *name;
    PadAsk ask;
    /// What the screen shows large: a button's label, or a word.
    const char *big;
    /// The sentence under it.
    const char *how;
    /// For PAD_ASK_BUTTON, the one bit that should arrive.
    uint64_t button;
    /// For PAD_ASK_VIBRATION, the amplitude on both bands.
    float amplitude;
} PadPrompt;

/// The prompts, in the order a person is given them: the buttons first, then the controller they
/// came from, the sticks and the vibration.
size_t PadPromptCount(void);
const PadPrompt *PadPromptAt(size_t index);

/// The short name of each bit set in @p mask, space separated, low bit first, or "nothing".
/// Stick directions are named as such. Returns the length; the text is always terminated.
size_t PadButtonWords(char *out, size_t out_size, uint64_t mask);

/// A press, as a prompt collects it from the buttons held frame by frame.
typedef enum {
    PAD_PRESS_WAITING,
    /// The first bit has arrived and the window is open.
    PAD_PRESS_COLLECTING,
    PAD_PRESS_DONE,
    PAD_PRESS_TIMED_OUT,
} PadPressState;

typedef struct {
    uint64_t previous;
    uint64_t arrived;
    uint64_t began_ns;
    uint64_t first_ns;
    PadPressState state;
} PadPress;

/// Begins collecting with what is held now, which is not a press: a button still down from the
/// prompt before, or one an emulator holds, counts only once it is let go and pressed again.
void PadPressBegin(PadPress *press, uint64_t held, uint64_t now_ns);

/// One frame: the bits held now. A bit counts when it goes from up to down; stick directions
/// never do. Hands back the state after this frame.
PadPressState PadPressStep(PadPress *press, uint64_t held, uint64_t now_ns);

/// Whether what arrived is the bit asked for and nothing else, with the sentence the line carries
/// either way.
bool PadPressJudge(uint64_t asked, uint64_t arrived, char *detail, size_t detail_size);

/// The bits that went down between two frames, stick directions left out: what a yes or no
/// answer is read from.
uint64_t PadEdges(uint64_t previous, uint64_t held);

/// A yes or no question's answer: up on the d-pad is yes, down is no, both or neither is none.
typedef enum {
    PAD_ANSWER_NONE,
    PAD_ANSWER_YES,
    PAD_ANSWER_NO,
} PadAnswer;

PadAnswer PadAnswerOf(uint64_t edges);

/// The farthest one stick went over a stretch of frames.
typedef struct {
    uint32_t max_x;
    uint32_t max_y;
    uint32_t max_radius;
    uint32_t samples;
} PadStick;

void PadStickReset(PadStick *stick);
void PadStickAdd(PadStick *stick, int32_t x, int32_t y);

/// The whole number part of the distance of (@p x, @p y) from the centre.
uint32_t PadRadius(int32_t x, int32_t y);

/// Whether a stick has gone far enough that the person has started pushing it: past half way.
bool PadStickStarted(int32_t x, int32_t y);

/// Both sticks let go read nought on both axes: the exact tier.
bool PadRestJudge(const PadStick *left, const PadStick *right, char *detail, size_t detail_size);

/// A stick pushed to its edge reached full scale on both axes, within PAD_EDGE_TOLERANCE_PER_MILLE
/// (the level tier); the sentence says whether its edge is a circle or a square.
#define PAD_EDGE_TOLERANCE_PER_MILLE 20u
bool PadEdgeJudge(const char *which, const PadStick *stick, char *detail, size_t detail_size);

/// "a circle" when the farthest radius is about the farthest axis, "a square" when it is about
/// the axis times the square root of two, and "between a circle and a square" otherwise.
const char *PadEdgeShape(const PadStick *stick);

/// The id as the line names it: "No1" to "No8", "Handheld", "Other", or "unknown".
const char *PadIdWord(uint32_t id);

/// The style bits' names, space separated, or "none". Returns the length.
size_t PadStyleWords(char *out, size_t out_size, uint32_t style);

/// What the style case decides about the controller the buttons arrived from.
typedef enum {
    PAD_STYLE_PASS,
    PAD_STYLE_FAIL,
    /// Answered on the handheld controls: the case asks about an external controller.
    PAD_STYLE_HANDHELD_ONLY,
} PadStyleVerdict;

/// An external controller should be player 1 with the full key style.
PadStyleVerdict PadStyleJudge(uint32_t id, uint32_t style, char *detail, size_t detail_size);

/// Whether the prompts left are skipped because nobody answered the last PAD_UNANSWERED_LIMIT.
bool PadNobodyThere(uint32_t unanswered_in_a_row);

/// The id of slot @p slot: players 1 to 8, then the handheld controls.
uint32_t PadSlotId(uint32_t slot);

/// Every slot that is connected has a style, and at least one is connected; the sentence names
/// all nine, a slot that is not connected as "-".
bool PadSlotsJudge(const bool connected[PAD_SLOTS], const uint32_t styles[PAD_SLOTS], char *detail,
                   size_t detail_size);

/// Pressing each controller in turn, as the prompt collects it: one mask of slots per press, bit
/// n for slot n. The slots that go down within PAD_WINDOW_NANOSECONDS of a press's first are that
/// press: one controller that two slots read arrives in both.
typedef struct {
    uint32_t presses[PAD_EACH_MAX];
    uint32_t count;
    uint64_t began_ns;
    uint64_t first_ns;
    uint64_t last_ns;
    bool collecting;
    PadPressState state;
} PadEach;

void PadEachBegin(PadEach *each, uint64_t now_ns);

/// One frame: the slots that had a button go down in it. Done PAD_EACH_QUIET_NANOSECONDS after
/// the last press; timed out after PAD_PROMPT_NANOSECONDS with none.
PadPressState PadEachStep(PadEach *each, uint32_t slots_pressed, uint64_t now_ns);

/// Each press arrived in one slot only.
bool PadEachJudge(const PadEach *each, char *detail, size_t detail_size);

#ifdef __cplusplus
}
#endif
