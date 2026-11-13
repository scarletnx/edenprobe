#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the four hand-made ASTC blocks of source/astcblocks.c from their fields.

A block is 128 bits, little endian. The block mode is bits 0-10, the partition count less one
bits 11-12, the endpoint mode bits 13-16 and the endpoints from bit 17 (Khronos Data Format
Specification 1.3, 23.10 and 23.11); weights are stored from bit 127 downwards, bit j of weight
k at bit 127 - (k * width + j) (23.16). A void extent is bits 0-8 = 0x1FC, bit 9 = 0 (LDR),
bits 10-11 = 3, all-ones extent in bits 12-63 and the RGBA16 colour in bits 64-127 (23.9).
"""

import sys


def put(bits, pos, value, count):
    for i in range(count):
        bits[pos + i] = (value >> i) & 1


def to_bytes(bits):
    return bytes(sum(bits[8 * k + i] << i for i in range(8)) for k in range(16))


def weights_block(mode, v0, v1, weights, width):
    """One partition, endpoint mode 0 (luminance, direct), 8-bit endpoints v0 and v1."""
    bits = [0] * 128
    put(bits, 0, mode, 11)
    put(bits, 11, 0, 2)
    put(bits, 13, 0, 4)
    put(bits, 17, v0, 8)
    put(bits, 25, v1, 8)
    for k, w in enumerate(weights):
        for j in range(width):
            bits[127 - (k * width + j)] = (w >> j) & 1
    return to_bytes(bits)


def void_extent(rgba):
    bits = [0] * 128
    put(bits, 0, 0x1FC, 9)
    put(bits, 10, 3, 2)
    put(bits, 12, (1 << 52) - 1, 52)
    for c, v in enumerate(rgba):
        put(bits, 64 + 16 * c, v, 16)
    return to_bytes(bits)


def block(mode, partitions, fields, weights, width):
    """A block from its fields: the mode, the partition count, then (value, bits) pairs laid from
    bit 13 on (the endpoint mode and endpoints of one partition; the seed, the modes and the
    endpoints of several), and the weights from bit 127 down."""
    bits = [0] * 128
    put(bits, 0, mode, 11)
    put(bits, 11, partitions - 1, 2)
    pos = 13
    for value, count in fields:
        put(bits, pos, value, count)
        pos += count
    for k, w in enumerate(weights):
        for j in range(width):
            bits[127 - (k * width + j)] = (w >> j) & 1
    return to_bytes(bits)


# Mode 0x242: 4x4 grid, weight range 16 (R = 100, H = 1); 0x253: 4x4 grid, range 32; 0x042: 4x4
# grid, range 4; 0x184 and 0x1a4: 6x10 and 10x6 grids, range 2; 0x24e: 4x6 grid, range 16.
BLOCKS = {
    "void": void_extent((0x1234, 0x5678, 0x9ABC, 0xDEF0)),
    "q16": weights_block(0x242, 0, 255, list(range(16)), 4),
    "q32even": weights_block(0x253, 0, 255, [2 * k for k in range(16)], 5),
    "q32odd": weights_block(0x253, 0, 255, [2 * k + 1 for k in range(16)], 5),
    # A 10x10 block: its texels step 114 across the grid ((1024 + 5) / 9), where 1024 / 9 is
    # 113 and lands half the columns a weight short. One-bit weights 1 1 0 1 0 0 along a row of
    # six, 1 1 0 1 0 0 1 1 0 0 along a row of ten, each row a step on from the one above, so the
    # two grids read apart and every row and column of texels has its own run.
    "grid6x10": weights_block(0x184, 0, 255,
                              [(0b001011 >> ((k % 6 + k // 6) % 6)) & 1 for k in range(60)], 1),
    "grid10x6": weights_block(0x1A4, 0, 255,
                              [(0b0011001011 >> ((k % 10 + k // 10) % 10)) & 1 for k in range(60)],
                              1),
    # Endpoint mode 1: L0 = (v0 >> 2) | (v1 & 0xc0) = 0xf9, L1 = L0 + (v1 & 0x3f) = 312, clamped.
    "lumdelta": block(0x242, 1, [(1, 4), (0xE7, 8), (0xFF, 8)], list(range(16)), 4),
    # Endpoint mode 13: bases 200 120 240 180 and offsets 0x40 0x40 0x40 0x94. Each colour offset
    # is -32 after the bit transfer, a negative sum, so the endpoints swap and blue contracts
    # them: 78 58 88 228 to 110 90 120 218.
    "contract13": block(0x042, 1, [(13, 4), (200, 8), (0x40, 8), (120, 8), (0x40, 8), (240, 8),
                                   (0x40, 8), (180, 8), (0x94, 8)],
                        [k % 4 for k in range(16)], 2),
    # Two partitions of flat luminance 16 and 240 on a 6x5 block, seed 0x155: 30 texels is a
    # small block, so the partition hash takes x and y doubled.
    "part6x5": block(0x042, 2, [(0x155, 10), (0, 6), (16, 8), (16, 8), (240, 8), (240, 8)],
                     [0] * 16, 2),
    # Two luminance endpoints in quints with five bits each, 15 bits, right below 96 bits of
    # weights: the group's third value and its Q[6:5] are not there and read as nought, which
    # with Q[2:0] = 110 makes both quints 4 (endpoints 6 and 249). Weight 22's bits 1 and 2 sit
    # where Q[6:5] would be: read as 11 they make both quints 0.
    "cutquints": block(0x24E, 1, [(0, 4), (0, 5), (0b110, 3), (1, 5), (0, 2)],
                       [6 if k == 22 else k % 16 for k in range(24)], 4),
    # Four luminance and alpha values in trits with six bits each, 31 bits, right below 80 bits
    # of weights: the fifth value and T[7] are not there. T[6:5] = 11 makes the fourth trit
    # T[7], nought; weight 14's bit 3 sits where T[7] would be.
    "cuttrits": block(0x253, 1, [(4, 4), (0, 6), (0, 2), (0b111110, 6), (0, 2), (0b000010, 6),
                                 (0, 1), (0b111110, 6), (0b11, 2)],
                      [8 if k == 14 else (2 * k) % 32 for k in range(16)], 5),
    # A reserved mode, over the fields of "grid6x10": bits 0 and 1 nought, bits 7 and 8 set and
    # bit 6 too, the row of the layout table that has 6x10 and 10x6 for A of 0 and 1 and nothing
    # for 2 and 3. Its weight range is a legal one and a 10x10 block holds either grid, so only
    # that row makes it illegal.
    "reserved": weights_block(0x1C4, 0, 255,
                              [(0b001011 >> ((k % 6 + k // 6) % 6)) & 1 for k in range(60)], 1),
    # Two planes of sixteen 5-bit weights: 160 bits of weights, over the 96 a block may hold.
    "weightbits": block(0x653, 1, [(0, 4), (0, 8), (255, 8)], [0] * 16, 5),
    # Two planes and four partitions, every other field legal: 32 bits of weights, eight
    # luminance endpoints of eight bits.
    "dual4": block(0x441, 4, [(0x2AA, 10), (0, 6)] + [(31 * k, 8) for k in range(1, 9)],
                   [k & 1 for k in range(32)], 1),
}


def main():
    for name, block in BLOCKS.items():
        print(name, ", ".join("0x%02x" % b for b in block))
    return 0


if __name__ == "__main__":
    sys.exit(main())
