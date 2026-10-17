// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The nvdec suite's VP9 jobs, written by tools/vp9-jobs.py from six libvpx test vectors (which
/// says how each piece was worked out and checked against FFmpeg's decoder): for every frame in
/// decode order, superframes already split, the frame as the stream has it, where the part the
/// engine reads starts, the picture setup and probability table the job hands over, which
/// surfaces it writes and reads, and FFmpeg's MD5s of its image. The sizes below hold every case;
/// the script's tests say so.

#define VP9_CASE_COUNT 6u
/// nvdec_vp9_pic_s, reden's PictureInfo.
#define VP9_SETUP_SIZE 0x100u
/// nvdec_vp9EntropyProbs_t, reden's EntropyProbs.
#define VP9_PROBS_SIZE 0xEA0u
#define VP9_FRAMES_MAX 32u
#define VP9_IMAGES_MAX 30u
#define VP9_SURFACES_MAX 5u
#define VP9_TILES_MAX 1u
#define VP9_BODY_MAX 49064u
#define VP9_WIDTH_MAX 352u
#define VP9_HEIGHT_MAX 288u

typedef struct Vp9Frame {
    /// The frame as the stream has it, both headers first, and its size.
    const uint8_t *data;
    uint32_t size;
    /// Where the part the engine reads starts: past the uncompressed and the compressed header.
    uint32_t body;
    const uint8_t *setup;
    /// The probability table: whole for a case's first job, or what changes from the job
    /// before as offset << 8 | value (none when it does not change).
    const uint8_t *probs;
    const uint32_t *patch;
    uint32_t patch_count;
    /// A show_existing_frame: no job, the image is the surface below as it stands.
    uint8_t existing;
    /// The image is one of the vector's list (a shown frame, or one shown again).
    uint8_t shown;
    /// The surface the job writes, or the one shown again.
    uint8_t surface;
    /// The last, golden and altref surfaces the job reads.
    uint8_t refs[3];
    uint8_t tiles;
    /// Each tile's width and height in superblocks.
    uint16_t tile_sizes[2u * VP9_TILES_MAX];
    /// FFmpeg's MD5 of the image, I420 cropped to the picture: the whole, then Y, U and V.
    uint8_t md5[4][16];
} Vp9Frame;

typedef struct Vp9Case {
    const char *name;
    const char *title;
    const char *vector;
    uint8_t sha256[32];
    uint16_t width;
    uint16_t height;
    uint8_t surfaces;
    uint32_t frame_count;
    const Vp9Frame *frames;
    /// libvpx's MD5 list for the vector, one per image shown.
    uint32_t image_count;
    const uint8_t (*images)[16];
} Vp9Case;

extern const Vp9Case VP9_CASES[VP9_CASE_COUNT];
/// FNV-1a over every job's table, case after case, with the patches applied.
extern const uint32_t VP9_TABLES_FNV;

#ifdef __cplusplus
}
#endif
