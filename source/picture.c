// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// opendir and rmdir, which the host build's C11 does not declare without asking.
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "picture.h"

// Nothing here knows what a console is: it writes ordinary files through the C library, which is
// what lets the host build exercise the writer and the naming with no device in the loop.

// The colours a diff picture is made of. Grey for a pixel the judge accepted, red for one it
// refused, blue for one it declined to judge, and magenta for one nothing ever looked at, which
// is the only one of the four that should never appear.
#define DIFF_COLOUR_COUNT 4
static const uint8_t DIFF_COLOURS[DIFF_COLOUR_COUNT][3] = {
    { 0x20, 0x20, 0x20 },
    { 0xE0, 0x20, 0x20 },
    { 0x30, 0x40, 0x70 },
    { 0xFF, 0x00, 0xFF },
};

#define MAX_PIXELS ((size_t)PICTURE_MAX_DIMENSION * PICTURE_MAX_DIMENSION)

// Static rather than allocated, so that collecting a picture cannot fail for want of memory
// while a case is being judged. The retained copies below are allocated, and those may fail.
static uint8_t expected_scratch[MAX_PIXELS * 4u];
static uint8_t verdict_scratch[MAX_PIXELS];
static uint8_t diff_scratch[MAX_PIXELS * 3u];

static Picture current;
static PicturePaths last_paths;

// Big enough for the longest path this file will put under it, so that a root which fits here
// cannot produce one that does not fit there.
static char root[PICTURE_PATH_MAX];
static bool write_every_case;
static bool retaining;

// How many finished cases the detailed mode can show, and how much of them it may hold. Three
// buffers a case at 64 by 64 is 36 KiB, so the cap is reached long before the limit below.
#define MAX_RETAINED_BYTES (16u * 1024u * 1024u)

// Allocated the first time a case is retained, which is never in an automatic run. As an array
// it was 182 KiB of the program's own memory that the common mode had no use for.
static PictureRecord *records;
static size_t record_count;
static size_t retained_bytes;

// File names come from case names, which are English sentences, so two cases can reach the same
// name through the dashes and one would silently overwrite the other's evidence. Every name
// taken is remembered by hash, three per case, and sized for every drawing case at once.
#define USED_MAX 1024
static uint32_t used_hashes[USED_MAX];
static size_t used_count;

bool PictureSetRoot(const char *directory) {
    // Refused rather than cut short: a truncated root is a directory nobody named, and one that
    // cannot be written to can truncate into one that can, which would let the case reporting
    // "nowhere to write" pass while every case after it failed for that reason.
    if (directory == NULL || strlen(directory) >= sizeof(root)) {
        root[0] = '\0';
        return false;
    }
    snprintf(root, sizeof(root), "%s", directory);
    return true;
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

/// Every file name one reserved stem occupies. The three pictures share a stem, so a case called
/// "foo expected" spells the file a case called "foo" writes its expectation into. The raw bytes
/// need no entry: they differ from the drawn picture only by the extension.
static const char *const NAME_FORMS[3] = { "%s/%s", "%s/%s-expected", "%s/%s-diff" };

/// Whether any of the names @p stem would occupy is already somebody else's.
static bool AnyFormTaken(const char *suite, const char *stem) {
    for (size_t form = 0; form < 3u; ++form) {
        char key[PICTURE_PATH_MAX];
        snprintf(key, sizeof(key), NAME_FORMS[form], suite, stem);
        if (HashSeen(Hash(key))) {
            return true;
        }
    }
    return false;
}

static void TakeEveryForm(const char *suite, const char *stem) {
    for (size_t form = 0; form < 3u; ++form) {
        char key[PICTURE_PATH_MAX];
        snprintf(key, sizeof(key), NAME_FORMS[form], suite, stem);
        if (used_count < USED_MAX) {
            used_hashes[used_count++] = Hash(key);
        }
    }
}

bool PictureReserveName(char *stem, size_t stem_size, const char *suite_stem,
                        const char *name) {
    if (stem == NULL || stem_size < 8u) {
        return false;
    }
    PictureStem(stem, stem_size, name);

    if (!AnyFormTaken(suite_stem, stem)) {
        TakeEveryForm(suite_stem, stem);
        return true;
    }

    // Up to ninety eight of them. The ceiling exists so this cannot run forever, not because
    // ninety eight cases are ever expected to spell one name.
    for (unsigned suffix = 2; suffix < 100u; ++suffix) {
        // Room for the dash, the digits and the terminator. A stem with no room for its own
        // number gives some up rather than going back as the name somebody else already has.
        const size_t digits = suffix < 10u ? 1u : 2u;
        size_t length = strlen(stem);
        if (length + digits + 2u > stem_size) {
            if (stem_size <= digits + 2u) {
                break;
            }
            length = stem_size - digits - 2u;
            stem[length] = '\0';
        }

        char candidate[PICTURE_PATH_MAX];
        const int wrote = snprintf(candidate, sizeof(candidate), "%s-%u", stem, suffix);
        if (wrote < 0 || (size_t)wrote >= sizeof(candidate)) {
            break;
        }
        if (AnyFormTaken(suite_stem, candidate)) {
            continue;
        }
        snprintf(stem + length, stem_size - length, "-%u", suffix);
        TakeEveryForm(suite_stem, stem);
        return true;
    }

    return false;
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
    // Anything off the end is a pixel nobody set, which is the one thing it certainly is not
    // safe to draw as accepted.
    memcpy(out, DIFF_COLOURS[verdict < DIFF_COLOUR_COUNT ? verdict : (uint8_t)PICTURE_UNSET],
           3u);
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
    if (!PictureReserveName(stem, sizeof(stem), suite_stem, name)) {
        // No name left that is this case's own. Nothing is written and the line says the case
        // has no picture, which is true, rather than naming one that belongs to another case.
        return;
    }

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
    if (record_count >= PICTURE_MAX_RECORDS || retained_bytes + bytes > MAX_RETAINED_BYTES) {
        return;
    }
    if (records == NULL) {
        records = (PictureRecord *)calloc(PICTURE_MAX_RECORDS, sizeof(*records));
        if (records == NULL) {
            retaining = false;
            return;
        }
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
    // Not the value a judge would ever choose. A pixel still holding this when the picture is
    // written is one no compare loop reached, and it says so in a colour nothing else uses.
    memset(verdict_scratch, PICTURE_UNSET, (size_t)width * height);
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

void PictureDiscard(void) {
    current.open = false;
}

size_t PictureCount(void) {
    return record_count;
}

const PictureRecord *PictureAt(size_t index) {
    return index < record_count ? &records[index] : NULL;
}

bool PictureRemoveSuite(const char *suite) {
    char suite_stem[32];
    PictureStem(suite_stem, sizeof(suite_stem), suite != NULL ? suite : "");
    char directory[PICTURE_PATH_MAX + 64];
    const int wrote = snprintf(directory, sizeof(directory), "%s/images/%s", root, suite_stem);
    if (root[0] == '\0' || suite_stem[0] == '\0' || wrote < 0 ||
        (size_t)wrote >= sizeof(directory)) {
        return false;
    }
    DIR *listing = opendir(directory);
    if (listing == NULL) {
        return true;
    }
    bool removed = true;
    const struct dirent *entry;
    while ((entry = readdir(listing)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char path[sizeof(directory) + 256];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        removed = remove(path) == 0 && removed;
    }
    closedir(listing);
    return rmdir(directory) == 0 && removed;
}
