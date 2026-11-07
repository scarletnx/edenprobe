// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stencilref.h"

const uint8_t STENCIL_COMPARE_BANDS[3] = { 0x40u, 0x80u, 0xC0u };

bool StencilPasses(StencilCompare op, uint8_t ref, uint8_t stored, uint8_t mask) {
    const uint32_t r = (uint32_t)(ref & mask);
    const uint32_t s = (uint32_t)(stored & mask);
    switch (op) {
    case STENCIL_NEVER:
        return false;
    case STENCIL_LESS:
        return r < s;
    case STENCIL_EQUAL:
        return r == s;
    case STENCIL_LEQUAL:
        return r <= s;
    case STENCIL_GREATER:
        return r > s;
    case STENCIL_NOTEQUAL:
        return r != s;
    case STENCIL_GEQUAL:
        return r >= s;
    case STENCIL_ALWAYS:
        return true;
    }
    return false;
}

uint8_t StencilApply(StencilOp op, uint8_t stored, uint8_t ref, uint8_t write_mask) {
    uint8_t result = stored;
    switch (op) {
    case STENCIL_KEEP:
        result = stored;
        break;
    case STENCIL_ZERO:
        result = 0;
        break;
    case STENCIL_REPLACE:
        result = ref;
        break;
    case STENCIL_INCR:
        result = stored == 0xFFu ? 0xFFu : (uint8_t)(stored + 1u);
        break;
    case STENCIL_DECR:
        result = stored == 0u ? 0u : (uint8_t)(stored - 1u);
        break;
    case STENCIL_INVERT:
        result = (uint8_t)~stored;
        break;
    case STENCIL_INCR_WRAP:
        result = (uint8_t)(stored + 1u);
        break;
    case STENCIL_DECR_WRAP:
        result = (uint8_t)(stored - 1u);
        break;
    }
    return (uint8_t)((stored & (uint8_t)~write_mask) | (result & write_mask));
}

const char *StencilCompareName(StencilCompare op) {
    static const char *const names[] = { "?",       "never",    "less",   "equal", "lequal",
                                         "greater", "notequal", "gequal", "always" };
    return (unsigned)op <= 8u ? names[op] : "?";
}

const char *StencilOpName(StencilOp op) {
    static const char *const names[] = { "?",    "keep",   "zero",      "replace",  "incr",
                                         "decr", "invert", "incr_wrap", "decr_wrap" };
    return (unsigned)op <= 8u ? names[op] : "?";
}
