#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""What each case of the video family expects, worked out apart from source/vicref.c.

The VIC's configuration structure is packed from the bit positions libdrm's tests/tegra/vic40.h
gives, the command words from the host1x opcodes, and every case's output from the rules the
cases are judged by, one rule per open question. It shares no code with vicref.c: the reference
suite holds the C model to the hashes this prints, so a mistake would have to be made twice, the
same way, to pass.

  tools/vic-expect.py          the tables, the BT.601 colours, and whether they agree with the
                               table the cases were asked with
  tools/vic-expect.py --c      the C table source/tests/t_vicref.c holds
"""

import sys

CONFIG_SIZE = 0x610
SLOT_BASE = 0x90
SLOT_SIZE = 0xB0

# Bit position and width of every field a case sets, from the start of the structure. Each
# group's start is its byte offset times eight; the positions inside a group are vic40.h's.
OUT_CFG = 0x10 * 8
OUT_SURF = 0x20 * 8
FIELDS = {
    "downsample_horiz": (0, 11),
    "downsample_vert": (16, 11),
    "alpha_fill_mode": (OUT_CFG + 0, 3),
    "alpha_fill_slot": (OUT_CFG + 3, 3),
    "background_a": (OUT_CFG + 6, 10),
    "background_r": (OUT_CFG + 16, 10),
    "background_g": (OUT_CFG + 26, 10),
    "background_b": (OUT_CFG + 36, 10),
    "target_left": (OUT_CFG + 64, 14),
    "target_right": (OUT_CFG + 80, 14),
    "target_top": (OUT_CFG + 96, 14),
    "target_bottom": (OUT_CFG + 112, 14),
    "out_format": (OUT_SURF + 0, 7),
    "out_block_kind": (OUT_SURF + 11, 4),
    "out_block_height": (OUT_SURF + 15, 4),
    "out_surface_width": (OUT_SURF + 32, 14),
    "out_surface_height": (OUT_SURF + 46, 14),
    "out_luma_width": (OUT_SURF + 64, 14),
    "out_luma_height": (OUT_SURF + 78, 14),
    "out_chroma_width": (OUT_SURF + 96, 14),
    "out_chroma_height": (OUT_SURF + 110, 14),
}
# Inside a slot: SlotConfig at 0, SlotSurfaceConfig at 0x40, the colour matrix at 0x60.
SLOT_FIELDS = {
    "enable": (0, 1),
    "current_field": (8, 1),
    "frame_format": (16, 4),
    "clamp_low": (128, 10),
    "clamp_high": (138, 10),
    "planar_alpha": (160, 10),
    "constant_alpha": (170, 1),
    "source_left": (256, 30),
    "source_right": (288, 30),
    "source_top": (320, 30),
    "source_bottom": (352, 30),
    "dest_left": (384, 14),
    "dest_right": (400, 14),
    "dest_top": (416, 14),
    "dest_bottom": (432, 14),
    "format": (0x40 * 8 + 0, 7),
    "block_kind": (0x40 * 8 + 11, 4),
    "block_height": (0x40 * 8 + 15, 4),
    "cache_width": (0x40 * 8 + 19, 3),
    "surface_width": (0x40 * 8 + 32, 14),
    "surface_height": (0x40 * 8 + 46, 14),
    "luma_width": (0x40 * 8 + 64, 14),
    "luma_height": (0x40 * 8 + 78, 14),
    "chroma_width": (0x40 * 8 + 96, 14),
    "chroma_height": (0x40 * 8 + 110, 14),
    "matrix_shift": (0x60 * 8 + 60, 4),
    "matrix_enable": (0x60 * 8 + 127, 1),
}
# coefficient (row, column) of the colour matrix: rows R, G, B; columns Y, U, V, constant.
# Word c holds column c, rows at 0, 20 and 40.
def coeff_position(row, column):
    return (0x60 * 8 + column * 64 + row * 20, 20)


FORMAT_A8B8G8R8 = 31
FORMAT_A8R8G8B8 = 32
FORMAT_X8B8G8R8 = 35
FORMAT_UV_N420 = 67
FORMAT_VU_N420 = 68

SIZE = 16
LUMA_STRIDE = 256
CHROMA_ROWS = SIZE // 2
RGBA_PITCH = 1024
FILL = 0xEE

BT601 = [[298, 0, 409, -228480], [298, -100, -208, 138624], [298, 516, 0, -283264]]


def put(config, position, value):
    bit, width = position
    value &= (1 << width) - 1
    for i in range(width):
        b = bit + i
        if value >> i & 1:
            config[b // 8] |= 1 << (b % 8)
        else:
            config[b // 8] &= ~(1 << (b % 8)) & 0xFF


# ---- the jobs ----------------------------------------------------------------------------

def standard_image():
    """Y = 16 + 14 x down every column, U 90 and V 240 everywhere."""
    luma = bytearray(LUMA_STRIDE * SIZE)
    chroma = bytearray(LUMA_STRIDE * CHROMA_ROWS)
    for y in range(SIZE):
        for x in range(SIZE):
            luma[y * LUMA_STRIDE + x] = 16 + 14 * x
    for y in range(CHROMA_ROWS):
        for x in range(0, SIZE, 2):
            chroma[y * LUMA_STRIDE + x] = 90
            chroma[y * LUMA_STRIDE + x + 1] = 240
    return luma, chroma


def flat_image(yv, u, v):
    luma = bytearray(LUMA_STRIDE * SIZE)
    chroma = bytearray(LUMA_STRIDE * CHROMA_ROWS)
    for y in range(SIZE):
        for x in range(SIZE):
            luma[y * LUMA_STRIDE + x] = yv
    for y in range(CHROMA_ROWS):
        for x in range(0, SIZE, 2):
            chroma[y * LUMA_STRIDE + x] = u
            chroma[y * LUMA_STRIDE + x + 1] = v
    return luma, chroma


def slot(image, **over):
    s = dict(enable=1, current_field=1, frame_format=0, clamp_low=0, clamp_high=1023,
             planar_alpha=1023, constant_alpha=1, source=(0, 0, 15, 15), dest=(0, 0, 15, 15),
             format=FORMAT_VU_N420, cache_width=2, surface_width=15, surface_height=15,
             luma_width=255, luma_height=15, chroma_width=127, chroma_height=7, matrix=None,
             matrix_shift=0, image=image)
    s.update(over)
    return s


def job(slots, **over):
    j = dict(alpha_fill_mode=0, alpha_fill_slot=0, background=(0, 0, 0, 0),
             target=(0, 0, 15, 15), out_format=FORMAT_A8B8G8R8, out_surface_width=15,
             out_surface_height=15, out_luma_width=255, out_luma_height=15,
             out_chroma_width=16383, out_chroma_height=16383, slots=slots)
    j.update(over)
    return j


def times4(m):
    return [[r[0] * 4, r[1] * 4, r[2] * 4, r[3]] for r in m]


def build_cases():
    std = standard_image()
    cases = []

    def case(name, jobs, questions):
        cases.append((name, jobs, questions))

    case("vic_passthrough", [job([slot(std)])], ["Q11"])
    case("vic_chroma_order", [job([slot(std, format=FORMAT_UV_N420)])], ["Q8", "Q11"])
    case("vic_order_argb", [job([slot(std)], out_format=FORMAT_A8R8G8B8)], ["Q11"])
    case("vic_order_x8", [job([slot(std, planar_alpha=512)], out_format=FORMAT_X8B8G8R8,
                              alpha_fill_mode=3)], ["Q2", "Q11"])
    colours = [(235, 128, 128), (16, 128, 128), (126, 128, 128), (81, 90, 240), (145, 54, 34),
               (41, 240, 110)]
    case("vic_matrix_601", [job([slot(flat_image(*c), matrix=BT601)]) for c in colours],
         ["Q9", "Q11"])
    case("vic_matrix_shift", [job([slot(flat_image(126, 128, 128), matrix=times4(BT601),
                                        matrix_shift=2)])], ["Q3", "Q9"])
    case("vic_soft_clamp", [job([slot(flat_image(c, 128, 128), matrix=BT601, clamp_low=64,
                                      clamp_high=940)], alpha_fill_mode=3)
                            for c in (235, 16)], ["Q4", "Q9"])
    case("vic_clamp_nomatrix", [job([slot(flat_image(c, 128, 128), clamp_low=64,
                                          clamp_high=940)]) for c in (0, 255)],
         ["Q10", "Q5", "Q4"])
    case("vic_alpha_opaque", [job([slot(std, planar_alpha=a)]) for a in (512, 0)],
         ["Q5", "Q11"])
    case("vic_planar_alpha", [job([slot(std, planar_alpha=a)], alpha_fill_mode=3)
                              for a in (1023, 512, 0)], ["Q11"])
    case("vic_target_clip", [job([slot(std)], target=(4, 4, 11, 11))], ["Q7", "Q11"])
    case("vic_target_clip_matrix", [job([slot(flat_image(126, 128, 128), matrix=BT601)],
                                        target=(4, 4, 11, 11))], ["Q7", "Q9"])
    case("vic_target_odd", [job([slot(std)], target=(5, 0, 15, 15))], ["Q7", "Q11"])
    bg = (1023, 512, 0, 1023)
    case("vic_background", [job([slot(std, enable=0)], target=(2, 2, 13, 13), background=bg)],
         ["Q7"])
    case("vic_background_ring", [job([slot(std, source=(4, 4, 11, 11), dest=(4, 4, 11, 11))],
                                     background=bg)], ["Q11"])
    case("vic_source_crop", [job([slot(std, source=(8, 0, 15, 15), dest=(0, 0, 7, 15))],
                                 target=(0, 0, 7, 15))], ["Q7", "Q11"])
    case("vic_two_slots", [job([slot(flat_image(60, 128, 128)),
                                slot(flat_image(180, 128, 128), source=(4, 4, 11, 11),
                                     dest=(4, 4, 11, 11))])], ["Q6"])
    case("vic_pitch", [job([slot(std)], out_surface_width=99)], ["Q7", "Q11"])
    case("vic_nv12_out", [job([slot(std)], out_format=FORMAT_VU_N420, out_chroma_width=127,
                              out_chroma_height=7)], [])
    return cases


def pack(j):
    config = bytearray(CONFIG_SIZE)
    put(config, FIELDS["downsample_horiz"], 4)
    put(config, FIELDS["downsample_vert"], 4)
    put(config, FIELDS["alpha_fill_mode"], j["alpha_fill_mode"])
    put(config, FIELDS["alpha_fill_slot"], j["alpha_fill_slot"])
    r, g, b, a = j["background"]
    put(config, FIELDS["background_r"], r)
    put(config, FIELDS["background_g"], g)
    put(config, FIELDS["background_b"], b)
    put(config, FIELDS["background_a"], a)
    left, top, right, bottom = j["target"]
    put(config, FIELDS["target_left"], left)
    put(config, FIELDS["target_top"], top)
    put(config, FIELDS["target_right"], right)
    put(config, FIELDS["target_bottom"], bottom)
    for key in ("out_format", "out_surface_width", "out_surface_height", "out_luma_width",
                "out_luma_height", "out_chroma_width", "out_chroma_height"):
        put(config, FIELDS[key], j[key])
    for n, s in enumerate(j["slots"]):
        base = (SLOT_BASE + n * SLOT_SIZE) * 8

        def sput(key, value):
            bit, width = SLOT_FIELDS[key]
            put(config, (base + bit, width), value)

        for key in ("enable", "current_field", "frame_format", "planar_alpha", "constant_alpha",
                    "format", "cache_width", "surface_width", "surface_height", "luma_width",
                    "luma_height", "chroma_width", "chroma_height"):
            sput(key, s[key])
        sput("clamp_low", s["clamp_low"])
        sput("clamp_high", s["clamp_high"])
        sl, st, sr, sb = s["source"]
        sput("source_left", sl << 16)
        sput("source_top", st << 16)
        sput("source_right", sr << 16)
        sput("source_bottom", sb << 16)
        dl, dt, dr, db = s["dest"]
        sput("dest_left", dl)
        sput("dest_top", dt)
        sput("dest_right", dr)
        sput("dest_bottom", db)
        if s["matrix"] is not None:
            sput("matrix_enable", 1)
            sput("matrix_shift", s["matrix_shift"])
            for row in range(3):
                for column in range(4):
                    bit, width = coeff_position(row, column)
                    put(config, (base + bit, width), s["matrix"][row][column])
    return bytes(config)


# ---- the command words ----------------------------------------------------------------------

def setclass(cls):
    return (cls & 0x3FF) << 6


def incr(offset, count):
    return (1 << 28) | (offset << 16) | count


def nonincr(offset, count):
    return (2 << 28) | (offset << 16) | count


def method(words, offset, value):
    words += [incr(0x10, 2), offset >> 2, value & 0xFFFFFFFF]


def commands(iovas, slots, nv12_out, syncpt):
    """iovas: config, out luma, out chroma, then luma and chroma of each slot."""
    w = [setclass(0x5D)]
    method(w, 0x704, (CONFIG_SIZE // 16) << 16 | 1 << 8 | 1)
    method(w, 0x708, iovas["config"] >> 8)
    method(w, 0x720, iovas["out_luma"] >> 8)
    if nv12_out:
        method(w, 0x724, iovas["out_chroma"] >> 8)
    for n in range(slots):
        method(w, 0x400 + n * 0x60, iovas["luma%d" % n] >> 8)
        method(w, 0x404 + n * 0x60, iovas["chroma%d" % n] >> 8)
    method(w, 0x300, 1 << 8)
    w += [nonincr(0, 1), 1 << 8 | syncpt]
    return w


# ---- the rules ------------------------------------------------------------------------------

def floor_shift(value, n):
    return value >> n  # Python's shift floors, as an arithmetic shift does


def matrix_channel(m, row, yuv, shift, after, rounding):
    s = yuv[0] * m[row][0] + yuv[1] * m[row][1] + yuv[2] * m[row][2]
    if after:
        t = floor_shift(s + m[row][3], shift)
    else:
        t = floor_shift(s, shift) + m[row][3]
    return floor_shift(t + (128 if rounding else 0), 8)


def model(j, rules):
    """The output bytes one job leaves, from a buffer filled with FILL."""
    nv12 = j["out_format"] == FORMAT_VU_N420
    if nv12:
        out_luma = bytearray([FILL]) * (LUMA_STRIDE * SIZE)
        out_chroma = bytearray([FILL]) * (LUMA_STRIDE * CHROMA_ROWS)
    else:
        out = bytearray([FILL]) * (RGBA_PITCH * SIZE)
    width = j["out_surface_width"] + 1
    height = j["out_surface_height"] + 1
    left, top, right, bottom = j["target"]
    bg = j["background"]  # r, g, b, a in ten bits
    order = list(range(len(j["slots"])))
    if rules["Q6"] == "B":
        order.reverse()  # slot 0 drawn last, so on top
    plane = [[None] * width for _ in range(height)]
    for y in range(height):
        for x in range(width):
            if not (left <= x <= right and top <= y <= bottom):
                if rules["Q7"] == "B":
                    plane[y][x] = (bg[0], bg[1], bg[2], bg[3])
                continue
            px = [bg[0], bg[1], bg[2], bg[3]]
            covered = False
            for n in order:
                s = j["slots"][n]
                if not s["enable"]:
                    continue
                dl, dt, dr, db = s["dest"]
                if not (dl <= x <= dr and dt <= y <= db):
                    continue
                sx = s["source"][0] + x - dl
                sy = s["source"][1] + y - dt
                luma, chroma = s["image"]
                yv = luma[sy * LUMA_STRIDE + sx]
                at = (sy // 2) * LUMA_STRIDE + (sx // 2) * 2
                pair = chroma[at:at + 2]
                u_even = rules["Q11"] == "A"  # format 68 reads U from the even byte
                if s["format"] == FORMAT_UV_N420 and rules["Q8"] == "A":
                    u_even = not u_even  # 67 the other way round from 68
                u, v = (pair[0], pair[1]) if u_even else (pair[1], pair[0])
                yuv = (yv << 2, u << 2, v << 2)
                low, high = s["clamp_low"], s["clamp_high"]
                alpha = s["planar_alpha"]
                if s["matrix"] is not None:
                    rgb = [matrix_channel(s["matrix"], r, yuv, s["matrix_shift"],
                                          rules["Q3"] == "B", rules["Q9"] == "B")
                           for r in range(3)]
                    clamp = True
                else:
                    rgb = list(yuv)
                    clamp = rules["Q10"] == "A"
                if clamp:
                    rgb = [min(max(c, low), high) for c in rgb]
                    if rules["Q4"] == "A":
                        alpha = min(max(alpha, low), high)
                px = [rgb[0], rgb[1], rgb[2], alpha]
                covered = True
            mode = j["alpha_fill_mode"]
            if mode == 0 and rules["Q5"] == "A":
                px[3] = 1023
            elif mode == 3 and not covered:
                px[3] = bg[3]
            plane[y][x] = tuple(px)
    for y in range(height):
        for x in range(width):
            p = plane[y][x]
            if p is None:
                continue
            r, g, b, a = (c >> 2 for c in p)
            if nv12:
                out_luma[y * LUMA_STRIDE + x] = r
                if y % 2 == 0 and x % 2 == 0:
                    # the output order is the input's: U first when 68 reads U first
                    first, second = (g, b) if rules["Q11"] == "A" else (b, g)
                    out_chroma[(y // 2) * LUMA_STRIDE + x] = first
                    out_chroma[(y // 2) * LUMA_STRIDE + x + 1] = second
            else:
                f = j["out_format"]
                if f == FORMAT_A8B8G8R8:
                    data = (r, g, b, a)
                elif f == FORMAT_A8R8G8B8:
                    data = (b, g, r, a)
                elif f == FORMAT_X8B8G8R8:
                    data = (r, g, b, a if rules["Q2"] == "A" else 255)
                else:
                    raise ValueError(f)
                out[y * RGBA_PITCH + x * 4: y * RGBA_PITCH + x * 4 + 4] = bytes(data)
    return bytes(out_luma + out_chroma) if nv12 else bytes(out)


QUESTIONS = ["Q2", "Q3", "Q4", "Q5", "Q6", "Q7", "Q8", "Q9", "Q10", "Q11"]


def combos(questions):
    for bits in range(1 << len(questions)):
        rules = {q: "A" for q in QUESTIONS}
        for i, q in enumerate(questions):
            rules[q] = "B" if bits >> i & 1 else "A"
        yield bits, rules


def fnv1a(data, h=2166136261):
    for byte in data:
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def case_table():
    rows = []
    for name, jobs, questions in build_cases():
        config_hash = 2166136261
        for j in jobs:
            config_hash = fnv1a(pack(j), config_hash)
        expected = []
        for bits, rules in combos(questions):
            h = 2166136261
            for j in jobs:
                h = fnv1a(model(j, rules), h)
            expected.append(h)
        rows.append((name, questions, config_hash, expected))
    return rows


FAKE_IOVAS = {"config": 0x00100000, "out_luma": 0x00105000, "out_chroma": 0x00109000,
              "luma0": 0x00101000, "chroma0": 0x00102000, "luma1": 0x00103000,
              "chroma1": 0x00104000}
FAKE_SYNCPT = 33

# The table vic_matrix_601 was asked with: YUV in, then R, G, B truncated and rounded.
ASKED_601 = [
    ((235, 128, 128), (254, 254, 254), (255, 255, 255)),
    ((16, 128, 128), (0, 0, 0), (0, 0, 0)),
    ((126, 128, 128), (128, 128, 128), (128, 128, 128)),
    ((81, 90, 240), (254, 0, 0), (254, 0, 0)),
    ((145, 54, 34), (0, 255, 1), (0, 255, 1)),
    ((41, 240, 110), (0, 0, 254), (0, 0, 254)),
]


def bt601(yuv, rounding):
    ten = tuple(c << 2 for c in yuv)
    return tuple(min(max(matrix_channel(BT601, r, ten, 0, False, rounding), 0), 1023) >> 2
                 for r in range(3))


def shift_case(after):
    ten = (126 << 2, 128 << 2, 128 << 2)
    return tuple(min(max(matrix_channel(times4(BT601), r, ten, 2, after, False), 0), 1023) >> 2
                 for r in range(3))


def print_report():
    print("vic-expect.py: the video family's expectations, worked out apart from vicref.c")
    print()
    print("BT.601 (S12.8, shift 0, constant after the shift, >> 8, clamp 0..1023, >> 2):")
    agree = True
    for yuv, trunc, rnd in ASKED_601:
        a, b = bt601(yuv, False), bt601(yuv, True)
        same = a == trunc and b == rnd
        agree = agree and same
        print("  YUV %3d %3d %3d  truncated %3d %3d %3d  rounded %3d %3d %3d  %s" %
              (yuv + a + b + ("as asked" if same else "NOT as asked",)))
    print("the table vic_matrix_601 was asked with: %s" % ("matches" if agree else "DIFFERS"))
    print("vic_matrix_shift (126, 128, 128): shift before the constant %s, after it %s" %
          (shift_case(False), shift_case(True)))
    print()
    print("command words, one slot, RGBA out, syncpoint %d:" % FAKE_SYNCPT)
    print("  " + " ".join("%08x" % w for w in commands(FAKE_IOVAS, 1, False, FAKE_SYNCPT)))
    print()
    print("case, the questions it is open on, the config hash, the expected-output hash per")
    print("combination of answers (A..A first, the first question's answer changing fastest):")
    for name, questions, config_hash, expected in case_table():
        print("  %-22s %-16s %08x  %s" % (name, " ".join(questions) or "-", config_hash,
                                         " ".join("%08x" % h for h in expected)))


def print_c():
    print("// Generated by tools/vic-expect.py --c; tools/test_vic_expect.py checks it.")
    print("static const VicPinned PINNED[] = {")
    for name, questions, config_hash, expected in case_table():
        print('    { "%s", 0x%08xu, %d,' % (name, config_hash, len(expected)))
        hashes = ["0x%08xu" % h for h in expected]
        rows = [", ".join(hashes[i:i + 6]) for i in range(0, len(hashes), 6)]
        print("      {" + (",\n       ".join(rows)) + "} },")
    print("};")
    words = commands(FAKE_IOVAS, 1, False, FAKE_SYNCPT)
    print("static const uint32_t COMMANDS_ONE_SLOT[] = {")
    for i in range(0, len(words), 6):
        print("    " + ", ".join("0x%08xu" % w for w in words[i:i + 6]) + ",")
    print("};")
    for slots, nv12, label in ((2, False, "TWO_SLOTS"), (1, True, "NV12_OUT")):
        print("#define COMMANDS_%s_HASH 0x%08xu" % (label, fnv1a(b"".join(
            w.to_bytes(4, "little") for w in commands(FAKE_IOVAS, slots, nv12, FAKE_SYNCPT)))))


if __name__ == "__main__":
    if sys.argv[1:] == ["--c"]:
        print_c()
    else:
        print_report()
