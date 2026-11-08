#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The ban list: source/bans.txt checked line by line and written as the C table the program holds.

  tools/bans.py          checks source/bans.txt and writes source/banlist.c from it
  tools/bans.py --check  checks it and says whether source/banlist.c is what it would write; exit 1 when not

A line that does not check is refused with its number and why, and nothing is written: a ban that does not
hold where its line says would leave a case skipped, or running, with nobody told.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TEXT = os.path.join(HERE, "..", "source", "bans.txt")
TABLE = os.path.join(HERE, "..", "source", "banlist.c")

FIELDS = ("suite", "case", "machine", "versions", "breaks", "source", "retest", "checked")
VERSION = re.compile(r"^[A-Za-z0-9._-]{1,32}$")
DOTTED = re.compile(r"^v?[0-9]+(\.[0-9]+)*$")
CHECKED = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2} \S")
# What the program keeps of a case's suite and name (journal.h, JOURNAL_SUITE_MAX and JOURNAL_NAME_MAX less the
# terminator): a longer one could never be matched.
SUITE_MAX = 31
NAME_MAX = 159


class Refused(Exception):
    pass


def versions_problem(field):
    """None when the versions field is one the program reads, else why not."""
    if field == "*":
        return None
    if field.startswith(("<", ">")):
        parts = re.findall(r"(<=|>=|<|>)\s*([^<>\s]+)", field)
        rebuilt = " ".join(op + " " + v for op, v in parts)
        if not parts or len(parts) > 2 or re.sub(r"\s+", "", rebuilt) != re.sub(r"\s+", "", field):
            return "bounds are < V, <= V, >= V, > V, or a lower bound then an upper one"
        for _, v in parts:
            if not DOTTED.match(v):
                return "a bound's version is dotted numbers (1.2.3), not %r" % v
        if len(parts) == 2 and (parts[0][0][0] != ">" or parts[1][0][0] != "<"):
            return "two bounds are a lower one (> or >=) then an upper one (< or <=)"
        return None
    for v in (x.strip() for x in field.split(",")):
        if not VERSION.match(v):
            return "a version is 1 to 32 letters, digits, dots, dashes and underscores, not %r" % v
    return None


def read(text):
    """The machines the header names and the lines, each a dict of FIELDS and its line number."""
    machines, lines, header = None, [], None
    for number, raw in enumerate(text.split("\n"), 1):
        line = raw.strip()
        if line.startswith("# Machines:"):
            machines = line.split(":", 1)[1].split()
            continue
        if not line or line.startswith("#"):
            continue
        fields = [f.strip() for f in line.split("|")]
        if header is None:
            if tuple(fields) != FIELDS:
                raise Refused("line %d: the header is %s, not %r" % (number, " | ".join(FIELDS), line))
            header = number
            continue
        if len(fields) != len(FIELDS):
            raise Refused("line %d: %d fields, not %d" % (number, len(fields), len(FIELDS)))
        lines.append(dict(zip(FIELDS, fields), line=number))
    if machines is None:
        raise Refused("no '# Machines:' line names the machines")
    if header is None:
        raise Refused("no header line: %s" % " | ".join(FIELDS))
    return machines, lines


def check(machines, lines):
    for name in machines:
        if not re.match(r"^[a-z][a-z0-9-]*$", name) or len(name) > 24:
            raise Refused("a machine is a lowercase name with dashes, at most 24 characters, not %r" % name)
    seen = {}
    for ban in lines:
        n = ban["line"]
        for field in FIELDS:
            if not ban[field]:
                raise Refused("line %d: the %s is empty" % (n, field))
        if " " in ban["suite"] or len(ban["suite"]) > SUITE_MAX:
            raise Refused("line %d: a suite is one word of at most %d characters, not %r" % (n, SUITE_MAX, ban["suite"]))
        if len(ban["case"]) > NAME_MAX:
            raise Refused("line %d: the case is %d characters, more than the %d a name is matched on"
                          % (n, len(ban["case"]), NAME_MAX))
        if ban["machine"] not in machines:
            raise Refused("line %d: machine %r is not one of %s" % (n, ban["machine"], " ".join(machines)))
        problem = versions_problem(ban["versions"])
        if problem:
            raise Refused("line %d: versions %r: %s" % (n, ban["versions"], problem))
        if not CHECKED.match(ban["checked"]):
            raise Refused("line %d: checked starts with the date, YYYY-MM-DD, then what was seen: %r"
                          % (n, ban["checked"]))
        key = (ban["suite"], ban["case"], ban["machine"])
        if key in seen:
            raise Refused("line %d: %s \"%s\" on %s is banned on line %d already" % (n, key[0], key[1], key[2], seen[key]))
        seen[key] = n


def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def table(machines, lines):
    out = ["// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors",
           "// SPDX-License-Identifier: GPL-3.0-or-later",
           "// Written by tools/bans.py from source/bans.txt; edit that file and run it again rather than this one.",
           "",
           "#include <stddef.h>",
           "",
           '#include "bans.h"',
           "",
           "const char *const BAN_MACHINES[] = {"]
    out += ["    %s," % c_string(m) for m in machines]
    out += ["};", "const size_t BAN_MACHINE_COUNT = %d;" % len(machines), ""]
    out += ["const BanLine BAN_LINES[] = {"]
    if not lines:
        out.append("    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0 },")
    for ban in lines:
        out.append("    {")
        for field in FIELDS:
            out.append("        %s," % c_string(ban[field]))
        out.append("        %d," % ban["line"])
        out.append("    },")
    out += ["};", "const size_t BAN_LINE_COUNT = %d;" % len(lines), ""]
    return "\n".join(out)


def written(text):
    machines, lines = read(text)
    check(machines, lines)
    return table(machines, lines)


def main(argv):
    try:
        with open(TEXT, encoding="utf-8") as f:
            out = written(f.read())
    except Refused as error:
        print("tools/bans.py: source/bans.txt %s; source/banlist.c was not written" % error, file=sys.stderr)
        return 1
    if "--check" in argv:
        try:
            with open(TABLE, encoding="utf-8") as f:
                same = f.read() == out
        except OSError:
            same = False
        print("source/banlist.c %s source/bans.txt" % ("is" if same else "is NOT what tools/bans.py writes from"))
        return 0 if same else 1
    with open(TABLE, "w", encoding="utf-8") as f:
        f.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
