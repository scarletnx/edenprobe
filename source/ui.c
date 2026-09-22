// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "font8x8.h"
#include "ui.h"

// The margin all four sides keep, and the gap between two panels. One number, because the
// layout below divides what is left by three and a gap that differed from the margin would put
// the middle panel off centre.
#define MARGIN 32u

// The cell a glyph advances by. The widest glyph is six columns, so a full eight leaves two
// clear, which is what keeps 8 by 8 text readable when it is scaled up by nearest neighbour.
#define CELL 8u

static const uint8_t BACKGROUND[4] = { 0x10, 0x12, 0x18, 0xFF };
static const uint8_t TEXT[4] = { 0xE0, 0xE4, 0xEC, 0xFF };
static const uint8_t DIM[4] = { 0x88, 0x90, 0xA0, 0xFF };
static const uint8_t PASSED[4] = { 0x40, 0xD0, 0x60, 0xFF };
static const uint8_t FAILED[4] = { 0xF0, 0x50, 0x50, 0xFF };
static const uint8_t BORDER[4] = { 0x50, 0x58, 0x68, 0xFF };

const uint8_t *UiPixel(const UiFrame *frame, uint32_t x, uint32_t y) {
    if (frame == NULL || frame->pixels == NULL || x >= frame->width || y >= frame->height) {
        return NULL;
    }
    return frame->pixels + (size_t)y * frame->pitch + (size_t)x * 4u;
}

static void PutPixel(const UiFrame *frame, uint32_t x, uint32_t y, const uint8_t colour[4]) {
    if (x >= frame->width || y >= frame->height) {
        return;
    }
    memcpy(frame->pixels + (size_t)y * frame->pitch + (size_t)x * 4u, colour, 4u);
}

static void FillRect(const UiFrame *frame, uint32_t x, uint32_t y, uint32_t width,
                     uint32_t height, const uint8_t colour[4]) {
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t column = 0; column < width; ++column) {
            PutPixel(frame, x + column, y + row, colour);
        }
    }
}

static void DrawGlyph(const UiFrame *frame, uint32_t x, uint32_t y, uint32_t scale,
                      const uint8_t colour[4], char character) {
    if (character < FONT8X8_FIRST || character > FONT8X8_LAST) {
        return;
    }
    const uint8_t *const rows = FONT8X8[character - FONT8X8_FIRST];
    for (uint32_t row = 0; row < 8u; ++row) {
        for (uint32_t column = 0; column < 8u; ++column) {
            if ((rows[row] & (1u << column)) == 0u) {
                continue;
            }
            FillRect(frame, x + column * scale, y + row * scale, scale, scale, colour);
        }
    }
}

/// Draws @p text at @p scale times the 8 by 8 cell.
static void UiText(const UiFrame *frame, uint32_t x, uint32_t y, uint32_t scale,
                   const uint8_t colour[4], const char *text) {
    if (frame == NULL || frame->pixels == NULL || text == NULL || scale == 0u) {
        return;
    }
    for (size_t i = 0; text[i] != '\0'; ++i) {
        DrawGlyph(frame, x + (uint32_t)i * CELL * scale, y, scale, colour, text[i]);
    }
}

/// Draws @p text broken into lines of @p columns, at most @p max_lines of them, and hands back
/// how many it used. Breaks on a space where there is one, because a detail is a sentence.
static uint32_t TextWrapped(const UiFrame *frame, uint32_t x, uint32_t y, uint32_t scale,
                            const uint8_t colour[4], const char *text, uint32_t columns,
                            uint32_t max_lines) {
    const size_t length = strlen(text);
    size_t start = 0;
    uint32_t line = 0;
    while (start < length && line < max_lines) {
        size_t take = length - start < columns ? length - start : columns;
        if (start + take < length) {
            size_t space = take;
            while (space > 0 && text[start + space] != ' ') {
                --space;
            }
            if (space > 0) {
                take = space;
            }
        }
        char buffer[200];
        const size_t room = take + 1u < sizeof(buffer) ? take : sizeof(buffer) - 1u;
        memcpy(buffer, text + start, room);
        buffer[room] = '\0';
        UiText(frame, x, y + line * (CELL + 1u) * scale, scale, colour, buffer);
        start += take;
        while (start < length && text[start] == ' ') {
            ++start;
        }
        ++line;
    }
    return line;
}

/// One panel: the picture at @p scale times its own size, nearest neighbour, in a frame.
///
/// A colour picture is drawn over a checkerboard through its alpha, which is the only way a
/// transparent clear tells itself apart from an opaque one on a screen. A picture of bit
/// patterns is drawn opaque from the three bytes below the top one, because those carry the
/// variation in a float and in a depth; the byte left off is still in the files and still in
/// the verdict, so nothing is judged by what the screen leaves out.
static void DrawPanel(const UiFrame *frame, uint32_t x, uint32_t y, uint32_t scale,
                      const PictureRecord *record, UiView which) {
    const uint32_t width = record->width * scale;
    const uint32_t height = record->height * scale;
    // A panel at the very edge of the frame has no room for a border on that side, and a
    // subtraction there would wrap round and draw it on the opposite one.
    if (x > 0u) {
        FillRect(frame, x - 1u, y, 1u, height, BORDER);
    }
    if (y > 0u) {
        FillRect(frame, x, y - 1u, width, 1u, BORDER);
    }
    FillRect(frame, x, y + height, width, 1u, BORDER);
    FillRect(frame, x + width, y, 1u, height, BORDER);

    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t column = 0; column < width; ++column) {
            const size_t index =
                (size_t)(row / scale) * record->width + (column / scale);
            uint8_t colour[4] = { 0, 0, 0, 0xFF };
            if (which == UI_VIEW_DIFF) {
                PictureDiffColour(record->verdict[index], colour);
            } else {
                const uint8_t *const source =
                    (which == UI_VIEW_EXPECTED ? record->expected : record->got) + index * 4u;
                if (record->format == PICTURE_COLOUR) {
                    const uint8_t check =
                        ((((x + column) >> 3) ^ ((y + row) >> 3)) & 1u) != 0u ? 0x60 : 0x90;
                    const uint32_t alpha = source[3];
                    for (uint32_t channel = 0; channel < 3u; ++channel) {
                        colour[channel] = (uint8_t)((source[channel] * alpha +
                                                     check * (255u - alpha) + 127u) /
                                                    255u);
                    }
                } else {
                    colour[0] = source[2];
                    colour[1] = source[1];
                    colour[2] = source[0];
                }
            }
            PutPixel(frame, x + column, y + row, colour);
        }
    }
}

/// Copies at most @p columns characters of @p text, marking a cut with a tilde so a name that
/// did not fit says so instead of running off the edge.
static void Fit(char *out, size_t out_size, const char *text, uint32_t columns) {
    const size_t room = columns + 1u < out_size ? columns : out_size - 1u;
    const size_t length = strlen(text);
    const size_t take = length < room ? length : room;
    memcpy(out, text, take);
    out[take] = '\0';
    if (length > take && take > 0u) {
        out[take - 1u] = '~';
    }
}

/// The three colours the diff is made of, named on the screen. The person holding the pad is
/// not holding the README, and red is the most important thing on this display.
static void DrawKey(const UiFrame *frame, uint32_t x, uint32_t y) {
    static const struct {
        PictureVerdict verdict;
        const char *word;
    } KEY[4] = {
        { PICTURE_MATCHED, "accepted" },
        { PICTURE_WRONG, "refused" },
        { PICTURE_UNJUDGED, "not judged" },
        { PICTURE_UNSET, "nobody looked" },
    };
    uint32_t at = x;
    for (uint32_t i = 0; i < 4u; ++i) {
        uint8_t colour[4] = { 0, 0, 0, 0xFF };
        PictureDiffColour((uint8_t)KEY[i].verdict, colour);
        FillRect(frame, at, y, 16u, 16u, colour);
        UiText(frame, at + 22u, y, 2, DIM, KEY[i].word);
        at += 22u + (uint32_t)strlen(KEY[i].word) * CELL * 2u + 2u * CELL * 2u;
    }
}

static const char *ViewLabel(UiView view) {
    switch (view) {
    case UI_VIEW_GOT:
        return "drawn";
    case UI_VIEW_EXPECTED:
        return "expected";
    case UI_VIEW_DIFF:
        return "judged";
    case UI_VIEW_ALL:
        break;
    }
    return "all three";
}

void UiCompose(const UiFrame *frame, const PictureRecord *record, UiState state, uint32_t count,
               const uint8_t *failed, uint32_t run_failures) {
    if (frame == NULL || frame->pixels == NULL) {
        return;
    }
    FillRect(frame, 0, 0, frame->width, frame->height, BACKGROUND);
    if (record == NULL) {
        UiText(frame, MARGIN, MARGIN, 2, TEXT, "no case kept a picture");
        return;
    }

    // Only the cases that drew something are in this list, which is why the number is smaller
    // than the run's, and the header says so rather than leaving a reader to work it out.
    uint32_t failures = 0;
    uint32_t rank = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (failed != NULL && failed[i] != 0u) {
            ++failures;
            if (i <= state.index) {
                ++rank;
            }
        }
    }

    char header[160];
    if (!record->passed && failures > 0u) {
        snprintf(header, sizeof(header),
                 "drawing case %u of %u   failure %u of %u   %s   tier %s",
                 (unsigned)(state.index + 1u), (unsigned)count, (unsigned)rank,
                 (unsigned)failures, record->suite, record->tier);
    } else {
        snprintf(header, sizeof(header), "drawing case %u of %u   %u failed   %s   tier %s",
                 (unsigned)(state.index + 1u), (unsigned)count, (unsigned)failures,
                 record->suite, record->tier);
    }
    const uint32_t columns_for_name =
        frame->width > 2u * MARGIN ? (frame->width - 2u * MARGIN) / (CELL * 2u) : 1u;

    // The verdict first, and at the margin, because it is the one thing on this screen that
    // must be there however narrow the frame is or however long the rest of the line runs.
    UiText(frame, MARGIN, 8, 2, record->passed ? PASSED : FAILED,
           record->passed ? "PASS" : "FAIL");
    char fitted[200];
    Fit(fitted, sizeof(fitted), header, columns_for_name > 6u ? columns_for_name - 6u : 1u);
    UiText(frame, MARGIN + 6u * CELL * 2u, 8, 2, DIM, fitted);

    char name[200];
    Fit(name, sizeof(name), record->name, columns_for_name);
    UiText(frame, MARGIN, 30, 2, TEXT, name);

    const uint32_t top = UI_PANEL_TOP;
    const uint32_t columns =
        frame->width > 2u * MARGIN ? (frame->width - 2u * MARGIN) / (CELL * 2u) : 1u;
    uint32_t bottom;

    if (state.view == UI_VIEW_ALL) {
        // Every division below is guarded, because a frame too small for the layout should come
        // out cramped rather than wrap around and address the whole of memory.
        const uint32_t room =
            frame->width > 4u * MARGIN ? (frame->width - 4u * MARGIN) / 3u : record->width;
        uint32_t scale = record->width > 0u ? room / record->width : 1u;
        if (scale == 0u) {
            scale = 1u;
        }
        const uint32_t size = record->width * scale;
        static const UiView PANELS[3] = { UI_VIEW_GOT, UI_VIEW_EXPECTED, UI_VIEW_DIFF };
        for (uint32_t panel = 0; panel < 3u; ++panel) {
            const uint32_t x = MARGIN + panel * (size + MARGIN);
            UiText(frame, x, top - 20u, 2, DIM, ViewLabel(PANELS[panel]));
            DrawPanel(frame, x, top, scale, record, PANELS[panel]);
        }
        bottom = top + record->height * scale;
    } else {
        const uint32_t room_across =
            frame->width > 2u * MARGIN ? frame->width - 2u * MARGIN : record->width;
        const uint32_t room_down =
            frame->height > top + 128u ? frame->height - top - 128u : record->height;
        uint32_t scale = record->width > 0u ? room_across / record->width : 1u;
        const uint32_t down = record->height > 0u ? room_down / record->height : 1u;
        if (down < scale) {
            scale = down;
        }
        if (scale == 0u) {
            scale = 1u;
        }
        const uint32_t size = record->width * scale;
        const uint32_t x = frame->width > size ? (frame->width - size) / 2u : 0u;
        UiText(frame, x, top - 20u, 2, DIM, ViewLabel(state.view));
        DrawPanel(frame, x, top, scale, record, state.view);
        bottom = top + record->height * scale;
    }

    // The key sits between the pictures and the sentence, because it explains the one and is
    // read on the way to the other.
    DrawKey(frame, MARGIN, bottom + 12u);

    // A case that failed before it read anything back has no picture, so it is not in this list
    // at all. On the machine you would open this mode on, that is exactly the case you would
    // most want to know about.
    if (run_failures > failures) {
        char missing[96];
        snprintf(missing, sizeof(missing),
                 "%u of the run's %u failures are here; the rest failed before they drew",
                 (unsigned)failures, (unsigned)run_failures);
        UiText(frame, MARGIN, bottom + 34u, 1, DIM, missing);
    }

    const uint32_t detail_y = bottom + 48u;
    const uint32_t room_for_lines =
        frame->height > detail_y + 32u ? (frame->height - detail_y - 32u) / 18u : 1u;
    if (record->detail[0] != '\0') {
        TextWrapped(frame, MARGIN, detail_y, 2, TEXT, record->detail, columns, room_for_lines);
    } else {
        UiText(frame, MARGIN, detail_y, 2, DIM, "no detail: the case passed");
    }

    UiText(frame, MARGIN, frame->height > 14u ? frame->height - 14u : 0u, 1, DIM,
           "A or right next   B or left previous   X view   down next failure   "
           "up previous failure   plus quit");
}

/// Whether @p word appears in @p text on its own, rather than inside a longer word. Case is
/// ignored, and so is everything that is not a letter, so a file with one word per line and a
/// file with them on one line say the same thing.
static bool HasWord(const char *text, const char *word) {
    const size_t length = strlen(word);
    for (size_t i = 0; text[i] != '\0'; ++i) {
        const char before = i == 0 ? ' ' : text[i - 1];
        if ((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z')) {
            continue;
        }
        size_t matched = 0;
        while (matched < length) {
            char c = text[i + matched];
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c - 'A' + 'a');
            }
            if (c != word[matched]) {
                break;
            }
            ++matched;
        }
        if (matched != length) {
            continue;
        }
        const char after = text[i + length];
        if (!((after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z'))) {
            return true;
        }
    }
    return false;
}

UiMode UiModeFromText(const char *text) {
    UiMode mode = { false, false };
    if (text == NULL) {
        return mode;
    }
    mode.detailed = HasWord(text, "detailed");
    mode.every_case = HasWord(text, "images");
    return mode;
}

UiMode UiModeAtLaunch(const char *text, bool cycle_button_held) {
    UiMode mode = UiModeFromText(text);
    mode.detailed = mode.detailed || cycle_button_held;
    return mode;
}

bool UiWaitsForPad(uint32_t frame_index, bool connected) {
    return !connected && frame_index < UI_PAD_WAIT_FRAMES;
}

UiInput UiInputFromButtons(uint32_t buttons, uint32_t frame_index) {
    if (frame_index < UI_SETTLING_FRAMES) {
        return UI_INPUT_NONE;
    }
    // Quitting first, so that a mask with everything in it still leaves.
    if ((buttons & UI_BUTTON_QUIT) != 0u) {
        return UI_INPUT_QUIT;
    }
    if ((buttons & UI_BUTTON_CYCLE_VIEW) != 0u) {
        return UI_INPUT_CYCLE_VIEW;
    }
    if ((buttons & UI_BUTTON_NEXT_FAILURE) != 0u) {
        return UI_INPUT_NEXT_FAILURE;
    }
    if ((buttons & UI_BUTTON_PREVIOUS_FAILURE) != 0u) {
        return UI_INPUT_PREVIOUS_FAILURE;
    }
    if ((buttons & UI_BUTTON_NEXT) != 0u) {
        return UI_INPUT_NEXT;
    }
    if ((buttons & UI_BUTTON_PREVIOUS) != 0u) {
        return UI_INPUT_PREVIOUS;
    }
    return UI_INPUT_NONE;
}

/// The first failing case, or the first case when the run had none.
static uint32_t FirstFailure(const uint8_t *failed, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (failed != NULL && failed[i] != 0u) {
            return i;
        }
    }
    return 0u;
}

UiState UiStart(const uint8_t *failed, uint32_t count) {
    UiState state;
    state.index = FirstFailure(failed, count);
    state.view = UI_VIEW_ALL;
    state.quit = false;
    state.changed = true;
    return state;
}

/// The next case in @p step's direction that failed, wrapping once, or @p from when there is
/// none. Wrapping rather than stopping at the end, because a run with two failures out of a
/// hundred and seventy is exactly the run this mode is for.
static uint32_t SeekFailure(uint32_t from, int step, const uint8_t *failed, uint32_t count) {
    if (failed == NULL || count == 0u) {
        return from;
    }
    uint32_t at = from;
    for (uint32_t tried = 0; tried < count; ++tried) {
        at = step > 0 ? (at + 1u) % count : (at + count - 1u) % count;
        if (failed[at] != 0u) {
            return at;
        }
    }
    return from;
}

void UiFrameDrawn(UiState *state) {
    if (state != NULL) {
        state->changed = false;
    }
}

UiState UiStep(UiState state, UiInput input, const uint8_t *failed, uint32_t count) {
    UiState next = state;
    if (count == 0u) {
        next.index = 0u;
        next.quit = state.quit || input == UI_INPUT_QUIT;
        next.changed = next.changed || next.quit != state.quit;
        return next;
    }
    if (next.index >= count) {
        next.index = count - 1u;
        next.changed = true;
    }

    switch (input) {
    case UI_INPUT_NEXT:
        next.index = (next.index + 1u) % count;
        break;
    case UI_INPUT_PREVIOUS:
        next.index = (next.index + count - 1u) % count;
        break;
    case UI_INPUT_NEXT_FAILURE:
        next.index = SeekFailure(next.index, 1, failed, count);
        break;
    case UI_INPUT_PREVIOUS_FAILURE:
        next.index = SeekFailure(next.index, -1, failed, count);
        break;
    case UI_INPUT_CYCLE_VIEW:
        next.view = (UiView)((next.view + 1) % 4);
        break;
    case UI_INPUT_QUIT:
        next.quit = true;
        break;
    case UI_INPUT_NONE:
        break;
    }

    next.changed = next.changed || next.index != state.index || next.view != state.view ||
                   next.quit != state.quit;
    return next;
}
