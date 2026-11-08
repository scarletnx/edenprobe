# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""source/banlist.c is what tools/bans.py writes from source/bans.txt, and a line that does not check is refused
with its number and why."""

import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("bans", os.path.join(HERE, "bans.py"))
bans = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bans)

HEADER = "# Machines: eden scarletnx\nsuite | case | machine | versions | breaks | source | retest | checked\n"
GOOD = "cpu | a case | eden | * | ends the program | a run | quick unban | 2026-11-08 ends on 1.2\n"


def refusal(text):
    try:
        bans.written(text)
    except bans.Refused as error:
        return str(error)
    return None


class Table(unittest.TestCase):
    def test_the_c_table_is_the_scripts(self):
        with open(bans.TEXT, encoding="utf-8") as f:
            text = f.read()
        with open(bans.TABLE, encoding="utf-8") as f:
            self.assertEqual(f.read(), bans.written(text), "run tools/bans.py")

    def test_a_good_line_is_written_with_its_line_number(self):
        out = bans.written(HEADER + "\n" + GOOD)
        self.assertIn('"a case",', out)
        self.assertIn("        4,", out)
        self.assertIn("const size_t BAN_LINE_COUNT = 1;", out)
        self.assertIn('"scarletnx",', out)

    def test_quotes_are_escaped_for_c(self):
        out = bans.written(HEADER + GOOD.replace("a run", 'make run MODE="quick cpu"'))
        self.assertIn('"make run MODE=\\"quick cpu\\"",', out)

    def test_an_empty_list_still_compiles(self):
        out = bans.written(HEADER)
        self.assertIn("const size_t BAN_LINE_COUNT = 0;", out)
        self.assertIn("{ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0 },", out)


class Refusals(unittest.TestCase):
    def test_each_bad_line_says_its_number_and_why(self):
        cases = [
            (HEADER + GOOD.replace("eden", "yuzu"), "line 3: machine 'yuzu' is not one of eden scarletnx"),
            (HEADER + GOOD.replace("| * |", "| < x |"), "line 3: versions '< x': a bound's version is dotted"),
            (HEADER + GOOD.replace("| * |", "| < 2 > 1 |"), "line 3: versions '< 2 > 1': two bounds are a lower"),
            (HEADER + GOOD.replace("| * |", "| a b |"), "line 3: versions 'a b': a version is 1 to 32"),
            (HEADER + GOOD.replace("2026-11-08 ends", "yesterday ends"), "line 3: checked starts with the date"),
            (HEADER + GOOD.replace("| a run |", "|  |"), "line 3: the source is empty"),
            (HEADER + GOOD.replace("cpu |", "two words |"), "line 3: a suite is one word"),
            (HEADER + GOOD.replace("| quick unban |", "| quick | unban |"), "line 3: 9 fields, not 8"),
            (HEADER + GOOD + GOOD, "line 4: cpu \"a case\" on eden is banned on line 3 already"),
            ("suite | case | machine | versions | breaks | source | retest | checked\n" + GOOD,
             "no '# Machines:' line names the machines"),
            ("# Machines: eden\nsuite | case | machine\n", "line 2: the header is"),
            (HEADER + GOOD.replace("a case", "x" * 160), "line 3: the case is 160 characters"),
        ]
        for text, said in cases:
            got = refusal(text)
            self.assertIsNotNone(got, said)
            self.assertTrue(got.startswith(said), "%r does not start with %r" % (got, said))

    def test_the_versions_the_program_reads(self):
        for field in ("*", "1.2", "1.2, 6a9235c22d", "6a9235c22d,10ed344", "< 2", "<= 1.2.3", ">= v1.2",
                      ">= 1.2 < 1.4", "> 1 <= 2"):
            self.assertIsNone(bans.versions_problem(field), field)
        for field in ("a b", "1.2,,3", "< x", ">= 1 >= 2", "< 2 > 1", ">= 1 < 2 < 3", "<= 1.", "has/slash",
                      "x" * 33):
            self.assertIsNotNone(bans.versions_problem(field), field)


if __name__ == "__main__":
    unittest.main()
