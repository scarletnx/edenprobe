#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Copies a person's controller setup into the settings a `make run` reads.

    tools/eden-controls.py FROM INTO

FROM is an Eden settings file with the person's controllers (the desktop frontend's qt-config.ini),
INTO the command line frontend's sdl2-config.ini under the run's XDG_CONFIG_HOME. Every key of
FROM's [Controls] section that INTO's [Controls] also has takes FROM's value, its `\\default` flag
included; nothing else in INTO changes, and a key INTO lacks is left out, since that frontend would
not read it. The pad family wants this: `make run` starts from the emulator's defaults, which bind
the keyboard, and the family's report is worth comparing only with the same controller in hand.
"""
import sys

SECTION = "[Controls]"


def controls(lines):
    """The key=value pairs of the [Controls] section, in order."""
    found = {}
    inside = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            inside = stripped == SECTION
            continue
        if inside and "=" in line:
            key, value = line.split("=", 1)
            found[key.strip()] = value.rstrip("\r\n")
    return found


def merge(source_text, target_text):
    """INTO's text with FROM's [Controls] values, and how many keys changed."""
    source = controls(source_text.splitlines())
    out = []
    changed = 0
    inside = False
    for line in target_text.splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            inside = stripped == SECTION
            out.append(line)
            continue
        if inside and "=" in line:
            key, value = line.split("=", 1)
            key = key.strip()
            if key in source and source[key] != value.rstrip("\r\n"):
                ending = line[len(line.rstrip("\r\n")):]
                line = "%s=%s%s" % (key, source[key], ending)
                changed += 1
        out.append(line)
    return "".join(out), changed


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    # newline="" keeps each line ending as the file has it.
    with open(argv[1], encoding="utf-8", newline="") as f:
        source_text = f.read()
    with open(argv[2], encoding="utf-8", newline="") as f:
        target_text = f.read()
    if SECTION not in source_text:
        print("eden-controls: %s has no %s section" % (argv[1], SECTION), file=sys.stderr)
        return 1
    if SECTION not in target_text:
        print("eden-controls: %s has no %s section: start the emulator once with this XDG "
              "first, so it writes its settings" % (argv[2], SECTION), file=sys.stderr)
        return 1
    merged, changed = merge(source_text, target_text)
    with open(argv[2], "w", encoding="utf-8", newline="") as f:
        f.write(merged)
    print("eden-controls: %d keys of %s taken from %s" % (changed, SECTION, argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
