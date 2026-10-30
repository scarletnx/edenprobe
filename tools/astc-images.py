#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds source/astcimages.c: one 128x128 picture, encoded by ARM's astcenc, for the astcimage suite.

The picture is made here, integer arithmetic only, so every machine makes the same bytes. It has
four 64x64 regions, each what a texture compressor finds hard in its own way: smooth gradients,
hard edges where three colours meet inside one block, fine noise, and alpha (a ramp above, a
cutout below). astcenc encodes it at each footprint the suite samples, as a game's tools would,
and decodes it again both ways an implementation may: to FP16, the specification's default (-dh,
to half floats, which this rounds to eight bits as a target would), and to eight bits
(decode_unorm8, what astcenc does whenever its output is eight bits). The C file carries the
blocks and an FNV-1a hash of each decode, which the reference suite holds source/astcref.c to.
An sRGB image decodes to eight bits either way, so its two hashes are one.

    tools/astc-images.py --astcenc PATH/astcenc-none [--out source/astcimages.c]

astcenc is commit 9fbec680 built with -DASTCENC_ISA_NONE=ON, run on one thread at -medium.
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

SIZE = 128
HALF = SIZE // 2

# name, footprint, sRGB: the sRGB ones are encoded as sRGB (astcenc -cs) and sampled through an
# sRGB format.
IMAGES = (
    ("4x4", 4, 4, False),
    ("6x6", 6, 6, False),
    ("8x8", 8, 8, False),
    ("12x12", 12, 12, False),
    ("4x4srgb", 4, 4, True),
    ("8x8srgb", 8, 8, True),
)


def clamp(v):
    return 0 if v < 0 else 255 if v > 255 else v


def noise(x, y):
    """A 32-bit hash of the texel's position (lowbias32), with no pattern a block could follow."""
    s = (y * HALF + x + 0x9E3779B9) & 0xFFFFFFFF
    s ^= s >> 16
    s = (s * 0x7FEB352D) & 0xFFFFFFFF
    s ^= s >> 15
    s = (s * 0x846CA68B) & 0xFFFFFFFF
    s ^= s >> 16
    return s


def texel(x, y):
    """RGBA of texel (x, y)."""
    u, v = x % HALF, y % HALF
    if y < HALF and x < HALF:
        # Gradients: a ramp per channel and a bowl, nothing a block cannot follow.
        d = (u - 32) * (u - 32) + (v - 32) * (v - 32)
        return (clamp(24 + 3 * u), clamp(40 + 3 * v), clamp(220 - u - v - (d >> 5)), 255)
    if y < HALF:
        # Edges: a disc, a diagonal split, thin lines and a three colour checker, so many blocks
        # hold three colours or more.
        if (u - 20) * (u - 20) + (v - 20) * (v - 20) < 196:
            return (220, 40, 30, 255)
        if u >= 40 and v >= 40:
            return ((250, 250, 250, 255), (20, 20, 20, 255), (40, 200, 60, 255))[(u // 3 + v // 3) % 3]
        if u % 9 == 4:
            return (255, 255, 255, 255)
        return (30, 60, 200, 255) if u + v > 70 else (240, 200, 40, 255)
    if x < HALF:
        # Noise: grain of up to 40 either way on a mid colour, independent per channel.
        n = noise(u, v)
        return (clamp(110 + (n & 0xFF) * 80 // 255 - 40), clamp(140 + (n >> 8 & 0xFF) * 80 // 255 - 40),
                clamp(90 + (n >> 16 & 0xFF) * 80 // 255 - 40), 255)
    # Alpha: a colour gradient whose alpha ramps across the top half and is cut out below.
    rgb = (clamp(255 - 3 * u), clamp(128 + v), clamp(4 * u))
    if v < 32:
        a = clamp(8 * v + 2 * u)
    else:
        a = 255 if (u * u + 3 * v * v) % 23 < 12 else 0
    return rgb + (a,)


def picture():
    """The RGBA bytes, row major."""
    return bytes(c for y in range(SIZE) for x in range(SIZE) for c in texel(x, y))


def fnv1a(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def write_png(path, rgba):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    rows = b"".join(b"\0" + rgba[y * SIZE * 4:(y + 1) * SIZE * 4] for y in range(SIZE))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def read_astc(path, bw, bh):
    """The blocks of a .astc file, after checking its header says what was asked for."""
    data = open(path, "rb").read()
    magic, x, y, z = struct.unpack("<I3B", data[:7])
    dims = [int.from_bytes(data[7 + 3 * i:10 + 3 * i], "little") for i in range(3)]
    if magic != 0x5CA1AB13 or (x, y, z) != (bw, bh, 1) or dims != [SIZE, SIZE, 1]:
        raise SystemExit("%s: header says %ux%ux%u blocks, %s texels" % (path, x, y, z, dims))
    blocks = data[16:]
    count = -(-SIZE // bw) * -(-SIZE // bh)
    if len(blocks) != 16 * count:
        raise SystemExit("%s: %u bytes of blocks, %u blocks wanted" % (path, len(blocks), count))
    return blocks


def read_ktx(path, half):
    """The texels of the uncompressed KTX 1 file astcenc decodes into, as eight bit RGBA: bytes as
    they are, or half floats times 255 rounded to nearest, ties to even (exact: eleven bits)."""
    data = open(path, "rb").read()
    if data[:12] != b"\xabKTX 11\xbb\r\n\x1a\n":
        raise SystemExit("%s is not a KTX 1 file" % path)
    fields = struct.unpack("<13I", data[12:64])
    gl_type, gl_format, width, height, kv = fields[1], fields[3], fields[6], fields[7], fields[12]
    want = 0x140B if half else 0x1401
    if (gl_type, gl_format, width, height) != (want, 0x1908, SIZE, SIZE):
        raise SystemExit("%s: type %#x format %#x %ux%u, type %#x RGBA %ux%u wanted" %
                         (path, gl_type, gl_format, width, height, want, SIZE, SIZE))
    start = 64 + kv
    (size,) = struct.unpack("<I", data[start:start + 4])
    if size != SIZE * SIZE * 4 * (2 if half else 1):
        raise SystemExit("%s: %u bytes of texels" % (path, size))
    texels = data[start + 4:start + 4 + size]
    if not half:
        return texels
    return bytes(round(v * 255) for (v,) in struct.iter_unpack("<e", texels))


def encode(astcenc, png, tmp, name, bw, bh, srgb):
    """The blocks, and the hashes of the FP16 decode and of the eight bit one."""
    out = os.path.join(tmp, name + ".astc")
    ktx = os.path.join(tmp, name + ".ktx")
    profile = "s" if srgb else "l"
    subprocess.run([astcenc, "-c" + profile, png, out, "%ux%u" % (bw, bh), "-medium", "-j", "1", "-silent"],
                   check=True)
    subprocess.run([astcenc, "-d" + profile, out, ktx, "-j", "1", "-silent"], check=True)
    unorm8 = fnv1a(read_ktx(ktx, False))
    if srgb:
        return read_astc(out, bw, bh), unorm8, unorm8
    # The HDR profile decodes an LDR block as the LDR one does, to FP16; the picture has no HDR
    # block, whose error colour would differ.
    half = os.path.join(tmp, name + "-half.ktx")
    subprocess.run([astcenc, "-dh", out, half, "-j", "1", "-silent"], check=True)
    return read_astc(out, bw, bh), fnv1a(read_ktx(half, True)), unorm8


def c_bytes(name, data):
    lines = ["static const uint8_t %s[%u] = {" % (name, len(data))]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def c_file(source_hash, encoded):
    parts = [
        "// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors",
        "// SPDX-License-Identifier: GPL-3.0-or-later",
        "",
        "// Written by tools/astc-images.py; run it again rather than editing this file.",
        "",
        '#include "astcimages.h"',
        "",
        "const uint32_t ASTC_IMAGE_SOURCE_HASH = 0x%08xu;" % source_hash,
        "",
    ]
    for (name, bw, bh, srgb), (blocks, _, _) in zip(IMAGES, encoded):
        parts.append(c_bytes("BLOCKS_%s" % name.upper(), blocks))
        parts.append("")
    parts.append("const AstcImage ASTC_IMAGE[ASTC_IMAGES] = {")
    for (name, bw, bh, srgb), (blocks, h, h8) in zip(IMAGES, encoded):
        parts.append('    { "%s", %u, %u, %s, BLOCKS_%s, %u, 0x%08xu, 0x%08xu },' %
                     (name, bw, bh, "true" if srgb else "false", name.upper(), len(blocks) // 16, h, h8))
    parts.append("};")
    return "\n".join(parts) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--astcenc", required=True, help="astcenc built from commit 9fbec680, ISA none")
    parser.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "source",
                                                      "astcimages.c"))
    args = parser.parse_args()
    rgba = picture()
    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, "picture.png")
        write_png(png, rgba)
        encoded = [encode(args.astcenc, png, tmp, *image) for image in IMAGES]
    with open(args.out, "w") as f:
        f.write(c_file(fnv1a(rgba), encoded))
    for (name, bw, bh, _), (blocks, h, h8) in zip(IMAGES, encoded):
        print("%s: %u blocks, decode hashes %08x (FP16) and %08x (eight bits)" % (name, len(blocks) // 16, h, h8))
    return 0


if __name__ == "__main__":
    sys.exit(main())
