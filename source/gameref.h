// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The game family's scene, worked out on the processor: what every frame draws, what the file
/// thread reads, and what the frame times add up to. No libnx, so the host build checks all of it
/// before the console build draws a frame.
///
/// Every frame is a function of its number and nothing else. The world the main thread steps,
/// the colours of the fill quads, the texels uploaded and the words in the uniform buffers all
/// come from here, so the image a frame should leave, and the bytes each read should bring, are
/// known to the bit whatever the frames took.

/// The offscreen target the scene draws into, square, and its four quarters: the texture in the
/// top left, the fill quads' colour in the top right and the bottom left, the uniform buffer
/// cells in the bottom right.
#define GAME_TARGET 128u
#define GAME_HALF (GAME_TARGET / 2u)
#define GAME_IMAGE_BYTES (GAME_TARGET * GAME_TARGET * 4u)

typedef enum {
    GAME_REGION_TEXTURE,
    GAME_REGION_FILL_RIGHT,
    GAME_REGION_FILL_LEFT,
    GAME_REGION_STREAM,
    GAME_REGIONS,
} GameRegion;

/// What each quarter is called on a line.
const char *GameRegionName(GameRegion region);

/// The full target quads drawn first, each depth tested and written; the one nearest the eye is
/// not the last one drawn, so a depth test that lets everything through leaves another colour.
#define GAME_FILL_QUADS 8u

/// The texture, uploaded every frame, and how many pixels of the target each texel covers.
#define GAME_TEXTURE 16u
#define GAME_TEXEL_PIXELS (GAME_HALF / GAME_TEXTURE)

/// The bottom right quarter as cells of 2 by 2 pixels, one draw each, each draw reading a
/// uniform buffer of its own that the main thread rewrites every frame.
#define GAME_CELL_PIXELS 2u
#define GAME_CELLS (GAME_HALF / GAME_CELL_PIXELS)
#define GAME_DRAWS (GAME_CELLS * GAME_CELLS)
/// Bytes of each draw's uniform buffer: entries of 16 bytes, each holding the draw's word, which
/// value_ubo_all.frag.glsl reads at an index the pixel picks.
#define GAME_UBO_BYTES 1024u
#define GAME_UBO_ENTRIES (GAME_UBO_BYTES / 16u)

/// What the file thread reads each frame, from the fs family's big file (fsref.h).
#define GAME_FILE_CHUNK (64u * 1024u)
#define GAME_FILE_CHUNKS 1024u

/// One frame at 60 Hz, and a hitch: a frame over two of them, as a player sees a stutter.
#define GAME_FRAME_US 16667u
#define GAME_HITCH_US 33333u
/// How many hitches a line names by frame number before it says how many more.
#define GAME_HITCHES_LISTED 8u

/// The world the main thread steps every frame: a cell's position, in 16.16 fixed point inside
/// 256 by 256, and its velocity, bouncing off the edges. Integer arithmetic only, so the processor
/// that checks the frames steps it exactly as the one that ran them.
typedef struct {
    uint32_t frame;
    int32_t x[GAME_DRAWS];
    int32_t y[GAME_DRAWS];
    int32_t vx[GAME_DRAWS];
    int32_t vy[GAME_DRAWS];
} GameWorld;

/// The world as frame 0 draws it.
void GameWorldStart(GameWorld *world);

/// From the frame the world is at to the next one.
void GameWorldStep(GameWorld *world);

/// The word cell @p draw shows in the frame @p world is at: its position's two whole parts, a
/// byte from its number and the frame, and an opaque alpha.
uint32_t GameStreamWord(const GameWorld *world, uint32_t draw);

/// Fill quad @p quad's colour in frame @p frame.
void GameFillColour(uint32_t frame, uint32_t quad, uint8_t out[4]);

/// Which fill quad is nearest the eye in frame @p frame, and so the colour the fill leaves.
uint32_t GameFillNearest(uint32_t frame);

/// Fill quad @p quad's depth in frame @p frame: a binary fraction the transform carries exactly,
/// 0.25 for the nearest and a sixteenth further for each place after it.
float GameFillDepth(uint32_t frame, uint32_t quad);

/// Texel @p x, @p y of the texture frame @p frame uploads.
void GameTexel(uint32_t frame, uint32_t x, uint32_t y, uint8_t out[4]);

/// The image frame @p frame leaves in the target, GAME_IMAGE_BYTES in row order, the top row
/// first, from @p world at that frame.
void GameExpectedImage(const GameWorld *world, uint8_t *pixels);

/// FNV-1a over little-endian 32-bit words: @p count bytes, a multiple of four.
uint32_t GameHashWords(const uint8_t *bytes, size_t count);

/// The hash of each quarter of an image laid out as GameExpectedImage lays one out, its rows in
/// order, each pixel one word.
void GameRegionHashes(const uint8_t *pixels, uint32_t out[GAME_REGIONS]);

/// Which 64 KiB of the big file frame @p frame reads: every chunk once in 1024 frames, in an
/// order that jumps around the file as a game streaming its assets does.
uint32_t GameFileChunk(uint32_t frame);

/// GameHashWords of chunk @p chunk as the fs family wrote it, from fsref.h's pattern.
uint32_t GameFileChunkHash(uint32_t chunk);

/// What the frame times of a run of the scene add up to, in microseconds.
typedef struct {
    uint32_t frames;
    uint64_t total;
    /// Rounded to the nearest microsecond.
    uint32_t mean;
    /// Nearest rank: the smallest time at least 99 in 100 frames took no longer than.
    uint32_t p99;
    uint32_t longest;
    uint32_t longest_at;
    /// Frames over GAME_HITCH_US, and the first GAME_HITCHES_LISTED of them by number.
    uint32_t hitches;
    uint32_t hitch_at[GAME_HITCHES_LISTED];
} GameFrameStats;

/// The stats of @p count frame times; @p scratch holds @p count values and is sorted in its
/// place. All zeros for none.
void GameFrameStatsOf(const uint32_t *times, uint32_t count, uint32_t *scratch,
                      GameFrameStats *out);

/// What the scene's other threads timed, for its note.
typedef struct {
    /// Renderer frames the audio thread waited for, the longest wait between two, and how many
    /// waits were over two renderer frames (10 ms), each a stretch the renderer had no update.
    uint32_t audio_steps;
    uint32_t audio_longest;
    uint32_t audio_late;
    /// Reads the file thread made and the longest of them.
    uint32_t file_reads;
    uint32_t file_longest;
} GameThreadTimes;

/// The note a timed run of the scene ends on, without the braces around it: the frames, their
/// times, the hitches by number, and what the audio and file threads waited. Returns the length;
/// the text is always terminated, and cut short rather than overrun.
size_t GameDescribeTimes(char *out, size_t out_size, const GameFrameStats *frames,
                         const GameThreadTimes *threads);

#ifdef __cplusplus
}
#endif
