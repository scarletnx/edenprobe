// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "picture.h"
#include "report.h"
#include "ui.h"
#include "viewer.h"

// The only place in this suite where anything reaches a screen. The render targets stay
// offscreen; what is drawn here is a picture of one of them, after the fact.
#define SCREEN_WIDTH 1280u
#define SCREEN_HEIGHT 720u

// How long the pad is given to turn up before the launch button is given up on. A controller
// that is already attached answers within a frame or two; the rest of this is for one being
// plugged in as the program starts, and it is the whole of what an automatic run pays.
#define PAD_WAIT_FRAMES 12

// How many frames of pad state the viewer ignores before it will act on a press. Long enough
// for a controller arriving with the window to stop reporting its whole mask as new, short
// enough that nobody notices.
#define SETTLING_FRAMES 8u
#define FRAME_NANOSECONDS 16000000ull

// Sized off the store's own cap rather than off a number that could drift away from it, since
// the count below indexes this array.
static uint8_t failed[PICTURE_MAX_RECORDS];

static void ReadPad(PadState *pad) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(pad);
    padUpdate(pad);
}

UiMode ViewerModeAtLaunch(const char *mode_path) {
    char text[128] = { 0 };
    FILE *file = fopen(mode_path, "r");
    if (file != NULL) {
        const size_t read = fread(text, 1, sizeof(text) - 1u, file);
        text[read] = '\0';
        fclose(file);
    }
    UiMode mode = UiModeFromText(text);

    if (mode.detailed) {
        return mode;
    }

    // The other way in, for a person who has no way to put a file on the memory card. Held, not
    // pressed: the program is already running by the time anything can be pressed.
    PadState pad;
    ReadPad(&pad);
    for (int frame = 0; frame < PAD_WAIT_FRAMES && !padIsConnected(&pad); ++frame) {
        svcSleepThread(FRAME_NANOSECONDS);
        padUpdate(&pad);
    }
    if ((padGetButtons(&pad) & HidNpadButton_X) != 0u) {
        mode.detailed = true;
    }
    return mode;
}

static UiInput InputFrom(uint64_t pressed) {
    if ((pressed & (HidNpadButton_Plus | HidNpadButton_Minus)) != 0u) {
        return UI_INPUT_QUIT;
    }
    if ((pressed & HidNpadButton_X) != 0u) {
        return UI_INPUT_CYCLE_VIEW;
    }
    if ((pressed & HidNpadButton_AnyDown) != 0u) {
        return UI_INPUT_NEXT_FAILURE;
    }
    if ((pressed & HidNpadButton_AnyUp) != 0u) {
        return UI_INPUT_PREVIOUS_FAILURE;
    }
    if ((pressed & (HidNpadButton_A | HidNpadButton_AnyRight)) != 0u) {
        return UI_INPUT_NEXT;
    }
    if ((pressed & (HidNpadButton_B | HidNpadButton_AnyLeft)) != 0u) {
        return UI_INPUT_PREVIOUS;
    }
    return UI_INPUT_NONE;
}

void ViewerRun(void) {
    if (ReportIsOpen()) {
        return;
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
        return;
    }
    // Linear, because every frame here is composed by the processor and this is what has the
    // library do the swizzle on the way out rather than the program doing it by hand.
    if (R_FAILED(framebufferMakeLinear(&fb))) {
        framebufferClose(&fb);
        return;
    }

    PadState pad;
    ReadPad(&pad);
    UiState state = UiStart(failed, (uint32_t)count);

    // The first few frames of pad state are thrown away. A controller that turns up while the
    // loop is starting reports its whole button mask as newly pressed, and the screen answered
    // those before a person had touched anything.
    uint32_t settling = SETTLING_FRAMES;

    while (appletMainLoop() && !state.quit) {
        padUpdate(&pad);
        const uint64_t pressed = padGetButtonsDown(&pad);
        UiInput input = UI_INPUT_NONE;
        if (settling > 0u) {
            --settling;
        } else {
            input = InputFrom(pressed);
        }
        state = UiStep(state, input, failed, (uint32_t)count);
        if (state.changed) {
            uint32_t stride = 0;
            UiFrame frame = { (uint8_t *)framebufferBegin(&fb, &stride), SCREEN_WIDTH,
                              SCREEN_HEIGHT, stride };
            UiCompose(&frame, PictureAt(state.index), state, (uint32_t)count, failed,
                      (uint32_t)ReportFailed());
            framebufferEnd(&fb);
            UiFrameDrawn(&state);
        }
        svcSleepThread(FRAME_NANOSECONDS);
    }

    framebufferClose(&fb);
}
