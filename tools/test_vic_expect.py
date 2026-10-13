# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The table source/tests/t_vicref.c holds is the one tools/vic-expect.py prints."""

import importlib.util
import io
import os
import re
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("vic_expect", os.path.join(HERE, "vic-expect.py"))
vic_expect = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(vic_expect)


def held():
    text = open(os.path.join(HERE, "..", "source", "tests", "t_vicref.c")).read()
    m = re.search(r"// BEGIN tools/vic-expect.py --c\n(.*)// END tools/vic-expect.py --c", text,
                  re.S)
    return m.group(1) if m else None


class PinnedTable(unittest.TestCase):
    def test_the_c_table_is_the_scripts(self):
        printed = io.StringIO()
        with redirect_stdout(printed):
            vic_expect.print_c()
        self.assertEqual(held(), printed.getvalue())

    def test_the_handbacks_bt601_table(self):
        for yuv, trunc, rnd in vic_expect.HANDBACK_601:
            self.assertEqual(vic_expect.bt601(yuv, False), trunc, yuv)
            self.assertEqual(vic_expect.bt601(yuv, True), rnd, yuv)

    def test_an_open_question_changes_some_case(self):
        # A question no case can tell apart would be a question nobody asks.
        seen = set()
        for name, jobs, questions in vic_expect.build_cases():
            for q in questions:
                outputs = set()
                for _, rules in vic_expect.combos([q]):
                    outputs.add(b"".join(vic_expect.model(j, rules) for j in jobs))
                if len(outputs) > 1:
                    seen.add(q)
        self.assertEqual(seen, set(vic_expect.QUESTIONS))


if __name__ == "__main__":
    unittest.main()
