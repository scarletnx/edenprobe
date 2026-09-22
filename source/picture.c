// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "picture.h"

// Nothing here knows what a console is: it writes ordinary files through the C library, which is
// what lets the host build exercise the writer and the naming with no device in the loop.

// The three colours a diff picture is made of. Grey for a pixel the judge accepted, red for one
// it refused, and blue for one it declined to judge, which is the band an edge runs through.
static const uint8_t DIFF_COLOURS[3][3] = {
    { 0x20, 0x20, 0x20 },
    { 0xE0, 0x20, 0x20 },
    { 0x30, 0x40, 0x70 },
};

#define MAX_PIXELS ((size_t)PICTURE_MAX_DIMENSION * PICTURE_MAX_DIMENSION)

// Static rather than allocated, so that collecting a picture cannot fail for want of memory
// while a case is being judged. The retained copies below are allocated, and those may fail.
static uint8_t expected_scratch[MAX_PIXELS * 4u];
static uint8_t verdict_scratch[MAX_PIXELS];
static uint8_t diff_scratch[MAX_PIXELS * 3u];

static Picture current;
static PicturePaths last_paths;

static char root[96];
static bool write_every_case;
static bool retaining;

// How many finished cases the detailed mode can show, and how much of them it may hold. Three
// buffers a case at 64 by 64 is 36 KiB, so the cap is reached long before the limit below.
#define MAX_RECORDS 256
#define MAX_RETAINED_BYTES (16u * 1024u * 1024u)

static PictureRecord records[MAX_RECORDS];
static size_t record_count;
static size_t retained_bytes;

// File names are built from case names, which are English sentences, so two cases can reach the
// same name through the dashes. A collision would overwrite the evidence of one failure with
// another's, silently, so every name used is remembered by hash and a repeat gets a number.
#define USED_MAX 512
static uint32_t used_hashes[USED_MAX];
static size_t used_count;

void PictureSetRoot(const char *directory) {
    snprintf(root, sizeof(root), "%s", directory != NULL ? directory : "");
}

const char *PictureRoot(void) {
    return root;
}

bool PictureWritesEveryCase(void) {
    return write_every_case;
}

void PictureWriteEveryCase(bool on) {
    write_every_case = on;
}

bool PictureIsRetaining(void) {
    return retaining;
}

void PictureRetain(bool on) {
    retaining = on;
}

void PictureStem(char *out, size_t out_size, const char *name) {
    size_t w = 0;
    bool pending_dash = false;
    for (size_t r = 0; name[r] != '\0' && w + 1 < out_size; ++r) {
        const char c = name[r];
        char mapped;
        if (c >= 'a' && c <= 'z') {
            mapped = c;
        } else if (c >= 'A' && c <= 'Z') {
            mapped = (char)(c - 'A' + 'a');
        } else if (c >= '0' && c <= '9') {
            mapped = c;
        } else {
            // Held back rather than written, so a run of punctuation collapses and a trailing
            // one never reaches the file name.
            pending_dash = w > 0;
            continue;
        }
        if (pending_dash && w + 2 < out_size) {
            out[w++] = '-';
        }
        pending_dash = false;
        out[w++] = mapped;
    }
    out[w] = '\0';
    if (w == 0 && out_size > 1) {
        snprintf(out, out_size, "unnamed");
    }
}

static uint32_t Hash(const char *text) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        hash ^= (uint8_t)text[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool HashSeen(uint32_t hash) {
    for (size_t i = 0; i < used_count; ++i) {
        if (used_hashes[i] == hash) {
            return true;
        }
    }
    return false;
}

/// Makes @p stem unique within @p suite for this run, by number if it has to.
static void MakeUnique(char *stem, size_t stem_size, const char *suite) {
    char key[PICTURE_PATH_MAX];
    snprintf(key, sizeof(key), "%s/%s", suite, stem);
    uint32_t hash = Hash(key);
    for (unsigned suffix = 2; suffix < 10 && HashSeen(hash); ++suffix) {
        snprintf(key, sizeof(key), "%s/%s-%u", suite, stem, suffix);
        hash = Hash(key);
        if (!HashSeen(hash)) {
            const size_t length = strlen(stem);
            snprintf(stem + length, stem_size - length, "-%u", suffix);
        }
    }
    if (used_count < USED_MAX) {
        used_hashes[used_count++] = hash;
    }
}

static void MakeDirectory(const char *path) {
    // Every call but the first fails with the directory already there, which is the normal case
    // and not worth a line in a report.
    (void)mkdir(path, 0777);
}

static bool WritePam(const char *path, uint32_t width, uint32_t height, uint32_t depth,
                     const char *tupltype, const uint8_t *pixels) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    fprintf(file, "P7\nWIDTH %u\nHEIGHT %u\nDEPTH %u\nMAXVAL 255\nTUPLTYPE %s\nENDHDR\n",
            (unsigned)width, (unsigned)height, (unsigned)depth, tupltype);
    const size_t count = (size_t)width * height * depth;
    const bool written = fwrite(pixels, 1, count, file) == count;
    return fclose(file) == 0 && written;
}

static bool WriteRaw(const char *path, const uint8_t *bytes, size_t count) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    const bool written = fwrite(bytes, 1, count, file) == count;
    return fclose(file) == 0 && written;
}

void PictureDiffColour(uint8_t verdict, uint8_t out[3]) {
    // Anything but the three is a pixel nobody set, which is the one thing it certainly is not
    // safe to draw as accepted.
    memcpy(out, DIFF_COLOURS[verdict < 3u ? verdict : (uint8_t)PICTURE_UNJUDGED], 3u);
}

/// Turns the judge's verdicts into the three colours a reader can see.
static void BuildDiff(const Picture *picture) {
    const size_t count = (size_t)picture->width * picture->height;
    for (size_t i = 0; i < count; ++i) {
        PictureDiffColour(picture->verdict[i], diff_scratch + i * 3u);
    }
}

static void WriteFiles(const Picture *picture, const char *suite, const char *name,
                       PicturePaths *paths) {
    char suite_stem[32];
    char stem[96];
    PictureStem(suite_stem, sizeof(suite_stem), suite);
    PictureStem(stem, sizeof(stem), name);
    MakeUnique(stem, sizeof(stem), suite_stem);

    // Room for the longest directory and the longest relative path underneath it, so that no
    // name this program can build is ever cut short into a file somewhere else.
    char directory[sizeof(root) + PICTURE_PATH_MAX + 2u];
    snprintf(directory, sizeof(directory), "%s/images", root);
    MakeDirectory(root);
    MakeDirectory(directory);
    snprintf(directory, sizeof(directory), "%s/images/%s", root, suite_stem);
    MakeDirectory(directory);

    // Every character in a stem is a lower case letter, a digit or a dash, and the rest of the
    // path is punctuation this program wrote, so a path can go onto a JSON line unescaped.
    char full[sizeof(root) + PICTURE_PATH_MAX + 2u];
    const size_t pixels = (size_t)picture->width * picture->height;

    snprintf(paths->image, sizeof(paths->image), "images/%s/%s.pam", suite_stem, stem);
    snprintf(full, sizeof(full), "%s/%s", root, paths->image);
    if (!WritePam(full, picture->width, picture->height, 4, "RGB_ALPHA", picture->got)) {
        paths->image[0] = '\0';
    }

    snprintf(paths->expected, sizeof(paths->expected), "images/%s/%s-expected.pam", suite_stem,
             stem);
    snprintf(full, sizeof(full), "%s/%s", root, paths->expected);
    if (!WritePam(full, picture->width, picture->height, 4, "RGB_ALPHA", picture->expected)) {
        paths->expected[0] = '\0';
    }

    BuildDiff(picture);
    snprintf(paths->diff, sizeof(paths->diff), "images/%s/%s-diff.pam", suite_stem, stem);
    snprintf(full, sizeof(full), "%s/%s", root, paths->diff);
    if (!WritePam(full, picture->width, picture->height, 3, "RGB", diff_scratch)) {
        paths->diff[0] = '\0';
    }

    // Only where the picture is of bit patterns. A colour target loses nothing on the way into
    // an image, and a second copy of it would be a second thing to keep in step.
    if (picture->format == PICTURE_BITS) {
        snprintf(paths->raw, sizeof(paths->raw), "images/%s/%s.bin", suite_stem, stem);
        snprintf(full, sizeof(full), "%s/%s", root, paths->raw);
        if (!WriteRaw(full, picture->got, pixels * 4u)) {
            paths->raw[0] = '\0';
        }
    }
}

/// Keeps a copy of one finished case for the detailed mode. Silent when there is no room: a
/// note here would put the choice of mode into the report, and the two modes report the same
/// bytes or the report is not worth diffing.
static void Retain(const Picture *picture, const char *suite, const char *name, const char *tier,
                   bool passed, const char *detail) {
    const size_t pixels = (size_t)picture->width * picture->height;
    const size_t bytes = pixels * 9u;
    if (record_count >= MAX_RECORDS || retained_bytes + bytes > MAX_RETAINED_BYTES) {
        return;
    }

    PictureRecord *const record = &records[record_count];
    record->got = (uint8_t *)malloc(pixels * 4u);
    record->expected = (uint8_t *)malloc(pixels * 4u);
    record->verdict = (uint8_t *)malloc(pixels);
    if (record->got == NULL || record->expected == NULL || record->verdict == NULL) {
        free(record->got);
        free(record->expected);
        free(record->verdict);
        memset(record, 0, sizeof(*record));
        retaining = false;
        return;
    }

    memcpy(record->got, picture->got, pixels * 4u);
    memcpy(record->expected, picture->expected, pixels * 4u);
    memcpy(record->verdict, picture->verdict, pixels);
    snprintf(record->suite, sizeof(record->suite), "%s", suite);
    snprintf(record->name, sizeof(record->name), "%s", name);
    snprintf(record->tier, sizeof(record->tier), "%s", tier);
    snprintf(record->detail, sizeof(record->detail), "%s", detail != NULL ? detail : "");
    record->passed = passed;
    record->width = picture->width;
    record->height = picture->height;
    record->format = picture->format;

    retained_bytes += bytes;
    ++record_count;
}

Picture *PictureBegin(uint32_t width, uint32_t height, PictureFormat format,
                      const uint8_t *got) {
    if (got == NULL || width == 0u || height == 0u || width > PICTURE_MAX_DIMENSION ||
        height > PICTURE_MAX_DIMENSION) {
        return NULL;
    }
    current.width = width;
    current.height = height;
    current.format = format;
    current.got = got;
    current.expected = expected_scratch;
    current.verdict = verdict_scratch;
    current.open = true;
    // Every pixel a judge looks at is set below, and a judge that skips one would otherwise
    // leave the previous case's answer there.
    memset(verdict_scratch, PICTURE_UNJUDGED, (size_t)width * height);
    memset(expected_scratch, 0, (size_t)width * height * 4u);
    return &current;
}

void PictureFinish(const char *suite, const char *name, const char *tier, bool passed,
                   const char *detail, PicturePaths *paths) {
    memset(&last_paths, 0, sizeof(last_paths));
    if (current.open) {
        if ((!passed || write_every_case) && root[0] != '\0') {
            WriteFiles(&current, suite, name, &last_paths);
        }
        if (retaining) {
            Retain(&current, suite, name, tier, passed, detail);
        }
        current.open = false;
    }
    if (paths != NULL) {
        *paths = last_paths;
    }
}

size_t PictureCount(void) {
    return record_count;
}

const PictureRecord *PictureAt(size_t index) {
    return index < record_count ? &records[index] : NULL;
}
