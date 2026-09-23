# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: MIT
"""Tests for perf.py: python3 -m unittest discover -s tools (host.mk's host-run does it)."""

import contextlib
import io
import os
import tempfile
import unittest

import perf


def case(suite, name, us=None, result="pass"):
    line = '{"type":"case","suite":"%s","name":"%s","tier":"exact","result":"%s","detail":""' % (
        suite, name, result)
    if us is not None:
        line += ',"us":%s' % us
    return line + "}\n"


class Parse(unittest.TestCase):
    def test_only_timed_cases_count(self):
        lines = [
            '{"type":"note","text":"edenprobe starting"}\n',
            case("selfcheck", "no time"),
            case("clear", "opaque red", 8000),
            case("clear", "cyan", 250, result="fail"),
            '{"type":"perf","suite":"clear","timed":2,"total_us":8250}\n',
            '{"type":"summary","passed":1,"failed":1}\n',
        ]
        times, order, unreadable = perf.parse(lines)
        self.assertEqual(times, {("clear", "opaque red"): 8000, ("clear", "cyan"): 250})
        self.assertEqual(order, ["clear"])
        self.assertEqual(unreadable, 0)

    def test_a_torn_last_line_costs_only_itself(self):
        lines = [case("raster", "quad", 9000), '{"type":"case","suite":"raster","na']
        times, _, unreadable = perf.parse(lines)
        self.assertEqual(times, {("raster", "quad"): 9000})
        self.assertEqual(unreadable, 1)

    def test_a_time_that_is_not_a_count_is_ignored(self):
        lines = [case("a", "bool", "true"), case("a", "float", "2.5"), case("a", "neg", "-1"),
                 case("a", "string", '"7"'), "[1,2]\n", "\n"]
        times, order, unreadable = perf.parse(lines)
        self.assertEqual(times, {})
        self.assertEqual(order, [])
        self.assertEqual(unreadable, 0)

    def test_suites_keep_the_order_the_run_had(self):
        lines = [case("raster", "x", 1), case("clear", "y", 2), case("raster", "z", 3)]
        self.assertEqual(perf.parse(lines)[1], ["raster", "clear"])


class Arithmetic(unittest.TestCase):
    def test_median_matches_the_probe(self):
        self.assertEqual(perf.median([9, 1, 5]), 5)
        self.assertEqual(perf.median([40, 10, 30, 20]), 25)
        self.assertEqual(perf.median([1, 2]), 1)
        self.assertEqual(perf.median([7]), 7)
        self.assertEqual(perf.median([]), 0)

    def test_summary_of_a_suite(self):
        times = {("s", "a"): 8000, ("s", "b"): 250, ("s", "c"): 300, ("s", "d"): 275,
                 ("s", "e"): 260, ("t", "f"): 1}
        self.assertEqual(perf.summarize(times, "s"), (5, 9085, 250, 275, 8000))
        self.assertEqual(perf.summarize(times, "missing"), (0, 0, 0, 0, 0))

    def test_change_and_the_threshold(self):
        self.assertEqual(perf.change(200, 250), 25.0)
        self.assertIsNone(perf.change(0, 5))
        a = {("s", "exactly twenty"): 100, ("s", "just over"): 1000, ("s", "faster"): 1000,
             ("s", "from zero"): 0, ("s", "gone"): 5}
        b = {("s", "exactly twenty"): 120, ("s", "just over"): 1201, ("s", "faster"): 500,
             ("s", "from zero"): 100}
        rows, shaky = perf.moved(a, b, 20.0)
        self.assertEqual([r[0][1] for r in rows], ["faster", "just over"])
        self.assertAlmostEqual(rows[1][3], 20.1)
        self.assertEqual(shaky, [])

    def test_a_move_must_also_clear_the_floor(self):
        a = {("s", "tiny"): 200, ("s", "big"): 200}
        b = {("s", "tiny"): 290, ("s", "big"): 330}
        rows, _ = perf.moved(a, b, 30.0, 100)
        self.assertEqual([r[0][1] for r in rows], ["big"])

    def test_the_unsteady_limit_is_a_spread_of_three_quarters(self):
        a = {("s", "at the limit"): 1000, ("s", "past it"): 1000}
        b = {("s", "at the limit"): 1500, ("s", "past it"): 1500}
        spreads = {("s", "at the limit"): 75.0, ("s", "past it"): 76.0}
        rows, shaky = perf.moved(a, b, perf.THRESHOLD, perf.FLOOR, spreads, {})
        self.assertEqual([r[0][1] for r in rows], ["at the limit"])
        self.assertEqual(shaky, [("s", "past it")])
        self.assertEqual((perf.THRESHOLD, perf.FLOOR, perf.UNSTEADY), (20.0, 100, 75.0))

    def test_an_unsteady_case_is_named_and_not_compared(self):
        a = {("s", "upload"): 2300, ("s", "steady"): 250}
        b = {("s", "upload"): 120, ("s", "steady"): 251}
        rows, shaky = perf.moved(a, b, 30.0, 100, {("s", "upload"): 1800.0}, {})
        self.assertEqual(rows, [])
        self.assertEqual(shaky, [("s", "upload")])

    def test_a_side_is_the_median_of_its_runs_and_its_spread(self):
        merged, spreads = perf.merge([{("s", "x"): 100, ("s", "y"): 10},
                                      {("s", "x"): 300, ("s", "y"): 10},
                                      {("s", "x"): 120}])
        self.assertEqual(merged, {("s", "x"): 120, ("s", "y"): 10})
        self.assertEqual(spreads, {("s", "x"): 200.0, ("s", "y"): 0.0})
        self.assertEqual(perf.spread([5]), 0.0)

    def test_the_largest_move_comes_first_whatever_its_sign(self):
        a = {("s", "slower a little"): 100, ("s", "faster"): 100, ("s", "much slower"): 100}
        b = {("s", "slower a little"): 125, ("s", "faster"): 70, ("s", "much slower"): 160}
        rows, _ = perf.moved(a, b, 20.0)
        self.assertEqual([r[0][1] for r in rows], ["much slower", "faster", "slower a little"])


class Output(unittest.TestCase):
    A = {("clear", "red"): 8000, ("clear", "cyan"): 250, ("fmath", "add"): 300}
    B = {("clear", "red"): 8100, ("clear", "cyan"): 400, ("format", "r32"): 90}

    def test_the_table_of_one_run(self):
        self.assertEqual(perf.table_one(self.A, ["clear", "fmath"]), [
            "suite         timed   total_us      min   median      max",
            "clear             2       8250      250     4125     8000",
            "fmath             1        300      300      300      300",
        ])

    def test_the_comparison(self):
        lines = perf.compare(self.A, ["clear", "fmath"], self.B, ["clear", "format"], 20.0)
        self.assertEqual(lines, [
            "cases that moved more than 20 percent and 0 us: 1 of 2 timed in both",
            "    +60.0%      250 -> 400      clear: cyan",
            "only in a: 1",
            "  fmath: add",
            "only in b: 1",
            "  format: r32",
            "",
            "suite         timed    total_a    total_b    change median_a median_b    change",
            "clear             2       8250       8500     +3.0%     4125     4250     +3.0%",
            "fmath           1/0        300          0 only in a      300        0 only in a",
            "format          0/1          0         90 only in b        0       90 only in b",
        ])


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
            code = perf.main(list(args))
        return code, out.getvalue(), err.getvalue()

    def test_one_report_a_side_end_to_end(self):
        a = self.report("a.jsonl", case("clear", "red", 1000), '{"type":"sum')
        b = self.report("b.jsonl", case("clear", "red", 1100))
        code, out, err = self.run_main(a, b)
        self.assertEqual(code, 0)
        self.assertIn("a: 1 report, b: 1 report", out)
        self.assertIn("moved more than 20 percent and 100 us: 0 of 1", out)
        self.assertIn("1 unreadable lines skipped", err)
        code, out, _ = self.run_main(a, b, "--threshold", "5", "--floor", "50")
        self.assertIn("+10.0%     1000 -> 1100     clear: red", out)
        code, out, _ = self.run_main("--threshold=5", "--floor=100", a, b)
        self.assertIn("moved more than 5 percent and 100 us: 0 of 1", out)
        code, out, _ = self.run_main(a)
        self.assertIn("clear             1       1000", out)

    def test_a_side_is_each_case_s_median_over_its_runs(self):
        a1 = self.report("a1", case("clear", "red", 1300), case("clear", "cyan", 250),
                         case("coherence", "upload", 2300))
        a2 = self.report("a2", case("clear", "red", 1000), case("clear", "cyan", 300),
                         case("coherence", "upload", 120))
        a3 = self.report("a3", case("clear", "red", 1200), case("depth", "only here", 90),
                         case("coherence", "upload", 2200))
        b1 = self.report("b1", case("clear", "red", 1700), case("clear", "cyan", 240),
                         case("coherence", "upload", 2250))
        b2 = self.report("b2", case("clear", "red", 1600), case("clear", "cyan", 400),
                         case("coherence", "upload", 2240))
        b3 = self.report("b3", case("clear", "red", 1650), case("clear", "cyan", 260),
                         case("coherence", "upload", 2260))
        merged, spreads, order = perf.side([a1, a2, a3])
        self.assertEqual(merged, {("clear", "red"): 1200, ("clear", "cyan"): 275,
                                  ("depth", "only here"): 90, ("coherence", "upload"): 2200})
        self.assertEqual(order, ["clear", "coherence", "depth"])
        code, out, _ = self.run_main(a1, a2, a3, "--", b1, b2, b3)
        self.assertEqual(code, 0)
        self.assertIn("a: 3 reports, b: 3 reports", out)
        self.assertIn("moved more than 20 percent and 100 us: 1 of 3 timed in both", out)
        self.assertIn("+37.5%     1200 -> 1650     clear: red", out)
        self.assertIn("not compared: 1\n  coherence: upload", out)
        code, out, _ = self.run_main(a1, a2, a3, "--")
        self.assertEqual(code, 0)
        self.assertIn("clear             2       1475      275      737     1200", out)

    def test_the_arguments_that_are_refused(self):
        a = self.report("a", case("clear", "red", 1000))
        for args in ([a, a, a], ["--", a], [a, "--", a, "--", a], [], [a, "--threshold", "x"],
                     [a, "--floor"], [a, a, "--threshold", "nan"], [a, a, "--threshold=-1"],
                     [a, a, "--floor", "-1"], [a, a, "--unsteady", "inf"]):
            code, out, err = self.run_main(*args)
            self.assertEqual(code, 1, args)
            self.assertEqual(out, "")
            self.assertTrue(err.startswith("perf.py: "), err)

    def test_nothing_timed_is_said_and_fails(self):
        timed = self.report("timed", case("clear", "red", 1000))
        empty = self.report("empty")
        untimed = self.report("notime", '{"type":"note","text":"mode: quick"}\n',
                              case("clear", "red"), '{"type":"summary","passed":1,"failed":0}\n')
        for args in ([empty], [untimed], [timed, untimed], [untimed, timed],
                     [timed, "--", empty, untimed], [timed, untimed, empty, "--", timed],
                     [timed, timed, untimed, "--", timed], [timed, "--", timed, timed, empty]):
            code, out, err = self.run_main(*args)
            self.assertEqual(code, 1, args)
            self.assertEqual(out, "")
            self.assertIn("no timed cases", err)

    def test_a_byte_that_is_not_utf8_costs_only_its_line(self):
        path = os.path.join(self.tmp.name, "binary.jsonl")
        with open(path, "wb") as handle:
            handle.write(case("clear", "red", 1000).encode() + b"\xff\xfe junk\n"
                         + case("clear", "cyan", 250).encode())
        code, out, err = self.run_main(path)
        self.assertEqual(code, 0)
        self.assertIn("clear             2       1250", out)
        self.assertIn("1 unreadable lines skipped", err)

    def test_a_missing_file_is_a_message_not_a_trace(self):
        code, out, err = self.run_main("/nonexistent/report.jsonl")
        self.assertEqual(code, 1)
        self.assertIn("/nonexistent/report.jsonl", err)


if __name__ == "__main__":
    unittest.main()
