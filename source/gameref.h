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

/// What one measure of a run's frames adds up to, in microseconds.
typedef struct {
    uint32_t frames;
    uint64_t total;
    /// Rounded to the nearest microsecond.
    uint32_t mean;
    /// Nearest rank, the ceiling of 99 in 100 of the frames: the smallest time at least 99 in
    /// 100 of them took no longer than. Under 100 frames that rank is the last, the longest.
    uint32_t p99;
    /// The longest, and the first frame that took it.
    uint32_t longest;
    uint32_t longest_at;
    /// Frames over GAME_HITCH_US, and the first GAME_HITCHES_LISTED of them by number.
    uint32_t hitches;
    uint32_t hitch_at[GAME_HITCHES_LISTED];
} GameFrameStats;

/// The stats of frames @p first to @p count - 1 of @p times, numbered as they are there;
/// @p scratch holds @p count values and is sorted in its place. All zeros for none.
void GameFrameStatsOf(const uint32_t *times, uint32_t first, uint32_t count, uint32_t *scratch,
                      GameFrameStats *out);

/// Frame 0 is the warm-up, left out of every measure, as a drawing suite's warm-up pass is: it
/// pays for the device's first work, and in a run of 600 frames it alone moves a mean by a
/// percent. The measures start at the next.
#define GAME_WARMUP_FRAMES 1u

/// The audio thread: the tone is read once the voice has played this many samples (100 renderer
/// frames, as the audio family settles), and the thread waits at most this long for that once
/// the frames are over, as the audio family's PlayUntil does.
#define GAME_AUDIO_SETTLE 24000u
#define GAME_AUDIO_LIMIT_MS 5000u

typedef enum {
    GAME_AUDIO_GO_ON,
    /// The frames are over and the voice has played GAME_AUDIO_SETTLE samples.
    GAME_AUDIO_SETTLED,
    /// The frames are over and GAME_AUDIO_LIMIT_MS passed before it had.
    GAME_AUDIO_LATE,
} GameAudioNext;

/// What the audio thread does after a renderer step: @p over says the frames are over, @p played
/// the samples the voice has played, @p since_over_ms how long ago the frames ended.
GameAudioNext GameAudioAfterStep(bool over, uint64_t played, uint64_t since_over_ms);

/// What the scene's other threads timed.
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

/// Everything a run's times come to: the warm-up frame's own three times, and the three measures
/// of the frames past it. Present to present runs from one present queued to the next; the main
/// thread's part from taking the frame's slot to handing the frame over; the render thread's from
/// the image in hand to the queue standing idle after the frame's submit, so the graphics unit's
/// work is in it and no wait for the display is.
typedef struct {
    uint32_t frames;
    uint32_t warmup_present;
    uint32_t warmup_main;
    uint32_t warmup_render;
    GameFrameStats present;
    GameFrameStats main_part;
    GameFrameStats render_part;
    GameThreadTimes threads;
} GameTimes;

/// The stats of a run of @p frames frames: the warm-up's times taken apart, each measure over the
/// frames past it. @p scratch holds @p frames values. The threads' times are left at nought, for
/// the caller to fill in.
void GameTimesOf(GameTimes *out, uint32_t frames, const uint32_t *present_us,
                 const uint32_t *main_us, const uint32_t *render_us, uint32_t *scratch);

/// The timed cases a run reports, in order: present to present's mean, 99th percentile and
/// longest, then the main thread's part's mean and 99th percentile, then the render thread's.
#define GAME_TIME_CASES 7u
#define GAME_TIME_NAME_MAX 96u

typedef struct {
    char name[GAME_TIME_NAME_MAX];
    uint32_t us;
} GameTimeCase;

/// The timed cases of @p times, names carrying the frames; with @p times NULL, the names alone
/// for a run of @p frames frames and every time nought. Returns how many frames past the warm-up
/// the times are over, nought when there is none to time.
uint32_t GameTimeCases(const GameTimes *times, uint32_t frames,
                       GameTimeCase out[GAME_TIME_CASES]);

/// The two notes a timed run of the scene ends on, without the braces around them, each whole
/// in a note at the widest its numbers can be. The frames: how many, the warm-up left out and
/// what it took, present to present past it and its hitches by number. The work: the main and
/// render threads' parts past the warm-up, and what the audio and file threads waited. Each
/// returns the length; the text is always terminated, and cut short rather than overrun.
size_t GameDescribeFrames(char *out, size_t out_size, const GameTimes *times);
size_t GameDescribeWork(char *out, size_t out_size, const GameTimes *times);

#ifdef __cplusplus
}
#endif
