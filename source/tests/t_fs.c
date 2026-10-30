// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "../check.h"
#include "../fsref.h"
#include "../perf.h"
#include "../report.h"
#include "tests.h"

// Reads from the memory card the way a game reads its files: IFile::Read with an offset every
// time, through FsFile, which is the command the emulator serves. Every case times the reads and
// checks every byte against the pattern the file was written with, so a fast wrong read fails.
// An emulator does the seek on its side of each read, so how much a seek costs shows as the
// difference between the patterns: sequential, random, backwards, the same place again.
// The host's page cache is warm after the first pass; nothing here can empty it without root.

static const char *const SUITE = "fs";

#define DIRECTORY FS_DIRECTORY
#define BIG_PATH FS_BIG_PATH
#define SMALL_DIRECTORY DIRECTORY "/small"
#define BIG_SIZE FS_BIG_SIZE
#define SMALL_COUNT 256u
#define SMALL_SIZE 4096u
#define RANDOM_READS 4096u
#define BUFFER_SIZE (16u * 1024u * 1024u)

static u8 *buffer;

/// Byte @p offset of small file @p index.
static u8 SmallByte(u32 index, u32 offset) {
    return (u8)(index * 31u + offset * 7u + 1u);
}

/// Where the bytes read first differ from the pattern, or -1.
static s64 FirstWrong(const u8 *data, u64 offset, u64 size) {
    for (u64 i = 0; i < size; ++i) {
        if (data[i] != FsRefPatternByte(offset + i)) {
            return (s64)i;
        }
    }
    return -1;
}

static void Fill(u8 *data, u64 offset, u64 size) {
    for (u64 i = 0; i < size; ++i) {
        data[i] = FsRefPatternByte(offset + i);
    }
}

/// Makes the big file and the small ones unless they are already there with the right size,
/// so only the first pass of a timed run writes.
static bool Prepare(FsFileSystem *fs, char *why, size_t why_size) {
    fsFsCreateDirectory(fs, "/edenprobe");
    fsFsCreateDirectory(fs, DIRECTORY);
    fsFsCreateDirectory(fs, SMALL_DIRECTORY);
    FsFile file;
    s64 size = -1;
    if (R_SUCCEEDED(fsFsOpenFile(fs, BIG_PATH, FsOpenMode_Read, &file))) {
        fsFileGetSize(&file, &size);
        fsFileClose(&file);
    }
    if (size != (s64)BIG_SIZE) {
        fsFsDeleteFile(fs, BIG_PATH);
        Result rc = fsFsCreateFile(fs, BIG_PATH, BIG_SIZE, 0);
        if (R_SUCCEEDED(rc)) {
            rc = fsFsOpenFile(fs, BIG_PATH, FsOpenMode_Write, &file);
        }
        if (R_FAILED(rc)) {
            snprintf(why, why_size, "could not make %s: 0x%x", BIG_PATH, (unsigned)rc);
            return false;
        }
        const u32 chunk = 1024u * 1024u;
        for (u64 offset = 0; offset < BIG_SIZE && R_SUCCEEDED(rc); offset += chunk) {
            Fill(buffer, offset, chunk);
            rc = fsFileWrite(&file, (s64)offset, buffer, chunk, FsWriteOption_None);
        }
        fsFileFlush(&file);
        fsFileClose(&file);
        if (R_FAILED(rc)) {
            snprintf(why, why_size, "writing %s failed: 0x%x", BIG_PATH, (unsigned)rc);
            return false;
        }
    }
    char path[64];
    for (u32 i = 0; i < SMALL_COUNT; ++i) {
        snprintf(path, sizeof(path), SMALL_DIRECTORY "/%03u.bin", (unsigned)i);
        size = -1;
        if (R_SUCCEEDED(fsFsOpenFile(fs, path, FsOpenMode_Read, &file))) {
            fsFileGetSize(&file, &size);
            fsFileClose(&file);
        }
        if (size == (s64)SMALL_SIZE) {
            continue;
        }
        fsFsDeleteFile(fs, path);
        Result rc = fsFsCreateFile(fs, path, SMALL_SIZE, 0);
        if (R_SUCCEEDED(rc)) {
            rc = fsFsOpenFile(fs, path, FsOpenMode_Write, &file);
        }
        if (R_SUCCEEDED(rc)) {
            for (u32 b = 0; b < SMALL_SIZE; ++b) {
                buffer[b] = SmallByte(i, b);
            }
            rc = fsFileWrite(&file, 0, buffer, SMALL_SIZE, FsWriteOption_None);
            fsFileClose(&file);
        }
        if (R_FAILED(rc)) {
            snprintf(why, why_size, "could not make %s: 0x%x", path, (unsigned)rc);
            return false;
        }
    }
    return true;
}

typedef struct {
    u64 calls;
    u64 bytes;
    s64 wrong_at; // offset in the file of the first wrong byte, or -1
    Result failed; // the first failing result, or 0
    u64 short_reads;
} Tally;

/// One timed read of @p size bytes at @p offset, checked against the pattern.
static void Read(FsFile *file, u64 offset, u64 size, Tally *t) {
    u64 got = 0;
    const u64 start = armGetSystemTick();
    const Result rc = fsFileRead(file, (s64)offset, buffer, size, FsReadOption_None, &got);
    PerfAddWork(armTicksToNs(armGetSystemTick() - start));
    ++t->calls;
    t->bytes += got;
    if (R_FAILED(rc)) {
        if (t->failed == 0) {
            t->failed = rc;
        }
        return;
    }
    if (got != size) {
        ++t->short_reads;
    }
    if (t->wrong_at < 0) {
        const s64 at = FirstWrong(buffer, offset, got);
        if (at >= 0) {
            t->wrong_at = (s64)offset + at;
        }
    }
}

static void Judge(const char *name, const Tally *t) {
    CHECK_TRUE(SUITE, name, CHECK_EXACT,
               t->failed == 0 && t->short_reads == 0 && t->wrong_at < 0,
               "%llu calls, %llu bytes; first failure 0x%x, %llu short reads, first wrong byte "
               "at %lld",
               (unsigned long long)t->calls, (unsigned long long)t->bytes, (unsigned)t->failed,
               (unsigned long long)t->short_reads, (long long)t->wrong_at);
}

static void Sequential(FsFile *file, const char *name, u64 block, u64 total) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    Tally t = { 0, 0, -1, 0, 0 };
    for (u64 offset = 0; offset < total; offset += block) {
        Read(file, offset, block, &t);
    }
    Judge(name, &t);
}

/// A fixed sequence, so every pass and every build reads the same places.
static u32 Next(u32 *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void Random(FsFile *file, const char *name, bool aligned) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    Tally t = { 0, 0, -1, 0, 0 };
    u32 state = aligned ? 1u : 2u;
    for (u32 i = 0; i < RANDOM_READS; ++i) {
        u64 offset = (u64)(Next(&state) % (BIG_SIZE - SMALL_SIZE));
        offset = aligned ? (offset & ~(u64)(SMALL_SIZE - 1u)) : (offset | 1u);
        Read(file, offset, SMALL_SIZE, &t);
    }
    Judge(name, &t);
}

static void Backwards(FsFile *file) {
    static const char *const NAME = "64 KiB reads walk the file backwards";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    Tally t = { 0, 0, -1, 0, 0 };
    const u64 block = 64u * 1024u;
    for (u64 offset = BIG_SIZE; offset >= block; offset -= block) {
        Read(file, offset - block, block, &t);
    }
    Judge(NAME, &t);
}

static void SamePlace(FsFile *file) {
    static const char *const NAME = "4096 reads of the same 4 KiB";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    Tally t = { 0, 0, -1, 0, 0 };
    for (u32 i = 0; i < RANDOM_READS; ++i) {
        Read(file, 12345u * SMALL_SIZE, SMALL_SIZE, &t);
    }
    Judge(NAME, &t);
}

static void SmallFiles(FsFileSystem *fs) {
    static const char *const NAME = "256 small files are opened, read and closed";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    u64 wrong = 0;
    Result failed = 0;
    char path[64];
    for (u32 i = 0; i < SMALL_COUNT; ++i) {
        snprintf(path, sizeof(path), SMALL_DIRECTORY "/%03u.bin", (unsigned)i);
        FsFile file;
        u64 got = 0;
        const u64 start = armGetSystemTick();
        Result rc = fsFsOpenFile(fs, path, FsOpenMode_Read, &file);
        if (R_SUCCEEDED(rc)) {
            rc = fsFileRead(&file, 0, buffer, SMALL_SIZE, FsReadOption_None, &got);
            fsFileClose(&file);
        }
        PerfAddWork(armTicksToNs(armGetSystemTick() - start));
        if (R_FAILED(rc)) {
            if (failed == 0) {
                failed = rc;
            }
            continue;
        }
        for (u32 b = 0; b < SMALL_SIZE; ++b) {
            if (got != SMALL_SIZE || buffer[b] != SmallByte(i, b)) {
                ++wrong;
                break;
            }
        }
    }
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, failed == 0 && wrong == 0,
               "first failure 0x%x, %llu files with wrong bytes", (unsigned)failed,
               (unsigned long long)wrong);
}

/// The reads at and past the end, which every one of the above stays clear of.
static void Edges(FsFile *file) {
    static const char *const PAST =
        "a read that runs past the end returns only the bytes that are there";
    static const char *const AT_END = "a read that starts at the end returns no bytes";
    static const char *const EMPTY = "a read of no bytes returns no bytes";
    u64 got = 12345;
    Result rc = 0;
    if (ReportStart(SUITE, PAST)) {
        rc = fsFileRead(file, BIG_SIZE - 100u, buffer, SMALL_SIZE, FsReadOption_None, &got);
        const bool tail_right =
            R_SUCCEEDED(rc) && got == 100u && FirstWrong(buffer, BIG_SIZE - 100u, 100u) < 0;
        CHECK_TRUE(SUITE, PAST, CHECK_EXACT, tail_right, "result 0x%x, %llu bytes (100 are there)",
                   (unsigned)rc, (unsigned long long)got);
    }

    if (ReportStart(SUITE, AT_END)) {
        got = 12345;
        rc = fsFileRead(file, BIG_SIZE, buffer, SMALL_SIZE, FsReadOption_None, &got);
        CHECK_TRUE(SUITE, AT_END, CHECK_EXACT, R_SUCCEEDED(rc) && got == 0u,
                   "result 0x%x, %llu bytes", (unsigned)rc, (unsigned long long)got);
    }

    if (ReportStart(SUITE, EMPTY)) {
        got = 12345;
        rc = fsFileRead(file, 4096u, buffer, 0u, FsReadOption_None, &got);
        CHECK_TRUE(SUITE, EMPTY, CHECK_EXACT, R_SUCCEEDED(rc) && got == 0u,
                   "result 0x%x, %llu bytes", (unsigned)rc, (unsigned long long)got);
    }
}

void RunFs(void) {
    if (buffer == NULL) {
        buffer = malloc(BUFFER_SIZE);
    }
    static const char *const OPENS = "the memory card opens as a file system";
    static const char *const HOLDS = "the card holds a 64 MiB file and 256 small ones to read";
    if (!ReportStart(SUITE, OPENS)) {
        return;
    }
    FsFileSystem fs;
    const Result opened = buffer != NULL ? fsOpenSdCardFileSystem(&fs)
                                         : MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    CHECK_TRUE(SUITE, OPENS, CHECK_NONE, R_SUCCEEDED(opened), "result 0x%x", (unsigned)opened);
    if (R_FAILED(opened)) {
        return;
    }
    if (!ReportStart(SUITE, HOLDS)) {
        fsFsClose(&fs);
        return;
    }
    char why[160] = "";
    const bool ready = Prepare(&fs, why, sizeof(why));
    CHECK_TRUE(SUITE, HOLDS, CHECK_NONE, ready, "%s", why);
    FsFile file;
    const Result big = ready ? fsFsOpenFile(&fs, BIG_PATH, FsOpenMode_Read, &file) : opened;
    if (!ready || R_FAILED(big)) {
        fsFsClose(&fs);
        return;
    }
    Sequential(&file, "sequential reads of 512 bytes cover 4 MiB", 512u, 4u * 1024u * 1024u);
    Sequential(&file, "sequential reads of 4 KiB cover 16 MiB", 4096u, 16u * 1024u * 1024u);
    Sequential(&file, "sequential reads of 64 KiB cover 64 MiB", 64u * 1024u, BIG_SIZE);
    Sequential(&file, "sequential reads of 1 MiB cover 64 MiB", 1024u * 1024u, BIG_SIZE);
    Sequential(&file, "sequential reads of 16 MiB cover 64 MiB", BUFFER_SIZE, BIG_SIZE);
    Random(&file, "4096 random 4 KiB reads at aligned offsets", true);
    Random(&file, "4096 random 4 KiB reads at odd offsets", false);
    Backwards(&file);
    SamePlace(&file);
    SmallFiles(&fs);
    Edges(&file);
    fsFileClose(&file);
    fsFsClose(&fs);
}
