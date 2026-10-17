#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes source/vp9jobs.c: the VP9 jobs the video family's nvdec suite hands the decode engine,
one per frame of six libvpx test vectors, and what each image must hash to.

A job is what the engine reads, not the stream: a picture setup of 0x100 bytes (NVIDIA's
nvdec_vp9_pic_s, which reden's PictureInfo describes), a probability table of 0xEA0 bytes
(nvdec_vp9EntropyProbs_t, reden's EntropyProbs) holding the probabilities the frame decodes with,
which is the saved context it starts from with its compressed header's updates applied, and the
frame's body, everything after the two headers. The table can only come from a decoder, because
a context adapts to what the frame before it decoded, so `generate` runs FFmpeg's VP9 decoder
(tools/vp9-ref.c, built from source by `build-ref`) one frame at a time and reads its state; the
program carries the jobs ready, never the decoder.

Everything the script works out by itself is checked against that decoder as it goes: the
superframes split here, every header field parsed here (FFmpeg's fields, stale ones included, so
the setup is the one the homebrew backend builds from them), and the compressed header's updates
applied here to the context the frame starts from, which must give the decoder's table wherever
the frame can use it. The libvpx MD5 list of each vector must match FFmpeg's images.

The setup and the table follow the homebrew FFmpeg backend for the console (nvtegra, ffmpeg-devel,
May 2024, nvtegra_vp9.c): field by field, the prediction mode order, the partition table's order,
the keyframe tables. Where it reads its own decoder's state, so does this. One pair of bytes
differs on purpose, the chroma quantizer deltas (see setup).

    tools/vp9-jobs.py build-ref TARBALL DIR   FFmpeg 9.0.2 from its tarball, and the helper
    tools/vp9-jobs.py measure REF VECTORS     the handback's table: hidden frames and visibility
    tools/vp9-jobs.py generate REF VECTORS    writes source/vp9jobs.c (--out to put it elsewhere)
    tools/vp9-jobs.py check                   what can be checked with neither: the committed file
"""

import argparse
import functools
import hashlib
import importlib.util
import json
import os
import re
import struct
import subprocess
import sys
import tarfile
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "source", "vp9jobs.c")

FFMPEG = "ffmpeg-9.0.2"
FFMPEG_SHA256 = "8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"
# Only the decoder, its parser and the split; the struct layouts the helper reads come from this
# configuration's headers, so the helper is compiled against this very tree.
FFMPEG_CONFIGURE = [
    "--disable-everything", "--disable-programs", "--disable-doc", "--disable-network",
    "--disable-autodetect", "--enable-static", "--disable-shared", "--enable-decoder=vp9",
    "--enable-parser=vp9", "--enable-bsf=vp9_superframe_split", "--enable-protocol=file",
    "--disable-debug", "--extra-cflags=-g",
]

# name, vector, its SHA-256, what the case is about. Downloaded from
# https://storage.googleapis.com/downloads.webmproject.org/test_data/libvpx/<vector>(.md5).
CASES = [
    ("vp9_two_frames", "vp90-2-03-deltaq.webm",
     "c1ef6ca3eea22a53a79c7f82bdf295f2bf4d5c6dce7bb9659e0ab0e14bd6a72b",
     "two frames, none hidden"),
    ("vp9_hidden_altref", "vp90-2-02-size-66x66.webm",
     "749e4260de357b2704117389cd0a620f73f8229043d214528d1cc7d81524faaa",
     "a hidden frame, contexts that adapt, 66 by 66"),
    ("vp9_frame_parallel", "vp90-2-07-frame_parallel.webm",
     "b101f5f71920b893ff301bd506d43d62f2f678ab9196ffc06fe605f1fa4caf4f",
     "a hidden frame, no adaptation"),
    ("vp9_hidden_lf_deltas", "vp90-2-09-lf_deltas.webm",
     "651fc818fea9687fb1acccac35433c555cf005d2a78d39d5168d40760fd23fa1",
     "two hidden frames, loop filter deltas"),
    ("vp9_error_resilient", "vp90-2-20-big_superframe-01.webm",
     "08c30e4e653e6f0bbcb58f0d506e843a45d9630060bb9568b6a04b3a8c61bdfa",
     "seven hidden frames in one superframe, error resilient"),
    ("vp9_show_existing", "vp90-2-10-show-existing-frame.webm",
     "21d60d305ed05f06d838f90f0e62bbd2a37c4b994304ad7f8556430cc551aa1b",
     "frames shown again, LastShowFrame both ways"),
]

SETUP_SIZE = 0x100
PROBS_SIZE = 0xEA0

# ---- WebM: just enough Matroska to take the blocks of the video track out


class Webm:
    MASTERS = {0x18538067, 0x1F43B675, 0xA0, 0x1654AE6B, 0xAE, 0xE0}

    def __init__(self, data):
        self.data = data
        self.tracks = {}
        self.blocks = []
        self.walk(0, len(data), None)

    def vint(self, pos, keep_marker):
        first = self.data[pos]
        length = 1
        while length <= 8 and not first & (0x100 >> length):
            length += 1
        if length > 8:
            raise ValueError("bad EBML length at %d" % pos)
        value = first if keep_marker else first & ((0x100 >> length) - 1)
        for i in range(1, length):
            value = value << 8 | self.data[pos + i]
        unknown = not keep_marker and value == (1 << 7 * length) - 1
        return value, pos + length, unknown

    def walk(self, pos, end, track):
        while pos < end:
            eid, p, _ = self.vint(pos, True)
            size, p, unknown = self.vint(p, False)
            stop = end if unknown else p + size
            body = self.data[p:stop]
            if eid == 0xAE:
                entry = {}
                self.walk(p, stop, entry)
                self.tracks[entry.get("number")] = entry
            elif eid in self.MASTERS:
                self.walk(p, stop, track)
            elif eid == 0xD7 and track is not None:
                track["number"] = int.from_bytes(body, "big")
            elif eid == 0x86 and track is not None:
                track["codec"] = body.decode("ascii")
            elif eid == 0xB0 and track is not None:
                track["width"] = int.from_bytes(body, "big")
            elif eid == 0xBA and track is not None:
                track["height"] = int.from_bytes(body, "big")
            elif eid in (0xA3, 0xA1):
                number, q, _ = self.vint(p, False)
                flags = self.data[q + 2]
                if flags & 0x06:
                    raise ValueError("laced blocks are not expected in a VP9 track")
                self.blocks.append((number, self.data[q + 3:stop]))
            pos = stop

    def vp9(self):
        """The VP9 track's blocks, in file order, and its size."""
        ids = [n for n, t in self.tracks.items() if t.get("codec") == "V_VP9"]
        if len(ids) != 1:
            raise ValueError("expected one VP9 track, found %d" % len(ids))
        t = self.tracks[ids[0]]
        return [b for n, b in self.blocks if n == ids[0]], (t["width"], t["height"])


def split_superframe(chunk):
    """The frames of one block: a superframe's index (VP9 Annex B) at its end lists their sizes,
    and a block without one is a single frame."""
    marker = chunk[-1]
    if marker & 0xE0 == 0xC0:
        width = 1 + (marker >> 3 & 3)
        count = 1 + (marker & 7)
        index = 2 + width * count
        if len(chunk) >= index and chunk[-index] == marker:
            sizes = [int.from_bytes(chunk[len(chunk) - index + 1 + i * width:][:width], "little")
                     for i in range(count)]
            if sum(sizes) > len(chunk) - index or 0 in sizes:
                raise ValueError("a superframe index that does not add up")
            frames, pos = [], 0
            for size in sizes:
                frames.append(chunk[pos:pos + size])
                pos += size
            return frames
    return [chunk]


# ---- the uncompressed header, field for field as FFmpeg's decode_frame_header keeps it


class Bits:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def f(self, n):
        v = 0
        for _ in range(n):
            byte = self.data[self.pos >> 3]
            v = v << 1 | (byte >> (7 - (self.pos & 7)) & 1)
            self.pos += 1
        return v

    def sinv(self, n):
        v = self.f(n)
        return -v if self.f(1) else v


SYNC = 0x498342
FILTER_SWITCHABLE = 4
TX_SWITCHABLE = 4
PRED_SINGLEREF, PRED_COMPREF, PRED_SWITCHABLE = 0, 1, 2


def blank_header():
    """FFmpeg's header state before the first frame: a zeroed context."""
    return {
        "keyframe": 0, "invisible": 0, "errorres": 0, "intraonly": 0, "resetctx": 0,
        "refreshrefmask": 0, "highprecisionmvs": 0, "filtermode": 0, "allowcompinter": 0,
        "refreshctx": 0, "parallelmode": 0, "framectxid": 0, "use_last_frame_mvs": 0,
        "refidx": [0, 0, 0], "signbias": [0, 0, 0], "fixcompref": 0, "varcompref": [0, 0],
        "level": 0, "sharpness": 0, "lf_enabled": 0, "lf_updated": 0, "lf_mode": [0, 0],
        "lf_ref": [0, 0, 0, 0], "yac_qi": 0, "ydc_qdelta": 0, "uvdc_qdelta": 0,
        "uvac_qdelta": 0, "lossless": 0, "txfmmode": 0, "comppredmode": 0,
        "log2_tile_cols": 0, "log2_tile_rows": 0, "tile_cols": 0, "tile_rows": 0,
        "uncompressed_header_size": 0, "compressed_header_size": 0,
        "seg": {"enabled": 0, "temporal": 0, "absolute_vals": 0, "update_map": 0,
                "prob": [0] * 7, "pred_prob": [0] * 3, "feat": [[0] * 7 for _ in range(8)]},
        "last_keyframe": 0, "w": 0, "h": 0, "sb_cols": 0, "sb_rows": 0,
    }


def is_show_existing(frame):
    profile = (frame[0] >> 5 & 1) | (frame[0] >> 4 & 1) << 1
    return frame[0] >> (2 if profile == 3 else 3) & 1


class Stream:
    """What a decoder keeps between frames that the setup and the header parse need: the
    header fields, kept stale where FFmpeg keeps them, which slot holds a picture of which size,
    and the four saved probability contexts (None where only a decoder knows one)."""

    def __init__(self, defaults):
        self.h = blank_header()
        self.slots = [None] * 8  # (width, height) of what each of the eight slots holds
        self.last = None  # the size of the last frame decoded
        self.defaults = defaults
        self.ctx = None  # four (p, coef) pairs, set by the first keyframe

    def parse(self, frame):
        """Parses one frame's uncompressed header into self.h, resets the saved contexts where
        the frame does, and returns the bit offsets of show_frame, intra_only and the header's
        end, and the context index as coded."""
        h = self.h
        b = Bits(frame)
        if b.f(2) != 2:
            raise ValueError("bad frame marker")
        profile = b.f(1) | b.f(1) << 1
        if profile != 0:
            raise ValueError("only profile 0 is expected")
        if b.f(1):
            raise ValueError("a show_existing_frame has no header to parse")
        h["last_keyframe"] = h["keyframe"]
        h["keyframe"] = 1 - b.f(1)
        last_invisible = h["invisible"]
        where = {"show_frame": b.pos}
        h["invisible"] = 1 - b.f(1)
        h["errorres"] = b.f(1)
        h["use_last_frame_mvs"] = int(not h["errorres"] and not last_invisible)
        if h["keyframe"]:
            if b.f(24) != SYNC:
                raise ValueError("bad sync code")
            self.colour(b)
            h["refreshrefmask"] = 0xFF
            w, hh = b.f(16) + 1, b.f(16) + 1
            if b.f(1):
                b.f(32)
        else:
            where["intra_only"] = b.pos if h["invisible"] else None
            h["intraonly"] = b.f(1) if h["invisible"] else 0
            h["resetctx"] = 0 if h["errorres"] else b.f(2)
            if h["intraonly"]:
                if b.f(24) != SYNC:
                    raise ValueError("bad sync code")
                h["refreshrefmask"] = b.f(8)
                w, hh = b.f(16) + 1, b.f(16) + 1
                if b.f(1):
                    b.f(32)
            else:
                h["refreshrefmask"] = b.f(8)
                for i in range(3):
                    h["refidx"][i] = b.f(3)
                    h["signbias"][i] = int(b.f(1) and not h["errorres"])
                for i in range(3):
                    if self.slots[h["refidx"][i]] is None:
                        raise ValueError("a reference that is not there")
                for i in range(3):
                    if b.f(1):
                        w, hh = self.slots[h["refidx"][i]]
                        break
                else:
                    w, hh = b.f(16) + 1, b.f(16) + 1
                h["use_last_frame_mvs"] &= int(self.last == (w, hh))
                if b.f(1):
                    b.f(32)
                h["highprecisionmvs"] = b.f(1)
                h["filtermode"] = FILTER_SWITCHABLE if b.f(1) else b.f(2)
                sb = h["signbias"]
                h["allowcompinter"] = int(sb[0] != sb[1] or sb[0] != sb[2])
                if h["allowcompinter"]:
                    if sb[0] == sb[1]:
                        h["fixcompref"], h["varcompref"] = 2, [0, 1]
                    elif sb[0] == sb[2]:
                        h["fixcompref"], h["varcompref"] = 1, [0, 2]
                    else:
                        h["fixcompref"], h["varcompref"] = 0, [1, 2]
        h["refreshctx"] = 0 if h["errorres"] else b.f(1)
        h["parallelmode"] = 1 if h["errorres"] else b.f(1)
        coded_ctx = b.f(2)
        h["framectxid"] = 0 if h["keyframe"] or h["intraonly"] else coded_ctx
        if h["keyframe"] or h["errorres"] or h["intraonly"]:
            h["lf_ref"] = [1, 0, -1, -1]
            h["lf_mode"] = [0, 0]
            h["seg"]["feat"] = [[0] * 7 for _ in range(8)]
        h["level"] = b.f(6)
        h["sharpness"] = b.f(3)
        h["lf_enabled"] = b.f(1)
        if h["lf_enabled"]:
            h["lf_updated"] = b.f(1)
            if h["lf_updated"]:
                for i in range(4):
                    if b.f(1):
                        h["lf_ref"][i] = b.sinv(6)
                for i in range(2):
                    if b.f(1):
                        h["lf_mode"][i] = b.sinv(6)
        h["yac_qi"] = b.f(8)
        h["ydc_qdelta"] = b.sinv(4) if b.f(1) else 0
        h["uvdc_qdelta"] = b.sinv(4) if b.f(1) else 0
        h["uvac_qdelta"] = b.sinv(4) if b.f(1) else 0
        h["lossless"] = int(h["yac_qi"] == 0 and h["ydc_qdelta"] == 0 and
                            h["uvdc_qdelta"] == 0 and h["uvac_qdelta"] == 0)
        seg = h["seg"]
        seg["enabled"] = b.f(1)
        if seg["enabled"]:
            seg["update_map"] = b.f(1)
            if seg["update_map"]:
                seg["prob"] = [b.f(8) if b.f(1) else 255 for _ in range(7)]
                seg["temporal"] = b.f(1)
                if seg["temporal"]:
                    seg["pred_prob"] = [b.f(8) if b.f(1) else 255 for _ in range(3)]
            if b.f(1):
                seg["absolute_vals"] = b.f(1)
                for f in seg["feat"]:
                    # q, lf, ref enabled, skip enabled, then q, lf and ref's values
                    f[0] = b.f(1)
                    if f[0]:
                        f[4] = b.sinv(8)
                    f[1] = b.f(1)
                    if f[1]:
                        f[5] = b.sinv(6)
                    f[2] = b.f(1)
                    if f[2]:
                        f[6] = b.f(2)
                    f[3] = b.f(1)
        else:
            seg["temporal"] = 0
            seg["update_map"] = 0
        h["w"], h["h"] = w, hh
        h["sb_cols"], h["sb_rows"] = (w + 63) >> 6, (hh + 63) >> 6
        log2 = 0
        while h["sb_cols"] > 64 << log2:
            log2 += 1
        top = 0
        while h["sb_cols"] >> top >= 4:
            top += 1
        top = max(0, top - 1)
        while top > log2:
            if b.f(1):
                log2 += 1
            else:
                break
        h["log2_tile_cols"] = log2
        h["log2_tile_rows"] = (b.f(1) + 1) if b.f(1) else 0
        h["tile_cols"] = 1 << log2
        h["tile_rows"] = 1 << h["log2_tile_rows"]
        if self.ctx is None and not h["keyframe"]:
            raise ValueError("a stream that does not start with a keyframe")
        if h["keyframe"] or h["errorres"] or (h["intraonly"] and h["resetctx"] == 3):
            self.ctx = [self.defaults.copy_ctx() for _ in range(4)]
        elif h["intraonly"] and h["resetctx"] == 2:
            self.ctx[coded_ctx] = self.defaults.copy_ctx()
        h["compressed_header_size"] = b.f(16)
        h["uncompressed_header_size"] = (b.pos + 7) // 8
        where["coded_ctx"] = coded_ctx
        where["end_bits"] = b.pos
        return where

    @staticmethod
    def colour(b):
        if b.f(3) == 7:
            raise ValueError("RGB is not profile 0")
        b.f(1)

    def decoded(self, frame_size):
        """After a frame was decoded: the slots it refreshed hold it."""
        for i in range(8):
            if self.h["refreshrefmask"] >> i & 1:
                self.slots[i] = frame_size
        self.last = frame_size


# ---- the probabilities, in FFmpeg's ProbContext layout


PROB_FIELDS = [
    ("y_mode", (4, 9)), ("uv_mode", (10, 9)), ("filter", (4, 2)), ("mv_mode", (7, 3)),
    ("intra", (4,)), ("comp", (5,)), ("single_ref", (5, 2)), ("comp_ref", (5,)),
    ("tx32p", (2, 3)), ("tx16p", (2, 2)), ("tx8p", (2,)), ("skip", (3,)), ("mv_joint", (3,)),
]
MV_COMP_FIELDS = [
    ("sign", (1,)), ("classes", (10,)), ("class0", (1,)), ("bits", (10,)),
    ("class0_fp", (2, 3)), ("fp", (3,)), ("class0_hp", (1,)), ("hp", (1,)),
]


def _count(shape):
    n = 1
    for d in shape:
        n *= d
    return n


def _layout():
    at, offsets = 0, {}
    for name, shape in PROB_FIELDS:
        offsets[name] = (at, shape)
        at += _count(shape)
    for c in range(2):
        for name, shape in MV_COMP_FIELDS:
            offsets["mv_comp%d_%s" % (c, name)] = (at, shape)
            at += _count(shape)
    offsets["partition"] = (at, (4, 4, 3))
    at += 48
    return offsets, at


PROB_OFFSETS, PROB_BYTES = _layout()
COEF_BYTES = 4 * 2 * 2 * 6 * 6 * 3


class Probs:
    """One probability state: FFmpeg's ProbContext bytes and the three model probabilities of
    each coefficient context."""

    def __init__(self, p, coef):
        assert len(p) == PROB_BYTES and len(coef) == COEF_BYTES
        self.p = bytearray(p)
        self.coef = bytearray(coef)

    def copy_ctx(self):
        return Probs(self.p, self.coef)

    def at(self, name, *index):
        base, shape = PROB_OFFSETS[name]
        flat = 0
        for i, d in zip(index, shape):
            flat = flat * d + i
        for d in shape[len(index):]:
            flat *= d
        return base + flat

    def get(self, name, *index):
        return self.p[self.at(name, *index)]

    def set(self, name, value, *index):
        self.p[self.at(name, *index)] = value

    @staticmethod
    def coef_at(tx, i, j, k, l, m):
        return ((((tx * 2 + i) * 2 + j) * 6 + k) * 6 + l) * 3 + m


class Defaults:
    """FFmpeg's default tables, as the helper printed them."""

    def __init__(self, record):
        self.p = bytes.fromhex(record["p"])
        self.coef = bytes.fromhex(record["coef"])
        self.kf_y = bytes.fromhex(record["kf_y"])
        self.kf_uv = bytes.fromhex(record["kf_uv"])
        self.kf_partition = bytes.fromhex(record["kf_partition"])

    def copy_ctx(self):
        return Probs(self.p, self.coef)


# ---- the compressed header, read here with the spec's boolean decoder (9.2) and update rules


INV_MAP = [7 + 13 * i for i in range(20)]
INV_MAP += [v for v in range(1, 254) if (v - 7) % 13 != 0 or v < 7] + [253]


class BoolDecoder:
    def __init__(self, data):
        self.bits = Bits(data)
        self.max_bits = 8 * len(data) - 8
        self.value = self.bits.f(8)
        self.range = 255
        if self.read(128):
            raise ValueError("the compressed header's marker bit is set")

    def read(self, p):
        split = 1 + ((self.range - 1) * p >> 8)
        if self.value < split:
            self.range = split
            bit = 0
        else:
            self.range -= split
            self.value -= split
            bit = 1
        while self.range < 128:
            new = 0
            if self.max_bits > 0:
                new = self.bits.f(1)
                self.max_bits -= 1
            self.value = self.value << 1 | new
            self.range <<= 1
        return bit

    def literal(self, n):
        v = 0
        for _ in range(n):
            v = v << 1 | self.read(128)
        return v


def inv_recenter_nonneg(v, m):
    if v > 2 * m:
        return v
    if v & 1:
        return m - ((v + 1) >> 1)
    return m + (v >> 1)


def update_prob(d, prob):
    """diff_update_prob (6.3.5): an update coded against the probability it replaces."""
    if not d.read(252):
        return prob
    if not d.literal(1):
        delta = d.literal(4)
    elif not d.literal(1):
        delta = d.literal(4) + 16
    elif not d.literal(1):
        delta = d.literal(5) + 32
    else:
        v = d.literal(7)
        delta = v + 64 if v < 65 else (v << 1) - 1 + d.literal(1)
    v = INV_MAP[delta]
    m = prob - 1
    if m << 1 <= 255:
        return 1 + inv_recenter_nonneg(v, m)
    return 255 - inv_recenter_nonneg(v, 255 - 1 - m)


def update_mv_prob(d, prob):
    return (d.literal(7) << 1 | 1) if d.read(252) else prob


def compressed_header(h, start, frame):
    """The probabilities the frame decodes with, worked out here: the saved context it starts
    from with every update of its compressed header applied. Returns the state (None when the
    context is not known), the transform mode and reference mode the header names, and the
    largest transform size the frame can use."""
    pr = start.copy_ctx() if start is not None else Probs(bytes(PROB_BYTES), bytes(COEF_BYTES))
    d = BoolDecoder(frame[h["uncompressed_header_size"]:][:h["compressed_header_size"]])
    if h["lossless"]:
        tx_mode = 0
    else:
        tx_mode = d.literal(2)
        if tx_mode == 3:
            tx_mode += d.literal(1)
        if tx_mode == TX_SWITCHABLE:
            for i in range(2):
                pr.set("tx8p", update_prob(d, pr.get("tx8p", i)), i)
            for i in range(2):
                for j in range(2):
                    pr.set("tx16p", update_prob(d, pr.get("tx16p", i, j)), i, j)
            for i in range(2):
                for j in range(3):
                    pr.set("tx32p", update_prob(d, pr.get("tx32p", i, j)), i, j)
    biggest = min(tx_mode, 3)
    for tx in range(biggest + 1):
        if d.literal(1):
            for i in range(2):
                for j in range(2):
                    for k in range(6):
                        for l in range(3 if k == 0 else 6):
                            for m in range(3):
                                a = Probs.coef_at(tx, i, j, k, l, m)
                                pr.coef[a] = update_prob(d, pr.coef[a])
    for i in range(3):
        pr.set("skip", update_prob(d, pr.get("skip", i)), i)
    ref_mode = h["comppredmode"]
    if not h["keyframe"] and not h["intraonly"]:
        for i in range(7):
            for j in range(3):
                pr.set("mv_mode", update_prob(d, pr.get("mv_mode", i, j)), i, j)
        if h["filtermode"] == FILTER_SWITCHABLE:
            for i in range(4):
                for j in range(2):
                    pr.set("filter", update_prob(d, pr.get("filter", i, j)), i, j)
        for i in range(4):
            pr.set("intra", update_prob(d, pr.get("intra", i)), i)
        if h["allowcompinter"]:
            ref_mode = d.literal(1)
            if ref_mode:
                ref_mode += d.literal(1)
            if ref_mode == PRED_SWITCHABLE:
                for i in range(5):
                    pr.set("comp", update_prob(d, pr.get("comp", i)), i)
        else:
            ref_mode = PRED_SINGLEREF
        if ref_mode != PRED_COMPREF:
            for i in range(5):
                for j in range(2):
                    pr.set("single_ref", update_prob(d, pr.get("single_ref", i, j)), i, j)
        if ref_mode != PRED_SINGLEREF:
            for i in range(5):
                pr.set("comp_ref", update_prob(d, pr.get("comp_ref", i)), i)
        for i in range(4):
            for j in range(9):
                pr.set("y_mode", update_prob(d, pr.get("y_mode", i, j)), i, j)
        # The spec's partition contexts run from 8x8 up; FFmpeg's levels from 64x64 down.
        for i in range(4):
            for j in range(4):
                for k in range(3):
                    pr.set("partition", update_prob(d, pr.get("partition", 3 - i, j, k)),
                           3 - i, j, k)
        for i in range(3):
            pr.set("mv_joint", update_mv_prob(d, pr.get("mv_joint", i)), i)
        for c in range(2):
            n = "mv_comp%d_" % c
            pr.set(n + "sign", update_mv_prob(d, pr.get(n + "sign", 0)), 0)
            for j in range(10):
                pr.set(n + "classes", update_mv_prob(d, pr.get(n + "classes", j)), j)
            pr.set(n + "class0", update_mv_prob(d, pr.get(n + "class0", 0)), 0)
            for j in range(10):
                pr.set(n + "bits", update_mv_prob(d, pr.get(n + "bits", j)), j)
        for c in range(2):
            n = "mv_comp%d_" % c
            for j in range(2):
                for k in range(3):
                    pr.set(n + "class0_fp", update_mv_prob(d, pr.get(n + "class0_fp", j, k)), j, k)
            for j in range(3):
                pr.set(n + "fp", update_mv_prob(d, pr.get(n + "fp", j)), j)
        if h["highprecisionmvs"]:
            for c in range(2):
                n = "mv_comp%d_" % c
                pr.set(n + "class0_hp", update_mv_prob(d, pr.get(n + "class0_hp", 0)), 0)
                pr.set(n + "hp", update_mv_prob(d, pr.get(n + "hp", 0)), 0)
    return (pr if start is not None else None), tx_mode, ref_mode, biggest


@functools.lru_cache(maxsize=None)
def usable_coef(tx_biggest):
    """The coefficient bytes a frame can read: its transform sizes, and three contexts in band
    0. FFmpeg leaves the rest of its table as an earlier frame had it, and so does the job."""
    keep = set()
    for tx in range(tx_biggest + 1):
        for i in range(2):
            for j in range(2):
                for k in range(6):
                    for l in range(3 if k == 0 else 6):
                        for m in range(3):
                            keep.add(Probs.coef_at(tx, i, j, k, l, m))
    return frozenset(keep)


# ---- what the engine reads: the probability table and the setup

# The prediction modes, in the hardware's order, as FFmpeg numbers them.
PMCONV = [2, 0, 1, 3, 4, 5, 6, 8, 7, 9]

# Offsets in nvdec_vp9EntropyProbs_t; reden's EntropyProbs agrees with every one it names.
T = {
    "kf_bmode_prob": 0x000, "kf_bmode_probB": 0x320, "ref_pred_probs": 0x384,
    "mb_segment_tree_probs": 0x387, "segment_pred_probs": 0x38E, "ref_scores": 0x391,
    "prob_comppred": 0x395, "kf_uv_mode_prob": 0x3A0, "kf_uv_mode_probB": 0x3F0,
    "inter_mode_prob": 0x400, "intra_inter_prob": 0x41C, "uv_mode_prob": 0x420,
    "tx8x8_prob": 0x470, "tx16x16_prob": 0x472, "tx32x32_prob": 0x476,
    "sb_ymode_probB": 0x47C, "sb_ymode_prob": 0x480, "partition_prob": 0x4A0,
    "uv_mode_probB": 0x520, "switchable_interp_prob": 0x52A, "comp_inter_prob": 0x532,
    "mbskip_probs": 0x537, "joints": 0x53B, "sign": 0x53E, "class0": 0x540, "fp": 0x542,
    "class0_hp": 0x548, "hp": 0x54A, "classes": 0x54C, "class0_fp": 0x560, "bits": 0x56C,
    "single_ref_prob": 0x580, "comp_ref_prob": 0x58A, "coef": 0x5A0,
}


def prob_table(pr, seg, defaults):
    """nvdec_vp9EntropyProbs_t for one frame, filled as nvtegra_vp9_update_probs fills it. Its
    ref_pred_probs come from a line buffer of the decoder there, which is no probability and
    which neither the official driver nor reden reads, so they are nought here."""
    t = bytearray(PROBS_SIZE)
    for i in range(10):
        for j in range(10):
            src = (PMCONV[i] * 10 + PMCONV[j]) * 9
            at = T["kf_bmode_prob"] + (i * 10 + j) * 8
            t[at:at + 8] = defaults.kf_y[src:src + 8]
            t[T["kf_bmode_probB"] + i * 10 + j] = defaults.kf_y[src + 8]
        src = PMCONV[i] * 9
        t[T["kf_uv_mode_prob"] + i * 8:T["kf_uv_mode_prob"] + i * 8 + 8] = defaults.kf_uv[src:src + 8]
        t[T["kf_uv_mode_probB"] + i] = defaults.kf_uv[src + 8]
    t[T["mb_segment_tree_probs"]:T["mb_segment_tree_probs"] + 7] = bytes(seg["prob"])
    t[T["segment_pred_probs"]:T["segment_pred_probs"] + 3] = (bytes(seg["pred_prob"]) if seg["temporal"]
                                                         else b"\xff" * 3)
    for i in range(7):
        for j in range(3):
            t[T["inter_mode_prob"] + i * 4 + j] = pr.get("mv_mode", i, j)
    for i in range(4):
        t[T["intra_inter_prob"] + i] = pr.get("intra", i)
    for i in range(10):
        for j in range(8):
            t[T["uv_mode_prob"] + i * 8 + j] = pr.get("uv_mode", PMCONV[i], j)
        t[T["uv_mode_probB"] + i] = pr.get("uv_mode", PMCONV[i], 8)
    for i in range(2):
        t[T["tx8x8_prob"] + i] = pr.get("tx8p", i)
        for j in range(2):
            t[T["tx16x16_prob"] + i * 2 + j] = pr.get("tx16p", i, j)
        for j in range(3):
            t[T["tx32x32_prob"] + i * 3 + j] = pr.get("tx32p", i, j)
    for i in range(4):
        for j in range(8):
            t[T["sb_ymode_prob"] + i * 8 + j] = pr.get("y_mode", i, j)
        t[T["sb_ymode_probB"] + i] = pr.get("y_mode", i, 8)
    for i in range(4):
        for j in range(4):
            for k in range(3):
                t[T["partition_prob"] + (4 * (3 - i) + j) * 4 + k] = \
                    defaults.kf_partition[(i * 4 + j) * 3 + k]
                t[T["partition_prob"] + 64 + (4 * (3 - i) + j) * 4 + k] = \
                    pr.get("partition", i, j, k)
    for i in range(4):
        for j in range(2):
            t[T["switchable_interp_prob"] + i * 2 + j] = pr.get("filter", i, j)
    for i in range(5):
        t[T["comp_inter_prob"] + i] = pr.get("comp", i)
        t[T["comp_ref_prob"] + i] = pr.get("comp_ref", i)
        for j in range(2):
            t[T["single_ref_prob"] + i * 2 + j] = pr.get("single_ref", i, j)
    for i in range(3):
        t[T["mbskip_probs"] + i] = pr.get("skip", i)
        t[T["joints"] + i] = pr.get("mv_joint", i)
    for c in range(2):
        n = "mv_comp%d_" % c
        t[T["sign"] + c] = pr.get(n + "sign", 0)
        t[T["class0"] + c] = pr.get(n + "class0", 0)
        t[T["class0_hp"] + c] = pr.get(n + "class0_hp", 0)
        t[T["hp"] + c] = pr.get(n + "hp", 0)
        for j in range(3):
            t[T["fp"] + c * 3 + j] = pr.get(n + "fp", j)
        for j in range(10):
            t[T["classes"] + c * 10 + j] = pr.get(n + "classes", j)
            t[T["bits"] + c * 10 + j] = pr.get(n + "bits", j)
        for j in range(2):
            for k in range(3):
                t[T["class0_fp"] + c * 6 + j * 3 + k] = pr.get(n + "class0_fp", j, k)
    for tx in range(4):
        for i in range(2):
            for j in range(2):
                for k in range(6):
                    for l in range(6):
                        dst = T["coef"] + tx * 576 + (((i * 2 + j) * 6 + k) * 6 + l) * 4
                        t[dst:dst + 3] = pr.coef[Probs.coef_at(tx, i, j, k, l, 0):][:3]
    return bytes(t)


def align(v, a):
    return (v + a - 1) // a * a


def surface_layout(width, height):
    """A picture's NV12 surface as nvtegra allocates one for the engine: rows of a multiple of
    64 bytes, the height a multiple of 32, the chroma plane right after the luma one."""
    stride = align(align(width, 2), 64)
    rows = align(align(height, 2), 32)
    return stride, rows, stride * rows


def setup(h, prev_shown, refs, stride, stream_len):
    """nvdec_vp9_pic_s, filled as nvtegra_vp9_prepare_frame_setup fills it from FFmpeg's state,
    but for the two chroma quantizer deltas. refs is the three references' (width, height,
    present); a slot that holds nothing yet gives nought, as there."""
    s = bytearray(SETUP_SIZE)
    struct.pack_into("<I", s, 0x30, stream_len)
    struct.pack_into("<I", s, 0x44, align(h["h"], 64) * 912 // 256)
    for i, (w, hh, present) in enumerate(refs):
        if present:
            struct.pack_into("<4H", s, 0x48 + i * 8, w, hh, stride, stride)
    struct.pack_into("<4H", s, 0x60, h["w"], h["h"], stride, stride)
    s[0x68] = (h["keyframe"] | h["last_keyframe"] << 1 | h["errorres"] << 3 |
               int(prev_shown) << 4 | h["intraonly"] << 5)
    s[0x6C:0x70] = bytes([0] + h["signbias"])
    # 0x74 the chroma AC delta and 0x75 the DC one, as NVIDIA's header names the two bytes and
    # reden reads them; nvtegra writes them the other way round. Only a stream whose two chroma
    # deltas differ can tell, and vp9_two_frames runs both (Q4).
    struct.pack_into("<BbBbbb", s, 0x70, h["level"], h["sharpness"], h["yac_qi"],
                     h["ydc_qdelta"], h["uvac_qdelta"], h["uvdc_qdelta"])
    allowcomp = h["allowcompinter"]
    struct.pack_into("<bbbbbb", s, 0x76, h["lossless"], h["txfmmode"],
                     0 if h["keyframe"] else h["highprecisionmvs"], h["filtermode"],
                     h["comppredmode"], h["fixcompref"] + 1 if allowcomp else 0)
    s[0x7C] = h["varcompref"][0] + 1 if allowcomp else 0
    s[0x7D] = h["varcompref"][1] + 1 if allowcomp else 0
    s[0x7E] = h["log2_tile_cols"]
    s[0x7F] = h["log2_tile_rows"]
    seg = h["seg"]
    s[0x80:0x84] = bytes([seg["enabled"], seg["update_map"], seg["temporal"],
                          seg["absolute_vals"]])
    for i, f in enumerate(seg["feat"]):
        s[0x84 + i * 4:0x88 + i * 4] = bytes(f[0:4])
        struct.pack_into("<4h", s, 0xA4 + i * 8, f[4], f[5], f[6], 0)
    s[0xE4] = h["lf_enabled"]
    struct.pack_into("<4b2b", s, 0xE5, *h["lf_ref"], *h["lf_mode"])
    return bytes(s)


def tile_sizes(h):
    """Each tile's width and height in superblocks, as nvtegra writes the tile size buffer."""
    out = []
    for i in range(h["tile_rows"]):
        for j in range(h["tile_cols"]):
            out.append((h["sb_cols"] * (j + 1) >> h["log2_tile_cols"]) -
                       (h["sb_cols"] * j >> h["log2_tile_cols"]))
            out.append((h["sb_rows"] * (i + 1) >> h["log2_tile_rows"]) -
                       (h["sb_rows"] * i >> h["log2_tile_rows"]))
    return out


# ---- the reference decoder


def run_ref(ref, packets):
    """Runs the helper over (op, bytes) packets and returns its records."""
    with tempfile.NamedTemporaryFile(suffix=".vp9p", delete=False) as f:
        for op, data in packets:
            f.write(struct.pack("<IB", len(data), op) + data)
        path = f.name
    try:
        out = subprocess.run([ref, path], check=True, capture_output=True, text=True).stdout
    finally:
        os.unlink(path)
    return [json.loads(line) for line in out.splitlines()]


def load_defaults(ref):
    return Defaults(run_ref(ref, [(3, b"")])[0])


def read_vector(vectors, vector, sha):
    path = os.path.join(vectors, vector)
    data = open(path, "rb").read()
    got = hashlib.sha256(data).hexdigest()
    if got != sha:
        raise SystemExit("%s: SHA-256 %s, expected %s" % (vector, got, sha))
    lines = open(path + ".md5").read().split()
    md5s = [lines[i] for i in range(0, len(lines), 2)]
    blocks, size = Webm(data).vp9()
    frames = [f for b in blocks for f in split_superframe(b)]
    return frames, md5s, size


def rewrite_shown(frame, where):
    """The hidden frame's header with show_frame set and its intra_only bit taken out, the way
    a job's setup can only say it: the engine is never told a frame is hidden."""
    bits = []
    for i in range(where["end_bits"]):
        bits.append(frame[i >> 3] >> (7 - (i & 7)) & 1)
    bits[where["show_frame"]] = 1
    if where.get("intra_only") is not None:
        if bits[where["intra_only"]]:
            raise ValueError("an intra-only hidden frame cannot be shown")
        del bits[where["intra_only"]]
    while len(bits) % 8:
        bits.append(0)
    head = bytes(int("".join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8))
    return head + frame[(where["end_bits"] + 7) // 8:]


def measure_one(ref, frames, md5s, defaults):
    """The three columns of the handback's table for one vector, and the first image wrong in
    each: how many of the list's MD5s FFmpeg matches with the frames as split, with each hidden
    frame rewritten as shown, and with that and the decoder told before each frame whether the
    one before was shown."""
    stream = Stream(defaults)
    hidden, wheres, shown_prev = [], [], []
    last_shown = 0
    for frame in frames:
        if is_show_existing(frame):
            hidden.append(False)
            wheres.append(None)
            shown_prev.append(None)
            continue
        where = stream.parse(frame)
        stream.decoded((stream.h["w"], stream.h["h"]))
        hidden.append(bool(stream.h["invisible"]))
        wheres.append(where)
        shown_prev.append(last_shown)
        last_shown = 1 - stream.h["invisible"]

    def score(packets, drop):
        images = []
        for i, rec in enumerate(run_ref(ref, packets)):
            if rec["ret"] < 0 or rec["rret"] < 0:
                break
            if i in drop:
                continue
            images += [o["md5"][0] for o in rec["out"]]
        right = sum(1 for a, b in zip(images, md5s) if a == b)
        first = next((k for k, (a, b) in enumerate(zip(images, md5s)) if a != b), None)
        if first is None and len(images) != len(md5s):
            first = min(len(images), len(md5s))
        return right, first

    plain = score([(0, f) for f in frames], set())
    rewritten = [rewrite_shown(f, w) if hid else f for f, w, hid in zip(frames, wheres, hidden)]
    drop = {i for i, hid in enumerate(hidden) if hid}
    blind = score([(0, f) for f in rewritten], drop)
    told = score([(0 if p is None else (1 if p else 2), f) for f, p in zip(rewritten, shown_prev)],
                 drop)
    return plain, blind, told


def build_ref(tarball, out):
    data = open(tarball, "rb").read()
    if hashlib.sha256(data).hexdigest() != FFMPEG_SHA256:
        raise SystemExit("%s is not FFmpeg 9.0.2's tarball" % tarball)
    os.makedirs(out, exist_ok=True)
    with tarfile.open(tarball) as t:
        t.extractall(out, filter="data")
    src = os.path.join(out, FFMPEG)
    build = os.path.join(out, "build")
    os.makedirs(build, exist_ok=True)
    subprocess.run([os.path.join(src, "configure")] + FFMPEG_CONFIGURE, cwd=build, check=True)
    subprocess.run(["make", "-j8"], cwd=build, check=True)
    helper = os.path.join(out, "vp9-ref")
    subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wno-deprecated-declarations",
                    "-I" + build, "-I" + src, os.path.join(HERE, "vp9-ref.c"),
                    os.path.join(build, "libavcodec", "libavcodec.a"),
                    os.path.join(build, "libavutil", "libavutil.a"), "-lm", "-lpthread",
                    "-o", helper], check=True)
    print(helper)


# ---- the jobs


def show_existing_slot(frame):
    """The slot a show_existing_frame names: the three bits after its flag (profile 0)."""
    return frame[0] & 7


class Walk:
    """A vector's frames in decode order, worked into jobs: every setup, the tile sizes, which surface each frame is written to and read from, and
    the probability tables wherever the frame's saved context is known here. The contexts are
    known after a keyframe or an error resilient frame (the defaults) and after a frame that
    saves its context without adapting it; a frame that adapts leaves its context to the
    decoder, and `generate` hands those in from FFmpeg."""

    def __init__(self, frames, defaults, size):
        self.jobs = []
        stream = Stream(defaults)
        prev_shown = 0
        for frame in frames:
            if is_show_existing(frame):
                self.jobs.append({"frame": frame, "existing": show_existing_slot(frame),
                                  "shown": True})
                continue
            refs_before = list(stream.slots)
            where = stream.parse(frame)
            h = stream.h
            # FFmpeg starts from the coded index even where the frame saves to index 0.
            start = stream.ctx[where["coded_ctx"]]
            pr, tx_mode, ref_mode, biggest = compressed_header(h, start, frame)
            h["txfmmode"], h["comppredmode"] = tx_mode, ref_mode
            if (h["w"], h["h"]) != size:
                raise ValueError("a frame of %dx%d in a %dx%d track" % (h["w"], h["h"], *size))
            stride = surface_layout(*size)[0]
            refs = []
            for r in h["refidx"]:
                slot = refs_before[r]
                refs.append((slot[0], slot[1], True) if slot else (0, 0, False))
            body = h["uncompressed_header_size"] + h["compressed_header_size"]
            self.jobs.append({
                "frame": frame, "existing": None, "body": body,
                "setup": setup(h, prev_shown, refs, stride, len(frame) - body),
                "tiles": tile_sizes(h), "shown": not h["invisible"], "probs": pr,
                "biggest": biggest, "header": json.loads(json.dumps(h)),
                "refidx": list(h["refidx"]), "refreshrefmask": h["refreshrefmask"],
            })
            # What the frame leaves in its saved context, as FFmpeg saves it.
            if h["refreshctx"]:
                if h["parallelmode"] and pr is not None and stream.ctx[h["framectxid"]]:
                    saved = stream.ctx[h["framectxid"]].copy_ctx()
                    saved.p[:] = pr.p
                    for tx in range(min(tx_mode, 3) + 1):
                        lo = Probs.coef_at(tx, 0, 0, 0, 0, 0)
                        saved.coef[lo:lo + 432] = pr.coef[lo:lo + 432]
                    stream.ctx[h["framectxid"]] = saved
                else:
                    stream.ctx[h["framectxid"]] = None
            stream.decoded((h["w"], h["h"]))
            prev_shown = not h["invisible"]
        self.surfaces = assign_surfaces(self.jobs)

    def decoded(self):
        return [j for j in self.jobs if j["existing"] is None]


def assign_surfaces(jobs):
    """Gives every decoded frame a surface: the lowest one no slot holds and the frame before
    did not use, so a surface is never rewritten while a slot or the next read still needs it.
    A reference the slots do not hold yet is the frame's own surface, as nvtegra hands it.
    Returns how many surfaces the case needs."""
    slots = [None] * 8
    prev = None
    count = 0
    for job in jobs:
        if job["existing"] is not None:
            job["surface"] = slots[job["existing"]]
            continue
        busy = set(s for s in slots if s is not None) | {prev}
        cur = next(i for i in range(16) if i not in busy)
        count = max(count, cur + 1)
        job["surface"] = cur
        job["refs"] = [slots[r] if slots[r] is not None else cur for r in job["refidx"]]
        for i in range(8):
            if job["refreshrefmask"] >> i & 1:
                slots[i] = cur
        prev = cur
    return count


def table_matches(job, table, defaults):
    """Whether a committed table is the one worked out here, on every byte the frame can read:
    all of them but the coefficient sizes above its transform mode and band 0's unused
    contexts, which FFmpeg leaves as an earlier frame had them."""
    mine = prob_table(job["probs"], job["header"]["seg"], defaults)
    return all(mine[i] == table[i] for i in usable_table(job["biggest"]))


@functools.lru_cache(maxsize=None)
def usable_table(tx_biggest):
    usable = list(range(T["coef"]))
    for a in sorted(usable_coef(tx_biggest)):
        tx, rest = divmod(a, 432)
        usable.append(T["coef"] + tx * 576 + rest // 3 * 4 + rest % 3)
    return usable


def build_case(name, vector, sha, title, vectors, ref, defaults):
    """One case from its vector: the jobs walked here, every one checked against FFmpeg's state
    after the same frame, and FFmpeg's tables and MD5s kept."""
    frames, md5s, size = read_vector(vectors, vector, sha)
    records = run_ref(ref, [(0, f) for f in frames])
    if len(records) != len(frames):
        raise SystemExit("%s: the decoder answered %d of %d frames" %
                         (vector, len(records), len(frames)))
    walk = Walk(frames, defaults, size)
    images, checked = [], 0
    for i, (job, rec) in enumerate(zip(walk.jobs, records)):
        where = "%s frame %d" % (vector, i)
        if rec["ret"] < 0 or rec["rret"] < 0:
            raise SystemExit(where + ": did not decode")
        if job["existing"] is not None:
            if len(rec["out"]) != 1:
                raise SystemExit(where + ": showed %d images" % len(rec["out"]))
            job["md5"] = rec["out"][0]["md5"]
            images.append(job["md5"][0])
            continue
        if not rec["decoded"]:
            raise SystemExit(where + ": was not decoded")
        h = job["header"]
        for key, value in h.items():
            if rec["h"][key] != value:
                raise SystemExit("%s: %s is %r here and %r in FFmpeg" %
                                 (where, key, value, rec["h"][key]))
        for k, r in enumerate(h["refidx"]):
            if rec["refs_before"][r] != list(struct.unpack_from("<2H", job["setup"], 0x48 + k * 8)):
                raise SystemExit(where + ": a reference's size is not FFmpeg's")
        theirs = Probs(bytes.fromhex(rec["p"]), bytes.fromhex(rec["coef"]))
        job["table"] = prob_table(theirs, h["seg"], defaults)
        if job["probs"] is not None:
            if not table_matches(job, job["table"], defaults):
                raise SystemExit(where + ": the table worked out here is not FFmpeg's")
            checked += 1
        job["md5"] = rec["cur"]
        if job["shown"]:
            if len(rec["out"]) != 1 or rec["out"][0]["md5"] != rec["cur"]:
                raise SystemExit(where + ": shown but not output")
            images.append(rec["cur"][0])
    if images != md5s:
        raise SystemExit("%s: FFmpeg's images are not the libvpx list" % vector)
    check_against_contexts(vector, frames, records, defaults)
    return {"name": name, "vector": vector, "sha": sha, "title": title, "size": size,
            "jobs": walk.jobs, "md5s": md5s, "surfaces": walk.surfaces, "checked": checked}


def check_against_contexts(vector, frames, records, defaults):
    """Every frame's updates applied here to the context FFmpeg saved before it must give the
    table FFmpeg decoded it with: the compressed header read here, for the frames whose
    context only the decoder knows."""
    stream = Stream(defaults)
    ctx = None
    for i, (frame, rec) in enumerate(zip(frames, records)):
        if is_show_existing(frame):
            continue
        where = stream.parse(frame)
        h = stream.h
        if h["keyframe"] or h["errorres"] or (h["intraonly"] and h["resetctx"] == 3):
            ctx = [defaults.copy_ctx() for _ in range(4)]
        elif h["intraonly"] and h["resetctx"] == 2:
            ctx[where["coded_ctx"]] = defaults.copy_ctx()
        pr, tx_mode, ref_mode, biggest = compressed_header(h, ctx[where["coded_ctx"]], frame)
        theirs = Probs(bytes.fromhex(rec["p"]), bytes.fromhex(rec["coef"]))
        if pr.p != theirs.p or any(pr.coef[a] != theirs.coef[a] for a in usable_coef(biggest)):
            raise SystemExit("%s frame %d: the updates applied to FFmpeg's context are not "
                             "FFmpeg's table" % (vector, i))
        ctx = [Probs(bytes.fromhex(c["p"]), bytes.fromhex(c["coef"])) for c in rec["ctx"]]
        stream.decoded((h["w"], h["h"]))


# ---- the C file


def fnv(data, h=0x811C9DC5):
    for b in data:
        h = (h ^ b) * 16777619 & 0xFFFFFFFF
    return h


def patch(before, after):
    """What changes from one table to the next, as the program applies it: offset << 8 | value."""
    return [i << 8 | b for i, (a, b) in enumerate(zip(before, after)) if a != b]


def c_bytes(data, indent="    ", per=24):
    return "\n".join(indent + ",".join(str(b) for b in data[i:i + per]) + ","
                     for i in range(0, len(data), per))


def c_md5(hexes):
    return "{" + ",".join("{%s}" % ",".join(str(b) for b in bytes.fromhex(x)) for x in hexes) + "}"


def limits(cases):
    """The sizes source/vp9jobs.h has to hold."""
    jobs = [j for c in cases for j in c["jobs"]]
    decoded = [j for j in jobs if j["existing"] is None]
    return {
        "VP9_FRAMES_MAX": max(len(c["jobs"]) for c in cases),
        "VP9_IMAGES_MAX": max(len(c["md5s"]) for c in cases),
        "VP9_SURFACES_MAX": max(c["surfaces"] for c in cases),
        "VP9_TILES_MAX": max(len(j["tiles"]) // 2 for j in decoded),
        "VP9_BODY_MAX": max(len(j["frame"]) - j["body"] for j in decoded),
        "VP9_WIDTH_MAX": max(c["size"][0] for c in cases),
        "VP9_HEIGHT_MAX": max(c["size"][1] for c in cases),
    }


def render(cases):
    out = []
    w = out.append
    w("// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors")
    w("// SPDX-License-Identifier: GPL-3.0-or-later")
    w("// Written by tools/vp9-jobs.py generate; run it again rather than editing this file.")
    w("// The frames are libvpx's test vectors (Copyright (c) 2010, The WebM Project authors,")
    w("// BSD-3-Clause); the tables are FFmpeg 9.0.2's state after each frame's compressed header.")
    w("// See docs/Credits.md.")
    w("")
    w('#include "vp9jobs.h"')
    w("")
    w("#include <stddef.h>")
    tables = b""
    for n, c in enumerate(cases):
        w("")
        w("// %s: %s, %s" % (c["name"], c["vector"], c["title"]))
        prev = None
        for k, job in enumerate(c["jobs"]):
            w("static const uint8_t FRAME_%d_%d[%d] = {" % (n, k, len(job["frame"])))
            w(c_bytes(job["frame"]))
            w("};")
            if job["existing"] is not None:
                continue
            tables += job["table"]
            if prev is None:
                w("static const uint8_t TABLE_%d[VP9_PROBS_SIZE] = {" % n)
                w(c_bytes(job["table"]))
                w("};")
            else:
                job["patch"] = patch(prev, job["table"])
                if job["patch"]:
                    w("static const uint32_t PATCH_%d_%d[%d] = {" % (n, k, len(job["patch"])))
                    w("\n".join("    " + ",".join("0x%05x" % v for v in job["patch"][i:i + 10])
                                + "," for i in range(0, len(job["patch"]), 10)))
                    w("};")
            prev = job["table"]
        decoded = [j for j in c["jobs"] if j["existing"] is None]
        w("static const uint8_t SETUP_%d[%d][VP9_SETUP_SIZE] = {" % (n, len(decoded)))
        for job in decoded:
            w("    {")
            w(c_bytes(job["setup"], "        "))
            w("    },")
        w("};")
        w("static const Vp9Frame FRAMES_%d[%d] = {" % (n, len(c["jobs"])))
        d = 0
        first = True
        for k, job in enumerate(c["jobs"]):
            if job["existing"] is not None:
                w("    {.data = FRAME_%d_%d, .size = %d, .existing = 1, .shown = 1, "
                  ".surface = %d,\n     .md5 = %s}," %
                  (n, k, len(job["frame"]), job["surface"], c_md5(job["md5"])))
                continue
            if first:
                table = ".probs = TABLE_%d" % n
                first = False
            elif job["patch"]:
                table = ".patch = PATCH_%d_%d, .patch_count = %d" % (n, k, len(job["patch"]))
            else:
                table = ".patch_count = 0"
            w("    {.data = FRAME_%d_%d, .size = %d, .body = %d, .setup = SETUP_%d[%d], %s,\n"
              "     .shown = %d, .surface = %d, .refs = {%s}, .tiles = %d, .tile_sizes = {%s},\n"
              "     .md5 = %s}," %
              (n, k, len(job["frame"]), job["body"], n, d, table, int(job["shown"]),
               job["surface"], ", ".join(map(str, job["refs"])), len(job["tiles"]) // 2,
               ", ".join(map(str, job["tiles"])), c_md5(job["md5"])))
            d += 1
        w("};")
        w("static const uint8_t LIST_%d[%d][16] = {" % (n, len(c["md5s"])))
        for m in c["md5s"]:
            w("    {%s}," % ",".join(str(b) for b in bytes.fromhex(m)))
        w("};")
    w("")
    w("const Vp9Case VP9_CASES[VP9_CASE_COUNT] = {")
    for n, c in enumerate(cases):
        w('    {.name = "%s", .title = "%s",' % (c["name"], c["title"]))
        w('     .vector = "%s",' % c["vector"])
        w("     .sha256 = {%s}," % ",".join(str(b) for b in bytes.fromhex(c["sha"])))
        w("     .width = %d, .height = %d, .surfaces = %d, .frame_count = %d, .frames = FRAMES_%d,"
          % (c["size"][0], c["size"][1], c["surfaces"], len(c["jobs"]), n))
        w("     .image_count = %d, .images = LIST_%d}," % (len(c["md5s"]), n))
    w("};")
    w("")
    w("// FNV-1a of every table in order, what applying the patches must give.")
    w("const uint32_t VP9_TABLES_FNV = 0x%08xu;" % fnv(tables))
    return "\n".join(out) + "\n"


def render_defaults(defaults):
    out = [
        "# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors",
        "# SPDX-License-Identifier: GPL-3.0-or-later",
        '"""Written by tools/vp9-jobs.py generate; run it again rather than editing this file.',
        "",
        "FFmpeg 9.0.2's default VP9 probabilities (libavcodec/vp9data.c, LGPL-2.1-or-later), as the",
        "reference helper printed them: what a keyframe resets the four saved contexts to, and the",
        "keyframe mode and partition tables. `tools/vp9-jobs.py check` starts from them, so the",
        'committed jobs can be checked where FFmpeg is not at hand."""',
        "",
        "DEFAULTS = {",
    ]
    for key in ("p", "coef", "kf_y", "kf_uv", "kf_partition"):
        text = getattr(defaults, key).hex()
        out.append('    "%s": (' % key)
        for i in range(0, len(text), 88):
            out.append('        "%s"' % text[i:i + 88])
        out.append("    ),")
    out.append("}")
    return "\n".join(out) + "\n"


# ---- reading the committed file back


def parse_c(text):
    """The committed source/vp9jobs.c, read back into what render wrote."""
    arrays = {}
    for m in re.finditer(r"static const uint(8|32)_t (\w+)\[[^\]]*\](?:\[[^\]]*\])* = \{(.*?)\n\};",
                         text, re.S):
        nums = [int(v, 0) for v in re.findall(r"0x[0-9a-f]+|\d+", m.group(3))]
        arrays[m.group(2)] = nums
    frames = {}
    for m in re.finditer(r"static const Vp9Frame (FRAMES_\d+)\[\d+\] = \{\n(.*?)\n\};", text, re.S):
        entries = []
        for e in re.findall(r"    \{(\.data = .*?)\},(?=\n    \{|$)", m.group(2), re.S):
            fields = {}
            for f in re.finditer(r"\.(\w+) = (\{(?:[^{}]|\{[^{}]*\})*\}|[^,{}]+)", e):
                fields[f.group(1)] = f.group(2).strip()
            entries.append(fields)
        frames[m.group(1)] = entries
    cases = []
    body = re.search(r"const Vp9Case VP9_CASES\[VP9_CASE_COUNT\] = \{\n(.*?)\n\};", text, re.S)
    for e in re.findall(r"    \{(\.name = .*?\.images = LIST_\d+)\},", body.group(1), re.S):
        fields = {}
        for f in re.finditer(r'\.(\w+) = ("[^"]*"|\{[^{}]*\}|[^,{}]+)', e):
            fields[f.group(1)] = f.group(2).strip()
        cases.append(fields)
    fnv_value = int(re.search(r"VP9_TABLES_FNV = 0x([0-9a-f]+)u;", text).group(1), 16)
    return arrays, frames, cases, fnv_value


def nums(text):
    return [int(v, 0) for v in re.findall(r"0x[0-9a-f]+|\d+", text)]


def check(text=None, header=None, defaults_record=None, report=print):
    """Checks the committed jobs with neither FFmpeg nor the vectors: every frame's header parsed
    again from the frame the file carries, every setup, tile list and surface worked out again,
    and every table worked out again wherever its context is known here. Returns 0 when all
    holds, 1 with what does not said."""
    if text is None:
        text = open(OUT).read()
    if header is None:
        header = open(os.path.join(ROOT, "source", "vp9jobs.h")).read()
    if defaults_record is None:
        spec = importlib.util.spec_from_file_location("vp9defaults",
                                                      os.path.join(HERE, "vp9defaults.py"))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        defaults_record = mod.DEFAULTS
    defaults = Defaults(defaults_record)
    arrays, frames, cases, fnv_value = parse_c(text)
    problems = []
    tables = b""
    built = []
    if [c["name"].strip('"') for c in cases] != [c[0] for c in CASES]:
        problems.append("the cases are not tools/vp9-jobs.py's, in its order")
    for n, (fields, (name, vector, sha, title)) in enumerate(zip(cases, CASES)):
        where = name
        if fields["vector"].strip('"') != vector or bytes(nums(fields["sha256"])).hex() != sha:
            problems.append("%s: not the vector %s with its SHA-256" % (where, vector))
        size = (int(fields["width"]), int(fields["height"]))
        entries = frames["FRAMES_%d" % n]
        data = [bytes(arrays[e["data"]]) for e in entries]
        try:
            walk = Walk(data, defaults, size)
        except ValueError as e:
            problems.append("%s: %s" % (where, e))
            continue
        if int(fields["surfaces"]) != walk.surfaces:
            problems.append("%s: %s surfaces, worked out %d" % (where, fields["surfaces"],
                                                               walk.surfaces))
        table = None
        images = []
        known = 0
        for k, (e, job) in enumerate(zip(entries, walk.jobs)):
            at = "%s frame %d" % (where, k)
            md5 = [bytes(nums(m)).hex() for m in re.findall(r"\{[^{}]*\}", e["md5"][1:-1])]
            if int(e.get("shown", 0)):
                images.append(md5[0])
            if job["existing"] is not None:
                if not int(e.get("existing", 0)) or int(e["surface"]) != job["surface"]:
                    problems.append(at + ": not the show_existing_frame worked out")
                continue
            setup_name, setup_index = re.match(r"(\w+)\[(\d+)\]", e["setup"]).groups()
            got_setup = bytes(arrays[setup_name][int(setup_index) * SETUP_SIZE:]
                              [:SETUP_SIZE])
            if got_setup != job["setup"]:
                problems.append(at + ": the setup is not the one worked out")
            if int(e["body"]) != job["body"] or int(e["shown"]) != int(job["shown"]):
                problems.append(at + ": the body or the shown flag is not worked out")
            if int(e["surface"]) != job["surface"] or nums(e["refs"]) != job["refs"]:
                problems.append(at + ": the surfaces are not the ones worked out")
            if nums(e["tile_sizes"]) != job["tiles"] or int(e["tiles"]) * 2 != len(job["tiles"]):
                problems.append(at + ": the tiles are not the ones worked out")
            if "probs" in e:
                table = bytearray(arrays[e["probs"].strip()])
            else:
                for v in (arrays[e["patch"]] if "patch" in e else []):
                    table[v >> 8] = v & 0xFF
            tables += bytes(table)
            if job["probs"] is not None:
                known += 1
                if not table_matches(job, table, defaults):
                    problems.append(at + ": the table is not the one worked out")
        listed = arrays["LIST_%d" % n]
        if images != [bytes(listed[i:i + 16]).hex() for i in range(0, len(listed), 16)]:
            problems.append(where + ": the shown frames' MD5s are not the list")
        built.append({"name": name, "jobs": walk.jobs, "size": size, "surfaces": walk.surfaces,
                      "md5s": images})
        report("%s: %d frames, %d tables worked out here" % (name, len(entries), known))
    if fnv(tables) != fnv_value:
        problems.append("VP9_TABLES_FNV is not the tables' FNV")
    if built:
        for key, value in limits_from(built).items():
            m = re.search(r"#define %s (0x[0-9a-fA-F]+|\d+)u" % key, header)
            if not m or int(m.group(1), 0) < value:
                problems.append("source/vp9jobs.h: %s must hold %d" % (key, value))
    for p in problems:
        report("problem: " + p)
    return 1 if problems else 0


def limits_from(built):
    for c in built:
        for j in c["jobs"]:
            j.setdefault("tiles", [])
    return limits([{"jobs": c["jobs"], "md5s": c["md5s"], "surfaces": c["surfaces"],
                    "size": c["size"]} for c in built])


def generate(ref, vectors):
    defaults = load_defaults(ref)
    cases = [build_case(name, vector, sha, title, vectors, ref, defaults)
             for name, vector, sha, title in CASES]
    return cases, defaults


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build-ref")
    b.add_argument("tarball")
    b.add_argument("dir")
    m = sub.add_parser("measure")
    m.add_argument("ref")
    m.add_argument("vectors")
    g = sub.add_parser("generate")
    g.add_argument("ref")
    g.add_argument("vectors")
    g.add_argument("--out", default=OUT)
    g.add_argument("--defaults", default=os.path.join(HERE, "vp9defaults.py"))
    sub.add_parser("check")
    args = ap.parse_args()
    if args.cmd == "build-ref":
        build_ref(args.tarball, args.dir)
    elif args.cmd == "measure":
        defaults = load_defaults(args.ref)
        print("| vector | the stream split | without visibility | with visibility |")
        print("|---|---|---|---|")
        for name, vector, sha, title in CASES:
            frames, md5s, _ = read_vector(args.vectors, vector, sha)
            cols = measure_one(args.ref, frames, md5s, defaults)
            cells = ["%d/%d%s" % (right, len(md5s), "" if first is None else
                                  " (first wrong %d)" % (first + 1)) for right, first in cols]
            print("| %s | %s |" % (vector[:-5], " | ".join(cells)))
    elif args.cmd == "generate":
        cases, defaults = generate(args.ref, args.vectors)
        with open(args.out, "w") as f:
            f.write(render(cases))
        with open(args.defaults, "w") as f:
            f.write(render_defaults(defaults))
        for key, value in limits(cases).items():
            print("%s %d" % (key, value))
    elif args.cmd == "check":
        sys.exit(check())


if __name__ == "__main__":
    main()
