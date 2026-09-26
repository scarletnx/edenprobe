// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../rawref.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// The samples passed counter and the render enable that reads counters back.
//
// Counts: quads whose edges sit on pixel edges, so the count is the rectangle's area and no
// pixel centre is left to the coverage rule. The target is 320 rows tall, past the 288 an
// emulator that scales render targets starts at, so a run with scaling on says whether the
// count a game reads is the console's or the scaled target's.
//
// Render enable: SET_RENDER_ENABLE_A to C, which the library never writes, sent as raw methods
// around one draw of a full target quad. IF_EQUAL and IF_NOT_EQUAL compare the 64-bit words at
// the address and 16 bytes past it, the two halves of a pair of counter reports, which is the
// way the console's own graphics API uses it. The values come from the processor in some cases
// and from counter reports in others, in the same list after a wait for idle or in a list that
// finished before, since an emulator can take these apart (plain memory, a query synced at the
// wait, a query from an earlier submission). CONDITIONAL, which looks at one address, gets notes
// rather than cases: which bytes it reads is not settled.

static const char *const SUITE = "query";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 320u
#define PIXEL_COUNT (TARGET_WIDTH * TARGET_HEIGHT)

/// Two counter reports, 16 bytes each: the value, then the time it was taken.
typedef struct {
    uint64_t value;
    uint64_t timestamp;
} Report;

typedef struct {
    GpuContext gpu;
    DkShader vs;
    DkShader fs;
} QueryContext;

/// A rectangle of whole pixels, in the target's own coordinates.
typedef struct {
    uint32_t x0, y0, x1, y1;
} Rect;

static const Rect FIRST = { 4, 10, 36, 310 };  // 32 by 300: 9600 pixels
static const Rect SECOND = { 40, 20, 60, 100 }; // 20 by 80: 1600 pixels
static const Rect WHOLE = { 0, 0, TARGET_WIDTH, TARGET_HEIGHT };

static const uint8_t DRAWN[4] = { 0x3C, 0xA5, 0x5A, 0xFF };
static const uint8_t CLEARED[4] = { 0, 0, 0, 0 };
static const uint8_t BEFORE[4] = { 0x80, 0x10, 0xC0, 0xFF };

static uint64_t Area(Rect r) {
    return (uint64_t)(r.x1 - r.x0) * (r.y1 - r.y0);
}

/// A vertex at pixel @p x, @p y, rows counted from the top as the readback's are; both map to
/// binary fractions of clip space, which the viewport carries back to the same pixel edge.
static GpuVertex Corner(uint32_t x, uint32_t y, const uint8_t colour[4]) {
    GpuVertex v;
    v.x = 2.0f * (float)x / (float)TARGET_WIDTH - 1.0f;
    v.y = 1.0f - 2.0f * (float)y / (float)TARGET_HEIGHT;
    v.z = 0.5f;
    v.u = 0.0f;
    v.v = 0.0f;
    v.r = (float)colour[0] / 255.0f;
    v.g = (float)colour[1] / 255.0f;
    v.b = (float)colour[2] / 255.0f;
    v.a = (float)colour[3] / 255.0f;
    return v;
}

static bool DrawRect(QueryContext *q, Rect r, const uint8_t colour[4]) {
    const GpuVertex quad[6] = { Corner(r.x0, r.y0, colour), Corner(r.x1, r.y0, colour),
                                Corner(r.x1, r.y1, colour), Corner(r.x0, r.y0, colour),
                                Corner(r.x1, r.y1, colour), Corner(r.x0, r.y1, colour) };
    return GpuDrawTriangles(&q->gpu, &q->vs, &q->fs, quad, 6);
}

/// Two reports' worth of scratch, 16 byte aligned as a counter report must be.
static Report *Reports(QueryContext *q, DkGpuAddr *address) {
    Report *const reports = (Report *)GpuAllocData(&q->gpu, 2u * sizeof(Report), 16u, address);
    if (reports != NULL) {
        memset(reports, 0, 2u * sizeof(Report));
    }
    return reports;
}

static bool RenderEnable(QueryContext *q, DkGpuAddr address, uint32_t mode) {
    uint32_t words[4];
    const size_t count = RawRenderEnable(words, address, mode);
    return GpuRawMethods(&q->gpu, words, (uint32_t)count);
}

/// How many pixels hold @p want, and the first that does not.
static uint32_t CountHolding(const uint8_t *pixels, const uint8_t want[4], uint32_t *first) {
    uint32_t holding = 0;
    *first = PIXEL_COUNT;
    for (uint32_t i = 0; i < PIXEL_COUNT; ++i) {
        if (memcmp(pixels + (size_t)i * 4u, want, 4) == 0) {
            ++holding;
        } else if (*first == PIXEL_COUNT) {
            *first = i;
        }
    }
    return holding;
}

// ---- Counts

typedef enum {
    COUNT_ONE,      // reset, the first quad, report
    COUNT_TWO,      // reset, both quads, report
    COUNT_AFTER,    // the first quad, reset, report
} CountShape;

static void CountCase(QueryContext *q, const char *name, CountShape shape, uint64_t want) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    GpuClear(&q->gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    DkGpuAddr address = 0;
    Report *const reports = Reports(q, &address);
    if (reports == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the report");
        return;
    }
    reports[0].value = 0xDEADBEEFCAFEF00Dull; // a count that was never written says so
    GpuBeginPass(&q->gpu);
    bool ok = true;
    if (shape == COUNT_AFTER) {
        ok = DrawRect(q, FIRST, DRAWN);
    }
    dkCmdBufResetCounter(q->gpu.cmdbuf, DkCounter_SamplesPassed);
    if (shape != COUNT_AFTER) {
        ok = DrawRect(q, FIRST, DRAWN);
    }
    if (ok && shape == COUNT_TWO) {
        ok = DrawRect(q, SECOND, DRAWN);
    }
    dkCmdBufReportCounter(q->gpu.cmdbuf, DkCounter_SamplesPassed, address);
    GpuSubmit(&q->gpu);
    if (!ok || !q->gpu.ready) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   q->gpu.failure[0] != '\0' ? q->gpu.failure : "the list was not recorded");
        return;
    }
    const uint64_t got = reports[0].value;
    CHECK_TRUE(SUITE, name, CHECK_EXACT, got == want,
               "counted %" PRIu64 ", expected %" PRIu64 "%s", got, want,
               got == want * 4u   ? " (four times: a target scaled by two each way)"
               : got == want * 9u ? " (nine times: a target scaled by three each way)"
                                  : "");
}

// ---- Render enable

typedef enum {
    VALUES_NONE,         // the mode alone decides
    VALUES_PROCESSOR,    // the processor writes both reports before the submit
    VALUES_SAME,         // the first quad counted twice, reported in the same list
    VALUES_APART,        // the first quad counted, then nothing, reported in the same list
    VALUES_SAME_BEFORE,  // VALUES_SAME, in a list submitted and finished before
    VALUES_APART_BEFORE, // VALUES_APART, likewise
} Values;

/// Records the reports @p values asks for into the open list; for the processor's, writes them.
static bool MakeValues(QueryContext *q, Report *reports, DkGpuAddr address, Values values,
                       uint64_t first, uint64_t second) {
    switch (values) {
    case VALUES_NONE:
        return true;
    case VALUES_PROCESSOR:
        // The timestamps differ, as any two real reports' do; they must not take part.
        reports[0].value = first;
        reports[0].timestamp = 0x1111u;
        reports[1].value = second;
        reports[1].timestamp = 0x2222u;
        return true;
    case VALUES_SAME:
    case VALUES_APART:
    case VALUES_SAME_BEFORE:
    case VALUES_APART_BEFORE:
        dkCmdBufResetCounter(q->gpu.cmdbuf, DkCounter_SamplesPassed);
        if (!DrawRect(q, FIRST, DRAWN)) {
            return false;
        }
        dkCmdBufReportCounter(q->gpu.cmdbuf, DkCounter_SamplesPassed, address);
        dkCmdBufResetCounter(q->gpu.cmdbuf, DkCounter_SamplesPassed);
        if ((values == VALUES_SAME || values == VALUES_SAME_BEFORE) &&
            !DrawRect(q, FIRST, DRAWN)) {
            return false;
        }
        dkCmdBufReportCounter(q->gpu.cmdbuf, DkCounter_SamplesPassed, address + sizeof(Report));
        // A report lands at the end of the pipeline and the render enable is read at its start,
        // and nothing promises the unit waits between the two, so the list waits for
        // everything before it: without the wait, a console could read the memset's nought.
        // The earlier list's submit waits for everything instead, the way reports from a
        // previous frame are.
        if (values == VALUES_SAME_BEFORE || values == VALUES_APART_BEFORE) {
            GpuSubmit(&q->gpu);
            GpuBeginPass(&q->gpu);
        } else {
            dkCmdBufBarrier(q->gpu.cmdbuf, DkBarrier_Full, 0);
        }
        // The counted quads drew too; clear them away so that only the judged draw is left.
        dkCmdBufClearColorFloat(q->gpu.cmdbuf, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 0.0f);
        return q->gpu.ready;
    }
    return false;
}

/// What a judged operation under the render enable left behind.
typedef enum {
    OUTCOME_HAPPENED,
    OUTCOME_SKIPPED,
    OUTCOME_MIXED,     // some of the target changed and some did not: neither answer
    OUTCOME_NOT_RUN,   // the list or the readback never happened
} Outcome;

/// Runs one judged operation under the render enable and says what became of it.
///
/// @p clear judges a clear to DRAWN over a target cleared to BEFORE instead of a draw over a
/// cleared target. @p detail says why, for the two outcomes that are neither answer.
static Outcome Enabled(QueryContext *q, Values values, uint32_t mode, uint64_t first,
                       uint64_t second, bool clear, char *detail, size_t detail_size) {
    const uint8_t *const before = clear ? BEFORE : CLEARED;
    GpuClear(&q->gpu, before[0] / 255.0f, before[1] / 255.0f, before[2] / 255.0f,
             before[3] / 255.0f);
    DkGpuAddr address = 0;
    Report *const reports = Reports(q, &address);
    if (reports == NULL) {
        snprintf(detail, detail_size, "no scratch left for the reports");
        return OUTCOME_NOT_RUN;
    }
    GpuBeginPass(&q->gpu);
    bool ok = MakeValues(q, reports, address, values, first, second);
    if (ok && clear) {
        // What MakeValues cleared to nought goes back to the colour the case starts from.
        dkCmdBufClearColorFloat(q->gpu.cmdbuf, 0, DkColorMask_RGBA, before[0] / 255.0f,
                                before[1] / 255.0f, before[2] / 255.0f, before[3] / 255.0f);
    }
    ok = ok && RenderEnable(q, address, mode);
    if (ok && clear) {
        dkCmdBufClearColorFloat(q->gpu.cmdbuf, 0, DkColorMask_RGBA, DRAWN[0] / 255.0f,
                                DRAWN[1] / 255.0f, DRAWN[2] / 255.0f, DRAWN[3] / 255.0f);
    } else if (ok) {
        ok = DrawRect(q, WHOLE, DRAWN);
    }
    // Rendering back on for everything after this list, whatever the case did.
    ok = RenderEnable(q, 0, RAW_RENDER_TRUE) && ok;
    GpuSubmit(&q->gpu);
    const uint8_t *const pixels = ok && q->gpu.ready ? GpuReadBack(&q->gpu) : NULL;
    if (pixels == NULL) {
        snprintf(detail, detail_size, "%s",
                 q->gpu.failure[0] != '\0' ? q->gpu.failure
                                                : "the list or readback did not happen");
        return OUTCOME_NOT_RUN;
    }
    uint32_t first_other = 0;
    const uint32_t holding_drawn = CountHolding(pixels, DRAWN, &first_other);
    uint32_t first_not_before = 0;
    const uint32_t holding_before = CountHolding(pixels, before, &first_not_before);
    if (holding_drawn == PIXEL_COUNT) {
        return OUTCOME_HAPPENED;
    }
    if (holding_before == PIXEL_COUNT) {
        return OUTCOME_SKIPPED;
    }
    const uint8_t *const p = pixels + (size_t)first_other * 4u;
    snprintf(detail, detail_size,
             "neither happened nor left alone: %u pixels changed, %u as before, first other at "
             "(%u,%u) is %02x%02x%02x%02x",
             (unsigned)holding_drawn, (unsigned)holding_before,
             (unsigned)(first_other % TARGET_WIDTH), (unsigned)(first_other / TARGET_WIDTH), p[0],
             p[1], p[2], p[3]);
    return OUTCOME_MIXED;
}

static void EnableCase(QueryContext *q, const char *name, Values values, uint32_t mode,
                       uint64_t first, uint64_t second, bool clear, bool want) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    char detail[256] = "";
    const Outcome outcome = Enabled(q, values, mode, first, second, clear, detail,
                                    sizeof(detail));
    if (outcome == OUTCOME_NOT_RUN) {
        ReportCase(SUITE, name, CHECK_NONE, false, detail);
        return;
    }
    if (outcome == OUTCOME_MIXED) {
        ReportCase(SUITE, name, CHECK_EXACT, false, detail);
        return;
    }
    const bool happened = outcome == OUTCOME_HAPPENED;
    const char *const what = clear ? "cleared" : "drawn";
    CHECK_TRUE(SUITE, name, CHECK_EXACT, happened == want, "%s%s when it should %s",
               happened ? "" : "not ", what, want ? "have been" : "not have been");
}

/// CONDITIONAL reads one address, and which of its bytes decide is not settled, so what it did
/// is a note on every run rather than a case: a console run is what says which reading is right.
static void ConditionalNote(QueryContext *q, const char *what, Values values, uint64_t value) {
    char detail[256] = "";
    const Outcome outcome =
        Enabled(q, values, RAW_RENDER_CONDITIONAL, value, 0, false, detail, sizeof(detail));
    ReportNote("query: render enable CONDITIONAL over %s: %s%s", what,
               outcome == OUTCOME_HAPPENED  ? "drawn"
               : outcome == OUTCOME_SKIPPED ? "not drawn"
                                            : "no answer, ",
               outcome == OUTCOME_HAPPENED || outcome == OUTCOME_SKIPPED ? "" : detail);
}

void RunQuery(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;

    if (!ReportStart(SUITE, "graphics device opens with a 64 by 320 target")) {
        return;
    }
    QueryContext q;
    const bool opened = GpuOpenSpec(&q.gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with a 64 by 320 target", CHECK_NONE, opened, "%s",
               q.gpu.failure);
    if (!opened) {
        return;
    }
    if (!ReportStart(SUITE, "the flat shaders load")) {
        GpuClose(&q.gpu);
        return;
    }
    const bool loaded = GpuLoadShader(&q.gpu, &q.vs, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&q.gpu, &q.fs, ShaderData(SHADER_FLAT_FRAG),
                                      ShaderSize(SHADER_FLAT_FRAG));
    CHECK_TRUE(SUITE, "the flat shaders load", CHECK_NONE, loaded, "%s", q.gpu.failure);
    if (!loaded) {
        GpuClose(&q.gpu);
        return;
    }

    CountCase(&q, "a 32 by 300 pixel quad passes 9600 samples", COUNT_ONE, Area(FIRST));
    CountCase(&q, "a second quad of 1600 pixels adds to the count, 11200", COUNT_TWO,
              Area(FIRST) + Area(SECOND));
    CountCase(&q, "a reset after a draw leaves a count of nought", COUNT_AFTER, 0u);

    EnableCase(&q, "render enable FALSE skips the draw", VALUES_NONE, RAW_RENDER_FALSE, 0, 0,
               false, false);
    EnableCase(&q, "render enable TRUE draws", VALUES_NONE, RAW_RENDER_TRUE, 0, 0, false, true);

    const uint64_t high = 1ull << 32;
    EnableCase(&q, "IF_EQUAL draws over two equal values whose timestamps differ",
               VALUES_PROCESSOR, RAW_RENDER_IF_EQUAL, 9600u, 9600u, false, true);
    EnableCase(&q, "IF_NOT_EQUAL skips the same two values", VALUES_PROCESSOR,
               RAW_RENDER_IF_NOT_EQUAL, 9600u, 9600u, false, false);
    EnableCase(&q, "IF_EQUAL skips values that differ in the low word", VALUES_PROCESSOR,
               RAW_RENDER_IF_EQUAL, 9600u, 9601u, false, false);
    EnableCase(&q, "IF_NOT_EQUAL draws over values that differ in the low word",
               VALUES_PROCESSOR, RAW_RENDER_IF_NOT_EQUAL, 9600u, 9601u, false, true);
    EnableCase(&q, "IF_EQUAL skips values that differ only in the high word", VALUES_PROCESSOR,
               RAW_RENDER_IF_EQUAL, 9600u, 9600u | high, false, false);
    EnableCase(&q, "IF_NOT_EQUAL draws over values that differ only in the high word",
               VALUES_PROCESSOR, RAW_RENDER_IF_NOT_EQUAL, 9600u, 9600u | high, false, true);

    EnableCase(&q, "IF_EQUAL draws after two reports of the same count in the same list",
               VALUES_SAME, RAW_RENDER_IF_EQUAL, 0, 0, false, true);
    EnableCase(&q, "IF_NOT_EQUAL skips after two reports of the same count", VALUES_SAME,
               RAW_RENDER_IF_NOT_EQUAL, 0, 0, false, false);
    EnableCase(&q, "IF_EQUAL skips after a report of 9600 and one of nought", VALUES_APART,
               RAW_RENDER_IF_EQUAL, 0, 0, false, false);
    EnableCase(&q, "IF_NOT_EQUAL draws after a report of 9600 and one of nought", VALUES_APART,
               RAW_RENDER_IF_NOT_EQUAL, 0, 0, false, true);
    EnableCase(&q, "IF_EQUAL draws after two equal reports from a list that finished before",
               VALUES_SAME_BEFORE, RAW_RENDER_IF_EQUAL, 0, 0, false, true);
    EnableCase(&q, "IF_NOT_EQUAL skips after two equal reports from a list before",
               VALUES_SAME_BEFORE, RAW_RENDER_IF_NOT_EQUAL, 0, 0, false, false);
    EnableCase(&q, "IF_EQUAL skips after 9600 and nought reported in a list before",
               VALUES_APART_BEFORE, RAW_RENDER_IF_EQUAL, 0, 0, false, false);
    EnableCase(&q, "IF_NOT_EQUAL draws after 9600 and nought reported in a list before",
               VALUES_APART_BEFORE, RAW_RENDER_IF_NOT_EQUAL, 0, 0, false, true);
    EnableCase(&q, "a full target clear under IF_EQUAL after 9600 and nought is skipped too",
               VALUES_APART, RAW_RENDER_IF_EQUAL, 0, 0, true, false);
    EnableCase(&q, "a full target clear under IF_NOT_EQUAL after 9600 and nought happens",
               VALUES_APART, RAW_RENDER_IF_NOT_EQUAL, 0, 0, true, true);

    ConditionalNote(&q, "the value one, high word nought", VALUES_PROCESSOR, 1u);
    ConditionalNote(&q, "the value 2^32, low word nought", VALUES_PROCESSOR, high);
    ConditionalNote(&q, "a report of 9600 in the same list", VALUES_APART, 0);
    GpuClose(&q.gpu);
}
