// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
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
    SHADER_COUNT,
} ShaderId;

/// The module's bytes, header included, as the compiler emitted them. NULL for a bad id.
const void *ShaderData(ShaderId id);

/// How many bytes ShaderData returned. Zero for a bad id.
uint32_t ShaderSize(ShaderId id);

#ifdef __cplusplus
}
#endif
