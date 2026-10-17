# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The table source/tests/t_blit2dref.c holds is the one tools/blit2d-expect.py prints."""

import importlib.util
import io
import os
import re
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("blit2d_expect",
                                              os.path.join(HERE, "blit2d-expect.py"))
blit2d_expect = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(blit2d_expect)


def held():
    with open(os.path.join(HERE, "..", "source", "tests", "t_blit2dref.c")) as f:
        text = f.read()
    m = re.search(r"// BEGIN tools/blit2d-expect.py --c\n(.*)// END tools/blit2d-expect.py --c",
                  text, re.S)
    return m.group(1) if m else None


class PinnedTable(unittest.TestCase):
    def test_the_c_table_is_the_scripts(self):
        printed = io.StringIO()
        with redirect_stdout(printed):
            blit2d_expect.print_c()
        self.assertEqual(held(), printed.getvalue())

    def test_the_table_the_cases_were_asked_with(self):
        rows = {name: hashes for name, _, _, hashes in blit2d_expect.case_table()}
        for name, asked in blit2d_expect.ASKED.items():
            self.assertEqual(rows[name][0], asked, name)
        eden = 2  # Q1 A, Q2 B
        self.assertEqual(rows["blit2d_corner_2_3"][eden], blit2d_expect.ASKED_EDEN_2_3)
        self.assertEqual(rows["blit2d_center_2_3"][eden], blit2d_expect.ASKED_EDEN_2_3)
        self.assertEqual(blit2d_expect.md5(blit2d_expect.untouched()),
                         blit2d_expect.ASKED_UNTOUCHED)

    def test_a_gob_is_the_layout_the_cases_rely_on(self):
        bl = blit2d_expect.block_linear
        row = blit2d_expect.DST_WIDTH * 4
        # Runs of 16 bytes, row pairs, 32 byte halves, then the next gob down the same block,
        # then the next block across.
        self.assertEqual([bl(16, 0, row), bl(0, 1, row), bl(0, 2, row), bl(32, 0, row),
                          bl(0, 8, row), bl(64, 0, row)], [32, 16, 64, 256, 512, 1024])
        self.assertEqual(sorted(bl(x, y, row) for y in range(16) for x in range(row)),
                         list(range(row * 16)))

    def test_the_window_cuts_runs_and_crosses_a_gob(self):
        start, end = 4 * blit2d_expect.WIN_X, 4 * (blit2d_expect.WIN_X + blit2d_expect.WIN_WIDTH)
        self.assertEqual((start % 16, end % 16), (4, 8))
        last_row = blit2d_expect.WIN_Y + blit2d_expect.WIN_HEIGHT - 1
        self.assertLess(blit2d_expect.WIN_Y, 8)
        self.assertGreaterEqual(last_row, 8)
        self.assertEqual(blit2d_expect.window_counts(), (189, 835))

    def test_each_question_changes_some_case(self):
        # A question no case can tell apart would be a question nobody asks.
        seen = set()
        for name, _, _, hashes in blit2d_expect.case_table():
            for bit, q in enumerate(blit2d_expect.QUESTIONS):
                if any(hashes[k] != hashes[k ^ (1 << bit)] for k in range(len(hashes))):
                    seen.add(q)
        self.assertEqual(seen, set(blit2d_expect.QUESTIONS))

    def test_eden_starts_its_corner_span_one_texel_back_at_two(self):
        # fermi_2d.cpp:79-82 with du/dx 2.0 and the corner origin: src_x0 becomes -1, and the
        # centre sample at -1 + (i + 1/2) * 2 lands on 2i.
        rules = {"Q1": "A", "Q2": "B"}
        two = blit2d_expect.Fraction(2)
        self.assertEqual([blit2d_expect.pick(1, two, i, 21, rules) for i in range(4)],
                         [0, 2, 4, 6])


if __name__ == "__main__":
    unittest.main()
