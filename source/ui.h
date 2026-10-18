// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mode.h"
#include "padref.h"
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
    UI_FAMILY_PAD,
    UI_FAMILY_GRAPHICS,
    UI_FAMILY_FS,
    UI_FAMILY_KERNEL,
    UI_FAMILY_DISPLAY,
    UI_FAMILY_VIDEO,
    UI_FAMILY_AUDIO,
    UI_FAMILY_SERVICE,
    UI_FAMILY_NET,
    UI_FAMILY_OTHER,
    UI_FAMILY_COUNT,
} UiFamily;

UiFamily UiFamilyOf(const char *suite);
const char *UiFamilyName(UiFamily family);

/// The most groups and cases the screens index. A run has up to 66 suites and about eight
/// hundred cases, and the groups leave room for the families still growing; what does not fit
/// is counted and the overview says so. Under 255, which marks a case with no group while the
/// index is built.
#define UI_MAX_GROUPS 96u
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

/// Touch, on every screen the program draws for a person. A tap is one finger put down after the
/// screen has settled and lifted again, and it counts when it is lifted, only if the place it
/// went down and the place it was lifted are the same row or the same box. A tap on a row moves
/// the cursor there; a tap on the row the cursor is on does what A does there. The boxes of the
/// touch bar, at the bottom right, do what their buttons do. The buttons are unchanged. Every
/// decision is here, where the host build checks it; the display only reads the touch screen.

/// A rectangle of the frame.
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} UiRect;

/// Where a tap went down and where it was last seen before it was lifted.
typedef struct {
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;
} UiTap;

/// The finger being followed, from one frame to the next.
typedef struct {
    /// Something was on the screen last frame.
    bool down;
    /// It is the start of a tap: one finger, down after the settling frames.
    bool armed;
    UiTap tap;
} UiTouch;

typedef enum {
    UI_TOUCH_NONE,
    /// A finger that can be a tap has just gone down, which keeps a screen as a press does.
    UI_TOUCH_BEGAN,
    /// A tap has ended; it is in the step's @p tap.
    UI_TOUCH_TAP,
} UiTouchEvent;

/// Starts following as if a finger were already down that is not a tap: one on the screen when
/// a screen opens, or when a question is put, counts only once it is lifted and put down again.
void UiTouchReset(UiTouch *touch);

/// One frame of the touch screen: @p count fingers on it, the first at @p x, @p y. Nothing while
/// the screen settles, as for the buttons; a second finger makes the touch not a tap.
UiTouchEvent UiTouchStep(UiTouch *touch, uint32_t count, uint32_t x, uint32_t y,
                         uint32_t frame_index, UiTap *tap);

/// The touch bar: up to UI_TOUCH_BOXES boxes along the bottom right of the frame, over the foot.
#define UI_TOUCH_BOX_WIDTH 88u
#define UI_TOUCH_BOX_HEIGHT 28u
#define UI_TOUCH_BOX_GAP 8u
#define UI_TOUCH_BOXES 4u
#define UI_TOUCH_BAR_WIDTH                                                                         \
    (UI_TOUCH_BOXES * UI_TOUCH_BOX_WIDTH + (UI_TOUCH_BOXES - 1u) * UI_TOUCH_BOX_GAP)

/// Box @p index, from the left, of a bar of @p count; all zeros for a frame the screens do not
/// lay out on, or a box that is not there.
UiRect UiTouchBox(uint32_t frame_width, uint32_t frame_height, uint32_t count, uint32_t index);

/// Where the countdown and the button line stop, so the widest bar never covers them.
uint32_t UiFootRight(uint32_t frame_width);

/// The results screens' touch: rows as above; the bar holds quit on the overview, back on a
/// group, and the case before, the case after, the view (when the case kept pictures) and back on
/// a case.
UiState UiTapStep(UiState state, const UiTap *tap, const UiRun *run, uint32_t frame_width,
                  uint32_t frame_height);

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

/// The start screen: what a fresh detailed run shows before it begins, and the menu that chooses
/// what runs. A cursor walks its rows: begin the run, where it starts; the families the screen
/// offers (graphics, fs, audio, net) and the listening sequence; quick and notime; the may-crash
/// table's cases (the word `fatal`) and the crash list's cases again (the word `retry`); and
/// whether to write the choice into the mode file. Every row starts as the mode file leaves it.
/// A choice that can end the program shows a warning of what that costs while it is on. The judge
/// and the cpu suite run whatever is chosen.

/// The most cases each list names; the rest are counted.
#define UI_START_LIST_MAX 24u

/// The families the screen offers a row each; cpu always runs, so it has none.
#define UI_START_FAMILIES                                                                          \
    ((uint32_t)(MODE_FAMILY_GRAPHICS | MODE_FAMILY_FS | MODE_FAMILY_AUDIO | MODE_FAMILY_NET |      \
                MODE_FAMILY_PAD))

typedef struct {
    /// The first line: the program and its build.
    const char *heading;
    /// The families this build has; a row for one it lacks cannot be turned on.
    uint32_t families_present;
    /// The may-crash table's cases, each with its family: the screen counts and lists those in
    /// the families chosen, and those of the cpu suite, which always runs.
    uint32_t may_crash_count;
    const char *may_crash_suite[UI_START_LIST_MAX];
    const char *may_crash_name[UI_START_LIST_MAX];
    uint32_t may_crash_family[UI_START_LIST_MAX];
    /// The crash list's cases.
    uint32_t listed_count;
    const char *listed_suite[UI_START_LIST_MAX];
    const char *listed_name[UI_START_LIST_MAX];
} UiStartInfo;

/// The menu's rows, top to bottom.
typedef enum {
    UI_START_ROW_BEGIN = 0,
    UI_START_ROW_GRAPHICS,
    UI_START_ROW_FS,
    UI_START_ROW_AUDIO,
    UI_START_ROW_LISTEN,
    UI_START_ROW_NET,
    UI_START_ROW_PAD,
    UI_START_ROW_QUICK,
    UI_START_ROW_NOTIME,
    UI_START_ROW_MAY_CRASH,
    UI_START_ROW_RETRY,
    UI_START_ROW_SAVE,
    UI_START_ROWS,
} UiStartRow;

typedef struct {
    bool may_crash;
    bool retry;
    /// Begun, by a press or by the countdown.
    bool begun;
    /// Owes a frame, as UiState does.
    bool changed;
    /// The row the cursor is on.
    uint32_t cursor;
    /// The families of UI_START_FAMILIES that run; audio is on whenever listen is.
    uint32_t families;
    bool listen;
    /// Leave once the report closes, with no results screens.
    bool quick;
    bool no_timing;
    /// Write the choice into the mode file.
    bool save;
} UiStartState;

/// Whether there is anything to choose: a family, a case that may crash, or one on the crash
/// list.
bool UiStartShown(const UiStartInfo *info);

/// The screen as the mode file leaves it, the cursor on begin the run.
UiStartState UiStartOpen(const RunMode *from_file, uint32_t present);

/// How many of the may-crash table's cases the run as chosen would reach.
uint32_t UiStartMayCrashCount(const UiStartInfo *info, const UiStartState *state);

/// The mode the run begins with: @p from_file with what the screen chose. A family set the
/// screen did not change keeps the file's words for it; a changed one is named whole, cpu
/// included, so the words the journal keeps are never empty. The label and the words the screen
/// has no row for stay the file's.
RunMode UiStartMode(const UiStartState *state, RunMode from_file, uint32_t present);

/// The start screen's buttons, named for what they do there.
enum {
    /// Plus or minus: begins the run from any row.
    UI_START_BEGIN = 1u << 0,
    /// Y and X: the may-crash and crash list choices from any row.
    UI_START_TOGGLE_MAY_CRASH = 1u << 1,
    UI_START_TOGGLE_RETRY = 1u << 2,
    /// A: the row under the cursor, which on begin the run begins it.
    UI_START_SELECT = 1u << 3,
    UI_START_UP = 1u << 4,
    UI_START_DOWN = 1u << 5,
};

/// One frame's presses. Beginning wins over everything else in the same frame; a toggle for a
/// list with nothing in it does nothing; and nothing counts while the pad is still settling. Y
/// and X in the same edge change neither choice, and A in an edge that also moves the cursor
/// changes nothing: a pad that connects late reports every button it is already holding as one
/// edge. Turning listen on turns audio on, and turning audio off turns listen off.
UiStartState UiStartStep(UiStartState state, uint32_t pressed, uint32_t frame_index,
                         const UiStartInfo *info);

/// One tap: a row, then the same row again to do what A does there; the bar's one box begins.
UiStartState UiStartTap(UiStartState state, const UiTap *tap, uint32_t frame_width,
                        uint32_t frame_height, const UiStartInfo *info);

/// How long the start screen waits for a person before it begins with what the file said: a
/// detailed run nobody attends must not wait for ever, and any press keeps the screen.
#define UI_START_NANOSECONDS 10000000000ull

bool UiStartTimesOut(uint64_t elapsed_ns, bool pressed);

/// Whole seconds left, rounded up; 0 once a press was seen or the time is up.
uint32_t UiStartSecondsLeft(uint64_t elapsed_ns, bool pressed);

/// Where the menu's row @p row starts down the frame, for the cases that check the cursor.
uint32_t UiStartRowTop(uint32_t row);

/// Draws the start screen over the whole frame, its countdown included.
void UiComposeStart(const UiFrame *frame, const UiStartInfo *info, UiStartState state,
                    uint32_t seconds_left);

/// Where the warning is drawn, for the cases that check it is there exactly when it should be.
#define UI_START_WARNING_LINES 3u
uint32_t UiStartWarningTop(uint32_t frame_height);

/// The screen a detailed launch shows when its journal left a run in the middle, before anything
/// is opened: where the program stopped, how far the run had got, and two choices, to go on from
/// there or to start over. Nothing pressed for UI_RESUME_NANOSECONDS goes on, as a launch always
/// did; a quick run never shows it.
typedef struct {
    /// The first line: the program and its build.
    const char *heading;
    /// Where the program stopped, a sentence (JournalDescribeStop).
    const char *where;
    /// The run's folder.
    const char *folder;
    /// This launch's number in the run, how many steps the run has and how many are done.
    uint32_t launch;
    uint32_t steps;
    uint32_t steps_done;
    /// The counts the report holds where the run would go on from.
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
    uint32_t crashed;
} UiResumeInfo;

/// The two choices, in the order the screen lists them.
enum {
    UI_RESUME_GO_ON = 0,
    UI_RESUME_START_OVER = 1,
    UI_RESUME_CHOICES = 2,
};

typedef struct {
    /// The row the cursor is on.
    uint32_t cursor;
    /// A press chose the row under the cursor.
    bool decided;
    /// What was decided: start over rather than go on.
    bool start_over;
    /// Owes a frame, as UiState does.
    bool changed;
} UiResumeState;

/// The screen's buttons, named for what they do there.
enum {
    UI_RESUME_UP = 1u << 0,
    UI_RESUME_DOWN = 1u << 1,
    UI_RESUME_CHOOSE = 1u << 2,
};

/// The cursor on going on, which is what a launch did before it asked.
UiResumeState UiResumeOpen(void);

/// One frame's presses. Nothing while the pad settles; up and down move the cursor, coming
/// round; choose takes the row under it. A choice in the same edge as a move does nothing: a pad
/// that connects late reports every button it holds as one edge, and that is nobody choosing.
UiResumeState UiResumeStep(UiResumeState state, uint32_t pressed, uint32_t frame_index);

/// One tap: a choice, then the same choice again to take it.
UiResumeState UiResumeTap(UiResumeState state, const UiTap *tap, uint32_t frame_width,
                          uint32_t frame_height);

/// How long the screen waits for a person before it goes on with the run.
#define UI_RESUME_NANOSECONDS 10000000000ull

bool UiResumeTimesOut(uint64_t elapsed_ns, bool pressed);

/// Whole seconds left, rounded up; 0 once a press was seen or the time is up.
uint32_t UiResumeSecondsLeft(uint64_t elapsed_ns, bool pressed);

/// Where the row of choice @p row starts down the frame, for the cases that check the cursor.
uint32_t UiResumeRowTop(uint32_t row);

/// Draws the screen over the whole frame, its countdown included.
void UiComposeResume(const UiFrame *frame, const UiResumeInfo *info, UiResumeState state,
                     uint32_t seconds_left);

/// The running screen: what a run that has a screen shows while its suites run, so a person can
/// tell a long run from a stuck one. It is drawn between cases, when the report says one started
/// or ended, never inside a timed interval, and at most once every UI_PROGRESS_NANOSECONDS but
/// when a step begins. Like every screen here, it decides nothing.
typedef struct {
    /// The first line: the program and its build.
    const char *heading;
    /// The run's folder.
    const char *folder;
    /// The step under way, counted from one among the steps this run has, and its name.
    uint32_t step;
    uint32_t steps;
    const char *step_name;
    /// The pass of a timed step, counted from one with the warm-up first, and how many it makes;
    /// both nought for a step that is not timed.
    uint32_t pass;
    uint32_t passes;
    /// The case under way, or the last that ended, with the suite its line names; NULL before
    /// the step's first.
    const char *suite;
    const char *name;
    /// Its number among the cases this pass of the step has met, and how many the pass before
    /// met, which is how many this one will: nought when there was no pass before.
    uint32_t case_number;
    uint32_t case_total;
    /// The report's counts so far.
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
    uint32_t crashed;
    /// Since this launch began the run's first step.
    uint64_t elapsed_ns;
    /// What comes after the run: the program leaves, or the results come up.
    bool quick;
} UiProgress;

/// How often, at most, the running screen is drawn between two steps' first frames.
#define UI_PROGRESS_NANOSECONDS 500000000ull

/// Whether to draw now: the first time, when a step begins, and otherwise once
/// UI_PROGRESS_NANOSECONDS have passed since the last frame. @p now_ns and @p drawn_ns are
/// readings of one clock.
bool UiProgressDue(bool drawn_before, uint64_t drawn_ns, uint64_t now_ns, bool step_began);

/// The cases one step has met, pass by pass, from the report's starts and ends: a case that
/// starts counts, and so does one that ends without having started, as the judge's do.
typedef struct {
    uint32_t pass;
    uint32_t count;
    /// What the pass before counted, nought when there was none.
    uint32_t previous;
    bool open;
} UiProgressCount;

/// A new step: nothing met.
void UiProgressCountReset(UiProgressCount *count);

/// One start or end, in pass @p pass (PerfPassNumber, nought outside a timed suite).
void UiProgressCountCase(UiProgressCount *count, uint32_t pass, bool started);

/// "pass 3 of 10: a rehearsal" and the like, or an empty string for a step that is not timed.
size_t UiProgressPassWords(char *out, size_t out_size, uint32_t pass, uint32_t passes);

/// A length of time as the screen says it: "45 s", "1 min 23 s", "2 h 5 min".
size_t UiElapsedWords(char *out, size_t out_size, uint64_t ns);

/// Where the bar of steps is drawn, for the cases that check how much of it is filled.
#define UI_PROGRESS_BAR_TOP (UI_LIST_TOP + UI_ROW_HEIGHT + 6u)
#define UI_PROGRESS_BAR_HEIGHT 8u

/// Draws the running screen over the whole frame.
void UiComposeProgress(const UiFrame *frame, const UiProgress *progress);

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

/// Draws @p screen over the whole frame in the running screen's layout, which the sequence
/// comes between: the part on the top row, the step of the sequence with its bar of the steps
/// done, what to listen for below it, the buttons at the foot. @p left and @p right are the
/// recording's peaks, read on part b's two echoes and ignored on every other screen.
void UiComposeListen(const UiFrame *frame, UiListenScreen screen, double left, double right);

/// The pad family's screen, up while its prompts run: the prompt large, what to do under it, and
/// what the controller sends right now, so a person sees a press arrive. Like the listening
/// screen it comes while the report is open, and it decides no verdict: the suite judges from
/// the bits it read, never from what was drawn.
typedef struct {
    const char *heading;
    /// The prompt's number, from 1, and how many there are.
    uint32_t prompt;
    uint32_t prompts;
    /// The prompt large, and the sentence under it.
    const char *big;
    const char *how;
    /// Where the prompt is: waiting with the seconds left, measuring, done.
    const char *state;
    /// The buttons held now, in the service's bits, and both sticks.
    uint64_t held;
    int32_t left_x;
    int32_t left_y;
    int32_t right_x;
    int32_t right_y;
    /// The controllers connected, as their ids and styles.
    const char *controllers;
    /// The prompt before, what it asked and what came of it; NULL before the first.
    const char *last;
    ReportResult last_result;
    /// A yes or no question waits: the touch bar holds yes and no.
    bool asks_yes_no;
} UiPadScreen;

/// How many times the cell the prompt is drawn at, when it fits across the frame.
#define UI_PAD_BIG 12u

void UiComposePad(const UiFrame *frame, const UiPadScreen *screen);

/// The answer a tap on the pad screen's bar gives while a question waits, or none.
PadAnswer UiPadTap(const UiTap *tap, uint32_t frame_width, uint32_t frame_height,
                   bool asks_yes_no);

/// The touch suite's screen: the prompt, the marks to aim at, and a square where each finger on
/// the screen is now, so a person sees where the machine puts their touch. It decides no verdict:
/// the suite judges from the states it read.
typedef struct {
    const char *heading;
    /// The prompt's number, from 1, and how many there are.
    uint32_t prompt;
    uint32_t prompts;
    const char *big;
    const char *how;
    /// Where the prompt is: waiting with the seconds left, done.
    const char *state;
    /// The marks: none, a target, or the drag's two ends, at frame pixels.
    uint32_t marks;
    uint32_t mark_x[2];
    uint32_t mark_y[2];
    /// The fingers on the screen now, at most four drawn.
    uint32_t touches;
    uint32_t touch_x[4];
    uint32_t touch_y[4];
    /// What arrives now, as a sentence.
    const char *now;
    /// The prompt before, what it asked and what came of it; NULL before the first.
    const char *last;
    ReportResult last_result;
} UiTouchScreen;

/// A mark's ring, around its middle.
#define UI_TOUCH_MARK_RADIUS 28u

/// Where the prompt's words start down the frame and where its live lines do: the words sit
/// between the top targets and the middle one, the live lines between the middle one and the
/// drag's row, so nothing written covers a mark.
#define UI_TOUCH_WORDS_TOP 150u
#define UI_TOUCH_LIVE_TOP 420u

void UiComposeTouch(const UiFrame *frame, const UiTouchScreen *screen);

#ifdef __cplusplus
}
#endif
