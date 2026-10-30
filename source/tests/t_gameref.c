// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../fsref.h"
#include "../gameref.h"
#include "../mode.h"
#include "../report.h"
#include "tests.h"

// The game scene's arithmetic against answers worked out by hand, so a frame the scene calls
// wrong is the machine's and not the world's, the hash's or the stats'.

static const char *const SUITE = "reference";

static GameWorld world;
static uint8_t image[GAME_IMAGE_BYTES];
static uint8_t chunk[GAME_FILE_CHUNK];
static uint32_t times[600];
static uint32_t scratch[600];

/// The first reading that disagreed, for the line.
typedef struct {
    uint32_t wrong;
    const char *first;
} Tally;

static void Expect(Tally *tally, bool condition, const char *what) {
    if (!condition && tally->wrong++ == 0u) {
        tally->first = what;
    }
}

static void Report(const char *name, const Tally *tally) {
    CHECK_TRUE(SUITE, name, CHECK_NONE, tally->wrong == 0u, "%u readings wrong, the first: %s",
               (unsigned)tally->wrong, tally->first != NULL ? tally->first : "none");
}

static const uint8_t *Pixel(uint32_t x, uint32_t y) {
    return image + ((size_t)y * GAME_TARGET + x) * 4u;
}

static bool WordAt(uint32_t x, uint32_t y, uint32_t word) {
    const uint8_t want[4] = { (uint8_t)word, (uint8_t)(word >> 8), (uint8_t)(word >> 16),
                              (uint8_t)(word >> 24) };
    return memcmp(Pixel(x, y), want, 4u) == 0;
}

static void CheckWorld(void) {
    Tally t = { 0, NULL };
    // One step off each edge, worked out by hand: 100 - 300 reflects to 200, and 16777116 + 500
    // runs 400 past the last place, 16777215, so reflects to 16776814.
    memset(&world, 0, sizeof(world));
    world.x[0] = 100;
    world.vx[0] = -300;
    world.x[1] = (256 << 16) - 100;
    world.vx[1] = 500;
    world.y[2] = 7;
    world.vy[2] = 9;
    GameWorldStep(&world);
    Expect(&t, world.x[0] == 200 && world.vx[0] == 300, "off the low edge");
    Expect(&t, world.x[1] == 16776814 && world.vx[1] == -500, "off the high edge");
    Expect(&t, world.y[2] == 16 && world.vy[2] == 9, "inside");
    Expect(&t, world.frame == 1u, "the frame counts");

    // The word: whole parts 0xAB and 0xCD, the tag (5 * 37 + 3) & 0xFF = 0xBC, opaque.
    world.frame = 3;
    world.x[5] = 0x00AB1234;
    world.y[5] = 0x00CDFFFF;
    Expect(&t, GameStreamWord(&world, 5) == 0xFFBCCDABu, "a cell's word");

    // The real start, stepped across many bounces: always inside, never still.
    GameWorldStart(&world);
    bool inside = world.frame == 0u;
    for (uint32_t k = 0; k < 2000u && inside; ++k) {
        for (uint32_t d = 0; d < GAME_DRAWS && inside; ++d) {
            inside = world.x[d] >= 0 && world.x[d] < (256 << 16) && world.y[d] >= 0 &&
                     world.y[d] < (256 << 16) && world.vx[d] != 0 && world.vy[d] != 0;
        }
        GameWorldStep(&world);
    }
    Expect(&t, inside, "2000 frames inside the world, every cell moving");
    Report("game: the world steps and bounces as worked out by hand", &t);
}

static void CheckFill(void) {
    Tally t = { 0, NULL };
    uint32_t missed_by_no_test = 0;
    for (uint32_t frame = 0; frame < 16u; ++frame) {
        // A depth test of less, drawn in order, against a buffer cleared to one.
        float nearest_depth = 1.0f;
        uint32_t winner = GAME_FILL_QUADS;
        for (uint32_t q = 0; q < GAME_FILL_QUADS; ++q) {
            const float z = GameFillDepth(frame, q);
            const float scaled = z * 256.0f;
            Expect(&t, scaled == (float)(uint32_t)scaled && z >= 0.25f && z < 1.0f,
                   "a depth that is not a binary fraction from 0.25 below one");
            if (z < nearest_depth) {
                nearest_depth = z;
                winner = q;
            }
        }
        Expect(&t, winner == GameFillNearest(frame) && nearest_depth == 0.25f,
               "the depth test's winner is the nearest quad");
        uint8_t colours[GAME_FILL_QUADS][4];
        for (uint32_t q = 0; q < GAME_FILL_QUADS; ++q) {
            GameFillColour(frame, q, colours[q]);
            for (uint32_t r = 0; r < q; ++r) {
                Expect(&t, memcmp(colours[q], colours[r], 4u) != 0, "two quads of one colour");
            }
        }
        missed_by_no_test += winner == GAME_FILL_QUADS - 1u ? 1u : 0u;
    }
    // By hand: the nearest is the frame's number mod 8, at a quarter, the next a sixteenth on.
    Expect(&t, GameFillNearest(0) == 0u && GameFillNearest(9) == 1u &&
                   GameFillDepth(3, 3) == 0.25f && GameFillDepth(3, 4) == 80.0f / 256.0f &&
                   GameFillDepth(3, 2) == 176.0f / 256.0f,
           "frame 3's depths, worked out by hand");
    // A machine with no depth test leaves the last quad's colour: right in two frames of 16.
    Expect(&t, missed_by_no_test == 2u, "frames where the last quad is the nearest");
    Report("game: each frame's fill leaves the nearest quad, which is not the last drawn", &t);
}

static void CheckImage(void) {
    Tally t = { 0, NULL };
    GameWorldStart(&world);
    for (uint32_t k = 0; k < 5u; ++k) {
        GameWorldStep(&world);
    }
    GameExpectedImage(&world, image);
    uint8_t texel[4];
    GameTexel(5, 0, 0, texel);
    Expect(&t, memcmp(Pixel(0, 0), texel, 4u) == 0 && memcmp(Pixel(3, 3), texel, 4u) == 0,
           "the first texel covers the top left four by four");
    GameTexel(5, 1, 0, texel);
    Expect(&t, memcmp(Pixel(4, 0), texel, 4u) == 0, "the second texel starts four pixels in");
    GameTexel(5, 15, 15, texel);
    Expect(&t, memcmp(Pixel(63, 63), texel, 4u) == 0, "the last texel");
    // Frame 5: r = 5 * 5 + 0 + 0 = 25 at texel (0, 0), b of it, worked out by hand.
    GameTexel(5, 0, 0, texel);
    Expect(&t, texel[0] == 8u && texel[1] == 8u && texel[2] == 25u && texel[3] == 0xFFu,
           "texel (0, 0) of frame 5");
    uint8_t fill[4];
    GameFillColour(5, GameFillNearest(5), fill);
    Expect(&t, memcmp(Pixel(64, 0), fill, 4u) == 0 && memcmp(Pixel(127, 63), fill, 4u) == 0 &&
                   memcmp(Pixel(0, 64), fill, 4u) == 0 && memcmp(Pixel(63, 127), fill, 4u) == 0,
           "the two fill quarters");
    Expect(&t, WordAt(64, 64, GameStreamWord(&world, 0)) &&
                   WordAt(65, 65, GameStreamWord(&world, 0)) &&
                   WordAt(66, 64, GameStreamWord(&world, 1)) &&
                   WordAt(64, 66, GameStreamWord(&world, GAME_CELLS)) &&
                   WordAt(127, 127, GameStreamWord(&world, GAME_DRAWS - 1u)),
           "the cells, two by two pixels, row by row");
    Report("game: a frame's image has the texture, the fill and the cells where they belong", &t);
}

static void CheckHashes(void) {
    Tally t = { 0, NULL };
    static const uint8_t ONE[4] = { 1, 0, 0, 0 };
    static const uint8_t COUNTING[4] = { 1, 2, 3, 4 };
    Expect(&t, GameHashWords(ONE, 0u) == 0x811C9DC5u, "nothing hashes to the basis");
    Expect(&t, GameHashWords(ONE, 4u) == 0x040C5B8Cu, "the word 1");
    Expect(&t, GameHashWords(COUNTING, 4u) == 0x54C8818Cu, "the word 0x04030201, little endian");

    GameWorldStart(&world);
    GameExpectedImage(&world, image);
    uint32_t before[GAME_REGIONS];
    uint32_t after[GAME_REGIONS];
    GameRegionHashes(image, before);
    image[(100u * GAME_TARGET + 100u) * 4u] ^= 1u;
    GameRegionHashes(image, after);
    Expect(&t, before[GAME_REGION_TEXTURE] == after[GAME_REGION_TEXTURE] &&
                   before[GAME_REGION_FILL_RIGHT] == after[GAME_REGION_FILL_RIGHT] &&
                   before[GAME_REGION_FILL_LEFT] == after[GAME_REGION_FILL_LEFT] &&
                   before[GAME_REGION_STREAM] != after[GAME_REGION_STREAM],
           "one bit in the cells' quarter moves that quarter's hash alone");
    // A quarter's hash is its rows in order: the top right one, row by row, by hand.
    uint32_t rows = 0x811C9DC5u;
    for (uint32_t y = 0; y < GAME_HALF; ++y) {
        for (uint32_t x = GAME_HALF; x < GAME_TARGET; ++x) {
            const uint8_t *const p = Pixel(x, y);
            const uint32_t word = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
                                  (uint32_t)p[3] << 24;
            rows = (rows ^ word) * 16777619u;
        }
    }
    Expect(&t, rows == after[GAME_REGION_FILL_RIGHT], "the top right quarter, row by row");
    Expect(&t, strcmp(GameRegionName(GAME_REGION_STREAM), "the uniform buffer quarter") == 0 &&
                   strcmp(GameRegionName(GAME_REGIONS), "no quarter") == 0,
           "the quarters' names");
    Report("game: a quarter's hash is FNV-1a over its pixels, row by row", &t);
}

static void CheckFile(void) {
    Tally t = { 0, NULL };
    // Word 0 of the big file: 1 * 2654435761 ^ 0x5EED1234 = 0xC0DA6B85, little endian.
    Expect(&t, FsRefPatternByte(0) == 0x85u && FsRefPatternByte(1) == 0x6Bu &&
                   FsRefPatternByte(2) == 0xDAu && FsRefPatternByte(3) == 0xC0u,
           "the big file's first word");
    static bool seen[GAME_FILE_CHUNKS];
    memset(seen, 0, sizeof(seen));
    uint32_t distinct = 0;
    for (uint32_t k = 0; k < GAME_FILE_CHUNKS; ++k) {
        const uint32_t c = GameFileChunk(k);
        distinct += c < GAME_FILE_CHUNKS && !seen[c] ? 1u : 0u;
        if (c < GAME_FILE_CHUNKS) {
            seen[c] = true;
        }
    }
    Expect(&t, distinct == GAME_FILE_CHUNKS, "1024 frames read every chunk once");
    Expect(&t, GameFileChunk(1) == 37u && GameFileChunk(GAME_FILE_CHUNKS) == GameFileChunk(0) &&
                   GameFileChunk(MODE_FRAMES_MAX - 1u) < GAME_FILE_CHUNKS,
           "the order, and where it starts again");
    Expect(&t, (uint64_t)GAME_FILE_CHUNK * GAME_FILE_CHUNKS == FS_BIG_SIZE,
           "the chunks are the whole file");
    static const uint32_t CHUNKS[] = { 0u, 37u, GAME_FILE_CHUNKS - 1u };
    for (size_t i = 0; i < sizeof(CHUNKS) / sizeof(CHUNKS[0]); ++i) {
        const uint64_t offset = (uint64_t)CHUNKS[i] * GAME_FILE_CHUNK;
        for (uint32_t b = 0; b < GAME_FILE_CHUNK; ++b) {
            chunk[b] = FsRefPatternByte(offset + b);
        }
        Expect(&t, GameHashWords(chunk, GAME_FILE_CHUNK) == GameFileChunkHash(CHUNKS[i]),
               "a chunk's hash from its bytes and from its words");
    }
    Report("game: the file thread reads the whole file in 1024 frames, each chunk's hash the "
           "pattern's",
           &t);
}

static bool StatsAre(const GameFrameStats *s, uint32_t frames, uint32_t mean, uint32_t p99,
                     uint32_t longest, uint32_t longest_at, uint32_t hitches) {
    return s->frames == frames && s->mean == mean && s->p99 == p99 && s->longest == longest &&
           s->longest_at == longest_at && s->hitches == hitches;
}

static void CheckStats(void) {
    Tally t = { 0, NULL };
    GameFrameStats s;

    // 99 frames at one frame and one at three: the mean 1700033 / 100 rounds to 17000, the
    // 99th of 100 is the 99th smallest, a frame's time, and the slow one is the hitch.
    for (uint32_t k = 0; k < 100u; ++k) {
        times[k] = k == 99u ? 50000u : GAME_FRAME_US;
    }
    GameFrameStatsOf(times, 100u, scratch, &s);
    Expect(&t, StatsAre(&s, 100u, 17000u, GAME_FRAME_US, 50000u, 99u, 1u) && s.hitch_at[0] == 99u,
           "a hundred frames with one slow one");

    times[0] = 1234u;
    GameFrameStatsOf(times, 1u, scratch, &s);
    Expect(&t, StatsAre(&s, 1u, 1234u, 1234u, 1234u, 0u, 0u), "one frame");

    // 600 frames: the first at 40000, frames 100 to 105 at 33334, the rest at one frame. The
    // 99th percentile is the 594th smallest: the 593 short ones come first, so it is 33334.
    for (uint32_t k = 0; k < 600u; ++k) {
        times[k] = k == 0u ? 40000u : (k >= 100u && k <= 105u ? 33334u : GAME_FRAME_US);
    }
    GameFrameStatsOf(times, 600u, scratch, &s);
    const uint32_t mean = (uint32_t)((40000u + 6u * 33334u + 593u * GAME_FRAME_US + 300u) / 600u);
    Expect(&t, StatsAre(&s, 600u, mean, 33334u, 40000u, 0u, 7u) && s.hitch_at[0] == 0u &&
                   s.hitch_at[1] == 100u && s.hitch_at[6] == 105u,
           "600 frames with seven hitches");
    Expect(&t, times[0] == 40000u && times[100] == 33334u, "the times are left in their order");

    // Two frames, one exactly at the hitch line, which is not over it.
    times[0] = GAME_HITCH_US;
    times[1] = GAME_HITCH_US + 1u;
    GameFrameStatsOf(times, 2u, scratch, &s);
    Expect(&t, s.hitches == 1u && s.hitch_at[0] == 1u && s.mean == GAME_HITCH_US + 1u,
           "the line itself, and a mean of a half rounded up");

    // Eleven hitches, the even frames and the last: the first eight named, every one counted.
    for (uint32_t k = 0; k < 20u; ++k) {
        times[k] = k % 2u == 0u ? 50000u : 1000u;
    }
    times[19] = 60000u;
    GameFrameStatsOf(times, 20u, scratch, &s);
    Expect(&t, s.hitches == 11u && s.hitch_at[7] == 14u && s.longest == 60000u &&
                   s.longest_at == 19u,
           "more hitches than are named");

    GameFrameStatsOf(times, 0u, scratch, &s);
    Expect(&t, StatsAre(&s, 0u, 0u, 0u, 0u, 0u, 0u) && s.total == 0u, "no frames");
    Report("game: frame times sum up to a mean, a 99th percentile, the longest and the hitches",
           &t);
}

static void CheckNote(void) {
    Tally t = { 0, NULL };
    GameFrameStats s;
    memset(&s, 0, sizeof(s));
    s.frames = 600u;
    s.total = 10012345u;
    s.mean = 16687u;
    s.p99 = 17012u;
    s.longest = 41000u;
    s.longest_at = 0u;
    s.hitches = 2u;
    s.hitch_at[0] = 0u;
    s.hitch_at[1] = 377u;
    const GameThreadTimes threads = { 2004u, 10200u, 1u, 600u, 1500u };
    char text[REPORT_NOTE_MAX + 1];
    GameDescribeTimes(text, sizeof(text), &s, &threads);
    Expect(&t,
           strcmp(text, "game: 600 frames in 10.01 s; present to present: mean 16687 us, 99th "
                        "percentile 17012 us, longest 41000 us at frame 0; 2 over 33333 us, two "
                        "frames at 60 Hz: frames 0, 377; the audio thread waited for 2004 "
                        "renderer frames, the longest wait 10200 us, 1 over 10000 us; the file "
                        "thread made 600 reads of 64 KiB, the longest 1500 us") == 0,
           text);

    s.hitches = 0u;
    GameDescribeTimes(text, sizeof(text), &s, &threads);
    Expect(&t, strstr(text, "; none over 33333 us, two frames at 60 Hz; the audio") != NULL,
           "no hitches");

    s.hitches = 11u;
    for (uint32_t i = 0; i < GAME_HITCHES_LISTED; ++i) {
        s.hitch_at[i] = i * 10u;
    }
    GameDescribeTimes(text, sizeof(text), &s, &threads);
    Expect(&t, strstr(text, "11 over 33333 us, two frames at 60 Hz: frames 0, 10, 20, 30, 40, "
                            "50, 60, 70 and 3 more;") != NULL,
           "the hitches past the eighth counted");
    s.hitches = 1u;
    GameDescribeTimes(text, sizeof(text), &s, &threads);
    Expect(&t, strstr(text, "1 over 33333 us, two frames at 60 Hz: frame 0;") != NULL,
           "one hitch");

    // Every number at its widest still fits a note whole.
    const GameFrameStats widest = { MODE_FRAMES_MAX, UINT64_MAX / 2u, UINT32_MAX, UINT32_MAX,
                                    UINT32_MAX, MODE_FRAMES_MAX - 1u, MODE_FRAMES_MAX,
                                    { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
                                      UINT32_MAX, UINT32_MAX, UINT32_MAX } };
    const GameThreadTimes slowest = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
                                      UINT32_MAX };
    Expect(&t, GameDescribeTimes(text, sizeof(text), &widest, &slowest) < REPORT_NOTE_MAX - 1u,
           "the widest note fits a note");

    char tiny[24];
    memset(tiny, 'x', sizeof(tiny));
    const size_t length = GameDescribeTimes(tiny, sizeof(tiny), &s, &threads);
    Expect(&t, length == sizeof(tiny) - 1u && tiny[sizeof(tiny) - 1u] == '\0' &&
                   strncmp(tiny, "game: 600 frames in", 19u) == 0,
           "a buffer too small is cut and terminated");
    Report("game: the timing note says the frames, their times and the hitches by number", &t);
}

void RunReferenceGame(void) {
    CheckWorld();
    CheckFill();
    CheckImage();
    CheckHashes();
    CheckFile();
    CheckStats();
    CheckNote();
}
