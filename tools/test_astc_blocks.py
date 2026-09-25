# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The hand-made blocks in source/astcblocks.c are the ones tools/astc-blocks.py builds."""

import importlib.util
import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("astc_blocks", os.path.join(HERE, "astc-blocks.py"))
astc_blocks = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(astc_blocks)


def table():
    text = open(os.path.join(HERE, "..", "source", "astcblocks.c")).read()
    out = {}
    for m in re.finditer(r'\{ "(\w+)", "[^"]*", \d+, \d+,\s*\{([^}]*)\}', text):
        out[m.group(1)] = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", m.group(2)))
    return out


class HandMadeBlocks(unittest.TestCase):
    def test_table_matches_the_builder(self):
        t = table()
        for name, block in astc_blocks.BLOCKS.items():
            self.assertEqual(t[name], block, name)

    def test_q16_weights_are_zero_to_fifteen(self):
        bits = int.from_bytes(astc_blocks.BLOCKS["q16"], "little")
        weights = [sum(((bits >> (127 - (k * 4 + j))) & 1) << j for j in range(4))
                   for k in range(16)]
        self.assertEqual(weights, list(range(16)))


if __name__ == "__main__":
    unittest.main()
