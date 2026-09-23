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

/// What this run was asked to do, from the file on the memory card and from the pad. The pad
/// is asked only when the file said quick, since the button can only add the detailed mode.
RunMode ViewerModeAtLaunch(const char *mode_path);

/// How the screen ended.
typedef enum {
    /// Quit with plus or minus, or closed by the system.
    VIEWER_CLOSED,
    /// Nothing was pressed within UI_UNATTENDED_NANOSECONDS of the report closing.
    VIEWER_UNATTENDED,
    /// There was no screen to open, or the report was still open and it refused to.
    VIEWER_NO_SCREEN,
} ViewerEnd;

/// Presents the cases kept in memory until the viewer is quit, or until nobody has pressed
/// anything for UI_UNATTENDED_NANOSECONDS, which is how a forgotten mode file ends.
///
/// Does nothing while a report is still being written: the verdicts are decided, emitted and
/// closed before a pixel reaches a screen, so the two modes cannot report different bytes.
ViewerEnd ViewerRun(void);

#ifdef __cplusplus
}
#endif
