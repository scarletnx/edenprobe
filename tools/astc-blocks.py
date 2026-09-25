#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: MIT
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


# Mode 0x242: 4x4 grid, weight range 16 (R = 100, H = 1); 0x253: 4x4 grid, range 32.
BLOCKS = {
    "void": void_extent((0x1234, 0x5678, 0x9ABC, 0xDEF0)),
    "q16": weights_block(0x242, 0, 255, list(range(16)), 4),
    "q32even": weights_block(0x253, 0, 255, [2 * k for k in range(16)], 5),
    "q32odd": weights_block(0x253, 0, 255, [2 * k + 1 for k in range(16)], 5),
}


def main():
    for name, block in BLOCKS.items():
        print(name, ", ".join("0x%02x" % b for b in block))
    return 0


if __name__ == "__main__":
    sys.exit(main())
