# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The judge's case counts the docs give are the ones the host build just ran: a suite that grows
moves its number in docs/families/Judge.md and docs/Console.md in the same commit. host-run runs the
binary before these tests, so the report it wrote is this tree's."""

import json
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPORT = os.path.join(ROOT, "edenprobe-host-report.jsonl")

UNITS = ("one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen "
         "sixteen seventeen eighteen nineteen").split()
TENS = "twenty thirty forty fifty sixty seventy eighty ninety".split()


def number(text):
    """A count as the docs spell it: "sixteen", "three hundred and forty one"."""
    total = 0
    for word in text.split():
        if word == "and":
            continue
        if word == "hundred":
            total *= 100
        elif word in UNITS:
            total += UNITS.index(word) + 1
        elif word in TENS:
            total += (TENS.index(word) + 2) * 10
        else:
            raise ValueError("not a number word: %r in %r" % (word, text))
    return total


def spelled(path, pattern):
    with open(os.path.join(ROOT, path)) as f:
        found = re.findall(pattern, f.read())
    if len(found) != 1:
        raise AssertionError("%s: %d matches of %r, want one" % (path, len(found), pattern))
    return number(" ".join(found[0].split()))


def ran():
    if not os.path.exists(REPORT):
        raise AssertionError("%s is missing: run make -f host.mk host-run" % REPORT)
    counts = {}
    with open(REPORT) as f:
        for line in f:
            entry = json.loads(line)
            if entry.get("type") == "case":
                counts[entry["suite"]] = counts.get(entry["suite"], 0) + 1
    return counts


WORDS = r"([a-z]+(?:\s+[a-z]+)*?)"


class Number(unittest.TestCase):
    def test_spellings(self):
        self.assertEqual(number("sixteen"), 16)
        self.assertEqual(number("one hundred and fifty two"), 152)
        self.assertEqual(number("three hundred and forty one"), 341)
        self.assertEqual(number("eighty"), 80)
        self.assertRaises(ValueError, number, "three dozen")


class Counts(unittest.TestCase):
    def setUp(self):
        self.ran = ran()

    def judge(self, suite):
        return spelled("docs/families/Judge.md", r"\*\*%s\*\*, %s cases" % (suite, WORDS))

    def test_selfcheck(self):
        self.assertEqual(self.judge("selfcheck"), self.ran["selfcheck"])

    def test_support(self):
        # the host build has one case more, a backward seek through the C library's stream buffer
        self.assertEqual(self.judge("support") + 1, self.ran["support"])

    def test_reference(self):
        self.assertEqual(self.judge("reference"), self.ran["reference"])
        console = spelled("docs/Console.md", r"the reference suite's\s+%s\s+cases" % WORDS)
        self.assertEqual(console, self.ran["reference"])


if __name__ == "__main__":
    unittest.main()
