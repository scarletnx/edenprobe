#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Whether the program exports every SDK function source/sdk.h declares, under its symbol, and nothing more under nn::.

    tools/sdk-exports.py [--readelf PATH] ELF

A machine that answers a game's SDK functions itself takes the program's by the symbols its dynamic symbol table
holds (docs/Hooks.md): one the table lacks is a function no machine takes, whose own code then sends its request.
Reads the SDK_EXPORT symbols of source/sdk.h and the dynamic symbols of ELF (`readelf --dyn-syms -W`, the toolchain's
by default), and exits 1 naming each declared symbol that is not a defined global function there, and each `_ZN2nn`
one there that sdk.h does not declare."""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDK_H = os.path.join(ROOT, "source", "sdk.h")
READELF = os.path.join(os.environ.get("DEVKITPRO", "/opt/devkitpro"), "devkitA64", "bin", "aarch64-none-elf-readelf")


def declared(text):
    """The symbols of every SDK_EXPORT("...") in a header's text, its adjacent string literals joined, in order."""
    return ["".join(re.findall(r'"([^"]*)"', m.group(1)))
            for m in re.finditer(r'SDK_EXPORT\(((?:\s*"[^"]*")+)\s*\)', text)]


def exported(listing):
    """{symbol: (type, bind, visibility, section)} of `readelf --dyn-syms -W`'s lines."""
    out = {}
    for line in listing.splitlines():
        fields = line.split()
        # Num: Value Size Type Bind Vis Ndx Name
        if len(fields) >= 8 and fields[0].endswith(":") and fields[0][:-1].isdigit():
            out[fields[7]] = (fields[3], fields[4], fields[5], fields[6])
    return out


def problems(symbols, table):
    """Each declared symbol the table lacks as a defined global function, then each nn:: one it has that is not
    declared, as a line."""
    out = []
    if len(set(symbols)) != len(symbols):
        out.extend("declared twice: %s" % s for s in sorted({s for s in symbols if symbols.count(s) > 1}))
    for s in symbols:
        entry = table.get(s)
        if entry is None:
            out.append("not exported: %s" % s)
        elif entry[0] != "FUNC" or entry[1] != "GLOBAL" or entry[2] != "DEFAULT" or entry[3] == "UND":
            out.append("exported as %s %s %s in %s, not a defined global function: %s" % (entry + (s,)))
    out.extend("exported and not declared in sdk.h: %s" % s for s in sorted(table)
               if s.startswith("_ZN2nn") and s not in symbols)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--readelf", default=READELF)
    parser.add_argument("elf")
    args = parser.parse_args()
    with open(SDK_H, encoding="utf-8") as f:
        symbols = declared(f.read())
    if not symbols:
        sys.exit("sdk-exports: %s declares no SDK_EXPORT" % SDK_H)
    listing = subprocess.run([args.readelf, "--dyn-syms", "-W", args.elf], capture_output=True, text=True,
                             check=True).stdout
    found = problems(symbols, exported(listing))
    for line in found:
        print("sdk-exports: %s" % line, file=sys.stderr)
    if not found:
        print("sdk-exports: %s exports the %d SDK functions sdk.h declares" % (args.elf, len(symbols)))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
