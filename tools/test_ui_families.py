# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Every step main.c runs is shown under its own family on the screens, and the support suite's list checks it.

A step the screens know no family for is shown under other (main.c notes it at run time, on the machine); here the
two tables are read from the source, so a suite added to STEPS without its row in ui.c's FAMILIES and in
t_screens.c's list fails host-run."""

import os
import re
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def read(path):
    return open(os.path.join(ROOT, path), encoding="utf-8").read()


def table(text, start):
    """the braces of the array that opens at start, as text"""
    begin = text.index("{", text.index(start))
    depth = 0
    for i in range(begin, len(text)):
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        if depth == 0:
            return text[begin : i + 1]
    raise AssertionError(f"{start}: no end")


ROW = re.compile(r'\{\s*"([a-z0-9-]+)",\s*UI_FAMILY_([A-Z]+)\s*\}')
# the steps that run whatever the mode names (family 0 in STEPS): the judge's three and the processor's
ALWAYS = {"selfcheck": "JUDGE", "support": "JUDGE", "reference": "JUDGE", "cpu": "CPU"}
# net and service report under their suites' names (nifm, hwopus...), which the screens list instead
UNDER_SUITES = {"net", "service"}


class Families(unittest.TestCase):
    def setUp(self):
        steps = table(read("source/main.c"), "static const Step STEPS[]")
        rows = re.findall(r'\{\s*"([a-z0-9-]+)",\s*Run\w+,\s*(?:MODE_FAMILY_([A-Z]+)|0),', steps)
        self.steps = {name: family or ALWAYS.get(name) for name, family in rows}
        self.ui = dict(ROW.findall(table(read("source/ui.c"), "FAMILIES[]")))
        self.checked = dict(ROW.findall(table(read("source/tests/t_screens.c"), "SUITES[]")))

    def test_tables_read(self):
        self.assertGreater(len(self.steps), 40)
        self.assertIn("astc", self.ui)
        self.assertNotIn(None, self.steps.values(), "a step of family 0 that ALWAYS does not name")

    def test_every_step_has_its_family_on_the_screens(self):
        for name, family in self.steps.items():
            if name in UNDER_SUITES:
                continue
            with self.subTest(step=name):
                self.assertEqual(self.ui.get(name), family, f"source/ui.c's FAMILIES has no {name} under {family}")

    def test_the_support_suite_checks_every_suite_the_screens_know(self):
        self.assertEqual(self.checked, self.ui, "source/tests/t_screens.c's SUITES and ui.c's FAMILIES differ")


if __name__ == "__main__":
    unittest.main()
