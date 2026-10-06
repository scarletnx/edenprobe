// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "picture.h"
#include "report.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The detailed mode's screens, and the navigation behind them, with nothing console specific in
/// either, so the host build can compose a frame and step the navigation with no device.
///
/// Three levels: an overview of every group, one group's cases, and one case, which is its three
/// pictures when it kept them and its whole detail when it did not. A group is a suite as the
/// report names it, and the groups are gathered under their family.
///
/// Nothing here decides anything: it draws the cases the report recorded, and the pixels a
/// verdict came from. A screen is never an input to a result.

/// Where the pictures start down the frame, under the two lines of heading and the labels.
/// Named here rather than buried in the layout because the cases that check the layout have to
/// look in the same place it drew.
#define UI_PANEL_TOP 72u

/// Where the overview's and a group's list start, the height of one of their rows, and where a
/// group's result marks start across it.
#define UI_LIST_TOP 72u
#define UI_ROW_HEIGHT 20u
#define UI_MARK_LEFT 64u

/// The smallest frame the screens lay out on; a smaller one gets a line saying so. The console's
/// is 1280 by 720.
#define UI_MIN_WIDTH 640u
#define UI_MIN_HEIGHT 360u

/// Somewhere to draw. @p pitch is the bytes between one row and the next, which is the display's
/// business and not always four times the width.
typedef struct {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} UiFrame;

/// A case's pictures: the three side by side, or one of them on its own.
typedef enum {
    UI_VIEW_ALL,
    UI_VIEW_GOT,
    UI_VIEW_EXPECTED,
    UI_VIEW_DIFF,
} UiView;

/// The families the groups are gathered under, in the order the run reaches them. A suite this
/// build does not know is shown under other, last, rather than not at all.
typedef enum {
    UI_FAMILY_JUDGE,
    UI_FAMILY_CPU,
    UI_FAMILY_GRAPHICS,
    UI_FAMILY_FS,
    UI_FAMILY_AUDIO,
    UI_FAMILY_NET,
    UI_FAMILY_OTHER,
    UI_FAMILY_COUNT,
} UiFamily;

UiFamily UiFamilyOf(const char *suite);
const char *UiFamilyName(UiFamily family);

/// The most groups and cases the screens index. A run has up to forty suites and about eight
/// hundred cases; what does not fit is counted and the overview says so. Under 255, which marks
/// a case with no group while the index is built.
#define UI_MAX_GROUPS 64u
#define UI_MAX_CASES REPORT_MAX_RECORDS

typedef struct {
    /// The suite as the report names it, pointing into the records.
    const char *suite;
    UiFamily family;
    /// Where this group's cases start in UiRun.order, and how many there are.
    uint32_t first;
    uint32_t count;
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
    /// Skipped because the program ended inside them, counted apart as the summary counts them.
    uint32_t crashed;
} UiGroup;

/// The recorded cases, gathered into groups: families in their order, groups within a family in
/// the order the run first reached them, cases within a group in the order they were reported.
typedef struct {
    const ReportRecord *records;
    uint32_t count;
    /// How many cases the run reported, which is more than @p count when the store ran out.
    uint32_t reported;
    /// Cases that did not fit the index, past UI_MAX_GROUPS suites or UI_MAX_CASES cases.
    uint32_t unplaced;
    uint32_t group_count;
    UiGroup groups[UI_MAX_GROUPS];
    /// Indices into @p records, group by group.
    uint16_t order[UI_MAX_CASES];
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
    uint32_t crashed;
} UiRun;

/// Indexes @p count records for the screens. @p reported is what the run's own counts add up to.
void UiRunIndex(UiRun *run, const ReportRecord *records, uint32_t count, uint32_t reported);

/// The @p index th case of @p group, or NULL when there is no such case.
const ReportRecord *UiCaseAt(const UiRun *run, uint32_t group, uint32_t index);

typedef enum {
    UI_LEVEL_OVERVIEW,
    UI_LEVEL_GROUP,
    UI_LEVEL_CASE,
} UiLevel;

typedef struct {
    UiLevel level;
    /// The group the cursor is on, at every level.
    uint32_t group;
    /// The case the cursor is on within that group, at the group and case levels.
    uint32_t index;
    UiView view;
    bool quit;
    /// Whether the screen owes a frame. Set by anything that moved, and stays set until a
    /// frame is actually drawn, which is what makes the very first one get drawn at all. The
    /// display clears it and sleeps otherwise, so a still screen costs an emulated processor
    /// nothing.
    bool changed;
} UiState;

/// The case the cursor is on, or NULL on the overview and in a run with nothing kept.
const ReportRecord *UiSelected(const UiRun *run, UiState state);

/// The index of the picture the screen shows, for PictureAt: the open case's, or -1 when no case
/// is open or the open one kept none, which the case screen then draws as its detail.
int32_t UiOpenPicture(const UiRun *run, UiState state);

typedef enum {
    UI_INPUT_NONE,
    UI_INPUT_UP,
    UI_INPUT_DOWN,
    UI_INPUT_LEFT,
    UI_INPUT_RIGHT,
    UI_INPUT_OPEN,
    UI_INPUT_BACK,
    UI_INPUT_NEXT_FAILURE,
    UI_INPUT_PREVIOUS_FAILURE,
    UI_INPUT_CYCLE_VIEW,
    UI_INPUT_QUIT,
} UiInput;

/// How many frames the pad is given to turn up before the launch button is given up on.
#define UI_PAD_WAIT_FRAMES 12u

/// Whether to keep waiting for a controller to appear. It stops the moment one answers, so a
/// machine that already has one pays two frames and a machine with none pays the ceiling.
bool UiWaitsForPad(uint32_t frame_index, bool connected);

/// How long a detailed run waits, once its report is closed, for anyone to press anything.
/// An emulator shows a controller as connected whether or not a person holds it, so the only
/// sign of a person is a press. A script that forgot the mode file must not hang an emulator.
/// Ten seconds, so that a person reading the first screen on a console does not lose it.
#define UI_UNATTENDED_NANOSECONDS 10000000000ull

/// Whether the screen leaves by itself: nothing pressed, @p elapsed_ns after the report closed.
bool UiLeavesUnattended(uint64_t elapsed_ns, bool pressed);

/// Whole seconds left before it does, rounded up, and 0 once a press has been seen or the time
/// is up. What the screen counts down with.
uint32_t UiUnattendedSecondsLeft(uint64_t elapsed_ns, bool pressed);

/// Draws the countdown line above the key at the bottom of the frame, or clears it for 0.
void UiComposeCountdown(const UiFrame *frame, uint32_t seconds_left);

/// The buttons the screens know about. Named here rather than taken from the console's own
/// header so that what each one does is a decision this build can check.
enum {
    UI_BUTTON_UP = 1u << 0,
    UI_BUTTON_DOWN = 1u << 1,
    UI_BUTTON_LEFT = 1u << 2,
    UI_BUTTON_RIGHT = 1u << 3,
    UI_BUTTON_OPEN = 1u << 4,
    UI_BUTTON_BACK = 1u << 5,
    UI_BUTTON_NEXT_FAILURE = 1u << 6,
    UI_BUTTON_PREVIOUS_FAILURE = 1u << 7,
    UI_BUTTON_CYCLE_VIEW = 1u << 8,
    UI_BUTTON_QUIT = 1u << 9,
    /// The listening sequence only. Y repeats the take, X skips to the next part. The results
    /// screens do not act on either: Y is unused there, and X cycles pictures only on a case
    /// screen, which is not open while the sequence plays.
    UI_BUTTON_LISTEN_REPEAT = 1u << 10,
    UI_BUTTON_LISTEN_SKIP = 1u << 11,
};

/// The buttons that act again while they are held: the four directions, and nothing that opens,
/// closes or leaves.
#define UI_BUTTONS_REPEATING \
    ((uint32_t)(UI_BUTTON_UP | UI_BUTTON_DOWN | UI_BUTTON_LEFT | UI_BUTTON_RIGHT))

/// How many frames of pad state the screen ignores before it will act on a press.
#define UI_SETTLING_FRAMES 8u

/// What one frame's buttons mean. One input per frame, and quitting ahead of everything, so a
/// mask with every button in it still leaves.
///
/// Nothing at all while the pad is still settling. A controller arriving with the window
/// reports its whole mask as newly pressed, and obeying those opened the screen on a view
/// nobody had chosen.
UiInput UiInputFromButtons(uint32_t buttons, uint32_t frame_index);

/// A held direction acts again after UI_REPEAT_DELAY_FRAMES and then every
/// UI_REPEAT_EVERY_FRAMES, which is what makes a list of a hundred cases walkable.
#define UI_REPEAT_DELAY_FRAMES 20u
#define UI_REPEAT_EVERY_FRAMES 4u

typedef struct {
    uint32_t held;
    uint32_t frames;
} UiRepeat;

/// The buttons to act on this frame: the newly pressed ones, plus a held direction when its
/// repeat comes round. Only a press counted after the settling frames starts a repeat, so a
/// mask the pad reports as held before anyone touched it never scrolls anything.
uint32_t UiRepeatButtons(UiRepeat *repeat, uint32_t held, uint32_t down, uint32_t frame_index);

/// Where the screens open: the overview, on the first group that failed if the run had one,
/// since that is what anyone starting it came to look at.
UiState UiStart(const UiRun *run);

/// The whole navigation, as one step.
///
/// Overview: up and down move between groups, left and right to the first group of the family
/// before or after, the failure jumps to the groups that failed, open enters the group.
/// Group: up and down move between its cases, left and right to the group before or after, the
/// failure jumps to its cases that failed, open opens the case, back returns to the overview.
/// Case: any direction moves to the case before or after in the group, the failure jumps as in
/// the group, the view cycles the pictures of a case that kept them, back returns to the group.
/// Quit leaves from anywhere. A group is entered, and a neighbour moved to, on its first failure.
UiState UiStep(UiState state, UiInput input, const UiRun *run);

/// Says a frame has been drawn, so the next step will not ask for another unless something
/// moves. Separate from drawing because the display owns the one and the navigation the other.
void UiFrameDrawn(UiState *state);

/// Draws the screen @p state is on over the whole frame. @p picture is the selected case's kept
/// picture at the case level, or NULL, which draws the case as its detail instead; the caller
/// looks it up because the pictures live in their own store.
void UiCompose(const UiFrame *frame, const UiRun *run, UiState state,
               const PictureRecord *picture);

/// The start screen: what a detailed run asks before it begins, when a case in it might end the
/// program. Two choices, each off unless the mode file's word turned it on: the may-crash table's
/// cases (the word `fatal`), and the crash list's cases again (the word `retry`). A choice that
/// can end the program shows a warning of what that costs while it is on.

/// The most cases each list names; the rest are counted.
#define UI_START_LIST_MAX 24u

typedef struct {
    /// The first line: the program and its build.
    const char *heading;
    /// The may-crash table's cases in the families this run has.
    uint32_t may_crash_count;
    const char *may_crash_suite[UI_START_LIST_MAX];
    const char *may_crash_name[UI_START_LIST_MAX];
    /// The crash list's cases.
    uint32_t listed_count;
    const char *listed_suite[UI_START_LIST_MAX];
    const char *listed_name[UI_START_LIST_MAX];
} UiStartInfo;

typedef struct {
    bool may_crash;
    bool retry;
    /// Begun, by a press or by the countdown.
    bool begun;
    /// Owes a frame, as UiState does.
    bool changed;
} UiStartState;

/// Whether there is anything to choose: a case that may crash, or one on the crash list.
bool UiStartShown(const UiStartInfo *info);

/// The screen as the mode file leaves it.
UiStartState UiStartOpen(bool may_crash, bool retry);

/// The start screen's buttons, named for what they do there.
enum {
    UI_START_BEGIN = 1u << 0,
    UI_START_TOGGLE_MAY_CRASH = 1u << 1,
    UI_START_TOGGLE_RETRY = 1u << 2,
};

/// One frame's presses. Beginning wins over a toggle in the same frame; a toggle for a list with
/// nothing in it does nothing; and nothing counts while the pad is still settling. Y and X in
/// the same edge change neither choice: a pad that connects late reports every button it is
/// already holding as one edge.
UiStartState UiStartStep(UiStartState state, uint32_t pressed, uint32_t frame_index,
                         const UiStartInfo *info);

/// How long the start screen waits for a person before it begins with what the file said: a
/// detailed run nobody attends must not wait for ever, and any press keeps the screen.
#define UI_START_NANOSECONDS 10000000000ull

bool UiStartTimesOut(uint64_t elapsed_ns, bool pressed);

/// Whole seconds left, rounded up; 0 once a press was seen or the time is up.
uint32_t UiStartSecondsLeft(uint64_t elapsed_ns, bool pressed);

/// Draws the start screen over the whole frame, its countdown included.
void UiComposeStart(const UiFrame *frame, const UiStartInfo *info, UiStartState state,
                    uint32_t seconds_left);

/// Where the warning is drawn, for the cases that check it is there exactly when it should be.
#define UI_START_WARNING_LINES 3u
uint32_t UiStartWarningTop(uint32_t frame_height);

/// How many rows the overview (UI_LEVEL_OVERVIEW) or a group (UI_LEVEL_GROUP) has room for on
/// a frame @p height tall.
uint32_t UiListRows(UiLevel level, uint32_t height);

/// The first row a list of @p total shows so that row @p selected is on screen: in the middle
/// where the list allows it, which needs no memory of where the list was before.
uint32_t UiListFirst(uint32_t selected, uint32_t total, uint32_t rows);

/// The overview row @p group sits on, counting the family headings above it.
uint32_t UiOverviewRow(const UiRun *run, uint32_t group);

/// How many lines @p text takes wrapped at @p columns, breaking on spaces where it can.
uint32_t UiWrappedLines(const char *text, uint32_t columns);

/// The four bytes of the pixel at @p x, @p y, or NULL when it is off the frame.
const uint8_t *UiPixel(const UiFrame *frame, uint32_t x, uint32_t y);

/// The listening sequence's screens, in the order a run with no button plays them. Not a level
/// of the results navigation: this screen is up while the report is still open, and it names
/// what is playing. It decides no verdict.
typedef enum {
    UI_LISTEN_OPENING = 0,
    UI_LISTEN_A_BEEP,
    UI_LISTEN_A1,
    UI_LISTEN_A2,
    UI_LISTEN_A3,
    UI_LISTEN_A4,
    UI_LISTEN_B_BEEP,
    UI_LISTEN_B_RECORD,
    UI_LISTEN_B_LEFT,
    UI_LISTEN_B_RIGHT,
    UI_LISTEN_C_BEEP,
    UI_LISTEN_C1,
    UI_LISTEN_C2,
    UI_LISTEN_C3,
    UI_LISTEN_C4,
    UI_LISTEN_CLOSING,
    UI_LISTEN_DONE,
} UiListenScreen;

/// What one press means during the sequence. Skip wins when both are down. Nothing until the
/// pad has settled, and nothing for a button the results screens use, so a press of one cannot
/// quit the sequence or move a list that is not on screen.
typedef enum {
    UI_LISTEN_NONE = 0,
    UI_LISTEN_REPEAT,
    UI_LISTEN_SKIP,
} UiListenInput;

UiListenInput UiListenInputFromButtons(uint32_t buttons, uint32_t frame_index);

/// The words on @p screen. @p guide receives the line when it names a recording's peaks;
/// otherwise the guide points at a fixed sentence and @p guide is left alone.
typedef struct {
    /// "a", "b" or "c", or NULL for the opening and the closing notes.
    const char *mark;
    const char *name;
    const char *take;
    const char *guide;
    const char *buttons;
} UiListenCopy;

void UiListenCopyOf(UiListenScreen screen, double left, double right, UiListenCopy *text,
                    char *guide, size_t guide_size);

/// The next screen when the one playing finishes on its own.
UiListenScreen UiListenNext(UiListenScreen screen);

/// Where X lands: the opening notes go on to part a, a part goes on to the next part, and the
/// closing notes end the sequence.
UiListenScreen UiListenSkip(UiListenScreen screen);

/// Draws @p screen over the whole frame, in the results screens' header, second line and
/// button line. @p left and @p right are the recording's peaks, read on part b's two echoes
/// and ignored on every other screen.
void UiComposeListen(const UiFrame *frame, UiListenScreen screen, double left, double right);

#ifdef __cplusplus
}
#endif
