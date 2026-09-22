// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

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
    if (UiModeFromText(text).detailed) {
        // Already answered, and asking the pad would only cost the run the wait below.
        return UiModeAtLaunch(text, false);
    }

    // The other way in, for a person who has no way to put a file on the memory card. Held, not
    // pressed: the program is already running by the time anything can be pressed.
    PadState pad;
    ReadPad(&pad);
    for (uint32_t frame = 0; UiWaitsForPad(frame, padIsConnected(&pad)); ++frame) {
        svcSleepThread(FRAME_NANOSECONDS);
        padUpdate(&pad);
    }
    return UiModeAtLaunch(text, (padGetButtons(&pad) & HidNpadButton_X) != 0u);
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

    uint32_t frames_seen = 0;
    while (appletMainLoop() && !state.quit) {
        padUpdate(&pad);
        const UiInput input =
            UiInputFromButtons(ButtonsFrom(padGetButtonsDown(&pad)), frames_seen);
        if (frames_seen < UI_SETTLING_FRAMES) {
            ++frames_seen;
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
