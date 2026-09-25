#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: MIT
"""What the Vulkan validation layer says about an emulator while it runs edenprobe.

    tools/validate.py run EMULATOR [LOG]   run the graphics family once under the Khronos
                                            validation layer, synchronisation validation on,
                                            and print the table below; the log goes to LOG
    tools/validate.py read LOG              the table from a log already written
    tools/validate.py diff LOG_A LOG_B      the ids whose counts differ between two logs

The table has one row per message id (a VUID, or a SYNC-HAZARD-* of the synchronisation
validation), how many times it came, and the first probe case it came before: the layer prints
while the case works, and the case reports when it is done, so a message belongs to the next
case line in the log. This is the host side of the check. The guest cannot see a VUID; a
hazard that a forgiving driver hides shows here and not on a pixel.

The run uses the Makefile's XDG override (build/xdg-validate), so nothing of the person's own
emulator setup is read or written, and the mode `quick notime graphics`, one pass per suite.
Standard library only.
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ID = re.compile(r"\[ (VUID-[A-Za-z0-9_-]+|SYNC-HAZARD-[A-Z-]+|UNASSIGNED-[A-Za-z0-9_.-]+) \]")
CASE = re.compile(r'"type":"case","suite":"([^"]+)","name":"([^"]+)"')


def parse(lines):
    """Reads a log. Hands back {id: [count, 'suite: case' or None]} in first-seen order."""
    table = {}
    waiting = []
    for raw in lines:
        for message in ID.findall(raw):
            if message not in table:
                table[message] = [0, None]
                waiting.append(message)
            table[message][0] += 1
        case = CASE.search(raw)
        if case:
            for message in waiting:
                if table[message][1] is None:
                    table[message][1] = case.group(1) + ": " + case.group(2)
            waiting = []
    return table


def render(table):
    if not table:
        return "no validation messages\n"
    width = max(len(k) for k in table)
    out = []
    for key, (count, case) in table.items():
        out.append("%-*s %6d  %s" % (width, key, count, case or "(after the last case)"))
    return "\n".join(out) + "\n"


def diff(a, b):
    out = []
    for key in list(a) + [k for k in b if k not in a]:
        ca = a.get(key, [0, None])[0]
        cb = b.get(key, [0, None])[0]
        if ca != cb:
            out.append("%s %d -> %d" % (key, ca, cb))
    return "\n".join(out) + ("\n" if out else "no difference\n")


def run(emulator, log):
    xdg = os.path.join(ROOT, "build", "xdg-validate")
    sdmc = os.path.join(xdg, "data", "eden", "sdmc", "edenprobe")
    os.makedirs(sdmc, exist_ok=True)
    for d in ("config", "cache"):
        os.makedirs(os.path.join(xdg, d), exist_ok=True)
    with open(os.path.join(sdmc, "mode"), "w") as f:
        f.write("quick notime graphics\n")
    env = dict(os.environ)
    env.update({
        "XDG_DATA_HOME": os.path.join(xdg, "data"),
        "XDG_CONFIG_HOME": os.path.join(xdg, "config"),
        "XDG_CACHE_HOME": os.path.join(xdg, "cache"),
        "VK_INSTANCE_LAYERS": "VK_LAYER_KHRONOS_validation",
        "VK_KHRONOS_VALIDATION_VALIDATE_SYNC": "true",
        "VK_KHRONOS_VALIDATION_DUPLICATE_MESSAGE_LIMIT": "1000",
    })
    env.setdefault("DISPLAY", ":0")
    nro = os.path.join(ROOT, "edenprobe.nro")
    with open(log, "wb") as out:
        subprocess.run(["timeout", "-k", "10", "600", emulator, "-g", nro], env=env,
                       stdout=out, stderr=subprocess.STDOUT, check=False)


def read(path):
    # The emulator's log carries NUL bytes; they are not text and nothing here needs them.
    with open(path, "rb") as f:
        return f.read().replace(b"\0", b"").decode("utf-8", "replace").splitlines()


def main(argv):
    if len(argv) >= 2 and argv[0] == "run":
        log = argv[2] if len(argv) >= 3 else os.path.join(ROOT, "build", "validate.log")
        run(argv[1], log)
        sys.stdout.write(render(parse(read(log))))
        return 0
    if len(argv) == 2 and argv[0] == "read":
        sys.stdout.write(render(parse(read(argv[1]))))
        return 0
    if len(argv) == 3 and argv[0] == "diff":
        sys.stdout.write(diff(parse(read(argv[1])), parse(read(argv[2]))))
        return 0
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
