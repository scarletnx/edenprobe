# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for perf-self.py: python3 -m unittest discover -s tools (host.mk's host-run does it)."""

import contextlib
import importlib.util
import io
import math
import os
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("perf_self", os.path.join(HERE, "perf-self.py"))
perf_self = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(perf_self)


def case(suite, name, us=None):
    line = '{"type":"case","suite":"%s","name":"%s","tier":"exact","result":"pass","detail":""' % (
        suite, name)
    if us is not None:
        line += ',"us":%s' % us
    return line + "}\n"


def runs(**columns):
    """Runs from one column of times per case: runs(x=[1, 2]) is two runs of case s: x."""
    count = len(next(iter(columns.values())))
    return [{("s", name): times[i] for name, times in columns.items()} for i in range(count)]


class Splits(unittest.TestCase):
    def test_every_pair_of_disjoint_sides_once(self):
        for count in (6, 7, 9, 12):
            pairs = perf_self.splits(count)
            want = math.comb(count, 3) * math.comb(count - 3, 3) // 2
            self.assertEqual(len(pairs), want, count)
            self.assertEqual(len(set(pairs)), want)
            for a, b in pairs:
                self.assertFalse(set(a) & set(b))
                self.assertLess(a, b)
        # The counts docs/TimePerCase.md gives for nine, twelve and eighteen runs.
        self.assertEqual([len(perf_self.splits(n)) for n in (9, 12)], [840, 9240])
        self.assertEqual(math.comb(18, 3) * math.comb(15, 3) // 2, 185640)


class Within(unittest.TestCase):
    def test_the_limit_itself_agrees(self):
        self.assertTrue(perf_self.within(100, 110, 10.0))
        self.assertTrue(perf_self.within(100, 90, 10.0))
        self.assertFalse(perf_self.within(100, 111, 10.0))
        self.assertFalse(perf_self.within(100, 89, 10.0))
        # Nothing to measure from: equal is agreeing, anything else is not.
        self.assertTrue(perf_self.within(0, 0, 10.0))
        self.assertFalse(perf_self.within(0, 5, 10.0))


class Measure(unittest.TestCase):
    def test_one_build_that_never_moves_lists_nothing(self):
        result = perf_self.Measure(runs(x=[1000] * 6, y=[250] * 6))
        self.assertEqual((result.splits, result.moving), (10, 0))
        self.assertEqual((result.listed, result.shaky), ({}, {}))
        self.assertEqual((min(result.tenth), min(result.fifth), result.suites), (100.0, 100.0, 0.0))

    def test_one_slow_process_cannot_move_a_side(self):
        # The run at 1300 always shares its side with two at 1000, whose median wins, and a
        # spread of 30 percent is steady.
        result = perf_self.Measure(runs(x=[1000, 1000, 1000, 1000, 1000, 1300]))
        self.assertEqual((result.moving, result.listed, result.shaky), (0, {}, {}))

    def test_one_wild_process_sets_the_case_aside_in_every_split_it_is_in(self):
        result = perf_self.Measure(runs(x=[1000] * 6 + [4000], y=[300] * 7))
        self.assertEqual(result.splits, 70)
        # Run 6 is left out of a split in C(6,3) * C(3,3) / 2 = 10 of the 70.
        self.assertEqual(result.shaky, {("s", "x"): 60})
        self.assertEqual((result.moving, result.listed), (0, {}))

    def test_two_sittings_list_a_move_in_every_split(self):
        # Three runs at 1000 and three at 1300: whichever way they split, one side holds two of
        # one sitting and the other two of the other, and the medians are 30 percent apart.
        result = perf_self.Measure(runs(x=[1000, 1000, 1000, 1300, 1300, 1300], y=[200] * 6))
        self.assertEqual((result.splits, result.moving), (10, 10))
        self.assertEqual(result.listed, {("s", "x"): 10})
        self.assertEqual(min(result.fifth), 50.0)

    def test_the_floor_keeps_a_small_case_quiet(self):
        result = perf_self.Measure(runs(x=[100, 100, 100, 160, 160, 160]))
        self.assertEqual((result.moving, result.listed), (0, {}))
        self.assertEqual(min(result.tenth), 0.0)

    def test_how_closely_the_sides_agree(self):
        # y's two runs at 100 share a side in 4 of the 10 splits (the side holding both picks one
        # of the other four), and that side's median is 12 percent below the other's.
        result = perf_self.Measure(runs(x=[1000] * 6, y=[100, 100, 112, 112, 112, 112]))
        self.assertEqual(sorted(result.tenth), [50.0] * 4 + [100.0] * 6)
        self.assertEqual(min(result.fifth), 100.0)
        # The suite's median is between its two cases: 550 on one side, 556 on the other.
        self.assertAlmostEqual(result.suites, 600.0 / 550)
        self.assertEqual(result.suite_worst, "s")
        lines = result.lines(6)
        self.assertIn("cases within a tenth between the sides: 50.0 percent at worst, 100.0 "
                      "typical; within a fifth: 100.0 at worst", lines)
        self.assertIn("suite medians within 1.1 percent at worst (s)", lines)

    def test_a_suite_is_its_median_case_not_its_total(self):
        # b is the middle case of three; its two runs at 200 share side a in 4 splits, 20
        # percent under the other side, while the suite's total moves under 1 percent.
        result = perf_self.Measure(runs(a=[100] * 6, b=[200, 200, 240, 240, 240, 240],
                                        c=[5000] * 6))
        self.assertAlmostEqual(result.suites, 20.0)
        self.assertEqual(result.moving, 0)

    def test_the_limits_are_perf_py_s(self):
        result = perf_self.Measure(runs(x=[1000] * 6))
        self.assertEqual(result.limits, (perf_self.perf.THRESHOLD, perf_self.perf.FLOOR,
                                         perf_self.perf.UNSTEADY))


class Command(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def report(self, name, *lines):
        path = os.path.join(self.tmp.name, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("".join(lines))
        return path

    def run_main(self, *args):
        out = io.StringIO()
        err = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = perf_self.main(list(args))
        return code, out.getvalue(), err.getvalue()

    def test_six_runs_end_to_end(self):
        times = [1000, 1000, 1000, 1300, 1300, 1300]
        paths = [self.report("r%d" % i, case("clear", "red", us), case("clear", "cyan", 250),
                             case("coherence", "upload", 2300 if i else 120))
                 for i, us in enumerate(times)]
        # The upload's fast run sits on one side of every split of six, so it is never compared.
        code, out, err = self.run_main(*paths)
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(out.splitlines(), [
            "6 reports, 10 splits into two sides of 3, each case's median over its side; a move "
            "is past 20 percent and 100 us, unsteady past 75 percent",
            "splits listing a move: 10 of 10 (100.00 percent)",
            "cases listed as moved: 1",
            "       10 of 10  clear: red",
            "cases set aside as unsteady: 1",
            "       10 of 10  coherence: upload",
            "cases within a tenth between the sides: 66.7 percent at worst, 66.7 typical; within "
            "a fifth: 66.7 at worst",
            "suite medians within 24.0 percent at worst (clear)",
        ])

    def test_what_is_refused(self):
        timed = self.report("timed", case("clear", "red", 1000))
        untimed = self.report("notime", '{"type":"note","text":"mode: quick"}\n',
                              case("clear", "red"))
        for args, says in (([timed] * 5, "5 reports: two sides of 3 need 6 at least"),
                           ([], "0 reports"),
                           ([timed] * 5 + [untimed], "notime: no timed cases"),
                           ([timed] * 6 + ["--floor", "50"], "--floor is not an option"),
                           ([timed] * 5 + ["/nonexistent/r.jsonl"], "/nonexistent/r.jsonl")):
            code, out, err = self.run_main(*args)
            self.assertEqual(code, 1, args)
            self.assertEqual(out, "")
            self.assertTrue(err.startswith("perf-self.py: "), err)
            self.assertIn(says, err)

    def test_help(self):
        code, out, _ = self.run_main("--help")
        self.assertEqual(code, 0)
        self.assertIn("six at least", out)


if __name__ == "__main__":
    unittest.main()
