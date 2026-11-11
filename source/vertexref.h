// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The vertex suite's rules: the four floats a vertex attribute of a given layout gives the
/// shader from the bytes it reads, the components it lacks filled in, and where each vertex and
/// instance reads them.
///
/// Touches no graphics library, so the reference suite pins all of it with no device present.

/// The attribute layouts the suite reads: the 3D class's sizes and types as deko3d names them.
typedef enum {
    VERTEX_RGBA32F,
    VERTEX_RGB32F,
    VERTEX_RG32F,
    VERTEX_R32F,
    /// Four unsigned normalised bytes, in BGRA order when the attribute says so.
    VERTEX_RGBA8_UNORM,
    VERTEX_RGBA16_SSCALED,
    VERTEX_RGBA8_USCALED,
    VERTEX_RGB10A2_UNORM,
} VertexLayout;

/// The bytes one attribute of @p layout takes.
uint32_t VertexLayoutBytes(VertexLayout layout);

/// The four floats, as bit patterns, an attribute of @p layout gives from the @p bytes it reads,
/// red and blue swapped when @p bgra: a missing y or z is nought and a missing w one (OpenGL's and
/// Vulkan's rule for a vector the format leaves short). Unsigned normalised values are only ever
/// nought or full here, which every conversion gives exactly; false for any other, which the case
/// never feeds.
bool VertexExpected(VertexLayout layout, bool bgra, const uint8_t *bytes, uint32_t out[4]);

/// The byte an attribute at @p offset of vertex @p vertex reads first, in a buffer whose vertices
/// are @p stride bytes apart; for a per instance attribute stepping every @p divisor instances,
/// @p vertex is the instance and the element is instance / divisor.
uint32_t VertexAddress(uint32_t vertex, uint32_t stride, uint32_t offset, uint32_t divisor);

/// The pixel of the 16 by 16 target the point of vertex @p vertex of instance @p instance lands
/// on, as vertex_value.vert.glsl places it: the vertex's number plus eight per instance, row by
/// row.
uint32_t VertexPixel(uint32_t vertex, uint32_t instance);

#ifdef __cplusplus
}
#endif
