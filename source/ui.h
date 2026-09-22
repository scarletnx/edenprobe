// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "picture.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The detailed mode's screen, and the navigation behind it, with nothing console specific in
/// either, so the host build can compose a frame and step the navigation with no device.
///
/// Nothing here decides anything: it draws a verdict the program already reached and the pixels
/// that verdict came from. A screen is never an input to a result.

/// Somewhere to draw. @p pitch is the bytes between one row and the next, which is the display's
/// business and not always four times the width.
typedef struct {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} UiFrame;

/// What is on screen: the three pictures side by side, or one of them on its own.
typedef enum {
    UI_VIEW_ALL,
    UI_VIEW_GOT,
    UI_VIEW_EXPECTED,
    UI_VIEW_DIFF,
} UiView;

typedef struct {
    uint32_t index;
    UiView view;
    bool quit;
    /// Whether the screen owes a frame. Set by anything that moved, and stays set until a
    /// frame is actually drawn, which is what makes the very first one get drawn at all. The
    /// display clears it and sleeps otherwise, so a still screen costs an emulated processor
    /// nothing.
    bool changed;
} UiState;

typedef enum {
    UI_INPUT_NONE,
    UI_INPUT_NEXT,
    UI_INPUT_PREVIOUS,
    UI_INPUT_NEXT_FAILURE,
    UI_INPUT_PREVIOUS_FAILURE,
    UI_INPUT_CYCLE_VIEW,
    UI_INPUT_QUIT,
} UiInput;

/// What a run was asked to do, which is a word in a file and so belongs somewhere it can be
/// checked without one.
typedef struct {
    /// Present the cases on screen once every suite has finished.
    bool detailed;
    /// Write a picture for a passing case too, which is off by default because a green run
    /// would otherwise leave eighty files nobody asked for.
    bool every_case;
} UiMode;

/// Reads the words in @p text, whole words only and in any order, ignoring case.
UiMode UiModeFromText(const char *text);

/// Where the detailed mode opens: on the first failure if the run had one, since that is what
/// anyone starting it came to look at.
UiState UiStart(const uint8_t *failed, uint32_t count);

/// The whole navigation, as one step. @p failed is one byte per case, non-zero where it failed.
UiState UiStep(UiState state, UiInput input, const uint8_t *failed, uint32_t count);

/// Says a frame has been drawn, so the next step will not ask for another unless something
/// moves. Separate from drawing because the display owns the one and the navigation the other.
void UiFrameDrawn(UiState *state);

/// Draws one case over the whole frame.
void UiCompose(const UiFrame *frame, const PictureRecord *record, UiState state, uint32_t count,
               uint32_t failures);

/// Draws @p text at @p scale times the 8 by 8 cell. Exposed for the cases that check it.
void UiText(const UiFrame *frame, uint32_t x, uint32_t y, uint32_t scale,
            const uint8_t colour[4], const char *text);

/// The four bytes of the pixel at @p x, @p y, or NULL when it is off the frame.
const uint8_t *UiPixel(const UiFrame *frame, uint32_t x, uint32_t y);

#ifdef __cplusplus
}
#endif
