#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""libc-math.py: prints source/libcmath.h, the correctly rounded results the libc suite's libm case
compares the C library with (source/tests/t_libc.c). Each value is computed in decimal at 100
digits from the input's exact binary value, then rounded to the nearest double (Python's float()
of a decimal string is correctly rounded) or float.

    tools/libc-math.py > source/libcmath.h

A second run prints the same bytes; test_libc_math.py beside it checks the header against it.
"""

import decimal
import struct
import sys
from decimal import Decimal as D

decimal.getcontext().prec = 100


def exp(x):
    return x.exp()


def ln(x):
    return x.ln()


def series_sin(x):
    term, total, n = x, x, 1
    while True:
        term = -term * x * x / ((2 * n) * (2 * n + 1))
        if abs(term) < D(10) ** -110:
            return total
        total += term
        n += 1


def series_cos(x):
    term, total, n = D(1), D(1), 1
    while True:
        term = -term * x * x / ((2 * n - 1) * (2 * n))
        if abs(term) < D(10) ** -110:
            return total
        total += term
        n += 1


def series_atan(x):
    # |x| <= 0.5 here, so the series converges fast enough
    term, total, n = x, x, 1
    while True:
        term = -term * x * x
        add = term / (2 * n + 1)
        if abs(add) < D(10) ** -110:
            return total
        total += add
        n += 1


def nearest_float(d):
    """the float32 nearest to the decimal d"""
    f = struct.unpack("<f", struct.pack("<f", float(d)))[0]
    bits = struct.unpack("<I", struct.pack("<f", f))[0]
    best = None
    for b in (bits - 1, bits, bits + 1):
        g = struct.unpack("<f", struct.pack("<I", b & 0xFFFFFFFF))[0]
        err = abs(D(g) - d)
        if best is None or err < best[0]:
            best = (err, b)
    return best[1]


def dbits(d):
    return struct.unpack("<Q", struct.pack("<d", float(d)))[0]


def main():
    one, two, ten, half = D(1), D(2), D(10), D("0.5")
    tiny = D(1e-10)  # the double nearest 1e-10, exactly
    doubles = [
        ("exp", "exp(VD(1.0))", exp(one)),
        ("log", "log(VD(10.0))", ln(ten)),
        ("pow", "pow(VD(2.0), VD(0.5))", two.sqrt()),
        ("sin", "sin(VD(1.0))", series_sin(one)),
        ("cos", "cos(VD(10.0))", series_cos(ten)),
        ("tan", "tan(VD(0.5))", series_sin(half) / series_cos(half)),
        ("atan", "atan(VD(0.5))", series_atan(half)),
        ("cbrt", "cbrt(VD(2.0))", exp(ln(two) / 3)),
        ("expm1", "expm1(VD(1e-10))", exp(tiny) - 1),
        ("log1p", "log1p(VD(1e-10))", ln(1 + tiny)),
        ("sinh", "sinh(VD(1.0))", (exp(one) - exp(-one)) / 2),
    ]
    floats = [
        ("sinf", "sinf(VF(1.0f))", series_sin(one)),
        ("expf", "expf(VF(1.0f))", exp(one)),
        ("logf", "logf(VF(10.0f))", ln(ten)),
    ]
    out = ["// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors",
           "// SPDX-License-Identifier: GPL-3.0-or-later",
           "",
           "// Made by tools/libc-math.py: the correctly rounded results of the libc suite's libm case. Do not",
           "// edit. X(name, the call, the result's bits); VD and VF read their argument through a volatile, so",
           "// the compiler cannot fold the call into a constant of its own.",
           "",
           "#pragma once",
           "",
           "#define MATH_DOUBLES(X) \\"]
    for name, call, value in doubles:
        out.append(f"    X({name}, {call}, 0x{dbits(value):016x}ULL) /* {float(value)!r} */ \\")
    out.append("")
    out.append("#define MATH_FLOATS(X) \\")
    for name, call, value in floats:
        out.append(f"    X({name}, {call}, 0x{nearest_float(value):08x}U) \\")
    out.append("")
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
