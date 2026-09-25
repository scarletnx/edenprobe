// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "mode.h"
#include "picture.h"
#include "report.h"
#include "ui.h"
#include "viewer.h"

// The pad and the framebuffer are driven the way switchbrew's switch-examples drive them:
// hid/read-controls for padConfigureInput, padInitializeDefault, padUpdate, padGetButtonsDown
// and padGetButtons, graphics/simplegfx for a linear double buffered framebuffer and the loop
// around it. https://github.com/switchbrew/switch-examples, marked public domain (Creative
// Commons Public Domain Mark 1.0, in its README). The library calls are libnx's, ISC licence.

// The only place in this suite where anything reaches a screen. The render targets stay
// offscreen; what is drawn here is a picture of one of them, after the fact.
#define SCREEN_WIDTH 1280u
#define SCREEN_HEIGHT 720u

#define FRAME_NANOSECONDS 16000000ull

// Sized off the store's own cap rather than off a number that could drift away from it, since
// the count below indexes this array.
static uint8_t failed[PICTURE_MAX_RECORDS];

static void ReadPad(PadState *pad) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(pad);
    padUpdate(pad);
}

RunMode ViewerModeAtLaunch(const char *mode_path) {
    // One byte past the limit, which is how a longer file is told apart from one exactly as
    // long, and which says whether the last word read runs on. Static, because it is four
    // kilobytes and read once.
    static char text[MODE_FILE_MAX + 2u];
    ModeSource source = MODE_SOURCE_NONE;
    size_t read = 0;
    struct stat about;
    if (stat(mode_path, &about) == 0 && S_ISDIR(about.st_mode)) {
        source = MODE_SOURCE_DIRECTORY;
    } else {
        FILE *file = fopen(mode_path, "r");
        if (file != NULL) {
            source = MODE_SOURCE_FILE;
            read = fread(text, 1, MODE_FILE_MAX + 1u, file);
            fclose(file);
        }
    }
    text[read] = '\0';
    const bool longer = read > MODE_FILE_MAX;
    const RunMode from_file = ModeFromFile(source, text, longer ? MODE_FILE_MAX : read, longer);
    if (!ModeAsksPad(&from_file)) {
        // Already detailed, and the button could only have asked for that again.
        return from_file;
    }

    // The other way in, for a person with a card that says quick. Held, not pressed: the
    // program is already running by the time anything can be pressed.
    PadState pad;
    ReadPad(&pad);
    for (uint32_t frame = 0; UiWaitsForPad(frame, padIsConnected(&pad)); ++frame) {
        svcSleepThread(FRAME_NANOSECONDS);
        padUpdate(&pad);
    }
    return ModeWithButton(from_file, (padGetButtons(&pad) & HidNpadButton_X) != 0u);
}

/// Translates the console's own button bits into the ones the screen is written against. A flat
/// mapping and nothing else: what each one does, and when a press counts at all, is in ui.c
/// where this build can check it.
static uint32_t ButtonsFrom(uint64_t pressed) {
    uint32_t buttons = 0;
    if ((pressed & (HidNpadButton_Plus | HidNpadButton_Minus)) != 0u) {
        buttons |= UI_BUTTON_QUIT;
    }
    if ((pressed & HidNpadButton_X) != 0u) {
        buttons |= UI_BUTTON_CYCLE_VIEW;
    }
    if ((pressed & HidNpadButton_AnyDown) != 0u) {
        buttons |= UI_BUTTON_NEXT_FAILURE;
    }
    if ((pressed & HidNpadButton_AnyUp) != 0u) {
        buttons |= UI_BUTTON_PREVIOUS_FAILURE;
    }
    if ((pressed & (HidNpadButton_A | HidNpadButton_AnyRight)) != 0u) {
        buttons |= UI_BUTTON_NEXT;
    }
    if ((pressed & (HidNpadButton_B | HidNpadButton_AnyLeft)) != 0u) {
        buttons |= UI_BUTTON_PREVIOUS;
    }
    return buttons;
}

ViewerEnd ViewerRun(void) {
    if (ReportIsOpen()) {
        return VIEWER_NO_SCREEN;
    }
    size_t count = PictureCount();
    if (count > sizeof(failed)) {
        count = sizeof(failed);
    }
    for (size_t i = 0; i < count; ++i) {
        const PictureRecord *const record = PictureAt(i);
        failed[i] = record != NULL && !record->passed ? 1u : 0u;
    }

    Framebuffer fb;
    if (R_FAILED(framebufferCreate(&fb, nwindowGetDefault(), SCREEN_WIDTH, SCREEN_HEIGHT,
                                   PIXEL_FORMAT_RGBA_8888, 2))) {
        return VIEWER_NO_SCREEN;
    }
    // Linear, because every frame here is composed by the processor and this is what has the
    // library do the swizzle on the way out rather than the program doing it by hand.
    if (R_FAILED(framebufferMakeLinear(&fb))) {
        framebufferClose(&fb);
        return VIEWER_NO_SCREEN;
    }

    PadState pad;
    ReadPad(&pad);
    UiState state = UiStart(failed, (uint32_t)count);

    // Counted from here, which is the moment after the report closed. A clock rather than a
    // count of frames, because a frame here is a sleep plus however long composing took.
    const uint64_t opened = armGetSystemTick();
    bool pressed = false;
    uint32_t shown_left = UINT32_MAX;
    ViewerEnd end = VIEWER_CLOSED;

    uint32_t frames_seen = 0;
    while (appletMainLoop() && !state.quit) {
        padUpdate(&pad);
        const uint64_t down = padGetButtonsDown(&pad);
        // The same settling the navigation keeps: a controller arriving with the window reports
        // its whole mask as newly pressed, and that is not a person.
        if (frames_seen >= UI_SETTLING_FRAMES && down != 0u) {
            pressed = true;
        }
        const UiInput input = UiInputFromButtons(ButtonsFrom(down), frames_seen);
        if (frames_seen < UI_SETTLING_FRAMES) {
            ++frames_seen;
        }
        const uint64_t elapsed = armTicksToNs(armGetSystemTick() - opened);
        if (UiLeavesUnattended(elapsed, pressed)) {
            end = VIEWER_UNATTENDED;
            break;
        }
        const uint32_t left = UiUnattendedSecondsLeft(elapsed, pressed);
        state = UiStep(state, input, failed, (uint32_t)count);
        if (state.changed || left != shown_left) {
            uint32_t stride = 0;
            UiFrame frame = { (uint8_t *)framebufferBegin(&fb, &stride), SCREEN_WIDTH,
                              SCREEN_HEIGHT, stride };
            // The whole frame every time, the countdown included: there are two buffers, and
            // the one handed over holds whatever was drawn two frames ago.
            UiCompose(&frame, PictureAt(state.index), state, (uint32_t)count, failed,
                      (uint32_t)ReportFailed());
            UiComposeCountdown(&frame, left);
            framebufferEnd(&fb);
            UiFrameDrawn(&state);
            shown_left = left;
        }
        svcSleepThread(FRAME_NANOSECONDS);
    }

    framebufferClose(&fb);
    return end;
}
