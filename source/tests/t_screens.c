// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../check.h"
#include "../padref.h"
#include "../picture.h"
#include "../report.h"
#include "../touchref.h"
#include "../ui.h"
#include "tests.h"

// The detailed mode's screens: the index of the recorded cases, the walk from the overview to a
// group, a case and back, and the frames each level composes. Part of the support suite, since
// nothing here asks the hardware for anything, and every line is the none tier.

static const char *const SUITE = "support";

#define FRAME_WIDTH 1280u
#define FRAME_HEIGHT 720u

// The screen's own colours, repeated here so a case that looks for red is looking for the red
// the layout promised rather than whatever it happens to use today.
static const uint8_t BACKGROUND[3] = { 0x10, 0x12, 0x18 };
static const uint8_t PASSED[3] = { 0x40, 0xD0, 0x60 };
static const uint8_t FAILED[3] = { 0xF0, 0x50, 0x50 };
static const uint8_t SKIPPED[3] = { 0xE8, 0xB0, 0x40 };
static const uint8_t SELECTED[3] = { 0x28, 0x2E, 0x3C };
static const uint8_t CRASHED[3] = { 0xC0, 0x78, 0xF0 };

// The shape of a real run: every suite with the counts a quick run of main 39b6920 reported,
// with two failures put into intmath and one into ssl. Real names where a frame shows them.
static const char *const CLEAR_NAMES[] = {
    "graphics device opens",       "clear to opaque red",         "clear to transparent cyan",
    "clear to a mid channel value", "clear bounded by a rectangle",
};
static const char *const INTMATH_NAMES[] = {
    "graphics device opens with an integer target",
    "the integer shaders load",
    "the uniform buffer has somewhere to live",
    "the fragment coordinate's x is the pixel centre, bit for bit",
    "the fragment coordinate's y is the pixel centre, bit for bit",
    "the uniform buffer delivers its value unaltered",
    "a multiply and an add that both overflow 32 bits",
    "a left shift discards the bits it pushes out",
    "an unsigned right shift brings in noughts",
    "a signed right shift extends the sign bit",
    "bitwise and",
    "bitwise or",
    "bitwise exclusive or",
    "counting the set bits",
    "finding the lowest and the highest set bit",
    "unsigned division by a prime",
    "unsigned remainder by a prime",
    "a signed integer converts to the nearest float, ties to even",
    "an unsigned integer converts to the nearest float, ties to even",
    "a float converts to a signed integer towards nought",
    "a float above two to the 31 converts to an unsigned integer",
    "packUnorm4x8 clamps and rounds each component",
    "unpackUnorm4x8 then packUnorm4x8 gives the word back",
    "packSnorm2x16 clamps and rounds each component",
    "unpackSnorm2x16 then packSnorm2x16 gives the word back, 0x8000 as 0x8001",
    "packHalf2x16 of floats that are exactly half precision",
    "unpackHalf2x16 of normal halves and infinities",
    "a left shift by 31, 32 and 33 in a register clamps the count to 32",
    "an unsigned right shift by 31, 32 and 33 clamps the count to 32",
    "a signed right shift by 31, 32 and 33 clamps the count to 32",
    "a left shift by every count from nought to 63",
    "an unsigned right shift by every count from nought to 63",
    "a signed right shift by every count from nought to 63",
    "signed division with negative operands truncates towards nought",
    "signed remainder with negative operands takes the dividend's sign",
    "findMSB of an unsigned value, nought included",
    "findMSB of a signed value, minus one and nought included",
    "findLSB, nought included",
    "bitfieldExtract of an unsigned value at every offset and width",
    "bitfieldExtract of a signed value extends the field's sign",
    "bitfieldInsert at every offset and width",
    "bitfieldReverse",
    "the signed minimum",
    "the unsigned maximum",
    "a signed clamp",
    "an unsigned clamp",
};
static const char *const NIFM_NAMES[] = {
    "nifm:u opens",
    "a request nobody submitted says it was not submitted",
    "a machine with a backend reports its connection connected",
    "a machine with a backend has an address of its own",
    "a submitted request is accepted without being asked for its result",
    "a submitted request asked for its result is accepted",
    "while a request is accepted, an internet request is accepted",
};
static const char *const SSL_NAMES[] = {
    "a context is created",
    "a new connection verifies the peer and its name by default",
    "the name set on a connection reads back",
    "an option set on a connection reads back",
    "a connection takes a socket and is set to block",
    "a connection set to block says it blocks",
    "a connection hands a descriptor back unless told not to close the socket",
    "a verifying handshake refuses a certificate no authority signed",
    "a verifying handshake succeeds once the server's certificate is imported",
    "the handshake hands back the server's certificate",
    "a handshake that does not verify succeeds",
    "a request written over it goes out whole",
    "the server answers 200 with a status document that says nothing is wrong",
    "a connection keeps its socket after the program closes its own descriptor",
};

static const struct {
    const char *suite;
    uint32_t passed;
    uint32_t skipped;
    const char *const *names;
} SHAPE[] = {
    { "selfcheck", 16, 0, NULL }, { "support", 72, 0, NULL },   { "reference", 118, 0, NULL },
    { "cpu", 3, 0, NULL },        { "clear", 5, 0, CLEAR_NAMES }, { "format", 64, 0, NULL },
    { "raster", 10, 0, NULL },    { "intmath", 46, 0, INTMATH_NAMES },
    { "blend", 13, 0, NULL },     { "depth", 31, 0, NULL },     { "texture", 12, 0, NULL },
    { "coherence", 21, 0, NULL }, { "fmath", 26, 0, NULL },     { "nifm", 2, 5, NIFM_NAMES },
    { "dns", 6, 5, NULL },        { "bsd", 6, 2, NULL },        { "ssl", 7, 7, SSL_NAMES },
    { "acc", 2, 4, NULL },        { "friend", 1, 4, NULL },
};
#define RUN_CASES 488u
// Room for the fixtures that need more cases than a run has.
#define FIXTURE_CASES 1024u

static const char *const FAILS_BEFORE_DRAWING = "the uniform buffer has somewhere to live";
static const char *const FAILS_WITH_PICTURE = "bitwise and";
static const char *const FAILS_IN_SSL = "the name set on a connection reads back";

// A 64 by 64 integer target, as intmath keeps one, with eight rows that came back zero.
#define PICTURE_SIDE 64u
#define PICTURE_PIXELS (PICTURE_SIDE * PICTURE_SIDE)
#define WRONG_FROM_ROW 40u
#define WRONG_ROWS 8u

typedef struct {
    ReportRecord records[FIXTURE_CASES];
    UiRun run;
    UiRun small;
    PictureRecord picture;
    uint8_t got[PICTURE_PIXELS * 4u];
    uint8_t expected[PICTURE_PIXELS * 4u];
    uint8_t verdict[PICTURE_PIXELS];
    uint8_t pixels[FRAME_WIDTH * FRAME_HEIGHT * 4u];
} Fixture;

static void Fill(ReportRecord *record, const char *suite, const char *name, const char *tier,
                 ReportResult result, const char *detail, int32_t picture) {
    memset(record, 0, sizeof(*record));
    ReportRecordFill(record, suite, name, tier, result, detail, picture);
}

/// The words of the intmath failure and the picture that goes with them, so the detail on the
/// screen says what the picture shows.
static void BuildPicture(Fixture *f, char *detail, size_t detail_size) {
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint32_t first_expected = 0;
    for (uint32_t y = 0; y < PICTURE_SIDE; ++y) {
        for (uint32_t x = 0; x < PICTURE_SIDE; ++x) {
            const size_t i = (size_t)y * PICTURE_SIDE + x;
            const uint32_t word = ((x * 4u) << 16) | ((y * 4u) << 8) | ((x ^ y) * 4u);
            const bool bad = y >= WRONG_FROM_ROW && y < WRONG_FROM_ROW + WRONG_ROWS;
            const uint32_t drawn = bad ? 0u : word;
            for (uint32_t b = 0; b < 4u; ++b) {
                f->expected[i * 4u + b] = (uint8_t)(word >> (8u * b));
                f->got[i * 4u + b] = (uint8_t)(drawn >> (8u * b));
            }
            f->verdict[i] = (uint8_t)(bad ? PICTURE_WRONG : PICTURE_MATCHED);
            if (bad && wrong++ == 0u) {
                first_x = x;
                first_y = y;
                first_expected = word;
            }
        }
    }
    memset(&f->picture, 0, sizeof(f->picture));
    snprintf(f->picture.suite, sizeof(f->picture.suite), "intmath");
    snprintf(f->picture.name, sizeof(f->picture.name), "%s", FAILS_WITH_PICTURE);
    snprintf(f->picture.tier, sizeof(f->picture.tier), "exact");
    f->picture.width = PICTURE_SIDE;
    f->picture.height = PICTURE_SIDE;
    f->picture.format = PICTURE_BITS;
    f->picture.got = f->got;
    f->picture.expected = f->expected;
    f->picture.verdict = f->verdict;
    snprintf(detail, detail_size,
             "%u of %u pixels wrong, first at (%u,%u) is 0x00000000, expected 0x%08x",
             (unsigned)wrong, (unsigned)PICTURE_PIXELS, (unsigned)first_x, (unsigned)first_y,
             (unsigned)first_expected);
    snprintf(f->picture.detail, sizeof(f->picture.detail), "%s", detail);
}

static void BuildRun(Fixture *f) {
    char picture_detail[160];
    BuildPicture(f, picture_detail, sizeof(picture_detail));
    uint32_t at = 0;
    for (size_t s = 0; s < sizeof(SHAPE) / sizeof(SHAPE[0]); ++s) {
        const UiFamily family = UiFamilyOf(SHAPE[s].suite);
        for (uint32_t i = 0; i < SHAPE[s].passed + SHAPE[s].skipped; ++i) {
            char made[64];
            snprintf(made, sizeof(made), "%s case %u", SHAPE[s].suite, (unsigned)(i + 1u));
            const char *const name = SHAPE[s].names != NULL ? SHAPE[s].names[i] : made;
            const bool skipped = i >= SHAPE[s].passed;
            // Every drawing case past the device's bring-up kept a picture.
            const bool draws = family == UI_FAMILY_GRAPHICS && strstr(name, "opens") == NULL &&
                               strstr(name, "load") == NULL &&
                               strcmp(name, FAILS_BEFORE_DRAWING) != 0;
            const char *tier = family == UI_FAMILY_JUDGE || skipped ? "none" : "exact";
            if (skipped) {
                Fill(&f->records[at], SHAPE[s].suite, name, tier, REPORT_SKIP,
                     family == UI_FAMILY_NET && strcmp(SHAPE[s].suite, "ssl") == 0
                         ? "needs a game server: sdmc:/edenprobe/net names none"
                         : "nothing says this machine should be online: sdmc:/edenprobe/net "
                           "names no session and no server",
                     -1);
            } else if (strcmp(name, FAILS_BEFORE_DRAWING) == 0) {
                Fill(&f->records[at], SHAPE[s].suite, name, "none", REPORT_FAIL,
                     "no memory for the uniform buffer, so every case after it draws with "
                     "nothing bound",
                     -1);
            } else if (strcmp(name, FAILS_WITH_PICTURE) == 0) {
                Fill(&f->records[at], SHAPE[s].suite, name, tier, REPORT_FAIL, picture_detail,
                     0);
            } else if (strcmp(name, FAILS_IN_SSL) == 0) {
                Fill(&f->records[at], SHAPE[s].suite, name, tier, REPORT_FAIL,
                     "expected \"edenprobe.invalid\" back after SetHostName; SetHostName "
                     "2000-0000 (0x0), GetHostName 2000-0000 (0x0), \"\" (0 bytes)",
                     -1);
            } else {
                Fill(&f->records[at], SHAPE[s].suite, name, tier, REPORT_PASS, "",
                     draws ? 0 : -1);
            }
            ++at;
        }
    }
    UiRunIndex(&f->run, f->records, at, at);
}

static uint32_t GroupNamed(const UiRun *run, const char *suite) {
    for (uint32_t g = 0; g < run->group_count; ++g) {
        if (strcmp(run->groups[g].suite, suite) == 0) {
            return g;
        }
    }
    return UINT32_MAX;
}

static uint32_t CaseNamed(const UiRun *run, uint32_t group, const char *name) {
    for (uint32_t i = 0; group < run->group_count && i < run->groups[group].count; ++i) {
        if (strcmp(UiCaseAt(run, group, i)->name, name) == 0) {
            return i;
        }
    }
    return UINT32_MAX;
}

/// Writes a composed frame where a person can look at it, when EDENPROBE_FRAMES names a
/// directory. Host only, and never a case: a frame nobody asked for is not a result.
static void DumpFrame(const UiFrame *frame, const char *name) {
#ifndef __SWITCH__
    const char *const directory = getenv("EDENPROBE_FRAMES");
    if (directory == NULL || directory[0] == '\0') {
        return;
    }
    char path[512];
    snprintf(path, sizeof(path), "%.400s/%s.pam", directory, name);
    FILE *const file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "edenprobe: cannot write the frame %s\n", path);
        return;
    }
    fprintf(file, "P7\nWIDTH %u\nHEIGHT %u\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n",
            (unsigned)frame->width, (unsigned)frame->height);
    bool written = true;
    for (uint32_t y = 0; y < frame->height; ++y) {
        written = written &&
                  fwrite(frame->pixels + (size_t)y * frame->pitch, 4u, frame->width, file) ==
                      frame->width;
    }
    if (fclose(file) != 0 || !written) {
        fprintf(stderr, "edenprobe: the frame %s is incomplete\n", path);
    }
#else
    (void)frame;
    (void)name;
#endif
}

static uint32_t CountColour(const UiFrame *frame, uint32_t x0, uint32_t y0, uint32_t x1,
                            uint32_t y1, const uint8_t colour[3]) {
    uint32_t seen = 0;
    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t x = x0; x < x1; ++x) {
            const uint8_t *const pixel = UiPixel(frame, x, y);
            if (pixel != NULL && memcmp(pixel, colour, 3u) == 0) {
                ++seen;
            }
        }
    }
    return seen;
}

static uint32_t CountLit(const UiFrame *frame, uint32_t y0, uint32_t y1) {
    uint32_t lit = 0;
    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t x = 0; x < frame->width; ++x) {
            const uint8_t *const pixel = UiPixel(frame, x, y);
            if (pixel != NULL && memcmp(pixel, BACKGROUND, 3u) != 0) {
                ++lit;
            }
        }
    }
    return lit;
}

/// Pixels other than the background inside a rectangle.
static uint32_t CountLitIn(const UiFrame *frame, uint32_t x0, uint32_t y0, uint32_t x1,
                           uint32_t y1) {
    uint32_t lit = 0;
    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t x = x0; x < x1; ++x) {
            const uint8_t *const pixel = UiPixel(frame, x, y);
            if (pixel != NULL && memcmp(pixel, BACKGROUND, 3u) != 0) {
                ++lit;
            }
        }
    }
    return lit;
}

/// Lines of text between two rows: runs of rows with something drawn in them.
static uint32_t TextLines(const UiFrame *frame, uint32_t y0, uint32_t y1) {
    uint32_t lines = 0;
    bool inside = false;
    for (uint32_t y = y0; y < y1; ++y) {
        const bool lit = CountLit(frame, y, y + 1u) > 0u;
        if (lit && !inside) {
            ++lines;
        }
        inside = lit;
    }
    return lines;
}

// Named here because the store's case below looks for this line as the last one reported.
static const char *const INDEX_CASE =
    "the screens gather the cases into their suites, families in the order the run met them";

static void CheckIndex(void) {
    // Out of order on purpose: a suite that comes back after another, and one no family has.
    static const struct {
        const char *suite;
        ReportResult result;
    } LINES[] = {
        { "cpu", REPORT_PASS },  { "selfcheck", REPORT_PASS }, { "cpu", REPORT_FAIL },
        { "weird", REPORT_PASS }, { "nifm", REPORT_SKIP },     { "clear", REPORT_PASS },
        { "selfcheck", REPORT_FAIL },
    };
    static const struct {
        const char *suite;
        UiFamily family;
        uint32_t cases[2];
        uint32_t count;
        uint32_t passed;
        uint32_t failed;
        uint32_t skipped;
    } WANT[] = {
        { "selfcheck", UI_FAMILY_JUDGE, { 1, 6 }, 2, 1, 1, 0 },
        { "cpu", UI_FAMILY_CPU, { 0, 2 }, 2, 1, 1, 0 },
        { "clear", UI_FAMILY_GRAPHICS, { 5, 0 }, 1, 1, 0, 0 },
        { "nifm", UI_FAMILY_NET, { 4, 0 }, 1, 0, 0, 1 },
        { "weird", UI_FAMILY_OTHER, { 3, 0 }, 1, 1, 0, 0 },
    };
    static ReportRecord records[7];
    static UiRun run;
    for (uint32_t i = 0; i < 7u; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "line %u", (unsigned)i);
        Fill(&records[i], LINES[i].suite, name, "none", LINES[i].result, "", -1);
    }
    UiRunIndex(&run, records, 7, 7);
    uint32_t wrong = run.group_count == 5u && run.unplaced == 0u && run.passed == 4u &&
                             run.failed == 2u && run.skipped == 1u
                         ? 0u
                         : 1u;
    uint32_t first_wrong = UINT32_MAX;
    for (uint32_t g = 0; g < 5u && g < run.group_count; ++g) {
        const UiGroup *const group = &run.groups[g];
        bool right = strcmp(group->suite, WANT[g].suite) == 0 &&
                     group->family == WANT[g].family && group->count == WANT[g].count &&
                     group->passed == WANT[g].passed && group->failed == WANT[g].failed &&
                     group->skipped == WANT[g].skipped;
        for (uint32_t i = 0; right && i < WANT[g].count; ++i) {
            right = UiCaseAt(&run, g, i) == &records[WANT[g].cases[i]];
        }
        if (!right) {
            ++wrong;
            if (first_wrong == UINT32_MAX) {
                first_wrong = g;
            }
        }
    }
    CHECK_TRUE(SUITE, INDEX_CASE, CHECK_NONE,
               wrong == 0u && UiCaseAt(&run, 0, 2) == NULL && UiCaseAt(&run, 5, 0) == NULL,
               "%u groups, %u things wrong, the first at group %u (%s)",
               (unsigned)run.group_count, (unsigned)wrong, (unsigned)first_wrong,
               first_wrong < run.group_count ? run.groups[first_wrong].suite : "none");
}

/// The store the screens read, against the cases this very run has reported so far: all of them
/// when the run keeps them, which a detailed run and the host build do, and none otherwise. The
/// host build turns it on itself, so there none is a store that failed.
static void CheckStore(void) {
    size_t count = 0;
    const ReportRecord *const records = ReportRecords(&count);
    const size_t reported = ReportPassed() + ReportFailed() + ReportSkipped();
    const ReportRecord *const last = records != NULL && count > 0u ? &records[count - 1u] : NULL;
    const bool kept_all = ReportIsRetaining() && count == reported && last != NULL &&
                          strcmp(last->suite, SUITE) == 0 &&
                          strcmp(last->name, INDEX_CASE) == 0 &&
                          strcmp(last->tier, "none") == 0 && last->picture == -1 &&
                          (last->result == REPORT_PASS) == (last->detail[0] == '\0');
#ifdef __SWITCH__
    const bool kept_none = !ReportIsRetaining() && count == 0u && records == NULL;
#else
    const bool kept_none = false;
#endif
    CHECK_TRUE(SUITE, "every case the run has reported is on record for the screens, or none is",
               CHECK_NONE, kept_all || kept_none,
               "retaining %d, %u kept of %u reported, the last \"%.60s\" in %.20s",
               (int)ReportIsRetaining(), (unsigned)count, (unsigned)reported,
               last != NULL ? last->name : "(none)", last != NULL ? last->suite : "(none)");
}

/// Every suite a run reports sits under its own family on the screens, never under other: the
/// suites in the order main runs them, the net family's six by the names their lines carry. A
/// suite added to main goes here too; main also says in a note when one has no family.
static void CheckFamilies(void) {
    static const struct {
        const char *suite;
        UiFamily family;
    } SUITES[] = {
        { "selfcheck", UI_FAMILY_JUDGE },   { "support", UI_FAMILY_JUDGE },
        { "reference", UI_FAMILY_JUDGE },   { "cpu", UI_FAMILY_CPU },
        { "pad", UI_FAMILY_PAD },           { "touch", UI_FAMILY_PAD },
        { "clear", UI_FAMILY_GRAPHICS },    { "format", UI_FAMILY_GRAPHICS },
        { "raster", UI_FAMILY_GRAPHICS },   { "intmath", UI_FAMILY_GRAPHICS },
        { "blend", UI_FAMILY_GRAPHICS },    { "depth", UI_FAMILY_GRAPHICS },
        { "texture", UI_FAMILY_GRAPHICS },  { "coherence", UI_FAMILY_GRAPHICS },
        { "fmath", UI_FAMILY_GRAPHICS },    { "order", UI_FAMILY_GRAPHICS },
        { "index", UI_FAMILY_GRAPHICS },    { "instance", UI_FAMILY_GRAPHICS },
        { "geometry", UI_FAMILY_GRAPHICS }, { "bindless", UI_FAMILY_GRAPHICS },
        { "astc", UI_FAMILY_GRAPHICS },     { "eac", UI_FAMILY_GRAPHICS },
        { "etc2", UI_FAMILY_GRAPHICS },     { "bcn", UI_FAMILY_GRAPHICS },
        { "zeta", UI_FAMILY_GRAPHICS },     { "swizzle", UI_FAMILY_GRAPHICS },
        { "texel", UI_FAMILY_GRAPHICS },    { "stream", UI_FAMILY_GRAPHICS },
        { "fill", UI_FAMILY_GRAPHICS },     { "ring", UI_FAMILY_GRAPHICS },
        { "occlusion", UI_FAMILY_GRAPHICS }, { "drawtex", UI_FAMILY_GRAPHICS },
        { "query", UI_FAMILY_GRAPHICS },    { "dma", UI_FAMILY_GRAPHICS },
        { "blit2d", UI_FAMILY_GRAPHICS },
        { "fs", UI_FAMILY_FS },             { "sched", UI_FAMILY_KERNEL },
        { "binder", UI_FAMILY_DISPLAY },
        { "vic", UI_FAMILY_VIDEO },         { "nvdec", UI_FAMILY_VIDEO },
        { "audio", UI_FAMILY_AUDIO },       { "hwopus", UI_FAMILY_SERVICE },
        { "am", UI_FAMILY_SERVICE },         { "fsp", UI_FAMILY_SERVICE },
        { "account", UI_FAMILY_SERVICE },    { "ldn", UI_FAMILY_SERVICE },
        { "set", UI_FAMILY_SERVICE },        { "csrng", UI_FAMILY_SERVICE },
        { "apm", UI_FAMILY_SERVICE },        { "nfp", UI_FAMILY_SERVICE },
        { "nifm", UI_FAMILY_NET },
        { "dns", UI_FAMILY_NET },           { "bsd", UI_FAMILY_NET },
        { "ssl", UI_FAMILY_NET },           { "acc", UI_FAMILY_NET },
        { "friend", UI_FAMILY_NET },
    };
    const size_t count = sizeof(SUITES) / sizeof(SUITES[0]);
    uint32_t wrong = 0;
    const char *first = "none";
    for (size_t i = 0; i < count; ++i) {
        if (UiFamilyOf(SUITES[i].suite) != SUITES[i].family && wrong++ == 0u) {
            first = SUITES[i].suite;
        }
    }
    CHECK_TRUE(SUITE, "every suite a run reports is shown under its own family", CHECK_NONE,
               wrong == 0u && count == 56u && UiFamilyOf("weird") == UI_FAMILY_OTHER &&
                   UiFamilyOf(NULL) == UI_FAMILY_OTHER,
               "%u of %u suites under the wrong family, the first %s", (unsigned)wrong,
               (unsigned)count, first);
}

/// One string field of a line with its escapes undone, as a reader of the report gets it back.
static bool FieldOf(const char *line, const char *key, char *out, size_t out_size) {
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *at = strstr(line, pattern);
    if (at == NULL || out_size == 0u) {
        return false;
    }
    at += strlen(pattern);
    size_t w = 0;
    while (*at != '\0' && *at != '"' && w + 1u < out_size) {
        if (*at == '\\' && at[1] != '\0') {
            ++at;
            out[w++] = *at == 'n' ? '\n' : *at;
        } else {
            out[w++] = *at;
        }
        ++at;
    }
    out[w] = '\0';
    return *at == '"';
}

/// What the store keeps of one case, against what a reader of that case's line gets back. The
/// host run's own lines all pass with no detail, so the store's case above never sees a detail,
/// an escape or a skip being copied; this one does.
static void CheckRecordFill(void) {
    // Longer than the line holds, with quotes, backslashes, a tab and a newline in it, so the
    // line's escaping and the record's copy have to stop at the same character.
    static const char PIECE[] = "said \"no\" \\ then\ttab\nline ";
    char name[300];
    char detail[900];
    name[0] = '\0';
    detail[0] = '\0';
    while (strlen(name) + sizeof(PIECE) < sizeof(name)) {
        strcat(name, PIECE);
    }
    while (strlen(detail) + sizeof(PIECE) < sizeof(detail)) {
        strcat(detail, PIECE);
    }
    static ReportRecord kept;
    ReportRecordFill(&kept, "acc", name, "none", REPORT_SKIP, detail, -1);
    static char line[REPORT_LINE_MAX];
    ReportBuildResultLine(line, sizeof(line), "acc", name, "none", "skip", "not configured",
                          detail, NULL);
    static char read_name[REPORT_KEPT_NAME];
    static char read_detail[REPORT_KEPT_DETAIL];
    char read_suite[REPORT_KEPT_SUITE];
    const bool parsed = FieldOf(line, "name", read_name, sizeof(read_name)) &&
                        FieldOf(line, "detail", read_detail, sizeof(read_detail)) &&
                        FieldOf(line, "suite", read_suite, sizeof(read_suite));
    const bool same = parsed && strcmp(kept.name, read_name) == 0 &&
                      strcmp(kept.detail, read_detail) == 0 &&
                      strcmp(kept.suite, read_suite) == 0 && strcmp(kept.tier, "none") == 0 &&
                      kept.result == REPORT_SKIP && kept.picture == -1 &&
                      strchr(kept.detail, '\t') == NULL && strchr(kept.detail, '\n') != NULL &&
                      strlen(kept.detail) < strlen(detail);
    ReportRecordFill(&kept, "clear", "a", "exact", REPORT_FAIL, NULL, 7);
    const bool empty_right = kept.detail[0] == '\0' && kept.picture == 7 &&
                             kept.result == REPORT_FAIL && strcmp(kept.name, "a") == 0;
    CHECK_TRUE(SUITE, "a kept case holds what a reader of its line gets back, and no more",
               CHECK_NONE, same && empty_right,
               "parsed %d; name %u against %u, detail %u against %u; with no detail \"%.20s\"",
               (int)parsed, (unsigned)strlen(read_name), (unsigned)strlen(kept.name),
               (unsigned)strlen(read_detail), (unsigned)strlen(kept.detail), kept.detail);
}

static void CheckOverflow(Fixture *f) {
    // Eight suites more than the index holds, of one case each: it holds its limit and counts
    // the rest.
    const uint32_t suites = UI_MAX_GROUPS + 8u;
    for (uint32_t i = 0; i < suites; ++i) {
        char suite[16];
        snprintf(suite, sizeof(suite), "suite%02u", (unsigned)i);
        Fill(&f->records[i], suite, "a case", "none", REPORT_PASS, "", -1);
    }
    UiRunIndex(&f->small, f->records, suites, suites);
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    UiCompose(&frame, &f->small, UiStart(&f->small), NULL);
    const uint32_t said = CountColour(&frame, 0, 50, FRAME_WIDTH, 62, SKIPPED);
    const bool counted = f->small.group_count == UI_MAX_GROUPS && f->small.unplaced == 8u;

    // A run with every family has forty suites, and every one of them has a group of its own.
    UiRunIndex(&f->small, f->records, 40, 40);
    const bool whole_run = f->small.group_count == 40u && f->small.unplaced == 0u;

    // And a run whose store ran out: the report said more cases than the screens hold.
    UiRunIndex(&f->small, f->records, 3, 9);
    UiCompose(&frame, &f->small, UiStart(&f->small), NULL);
    const uint32_t short_said = CountColour(&frame, 0, 50, FRAME_WIDTH, 62, SKIPPED);
    UiRunIndex(&f->small, f->records, 3, 3);
    UiCompose(&frame, &f->small, UiStart(&f->small), NULL);
    const uint32_t quiet = CountColour(&frame, 0, 50, FRAME_WIDTH, 62, SKIPPED);
    CHECK_TRUE(SUITE, "cases the screens could not hold are counted and the overview says so",
               CHECK_NONE,
               counted && whole_run && said > 50u && short_said > 50u && quiet == 0u,
               "past the limit counted %d, a whole run placed %d; %u, %u and %u pixels of the "
               "warning",
               (int)counted, (int)whole_run, (unsigned)said, (unsigned)short_said,
               (unsigned)quiet);
}

/// The walk a person makes: the overview, into the first failing group, to a case with pictures
/// and through its views, back, to the next failing group and one of its cases, and across to a
/// net group. Every step is checked, and each screen along the way is composed and kept.
static void CheckWalk(Fixture *f) {
    const UiRun *const run = &f->run;
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    const uint32_t intmath = GroupNamed(run, "intmath");
    const uint32_t ssl = GroupNamed(run, "ssl");
    const uint32_t nifm = GroupNamed(run, "nifm");
    const uint32_t before_drawing = CaseNamed(run, intmath, FAILS_BEFORE_DRAWING);
    const uint32_t with_picture = CaseNamed(run, intmath, FAILS_WITH_PICTURE);
    const uint32_t in_ssl = CaseNamed(run, ssl, FAILS_IN_SSL);
    uint32_t step = 0;
    uint32_t failed_at = 0;
#define EXPECT(condition)                                                                      \
    do {                                                                                       \
        ++step;                                                                                \
        if (!(condition) && failed_at == 0u) {                                                 \
            failed_at = step;                                                                  \
        }                                                                                      \
    } while (0)
#define SHOW(state, name)                                                                      \
    do {                                                                                       \
        UiCompose(&frame, run, (state), UiOpenPicture(run, (state)) >= 0 ? &f->picture : NULL); \
        DumpFrame(&frame, (name));                                                             \
    } while (0)

    UiState state = UiStart(run);
    EXPECT(state.level == UI_LEVEL_OVERVIEW && state.group == intmath && state.changed);
    SHOW(state, "1-overview");

    state = UiStep(state, UI_INPUT_OPEN, run);
    EXPECT(state.level == UI_LEVEL_GROUP && state.group == intmath &&
           state.index == before_drawing);
    SHOW(state, "2-group-intmath");

    state = UiStep(state, UI_INPUT_NEXT_FAILURE, run);
    EXPECT(state.index == with_picture);
    state = UiStep(state, UI_INPUT_OPEN, run);
    EXPECT(state.level == UI_LEVEL_CASE && state.index == with_picture &&
           state.view == UI_VIEW_ALL);
    SHOW(state, "3-case-intmath-pictures");

    for (uint32_t i = 0; i < 3u; ++i) {
        state = UiStep(state, UI_INPUT_CYCLE_VIEW, run);
    }
    EXPECT(state.view == UI_VIEW_DIFF);
    SHOW(state, "4-case-intmath-judged");
    state = UiStep(state, UI_INPUT_CYCLE_VIEW, run);
    EXPECT(state.view == UI_VIEW_ALL);

    state = UiStep(state, UI_INPUT_BACK, run);
    EXPECT(state.level == UI_LEVEL_GROUP && state.group == intmath &&
           state.index == with_picture);
    SHOW(state, "5-group-intmath-back");

    state = UiStep(state, UI_INPUT_BACK, run);
    EXPECT(state.level == UI_LEVEL_OVERVIEW && state.group == intmath);
    state = UiStep(state, UI_INPUT_NEXT_FAILURE, run);
    EXPECT(state.group == ssl);
    state = UiStep(state, UI_INPUT_OPEN, run);
    EXPECT(state.level == UI_LEVEL_GROUP && state.index == in_ssl);
    state = UiStep(state, UI_INPUT_OPEN, run);
    EXPECT(state.level == UI_LEVEL_CASE && UiSelected(run, state) != NULL &&
           UiSelected(run, state)->picture < 0);
    SHOW(state, "6-case-ssl-detail");

    state = UiStep(state, UI_INPUT_BACK, run);
    for (uint32_t i = 0; i < 3u; ++i) {
        state = UiStep(state, UI_INPUT_LEFT, run);
    }
    EXPECT(state.level == UI_LEVEL_GROUP && state.group == nifm && state.index == 0u);
    state = UiStep(state, UI_INPUT_DOWN, run);
    state = UiStep(state, UI_INPUT_DOWN, run);
    EXPECT(UiSelected(run, state) != NULL && UiSelected(run, state)->result == REPORT_SKIP);
    SHOW(state, "7-group-nifm");

    state = UiStep(state, UI_INPUT_QUIT, run);
    EXPECT(state.quit);
#undef EXPECT
#undef SHOW
    CHECK_TRUE(SUITE, "the walk from the overview to a case and back lands where it should",
               CHECK_NONE,
               failed_at == 0u && intmath != UINT32_MAX && ssl != UINT32_MAX &&
                   nifm != UINT32_MAX,
               "step %u of %u went somewhere else", (unsigned)failed_at, (unsigned)step);
}

static void CheckNavigation(Fixture *f) {
    const UiRun *const run = &f->run;
    const uint32_t intmath = GroupNamed(run, "intmath");
    const uint32_t ssl = GroupNamed(run, "ssl");
    const uint32_t count = run->groups[intmath].count;

    // The screen owes a frame before anything has been pressed, or the first thing a person
    // sees is whatever the last program left in the window.
    UiState fresh = UiStep(UiStart(run), UI_INPUT_NONE, run);
    const bool asked_first = fresh.changed;
    UiFrameDrawn(&fresh);
    fresh = UiStep(fresh, UI_INPUT_NONE, run);
    CHECK_TRUE(SUITE, "a frame is owed until one is drawn, and not again until something moves",
               CHECK_NONE,
               asked_first && !fresh.changed && UiStep(fresh, UI_INPUT_DOWN, run).changed &&
                   !UiStep(fresh, UI_INPUT_BACK, run).changed,
               "asked first %d, asked again %d", (int)asked_first, (int)fresh.changed);

    // Round the whole group one way, and one step the other way from the top.
    UiState walk = UiStep(UiStart(run), UI_INPUT_OPEN, run);
    const uint32_t from = walk.index;
    for (uint32_t i = 0; i < count; ++i) {
        walk = UiStep(walk, UI_INPUT_DOWN, run);
    }
    UiState top = walk;
    top.index = 0;
    top = UiStep(top, UI_INPUT_UP, run);
    CHECK_TRUE(SUITE, "stepping through a group's cases comes round to where it started",
               CHECK_NONE, walk.index == from && top.index == count - 1u,
               "back at %u from %u; up from the top lands on %u of %u", (unsigned)walk.index,
               (unsigned)from, (unsigned)top.index, (unsigned)count);

    // The failure jumps skip passes and skips alike, and come round, on the overview and in a
    // group.
    UiState over = UiStart(run);
    over = UiStep(over, UI_INPUT_NEXT_FAILURE, run);
    const uint32_t second = over.group;
    over = UiStep(over, UI_INPUT_NEXT_FAILURE, run);
    const uint32_t round = over.group;
    over = UiStep(over, UI_INPUT_PREVIOUS_FAILURE, run);
    UiState in_group = UiStep(UiStart(run), UI_INPUT_OPEN, run);
    const uint32_t first_failure = in_group.index;
    in_group = UiStep(in_group, UI_INPUT_NEXT_FAILURE, run);
    const uint32_t next_failure = in_group.index;
    in_group = UiStep(in_group, UI_INPUT_NEXT_FAILURE, run);
    CHECK_TRUE(SUITE, "the failure jumps skip every pass and skip and come round", CHECK_NONE,
               second == ssl && round == intmath && over.group == ssl &&
                   next_failure == CaseNamed(run, intmath, FAILS_WITH_PICTURE) &&
                   in_group.index == first_failure,
               "groups %u then %u then back to %u; cases %u then %u then %u", (unsigned)second,
               (unsigned)round, (unsigned)over.group, (unsigned)first_failure,
               (unsigned)next_failure, (unsigned)in_group.index);

    // Left and right: a family at a time on the overview, a group at a time inside one.
    UiState families = UiStart(run);
    families = UiStep(families, UI_INPUT_RIGHT, run);
    const uint32_t net = families.group;
    families = UiStep(families, UI_INPUT_RIGHT, run);
    const uint32_t judge = families.group;
    families = UiStep(families, UI_INPUT_LEFT, run);
    UiState across = UiStep(UiStart(run), UI_INPUT_OPEN, run);
    across = UiStep(across, UI_INPUT_RIGHT, run);
    CHECK_TRUE(SUITE, "left and right move between families on the overview and groups inside",
               CHECK_NONE,
               net == GroupNamed(run, "nifm") && judge == 0u && families.group == net &&
                   across.level == UI_LEVEL_GROUP && across.group == intmath + 1u &&
                   across.index == 0u,
               "overview right %u then %u, left %u; in a group right to %u at %u",
               (unsigned)net, (unsigned)judge, (unsigned)families.group,
               (unsigned)across.group, (unsigned)across.index);

    // The view cycles on a case that kept pictures and nowhere else, so the one a person chose
    // is still the one showing when they reach the next drawing.
    UiState seen = UiStart(run);
    seen.level = UI_LEVEL_CASE;
    seen.group = intmath;
    seen.index = CaseNamed(run, intmath, FAILS_BEFORE_DRAWING);
    UiFrameDrawn(&seen);
    const UiState unmoved = UiStep(seen, UI_INPUT_CYCLE_VIEW, run);
    seen.index = CaseNamed(run, intmath, FAILS_WITH_PICTURE);
    UiState cycled = seen;
    for (uint32_t i = 0; i < 4u; ++i) {
        cycled = UiStep(cycled, UI_INPUT_CYCLE_VIEW, run);
    }
    const UiState once = UiStep(seen, UI_INPUT_CYCLE_VIEW, run);
    CHECK_TRUE(SUITE, "the view cycles only on a case that kept pictures", CHECK_NONE,
               unmoved.view == UI_VIEW_ALL && !unmoved.changed && once.view == UI_VIEW_GOT &&
                   cycled.view == UI_VIEW_ALL,
               "without pictures view %d changed %d; with them once %d, four times %d",
               (int)unmoved.view, (int)unmoved.changed, (int)once.view, (int)cycled.view);

    // Quit from every level, and a run that kept nothing can still leave: without that the only
    // way out would be to kill the process.
    uint32_t stuck = 0;
    for (uint32_t level = UI_LEVEL_OVERVIEW; level <= UI_LEVEL_CASE; ++level) {
        UiState at = UiStart(run);
        at.level = (UiLevel)level;
        if (!UiStep(at, UI_INPUT_QUIT, run).quit) {
            ++stuck;
        }
    }
    UiRunIndex(&f->small, NULL, 0, 0);
    UiState empty = UiStart(&f->small);
    empty = UiStep(empty, UI_INPUT_DOWN, &f->small);
    empty = UiStep(empty, UI_INPUT_OPEN, &f->small);
    CHECK_TRUE(SUITE, "quit leaves from every level, and a run that kept nothing can leave",
               CHECK_NONE,
               stuck == 0u && !empty.quit && empty.changed &&
                   UiStep(empty, UI_INPUT_QUIT, &f->small).quit &&
                   UiStep(empty, UI_INPUT_QUIT, NULL).quit,
               "%u levels would not quit; the empty run quit %d, owed a frame %d",
               (unsigned)stuck, (int)empty.quit, (int)empty.changed);
}

static void CheckButtons(void) {
    // The photograph bug. A controller arriving with the window reports its whole mask as
    // newly pressed, and every one of those was obeyed before a person had touched anything,
    // so the screen opened on a view nobody had chosen.
    uint32_t obeyed_early = 0;
    for (uint32_t f = 0; f < UI_SETTLING_FRAMES; ++f) {
        if (UiInputFromButtons(0xFFFFFFFFu, f) != UI_INPUT_NONE) {
            ++obeyed_early;
        }
    }
    CHECK_TRUE(SUITE, "a press before the pad has settled is not a press", CHECK_NONE,
               obeyed_early == 0u &&
                   UiInputFromButtons(UI_BUTTON_OPEN, UI_SETTLING_FRAMES) == UI_INPUT_OPEN,
               "%u of the first %u frames were obeyed", (unsigned)obeyed_early,
               (unsigned)UI_SETTLING_FRAMES);

    // One button, one meaning; leaving ahead of everything, and going back ahead of moving.
    static const struct {
        uint32_t button;
        UiInput means;
    } BUTTONS[] = {
        { UI_BUTTON_UP, UI_INPUT_UP },
        { UI_BUTTON_DOWN, UI_INPUT_DOWN },
        { UI_BUTTON_LEFT, UI_INPUT_LEFT },
        { UI_BUTTON_RIGHT, UI_INPUT_RIGHT },
        { UI_BUTTON_OPEN, UI_INPUT_OPEN },
        { UI_BUTTON_BACK, UI_INPUT_BACK },
        { UI_BUTTON_NEXT_FAILURE, UI_INPUT_NEXT_FAILURE },
        { UI_BUTTON_PREVIOUS_FAILURE, UI_INPUT_PREVIOUS_FAILURE },
        { UI_BUTTON_CYCLE_VIEW, UI_INPUT_CYCLE_VIEW },
        { UI_BUTTON_QUIT, UI_INPUT_QUIT },
        { UI_BUTTON_BACK | UI_BUTTON_DOWN, UI_INPUT_BACK },
        { 0xFFFFFFFFu, UI_INPUT_QUIT },
        { 0u, UI_INPUT_NONE },
    };
    uint32_t mismapped = 0;
    for (size_t i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); ++i) {
        if (UiInputFromButtons(BUTTONS[i].button, UI_SETTLING_FRAMES) != BUTTONS[i].means) {
            ++mismapped;
        }
    }
    CHECK_TRUE(SUITE, "every button means one thing, and everything at once still quits",
               CHECK_NONE, mismapped == 0u, "%u of %u masks meant something else",
               (unsigned)mismapped, (unsigned)(sizeof(BUTTONS) / sizeof(BUTTONS[0])));

    // A held direction: nothing for the delay, then once every few frames, and only after a
    // press that counted. Held from before the settling, it never repeats at all.
    UiRepeat repeat = { 0, 0 };
    uint32_t early = 0;
    for (uint32_t f = 0; f < UI_SETTLING_FRAMES + 60u; ++f) {
        const uint32_t down = f == 0u ? UI_BUTTON_DOWN : 0u;
        if ((UiRepeatButtons(&repeat, UI_BUTTON_DOWN, down, f) & UI_BUTTON_DOWN) != 0u && f > 0u) {
            ++early;
        }
    }
    repeat = (UiRepeat){ 0, 0 };
    const uint32_t start = UI_SETTLING_FRAMES + 1u;
    uint32_t first_repeat = 0;
    uint32_t repeats = 0;
    const bool pressed = (UiRepeatButtons(&repeat, UI_BUTTON_DOWN, UI_BUTTON_DOWN, start) &
                          UI_BUTTON_DOWN) != 0u;
    for (uint32_t held = 1; held <= UI_REPEAT_DELAY_FRAMES + 4u * UI_REPEAT_EVERY_FRAMES; ++held) {
        if ((UiRepeatButtons(&repeat, UI_BUTTON_DOWN, 0u, start + held) & UI_BUTTON_DOWN) != 0u) {
            if (repeats++ == 0u) {
                first_repeat = held;
            }
        }
    }
    // Let go, then held again without a press: nothing.
    UiRepeatButtons(&repeat, 0u, 0u, start + 100u);
    uint32_t unpressed = 0;
    for (uint32_t held = 0; held < 40u; ++held) {
        unpressed += (UiRepeatButtons(&repeat, UI_BUTTON_DOWN, 0u, start + 101u + held) &
                      UI_BUTTON_DOWN) != 0u;
    }
    // A button that is not a direction never repeats.
    repeat = (UiRepeat){ 0, 0 };
    UiRepeatButtons(&repeat, UI_BUTTON_OPEN, UI_BUTTON_OPEN, start);
    uint32_t opened = 0;
    for (uint32_t held = 1; held < 40u; ++held) {
        opened += UiRepeatButtons(&repeat, UI_BUTTON_OPEN, 0u, start + held) != 0u;
    }
    CHECK_TRUE(SUITE, "a held direction repeats after a delay, and only after a press that counted",
               CHECK_NONE,
               early == 0u && pressed && first_repeat == UI_REPEAT_DELAY_FRAMES &&
                   repeats == 5u && unpressed == 0u && opened == 0u,
               "before settling %u; first repeat after %u frames, %u of them; held again %u, "
               "open %u",
               (unsigned)early, (unsigned)first_repeat, (unsigned)repeats, (unsigned)unpressed,
               (unsigned)opened);

    // What repeats is the direction pressed, whatever else the pad reports held: down rolled
    // into down and left repeats left; up held since before the pad settled, then down pressed,
    // repeats down, and goes on repeating it once up is let go.
    const uint32_t LEFT = UI_BUTTON_LEFT;
    const uint32_t DOWN = UI_BUTTON_DOWN;
    repeat = (UiRepeat){ 0, 0 };
    UiRepeatButtons(&repeat, DOWN, DOWN, start);
    for (uint32_t held = 1; held < 6u; ++held) {
        UiRepeatButtons(&repeat, DOWN, 0u, start + held);
    }
    UiRepeatButtons(&repeat, DOWN | LEFT, LEFT, start + 6u);
    uint32_t rolled = 0;
    for (uint32_t held = 1; held <= UI_REPEAT_DELAY_FRAMES; ++held) {
        rolled = UiRepeatButtons(&repeat, DOWN | LEFT, 0u, start + 6u + held);
    }
    repeat = (UiRepeat){ 0, 0 };
    for (uint32_t f = 0; f < start; ++f) {
        UiRepeatButtons(&repeat, UI_BUTTON_UP, f == 0u ? UI_BUTTON_UP : 0u, f);
    }
    UiRepeatButtons(&repeat, UI_BUTTON_UP | DOWN, DOWN, start);
    uint32_t stale = 0;
    for (uint32_t held = 1; held <= UI_REPEAT_DELAY_FRAMES; ++held) {
        stale = UiRepeatButtons(&repeat, UI_BUTTON_UP | DOWN, 0u, start + held);
    }
    uint32_t released = 0;
    for (uint32_t held = 1; held <= UI_REPEAT_EVERY_FRAMES; ++held) {
        released = UiRepeatButtons(&repeat, DOWN, 0u, start + UI_REPEAT_DELAY_FRAMES + held);
    }
    CHECK_TRUE(SUITE, "what repeats is the direction pressed, whatever else is held",
               CHECK_NONE, rolled == LEFT && stale == DOWN && released == DOWN,
               "rolled into a second direction 0x%x, pressed beside a stale one 0x%x, after "
               "letting the stale one go 0x%x",
               (unsigned)rolled, (unsigned)stale, (unsigned)released);
}

static void CheckScreens(Fixture *f) {
    const UiRun *const run = &f->run;
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };

    // The overview: red in a group's row exactly where the group failed, amber exactly where it
    // skipped, and the cursor's band on the one group it is on.
    const UiState start = UiStart(run);
    UiCompose(&frame, run, start, NULL);
    const uint32_t rows = UiListRows(UI_LEVEL_OVERVIEW, FRAME_HEIGHT);
    const uint32_t total = UiOverviewRow(run, run->group_count - 1u) + 1u;
    const uint32_t first = UiListFirst(UiOverviewRow(run, start.group), total, rows);
    uint32_t wrong_rows = 0;
    uint32_t first_wrong = UINT32_MAX;
    for (uint32_t g = 0; g < run->group_count; ++g) {
        const uint32_t row = UiOverviewRow(run, g);
        if (row < first || row >= first + rows) {
            ++wrong_rows;
            continue;
        }
        const uint32_t y = UI_LIST_TOP + (row - first) * UI_ROW_HEIGHT;
        const uint32_t y1 = y + UI_ROW_HEIGHT;
        const bool red = CountColour(&frame, 0, y, FRAME_WIDTH, y1, FAILED) > 0u;
        const bool amber = CountColour(&frame, 0, y, FRAME_WIDTH, y1, SKIPPED) > 0u;
        const bool band = CountColour(&frame, 0, y, FRAME_WIDTH, y1, SELECTED) > 0u;
        if (red != (run->groups[g].failed > 0u) || amber != (run->groups[g].skipped > 0u) ||
            band != (g == start.group)) {
            ++wrong_rows;
            if (first_wrong == UINT32_MAX) {
                first_wrong = g;
            }
        }
    }
    CHECK_TRUE(SUITE, "the overview marks failed groups red, skipped ones amber, and the cursor",
               CHECK_NONE, wrong_rows == 0u && total == run->group_count + 4u,
               "%u of %u rows wrong, the first %s; %u rows in all",
               (unsigned)wrong_rows, (unsigned)run->group_count,
               first_wrong < run->group_count ? run->groups[first_wrong].suite : "none",
               (unsigned)total);

    // A group: each case's mark in its result's colour and no other, in the column the marks
    // are drawn in.
    UiState in_ssl = UiStart(run);
    in_ssl.level = UI_LEVEL_GROUP;
    in_ssl.group = GroupNamed(run, "ssl");
    in_ssl.index = 0;
    UiCompose(&frame, run, in_ssl, NULL);
    uint32_t mismarked = 0;
    const UiGroup *const ssl = &run->groups[in_ssl.group];
    for (uint32_t i = 0; i < ssl->count; ++i) {
        const ReportRecord *const record = UiCaseAt(run, in_ssl.group, i);
        const uint32_t y = UI_LIST_TOP + i * UI_ROW_HEIGHT;
        const uint32_t x0 = UI_MARK_LEFT;
        const uint32_t x1 = UI_MARK_LEFT + 4u * 16u;
        const uint32_t pass = CountColour(&frame, x0, y, x1, y + UI_ROW_HEIGHT, PASSED);
        const uint32_t fail = CountColour(&frame, x0, y, x1, y + UI_ROW_HEIGHT, FAILED);
        const uint32_t skip = CountColour(&frame, x0, y, x1, y + UI_ROW_HEIGHT, SKIPPED);
        const bool right = record->result == REPORT_PASS   ? pass > 0u && fail + skip == 0u
                           : record->result == REPORT_FAIL ? fail > 0u && pass + skip == 0u
                                                           : skip > 0u && pass + fail == 0u;
        mismarked += right ? 0u : 1u;
    }
    CHECK_TRUE(SUITE, "a group marks every case in its result's colour", CHECK_NONE,
               mismarked == 0u && ssl->passed > 0u && ssl->failed > 0u && ssl->skipped > 0u,
               "%u of %u marks wrong", (unsigned)mismarked, (unsigned)ssl->count);

    // A long group keeps the cursor on screen, in the middle where it can.
    const uint32_t group_rows = UiListRows(UI_LEVEL_GROUP, FRAME_HEIGHT);
    UiState deep = UiStart(run);
    deep.level = UI_LEVEL_GROUP;
    deep.group = GroupNamed(run, "reference");
    deep.index = run->groups[deep.group].count - 1u;
    UiCompose(&frame, run, deep, NULL);
    const uint32_t last_row = UI_LIST_TOP + (group_rows - 1u) * UI_ROW_HEIGHT;
    const uint32_t band_at_end =
        CountColour(&frame, 0, last_row, FRAME_WIDTH, last_row + UI_ROW_HEIGHT, SELECTED);
    CHECK_TRUE(SUITE, "a long group scrolls to keep the selected case on screen", CHECK_NONE,
               group_rows >= 20u && UiListFirst(0, 118, 23) == 0u &&
                   UiListFirst(117, 118, 23) == 95u && UiListFirst(50, 118, 23) == 39u &&
                   UiListFirst(5, 10, 23) == 0u && band_at_end > 1000u,
               "%u rows; first rows %u, %u, %u; %u pixels of the cursor on the last row",
               (unsigned)group_rows, (unsigned)UiListFirst(0, 118, 23),
               (unsigned)UiListFirst(117, 118, 23), (unsigned)UiListFirst(50, 118, 23),
               (unsigned)band_at_end);

    // The selected case's detail under the list: the same case with and without one, so the
    // only thing that differs is the detail.
    ReportRecord *const failing =
        (ReportRecord *)UiCaseAt(run, in_ssl.group, CaseNamed(run, in_ssl.group, FAILS_IN_SSL));
    char saved[sizeof(failing->detail)];
    memcpy(saved, failing->detail, sizeof(saved));
    in_ssl.index = CaseNamed(run, in_ssl.group, FAILS_IN_SSL);
    const uint32_t list_end = UI_LIST_TOP + group_rows * UI_ROW_HEIGHT;
    UiCompose(&frame, run, in_ssl, NULL);
    const uint32_t with_lines = TextLines(&frame, list_end, FRAME_HEIGHT - 32u);
    failing->detail[0] = '\0';
    UiCompose(&frame, run, in_ssl, NULL);
    const uint32_t without_lines = TextLines(&frame, list_end, FRAME_HEIGHT - 32u);
    memcpy(failing->detail, saved, sizeof(saved));
    CHECK_TRUE(SUITE, "a group shows the selected case's detail under its list, wrapped",
               CHECK_NONE, with_lines == without_lines + UiWrappedLines(saved, 76u) - 1u,
               "%u lines with the detail, %u without, the detail wraps to %u",
               (unsigned)with_lines, (unsigned)without_lines,
               (unsigned)UiWrappedLines(saved, 76u));

    // A case with no pictures opens on a page with all of its detail, however long.
    char long_detail[sizeof(failing->detail)];
    memset(long_detail, 0, sizeof(long_detail));
    while (strlen(long_detail) + 40u < sizeof(long_detail)) {
        strcat(long_detail, "a long detail keeps going past a line ");
    }
    memcpy(failing->detail, long_detail, sizeof(long_detail));
    UiState page = in_ssl;
    page.level = UI_LEVEL_CASE;
    UiCompose(&frame, run, page, NULL);
    const uint32_t page_lines = TextLines(&frame, 36u, FRAME_HEIGHT - 32u);
    const uint32_t wanted = UiWrappedLines(failing->name, 76u) + 1u +
                            UiWrappedLines(long_detail, 76u);
    // The viewer asks the same function which picture to hand over, and it says none here.
    const int32_t page_picture = UiOpenPicture(run, page);
    memcpy(failing->detail, saved, sizeof(saved));
    CHECK_TRUE(SUITE, "a case with no pictures opens on its whole detail", CHECK_NONE,
               page_lines == wanted && page_picture < 0,
               "%u lines of text, wanted %u; picture %d", (unsigned)page_lines,
               (unsigned)wanted, (int)page_picture);

    // And a drawing case on its pictures: the judge's red where the rows came back wrong, in
    // the third panel, which sits where the three side by side put it.
    UiState pictures = UiStart(run);
    pictures.level = UI_LEVEL_CASE;
    pictures.group = GroupNamed(run, "intmath");
    pictures.index = CaseNamed(run, pictures.group, FAILS_WITH_PICTURE);
    UiCompose(&frame, run, pictures, &f->picture);
    const uint32_t scale = (FRAME_WIDTH - 4u * 32u) / 3u / PICTURE_SIDE;
    const uint32_t diff_x = 32u + 2u * (PICTURE_SIDE * scale + 32u);
    const uint8_t *const bad =
        UiPixel(&frame, diff_x + scale / 2u, UI_PANEL_TOP + WRONG_FROM_ROW * scale + scale / 2u);
    const uint8_t *const good = UiPixel(&frame, diff_x + scale / 2u, UI_PANEL_TOP + scale / 2u);
    uint8_t accepted[3];
    uint8_t refused[3];
    PictureDiffColour(PICTURE_MATCHED, accepted);
    PictureDiffColour(PICTURE_WRONG, refused);
    // Only the case level hands a picture over; the same case selected in its group does not.
    UiState listed = pictures;
    listed.level = UI_LEVEL_GROUP;
    const int32_t opened = UiOpenPicture(run, pictures);
    const int32_t in_list = UiOpenPicture(run, listed);
    CHECK_TRUE(SUITE, "a drawing case opens on its pictures", CHECK_NONE,
               bad != NULL && good != NULL && memcmp(bad, refused, 3u) == 0 &&
                   memcmp(good, accepted, 3u) == 0 && opened == 0 && in_list < 0,
               "the wrong row reads %02x%02x%02x, the right one %02x%02x%02x; picture %d open, "
               "%d in the list",
               bad != NULL ? bad[0] : 0, bad != NULL ? bad[1] : 0, bad != NULL ? bad[2] : 0,
               good != NULL ? good[0] : 0, good != NULL ? good[1] : 0,
               good != NULL ? good[2] : 0, (int)opened, (int)in_list);

    // Every screen leaves the countdown's rows alone and says what the buttons do on its last
    // line; the countdown draws over nothing, and nothing draws over it.
    const struct {
        UiLevel level;
        uint32_t group;
        const char *index_name;
        UiView view;
    } SCREENS[] = {
        { UI_LEVEL_OVERVIEW, pictures.group, NULL, UI_VIEW_ALL },
        { UI_LEVEL_GROUP, pictures.group, FAILS_WITH_PICTURE, UI_VIEW_ALL },
        { UI_LEVEL_GROUP, in_ssl.group, FAILS_IN_SSL, UI_VIEW_ALL },
        { UI_LEVEL_CASE, in_ssl.group, FAILS_IN_SSL, UI_VIEW_ALL },
        { UI_LEVEL_CASE, pictures.group, FAILS_WITH_PICTURE, UI_VIEW_ALL },
        { UI_LEVEL_CASE, pictures.group, FAILS_WITH_PICTURE, UI_VIEW_DIFF },
    };
    uint32_t crowded = 0;
    uint32_t unlabelled = 0;
    uint32_t barless = 0;
    for (size_t i = 0; i < sizeof(SCREENS) / sizeof(SCREENS[0]); ++i) {
        UiState at = UiStart(run);
        at.level = SCREENS[i].level;
        at.group = SCREENS[i].group;
        at.index = SCREENS[i].index_name != NULL
                       ? CaseNamed(run, SCREENS[i].group, SCREENS[i].index_name)
                       : 0u;
        at.view = SCREENS[i].view;
        UiCompose(&frame, run, at, UiOpenPicture(run, at) >= 0 ? &f->picture : NULL);
        // Left of the touch bar, which sits over the foot at the right.
        const uint32_t right = UiFootRight(FRAME_WIDTH);
        crowded += CountLitIn(&frame, 0, FRAME_HEIGHT - 31u, right, FRAME_HEIGHT - 15u) > 0u;
        unlabelled +=
            CountLitIn(&frame, 0, FRAME_HEIGHT - 14u, right, FRAME_HEIGHT - 6u) > 100u ? 0u : 1u;
        const UiRect box = UiTouchBox(FRAME_WIDTH, FRAME_HEIGHT, 1u, 0u);
        barless += CountColour(&frame, box.x + 1u, box.y + 1u, box.x + box.width - 1u,
                               box.y + box.height - 1u, SELECTED) > 100u
                       ? 0u
                       : 1u;
    }
    CHECK_TRUE(SUITE, "every screen leaves the countdown its rows and names its buttons",
               CHECK_NONE, crowded == 0u && unlabelled == 0u && barless == 0u,
               "%u screens drew into the countdown's rows, %u had no button line, %u no touch bar",
               (unsigned)crowded, (unsigned)unlabelled, (unsigned)barless);
}

/// The corners a real run does not reach: a frame smaller than the console's, a group of hundreds
/// of failures, and a failure or a skip whose line carried no detail.
static void CheckEdges(Fixture *f) {
    // Composed into the front of the big buffer, with everything after the small frame's last
    // byte set to a value nothing draws, so a subtraction that wrapped shows up as a write there.
    static const struct {
        uint32_t width;
        uint32_t height;
    } SMALL[] = { { 24u, 100u }, { 1280u, 140u }, { 639u, 720u } };
    uint32_t spilled = 0;
    uint32_t shouted = 0;
    for (size_t i = 0; i < sizeof(SMALL) / sizeof(SMALL[0]); ++i) {
        const UiFrame small = { f->pixels, SMALL[i].width, SMALL[i].height,
                                SMALL[i].width * 4u };
        const size_t used = (size_t)small.pitch * small.height;
        memset(f->pixels + used, 0xA5, sizeof(f->pixels) - used);
        UiCompose(&small, &f->run, UiStart(&f->run), NULL);
        for (size_t b = used; b < sizeof(f->pixels); ++b) {
            if (f->pixels[b] != 0xA5) {
                ++spilled;
                break;
            }
        }
        shouted += CountColour(&small, 0, 0, small.width, small.height, FAILED);
    }
    CHECK_TRUE(SUITE, "a frame too small for the screens gets one line and nothing past its edge",
               CHECK_NONE, spilled == 0u && shouted == 0u,
               "%u frames wrote past their end, %u pixels of a failure drawn", (unsigned)spilled,
               (unsigned)shouted);

    // Eight hundred failures, one skip and one pass: the two floors of two pixels add up to
    // more than is left, and the bar must still end where the row does.
    uint32_t at = 0;
    Fill(&f->records[at++], "big", "a pass", "exact", REPORT_PASS, "", -1);
    Fill(&f->records[at++], "big", "a skip", "none", REPORT_SKIP, "no reason", -1);
    while (at < 802u) {
        Fill(&f->records[at++], "big", "a failure", "exact", REPORT_FAIL, "wrong", -1);
    }
    UiRunIndex(&f->small, f->records, at, at);
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    UiCompose(&frame, &f->small, UiStart(&f->small), NULL);
    const uint32_t y = UI_LIST_TOP + UiOverviewRow(&f->small, 0) * UI_ROW_HEIGHT;
    const uint32_t bar = 32u + 52u * 16u;
    const uint32_t outside = CountColour(&frame, 0, y, 60u, y + UI_ROW_HEIGHT, PASSED) +
                             CountColour(&frame, FRAME_WIDTH - 31u, y, FRAME_WIDTH,
                                         y + UI_ROW_HEIGHT, PASSED);
    const uint32_t red = CountColour(&frame, bar, y, FRAME_WIDTH, y + UI_ROW_HEIGHT, FAILED);
    const uint32_t amber = CountColour(&frame, bar, y, FRAME_WIDTH, y + UI_ROW_HEIGHT, SKIPPED);
    CHECK_TRUE(SUITE, "a group of hundreds of failures still draws its bar inside its row",
               CHECK_NONE, outside == 0u && red > 1000u && amber > 0u,
               "%u pixels of the bar outside it; %u red and %u amber inside",
               (unsigned)outside, (unsigned)red, (unsigned)amber);

    // The same name, no detail, three results: what is said under the list follows the result.
    Fill(&f->records[0], "quiet", "a case", "exact", REPORT_PASS, "", -1);
    Fill(&f->records[1], "quiet", "a case", "exact", REPORT_FAIL, "", -1);
    Fill(&f->records[2], "quiet", "a case", "none", REPORT_SKIP, "", -1);
    UiRunIndex(&f->small, f->records, 3, 3);
    // Told apart by a hash of the rows under the list, which is where the words are.
    const uint32_t under = UI_LIST_TOP + UiListRows(UI_LEVEL_GROUP, FRAME_HEIGHT) * UI_ROW_HEIGHT;
    const size_t band = (size_t)(FRAME_HEIGHT - 32u - under) * FRAME_WIDTH * 4u;
    uint32_t said[3];
    for (uint32_t i = 0; i < 3u; ++i) {
        UiState at_case = UiStart(&f->small);
        at_case.level = UI_LEVEL_GROUP;
        at_case.index = i;
        UiCompose(&frame, &f->small, at_case, NULL);
        const uint8_t *const bytes = f->pixels + (size_t)under * FRAME_WIDTH * 4u;
        said[i] = 2166136261u;
        for (size_t b = 0; b < band; ++b) {
            said[i] = (said[i] ^ bytes[b]) * 16777619u;
        }
    }
    CHECK_TRUE(SUITE, "a case with no detail is only said to have passed when it did",
               CHECK_NONE, said[0] != said[1] && said[0] != said[2] && said[1] != said[2],
               "under a pass %08x, a failure %08x, a skip %08x", (unsigned)said[0],
               (unsigned)said[1], (unsigned)said[2]);
}

/// The start screen as the mode file @p words (NULL for none) leaves it, in a build with every
/// family.
static UiStartState StartFrom(const char *words) {
    const RunMode mode = ModeFromText(words);
    return UiStartOpen(&mode, MODE_FAMILIES_PRESENT);
}

/// The start screen: shown only with something to choose, its buttons for the two lists, its
/// ten seconds, and the warning exactly while a choice that can end the program is on.
static void CheckStartScreen(Fixture *f) {
    UiStartInfo info;
    memset(&info, 0, sizeof(info));
    info.heading = "edenprobe 0.2.0 (test)";
    const bool nothing = !UiStartShown(&info) && !UiStartShown(NULL);
    info.may_crash_count = 1u;
    info.may_crash_suite[0] = "cpu";
    info.may_crash_name[0] = "sm4ekey and sm4e give the SM4 standard's worked example";
    info.may_crash_family[0] = MODE_FAMILY_CPU;
    const bool something = UiStartShown(&info);

    // Only the may-crash list has cases: X, for the crash list, does nothing; Y turns the choice
    // on and off; plus or A beside Y in one frame begins with the choice as it was; nothing
    // counts while the pad settles.
    UiStartState state = StartFrom(NULL);
    state = UiStartStep(state, UI_START_TOGGLE_MAY_CRASH, UI_SETTLING_FRAMES - 1u, &info);
    const bool settling = !state.may_crash;
    state = UiStartStep(state, UI_START_TOGGLE_RETRY, UI_SETTLING_FRAMES, &info);
    const bool empty_list = !state.retry;
    state = UiStartStep(state, UI_START_TOGGLE_MAY_CRASH, UI_SETTLING_FRAMES, &info);
    const bool on = state.may_crash && !state.begun;
    state = UiStartStep(state, UI_START_TOGGLE_MAY_CRASH, UI_SETTLING_FRAMES + 1u, &info);
    const bool off = !state.may_crash;
    UiStartState with_a = UiStartStep(state, UI_START_SELECT | UI_START_TOGGLE_MAY_CRASH,
                                      UI_SETTLING_FRAMES + 2u, &info);
    state = UiStartStep(state, UI_START_BEGIN | UI_START_TOGGLE_MAY_CRASH,
                        UI_SETTLING_FRAMES + 2u, &info);
    const bool begun = state.begun && !state.may_crash && with_a.begun && !with_a.may_crash;
    const bool from_file = StartFrom("fatal retry").may_crash && StartFrom("fatal retry").retry;
    CHECK_TRUE(SUITE, "the start screen asks only with something to choose, a button a choice",
               CHECK_NONE,
               nothing && something && settling && empty_list && on && off && begun && from_file,
               "shown with nothing %d, with a case %d; settling kept it off %d, the empty list "
               "stayed off %d, Y on %d and off %d, A began as it was %d, the file's words %d",
               (int)!nothing, (int)something, (int)settling, (int)empty_list, (int)on, (int)off,
               (int)begun, (int)from_file);

    const uint64_t second = 1000000000ull;
    CHECK_TRUE(SUITE, "the start screen waits ten seconds for a person, and a press keeps it",
               CHECK_NONE,
               UI_START_NANOSECONDS == 10u * second && !UiStartTimesOut(0, false) &&
                   !UiStartTimesOut(10u * second - 1u, false) &&
                   UiStartTimesOut(10u * second, false) &&
                   !UiStartTimesOut(100u * second, true) && UiStartSecondsLeft(0, false) == 10u &&
                   UiStartSecondsLeft(9u * second + 1u, false) == 1u &&
                   UiStartSecondsLeft(10u * second, false) == 0u &&
                   UiStartSecondsLeft(0, true) == 0u,
               "times out at ten %d, just before %d; seconds left at nought %u",
               (int)UiStartTimesOut(10u * second, false),
               (int)UiStartTimesOut(10u * second - 1u, false),
               (unsigned)UiStartSecondsLeft(0, false));

    // The warning's band holds the warning's red while a choice is on and nothing of it while
    // both are off; the rest of the screen is the same either way.
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    const uint32_t top = UiStartWarningTop(FRAME_HEIGHT);
    const uint32_t bottom = top + UI_START_WARNING_LINES * UI_ROW_HEIGHT + 16u;
    info.listed_count = 1u;
    info.listed_suite[0] = "bcn";
    info.listed_name[0] = "every layer of a six-layer BC4 array keeps its own 2x2 and 1x1 levels";

    // Y and X in one edge are a pad that connected after the settling frames, not two choices.
    // A in that edge still begins. One button, then the other, still chooses both.
    UiStartState both = StartFrom(NULL);
    both = UiStartStep(both, UI_START_TOGGLE_MAY_CRASH | UI_START_TOGGLE_RETRY,
                       UI_SETTLING_FRAMES, &info);
    const bool neither = !both.may_crash && !both.retry && !both.begun;
    UiStartState y_only = StartFrom(NULL);
    y_only = UiStartStep(y_only, UI_START_TOGGLE_MAY_CRASH, UI_SETTLING_FRAMES, &info);
    const bool y_toggles = y_only.may_crash && !y_only.retry && !y_only.begun;
    UiStartState began = StartFrom(NULL);
    began = UiStartStep(began,
                        UI_START_BEGIN | UI_START_TOGGLE_MAY_CRASH | UI_START_TOGGLE_RETRY,
                        UI_SETTLING_FRAMES, &info);
    const bool began_clean = began.begun && !began.may_crash && !began.retry;
    UiStartState apart = StartFrom(NULL);
    apart = UiStartStep(apart, UI_START_TOGGLE_MAY_CRASH, UI_SETTLING_FRAMES, &info);
    apart = UiStartStep(apart, UI_START_TOGGLE_RETRY, UI_SETTLING_FRAMES + 1u, &info);
    const bool two_presses = apart.may_crash && apart.retry && !apart.begun;
    CHECK_TRUE(SUITE, "Y and X in one edge change neither choice, and A in that edge still begins",
               CHECK_NONE, neither && y_toggles && began_clean && two_presses,
               "together off %d, Y alone on %d, A with both began clean %d, two presses %d",
               (int)neither, (int)y_toggles, (int)began_clean, (int)two_presses);

    UiComposeStart(&frame, &info, StartFrom(NULL), 7u);
    const uint32_t quiet = CountColour(&frame, 0, top, FRAME_WIDTH, bottom, FAILED);
    const uint32_t above_quiet = CountLit(&frame, 0, top);
    const uint32_t counting = CountColour(&frame, 0, FRAME_HEIGHT - 31u, FRAME_WIDTH,
                                          FRAME_HEIGHT - 15u, FAILED);
    const uint32_t buttons = CountLit(&frame, FRAME_HEIGHT - 14u, FRAME_HEIGHT - 6u);
    UiComposeStart(&frame, &info, StartFrom("fatal"), 7u);
    const uint32_t loud = CountColour(&frame, 0, top, FRAME_WIDTH, bottom, FAILED);
    UiComposeStart(&frame, &info, StartFrom("retry"), 0u);
    const uint32_t retrying = CountColour(&frame, 0, top, FRAME_WIDTH, bottom, FAILED);
    const uint32_t no_count = CountColour(&frame, 0, FRAME_HEIGHT - 31u, FRAME_WIDTH,
                                          FRAME_HEIGHT - 15u, FAILED);
    CHECK_TRUE(SUITE, "the start screen warns exactly while a choice that can end the program is on",
               CHECK_NONE,
               quiet == 0u && loud > 1000u && retrying > 1000u && above_quiet > 1000u &&
                   counting > 0u && no_count == 0u && buttons > 100u && top > UI_LIST_TOP,
               "warning pixels off %u, may crash on %u, retry on %u; countdown %u then %u; "
               "button line %u",
               (unsigned)quiet, (unsigned)loud, (unsigned)retrying, (unsigned)counting,
               (unsigned)no_count, (unsigned)buttons);
    DumpFrame(&frame, "9-start");
}

/// Moves the start screen's cursor down @p rows and presses A there, a frame each.
static UiStartState StartChoose(UiStartState state, uint32_t row, const UiStartInfo *info) {
    uint32_t frame = UI_SETTLING_FRAMES;
    while (state.cursor != row) {
        state = UiStartStep(state, UI_START_DOWN, frame++, info);
    }
    return UiStartStep(state, UI_START_SELECT, frame, info);
}

/// The start screen's menu: it opens on begin the run with every row as the mode file leaves
/// it, A turns the row under the cursor, listen brings audio and audio takes listen, a family
/// this build lacks stays off, and the may-crash row counts the cases of the families chosen.
static void CheckStartMenu(Fixture *f) {
    UiStartInfo info;
    memset(&info, 0, sizeof(info));
    info.heading = "edenprobe 0.3.0 (test)";
    info.families_present = MODE_FAMILIES_PRESENT;
    static const struct {
        const char *suite;
        uint32_t family;
    } MAY[] = {
        { "cpu", MODE_FAMILY_CPU },          { "geometry", MODE_FAMILY_GRAPHICS },
        { "geometry", MODE_FAMILY_GRAPHICS }, { "bcn", MODE_FAMILY_GRAPHICS },
        { "bcn", MODE_FAMILY_GRAPHICS },      { "bcn", MODE_FAMILY_GRAPHICS },
    };
    for (uint32_t i = 0; i < 6u; ++i) {
        info.may_crash_suite[i] = MAY[i].suite;
        info.may_crash_name[i] = "a case";
        info.may_crash_family[i] = MAY[i].family;
    }
    info.may_crash_count = 6u;
    const bool shown = UiStartShown(&info);

    const UiStartState plain = StartFrom(NULL);
    const UiStartState listening = StartFrom("listen net notime");
    const bool opens =
        plain.cursor == UI_START_ROW_BEGIN && !plain.begun && plain.changed &&
        plain.families == (MODE_FAMILY_GRAPHICS | MODE_FAMILY_FS | MODE_FAMILY_NET) &&
        !plain.listen && !plain.quick && !plain.no_timing && !plain.save && !plain.may_crash &&
        listening.families == (MODE_FAMILY_NET | MODE_FAMILY_AUDIO) && listening.listen &&
        listening.no_timing && StartFrom("cpu").families == 0u;
    CHECK_TRUE(SUITE, "the start screen opens on begin the run, every row as the mode file says",
               CHECK_NONE, shown && opens,
               "shown %d; plain families %x listen %d quick %d; listen net notime families %x",
               (int)shown, (unsigned)plain.families, (int)plain.listen, (int)plain.quick,
               (unsigned)listening.families);

    // Each row in turn, from a fresh screen: A on it turns it.
    UiStartState up = UiStartStep(plain, UI_START_UP, UI_SETTLING_FRAMES, &info);
    UiStartState round = plain;
    for (uint32_t i = 0; i < UI_START_ROWS; ++i) {
        round = UiStartStep(round, UI_START_DOWN, UI_SETTLING_FRAMES + i, &info);
    }
    const UiStartState no_graphics = StartChoose(plain, UI_START_ROW_GRAPHICS, &info);
    const UiStartState no_fs = StartChoose(plain, UI_START_ROW_FS, &info);
    const UiStartState audio = StartChoose(plain, UI_START_ROW_AUDIO, &info);
    const UiStartState listen = StartChoose(plain, UI_START_ROW_LISTEN, &info);
    const UiStartState no_net = StartChoose(plain, UI_START_ROW_NET, &info);
    const UiStartState quick = StartChoose(plain, UI_START_ROW_QUICK, &info);
    const UiStartState notime = StartChoose(plain, UI_START_ROW_NOTIME, &info);
    const UiStartState fatal = StartChoose(plain, UI_START_ROW_MAY_CRASH, &info);
    const UiStartState retry = StartChoose(plain, UI_START_ROW_RETRY, &info);
    const UiStartState save = StartChoose(plain, UI_START_ROW_SAVE, &info);
    UiStartState audio_off = StartChoose(listening, UI_START_ROW_AUDIO, &info);
    UiStartState begun = StartChoose(plain, UI_START_ROW_BEGIN, &info);
    // A beside a direction, away from begin the run, is a whole mask: nothing moves or turns.
    UiStartState at_fs = StartChoose(plain, UI_START_ROW_FS, &info);
    at_fs = UiStartStep(at_fs, UI_START_SELECT | UI_START_DOWN, UI_SETTLING_FRAMES + 9u, &info);
    // Y works from any row, and plus begins from any row.
    UiStartState y_far = UiStartStep(no_fs, UI_START_TOGGLE_MAY_CRASH, UI_SETTLING_FRAMES + 9u,
                                     &info);
    UiStartState plus_far = UiStartStep(no_fs, UI_START_BEGIN, UI_SETTLING_FRAMES + 9u, &info);
    UiStartInfo no_net_build = info;
    no_net_build.families_present = MODE_FAMILIES_PRESENT & ~(uint32_t)MODE_FAMILY_NET;
    const UiStartState kept_off = StartChoose(StartFrom("cpu"), UI_START_ROW_NET, &no_net_build);
    const bool turns =
        up.cursor == UI_START_ROW_SAVE && round.cursor == UI_START_ROW_BEGIN &&
        no_graphics.families == (MODE_FAMILY_FS | MODE_FAMILY_NET) &&
        no_fs.families == (MODE_FAMILY_GRAPHICS | MODE_FAMILY_NET) &&
        audio.families == (MODE_FAMILY_GRAPHICS | MODE_FAMILY_FS | MODE_FAMILY_NET |
                           MODE_FAMILY_AUDIO) &&
        !audio.listen && listen.listen && (listen.families & MODE_FAMILY_AUDIO) != 0u &&
        no_net.families == (MODE_FAMILY_GRAPHICS | MODE_FAMILY_FS) && quick.quick &&
        notime.no_timing && fatal.may_crash && !retry.retry && save.save &&
        !audio_off.listen && (audio_off.families & MODE_FAMILY_AUDIO) == 0u && begun.begun &&
        at_fs.cursor == UI_START_ROW_FS && !at_fs.begun &&
        at_fs.families == (MODE_FAMILY_GRAPHICS | MODE_FAMILY_NET) && y_far.may_crash &&
        y_far.cursor == UI_START_ROW_FS && plus_far.begun && kept_off.families == 0u &&
        !no_graphics.begun && !save.begun;
    CHECK_TRUE(SUITE, "the start screen's menu turns the row under the cursor, listen with audio",
               CHECK_NONE, turns,
               "up %u round %u; graphics %x fs %x audio %x/%d listen %d/%x net %x quick %d notime "
               "%d fatal %d retry %d save %d; audio off %d/%x; A+down %u/%x; Y %d plus %d; no net "
               "%x",
               (unsigned)up.cursor, (unsigned)round.cursor, (unsigned)no_graphics.families,
               (unsigned)no_fs.families, (unsigned)audio.families, (int)audio.listen,
               (int)listen.listen, (unsigned)listen.families, (unsigned)no_net.families,
               (int)quick.quick, (int)notime.no_timing, (int)fatal.may_crash, (int)retry.retry,
               (int)save.save, (int)audio_off.listen, (unsigned)audio_off.families,
               (unsigned)at_fs.cursor, (unsigned)at_fs.families, (int)y_far.may_crash,
               (int)plus_far.begun, (unsigned)kept_off.families);

    // The may-crash row counts what the families chosen reach: all six with graphics, the cpu
    // suite's one without; with none reached, Y does nothing.
    UiStartInfo graphics_only = info;
    graphics_only.may_crash_count = 5u;
    for (uint32_t i = 0; i < 5u; ++i) {
        graphics_only.may_crash_suite[i] = MAY[i + 1u].suite;
        graphics_only.may_crash_family[i] = MAY[i + 1u].family;
    }
    const UiStartState cpu_only = StartFrom("cpu");
    const UiStartState y_nothing =
        UiStartStep(cpu_only, UI_START_TOGGLE_MAY_CRASH, UI_SETTLING_FRAMES, &graphics_only);
    const bool counted = UiStartMayCrashCount(&info, &plain) == 6u &&
                         UiStartMayCrashCount(&info, &no_graphics) == 1u &&
                         UiStartMayCrashCount(&graphics_only, &cpu_only) == 0u &&
                         !y_nothing.may_crash && UiStartMayCrashCount(NULL, &plain) == 0u;

    // The frame: the cursor's band on its row and on no other, a family's box in the colour of a
    // pass while it is on, and the warning only while a case that may crash is reached.
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    const uint32_t graphics_row = UiStartRowTop(UI_START_ROW_GRAPHICS);
    const uint32_t save_row = UiStartRowTop(UI_START_ROW_SAVE);
    const uint32_t box_right = 32u + 6u * 16u;
    UiComposeStart(&frame, &info, plain, 0u);
    const uint32_t band_begin = CountColour(&frame, 0, UiStartRowTop(UI_START_ROW_BEGIN),
                                            FRAME_WIDTH, graphics_row, SELECTED);
    const uint32_t band_elsewhere =
        CountColour(&frame, 0, graphics_row, FRAME_WIDTH, save_row + UI_ROW_HEIGHT, SELECTED);
    const uint32_t box_on = CountColour(&frame, 0, graphics_row, box_right,
                                        graphics_row + UI_ROW_HEIGHT, PASSED);
    DumpFrame(&frame, "8-start-menu");
    UiComposeStart(&frame, &info, no_graphics, 0u);
    const uint32_t band_moved = CountColour(&frame, 0, graphics_row, FRAME_WIDTH,
                                            graphics_row + UI_ROW_HEIGHT, SELECTED);
    const uint32_t box_off = CountColour(&frame, 0, graphics_row, box_right,
                                         graphics_row + UI_ROW_HEIGHT, PASSED);
    const uint32_t top = UiStartWarningTop(FRAME_HEIGHT);
    const uint32_t bottom = top + UI_START_WARNING_LINES * UI_ROW_HEIGHT + 16u;
    UiStartState unreached = UiStartStep(cpu_only, UI_START_TOGGLE_MAY_CRASH, UI_SETTLING_FRAMES,
                                         &info);
    UiComposeStart(&frame, &graphics_only, unreached, 0u);
    const uint32_t warned_for_nothing = CountColour(&frame, 0, top, FRAME_WIDTH, bottom, FAILED);
    const bool drawn = band_begin > 10000u && band_elsewhere == 0u && box_on > 50u &&
                       band_moved > 10000u && box_off == 0u && unreached.may_crash &&
                       warned_for_nothing == 0u &&
                       UiStartRowTop(UI_START_ROWS) + UI_ROW_HEIGHT < top;
    CHECK_TRUE(SUITE, "the start screen counts the cases that may crash in the families chosen",
               CHECK_NONE, counted && drawn,
               "counts %u %u %u, Y with none %d; cursor band %u elsewhere %u moved %u; box on %u "
               "off %u; warning with none reached %u",
               (unsigned)UiStartMayCrashCount(&info, &plain),
               (unsigned)UiStartMayCrashCount(&info, &no_graphics),
               (unsigned)UiStartMayCrashCount(&graphics_only, &cpu_only), (int)y_nothing.may_crash,
               (unsigned)band_begin, (unsigned)band_elsewhere, (unsigned)band_moved,
               (unsigned)box_on, (unsigned)box_off, (unsigned)warned_for_nothing);

    // What the screen chose becomes the run's mode: a family set it left alone keeps the file's
    // words, a changed one is named whole with cpu, and the label stays the file's.
    const RunMode none = ModeFromText(NULL);
    const RunMode same = UiStartMode(&plain, none, MODE_FAMILIES_PRESENT);
    const RunMode fewer = UiStartMode(&no_graphics, none, MODE_FAMILIES_PRESENT);
    const RunMode nothing_but_cpu = UiStartMode(&cpu_only, ModeFromText("net"),
                                                MODE_FAMILIES_PRESENT);
    const RunMode left = UiStartMode(&quick, none, MODE_FAMILIES_PRESENT);
    const RunMode risky = UiStartMode(&fatal, none, MODE_FAMILIES_PRESENT);
    const RunMode kept = UiStartMode(&save, none, MODE_FAMILIES_PRESENT);
    const RunMode labelled_file = ModeFromText("label=phone net");
    UiStartState labelled = UiStartOpen(&labelled_file, MODE_FAMILIES_PRESENT);
    labelled = StartChoose(labelled, UI_START_ROW_LISTEN, &info);
    const RunMode heard = UiStartMode(&labelled, labelled_file, MODE_FAMILIES_PRESENT);
    // Listen turned off alone leaves audio on, and still changes the words.
    const RunMode listen_file = ModeFromText("listen");
    const UiStartState unheard_state =
        StartChoose(UiStartOpen(&listen_file, MODE_FAMILIES_PRESENT), UI_START_ROW_LISTEN, &info);
    const RunMode unheard = UiStartMode(&unheard_state, listen_file, MODE_FAMILIES_PRESENT);
    const bool quiet_audio =
        !unheard.listen && unheard.chosen_on_screen &&
        (ModeFamiliesToRun(&unheard, MODE_FAMILIES_PRESENT) & MODE_FAMILY_AUDIO) != 0u;
    char key_fewer[128];
    char key_heard[128];
    ModeKey(key_fewer, sizeof(key_fewer), &fewer);
    ModeKey(key_heard, sizeof(key_heard), &heard);
    char said[REPORT_NOTE_MAX + 1];
    ModeDescribe(said, sizeof(said), &risky, MODE_FAMILIES_PRESENT);
    char said_fewer[REPORT_NOTE_MAX + 1];
    ModeDescribe(said_fewer, sizeof(said_fewer), &fewer, MODE_FAMILIES_PRESENT);
    const bool modes =
        same.families_named == 0u && !same.chosen_on_screen && same.detailed && !same.fatal &&
        fewer.families_named == (MODE_FAMILY_CPU | MODE_FAMILY_FS | MODE_FAMILY_NET) &&
        fewer.chosen_on_screen && strcmp(key_fewer, "cpu fs net") == 0 &&
        ModeFamiliesToRun(&nothing_but_cpu, MODE_FAMILIES_PRESENT) == MODE_FAMILY_CPU &&
        left.quick && !left.detailed && left.chosen_on_screen && risky.fatal &&
        risky.fatal_by_screen && !risky.chosen_on_screen && kept.saved_by_screen &&
        !kept.chosen_on_screen && strcmp(heard.label, "phone") == 0 && heard.listen &&
        strcmp(key_heard, "audio cpu listen net") == 0 &&
        strcmp(said, "mode: detailed, no mode file; pictures of failures only; families cpu "
                     "graphics fs net; the cases that may crash, chosen on the start screen") ==
            0 &&
        strncmp(said_fewer, "mode: detailed, chosen on the start screen; ", 44u) == 0 &&
        quiet_audio;
    CHECK_TRUE(SUITE, "what the start screen chooses is the run's mode, the label the file's",
               CHECK_NONE, modes,
               "same %x/%d; fewer %x \"%.40s\"; cpu only %x; quick %d/%d; fatal %d/%d/%d; "
               "saved %d; heard \"%.32s\" \"%.40s\"; listen off alone %d; said \"%.150s\"",
               (unsigned)same.families_named, (int)same.chosen_on_screen,
               (unsigned)fewer.families_named, key_fewer,
               (unsigned)ModeFamiliesToRun(&nothing_but_cpu, MODE_FAMILIES_PRESENT),
               (int)left.quick, (int)left.detailed, (int)risky.fatal, (int)risky.fatal_by_screen,
               (int)risky.chosen_on_screen, (int)kept.saved_by_screen, heard.label, key_heard,
               (int)quiet_audio, said);

    // The video, kernel, display and service families have no row: a file that names them keeps
    // them whatever the screen changes.
    const RunMode video_file = ModeFromText("video kernel display service net");
    const UiStartState video_state =
        StartChoose(UiStartOpen(&video_file, MODE_FAMILIES_PRESENT), UI_START_ROW_FS, &info);
    const RunMode video_mode = UiStartMode(&video_state, video_file, MODE_FAMILIES_PRESENT);
    const uint32_t video_run = ModeFamiliesToRun(&video_mode, MODE_FAMILIES_PRESENT);
    char key_video[128];
    ModeKey(key_video, sizeof(key_video), &video_mode);
    CHECK_TRUE(SUITE,
               "the start screen keeps the video, kernel, display and service families the file "
               "names, which have no row",
               CHECK_NONE,
               video_mode.chosen_on_screen &&
                   video_run == (MODE_FAMILY_CPU | MODE_FAMILY_FS | MODE_FAMILY_NET |
                                 MODE_FAMILY_VIDEO | MODE_FAMILY_KERNEL | MODE_FAMILY_DISPLAY |
                                 MODE_FAMILY_SERVICE) &&
                   strcmp(key_video, "cpu display fs kernel net service video") == 0 &&
                   (MODE_FAMILIES_DEFAULT & (MODE_FAMILY_VIDEO | MODE_FAMILY_KERNEL |
                                             MODE_FAMILY_DISPLAY | MODE_FAMILY_SERVICE)) == 0u,
               "families %x, words \"%s\"", (unsigned)video_run, key_video);

    // The words the screen saves and the ones the journal keeps read back as the same run.
    char file_words[256];
    char chosen_words[JOURNAL_MODE_MAX];
    char empty_words[JOURNAL_MODE_MAX];
    ModeFormat(file_words, sizeof(file_words), &heard);
    const RunMode reread = ModeFromText(file_words);
    char key_reread[128];
    ModeKey(key_reread, sizeof(key_reread), &reread);
    char left_words[256];
    ModeFormat(left_words, sizeof(left_words), &left);
    ModeChosenWords(chosen_words, sizeof(chosen_words), &fewer);
    const RunMode resumed = ModeWithChosen(none, chosen_words);
    char key_resumed[128];
    ModeKey(key_resumed, sizeof(key_resumed), &resumed);
    // A file that said fatal, the screen turning it off: the choice is the empty set of words,
    // which the journal still keeps, and which turns fatal off when the run goes on.
    const RunMode fatal_file = ModeFromText("fatal");
    UiStartState unfatal = UiStartOpen(&fatal_file, MODE_FAMILIES_PRESENT);
    unfatal = UiStartStep(unfatal, UI_START_TOGGLE_MAY_CRASH, UI_SETTLING_FRAMES, &info);
    const RunMode unfatal_mode = UiStartMode(&unfatal, fatal_file, MODE_FAMILIES_PRESENT);
    ModeChosenWords(empty_words, sizeof(empty_words), &unfatal_mode);
    const RunMode unfatal_resumed = ModeWithChosen(fatal_file, empty_words);
    char quick_words[JOURNAL_MODE_MAX];
    ModeChosenWords(quick_words, sizeof(quick_words), &left);
    const RunMode quick_resumed = ModeWithChosen(none, quick_words);
    // A chosen record from before the start screen chose more than fatal and retry adds to the
    // file's words; a word that only starts with screen is not the marker.
    const RunMode legacy = ModeWithChosen(ModeFromText("net"), "fatal retry");
    const RunMode not_marker = ModeWithChosen(ModeFromText("net"), "screenx fatal");
    const bool words =
        strcmp(file_words, "audio cpu listen net label=phone") == 0 &&
        strcmp(key_reread, key_heard) == 0 && strcmp(reread.label, "phone") == 0 &&
        reread.detailed && strcmp(left_words, "quick") == 0 &&
        strcmp(chosen_words, "screen cpu fs net") == 0 && strcmp(key_resumed, key_fewer) == 0 &&
        resumed.chosen_on_screen && resumed.detailed && strcmp(empty_words, "screen") == 0 &&
        !unfatal_resumed.fatal && strcmp(quick_words, "screen quick") == 0 &&
        quick_resumed.quick && !quick_resumed.detailed && legacy.fatal && legacy.retry &&
        legacy.fatal_by_screen && legacy.families_named == MODE_FAMILY_NET &&
        not_marker.fatal && not_marker.families_named == MODE_FAMILY_NET &&
        ModeWithChosen(none, NULL).families_named == 0u &&
        ModeWithChosen(none, "").families_named == 0u;
    CHECK_TRUE(SUITE, "the words the start screen saves and the journal keeps read back the same",
               CHECK_NONE, words,
               "saved \"%.50s\" reread \"%.40s\"; quick saved \"%.40s\"; chosen \"%.40s\" "
               "resumed \"%.40s\"; fatal off \"%.20s\" %d; quick \"%.20s\" %d; legacy %d/%d; "
               "not the marker %d",
               file_words, key_reread, left_words, chosen_words, key_resumed, empty_words,
               (int)unfatal_resumed.fatal, quick_words, (int)quick_resumed.quick,
               (int)legacy.fatal, (int)legacy.retry, (int)not_marker.fatal);
}

/// The screen that asks whether to go on: the cursor on going on, up and down coming round, A
/// taking the row under the cursor, nothing while the pad settles or in an edge that also moves,
/// ten seconds before it goes on by itself; and the frame shows where the cursor is.
static void CheckResumeScreen(Fixture *f) {
    UiResumeState state = UiResumeOpen();
    const bool opens = state.cursor == UI_RESUME_GO_ON && !state.decided && state.changed;
    UiResumeState settling = UiResumeStep(state, UI_RESUME_CHOOSE, UI_SETTLING_FRAMES - 1u);
    const bool settled = !settling.decided;
    UiResumeState go_on = UiResumeStep(state, UI_RESUME_CHOOSE, UI_SETTLING_FRAMES);
    const bool went_on = go_on.decided && !go_on.start_over;
    UiResumeState over = UiResumeStep(state, UI_RESUME_DOWN, UI_SETTLING_FRAMES);
    const bool moved = over.cursor == UI_RESUME_START_OVER && !over.decided;
    over = UiResumeStep(over, UI_RESUME_CHOOSE, UI_SETTLING_FRAMES + 1u);
    const bool started_over = over.decided && over.start_over;
    const bool stays = UiResumeStep(over, UI_RESUME_UP, UI_SETTLING_FRAMES + 2u).cursor ==
                       UI_RESUME_START_OVER;
    UiResumeState round = UiResumeStep(state, UI_RESUME_UP, UI_SETTLING_FRAMES);
    const bool came_round = round.cursor == UI_RESUME_START_OVER &&
                            UiResumeStep(round, UI_RESUME_DOWN, UI_SETTLING_FRAMES).cursor ==
                                UI_RESUME_GO_ON;
    UiResumeState mask = UiResumeStep(state, UI_RESUME_CHOOSE | UI_RESUME_DOWN | UI_RESUME_UP,
                                      UI_SETTLING_FRAMES);
    const bool whole_mask = !mask.decided && mask.cursor == UI_RESUME_GO_ON;
    const uint64_t second = 1000000000ull;
    const bool timer = UI_RESUME_NANOSECONDS == 10u * second &&
                       !UiResumeTimesOut(10u * second - 1u, false) &&
                       UiResumeTimesOut(10u * second, false) &&
                       !UiResumeTimesOut(100u * second, true) &&
                       UiResumeSecondsLeft(0, false) == 10u &&
                       UiResumeSecondsLeft(9u * second + 1u, false) == 1u &&
                       UiResumeSecondsLeft(0, true) == 0u;
    CHECK_TRUE(SUITE, "the resume screen goes on unless start over is chosen, and a mask chooses "
                      "nothing",
               CHECK_NONE,
               opens && settled && went_on && moved && started_over && stays && came_round &&
                   whole_mask && timer,
               "opens %d, settling %d, A goes on %d, down %d, A starts over %d, decided stays %d, "
               "comes round %d, whole mask %d, ten seconds %d",
               (int)opens, (int)settled, (int)went_on, (int)moved, (int)started_over, (int)stays,
               (int)came_round, (int)whole_mask, (int)timer);

    UiResumeInfo info;
    memset(&info, 0, sizeof(info));
    info.heading = "edenprobe 0.3.0 (test)";
    info.where = "the program ended inside bcn case \"every layer of a six-layer BC4 array keeps "
                 "its own 2x2 and 1x1 levels\" in its warm-up";
    info.folder = "sdmc:/edenprobe/runs/b7172fc";
    info.launch = 2u;
    info.steps = 34u;
    info.steps_done = 21u;
    info.passed = 412u;
    info.failed = 3u;
    info.skipped = 20u;
    info.crashed = 1u;
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    const uint32_t row0 = UiResumeRowTop(UI_RESUME_GO_ON);
    const uint32_t row1 = UiResumeRowTop(UI_RESUME_START_OVER);
    UiComposeResume(&frame, &info, UiResumeOpen(), 7u);
    const uint32_t on_first = CountColour(&frame, 0, row0, FRAME_WIDTH, row0 + 2u * UI_ROW_HEIGHT,
                                          SELECTED);
    const uint32_t off_second = CountColour(&frame, 0, row1, FRAME_WIDTH,
                                            row1 + 2u * UI_ROW_HEIGHT, SELECTED);
    const uint32_t counting = CountColour(&frame, 0, FRAME_HEIGHT - 31u, FRAME_WIDTH,
                                          FRAME_HEIGHT - 15u, FAILED);
    const uint32_t heading = CountLit(&frame, 8u, 24u);
    const uint32_t where = TextLines(&frame, UI_LIST_TOP, UI_LIST_TOP + 3u * UI_ROW_HEIGHT);
    const uint32_t crashed = CountColour(&frame, 0, UI_LIST_TOP, FRAME_WIDTH, row0, CRASHED);
    const uint32_t buttons = CountLit(&frame, FRAME_HEIGHT - 14u, FRAME_HEIGHT - 6u);
    DumpFrame(&frame, "10-resume");
    UiComposeResume(&frame, &info, over, 0u);
    const uint32_t off_first = CountColour(&frame, 0, row0, FRAME_WIDTH,
                                           row0 + 2u * UI_ROW_HEIGHT, SELECTED);
    const uint32_t on_second = CountColour(&frame, 0, row1, FRAME_WIDTH,
                                           row1 + 2u * UI_ROW_HEIGHT, SELECTED);
    const uint32_t not_counting = CountColour(&frame, 0, FRAME_HEIGHT - 31u, FRAME_WIDTH,
                                              FRAME_HEIGHT - 15u, FAILED);
    const UiFrame small = { f->pixels, 320u, 200u, 320u * 4u };
    UiComposeResume(&small, &info, over, 3u);
    const uint32_t small_lit = CountLit(&small, 0u, small.height);
    CHECK_TRUE(SUITE, "the resume screen shows where it stopped and which choice the cursor is on",
               CHECK_NONE,
               on_first > 10000u && off_second == 0u && off_first == 0u && on_second > 10000u &&
                   counting > 0u && not_counting == 0u && heading > 100u && where == 2u &&
                   crashed > 0u && buttons > 100u && small_lit > 0u && small_lit < 2000u &&
                   row1 > row0 + UI_ROW_HEIGHT && row1 + 2u * UI_ROW_HEIGHT < FRAME_HEIGHT - 32u,
               "cursor band %u then %u, other row %u then %u; countdown %u then %u; heading %u, "
               "where in %u lines, crashed %u, buttons %u, small frame %u",
               (unsigned)on_first, (unsigned)on_second, (unsigned)off_second,
               (unsigned)off_first, (unsigned)counting, (unsigned)not_counting,
               (unsigned)heading, (unsigned)where, (unsigned)crashed, (unsigned)buttons,
               (unsigned)small_lit);
}

/// The running screen: when it draws, how it counts a step's cases pass by pass, the words for
/// a pass and a length of time, and a frame with the bar of steps in proportion.
static void CheckProgress(Fixture *f) {
    const uint64_t ms = 1000000ull;
    const bool due = UI_PROGRESS_NANOSECONDS == 500u * ms && UiProgressDue(false, 0, 0, false) &&
                     !UiProgressDue(true, 1000u * ms, 1499u * ms, false) &&
                     UiProgressDue(true, 1000u * ms, 1500u * ms, false) &&
                     UiProgressDue(true, 1000u * ms, 1000u * ms + 1u, true) &&
                     UiProgressDue(true, 2000u * ms, 1000u * ms, false);

    UiProgressCount untimed;
    UiProgressCountReset(&untimed);
    UiProgressCountCase(&untimed, 0, true);
    UiProgressCountCase(&untimed, 0, false);
    UiProgressCountCase(&untimed, 0, true);
    UiProgressCountCase(&untimed, 0, false);
    UiProgressCount judge;
    UiProgressCountReset(&judge);
    for (int i = 0; i < 3; ++i) {
        UiProgressCountCase(&judge, 0, false);
    }
    UiProgressCount timed;
    UiProgressCountReset(&timed);
    for (int i = 0; i < 3; ++i) {
        UiProgressCountCase(&timed, 1, true);
        UiProgressCountCase(&timed, 1, false);
    }
    const uint32_t warm_up = timed.count;
    UiProgressCountCase(&timed, 2, true);
    const bool second_pass = timed.previous == 3u && timed.count == 1u;
    UiProgressCountCase(&timed, 2, false);
    // A case skipped at its start ends with no start, and counts once.
    UiProgressCountCase(&timed, 2, false);
    const bool counts = untimed.count == 2u && untimed.previous == 0u && judge.count == 3u &&
                        warm_up == 3u && second_pass && timed.count == 2u;

    char words[4][96];
    UiProgressPassWords(words[0], sizeof(words[0]), 0u, 0u);
    UiProgressPassWords(words[1], sizeof(words[1]), 1u, 10u);
    UiProgressPassWords(words[2], sizeof(words[2]), 5u, 10u);
    UiProgressPassWords(words[3], sizeof(words[3]), 10u, 10u);
    char elapsed[4][32];
    UiElapsedWords(elapsed[0], sizeof(elapsed[0]), 0u);
    UiElapsedWords(elapsed[1], sizeof(elapsed[1]), 59999u * ms);
    UiElapsedWords(elapsed[2], sizeof(elapsed[2]), 61000u * ms);
    UiElapsedWords(elapsed[3], sizeof(elapsed[3]), 3725000u * ms);
    const bool said = words[0][0] == '\0' &&
                      strcmp(words[1], "pass 1 of 10: the warm-up") == 0 &&
                      strcmp(words[2], "pass 5 of 10: a rehearsal, timed") == 0 &&
                      strcmp(words[3], "pass 10 of 10: the verdict pass, timed and reported") ==
                          0 &&
                      strcmp(elapsed[0], "0 s") == 0 && strcmp(elapsed[1], "59 s") == 0 &&
                      strcmp(elapsed[2], "1 min 1 s") == 0 &&
                      strcmp(elapsed[3], "1 h 2 min") == 0;
    CHECK_TRUE(SUITE, "the running screen draws twice a second and counts a step's cases by pass",
               CHECK_NONE, due && counts && said,
               "due %d; untimed %u, judge %u, warm-up %u, second pass %d then %u; words \"%s\" "
               "\"%s\" \"%s\"; times \"%s\" \"%s\" \"%s\" \"%s\"",
               (int)due, (unsigned)untimed.count, (unsigned)judge.count, (unsigned)warm_up,
               (int)second_pass, (unsigned)timed.count, words[1], words[2], words[3],
               elapsed[0], elapsed[1], elapsed[2], elapsed[3]);

    UiProgress progress;
    memset(&progress, 0, sizeof(progress));
    progress.heading = "edenprobe 0.3.0 (test)";
    progress.folder = "sdmc:/edenprobe/runs/b7172fc";
    progress.step = 17u;
    progress.steps = 34u;
    progress.step_name = "net";
    progress.pass = 0u;
    progress.passes = 0u;
    progress.suite = "ssl";
    progress.name = "the name set on a connection reads back";
    progress.case_number = 5u;
    progress.case_total = 0u;
    progress.passed = 412u;
    progress.failed = 3u;
    progress.skipped = 20u;
    progress.crashed = 1u;
    progress.elapsed_ns = 83000u * ms;
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    static const uint8_t DIM_COLOUR[3] = { 0x88, 0x90, 0xA0 };
    const uint32_t bar = UI_PROGRESS_BAR_TOP;
    const uint32_t bar_end = bar + UI_PROGRESS_BAR_HEIGHT;
    const uint32_t width = FRAME_WIDTH - 64u;
    UiComposeProgress(&frame, &progress);
    const uint32_t done = CountColour(&frame, 0, bar, FRAME_WIDTH, bar_end, PASSED);
    const uint32_t under_way = CountColour(&frame, 0, bar, FRAME_WIDTH, bar_end, DIM_COLOUR);
    const uint32_t header = CountLit(&frame, 8u, 24u);
    const uint32_t counted = CountColour(&frame, 0, 30u, FRAME_WIDTH, 46u, FAILED);
    const uint32_t named = TextLines(&frame, bar_end + 16u, bar_end + 16u + 4u * UI_ROW_HEIGHT);
    const uint32_t footer = CountLit(&frame, FRAME_HEIGHT - 14u, FRAME_HEIGHT - 6u);
    DumpFrame(&frame, "11-running");
    progress.step = 1u;
    UiComposeProgress(&frame, &progress);
    const uint32_t first = CountColour(&frame, 0, bar, FRAME_WIDTH, bar_end, PASSED);
    progress.step = 40u;
    UiComposeProgress(&frame, &progress);
    const uint32_t past = CountColour(&frame, 0, bar, FRAME_WIDTH, bar_end, PASSED);
    progress.name = NULL;
    progress.step = 3u;
    progress.step_name = "clear";
    progress.pass = 3u;
    progress.passes = 10u;
    UiComposeProgress(&frame, &progress);
    const uint32_t unnamed = TextLines(&frame, bar_end + 16u + UI_ROW_HEIGHT,
                                       bar_end + 16u + 4u * UI_ROW_HEIGHT);
    const uint32_t pass_line = CountLit(&frame, bar_end + 16u, bar_end + 16u + 16u);
    const UiFrame small = { f->pixels, 24u, 100u, 24u * 4u };
    const size_t used = (size_t)small.pitch * small.height;
    memset(f->pixels + used, 0xA5, 64u);
    UiComposeProgress(&small, &progress);
    uint32_t spilled = 0;
    for (size_t b = used; b < used + 64u; ++b) {
        spilled += f->pixels[b] != 0xA5 ? 1u : 0u;
    }
    const bool drawn = done == (width * 16u / 34u) * UI_PROGRESS_BAR_HEIGHT &&
                       under_way == (width * 17u / 34u - width * 16u / 34u) *
                                        UI_PROGRESS_BAR_HEIGHT &&
                       first == 0u && past == (width * 33u / 34u) * UI_PROGRESS_BAR_HEIGHT &&
                       header > 100u && counted > 0u && named >= 2u && footer > 100u &&
                       unnamed == 0u && pass_line > 100u && spilled == 0u;
    CHECK_TRUE(SUITE, "the running screen shows the step, the case and the steps done in proportion",
               CHECK_NONE, drawn,
               "bar done %u (want %u), under way %u, at the first step %u, past the last %u; "
               "header %u, counts %u, case lines %u, footer %u, with no case %u, pass line %u, "
               "spilled %u",
               (unsigned)done, (unsigned)((width * 16u / 34u) * UI_PROGRESS_BAR_HEIGHT),
               (unsigned)under_way, (unsigned)first, (unsigned)past, (unsigned)header,
               (unsigned)counted, (unsigned)named, (unsigned)footer, (unsigned)unnamed,
               (unsigned)pass_line, (unsigned)spilled);
}

/// A case the program ended inside is counted apart from the skips, as the summary counts it,
/// and marked in a colour of its own.
static void CheckCrashed(Fixture *f) {
    uint32_t at = 0;
    Fill(&f->records[at++], "clear", "graphics device opens", "none", REPORT_PASS, "", -1);
    Fill(&f->records[at++], "clear", "clear to opaque red", "none", REPORT_CRASHED,
         "the program ended inside it earlier in this run", -1);
    Fill(&f->records[at++], "clear", "clear to transparent cyan", "none", REPORT_SKIP,
         "depends", -1);
    Fill(&f->records[at++], "fill", "one quad", "exact", REPORT_PASS, "", -1);
    UiRunIndex(&f->small, f->records, at, at);
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    const UiState start = UiStart(&f->small);
    UiCompose(&frame, &f->small, start, NULL);
    const uint32_t row = UI_LIST_TOP + UiOverviewRow(&f->small, 0) * UI_ROW_HEIGHT;
    const uint32_t in_row = CountColour(&frame, 0, row, FRAME_WIDTH, row + UI_ROW_HEIGHT, CRASHED);
    const uint32_t heading = CountColour(&frame, 0, 8, FRAME_WIDTH, 26, CRASHED);
    UiState in_group = start;
    in_group.level = UI_LEVEL_GROUP;
    in_group.group = 0;
    in_group.index = 0;
    UiCompose(&frame, &f->small, in_group, NULL);
    const uint32_t mark_y = UI_LIST_TOP + UI_ROW_HEIGHT;
    const uint32_t marked = CountColour(&frame, UI_MARK_LEFT, mark_y, UI_MARK_LEFT + 64u,
                                        mark_y + UI_ROW_HEIGHT, CRASHED);
    const UiGroup *const clear = &f->small.groups[0];
    CHECK_TRUE(SUITE, "a case the program ended inside is counted apart and in a colour of its own",
               CHECK_NONE,
               f->small.crashed == 1u && f->small.skipped == 1u && f->small.passed == 2u &&
                   clear->crashed == 1u && clear->skipped == 1u && in_row > 0u &&
                   heading > 0u && marked > 0u,
               "run crashed %u skipped %u passed %u; the row %u, the heading %u and the mark %u "
               "pixels of its colour",
               (unsigned)f->small.crashed, (unsigned)f->small.skipped, (unsigned)f->small.passed,
               (unsigned)in_row, (unsigned)heading, (unsigned)marked);
}

/// The region a screen hashes, so two wordings cannot draw the same frame and pass.
static uint32_t HashRows(const UiFrame *frame, uint32_t y0, uint32_t y1) {
    uint32_t hash = 2166136261u;
    for (uint32_t y = y0; y < y1 && y < frame->height; ++y) {
        for (uint32_t x = 0; x < frame->width; x += 4u) {
            const uint8_t *const pixel = UiPixel(frame, x, y);
            if (pixel == NULL) {
                continue;
            }
            for (uint32_t channel = 0; channel < 4u; ++channel) {
                hash = (hash ^ pixel[channel]) * 16777619u;
            }
        }
    }
    return hash;
}

static bool TextIs(UiListenScreen screen, double left, double right, const char *mark,
                   const char *name, const char *take, const char *guide, bool numbered) {
    char buffer[192];
    UiListenCopy text;
    UiListenCopyOf(screen, left, right, &text, buffer, sizeof(buffer));
    const char *const buttons = numbered ? "Y repeat this take   X next part" : "Y repeat   X next part";
    const bool mark_ok = mark == NULL ? text.mark == NULL : text.mark != NULL && strcmp(text.mark, mark) == 0;
    return mark_ok && strcmp(text.name, name) == 0 && strcmp(text.take, take) == 0 &&
           strcmp(text.guide, guide) == 0 && strcmp(text.buttons, buttons) == 0;
}

static void CheckListen(Fixture *f) {
    const uint32_t results =
        UI_BUTTON_UP | UI_BUTTON_DOWN | UI_BUTTON_LEFT | UI_BUTTON_RIGHT | UI_BUTTON_OPEN |
        UI_BUTTON_BACK | UI_BUTTON_NEXT_FAILURE | UI_BUTTON_PREVIOUS_FAILURE |
        UI_BUTTON_CYCLE_VIEW | UI_BUTTON_QUIT;
    uint32_t wrong = 0;
    if (UiListenInputFromButtons(UI_BUTTON_LISTEN_SKIP, 0) != UI_LISTEN_NONE ||
        UiListenInputFromButtons(UI_BUTTON_LISTEN_REPEAT, UI_SETTLING_FRAMES) != UI_LISTEN_REPEAT ||
        UiListenInputFromButtons(UI_BUTTON_LISTEN_SKIP, UI_SETTLING_FRAMES) != UI_LISTEN_SKIP ||
        UiListenInputFromButtons(UI_BUTTON_LISTEN_REPEAT | UI_BUTTON_LISTEN_SKIP,
                                 UI_SETTLING_FRAMES) != UI_LISTEN_SKIP ||
        UiListenInputFromButtons(results, UI_SETTLING_FRAMES) != UI_LISTEN_NONE ||
        UiInputFromButtons(UI_BUTTON_LISTEN_REPEAT | UI_BUTTON_LISTEN_SKIP, UI_SETTLING_FRAMES) !=
            UI_INPUT_NONE) {
        ++wrong;
    }
    // Opening, then each part, then the close. Sixteen steps from the first screen.
    UiListenScreen walk = UI_LISTEN_OPENING;
    unsigned steps = 0;
    while (walk != UI_LISTEN_DONE && steps < 20u) {
        const UiListenScreen next = UiListenNext(walk);
        if (next != (walk == UI_LISTEN_CLOSING ? UI_LISTEN_DONE : (UiListenScreen)(walk + 1))) {
            ++wrong;
        }
        walk = next;
        ++steps;
    }
    if (steps != 16u || UiListenNext(UI_LISTEN_DONE) != UI_LISTEN_DONE) {
        ++wrong;
    }
    if (UiListenSkip(UI_LISTEN_OPENING) != UI_LISTEN_A_BEEP ||
        UiListenSkip(UI_LISTEN_A_BEEP) != UI_LISTEN_B_BEEP ||
        UiListenSkip(UI_LISTEN_A1) != UI_LISTEN_B_BEEP ||
        UiListenSkip(UI_LISTEN_A4) != UI_LISTEN_B_BEEP ||
        UiListenSkip(UI_LISTEN_B_BEEP) != UI_LISTEN_C_BEEP ||
        UiListenSkip(UI_LISTEN_B_RECORD) != UI_LISTEN_C_BEEP ||
        UiListenSkip(UI_LISTEN_B_RIGHT) != UI_LISTEN_C_BEEP ||
        UiListenSkip(UI_LISTEN_C_BEEP) != UI_LISTEN_CLOSING ||
        UiListenSkip(UI_LISTEN_C1) != UI_LISTEN_CLOSING ||
        UiListenSkip(UI_LISTEN_C4) != UI_LISTEN_CLOSING ||
        UiListenSkip(UI_LISTEN_CLOSING) != UI_LISTEN_DONE) {
        ++wrong;
    }
    const char *const a_low =
        "duller than the high beep, most in the hiss and the top of the sweep, means default "
        "quality is low-passing. the same sound means it is not.";
    const char *const b_left =
        "the low notes again means the sink recorded them. silence means it recorded nothing. "
        "peak 0.150 on the left and 0.000 on the right (0 is silence).";
    const char *const c_high =
        "about 7 dB louder than mode 0 where the mode byte is read. the same volume means it is not.";
    if (!TextIs(UI_LISTEN_OPENING, 0.0, 0.0, NULL, "listening", "three rising notes",
                "then part a, the resampler; part b, the circular buffer sink; part c, the limiter.",
                false) ||
        !TextIs(UI_LISTEN_A1, 0.0, 0.0, "a", "resampler",
                "take 1 of 4, low beep: default quality", a_low, true) ||
        !TextIs(UI_LISTEN_A2, 0.0, 0.0, "a", "resampler", "take 2 of 4, high beep: high quality",
                "the control. compare its hiss with the take before it.", true) ||
        !TextIs(UI_LISTEN_B_BEEP, 0.0, 0.0, "b", "circular buffer sink", "two beeps, then the phrase",
                "three low notes on the left, then three high notes on the right. the sink records them.",
                false) ||
        !TextIs(UI_LISTEN_B_RECORD, 0.0, 0.0, "b", "circular buffer sink", "recording",
                "low notes on the left, then high notes on the right.", false) ||
        !TextIs(UI_LISTEN_B_LEFT, 0.15, 0.0, "b", "circular buffer sink",
                "low beep: left channel of the recording, on the left", b_left, true) ||
        !TextIs(UI_LISTEN_C2, 0.0, 0.0, "c", "limiter", "take 2 of 4, high beep: mode 1", c_high,
                true) ||
        !TextIs(UI_LISTEN_C1, 0.0, 0.0, "c", "limiter", "take 1 of 4, low beep: mode 0",
                "where mode 1 is read from its byte, this take is the quieter one. the same volume "
                "as mode 1 means the mode is ignored.",
                true) ||
        !TextIs(UI_LISTEN_CLOSING, 0.0, 0.0, NULL, "listening", "three falling notes",
                "the cases that follow are nearly silent.", false)) {
        ++wrong;
    }
    char buffer[192];
    UiListenCopy a3;
    UiListenCopyOf(UI_LISTEN_A3, 0.0, 0.0, &a3, buffer, sizeof(buffer));
    UiListenCopy a4;
    UiListenCopyOf(UI_LISTEN_A4, 0.0, 0.0, &a4, buffer, sizeof(buffer));
    UiListenCopy c4;
    UiListenCopyOf(UI_LISTEN_C4, 0.0, 0.0, &c4, buffer, sizeof(buffer));
    if (strcmp(a3.take, "take 3 of 4, low beep: default quality") != 0 ||
        strcmp(a3.guide, a_low) != 0 ||
        strcmp(a4.take, "take 4 of 4, high beep: high quality") != 0 ||
        strcmp(c4.take, "take 4 of 4, high beep: mode 1") != 0) {
        ++wrong;
    }
    CHECK_TRUE(SUITE,
               "the listening screen names the part, the take and what to listen for, and its "
               "buttons are not the results screens'",
               CHECK_NONE, wrong == 0u, "%u checks disagreed", (unsigned)wrong);

    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    static const UiListenScreen SHOWN[] = {
        UI_LISTEN_OPENING, UI_LISTEN_A1, UI_LISTEN_A2, UI_LISTEN_B_LEFT, UI_LISTEN_C2,
        UI_LISTEN_CLOSING,
    };
    uint32_t hashes[6];
    uint32_t crowded = 0;
    uint32_t unlabelled = 0;
    uint32_t blank = 0;
    uint32_t bare_header = 0;
    for (size_t i = 0; i < sizeof(SHOWN) / sizeof(SHOWN[0]); ++i) {
        UiComposeListen(&frame, SHOWN[i], i == 3u ? 0.15 : 0.0, 0.0);
        hashes[i] = HashRows(&frame, 32u, 200u);
        crowded += CountLit(&frame, FRAME_HEIGHT - 31u, FRAME_HEIGHT - 15u) > 0u ? 1u : 0u;
        unlabelled += CountLit(&frame, FRAME_HEIGHT - 14u, FRAME_HEIGHT - 6u) > 100u ? 0u : 1u;
        blank += CountLit(&frame, 36u, 140u) > 100u ? 0u : 1u;
        // The results header is the row at y = 8, sixteen pixels tall. A letter four cells
        // tall does not sit there.
        bare_header += CountLit(&frame, 8u, 24u) > 40u ? 0u : 1u;
    }
    UiCompose(&frame, &f->run, UiStart(&f->run), NULL);
    const uint32_t results_header = CountLit(&frame, 8u, 24u);
    UiComposeListen(&frame, UI_LISTEN_A1, 0.0, 0.0);
    DumpFrame(&frame, "12-listening");
    const uint32_t listen_header = CountLit(&frame, 8u, 24u);
    const uint32_t listen_second = CountLit(&frame, 30u, 46u);
    uint32_t same = 0;
    for (size_t i = 0; i < 6u; ++i) {
        for (size_t j = i + 1u; j < 6u; ++j) {
            same += hashes[i] == hashes[j] ? 1u : 0u;
        }
    }
    const UiFrame small = { f->pixels, 24u, 100u, 24u * 4u };
    const size_t used = (size_t)small.pitch * small.height;
    memset(f->pixels + used, 0xA5, 64u);
    UiComposeListen(&small, UI_LISTEN_A1, 0.0, 0.0);
    uint32_t spilled = 0;
    for (size_t b = used; b < used + 64u; ++b) {
        spilled += f->pixels[b] != 0xA5 ? 1u : 0u;
    }
    const bool shouted = CountLit(&small, 0u, 8u) > 0u;

    // The bar of the sequence's steps, where the running screen draws its own and in its
    // colours: the steps done in the passed colour and the one playing in the dim one.
    static const uint8_t DIM_COLOUR[3] = { 0x88, 0x90, 0xA0 };
    const uint32_t bar = UI_PROGRESS_BAR_TOP;
    const uint32_t bar_end = bar + UI_PROGRESS_BAR_HEIGHT;
    const uint32_t width = FRAME_WIDTH - 64u;
    const uint32_t n = UI_LISTEN_CLOSING + 1u;
    UiComposeListen(&frame, UI_LISTEN_A1, 0.0, 0.0);
    const uint32_t done = CountColour(&frame, 0, bar, FRAME_WIDTH, bar_end, PASSED);
    const uint32_t playing = CountColour(&frame, 0, bar, FRAME_WIDTH, bar_end, DIM_COLOUR);
    const uint32_t step_line = CountLit(&frame, UI_LIST_TOP, UI_LIST_TOP + 16u);
    UiComposeListen(&frame, UI_LISTEN_OPENING, 0.0, 0.0);
    const uint32_t opening = CountColour(&frame, 0, bar, FRAME_WIDTH, bar_end, PASSED);
    UiComposeListen(&frame, UI_LISTEN_CLOSING, 0.0, 0.0);
    const uint32_t closing = CountColour(&frame, 0, bar, FRAME_WIDTH, bar_end, PASSED);
    const bool bar_right =
        done == (width * 2u / n) * UI_PROGRESS_BAR_HEIGHT &&
        playing == (width * 3u / n - width * 2u / n) * UI_PROGRESS_BAR_HEIGHT &&
        opening == 0u && closing == (width * (n - 1u) / n) * UI_PROGRESS_BAR_HEIGHT &&
        step_line > 100u;
    CHECK_TRUE(SUITE, "the listening screen shows its steps done in a bar, as the running screen",
               CHECK_NONE, bar_right,
               "done %u (want %u), playing %u, at the opening %u, at the closing %u (want %u), "
               "step line %u",
               (unsigned)done, (unsigned)((width * 2u / n) * UI_PROGRESS_BAR_HEIGHT),
               (unsigned)playing, (unsigned)opening, (unsigned)closing,
               (unsigned)((width * (n - 1u) / n) * UI_PROGRESS_BAR_HEIGHT),
               (unsigned)step_line);

    CHECK_TRUE(SUITE, "the listening screen leaves the countdown its rows and names its buttons",
               CHECK_NONE, crowded == 0u && unlabelled == 0u && blank == 0u && same == 0u &&
                               spilled == 0u && shouted && bare_header == 0u &&
                               results_header > 40u && listen_header > 40u && listen_second > 40u,
               "countdown %u, no buttons %u, blank %u, identical %u, spilled %u, small %d, "
               "header %u, results %u, listen %u, second %u",
               (unsigned)crowded, (unsigned)unlabelled, (unsigned)blank, (unsigned)same,
               (unsigned)spilled, (int)shouted, (unsigned)bare_header, (unsigned)results_header,
               (unsigned)listen_header, (unsigned)listen_second);
}

/// The pad family's screen: its rows where the running screen has them, the prompt large, the
/// live state and the sticks' boxes following the values, the prompt before in its result's
/// colour, and nothing drawn past a small frame. And its row on the start screen, which turns the
/// family that only runs when named.
static void CheckPad(Fixture *f) {
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    UiPadScreen screen;
    memset(&screen, 0, sizeof(screen));
    screen.heading = "edenprobe 0.3.0 (test)";
    screen.prompt = 1u;
    screen.prompts = (uint32_t)PadPromptCount();
    screen.big = PadPromptAt(0)->big;
    screen.how = PadPromptAt(0)->how;
    screen.state = "waiting: 9 s left";
    screen.controllers = "No1 FullKey";
    UiComposePad(&frame, &screen);
    DumpFrame(&frame, "20-pad-first");
    const uint32_t first = HashRows(&frame, 100u, 300u);
    const uint32_t header = CountLit(&frame, 8u, 24u);
    const uint32_t second = CountLit(&frame, 30u, 46u);
    const uint32_t big_top = UI_PROGRESS_BAR_TOP + UI_PROGRESS_BAR_HEIGHT + 24u;
    const uint32_t big = CountLit(&frame, big_top, big_top + 8u * UI_PAD_BIG);
    const uint32_t foot = CountLit(&frame, FRAME_HEIGHT - 14u, FRAME_HEIGHT - 6u);
    const uint32_t before_none = CountColour(&frame, 0, 0, FRAME_WIDTH, FRAME_HEIGHT, FAILED);

    // The same prompt with the stick moved and a failure before it.
    static const uint8_t MARK[3] = { 0xE0, 0xE4, 0xEC };
    const uint32_t box_x = FRAME_WIDTH - 32u - 2u * 112u - 24u;
    uint32_t box_y = 0;
    for (uint32_t y = big_top; y < FRAME_HEIGHT && box_y == 0u; ++y) {
        const uint8_t *const p = UiPixel(&frame, box_x + 20u, y);
        box_y = p != NULL && p[0] == 0x50 && p[1] == 0x58 && p[2] == 0x68 ? y : 0u;
    }
    const uint32_t centre = CountColour(&frame, box_x + 52u, box_y + 52u, box_x + 60u,
                                        box_y + 60u, MARK);
    screen.left_x = PAD_STICK_FULL;
    screen.left_y = PAD_STICK_FULL;
    screen.prompt = 2u;
    screen.last = "asked for A, arrived B";
    screen.last_result = REPORT_FAIL;
    screen.held = PAD_A | PAD_ZR;
    UiComposePad(&frame, &screen);
    DumpFrame(&frame, "21-pad-moved");
    const uint32_t corner = CountColour(&frame, box_x + 104u - 8u, box_y + 2u, box_x + 110u,
                                        box_y + 12u, MARK);
    const uint32_t centre_after = CountColour(&frame, box_x + 52u, box_y + 52u, box_x + 60u,
                                              box_y + 60u, MARK);
    const uint32_t before_failed =
        CountColour(&frame, 0, box_y + 112u, FRAME_WIDTH, FRAME_HEIGHT - 20u, FAILED);
    const uint32_t bar_done =
        CountColour(&frame, 0, UI_PROGRESS_BAR_TOP, FRAME_WIDTH,
                    UI_PROGRESS_BAR_TOP + UI_PROGRESS_BAR_HEIGHT, PASSED);
    const uint32_t width = FRAME_WIDTH - 64u;
    const bool bar_right = bar_done == (width * 1u / screen.prompts) * UI_PROGRESS_BAR_HEIGHT;

    // A word too long for eight times the cell comes down a size rather than off the frame.
    screen.big = "a prompt far too long to sit at the largest size";
    UiComposePad(&frame, &screen);
    const uint32_t right_edge = CountLit(&frame, UI_PROGRESS_BAR_TOP + 24u,
                                         UI_PROGRESS_BAR_TOP + 120u);
    const UiFrame small = { f->pixels, 24u, 100u, 24u * 4u };
    const size_t used = (size_t)small.pitch * small.height;
    memset(f->pixels + used, 0xA5, 64u);
    UiComposePad(&small, &screen);
    uint32_t spilled = 0;
    for (size_t b = used; b < used + 64u; ++b) {
        spilled += f->pixels[b] != 0xA5 ? 1u : 0u;
    }
    UiComposePad(&frame, NULL);
    CHECK_TRUE(SUITE, "the pad screen shows the prompt large, what the controller sends and the "
                      "prompt before in its colour",
               CHECK_NONE,
               header > 40u && second > 40u && big > 1500u && foot > 100u && before_none == 0u &&
                   box_y > 0u && centre > 0u && corner > 0u && centre_after == 0u &&
                   before_failed > 100u && bar_right && first != HashRows(&frame, 100u, 300u) &&
                   right_edge > 0u && spilled == 0u,
               "header %u second %u big %u foot %u red before %u; box at %u, centre %u corner %u "
               "after %u; red after %u; bar %u; long %u; spilled %u",
               (unsigned)header, (unsigned)second, (unsigned)big, (unsigned)foot,
               (unsigned)before_none, (unsigned)box_y, (unsigned)centre, (unsigned)corner,
               (unsigned)centre_after, (unsigned)before_failed, (unsigned)bar_done,
               (unsigned)right_edge, (unsigned)spilled);

    // The family only runs when named, on the screen as in the file.
    UiStartInfo info;
    memset(&info, 0, sizeof(info));
    info.families_present = MODE_FAMILIES_PRESENT;
    const UiStartState plain = StartFrom(NULL);
    const UiStartState pad = StartChoose(plain, UI_START_ROW_PAD, &info);
    const RunMode chosen = UiStartMode(&pad, ModeFromText(NULL), MODE_FAMILIES_PRESENT);
    char key[128];
    ModeKey(key, sizeof(key), &chosen);
    const RunMode named = ModeFromText("quick pad");
    char key_named[128];
    ModeKey(key_named, sizeof(key_named), &named);
    CHECK_TRUE(SUITE, "the pad family runs only when named, and the start screen has its row",
               CHECK_NONE,
               (plain.families & MODE_FAMILY_PAD) == 0u &&
                   (pad.families & MODE_FAMILY_PAD) != 0u &&
                   (ModeFamiliesToRun(&chosen, MODE_FAMILIES_PRESENT) & MODE_FAMILY_PAD) != 0u &&
                   strstr(key, "pad") != NULL && strcmp(key_named, "pad") == 0 &&
                   ModeFamiliesToRun(&named, MODE_FAMILIES_PRESENT) == MODE_FAMILY_PAD &&
                   UiFamilyOf("pad") == UI_FAMILY_PAD &&
                   strcmp(UiFamilyName(UI_FAMILY_PAD), "pad") == 0,
               "plain %x, row %x, key \"%s\", named \"%s\"", (unsigned)plain.families,
               (unsigned)pad.families, key, key_named);
}

static UiTap TapAt(uint32_t x, uint32_t y) {
    const UiTap tap = { x, y, x, y };
    return tap;
}

/// The middle of box @p index of a bar of @p count.
static UiTap TapBox(uint32_t count, uint32_t index) {
    const UiRect box = UiTouchBox(FRAME_WIDTH, FRAME_HEIGHT, count, index);
    return TapAt(box.x + box.width / 2u, box.y + box.height / 2u);
}

/// Whether box @p index of a bar of @p count is drawn whole: the band inside it above its word,
/// every pixel of it in the cursor row's colour, and its word under that.
static bool BoxDrawn(const UiFrame *frame, uint32_t count, uint32_t index) {
    const UiRect box = UiTouchBox(frame->width, frame->height, count, index);
    const uint32_t word = box.y + (box.height - 16u) / 2u;
    return box.width > 0u &&
           CountColour(frame, box.x + 1u, box.y + 1u, box.x + box.width - 1u, word, SELECTED) ==
               (box.width - 2u) * (word - box.y - 1u) &&
           CountLitIn(frame, box.x + 1u, word, box.x + box.width - 1u, word + 16u) >
               (box.width - 2u) * 16u / 2u;
}

/// Touch: a tap is one finger that went down after the settling frames and came up again, a row
/// tapped is the cursor's and then chosen, and each box does what its word says.
static void CheckTouch(Fixture *f) {
    // The finger, frame by frame.
    UiTouch touch;
    UiTap tap = { 0, 0, 0, 0 };
    UiTouchReset(&touch);
    const bool held_at_open = UiTouchStep(&touch, 1u, 50u, 50u, 0u, &tap) == UI_TOUCH_NONE &&
                              UiTouchStep(&touch, 0u, 0u, 0u, 1u, &tap) == UI_TOUCH_NONE;
    const bool settling = UiTouchStep(&touch, 1u, 50u, 50u, UI_SETTLING_FRAMES - 1u, &tap) ==
                              UI_TOUCH_NONE &&
                          UiTouchStep(&touch, 0u, 0u, 0u, UI_SETTLING_FRAMES, &tap) ==
                              UI_TOUCH_NONE;
    const UiTouchEvent began = UiTouchStep(&touch, 1u, 100u, 200u, UI_SETTLING_FRAMES, &tap);
    const UiTouchEvent moved = UiTouchStep(&touch, 1u, 110u, 205u, UI_SETTLING_FRAMES, &tap);
    const UiTouchEvent lifted = UiTouchStep(&touch, 0u, 0u, 0u, UI_SETTLING_FRAMES, &tap);
    const bool one = began == UI_TOUCH_BEGAN && moved == UI_TOUCH_NONE &&
                     lifted == UI_TOUCH_TAP && tap.x0 == 100u && tap.y0 == 200u &&
                     tap.x1 == 110u && tap.y1 == 205u;
    // A second finger, joining or there from the start, makes it no tap.
    const bool two = UiTouchStep(&touch, 1u, 10u, 10u, 20u, &tap) == UI_TOUCH_BEGAN &&
                     UiTouchStep(&touch, 2u, 10u, 10u, 21u, &tap) == UI_TOUCH_NONE &&
                     UiTouchStep(&touch, 1u, 10u, 10u, 22u, &tap) == UI_TOUCH_NONE &&
                     UiTouchStep(&touch, 0u, 0u, 0u, 23u, &tap) == UI_TOUCH_NONE &&
                     UiTouchStep(&touch, 2u, 10u, 10u, 24u, &tap) == UI_TOUCH_NONE &&
                     UiTouchStep(&touch, 0u, 0u, 0u, 25u, &tap) == UI_TOUCH_NONE;
    CHECK_TRUE(SUITE, "a tap is one finger down after the screen settled, counted when lifted",
               CHECK_NONE, held_at_open && settling && one && two,
               "held at open %d, settling %d, one %d (began %d moved %d lifted %d at %u,%u to "
               "%u,%u), two fingers %d",
               held_at_open, settling, one, (int)began, (int)moved, (int)lifted,
               (unsigned)tap.x0, (unsigned)tap.y0, (unsigned)tap.x1, (unsigned)tap.y1, two);

    // The start screen: a row, the same row again to choose it, the box to begin.
    UiStartInfo info;
    memset(&info, 0, sizeof(info));
    info.heading = "edenprobe 0.3.0 (test)";
    info.families_present = MODE_FAMILIES_PRESENT;
    const uint32_t row_y = UiStartRowTop(UI_START_ROW_GRAPHICS) + UI_ROW_HEIGHT / 2u;
    const UiStartState plain = StartFrom(NULL);
    UiTap graphics = TapAt(300u, row_y);
    const UiStartState moved_to = UiStartTap(plain, &graphics, FRAME_WIDTH, FRAME_HEIGHT, &info);
    const UiStartState chosen = UiStartTap(moved_to, &graphics, FRAME_WIDTH, FRAME_HEIGHT, &info);
    UiTap slid = { 300u, row_y, 300u, row_y + UI_ROW_HEIGHT };
    const UiStartState slid_off = UiStartTap(moved_to, &slid, FRAME_WIDTH, FRAME_HEIGHT, &info);
    UiTap heading = TapAt(300u, 12u);
    const UiStartState off_rows = UiStartTap(plain, &heading, FRAME_WIDTH, FRAME_HEIGHT, &info);
    UiTap begin_row = TapAt(300u, UiStartRowTop(UI_START_ROW_BEGIN) + 4u);
    const UiStartState begun_row = UiStartTap(plain, &begin_row, FRAME_WIDTH, FRAME_HEIGHT, &info);
    UiTap begin_box = TapBox(1u, 0u);
    const UiStartState begun_box =
        UiStartTap(moved_to, &begin_box, FRAME_WIDTH, FRAME_HEIGHT, &info);
    const bool start =
        moved_to.cursor == UI_START_ROW_GRAPHICS && moved_to.families == plain.families &&
        moved_to.changed && (chosen.families & MODE_FAMILY_GRAPHICS) == 0u &&
        (plain.families & MODE_FAMILY_GRAPHICS) != 0u && slid_off.families == moved_to.families &&
        slid_off.cursor == moved_to.cursor && off_rows.cursor == plain.cursor &&
        !off_rows.begun && begun_row.begun && begun_box.begun &&
        (begun_box.families & MODE_FAMILY_GRAPHICS) != 0u;
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    UiComposeStart(&frame, &info, plain, 7u);
    const bool start_bar = BoxDrawn(&frame, 1u, 0u);

    // The resume screen: a choice, then the same choice to take it.
    UiTap over = TapAt(300u, UiResumeRowTop(UI_RESUME_START_OVER) + UI_ROW_HEIGHT);
    const UiResumeState resume_moved = UiResumeTap(UiResumeOpen(), &over, FRAME_WIDTH,
                                                   FRAME_HEIGHT);
    const UiResumeState resume_taken = UiResumeTap(resume_moved, &over, FRAME_WIDTH, FRAME_HEIGHT);
    UiTap go_on = TapAt(300u, UiResumeRowTop(UI_RESUME_GO_ON) + 4u);
    const UiResumeState resume_on = UiResumeTap(UiResumeOpen(), &go_on, FRAME_WIDTH, FRAME_HEIGHT);
    const bool resume = resume_moved.cursor == UI_RESUME_START_OVER && !resume_moved.decided &&
                        resume_taken.decided && resume_taken.start_over && resume_on.decided &&
                        !resume_on.start_over;
    CHECK_TRUE(SUITE, "on the start and resume screens a tapped row is the cursor's, then chosen",
               CHECK_NONE, start && start_bar && resume,
               "start: cursor %u families %x, again %x, slid %x, heading %u, begin row %d box %d, "
               "bar drawn %d; resume: %u %d, again %d %d, go on %d %d",
               (unsigned)moved_to.cursor, (unsigned)moved_to.families, (unsigned)chosen.families,
               (unsigned)slid_off.families, (unsigned)off_rows.cursor, begun_row.begun,
               begun_box.begun, start_bar, (unsigned)resume_moved.cursor, resume_moved.decided,
               resume_taken.decided, resume_taken.start_over, resume_on.decided,
               resume_on.start_over);

    // The results: a group's row, again to open it, a case's row, again to open it, then the
    // case after, back, back and quit, each from the bar.
    const UiRun *const run = &f->run;
    UiState state = UiStart(run);
    const uint32_t rows = UiListRows(UI_LEVEL_OVERVIEW, FRAME_HEIGHT);
    const uint32_t total = UiOverviewRow(run, run->group_count - 1u) + 1u;
    const uint32_t first = UiListFirst(UiOverviewRow(run, state.group), total, rows);
    const uint32_t ssl = GroupNamed(run, "ssl");
    const uint32_t ssl_y = UI_LIST_TOP + (UiOverviewRow(run, ssl) - first) * UI_ROW_HEIGHT + 10u;
    UiTap on_ssl = TapAt(400u, ssl_y);
    const UiState on_group = UiTapStep(state, &on_ssl, run, FRAME_WIDTH, FRAME_HEIGHT);
    const UiState opened = UiTapStep(on_group, &on_ssl, run, FRAME_WIDTH, FRAME_HEIGHT);
    // The family heading above the first group, the list's top row with the cursor on that group,
    // is not a group.
    UiState top = state;
    top.group = 0u;
    UiTap on_heading = TapAt(400u, UI_LIST_TOP + UI_ROW_HEIGHT / 2u);
    const UiState headed = UiTapStep(top, &on_heading, run, FRAME_WIDTH, FRAME_HEIGHT);
    const uint32_t named = CaseNamed(run, ssl, FAILS_IN_SSL);
    const uint32_t other = named > 0u ? named - 1u : named + 1u;
    const uint32_t case_first = UiListFirst(opened.index, run->groups[ssl].count,
                                            UiListRows(UI_LEVEL_GROUP, FRAME_HEIGHT));
    UiTap on_case = TapAt(400u, UI_LIST_TOP + (other - case_first) * UI_ROW_HEIGHT + 10u);
    const UiState on_other = UiTapStep(opened, &on_case, run, FRAME_WIDTH, FRAME_HEIGHT);
    const UiState in_case = UiTapStep(on_other, &on_case, run, FRAME_WIDTH, FRAME_HEIGHT);
    UiCompose(&frame, run, in_case, NULL);
    const bool case_bar = BoxDrawn(&frame, 3u, 0u) && BoxDrawn(&frame, 3u, 2u);
    UiTap next = TapBox(3u, 1u);
    const UiState after = UiTapStep(in_case, &next, run, FRAME_WIDTH, FRAME_HEIGHT);
    UiTap back = TapBox(3u, 2u);
    const UiState backed = UiTapStep(after, &back, run, FRAME_WIDTH, FRAME_HEIGHT);
    UiCompose(&frame, run, backed, NULL);
    UiComposeCountdown(&frame, 9u);
    const bool group_bar = BoxDrawn(&frame, 1u, 0u);
    UiTap one_box = TapBox(1u, 0u);
    const UiState overview = UiTapStep(backed, &one_box, run, FRAME_WIDTH, FRAME_HEIGHT);
    const UiState quit = UiTapStep(overview, &one_box, run, FRAME_WIDTH, FRAME_HEIGHT);
    const bool walk = on_group.group == ssl && on_group.level == UI_LEVEL_OVERVIEW &&
                      on_group.changed && opened.level == UI_LEVEL_GROUP &&
                      opened.group == ssl && headed.group == 0u && UiOverviewRow(run, 0u) == 1u &&
                      headed.level == UI_LEVEL_OVERVIEW && on_other.index == other &&
                      on_other.level == UI_LEVEL_GROUP && in_case.level == UI_LEVEL_CASE &&
                      in_case.index == other && after.index == other + 1u &&
                      backed.level == UI_LEVEL_GROUP && overview.level == UI_LEVEL_OVERVIEW &&
                      !overview.quit && quit.quit;

    // A drawing case has a fourth box, for the view, and it turns the view.
    const uint32_t pictures = GroupNamed(run, "intmath");
    UiState drawn = UiStart(run);
    drawn.level = UI_LEVEL_CASE;
    drawn.group = pictures;
    drawn.index = CaseNamed(run, pictures, FAILS_WITH_PICTURE);
    UiCompose(&frame, run, drawn, &f->picture);
    const bool four = BoxDrawn(&frame, 4u, 0u) && BoxDrawn(&frame, 4u, 3u);
    UiTap view = TapBox(4u, 2u);
    const UiState turned = UiTapStep(drawn, &view, run, FRAME_WIDTH, FRAME_HEIGHT);
    const UiFrame tiny = { f->pixels, 320u, 200u, 320u * 4u };
    const UiState small = UiTapStep(drawn, &view, run, tiny.width, tiny.height);
    CHECK_TRUE(SUITE, "on the results a tapped row is the cursor's, then opened; the bar's boxes "
                      "do what they say",
               CHECK_NONE,
               walk && case_bar && group_bar && four && turned.view == UI_VIEW_GOT &&
                   small.view == drawn.view,
               "group %u (ssl %u) level %d, opened %d; heading %u; case %u (wanted %u), opened "
               "%d; next %u; back %d, %d; quit %d; bars %d %d %d; view %d; small frame %d",
               (unsigned)on_group.group, (unsigned)ssl, (int)on_group.level, (int)opened.level,
               (unsigned)headed.group, (unsigned)on_other.index, (unsigned)other,
               (int)in_case.level, (unsigned)after.index, (int)backed.level,
               (int)overview.level, quit.quit, case_bar, group_bar, four, (int)turned.view,
               (int)small.view);

    // The pad screen: yes and no on the bar while a question waits, and nothing otherwise.
    UiPadScreen pad;
    memset(&pad, 0, sizeof(pad));
    pad.heading = "edenprobe 0.3.0 (test)";
    pad.prompt = 1u;
    pad.prompts = 2u;
    pad.big = "felt?";
    pad.how = "did the controller vibrate?";
    UiComposePad(&frame, &pad);
    const bool quiet = !BoxDrawn(&frame, 2u, 0u) && !BoxDrawn(&frame, 2u, 1u);
    pad.asks_yes_no = true;
    UiComposePad(&frame, &pad);
    const bool asking = BoxDrawn(&frame, 2u, 0u) && BoxDrawn(&frame, 2u, 1u);
    UiTap yes = TapBox(2u, 0u);
    UiTap no = TapBox(2u, 1u);
    UiTap middle = TapAt(FRAME_WIDTH / 2u, FRAME_HEIGHT / 2u);
    const bool answers =
        UiPadTap(&yes, FRAME_WIDTH, FRAME_HEIGHT, true) == PAD_ANSWER_YES &&
        UiPadTap(&no, FRAME_WIDTH, FRAME_HEIGHT, true) == PAD_ANSWER_NO &&
        UiPadTap(&middle, FRAME_WIDTH, FRAME_HEIGHT, true) == PAD_ANSWER_NONE &&
        UiPadTap(&yes, FRAME_WIDTH, FRAME_HEIGHT, false) == PAD_ANSWER_NONE;
    CHECK_TRUE(SUITE, "the pad screen's question takes yes or no on the bar, and only then",
               CHECK_NONE, quiet && asking && answers, "bar before %d, while asking %d, answers %d",
               !quiet, asking, answers);
}

/// The touch suite's screen: every target drawn at the point touchref.c names, nothing written
/// over one, the drag's two ends on its row, and a square where each finger is.
static void CheckTouchScreen(Fixture *f) {
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    static const uint8_t AMBER[3] = { 0xE8, 0xB0, 0x40 };
    UiTouchScreen screen;
    memset(&screen, 0, sizeof(screen));
    screen.heading = "edenprobe 0.3.0 (test)";
    screen.prompts = (uint32_t)TouchPromptCount();
    screen.state = "waiting: 9 s left";
    screen.now =
        "touching now: 1, id 0 at (1184,624), 90 by 90, angle 0, and words to fill the line";
    screen.last = "lifted at (96,96), +0 +0 from the target at (96,96): 0 pixels off, within 48; "
                  "came down at (90,90), id 3, 2 states";
    screen.last_result = REPORT_PASS;
    uint32_t missing = 0;
    uint32_t covered = 0;
    uint32_t targets = 0;
    for (size_t i = 0; i < TouchPromptCount(); ++i) {
        const TouchPrompt *const p = TouchPromptAt(i);
        if (p->ask != TOUCH_ASK_TARGET) {
            continue;
        }
        ++targets;
        screen.prompt = (uint32_t)i + 1u;
        screen.big = p->big;
        screen.how = p->how;
        screen.marks = 1u;
        screen.mark_x[0] = p->x;
        screen.mark_y[0] = p->y;
        UiComposeTouch(&frame, &screen);
        if (targets == 1u) {
            DumpFrame(&frame, "22-touch-target");
        }
        // The ring and the cross are there, and inside the tolerance nothing else is.
        const uint32_t r = UI_TOUCH_MARK_RADIUS + 8u;
        missing += CountColour(&frame, p->x - r, p->y - r, p->x + r, p->y + r, AMBER) < 300u;
        const uint32_t lit = CountLitIn(&frame, p->x - TOUCH_TOLERANCE, p->y - TOUCH_TOLERANCE,
                                        p->x + TOUCH_TOLERANCE, p->y + TOUCH_TOLERANCE);
        const uint32_t amber = CountColour(&frame, p->x - TOUCH_TOLERANCE,
                                           p->y - TOUCH_TOLERANCE, p->x + TOUCH_TOLERANCE,
                                           p->y + TOUCH_TOLERANCE, AMBER);
        covered += lit != amber;
    }
    // The drag: two marks on its row and the row between them; two fingers drawn as squares.
    screen.big = "drag";
    screen.how = TouchPromptAt(7)->how;
    screen.marks = 2u;
    screen.mark_x[0] = TOUCH_DRAG_FROM_X;
    screen.mark_x[1] = TOUCH_DRAG_TO_X;
    screen.mark_y[0] = TOUCH_DRAG_Y;
    screen.mark_y[1] = TOUCH_DRAG_Y;
    screen.touches = 2u;
    screen.touch_x[0] = 400u;
    screen.touch_y[0] = 300u;
    screen.touch_x[1] = 900u;
    screen.touch_y[1] = 300u;
    UiComposeTouch(&frame, &screen);
    DumpFrame(&frame, "23-touch-drag");
    const uint32_t ends = CountColour(&frame, TOUCH_DRAG_FROM_X - 40u, TOUCH_DRAG_Y - 40u,
                                      TOUCH_DRAG_FROM_X + 40u, TOUCH_DRAG_Y + 40u, AMBER) +
                          CountColour(&frame, TOUCH_DRAG_TO_X - 40u, TOUCH_DRAG_Y - 40u,
                                      TOUCH_DRAG_TO_X + 40u, TOUCH_DRAG_Y + 40u, AMBER);
    const uint32_t row = CountLitIn(&frame, 640u, TOUCH_DRAG_Y, 641u, TOUCH_DRAG_Y + 2u);
    const uint32_t fingers = CountColour(&frame, 394u, 294u, 406u, 306u, PASSED) +
                             CountColour(&frame, 894u, 294u, 906u, 306u, PASSED);
    UiComposeTouch(&frame, NULL);
    CHECK_TRUE(SUITE, "the touch screen draws each target where its case aims and writes over none",
               CHECK_NONE,
               targets == 5u && missing == 0u && covered == 0u && ends > 600u && row == 2u &&
                   fingers == 288u,
               "%u targets, %u missing, %u written over; drag ends %u, row %u; fingers %u",
               (unsigned)targets, (unsigned)missing, (unsigned)covered, (unsigned)ends,
               (unsigned)row, (unsigned)fingers);
}

void RunSupportScreens(void) {
    Fixture *const f = (Fixture *)calloc(1, sizeof(Fixture));
    if (f == NULL) {
        ReportCase(SUITE, "the screens have a run to show", CHECK_NONE, false,
                   "no memory for the fixture");
        return;
    }
    CheckIndex();
    CheckStore();
    CheckFamilies();
    CheckRecordFill();
    BuildRun(f);
    CheckWalk(f);
    CheckNavigation(f);
    CheckButtons();
    CheckListen(f);
    CheckPad(f);
    CheckTouch(f);
    CheckTouchScreen(f);
    CheckScreens(f);
    CheckOverflow(f);
    CheckEdges(f);
    CheckStartScreen(f);
    CheckStartMenu(f);
    CheckResumeScreen(f);
    CheckProgress(f);
    CheckCrashed(f);
    free(f);
}
