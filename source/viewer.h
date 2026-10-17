// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mode.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The console half of the detailed mode: a window, a controller, and the loop between them.
/// Everything it draws and every decision it makes about what to draw next is in ui.c, which
/// the host build exercises; what is left here is the two the host has no equivalent of.

/// What the mode file on the memory card asks for. Read before the report opens, since whether
/// this launch goes on with a run or starts one depends on its words.
RunMode ViewerModeFromFile(const char *mode_path);

/// Adds what the pad says, which is asked only when the file said quick, since the button can
/// only add the detailed mode. After the report opens: a machine that cannot reach the pad still
/// leaves a report saying how far it got.
RunMode ViewerModeWithPad(RunMode from_file);

/// Shows the start screen until the run begins: A on begin the run, plus from any row, or nothing
/// pressed for UI_START_NANOSECONDS. Every row starts as @p from_file leaves it, and @p chosen
/// receives the screen as it was when the run began. False, with @p chosen as the file left it,
/// when there was no screen to open. Before the report has any case in it and while it is open,
/// since what it decides is the mode, like holding X; it reads no case and writes no line.
bool ViewerStartScreen(const UiStartInfo *info, const RunMode *from_file, UiStartState *chosen);

/// Shows the screen that asks whether to go on with the run the journal left in the middle,
/// until a choice is confirmed or nothing has been pressed for UI_RESUME_NANOSECONDS, which goes
/// on. Sets @p start_over. False, with @p start_over false, when there was no screen to open.
/// Before the report is opened, and the window stays open for the screens after it.
bool ViewerResumeScreen(const UiResumeInfo *info, bool *start_over);

/// How the screen ended.
typedef enum {
    /// Quit with plus or minus, or closed by the system.
    VIEWER_CLOSED,
    /// Nothing was pressed within UI_UNATTENDED_NANOSECONDS of the report closing.
    VIEWER_UNATTENDED,
    /// There was no screen to open, or the report was still open and it refused to.
    VIEWER_NO_SCREEN,
} ViewerEnd;

/// Presents the cases the report kept, group by group, and the pictures of the drawing cases,
/// until the viewer is quit, or until nobody has pressed anything for UI_UNATTENDED_NANOSECONDS,
/// which is how a forgotten mode file ends.
///
/// Does nothing while a report is still being written: the verdicts are decided, emitted and
/// closed before a pixel reaches a screen, so the two modes cannot report different bytes.
ViewerEnd ViewerRun(void);

/// The running screen (UiComposeProgress), from the run's first step to its last. It draws into
/// the program's one framebuffer, which is open when a screen came before the run; @p open opens
/// it when none did, which a detailed run asks for and a quick one from the mode file does not,
/// so a script's run draws nothing, as it always did. @p steps is how many the run has, @p quick
/// what follows it. Nothing happens when there is no screen.
void ViewerProgressBegin(const char *heading, const char *folder, uint32_t steps, bool quick,
                         bool open);

/// A step begins: its number among the run's steps, its name, and whether its suite is timed,
/// which makes ten passes. Draws at once.
void ViewerProgressStep(uint32_t step, const char *name, bool timed);

/// The run has ended: nothing more is drawn until the results, and the report tells nobody.
void ViewerProgressEnd(void);

/// Closes the program's framebuffer if a screen left it open, for a run that leaves with no
/// results screen. Safe when it is closed.
void ViewerEndScreen(void);

/// What the listening sequence was asked since the previous poll, or that a voice did not end.
/// The viewer itself only returns a button or CLOSED. FAIL is the sequence's own: a step failed,
/// or a clip did not finish in time.
typedef enum {
    VIEWER_LISTEN_NONE = 0,
    VIEWER_LISTEN_REPEAT,
    VIEWER_LISTEN_SKIP,
    /// The system is closing the program. The sequence stops and the cases still run.
    VIEWER_LISTEN_CLOSED,
    VIEWER_LISTEN_FAIL,
} ViewerListenAction;

/// Draws @p screen if it is not already up, into the program's one framebuffer, which the first
/// call opens unless the start screen already did; a machine with no screen leaves the sequence
/// to play as it did with a black window.
void ViewerListenShow(UiListenScreen screen, double left, double right);

/// One look at the pad. The first UI_SETTLING_FRAMES calls are not a press: a controller
/// arriving with the window reports its whole mask as newly pressed. Y repeats, X skips.
/// Nothing else is read, and a button already held (X, from asking for the detailed mode at
/// launch) is not a new press.
ViewerListenAction ViewerListenPoll(void);

/// Ends the listening screen, and closes the framebuffer only when the sequence opened it: the
/// start screen's stays open for the results. The sequence calls it after the cases, so the last
/// frame stays up while they run. Safe when the screen never opened.
void ViewerListenClose(void);

/// Draws the pad family's screen into the program's one framebuffer, which the first call opens
/// unless a screen before it did. False when there is no screen: the suite then runs its prompts
/// with nothing to show them, and a person has only the log.
bool ViewerPadShow(const UiPadScreen *screen);

/// Activates the touch screen for the program, the first time only; the screens and the touch
/// suite both read it. False when the machine refused, with the result it gave in @p result (may
/// be NULL): libnx would end the program there, and is not let to, so a machine with no touch
/// screen still gets its run with the buttons alone.
bool ViewerTouchInit(uint32_t *result);

/// Draws the touch suite's screen the same way, into the same framebuffer; ViewerPadClose ends
/// it too.
bool ViewerTouchShow(const UiTouchScreen *screen);

/// Ends the pad screen, and closes the framebuffer only when the first ViewerPadShow opened it.
/// The running screen keeps off the framebuffer while the pad screen is up. Safe when it never
/// opened.
void ViewerPadClose(void);

/// Starts listening for a yes or no tap on the pad screen's bar: a finger already on the screen
/// counts only once it is lifted and put down again.
void ViewerPadTapBegin(void);

/// One look at the touch screen while a question waits: the answer a tap on the bar has just
/// given, or none.
PadAnswer ViewerPadTap(void);

#ifdef __cplusplus
}
#endif
