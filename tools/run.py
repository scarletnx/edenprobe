#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs edenprobe under an emulator until its report is whole.

    tools/run.py --emulator EMULATOR --nro NRO --xdg DIR [--mode WORDS] [--relaunches N]
                 [--timeout SECONDS] [--log FILE] [--who NAME]

The emulator's settings, memory card and caches go under DIR, as XDG_CONFIG_HOME, XDG_DATA_HOME and
XDG_CACHE_HOME, so nothing of the person's own setup is read or written. The run starts clean: the
reports and pictures in every run folder (sdmc:/edenprobe/runs/*), the support suite's scratch
directory, last-run and the mode file are removed, and WORDS (default `quick`; empty for no file,
which is the detailed screen) written into a new mode file. The program names the folder it
reports in, its build's hash and the mode file's label, in sdmc:/edenprobe/last-run; the report
and the journal are read from there.

A launch whose report has no summary line ended before the run did: it crashed, or was stopped at
its time limit, which is what a hang looks like from here. The program keeps a journal on the card
and goes on with the run after the case it ended inside (README, "When the program ends in the
middle"), so the emulator is started again, up to N more times (default 16): every launch makes
the run shorter by at least that case, and a launch that adds nothing to the journal but its own
start ends the relaunching, since the next would do the same.

Each launch has SECONDS (default 60) before it is stopped, SIGTERM and then SIGKILL ten seconds
later. A person stopping this script stops the emulator the same way and writes `stopped` into
the journal, so the case it was in is not taken for a crash by the next launch.

The crash list (sdmc:/edenprobe/crashes) is kept per emulator binary, under DIR/crash-lists/, and
put on the card before the first launch and taken back after the last: one binary's crashes are
not skipped on another's run. A rebuilt binary, another size or modification time, has a list of
its own.

Every line of the emulator's output goes to FILE when given; the lines about edenprobe are printed
as they come, with ANSI colours taken off. At the end it prints the counts and, on a line of its own,
the cases skipped as crashed, and exits 0 when the report ends on its summary, 1 when it does not,
and 130 when stopped. Standard library only.
"""

import argparse
import hashlib
import os
import re
import shutil
import signal
import subprocess
import sys
import threading

ANSI = re.compile(rb"\x1b\[[0-9;]*m")
SHOWN = re.compile(rb"edenprobe|EDENPROBE|\"type\":")


def card(xdg):
    return os.path.join(xdg, "data", "eden", "sdmc", "edenprobe")


# What the program writes into last-run: one folder under runs/, never a path out of the card.
FOLDER = re.compile(r"runs/[A-Za-z0-9.-]+(_[A-Za-z0-9._-]+)?")


def run_folder(sd):
    """The folder the program's last launch reported in, as named in last-run, or None."""
    try:
        with open(os.path.join(sd, "last-run"), "rb") as f:
            text = f.read().decode("ascii", "replace").strip()
    except FileNotFoundError:
        return None
    if not FOLDER.fullmatch(text) or not text[len("runs/"):].strip("."):
        return None
    return os.path.join(sd, text)


def run_folders(sd):
    """Every run folder on the card."""
    runs = os.path.join(sd, "runs")
    if not os.path.isdir(runs):
        return []
    return [os.path.join(runs, d) for d in sorted(os.listdir(runs))
            if os.path.isdir(os.path.join(runs, d))]


class Journal:
    """The journal as the probe writes it (source/journal.h): the last run's records, a last line
    with no newline left out as a write the program did not live to finish."""

    def __init__(self, text=""):
        self.records = []
        lines = text.split("\n")
        # The piece after the last newline is a torn line, or nothing.
        for line in lines[:-1]:
            fields = line.split(" ")
            if not fields or not fields[0]:
                continue
            if fields[0] == "run":
                self.records = []
            self.records.append(line)

    @classmethod
    def read(cls, path):
        try:
            with open(path, "rb") as f:
                return cls(f.read().decode("utf-8", "replace"))
        except FileNotFoundError:
            return None

    def progress(self):
        """How much the run has done: every record but the launches."""
        return sum(1 for r in self.records if not r.startswith("launch "))

    def finished(self):
        return any(r.startswith("finished ") for r in self.records)

    def inside(self):
        """The case the program ended inside, as (suite, pass, name), when the last record is a
        case starting; None otherwise."""
        if not self.records or not self.records[-1].startswith("start "):
            return None
        parts = self.records[-1].split(" ", 3)
        if len(parts) < 4:
            return None
        return parts[1], parts[2], parts[3]

    def crashed(self):
        """The cases the launches after a crash skipped, as (suite, name)."""
        out = []
        for r in self.records:
            if r.startswith("crashed "):
                parts = r.split(" ", 3)
                if len(parts) == 4 and (parts[1], parts[3]) not in out:
                    out.append((parts[1], parts[3]))
        return out


def summary(report):
    """The summary line of a report, as a dict of its counts, or None."""
    try:
        with open(report, "rb") as f:
            lines = f.read().decode("utf-8", "replace").splitlines()
    except FileNotFoundError:
        return None
    for line in reversed(lines):
        if '"type":"summary"' in line:
            counts = {}
            for key in ("passed", "failed", "skipped", "crashed"):
                m = re.search(r'"%s":(\d+)' % key, line)
                counts[key] = int(m.group(1)) if m else 0
            return counts
    return None


def list_key(emulator):
    """The crash list's name for a binary: its name, and a hash of where it is, its size and when
    it was written, so a rebuild at the same path starts a list of its own."""
    real = os.path.realpath(emulator)
    st = os.stat(real)
    token = "%s\0%d\0%d" % (real, st.st_size, st.st_mtime_ns)
    return "%s-%s" % (os.path.basename(real), hashlib.sha1(token.encode()).hexdigest()[:12])


def swap_in(xdg, emulator):
    store = os.path.join(xdg, "crash-lists", list_key(emulator))
    on_card = os.path.join(card(xdg), "crashes")
    if os.path.isfile(store):
        shutil.copyfile(store, on_card)
    elif os.path.isfile(on_card):
        os.remove(on_card)
    return store


def swap_out(xdg, store):
    on_card = os.path.join(card(xdg), "crashes")
    if os.path.isfile(on_card):
        os.makedirs(os.path.dirname(store), exist_ok=True)
        shutil.copyfile(on_card, store)


def prepare(xdg, mode):
    """The directories, the previous runs' leavings taken away, and the mode file. Every run
    folder loses its report and pictures, since this script cannot know which folder the build
    it runs will name. The journals stay: the probe reads its own to decide the run is new, the
    report being gone, and lists a case the last launch of an earlier run ended inside. The
    report and the pictures at the top of the card are a build's from before run folders."""
    for d in ("data", "config", "cache"):
        os.makedirs(os.path.join(xdg, d), exist_ok=True)
    sd = card(xdg)
    os.makedirs(sd, exist_ok=True)
    for folder in [sd] + run_folders(sd):
        names = ("report.jsonl", "mode", "last-run") if folder == sd else ("report.jsonl",)
        for name in names:
            path = os.path.join(folder, name)
            if os.path.isfile(path) or os.path.islink(path):
                os.remove(path)
        names = ("images", "support-check", "mode") if folder == sd else ("images",)
        for name in names:
            path = os.path.join(folder, name)
            if os.path.isdir(path):
                shutil.rmtree(path)
    if mode:
        with open(os.path.join(sd, "mode"), "w") as f:
            f.write(mode + "\n")


def describe(status, stopped_after):
    if stopped_after is not None:
        return "was stopped after %d s" % stopped_after
    if status < 0:
        try:
            name = signal.Signals(-status).name
        except ValueError:
            name = "a signal"
        return "ended by signal %d (%s)" % (-status, name)
    return "exited %d" % status


def launch(emulator, nro, xdg, timeout, log, echo):
    """One launch. Hands back (status, seconds it was stopped after or None)."""
    env = dict(os.environ)
    env.update({
        "XDG_DATA_HOME": os.path.join(xdg, "data"),
        "XDG_CONFIG_HOME": os.path.join(xdg, "config"),
        "XDG_CACHE_HOME": os.path.join(xdg, "cache"),
    })
    env["DISPLAY"] = env.get("DISPLAY") or ":0"
    process = subprocess.Popen([emulator, "-g", nro], env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)

    def pump():
        for raw in process.stdout:
            if log is not None:
                log.write(raw)
            if echo:
                line = ANSI.sub(b"", raw)
                if SHOWN.search(line):
                    sys.stdout.buffer.write(line)
                    sys.stdout.flush()

    reader = threading.Thread(target=pump, daemon=True)
    reader.start()
    stopped_after = None
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        stopped_after = timeout
        stop(process)
    except BaseException:
        stop(process)
        raise
    reader.join(timeout=10)
    return process.returncode, stopped_after


def stop(process):
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def interrupt(signum, frame):
    raise KeyboardInterrupt


def mark_stopped(journal_path):
    """Says in the journal that the program was stopped on purpose, so the case it was in is no
    crash; nothing when there is no journal."""
    if not os.path.isfile(journal_path):
        return False
    with open(journal_path, "ab") as f:
        f.write(b"stopped\n")
        f.flush()
        os.fsync(f.fileno())
    return True


def run(emulator, nro, xdg, mode="quick", relaunches=16, timeout=60, log_path=None,
        who="run.py", echo=True):
    say = lambda text: print("%s: %s" % (who, text), flush=True)
    if not (os.path.isfile(emulator) and os.access(emulator, os.X_OK)):
        print("%s: no executable emulator at %s" % (who, emulator), file=sys.stderr)
        return 1
    prepare(xdg, mode)
    say("mode file: %s" % mode if mode else "mode file: none, so the detailed screen")
    sd = card(xdg)
    # Where the report and the journal are once a launch has named its folder. Before the first
    # launch every folder's journal is kept as it was, to compare the one it names against.
    folder = None
    report = None
    journal_path = None
    earlier = {f: Journal.read(os.path.join(f, "journal")) for f in run_folders(sd)}
    store = swap_in(xdg, emulator)
    log = open(log_path, "wb") if log_path else None
    launches = 0
    code = 1
    # A person's SIGTERM stops the run the way ctrl-C does.
    previous = signal.signal(signal.SIGTERM, interrupt)
    try:
        while True:
            before = Journal.read(journal_path) if journal_path is not None else None
            launches += 1
            status, stopped_after = launch(emulator, nro, xdg, timeout, log, echo)
            said = "launch %d %s" % (launches, describe(status, stopped_after))
            named = run_folder(sd)
            if named is None:
                say(said + "; it named no run folder in last-run, so no journal to go on with "
                    "and no relaunch")
                break
            if named != folder:
                folder = named
                report = os.path.join(folder, "report.jsonl")
                journal_path = os.path.join(folder, "journal")
                before = earlier.get(folder) if launches == 1 else None
            if summary(report) is not None:
                code = 0
                break
            after = Journal.read(journal_path)
            where = after.inside() if after is not None else None
            if where is not None:
                said += " inside %s case \"%s\"%s" % (
                    where[0], where[2],
                    " (the warm-up)" if where[1] == "1" else
                    " (a timed pass)" if where[1] != "0" else "")
            if after is None:
                say(said + "; no journal to go on with, so no relaunch")
                break
            if before is not None and after.records == before.records:
                say(said + "; it left the journal as it was, ending before the run began, so no "
                    "relaunch")
                break
            if after.finished():
                say(said + "; the journal says the run finished, but the report has no summary")
                break
            if launches > 1 and before is not None and after.progress() <= before.progress():
                say(said + "; it added nothing to the journal, so no relaunch")
                break
            if launches > relaunches:
                say(said + "; that was the last of %d relaunches" % relaunches)
                break
            say(said + "; launching again (%d of at most %d)" % (launches + 1, relaunches + 1))
    except KeyboardInterrupt:
        named = run_folder(sd)
        if named is not None:
            mark_stopped(os.path.join(named, "journal"))
        say("stopped; the journal says so, so the case under way is not taken for a crash")
        code = 130
    finally:
        signal.signal(signal.SIGTERM, previous)
        if log is not None:
            log.close()
        swap_out(xdg, store)
    if code == 130:
        return code
    if folder is None:
        print("%s: the emulator named no run folder in %s, so it left no report" %
              (who, os.path.join(sd, "last-run")), file=sys.stderr)
        return 1
    if not os.path.isfile(report):
        print("%s: the emulator left no report at %s" % (who, report), file=sys.stderr)
        return 1
    counts = summary(report)
    if counts is None:
        print("%s: no summary in the report after %d launch%s: stopped at %d s, or died first" %
              (who, launches, "" if launches == 1 else "es", timeout), file=sys.stderr)
        return 1
    journal = Journal.read(journal_path)
    crashed = journal.crashed() if journal is not None else []
    say("passed %d, failed %d, skipped %d" % (counts["passed"], counts["failed"],
                                               counts["skipped"]))
    say("crashed %d%s%s" % (counts["crashed"],
                            ", in this run: " if crashed else "",
                            ", ".join('%s "%s"' % c for c in crashed)))
    say("%d launch%s" % (launches, "" if launches == 1 else "es"))
    print("report: %s" % report, flush=True)
    return code


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--emulator", required=True)
    parser.add_argument("--nro", required=True)
    parser.add_argument("--xdg", required=True)
    parser.add_argument("--mode", default="quick")
    parser.add_argument("--relaunches", type=int, default=16)
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--log")
    parser.add_argument("--who", default="run.py")
    args = parser.parse_args(argv)
    if args.relaunches < 0 or args.timeout <= 0:
        parser.error("relaunches must be nought or more and the timeout more than nought")
    return run(os.path.abspath(args.emulator), os.path.abspath(args.nro),
               os.path.abspath(args.xdg), args.mode.strip(), args.relaunches, args.timeout,
               args.log, args.who)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
