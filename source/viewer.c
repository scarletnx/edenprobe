// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

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

// Static, because the index is eight kilobytes of case numbers and built once.
static UiRun run;

static void ReadPad(PadState *pad) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(pad);
    padUpdate(pad);
}

RunMode ViewerModeFromFile(const char *mode_path) {
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
    return ModeFromFile(source, text, longer ? MODE_FILE_MAX : read, longer);
}

RunMode ViewerModeWithPad(RunMode from_file) {
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

/// Translates the console's own button bits into the ones the screens are written against. A
/// flat mapping and nothing else: what each one does, and when a press counts at all, is in ui.c
/// where this build can check it.
static uint32_t ButtonsFrom(uint64_t pressed) {
    static const struct {
        uint64_t console;
        uint32_t screen;
    } MAP[] = {
        { HidNpadButton_Plus | HidNpadButton_Minus, UI_BUTTON_QUIT },
        { HidNpadButton_B, UI_BUTTON_BACK },
        { HidNpadButton_A, UI_BUTTON_OPEN },
        { HidNpadButton_X, UI_BUTTON_CYCLE_VIEW },
        { HidNpadButton_R | HidNpadButton_ZR, UI_BUTTON_NEXT_FAILURE },
        { HidNpadButton_L | HidNpadButton_ZL, UI_BUTTON_PREVIOUS_FAILURE },
        { HidNpadButton_AnyUp, UI_BUTTON_UP },
        { HidNpadButton_AnyDown, UI_BUTTON_DOWN },
        { HidNpadButton_AnyLeft, UI_BUTTON_LEFT },
        { HidNpadButton_AnyRight, UI_BUTTON_RIGHT },
    };
    uint32_t buttons = 0;
    for (size_t i = 0; i < sizeof(MAP) / sizeof(MAP[0]); ++i) {
        if ((pressed & MAP[i].console) != 0u) {
            buttons |= MAP[i].screen;
        }
    }
    return buttons;
}

/// The kept picture of the case the cursor has open, or NULL, which draws the case as its detail.
static const PictureRecord *OpenPicture(UiState state) {
    const int32_t index = UiOpenPicture(&run, state);
    return index >= 0 ? PictureAt((size_t)index) : NULL;
}

ViewerEnd ViewerRun(void) {
    if (ReportIsOpen()) {
        return VIEWER_NO_SCREEN;
    }
    // What the run recorded, and nothing else: the cases as their lines went out, and the count
    // the report's own summary carries, so a store that ran out is said to have.
    size_t count = 0;
    const ReportRecord *const records = ReportRecords(&count);
    UiRunIndex(&run, records, (uint32_t)count,
               (uint32_t)(ReportPassed() + ReportFailed() + ReportSkipped() + ReportCrashed()));

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
    UiState state = UiStart(&run);
    UiRepeat repeat = { 0, 0 };

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
        const uint32_t buttons =
            UiRepeatButtons(&repeat, ButtonsFrom(padGetButtons(&pad)), ButtonsFrom(down),
                            frames_seen);
        const UiInput input = UiInputFromButtons(buttons, frames_seen);
        if (frames_seen < UI_SETTLING_FRAMES) {
            ++frames_seen;
        }
        const uint64_t elapsed = armTicksToNs(armGetSystemTick() - opened);
        if (UiLeavesUnattended(elapsed, pressed)) {
            end = VIEWER_UNATTENDED;
            break;
        }
        const uint32_t left = UiUnattendedSecondsLeft(elapsed, pressed);
        state = UiStep(state, input, &run);
        if (state.changed || left != shown_left) {
            uint32_t stride = 0;
            UiFrame frame = { (uint8_t *)framebufferBegin(&fb, &stride), SCREEN_WIDTH,
                              SCREEN_HEIGHT, stride };
            // The whole frame every time, the countdown included: there are two buffers, and
            // the one handed over holds whatever was drawn two frames ago.
            UiCompose(&frame, &run, state, OpenPicture(state));
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
