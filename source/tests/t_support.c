// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// mkdtemp, for the host build's scratch directory below.
#ifndef __SWITCH__
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../check.h"
#include "../mode.h"
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

/// Its own corner to write into, and an absolute one: a path relative to the working directory
/// makes the binary run from anywhere else report a pile of failures that all blame the picture
/// writer for the same missing directory.
///
/// On the host, a fresh one per process: two host runs at once on one machine shared the fixed
/// name, and each swept away the other's pictures mid-check. The sweep at the end removes it.
static const char *Scratch(void) {
    static char path[FULL_PATH_MAX];
    if (path[0] == '\0') {
#ifdef __SWITCH__
        snprintf(path, sizeof(path), "sdmc:/edenprobe/support-check");
#else
        const char *const base = getenv("TMPDIR");
        snprintf(path, sizeof(path), "%.200s/edenprobe-support-check-XXXXXX",
                 base != NULL && base[0] != '\0' ? base : "/tmp");
        if (mkdtemp(path) == NULL) {
            // Unique enough to keep two runs apart, and the cases below say so if it is not
            // somewhere they can write.
            snprintf(path, sizeof(path), "%.200s/edenprobe-support-check-%ld",
                     base != NULL && base[0] != '\0' ? base : "/tmp", (long)getpid());
        }
#endif
    }
    return path;
}

// The frame the layout cases compose into, at the size the console actually gives. Anything
// smaller and the key runs off the side, which is how the key's own case first passed while
// three of its four words were off the frame.
#define FRAME_WIDTH 1280u
#define FRAME_HEIGHT 720u

// Every file this suite writes, so that it can take them away again. A passing suite that left
// a directory of pictures called "expected" beside the report would read, to anyone who found
// it, as exactly the stored reference this program does not have.
#define WRITTEN_MAX 8
static PicturePaths written[WRITTEN_MAX];
static size_t written_count;

static uint8_t fixture_got[FIXTURE_PIXELS * 4u];
static uint8_t fixture_expected[FIXTURE_PIXELS * 4u];
static uint8_t fixture_verdict[FIXTURE_PIXELS];

static bool Discard(const char *relative);

/// Builds the path this suite reads a file back through, and says whether it fitted.
///
/// One place, because the writer and the reader have to agree on the whole name: a reader that
/// cut a path short where the writer did not would go looking in the wrong place and report it
/// as a defect in the writer. Refused rather than truncated, for the same reason the root is.
static bool FullPath(char *out, size_t out_size, const char *relative) {
    const int wrote = snprintf(out, out_size, "%s/%s", Scratch(), relative);
    return wrote >= 0 && (size_t)wrote < out_size;
}

/// Keeps one set of paths for the sweep at the end.
static void Remember(const PicturePaths *paths) {
    if (written_count < WRITTEN_MAX) {
        written[written_count++] = *paths;
    }
}

/// Removes one file and says whether it is gone. Nothing to remove counts as gone.
static bool Discard(const char *relative) {
    if (relative[0] == '\0') {
        return true;
    }
    char full[FULL_PATH_MAX];
    if (!FullPath(full, sizeof(full), relative)) {
        return false;
    }
    return remove(full) == 0;
}

/// Whether anything this suite wrote is still there. The directories go too, youngest first,
/// because an empty one left behind is still a directory nobody can explain.
static bool SweepUp(void) {
    bool gone = true;
    for (size_t i = 0; i < written_count; ++i) {
        // Every one of the four, and no short circuit: a sweep that stopped at the first file
        // it could not remove would leave the rest behind and still be able to say so.
        gone = Discard(written[i].image) && gone;
        gone = Discard(written[i].expected) && gone;
        gone = Discard(written[i].diff) && gone;
        gone = Discard(written[i].raw) && gone;
    }

    char directory[FULL_PATH_MAX];
    if (FullPath(directory, sizeof(directory), "images/support")) {
        (void)rmdir(directory);
    }
    if (FullPath(directory, sizeof(directory), "images")) {
        (void)rmdir(directory);
    }
    (void)rmdir(Scratch());
    return gone;
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

/// The naming decision on its own, which is where a case's evidence is either kept or lost.
static void CheckNames(void) {
    char first[96];
    char second[96];

    // Two names that agree for longer than a stem can hold. The stem gives characters up to
    // make room for its number rather than going back as the name the first one took.
    char long_a[200];
    char long_b[200];
    memset(long_a, 'x', 100);
    long_a[100] = '\0';
    memcpy(long_b, long_a, sizeof(long_a));
    snprintf(long_a + 100, sizeof(long_a) - 100u, " alpha");
    snprintf(long_b + 100, sizeof(long_b) - 100u, " beta");
    const bool got_first = PictureReserveName(first, sizeof(first), "names", long_a);
    const bool got_second = PictureReserveName(second, sizeof(second), "names", long_b);
    CHECK_TRUE(SUITE, "two names that agree for longer than a stem still get one each",
               CHECK_NONE,
               got_first && got_second && strcmp(first, second) != 0 &&
                   strlen(second) < sizeof(second),
               "\"%s\" against \"%s\"", first, second);

    // The same in the smallest buffer there is. What must not happen is the second call
    // answering with the name the first one took, whatever it has to give up to avoid it.
    char tiny_first[9];
    char tiny_second[9];
    const bool tiny_a = PictureReserveName(tiny_first, sizeof(tiny_first), "names", "abcdefghij");
    const bool tiny_b =
        PictureReserveName(tiny_second, sizeof(tiny_second), "names", "abcdefghij");
    CHECK_TRUE(SUITE, "a name with no room for its number gives characters up instead",
               CHECK_NONE,
               tiny_a && tiny_b && strcmp(tiny_first, tiny_second) != 0 &&
                   strlen(tiny_second) < sizeof(tiny_second),
               "\"%s\" against \"%s\"", tiny_first, tiny_second);

    // The pair that used to collide sideways. "foo expected" spells the file "foo" writes its
    // expectation into, so the second case's drawn picture landed on the first's expected one
    // while the first's line went on naming it.
    char plain[96];
    char derived[96];
    const bool got_plain = PictureReserveName(plain, sizeof(plain), "pair", "foo");
    const bool got_derived = PictureReserveName(derived, sizeof(derived), "pair", "foo expected");
    char plain_expected[128];
    snprintf(plain_expected, sizeof(plain_expected), "%s-expected", plain);
    CHECK_TRUE(SUITE, "a case named after another's expected picture does not land on it",
               CHECK_NONE,
               got_plain && got_derived && strcmp(derived, plain_expected) != 0 &&
                   strcmp(derived, plain) != 0,
               "\"%s\" writes \"%s\", and \"%s\" took \"%s\"", "foo", plain_expected,
               "foo expected", derived);

    // And the same the other way round, since either can be reported first.
    char first_derived[96];
    char then_plain[96];
    PictureReserveName(first_derived, sizeof(first_derived), "pair2", "bar diff");
    PictureReserveName(then_plain, sizeof(then_plain), "pair2", "bar");
    char then_diff[128];
    snprintf(then_diff, sizeof(then_diff), "%s-diff", then_plain);
    CHECK_TRUE(SUITE, "and not the other way round either", CHECK_NONE,
               strcmp(then_diff, first_derived) != 0,
               "\"bar\" writes \"%s\", and \"bar diff\" had taken \"%s\"", then_diff,
               first_derived);

    // A hundred cases spelling one name. Every one of them gets its own or is told no; what
    // must never happen is the hundredth being given the first one's.
    uint32_t reserved = 0;
    uint32_t repeats = 0;
    char taken[100][32];
    for (uint32_t i = 0; i < 100u; ++i) {
        char candidate[32];
        if (!PictureReserveName(candidate, sizeof(candidate), "crowd", "one name")) {
            continue;
        }
        for (uint32_t seen = 0; seen < reserved; ++seen) {
            if (strcmp(taken[seen], candidate) == 0) {
                ++repeats;
            }
        }
        snprintf(taken[reserved], sizeof(taken[0]), "%s", candidate);
        ++reserved;
    }
    // No repeat is the safety claim; fifty of them is the capacity one, so a ceiling lowered
    // back to a single digit fails here rather than quietly turning cases away.
    CHECK_TRUE(SUITE, "a hundred cases spelling one name never share a file", CHECK_NONE,
               repeats == 0u && reserved >= 50u,
               "%u of %u names came back already taken", (unsigned)repeats,
               (unsigned)reserved);
}

/// The longest root this program will take, exercised end to end.
///
/// The window between a root the writer accepts and a path the reader could hold was real: a
/// scratch directory long enough made eight cases fail and leave their files behind, every one
/// of them blaming the picture writer for a path the reader had cut short.
static void CheckTheLongestRoot(void) {
    char keep[PICTURE_PATH_MAX];
    snprintf(keep, sizeof(keep), "%s", PictureRoot());

    // Padded so the root is as long as one is allowed to be, wherever the scratch happens to
    // live. On a machine whose scratch is already that long there is nothing to pad and the
    // scratch itself is the longest root there is.
    char root[PICTURE_PATH_MAX];
    const size_t base = strlen(Scratch());
    const size_t want = sizeof(root) - 1u;
    if (base + 2u <= want) {
        memset(root, 'z', want);
        root[want] = '\0';
        memcpy(root, Scratch(), base);
        root[base] = '-';
    } else {
        snprintf(root, sizeof(root), "%s", Scratch());
    }

    bool parsed = false;
    PicturePaths paths;
    memset(&paths, 0, sizeof(paths));
    if (PictureSetRoot(root)) {
        Picture *const picture = BuildFixture(PICTURE_COLOUR);
        PictureFinish(SUITE, "a case at the longest root", "none", true, "", &paths);
        char full[FULL_PATH_MAX];
        ParsedPam back;
        const int wrote = snprintf(full, sizeof(full), "%s/%s", root, paths.image);
        if (picture != NULL && paths.image[0] != '\0' && wrote > 0 &&
            (size_t)wrote < sizeof(full)) {
            ParsePam(&back, full);
            parsed = back.parsed && back.width == FIXTURE_WIDTH;
        }
        // Taken away here rather than by the sweep, which only knows the ordinary scratch.
        for (size_t which = 0; which < 3u; ++which) {
            const char *const relative =
                which == 0u ? paths.image : (which == 1u ? paths.expected : paths.diff);
            if (relative[0] == '\0') {
                continue;
            }
            if (snprintf(full, sizeof(full), "%s/%s", root, relative) > 0) {
                (void)remove(full);
            }
        }
        snprintf(full, sizeof(full), "%s/images/support", root);
        (void)rmdir(full);
        snprintf(full, sizeof(full), "%s/images", root);
        (void)rmdir(full);
        if (strcmp(root, Scratch()) != 0) {
            (void)rmdir(root);
        }
    }

    CHECK_TRUE(SUITE, "a root as long as one can be still reads back what it wrote", CHECK_NONE,
               parsed, "root of %u characters, image \"%s\"", (unsigned)strlen(root),
               paths.image);
    PictureSetRoot(keep);
}

/// A root too long to hold whole. Cutting one short makes a directory nobody named, and a root
/// that could not be written to can truncate into one that can, which would let the case that
/// reports "nowhere to write" pass while every case after it failed for that reason.
static void CheckRoot(void) {
    const char *const saved_root = PictureRoot();
    char keep[PICTURE_PATH_MAX];
    snprintf(keep, sizeof(keep), "%s", saved_root);

    char too_long[PICTURE_PATH_MAX + 64];
    memset(too_long, 'r', sizeof(too_long) - 1u);
    too_long[sizeof(too_long) - 1u] = '\0';
    const bool refused = !PictureSetRoot(too_long);
    const bool empty = PictureRoot()[0] == '\0';

    Picture *const picture = BuildFixture(PICTURE_COLOUR);
    PicturePaths paths;
    PictureFinish(SUITE, "a case under a root that will not fit", "none", false, "", &paths);
    CHECK_TRUE(SUITE, "a root too long to hold whole is refused and nothing is written",
               CHECK_NONE,
               refused && empty && picture != NULL && paths.image[0] == '\0' &&
                   paths.diff[0] == '\0',
               "refused %d, root \"%s\", image \"%s\"", (int)refused, PictureRoot(),
               paths.image);

    CHECK_TRUE(SUITE, "a root that fits is taken", CHECK_NONE, PictureSetRoot(keep),
               "\"%s\" was refused", keep);
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

/// Returns false when this machine has nowhere to write, which every case below depends on.
static bool CheckWriting(void) {
    Picture *const picture = BuildFixture(PICTURE_COLOUR);
    if (picture == NULL) {
        ReportCase(SUITE, "a picture opens for a small target", CHECK_NONE, false,
                   "PictureBegin refused a target it should have taken");
        return false;
    }

    PicturePaths paths;
    PictureFinish(SUITE, "a colour picture", "none", true, "", &paths);
    Remember(&paths);

    // This fixture doubles as the probe. Named once, here, so that a corner of the card this
    // machine cannot write to, or one whose paths this suite cannot hold whole, is reported as
    // that rather than blamed on the writer eight more times.
    char probe[FULL_PATH_MAX];
    const bool writable = paths.image[0] != '\0';
    const bool readable = writable && FullPath(probe, sizeof(probe), paths.image);
    CHECK_TRUE(SUITE, "there is a corner of the memory card to write in and read back",
               CHECK_NONE, readable, "%s under %s",
               writable ? "the path is longer than this suite can hold" : "nothing was written",
               Scratch());
    if (!readable) {
        return false;
    }

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
    ParsePam(&parsed, FullPath(full, sizeof(full), paths.image) ? full : "");
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
    ParsePam(&wanted, FullPath(full, sizeof(full), paths.expected) ? full : "");
    CHECK_TRUE(SUITE, "the expected picture holds what the judge expected", CHECK_NONE,
               wanted.parsed &&
                   memcmp(wanted.bytes, fixture_expected, sizeof(fixture_expected)) == 0,
               "parsed=%d, pixel one is %02x%02x%02x%02x, wanted %02x%02x%02x%02x",
               (int)wanted.parsed, wanted.bytes[4], wanted.bytes[5], wanted.bytes[6],
               wanted.bytes[7], fixture_expected[4], fixture_expected[5], fixture_expected[6],
               fixture_expected[7]);

    ParsedPam diff;
    ParsePam(&diff, FullPath(full, sizeof(full), paths.diff) ? full : "");
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
    return true;
}

/// A picture with a hole in it: one pixel no judge ever looked at. It has to be visible, or a
/// compare loop that skipped a pixel would hand back a diff that quietly reads as accepted.
static void CheckAPixelNobodyLookedAt(void) {
    for (uint32_t i = 0; i < FIXTURE_PIXELS; ++i) {
        for (uint32_t channel = 0; channel < 4u; ++channel) {
            fixture_got[i * 4u + channel] = (uint8_t)(i * 4u + channel);
        }
    }
    Picture *const picture =
        PictureBegin(FIXTURE_WIDTH, FIXTURE_HEIGHT, PICTURE_COLOUR, fixture_got);
    for (uint32_t y = 0; y < FIXTURE_HEIGHT; ++y) {
        for (uint32_t x = 0; x < FIXTURE_WIDTH; ++x) {
            if (x == 4u && y == 1u) {
                continue;
            }
            PictureSet(picture, x, y, NULL, PICTURE_MATCHED);
        }
    }
    PicturePaths paths;
    PictureFinish(SUITE, "a picture with a hole", "none", true, "", &paths);
    Remember(&paths);

    char full[FULL_PATH_MAX];
    ParsedPam diff;
    ParsePam(&diff, FullPath(full, sizeof(full), paths.diff) ? full : "");
    uint8_t unset[3];
    uint8_t matched[3];
    PictureDiffColour((uint8_t)PICTURE_UNSET, unset);
    PictureDiffColour((uint8_t)PICTURE_MATCHED, matched);
    const size_t hole = ((size_t)1 * FIXTURE_WIDTH + 4u) * 3u;
    CHECK_TRUE(SUITE, "a pixel no judge looked at is not drawn as one it accepted", CHECK_NONE,
               diff.parsed && memcmp(diff.bytes + hole, unset, 3u) == 0 &&
                   memcmp(diff.bytes + hole - 3u, matched, 3u) == 0,
               "the hole reads %02x%02x%02x and its neighbour %02x%02x%02x",
               diff.bytes[hole], diff.bytes[hole + 1u], diff.bytes[hole + 2u],
               diff.bytes[hole - 3u], diff.bytes[hole - 2u], diff.bytes[hole - 1u]);
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
    const size_t read = FullPath(full, sizeof(full), paths.raw)
                            ? ReadWholeFile(full, raw, sizeof(raw))
                            : 0u;
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

    // A case that could not ask is a result of its own and a count of its own: a skip read as
    // a pass would make a run where nothing was asked look green. Its reason is one word of a
    // few, beside the sentence; a crash is counted apart from every other skip.
    char skip[REPORT_LINE_MAX];
    ReportBuildResultLine(skip, sizeof(skip), "acc", "the token", "none", "skip",
                          ReportReasonWord(REPORT_REASON_NOT_CONFIGURED),
                          "needs an online session", NULL);
    char summary[160];
    ReportBuildSummaryLine(summary, sizeof(summary), 3, 1, 2, 4);
    CHECK_TRUE(SUITE, "a skip is its own result and the summary counts it apart", CHECK_NONE,
               strcmp(skip, "{\"type\":\"case\",\"suite\":\"acc\",\"name\":\"the token\","
                            "\"tier\":\"none\",\"result\":\"skip\",\"reason\":\"not "
                            "configured\",\"detail\":\"needs an online session\"}") == 0 &&
                   strcmp(summary, "{\"type\":\"summary\",\"passed\":3,\"failed\":1,"
                                   "\"skipped\":2,\"crashed\":4}") == 0,
               "skip %.200s; summary %.100s", skip, summary);

    CHECK_TRUE(SUITE, "a run under way says its report is still open", CHECK_NONE,
               ReportIsOpen(),
               "the detailed mode asks this before it presents, and it answered no");

    // Every buffer size from too small for anything up to comfortable. What a reader cannot
    // survive is a line that stops inside a string: the brace never arrives, the quote count is
    // odd, and the parse error lands on the file rather than on the line.
    PicturePaths full;
    memset(&full, 0, sizeof(full));
    snprintf(full.image, sizeof(full.image), "images/clear/a-rather-long-case-name-here.pam");
    snprintf(full.expected, sizeof(full.expected),
             "images/clear/a-rather-long-case-name-here-expected.pam");
    snprintf(full.diff, sizeof(full.diff), "images/clear/a-rather-long-case-name-here-diff.pam");
    snprintf(full.raw, sizeof(full.raw), "images/clear/a-rather-long-case-name-here.bin");

    uint32_t unreadable = 0;
    uint32_t first_bad_size = 0;
    for (size_t size = 2; size <= 400u; ++size) {
        char small[401];
        memset(small, 'x', sizeof(small));
        ReportBuildCaseLine(small, size, "clear", "a case with a longish name", "exact", false,
                            "a detail with \"a quote\" in it", &full);
        const size_t length = strlen(small);
        uint32_t quotes = 0;
        for (size_t i = 0; i < length; ++i) {
            if (small[i] == '"' && (i == 0 || small[i - 1] != '\\')) {
                ++quotes;
            }
        }
        const bool shaped = length == 0u || (small[0] == '{' && small[length - 1u] == '}');
        if (!shaped || (quotes % 2u) != 0u || length >= size) {
            if (unreadable == 0u) {
                first_bad_size = (uint32_t)size;
            }
            ++unreadable;
        }
    }
    CHECK_TRUE(SUITE, "a line too big for its buffer comes back whole or not at all", CHECK_NONE,
               unreadable == 0u, "%u of 399 sizes came back unreadable, the first at %u bytes",
               (unsigned)unreadable, (unsigned)first_bad_size);

    // And the buffer a case actually gets is big enough for the longest line a case can make,
    // which is the margin the line above only has to fall back on when something is wrong.
    char longest[REPORT_LINE_MAX];
    char big_name[200];
    char big_detail[700];
    memset(big_name, 'n', sizeof(big_name) - 1u);
    big_name[sizeof(big_name) - 1u] = '\0';
    memset(big_detail, 'd', sizeof(big_detail) - 1u);
    big_detail[sizeof(big_detail) - 1u] = '\0';
    PicturePaths longest_paths;
    memset(&longest_paths, 0, sizeof(longest_paths));
    memset(longest_paths.image, 'i', PICTURE_PATH_MAX - 1u);
    memcpy(longest_paths.expected, longest_paths.image, PICTURE_PATH_MAX);
    memcpy(longest_paths.diff, longest_paths.image, PICTURE_PATH_MAX);
    memcpy(longest_paths.raw, longest_paths.image, PICTURE_PATH_MAX);
    const size_t written = ReportBuildCaseLine(longest, sizeof(longest), big_name, big_name,
                                               "position", false, big_detail, &longest_paths);
    CHECK_TRUE(SUITE, "the longest line a case can make still fits the buffer it is given",
               CHECK_NONE,
               written > 1u && written < sizeof(longest) && longest[written - 1u] == '}',
               "%u bytes of %u, ending in '%c'", (unsigned)written, (unsigned)sizeof(longest),
               written > 0u ? longest[written - 1u] : '?');
}

// The navigation between the screens, and the screens themselves, are checked in t_screens.c;
// what is left here is what happens before and after them: the launch and the mode file.
static void CheckLaunchAndMode(void) {
    // The pad is waited for until it answers, and no longer.
    CHECK_TRUE(SUITE, "the pad is waited for until it answers and then not at all", CHECK_NONE,
               UiWaitsForPad(0, false) && !UiWaitsForPad(0, true) &&
                   !UiWaitsForPad(UI_PAD_WAIT_FRAMES, false),
               "waits at nought %d, with a pad %d, at the ceiling %d",
               (int)UiWaitsForPad(0, false), (int)UiWaitsForPad(0, true),
               (int)UiWaitsForPad(UI_PAD_WAIT_FRAMES, false));

    // Once the report is closed, a detailed screen nobody presses anything on is not waited on
    // for ever: ten seconds, counted down in whole seconds, and a press ends the count for good.
    const uint64_t second = 1000000000ull;
    const uint64_t limit = UI_UNATTENDED_NANOSECONDS;
    const bool timer_right =
        limit == 10u * second && !UiLeavesUnattended(0, false) &&
        !UiLeavesUnattended(limit - 1u, false) && UiLeavesUnattended(limit, false) &&
        !UiLeavesUnattended(limit * 100u, true) && UiUnattendedSecondsLeft(0, false) == 10u &&
        UiUnattendedSecondsLeft(1u, false) == 10u &&
        UiUnattendedSecondsLeft(9u * second, false) == 1u &&
        UiUnattendedSecondsLeft(9u * second + 1u, false) == 1u &&
        UiUnattendedSecondsLeft(limit, false) == 0u && UiUnattendedSecondsLeft(0, true) == 0u;
    CHECK_TRUE(SUITE, "the detailed screen leaves by itself when nothing is pressed", CHECK_NONE,
               timer_right,
               "limit %llu ns; leaves at the limit %d, just before %d, pressed long after %d; "
               "seconds left at nought %u, at nine %u",
               (unsigned long long)limit, (int)UiLeavesUnattended(limit, false),
               (int)UiLeavesUnattended(limit - 1u, false),
               (int)UiLeavesUnattended(limit * 100u, true),
               (unsigned)UiUnattendedSecondsLeft(0, false),
               (unsigned)UiUnattendedSecondsLeft(9u * second, false));

    // Holding the button is the other way in, and it can only ever add the detailed mode. It
    // matters only on a card that says quick, which is the only time the pad is asked.
    const RunMode held_quick = ModeAtLaunch("quick", true);
    const RunMode held_none = ModeAtLaunch(NULL, true);
    CHECK_TRUE(SUITE, "holding the button asks for the detailed mode and never cancels it",
               CHECK_NONE,
               held_quick.detailed && held_quick.detailed_by_button &&
                   !ModeAtLaunch("quick", false).detailed && held_none.detailed &&
                   !held_none.detailed_by_button &&
                   ModeAtLaunch("quick detailed", false).detailed &&
                   ModeAtLaunch("quick images", true).every_case &&
                   ModeAsksPad(&(RunMode){ .detailed = false }) &&
                   !ModeAsksPad(&held_none),
               "the readings with the button held did not come out as they should");

    // The stress word turns the heavy variants on, and only it; the mode line says so last.
    {
        const RunMode plain = ModeFromText("quick");
        const RunMode heavy = ModeFromText("quick STRESS graphics");
        char said[REPORT_NOTE_MAX + 1] = "";
        ModeDescribe(said, sizeof(said), &heavy, MODE_FAMILY_GRAPHICS);
        const size_t length = strlen(said);
        const char *const tail = "; stress cases";
        CHECK_TRUE(SUITE, "the word stress, and nothing else, asks for the stress cases",
                   CHECK_NONE,
                   !plain.stress && heavy.stress && heavy.unknown_count == 0u &&
                       length > strlen(tail) && strcmp(said + length - strlen(tail), tail) == 0,
                   "plain %d, heavy %d, unknown %u, said \"%.200s\"", (int)plain.stress,
                   (int)heavy.stress, (unsigned)heavy.unknown_count, said);
    }

    // The fatal word turns on the may-crash table's cases, and only it; with the cpu family
    // alone the line names no other family. Chosen on the start screen, the line says so.
    {
        const RunMode plain = ModeFromText("quick cpu");
        const RunMode fatal = ModeFromText("quick cpu FATAL");
        char said[REPORT_NOTE_MAX + 1] = "";
        ModeDescribe(said, sizeof(said), &fatal, MODE_FAMILIES_PRESENT);
        RunMode screen = plain;
        screen.fatal = true;
        screen.fatal_by_screen = true;
        screen.retry = true;
        screen.retry_by_screen = true;
        char chosen[REPORT_NOTE_MAX + 1] = "";
        ModeDescribe(chosen, sizeof(chosen), &screen, MODE_FAMILIES_PRESENT);
        CHECK_TRUE(SUITE, "the word fatal, and nothing else, asks for the cases that may crash",
                   CHECK_NONE,
                   !plain.fatal && fatal.fatal && fatal.unknown_count == 0u &&
                       !ModeFromText("quick stress").fatal && !ModeFromText("fatal").stress &&
                       strcmp(said, "mode: quick, from the mode file; pictures of failures "
                                    "only; families cpu; the cases that may crash") == 0 &&
                       strcmp(chosen, "mode: quick, from the mode file; pictures of failures "
                                      "only; families cpu; the cases that may crash, chosen on "
                                      "the start screen; the crash list's cases again, chosen "
                                      "on the start screen") == 0,
                   "plain %d, fatal %d, unknown %u, said \"%.200s\"; chosen \"%.240s\"",
                   (int)plain.fatal, (int)fatal.fatal, (unsigned)fatal.unknown_count, said,
                   chosen);
    }

    // Every word, alone and together, any case, in a file that is missing, empty, or has words
    // nobody knows. What the run does is the first two columns; the rest is what it reports.
    static const struct {
        const char *text; // NULL is no file at all
        bool detailed;
        bool images;
        uint32_t families;
        uint32_t unknown;
        uint32_t run; // the families that run, in this build
    } MODES[] = {
        { NULL, true, false, 0, 0, MODE_FAMILIES_PRESENT },
        { "", true, false, 0, 0, MODE_FAMILIES_PRESENT },
        { "\n\n", true, false, 0, 0, MODE_FAMILIES_PRESENT },
        { "quick", false, false, 0, 0, MODE_FAMILIES_PRESENT },
        { "QUICK\n", false, false, 0, 0, MODE_FAMILIES_PRESENT },
        { "quick detailed", true, false, 0, 0, MODE_FAMILIES_PRESENT },
        { "quick images", false, true, 0, 0, MODE_FAMILIES_PRESENT },
        { "images", true, true, 0, 0, MODE_FAMILIES_PRESENT },
        { "detailed\n", true, false, 0, 0, MODE_FAMILIES_PRESENT },
        { "DETAILED images", true, true, 0, 0, MODE_FAMILIES_PRESENT },
        { "undetailed", true, false, 0, 1, MODE_FAMILIES_PRESENT },
        { "detailedly", true, false, 0, 1, MODE_FAMILIES_PRESENT },
        { "quickly", true, false, 0, 1, MODE_FAMILIES_PRESENT },
        { "graphics", true, false, MODE_FAMILY_GRAPHICS, 0, MODE_FAMILY_GRAPHICS },
        { "quick net", false, false, MODE_FAMILY_NET, 0, MODE_FAMILY_NET },
        { "NET", true, false, MODE_FAMILY_NET, 0, MODE_FAMILY_NET },
        { "fs", true, false, MODE_FAMILY_FS, 0, MODE_FAMILY_FS },
        { "graphics,net;quick", false, false, MODE_FAMILY_GRAPHICS | MODE_FAMILY_NET, 0,
          MODE_FAMILY_GRAPHICS | MODE_FAMILY_NET },
        { "nets", true, false, 0, 1, MODE_FAMILIES_PRESENT },
        { "grpahics fast quick", false, false, 0, 2, MODE_FAMILIES_PRESENT },
        { "quick2images", false, true, 0, 0, MODE_FAMILIES_PRESENT },
        { "quick cpu", false, false, MODE_FAMILY_CPU, 0, MODE_FAMILY_CPU },
        { "CPU net", true, false, MODE_FAMILY_CPU | MODE_FAMILY_NET, 0,
          MODE_FAMILY_CPU | MODE_FAMILY_NET },
        { "cpus", true, false, 0, 1, MODE_FAMILIES_PRESENT },
        { "quick fatal", false, false, 0, 0, MODE_FAMILIES_PRESENT },
    };
    uint32_t wrong = 0;
    size_t first_wrong = 0;
    for (size_t i = 0; i < sizeof(MODES) / sizeof(MODES[0]); ++i) {
        const RunMode mode = ModeFromText(MODES[i].text);
        if (mode.detailed != MODES[i].detailed || mode.every_case != MODES[i].images ||
            mode.families_named != MODES[i].families || mode.unknown_count != MODES[i].unknown ||
            (mode.source == MODE_SOURCE_FILE) != (MODES[i].text != NULL) ||
            ModeFamiliesToRun(&mode, MODE_FAMILIES_PRESENT) != MODES[i].run) {
            if (wrong == 0u) {
                first_wrong = i;
            }
            ++wrong;
        }
    }

    CHECK_TRUE(SUITE, "the mode file is read a whole word at a time", CHECK_NONE, wrong == 0,
               "%u of %u readings wrong, the first \"%.40s\"", (unsigned)wrong,
               (unsigned)(sizeof(MODES) / sizeof(MODES[0])),
               wrong > 0u && MODES[first_wrong].text != NULL ? MODES[first_wrong].text
                                                             : "(no file)");

    // What the report says about the file: the words it did not know, quoted and never fatal, a
    // family this build does not have, a list too long to quote ending short but counted, a
    // directory where the file should be, and a file longer than is read, whose cut word is
    // dropped rather than quoted as a word nobody wrote.
    char said[REPORT_NOTE_MAX + 1];
    const RunMode typo = ModeFromText("grpahics fast quick net");
    ModeDescribe(said, sizeof(said), &typo, MODE_FAMILY_GRAPHICS);
    const bool typo_said =
        strcmp(said, "mode: quick, from the mode file; pictures of failures only; families none;"
                     " not in this build: net; 2 unknown words ignored: grpahics fast") == 0;
    const RunMode none = ModeFromText(NULL);
    char none_said[REPORT_NOTE_MAX + 1];
    ModeDescribe(none_said, sizeof(none_said), &none, MODE_FAMILY_GRAPHICS);
    const bool none_said_right =
        strcmp(none_said,
               "mode: detailed, no mode file; pictures of failures only; families graphics") == 0;
    const RunMode folder = ModeFromFile(MODE_SOURCE_DIRECTORY, NULL, 0, false);
    char folder_said[REPORT_NOTE_MAX + 1];
    ModeDescribe(folder_said, sizeof(folder_said), &folder, MODE_FAMILY_GRAPHICS);
    const bool folder_right =
        folder.detailed && strstr(folder_said, "mode is a directory, read as no file") != NULL;
    // This build has every family, so a file naming one runs that one and names nothing
    // missing.
    const RunMode net_only = ModeFromText("quick net");
    char net_said[REPORT_NOTE_MAX + 1];
    ModeDescribe(net_said, sizeof(net_said), &net_only, MODE_FAMILIES_PRESENT);
    const bool net_said_right =
        MODE_FAMILIES_PRESENT ==
            (MODE_FAMILY_CPU | MODE_FAMILY_GRAPHICS | MODE_FAMILY_FS | MODE_FAMILY_NET) &&
        strcmp(net_said,
               "mode: quick, from the mode file; pictures of failures only; families net") == 0;
    const RunMode flood = ModeFromText(
        "aaaaaaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbbbbbb cccccccccccccccccccc dddddddddddddddddddd "
        "eeeeeeeeeeeeeeeeeeee ffffffffffffffffffff abcdefghijklmnopqrstuvwxyzabc");
    const size_t flood_length = strlen(flood.unknown);
    const bool flood_right = flood.unknown_count == 7u && flood_length < MODE_UNKNOWN_TEXT_MAX &&
                             flood_length >= 3u &&
                             strcmp(flood.unknown + flood_length - 3u, "...") == 0 &&
                             strstr(flood.unknown, "dddd") != NULL;
    // "images quick" with the limit falling inside "quick": the fragment "qui" is dropped, and
    // the same limit falling on the space after a whole word keeps it.
    static const char LONG[] = "images quick more";
    const RunMode cut = ModeFromFile(MODE_SOURCE_FILE, LONG, 10u, true);
    const RunMode whole = ModeFromFile(MODE_SOURCE_FILE, LONG, 12u, true);
    char cut_said[REPORT_NOTE_MAX + 1];
    ModeDescribe(cut_said, sizeof(cut_said), &cut, MODE_FAMILY_GRAPHICS);
    const bool cut_right = cut.longer && cut.unknown_count == 0u && cut.every_case &&
                           cut.detailed && whole.longer && !whole.detailed &&
                           whole.unknown_count == 0u &&
                           strstr(cut_said, "mode file longer than 4096 bytes, the rest ignored") !=
                               NULL &&
                           MODE_FILE_MAX == 4096u;
    CHECK_TRUE(SUITE, "the report says what the mode file was and what was not understood",
               CHECK_NONE,
               typo_said && none_said_right && folder_right && net_said_right && flood_right &&
                   cut_right,
               "typo line %.90s; no file line %.70s; directory line %.70s; net line %.70s; "
               "long list %.40s; cut file %.70s",
               typo_said ? "right" : said, none_said_right ? "right" : none_said,
               folder_right ? "right" : folder_said, net_said_right ? "right" : net_said,
               flood_right ? "right" : flood.unknown, cut_right ? "right" : cut_said);
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

    // No files for this one: it needs a closed picture and a record, and three more writes on
    // an emulated memory card cost more than everything else this suite does.
    PictureWriteEveryCase(false);
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

    // The fixture as the one case of a run, opened on its pictures, as the report would have
    // kept it.
    static ReportRecord line;
    static UiRun run;
    memset(&line, 0, sizeof(line));
    snprintf(line.suite, sizeof(line.suite), "support");
    snprintf(line.name, sizeof(line.name), "a fixture");
    snprintf(line.tier, sizeof(line.tier), "none");
    snprintf(line.detail, sizeof(line.detail), "one pixel wrong");
    line.result = REPORT_FAIL;
    line.picture = 0;
    UiRunIndex(&run, &line, 1, 1);
    UiState state = UiStart(&run);
    state.level = UI_LEVEL_CASE;
    state.view = UI_VIEW_DIFF;
    UiCompose(&frame, &run, state, &record);

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
    line.result = REPORT_FAIL;
    UiCompose(&frame, &run, state, &record);
    const uint32_t shouting = CountRed(&frame);
    line.result = REPORT_PASS;
    UiCompose(&frame, &run, state, &record);
    const uint32_t quiet = CountRed(&frame);
    CHECK_TRUE(SUITE, "a failed case shouts its verdict on the heading and a passing one does not",
               CHECK_NONE, shouting > 100u && quiet == 0u,
               "%u pixels of it when it failed, %u when it passed", (unsigned)shouting,
               (unsigned)quiet);
    line.result = REPORT_FAIL;

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

    // All four swatches, each with a lit pixel of its own word beside it, so that deleting one
    // of them or its label fails here.
    uint32_t key_wrong = 0;
    static const uint8_t KEY_ORDER[4] = { PICTURE_MATCHED, PICTURE_WRONG, PICTURE_UNJUDGED,
                                          PICTURE_UNSET };
    static const uint32_t KEY_LETTERS[4] = { 8u, 7u, 10u, 13u };
    uint32_t key_x = 32u;
    for (uint32_t i = 0; i < 4u; ++i) {
        uint8_t want[3];
        PictureDiffColour(KEY_ORDER[i], want);
        const uint8_t *const swatch = UiPixel(&frame, key_x + 8u, key_y + 8u);
        if (swatch == NULL || memcmp(swatch, want, 3u) != 0) {
            ++key_wrong;
        }
        // Somewhere in the word beside it there has to be a lit pixel, or the swatch is
        // labelled with nothing.
        bool lettered = false;
        for (uint32_t dx = 0; dx < KEY_LETTERS[i] * 16u && !lettered; ++dx) {
            for (uint32_t dy = 0; dy < 16u && !lettered; ++dy) {
                const uint8_t *const at = UiPixel(&frame, key_x + 22u + dx, key_y + dy);
                lettered = at != NULL && at[0] > 0x40 && at[1] > 0x40;
            }
        }
        if (!lettered) {
            ++key_wrong;
        }
        key_x += 22u + KEY_LETTERS[i] * 16u + 32u;
    }
    CHECK_TRUE(SUITE, "all four colours of a verdict are named under the pictures", CHECK_NONE,
               key_wrong == 0u, "%u of eight swatches and words were missing or wrong",
               (unsigned)key_wrong);

    // Three panels rather than one, and read at the pixel where what was drawn and what was
    // expected differ, so a layout that drew one picture twice fails here.
    record.format = PICTURE_BITS;
    UiCompose(&frame, &run, state, &record);
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
    char saved[PICTURE_PATH_MAX];
    snprintf(saved, sizeof(saved), "%s", previous_root);
    const bool saved_policy = PictureWritesEveryCase();
    const bool saved_retention = PictureIsRetaining();

    // Its own corner, and every picture written whatever the verdict, because the cases below
    // are about the writer and a passing case would otherwise write nothing to look at. Nothing
    // retained either: these fixtures came from this file and not from the graphics unit, and
    // the detailed mode is for looking at what the hardware drew.
    PictureSetRoot(Scratch());
    PictureWriteEveryCase(true);
    PictureRetain(false);

    CheckNaming();
    CheckNames();
    CheckRoot();

    if (!CheckWriting()) {
        // Every case below writes a file and reads it back. They would all report the same
        // missing directory, and each would read as a defect in the thing it was checking.
        SweepUp();
        PictureSetRoot(saved);
        PictureWriteEveryCase(saved_policy);
        PictureRetain(saved_retention);
        return;
    }
    CheckTheLongestRoot();
    CheckAPixelNobodyLookedAt();
    CheckBitsAndCollisions();
    CheckReportLine();
    CheckLaunchAndMode();
    CheckLayout();
    RunSupportScreens();
    RunSupportJournal(Scratch());

    CHECK_TRUE(SUITE, "this suite takes its own files away again", CHECK_NONE, SweepUp(),
               "something this suite wrote is still on the memory card");

    PictureSetRoot(saved);
    PictureWriteEveryCase(saved_policy);
    PictureRetain(saved_retention);
}
