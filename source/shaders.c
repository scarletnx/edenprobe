// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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
    [SHADER_ORDER_COPY_COMP] = { order_copy_comp_dksh, (uint32_t)order_copy_comp_dksh_size },
    [SHADER_ORDER_MARK_COMP] = { order_mark_comp_dksh, (uint32_t)order_mark_comp_dksh_size },
    [SHADER_POSITION_VERT] = { position_vert_dksh, (uint32_t)position_vert_dksh_size },
    [SHADER_INSTANCE_VERT] = { instance_vert_dksh, (uint32_t)instance_vert_dksh_size },
    [SHADER_VALUE_UBO_FRAG] = { value_ubo_frag_dksh, (uint32_t)value_ubo_frag_dksh_size },
    [SHADER_VALUE_SSBO_FRAG] = { value_ssbo_frag_dksh, (uint32_t)value_ssbo_frag_dksh_size },
    [SHADER_STORE_SSBO_FRAG] = { store_ssbo_frag_dksh, (uint32_t)store_ssbo_frag_dksh_size },
    [SHADER_BINDLESS_FRAG] = { bindless_frag_dksh, (uint32_t)bindless_frag_dksh_size },
    [SHADER_FETCH_FRAG] = { fetch_frag_dksh, (uint32_t)fetch_frag_dksh_size },
    [SHADER_SWIZZLE_FRAG] = { swizzle_frag_dksh, (uint32_t)swizzle_frag_dksh_size },
    [SHADER_TEXEL_FRAG] = { texel_frag_dksh, (uint32_t)texel_frag_dksh_size },
    [SHADER_VALUE_UBO_ALL_FRAG] = { value_ubo_all_frag_dksh, (uint32_t)value_ubo_all_frag_dksh_size },
    [SHADER_RING_VERT] = { ring_vert_dksh, (uint32_t)ring_vert_dksh_size },
    [SHADER_RING_FRAG] = { ring_frag_dksh, (uint32_t)ring_frag_dksh_size },
    [SHADER_SQUARE_GEOM] = { square_geom_dksh, (uint32_t)square_geom_dksh_size },
    [SHADER_QUAD_TESC] = { quad_tesc_dksh, (uint32_t)quad_tesc_dksh_size },
    [SHADER_QUAD_TESE] = { quad_tese_dksh, (uint32_t)quad_tese_dksh_size },
    [SHADER_STORE_LATE_FRAG] = { store_late_frag_dksh, (uint32_t)store_late_frag_dksh_size },
    [SHADER_LOAD_SSBO_FRAG] = { load_ssbo_frag_dksh, (uint32_t)load_ssbo_frag_dksh_size },
    [SHADER_FETCH_ARRAY_FRAG] = { fetch_array_frag_dksh, (uint32_t)fetch_array_frag_dksh_size },
    [SHADER_SQUARE_BRANCH_GEOM] = { square_branch_geom_dksh, (uint32_t)square_branch_geom_dksh_size },
    [SHADER_QUAD_COLOR_TESE] = { quad_color_tese_dksh, (uint32_t)quad_color_tese_dksh_size },
    [SHADER_FETCH_STEP_FRAG] = { fetch_step_frag_dksh, (uint32_t)fetch_step_frag_dksh_size },
    [SHADER_FETCH_LOD_FRAG] = { fetch_lod_frag_dksh, (uint32_t)fetch_lod_frag_dksh_size },
    [SHADER_POINT_COORD_FRAG] = { point_coord_frag_dksh, (uint32_t)point_coord_frag_dksh_size },
    [SHADER_DEPTH_OUT_FRAG] = { depth_out_frag_dksh, (uint32_t)depth_out_frag_dksh_size },
    [SHADER_COUNT_LATE_FRAG] = { count_late_frag_dksh, (uint32_t)count_late_frag_dksh_size },
    [SHADER_COUNT_EARLY_FRAG] = { count_early_frag_dksh, (uint32_t)count_early_frag_dksh_size },
    [SHADER_LOD_FRAG] = { lod_frag_dksh, (uint32_t)lod_frag_dksh_size },
    [SHADER_COVMASK_FRAG] = { covmask_frag_dksh, (uint32_t)covmask_frag_dksh_size },
    [SHADER_COVALPHA_FRAG] = { covalpha_frag_dksh, (uint32_t)covalpha_frag_dksh_size },
    [SHADER_MODULATE_FRAG] = { modulate_frag_dksh, (uint32_t)modulate_frag_dksh_size },
    [SHADER_MSFETCH_FRAG] = { msfetch_frag_dksh, (uint32_t)msfetch_frag_dksh_size },
    [SHADER_BINDLESS_MS_FRAG] = { bindless_ms_frag_dksh, (uint32_t)bindless_ms_frag_dksh_size },
    [SHADER_CLIP_VERT] = { clip_vert_dksh, (uint32_t)clip_vert_dksh_size },
    [SHADER_CLIP_FIRST_TESE] = { clip_first_tese_dksh, (uint32_t)clip_first_tese_dksh_size },
    [SHADER_CLIP_LAST_TESE] = { clip_last_tese_dksh, (uint32_t)clip_last_tese_dksh_size },
    [SHADER_STORE_TESE] = { store_tese_dksh, (uint32_t)store_tese_dksh_size },
    [SHADER_STORE_VERT] = { store_vert_dksh, (uint32_t)store_vert_dksh_size },
    [SHADER_FETCH_TEXEL_FRAG] = { fetch_texel_frag_dksh, (uint32_t)fetch_texel_frag_dksh_size },
    [SHADER_VERTEX_VALUE_VERT] = { vertex_value_vert_dksh, (uint32_t)vertex_value_vert_dksh_size },
    [SHADER_STORAGE_FLOAT_COMP] = { storage_float_comp_dksh, (uint32_t)storage_float_comp_dksh_size },
    [SHADER_STORAGE_UINT_COMP] = { storage_uint_comp_dksh, (uint32_t)storage_uint_comp_dksh_size },
    [SHADER_STORAGE_SINT_COMP] = { storage_sint_comp_dksh, (uint32_t)storage_sint_comp_dksh_size },
    [SHADER_STORAGE_LAYER_COMP] = { storage_layer_comp_dksh, (uint32_t)storage_layer_comp_dksh_size },
    [SHADER_STORAGE_SLICE_COMP] = { storage_slice_comp_dksh, (uint32_t)storage_slice_comp_dksh_size },
    [SHADER_STORAGE_LOAD_COMP] = { storage_load_comp_dksh, (uint32_t)storage_load_comp_dksh_size },
    [SHADER_STORAGE_COPY_COMP] = { storage_copy_comp_dksh, (uint32_t)storage_copy_comp_dksh_size },
    [SHADER_STORAGE_COPY_INT_COMP] = { storage_copy_int_comp_dksh, (uint32_t)storage_copy_int_comp_dksh_size },
    [SHADER_SAMPLE_AT_FRAG] = { sample_at_frag_dksh, (uint32_t)sample_at_frag_dksh_size },
    [SHADER_FETCH_VOLUME_FRAG] = { fetch_volume_frag_dksh, (uint32_t)fetch_volume_frag_dksh_size },
    [SHADER_SHARED_REACH_COMP] = { shared_reach_comp_dksh, (uint32_t)shared_reach_comp_dksh_size },
    [SHADER_SATURATE_FRAG] = { saturate_frag_dksh, (uint32_t)saturate_frag_dksh_size },
    [SHADER_BUFFER_READ_VERT] = { buffer_read_vert_dksh, (uint32_t)buffer_read_vert_dksh_size },
    [SHADER_BUFFER_STORE_GEOM] = { buffer_store_geom_dksh, (uint32_t)buffer_store_geom_dksh_size },
    [SHADER_CONST_RED_FRAG] = { const_red_frag_dksh, (uint32_t)const_red_frag_dksh_size },
    [SHADER_CONST_GREEN_FRAG] = { const_green_frag_dksh, (uint32_t)const_green_frag_dksh_size },
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
