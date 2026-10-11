#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes source/jitblocks.c: the straight-line functions the cpu suite's compile case calls, and
the state each must leave, worked out here from the architecture's rules and nothing else.

Each function is one basic block: it loads seventeen general registers and sixteen vector
registers from the state x0 points at, runs OPS operations drawn from a fixed pseudo-random
sequence, stores them all back and returns. Seventeen live integers and sixteen live vectors are
more than an x86-64 host has registers for, so a translator's register allocator has to spill,
which is where its bookkeeping costs the most and where a mistake in it shows as a wrong value.

The semantics below are the Arm ARM's for each instruction (C6 for the general registers, C7 for
SIMD), restated in Python integers; the generator shares no code with the program. Run it with no
arguments to print the file, or with --check to compare the committed one.
"""

import sys

BLOCKS = 48
OPS = 160
SEED = 0x6A09E667F3BCC908
M64 = (1 << 64) - 1
M32 = (1 << 32) - 1

# x0 holds the state; x18 is the platform register and x19 up are callee-saved, so they stay out.
GPRS = list(range(1, 18))
# v8 to v15 keep their low halves across a call, so they stay out too.
VECS = list(range(0, 8)) + list(range(16, 24))
VEC_OFFSET = 8 * 18  # after the seventeen integers and one word of padding, 16-byte aligned
STATE_WORDS = 18 + 2 * len(VECS)
CONDS = ["eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"]


class Rng:
    """splitmix64, so the sequence is the same on every Python."""

    def __init__(self, seed):
        self.state = seed & M64

    def next(self):
        self.state = (self.state + 0x9E3779B97F4A7C15) & M64
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
        return z ^ (z >> 31)

    def below(self, n):
        return self.next() % n

    def pick(self, items):
        return items[self.below(len(items))]


def signed(v):
    return v - (1 << 64) if v >> 63 else v


def asr(v, s):
    return (signed(v) >> s) & M64


def ror(v, s):
    return ((v >> s) | (v << (64 - s))) & M64 if s else v


def clz(v):
    return 64 - v.bit_length()


def adds(n, m):
    """The sum and NZCV of ADDS (C6.2.5): carry out of bit 63, signed overflow."""
    r = (n + m) & M64
    flags = {
        "n": r >> 63,
        "z": int(r == 0),
        "c": int(n + m > M64),
        "v": ((n ^ r) & (m ^ r)) >> 63,
    }
    return r, flags


def condition_holds(cond, f):
    """ConditionHolds (J1.2.4 shared/functions/system), for the conditions CSEL takes here."""
    n, z, c, v = f["n"], f["z"], f["c"], f["v"]
    table = {
        "eq": z == 1, "ne": z == 0, "cs": c == 1, "cc": c == 0,
        "mi": n == 1, "pl": n == 0, "vs": v == 1, "vc": v == 0,
        "hi": c == 1 and z == 0, "ls": not (c == 1 and z == 0),
        "ge": n == v, "lt": n != v, "gt": z == 0 and n == v, "le": not (z == 0 and n == v),
    }
    return table[cond]


def lanes32(q):
    lo, hi = q
    return [lo & M32, lo >> 32, hi & M32, hi >> 32]


def from_lanes32(lanes):
    return ((lanes[1] << 32) | lanes[0], (lanes[3] << 32) | lanes[2])


class Machine:
    """The registers a block touches, x by number and v as (low, high) 64-bit halves."""

    def __init__(self, state):
        self.x = {r: state[i] for i, r in enumerate(GPRS)}
        self.v = {r: (state[18 + 2 * i], state[19 + 2 * i]) for i, r in enumerate(VECS)}
        self.flags = None

    def state(self):
        out = [self.x[r] for r in GPRS] + [0]
        for r in VECS:
            out += list(self.v[r])
        return out


def gen_op(rng, m, lines):
    """Draws one operation, applies it to @p m and appends its assembly to @p lines."""
    kind = rng.below(100)
    x = lambda: rng.pick(GPRS)
    v = lambda: rng.pick(VECS)
    if kind < 30:
        op = rng.pick(["add", "sub", "eor", "orr", "and", "bic"])
        d, n, k, s = x(), x(), x(), rng.below(8)
        a, b = m.x[n], (m.x[k] << s) & M64
        r = {"add": a + b, "sub": a - b, "eor": a ^ b, "orr": a | b, "and": a & b,
             "bic": a & ~b}[op] & M64
        m.x[d] = r
        lines.append(f"{op} x{d}, x{n}, x{k}, lsl #{s}")
    elif kind < 38:
        op = rng.pick(["add", "sub"])
        d, n, imm = x(), x(), rng.below(4096)
        m.x[d] = (m.x[n] + imm if op == "add" else m.x[n] - imm) & M64
        lines.append(f"{op} x{d}, x{n}, #{imm}")
    elif kind < 48:
        op = rng.pick(["lsl", "lsr", "asr", "ror"])
        d, n, s = x(), x(), 1 + rng.below(63)
        a = m.x[n]
        m.x[d] = {"lsl": (a << s) & M64, "lsr": a >> s, "asr": asr(a, s), "ror": ror(a, s)}[op]
        lines.append(f"{op} x{d}, x{n}, #{s}")
    elif kind < 56:
        op = rng.pick(["mul", "madd", "umulh"])
        d, n, k, a = x(), x(), x(), x()
        p = m.x[n] * m.x[k]
        if op == "mul":
            m.x[d] = p & M64
            lines.append(f"mul x{d}, x{n}, x{k}")
        elif op == "madd":
            m.x[d] = (m.x[a] + p) & M64
            lines.append(f"madd x{d}, x{n}, x{k}, x{a}")
        else:
            m.x[d] = p >> 64
            lines.append(f"umulh x{d}, x{n}, x{k}")
    elif kind < 64:
        d, n, k = x(), x(), x()
        r, flags = adds(m.x[n], m.x[k])
        m.x[d] = r
        m.flags = flags
        lines.append(f"adds x{d}, x{n}, x{k}")
        c, a, b, cond = x(), x(), x(), rng.pick(CONDS)
        m.x[c] = m.x[a] if condition_holds(cond, flags) else m.x[b]
        lines.append(f"csel x{c}, x{a}, x{b}, {cond}")
    elif kind < 66:
        d, n = x(), x()
        m.x[d] = clz(m.x[n])
        lines.append(f"clz x{d}, x{n}")
    elif kind < 86:
        op = rng.pick(["add2d", "eor", "orr", "and", "add4s", "mul4s", "ushr", "shl"])
        d, n, k = v(), v(), v()
        (nl, nh), (kl, kh) = m.v[n], m.v[k]
        if op == "add2d":
            m.v[d] = ((nl + kl) & M64, (nh + kh) & M64)
            lines.append(f"add v{d}.2d, v{n}.2d, v{k}.2d")
        elif op in ("eor", "orr", "and"):
            f = {"eor": lambda p, q: p ^ q, "orr": lambda p, q: p | q, "and": lambda p, q: p & q}[op]
            m.v[d] = (f(nl, kl), f(nh, kh))
            lines.append(f"{op} v{d}.16b, v{n}.16b, v{k}.16b")
        elif op in ("add4s", "mul4s"):
            a, b = lanes32(m.v[n]), lanes32(m.v[k])
            if op == "add4s":
                r = [(p + q) & M32 for p, q in zip(a, b)]
            else:
                r = [(p * q) & M32 for p, q in zip(a, b)]
            m.v[d] = from_lanes32(r)
            lines.append(f"{op[:3]} v{d}.4s, v{n}.4s, v{k}.4s")
        elif op == "ushr":
            s = 1 + rng.below(63)
            m.v[d] = (nl >> s, nh >> s)
            lines.append(f"ushr v{d}.2d, v{n}.2d, #{s}")
        else:
            s = rng.below(64)
            m.v[d] = ((nl << s) & M64, (nh << s) & M64)
            lines.append(f"shl v{d}.2d, v{n}.2d, #{s}")
    else:
        op = rng.pick(["fmov", "ins", "umov"])
        if op == "fmov":
            d, n = v(), x()
            m.v[d] = (m.x[n], 0)  # FMOV Dd, Xn zeroes the upper half (C7.2.130)
            lines.append(f"fmov d{d}, x{n}")
        elif op == "ins":
            d, n = v(), x()
            m.v[d] = (m.v[d][0], m.x[n])
            lines.append(f"mov v{d}.d[1], x{n}")
        else:
            d, n, lane = x(), v(), rng.below(2)
            m.x[d] = m.v[n][lane]
            lines.append(f"mov x{d}, v{n}.d[{lane}]")


def initial_state(rng):
    """Random words, with the edges the flag and shift rules turn on mixed in."""
    edges = [0, M64, 1 << 63, (1 << 63) - 1, 1, M32]
    words = [rng.next() for _ in range(STATE_WORDS)]
    for i, e in enumerate(edges):
        words[3 * i + 1] = e
    words[17] = 0  # padding before the vectors
    return words


def load_store(op):
    """The ldp/ldr (or stp/str) lines moving every register between the state and the block."""
    out = []
    for i in range(0, 16, 2):
        out.append(f"{op}p x{GPRS[i]}, x{GPRS[i + 1]}, [x0, #{8 * i}]")
    out.append(f"{'ldr' if op == 'ld' else 'str'} x{GPRS[16]}, [x0, #{8 * 16}]")
    for i in range(0, len(VECS), 2):
        out.append(f"{op}p q{VECS[i]}, q{VECS[i + 1]}, [x0, #{VEC_OFFSET + 16 * i}]")
    return out


def generate():
    rng = Rng(SEED)
    init = initial_state(rng)
    blocks = []
    for b in range(BLOCKS):
        m = Machine(init)
        body = []
        for _ in range(OPS):
            gen_op(rng, m, body)
        blocks.append((load_store("ld") + body + load_store("st") + ["ret"], m.state()))
    return init, blocks


def words(values, indent):
    rows = []
    for i in range(0, len(values), 4):
        rows.append(indent + ", ".join(f"0x{w:016x}ull" for w in values[i:i + 4]) + ",")
    return "\n".join(rows)


def render():
    init, blocks = generate()
    out = []
    out.append("// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors")
    out.append("// SPDX-License-Identifier: GPL-3.0-or-later")
    out.append("// Written by tools/jit-blocks.py; run it again rather than editing this file.")
    out.append("")
    out.append('#include "jitblocks.h"')
    out.append("")
    out.append("#if defined(__aarch64__)")
    for b, (lines, _) in enumerate(blocks):
        out.append("__asm__(")
        out.append('    ".text\\n"')
        out.append('    ".balign 16\\n"')
        out.append(f'    ".global JitBlock{b}\\n"')
        out.append(f'    ".type JitBlock{b}, %function\\n"')
        out.append(f'    "JitBlock{b}:\\n"')
        for line in lines:
            out.append(f'    "    {line}\\n"')
        out.append(f'    ".size JitBlock{b}, . - JitBlock{b}\\n");')
    # The warm-up the case calls first, through the same call site, so the caller's own code is
    # compiled before any block is timed.
    out.append("__asm__(")
    out.append('    ".text\\n"')
    out.append('    ".global JitWarm\\n"')
    out.append('    ".type JitWarm, %function\\n"')
    out.append('    "JitWarm:\\n"')
    out.append('    "    ret\\n"')
    out.append('    ".size JitWarm, . - JitWarm\\n");')
    out.append("")
    for b in range(BLOCKS):
        out.append(f"void JitBlock{b}(uint64_t *state);")
    out.append("")
    out.append("void (*const JIT_BLOCK[JIT_BLOCKS])(uint64_t *state) = {")
    for b in range(0, BLOCKS, 6):
        out.append("    " + " ".join(f"JitBlock{i}," for i in range(b, min(b + 6, BLOCKS))))
    out.append("};")
    out.append("#endif")
    out.append("")
    out.append("const uint64_t JIT_INITIAL[JIT_STATE_WORDS] = {")
    out.append(words(init, "    "))
    out.append("};")
    out.append("")
    out.append("const uint64_t JIT_EXPECTED[JIT_BLOCKS][JIT_STATE_WORDS] = {")
    for b, (_, final) in enumerate(blocks):
        out.append("    {")
        out.append(words(final, "        "))
        out.append("    },")
    out.append("};")
    return "\n".join(out) + "\n"


def main(argv):
    text = render()
    if argv[1:] == ["--check"]:
        import os
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "source", "jitblocks.c")
        with open(path) as f:
            same = f.read() == text
        if not same:
            print("source/jitblocks.c differs from tools/jit-blocks.py; run it again", file=sys.stderr)
            return 1
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
