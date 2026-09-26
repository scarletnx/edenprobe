# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/run.py starts the emulator again after a crash, a bounded number of times, and reads the
journal the probe writes. The emulator here is a script that plays one launch after another from a
scenario on the card: crash inside a case, hang, finish, or end before beginning."""

import contextlib
import importlib.util
import io
import os
import stat
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("run", os.path.join(HERE, "run.py"))
run = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(run)

# One launch per line of the scenario, the last line repeating: `crash <suite> <case>` starts the
# case and aborts; `hang <suite> <case>` starts it and sleeps; `finish` ends the run with a
# summary; `early` exits 1 having written nothing; `step` ends one more case and aborts outside
# any, so every launch adds to the journal.
FAKE = r'''#!/usr/bin/env python3
import os, sys, time
def write(path, text):
    with open(path, "w") as f:
        f.write(text)
def read(path):
    with open(path) as f:
        return f.read()
card = os.path.join(os.environ["XDG_DATA_HOME"], "eden", "sdmc", "edenprobe")
with open(os.path.join(card, "fake-scenario")) as f:
    lines = [l.strip() for l in f if l.strip()]
count_path = os.path.join(card, "fake-launches")
n = int(read(count_path)) + 1 if os.path.exists(count_path) else 1
write(count_path, str(n))
what = lines[min(n, len(lines)) - 1].split(" ", 2)
print("\x1b[1;33mEDENPROBE fake launch %d: %s\x1b[0m" % (n, what[0]), flush=True)
if what[0] == "early":
    sys.exit(1)
journal = os.path.join(card, "journal")
report = os.path.join(card, "report.jsonl")
def add(path, text):
    with open(path, "a") as f:
        f.write(text)
if not os.path.exists(journal) or n == 1:
    write(journal, "run 0.0.0 fake quick\n")
    write(report, '{"type":"note","text":"starting"}\n')
add(journal, "launch %d\n" % n)
if what[0] in ("crash", "hang"):
    add(journal, "step %s 0 0 0 0 0\nstart %s 1 %s\n" % (what[1], what[1], what[2]))
    if what[0] == "hang":
        time.sleep(3600)
    os.abort()
if what[0] == "step":
    add(journal, "end one 0 pass case %d\n" % n)
    os.abort()
add(journal, "end one 0 pass a case\nfinished 1 0 0 1\n")
add(report, '{"type":"case","suite":"one","name":"a case","tier":"none","result":"pass","detail":""}\n')
add(report, '{"type":"summary","passed":1,"failed":0,"skipped":0,"crashed":1}\n')
print("EDENPROBE DONE passed=1 failed=0 skipped=0 crashed=1", flush=True)
'''


def write(path, text, how="w"):
    with open(path, how) as f:
        f.write(text)


def read(path):
    with open(path) as f:
        return f.read()


class Fixture:
    def __init__(self, scenario):
        self.tmp = tempfile.TemporaryDirectory(prefix="edenprobe-run-test-")
        self.dir = self.tmp.name
        self.emulator = self.make_emulator("eden-cli-fake")
        self.xdg = os.path.join(self.dir, "xdg")
        self.card = run.card(self.xdg)
        os.makedirs(self.card)
        self.scenario(scenario)
        self.nro = os.path.join(self.dir, "edenprobe.nro")
        open(self.nro, "w").close()

    def make_emulator(self, name, extra=""):
        path = os.path.join(self.dir, name)
        with open(path, "w") as f:
            f.write(FAKE + extra)
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
        return path

    def scenario(self, lines):
        with open(os.path.join(self.card, "fake-scenario"), "w") as f:
            f.write("\n".join(lines) + "\n")
        count = os.path.join(self.card, "fake-launches")
        if os.path.exists(count):
            os.remove(count)

    def launches(self):
        with open(os.path.join(self.card, "fake-launches")) as f:
            return int(f.read())

    def run(self, **kwargs):
        out = io.StringIO()
        err = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = run.run(self.emulator, self.nro, self.xdg, echo=False, **kwargs)
        return code, out.getvalue() + err.getvalue()

    def close(self):
        self.tmp.cleanup()


class Relaunch(unittest.TestCase):
    def setUp(self):
        self.fixtures = []

    def tearDown(self):
        for f in self.fixtures:
            f.close()

    def fixture(self, scenario):
        f = Fixture(scenario)
        self.fixtures.append(f)
        return f

    def test_a_crash_is_followed_by_a_launch_that_finishes(self):
        f = self.fixture(["crash bcn every layer keeps its own levels", "finish"])
        code, said = f.run()
        self.assertEqual(code, 0, said)
        self.assertEqual(f.launches(), 2)
        self.assertIn('launch 1 ended by signal 6 (SIGABRT) inside bcn case "every layer keeps '
                      'its own levels" (the warm-up); launching again (2 of at most 17)', said)
        self.assertIn("crashed 1", said)
        self.assertIn("2 launches", said)

    def test_a_hang_is_stopped_at_its_time_and_counted_as_a_crash(self):
        f = self.fixture(["hang fill 128 quads", "finish"])
        started = time.monotonic()
        code, said = f.run(timeout=1)
        self.assertEqual(code, 0, said)
        self.assertLess(time.monotonic() - started, 30)
        self.assertIn('launch 1 was stopped after 1 s inside fill case "128 quads"', said)

    def test_a_launch_that_adds_nothing_ends_the_relaunching(self):
        f = self.fixture(["crash cpu sm4", "early"])
        code, said = f.run()
        self.assertEqual(code, 1, said)
        self.assertEqual(f.launches(), 2)
        self.assertIn("it left the journal as it was", said)
        self.assertIn("no summary in the report after 2 launches", said)

    def test_relaunching_is_bounded(self):
        f = self.fixture(["step"])
        code, said = f.run(relaunches=3)
        self.assertEqual(code, 1, said)
        self.assertEqual(f.launches(), 4)
        self.assertIn("that was the last of 3 relaunches", said)

    def test_no_relaunch_without_a_journal(self):
        f = self.fixture(["early"])
        code, said = f.run()
        self.assertEqual(code, 1, said)
        self.assertEqual(f.launches(), 1)
        self.assertIn("no journal to go on with", said)

    def test_no_emulator_is_said_and_nothing_runs(self):
        f = self.fixture(["finish"])
        f.emulator = os.path.join(f.dir, "missing")
        code, said = f.run()
        self.assertEqual(code, 1)
        self.assertIn("no executable emulator at", said)

    def test_the_mode_file_and_the_leftovers(self):
        f = self.fixture(["finish"])
        os.makedirs(os.path.join(f.card, "images", "clear"))
        write(os.path.join(f.card, "report.jsonl"), "old\n")
        write(os.path.join(f.card, "journal"), "run 0.0.0 old quick\nfinished 0 0 0 0\n")
        run.prepare(f.xdg, "quick fatal")
        self.assertFalse(os.path.exists(os.path.join(f.card, "images")))
        self.assertFalse(os.path.exists(os.path.join(f.card, "report.jsonl")))
        self.assertTrue(os.path.exists(os.path.join(f.card, "journal")))
        self.assertEqual(read(os.path.join(f.card, "mode")), "quick fatal\n")
        run.prepare(f.xdg, "")
        self.assertFalse(os.path.exists(os.path.join(f.card, "mode")))


class CrashLists(unittest.TestCase):
    def test_each_binary_keeps_its_own(self):
        f = Fixture(["finish"])
        try:
            a = f.emulator
            b = f.make_emulator("eden-cli-other", "\n# another binary\n")
            on_card = os.path.join(f.card, "crashes")
            store_a = run.swap_in(f.xdg, a)
            write(on_card, "bcn a case\n")
            run.swap_out(f.xdg, store_a)
            store_b = run.swap_in(f.xdg, b)
            self.assertFalse(os.path.exists(on_card))
            run.swap_out(f.xdg, store_b)
            run.swap_in(f.xdg, a)
            self.assertEqual(read(on_card), "bcn a case\n")
            self.assertNotEqual(run.list_key(a), run.list_key(b))
            # A rebuild in place is another binary.
            key = run.list_key(a)
            os.utime(a, ns=(0, os.stat(a).st_mtime_ns + 1000000000))
            self.assertNotEqual(run.list_key(a), key)
        finally:
            f.close()


class JournalReading(unittest.TestCase):
    def test_a_torn_last_line_is_left_out(self):
        j = run.Journal("run 0 b q\nlaunch 1\nstart clear 1 a case\nstart clear 1 half a ca")
        self.assertEqual(j.inside(), ("clear", "1", "a case"))
        self.assertEqual(len(j.records), 3)

    def test_the_case_it_ended_inside_is_the_last_start(self):
        self.assertIsNone(run.Journal("run 0 b q\nstart a 0 x\nend a 0 pass x\n").inside())
        self.assertIsNone(run.Journal("run 0 b q\nstart a 0 x\nstopped\n").inside())
        self.assertEqual(run.Journal("run 0 b q\nstart a 0 x y z\n").inside(), ("a", "0", "x y z"))

    def test_progress_counts_everything_but_launches(self):
        a = run.Journal("run 0 b q\nlaunch 1\nstep a 0 0 0 0 0\nstart a 0 x\n")
        b = run.Journal("run 0 b q\nlaunch 1\nstep a 0 0 0 0 0\nstart a 0 x\nlaunch 2\n")
        self.assertEqual(a.progress(), b.progress())
        self.assertEqual(a.progress(), 3)

    def test_a_new_run_starts_the_records_over(self):
        j = run.Journal("run 0 b q\nlaunch 1\nfinished 1 0 0 0\nrun 0 b q\nlaunch 1\n")
        self.assertFalse(j.finished())
        self.assertEqual(j.records, ["run 0 b q", "launch 1"])

    def test_crashed_cases_once_each(self):
        j = run.Journal("run 0 b q\ncrashed bcn 1 a case\ncrashed bcn 1 a case\n"
                        "crashed geometry 1 another\n")
        self.assertEqual(j.crashed(), [("bcn", "a case"), ("geometry", "another")])

    def test_stopped_is_written_only_to_a_journal_there(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "journal")
            self.assertFalse(run.mark_stopped(path))
            self.assertFalse(os.path.exists(path))
            write(path, "run 0 b q\nstart a 1 x\n")
            self.assertTrue(run.mark_stopped(path))
            self.assertIsNone(run.Journal.read(path).inside())

    def test_the_summary_counts(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "report.jsonl")
            self.assertIsNone(run.summary(path))
            write(path, '{"type":"note","text":"x"}\n')
            self.assertIsNone(run.summary(path))
            write(path, how="a",
                  text='{"type":"summary","passed":3,"failed":1,"skipped":2,"crashed":4}\n')
            self.assertEqual(run.summary(path),
                             {"passed": 3, "failed": 1, "skipped": 2, "crashed": 4})


if __name__ == "__main__":
    unittest.main()
