// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "blit2dref.h"
#include "coherref.h"
#include "md5.h"
#include "rawref.h"

#define ONE (1ull << 32)

static const Blit2dCase CASES[BLIT2D_CASES] = {
    { "blit2d_corner_half", "half size from the corner origin, into a window cut mid-run",
      BLIT2D_ORIGIN_CORNER, 2u * ONE },
    { "blit2d_center_half", "half size from the centre origin, into the same window", 0u,
      2u * ONE },
    { "blit2d_corner_2_3", "two thirds size from the corner origin, 1.5 texels a pixel",
      BLIT2D_ORIGIN_CORNER, ONE + ONE / 2u },
    { "blit2d_center_2_3", "two thirds size from the centre origin, 1.5 texels a pixel", 0u,
      ONE + ONE / 2u },
};

const Blit2dCase *Blit2dCaseAt(Blit2dCaseId id) {
    return (unsigned)id < BLIT2D_CASES ? &CASES[id] : NULL;
}

uint32_t Blit2dSourceTexel(uint32_t x, uint32_t y) {
    return 0x10000000u | y << 12 | x;
}

static void Put(uint8_t *at, uint32_t word) {
    for (uint32_t i = 0; i < 4u; ++i) {
        at[i] = (uint8_t)(word >> (8u * i));
    }
}

uint32_t Blit2dDstOffset(uint32_t x, uint32_t y) {
    return CohBlockLinearOffset(x * 4u, y, BLIT2D_DST_WIDTH * 4u, BLIT2D_BLOCK_GOBS);
}

void Blit2dFillSource(uint8_t *bytes) {
    for (uint32_t y = 0; y < BLIT2D_SRC_HEIGHT; ++y) {
        for (uint32_t x = 0; x < BLIT2D_SRC_WIDTH; ++x) {
            Put(bytes + CohBlockLinearOffset(x * 4u, y, BLIT2D_SRC_WIDTH * 4u, BLIT2D_BLOCK_GOBS),
                Blit2dSourceTexel(x, y));
        }
    }
}

int32_t Blit2dTexel(const Blit2dCase *c, uint32_t answers, uint32_t n, uint32_t extent) {
    const bool corner = (c->origin == BLIT2D_ORIGIN_CORNER) != ((answers & BLIT2D_Q1_B) != 0u);
    if ((answers & BLIT2D_Q2_B) == 0u) {
        // The engine's own walk: start, plus half a step for the centre, plus n steps.
        const uint64_t at = (corner ? 0u : c->du / 2u) + (uint64_t)n * c->du;
        return (int32_t)(at >> 32);
    }
    // Eden's Blit (fermi_2d.cpp:79-96) as it reads: the shifts on a signed 32.32 start, then
    // its blit shader's nearest sample at each pixel's centre across the whole texel span.
    const int64_t start = corner ? -(int64_t)((c->du >> 33) << 32) : 0;
    const int64_t first = start >> 32;
    const int64_t last = (start + (int64_t)c->du * (int64_t)extent) >> 32;
    return (int32_t)(first + (int64_t)(2u * n + 1u) * (last - first) / (2 * (int64_t)extent));
}

void Blit2dExpected(const Blit2dCase *c, uint32_t answers, uint8_t *out) {
    memset(out, BLIT2D_FILL, BLIT2D_DST_BYTES);
    for (uint32_t j = 0; j < BLIT2D_WIN_HEIGHT; ++j) {
        const int32_t ty = Blit2dTexel(c, answers, j, BLIT2D_WIN_HEIGHT);
        for (uint32_t i = 0; i < BLIT2D_WIN_WIDTH; ++i) {
            const int32_t tx = Blit2dTexel(c, answers, i, BLIT2D_WIN_WIDTH);
            Put(out + Blit2dDstOffset(BLIT2D_WIN_X + i, BLIT2D_WIN_Y + j),
                Blit2dSourceTexel((uint32_t)tx, (uint32_t)ty));
        }
    }
}

void Blit2dMd5Hex(const void *bytes, size_t size, char out[33]) {
    MD5_CTX ctx;
    unsigned char digest[16];
    MD5_Init(&ctx);
    MD5_Update(&ctx, bytes, (unsigned long)size);
    MD5_Final(digest, &ctx);
    for (uint32_t i = 0; i < 16u; ++i) {
        snprintf(out + 2u * i, 3, "%02x", digest[i]);
    }
}

size_t Blit2dRawWords(const Blit2dCase *c, uint64_t src_iova, uint64_t dst_iova, uint32_t *out) {
    size_t n = 0;
    out[n++] = RawInlineHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_OPERATION, BLIT2D_OPERATION_SRCCOPY);
    out[n++] = RawInlineHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_CLIP_ENABLE, 0u);
    out[n++] = RawIncreasingHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_SRC_FORMAT, 4u);
    out[n++] = BLIT2D_FORMAT_A8B8G8R8;
    out[n++] = 0u; // block linear
    out[n++] = BLIT2D_BLOCK_SIZE_TWO_GOBS;
    out[n++] = 1u; // depth
    out[n++] = RawIncreasingHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_SRC_WIDTH, 4u);
    out[n++] = BLIT2D_SRC_WIDTH;
    out[n++] = BLIT2D_SRC_HEIGHT;
    out[n++] = (uint32_t)(src_iova >> 32);
    out[n++] = (uint32_t)src_iova;
    out[n++] = RawIncreasingHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_DST_FORMAT, 5u);
    out[n++] = BLIT2D_FORMAT_A8B8G8R8;
    out[n++] = 0u;
    out[n++] = BLIT2D_BLOCK_SIZE_TWO_GOBS;
    out[n++] = 1u;
    out[n++] = 0u; // layer
    out[n++] = RawIncreasingHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_DST_WIDTH, 4u);
    out[n++] = BLIT2D_DST_WIDTH;
    out[n++] = BLIT2D_DST_HEIGHT;
    out[n++] = (uint32_t)(dst_iova >> 32);
    out[n++] = (uint32_t)dst_iova;
    out[n++] = RawInlineHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_COMPRESSION, 1u);
    out[n++] = RawInlineHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_SAMPLE_MODE, c->origin);
    out[n++] = RawIncreasingHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_DST_X0, 12u);
    out[n++] = BLIT2D_WIN_X;
    out[n++] = BLIT2D_WIN_Y;
    out[n++] = BLIT2D_WIN_WIDTH;
    out[n++] = BLIT2D_WIN_HEIGHT;
    for (uint32_t axis = 0; axis < 2u; ++axis) {
        out[n++] = (uint32_t)c->du;
        out[n++] = (uint32_t)(c->du >> 32);
    }
    for (uint32_t i = 0; i < 4u; ++i) {
        out[n++] = 0u; // the source origin, 0.0 on both axes; the last word launches
    }
    return n;
}
