#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Time per case, from edenprobe reports.

    tools/perf.py a1.jsonl a2.jsonl a3.jsonl -- b1.jsonl b2.jsonl b3.jsonl
    tools/perf.py a.jsonl b.jsonl        one report a side, which the noise allows less well
    tools/perf.py a.jsonl                the per-suite table of one run
    tools/perf.py a1.jsonl a2.jsonl --   the table of one side of several runs

A case's time is the "us" field on its line: microseconds the graphics unit was busy with it,
the smallest of the samples one run took. One emulator process differs from the next by more
than the samples inside it do, so a side is several runs, three by the README's method, and a
case's time on a side is its median over them. Standard library only.
"""

import json
import sys

# What a move has to clear to be listed (the README's "Time per case" has the figures). The
# percent is the brief's 20. The floor and the unsteady rule are what keep noise out: without
# the floor the percent lists the round-trip jitter of the quarter-millisecond cases, and without
# the unsteady rule a side holding one fast emulator process lists the uploads. The unsteady
# limit is 75 because at 100 the uploads still got through (91 of 9240 splits of twelve runs)
# and at 50 steady blend cases start being set aside.
THRESHOLD = 20.0  # percent of the time on side a
FLOOR = 100  # microseconds, about one submit's round trip under an emulator
UNSTEADY = 75.0  # percent between a side's fastest and slowest run of one case


def parse(lines):
    """Reads a report. Hands back ({(suite, name): us}, [suite order], lines it could not read).

    A run that was killed can end on half a line, and that must not cost the rest of the file,
    so an unreadable line is counted and skipped rather than fatal.
    """
    times = {}
    order = []
    unreadable = 0
    for raw in lines:
        raw = raw.strip()
        if not raw:
            continue
        try:
            line = json.loads(raw)
        except ValueError:
            unreadable += 1
            continue
        if not isinstance(line, dict) or line.get("type") != "case":
            continue
        us = line.get("us")
        suite = line.get("suite")
        name = line.get("name")
        if not isinstance(us, int) or isinstance(us, bool) or us < 0:
            continue
        if not isinstance(suite, str) or not isinstance(name, str):
            continue
        if suite not in order:
            order.append(suite)
        times[(suite, name)] = us
    return times, order, unreadable


def median(values):
    """The middle value, or between the two middle ones rounded down, as the probe's perf line."""
    ordered = sorted(values)
    count = len(ordered)
    if count == 0:
        return 0
    if count % 2:
        return ordered[count // 2]
    return (ordered[count // 2 - 1] + ordered[count // 2]) // 2


def spread(values):
    """Percent from the fastest of @values to the slowest. Zero for one value or none."""
    if len(values) < 2 or min(values) <= 0:
        return 0.0
    return (max(values) - min(values)) * 100.0 / min(values)


def merge(runs):
    """One side from its runs: ({case: median}, {case: spread}).

    The median, not the smallest: now and then a whole emulator process runs a case in a fast
    mode the others never reach, and the smallest over three runs then belongs to that one
    process. The median needs two of the three to agree.
    """
    values = {}
    for times in runs:
        for key, us in times.items():
            values.setdefault(key, []).append(us)
    return ({key: median(v) for key, v in values.items()},
            {key: spread(v) for key, v in values.items()})


def summarize(times, suite):
    """(timed, total, min, median, max) for one suite, in microseconds."""
    values = [us for (s, _), us in times.items() if s == suite]
    if not values:
        return (0, 0, 0, 0, 0)
    return (len(values), sum(values), min(values), median(values), max(values))


def change(before, after):
    """Percent from @before to @after. None when there is nothing to measure from."""
    if before <= 0:
        return None
    return (after - before) * 100.0 / before


def percent(value):
    return "-" if value is None else "%+.1f%%" % value


def moved(a, b, threshold, floor=0, spread_a=None, spread_b=None, unsteady=UNSTEADY):
    """(moves, unsteady cases) between two sides.

    A move is a case timed on both sides whose time changed by more than @threshold percent and
    by more than @floor microseconds, largest first. A case whose runs on either side spread by
    more than @unsteady percent is not compared at all: its time on that side is not one number.
    """
    out = []
    shaky = []
    for key in a:
        if key not in b:
            continue
        if max((spread_a or {}).get(key, 0.0), (spread_b or {}).get(key, 0.0)) > unsteady:
            shaky.append(key)
            continue
        delta = change(a[key], b[key])
        if delta is not None and abs(delta) > threshold and abs(b[key] - a[key]) > floor:
            out.append((key, a[key], b[key], delta))
    out.sort(key=lambda row: -abs(row[3]))
    return out, sorted(shaky)


def table_one(times, order):
    rows = ["%-12s %6s %10s %8s %8s %8s" % ("suite", "timed", "total_us", "min", "median", "max")]
    for suite in order:
        timed, total, low, mid, high = summarize(times, suite)
        rows.append("%-12s %6d %10d %8d %8d %8d" % (suite, timed, total, low, mid, high))
    return rows


def table_two(a, b, order):
    rows = ["%-12s %6s %10s %10s %9s %8s %8s %9s" % (
        "suite", "timed", "total_a", "total_b", "change", "median_a", "median_b", "change")]
    for suite in order:
        ta = summarize(a, suite)
        tb = summarize(b, suite)
        timed = "%d/%d" % (ta[0], tb[0]) if ta[0] != tb[0] else str(ta[0])
        if ta[0] == 0 or tb[0] == 0:
            # A suite only one side timed has no change to show, and minus a hundred percent
            # would read as the fastest suite of all.
            total_change = median_change = "only in %s" % ("b" if ta[0] == 0 else "a")
        else:
            total_change = percent(change(ta[1], tb[1]))
            median_change = percent(change(ta[3], tb[3]))
        rows.append("%-12s %6s %10d %10d %9s %8d %8d %9s" % (
            suite, timed, ta[1], tb[1], total_change, ta[3], tb[3], median_change))
    return rows


def compare(a, order_a, b, order_b, threshold, floor=0, spread_a=None, spread_b=None,
            unsteady=UNSTEADY):
    lines = []
    shifted, shaky = moved(a, b, threshold, floor, spread_a, spread_b, unsteady)
    lines.append("cases that moved more than %g percent and %d us: %d of %d timed in both" % (
        threshold, floor, len(shifted), len([k for k in a if k in b])))
    for (suite, name), before, after, delta in shifted:
        lines.append("  %+7.1f%% %8d -> %-8d %s: %s" % (delta, before, after, suite, name))
    if shaky:
        lines.append("unsteady, runs of one side more than %g percent apart, not compared: %d" % (
            unsteady, len(shaky)))
        lines.extend("  %s: %s" % key for key in shaky)
    only_a = sorted(k for k in a if k not in b)
    only_b = sorted(k for k in b if k not in a)
    for label, keys in (("only in a", only_a), ("only in b", only_b)):
        if keys:
            lines.append("%s: %d" % (label, len(keys)))
            lines.extend("  %s: %s" % key for key in keys)
    lines.append("")
    order = list(order_a) + [s for s in order_b if s not in order_a]
    lines.extend(table_two(a, b, order))
    return lines


class Refused(Exception):
    """A reason to stop that is the input's, said in one line rather than a trace."""


def decoded(handle, bad):
    """The lines of a binary @handle as text; a line that is not UTF-8 is counted in @bad[0] and
    left out, the same as a line that is not JSON."""
    for raw in handle:
        try:
            yield raw.decode("utf-8")
        except UnicodeDecodeError:
            bad[0] += 1


def read(path):
    bad = [0]
    try:
        with open(path, "rb") as handle:
            times, order, unreadable = parse(decoded(handle, bad))
    except OSError as error:
        raise Refused("%s: %s" % (path, error.strerror)) from None
    unreadable += bad[0]
    if unreadable:
        print("perf.py: %s: %d unreadable lines skipped" % (path, unreadable), file=sys.stderr)
    return times, order


def side(paths):
    """The runs of one side: ({case: median over the runs}, {case: spread}, [suite order]).

    A report with nothing timed is refused, alone or as one run of several: a notime run, a
    report from before cases were timed and an empty file all read that way, and comparing
    against one would print every suite at minus a hundred percent, which reads as a speed-up.
    """
    runs = []
    order = []
    for path in paths:
        times, run_order = read(path)
        # Refused wherever it stands, not only as a whole side: one quietly missing run of three
        # leaves a side of two presented as three, and the method rests on the three.
        if not times:
            raise Refused("%s: no timed cases (a notime run, a report from before cases were "
                          "timed, or an empty one)" % path)
        runs.append(times)
        order.extend(suite for suite in run_order if suite not in order)
    merged, spreads = merge(runs)
    return merged, spreads, order


USAGE = """usage: perf.py [options] A.jsonl [A2.jsonl ...] -- [B.jsonl [B2.jsonl ...]]
       perf.py [options] A.jsonl [B.jsonl]

One side prints its per-suite table; two sides print the cases that moved, the cases too
unsteady to compare, then the table. A side of several runs takes each case's median over them;
three runs a side is the method the README measures.

  --threshold PERCENT  a move must exceed this share of side a's time (default %g)
  --floor US           and this many microseconds (default %d)
  --unsteady PERCENT   a case whose runs on one side spread further is not compared (default %g)
""" % (THRESHOLD, FLOOR, UNSTEADY)

OPTIONS = {"--threshold": float, "--floor": int, "--unsteady": float}


def arguments(argv):
    """(side a, side b or None, options). Split by hand, since the sides are split by `--`."""
    values = {"--threshold": THRESHOLD, "--floor": FLOOR, "--unsteady": UNSTEADY}
    files = []
    sides = None
    i = 0
    while i < len(argv):
        word = argv[i]
        name = word.split("=", 1)[0]
        if word in ("-h", "--help"):
            print(USAGE)
            raise SystemExit(0)
        if name in OPTIONS:
            if "=" in word:
                value = word.split("=", 1)[1]
            else:
                i += 1
                value = argv[i] if i < len(argv) else ""
            try:
                number = OPTIONS[name](value)
            except ValueError:
                number = None
            # Not a number, not finite or below nought: nan compares false with everything and
            # would list nothing, a negative limit would list everything.
            if number is None or number != number or number in (float("inf"), float("-inf")) \
                    or number < 0:
                raise Refused("%s wants a finite number of nought or more, not %r" % (
                    name, value)) from None
            values[name] = number
        elif word == "--":
            if sides is not None:
                raise Refused("one -- only, between the two sides")
            sides = files
            files = []
        else:
            files.append(word)
        i += 1
    if sides is None:
        if len(files) == 2:
            return [files[0]], [files[1]], values
        if not files:
            raise Refused("no report given\n" + USAGE)
        if len(files) > 2:
            raise Refused("more than two reports and no --: put -- between the two sides, or "
                          "after the runs of one side to see its table")
        return files, None, values
    if not sides:
        raise Refused("the side before -- needs at least one report")
    # Nothing after the -- is one side on its own, several runs of it merged into one table.
    return sides, files or None, values


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    try:
        paths_a, paths_b, values = arguments(argv)
        a, spread_a, order_a = side(paths_a)
        if paths_b is None:
            lines = table_one(a, order_a)
        else:
            b, spread_b, order_b = side(paths_b)
            lines = ["a: %d report%s, b: %d report%s, each case's median over its side" % (
                len(paths_a), "" if len(paths_a) == 1 else "s", len(paths_b),
                "" if len(paths_b) == 1 else "s")]
            lines.extend(compare(a, order_a, b, order_b, values["--threshold"],
                                 values["--floor"], spread_a, spread_b, values["--unsteady"]))
    except Refused as refusal:
        print("perf.py: %s" % refusal, file=sys.stderr)
        return 1
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
