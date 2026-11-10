#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs edenprobe under an emulator until its report is whole.

    tools/run.py --emulator EMULATOR --nro NRO --xdg DIR [--mode WORDS] [--machine NAME[@VERSION]]
                 [--relaunches N] [--timeout SECONDS] [--rounds N] [--log FILE] [--who NAME]

The emulator's settings, memory card and caches go under DIR, as XDG_CONFIG_HOME, XDG_DATA_HOME and
XDG_CACHE_HOME, so nothing of the person's own setup is read or written. The settings are each
emulator binary's own, DIR/settings/<binary>, named as its crash list is (below): a frontend writes
its settings on the way out, and one build's written settings would otherwise be the next build's
starting point, so two builds would not run from their own defaults. A binary's first run makes
its folder empty, and the emulator starts from its defaults; its later runs keep what it, or a
person, wrote there. DIR/config, every binary's settings before, is no longer read and stays where
it is, which the run says. The run starts clean: the
reports and pictures in every run folder (sdmc:/edenprobe/runs/*), the support suite's scratch
directory, last-run and the mode file are removed, and WORDS (default `quick`; empty for no file,
which is the detailed screen) written into a new mode file, with machine=NAME[@VERSION] after them
for the ban list (docs/Crashes.md): --machine's, or the one the emulator's name says, eden asked for
its version with --version, ryujinx with none; WORDS that name a machine already keep theirs, and
an emulator of another name gets no machine word, which is said. After it goes vram=MiB, the
graphics memory this machine's card says it has of its own (the largest mem_info_vram_total under
/sys/class/drm, a file amdgpu writes): with less than the tex8k suite's textures want, the program
does not try them and says both numbers (docs/families/Graphics.md), since a host that runs out
there can lose its graphics device. WORDS that say vram= keep theirs, and a machine whose driver
says no such number gets no word, which is said: its textures are tried. The program names the folder it
reports in, its build's hash and the mode file's label, in sdmc:/edenprobe/last-run; the report
and the journal are read from there.

A launch whose report has no summary line ended before the run did: it crashed, or was stopped at
its time limit, which is what a hang looks like from here. The program keeps a journal on the card
and goes on with the run after the case it ended inside (docs/Crashes.md), so the emulator is
started again, up to N more times (default 16): every launch makes the run shorter by at least
that case, and a launch that adds nothing to the journal but its own start ends the relaunching,
since the next would do the same.

Each launch has SECONDS (default 60) before it is stopped, SIGTERM and then SIGKILL ten seconds
later. A launch stopped inside a case gets `timeout SECONDS QUIET` in the journal, QUIET being how
long the journal had not grown, which is how long that case had run: the next launch skips the
case as `timeout` with both numbers and leaves it off the crash list, since the limit is the
launch's and the case may only have been the one running when it came. A person stopping this
script stops the emulator the same way and writes `stopped` into the journal, so the case it was
in is not taken for a crash by the next launch.

The crash list (sdmc:/edenprobe/crashes) is kept per emulator binary, under DIR/crash-lists/, and
put on the card before the first launch and taken back after the last: one binary's crashes are
not skipped on another's run. A rebuilt binary, another size or modification time, has a list of
its own.

A line of the emulator's own that says its graphics device was lost (Eden's "Device loss
occurred!", the Vulkan result named DEVICE_LOST or DeviceLost) ends the run there: the emulator is stopped,
nothing is launched again, the report gets a note with that line and how many case lines came
after it, which are no results, and the exit is 1. What an emulator draws once the host's driver
has dropped its device says nothing about the emulator, and on some machines the next launch
finds the display itself reset.

The emulator's output is kept beside the report, colours taken off: all of it in emulator.log, a
launch after another, and in logs/<suite>/<case>.log what the emulator itself said while each case
that did not pass ran, in every pass and launch, cut out by the probe's own lines in that output
(CaseLogs). A failure whose cause only the emulator knows, a shader it could not translate, a job
it never ran, is then beside the case. Every line also goes to FILE as it came when --log is
given; the lines about edenprobe are printed as they come, with ANSI colours taken off. At the end it prints the counts and, on lines of their
own, the cases skipped as crashed and the ones a launch was stopped inside at its limit, and exits
0 when the report ends on its summary, 1 when it does not, 1 too when the run ended at a device loss, and 130 when stopped.

With --rounds N, more than one, the whole run is made N times, one after another, for the times:
one run is one sample of the emulator's process, and processes differ by more than a small change
does (docs/TimePerCase.md). Each round's report is kept as rounds/report-<n>.jsonl in the run's
folder, the folder's own report.jsonl, pictures and logs being the last round's; then every timed
case's median, mean, fastest, slowest and spread over the rounds go to rounds/times.tsv, and a
line a suite is printed with its cases' spreads (tools/perf.py). A round that does not end on its
summary ends the rounds with its exit status, the rounds before it kept. Refused, exit 2, with the
mode word notime, which times nothing. Standard library only.
"""

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time

ANSI = re.compile(rb"\x1b\[[0-9;]*m")
SHOWN = re.compile(rb"edenprobe|EDENPROBE|\"type\":")
# The probe's own lines in the emulator's output, its report's and its breadcrumbs, and the case
# lines among them.
PROBE = re.compile(rb"EDENPROBE|\"type\":")
CASE = re.compile(rb"\"type\":\"case\"")
# A breadcrumb before a rehearsed case (source/report.h, ReportBuildBreadcrumb): its suite and pass.
CRUMB = re.compile(rb"EDENPROBE TIMING (\S+) pass (\d+) case ")
# What an emulator logs when the host's driver has dropped its graphics device: Eden's "Device loss
# occurred!", or the Vulkan result by its name (VK_ERROR_DEVICE_LOST, ErrorDeviceLost). Names only,
# as they are spelled: "audio device lost" is another device's.
LOST = re.compile(rb"Device loss occurred|DEVICE_LOST|DeviceLost")


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
    """The journal as the probe writes it (source/journal.h): the last run's records. Every record
    ends in a tab before its newline, so a last line that ends in its tab lost only its newline
    (Eden loses a file's last write in an abort) and is read; one without the tab was cut inside
    its body and is left out."""

    def __init__(self, text=""):
        self.records = []
        lines = text.split("\n")
        # The piece after the last newline is a torn line, or nothing.
        if lines[-1].endswith("\t"):
            lines.append("")
        for line in lines[:-1]:
            if line.endswith("\t"):
                line = line[:-1]
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

    def timed_out(self):
        """The cases the launches after a stop at the time limit skipped, as (suite, name)."""
        out = []
        for r in self.records:
            if r.startswith("timedout "):
                parts = r.split(" ", 5)
                if len(parts) == 6 and (parts[1], parts[5]) not in out:
                    out.append((parts[1], parts[5]))
        return out


def stem(text):
    """A file's name for a suite or a case, as a picture's (docs/FailurePictures.md): lower case,
    every run of other characters one dash, at most 96 characters."""
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")[:96].strip("-") or "case"


class CaseLogs:
    """What the emulator said while each case ran, cut out of its output by the probe's own lines
    in it, which an emulator logs from the debug channel in the order they came.

    A case's line closes the emulator's lines before it: they came while that case ran. In the
    passes a timed suite makes before the one that reports, no case has a line, and a breadcrumb
    opens each case instead: the n-th breadcrumb of a pass is the suite's n-th case, named by the
    n-th case line the suite then reports, and the one past the last is the device closing, which
    is no case's. What a launch said after its last such line belongs to the case the journal
    says it ended inside. Lines before a suite's first breadcrumb, and anything no line of the
    probe's closes, stay in the whole log only."""

    def __init__(self):
        self.lock = threading.Lock()
        self.launch = 0
        self.output = []
        self.pending = []
        self.opened = None
        self.crumbs = {}
        self.reported = set()
        self.segments = []
        self.results = {}
        self.order = {}
        self.inside = set()
        self.markers = 0

    def begin(self, launch):
        with self.lock:
            self.launch = launch
            self.output = []
            self.pending = []
            self.opened = None
            self.crumbs = {}
            self.reported = set()

    def close(self, owner, label):
        if self.pending and owner is not None:
            self.segments.append((owner, "launch %d, %s" % (self.launch, label), self.pending))
        self.pending = []

    def feed(self, line):
        """One line of the emulator's output, colours off."""
        with self.lock:
            self.output.append(line)
            crumb = CRUMB.search(line)
            if crumb:
                if self.opened is not None:
                    self.close(*self.opened)
                else:
                    self.pending = []
                suite, number = crumb.group(1).decode("utf-8", "replace"), int(crumb.group(2))
                index = self.crumbs.get((suite, number), 0)
                self.crumbs[(suite, number)] = index + 1
                self.opened = (("index", suite, index),
                               "pass %d%s" % (number, " (the warm-up)" if number == 1 else ""))
                self.markers += 1
                return
            at = line.find(b'{"type":')
            if at >= 0:
                try:
                    said = json.loads(line[at:].decode("utf-8", "replace").strip())
                except ValueError:
                    said = None
                if isinstance(said, dict) and said.get("type") == "case" and \
                        isinstance(said.get("suite"), str) and isinstance(said.get("name"), str):
                    suite, name = said["suite"], said["name"]
                    self.close(("name", suite, name), "up to its line in the report")
                    self.opened = None
                    result = str(said.get("result"))
                    if isinstance(said.get("reason"), str):
                        result += " (%s)" % said["reason"]
                    self.results[(suite, name)] = result
                    if suite not in self.reported:
                        self.reported.add(suite)
                        self.order[suite] = []
                    self.order[suite].append(name)
                    self.markers += 1
                return
            if PROBE.search(line):
                return
            self.pending.append(line)

    def end(self, where, how):
        """The launch is over: @where is the case the journal says it ended inside, as (suite,
        pass, name), or None, and @how what became of the launch."""
        with self.lock:
            if where is not None:
                suite, number, name = where
                self.inside.add((suite, name))
                self.results.setdefault((suite, name), "the launch ended inside it")
                self.close(("name", suite, name), "%swhere the launch %s" % (
                    "pass %s%s, " % (number, " (the warm-up)" if number == "1" else "")
                    if number != "0" else "", how))
            elif self.opened is not None:
                self.close(*self.opened)
            self.pending = []
            self.opened = None

    def keep_output(self, folder):
        """Adds the launch's whole output to emulator.log in @folder."""
        with self.lock:
            with open(os.path.join(folder, "emulator.log"), "ab") as f:
                f.write(b"== launch %d\n" % self.launch)
                for line in self.output:
                    f.write(line if line.endswith(b"\n") else line + b"\n")
            self.output = []

    def write(self, folder):
        """Writes logs/<suite>/<case>.log in @folder for every case that did not pass and that
        the emulator said something during. Hands back (files written, cases that did not
        pass)."""
        with self.lock:
            said = {}
            for owner, label, lines in self.segments:
                kind, suite, which = owner
                if kind == "index":
                    names = self.order.get(suite, [])
                    if which >= len(names):
                        continue
                    which = names[which]
                said.setdefault((suite, which), []).append((label, lines))
            wanted = [key for key, result in self.results.items()
                      if result != "pass" or key in self.inside]
            taken = {}
            written = 0
            for key in wanted:
                if key not in said:
                    continue
                suite, name = key
                directory = os.path.join(folder, "logs", stem(suite))
                os.makedirs(directory, exist_ok=True)
                base = stem(name)
                number = taken.get((directory, base), 0) + 1
                taken[(directory, base)] = number
                path = os.path.join(directory, base + ("-%d" % number if number > 1 else "") + ".log")
                with open(path, "wb") as f:
                    f.write(("%s: %s: %s\n" % (suite, name, self.results[key])).encode("utf-8"))
                    for label, lines in said[key]:
                        f.write(("== %s\n" % label).encode("utf-8"))
                        for line in lines:
                            f.write(line if line.endswith(b"\n") else line + b"\n")
                written += 1
            return written, len(wanted)


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


def prepare(xdg, mode, keep_rounds=False):
    """The directories, the previous runs' leavings taken away, and the mode file. Every run
    folder loses its report, its pictures and the emulator's logs, since this script cannot know which folder the build
    it runs will name. The journals stay: the probe reads its own to decide the run is new, the
    report being gone, and lists a case the last launch of an earlier run ended inside. The
    report and the pictures at the top of the card are a build's from before run folders. The
    rounds' reports go too, unless @keep_rounds: a round after the first of --rounds."""
    for d in ("data", "cache"):
        os.makedirs(os.path.join(xdg, d), exist_ok=True)
    sd = card(xdg)
    os.makedirs(sd, exist_ok=True)
    for folder in [sd] + run_folders(sd):
        names = ("report.jsonl", "mode", "last-run") if folder == sd else ("report.jsonl",
                                                                         "emulator.log")
        for name in names:
            path = os.path.join(folder, name)
            if os.path.isfile(path) or os.path.islink(path):
                os.remove(path)
        names = ("images", "support-check", "mode") if folder == sd else \
            ("images", "logs") if keep_rounds else ("images", "logs", "rounds")
        for name in names:
            path = os.path.join(folder, name)
            if os.path.isdir(path):
                shutil.rmtree(path)
    if mode:
        with open(os.path.join(sd, "mode"), "w") as f:
            f.write(mode + "\n")


def settings_folder(xdg, emulator):
    """The binary's own settings folder, its XDG_CONFIG_HOME: named as its crash list is, so a
    rebuild starts from its defaults again."""
    return os.path.join(xdg, "settings", list_key(emulator))


def emulator_env(xdg, emulator):
    """The emulator's environment: its settings, card and caches under xdg, never the person's,
    and the settings the binary's own."""
    env = dict(os.environ)
    env.update({
        "XDG_DATA_HOME": os.path.join(xdg, "data"),
        "XDG_CONFIG_HOME": settings_folder(xdg, emulator),
        "XDG_CACHE_HOME": os.path.join(xdg, "cache"),
    })
    env["DISPLAY"] = env.get("DISPLAY") or ":0"
    return env


def prepare_settings(xdg, emulator):
    """Makes the binary's settings folder when it has none, and says which it is and whether the
    old shared one is left unread."""
    folder = settings_folder(xdg, emulator)
    made = not os.path.isdir(folder)
    os.makedirs(folder, exist_ok=True)
    said = ["settings: %s, this binary's own, %s" % (
        folder, "made empty: the emulator starts from its defaults" if made else
        "as its earlier runs left it")]
    shared = os.path.join(xdg, "config")
    if os.path.isdir(shared) and os.listdir(shared):
        said.append("%s, the settings every binary shared before, is not read: each binary has "
                    "its own now; what a binary needs from it goes into that binary's folder" %
                    shared)
    return said


VERSION = re.compile(r"^[A-Za-z0-9._-]{1,32}$")


def eden_version(emulator, xdg):
    """The version Eden says with --version, its commit or its release number, or None: it logs
    "Eden <branch> <commit>-<branch>" or "Eden v0.1.0"."""
    try:
        done = subprocess.run([emulator, "--version"], env=emulator_env(xdg, emulator),
                              capture_output=True, timeout=30)
        said = (done.stdout + done.stderr).decode("utf-8", "replace")
    except (OSError, subprocess.TimeoutExpired):
        return None
    for line in ANSI.sub(b"", said.encode()).decode().splitlines():
        at = line.find(" Eden ")
        if at < 0 and not line.startswith("Eden "):
            continue
        rest = line[at + 6:] if at >= 0 else line[5:]
        found = re.search(r"(?<![0-9A-Za-z])([0-9a-f]{7,40})(?![0-9A-Za-z])", rest) or \
            re.search(r"(?<![0-9A-Za-z.])(v?[0-9]+(?:\.[0-9]+)+)(?![0-9A-Za-z])", rest)
        if found and VERSION.match(found.group(1)):
            return found.group(1)
    return None


def machine_word(emulator, xdg, mode, machine):
    """The machine= word to add to the mode file and a line saying where it came from, or None and
    why there is none."""
    if not mode:
        return None, "no mode file, so no machine= word: the ban list holds for none"
    if re.search(r"(?:^|\s)machine=", mode, re.I):
        return None, "the mode names its machine"
    if machine:
        return "machine=" + machine, "machine=%s, from --machine" % machine
    name = os.path.basename(emulator).lower()
    if name.startswith("eden"):
        version = eden_version(emulator, xdg)
        if version is None:
            return "machine=eden", "machine=eden, with no version: %s --version said none" % emulator
        return "machine=eden@" + version, "machine=eden@%s, from %s --version" % (version, emulator)
    if name.startswith("ryujinx"):
        return "machine=ryujinx", "machine=ryujinx, with no version: make run asks Ryujinx for none"
    return None, ("no machine= word: %s is no emulator known by its name (eden, ryujinx), so the "
                  "ban list holds for none; MACHINE=<name>[@<version>] names it" % emulator)


# Where the kernel's graphics drivers show their cards.
DRM = "/sys/class/drm"
# The most vram= takes (source/mode.h, MODE_VRAM_MAX).
VRAM_MAX = 1048576


def graphics_memory():
    """The graphics memory a card here says it has of its own, as (MiB, the file that says it, how
    many cards said one), the largest when several do; None when none does. The file is the
    driver's mem_info_vram_total, in bytes, which amdgpu writes and most other drivers do not."""
    found = []
    try:
        cards = sorted(os.listdir(DRM))
    except OSError:
        cards = []
    for name in cards:
        if not re.fullmatch(r"card[0-9]+", name):
            continue
        path = os.path.join(DRM, name, "device", "mem_info_vram_total")
        try:
            with open(path) as f:
                size = int(f.read().strip())
        except (OSError, ValueError):
            continue
        if size >= 1024 * 1024:
            found.append((min(size // (1024 * 1024), VRAM_MAX), path))
    if not found:
        return None
    mib, path = max(found)
    return mib, path, len(found)


def vram_word(mode):
    """The vram= word to add to the mode file and a line saying where it came from, or None and
    why there is none."""
    if not mode:
        return None, None
    if re.search(r"(?:^|\s)vram=", mode, re.I):
        return None, "the mode says its vram"
    memory = graphics_memory()
    if memory is None:
        return None, ("no vram= word: no card under %s says its memory in mem_info_vram_total, "
                      "amdgpu's file, so tex8k's textures are tried whatever this machine has; "
                      "vram=<MiB> among MODE's words says it" % DRM)
    mib, path, cards = memory
    return "vram=%d" % mib, "vram=%d, the MiB %s says%s; vram=<MiB> among MODE's words says another" % (
        mib, path, ", the largest of %d cards" % cards if cards > 1 else "")


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


# How often a launch under way is looked at: whether it ended, its time, and the journal's length.
POLL = 0.25


def journal_size(sd):
    """The length of the journal in the folder last-run names, or None while there is none."""
    folder = run_folder(sd)
    if folder is None:
        return None
    try:
        return os.path.getsize(os.path.join(folder, "journal"))
    except OSError:
        return None


class Launched:
    """What one launch came to: its exit status; the seconds it was stopped after at its limit,
    or None; quiet, the whole seconds the journal had gone without growing by then, which is how
    long the case under way had run, since a case's start is a record; lost, the emulator's line
    saying its graphics device was lost, or None; and after, how many case lines the probe still
    reported after that line."""

    def __init__(self):
        self.status = None
        self.stopped_after = None
        self.quiet = 0
        self.lost = None
        self.after = 0


def launch(emulator, nro, xdg, timeout, log, echo, logs=None):
    """One launch, stopped at its limit or at the emulator's word that its device was lost. Its
    output goes to @log as it came and to @logs a line at a time, colours off."""
    process = subprocess.Popen([emulator, "-g", nro], env=emulator_env(xdg, emulator),
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    ended = Launched()

    def pump():
        for raw in process.stdout:
            if log is not None:
                log.write(raw)
            line = ANSI.sub(b"", raw)
            if logs is not None:
                logs.feed(line)
            if PROBE.search(line):
                # The probe's own words are never the emulator's: a case may quote a loss.
                if ended.lost is not None and CASE.search(line):
                    ended.after += 1
            elif ended.lost is None and LOST.search(line):
                ended.lost = line.decode("utf-8", "replace").strip()
            if echo and SHOWN.search(line):
                sys.stdout.buffer.write(line)
                sys.stdout.flush()

    reader = threading.Thread(target=pump, daemon=True)
    reader.start()
    sd = card(xdg)
    started = time.monotonic()
    size = journal_size(sd)
    grew = started
    try:
        while True:
            try:
                process.wait(timeout=POLL)
                break
            except subprocess.TimeoutExpired:
                pass
            if ended.lost is not None:
                stop(process)
                break
            now = time.monotonic()
            current = journal_size(sd)
            if current != size:
                size = current
                grew = now
            if now - started >= timeout:
                ended.stopped_after = timeout
                ended.quiet = int(now - grew + 0.5)
                stop(process)
                break
    except BaseException:
        stop(process)
        raise
    reader.join(timeout=10)
    ended.status = process.returncode
    return ended


def stop(process):
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def interrupt(signum, frame):
    raise KeyboardInterrupt


def mark(journal_path, record):
    """Appends @record, a runner's own, to the journal there, on a line of its own; nothing when
    there is no journal."""
    if not os.path.isfile(journal_path):
        return False
    with open(journal_path, "rb") as f:
        open_line = False
        if f.seek(0, os.SEEK_END) > 0:
            f.seek(-1, os.SEEK_END)
            open_line = f.read(1) != b"\n"
    with open(journal_path, "ab") as f:
        # A line the program left open would swallow the word.
        f.write((b"\n" if open_line else b"") + record.encode("ascii") + b"\n")
        f.flush()
        os.fsync(f.fileno())
    return True


def note_in_report(report, text):
    """Adds a note to the end of the report there, on a line of its own; nothing when there is no
    report."""
    if not os.path.isfile(report):
        return False
    with open(report, "rb") as f:
        open_line = False
        if f.seek(0, os.SEEK_END) > 0:
            f.seek(-1, os.SEEK_END)
            open_line = f.read(1) != b"\n"
    with open(report, "ab") as f:
        line = json.dumps({"type": "note", "text": text}, separators=(",", ":"))
        f.write((b"\n" if open_line else b"") + line.encode("utf-8") + b"\n")
    return True


def mark_stopped(journal_path):
    """Says in the journal that the program was stopped on purpose, so the case it was in is no
    crash."""
    return mark(journal_path, "stopped")


def mark_timeout(journal_path, limit, quiet):
    """Says in the journal that the launch was stopped at its limit of @limit seconds, the journal
    @quiet seconds without growing by then, so the next launch skips the case it was in as
    timeout and not as crashed (source/journal.h)."""
    return mark(journal_path, "timeout %d %d\t" % (limit, quiet))


def one_run(emulator, nro, xdg, mode="quick", relaunches=16, timeout=60, log_path=None,
            who="run.py", echo=True, machine=None, round_number=1):
    """One whole run, launched again after each crash. Hands back (exit status, the report's path
    or None). @round_number past 1 keeps the rounds before it and adds to the log file."""
    say = lambda text: print("%s: %s" % (who, text), flush=True)
    if not (os.path.isfile(emulator) and os.access(emulator, os.X_OK)):
        print("%s: no executable emulator at %s" % (who, emulator), file=sys.stderr)
        return 1, None
    for d in ("data", "cache"):
        os.makedirs(os.path.join(xdg, d), exist_ok=True)
    for line in prepare_settings(xdg, emulator):
        say(line)
    word, why = machine_word(emulator, xdg, mode, machine)
    say(why)
    if word:
        mode = mode + " " + word
    word, why = vram_word(mode)
    if why:
        say(why)
    if word:
        mode = mode + " " + word
    prepare(xdg, mode, keep_rounds=round_number > 1)
    say("mode file: %s" % mode if mode else "mode file: none, so the detailed screen")
    sd = card(xdg)
    # Where the report and the journal are once a launch has named its folder. Before the first
    # launch every folder's journal is kept as it was, to compare the one it names against.
    folder = None
    report = None
    journal_path = None
    earlier = {f: Journal.read(os.path.join(f, "journal")) for f in run_folders(sd)}
    store = swap_in(xdg, emulator)
    log = open(log_path, "wb" if round_number == 1 else "ab") if log_path else None
    launches = 0
    code = 1
    lost = None
    logs = CaseLogs()
    # A person's SIGTERM stops the run the way ctrl-C does.
    previous = signal.signal(signal.SIGTERM, interrupt)
    try:
        while True:
            before = Journal.read(journal_path) if journal_path is not None else None
            launches += 1
            logs.begin(launches)
            ended = launch(emulator, nro, xdg, timeout, log, echo, logs)
            status, stopped_after, quiet = ended.status, ended.stopped_after, ended.quiet
            said = "launch %d %s" % (launches, describe(status, stopped_after))
            named = run_folder(sd)
            # What the launch said after the probe's last line is the case's it ended inside.
            left = Journal.read(os.path.join(named, "journal")) if named is not None else None
            logs.end(left.inside() if left is not None else None,
                     "was stopped at the emulator's word that its device was lost"
                     if ended.lost is not None else describe(status, stopped_after))
            if named is not None:
                logs.keep_output(named)
            if ended.lost is not None:
                # Before anything else the launch left: no summary, no relaunch and no case after
                # the line counts once the device is gone.
                where = None
                if named is not None:
                    folder = named
                    report = os.path.join(folder, "report.jsonl")
                    journal_path = os.path.join(folder, "journal")
                    after = Journal.read(journal_path)
                    where = after.inside() if after is not None else None
                lost = "launch %d: the emulator said its graphics device was lost%s: \"%s\"" % (
                    launches, " inside %s case \"%s\"" % (where[0], where[2]) if where else "",
                    ended.lost)
                came = ("%d case line%s came after that line and %s" % (
                    ended.after, "" if ended.after == 1 else "s",
                    "is no result" if ended.after == 1 else "are no results")
                    if ended.after else "no case line came after that line")
                noted = report is not None and note_in_report(
                    report, "%s: %s; the run was stopped there, with no summary: %s" % (
                        who, lost, came.replace("came after that line", "above came after it")))
                say("%s; stopped there and not launched again, since what an emulator draws "
                    "after a device loss is not its own work: %s%s%s" % (
                        lost, came,
                        "; the report's last line says so" if noted else "",
                        "; that case goes on the crash list at the next run" if where else ""))
                break
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
                if stopped_after is not None and mark_timeout(journal_path, stopped_after, quiet):
                    said += (", %d s after the journal last grew: the journal says timeout %d %d, "
                             "so the next launch skips that case as timeout, not as crashed" %
                             (quiet, stopped_after, quiet))
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
            # The launch under way never reached its end here: its output is kept all the same.
            logs.end(None, "was stopped by hand")
            logs.keep_output(named)
            folder = named
        say("stopped; the journal says so, so the case under way is not taken for a crash")
        code = 130
    finally:
        signal.signal(signal.SIGTERM, previous)
        if log is not None:
            log.close()
        swap_out(xdg, store)
    if folder is not None:
        written, wanted = logs.write(folder)
        say("the emulator's output: %s" % os.path.join(folder, "emulator.log"))
        if logs.markers == 0:
            say("that output carried none of the report's lines, so it is not cut by case: an "
                "emulator that logs the debug channel is what logs/ needs")
        elif wanted:
            say("of the %d case%s that did not pass, the emulator said something during %d: %s" %
                (wanted, "" if wanted == 1 else "s", written,
                 os.path.join(folder, "logs") if written else "no logs/ written"))
    if code == 130:
        return code, report
    if lost is not None:
        print("%s: the run ended at a device loss, so the report has no summary to trust: %s%s" %
              (who, lost, "; what it has is at %s" % report if report is not None else ""),
              file=sys.stderr)
        return 1, report
    if folder is None:
        print("%s: the emulator named no run folder in %s, so it left no report" %
              (who, os.path.join(sd, "last-run")), file=sys.stderr)
        return 1, None
    if not os.path.isfile(report):
        print("%s: the emulator left no report at %s" % (who, report), file=sys.stderr)
        return 1, None
    counts = summary(report)
    if counts is None:
        print("%s: no summary in the report after %d launch%s: stopped at %d s, or died first" %
              (who, launches, "" if launches == 1 else "es", timeout), file=sys.stderr)
        return 1, report
    journal = Journal.read(journal_path)
    crashed = journal.crashed() if journal is not None else []
    timed_out = journal.timed_out() if journal is not None else []
    say("passed %d, failed %d, skipped %d" % (counts["passed"], counts["failed"],
                                               counts["skipped"]))
    say("crashed %d%s%s" % (counts["crashed"],
                            ", in this run: " if crashed else "",
                            ", ".join('%s "%s"' % c for c in crashed)))
    if timed_out:
        say("skipped as timeout %d, a launch stopped inside each at its limit of %d s: %s" %
            (len(timed_out), timeout, ", ".join('%s "%s"' % c for c in timed_out)))
    say("%d launch%s" % (launches, "" if launches == 1 else "es"))
    print("report: %s" % report, flush=True)
    return code, report


def perf_tool():
    """tools/perf.py, which reads a report's times and knows a median and a spread."""
    spec = importlib.util.spec_from_file_location(
        "edenprobe_perf", os.path.join(os.path.dirname(os.path.abspath(__file__)), "perf.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sum_up_rounds(reports, who):
    """The times of the rounds' @reports case by case, into times.tsv beside them and a line a
    suite on the output. Hands back the file's path, or None when no round timed a case."""
    say = lambda text: print("%s: %s" % (who, text), flush=True)
    perf = perf_tool()
    runs = []
    order = []
    for path in reports:
        times, suites = perf.read(path)
        runs.append(times)
        order.extend(suite for suite in suites if suite not in order)
    rows = perf.rounds(runs)
    folder = os.path.dirname(reports[-1])
    if not rows:
        say("no case of these %d rounds carries a time (their families time none), so there is "
            "nothing to sum up; their reports are in %s" % (len(reports), folder))
        return None
    table = os.path.join(folder, "times.tsv")
    with open(table, "w") as f:
        f.write("\n".join(perf.rounds_tsv(rows, order)) + "\n")
    say("%d rounds, %d timed cases; each case's spread is from its fastest round to its slowest, "
        "and a change under it is not told from one round to the next:" % (len(reports), len(rows)))
    for line in perf.table_rounds(rows, order):
        print("  " + line, flush=True)
    say("every case's median, mean, fastest, slowest and spread: %s" % table)
    say("to hold another build's rounds to these: tools/perf.py %s -- THE-OTHER/rounds/report-*.jsonl"
        % os.path.join(folder, "report-*.jsonl"))
    return table


NOTIME = re.compile(r"(?:^|[^A-Za-z])notime(?:$|[^A-Za-z])", re.I)


def run(emulator, nro, xdg, mode="quick", relaunches=16, timeout=60, log_path=None,
        who="run.py", echo=True, machine=None, rounds=1):
    """The run, or @rounds of it one after another with their times summed up. The exit status."""
    if rounds == 1:
        return one_run(emulator, nro, xdg, mode, relaunches, timeout, log_path, who, echo,
                       machine)[0]
    if NOTIME.search(mode):
        print("%s: rounds=%d with the mode word notime would time nothing %d times: rounds are "
              "for the times, so nothing was run" % (who, rounds, rounds), file=sys.stderr)
        return 2
    reports = []
    for number in range(1, rounds + 1):
        print("%s: round %d of %d" % (who, number, rounds), flush=True)
        code, report = one_run(emulator, nro, xdg, mode, relaunches, timeout, log_path, who, echo,
                               machine, round_number=number)
        if code != 0:
            print("%s: round %d of %d did not end on its summary (exit %d), so the rounds end "
                  "there with %d kept%s" % (
                      who, number, rounds, code, len(reports),
                      " in %s" % os.path.dirname(reports[-1]) if reports else ""), file=sys.stderr)
            return code
        kept = os.path.join(os.path.dirname(report), "rounds", "report-%d.jsonl" % number)
        os.makedirs(os.path.dirname(kept), exist_ok=True)
        shutil.copyfile(report, kept)
        reports.append(kept)
    sum_up_rounds(reports, who)
    return 0




def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--emulator", required=True)
    parser.add_argument("--nro", required=True)
    parser.add_argument("--xdg", required=True)
    parser.add_argument("--mode", default="quick")
    parser.add_argument("--machine", default="")
    parser.add_argument("--relaunches", type=int, default=16)
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--log")
    parser.add_argument("--who", default="run.py")
    args = parser.parse_args(argv)
    if args.relaunches < 0 or args.timeout <= 0:
        parser.error("relaunches must be nought or more and the timeout more than nought")
    if args.rounds < 1:
        parser.error("--rounds %d is not a whole number from 1 up: one run is --rounds 1" %
                     args.rounds)
    return run(os.path.abspath(args.emulator), os.path.abspath(args.nro),
               os.path.abspath(args.xdg), args.mode.strip(), args.relaunches, args.timeout,
               args.log, args.who, machine=args.machine.strip() or None, rounds=args.rounds)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
