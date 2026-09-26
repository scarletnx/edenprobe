// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The compiled shader modules carried inside the binary.
///
/// Compiled by uam at build time and turned into read-only data, so the file that runs on the
/// console and the file that runs under an emulator carry the same shaders down to the byte.
typedef enum {
    SHADER_COMMON_VERT,
    SHADER_FLAT_FRAG,
    SHADER_INTMATH_FRAG,
    SHADER_TEXTURE_FRAG,
    SHADER_FMATH_FRAG,
    SHADER_UNIFORM_FRAG,
    SHADER_ALIAS_FRAG,
    SHADER_PATTERN_FRAG,
    SHADER_FORMAT_FLOAT_FRAG,
    SHADER_FORMAT_UINT_FRAG,
    SHADER_FORMAT_SINT_FRAG,
    SHADER_INTCONV_FRAG,
    SHADER_INTSHIFT_FRAG,
    SHADER_INTDIV_FRAG,
    SHADER_INTREM_FRAG,
    SHADER_INTBITS_FRAG,
    SHADER_ORDER_COPY_COMP,
    SHADER_ORDER_MARK_COMP,
    SHADER_POSITION_VERT,
    SHADER_INSTANCE_VERT,
    SHADER_VALUE_UBO_FRAG,
    SHADER_VALUE_SSBO_FRAG,
    SHADER_STORE_SSBO_FRAG,
    SHADER_BINDLESS_FRAG,
    SHADER_FETCH_FRAG,
    SHADER_SWIZZLE_FRAG,
    SHADER_TEXEL_FRAG,
    SHADER_VALUE_UBO_ALL_FRAG,
    SHADER_RING_VERT,
    SHADER_RING_FRAG,
    SHADER_COUNT,
} ShaderId;

/// The module's bytes, header included, as the compiler emitted them. NULL for a bad id.
const void *ShaderData(ShaderId id);

/// How many bytes ShaderData returned. Zero for a bad id.
uint32_t ShaderSize(ShaderId id);

#ifdef __cplusplus
}
#endif
