# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""source/vp9jobs.c holds what tools/vp9-jobs.py works out from the frames it carries, and the
script's pieces give the answers worked out by hand: the container, the superframe index, the
boolean decoder, the two layouts the engine reads at the offsets reden names, the tiles and the
surfaces."""

import importlib.util
import os
import struct
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("vp9_jobs", os.path.join(HERE, "vp9-jobs.py"))
vj = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(vj)
DSPEC = importlib.util.spec_from_file_location("vp9defaults", os.path.join(HERE, "vp9defaults.py"))
vd = importlib.util.module_from_spec(DSPEC)
DSPEC.loader.exec_module(vd)

SOURCE = os.path.join(HERE, "..", "source", "vp9jobs.c")
HEADER = os.path.join(HERE, "..", "source", "vp9jobs.h")


class Rng:
    """xorshift32, so the symbols are the same on every Python."""

    def __init__(self, seed):
        self.state = seed

    def below(self, n):
        x = self.state
        x ^= x << 13 & 0xFFFFFFFF
        x ^= x >> 17
        x ^= x << 5 & 0xFFFFFFFF
        self.state = x
        return x % n


class Writer:
    """libvpx's boolean encoder (vpx_dsp/bitwriter.h, BSD) and its probability update coding
    (vp9/encoder/vp9_subexp.c): the other side of what the script reads, written apart from it."""

    def __init__(self):
        self.low, self.range, self.count, self.out = 0, 255, -24, bytearray()
        self.write(0, 128)  # the marker

    def write(self, bit, p):
        split = 1 + ((self.range - 1) * p >> 8)
        rng = split
        if bit:
            self.low += split
            rng = self.range - split
        shift = 8 - rng.bit_length()
        rng <<= shift
        count = self.count + shift
        if count >= 0:
            offset = shift - count
            if self.low << (offset - 1) & 0x80000000:
                x = len(self.out) - 1
                while x >= 0 and self.out[x] == 0xFF:
                    self.out[x] = 0
                    x -= 1
                self.out[x] += 1
            self.out.append(self.low >> (24 - offset) & 0xFF)
            self.low = self.low << offset & 0xFFFFFF
            shift = count
            count -= 8
        self.low <<= shift
        self.count, self.range = count, rng

    def literal(self, v, n):
        for i in range(n - 1, -1, -1):
            self.write(v >> i & 1, 128)

    def prob_update(self, new, old):
        self.write(int(new != old), 252)
        if new == old:
            return
        v, m = new - 1, old - 1
        def recenter(v, m):
            return v if v > m << 1 else (v - m) << 1 if v >= m else ((m - v) << 1) - 1
        i = recenter(v, m) - 1 if m << 1 <= 255 else recenter(254 - v, 254 - m) - 1
        word = (i + 7) // 13 - 1 if (i + 7) % 13 == 0 else i + 20 - (i + 7) // 13
        for test, bits, base in ((16, 4, 0), (32, 4, 16), (64, 5, 32)):
            self.write(int(word >= test), 128)
            if word < test:
                self.literal(word - base, bits)
                return
        word -= 64
        if word < 65:
            self.literal(word, 7)
        else:
            self.literal(65 + (word - 65 >> 1), 7)
            self.literal(word - 65 & 1, 1)

    def finish(self):
        for _ in range(32):
            self.write(0, 128)
        return bytes(self.out)


def committed():
    with open(SOURCE) as f, open(HEADER) as g:
        return f.read(), g.read()


def run_check(text, header):
    said = []
    return vj.check(text, header, vd.DEFAULTS, report=said.append), said


class Committed(unittest.TestCase):
    def test_the_committed_jobs_are_the_ones_worked_out(self):
        text, header = committed()
        status, said = run_check(text, header)
        self.assertEqual(status, 0, "\n".join(said))
        # Every case has at least its first table worked out here, and the two cases that never
        # adapt have every one.
        known = {line.split(":")[0]: int(line.split(", ")[1].split()[0]) for line in said}
        self.assertTrue(all(n >= 1 for n in known.values()), said)
        self.assertEqual(known["vp9_frame_parallel"], 11)
        self.assertEqual(known["vp9_error_resilient"], 20)

    def test_the_cases_are_the_handbacks(self):
        # The vectors, their SHA-256's first 16 digits and the frame counts the handback gives.
        asked = {
            "vp9_two_frames": ("vp90-2-03-deltaq.webm", "c1ef6ca3eea22a53", 2, 2),
            "vp9_hidden_altref": ("vp90-2-02-size-66x66.webm", "749e4260de357b27", 11, 10),
            "vp9_hidden_lf_deltas": ("vp90-2-09-lf_deltas.webm", "651fc818fea9687f", 32, 30),
            "vp9_frame_parallel": ("vp90-2-07-frame_parallel.webm", "b101f5f71920b893", 11, 10),
            "vp9_show_existing": ("vp90-2-10-show-existing-frame.webm", "21d60d305ed05f06", 17, 13),
            "vp9_error_resilient": ("vp90-2-20-big_superframe-01.webm", "08c30e4e653e6f0b", 20, 13),
        }
        text, _ = committed()
        _, frames, cases, _ = vj.parse_c(text)
        for n, fields in enumerate(cases):
            name = fields["name"].strip('"')
            vector, sha, count, images = asked[name]
            self.assertEqual(fields["vector"].strip('"'), vector)
            self.assertTrue(bytes(vj.nums(fields["sha256"])).hex().startswith(sha), name)
            self.assertEqual(len(frames["FRAMES_%d" % n]), count, name)
            self.assertEqual(int(fields["image_count"]), images, name)

    def test_a_changed_setup_byte_is_caught(self):
        text, header = committed()
        # The first setup's stream length, one more than it is.
        at = text.index("static const uint8_t SETUP_0")
        body = text.index("{", text.index("{", at) + 1) + 1
        row = text[body:].split(",")
        row[0x30] = str(int(row[0x30].strip()) ^ 1)
        status, said = run_check(text[:body] + ",".join(row), header)
        self.assertEqual(status, 1)
        self.assertIn("vp9_two_frames frame 0: the setup is not the one worked out", "\n".join(said))

    def test_a_changed_table_byte_is_caught(self):
        text, header = committed()
        # The error resilient case's first table: an inter mode probability, one more.
        at = text.index("static const uint8_t TABLE_4")
        body = text.index("{", at) + 1
        row = text[body:].split(",")
        row[0x400] = "\n" + str(int(row[0x400].strip()) % 255 + 1)
        status, said = run_check(text[:body] + ",".join(row), header)
        self.assertEqual(status, 1)
        self.assertIn("vp9_error_resilient frame 0: the table is not the one worked out",
                      "\n".join(said))

    def test_a_header_too_small_is_caught(self):
        text, header = committed()
        status, said = run_check(text, header.replace("#define VP9_SURFACES_MAX 5u",
                                                      "#define VP9_SURFACES_MAX 4u"))
        self.assertEqual(status, 1)
        self.assertIn("VP9_SURFACES_MAX must hold 5", "\n".join(said))


class Container(unittest.TestCase):
    def ebml(self, eid, body):
        size = len(body)
        head = bytes([0x80 | size]) if size < 127 else bytes([0x40 | size >> 8, size & 0xFF])
        return eid + head + body

    def test_blocks_of_the_vp9_track_in_order(self):
        e = self.ebml
        track = e(b"\xAE", e(b"\xD7", b"\x01") + e(b"\x86", b"V_VP9") +
                  e(b"\xE0", e(b"\xB0", b"\x01\x60") + e(b"\xBA", b"\xF0")))
        other = e(b"\xAE", e(b"\xD7", b"\x02") + e(b"\x86", b"A_OPUS"))
        block = lambda n, payload: e(b"\xA3", bytes([0x80 | n]) + b"\x00\x00\x80" + payload)
        cluster = e(b"\x1F\x43\xB6\x75", block(1, b"one") + block(2, b"audio") +
                    e(b"\xA0", e(b"\xA1", bytes([0x81]) + b"\x00\x01\x00" + b"two")))
        # A Segment of unknown size, as a live stream writes it.
        data = (e(b"\x1A\x45\xDF\xA3", b"") + b"\x18\x53\x80\x67\x01\xFF\xFF\xFF\xFF\xFF\xFF\xFF" +
                e(b"\x16\x54\xAE\x6B", track + other) + cluster)
        blocks, size = vj.Webm(data).vp9()
        self.assertEqual(blocks, [b"one", b"two"])
        self.assertEqual(size, (352, 240))

    def test_laced_blocks_are_refused(self):
        e = self.ebml
        track = e(b"\xAE", e(b"\xD7", b"\x01") + e(b"\x86", b"V_VP9"))
        data = e(b"\x16\x54\xAE\x6B", track) + e(b"\xA3", b"\x81\x00\x00\x82xx")
        with self.assertRaises(ValueError):
            vj.Webm(data)

    def test_superframe_index(self):
        a, b = b"\x82" + b"A" * 4, b"\x82" + b"B" * 299
        # Two frames, two bytes per size: marker 0b110 01 001.
        marker = 0xC0 | 1 << 3 | 1
        index = bytes([marker]) + struct.pack("<HH", len(a), len(b)) + bytes([marker])
        self.assertEqual(vj.split_superframe(a + b + index), [a, b])
        self.assertEqual(vj.split_superframe(a), [a])
        # An index whose first byte is not the marker is no index.
        self.assertEqual(vj.split_superframe(a + b + b"\x00" + index[1:]), [a + b + b"\x00" + index[1:]])
        bad = bytes([marker]) + struct.pack("<HH", len(a), len(b) + 1) + bytes([marker])
        with self.assertRaises(ValueError):
            vj.split_superframe(a + b + bad)


class Probabilities(unittest.TestCase):
    def test_inverse_map_ends(self):
        self.assertEqual(len(vj.INV_MAP), 255)
        self.assertEqual(vj.INV_MAP[:21], [7, 20, 33, 46, 59, 72, 85, 98, 111, 124, 137, 150,
                                           163, 176, 189, 202, 215, 228, 241, 254, 1])
        self.assertEqual(vj.INV_MAP[-3:], [252, 253, 253])

    def test_inv_recenter(self):
        # 6.3.6: above twice m it is v itself; odd below m, even above.
        self.assertEqual(vj.inv_recenter_nonneg(9, 4), 9)
        self.assertEqual(vj.inv_recenter_nonneg(3, 4), 2)
        self.assertEqual(vj.inv_recenter_nonneg(4, 4), 6)

    def test_bool_decoder_reads_what_libvpx_writes(self):
        rng = Rng(0x9E3779B9)
        symbols = [(rng.below(2), 1 + rng.below(255)) for _ in range(3000)]
        w = Writer()
        for bit, p in symbols:
            w.write(bit, p)
        d = vj.BoolDecoder(w.finish())
        self.assertEqual([d.read(p) for _, p in symbols], [b for b, _ in symbols])

    def test_update_prob_reads_what_libvpx_writes(self):
        # Every probability against old ones at the edges and spread between: libvpx's writer
        # codes the change, the script's reader must give the new probability back.
        olds = [1, 2, 3, 64, 127, 128, 129, 200, 253, 254, 255]
        pairs = [(o, n) for o in olds for n in range(1, 256)]
        w = Writer()
        for old, new in pairs:
            w.prob_update(new, old)
        d = vj.BoolDecoder(w.finish())
        self.assertEqual([vj.update_prob(d, old) for old, _ in pairs], [n for _, n in pairs])

    def test_marker_bit(self):
        with self.assertRaises(ValueError):
            vj.BoolDecoder(bytes([0x80, 0, 0]))

    def test_table_layout_at_reden_offsets(self):
        defaults = vj.Defaults(vd.DEFAULTS)
        pr = defaults.copy_ctx()
        seg = {"prob": [1, 2, 3, 4, 5, 6, 7], "pred_prob": [8, 9, 10], "temporal": 1}
        t = vj.prob_table(pr, seg, defaults)
        self.assertEqual(len(t), 0xEA0)
        # reden's EntropyProbs: the segment tree at 0x387, the prediction probabilities at
        # 0x38E, the inter modes at 0x400 in rows of four, intra/inter at 0x41C.
        self.assertEqual(list(t[0x387:0x38E]), [1, 2, 3, 4, 5, 6, 7])
        self.assertEqual(list(t[0x38E:0x391]), [8, 9, 10])
        self.assertEqual(list(t[0x400:0x408]), [pr.get("mv_mode", 0, 0), pr.get("mv_mode", 0, 1),
                                                pr.get("mv_mode", 0, 2), 0, pr.get("mv_mode", 1, 0),
                                                pr.get("mv_mode", 1, 1), pr.get("mv_mode", 1, 2), 0])
        # libvpx's default inter mode probabilities, which reden's default_probs repeats.
        self.assertEqual(list(t[0x400:0x403]), [2, 173, 34])
        self.assertEqual(list(t[0x41C:0x420]), [9, 102, 187, 225])
        # The partition table reden reads is the second of two at 0x4A0, 8x8 first: libvpx's
        # default for an 8x8 block with no context is 199, 122, 141.
        self.assertEqual(list(t[0x4E0:0x4E3]), [199, 122, 141])
        # The coefficients at 0x5A0 in fours, the fourth nought: 4x4, Y, intra, band 0.
        self.assertEqual(list(t[0x5A0:0x5A8]), [195, 29, 183, 0, 84, 49, 136, 0])
        # The keyframe tables, in the hardware's mode order: DC above and left of DC first.
        self.assertEqual(list(t[0:8]), [137, 30, 42, 148, 151, 207, 70, 52])
        self.assertEqual(t[0x320], 91)
        # Nothing left unwritten among the probabilities: every byte of the keyframe tables is a
        # probability, so none is nought.
        self.assertNotIn(0, t[0:0x384])
        # No temporal update: three 255.
        seg["temporal"] = 0
        self.assertEqual(list(vj.prob_table(pr, seg, defaults)[0x38E:0x391]), [255, 255, 255])


class Setup(unittest.TestCase):
    def header(self, **kw):
        h = vj.blank_header()
        h.update(w=352, h=240, keyframe=0, last_keyframe=1, errorres=0, intraonly=0,
                 signbias=[0, 1, 1], level=36, sharpness=2, yac_qi=60, ydc_qdelta=-1,
                 uvdc_qdelta=1, uvac_qdelta=2, txfmmode=4, highprecisionmvs=1, filtermode=4,
                 comppredmode=2, allowcompinter=1, fixcompref=0, varcompref=[1, 2],
                 log2_tile_cols=0, log2_tile_rows=1, lf_enabled=1, lf_ref=[1, 0, -1, -1],
                 lf_mode=[0, 0])
        h.update(kw)
        return h

    def test_fields_at_reden_offsets(self):
        s = vj.setup(self.header(), True, [(352, 240, True), (0, 0, False), (352, 240, True)],
                     384, 1234)
        self.assertEqual(len(s), 0x100)
        # reden's PictureInfo: bitstream_size 0x30, the four sizes from 0x48, the flags at 0x68.
        self.assertEqual(struct.unpack_from("<I", s, 0x30)[0], 1234)
        self.assertEqual(struct.unpack_from("<4H", s, 0x48), (352, 240, 384, 384))
        self.assertEqual(struct.unpack_from("<4H", s, 0x50), (0, 0, 0, 0))
        self.assertEqual(struct.unpack_from("<4H", s, 0x60), (352, 240, 384, 384))
        # LastFrameIsKeyFrame bit 1, LastShowFrame bit 4.
        self.assertEqual(s[0x68], 0b10010)
        self.assertEqual(list(s[0x6C:0x70]), [0, 0, 1, 1])
        self.assertEqual(struct.unpack_from("<BbBbbb", s, 0x70), (36, 2, 60, -1, 2, 1))
        # tx_mode 0x77, interp_filter 0x79, reference_mode 0x7A, the tiles 0x7E.
        self.assertEqual(s[0x77], 4)
        self.assertEqual(s[0x79], 4)
        self.assertEqual(s[0x7A], 2)
        self.assertEqual(list(s[0x7B:0x7E]), [1, 2, 3])
        self.assertEqual(list(s[0x7E:0x80]), [0, 1])
        # The loop filter deltas at 0xE4.
        self.assertEqual(s[0xE4], 1)
        self.assertEqual(struct.unpack_from("<4b2b", s, 0xE5), (1, 0, -1, -1, 0, 0))
        # The bsd control offset: the height to 64, times 912, in 256-byte units.
        self.assertEqual(struct.unpack_from("<I", s, 0x44)[0], 912)

    def test_keyframe_has_no_high_precision(self):
        s = vj.setup(self.header(keyframe=1), False, [(0, 0, False)] * 3, 384, 1)
        self.assertEqual(s[0x78], 0)
        self.assertEqual(s[0x68] & 1, 1)

    def test_tile_sizes(self):
        h = self.header(sb_cols=27, sb_rows=17, tile_cols=4, tile_rows=2, log2_tile_cols=2,
                        log2_tile_rows=1)
        # Columns of 27 superblocks in four: 6, 7, 7, 7; rows of 17 in two: 8, 9.
        self.assertEqual(vj.tile_sizes(h), [6, 8, 7, 8, 7, 8, 7, 8, 6, 9, 7, 9, 7, 9, 7, 9])

    def test_surface_layout(self):
        self.assertEqual(vj.surface_layout(352, 240), (384, 256, 384 * 256))
        self.assertEqual(vj.surface_layout(66, 66), (128, 96, 128 * 96))
        self.assertEqual(vj.surface_layout(352, 288), (384, 288, 384 * 288))


class Surfaces(unittest.TestCase):
    def test_slots_keep_their_surfaces(self):
        jobs = [
            {"existing": None, "refidx": [0, 1, 2], "refreshrefmask": 0xFF},  # keyframe
            {"existing": None, "refidx": [0, 1, 2], "refreshrefmask": 0x04},  # hidden altref
            {"existing": None, "refidx": [0, 1, 2], "refreshrefmask": 0x01},
            {"existing": None, "refidx": [0, 1, 2], "refreshrefmask": 0x00},  # kept by none
            {"existing": 2},                                                  # the altref again
            {"existing": None, "refidx": [0, 1, 2], "refreshrefmask": 0x00},
        ]
        self.assertEqual(vj.assign_surfaces(jobs), 5)
        self.assertEqual([j["surface"] for j in jobs], [0, 1, 2, 3, 1, 4])
        self.assertEqual(jobs[0]["refs"], [0, 0, 0])
        self.assertEqual(jobs[2]["refs"], [0, 0, 1])
        self.assertEqual(jobs[3]["refs"], [2, 0, 1])
        # The surface the frame before wrote is not taken, so it can be read again.
        self.assertEqual(jobs[5]["refs"], [2, 0, 1])

    def test_an_empty_slot_reads_the_frames_own_surface(self):
        jobs = [{"existing": None, "refidx": [3, 4, 5], "refreshrefmask": 0x01}]
        vj.assign_surfaces(jobs)
        self.assertEqual(jobs[0]["refs"], [0, 0, 0])


class Rewrite(unittest.TestCase):
    def test_hidden_frame_shown_without_its_intra_only_bit(self):
        # marker 10, profile 00, show_existing 0, frame_type 1, show_frame 0, error_res 0,
        # intra_only 0, then eleven bits 1..1 and a byte of body.
        bits = "10" + "00" + "0" + "1" + "0" + "0" + "0" + "11111111111" + "0000"
        frame = bytes(int(bits[i:i + 8], 2) for i in range(0, 24, 8)) + b"\xAB"
        where = {"show_frame": 6, "intra_only": 8, "end_bits": 20}
        out = vj.rewrite_shown(frame, where)
        want = "10" + "00" + "0" + "1" + "1" + "0" + "11111111111"
        want += "0" * (-len(want) % 8)
        self.assertEqual(out[:3], bytes(int(want[i:i + 8], 2) for i in range(0, 24, 8)))
        self.assertEqual(out[3:], b"\xAB")

    def test_intra_only_cannot_be_shown(self):
        frame = bytes([0b10000100, 0b10000000, 0])
        with self.assertRaises(ValueError):
            vj.rewrite_shown(frame, {"show_frame": 6, "intra_only": 8, "end_bits": 9})


if __name__ == "__main__":
    unittest.main()
