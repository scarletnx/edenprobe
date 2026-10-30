// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsref.h"
#include "gameref.h"

// The world's edge, in 16.16 fixed point: 256 whole units, so a position's whole part is a byte.
#define WORLD_LIMIT (256 << 16)

static const char *const REGION_NAMES[GAME_REGIONS] = {
    "the texture quarter",
    "the top right fill quarter",
    "the bottom left fill quarter",
    "the uniform buffer quarter",
};

const char *GameRegionName(GameRegion region) {
    return region < GAME_REGIONS ? REGION_NAMES[region] : "no quarter";
}

/// A hash of a small number, for the world's starting state: every cell somewhere else.
static uint32_t Mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/// A velocity of less than one unit a frame either way, never nought.
static int32_t Velocity(uint32_t seed) {
    const int32_t v = (int32_t)(Mix(seed) % (2u << 16)) - (1 << 16);
    return v != 0 ? v : 1;
}

void GameWorldStart(GameWorld *world) {
    world->frame = 0;
    for (uint32_t d = 0; d < GAME_DRAWS; ++d) {
        world->x[d] = (int32_t)(Mix(d * 4u + 1u) % (uint32_t)WORLD_LIMIT);
        world->y[d] = (int32_t)(Mix(d * 4u + 2u) % (uint32_t)WORLD_LIMIT);
        world->vx[d] = Velocity(d * 4u + 3u);
        world->vy[d] = Velocity(d * 4u + 4u);
    }
}

/// One step along one axis, bouncing back in off an edge. A velocity under one unit never
/// carries a position more than one unit past an edge, so one reflection always lands inside.
static void Bounce(int32_t *position, int32_t *velocity) {
    int32_t next = *position + *velocity;
    if (next < 0) {
        next = -next;
        *velocity = -*velocity;
    } else if (next >= WORLD_LIMIT) {
        next = 2 * (WORLD_LIMIT - 1) - next;
        *velocity = -*velocity;
    }
    *position = next;
}

void GameWorldStep(GameWorld *world) {
    for (uint32_t d = 0; d < GAME_DRAWS; ++d) {
        Bounce(&world->x[d], &world->vx[d]);
        Bounce(&world->y[d], &world->vy[d]);
    }
    ++world->frame;
}

uint32_t GameStreamWord(const GameWorld *world, uint32_t draw) {
    const uint32_t x = (uint32_t)world->x[draw] >> 16;
    const uint32_t y = (uint32_t)world->y[draw] >> 16;
    const uint32_t tag = (draw * 37u + world->frame) & 0xFFu;
    return x | y << 8 | tag << 16 | 0xFF000000u;
}

void GameFillColour(uint32_t frame, uint32_t quad, uint8_t out[4]) {
    // Odd steps between quads, so the eight of one frame are eight colours.
    out[0] = (uint8_t)(frame * 3u + quad * 29u + 16u);
    out[1] = (uint8_t)(frame * 5u + quad * 71u + 64u);
    out[2] = (uint8_t)(frame * 11u + quad * 97u);
    out[3] = 0xFFu;
}

uint32_t GameFillNearest(uint32_t frame) {
    return frame % GAME_FILL_QUADS;
}

float GameFillDepth(uint32_t frame, uint32_t quad) {
    const uint32_t place = (quad + GAME_FILL_QUADS - GameFillNearest(frame)) % GAME_FILL_QUADS;
    return (float)(64u + 16u * place) / 256.0f;
}

void GameTexel(uint32_t frame, uint32_t x, uint32_t y, uint8_t out[4]) {
    out[0] = (uint8_t)(x * 16u + 8u);
    out[1] = (uint8_t)(y * 16u + 8u);
    out[2] = (uint8_t)(frame * 5u + x * 3u + y * 11u);
    out[3] = 0xFFu;
}

void GameExpectedImage(const GameWorld *world, uint8_t *pixels) {
    uint8_t fill[4];
    GameFillColour(world->frame, GameFillNearest(world->frame), fill);
    for (uint32_t y = 0; y < GAME_TARGET; ++y) {
        for (uint32_t x = 0; x < GAME_TARGET; ++x) {
            uint8_t *const pixel = pixels + ((size_t)y * GAME_TARGET + x) * 4u;
            if (x < GAME_HALF && y < GAME_HALF) {
                GameTexel(world->frame, x / GAME_TEXEL_PIXELS, y / GAME_TEXEL_PIXELS, pixel);
            } else if (x >= GAME_HALF && y >= GAME_HALF) {
                const uint32_t cx = (x - GAME_HALF) / GAME_CELL_PIXELS;
                const uint32_t cy = (y - GAME_HALF) / GAME_CELL_PIXELS;
                const uint32_t word = GameStreamWord(world, cy * GAME_CELLS + cx);
                pixel[0] = (uint8_t)word;
                pixel[1] = (uint8_t)(word >> 8);
                pixel[2] = (uint8_t)(word >> 16);
                pixel[3] = (uint8_t)(word >> 24);
            } else {
                memcpy(pixel, fill, 4u);
            }
        }
    }
}

#define FNV_BASIS 2166136261u
#define FNV_PRIME 16777619u

static uint32_t HashMore(uint32_t hash, const uint8_t *bytes, size_t count) {
    for (size_t i = 0; i + 4u <= count; i += 4u) {
        const uint32_t word = (uint32_t)bytes[i] | (uint32_t)bytes[i + 1u] << 8 |
                              (uint32_t)bytes[i + 2u] << 16 | (uint32_t)bytes[i + 3u] << 24;
        hash = (hash ^ word) * FNV_PRIME;
    }
    return hash;
}

uint32_t GameHashWords(const uint8_t *bytes, size_t count) {
    return HashMore(FNV_BASIS, bytes, count);
}

void GameRegionHashes(const uint8_t *pixels, uint32_t out[GAME_REGIONS]) {
    for (uint32_t region = 0; region < GAME_REGIONS; ++region) {
        const uint32_t x0 = (region & 1u) != 0u ? GAME_HALF : 0u;
        const uint32_t y0 = (region & 2u) != 0u ? GAME_HALF : 0u;
        uint32_t hash = FNV_BASIS;
        for (uint32_t y = y0; y < y0 + GAME_HALF; ++y) {
            hash = HashMore(hash, pixels + ((size_t)y * GAME_TARGET + x0) * 4u, GAME_HALF * 4u);
        }
        out[region] = hash;
    }
}

uint32_t GameFileChunk(uint32_t frame) {
    // 37 shares no factor with 1024, so 1024 frames in a row read every chunk once.
    return (uint32_t)(((uint64_t)frame * 37u) % GAME_FILE_CHUNKS);
}

uint32_t GameFileChunkHash(uint32_t chunk) {
    const uint64_t first = (uint64_t)chunk * (GAME_FILE_CHUNK / 4u);
    uint32_t hash = FNV_BASIS;
    for (uint64_t i = 0; i < GAME_FILE_CHUNK / 4u; ++i) {
        hash = (hash ^ FsRefPatternWord(first + i)) * FNV_PRIME;
    }
    return hash;
}

static int CompareTimes(const void *a, const void *b) {
    const uint32_t x = *(const uint32_t *)a;
    const uint32_t y = *(const uint32_t *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

void GameFrameStatsOf(const uint32_t *times, uint32_t count, uint32_t *scratch,
                      GameFrameStats *out) {
    memset(out, 0, sizeof(*out));
    if (times == NULL || scratch == NULL || count == 0u) {
        return;
    }
    out->frames = count;
    for (uint32_t k = 0; k < count; ++k) {
        out->total += times[k];
        if (times[k] > out->longest) {
            out->longest = times[k];
            out->longest_at = k;
        }
        if (times[k] > GAME_HITCH_US) {
            if (out->hitches < GAME_HITCHES_LISTED) {
                out->hitch_at[out->hitches] = k;
            }
            ++out->hitches;
        }
    }
    out->mean = (uint32_t)((out->total + count / 2u) / count);
    memcpy(scratch, times, (size_t)count * sizeof(scratch[0]));
    qsort(scratch, count, sizeof(scratch[0]), CompareTimes);
    // The rank is 99 in 100 of the count rounded up, so a run of under a hundred frames has its
    // slowest frame as its 99th percentile rather than one interpolated past it.
    const uint64_t rank = ((uint64_t)count * 99u + 99u) / 100u;
    out->p99 = scratch[rank - 1u];
}

/// Appends to @p out, never past @p out_size, and says whether the whole text fitted.
static bool Append(char *out, size_t out_size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static bool Append(char *out, size_t out_size, const char *fmt, ...) {
    const size_t used = strlen(out);
    if (used + 1u >= out_size) {
        return false;
    }
    va_list args;
    va_start(args, fmt);
    const int wrote = vsnprintf(out + used, out_size - used, fmt, args);
    va_end(args);
    return wrote >= 0 && (size_t)wrote < out_size - used;
}

size_t GameDescribeTimes(char *out, size_t out_size, const GameFrameStats *frames,
                         const GameThreadTimes *threads) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (frames == NULL || threads == NULL) {
        return 0;
    }
    Append(out, out_size,
           "game: %u frames in %.2f s; present to present: mean %u us, 99th percentile %u us, "
           "longest %u us at frame %u; ",
           (unsigned)frames->frames, (double)frames->total / 1e6, (unsigned)frames->mean,
           (unsigned)frames->p99, (unsigned)frames->longest, (unsigned)frames->longest_at);
    if (frames->hitches == 0u) {
        Append(out, out_size, "none over %u us, two frames at 60 Hz", (unsigned)GAME_HITCH_US);
    } else {
        Append(out, out_size, "%u over %u us, two frames at 60 Hz: frame%s",
               (unsigned)frames->hitches, (unsigned)GAME_HITCH_US,
               frames->hitches == 1u ? "" : "s");
        const uint32_t listed =
            frames->hitches < GAME_HITCHES_LISTED ? frames->hitches : GAME_HITCHES_LISTED;
        for (uint32_t i = 0; i < listed; ++i) {
            Append(out, out_size, "%s %u", i == 0u ? "" : ",", (unsigned)frames->hitch_at[i]);
        }
        if (frames->hitches > listed) {
            Append(out, out_size, " and %u more", (unsigned)(frames->hitches - listed));
        }
    }
    Append(out, out_size,
           "; the audio thread waited for %u renderer frames, the longest wait %u us, %u over "
           "10000 us; the file thread made %u reads of 64 KiB, the longest %u us",
           (unsigned)threads->audio_steps, (unsigned)threads->audio_longest,
           (unsigned)threads->audio_late, (unsigned)threads->file_reads,
           (unsigned)threads->file_longest);
    return strlen(out);
}
