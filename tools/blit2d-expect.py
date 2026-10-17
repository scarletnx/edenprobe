#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""What each case of the blit2d suite expects, worked out apart from source/blit2dref.c.

The source and the destination are built byte by byte in the block linear layout (gobs of 64
bytes by 8 rows, blocks two gobs tall), every pixel of the window takes the source texel the
rule under test names, and the 4096 bytes of the destination are hashed with MD5. Here the
rules are exact fractions and a gob is a permutation of address bits; in the C they are 32.32
integer steps and the arithmetic of CohBlockLinearOffset. The reference suite holds the C to
the table this prints, so a mistake would have to be made twice, two different ways, to pass.

  tools/blit2d-expect.py          the texels each rule picks, the MD5 of every case under every
                                  combination of answers, the 2D engine's command words, and
                                  whether the hashes agree with the table the cases were asked
                                  with
  tools/blit2d-expect.py --c      the C table source/tests/t_blit2dref.c holds
"""

import hashlib
import sys
from fractions import Fraction
from math import floor

SRC_WIDTH, SRC_HEIGHT = 128, 32
DST_WIDTH, DST_HEIGHT = 64, 16
BLOCK_GOBS = 2
WIN_X, WIN_Y, WIN_WIDTH, WIN_HEIGHT = 5, 3, 21, 9
FILL = 0xEE

# name, origin (bit 0 of SAMPLE_MODE: 0 centre, 1 corner), texels per pixel on both axes
CASES = [
    ("blit2d_corner_half", 1, Fraction(2)),
    ("blit2d_center_half", 0, Fraction(2)),
    ("blit2d_corner_2_3", 1, Fraction(3, 2)),
    ("blit2d_center_2_3", 0, Fraction(3, 2)),
]
QUESTIONS = ("Q1", "Q2")

# The table the cases were asked with: the destination's MD5 under Q1 A and Q2 A, the one the
# 1.5 pair would give under Eden's rectangle, and the destination as filled.
ASKED = {
    "blit2d_corner_half": "84fd46bdbeba5e70f758996dd896cf84",
    "blit2d_center_half": "1654028909d43464ff4556fbf1802371",
    "blit2d_corner_2_3": "3ee33409edbf6bf600caa6435dccd6ea",
    "blit2d_center_2_3": "6560d24df99ce3fb20477313332d5d18",
}
ASKED_EDEN_2_3 = "ff261b6480a54b7fd8b2ff3479cb7352"
ASKED_UNTOUCHED = "85a0bd9e5c984f5d1786e9309dd7930e"


def gob_offset(x, y):
    """Byte x of row y inside one gob: the address bits are x[3:0], y[0], x[4], y[2:1], x[5]."""
    bits = [(x, 0), (x, 1), (x, 2), (x, 3), (y, 0), (x, 4), (y, 1), (y, 2), (x, 5)]
    return sum(((v >> b) & 1) << i for i, (v, b) in enumerate(bits))


def block_linear(x_byte, y, row_bytes):
    """Where byte x_byte of row y lands: blocks left to right then down, gobs down a block."""
    gobs_across = -(-row_bytes // 64)
    block_rows = 8 * BLOCK_GOBS
    block = (y // block_rows) * gobs_across + x_byte // 64
    gob = block * BLOCK_GOBS + (y % block_rows) // 8
    return gob * 512 + gob_offset(x_byte % 64, y % 8)


def texel(x, y):
    return 0x10000000 | y << 12 | x


def place(buf, x, y, row_bytes, value):
    at = block_linear(4 * x, y, row_bytes)
    buf[at:at + 4] = value.to_bytes(4, "little")


def source_bytes():
    buf = bytearray(SRC_WIDTH * SRC_HEIGHT * 4)
    for y in range(SRC_HEIGHT):
        for x in range(SRC_WIDTH):
            place(buf, x, y, SRC_WIDTH * 4, texel(x, y))
    return bytes(buf)


def untouched():
    return bytes([FILL]) * (DST_WIDTH * DST_HEIGHT * 4)


def pick(origin, du, n, extent, rules):
    """The source texel pixel n of the window takes on one axis of extent pixels.

    Q1 A: the corner origin samples at n * du and the centre at (n + 1/2) * du; B swaps them.
    Q2 A: exactly that, du stepped in 32.32. Q2 B: Eden's rectangle (fermi_2d.cpp:79-96), a
    corner start moved back by the integer half of du, both ends cut to whole texels, and the
    span sampled at each pixel's centre as its blit shader does.
    """
    corner = (origin == 1) != (rules["Q1"] == "B")
    if rules["Q2"] == "A":
        return floor((n + (0 if corner else Fraction(1, 2))) * du)
    start = -floor(du / 2) if corner else 0
    end = floor(start + du * extent)
    return floor(start + (n + Fraction(1, 2)) * (end - start) / extent)


def destination(origin, du, rules):
    buf = bytearray(untouched())
    for j in range(WIN_HEIGHT):
        ty = pick(origin, du, j, WIN_HEIGHT, rules)
        for i in range(WIN_WIDTH):
            tx = pick(origin, du, i, WIN_WIDTH, rules)
            place(buf, WIN_X + i, WIN_Y + j, DST_WIDTH * 4, texel(tx, ty))
    return bytes(buf)


def combos():
    """Every combination of answers, the first question's changing fastest."""
    for bits in range(1 << len(QUESTIONS)):
        yield bits, {q: "B" if bits >> i & 1 else "A" for i, q in enumerate(QUESTIONS)}


def md5(data):
    return hashlib.md5(data).hexdigest()


def case_table():
    rows = []
    for name, origin, du in CASES:
        rows.append((name, origin, du, [md5(destination(origin, du, rules))
                                        for _, rules in combos()]))
    return rows


# The 2D engine's methods, at their byte offsets in NVIDIA's cl902d.h; a header names the word.
SUBCHANNEL_2D = 3  # where the graphics library binds the 2D class at queue creation
INCREASING, INLINE = 1, 4
SET_DST_FORMAT = 0x0200  # then layout, block size, depth, layer
SET_DST_WIDTH = 0x0218  # then height, offset upper, offset lower
SET_SRC_FORMAT = 0x0230  # then layout, block size, depth
SET_SRC_WIDTH = 0x0248  # then height, offset upper, offset lower
SET_CLIP_ENABLE = 0x0290
SET_OPERATION = 0x02AC
SET_COMPRESSION = 0x02D4
SET_SAMPLE_MODE = 0x088C
SET_DST_X0 = 0x08B0  # through PIXELS_FROM_MEMORY_SRC_Y0_INT at 0x08DC, which launches
FORMAT_A8B8G8R8 = 0xD5
SRCCOPY = 3
BLOCK_SIZE_TWO_GOBS = 1 << 4

FAKE_SRC = 0x0000000512340000
FAKE_DST = 0x0000000667890000


def header(mode, method, count):
    return method // 4 | SUBCHANNEL_2D << 13 | count << 16 | mode << 29


def commands(origin, du, src, dst):
    def fixed(v):
        f = int(v * (1 << 32))
        return [f & 0xFFFFFFFF, f >> 32]

    words = [header(INLINE, SET_OPERATION, SRCCOPY), header(INLINE, SET_CLIP_ENABLE, 0)]
    words += [header(INCREASING, SET_SRC_FORMAT, 4), FORMAT_A8B8G8R8, 0, BLOCK_SIZE_TWO_GOBS, 1]
    words += [header(INCREASING, SET_SRC_WIDTH, 4), SRC_WIDTH, SRC_HEIGHT, src >> 32,
              src & 0xFFFFFFFF]
    words += [header(INCREASING, SET_DST_FORMAT, 5), FORMAT_A8B8G8R8, 0, BLOCK_SIZE_TWO_GOBS, 1,
              0]
    words += [header(INCREASING, SET_DST_WIDTH, 4), DST_WIDTH, DST_HEIGHT, dst >> 32,
              dst & 0xFFFFFFFF]
    words += [header(INLINE, SET_COMPRESSION, 1), header(INLINE, SET_SAMPLE_MODE, origin)]
    words += [header(INCREASING, SET_DST_X0, 12), WIN_X, WIN_Y, WIN_WIDTH, WIN_HEIGHT]
    words += fixed(du) + fixed(du) + [0, 0, 0, 0]
    return words


def window_counts():
    inside = WIN_WIDTH * WIN_HEIGHT
    return inside, DST_WIDTH * DST_HEIGHT - inside


def print_report():
    print("blit2d-expect.py: the blit2d suite's expectations, worked out apart from blit2dref.c")
    print()
    inside, outside = window_counts()
    first, last = 4 * WIN_X, 4 * (WIN_X + WIN_WIDTH)
    print("source %dx%d, %d bytes, MD5 %s" % (SRC_WIDTH, SRC_HEIGHT, len(source_bytes()),
                                             md5(source_bytes())))
    print("destination %dx%d, %d bytes, blocks %d gobs tall; filled with 0x%02X: MD5 %s, %s" %
          (DST_WIDTH, DST_HEIGHT, len(untouched()), BLOCK_GOBS, FILL, md5(untouched()),
           "as asked" if md5(untouched()) == ASKED_UNTOUCHED else "DIFFERS from the table"))
    print("window %dx%d at (%d,%d): %d pixels in, %d out; each row from byte %d (16-byte run %d)"
          " to byte %d (inside run %d); rows %d..%d, the gob border at row 8" %
          (WIN_WIDTH, WIN_HEIGHT, WIN_X, WIN_Y, inside, outside, first, first // 16, last,
           (last - 1) // 16, WIN_Y, WIN_Y + WIN_HEIGHT - 1))
    print()
    print("case, the answers, the first 8 texels of x and of y, the destination's MD5:")
    agree = True
    for name, origin, du, hashes in case_table():
        for bits, rules in combos():
            xs = [pick(origin, du, i, WIN_WIDTH, rules) for i in range(8)]
            ys = [pick(origin, du, j, WIN_HEIGHT, rules) for j in range(8)]
            note = ""
            if bits == 0:
                same = hashes[0] == ASKED[name]
                agree = agree and same
                note = "  as asked" if same else "  DIFFERS from the table, which says " + \
                    ASKED[name]
            elif rules == {"Q1": "A", "Q2": "B"}:
                note = "  Eden's image (G1, G2)"
                if du != 2:
                    same = hashes[bits] == ASKED_EDEN_2_3
                    agree = agree and same
                    note += ", as asked" if same else ", DIFFERS from " + ASKED_EDEN_2_3
            print("  %-19s Q1 %s Q2 %s  x %-26s y %-22s %s%s" %
                  (name if bits == 0 else "", rules["Q1"], rules["Q2"],
                   " ".join(map(str, xs)), " ".join(map(str, ys)), hashes[bits], note))
    print("every hash of the table the cases were asked with: %s" %
          ("matches" if agree else "one DIFFERS"))
    print("Eden's raw read until its cache writes the image back: the destination as filled")
    print()
    print("command words, corner origin, 1.5, source 0x%x, destination 0x%x:" %
          (FAKE_SRC, FAKE_DST))
    words = commands(1, Fraction(3, 2), FAKE_SRC, FAKE_DST)
    for i in range(0, len(words), 8):
        print("  " + " ".join("%08x" % w for w in words[i:i + 8]))


def print_c():
    print("// Generated by tools/blit2d-expect.py --c; tools/test_blit2d_expect.py checks it.")
    print("static const Blit2dPinned PINNED[] = {")
    for name, origin, du, hashes in case_table():
        print('    { "%s",' % name)
        print("      { " + ",\n        ".join('"%s"' % h for h in hashes) + " } },")
    print("};")
    print('static const char PINNED_SOURCE[] = "%s";' % md5(source_bytes()))
    print('static const char PINNED_UNTOUCHED[] = "%s";' % md5(untouched()))
    print("#define PINNED_FAKE_SRC 0x%016xull" % FAKE_SRC)
    print("#define PINNED_FAKE_DST 0x%016xull" % FAKE_DST)
    words = commands(1, Fraction(3, 2), FAKE_SRC, FAKE_DST)
    print("static const uint32_t PINNED_WORDS_CORNER_2_3[] = {")
    for i in range(0, len(words), 6):
        print("    " + ", ".join("0x%08xu" % w for w in words[i:i + 6]) + ",")
    print("};")


if __name__ == "__main__":
    if sys.argv[1:] == ["--c"]:
        print_c()
    else:
        print_report()
