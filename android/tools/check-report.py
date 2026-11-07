#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""check-report.py REPORT.jsonl...: checks that a report has edenprobe's format, the one tools/edenprobe-diff.py and
tools/edenprobe-table.py of the ScarletNX workspace read: one JSON object a line; notes ({"type":"note","text"});
cases with suite, name, tier, result (pass, fail, skip, crashed), detail, and androidprobe's id (a skip also has its
reason); the summary last, its counts those of the case lines. Each id and each suite and name appear once. Prints
each problem with its line number. Exit 0 when every report is whole, 1 when one is not, 2 refused (usage)."""

import json
import sys

RESULTS = {"pass": "passed", "fail": "failed", "skip": "skipped", "crashed": "crashed"}
TIERS = {"none", "exact", "ulp", "level", "position"}


def check(path):
    problems = []
    counts = dict.fromkeys(RESULTS.values(), 0)
    ids, keys, summary_at = {}, {}, None
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().split("\n")
    except (OSError, UnicodeDecodeError) as e:
        return [f"{path}: cannot read: {e}"]
    if lines and lines[-1] == "":
        lines.pop()
    else:
        problems.append(f"{path}: the last line has no newline: a run cut short")
    for n, line in enumerate(lines, 1):
        where = f"{path}:{n}"
        try:
            o = json.loads(line)
        except ValueError as e:
            problems.append(f"{where}: not JSON ({e})")
            continue
        if not isinstance(o, dict):
            problems.append(f"{where}: not a JSON object")
            continue
        if summary_at is not None:
            problems.append(f"{where}: a line after the summary of line {summary_at}")
        t = o.get("type")
        if t == "note":
            if not isinstance(o.get("text"), str):
                problems.append(f"{where}: a note without its text")
        elif t == "case":
            for k in ("suite", "name", "tier", "result", "detail", "id"):
                if not isinstance(o.get(k), str):
                    problems.append(f"{where}: a case without a string {k}")
            r, tier = o.get("result"), o.get("tier")
            if r not in RESULTS:
                problems.append(f"{where}: result {r!r} is none of {', '.join(RESULTS)}")
            else:
                counts[RESULTS[r]] += 1
            if tier not in TIERS:
                problems.append(f"{where}: tier {tier!r} is none of {', '.join(sorted(TIERS))}")
            if r == "skip" and not isinstance(o.get("reason"), str):
                problems.append(f"{where}: a skip without its reason")
            if "us" in o and (not isinstance(o["us"], int) or o["us"] < 0):
                problems.append(f"{where}: us {o['us']!r} is not a whole number of microseconds")
            key = (o.get("suite"), o.get("name"))
            if o.get("id") in ids:
                problems.append(f"{where}: id {o.get('id')} again (line {ids[o.get('id')]})")
            if key in keys:
                problems.append(f"{where}: {key[0]}/{key[1]} again (line {keys[key]})")
            ids.setdefault(o.get("id"), n)
            keys.setdefault(key, n)
        elif t == "summary":
            summary_at = n
            for k, v in counts.items():
                if o.get(k) != v:
                    problems.append(f"{where}: the summary says {k} {o.get(k)!r}, the case lines {v}")
        else:
            problems.append(f"{where}: type {t!r} is none of note, case, summary")
    if summary_at is None:
        problems.append(f"{path}: no summary: the run did not finish")
    return problems


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__, file=sys.stdout if argv else sys.stderr)
        return 0 if argv else 2
    bad = 0
    for path in argv:
        problems = check(path)
        for p in problems:
            print(p)
        if problems:
            bad = 1
        else:
            print(f"{path}: whole")
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
