#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Whether every multisample fetch through a handle in compiled shaders reads its handle where the console does.

    tools/dksh-handles.py FILE...   each a DKSH module (build/shaders/*.dksh) or anything holding them (the NRO)

A multisample TLD.B (0xdd in the top byte, bit 50 set) takes its handle in Rb and the sample in the register after it,
the order Mesa's compilers give since f9d385d77716 ("nouveau/codegen: Handle/indirect goes before sample index"); uam
1.1.0 gave the other one (tools/uam/0001-handle-before-sample-index.patch). The handle comes from a constant buffer,
so the last load from one before the fetch (LDC, or MOV of c[]) into Rb or Rb + 1 has to be the one into Rb. One line
for each such fetch; the status is 1 when one reads its handle from the wrong register or no load names either, 2 when
a file holds no DKSH module. Straight-line programs: the load is looked for in program order, not along branches.
"""

import struct
import sys

MAGIC = b"DKSH"
COMPUTE = 5  # DkshProgramType_Compute: no 0x50-byte header before the code


def opcode(word, mask, value):
    return (word >> 48) & mask == value


def is_ms_fetch(word):
    return opcode(word, 0xFF00, 0xDD00) and (word >> 50) & 1


def is_handle_load(word):
    return opcode(word, 0xFFF8, 0xEF90) or opcode(word, 0xFFF8, 0x4C98)


def programs(data):
    """(where, the program's instruction words in order, scheduling words left out) of every DKSH module in data"""
    at = data.find(MAGIC)
    while at >= 0:
        if at + 24 <= len(data):
            _, header, control, code, first, count = struct.unpack_from("<6I", data, at)
            end = at + control + code
            if header == 24 and count <= 8 and end <= len(data):
                for p in range(count):
                    kind, entry = struct.unpack_from("<2I", data, at + first + 64 * p)
                    start = at + control + entry + (0 if kind == COMPUTE else 0x50)
                    words = [struct.unpack_from("<Q", data, w)[0] for w in range(start, end - 7, 8)]
                    yield (at, p), [w for k, w in enumerate(words) if k % 4]
        at = data.find(MAGIC, at + 4)


def check(data):
    """the multisample fetches of data's programs: (module offset, program, instruction, problem or None)"""
    found = []
    for (module, program), words in programs(data):
        for i, word in enumerate(words):
            if word == 0xE30000000007000F:  # EXIT: the program's code ends here
                break
            if not is_ms_fetch(word):
                continue
            rb = (word >> 20) & 0xFF
            loads = [w & 0xFF for w in words[:i] if is_handle_load(w) and w & 0xFF in (rb, rb + 1)]
            problem = None
            if not loads:
                problem = "no constant buffer load into R%d or R%d" % (rb, rb + 1)
            elif loads[-1] != rb:
                problem = "the handle is in R%d, after the sample, where the fetch reads its sample" % loads[-1]
            found.append((module, program, i, problem))
    return found


def main(paths):
    status = 0
    for path in paths:
        with open(path, "rb") as f:
            data = f.read()
        if MAGIC not in data:
            print("%s: no DKSH module" % path)
            status = max(status, 2)
            continue
        for module, program, i, problem in check(data):
            print("%s: module at %#x, program %d, instruction %d: %s" %
                  (path, module, program, i, problem or "handle in Rb, the sample after it"))
            if problem:
                status = max(status, 1)
    return status


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1:]))
