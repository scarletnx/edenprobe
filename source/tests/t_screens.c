// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../check.h"
#include "../picture.h"
#include "../report.h"
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

// The shape of a real run: every suite with the counts a quick run of main 2b74917 reported,
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
    ReportBuildResultLine(line, sizeof(line), "acc", name, "none", "skip", detail, NULL);
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
    // Forty suites of one case each: the index holds thirty two and counts the rest.
    for (uint32_t i = 0; i < 40u; ++i) {
        char suite[16];
        snprintf(suite, sizeof(suite), "suite%02u", (unsigned)i);
        Fill(&f->records[i], suite, "a case", "none", REPORT_PASS, "", -1);
    }
    UiRunIndex(&f->small, f->records, 40, 40);
    const UiFrame frame = { f->pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };
    UiCompose(&frame, &f->small, UiStart(&f->small), NULL);
    const uint32_t said = CountColour(&frame, 0, 50, FRAME_WIDTH, 62, SKIPPED);
    const bool counted = f->small.group_count == UI_MAX_GROUPS && f->small.unplaced == 8u;

    // And a run whose store ran out: the report said more cases than the screens hold.
    UiRunIndex(&f->small, f->records, 3, 9);
    UiCompose(&frame, &f->small, UiStart(&f->small), NULL);
    const uint32_t short_said = CountColour(&frame, 0, 50, FRAME_WIDTH, 62, SKIPPED);
    UiRunIndex(&f->small, f->records, 3, 3);
    UiCompose(&frame, &f->small, UiStart(&f->small), NULL);
    const uint32_t quiet = CountColour(&frame, 0, 50, FRAME_WIDTH, 62, SKIPPED);
    CHECK_TRUE(SUITE, "cases the screens could not hold are counted and the overview says so",
               CHECK_NONE, counted && said > 50u && short_said > 50u && quiet == 0u,
               "%u groups and %u unplaced; %u, %u and %u pixels of the warning",
               (unsigned)f->small.group_count, (unsigned)f->small.unplaced, (unsigned)said,
               (unsigned)short_said, (unsigned)quiet);
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
    for (size_t i = 0; i < sizeof(SCREENS) / sizeof(SCREENS[0]); ++i) {
        UiState at = UiStart(run);
        at.level = SCREENS[i].level;
        at.group = SCREENS[i].group;
        at.index = SCREENS[i].index_name != NULL
                       ? CaseNamed(run, SCREENS[i].group, SCREENS[i].index_name)
                       : 0u;
        at.view = SCREENS[i].view;
        UiCompose(&frame, run, at, UiOpenPicture(run, at) >= 0 ? &f->picture : NULL);
        crowded += CountLit(&frame, FRAME_HEIGHT - 31u, FRAME_HEIGHT - 15u) > 0u ? 1u : 0u;
        unlabelled += CountLit(&frame, FRAME_HEIGHT - 14u, FRAME_HEIGHT - 6u) > 100u ? 0u : 1u;
    }
    CHECK_TRUE(SUITE, "every screen leaves the countdown its rows and names its buttons",
               CHECK_NONE, crowded == 0u && unlabelled == 0u,
               "%u screens drew into the countdown's rows, %u had no button line",
               (unsigned)crowded, (unsigned)unlabelled);
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

void RunSupportScreens(void) {
    Fixture *const f = (Fixture *)calloc(1, sizeof(Fixture));
    if (f == NULL) {
        ReportCase(SUITE, "the screens have a run to show", CHECK_NONE, false,
                   "no memory for the fixture");
        return;
    }
    CheckIndex();
    CheckStore();
    CheckRecordFill();
    BuildRun(f);
    CheckWalk(f);
    CheckNavigation(f);
    CheckButtons();
    CheckScreens(f);
    CheckOverflow(f);
    CheckEdges(f);
    free(f);
}
