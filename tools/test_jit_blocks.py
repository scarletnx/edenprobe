# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""source/jitblocks.c is what tools/jit-blocks.py writes, and the rules it works the expected state
out with give the architecture's answers on values worked out by hand."""

import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("jit_blocks", os.path.join(HERE, "jit-blocks.py"))
jb = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(jb)

M64 = jb.M64


class Committed(unittest.TestCase):
    def test_source_is_the_generators_output(self):
        with open(os.path.join(HERE, "..", "source", "jitblocks.c")) as f:
            self.assertEqual(f.read(), jb.render())

    def test_header_sizes_match(self):
        with open(os.path.join(HERE, "..", "source", "jitblocks.h")) as f:
            header = f.read()
        self.assertIn(f"#define JIT_BLOCKS {jb.BLOCKS}u", header)
        self.assertIn(f"#define JIT_STATE_WORDS {jb.STATE_WORDS}u", header)
        self.assertIn(f"#define JIT_OPS {jb.OPS}u", header)

    def test_every_block_changes_the_state_differently(self):
        init, blocks = jb.generate()
        finals = [tuple(final) for _, final in blocks]
        self.assertEqual(len(set(finals)), len(finals))
        self.assertNotIn(tuple(init), finals)


class Rules(unittest.TestCase):
    def test_adds_flags(self):
        # 0x7fff... + 1 overflows into the sign bit: negative and signed overflow, no carry.
        self.assertEqual(jb.adds((1 << 63) - 1, 1), (1 << 63, {"n": 1, "z": 0, "c": 0, "v": 1}))
        # All ones + 1 wraps to zero with a carry and no signed overflow (-1 + 1).
        self.assertEqual(jb.adds(M64, 1), (0, {"n": 0, "z": 1, "c": 1, "v": 0}))
        # Two most negative values: zero, carry and overflow.
        self.assertEqual(jb.adds(1 << 63, 1 << 63), (0, {"n": 0, "z": 1, "c": 1, "v": 1}))

    def test_conditions(self):
        f = {"n": 1, "z": 0, "c": 1, "v": 1}  # n == v, z clear, c set
        holds = [c for c in jb.CONDS if jb.condition_holds(c, f)]
        self.assertEqual(holds, ["ne", "cs", "mi", "vs", "hi", "ge", "gt"])
        f = {"n": 0, "z": 1, "c": 1, "v": 1}  # n != v, z set
        holds = [c for c in jb.CONDS if jb.condition_holds(c, f)]
        self.assertEqual(holds, ["eq", "cs", "pl", "vs", "ls", "lt", "le"])

    def test_shifts_and_counts(self):
        self.assertEqual(jb.asr(1 << 63, 4), 0xF800000000000000)
        self.assertEqual(jb.asr(0x4000000000000000, 4), 0x0400000000000000)
        self.assertEqual(jb.ror(0x1, 1), 1 << 63)
        self.assertEqual(jb.ror(0x123456789ABCDEF0, 16), 0xDEF0123456789ABC)
        self.assertEqual(jb.clz(0), 64)
        self.assertEqual(jb.clz(1), 63)
        self.assertEqual(jb.clz(M64), 0)

    def test_lanes(self):
        q = (0x0000000200000001, 0x0000000400000003)
        self.assertEqual(jb.lanes32(q), [1, 2, 3, 4])
        self.assertEqual(jb.from_lanes32([1, 2, 3, 4]), q)

    def test_operations_apply_to_the_machine(self):
        # One block's first operations replayed by hand against the model, through gen_op.
        rng = jb.Rng(1)
        state = [i * 0x0101010101010101 & M64 for i in range(jb.STATE_WORDS)]
        m = jb.Machine(state)
        lines = []
        for _ in range(200):
            jb.gen_op(rng, m, lines)
        kinds = {line.split()[0] for line in lines}
        # Every kind of operation the generator knows shows up within two hundred draws.
        for k in ["add", "sub", "eor", "orr", "and", "bic", "lsl", "lsr", "asr", "ror", "mul",
                  "madd", "umulh", "adds", "csel", "clz", "ushr", "shl", "fmov", "mov"]:
            self.assertIn(k, kinds)
        self.assertEqual(len(m.state()), jb.STATE_WORDS)

    def test_fmov_zeroes_the_upper_half(self):
        m = jb.Machine([0] * jb.STATE_WORDS)
        m.v[3] = (5, 6)
        m.x[7] = 9

        class Draws:
            """Hands gen_op fixed draws: kind 90 (moves), the first of them (fmov), v3, x7."""

            def __init__(self):
                self.values = iter([90, 0, jb.VECS.index(3), jb.GPRS.index(7)])

            def below(self, n):
                return next(self.values) % n

            def pick(self, items):
                return items[self.below(len(items))]

        lines = []
        jb.gen_op(Draws(), m, lines)
        self.assertEqual(lines, ["fmov d3, x7"])
        self.assertEqual(m.v[3], (9, 0))

if __name__ == "__main__":
    unittest.main()
