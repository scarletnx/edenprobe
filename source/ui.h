// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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

/// Where the pictures start down the frame, under the two lines of heading and the labels.
/// Named here rather than buried in the layout because the cases that check the layout have to
/// look in the same place it drew.
#define UI_PANEL_TOP 72u

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

/// What a launch asks for, from the words in the mode file and the buttons held at the time.
/// The button can only add the detailed mode, never take it away.
UiMode UiModeAtLaunch(const char *text, bool cycle_button_held);

/// How many frames the pad is given to turn up before the launch button is given up on.
#define UI_PAD_WAIT_FRAMES 12u

/// Whether to keep waiting for a controller to appear. It stops the moment one answers, so a
/// machine that already has one pays two frames and a machine with none pays the ceiling.
bool UiWaitsForPad(uint32_t frame_index, bool connected);

/// The buttons the screen knows about. Named here rather than taken from the console's own
/// header so that what each one does is a decision this build can check.
enum {
    UI_BUTTON_NEXT = 1u << 0,
    UI_BUTTON_PREVIOUS = 1u << 1,
    UI_BUTTON_NEXT_FAILURE = 1u << 2,
    UI_BUTTON_PREVIOUS_FAILURE = 1u << 3,
    UI_BUTTON_CYCLE_VIEW = 1u << 4,
    UI_BUTTON_QUIT = 1u << 5,
};

/// How many frames of pad state the screen ignores before it will act on a press.
#define UI_SETTLING_FRAMES 8u

/// What one frame's freshly pressed buttons mean.
///
/// Nothing at all while the pad is still settling. A controller arriving with the window
/// reports its whole mask as newly pressed, and obeying those opened the screen on a view
/// nobody had chosen.
UiInput UiInputFromButtons(uint32_t buttons, uint32_t frame_index);

/// Where the detailed mode opens: on the first failure if the run had one, since that is what
/// anyone starting it came to look at.
UiState UiStart(const uint8_t *failed, uint32_t count);

/// The whole navigation, as one step. @p failed is one byte per case, non-zero where it failed.
UiState UiStep(UiState state, UiInput input, const uint8_t *failed, uint32_t count);

/// Says a frame has been drawn, so the next step will not ask for another unless something
/// moves. Separate from drawing because the display owns the one and the navigation the other.
void UiFrameDrawn(UiState *state);

/// Draws one case over the whole frame. @p failed is the same array the navigation walks, so
/// the header can say where this case sits among the failures rather than only among the cases.
/// @p run_failures is what the whole run reported, which can be more than this list holds: a
/// case that failed before it ever read a target back has no picture and is not here.
void UiCompose(const UiFrame *frame, const PictureRecord *record, UiState state, uint32_t count,
               const uint8_t *failed, uint32_t run_failures);


/// The four bytes of the pixel at @p x, @p y, or NULL when it is off the frame.
const uint8_t *UiPixel(const UiFrame *frame, uint32_t x, uint32_t y);

#ifdef __cplusplus
}
#endif
