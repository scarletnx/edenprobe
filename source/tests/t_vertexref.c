// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <string.h>

#include "../check.h"
#include "../report.h"
#include "../vertexref.h"
#include "tests.h"

// The vertex suite's rules, against values worked out by hand: the floats each layout gives from
// its bytes and the components it lacks, BGRA's swap, and where a vertex and an instance read.

static const char *const SUITE = "reference";

static bool Same(VertexLayout layout, bool bgra, const uint8_t *bytes, uint32_t x, uint32_t y,
                 uint32_t z, uint32_t w) {
    uint32_t out[4];
    return VertexExpected(layout, bgra, bytes, out) && out[0] == x && out[1] == y && out[2] == z &&
           out[3] == w;
}

void RunReferenceVertex(void) {
    // 2.0 and -0.5 as an RG32F attribute: z nought and w one. The same bytes as R32F: 2.0, 0, 0, 1.
    static const uint8_t RG[8] = { 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0xbf };
    // Bytes full, nought, nought, full in BGRA: red is the third byte's nought, blue the first's
    // one.
    static const uint8_t BGRA[4] = { 0xff, 0x00, 0x00, 0xff };
    // -32768 is -2^15, 0xc7000000; 1234 is 0x449a4000; -1 is 0xbf800000; 32767 is 0x46fffe00.
    static const uint8_t SSCALED[8] = { 0x00, 0x80, 0xd2, 0x04, 0xff, 0xff, 0xff, 0x7f };
    // 255 is 0x437f0000, 1 is 0x3f800000, 128 is 0x43000000.
    static const uint8_t USCALED[4] = { 0xff, 0x01, 0x80, 0x00 };
    // x and z full, y nought, w 3: one, nought, one, one.
    static const uint8_t PACKED[4] = { 0xff, 0x03, 0xf0, 0xff };
    static const uint8_t HALFWAY[4] = { 0x80, 0x00, 0x00, 0x00 };
    uint32_t unused[4];
    const bool layouts =
        Same(VERTEX_RG32F, false, RG, 0x40000000u, 0xbf000000u, 0u, 0x3f800000u) &&
        Same(VERTEX_R32F, false, RG, 0x40000000u, 0u, 0u, 0x3f800000u) &&
        Same(VERTEX_RGBA8_UNORM, true, BGRA, 0u, 0u, 0x3f800000u, 0x3f800000u) &&
        Same(VERTEX_RGBA16_SSCALED, false, SSCALED, 0xc7000000u, 0x449a4000u, 0xbf800000u,
             0x46fffe00u) &&
        Same(VERTEX_RGBA8_USCALED, false, USCALED, 0x437f0000u, 0x3f800000u, 0x43000000u, 0u) &&
        Same(VERTEX_RGB10A2_UNORM, false, PACKED, 0x3f800000u, 0u, 0x3f800000u, 0x3f800000u) &&
        !VertexExpected(VERTEX_RGBA8_UNORM, false, HALFWAY, unused) &&
        VertexLayoutBytes(VERTEX_RGB32F) == 12u && VertexLayoutBytes(VERTEX_RGBA16_SSCALED) == 8u;
    const bool places = VertexAddress(3, 69, 0, 0) == 207u && VertexAddress(2, 32, 40, 0) == 104u &&
                        VertexAddress(5, 16, 0, 3) == 16u && VertexAddress(8, 16, 0, 3) == 32u &&
                        VertexPixel(3, 0) == 3u && VertexPixel(0, 2) == 16u;
    CHECK_TRUE(SUITE,
               "the vertex suite's layouts give hand-worked floats and its vertices read where "
               "worked out by hand",
               CHECK_NONE, layouts && places, "layouts %d, places %d", layouts, places);
}
