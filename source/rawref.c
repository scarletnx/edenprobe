// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include "rawref.h"

// Submission modes, deko3d source/maxwell/command.h.
#define RAW_MODE_INCREASING 1u
#define RAW_MODE_INLINE 4u

static uint32_t Field(uint32_t value, uint32_t position, uint32_t size) {
    return (value & ((1u << size) - 1u)) << position;
}

uint32_t RawIncreasingHeader(uint32_t subchannel, uint32_t method, uint32_t count) {
    return Field(method, 0, 13) | Field(subchannel, 13, 3) | Field(count, 16, 13) |
           Field(RAW_MODE_INCREASING, 29, 3);
}

uint32_t RawInlineHeader(uint32_t subchannel, uint32_t method, uint32_t value) {
    return Field(method, 0, 13) | Field(subchannel, 13, 3) | Field(value, 16, 13) |
           Field(RAW_MODE_INLINE, 29, 3);
}

size_t RawIndexLimit(uint32_t out[3], uint64_t limit) {
    out[0] = RawIncreasingHeader(RAW_SUBCHANNEL_3D, RAW_3D_INDEX_ARRAY_LIMIT, 2);
    out[1] = (uint32_t)(limit >> 32);
    out[2] = (uint32_t)limit;
    return 3;
}

size_t RawDrawTexture(uint32_t out[13], uint32_t texture, uint32_t sampler, uint32_t dst_x,
                      uint32_t dst_y, uint32_t width, uint32_t height, uint32_t src_x,
                      uint32_t src_y) {
    out[0] = RawIncreasingHeader(RAW_SUBCHANNEL_3D, RAW_3D_DRAW_TEXTURE, 12);
    out[1] = dst_x << 12;
    out[2] = dst_y << 12;
    out[3] = width << 12;
    out[4] = height << 12;
    // One texel per pixel on each axis: 1.0 in 32.32 is a low word of nought and a high of one.
    out[5] = 0u;
    out[6] = 1u;
    out[7] = 0u;
    out[8] = 1u;
    out[9] = sampler;
    out[10] = texture;
    out[11] = src_x << 12;
    out[12] = src_y << 12;
    return 13;
}

size_t RawRenderEnable(uint32_t out[4], uint64_t address, uint32_t mode) {
    out[0] = RawIncreasingHeader(RAW_SUBCHANNEL_3D, RAW_3D_RENDER_ENABLE, 3);
    out[1] = (uint32_t)(address >> 32);
    out[2] = (uint32_t)address;
    out[3] = mode;
    return 4;
}

uint32_t RawInstanceArrayWord(uint32_t topology, uint32_t first, uint32_t count) {
    return Field(first, 0, 16) | Field(count, 16, 12) | Field(topology, 28, 4);
}

uint64_t RawTightIndexLimit(uint64_t start, uint32_t first, uint32_t count, uint32_t bytes) {
    return start + ((uint64_t)first + count) * bytes - 1u;
}
