#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Whether the program exports every SDK function source/sdk.h declares, under its symbol, and nothing more under nn::,
and reaches them with calls.

    tools/sdk-exports.py [--readelf PATH] [--objdump PATH] ELF

A machine that answers a game's SDK functions itself takes the program's by the symbols its dynamic symbol table
holds (docs/Hooks.md): one the table lacks is a function no machine takes, whose own code then sends its request.
Reads the SDK_EXPORT symbols of source/sdk.h and the dynamic symbols of ELF (`readelf --dyn-syms -W`, the toolchain's
by default), and exits 1 naming each declared symbol that is not a defined global function there, and each `_ZN2nn`
one there that sdk.h does not declare. Then, in ELF's code (`objdump -d`), each tail branch, a `b` to such a
function's entry from another function, but the one the sdk suite asks on purpose (TAIL_ON_PURPOSE): a machine may
take a function only where it is called, so a case that reached one by a branch would depend on the compiler. Last,
whether ELF holds sdk.h's SDK_LABEL, the SDK version a machine reads for the functions it takes only for some."""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDK_H = os.path.join(ROOT, "source", "sdk.h")
BIN = os.path.join(os.environ.get("DEVKITPRO", "/opt/devkitpro"), "devkitA64", "bin")
READELF = os.path.join(BIN, "aarch64-none-elf-readelf")
OBJDUMP = os.path.join(BIN, "aarch64-none-elf-objdump")
# source/sdk.h's SdkTailIsInitialized, the sdk suite's tail call
TAIL_ON_PURPOSE = {"SdkTailIsInitialized"}


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


def label(header):
    """sdk.h's SDK_LABEL, or None."""
    m = re.search(r'^#define SDK_LABEL "([^"]+)"$', header, re.M)
    return m.group(1) if m else None


def label_problems(wanted, image):
    """Why image, the program's bytes, does not carry the label a machine reads the SDK version from, as lines."""
    if wanted is None:
        return ["sdk.h defines no SDK_LABEL"]
    if not re.fullmatch(r"SDK MW\+[A-Za-z]+\+[A-Za-z]+S[Dd][Kk]_nnSdk-\d{1,3}_\d{1,3}_\d{1,3}", wanted):
        return ["SDK_LABEL is %r, not a label of the form SDK MW+<vendor>+<vendor>Sdk_nnSdk-x_y_z" % wanted]
    if wanted.encode() + b"\0" not in image:
        return ["the program does not hold SDK_LABEL (%s)" % wanted]
    return []


FUNCTION = re.compile(r"^[0-9a-f]+ <([^>]+)>:$")
TAIL = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+b\s+[0-9a-f]+ <([^>+]+)>\s*$")


def tail_branches(disassembly, symbols):
    """Each `b` to the entry of one of symbols from another function of `objdump -d`'s text, as a line, but those of
    TAIL_ON_PURPOSE."""
    out, inside = [], None
    wanted = set(symbols)
    for line in disassembly.splitlines():
        m = FUNCTION.match(line)
        if m:
            inside = m.group(1)
            continue
        m = TAIL.match(line)
        if m and m.group(2) in wanted and m.group(2) != inside and inside not in TAIL_ON_PURPOSE:
            out.append("a tail branch to %s in %s at %s" % (m.group(2), inside, m.group(1)))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--readelf", default=READELF)
    parser.add_argument("--objdump", default=OBJDUMP)
    parser.add_argument("elf")
    args = parser.parse_args()
    with open(SDK_H, encoding="utf-8") as f:
        header = f.read()
    symbols = declared(header)
    if not symbols:
        sys.exit("sdk-exports: %s declares no SDK_EXPORT" % SDK_H)
    listing = subprocess.run([args.readelf, "--dyn-syms", "-W", args.elf], capture_output=True, text=True,
                             check=True).stdout
    found = problems(symbols, exported(listing))
    code = subprocess.run([args.objdump, "-d", args.elf], capture_output=True, text=True, check=True).stdout
    found += tail_branches(code, symbols)
    with open(args.elf, "rb") as f:
        found += label_problems(label(header), f.read())
    for line in found:
        print("sdk-exports: %s" % line, file=sys.stderr)
    if not found:
        print("sdk-exports: %s exports the %d SDK functions sdk.h declares, reaches them with calls and holds %s" % (
            args.elf, len(symbols), label(header)))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
