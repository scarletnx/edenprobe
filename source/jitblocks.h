// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The cpu suite's compile case: straight-line functions, each one basic block, written with the
/// state each must leave by tools/jit-blocks.py (which says what they do and why).

#define JIT_BLOCKS 48u
/// Seventeen general registers, a word of padding, then sixteen vector registers, low half first.
#define JIT_STATE_WORDS 50u
/// Operations in each block between its loads and its stores.
#define JIT_OPS 160u

#if defined(__aarch64__)
/// Loads every register from @p state, runs the block and stores them back.
extern void (*const JIT_BLOCK[JIT_BLOCKS])(uint64_t *state);
/// Returns at once: called before the blocks so the caller's code is translated untimed.
void JitWarm(uint64_t *state);
#endif

extern const uint64_t JIT_INITIAL[JIT_STATE_WORDS];
extern const uint64_t JIT_EXPECTED[JIT_BLOCKS][JIT_STATE_WORDS];

#ifdef __cplusplus
}
#endif
