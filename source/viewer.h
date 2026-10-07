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

#ifdef __cplusplus
}
#endif
