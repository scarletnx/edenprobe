// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../check.h"
#include "../picture.h"
#include "../report.h"
#include "tests.h"

// The picture a failing case leaves beside the report. It decides nothing, and no case here
// asks the hardware for anything, so every line is the none tier.
//
// It runs in both builds because both carry the code it checks, and because a writer that only
// ran inside an emulator would be a writer nobody could check.

static const char *const SUITE = "support";

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

static uint8_t fixture_got[FIXTURE_PIXELS * 4u];
static uint8_t fixture_expected[FIXTURE_PIXELS * 4u];

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
    CHECK_TRUE(SUITE, "a picture is written where the line says it is", CHECK_NONE,
               strcmp(paths.image, "images/support/a-colour-picture.pam") == 0 &&
                   strcmp(paths.expected, "images/support/a-colour-picture-expected.pam") == 0 &&
                   strcmp(paths.diff, "images/support/a-colour-picture-diff.pam") == 0,
               "image \"%s\", expected \"%s\", diff \"%s\"", paths.image, paths.expected,
               paths.diff);
    CHECK_TRUE(SUITE, "a colour picture keeps no raw file beside it", CHECK_NONE,
               paths.raw[0] == '\0', "got \"%s\"", paths.raw);

    char full[256];
    ParsedPam parsed;
    snprintf(full, sizeof(full), "%s/%s", SCRATCH, paths.image);
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
    snprintf(full, sizeof(full), "%s/%s", SCRATCH, paths.expected);
    ParsePam(&wanted, full);
    CHECK_TRUE(SUITE, "the expected picture holds what the judge expected", CHECK_NONE,
               wanted.parsed &&
                   memcmp(wanted.bytes, fixture_expected, sizeof(fixture_expected)) == 0,
               "parsed=%d, pixel one is %02x%02x%02x%02x, wanted %02x%02x%02x%02x",
               (int)wanted.parsed, wanted.bytes[4], wanted.bytes[5], wanted.bytes[6],
               wanted.bytes[7], fixture_expected[4], fixture_expected[5], fixture_expected[6],
               fixture_expected[7]);

    ParsedPam diff;
    snprintf(full, sizeof(full), "%s/%s", SCRATCH, paths.diff);
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
    CHECK_TRUE(SUITE, "a picture of bit patterns keeps the bytes beside it", CHECK_NONE,
               picture != NULL && strcmp(paths.raw, "images/support/a-picture-of-bits.bin") == 0,
               "got \"%s\"", paths.raw);

    char full[256];
    uint8_t raw[FIXTURE_PIXELS * 4u + 1u];
    snprintf(full, sizeof(full), "%s/%s", SCRATCH, paths.raw);
    const size_t read = ReadWholeFile(full, raw, sizeof(raw));
    CHECK_TRUE(SUITE, "the raw file is every byte the target held and nothing more", CHECK_NONE,
               read == FIXTURE_PIXELS * 4u &&
                   memcmp(raw, fixture_got, FIXTURE_PIXELS * 4u) == 0,
               "read %u bytes of %u", (unsigned)read, (unsigned)(FIXTURE_PIXELS * 4u));

    // Two names that spell the same file name. One must not overwrite the other's evidence.
    picture = BuildFixture(PICTURE_COLOUR);
    PicturePaths first;
    PictureFinish(SUITE, "a name in two ways", "none", true, "", &first);
    picture = BuildFixture(PICTURE_COLOUR);
    PicturePaths second;
    PictureFinish(SUITE, "A NAME, IN TWO WAYS!", "none", true, "", &second);
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

    PictureSetRoot(saved);
    PictureWriteEveryCase(saved_policy);
    PictureRetain(saved_retention);
}
