// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
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

// Text is drawn at twice the cell. A line of it takes a list row, with more room on a page of
// detail, which is read rather than scanned.
#define BIG 2u
#define GLYPH (CELL * BIG)
#define LINE UI_ROW_HEIGHT
#define PAGE_LINE 24u

// The bottom of the frame belongs to the countdown and the button line, and nothing else is
// drawn below this far from the edge.
#define FOOT 32u

// Lines under the overview's list for the selected group, and under a group's for its case.
#define PREVIEW_LINES 6u
#define DETAIL_LINES 7u

static const uint8_t BACKGROUND[4] = { 0x10, 0x12, 0x18, 0xFF };
static const uint8_t TEXT[4] = { 0xE0, 0xE4, 0xEC, 0xFF };
static const uint8_t DIM[4] = { 0x88, 0x90, 0xA0, 0xFF };
static const uint8_t PASSED[4] = { 0x40, 0xD0, 0x60, 0xFF };
static const uint8_t FAILED[4] = { 0xF0, 0x50, 0x50, 0xFF };
static const uint8_t SKIPPED[4] = { 0xE8, 0xB0, 0x40, 0xFF };
static const uint8_t CRASHED[4] = { 0xC0, 0x78, 0xF0, 0xFF };
static const uint8_t BORDER[4] = { 0x50, 0x58, 0x68, 0xFF };
static const uint8_t SELECTED[4] = { 0x28, 0x2E, 0x3C, 0xFF };

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

/// Big text, handing back where the next word along the line would start.
static uint32_t Say(const UiFrame *frame, uint32_t x, uint32_t y, const uint8_t colour[4],
                    const char *text) {
    UiText(frame, x, y, BIG, colour, text);
    return x + (uint32_t)strlen(text) * GLYPH;
}

/// Draws @p text broken into lines of @p columns, @p step rows apart, at most @p max_lines of
/// them, and hands back how many it used. Breaks on a space where there is one, because a detail
/// is a sentence. With no frame it draws nothing and only counts.
static uint32_t TextWrapped(const UiFrame *frame, uint32_t x, uint32_t y, uint32_t scale,
                            uint32_t step, const uint8_t colour[4], const char *text,
                            uint32_t columns, uint32_t max_lines) {
    if (text == NULL) {
        return 0;
    }
    // A line of no columns would take nothing and loop for ever.
    if (columns == 0u) {
        columns = 1u;
    }
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
        UiText(frame, x, y + line * step, scale, colour, buffer);
        start += take;
        while (start < length && text[start] == ' ') {
            ++start;
        }
        ++line;
    }
    return line;
}

uint32_t UiWrappedLines(const char *text, uint32_t columns) {
    return TextWrapped(NULL, 0, 0, 1, 0, TEXT, text, columns, UINT32_MAX);
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

/// How many big characters fit across the frame inside the margins.
static uint32_t Columns(const UiFrame *frame) {
    return frame->width > 2u * MARGIN + GLYPH ? (frame->width - 2u * MARGIN) / GLYPH : 1u;
}

/// Big text cut to what is left of the line from @p x, so nothing runs off the frame's edge.
static uint32_t SayFitted(const UiFrame *frame, uint32_t x, uint32_t y, const uint8_t colour[4],
                          const char *text) {
    const uint32_t right = frame->width > MARGIN ? frame->width - MARGIN : 0u;
    char fitted[200];
    Fit(fitted, sizeof(fitted), text, x < right ? (right - x) / GLYPH : 0u);
    return Say(frame, x, y, colour, fitted);
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

/// What the buttons do on this screen, on the frame's last line.
static void DrawButtons(const UiFrame *frame, const char *text) {
    UiText(frame, MARGIN, frame->height > 14u ? frame->height - 14u : 0u, 1, DIM, text);
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

// Which family each suite belongs to, by the name its lines carry. The judge first, because
// nothing after it can be believed without it.
static const struct {
    const char *suite;
    UiFamily family;
} FAMILIES[] = {
    { "selfcheck", UI_FAMILY_JUDGE }, { "support", UI_FAMILY_JUDGE },
    { "reference", UI_FAMILY_JUDGE }, { "cpu", UI_FAMILY_CPU },
    { "clear", UI_FAMILY_GRAPHICS },  { "format", UI_FAMILY_GRAPHICS },
    { "raster", UI_FAMILY_GRAPHICS }, { "intmath", UI_FAMILY_GRAPHICS },
    { "blend", UI_FAMILY_GRAPHICS },  { "depth", UI_FAMILY_GRAPHICS },
    { "texture", UI_FAMILY_GRAPHICS }, { "coherence", UI_FAMILY_GRAPHICS },
    { "fmath", UI_FAMILY_GRAPHICS },  { "order", UI_FAMILY_GRAPHICS },
    { "index", UI_FAMILY_GRAPHICS },  { "instance", UI_FAMILY_GRAPHICS },
    { "bindless", UI_FAMILY_GRAPHICS }, { "astc", UI_FAMILY_GRAPHICS },
    { "eac", UI_FAMILY_GRAPHICS },    { "etc2", UI_FAMILY_GRAPHICS },
    { "bcn", UI_FAMILY_GRAPHICS },    { "zeta", UI_FAMILY_GRAPHICS },
    { "swizzle", UI_FAMILY_GRAPHICS },
    { "texel", UI_FAMILY_GRAPHICS },  { "stream", UI_FAMILY_GRAPHICS },
    { "fill", UI_FAMILY_GRAPHICS },   { "geometry", UI_FAMILY_GRAPHICS },
    { "query", UI_FAMILY_GRAPHICS },  { "nifm", UI_FAMILY_NET },
    { "dns", UI_FAMILY_NET },         { "bsd", UI_FAMILY_NET },
    { "ssl", UI_FAMILY_NET },         { "acc", UI_FAMILY_NET },
    { "friend", UI_FAMILY_NET },
};

UiFamily UiFamilyOf(const char *suite) {
    for (size_t i = 0; suite != NULL && i < sizeof(FAMILIES) / sizeof(FAMILIES[0]); ++i) {
        if (strcmp(FAMILIES[i].suite, suite) == 0) {
            return FAMILIES[i].family;
        }
    }
    return UI_FAMILY_OTHER;
}

const char *UiFamilyName(UiFamily family) {
    switch (family) {
    case UI_FAMILY_JUDGE:
        return "judge";
    case UI_FAMILY_CPU:
        return "cpu";
    case UI_FAMILY_GRAPHICS:
        return "graphics";
    case UI_FAMILY_NET:
        return "net";
    case UI_FAMILY_OTHER:
    case UI_FAMILY_COUNT:
        break;
    }
    return "other";
}

/// A case that asked nothing: skipped for any reason, crashed included.
static bool Skipped(ReportResult result) {
    return result == REPORT_SKIP || result == REPORT_CRASHED;
}

void UiRunIndex(UiRun *run, const ReportRecord *records, uint32_t count, uint32_t reported) {
    if (run == NULL) {
        return;
    }
    memset(run, 0, sizeof(*run));
    if (records == NULL) {
        count = 0u;
    }
    run->records = records;
    run->count = count;
    run->reported = reported > count ? reported : count;

    // Groups are made in the order the run first reached them; 0xFF marks a case that found no
    // room, which is counted rather than shown under somebody else's suite.
    uint8_t group_of[UI_MAX_CASES];
    const uint32_t indexed = count < UI_MAX_CASES ? count : UI_MAX_CASES;
    run->unplaced = count - indexed;
    for (uint32_t i = 0; i < indexed; ++i) {
        const ReportRecord *const record = &records[i];
        uint32_t g = 0;
        while (g < run->group_count && strcmp(run->groups[g].suite, record->suite) != 0) {
            ++g;
        }
        if (g == run->group_count) {
            if (run->group_count == UI_MAX_GROUPS) {
                group_of[i] = 0xFF;
                ++run->unplaced;
                continue;
            }
            run->groups[g].suite = record->suite;
            run->groups[g].family = UiFamilyOf(record->suite);
            ++run->group_count;
        }
        group_of[i] = (uint8_t)g;
        UiGroup *const group = &run->groups[g];
        ++group->count;
        if (record->result == REPORT_FAIL) {
            ++group->failed;
            ++run->failed;
        } else if (record->result == REPORT_CRASHED) {
            ++group->crashed;
            ++run->crashed;
        } else if (record->result == REPORT_SKIP) {
            ++group->skipped;
            ++run->skipped;
        } else {
            ++group->passed;
            ++run->passed;
        }
    }

    // Stable, so a family's groups keep the order the run reached them in.
    UiGroup sorted[UI_MAX_GROUPS];
    uint8_t position[UI_MAX_GROUPS];
    uint32_t placed = 0;
    for (uint32_t family = 0; family < UI_FAMILY_COUNT; ++family) {
        for (uint32_t g = 0; g < run->group_count; ++g) {
            if (run->groups[g].family == (UiFamily)family) {
                position[g] = (uint8_t)placed;
                sorted[placed++] = run->groups[g];
            }
        }
    }
    uint32_t cursor[UI_MAX_GROUPS];
    uint32_t first = 0;
    for (uint32_t g = 0; g < run->group_count; ++g) {
        run->groups[g] = sorted[g];
        run->groups[g].first = first;
        cursor[g] = first;
        first += sorted[g].count;
    }
    for (uint32_t i = 0; i < indexed; ++i) {
        if (group_of[i] != 0xFF) {
            run->order[cursor[position[group_of[i]]]++] = (uint16_t)i;
        }
    }
}

const ReportRecord *UiCaseAt(const UiRun *run, uint32_t group, uint32_t index) {
    if (run == NULL || group >= run->group_count || index >= run->groups[group].count) {
        return NULL;
    }
    return &run->records[run->order[run->groups[group].first + index]];
}

const ReportRecord *UiSelected(const UiRun *run, UiState state) {
    return state.level == UI_LEVEL_OVERVIEW ? NULL : UiCaseAt(run, state.group, state.index);
}

int32_t UiOpenPicture(const UiRun *run, UiState state) {
    const ReportRecord *const selected = UiSelected(run, state);
    return state.level == UI_LEVEL_CASE && selected != NULL ? selected->picture : -1;
}

/// One step along a list of @p count, coming round at either end.
static uint32_t Around(uint32_t at, int step, uint32_t count) {
    return step > 0 ? (at + 1u) % count : (at + count - 1u) % count;
}

static bool CaseFailed(const UiRun *run, uint32_t group, uint32_t index) {
    const ReportRecord *const record = UiCaseAt(run, group, index);
    return record != NULL && record->result == REPORT_FAIL;
}

/// Where a group is entered: its first failure, or its first case.
static uint32_t FirstFailureIn(const UiRun *run, uint32_t group) {
    for (uint32_t i = 0; i < run->groups[group].count; ++i) {
        if (CaseFailed(run, group, i)) {
            return i;
        }
    }
    return 0u;
}

/// The next failing case of @p group in @p step's direction, coming round once, or @p from when
/// there is none. Coming round rather than stopping, because a group with two failures out of a
/// hundred is exactly the group this is for.
static uint32_t SeekFailingCase(const UiRun *run, uint32_t group, uint32_t from, int step) {
    const uint32_t count = run->groups[group].count;
    uint32_t at = from;
    for (uint32_t tried = 0; tried < count; ++tried) {
        at = Around(at, step, count);
        if (CaseFailed(run, group, at)) {
            return at;
        }
    }
    return from;
}

static uint32_t SeekFailingGroup(const UiRun *run, uint32_t from, int step) {
    uint32_t at = from;
    for (uint32_t tried = 0; tried < run->group_count; ++tried) {
        at = Around(at, step, run->group_count);
        if (run->groups[at].failed > 0u) {
            return at;
        }
    }
    return from;
}

/// The first group of the family before or after the one @p from is in, coming round.
static uint32_t SeekFamily(const UiRun *run, uint32_t from, int step) {
    const UiGroup *const groups = run->groups;
    uint32_t start = from;
    while (start > 0u && groups[start - 1u].family == groups[from].family) {
        --start;
    }
    uint32_t at = start;
    if (step > 0) {
        do {
            at = Around(at, 1, run->group_count);
        } while (at != start && groups[at].family == groups[from].family);
    } else {
        at = Around(start, -1, run->group_count);
        while (at > 0u && groups[at - 1u].family == groups[at].family) {
            --at;
        }
    }
    return at;
}

UiState UiStart(const UiRun *run) {
    UiState state;
    memset(&state, 0, sizeof(state));
    state.level = UI_LEVEL_OVERVIEW;
    state.view = UI_VIEW_ALL;
    state.changed = true;
    for (uint32_t g = 0; run != NULL && g < run->group_count; ++g) {
        if (run->groups[g].failed > 0u) {
            state.group = g;
            break;
        }
    }
    return state;
}

void UiFrameDrawn(UiState *state) {
    if (state != NULL) {
        state->changed = false;
    }
}

static void StepOverview(UiState *next, UiInput input, const UiRun *run) {
    switch (input) {
    case UI_INPUT_UP:
        next->group = Around(next->group, -1, run->group_count);
        break;
    case UI_INPUT_DOWN:
        next->group = Around(next->group, 1, run->group_count);
        break;
    case UI_INPUT_LEFT:
        next->group = SeekFamily(run, next->group, -1);
        break;
    case UI_INPUT_RIGHT:
        next->group = SeekFamily(run, next->group, 1);
        break;
    case UI_INPUT_NEXT_FAILURE:
        next->group = SeekFailingGroup(run, next->group, 1);
        break;
    case UI_INPUT_PREVIOUS_FAILURE:
        next->group = SeekFailingGroup(run, next->group, -1);
        break;
    case UI_INPUT_OPEN:
        next->level = UI_LEVEL_GROUP;
        next->index = FirstFailureIn(run, next->group);
        break;
    default:
        break;
    }
}

static void StepGroup(UiState *next, UiInput input, const UiRun *run) {
    const uint32_t count = run->groups[next->group].count;
    switch (input) {
    case UI_INPUT_UP:
        next->index = Around(next->index, -1, count);
        break;
    case UI_INPUT_DOWN:
        next->index = Around(next->index, 1, count);
        break;
    case UI_INPUT_LEFT:
    case UI_INPUT_RIGHT:
        next->group = Around(next->group, input == UI_INPUT_RIGHT ? 1 : -1, run->group_count);
        next->index = FirstFailureIn(run, next->group);
        break;
    case UI_INPUT_NEXT_FAILURE:
        next->index = SeekFailingCase(run, next->group, next->index, 1);
        break;
    case UI_INPUT_PREVIOUS_FAILURE:
        next->index = SeekFailingCase(run, next->group, next->index, -1);
        break;
    case UI_INPUT_OPEN:
        next->level = UI_LEVEL_CASE;
        break;
    case UI_INPUT_BACK:
        next->level = UI_LEVEL_OVERVIEW;
        break;
    default:
        break;
    }
}

static void StepCase(UiState *next, UiInput input, const UiRun *run) {
    const uint32_t count = run->groups[next->group].count;
    switch (input) {
    case UI_INPUT_UP:
    case UI_INPUT_LEFT:
        next->index = Around(next->index, -1, count);
        break;
    case UI_INPUT_DOWN:
    case UI_INPUT_RIGHT:
        next->index = Around(next->index, 1, count);
        break;
    case UI_INPUT_NEXT_FAILURE:
        next->index = SeekFailingCase(run, next->group, next->index, 1);
        break;
    case UI_INPUT_PREVIOUS_FAILURE:
        next->index = SeekFailingCase(run, next->group, next->index, -1);
        break;
    case UI_INPUT_CYCLE_VIEW: {
        // Only where there is something to cycle: on a case with no pictures the press would
        // change a view nobody can see, and the one it opened next would not be the one chosen.
        const ReportRecord *const record = UiCaseAt(run, next->group, next->index);
        if (record != NULL && record->picture >= 0) {
            next->view = (UiView)((next->view + 1) % 4);
        }
        break;
    }
    case UI_INPUT_BACK:
        next->level = UI_LEVEL_GROUP;
        break;
    default:
        break;
    }
}

UiState UiStep(UiState state, UiInput input, const UiRun *run) {
    UiState next = state;
    if (input == UI_INPUT_QUIT) {
        next.quit = true;
    } else if (run != NULL && run->group_count > 0u) {
        if (next.group >= run->group_count) {
            next.group = run->group_count - 1u;
        }
        if (next.index >= run->groups[next.group].count) {
            next.index = run->groups[next.group].count - 1u;
        }
        switch (next.level) {
        case UI_LEVEL_OVERVIEW:
            StepOverview(&next, input, run);
            break;
        case UI_LEVEL_GROUP:
            StepGroup(&next, input, run);
            break;
        case UI_LEVEL_CASE:
            StepCase(&next, input, run);
            break;
        }
    }
    next.changed = next.changed || next.level != state.level || next.group != state.group ||
                   next.index != state.index || next.view != state.view ||
                   next.quit != state.quit;
    return next;
}

UiInput UiInputFromButtons(uint32_t buttons, uint32_t frame_index) {
    if (frame_index < UI_SETTLING_FRAMES) {
        return UI_INPUT_NONE;
    }
    // Leaving first, then going back, so that a mask with everything in it leaves, and one with
    // back and a direction in it does not carry the person somewhere they were leaving.
    static const struct {
        uint32_t button;
        UiInput input;
    } ORDER[] = {
        { UI_BUTTON_QUIT, UI_INPUT_QUIT },
        { UI_BUTTON_BACK, UI_INPUT_BACK },
        { UI_BUTTON_OPEN, UI_INPUT_OPEN },
        { UI_BUTTON_CYCLE_VIEW, UI_INPUT_CYCLE_VIEW },
        { UI_BUTTON_NEXT_FAILURE, UI_INPUT_NEXT_FAILURE },
        { UI_BUTTON_PREVIOUS_FAILURE, UI_INPUT_PREVIOUS_FAILURE },
        { UI_BUTTON_UP, UI_INPUT_UP },
        { UI_BUTTON_DOWN, UI_INPUT_DOWN },
        { UI_BUTTON_LEFT, UI_INPUT_LEFT },
        { UI_BUTTON_RIGHT, UI_INPUT_RIGHT },
    };
    for (size_t i = 0; i < sizeof(ORDER) / sizeof(ORDER[0]); ++i) {
        if ((buttons & ORDER[i].button) != 0u) {
            return ORDER[i].input;
        }
    }
    return UI_INPUT_NONE;
}

uint32_t UiRepeatButtons(UiRepeat *repeat, uint32_t held, uint32_t down, uint32_t frame_index) {
    const uint32_t directions = held & UI_BUTTONS_REPEATING;
    if (repeat == NULL) {
        return down;
    }
    if (frame_index < UI_SETTLING_FRAMES) {
        repeat->held = 0u;
        repeat->frames = 0u;
        return down;
    }
    // Only what this frame pressed: a direction held from before, or since before the pad
    // settled, is not what the person just asked for.
    if ((down & UI_BUTTONS_REPEATING) != 0u) {
        repeat->held = down & UI_BUTTONS_REPEATING;
        repeat->frames = 0u;
        return down;
    }
    if (repeat->held == 0u || (directions & repeat->held) != repeat->held) {
        repeat->held = 0u;
        repeat->frames = 0u;
        return down;
    }
    ++repeat->frames;
    if (repeat->frames >= UI_REPEAT_DELAY_FRAMES &&
        (repeat->frames - UI_REPEAT_DELAY_FRAMES) % UI_REPEAT_EVERY_FRAMES == 0u) {
        return down | repeat->held;
    }
    return down;
}

uint32_t UiListRows(UiLevel level, uint32_t height) {
    const uint32_t lines = level == UI_LEVEL_OVERVIEW ? PREVIEW_LINES : DETAIL_LINES;
    // The foot, the lines under the list, and a gap with a rule in it.
    const uint32_t under = FOOT + lines * LINE + 16u;
    if (height < UI_LIST_TOP + under + UI_ROW_HEIGHT) {
        return 1u;
    }
    return (height - UI_LIST_TOP - under) / UI_ROW_HEIGHT;
}

uint32_t UiListFirst(uint32_t selected, uint32_t total, uint32_t rows) {
    if (rows == 0u || total <= rows) {
        return 0u;
    }
    const uint32_t half = rows / 2u;
    const uint32_t first = selected > half ? selected - half : 0u;
    return first > total - rows ? total - rows : first;
}

uint32_t UiOverviewRow(const UiRun *run, uint32_t group) {
    uint32_t row = 0;
    for (uint32_t g = 0; run != NULL && g <= group && g < run->group_count; ++g) {
        if (g == 0u || run->groups[g].family != run->groups[g - 1u].family) {
            ++row;
        }
        if (g < group) {
            ++row;
        }
    }
    return row;
}

static const uint8_t *ResultColour(ReportResult result) {
    return result == REPORT_FAIL      ? FAILED
           : result == REPORT_CRASHED ? CRASHED
           : result == REPORT_SKIP    ? SKIPPED
                                      : PASSED;
}

/// What to say where a case's line carried no detail, which is only unremarkable for a pass.
static const char *NoDetail(ReportResult result) {
    return result == REPORT_FAIL   ? "no detail: the line says only that it failed"
           : Skipped(result)       ? "no detail: the line gives no reason"
                                   : "no detail: the case passed";
}

static const char *ResultWord(ReportResult result) {
    return result == REPORT_FAIL ? "FAIL" : Skipped(result) ? "SKIP" : "PASS";
}

/// A group's verdict in one word: failed if anything did, passed if anything did, and skipped
/// only when nothing was asked at all.
static ReportResult GroupResult(uint32_t passed, uint32_t failed) {
    return failed > 0u ? REPORT_FAIL : passed > 0u ? REPORT_PASS : REPORT_SKIP;
}

/// The counts along a line, each in its own colour and dim when it is nought; crashes only
/// when there are any, apart from the skips as the summary counts them.
static uint32_t SayCounts(const UiFrame *frame, uint32_t x, uint32_t y, uint32_t passed,
                          uint32_t failed, uint32_t skipped, uint32_t crashed) {
    char text[32];
    snprintf(text, sizeof(text), "%u passed", (unsigned)passed);
    x = Say(frame, x, y, passed > 0u ? PASSED : DIM, text) + 2u * GLYPH;
    snprintf(text, sizeof(text), "%u failed", (unsigned)failed);
    x = Say(frame, x, y, failed > 0u ? FAILED : DIM, text) + 2u * GLYPH;
    snprintf(text, sizeof(text), "%u skipped", (unsigned)skipped);
    x = Say(frame, x, y, skipped > 0u ? SKIPPED : DIM, text);
    if (crashed == 0u) {
        return x;
    }
    snprintf(text, sizeof(text), "%u crashed", (unsigned)crashed);
    return Say(frame, x + 2u * GLYPH, y, CRASHED, text);
}

/// The proportions of a group at a glance: failures first and never thinner than two pixels,
/// so one failure among a hundred passes still shows.
static void DrawBar(const UiFrame *frame, uint32_t x, uint32_t y, uint32_t width,
                    const UiGroup *group) {
    const uint32_t total = group->passed + group->failed + group->skipped + group->crashed;
    if (total == 0u || width < 8u) {
        return;
    }
    uint32_t failed = group->failed > 0u ? group->failed * width / total : 0u;
    uint32_t crashed = group->crashed > 0u ? group->crashed * width / total : 0u;
    uint32_t skipped = group->skipped > 0u ? group->skipped * width / total : 0u;
    if (group->failed > 0u && failed < 2u) {
        failed = 2u;
    }
    if (group->crashed > 0u && crashed < 2u) {
        crashed = 2u;
    }
    if (group->skipped > 0u && skipped < 2u) {
        skipped = 2u;
    }
    // The two pixel floors can add up to more than the bar; the largest share gives way.
    while (failed + crashed + skipped > width) {
        if (failed >= skipped && failed >= crashed) {
            --failed;
        } else if (skipped >= crashed) {
            --skipped;
        } else {
            --crashed;
        }
    }
    if (group->passed == 0u) {
        // Nothing passed: the others fill the bar between them.
        if (group->skipped > 0u) {
            skipped = width - failed - crashed;
        } else if (group->crashed > 0u) {
            crashed = width - failed;
        } else {
            failed = width;
        }
    }
    const uint32_t passed = width - failed - crashed - skipped;
    FillRect(frame, x, y, failed, 8u, FAILED);
    FillRect(frame, x + failed, y, crashed, 8u, CRASHED);
    FillRect(frame, x + failed + crashed, y, skipped, 8u, SKIPPED);
    FillRect(frame, x + failed + crashed + skipped, y, passed, 8u, PASSED);
}

/// A group's row. @p crashes_column is set when anything in the run crashed: a column of its own
/// then, and a run with none keeps the three it always had.
static void DrawGroupRow(const UiFrame *frame, const UiGroup *group, uint32_t y, bool selected,
                         bool crashes_column) {
    if (selected) {
        FillRect(frame, MARGIN / 2u, y, frame->width - MARGIN, UI_ROW_HEIGHT, SELECTED);
        Say(frame, MARGIN, y + 2u, TEXT, ">");
    }
    char text[32];
    Fit(text, sizeof(text), group->suite, 12u);
    Say(frame, MARGIN + 2u * GLYPH, y + 2u, group->failed > 0u ? FAILED : TEXT, text);
    // In columns, so a failure count is found by looking straight down.
    uint32_t x = MARGIN + 16u * GLYPH;
    snprintf(text, sizeof(text), "%4u passed", (unsigned)group->passed);
    Say(frame, x, y + 2u, group->passed > 0u ? PASSED : DIM, text);
    x += 12u * GLYPH;
    snprintf(text, sizeof(text), "%3u failed", (unsigned)group->failed);
    Say(frame, x, y + 2u, group->failed > 0u ? FAILED : DIM, text);
    x += 11u * GLYPH;
    snprintf(text, sizeof(text), "%3u skipped", (unsigned)group->skipped);
    Say(frame, x, y + 2u, group->skipped > 0u ? SKIPPED : DIM, text);
    if (crashes_column) {
        x += 12u * GLYPH;
        snprintf(text, sizeof(text), "%3u crashed", (unsigned)group->crashed);
        Say(frame, x, y + 2u, group->crashed > 0u ? CRASHED : DIM, text);
    }
    const uint32_t bar = MARGIN + (crashes_column ? 64u : 52u) * GLYPH;
    if (frame->width > bar + MARGIN) {
        DrawBar(frame, bar, y + 6u, frame->width - MARGIN - bar, group);
    }
}

/// The result, in its colour, and the case's name after it.
static void DrawCaseLine(const UiFrame *frame, uint32_t x, uint32_t y, const ReportRecord *record,
                         uint32_t name_columns) {
    Say(frame, x, y, ResultColour(record->result), ResultWord(record->result));
    char name[200];
    Fit(name, sizeof(name), record->name, name_columns);
    Say(frame, x + 5u * GLYPH, y, TEXT, name);
}

/// Under the overview's list: what the selected group holds, its failures first, then what the
/// program ended inside, then its skips.
static void ComposePreview(const UiFrame *frame, const UiRun *run, uint32_t index, uint32_t y) {
    const UiGroup *const group = &run->groups[index];
    char text[96];
    uint32_t x = Say(frame, MARGIN, y, TEXT, group->suite);
    snprintf(text, sizeof(text), " in %s: ", UiFamilyName(group->family));
    x = Say(frame, x, y, DIM, text);
    if (group->failed > 0u) {
        snprintf(text, sizeof(text), "%u of %u failed", (unsigned)group->failed,
                 (unsigned)group->count);
        SayFitted(frame, x, y, FAILED, text);
    } else if (group->crashed > 0u) {
        snprintf(text, sizeof(text), "nothing failed, %u of %u skipped as crashed",
                 (unsigned)group->crashed, (unsigned)group->count);
        SayFitted(frame, x, y, CRASHED, text);
    } else if (group->skipped > 0u) {
        snprintf(text, sizeof(text), "nothing failed, %u of %u skipped",
                 (unsigned)group->skipped, (unsigned)group->count);
        SayFitted(frame, x, y, SKIPPED, text);
    } else {
        snprintf(text, sizeof(text), "all %u passed", (unsigned)group->count);
        SayFitted(frame, x, y, PASSED, text);
        Say(frame, MARGIN, y + LINE, DIM, "A opens the list of its cases");
        return;
    }

    const ReportResult shown = group->failed > 0u    ? REPORT_FAIL
                               : group->crashed > 0u ? REPORT_CRASHED
                                                     : REPORT_SKIP;
    const uint32_t wanted = group->failed > 0u    ? group->failed
                            : group->crashed > 0u ? group->crashed
                                                  : group->skipped;
    const uint32_t room = PREVIEW_LINES - 1u;
    const uint32_t columns = Columns(frame) > 5u ? Columns(frame) - 5u : 1u;
    uint32_t listed = 0;
    for (uint32_t i = 0; i < group->count && listed < room; ++i) {
        const ReportRecord *const record = UiCaseAt(run, index, i);
        if (record == NULL || record->result != shown) {
            continue;
        }
        if (listed + 1u == room && wanted > room) {
            snprintf(text, sizeof(text), "and %u more: A opens the list",
                     (unsigned)(wanted - listed));
            Say(frame, MARGIN, y + (listed + 1u) * LINE, DIM, text);
            break;
        }
        DrawCaseLine(frame, MARGIN, y + (listed + 1u) * LINE, record, columns);
        ++listed;
    }
}

/// The suites that failed, crashed or skipped, as one line: which is what the overview says
/// first.
static void ListGroups(char *out, size_t out_size, const UiRun *run, ReportResult which) {
    size_t used = strlen(out);
    const char *separator = "";
    for (uint32_t g = 0; g < run->group_count && used + 1u < out_size; ++g) {
        const UiGroup *const group = &run->groups[g];
        const uint32_t count = which == REPORT_FAIL      ? group->failed
                               : which == REPORT_CRASHED ? group->crashed
                                                         : group->skipped;
        if (count == 0u) {
            continue;
        }
        snprintf(out + used, out_size - used, "%s%s", separator, group->suite);
        used = strlen(out);
        separator = ", ";
    }
}

static void ComposeOverview(const UiFrame *frame, const UiRun *run, UiState state) {
    const ReportResult verdict = GroupResult(run->passed, run->failed);
    Say(frame, MARGIN, 8, ResultColour(verdict), ResultWord(verdict));
    char text[256];
    const uint32_t shown = run->passed + run->failed + run->skipped + run->crashed;
    snprintf(text, sizeof(text), "%u cases", (unsigned)shown);
    const uint32_t x = Say(frame, MARGIN + 6u * GLYPH, 8, TEXT, text) + 2u * GLYPH;
    SayCounts(frame, x, 8, run->passed, run->failed, run->skipped, run->crashed);

    if (run->failed > 0u) {
        snprintf(text, sizeof(text), "failed in: ");
        ListGroups(text, sizeof(text), run, REPORT_FAIL);
        SayFitted(frame, MARGIN, 30, FAILED, text);
    } else {
        const uint32_t after = Say(frame, MARGIN, 30, PASSED, "nothing failed");
        if (run->crashed > 0u) {
            snprintf(text, sizeof(text), "   crashed in: ");
            ListGroups(text, sizeof(text), run, REPORT_CRASHED);
            SayFitted(frame, after, 30, CRASHED, text);
        } else if (run->skipped > 0u) {
            snprintf(text, sizeof(text), "   skipped in: ");
            ListGroups(text, sizeof(text), run, REPORT_SKIP);
            SayFitted(frame, after, 30, DIM, text);
        }
    }
    // A run whose cases did not all reach the screens says so where the person is, rather than
    // leaving the counts above to disagree with the report's summary in silence.
    if (run->reported > shown) {
        char memory[48] = "";
        char suites[48] = "";
        if (run->reported > run->count) {
            snprintf(memory, sizeof(memory), " %u did not fit in memory",
                     (unsigned)(run->reported - run->count));
        }
        if (run->unplaced > 0u) {
            snprintf(suites, sizeof(suites), "%s %u are in suites past the first %u",
                     memory[0] != '\0' ? ";" : "", (unsigned)run->unplaced,
                     (unsigned)UI_MAX_GROUPS);
        }
        snprintf(text, sizeof(text), "the screens hold %u of the run's %u cases:%s%s",
                 (unsigned)shown, (unsigned)run->reported, memory, suites);
        UiText(frame, MARGIN, 52, 1, SKIPPED, text);
    }

    // Every family heading and every group as one list, scrolled to keep the cursor in view.
    const uint32_t rows = UiListRows(UI_LEVEL_OVERVIEW, frame->height);
    const uint32_t selected = UiOverviewRow(run, state.group);
    const uint32_t total = UiOverviewRow(run, run->group_count - 1u) + 1u;
    const uint32_t first = UiListFirst(selected, total, rows);
    uint32_t row = 0;
    for (uint32_t g = 0; g < run->group_count; ++g) {
        if (g == 0u || run->groups[g].family != run->groups[g - 1u].family) {
            if (row >= first && row < first + rows) {
                const uint32_t y = UI_LIST_TOP + (row - first) * UI_ROW_HEIGHT;
                const uint32_t end =
                    Say(frame, MARGIN, y + 2u, DIM, UiFamilyName(run->groups[g].family));
                if (frame->width > end + GLYPH + MARGIN) {
                    FillRect(frame, end + GLYPH, y + 10u, frame->width - MARGIN - end - GLYPH,
                             1u, BORDER);
                }
            }
            ++row;
        }
        if (row >= first && row < first + rows) {
            DrawGroupRow(frame, &run->groups[g], UI_LIST_TOP + (row - first) * UI_ROW_HEIGHT,
                         g == state.group, run->crashed > 0u);
        }
        ++row;
    }
    if (first > 0u) {
        UiText(frame, frame->width - MARGIN - 80u, UI_LIST_TOP - 10u, 1, DIM, "more above");
    }
    if (first + rows < total) {
        UiText(frame, frame->width - MARGIN - 80u, UI_LIST_TOP + rows * UI_ROW_HEIGHT + 2u, 1,
               DIM, "more below");
    }

    const uint32_t below = frame->height - FOOT - PREVIEW_LINES * LINE;
    FillRect(frame, MARGIN, below - 8u, frame->width - 2u * MARGIN, 1u, BORDER);
    ComposePreview(frame, run, state.group, below);
    DrawButtons(frame, "up/down group   left/right family   A open   R or ZR next group that "
                       "failed   L or ZL previous one   plus quit");
}

static void ComposeGroup(const UiFrame *frame, const UiRun *run, UiState state) {
    const UiGroup *const group = &run->groups[state.group];
    const ReportResult verdict = GroupResult(group->passed, group->failed);
    Say(frame, MARGIN, 8, ResultColour(verdict), ResultWord(verdict));
    const uint32_t x = Say(frame, MARGIN + 6u * GLYPH, 8, TEXT, group->suite) + 2u * GLYPH;
    SayCounts(frame, x, 8, group->passed, group->failed, group->skipped, group->crashed);

    const uint32_t rows = UiListRows(UI_LEVEL_GROUP, frame->height);
    const uint32_t first = UiListFirst(state.index, group->count, rows);
    const uint32_t last = first + rows < group->count ? first + rows : group->count;
    char text[160];
    int wrote = snprintf(text, sizeof(text), "%s family   group %u of %u   case %u of %u",
                         UiFamilyName(group->family), (unsigned)(state.group + 1u),
                         (unsigned)run->group_count, (unsigned)(state.index + 1u),
                         (unsigned)group->count);
    if (group->count > rows && wrote > 0 && (size_t)wrote < sizeof(text)) {
        snprintf(text + wrote, sizeof(text) - (size_t)wrote, "   showing %u to %u",
                 (unsigned)(first + 1u), (unsigned)last);
    }
    SayFitted(frame, MARGIN, 30, DIM, text);

    // Room at the end of the row for the word that says there are pictures to open.
    const uint32_t name_x = UI_MARK_LEFT + 5u * GLYPH;
    const uint32_t name_right = name_x + MARGIN + 4u * GLYPH;
    const uint32_t name_columns =
        frame->width > name_right ? (frame->width - name_right) / GLYPH : 1u;
    for (uint32_t i = first; i < last; ++i) {
        const ReportRecord *const record = UiCaseAt(run, state.group, i);
        const uint32_t y = UI_LIST_TOP + (i - first) * UI_ROW_HEIGHT;
        if (i == state.index) {
            FillRect(frame, MARGIN / 2u, y, frame->width - MARGIN, UI_ROW_HEIGHT, SELECTED);
            Say(frame, MARGIN, y + 2u, TEXT, ">");
        }
        DrawCaseLine(frame, UI_MARK_LEFT, y + 2u, record, name_columns);
        if (record->picture >= 0) {
            Say(frame, frame->width - MARGIN - 3u * GLYPH, y + 2u, DIM, "pic");
        }
    }

    // The selected case under the list: its whole name, as much of its detail as fits, and what
    // opening it will show.
    const ReportRecord *const record = UiCaseAt(run, state.group, state.index);
    const uint32_t below = frame->height - FOOT - DETAIL_LINES * LINE;
    FillRect(frame, MARGIN, below - 8u, frame->width - 2u * MARGIN, 1u, BORDER);
    const uint32_t columns = Columns(frame);
    const uint32_t named =
        TextWrapped(frame, MARGIN, below, BIG, LINE, TEXT, record->name, columns, 2u);
    const uint32_t room = DETAIL_LINES - 1u - named;
    const uint32_t detail_y = below + named * LINE;
    uint32_t needed = 0;
    if (record->detail[0] == '\0') {
        Say(frame, MARGIN, detail_y, DIM, NoDetail(record->result));
    } else {
        needed = UiWrappedLines(record->detail, columns);
        TextWrapped(frame, MARGIN, detail_y, BIG, LINE, DIM, record->detail, columns, room);
    }
    const char *const hint = record->picture >= 0 ? "A opens its pictures"
                             : needed > room      ? "the detail runs on: A shows all of it"
                                                  : "A opens it on a page of its own";
    Say(frame, MARGIN, below + (DETAIL_LINES - 1u) * LINE, DIM, hint);

    DrawButtons(frame, "up/down case   left/right group   A open   B back   R or ZR next "
                       "failure   L or ZL previous failure   plus quit");
}

/// The case's pictures, laid out as the detailed mode always has: the three side by side or
/// one alone, the colours of a verdict named under them, and the detail under that.
static void ComposePictures(const UiFrame *frame, const PictureRecord *record,
                            const ReportRecord *line, UiView view) {
    const uint32_t top = UI_PANEL_TOP;
    const uint32_t columns =
        frame->width > 2u * MARGIN ? (frame->width - 2u * MARGIN) / (CELL * 2u) : 1u;
    uint32_t bottom;

    if (view == UI_VIEW_ALL) {
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
        UiText(frame, x, top - 20u, 2, DIM, ViewLabel(view));
        DrawPanel(frame, x, top, scale, record, view);
        bottom = top + record->height * scale;
    }

    // The key sits between the pictures and the sentence, because it explains the one and is
    // read on the way to the other.
    DrawKey(frame, MARGIN, bottom + 12u);

    const uint32_t detail_y = bottom + 48u;
    const uint32_t room_for_lines =
        frame->height > detail_y + FOOT ? (frame->height - detail_y - FOOT) / LINE : 1u;
    if (line->detail[0] != '\0') {
        TextWrapped(frame, MARGIN, detail_y, 2, LINE, TEXT, line->detail, columns,
                    room_for_lines);
    } else {
        UiText(frame, MARGIN, detail_y, 2, DIM, NoDetail(line->result));
    }

    DrawButtons(frame, "left/right or up/down case   X view   R or ZR next failure   L or ZL "
                       "previous failure   B back   plus quit");
}

/// A case with no pictures, as a page: its whole name and its whole detail.
static void ComposeDetail(const UiFrame *frame, const UiGroup *group, const ReportRecord *line) {
    const uint32_t columns = Columns(frame);
    uint32_t y = 40u;
    y += TextWrapped(frame, MARGIN, y, BIG, PAGE_LINE, TEXT, line->name, columns, 3u) *
             PAGE_LINE +
         12u;
    Say(frame, MARGIN, y, DIM, Skipped(line->result) ? "why it was skipped" : "detail");
    y += PAGE_LINE;
    // One line kept back for the note below.
    const uint32_t room =
        frame->height > y + FOOT + 2u * PAGE_LINE ? (frame->height - y - FOOT) / PAGE_LINE - 1u
                                                  : 1u;
    if (line->detail[0] == '\0') {
        Say(frame, MARGIN, y, DIM, NoDetail(line->result));
        y += PAGE_LINE;
    } else {
        y += TextWrapped(frame, MARGIN, y, BIG, PAGE_LINE, TEXT, line->detail, columns, room) *
             PAGE_LINE;
    }
    // A drawing case with no pictures stopped before it had any, which is worth saying where a
    // person would otherwise look for them.
    if (group->family == UI_FAMILY_GRAPHICS && line->picture < 0) {
        Say(frame, MARGIN, y + 8u, DIM, "no pictures were kept for this case");
    }
    DrawButtons(frame, "left/right or up/down case   R or ZR next failure   L or ZL previous "
                       "failure   B back   plus quit");
}

static void ComposeCase(const UiFrame *frame, const UiRun *run, UiState state,
                        const PictureRecord *picture) {
    const UiGroup *const group = &run->groups[state.group];
    const ReportRecord *const line = UiCaseAt(run, state.group, state.index);

    // Where this case sits among its group's failures as well as among its cases, so a person
    // stepping through the failures knows how many are left.
    uint32_t rank = 0;
    for (uint32_t i = 0; i <= state.index; ++i) {
        if (CaseFailed(run, state.group, i)) {
            ++rank;
        }
    }
    char header[160];
    if (line->result == REPORT_FAIL) {
        snprintf(header, sizeof(header), "%s   case %u of %u   failure %u of %u here   tier %s",
                 group->suite, (unsigned)(state.index + 1u), (unsigned)group->count,
                 (unsigned)rank, (unsigned)group->failed, line->tier);
    } else {
        snprintf(header, sizeof(header), "%s   case %u of %u   %u failed here   tier %s",
                 group->suite, (unsigned)(state.index + 1u), (unsigned)group->count,
                 (unsigned)group->failed, line->tier);
    }
    // The verdict first, and at the margin, because it is the one thing on this screen that
    // must be there however narrow the frame is or however long the rest of the line runs.
    Say(frame, MARGIN, 8, ResultColour(line->result), ResultWord(line->result));
    SayFitted(frame, MARGIN + 6u * GLYPH, 8, DIM, header);

    if (picture != NULL) {
        char name[200];
        Fit(name, sizeof(name), line->name, Columns(frame));
        Say(frame, MARGIN, 30, TEXT, name);
        ComposePictures(frame, picture, line, state.view);
    } else {
        ComposeDetail(frame, group, line);
    }
}

void UiCompose(const UiFrame *frame, const UiRun *run, UiState state,
               const PictureRecord *picture) {
    if (frame == NULL || frame->pixels == NULL) {
        return;
    }
    FillRect(frame, 0, 0, frame->width, frame->height, BACKGROUND);
    // Every subtraction in the layout is from a frame at least this big; a smaller one would
    // wrap round and draw a line four thousand million pixels long.
    if (frame->width < UI_MIN_WIDTH || frame->height < UI_MIN_HEIGHT) {
        UiText(frame, 0, 0, 1, TEXT, "frame too small");
        return;
    }
    if (run == NULL || run->group_count == 0u) {
        Say(frame, MARGIN, MARGIN, TEXT, "no case was kept for the screens");
        if (run != NULL && run->reported > 0u) {
            char text[96];
            snprintf(text, sizeof(text), "the run reported %u, and none of them fit in memory",
                     (unsigned)run->reported);
            Say(frame, MARGIN, MARGIN + LINE + 4u, DIM, text);
        }
        DrawButtons(frame, "plus quit");
        return;
    }
    // A state from anywhere is drawn as the navigation would have left it, never read past the
    // end of a group.
    if (state.group >= run->group_count) {
        state.group = run->group_count - 1u;
    }
    if (state.index >= run->groups[state.group].count) {
        state.index = run->groups[state.group].count - 1u;
    }
    switch (state.level) {
    case UI_LEVEL_OVERVIEW:
        ComposeOverview(frame, run, state);
        break;
    case UI_LEVEL_GROUP:
        ComposeGroup(frame, run, state);
        break;
    case UI_LEVEL_CASE:
        ComposeCase(frame, run, state, picture);
        break;
    }
}

bool UiWaitsForPad(uint32_t frame_index, bool connected) {
    return !connected && frame_index < UI_PAD_WAIT_FRAMES;
}

bool UiLeavesUnattended(uint64_t elapsed_ns, bool pressed) {
    return !pressed && elapsed_ns >= UI_UNATTENDED_NANOSECONDS;
}

uint32_t UiUnattendedSecondsLeft(uint64_t elapsed_ns, bool pressed) {
    if (pressed || elapsed_ns >= UI_UNATTENDED_NANOSECONDS) {
        return 0;
    }
    const uint64_t left = UI_UNATTENDED_NANOSECONDS - elapsed_ns;
    return (uint32_t)((left + 999999999ull) / 1000000000ull);
}

void UiComposeCountdown(const UiFrame *frame, uint32_t seconds_left) {
    if (frame == NULL || frame->pixels == NULL || frame->height < 30u) {
        return;
    }
    // Between the last line anything above can reach, which stops 32 rows from the bottom, and
    // the button line, which starts 14 from it, so neither is ever drawn over.
    const uint32_t y = frame->height - 28u;
    FillRect(frame, 0, y - 2u, frame->width, 12u, BACKGROUND);
    if (seconds_left == 0u) {
        return;
    }
    char line[96];
    snprintf(line, sizeof(line),
             "nothing pressed yet: this screen closes by itself in %u s, any button keeps it",
             (unsigned)seconds_left);
    UiText(frame, MARGIN, y, 1, FAILED, line);
}

bool UiStartShown(const UiStartInfo *info) {
    return info != NULL && (info->may_crash_count > 0u || info->listed_count > 0u);
}

UiStartState UiStartOpen(bool may_crash, bool retry) {
    const UiStartState state = { may_crash, retry, false, true };
    return state;
}

UiStartState UiStartStep(UiStartState state, uint32_t pressed, uint32_t frame_index,
                         const UiStartInfo *info) {
    if (frame_index < UI_SETTLING_FRAMES || info == NULL) {
        return state;
    }
    // Y and X in one edge are not a choice. The first frames are ignored because a pad that
    // arrives with the window reports its whole mask as newly pressed; one that connects
    // after those frames does the same in a single later edge. That edge wrote
    // "chosen fatal retry" with no mode file, and the next launch ran the may-crash table
    // and the crash list. A still begins, and the two choices stay as they were.
    if ((pressed & UI_START_TOGGLE_MAY_CRASH) != 0u && (pressed & UI_START_TOGGLE_RETRY) != 0u) {
        pressed &= ~(UI_START_TOGGLE_MAY_CRASH | UI_START_TOGGLE_RETRY);
    }
    if ((pressed & UI_START_BEGIN) != 0u) {
        state.begun = true;
        state.changed = true;
        return state;
    }
    if ((pressed & UI_START_TOGGLE_MAY_CRASH) != 0u && info->may_crash_count > 0u) {
        state.may_crash = !state.may_crash;
        state.changed = true;
    }
    if ((pressed & UI_START_TOGGLE_RETRY) != 0u && info->listed_count > 0u) {
        state.retry = !state.retry;
        state.changed = true;
    }
    return state;
}

bool UiStartTimesOut(uint64_t elapsed_ns, bool pressed) {
    return !pressed && elapsed_ns >= UI_START_NANOSECONDS;
}

uint32_t UiStartSecondsLeft(uint64_t elapsed_ns, bool pressed) {
    if (pressed || elapsed_ns >= UI_START_NANOSECONDS) {
        return 0u;
    }
    return (uint32_t)((UI_START_NANOSECONDS - elapsed_ns + 999999999ull) / 1000000000ull);
}

uint32_t UiStartWarningTop(uint32_t frame_height) {
    // Above the countdown and the button line, which take the frame's last 32 rows.
    const uint32_t height = UI_START_WARNING_LINES * LINE + 16u;
    return frame_height > FOOT + height + 8u ? frame_height - FOOT - height - 8u : 0u;
}

/// One choice's row: its box, the button that turns it, and what it runs.
static void DrawChoice(const UiFrame *frame, uint32_t y, bool on, bool available,
                       const char *button, const char *text) {
    const uint8_t *const colour = !available ? DIM : on ? SKIPPED : TEXT;
    uint32_t x = Say(frame, MARGIN, y, colour, on ? "[x]" : "[ ]");
    x = Say(frame, x + GLYPH, y, colour, button);
    SayFitted(frame, x + 2u * GLYPH, y, colour, text);
}

/// A list under its heading, in @p room lines at most: its heading, as many names as fit, and a
/// line saying how many more there are when not all of them do.
static uint32_t DrawStartList(const UiFrame *frame, uint32_t y, uint32_t room,
                              const char *heading, uint32_t count, const char *const *suites,
                              const char *const *names) {
    if (count == 0u || room < 2u) {
        return y;
    }
    Say(frame, MARGIN, y, DIM, heading);
    y += LINE;
    --room;
    const uint32_t held = count < UI_START_LIST_MAX ? count : UI_START_LIST_MAX;
    const uint32_t fits = held <= room && held == count ? held : (room > 1u ? room - 1u : 0u);
    const uint32_t shown = fits < held ? fits : held;
    const uint32_t columns = Columns(frame) > 13u ? Columns(frame) - 13u : 1u;
    for (uint32_t i = 0; i < shown; ++i) {
        char suite[16];
        Fit(suite, sizeof(suite), suites[i], 10u);
        Say(frame, MARGIN + 2u * GLYPH, y, DIM, suite);
        char name[200];
        Fit(name, sizeof(name), names[i], columns);
        Say(frame, MARGIN + 13u * GLYPH, y, TEXT, name);
        y += LINE;
    }
    if (shown < count) {
        char more[48];
        snprintf(more, sizeof(more), "and %u more", (unsigned)(count - shown));
        Say(frame, MARGIN + 2u * GLYPH, y, DIM, more);
        y += LINE;
    }
    return y;
}

void UiComposeStart(const UiFrame *frame, const UiStartInfo *info, UiStartState state,
                    uint32_t seconds_left) {
    if (frame == NULL || frame->pixels == NULL || info == NULL) {
        return;
    }
    FillRect(frame, 0, 0, frame->width, frame->height, BACKGROUND);
    if (frame->width < UI_MIN_WIDTH || frame->height < UI_MIN_HEIGHT) {
        UiText(frame, 0, 0, 1, TEXT, "frame too small");
        return;
    }
    SayFitted(frame, MARGIN, 8, TEXT, info->heading != NULL ? info->heading : "edenprobe");
    SayFitted(frame, MARGIN, 30, DIM,
              "before the run: these cases have ended an emulator's program");

    char text[96];
    snprintf(text, sizeof(text), "run the %u case%s that may crash",
             (unsigned)info->may_crash_count, info->may_crash_count == 1u ? "" : "s");
    DrawChoice(frame, UI_LIST_TOP, state.may_crash, info->may_crash_count > 0u, "Y", text);
    snprintf(text, sizeof(text), "run the crash list's %u case%s again",
             (unsigned)info->listed_count, info->listed_count == 1u ? "" : "s");
    DrawChoice(frame, UI_LIST_TOP + UI_ROW_HEIGHT, state.retry, info->listed_count > 0u, "X",
               text);

    // The two lists in what is left above the warning's place, which is kept free whether or
    // not the warning shows, so turning a choice on moves nothing else.
    const uint32_t warning_top = UiStartWarningTop(frame->height);
    uint32_t y = UI_LIST_TOP + 3u * UI_ROW_HEIGHT;
    const uint32_t rows = warning_top > y + 8u ? (warning_top - y - 8u) / LINE : 0u;
    const uint32_t first_room = info->listed_count > 0u ? rows / 2u : rows;
    const uint32_t after =
        DrawStartList(frame, y, first_room, "may crash (the mode word fatal runs them):",
                      info->may_crash_count, info->may_crash_suite, info->may_crash_name);
    const uint32_t used = (after - y) / LINE;
    DrawStartList(frame, after, rows > used ? rows - used : 0u,
                  "on the crash list (the mode word retry runs them):", info->listed_count,
                  info->listed_suite, info->listed_name);

    if (state.may_crash || state.retry) {
        static const char *const WARNING[UI_START_WARNING_LINES] = {
            "these cases ended an emulator's program; this one may end too",
            "if it does, start edenprobe again: the run goes on after the case",
            "that ended it, which is then skipped as crashed and listed",
        };
        const uint32_t height = UI_START_WARNING_LINES * LINE + 16u;
        FillRect(frame, MARGIN / 2u, warning_top, frame->width - MARGIN, height, FAILED);
        FillRect(frame, MARGIN / 2u + 2u, warning_top + 2u, frame->width - MARGIN - 4u,
                 height - 4u, BACKGROUND);
        for (uint32_t i = 0; i < UI_START_WARNING_LINES; ++i) {
            SayFitted(frame, MARGIN, warning_top + 8u + i * LINE, FAILED, WARNING[i]);
        }
    }

    const uint32_t countdown = frame->height - 28u;
    FillRect(frame, 0, countdown - 2u, frame->width, 12u, BACKGROUND);
    if (seconds_left > 0u) {
        char line[96];
        snprintf(line, sizeof(line),
                 "nothing pressed yet: the run begins in %u s as chosen, any button keeps this",
                 (unsigned)seconds_left);
        UiText(frame, MARGIN, countdown, 1, FAILED, line);
    }
    DrawButtons(frame, "A begin the run   Y the cases that may crash   X the crash list again");
}
