#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""How often tools/perf.py lists a move between runs of one build: its false-positive rate.

    tools/perf-self.py r1.jsonl r2.jsonl r3.jsonl r4.jsonl r5.jsonl r6.jsonl [...]

The reports are runs of one build on one machine, made in one sitting. Every way of taking two
sides of three runs out of them is compared the way perf.py compares two builds, with its limits:
each case's median over its side, a move past 20 percent and 100 microseconds, and a case whose
runs on either side spread past 75 percent set aside as unsteady. Nothing changed between the
runs, so every split that lists a move is a false positive, and their share is the rate a
comparison of two builds on this machine is wrong at. docs/TimePerCase.md has the method and the
figures it gave. Standard library only.
"""

import importlib.util
import itertools
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("perf", os.path.join(HERE, "perf.py"))
perf = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(perf)

SIDE = 3  # runs a side, the method's
LEAST = 2 * SIDE  # reports it takes to make one split
# How close two sides' times must be to count as agreeing: the shares docs/TimePerCase.md gives.
TENTH = 10.0
FIFTH = 20.0


def splits(count):
    """Every unordered pair of disjoint sides of three out of @count runs, as pairs of index
    tuples, the side with the lower indices first: C(n,3) * C(n-3,3) / 2 of them."""
    triples = list(itertools.combinations(range(count), SIDE))
    out = []
    for i, a in enumerate(triples):
        for b in triples[i + 1:]:
            if not set(a) & set(b):
                out.append((a, b))
    return out


def within(before, after, limit):
    """True when @after is within @limit percent of @before."""
    if before == after:
        return True
    delta = perf.change(before, after)
    return delta is not None and abs(delta) <= limit


class Measure:
    """What the splits of one build's runs say, with perf.py's limits."""

    def __init__(self, runs, threshold=perf.THRESHOLD, floor=perf.FLOOR,
                 unsteady=perf.UNSTEADY):
        self.limits = (threshold, floor, unsteady)
        self.splits = 0
        self.moving = 0  # splits that listed at least one case
        self.listed = {}  # case: splits that listed it
        self.shaky = {}  # case: splits that set it aside
        self.tenth = []  # per split, the percent of cases both sides timed within a tenth
        self.fifth = []
        self.suites = 0.0  # the largest change of a suite's median over every split
        self.suite_worst = None
        sides = {}
        suites = {}
        for triple in itertools.combinations(range(len(runs)), SIDE):
            merged, spreads = perf.merge([runs[i] for i in triple])
            sides[triple] = (merged, spreads)
            names = sorted({suite for suite, _ in merged})
            suites[triple] = {s: perf.summarize(merged, s)[3] for s in names}
        for a, b in splits(len(runs)):
            self.compare(sides[a], sides[b], suites[a], suites[b])

    def compare(self, side_a, side_b, suites_a, suites_b):
        (a, spread_a), (b, spread_b) = side_a, side_b
        threshold, floor, unsteady = self.limits
        moves, shaky = perf.moved(a, b, threshold, floor, spread_a, spread_b, unsteady)
        self.splits += 1
        if moves:
            self.moving += 1
        for key, _, _, _ in moves:
            self.listed[key] = self.listed.get(key, 0) + 1
        for key in shaky:
            self.shaky[key] = self.shaky.get(key, 0) + 1
        both = [key for key in a if key in b]
        if both:
            self.tenth.append(100.0 * sum(within(a[k], b[k], TENTH) for k in both) / len(both))
            self.fifth.append(100.0 * sum(within(a[k], b[k], FIFTH) for k in both) / len(both))
        for suite, median_a in suites_a.items():
            median_b = suites_b.get(suite)
            delta = perf.change(median_a, median_b) if median_b is not None else None
            if delta is not None and abs(delta) > self.suites:
                self.suites = abs(delta)
                self.suite_worst = suite

    def lines(self, count):
        threshold, floor, unsteady = self.limits
        out = ["%d reports, %d splits into two sides of %d, each case's median over its side; a "
               "move is past %g percent and %d us, unsteady past %g percent" % (
                   count, self.splits, SIDE, threshold, floor, unsteady)]
        out.append("splits listing a move: %d of %d (%.2f percent)" % (
            self.moving, self.splits, 100.0 * self.moving / self.splits))
        for title, counts in (("cases listed as moved", self.listed),
                              ("cases set aside as unsteady", self.shaky)):
            out.append("%s: %d" % (title, len(counts)))
            for key in sorted(counts, key=lambda k: (-counts[k], k)):
                out.append("  %7d of %d  %s: %s" % (counts[key], self.splits, key[0], key[1]))
        if self.tenth:
            out.append("cases within a tenth between the sides: %.1f percent at worst, %.1f "
                       "typical; within a fifth: %.1f at worst" % (
                           min(self.tenth), statistics.median(self.tenth), min(self.fifth)))
        if self.suite_worst is not None:
            out.append("suite medians within %.1f percent at worst (%s)" % (
                self.suites, self.suite_worst))
        return out


USAGE = """usage: perf-self.py R1.jsonl R2.jsonl R3.jsonl R4.jsonl R5.jsonl R6.jsonl [...]

Runs of one build on one machine, in one sitting, six at least. Every split into two sides of
three is compared as perf.py compares two builds, with its default limits; a split that lists a
move is a false positive. Prints their share, the cases listed and set aside, and how closely the
sides agree.
"""


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if any(word in ("-h", "--help") for word in argv):
        print(USAGE)
        return 0
    try:
        unknown = [word for word in argv if word.startswith("-")]
        if unknown:
            raise perf.Refused("%s is not an option: perf-self.py takes reports only" % unknown[0])
        if len(argv) < LEAST:
            raise perf.Refused("%d report%s: two sides of %d need %d at least" % (
                len(argv), "" if len(argv) == 1 else "s", SIDE, LEAST))
        runs = []
        for path in argv:
            times, _ = perf.read(path)
            if not times:
                raise perf.Refused("%s: no timed cases (a notime run, a report from before cases "
                                   "were timed, or an empty one)" % path)
            runs.append(times)
    except perf.Refused as refusal:
        print("perf-self.py: %s" % refusal, file=sys.stderr)
        return 1
    print("\n".join(Measure(runs).lines(len(runs))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
