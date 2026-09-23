// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include "shaders.h"

// Written by bin2s from the modules uam produced, and included here and nowhere else, so the
// table below is the only place a shader's name appears. A shader added to the directory and not
// to the table leaves its length unused, which this build treats as an error.
#include "edenprobe_shaders.h"

typedef struct {
    const uint8_t *data;
    uint32_t size;
} ShaderBlob;

static const ShaderBlob BLOBS[SHADER_COUNT] = {
    [SHADER_COMMON_VERT] = { common_vert_dksh, (uint32_t)common_vert_dksh_size },
    [SHADER_FLAT_FRAG] = { flat_frag_dksh, (uint32_t)flat_frag_dksh_size },
    [SHADER_INTMATH_FRAG] = { intmath_frag_dksh, (uint32_t)intmath_frag_dksh_size },
    [SHADER_TEXTURE_FRAG] = { texture_frag_dksh, (uint32_t)texture_frag_dksh_size },
    [SHADER_FMATH_FRAG] = { fmath_frag_dksh, (uint32_t)fmath_frag_dksh_size },
    [SHADER_UNIFORM_FRAG] = { uniform_frag_dksh, (uint32_t)uniform_frag_dksh_size },
    [SHADER_ALIAS_FRAG] = { alias_frag_dksh, (uint32_t)alias_frag_dksh_size },
    [SHADER_PATTERN_FRAG] = { pattern_frag_dksh, (uint32_t)pattern_frag_dksh_size },
    [SHADER_FORMAT_FLOAT_FRAG] = { format_float_frag_dksh, (uint32_t)format_float_frag_dksh_size },
    [SHADER_FORMAT_UINT_FRAG] = { format_uint_frag_dksh, (uint32_t)format_uint_frag_dksh_size },
    [SHADER_FORMAT_SINT_FRAG] = { format_sint_frag_dksh, (uint32_t)format_sint_frag_dksh_size },
    [SHADER_INTCONV_FRAG] = { intconv_frag_dksh, (uint32_t)intconv_frag_dksh_size },
    [SHADER_INTSHIFT_FRAG] = { intshift_frag_dksh, (uint32_t)intshift_frag_dksh_size },
    [SHADER_INTDIV_FRAG] = { intdiv_frag_dksh, (uint32_t)intdiv_frag_dksh_size },
    [SHADER_INTREM_FRAG] = { intrem_frag_dksh, (uint32_t)intrem_frag_dksh_size },
    [SHADER_INTBITS_FRAG] = { intbits_frag_dksh, (uint32_t)intbits_frag_dksh_size },
};

const void *ShaderData(ShaderId id) {
    if ((unsigned)id >= SHADER_COUNT) {
        return NULL;
    }
    return BLOBS[id].data;
}

uint32_t ShaderSize(ShaderId id) {
    if ((unsigned)id >= SHADER_COUNT) {
        return 0u;
    }
    return BLOBS[id].size;
}
