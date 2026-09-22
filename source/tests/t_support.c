// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../check.h"
#include "../picture.h"
#include "../report.h"
#include "../ui.h"
#include "tests.h"

// The two things this program does around a verdict rather than to reach one: the picture it
// leaves beside a failing case, and the screen the detailed mode puts it on. Neither decides
// anything and no case here asks the hardware for anything, so every line is the none tier. It
// runs in both builds, because a writer that only ran inside an emulator is one nobody can check.

static const char *const SUITE = "support";

// Long enough for the scratch directory and any path under it, and both are bounded in the
// format so the compiler can see it.
#define FULL_PATH_MAX 320

// Small on purpose: eight by four is enough to tell a row from a column and to have a pixel
// each for the three verdicts, and small enough that every byte of every file written here can
// be compared by hand.
#define FIXTURE_WIDTH 8u
#define FIXTURE_HEIGHT 4u
#define FIXTURE_PIXELS (FIXTURE_WIDTH * FIXTURE_HEIGHT)

#ifdef __SWITCH__
static const char *const SCRATCH = "sdmc:/gfxprobe/support-check";
#else
static const char *const SCRATCH = "build-host/support-check";
#endif

// The frame the layout cases compose into. Bigger than the fixture by enough that the panels
// land where the real screen puts them, and small enough to walk pixel by pixel.
#define FRAME_WIDTH 320u
#define FRAME_HEIGHT 240u

// Every file this suite writes, so that it can take them away again. A passing suite that left
// a directory of pictures called "expected" beside the report would read, to anyone who found
// it, as exactly the stored reference this program does not have.
#define WRITTEN_MAX 8
static PicturePaths written[WRITTEN_MAX];
static size_t written_count;

static uint8_t fixture_got[FIXTURE_PIXELS * 4u];
static uint8_t fixture_expected[FIXTURE_PIXELS * 4u];
static uint8_t fixture_verdict[FIXTURE_PIXELS];

/// Keeps one set of paths for the sweep at the end.
static void Remember(const PicturePaths *paths) {
    if (written_count < WRITTEN_MAX) {
        written[written_count++] = *paths;
    }
}

/// Removes one file, if there is one to remove.
static void Discard(const char *relative) {
    if (relative[0] == '\0') {
        return;
    }
    char full[FULL_PATH_MAX];
    snprintf(full, sizeof(full), "%.120s/%.159s", SCRATCH, relative);
    (void)remove(full);
}

/// Whether anything this suite wrote is still there. The directories go too, youngest first,
/// because an empty one left behind is still a directory nobody can explain.
static bool SweepUp(void) {
    for (size_t i = 0; i < written_count; ++i) {
        Discard(written[i].image);
        Discard(written[i].expected);
        Discard(written[i].diff);
        Discard(written[i].raw);
    }

    bool left = false;
    for (size_t i = 0; i < written_count; ++i) {
        const char *const paths[4] = { written[i].image, written[i].expected, written[i].diff,
                                       written[i].raw };
        for (size_t which = 0; which < 4u; ++which) {
            if (paths[which][0] == '\0') {
                continue;
            }
            char full[FULL_PATH_MAX];
            snprintf(full, sizeof(full), "%.120s/%.159s", SCRATCH, paths[which]);
            FILE *file = fopen(full, "rb");
            if (file != NULL) {
                fclose(file);
                left = true;
            }
        }
    }

    char directory[FULL_PATH_MAX];
    snprintf(directory, sizeof(directory), "%.120s/images/support", SCRATCH);
    (void)rmdir(directory);
    snprintf(directory, sizeof(directory), "%.120s/images", SCRATCH);
    (void)rmdir(directory);
    (void)rmdir(SCRATCH);
    return !left;
}

/// What one PAM file turned out to hold.
typedef struct {
    bool parsed;
    unsigned width;
    unsigned height;
    unsigned depth;
    unsigned maxval;
    char tupltype[16];
    size_t payload;
    uint8_t bytes[FIXTURE_PIXELS * 4u];
} ParsedPam;

/// Reads a PAM back the way a stranger would: the header line by line, then exactly as many
/// bytes as the header says there are, and nothing taken on trust from whoever wrote it.
static void ParsePam(ParsedPam *out, const char *path) {
    memset(out, 0, sizeof(*out));
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return;
    }

    char line[64];
    if (fgets(line, sizeof(line), file) == NULL || strcmp(line, "P7\n") != 0) {
        fclose(file);
        return;
    }
    bool ended = false;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strcmp(line, "ENDHDR\n") == 0) {
            ended = true;
            break;
        }
        char word[16];
        unsigned value = 0;
        if (sscanf(line, "TUPLTYPE %15s", word) == 1) {
            snprintf(out->tupltype, sizeof(out->tupltype), "%s", word);
        } else if (sscanf(line, "WIDTH %u", &value) == 1) {
            out->width = value;
        } else if (sscanf(line, "HEIGHT %u", &value) == 1) {
            out->height = value;
        } else if (sscanf(line, "DEPTH %u", &value) == 1) {
            out->depth = value;
        } else if (sscanf(line, "MAXVAL %u", &value) == 1) {
            out->maxval = value;
        }
    }
    if (!ended) {
        fclose(file);
        return;
    }

    out->payload = fread(out->bytes, 1, sizeof(out->bytes), file);
    // One byte more than the header promised would mean the writer and the header disagree,
    // which is worse than a short file because nothing downstream would notice.
    const bool exact = out->payload == (size_t)out->width * out->height * out->depth &&
                       fgetc(file) == EOF;
    out->parsed = exact;
    fclose(file);
}

static size_t ReadWholeFile(const char *path, uint8_t *out, size_t out_size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    const size_t read = fread(out, 1, out_size, file);
    fclose(file);
    return read;
}

/// Fills the fixture and hands back a picture whose verdicts are set by hand, so that what the
/// cases below check is the writer and not another suite's judgement.
static Picture *BuildFixture(PictureFormat format) {
    for (uint32_t i = 0; i < FIXTURE_PIXELS; ++i) {
        for (uint32_t channel = 0; channel < 4u; ++channel) {
            fixture_got[i * 4u + channel] = (uint8_t)(i * 4u + channel);
        }
    }
    Picture *const picture = PictureBegin(FIXTURE_WIDTH, FIXTURE_HEIGHT, format, fixture_got);
    if (picture == NULL) {
        return NULL;
    }
    for (uint32_t y = 0; y < FIXTURE_HEIGHT; ++y) {
        for (uint32_t x = 0; x < FIXTURE_WIDTH; ++x) {
            const uint32_t index = y * FIXTURE_WIDTH + x;
            uint8_t wanted[4];
            memcpy(wanted, fixture_got + index * 4u, 4u);
            PictureVerdict verdict = PICTURE_MATCHED;
            if (x == 1u && y == 0u) {
                wanted[0] = 0x99;
                verdict = PICTURE_WRONG;
            } else if (x == 2u && y == 0u) {
                // Different bytes and still accepted, which is what a tolerance looks like from
                // in here. The diff must not paint this red.
                wanted[3] = 0x77;
            } else if (x == 3u && y == 0u) {
                PictureSet(picture, x, y, NULL, PICTURE_UNJUDGED);
                continue;
            }
            PictureSet(picture, x, y, wanted, verdict);
        }
    }
    memcpy(fixture_expected, picture->expected, sizeof(fixture_expected));
    memcpy(fixture_verdict, picture->verdict, sizeof(fixture_verdict));
    return picture;
}

static void CheckNaming(void) {
    char stem[64];
    PictureStem(stem, sizeof(stem), "clear to opaque red");
    CHECK_TRUE(SUITE, "a case name becomes the file name it reads like", CHECK_NONE,
               strcmp(stem, "clear-to-opaque-red") == 0, "got \"%s\"", stem);

    PictureStem(stem, sizeof(stem), "  A fragment NEARER than 0.5 (passes!)  ");
    CHECK_TRUE(SUITE, "punctuation and case leave the file name alone", CHECK_NONE,
               strcmp(stem, "a-fragment-nearer-than-0-5-passes") == 0, "got \"%s\"", stem);

    PictureStem(stem, sizeof(stem), "...");
    CHECK_TRUE(SUITE, "a name with nothing usable in it still names a file", CHECK_NONE,
               strcmp(stem, "unnamed") == 0, "got \"%s\"", stem);
}

static void CheckWriting(void) {
    Picture *const picture = BuildFixture(PICTURE_COLOUR);
    if (picture == NULL) {
        ReportCase(SUITE, "a picture opens for a small target", CHECK_NONE, false,
                   "PictureBegin refused a target it should have taken");
        return;
    }

    PicturePaths paths;
    PictureFinish(SUITE, "a colour picture", "none", true, "", &paths);
    Remember(&paths);
    CHECK_TRUE(SUITE, "a picture is written where the line says it is", CHECK_NONE,
               strcmp(paths.image, "images/support/a-colour-picture.pam") == 0 &&
                   strcmp(paths.expected, "images/support/a-colour-picture-expected.pam") == 0 &&
                   strcmp(paths.diff, "images/support/a-colour-picture-diff.pam") == 0,
               "image \"%s\", expected \"%s\", diff \"%s\"", paths.image, paths.expected,
               paths.diff);
    CHECK_TRUE(SUITE, "a colour picture keeps no raw file beside it", CHECK_NONE,
               paths.raw[0] == '\0', "got \"%s\"", paths.raw);

    char full[FULL_PATH_MAX];
    ParsedPam parsed;
    snprintf(full, sizeof(full), "%.120s/%.159s", SCRATCH, paths.image);
    ParsePam(&parsed, full);
    CHECK_TRUE(SUITE, "the picture parses back as the image its header claims", CHECK_NONE,
               parsed.parsed && parsed.width == FIXTURE_WIDTH &&
                   parsed.height == FIXTURE_HEIGHT && parsed.depth == 4u &&
                   parsed.maxval == 255u && strcmp(parsed.tupltype, "RGB_ALPHA") == 0,
               "parsed=%d %ux%u depth %u maxval %u tupltype \"%s\", %u bytes",
               (int)parsed.parsed, parsed.width, parsed.height, parsed.depth, parsed.maxval,
               parsed.tupltype, (unsigned)parsed.payload);
    CHECK_TRUE(SUITE, "the picture holds the bytes the readback handed over", CHECK_NONE,
               parsed.parsed && memcmp(parsed.bytes, fixture_got, sizeof(fixture_got)) == 0,
               "first pixel %02x%02x%02x%02x, wanted %02x%02x%02x%02x", parsed.bytes[0],
               parsed.bytes[1], parsed.bytes[2], parsed.bytes[3], fixture_got[0], fixture_got[1],
               fixture_got[2], fixture_got[3]);

    ParsedPam wanted;
    snprintf(full, sizeof(full), "%.120s/%.159s", SCRATCH, paths.expected);
    ParsePam(&wanted, full);
    CHECK_TRUE(SUITE, "the expected picture holds what the judge expected", CHECK_NONE,
               wanted.parsed &&
                   memcmp(wanted.bytes, fixture_expected, sizeof(fixture_expected)) == 0,
               "parsed=%d, pixel one is %02x%02x%02x%02x, wanted %02x%02x%02x%02x",
               (int)wanted.parsed, wanted.bytes[4], wanted.bytes[5], wanted.bytes[6],
               wanted.bytes[7], fixture_expected[4], fixture_expected[5], fixture_expected[6],
               fixture_expected[7]);

    ParsedPam diff;
    snprintf(full, sizeof(full), "%.120s/%.159s", SCRATCH, paths.diff);
    ParsePam(&diff, full);
    uint8_t matched[3];
    uint8_t wrong[3];
    uint8_t unjudged[3];
    PictureDiffColour(PICTURE_MATCHED, matched);
    PictureDiffColour(PICTURE_WRONG, wrong);
    PictureDiffColour(PICTURE_UNJUDGED, unjudged);
    CHECK_TRUE(SUITE, "the diff picture is three colours deep and no more", CHECK_NONE,
               diff.parsed && diff.depth == 3u && strcmp(diff.tupltype, "RGB") == 0,
               "parsed=%d depth %u tupltype \"%s\"", (int)diff.parsed, diff.depth,
               diff.tupltype);
    // The case the whole idea rests on. Pixel two differs from its expectation in a byte and
    // was accepted anyway, which is what every tolerance in this suite looks like from in here.
    // A diff that compared the two images would paint it wrong and accuse a correct machine.
    CHECK_TRUE(SUITE, "the diff carries the judge's verdict, not a comparison of two images",
               CHECK_NONE,
               diff.parsed && memcmp(diff.bytes + 3u, wrong, 3u) == 0 &&
                   memcmp(diff.bytes + 6u, matched, 3u) == 0 &&
                   memcmp(diff.bytes + 9u, unjudged, 3u) == 0 &&
                   memcmp(diff.bytes + 0u, matched, 3u) == 0,
               "the four pixels read %02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x",
               diff.bytes[0], diff.bytes[1], diff.bytes[2], diff.bytes[3], diff.bytes[4],
               diff.bytes[5], diff.bytes[6], diff.bytes[7], diff.bytes[8], diff.bytes[9],
               diff.bytes[10], diff.bytes[11]);
}

static void CheckBitsAndCollisions(void) {
    Picture *picture = BuildFixture(PICTURE_BITS);
    PicturePaths paths;
    PictureFinish(SUITE, "a picture of bits", "none", true, "", &paths);
    Remember(&paths);
    CHECK_TRUE(SUITE, "a picture of bit patterns keeps the bytes beside it", CHECK_NONE,
               picture != NULL && strcmp(paths.raw, "images/support/a-picture-of-bits.bin") == 0,
               "got \"%s\"", paths.raw);

    char full[FULL_PATH_MAX];
    uint8_t raw[FIXTURE_PIXELS * 4u + 1u];
    snprintf(full, sizeof(full), "%.120s/%.159s", SCRATCH, paths.raw);
    const size_t read = ReadWholeFile(full, raw, sizeof(raw));
    CHECK_TRUE(SUITE, "the raw file is every byte the target held and nothing more", CHECK_NONE,
               read == FIXTURE_PIXELS * 4u &&
                   memcmp(raw, fixture_got, FIXTURE_PIXELS * 4u) == 0,
               "read %u bytes of %u", (unsigned)read, (unsigned)(FIXTURE_PIXELS * 4u));

    // Two names that spell the same file name. One must not overwrite the other's evidence.
    picture = BuildFixture(PICTURE_COLOUR);
    PicturePaths first;
    PictureFinish(SUITE, "a name in two ways", "none", true, "", &first);
    Remember(&first);
    picture = BuildFixture(PICTURE_COLOUR);
    PicturePaths second;
    PictureFinish(SUITE, "A NAME, IN TWO WAYS!", "none", true, "", &second);
    Remember(&second);
    CHECK_TRUE(SUITE, "two case names that spell one file name get a file each", CHECK_NONE,
               picture != NULL && first.image[0] != '\0' &&
                   strcmp(first.image, second.image) != 0,
               "\"%s\" against \"%s\"", first.image, second.image);

    // With the default policy back on, a passing case writes nothing at all.
    PictureWriteEveryCase(false);
    picture = BuildFixture(PICTURE_COLOUR);
    PicturePaths quiet;
    PictureFinish(SUITE, "a passing case", "none", true, "", &quiet);
    CHECK_TRUE(SUITE, "a passing case leaves no picture unless one was asked for", CHECK_NONE,
               picture != NULL && quiet.image[0] == '\0' && quiet.expected[0] == '\0' &&
                   quiet.diff[0] == '\0' && quiet.raw[0] == '\0',
               "image \"%s\"", quiet.image);
}

static void CheckReportLine(void) {
    char line[REPORT_LINE_MAX];
    ReportBuildCaseLine(line, sizeof(line), "clear", "clear to opaque red", "exact", true, "",
                        NULL);
    CHECK_TRUE(SUITE, "a case with no picture reports the line it always did", CHECK_NONE,
               strcmp(line, "{\"type\":\"case\",\"suite\":\"clear\",\"name\":\"clear to opaque "
                            "red\",\"tier\":\"exact\",\"result\":\"pass\",\"detail\":\"\"}") == 0,
               "%.480s", line);

    PicturePaths paths;
    memset(&paths, 0, sizeof(paths));
    snprintf(paths.image, sizeof(paths.image), "images/clear/a.pam");
    snprintf(paths.diff, sizeof(paths.diff), "images/clear/a-diff.pam");
    ReportBuildCaseLine(line, sizeof(line), "clear", "a", "exact", false, "one wrong", &paths);
    CHECK_TRUE(SUITE, "a case with pictures names them at the end of its own line", CHECK_NONE,
               strcmp(line,
                      "{\"type\":\"case\",\"suite\":\"clear\",\"name\":\"a\",\"tier\":\"exact\","
                      "\"result\":\"fail\",\"detail\":\"one wrong\","
                      "\"image\":\"images/clear/a.pam\",\"diff\":\"images/clear/a-diff.pam\"}") ==
                   0,
               "%.480s", line);

    CHECK_TRUE(SUITE, "a run under way says its report is still open", CHECK_NONE,
               ReportIsOpen(),
               "the detailed mode asks this before it presents, and it answered no");
}

static void CheckNavigation(void) {
    static const uint8_t FAILED[6] = { 0, 1, 0, 0, 1, 0 };

    UiState state = UiStart(FAILED, 6);
    CHECK_TRUE(SUITE, "the detailed mode opens on the first failure", CHECK_NONE,
               state.index == 1u && state.view == UI_VIEW_ALL, "opened at %u",
               (unsigned)state.index);

    // The screen owes a frame before anything has been pressed, or the first thing a person
    // sees is whatever the last program left in the window.
    UiState fresh = UiStep(UiStart(FAILED, 6), UI_INPUT_NONE, FAILED, 6);
    const bool asked_first = fresh.changed;
    UiFrameDrawn(&fresh);
    fresh = UiStep(fresh, UI_INPUT_NONE, FAILED, 6);
    CHECK_TRUE(SUITE, "a frame is owed until one is drawn, and not again until something moves",
               CHECK_NONE, asked_first && !fresh.changed &&
                               UiStep(fresh, UI_INPUT_NEXT, FAILED, 6).changed,
               "asked first %d, asked again %d", (int)asked_first, (int)fresh.changed);

    state = UiStep(state, UI_INPUT_NEXT_FAILURE, FAILED, 6);
    CHECK_TRUE(SUITE, "jumping forward skips every case that passed", CHECK_NONE,
               state.index == 4u, "landed on %u", (unsigned)state.index);
    state = UiStep(state, UI_INPUT_NEXT_FAILURE, FAILED, 6);
    CHECK_TRUE(SUITE, "jumping past the last failure comes round to the first", CHECK_NONE,
               state.index == 1u, "landed on %u", (unsigned)state.index);
    state = UiStep(state, UI_INPUT_PREVIOUS_FAILURE, FAILED, 6);
    CHECK_TRUE(SUITE, "jumping backwards comes round the other way", CHECK_NONE,
               state.index == 4u, "landed on %u", (unsigned)state.index);

    uint32_t walked = 0;
    UiState walk = UiStep(state, UI_INPUT_NONE, FAILED, 6);
    for (uint32_t step = 0; step < 6u; ++step) {
        walk = UiStep(walk, UI_INPUT_NEXT, FAILED, 6);
        ++walked;
    }
    CHECK_TRUE(SUITE, "stepping through every case comes back where it started", CHECK_NONE,
               walk.index == state.index && walked == 6u, "landed on %u after %u steps",
               (unsigned)walk.index, (unsigned)walked);

    UiState cycled = state;
    for (uint32_t step = 0; step < 4u; ++step) {
        cycled = UiStep(cycled, UI_INPUT_CYCLE_VIEW, FAILED, 6);
    }
    CHECK_TRUE(SUITE, "cycling the view four times is back to all three", CHECK_NONE,
               cycled.view == UI_VIEW_ALL && !cycled.quit, "view %d", (int)cycled.view);

    // A run with nothing kept must still be able to leave, or the only way out of the detailed
    // mode would be to kill the process.
    UiState empty = UiStart(NULL, 0);
    empty = UiStep(empty, UI_INPUT_NEXT, NULL, 0);
    CHECK_TRUE(SUITE, "a run that kept nothing still draws the frame that says so", CHECK_NONE,
               empty.index == 0u && !empty.quit && empty.changed &&
                   UiStep(empty, UI_INPUT_QUIT, NULL, 0).quit,
               "index %u, owed a frame %d", (unsigned)empty.index, (int)empty.changed);

    static const struct {
        const char *text;
        bool detailed;
        bool images;
    } MODES[] = {
        { "detailed\n", true, false },
        { "DETAILED images", true, true },
        { "undetailed", false, false },
        { "detailedly", false, false },
        { "images", false, true },
        { "", false, false },
    };
    uint32_t wrong = 0;
    for (size_t i = 0; i < sizeof(MODES) / sizeof(MODES[0]); ++i) {
        const UiMode mode = UiModeFromText(MODES[i].text);
        if (mode.detailed != MODES[i].detailed || mode.every_case != MODES[i].images) {
            ++wrong;
        }
    }
    CHECK_TRUE(SUITE, "the mode file is read a whole word at a time", CHECK_NONE, wrong == 0,
               "%u of %u readings came out wrong", (unsigned)wrong,
               (unsigned)(sizeof(MODES) / sizeof(MODES[0])));
}

/// How much of the heading band is drawn in the colour a failure is shouted in.
static uint32_t CountRed(const UiFrame *frame) {
    static const uint8_t SHOUT[3] = { 0xF0, 0x50, 0x50 };
    uint32_t seen = 0;
    for (uint32_t y = 8; y < 26u; ++y) {
        for (uint32_t x = 0; x < frame->width; ++x) {
            const uint8_t *const pixel = UiPixel(frame, x, y);
            if (pixel != NULL && memcmp(pixel, SHOUT, 3u) == 0) {
                ++seen;
            }
        }
    }
    return seen;
}

static void CheckLayout(void) {
    uint8_t *const pixels = (uint8_t *)malloc((size_t)FRAME_WIDTH * FRAME_HEIGHT * 4u);
    if (pixels == NULL) {
        ReportCase(SUITE, "a frame has somewhere to be composed", CHECK_NONE, false,
                   "no memory for a frame");
        return;
    }
    const UiFrame frame = { pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH * 4u };

    Picture *const picture = BuildFixture(PICTURE_COLOUR);
    PictureRecord record;
    memset(&record, 0, sizeof(record));
    snprintf(record.suite, sizeof(record.suite), "support");
    snprintf(record.name, sizeof(record.name), "a fixture");
    snprintf(record.tier, sizeof(record.tier), "none");
    snprintf(record.detail, sizeof(record.detail), "one pixel wrong");
    record.width = FIXTURE_WIDTH;
    record.height = FIXTURE_HEIGHT;
    record.format = PICTURE_COLOUR;
    record.got = fixture_got;
    record.expected = fixture_expected;
    record.verdict = fixture_verdict;
    // Closed before anything below reports, or the next line to go out would adopt this
    // picture and write it under that case's name.
    PicturePaths fixture_paths;
    PictureFinish(SUITE, "the fixture", "none", true, "", &fixture_paths);
    Remember(&fixture_paths);
    if (picture == NULL) {
        ReportCase(SUITE, "a frame has a picture to draw", CHECK_NONE, false,
                   "the fixture would not open");
        free(pixels);
        return;
    }

    static const uint8_t ONE_FAILED[1] = { 1 };
    UiState state = UiStart(ONE_FAILED, 1);
    state.view = UI_VIEW_DIFF;
    UiCompose(&frame, &record, state, 1, ONE_FAILED);

    // The single panel is centred, so where its first pixel lands is arithmetic a case can do
    // as well as the layout can, and a panel drawn anywhere else fails here.
    const uint32_t scale =
        (FRAME_HEIGHT - UI_PANEL_TOP - 128u) / FIXTURE_HEIGHT;
    const uint32_t left = (FRAME_WIDTH - FIXTURE_WIDTH * scale) / 2u;
    uint8_t wrong_colour[3];
    uint8_t matched_colour[3];
    PictureDiffColour(PICTURE_WRONG, wrong_colour);
    PictureDiffColour(PICTURE_MATCHED, matched_colour);
    const uint8_t *const at_wrong =
        UiPixel(&frame, left + scale + scale / 2u, UI_PANEL_TOP + scale / 2u);
    const uint8_t *const at_right = UiPixel(&frame, left + scale / 2u, UI_PANEL_TOP + scale / 2u);
    CHECK_TRUE(SUITE, "the diff panel puts the judge's colours where the pixels were",
               CHECK_NONE,
               at_wrong != NULL && at_right != NULL &&
                   memcmp(at_wrong, wrong_colour, 3u) == 0 &&
                   memcmp(at_right, matched_colour, 3u) == 0,
               "wrong pixel %02x%02x%02x, right pixel %02x%02x%02x",
               at_wrong != NULL ? at_wrong[0] : 0, at_wrong != NULL ? at_wrong[1] : 0,
               at_wrong != NULL ? at_wrong[2] : 0, at_right != NULL ? at_right[0] : 0,
               at_right != NULL ? at_right[1] : 0, at_right != NULL ? at_right[2] : 0);

    // The same target one scale factor up: every pixel of a block must be the one pixel it
    // came from, which is what nearest neighbour means and what a blurred upscale would fail.
    uint32_t blurred = 0;
    for (uint32_t row = 0; row < scale; ++row) {
        for (uint32_t column = 0; column < scale; ++column) {
            const uint8_t *const pixel = UiPixel(&frame, left + column, UI_PANEL_TOP + row);
            if (pixel == NULL || memcmp(pixel, matched_colour, 3u) != 0) {
                ++blurred;
            }
        }
    }
    CHECK_TRUE(SUITE, "a scaled up pixel is one colour and not an average", CHECK_NONE,
               blurred == 0, "%u of %u pixels in the first block were not the source colour",
               (unsigned)blurred, (unsigned)(scale * scale));

    // The verdict is on the heading in its own colour, and only when the case failed. Counted
    // across the band rather than read at one pixel, so moving the word along the line is a
    // layout change and not a broken case, while dropping it altogether still fails.
    state.view = UI_VIEW_ALL;
    record.passed = false;
    UiCompose(&frame, &record, state, 1, ONE_FAILED);
    const uint32_t shouting = CountRed(&frame);
    record.passed = true;
    UiCompose(&frame, &record, state, 1, ONE_FAILED);
    const uint32_t quiet = CountRed(&frame);
    CHECK_TRUE(SUITE, "a failed case shouts its verdict on the heading and a passing one does not",
               CHECK_NONE, shouting > 100u && quiet == 0u,
               "%u pixels of it when it failed, %u when it passed", (unsigned)shouting,
               (unsigned)quiet);
    record.passed = false;

    // The key under the panels, which is the only thing on the screen that says what the red
    // in the third one means.
    const uint32_t panel_scale = (FRAME_WIDTH - 4u * 32u) / 3u / FIXTURE_WIDTH;
    const uint32_t key_y = UI_PANEL_TOP + FIXTURE_HEIGHT * panel_scale + 12u;
    const uint8_t *const swatch = UiPixel(&frame, 32u + 8u, key_y + 8u);
    CHECK_TRUE(SUITE, "the three colours of a verdict are named under the pictures", CHECK_NONE,
               swatch != NULL && memcmp(swatch, matched_colour, 3u) == 0,
               "the first swatch reads %02x%02x%02x, wanted %02x%02x%02x",
               swatch != NULL ? swatch[0] : 0, swatch != NULL ? swatch[1] : 0,
               swatch != NULL ? swatch[2] : 0, matched_colour[0], matched_colour[1],
               matched_colour[2]);

    // Three panels rather than one, and read at the pixel where what was drawn and what was
    // expected differ, so a layout that drew one picture twice fails here.
    record.format = PICTURE_BITS;
    UiCompose(&frame, &record, state, 1, ONE_FAILED);
    const uint32_t wide = panel_scale;
    const uint32_t row = UI_PANEL_TOP + wide / 2u;
    const uint8_t *const drawn = UiPixel(&frame, 32u + wide + wide / 2u, row);
    const uint8_t *const wanted_panel =
        UiPixel(&frame, 32u + FIXTURE_WIDTH * wide + 32u + wide + wide / 2u, row);
    CHECK_TRUE(SUITE, "the two panels show two pictures and not one twice", CHECK_NONE,
               drawn != NULL && wanted_panel != NULL && drawn[2] == fixture_got[4] &&
                   wanted_panel[2] == fixture_expected[4],
               "drawn %02x against %02x, expected %02x against %02x",
               drawn != NULL ? drawn[2] : 0, fixture_got[4],
               wanted_panel != NULL ? wanted_panel[2] : 0, fixture_expected[4]);

    free(pixels);
}

void RunSupport(void) {
    const char *const previous_root = PictureRoot();
    char saved[96];
    snprintf(saved, sizeof(saved), "%s", previous_root);
    const bool saved_policy = PictureWritesEveryCase();
    const bool saved_retention = PictureIsRetaining();

    // Its own corner, and every picture written whatever the verdict, because the cases below
    // are about the writer and a passing case would otherwise write nothing to look at. Nothing
    // retained either: these fixtures came from this file and not from the graphics unit, and
    // the detailed mode is for looking at what the hardware drew.
    PictureSetRoot(SCRATCH);
    PictureWriteEveryCase(true);
    PictureRetain(false);

    CheckNaming();
    CheckWriting();
    CheckBitsAndCollisions();
    CheckReportLine();
    CheckNavigation();
    CheckLayout();

    CHECK_TRUE(SUITE, "this suite takes its own files away again", CHECK_NONE, SweepUp(),
               "something this suite wrote is still on the memory card");

    PictureSetRoot(saved);
    PictureWriteEveryCase(saved_policy);
    PictureRetain(saved_retention);
}
