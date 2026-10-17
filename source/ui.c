// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "font8x8.h"
#include "padref.h"
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
/// not holding the docs, and red is the most important thing on this display.
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
    { "pad", UI_FAMILY_PAD },
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
    { "fill", UI_FAMILY_GRAPHICS },   { "ring", UI_FAMILY_GRAPHICS },
    { "occlusion", UI_FAMILY_GRAPHICS }, { "drawtex", UI_FAMILY_GRAPHICS },
    { "geometry", UI_FAMILY_GRAPHICS },
    { "query", UI_FAMILY_GRAPHICS },  { "dma", UI_FAMILY_GRAPHICS },
    { "fs", UI_FAMILY_FS },           { "sched", UI_FAMILY_KERNEL },
    { "vic", UI_FAMILY_VIDEO },       { "nvdec", UI_FAMILY_VIDEO },
    { "audio", UI_FAMILY_AUDIO },
    { "nifm", UI_FAMILY_NET },
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
    case UI_FAMILY_PAD:
        return "pad";
    case UI_FAMILY_GRAPHICS:
        return "graphics";
    case UI_FAMILY_FS:
        return "fs";
    case UI_FAMILY_KERNEL:
        return "kernel";
    case UI_FAMILY_VIDEO:
        return "video";
    case UI_FAMILY_AUDIO:
        return "audio";
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

_Static_assert(UI_MAX_GROUPS < 0xFFu, "0xFF marks a case with no group");

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
    return info != NULL && ((info->families_present & UI_START_FAMILIES) != 0u ||
                            info->may_crash_count > 0u || info->listed_count > 0u);
}

UiStartState UiStartOpen(const RunMode *from_file, uint32_t present) {
    UiStartState state;
    memset(&state, 0, sizeof(state));
    state.cursor = UI_START_ROW_BEGIN;
    state.changed = true;
    if (from_file != NULL) {
        state.may_crash = from_file->fatal;
        state.retry = from_file->retry;
        state.families = ModeFamiliesToRun(from_file, present) & UI_START_FAMILIES;
        state.listen = from_file->listen;
        state.quick = !from_file->detailed;
        state.no_timing = from_file->no_timing;
    }
    return state;
}

/// Whether a may-crash entry of @p family is reached by a run of @p families: the cpu suite's
/// always, since it runs in every mode.
static bool MayCrashReached(uint32_t family, uint32_t families) {
    return family == 0u || family == MODE_FAMILY_CPU || (family & families) != 0u;
}

uint32_t UiStartMayCrashCount(const UiStartInfo *info, const UiStartState *state) {
    if (info == NULL || state == NULL) {
        return 0u;
    }
    uint32_t count = 0;
    const uint32_t held =
        info->may_crash_count < UI_START_LIST_MAX ? info->may_crash_count : UI_START_LIST_MAX;
    for (uint32_t i = 0; i < held; ++i) {
        count += MayCrashReached(info->may_crash_family[i], state->families) ? 1u : 0u;
    }
    return count;
}

/// The family a row turns on and off, or nought for a row that is not a family.
static uint32_t RowFamily(uint32_t row) {
    switch (row) {
    case UI_START_ROW_GRAPHICS:
        return MODE_FAMILY_GRAPHICS;
    case UI_START_ROW_FS:
        return MODE_FAMILY_FS;
    case UI_START_ROW_AUDIO:
    case UI_START_ROW_LISTEN:
        return MODE_FAMILY_AUDIO;
    case UI_START_ROW_NET:
        return MODE_FAMILY_NET;
    case UI_START_ROW_PAD:
        return MODE_FAMILY_PAD;
    default:
        return 0u;
    }
}

/// Whether a row can be turned on: a family this build has, a list with something in it.
static bool RowAvailable(const UiStartInfo *info, const UiStartState *state, uint32_t row) {
    const uint32_t family = RowFamily(row);
    if (family != 0u) {
        return (info->families_present & family) != 0u;
    }
    if (row == UI_START_ROW_MAY_CRASH) {
        return UiStartMayCrashCount(info, state) > 0u;
    }
    if (row == UI_START_ROW_RETRY) {
        return info->listed_count > 0u;
    }
    return row < UI_START_ROWS;
}

static bool RowOn(const UiStartState *state, uint32_t row) {
    switch (row) {
    case UI_START_ROW_GRAPHICS:
    case UI_START_ROW_FS:
    case UI_START_ROW_AUDIO:
    case UI_START_ROW_NET:
    case UI_START_ROW_PAD:
        return (state->families & RowFamily(row)) != 0u;
    case UI_START_ROW_LISTEN:
        return state->listen;
    case UI_START_ROW_QUICK:
        return state->quick;
    case UI_START_ROW_NOTIME:
        return state->no_timing;
    case UI_START_ROW_MAY_CRASH:
        return state->may_crash;
    case UI_START_ROW_RETRY:
        return state->retry;
    case UI_START_ROW_SAVE:
        return state->save;
    default:
        return false;
    }
}

static void Toggle(UiStartState *state, const UiStartInfo *info, uint32_t row) {
    if (!RowAvailable(info, state, row)) {
        return;
    }
    switch (row) {
    case UI_START_ROW_GRAPHICS:
    case UI_START_ROW_FS:
    case UI_START_ROW_NET:
    case UI_START_ROW_PAD:
        state->families ^= RowFamily(row);
        break;
    case UI_START_ROW_AUDIO:
        // The sequence plays before the audio suite's cases, so it goes when they do.
        state->families ^= MODE_FAMILY_AUDIO;
        state->listen = state->listen && (state->families & MODE_FAMILY_AUDIO) != 0u;
        break;
    case UI_START_ROW_LISTEN:
        state->listen = !state->listen;
        state->families |= state->listen ? MODE_FAMILY_AUDIO : 0u;
        break;
    case UI_START_ROW_QUICK:
        state->quick = !state->quick;
        break;
    case UI_START_ROW_NOTIME:
        state->no_timing = !state->no_timing;
        break;
    case UI_START_ROW_MAY_CRASH:
        state->may_crash = !state->may_crash;
        break;
    case UI_START_ROW_RETRY:
        state->retry = !state->retry;
        break;
    case UI_START_ROW_SAVE:
        state->save = !state->save;
        break;
    default:
        return;
    }
    state->changed = true;
}

UiStartState UiStartStep(UiStartState state, uint32_t pressed, uint32_t frame_index,
                         const UiStartInfo *info) {
    if (frame_index < UI_SETTLING_FRAMES || info == NULL || state.begun) {
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
    const bool select = (pressed & UI_START_SELECT) != 0u;
    if ((pressed & UI_START_BEGIN) != 0u || (select && state.cursor == UI_START_ROW_BEGIN)) {
        state.begun = true;
        state.changed = true;
        return state;
    }
    const uint32_t moves = pressed & (UI_START_UP | UI_START_DOWN);
    if (select && moves != 0u) {
        // The same whole mask, away from begin the run: nobody chose that row.
    } else if (select) {
        Toggle(&state, info, state.cursor);
    } else if (moves == UI_START_UP || moves == UI_START_DOWN) {
        state.cursor = Around(state.cursor, moves == UI_START_UP ? -1 : 1, UI_START_ROWS);
        state.changed = true;
    }
    if ((pressed & UI_START_TOGGLE_MAY_CRASH) != 0u) {
        Toggle(&state, info, UI_START_ROW_MAY_CRASH);
    }
    if ((pressed & UI_START_TOGGLE_RETRY) != 0u) {
        Toggle(&state, info, UI_START_ROW_RETRY);
    }
    return state;
}

RunMode UiStartMode(const UiStartState *state, RunMode from_file, uint32_t present) {
    RunMode mode = from_file;
    if (state == NULL) {
        return mode;
    }
    mode.fatal = state->may_crash;
    mode.retry = state->retry;
    mode.fatal_by_screen = mode.fatal && !from_file.fatal;
    mode.retry_by_screen = mode.retry && !from_file.retry;
    const uint32_t file_families = ModeFamiliesToRun(&from_file, present) & UI_START_FAMILIES;
    const uint32_t families = state->families & UI_START_FAMILIES & present;
    const bool families_changed = families != file_families || state->listen != from_file.listen;
    if (families_changed) {
        // Named whole, cpu with them, so even a run of nothing but cpu has a word to keep; a
        // family the screen has no row for stays as the file named it.
        mode.families_named =
            families | MODE_FAMILY_CPU | (from_file.families_named & ~UI_START_FAMILIES);
        mode.listen = state->listen;
    }
    const bool timing_changed = state->no_timing != from_file.no_timing;
    mode.no_timing = state->no_timing;
    const bool quick_changed = state->quick != !from_file.detailed;
    if (quick_changed) {
        mode.quick = state->quick;
        mode.detailed = !state->quick;
        mode.detailed_by_button = false;
    }
    mode.chosen_on_screen = families_changed || timing_changed || quick_changed;
    mode.saved_by_screen = state->save;
    return mode;
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

uint32_t UiStartRowTop(uint32_t row) {
    return UI_LIST_TOP + row * UI_ROW_HEIGHT;
}

// What each row is called and what it does, in the order of UiStartRow; the two lists' rows say
// how many cases they hold, so theirs are written where they are drawn.
static const struct {
    const char *name;
    const char *what;
} START_ROWS[UI_START_ROWS] = {
    { "begin the run", "A here, or plus from any row" },
    { "graphics", "the drawing suites, clear to dma" },
    { "fs", "reads from the memory card, timed" },
    { "audio", "the audio renderer, nearly silent" },
    { "listen", "about 40 s of sound to hear, before the audio suite" },
    { "net", "the online services, against sdmc:/edenprobe/net" },
    { "pad", "press what the screen names, first thing: needs a person" },
    { "quick", "leave when the report closes, with no results screens" },
    { "notime", "each drawing suite once, with no timing passes" },
    { "may crash", NULL },
    { "crash list", NULL },
    { "save", "write these choices into sdmc:/edenprobe/mode" },
};

// Across the row: the cursor, the box, the name, and what it does.
#define START_BOX_COLUMN 2u
#define START_NAME_COLUMN 6u
#define START_WHAT_COLUMN 18u

/// One row of the menu.
static void DrawStartRow(const UiFrame *frame, uint32_t row, const char *what, bool selected,
                         bool available, bool on) {
    const uint32_t y = UiStartRowTop(row);
    if (selected) {
        FillRect(frame, MARGIN / 2u, y, frame->width - MARGIN, UI_ROW_HEIGHT, SELECTED);
        Say(frame, MARGIN, y + 2u, TEXT, ">");
    }
    const bool risky = row == UI_START_ROW_MAY_CRASH || row == UI_START_ROW_RETRY;
    if (row == UI_START_ROW_BEGIN) {
        Say(frame, MARGIN + START_BOX_COLUMN * GLYPH, y + 2u, TEXT, START_ROWS[row].name);
    } else {
        const uint8_t *const box = !available ? DIM : on ? (risky ? SKIPPED : PASSED) : DIM;
        Say(frame, MARGIN + START_BOX_COLUMN * GLYPH, y + 2u, box, on ? "[x]" : "[ ]");
        Say(frame, MARGIN + START_NAME_COLUMN * GLYPH, y + 2u, available ? TEXT : DIM,
            START_ROWS[row].name);
    }
    SayFitted(frame, MARGIN + START_WHAT_COLUMN * GLYPH, y + 2u, DIM, what);
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
              "before the run: choose what runs, or let the countdown begin it");

    // The may-crash cases the run as chosen reaches, which is what its row counts and lists.
    const char *suites[UI_START_LIST_MAX];
    const char *names[UI_START_LIST_MAX];
    uint32_t reached = 0;
    const uint32_t held =
        info->may_crash_count < UI_START_LIST_MAX ? info->may_crash_count : UI_START_LIST_MAX;
    for (uint32_t i = 0; i < held; ++i) {
        if (MayCrashReached(info->may_crash_family[i], state.families)) {
            suites[reached] = info->may_crash_suite[i];
            names[reached] = info->may_crash_name[i];
            ++reached;
        }
    }
    for (uint32_t row = 0; row < UI_START_ROWS; ++row) {
        char what[96];
        if (row == UI_START_ROW_MAY_CRASH) {
            snprintf(what, sizeof(what), "the %u case%s that may crash; Y from any row",
                     (unsigned)reached, reached == 1u ? "" : "s");
        } else if (row == UI_START_ROW_RETRY) {
            snprintf(what, sizeof(what), "the crash list's %u case%s again; X from any row",
                     (unsigned)info->listed_count, info->listed_count == 1u ? "" : "s");
        } else {
            snprintf(what, sizeof(what), "%s", START_ROWS[row].what);
        }
        DrawStartRow(frame, row, what, state.cursor == row, RowAvailable(info, &state, row),
                     RowOn(&state, row));
    }
    const uint32_t note = UiStartRowTop(UI_START_ROWS) + 4u;
    SayFitted(frame, MARGIN, note, DIM, "the judge and the cpu suite run whatever is chosen");

    // The two lists in what is left above the warning's place, which is kept free whether or
    // not the warning shows, so turning a choice on moves nothing else.
    const uint32_t warning_top = UiStartWarningTop(frame->height);
    const uint32_t y = note + 2u * LINE;
    const uint32_t rows = warning_top > y + 8u ? (warning_top - y - 8u) / LINE : 0u;
    const uint32_t first_room = info->listed_count > 0u ? rows / 2u : rows;
    const uint32_t after = DrawStartList(frame, y, first_room,
                                         "may crash (the mode word fatal runs them):", reached,
                                         suites, names);
    const uint32_t used = (after - y) / LINE;
    DrawStartList(frame, after, rows > used ? rows - used : 0u,
                  "on the crash list (the mode word retry runs them):", info->listed_count,
                  info->listed_suite, info->listed_name);

    if ((state.may_crash && reached > 0u) || (state.retry && info->listed_count > 0u)) {
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
        char line[128];
        snprintf(line, sizeof(line),
                 "nothing pressed yet: the run begins in %u s as the mode file says, any button "
                 "keeps this",
                 (unsigned)seconds_left);
        UiText(frame, MARGIN, countdown, 1, FAILED, line);
    }
    DrawButtons(frame, "up/down move   A choose, or begin on its row   plus begin   Y may crash   "
                       "X crash list");
}

UiResumeState UiResumeOpen(void) {
    const UiResumeState state = { UI_RESUME_GO_ON, false, false, true };
    return state;
}

UiResumeState UiResumeStep(UiResumeState state, uint32_t pressed, uint32_t frame_index) {
    if (frame_index < UI_SETTLING_FRAMES || state.decided) {
        return state;
    }
    const uint32_t moves = pressed & (UI_RESUME_UP | UI_RESUME_DOWN);
    if ((pressed & UI_RESUME_CHOOSE) != 0u) {
        // With a move in the same edge it is a whole mask, not a choice; see the header.
        if (moves == 0u) {
            state.decided = true;
            state.start_over = state.cursor == UI_RESUME_START_OVER;
            state.changed = true;
        }
        return state;
    }
    if (moves == UI_RESUME_UP) {
        state.cursor = Around(state.cursor, -1, UI_RESUME_CHOICES);
        state.changed = true;
    } else if (moves == UI_RESUME_DOWN) {
        state.cursor = Around(state.cursor, 1, UI_RESUME_CHOICES);
        state.changed = true;
    }
    return state;
}

bool UiResumeTimesOut(uint64_t elapsed_ns, bool pressed) {
    return !pressed && elapsed_ns >= UI_RESUME_NANOSECONDS;
}

uint32_t UiResumeSecondsLeft(uint64_t elapsed_ns, bool pressed) {
    if (pressed || elapsed_ns >= UI_RESUME_NANOSECONDS) {
        return 0u;
    }
    return (uint32_t)((UI_RESUME_NANOSECONDS - elapsed_ns + 999999999ull) / 1000000000ull);
}

// Below the sentence of where it stopped, which takes up to three lines, and the three lines of
// how far the run got, with a line between.
#define RESUME_WHERE_LINES 3u
#define RESUME_ROWS_TOP (UI_LIST_TOP + (RESUME_WHERE_LINES + 4u) * LINE)

uint32_t UiResumeRowTop(uint32_t row) {
    return RESUME_ROWS_TOP + row * 2u * LINE;
}

void UiComposeResume(const UiFrame *frame, const UiResumeInfo *info, UiResumeState state,
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
    SayFitted(frame, MARGIN, 30, SKIPPED, "the last run in this folder stopped before its end");

    TextWrapped(frame, MARGIN, UI_LIST_TOP, BIG, LINE, TEXT,
                info->where != NULL && info->where[0] != '\0' ? info->where
                                                              : "where it stopped is not known",
                Columns(frame), RESUME_WHERE_LINES);
    uint32_t y = UI_LIST_TOP + RESUME_WHERE_LINES * LINE;
    char text[160];
    snprintf(text, sizeof(text), "this is launch %u of the run; %u of its %u steps are done",
             (unsigned)info->launch, (unsigned)info->steps_done, (unsigned)info->steps);
    SayFitted(frame, MARGIN, y, DIM, text);
    y += LINE;
    const uint32_t x = Say(frame, MARGIN, y, DIM, "so far: ");
    SayCounts(frame, x, y, info->passed, info->failed, info->skipped, info->crashed);
    y += LINE;
    snprintf(text, sizeof(text), "folder: %s", info->folder != NULL ? info->folder : "");
    SayFitted(frame, MARGIN, y, DIM, text);

    static const char *const CHOICES[UI_RESUME_CHOICES][2] = {
        { "go on from where it stopped",
          "the report keeps what it has, and a case the program ended inside is skipped" },
        { "start over",
          "a new report in this folder; a case the program ended inside stays on the crash list" },
    };
    for (uint32_t row = 0; row < UI_RESUME_CHOICES; ++row) {
        const uint32_t top = UiResumeRowTop(row);
        const bool selected = state.cursor == row;
        if (selected) {
            FillRect(frame, MARGIN / 2u, top, frame->width - MARGIN, 2u * LINE, SELECTED);
            Say(frame, MARGIN, top + 2u, TEXT, ">");
        }
        SayFitted(frame, MARGIN + 2u * GLYPH, top + 2u, selected ? TEXT : DIM, CHOICES[row][0]);
        UiText(frame, MARGIN + 2u * GLYPH, top + LINE + 4u, 1, DIM, CHOICES[row][1]);
    }

    const uint32_t countdown = frame->height - 28u;
    FillRect(frame, 0, countdown - 2u, frame->width, 12u, BACKGROUND);
    if (seconds_left > 0u) {
        snprintf(text, sizeof(text),
                 "nothing pressed yet: the run goes on in %u s, any button keeps this screen",
                 (unsigned)seconds_left);
        UiText(frame, MARGIN, countdown, 1, FAILED, text);
    }
    DrawButtons(frame, "up/down choose   A confirm");
}

bool UiProgressDue(bool drawn_before, uint64_t drawn_ns, uint64_t now_ns, bool step_began) {
    return !drawn_before || step_began || now_ns < drawn_ns ||
           now_ns - drawn_ns >= UI_PROGRESS_NANOSECONDS;
}

void UiProgressCountReset(UiProgressCount *count) {
    if (count != NULL) {
        memset(count, 0, sizeof(*count));
    }
}

void UiProgressCountCase(UiProgressCount *count, uint32_t pass, bool started) {
    if (count == NULL) {
        return;
    }
    if (pass != count->pass) {
        // A pass begins with its first case: the one before it counted every case there is.
        count->previous = count->count;
        count->count = 0u;
        count->pass = pass;
        count->open = false;
    }
    if (started || !count->open) {
        ++count->count;
    }
    count->open = started;
}

size_t UiProgressPassWords(char *out, size_t out_size, uint32_t pass, uint32_t passes) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (pass == 0u || passes == 0u) {
        return 0;
    }
    const char *const what = pass == 1u        ? "the warm-up"
                             : pass < passes ? "a rehearsal, timed"
                                             : "the verdict pass, timed and reported";
    snprintf(out, out_size, "pass %u of %u: %s", (unsigned)pass, (unsigned)passes, what);
    return strlen(out);
}

size_t UiElapsedWords(char *out, size_t out_size, uint64_t ns) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    const uint64_t seconds = ns / 1000000000ull;
    if (seconds < 60u) {
        snprintf(out, out_size, "%u s", (unsigned)seconds);
    } else if (seconds < 3600u) {
        snprintf(out, out_size, "%u min %u s", (unsigned)(seconds / 60u),
                 (unsigned)(seconds % 60u));
    } else {
        snprintf(out, out_size, "%u h %u min", (unsigned)(seconds / 3600u),
                 (unsigned)(seconds % 3600u / 60u));
    }
    return strlen(out);
}

/// The steps done and the one under way, in proportion, across the frame below the step line:
/// the running screen's bar, which the listening screen draws too.
static void DrawStepsBar(const UiFrame *frame, uint32_t step, uint32_t steps) {
    const uint32_t width = frame->width - 2u * MARGIN;
    FillRect(frame, MARGIN, UI_PROGRESS_BAR_TOP, width, UI_PROGRESS_BAR_HEIGHT, BORDER);
    if (steps > 0u && step > 0u) {
        const uint32_t done = (uint32_t)((uint64_t)width * (step - 1u) / steps);
        const uint32_t under_way = (uint32_t)((uint64_t)width * step / steps) - done;
        FillRect(frame, MARGIN, UI_PROGRESS_BAR_TOP, done, UI_PROGRESS_BAR_HEIGHT, PASSED);
        FillRect(frame, MARGIN + done, UI_PROGRESS_BAR_TOP, under_way, UI_PROGRESS_BAR_HEIGHT,
                 DIM);
    }
}

void UiComposeProgress(const UiFrame *frame, const UiProgress *progress) {
    if (frame == NULL || frame->pixels == NULL || progress == NULL) {
        return;
    }
    FillRect(frame, 0, 0, frame->width, frame->height, BACKGROUND);
    if (frame->width < UI_MIN_WIDTH || frame->height < UI_MIN_HEIGHT) {
        UiText(frame, 0, 0, 1, TEXT, "frame too small");
        return;
    }
    // The results screens' header: one word at the margin, the program after it, the counts on
    // the second row.
    Say(frame, MARGIN, 8, TEXT, "running");
    SayFitted(frame, MARGIN + 9u * GLYPH, 8, DIM,
              progress->heading != NULL ? progress->heading : "edenprobe");
    const uint32_t x = Say(frame, MARGIN, 30, DIM, "so far: ");
    SayCounts(frame, x, 30, progress->passed, progress->failed, progress->skipped,
              progress->crashed);

    char text[200];
    const uint32_t step = progress->step < progress->steps ? progress->step : progress->steps;
    snprintf(text, sizeof(text), "step %u of %u: %s", (unsigned)step, (unsigned)progress->steps,
             progress->step_name != NULL ? progress->step_name : "");
    SayFitted(frame, MARGIN, UI_LIST_TOP, TEXT, text);

    DrawStepsBar(frame, step, progress->steps);

    uint32_t y = UI_PROGRESS_BAR_TOP + UI_PROGRESS_BAR_HEIGHT + 16u;
    if (UiProgressPassWords(text, sizeof(text), progress->pass, progress->passes) > 0u) {
        SayFitted(frame, MARGIN, y, DIM, text);
    }
    y += LINE;
    if (progress->name != NULL && progress->case_number > 0u) {
        if (progress->case_total >= progress->case_number) {
            snprintf(text, sizeof(text), "case %u of %u", (unsigned)progress->case_number,
                     (unsigned)progress->case_total);
        } else {
            snprintf(text, sizeof(text), "case %u", (unsigned)progress->case_number);
        }
        // The suite as the case's line names it, when it is not the step's own name: the net
        // step's six suites.
        const bool other = progress->suite != NULL && progress->step_name != NULL &&
                           strcmp(progress->suite, progress->step_name) != 0;
        if (other) {
            const size_t used = strlen(text);
            snprintf(text + used, sizeof(text) - used, ", in %s", progress->suite);
        }
        SayFitted(frame, MARGIN, y, DIM, text);
        TextWrapped(frame, MARGIN, y + LINE, BIG, LINE, TEXT, progress->name, Columns(frame), 2u);
    }
    y += 3u * LINE + 8u;
    char elapsed[32];
    UiElapsedWords(elapsed, sizeof(elapsed), progress->elapsed_ns);
    snprintf(text, sizeof(text), "running for %s", elapsed);
    SayFitted(frame, MARGIN, y, DIM, text);
    snprintf(text, sizeof(text), "folder: %s", progress->folder != NULL ? progress->folder : "");
    SayFitted(frame, MARGIN, y + LINE, DIM, text);

    DrawButtons(frame, progress->quick
                           ? "nothing to press: the program leaves when the run ends; this screen "
                             "is drawn between cases"
                           : "nothing to press: the results come up when the run ends; this "
                             "screen is drawn between cases");
}

// The listening sequence, one row per screen, in the order a run with no button plays them.
// The guide is what a person is listening for; the log lines stay the sequence's own.
static const char *const LISTEN_A_LOW =
    "duller than the high beep, most in the hiss and the top of the sweep, means default "
    "quality is low-passing. the same sound means it is not.";
static const char *const LISTEN_A_HIGH =
    "the control. compare its hiss with the take before it.";
static const char *const LISTEN_B_PHRASE =
    "three low notes on the left, then three high notes on the right. the sink records them.";
static const char *const LISTEN_B_LEFT =
    "the low notes again means the sink recorded them. silence means it recorded nothing.";
static const char *const LISTEN_B_RIGHT =
    "the high notes again, on the right. silence means nothing was recorded.";
static const char *const LISTEN_C_LOW =
    "where mode 1 is read from its byte, this take is the quieter one. the same volume as "
    "mode 1 means the mode is ignored.";
static const char *const LISTEN_C_HIGH =
    "about 7 dB louder than mode 0 where the mode byte is read. the same volume means it is not.";
static const char *const LISTEN_REPEAT = "Y repeat   X next part";
static const char *const LISTEN_REPEAT_TAKE = "Y repeat this take   X next part";

static const struct {
    const char *mark;
    const char *name;
    const char *take;
    const char *guide;
    bool numbered;
    bool peaks;
} LISTEN[] = {
    { NULL, "listening", "three rising notes",
      "then part a, the resampler; part b, the circular buffer sink; part c, the limiter.",
      false, false },
    { "a", "resampler", "one beep, then four takes",
      "each take is noise bursts, a sweep to 16 kHz and a second of hiss.", false, false },
    { "a", "resampler", "take 1 of 4, low beep: default quality", LISTEN_A_LOW, true, false },
    { "a", "resampler", "take 2 of 4, high beep: high quality", LISTEN_A_HIGH, true, false },
    { "a", "resampler", "take 3 of 4, low beep: default quality", LISTEN_A_LOW, true, false },
    { "a", "resampler", "take 4 of 4, high beep: high quality", LISTEN_A_HIGH, true, false },
    { "b", "circular buffer sink", "two beeps, then the phrase", LISTEN_B_PHRASE, false, false },
    { "b", "circular buffer sink", "recording",
      "low notes on the left, then high notes on the right.", false, false },
    { "b", "circular buffer sink", "low beep: left channel of the recording, on the left",
      LISTEN_B_LEFT, true, true },
    { "b", "circular buffer sink", "high beep: right channel of the recording, on the right",
      LISTEN_B_RIGHT, true, true },
    { "c", "limiter", "three beeps, then four takes",
      "three notes through the limiter, threshold 0.2.", false, false },
    { "c", "limiter", "take 1 of 4, low beep: mode 0", LISTEN_C_LOW, true, false },
    { "c", "limiter", "take 2 of 4, high beep: mode 1", LISTEN_C_HIGH, true, false },
    { "c", "limiter", "take 3 of 4, low beep: mode 0", LISTEN_C_LOW, true, false },
    { "c", "limiter", "take 4 of 4, high beep: mode 1", LISTEN_C_HIGH, true, false },
    { NULL, "listening", "three falling notes", "the cases that follow are nearly silent.",
      false, false },
};

_Static_assert(sizeof(LISTEN) / sizeof(LISTEN[0]) == UI_LISTEN_CLOSING + 1u,
               "one row per listening screen");

UiListenInput UiListenInputFromButtons(uint32_t buttons, uint32_t frame_index) {
    if (frame_index < UI_SETTLING_FRAMES) {
        return UI_LISTEN_NONE;
    }
    // Moving on wins over hearing it again, so a mask with both does one thing.
    if ((buttons & UI_BUTTON_LISTEN_SKIP) != 0u) {
        return UI_LISTEN_SKIP;
    }
    if ((buttons & UI_BUTTON_LISTEN_REPEAT) != 0u) {
        return UI_LISTEN_REPEAT;
    }
    return UI_LISTEN_NONE;
}

void UiListenCopyOf(UiListenScreen screen, double left, double right, UiListenCopy *text,
                    char *guide, size_t guide_size) {
    if (text == NULL) {
        return;
    }
    if ((unsigned)screen >= sizeof(LISTEN) / sizeof(LISTEN[0])) {
        text->mark = NULL;
        text->name = "";
        text->take = "";
        text->guide = "";
        text->buttons = LISTEN_REPEAT;
        return;
    }
    text->mark = LISTEN[screen].mark;
    text->name = LISTEN[screen].name;
    text->take = LISTEN[screen].take;
    text->buttons = LISTEN[screen].numbered ? LISTEN_REPEAT_TAKE : LISTEN_REPEAT;
    text->guide = LISTEN[screen].guide;
    if (LISTEN[screen].peaks && guide != NULL && guide_size > 0u) {
        snprintf(guide, guide_size, "%s peak %.3f on the left and %.3f on the right (0 is silence).",
                 LISTEN[screen].guide, left, right);
        text->guide = guide;
    }
}

UiListenScreen UiListenNext(UiListenScreen screen) {
    if (screen >= UI_LISTEN_CLOSING) {
        return UI_LISTEN_DONE;
    }
    return (UiListenScreen)(screen + 1);
}

UiListenScreen UiListenSkip(UiListenScreen screen) {
    if (screen <= UI_LISTEN_OPENING) {
        return UI_LISTEN_A_BEEP;
    }
    if (screen <= UI_LISTEN_A4) {
        return UI_LISTEN_B_BEEP;
    }
    if (screen <= UI_LISTEN_B_RIGHT) {
        return UI_LISTEN_C_BEEP;
    }
    if (screen <= UI_LISTEN_C4) {
        return UI_LISTEN_CLOSING;
    }
    return UI_LISTEN_DONE;
}

void UiComposeListen(const UiFrame *frame, UiListenScreen screen, double left, double right) {
    if (frame == NULL || frame->pixels == NULL) {
        return;
    }
    FillRect(frame, 0, 0, frame->width, frame->height, BACKGROUND);
    if (frame->width < UI_MIN_WIDTH || frame->height < UI_MIN_HEIGHT) {
        UiText(frame, 0, 0, 1, TEXT, "frame too small");
        return;
    }
    char guide[192];
    UiListenCopy text;
    UiListenCopyOf(screen, left, right, &text, guide, sizeof(guide));

    // The running screen's layout, which comes before and after it: the word and what is
    // playing on the top row, a second row in the dim colour, the step and its bar where a list
    // would start, and what to listen for below the bar.
    Say(frame, MARGIN, 8, TEXT, "listening");
    char line[200];
    if (text.mark != NULL) {
        snprintf(line, sizeof(line), "part %s: %s", text.mark, text.name);
    } else {
        snprintf(line, sizeof(line), "%s", screen == UI_LISTEN_OPENING ? "before the parts"
                                                                       : "after the parts");
    }
    SayFitted(frame, MARGIN + 11u * GLYPH, 8, DIM, line);
    SayFitted(frame, MARGIN, 30, DIM,
              "about 40 seconds of sound to hear, before the audio suite's cases");

    const uint32_t steps = UI_LISTEN_CLOSING + 1u;
    const uint32_t step = screen < steps ? (uint32_t)screen + 1u : steps;
    snprintf(line, sizeof(line), "step %u of %u: %s", (unsigned)step, (unsigned)steps,
             text.take);
    SayFitted(frame, MARGIN, UI_LIST_TOP, TEXT, line);
    DrawStepsBar(frame, step, steps);

    const uint32_t y = UI_PROGRESS_BAR_TOP + UI_PROGRESS_BAR_HEIGHT + 16u;
    Say(frame, MARGIN, y, DIM, "listen for");
    const uint32_t top = y + LINE;
    const uint32_t room = frame->height > top + FOOT ? (frame->height - top - FOOT) / LINE : 1u;
    TextWrapped(frame, MARGIN, top, BIG, LINE, TEXT, text.guide, Columns(frame), room);
    DrawButtons(frame, text.buttons);
}

// The pad screen's two stick boxes, side by side at the right of the live lines: each stick's
// square of travel, with a mark where it is now.
#define PAD_BOX 112u
#define PAD_MARK 8u

static void DrawStickBox(const UiFrame *frame, uint32_t x, uint32_t y, const char *label,
                         int32_t stick_x, int32_t stick_y) {
    FillRect(frame, x, y, PAD_BOX, PAD_BOX, BORDER);
    FillRect(frame, x + 2u, y + 2u, PAD_BOX - 4u, PAD_BOX - 4u, BACKGROUND);
    FillRect(frame, x + PAD_BOX / 2u, y + 2u, 1u, PAD_BOX - 4u, SELECTED);
    FillRect(frame, x + 2u, y + PAD_BOX / 2u, PAD_BOX - 4u, 1u, SELECTED);
    // Up on the stick is up on the screen, which is the other way from the rows.
    const int64_t span = (int64_t)(PAD_BOX - PAD_MARK);
    int64_t cx = ((int64_t)stick_x + PAD_STICK_FULL) * span / (2 * PAD_STICK_FULL);
    int64_t cy = ((int64_t)PAD_STICK_FULL - stick_y) * span / (2 * PAD_STICK_FULL);
    cx = cx < 0 ? 0 : cx > span ? span : cx;
    cy = cy < 0 ? 0 : cy > span ? span : cy;
    FillRect(frame, x + (uint32_t)cx, y + (uint32_t)cy, PAD_MARK, PAD_MARK, TEXT);
    Say(frame, x, y + PAD_BOX + 4u, DIM, label);
}

void UiComposePad(const UiFrame *frame, const UiPadScreen *screen) {
    if (frame == NULL || frame->pixels == NULL || screen == NULL) {
        return;
    }
    FillRect(frame, 0, 0, frame->width, frame->height, BACKGROUND);
    if (frame->width < UI_MIN_WIDTH || frame->height < UI_MIN_HEIGHT) {
        UiText(frame, 0, 0, 1, TEXT, "frame too small");
        return;
    }
    // The running screen's layout, as the listening screen keeps it: the word and the program on
    // the top row, a dim second row, the prompt and its bar where a list would start.
    Say(frame, MARGIN, 8, TEXT, "controller");
    SayFitted(frame, MARGIN + 12u * GLYPH, 8, DIM,
              screen->heading != NULL ? screen->heading : "edenprobe");
    SayFitted(frame, MARGIN, 30, DIM,
              "press what the screen asks; nothing for 10 s skips the prompt");

    char line[200];
    const uint32_t prompt = screen->prompt < screen->prompts ? screen->prompt : screen->prompts;
    snprintf(line, sizeof(line), "prompt %u of %u", (unsigned)prompt, (unsigned)screen->prompts);
    SayFitted(frame, MARGIN, UI_LIST_TOP, TEXT, line);
    DrawStepsBar(frame, prompt, screen->prompts);

    // Large enough to read from a sofa: twelve times the cell, as long as it fits across.
    uint32_t y = UI_PROGRESS_BAR_TOP + UI_PROGRESS_BAR_HEIGHT + 24u;
    const char *const big = screen->big != NULL ? screen->big : "";
    const uint32_t room = frame->width - 2u * MARGIN;
    uint32_t scale = UI_PAD_BIG;
    while (scale > BIG && (uint32_t)strlen(big) * CELL * scale > room) {
        --scale;
    }
    UiText(frame, MARGIN, y, scale, TEXT, big);
    y += CELL * UI_PAD_BIG + 16u;
    y += TextWrapped(frame, MARGIN, y, BIG, LINE, TEXT, screen->how, Columns(frame), 2u) * LINE;
    if (screen->state != NULL) {
        SayFitted(frame, MARGIN, y + 4u, DIM, screen->state);
    }

    // What the controller sends now, in the bits the suite reads, beside the two sticks.
    const uint32_t live = y + 2u * LINE + 8u;
    const uint32_t boxes = frame->width - MARGIN - 2u * PAD_BOX - 24u;
    char held[160];
    PadButtonWords(held, sizeof(held), screen->held);
    snprintf(line, sizeof(line), "held: %s", held);
    TextWrapped(frame, MARGIN, live, BIG, LINE, TEXT, line, (boxes - MARGIN) / GLYPH, 2u);
    snprintf(line, sizeof(line), "left stick  x %6d  y %6d", (int)screen->left_x,
             (int)screen->left_y);
    Say(frame, MARGIN, live + 2u * LINE, DIM, line);
    snprintf(line, sizeof(line), "right stick x %6d  y %6d", (int)screen->right_x,
             (int)screen->right_y);
    Say(frame, MARGIN, live + 3u * LINE, DIM, line);
    snprintf(line, sizeof(line), "connected: %s",
             screen->controllers != NULL ? screen->controllers : "nothing");
    TextWrapped(frame, MARGIN, live + 4u * LINE, BIG, LINE, DIM, line, (boxes - MARGIN) / GLYPH,
                2u);
    DrawStickBox(frame, boxes, live, "left", screen->left_x, screen->left_y);
    DrawStickBox(frame, boxes + PAD_BOX + 24u, live, "right", screen->right_x, screen->right_y);

    if (screen->last != NULL) {
        const uint32_t last = live + PAD_BOX + 2u * LINE;
        const uint32_t x = Say(frame, MARGIN, last, DIM, "before: ");
        TextWrapped(frame, x, last, BIG, LINE, ResultColour(screen->last_result), screen->last,
                    (frame->width - MARGIN - x) / GLYPH, 2u);
    }
    DrawButtons(frame, "Home and Capture are left out: the system takes them. A question is "
                       "answered with up for yes and down for no on the d-pad.");
}
