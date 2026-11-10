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
# case and aborts; `hang <suite> <case>` starts it and sleeps; `lost <suite> <case>` starts it,
# logs Eden's device loss, reports two more cases and sleeps; `quoted` reports a case whose detail
# says device lost and finishes; `talk` finishes a run of one timed suite of two cases, the first
# failing, with the emulator's own lines between the probe's; `timed` finishes a run whose two
# cases carry a time, one of them 10 us more each launch; `finish` ends the run with a
# summary; `early` exits 1 having written nothing; `step` ends one more case and aborts outside
# any, so every launch adds to the journal. Every launch but an early one names its folder in
# last-run first, as the probe does: the one in fake-folder, or runs/fake; `named` exits 1 right
# after, having touched no journal.
FAKE = r'''#!/usr/bin/env python3
import os, sys, time
if "--version" in sys.argv:
    # what eden-cli logs, colours and all, as its version
    print("\x1b[0;37m[   0.002693] Frontend <Info> core/launch_params.cpp:184:ApplyLaunchParams: "
          "Eden HEAD 0123abcdef-HEAD\x1b[0m", file=sys.stderr)
    sys.exit(0)
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
# As a frontend does: it reads its settings, and writes them on the way out. What it found, and
# where, goes on the card for the test.
config = os.environ["XDG_CONFIG_HOME"]
ini = os.path.join(config, "eden", "fake-config.ini")
write(os.path.join(card, "fake-settings"), "%s\n%s\n" % (
    config, read(ini).strip() if os.path.exists(ini) else "defaults"))
os.makedirs(os.path.dirname(ini), exist_ok=True)
write(ini, "written by %s\n" % os.path.basename(sys.argv[0]))
what = lines[min(n, len(lines)) - 1].split(" ", 2)
print("\x1b[1;33mEDENPROBE fake launch %d: %s\x1b[0m" % (n, what[0]), flush=True)
if what[0] == "early":
    sys.exit(1)
folder_path = os.path.join(card, "fake-folder")
folder = read(folder_path).strip() if os.path.exists(folder_path) else "runs/fake"
os.makedirs(os.path.join(card, folder), exist_ok=True)
write(os.path.join(card, "last-run"), folder + "\n")
if what[0] == "named":
    sys.exit(1)
journal = os.path.join(card, folder, "journal")
report = os.path.join(card, folder, "report.jsonl")
def add(path, text):
    with open(path, "a") as f:
        f.write(text)
if not os.path.exists(journal) or n == 1:
    write(journal, "run 0.0.0 fake quick\n")
    write(report, '{"type":"note","text":"starting"}\n')
# As the probe: a launch that finds a runner's timeout right after a case's start says which case.
before = [l.rstrip("\t") for l in read(journal).split("\n") if l]
if len(before) > 1 and before[-1].startswith("timeout ") and before[-2].startswith("start "):
    _, suite, number, name = before[-2].split(" ", 3)
    add(journal, "launch %d\ntimedout %s %s %s %s\n" % (n, suite, number,
                                                        before[-1].split(" ", 1)[1], name))
else:
    add(journal, "launch %d\n" % n)
if what[0] in ("crash", "hang", "lost"):
    add(journal, "step %s 0 0 0 0 0\nstart %s 1 %s\n" % (what[1], what[1], what[2]))
    if what[0] == "lost":
        print("\x1b[1;35m[  12.345678] Render.Vulkan <Critical> video_core/vulkan_common/"
              "vulkan_device.cpp:898:ReportLoss: Device loss occurred!\x1b[0m", flush=True)
        for name in ("drawn after the loss", "and another"):
            line = '{"type":"case","suite":"%s","name":"%s","tier":"exact","result":"fail","detail":""}' % (what[1], name)
            add(report, line + "\n")
            print("[  12.4] Debug.Emulated <Info> svc_debug_string.cpp:25:OutputDebugString: " + line,
                  flush=True)
        # the last line with its newline held back, as Eden leaves it
        add(report, '{"type":"note","text":"cut"}')
    if what[0] != "crash":
        time.sleep(3600)
    print("\x1b[1;31m[   3.0] Core.ARM <Critical> core/arm/dynarmic.cpp:77:ExceptionRaised: "
          "Unsupported instruction\x1b[0m", flush=True)
    os.abort()
if what[0] == "timed":
    case = '{"type":"case","suite":"%s","name":"%s","tier":"exact","result":"pass","detail":"","us":%d}\n'
    add(report, case % ("occlusion", "a query", 1000 + 10 * n) + case % ("clear", "red", 200) +
        '{"type":"case","suite":"clear","name":"untimed","tier":"none","result":"pass","detail":""}\n'
        '{"type":"summary","passed":3,"failed":0,"skipped":0,"crashed":0}\n')
    add(journal, "finished 3 0 0 0\n")
    sys.exit(0)
if what[0] == "talk":
    def emulator(text):
        print("\x1b[1;31m[   1.0] Render.Vulkan <Error> shader.cpp:9:Build: %s\x1b[0m" % text, flush=True)
    def probe(text, to_report=True):
        if to_report:
            add(report, text + "\n")
        print("[   1.0] Debug.Emulated <Info> svc_debug_string.cpp:25:OutputDebugString: " + text,
              flush=True)
    case = '{"type":"case","suite":"one","name":"%s","tier":"exact","result":"%s","detail":""}'
    emulator("booting")
    probe('{"type":"note","text":"timing one: 1 warm-up and 1 rehearsal before the verdict pass"}')
    probe("EDENPROBE TIMING one pass 1 case number 1", False)
    emulator("IMUL32I not implemented")
    probe("EDENPROBE TIMING one pass 1 case number 2 after Bitwise AND, 4x4", False)
    emulator("said in the second case's warm-up")
    probe("EDENPROBE TIMING one pass 1 case number 3 after another", False)
    emulator("closing the device")
    probe("EDENPROBE TIMING one pass 2 case Bitwise AND, 4x4", False)
    emulator("pass two of the first")
    probe("EDENPROBE TIMING one pass 2 case another", False)
    probe("EDENPROBE TIMING one pass 2 case number 3 after another", False)
    emulator("every pixel 0x80000000")
    probe(case % ("Bitwise AND, 4x4", "fail"))
    emulator("said in the second case")
    probe(case % ("another", "pass"))
    add(journal, "finished 1 1 0 0\n")
    probe('{"type":"summary","passed":1,"failed":1,"skipped":0,"crashed":0}')
    sys.exit(0)
if what[0] == "quoted":
    print('[  1.0] Debug.Emulated <Info> svc_debug_string.cpp:25:OutputDebugString: {"type":"case",'
          '"suite":"one","name":"a case","tier":"none","result":"fail","detail":"the driver said '
          'VK_ERROR_DEVICE_LOST"}', flush=True)
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

    def test_a_hang_is_stopped_at_its_time_and_marked_as_a_timeout(self):
        f = self.fixture(["hang fill 128 quads", "finish"])
        started = time.monotonic()
        code, said = f.run(timeout=1)
        self.assertEqual(code, 0, said)
        self.assertLess(time.monotonic() - started, 30)
        self.assertRegex(said, r'launch 1 was stopped after 1 s inside fill case "128 quads" '
                               r'\(the warm-up\), [0-9]+ s after the journal last grew: the journal '
                               r'says timeout 1 [0-9]+, so the next launch skips that case as '
                               r'timeout, not as crashed; launching again \(2 of at most 17\)')
        # The runner's record follows the case's start, and the launch after names the case.
        journal = read(os.path.join(f.card, "runs", "fake", "journal"))
        self.assertRegex(journal, r"start fill 1 128 quads\ntimeout 1 [0-9]+\t\nlaunch 2\n"
                                  r"timedout fill 1 1 [0-9]+ 128 quads\n")
        self.assertIn('skipped as timeout 1, a launch stopped inside each at its limit of 1 s: '
                      'fill "128 quads"', said)
        self.assertNotIn("in this run:", said)

    def test_a_crash_is_not_marked_as_a_timeout(self):
        f = self.fixture(["crash bcn a case", "finish"])
        code, said = f.run()
        self.assertEqual(code, 0, said)
        self.assertNotIn("timeout", read(os.path.join(f.card, "runs", "fake", "journal")))
        self.assertNotIn("timeout", said)

    def test_a_device_loss_ends_the_run_and_is_said(self):
        f = self.fixture(["lost tex8k a copy", "finish"])
        started = time.monotonic()
        code, said = f.run()
        self.assertEqual(code, 1, said)
        self.assertLess(time.monotonic() - started, 30)
        self.assertEqual(f.launches(), 1)
        lost = ('launch 1: the emulator said its graphics device was lost inside tex8k case "a copy": '
                '"[  12.345678] Render.Vulkan <Critical> video_core/vulkan_common/vulkan_device.cpp:898:'
                'ReportLoss: Device loss occurred!"')
        self.assertIn(lost + "; stopped there and not launched again, since what an emulator draws "
                      "after a device loss is not its own work: 2 case lines came after that line "
                      "and are no results; the report's last line says so; that case goes on the "
                      "crash list at the next run", said)
        self.assertIn("the run ended at a device loss, so the report has no summary to trust", said)
        # The report's last line is the runner's note, on a line of its own, and it parses.
        folder = os.path.join(f.card, "runs", "fake")
        lines = read(os.path.join(folder, "report.jsonl")).split("\n")
        self.assertEqual(lines[-1], "")
        self.assertEqual(lines[-3], '{"type":"note","text":"cut"}')
        note = run.json.loads(lines[-2])
        self.assertEqual(note["type"], "note")
        self.assertEqual(note["text"], "run.py: " + lost + "; the run was stopped there, with no "
                         "summary: 2 case lines above came after it and are no results")
        # No word in the journal: the case is the one the launch ended inside, for the crash list.
        journal = read(os.path.join(folder, "journal"))
        self.assertTrue(journal.endswith("start tex8k 1 a copy\n"), journal)

    def test_a_case_that_quotes_a_loss_is_no_loss(self):
        f = self.fixture(["quoted"])
        code, said = f.run()
        self.assertEqual(code, 0, said)
        self.assertNotIn("device was lost", said)

    def test_what_reads_as_a_device_loss(self):
        for line in (b"ReportLoss: Device loss occurred!", b"vk::Check: VK_ERROR_DEVICE_LOST",
                     b"Vulkan: ErrorDeviceLost"):
            self.assertTrue(run.LOST.search(line), line)
        # Another device's loss, and words that only look like one.
        for line in (b"Audio device lost, reopening", b"the input device was lost",
                     b"BC7's loss on the picture", b"lossy format", b"device opened"):
            self.assertFalse(run.LOST.search(line), line)

    def test_the_emulators_words_are_kept_whole_and_beside_each_case_that_did_not_pass(self):
        f = self.fixture(["talk"])
        code, said = f.run()
        self.assertEqual(code, 0, said)
        folder = os.path.join(f.card, "runs", "fake")
        line = "[   1.0] Render.Vulkan <Error> shader.cpp:9:Build: %s\n"
        # The failing case: what the emulator said in each of its passes, the warm-up's found by
        # its place in the suite, and nothing of the other case's or the device's closing.
        self.assertEqual(read(os.path.join(folder, "logs", "one", "bitwise-and-4x4.log")),
                         "one: Bitwise AND, 4x4: fail\n"
                         "== launch 1, pass 1 (the warm-up)\n" + line % "IMUL32I not implemented" +
                         "== launch 1, pass 2\n" + line % "pass two of the first" +
                         "== launch 1, up to its line in the report\n" +
                         line % "every pixel 0x80000000")
        self.assertEqual(os.listdir(os.path.join(folder, "logs", "one")), ["bitwise-and-4x4.log"])
        whole = read(os.path.join(folder, "emulator.log"))
        self.assertTrue(whole.startswith("== launch 1\n"), whole[:80])
        for text in ("booting", "closing the device", "said in the second case",
                     "EDENPROBE TIMING one pass 2 case another", '"name":"another"'):
            self.assertIn(text, whole)
        self.assertNotIn("\x1b", whole)
        self.assertIn("the emulator's output: " + os.path.join(folder, "emulator.log"), said)
        self.assertIn("of the 1 case that did not pass, the emulator said something during 1: " +
                      os.path.join(folder, "logs"), said)

    def test_what_a_launch_said_last_is_the_case_s_it_ended_inside(self):
        f = self.fixture(["crash bcn A Case", "finish"])
        code, said = f.run()
        self.assertEqual(code, 0, said)
        folder = os.path.join(f.card, "runs", "fake")
        self.assertEqual(read(os.path.join(folder, "logs", "bcn", "a-case.log")),
                         "bcn: A Case: the launch ended inside it\n"
                         "== launch 1, pass 1 (the warm-up), where the launch ended by signal 6 "
                         "(SIGABRT)\n"
                         "[   3.0] Core.ARM <Critical> core/arm/dynarmic.cpp:77:ExceptionRaised: "
                         "Unsupported instruction\n")
        whole = read(os.path.join(folder, "emulator.log"))
        self.assertEqual(whole.count("== launch "), 2)
        self.assertIn("== launch 2\n", whole)

    def test_an_output_with_none_of_the_reports_lines_is_kept_whole_and_said(self):
        f = self.fixture(["finish"])
        code, said = f.run()
        self.assertEqual(code, 0, said)
        folder = os.path.join(f.card, "runs", "fake")
        self.assertTrue(os.path.isfile(os.path.join(folder, "emulator.log")))
        self.assertFalse(os.path.exists(os.path.join(folder, "logs")))
        self.assertIn("that output carried none of the report's lines, so it is not cut by case", said)

    def test_two_cases_one_name_would_share_get_a_file_each(self):
        logs = run.CaseLogs()
        logs.begin(1)
        case = b'x: {"type":"case","suite":"s","name":"%s","tier":"none","result":"fail","detail":""}'
        for name in (b"a b", b"A-B", b"passes"):
            logs.feed(b"said during " + name)
            logs.feed((case % name).replace(b'"fail"', b'"pass"') if name == b"passes" else case % name)
        logs.feed(b'x: {"type":"case","suite":"s","name":"cut')
        logs.end(None, "exited 0")
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(logs.write(d), (2, 2))
            self.assertEqual(sorted(os.listdir(os.path.join(d, "logs", "s"))), ["a-b-2.log", "a-b.log"])
            self.assertEqual(read(os.path.join(d, "logs", "s", "a-b-2.log")),
                             "s: A-B: fail\n== launch 1, up to its line in the report\nsaid during A-B\n")
        self.assertEqual(run.stem("The 8192x8192 BC7 texture's mip tail, levels 8 to 13"),
                         "the-8192x8192-bc7-texture-s-mip-tail-levels-8-to-13")
        self.assertEqual(run.stem("!!"), "case")

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
        self.assertIn("it named no run folder in last-run, so no journal to go on with", said)
        self.assertIn("so it left no report", said)

    def test_the_report_is_read_from_the_folder_last_run_names(self):
        f = self.fixture(["crash bcn a case", "finish"])
        write(os.path.join(f.card, "fake-folder"), "runs/b7172fc_phone\n")
        os.makedirs(os.path.join(f.card, "runs", "other"))
        write(os.path.join(f.card, "runs", "other", "report.jsonl"),
              '{"type":"summary","passed":9,"failed":0,"skipped":0,"crashed":0}\n')
        code, said = f.run()
        self.assertEqual(code, 0, said)
        self.assertEqual(f.launches(), 2)
        self.assertIn('inside bcn case "a case"', said)
        self.assertIn("passed 1, failed 0", said)
        self.assertIn(os.path.join(f.card, "runs", "b7172fc_phone", "report.jsonl"), said)
        # The other folder's report went with the leftovers, and its folder stayed.
        self.assertFalse(os.path.exists(os.path.join(f.card, "runs", "other", "report.jsonl")))
        self.assertTrue(os.path.isdir(os.path.join(f.card, "runs", "other")))

    def test_a_journal_unchanged_since_before_the_first_launch_ends_it(self):
        # The folder's journal as an earlier run left it is what the first launch is compared
        # with: one that names the folder and dies before writing anything is not relaunched.
        f = self.fixture(["named"])
        folder = os.path.join(f.card, "runs", "fake")
        os.makedirs(folder)
        write(os.path.join(folder, "journal"), "run 0.0.0 fake quick\nlaunch 1\n")
        code, said = f.run()
        self.assertEqual(code, 1, said)
        self.assertEqual(f.launches(), 1)
        self.assertIn("it left the journal as it was", said)

    def test_last_run_names_a_folder_under_runs_and_nothing_else(self):
        with tempfile.TemporaryDirectory() as sd:
            for text, folder in (("runs/b7172fc\n", "runs/b7172fc"),
                                 ("runs/b7172fc-dirty_eden-1.2\n", "runs/b7172fc-dirty_eden-1.2"),
                                 ("runs/..\n", None), ("../../etc\n", None),
                                 ("runs/a/b\n", None), ("runs/\n", None),
                                 ("runs/a b\n", None), ("", None)):
                write(os.path.join(sd, "last-run"), text)
                want = os.path.join(sd, folder) if folder else None
                self.assertEqual(run.run_folder(sd), want, text)
            os.remove(os.path.join(sd, "last-run"))
            self.assertIsNone(run.run_folder(sd))

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
        write(os.path.join(f.card, "last-run"), "runs/a\n")
        for name in ("a", "b_label"):
            folder = os.path.join(f.card, "runs", name)
            os.makedirs(os.path.join(folder, "images", "clear"))
            os.makedirs(os.path.join(folder, "logs", "clear"))
            write(os.path.join(folder, "emulator.log"), "old\n")
            write(os.path.join(folder, "report.jsonl"), "old\n")
            write(os.path.join(folder, "journal"), "run 0.0.0 old quick\nfinished 0 0 0 0\n")
        run.prepare(f.xdg, "quick fatal")
        self.assertFalse(os.path.exists(os.path.join(f.card, "images")))
        self.assertFalse(os.path.exists(os.path.join(f.card, "report.jsonl")))
        self.assertFalse(os.path.exists(os.path.join(f.card, "last-run")))
        for name in ("a", "b_label"):
            folder = os.path.join(f.card, "runs", name)
            self.assertFalse(os.path.exists(os.path.join(folder, "images")))
            self.assertFalse(os.path.exists(os.path.join(folder, "logs")))
            self.assertFalse(os.path.exists(os.path.join(folder, "emulator.log")))
            self.assertFalse(os.path.exists(os.path.join(folder, "report.jsonl")))
            self.assertTrue(os.path.exists(os.path.join(folder, "journal")))
        self.assertEqual(read(os.path.join(f.card, "mode")), "quick fatal\n")
        run.prepare(f.xdg, "")
        self.assertFalse(os.path.exists(os.path.join(f.card, "mode")))


class Rounds(unittest.TestCase):
    """--rounds: the whole run several times, each round's report kept and the times summed up."""

    def setUp(self):
        self.f = Fixture(["timed"])
        self.folder = os.path.join(self.f.card, "runs", "fake")

    def tearDown(self):
        self.f.close()

    def test_each_round_is_kept_and_the_times_summed_up(self):
        # What an earlier call's rounds left must not pass for these.
        os.makedirs(os.path.join(self.folder, "rounds"))
        write(os.path.join(self.folder, "rounds", "report-9.jsonl"), "old\n")
        code, said = self.f.run(rounds=3)
        self.assertEqual(code, 0, said)
        self.assertEqual(self.f.launches(), 3)
        rounds = os.path.join(self.folder, "rounds")
        self.assertEqual(sorted(os.listdir(rounds)),
                         ["report-1.jsonl", "report-2.jsonl", "report-3.jsonl", "times.tsv"])
        for number in (1, 2, 3):
            self.assertIn('"name":"a query","tier":"exact","result":"pass","detail":"","us":%d}' %
                          (1000 + 10 * number), read(os.path.join(rounds, "report-%d.jsonl" % number)))
        # The folder's own report is the last round's.
        self.assertEqual(read(os.path.join(self.folder, "report.jsonl")),
                         read(os.path.join(rounds, "report-3.jsonl")))
        self.assertEqual(read(os.path.join(rounds, "times.tsv")),
                         "suite\tcase\truns\tmedian_us\tmean_us\tmin_us\tmax_us\tspread_percent\n"
                         "occlusion\ta query\t3\t1020\t1020\t1010\t1030\t2.0\n"
                         "clear\tred\t3\t200\t200\t200\t200\t0.0\n")
        for text in ("round 1 of 3", "round 3 of 3",
                     "3 rounds, 2 timed cases; each case's spread is from its fastest round to its "
                     "slowest, and a change under it is not told from one round to the next:",
                     "  suite         timed  median_spread  max_spread  the case with it",
                     "  occlusion         1           2.0%        2.0%  a query",
                     "  clear             1           0.0%        0.0%  red",
                     "every case's median, mean, fastest, slowest and spread: " +
                     os.path.join(rounds, "times.tsv"),
                     "tools/perf.py " + os.path.join(rounds, "report-*.jsonl") +
                     " -- THE-OTHER/rounds/report-*.jsonl"):
            self.assertIn(text, said)

    def test_one_round_is_the_run_it_always_was(self):
        code, said = self.f.run(rounds=1)
        self.assertEqual(code, 0, said)
        self.assertEqual(self.f.launches(), 1)
        self.assertFalse(os.path.exists(os.path.join(self.folder, "rounds")))
        self.assertNotIn("round 1", said)

    def test_a_round_that_does_not_finish_ends_them_with_the_ones_before_kept(self):
        self.f.scenario(["timed", "early"])
        code, said = self.f.run(rounds=3)
        self.assertEqual(code, 1, said)
        self.assertEqual(self.f.launches(), 2)
        self.assertIn("round 2 of 3 did not end on its summary (exit 1), so the rounds end there "
                      "with 1 kept in " + os.path.join(self.folder, "rounds"), said)
        self.assertEqual(os.listdir(os.path.join(self.folder, "rounds")), ["report-1.jsonl"])

    def test_rounds_that_time_nothing(self):
        code, said = self.f.run(rounds=2, mode="quick NoTime")
        self.assertEqual(code, 2, said)
        self.assertFalse(os.path.exists(os.path.join(self.f.card, "fake-launches")))
        self.assertIn("rounds=2 with the mode word notime would time nothing 2 times: rounds are "
                      "for the times, so nothing was run", said)
        # A family that times no case: the rounds run, and it is said.
        self.f.scenario(["finish"])
        code, said = self.f.run(rounds=2)
        self.assertEqual(code, 0, said)
        self.assertIn("no case of these 2 rounds carries a time (their families time none), so "
                      "there is nothing to sum up; their reports are in " +
                      os.path.join(self.folder, "rounds"), said)
        self.assertFalse(os.path.exists(os.path.join(self.folder, "rounds", "times.tsv")))

    def test_rounds_must_be_one_or_more(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), self.assertRaises(SystemExit) as refused:
            run.main(["--emulator", self.f.emulator, "--nro", self.f.nro, "--xdg", self.f.xdg,
                      "--rounds", "0"])
        self.assertEqual(refused.exception.code, 2)
        self.assertIn("--rounds 0 is not a whole number from 1 up: one run is --rounds 1",
                      err.getvalue())


class Machine(unittest.TestCase):
    """The machine= word make run puts beside the mode's words, for the ban list."""

    def setUp(self):
        self.f = Fixture(["finish"])

    def tearDown(self):
        self.f.close()

    def mode_after(self, **kwargs):
        code, said = self.f.run(**kwargs)
        self.assertEqual(code, 0, said)
        return read(os.path.join(self.f.card, "mode")).strip(), said

    def test_an_eden_is_asked_its_version(self):
        mode, said = self.mode_after()
        self.assertEqual(mode, "quick machine=eden@0123abcdef")
        self.assertIn("machine=eden@0123abcdef, from %s --version" % self.f.emulator, said)
        self.assertEqual(self.f.launches(), 1)

    def test_machine_given_wins(self):
        mode, said = self.mode_after(machine="scarletnx@10ed344")
        self.assertEqual(mode, "quick machine=scarletnx@10ed344")
        self.assertIn("machine=scarletnx@10ed344, from --machine", said)

    def test_a_mode_that_names_one_keeps_it(self):
        mode, said = self.mode_after(mode="quick cpu machine=console", machine="eden")
        self.assertEqual(mode, "quick cpu machine=console")
        self.assertIn("the mode names its machine", said)

    def test_another_emulator_gets_none_and_it_is_said(self):
        self.f.emulator = self.f.make_emulator("someemu")
        mode, said = self.mode_after()
        self.assertEqual(mode, "quick")
        self.assertIn("no machine= word: %s is no emulator known by its name (eden, ryujinx)" %
                      self.f.emulator, said)
        self.assertIn("MACHINE=<name>[@<version>] names it", said)

    def test_ryujinx_by_its_name_with_no_version(self):
        self.f.emulator = self.f.make_emulator("Ryujinx")
        mode, _ = self.mode_after()
        self.assertEqual(mode, "quick machine=ryujinx")

    def test_an_eden_that_says_no_version(self):
        self.f.emulator = self.f.make_emulator("eden-quiet", "")
        with open(self.f.emulator) as f:
            text = f.read().replace("Eden HEAD 0123abcdef-HEAD", "Eden")
        with open(self.f.emulator, "w") as f:
            f.write(text)
        mode, said = self.mode_after()
        self.assertEqual(mode, "quick machine=eden")
        self.assertIn("machine=eden, with no version", said)

    def test_a_release_number(self):
        path = os.path.join(self.f.dir, "eden-release")
        write(path, "#!/bin/sh\necho '[ 0.1] Frontend <Info> launch_params.cpp:184:ApplyLaunchParams: "
                    "Eden v0.1.0 (stable)'\n")
        os.chmod(path, 0o755)
        self.assertEqual(run.eden_version(path, self.f.xdg), "v0.1.0")

    def test_no_mode_file_no_word(self):
        code, said = self.f.run(mode="")
        self.assertIn("no mode file, so no machine= word", said)
        self.assertFalse(os.path.exists(os.path.join(self.f.card, "mode")))


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


class Settings(unittest.TestCase):
    """Each emulator binary runs from settings of its own, never another build's or the
    person's."""

    def setUp(self):
        self.f = Fixture(["finish"])

    def tearDown(self):
        self.f.close()

    def seen(self):
        return read(os.path.join(self.f.card, "fake-settings")).splitlines()

    def test_each_binary_starts_from_its_own_defaults(self):
        a = self.f.emulator
        code, said = self.f.run()
        self.assertEqual(code, 0, said)
        folder_a = os.path.join(self.f.xdg, "settings", run.list_key(a))
        self.assertEqual(self.seen(), [folder_a, "defaults"])
        self.assertIn("settings: %s, this binary's own, made empty: the emulator starts from its "
                      "defaults" % folder_a, said)
        # Another build: what the first wrote on its way out is not its starting point.
        self.f.emulator = self.f.make_emulator("eden-cli-other", "\n# another build\n")
        code, said = self.f.run()
        self.assertEqual(code, 0, said)
        folder_b = os.path.join(self.f.xdg, "settings", run.list_key(self.f.emulator))
        self.assertNotEqual(folder_a, folder_b)
        self.assertEqual(self.seen(), [folder_b, "defaults"])
        # The first build again: its own settings, as it left them.
        self.f.emulator = a
        code, said = self.f.run()
        self.assertEqual(code, 0, said)
        self.assertEqual(self.seen(), [folder_a, "written by eden-cli-fake"])
        self.assertIn("settings: %s, this binary's own, as its earlier runs left it" % folder_a,
                      said)

    def test_a_rebuild_starts_from_its_defaults_again(self):
        code, said = self.f.run()
        self.assertEqual(code, 0, said)
        os.utime(self.f.emulator, ns=(0, os.stat(self.f.emulator).st_mtime_ns + 1000000000))
        code, said = self.f.run()
        self.assertEqual(code, 0, said)
        self.assertEqual(self.seen()[1], "defaults")

    def test_the_shared_folder_of_before_is_left_unread_and_said(self):
        shared = os.path.join(self.f.xdg, "config", "eden")
        os.makedirs(shared)
        write(os.path.join(shared, "fake-config.ini"), "a person's choice\n")
        code, said = self.f.run()
        self.assertEqual(code, 0, said)
        self.assertEqual(self.seen()[1], "defaults")
        self.assertEqual(read(os.path.join(shared, "fake-config.ini")), "a person's choice\n")
        self.assertIn("%s, the settings every binary shared before, is not read" %
                      os.path.join(self.f.xdg, "config"), said)

    def test_the_version_is_asked_with_the_binary_settings(self):
        env = run.emulator_env(self.f.xdg, self.f.emulator)
        self.assertEqual(env["XDG_CONFIG_HOME"],
                         os.path.join(self.f.xdg, "settings", run.list_key(self.f.emulator)))
        self.assertEqual(env["XDG_DATA_HOME"], os.path.join(self.f.xdg, "data"))


class JournalReading(unittest.TestCase):
    def test_a_torn_last_line_is_left_out(self):
        j = run.Journal("run 0 b q\nlaunch 1\nstart clear 1 a case\nstart clear 1 half a ca")
        self.assertEqual(j.inside(), ("clear", "1", "a case"))
        self.assertEqual(len(j.records), 3)

    def test_a_start_whose_newline_an_abort_took_is_read(self):
        # Eden keeps a file's last write in a host buffer, and the newline is every record's
        # last write: an abort leaves the start whole, ending in its tab, with no newline.
        j = run.Journal("run 0 b q\t\nlaunch 1\t\nstart clear 1 a case\t")
        self.assertEqual(j.inside(), ("clear", "1", "a case"))
        self.assertEqual(j.records[-1], "start clear 1 a case")

    def test_a_start_cut_inside_its_body_is_still_left_out(self):
        j = run.Journal("run 0 b q\t\nlaunch 1\t\nend clear 1 pass a\t\nstart clear 1 half a ca")
        self.assertIsNone(j.inside())
        self.assertEqual(j.records[-1], "end clear 1 pass a")

    def test_marking_a_stop_ends_a_torn_line_first(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "journal")
            with open(path, "wb") as f:
                f.write(b"run 0 b q\t\nlaunch 1\t\nstart clear 1 a case\t")
            self.assertTrue(run.mark_stopped(path))
            j = run.Journal.read(path)
            self.assertEqual(j.records[-2:], ["start clear 1 a case", "stopped"])
            self.assertIsNone(j.inside())

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

    def test_a_timeout_is_marked_after_a_torn_line_and_read_back(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "journal")
            write(path, "run 0 b quick\nstart tex8k 0 a copy\t")
            self.assertTrue(run.mark_timeout(path, 60, 58))
            self.assertEqual(read(path), "run 0 b quick\nstart tex8k 0 a copy\t\ntimeout 60 58\t\n")
            write(path, "launch 2\ntimedout tex8k 0 60 58 a copy\t\ntimedout tex8k 0 60 58 a copy\n"
                        "crashed bcn 1 a case\n", "a")
            journal = run.Journal.read(path)
            self.assertEqual(journal.timed_out(), [("tex8k", "a copy")])
            self.assertEqual(journal.crashed(), [("bcn", "a case")])
            self.assertFalse(run.mark_timeout(os.path.join(d, "none"), 60, 58))

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
