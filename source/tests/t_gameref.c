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
    // Landing on the edge itself, 16777216, is past the last place: it reflects to 16777214.
    world.y[3] = (256 << 16) - 300;
    world.vy[3] = 300;
    GameWorldStep(&world);
    Expect(&t, world.y[3] == (256 << 16) - 2 && world.vy[3] == -300, "onto the high edge");
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
    // By hand: quad 0 of frame 0 and of frame 1, so a colour that ignores the frame is caught.
    uint8_t c0[4];
    uint8_t c1[4];
    GameFillColour(0, 0, c0);
    GameFillColour(1, 0, c1);
    Expect(&t, c0[0] == 16u && c0[1] == 64u && c0[2] == 0u && c0[3] == 0xFFu && c1[0] == 19u &&
                   c1[1] == 69u && c1[2] == 11u && c1[3] == 0xFFu,
           "quad 0's colour in frames 0 and 1");
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
    // And one in the top right, x past the half and y not, which only a quarter whose x and y
    // are not swapped tells from the bottom left.
    uint32_t right[GAME_REGIONS];
    image[(10u * GAME_TARGET + 100u) * 4u] ^= 1u;
    GameRegionHashes(image, right);
    Expect(&t, right[GAME_REGION_FILL_RIGHT] != after[GAME_REGION_FILL_RIGHT] &&
                   right[GAME_REGION_FILL_LEFT] == after[GAME_REGION_FILL_LEFT],
           "one bit in the top right quarter moves it, not the bottom left");
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
    Expect(&t, rows == right[GAME_REGION_FILL_RIGHT], "the top right quarter, row by row");
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

/// The stats beside four words a write past the end of the hitch list would land on.
typedef struct {
    GameFrameStats stats;
    uint32_t guard[4];
} Guarded;

static void CheckStats(void) {
    Tally t = { 0, NULL };
    GameFrameStats s;

    // 99 frames at one frame and one at three: the mean 1700033 / 100 rounds to 17000, the
    // 99th of 100 is the 99th smallest, a frame's time, and the slow one is the hitch.
    for (uint32_t k = 0; k < 100u; ++k) {
        times[k] = k == 99u ? 50000u : GAME_FRAME_US;
    }
    GameFrameStatsOf(times, 0u, 100u, scratch, &s);
    Expect(&t, StatsAre(&s, 100u, 17000u, GAME_FRAME_US, 50000u, 99u, 1u) && s.hitch_at[0] == 99u,
           "a hundred frames with one slow one");

    times[0] = 1234u;
    GameFrameStatsOf(times, 0u, 1u, scratch, &s);
    Expect(&t, StatsAre(&s, 1u, 1234u, 1234u, 1234u, 0u, 0u), "one frame");

    // Times k + 1, so the rank reads straight off: the ceiling of 99 in 100 of 60 is 60, the
    // longest; of 160, 159 (158.4 rounded up), one short of it.
    for (uint32_t k = 0; k < 160u; ++k) {
        times[k] = k + 1u;
    }
    GameFrameStatsOf(times, 0u, 60u, scratch, &s);
    Expect(&t, s.p99 == 60u && s.longest == 60u, "the 99th percentile of 60 frames, the longest");
    GameFrameStatsOf(times, 0u, 160u, scratch, &s);
    Expect(&t, s.p99 == 159u && s.longest == 160u, "the 99th percentile of 160 frames, the 159th");

    // 600 frames, the first the warm-up at 40000 and left out; frames 100 to 105 at 33334, the
    // rest at one frame. 599 frames: the 99th percentile's rank is 594 (593.01 rounded up), past
    // the 593 short ones, so 33334.
    for (uint32_t k = 0; k < 600u; ++k) {
        times[k] = k == 0u ? 40000u : (k >= 100u && k <= 105u ? 33334u : GAME_FRAME_US);
    }
    GameFrameStatsOf(times, 1u, 600u, scratch, &s);
    const uint32_t mean = (uint32_t)((6u * 33334u + 593u * GAME_FRAME_US + 299u) / 599u);
    Expect(&t, StatsAre(&s, 599u, mean, 33334u, 33334u, 100u, 6u) && s.hitch_at[0] == 100u &&
                   s.hitch_at[5] == 105u,
           "600 frames past the warm-up with six hitches, numbered as they are");
    Expect(&t, times[0] == 40000u && times[100] == 33334u, "the times are left in their order");

    // Two frames, one exactly at the hitch line, which is not over it.
    times[0] = GAME_HITCH_US;
    times[1] = GAME_HITCH_US + 1u;
    GameFrameStatsOf(times, 0u, 2u, scratch, &s);
    Expect(&t, s.hitches == 1u && s.hitch_at[0] == 1u && s.mean == GAME_HITCH_US + 1u,
           "the line itself, and a mean of a half rounded up");

    // A tie for the longest: the first of them is named.
    times[0] = 5u;
    times[1] = 9u;
    times[2] = 9u;
    GameFrameStatsOf(times, 0u, 3u, scratch, &s);
    Expect(&t, s.longest == 9u && s.longest_at == 1u, "a tie for the longest names the first");

    // Eight hitches fill the list; twelve name the first eight, count all twelve, and write
    // nothing past the list.
    for (uint32_t hitches = 8u; hitches <= 12u; hitches += 4u) {
        Guarded g;
        memset(&g, 0, sizeof(g));
        for (uint32_t k = 0; k < 20u; ++k) {
            times[k] = k < hitches ? 50000u + k : 1000u;
        }
        GameFrameStatsOf(times, 0u, 20u, scratch, &g.stats);
        Expect(&t, g.stats.hitches == hitches && g.stats.hitch_at[7] == 7u && g.guard[0] == 0u &&
                       g.guard[1] == 0u && g.guard[2] == 0u && g.guard[3] == 0u,
               hitches == 8u ? "eight hitches, the list full" : "twelve hitches, eight named");
    }

    GameFrameStatsOf(times, 0u, 0u, scratch, &s);
    Expect(&t, StatsAre(&s, 0u, 0u, 0u, 0u, 0u, 0u) && s.total == 0u, "no frames");
    GameFrameStatsOf(times, 1u, 1u, scratch, &s);
    Expect(&t, StatsAre(&s, 0u, 0u, 0u, 0u, 0u, 0u), "a run of the warm-up alone");
    Report("game: frame times sum up to a mean, a 99th percentile, the longest and the hitches",
           &t);
}

static void CheckAudioLimit(void) {
    Tally t = { 0, NULL };
    Expect(&t, GameAudioAfterStep(false, 0u, 0u) == GAME_AUDIO_GO_ON &&
                   GameAudioAfterStep(false, 99999u, 99999u) == GAME_AUDIO_GO_ON,
           "while the frames run, whatever was played");
    Expect(&t, GameAudioAfterStep(true, GAME_AUDIO_SETTLE, 0u) == GAME_AUDIO_SETTLED &&
                   GameAudioAfterStep(true, GAME_AUDIO_SETTLE - 1u, 0u) == GAME_AUDIO_GO_ON,
           "over, settled at 24000 samples and not one short");
    Expect(&t, GameAudioAfterStep(true, 10u, GAME_AUDIO_LIMIT_MS - 1u) == GAME_AUDIO_GO_ON &&
                   GameAudioAfterStep(true, 10u, GAME_AUDIO_LIMIT_MS) == GAME_AUDIO_LATE &&
                   GameAudioAfterStep(true, GAME_AUDIO_SETTLE, GAME_AUDIO_LIMIT_MS) ==
                       GAME_AUDIO_SETTLED,
           "late at 5000 ms after the frames, unless settled");
    Report("game: the audio thread stops once its tone settled, or 5000 ms after the frames", &t);
}

static void CheckTimeCases(void) {
    Tally t = { 0, NULL };
    static uint32_t present[4] = { 90000u, 16000u, 17000u, 18000u };
    static uint32_t main_part[4] = { 700u, 100u, 300u, 200u };
    static uint32_t render_part[4] = { 80000u, 1000u, 3000u, 2000u };
    GameTimes times;
    GameTimesOf(&times, 4u, present, main_part, render_part, scratch);
    Expect(&t, times.frames == 4u && times.warmup_present == 90000u &&
                   times.warmup_main == 700u && times.warmup_render == 80000u &&
                   times.present.frames == 3u && times.present.mean == 17000u &&
                   times.present.longest == 18000u && times.present.longest_at == 3u &&
                   times.main_part.mean == 200u && times.render_part.p99 == 3000u,
           "the warm-up taken apart, each measure over the three frames past it");
    GameTimeCase cases[GAME_TIME_CASES];
    const uint32_t timed = GameTimeCases(&times, 4u, cases);
    static const struct {
        const char *name;
        uint32_t us;
    } WANT[GAME_TIME_CASES] = {
        { "4 frames, present to present: the mean", 17000u },
        { "4 frames, present to present: the 99th percentile", 18000u },
        { "4 frames, present to present: the longest", 18000u },
        { "4 frames, the main thread's part: the mean", 200u },
        { "4 frames, the main thread's part: the 99th percentile", 300u },
        { "4 frames, the render thread's part, the graphics unit's in it: the mean", 2000u },
        { "4 frames, the render thread's part, the graphics unit's in it: the 99th percentile",
          3000u },
    };
    bool right = timed == 3u;
    for (uint32_t i = 0; i < GAME_TIME_CASES; ++i) {
        right = right && strcmp(cases[i].name, WANT[i].name) == 0 && cases[i].us == WANT[i].us;
    }
    Expect(&t, right, "the seven cases' names and times");
    const uint32_t named = GameTimeCases(NULL, 600u, cases);
    Expect(&t,
           named == 0u &&
               strcmp(cases[2].name, "600 frames, present to present: the longest") == 0 &&
               cases[2].us == 0u,
           "the names alone, for the cases a skipped scene lists");
    GameTimesOf(&times, 1u, present, main_part, render_part, scratch);
    Expect(&t, GameTimeCases(&times, 1u, cases) == 0u && times.warmup_present == 90000u,
           "one frame: the warm-up, and nothing past it to time");
    Report("game: the timed cases carry each measure past the warm-up, in order", &t);
}

static void CheckNote(void) {
    Tally t = { 0, NULL };
    GameTimes g;
    memset(&g, 0, sizeof(g));
    g.frames = 600u;
    g.warmup_present = 99000u;
    g.warmup_main = 1200u;
    g.warmup_render = 96000u;
    g.present.frames = 599u;
    g.present.total = 10012345u - 99000u;
    g.present.mean = 16687u;
    g.present.p99 = 17012u;
    g.present.longest = 41000u;
    g.present.longest_at = 377u;
    g.present.hitches = 2u;
    g.present.hitch_at[0] = 377u;
    g.present.hitch_at[1] = 412u;
    g.main_part.mean = 420u;
    g.main_part.p99 = 610u;
    g.main_part.longest = 900u;
    g.main_part.longest_at = 5u;
    g.render_part.mean = 1100u;
    g.render_part.p99 = 1800u;
    g.render_part.longest = 2500u;
    g.render_part.longest_at = 6u;
    const GameThreadTimes threads = { 2004u, 10200u, 1u, 600u, 1500u };
    g.threads = threads;
    char text[REPORT_NOTE_MAX + 1];
    GameDescribeFrames(text, sizeof(text), &g);
    Expect(&t,
           strcmp(text,
                  "game: 600 frames in 10.01 s; frame 0, the warm-up, left out of the times "
                  "(99000 us present to present, 1200 us the main thread's part, 96000 us the "
                  "render thread's); the 599 past it; present to present: mean 16687 us, 99th "
                  "percentile 17012 us, longest 41000 us at frame 377, 2 over 33333 us, two "
                  "frames at 60 Hz: frames 377, 412") == 0,
           text);
    GameDescribeWork(text, sizeof(text), &g);
    Expect(&t,
           strcmp(text,
                  "game: past the warm-up; the main thread's part: mean 420 us, 99th percentile "
                  "610 us, longest 900 us at frame 5; the render thread's part, the graphics "
                  "unit's in it: mean 1100 us, 99th percentile 1800 us, longest 2500 us at "
                  "frame 6; the audio thread waited for 2004 renderer frames, the longest wait "
                  "10200 us, 1 over 10000 us; the file thread made 600 reads of 64 KiB, the "
                  "longest 1500 us") == 0,
           text);

    g.present.hitches = 0u;
    GameDescribeFrames(text, sizeof(text), &g);
    const size_t none = strlen(text);
    static const char NONE[] = ", none over 33333 us, two frames at 60 Hz";
    Expect(&t, none > sizeof(NONE) - 1u && strcmp(text + none - (sizeof(NONE) - 1u), NONE) == 0,
           "no hitches");
    g.present.hitches = 11u;
    for (uint32_t i = 0; i < GAME_HITCHES_LISTED; ++i) {
        g.present.hitch_at[i] = 10u + i * 10u;
    }
    GameDescribeFrames(text, sizeof(text), &g);
    Expect(&t, strstr(text, "11 over 33333 us, two frames at 60 Hz: frames 10, 20, 30, 40, 50, "
                            "60, 70, 80 and 3 more") != NULL,
           "the hitches past the eighth counted");
    g.present.hitches = 1u;
    GameDescribeFrames(text, sizeof(text), &g);
    Expect(&t, strstr(text, ", 1 over 33333 us, two frames at 60 Hz: frame 10") != NULL,
           "one hitch");

    GameTimes one;
    memset(&one, 0, sizeof(one));
    one.frames = 1u;
    one.warmup_present = 120000u;
    GameDescribeFrames(text, sizeof(text), &one);
    char work[REPORT_NOTE_MAX + 1];
    GameDescribeWork(work, sizeof(work), &one);
    Expect(&t, strstr(text, "game: 1 frame in 0.12 s; frame 0, the warm-up, left out of the "
                            "times (120000 us present to present") == text &&
                   strstr(text, "; no frame past it to time") != NULL &&
                   strstr(work, "game: past the warm-up, no frame to time; the audio") == work,
           "a run of one frame");

    // Every number at its widest, a frame's number at its last, still fits a note whole.
    GameTimes widest;
    memset(&widest, 0xFF, sizeof(widest));
    widest.frames = MODE_FRAMES_MAX;
    widest.present.frames = MODE_FRAMES_MAX - 1u;
    widest.present.total = UINT64_MAX / 4u;
    widest.present.longest_at = widest.main_part.longest_at = MODE_FRAMES_MAX - 1u;
    widest.render_part.longest_at = MODE_FRAMES_MAX - 1u;
    widest.present.hitches = MODE_FRAMES_MAX - 1u;
    for (uint32_t i = 0; i < GAME_HITCHES_LISTED; ++i) {
        widest.present.hitch_at[i] = MODE_FRAMES_MAX - 1u;
    }
    Expect(&t,
           GameDescribeFrames(text, sizeof(text), &widest) < REPORT_NOTE_MAX - 1u &&
               GameDescribeWork(work, sizeof(work), &widest) < REPORT_NOTE_MAX - 1u,
           "the widest notes fit a note each");

    char tiny[24];
    memset(tiny, 'x', sizeof(tiny));
    const size_t length = GameDescribeFrames(tiny, sizeof(tiny), &g);
    Expect(&t, length == sizeof(tiny) - 1u && tiny[sizeof(tiny) - 1u] == '\0' &&
                   strncmp(tiny, "game: 600 frames in", 19u) == 0,
           "a buffer too small is cut and terminated");
    Report("game: the timing notes say the warm-up, each measure and the hitches by number", &t);
}

void RunReferenceGame(void) {
    CheckWorld();
    CheckFill();
    CheckImage();
    CheckHashes();
    CheckFile();
    CheckStats();
    CheckAudioLimit();
    CheckTimeCases();
    CheckNote();
}
