// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include "astcref.h"

#include <string.h>

// Section numbers below are the Khronos Data Format Specification 1.3's ASTC chapter.

static uint32_t Bit(const uint8_t block[16], uint32_t index) {
    return (block[index >> 3] >> (index & 7u)) & 1u;
}

static uint32_t Bits(const uint8_t block[16], uint32_t start, uint32_t count) {
    uint32_t value = 0;
    for (uint32_t i = 0; i < count; ++i) {
        value |= Bit(block, start + i) << i;
    }
    return value;
}

// ---- Integer sequence encoding (23.12) ----

typedef struct {
    uint32_t trits;  // 1 when the range has a trit
    uint32_t quints; // 1 when it has a quint
    uint32_t bits;   // the plain bits per value
} Ise;

static bool IseOf(uint32_t levels, Ise *out) {
    static const struct {
        uint32_t levels, trits, quints, bits;
    } TABLE[] = {
        { 2, 0, 0, 1 },   { 3, 1, 0, 0 },   { 4, 0, 0, 2 },   { 5, 0, 1, 0 },
        { 6, 1, 0, 1 },   { 8, 0, 0, 3 },   { 10, 0, 1, 1 },  { 12, 1, 0, 2 },
        { 16, 0, 0, 4 },  { 20, 0, 1, 2 },  { 24, 1, 0, 3 },  { 32, 0, 0, 5 },
        { 40, 0, 1, 3 },  { 48, 1, 0, 4 },  { 64, 0, 0, 6 },  { 80, 0, 1, 4 },
        { 96, 1, 0, 5 },  { 128, 0, 0, 7 }, { 160, 0, 1, 5 }, { 192, 1, 0, 6 },
        { 256, 0, 0, 8 },
    };
    for (uint32_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); ++i) {
        if (TABLE[i].levels == levels) {
            out->trits = TABLE[i].trits;
            out->quints = TABLE[i].quints;
            out->bits = TABLE[i].bits;
            return true;
        }
    }
    return false;
}

static uint32_t IseBitCount(uint32_t count, uint32_t levels) {
    Ise ise = { 0, 0, 0 };
    if (!IseOf(levels, &ise)) {
        return 0xFFFFu;
    }
    uint32_t bits = count * ise.bits;
    if (ise.trits) {
        bits += (8u * count + 4u) / 5u;
    }
    if (ise.quints) {
        bits += (7u * count + 2u) / 3u;
    }
    return bits;
}

/// A bit stream over the block, forwards from @p start, or backwards from bit 127 (the weights).
typedef struct {
    const uint8_t *block;
    uint32_t position;
    bool reversed;
    uint32_t limit;
} Stream;

static uint32_t Take(Stream *s, uint32_t count) {
    uint32_t value = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t bit = 0;
        if (s->position < s->limit) {
            bit = s->reversed ? Bit(s->block, 127u - s->position) : Bit(s->block, s->position);
        }
        value |= bit << i;
        ++s->position;
    }
    return value;
}

static void DecodeTrits(uint32_t t, uint32_t out[5]) {
    uint32_t c;
    if (((t >> 2) & 7u) == 7u) {
        c = (((t >> 5) & 7u) << 2) | (t & 3u);
        out[4] = 2;
        out[3] = 2;
    } else {
        c = t & 0x1Fu;
        if (((t >> 5) & 3u) == 3u) {
            out[4] = 2;
            out[3] = (t >> 7) & 1u;
        } else {
            out[4] = (t >> 7) & 1u;
            out[3] = (t >> 5) & 3u;
        }
    }
    if ((c & 3u) == 3u) {
        out[2] = 2;
        out[1] = (c >> 4) & 1u;
        out[0] = (((c >> 3) & 1u) << 1) | (((c >> 2) & 1u) & ~((c >> 3) & 1u));
    } else if (((c >> 2) & 3u) == 3u) {
        out[2] = 2;
        out[1] = 2;
        out[0] = c & 3u;
    } else {
        out[2] = (c >> 4) & 1u;
        out[1] = (c >> 2) & 3u;
        out[0] = (((c >> 1) & 1u) << 1) | ((c & 1u) & ~((c >> 1) & 1u));
    }
}

static void DecodeQuints(uint32_t q, uint32_t out[3]) {
    if (((q >> 1) & 3u) == 3u && ((q >> 5) & 3u) == 0u) {
        const uint32_t q0 = q & 1u;
        out[2] = (q0 << 2) | ((((q >> 4) & 1u) & ~q0 & 1u) << 1) | (((q >> 3) & 1u) & ~q0 & 1u);
        out[1] = 4;
        out[0] = 4;
        return;
    }
    uint32_t c;
    if (((q >> 1) & 3u) == 3u) {
        out[2] = 4;
        c = (((q >> 3) & 3u) << 3) | ((~(q >> 5) & 3u) << 1) | (q & 1u);
    } else {
        out[2] = (q >> 5) & 3u;
        c = q & 0x1Fu;
    }
    if ((c & 7u) == 5u) {
        out[1] = 4;
        out[0] = (c >> 3) & 3u;
    } else {
        out[1] = (c >> 3) & 3u;
        out[0] = c & 7u;
    }
}

/// Decodes @p count values of range @p levels; each value comes back as (digit << bits) | bits,
/// with the trit or quint digit and the plain bits kept apart in @p digits and @p low.
static void DecodeIse(Stream *s, uint32_t count, uint32_t levels, uint32_t digits[],
                      uint32_t low[]) {
    Ise ise = { 0, 0, 0 };
    IseOf(levels, &ise);
    uint32_t done = 0;
    while (done < count) {
        if (ise.trits) {
            uint32_t m[5] = { 0, 0, 0, 0, 0 };
            uint32_t t = 0;
            m[0] = Take(s, ise.bits);
            t |= Take(s, 2);
            m[1] = Take(s, ise.bits);
            t |= Take(s, 2) << 2;
            m[2] = Take(s, ise.bits);
            t |= Take(s, 1) << 4;
            m[3] = Take(s, ise.bits);
            t |= Take(s, 2) << 5;
            m[4] = Take(s, ise.bits);
            t |= Take(s, 1) << 7;
            uint32_t d[5];
            DecodeTrits(t, d);
            for (uint32_t i = 0; i < 5u && done < count; ++i, ++done) {
                digits[done] = d[i];
                low[done] = m[i];
            }
        } else if (ise.quints) {
            uint32_t m[3] = { 0, 0, 0 };
            uint32_t q = 0;
            m[0] = Take(s, ise.bits);
            q |= Take(s, 3);
            m[1] = Take(s, ise.bits);
            q |= Take(s, 2) << 3;
            m[2] = Take(s, ise.bits);
            q |= Take(s, 2) << 5;
            uint32_t d[3];
            DecodeQuints(q, d);
            for (uint32_t i = 0; i < 3u && done < count; ++i, ++done) {
                digits[done] = d[i];
                low[done] = m[i];
            }
        } else {
            digits[done] = 0;
            low[done] = Take(s, ise.bits);
            ++done;
        }
    }
}

/// Plain bits replicated to fill @p to bits.
static uint32_t Replicate(uint32_t value, uint32_t from, uint32_t to) {
    if (from == 0) {
        return 0;
    }
    uint32_t out = 0;
    int32_t shift = (int32_t)to - (int32_t)from;
    while (shift > -(int32_t)from) {
        out |= shift >= 0 ? value << shift : value >> -shift;
        shift -= (int32_t)from;
    }
    return out & ((1u << to) - 1u);
}

/// The B pattern of 23.13's unquantization tables from the value's plain bits a (bit 0) to f,
/// written from the table's strings, most significant character first.
static uint32_t Pattern(const char *pattern, uint32_t bits) {
    uint32_t out = 0;
    for (const char *p = pattern; *p != '\0'; ++p) {
        out <<= 1;
        if (*p >= 'a' && *p <= 'f') {
            out |= (bits >> (uint32_t)(*p - 'a')) & 1u;
        }
    }
    return out;
}

// ---- Colour endpoint unquantization (23.13, table "Color Unquantization Parameters") ----

static uint32_t UnquantizeColour(uint32_t digit, uint32_t low, uint32_t levels) {
    Ise ise = { 0, 0, 0 };
    IseOf(levels, &ise);
    if (!ise.trits && !ise.quints) {
        return Replicate(low, ise.bits, 8);
    }
    static const struct {
        uint32_t levels;
        const char *b;
        uint32_t c;
    } TABLE[] = {
        { 6, "000000000", 204 },   { 10, "000000000", 113 }, { 12, "b000b0bb0", 93 },
        { 20, "b0000bb00", 54 },   { 24, "cb000cbcb", 44 },  { 40, "cb0000cbc", 26 },
        { 48, "dcb000dcb", 22 },   { 80, "dcb0000dc", 13 },  { 96, "edcb000ed", 11 },
        { 160, "edcb0000e", 6 },   { 192, "fedcb000f", 5 },
    };
    for (uint32_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); ++i) {
        if (TABLE[i].levels != levels) {
            continue;
        }
        const uint32_t a = (low & 1u) ? 0x1FFu : 0u;
        uint32_t t = digit * TABLE[i].c + Pattern(TABLE[i].b, low);
        t ^= a;
        return (a & 0x80u) | (t >> 2);
    }
    return 0;
}

// ---- Weight unquantization (23.17, table "Weight Unquantization Parameters") ----

uint32_t AstcUnquantizeWeight(uint32_t value, uint32_t levels) {
    Ise ise = { 0, 0, 0 };
    if (!IseOf(levels, &ise) || levels > 32u) {
        return 0;
    }
    const uint32_t digit = ise.bits == 0 ? value : value >> ise.bits;
    const uint32_t low = value & ((1u << ise.bits) - 1u);
    uint32_t t;
    if (!ise.trits && !ise.quints) {
        t = Replicate(low, ise.bits, 6);
    } else if (ise.bits == 0) {
        t = ise.trits ? digit * 32u : digit * 16u;
        return t;
    } else {
        static const struct {
            uint32_t levels;
            const char *b;
            uint32_t c;
        } TABLE[] = {
            { 6, "0000000", 50 }, { 10, "0000000", 28 }, { 12, "b000b0b", 23 },
            { 20, "b0000b0", 13 }, { 24, "cb000cb", 11 },
        };
        t = 0;
        for (uint32_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); ++i) {
            if (TABLE[i].levels != levels) {
                continue;
            }
            const uint32_t a = (low & 1u) ? 0x7Fu : 0u;
            t = digit * TABLE[i].c + Pattern(TABLE[i].b, low);
            t ^= a;
            t = (a & 0x20u) | (t >> 2);
        }
    }
    if (t > 32u) {
        t += 1u;
    }
    return t;
}

// ---- Block mode (23.10, table "2D Block Mode Layout") ----

typedef struct {
    uint32_t grid_width;
    uint32_t grid_height;
    bool dual;
    uint32_t levels;
} BlockMode;

static bool DecodeBlockMode(uint32_t mode, BlockMode *out) {
    uint32_t r;
    uint32_t w;
    uint32_t h;
    bool dual = ((mode >> 10) & 1u) != 0;
    bool high = ((mode >> 9) & 1u) != 0;
    const uint32_t a = (mode >> 5) & 3u;
    if ((mode & 3u) != 0u) {
        r = ((mode & 3u) << 1) | ((mode >> 4) & 1u);
        const uint32_t bb = (mode >> 7) & 3u;
        switch ((mode >> 2) & 3u) {
        case 0:
            w = bb + 4u;
            h = a + 2u;
            break;
        case 1:
            w = bb + 8u;
            h = a + 2u;
            break;
        case 2:
            w = a + 2u;
            h = bb + 8u;
            break;
        default:
            if (((mode >> 8) & 1u) == 0u) {
                w = a + 2u;
                h = ((mode >> 7) & 1u) + 6u;
            } else {
                w = ((mode >> 7) & 1u) + 2u;
                h = a + 2u;
            }
            break;
        }
    } else {
        if (((mode >> 2) & 3u) == 0u) {
            return false; // reserved (void extent is handled before this)
        }
        r = (((mode >> 2) & 3u) << 1) | ((mode >> 4) & 1u);
        switch ((mode >> 7) & 3u) {
        case 0:
            w = 12u;
            h = a + 2u;
            break;
        case 1:
            w = a + 2u;
            h = 12u;
            break;
        case 2:
            w = a + 6u;
            h = ((mode >> 9) & 3u) + 6u;
            dual = false;
            high = false;
            break;
        default:
            if (a == 0u) {
                w = 6u;
                h = 10u;
            } else if (a == 1u) {
                w = 10u;
                h = 6u;
            } else {
                return false;
            }
            break;
        }
    }
    static const uint32_t LOW[8] = { 0, 0, 2, 3, 4, 5, 6, 8 };
    static const uint32_t HIGH[8] = { 0, 0, 10, 12, 16, 20, 24, 32 };
    const uint32_t levels = high ? HIGH[r] : LOW[r];
    if (levels == 0u) {
        return false;
    }
    out->grid_width = w;
    out->grid_height = h;
    out->dual = dual;
    out->levels = levels;
    return true;
}

// ---- Endpoint modes (23.14) ----

static int32_t Clamp255(int32_t v) {
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

static void BitTransferSigned(int32_t *a, int32_t *b) {
    *b >>= 1;
    *b |= *a & 0x80;
    *a >>= 1;
    *a &= 0x3F;
    if (*a & 0x20) {
        *a -= 0x40;
    }
}

static void BlueContract(int32_t out[4], int32_t r, int32_t g, int32_t b, int32_t a) {
    out[0] = (r + b) >> 1;
    out[1] = (g + b) >> 1;
    out[2] = b;
    out[3] = a;
}

static void Set(int32_t out[4], int32_t r, int32_t g, int32_t b, int32_t a) {
    out[0] = r;
    out[1] = g;
    out[2] = b;
    out[3] = a;
}

/// The HDR modes give both endpoints the error colour in this profile, partition by partition,
/// as ARM's reference decoder does (astcenc, unpack_color_endpoints, ASTCENC_PRF_LDR).
static bool DecodeEndpoints(uint32_t mode, const uint32_t *values, int32_t e0[4], int32_t e1[4]) {
    int32_t v[8];
    for (uint32_t i = 0; i < 8u; ++i) {
        v[i] = (int32_t)values[i];
    }
    switch (mode) {
    case 0:
        Set(e0, v[0], v[0], v[0], 255);
        Set(e1, v[1], v[1], v[1], 255);
        break;
    case 1: {
        const int32_t l0 = (v[0] >> 2) | (v[1] & 0xC0);
        const int32_t l1 = Clamp255(l0 + (v[1] & 0x3F));
        Set(e0, l0, l0, l0, 255);
        Set(e1, l1, l1, l1, 255);
        break;
    }
    case 4:
        Set(e0, v[0], v[0], v[0], v[2]);
        Set(e1, v[1], v[1], v[1], v[3]);
        break;
    case 5:
        BitTransferSigned(&v[1], &v[0]);
        BitTransferSigned(&v[3], &v[2]);
        Set(e0, v[0], v[0], v[0], v[2]);
        Set(e1, v[0] + v[1], v[0] + v[1], v[0] + v[1], v[2] + v[3]);
        break;
    case 6:
        Set(e0, (v[0] * v[3]) >> 8, (v[1] * v[3]) >> 8, (v[2] * v[3]) >> 8, 255);
        Set(e1, v[0], v[1], v[2], 255);
        break;
    case 8:
        if (v[1] + v[3] + v[5] >= v[0] + v[2] + v[4]) {
            Set(e0, v[0], v[2], v[4], 255);
            Set(e1, v[1], v[3], v[5], 255);
        } else {
            BlueContract(e0, v[1], v[3], v[5], 255);
            BlueContract(e1, v[0], v[2], v[4], 255);
        }
        break;
    case 9:
        BitTransferSigned(&v[1], &v[0]);
        BitTransferSigned(&v[3], &v[2]);
        BitTransferSigned(&v[5], &v[4]);
        if (v[1] + v[3] + v[5] >= 0) {
            Set(e0, v[0], v[2], v[4], 255);
            Set(e1, v[0] + v[1], v[2] + v[3], v[4] + v[5], 255);
        } else {
            BlueContract(e0, v[0] + v[1], v[2] + v[3], v[4] + v[5], 255);
            BlueContract(e1, v[0], v[2], v[4], 255);
        }
        break;
    case 10:
        Set(e0, (v[0] * v[3]) >> 8, (v[1] * v[3]) >> 8, (v[2] * v[3]) >> 8, v[4]);
        Set(e1, v[0], v[1], v[2], v[5]);
        break;
    case 12:
        if (v[1] + v[3] + v[5] >= v[0] + v[2] + v[4]) {
            Set(e0, v[0], v[2], v[4], v[6]);
            Set(e1, v[1], v[3], v[5], v[7]);
        } else {
            BlueContract(e0, v[1], v[3], v[5], v[7]);
            BlueContract(e1, v[0], v[2], v[4], v[6]);
        }
        break;
    case 13:
        BitTransferSigned(&v[1], &v[0]);
        BitTransferSigned(&v[3], &v[2]);
        BitTransferSigned(&v[5], &v[4]);
        BitTransferSigned(&v[7], &v[6]);
        if (v[1] + v[3] + v[5] >= 0) {
            Set(e0, v[0], v[2], v[4], v[6]);
            Set(e1, v[0] + v[1], v[2] + v[3], v[4] + v[5], v[6] + v[7]);
        } else {
            BlueContract(e0, v[0] + v[1], v[2] + v[3], v[4] + v[5], v[6] + v[7]);
            BlueContract(e1, v[0], v[2], v[4], v[6]);
        }
        break;
    default:
        Set(e0, 255, 0, 255, 255);
        Set(e1, 255, 0, 255, 255);
        break;
    }
    for (uint32_t c = 0; c < 4u; ++c) {
        e0[c] = Clamp255(e0[c]);
        e1[c] = Clamp255(e1[c]);
    }
    return true;
}

// ---- Partition selection (23.19) ----

static uint32_t Hash52(uint32_t p) {
    p ^= p >> 15;
    p -= p << 17;
    p += p << 7;
    p += p << 4;
    p ^= p >> 5;
    p += p << 16;
    p ^= p >> 7;
    p ^= p >> 3;
    p ^= p << 6;
    p ^= p >> 17;
    return p;
}

static uint32_t SelectPartition(uint32_t seed, uint32_t x, uint32_t y, uint32_t z,
                                uint32_t count, bool small_block) {
    if (small_block) {
        x <<= 1;
        y <<= 1;
        z <<= 1;
    }
    seed += (count - 1u) * 1024u;
    const uint32_t rnum = Hash52(seed);
    uint32_t s[13];
    s[1] = rnum & 0xFu;
    s[2] = (rnum >> 4) & 0xFu;
    s[3] = (rnum >> 8) & 0xFu;
    s[4] = (rnum >> 12) & 0xFu;
    s[5] = (rnum >> 16) & 0xFu;
    s[6] = (rnum >> 20) & 0xFu;
    s[7] = (rnum >> 24) & 0xFu;
    s[8] = (rnum >> 28) & 0xFu;
    s[9] = (rnum >> 18) & 0xFu;
    s[10] = (rnum >> 22) & 0xFu;
    s[11] = (rnum >> 26) & 0xFu;
    s[12] = ((rnum >> 30) | (rnum << 2)) & 0xFu;
    for (uint32_t i = 1; i <= 12u; ++i) {
        s[i] *= s[i];
    }
    uint32_t sh1;
    uint32_t sh2;
    if (seed & 1u) {
        sh1 = (seed & 2u) ? 4u : 5u;
        sh2 = count == 3u ? 6u : 5u;
    } else {
        sh1 = count == 3u ? 6u : 5u;
        sh2 = (seed & 2u) ? 4u : 5u;
    }
    const uint32_t sh3 = (seed & 0x10u) ? sh1 : sh2;
    s[1] >>= sh1;
    s[2] >>= sh2;
    s[3] >>= sh1;
    s[4] >>= sh2;
    s[5] >>= sh1;
    s[6] >>= sh2;
    s[7] >>= sh1;
    s[8] >>= sh2;
    s[9] >>= sh3;
    s[10] >>= sh3;
    s[11] >>= sh3;
    s[12] >>= sh3;
    uint32_t a = s[1] * x + s[2] * y + s[11] * z + (rnum >> 14);
    uint32_t b = s[3] * x + s[4] * y + s[12] * z + (rnum >> 10);
    uint32_t c = s[5] * x + s[6] * y + s[9] * z + (rnum >> 6);
    uint32_t d = s[7] * x + s[8] * y + s[10] * z + (rnum >> 2);
    a &= 0x3Fu;
    b &= 0x3Fu;
    c &= 0x3Fu;
    d &= 0x3Fu;
    if (count < 4u) {
        d = 0;
    }
    if (count < 3u) {
        c = 0;
    }
    if (a >= b && a >= c && a >= d) {
        return 0;
    }
    if (b >= c && b >= d) {
        return 1;
    }
    if (c >= d) {
        return 2;
    }
    return 3;
}

// ---- The block (23.8 onwards) ----

static const uint32_t ENDPOINT_LADDER[] = { 256, 192, 160, 128, 96, 80, 64, 48, 40,
                                            32,  24,  20,  16,  12, 10, 8,  6 };

typedef struct {
    AstcBlockInfo info;
    uint32_t weights[2][ASTC_MAX_TEXELS]; // per texel after infill, per plane
    uint32_t ccs;
    int32_t e0[4][4];
    int32_t e1[4][4];
    uint32_t partition_index;
    uint16_t constant[4];
} Decoded;

static AstcBlockKind Decode(const uint8_t block[16], uint32_t width, uint32_t height,
                            Decoded *d) {
    memset(d, 0, sizeof(*d));
    AstcBlockInfo *const info = &d->info;
    const uint32_t mode = Bits(block, 0, 11);
    if ((mode & 0x1FFu) == 0x1FCu) {
        if ((mode & 0x200u) != 0u || Bits(block, 10, 2) != 3u) {
            return info->kind = ASTC_BLOCK_ERROR;
        }
        const uint32_t s0 = Bits(block, 12, 13);
        const uint32_t s1 = Bits(block, 25, 13);
        const uint32_t t0 = Bits(block, 38, 13);
        const uint32_t t1 = Bits(block, 51, 13);
        const bool all_ones = s0 == 0x1FFFu && s1 == 0x1FFFu && t0 == 0x1FFFu && t1 == 0x1FFFu;
        if (!all_ones && (s0 >= s1 || t0 >= t1)) {
            return info->kind = ASTC_BLOCK_ERROR;
        }
        for (uint32_t c = 0; c < 4u; ++c) {
            d->constant[c] = (uint16_t)Bits(block, 64u + 16u * c, 16);
        }
        return info->kind = ASTC_BLOCK_CONSTANT;
    }

    BlockMode bm;
    if (!DecodeBlockMode(mode, &bm)) {
        return info->kind = ASTC_BLOCK_ERROR;
    }
    const uint32_t planes = bm.dual ? 2u : 1u;
    const uint32_t grid = bm.grid_width * bm.grid_height;
    const uint32_t weight_count = grid * planes;
    const uint32_t weight_bits = IseBitCount(weight_count, bm.levels);
    if (bm.grid_width > width || bm.grid_height > height || weight_count > 64u ||
        weight_bits < 24u || weight_bits > 96u) {
        return info->kind = ASTC_BLOCK_ERROR;
    }
    info->grid_width = bm.grid_width;
    info->grid_height = bm.grid_height;
    info->dual_plane = bm.dual;
    info->weight_levels = bm.levels;

    const uint32_t partitions = Bits(block, 11, 2) + 1u;
    info->partitions = partitions;
    if (bm.dual && partitions == 4u) {
        return info->kind = ASTC_BLOCK_ERROR;
    }

    uint32_t below_weights = 128u - weight_bits;
    uint32_t extra_cem_bits = 0;
    uint32_t modes[4] = { 0, 0, 0, 0 };
    uint32_t colour_start;
    if (partitions == 1u) {
        modes[0] = Bits(block, 13, 4);
        colour_start = 17;
    } else {
        d->partition_index = Bits(block, 13, 10);
        const uint32_t low = Bits(block, 23, 6);
        colour_start = 29;
        if ((low & 3u) == 0u) {
            for (uint32_t p = 0; p < partitions; ++p) {
                modes[p] = low >> 2;
            }
        } else {
            extra_cem_bits = 3u * partitions - 4u;
            const uint32_t encoded =
                low | (Bits(block, below_weights - extra_cem_bits, extra_cem_bits) << 6);
            const uint32_t base = (encoded & 3u) - 1u;
            uint32_t pos = 2;
            for (uint32_t p = 0; p < partitions; ++p) {
                modes[p] = (((encoded >> pos) & 1u) + base) << 2;
                ++pos;
            }
            for (uint32_t p = 0; p < partitions; ++p) {
                modes[p] |= (encoded >> pos) & 3u;
                pos += 2;
            }
        }
    }
    for (uint32_t p = 0; p < partitions; ++p) {
        info->endpoint_modes[p] = modes[p];
    }
    if (bm.dual) {
        d->ccs = Bits(block, below_weights - extra_cem_bits - 2u, 2);
    }

    uint32_t value_count = 0;
    for (uint32_t p = 0; p < partitions; ++p) {
        value_count += ((modes[p] >> 2) + 1u) * 2u;
    }
    if (value_count > 18u) {
        return info->kind = ASTC_BLOCK_ERROR;
    }
    int32_t colour_bits = (int32_t)below_weights - (int32_t)extra_cem_bits -
                          (bm.dual ? 2 : 0) - (int32_t)colour_start;
    uint32_t endpoint_levels = 0;
    for (uint32_t i = 0; i < sizeof(ENDPOINT_LADDER) / sizeof(ENDPOINT_LADDER[0]); ++i) {
        if ((int32_t)IseBitCount(value_count, ENDPOINT_LADDER[i]) <= colour_bits) {
            endpoint_levels = ENDPOINT_LADDER[i];
            break;
        }
    }
    if (endpoint_levels == 0u) {
        return info->kind = ASTC_BLOCK_ERROR;
    }
    info->endpoint_levels = endpoint_levels;

    // Endpoint values, in the order the partitions' modes consume them.
    uint32_t digits[64];
    uint32_t low_bits[64];
    // Bits past the end of a sequence read as nought, so a last partial trit or quint group
    // takes nothing from the fields that follow it.
    Stream colour = { block, colour_start, false,
                      colour_start + IseBitCount(value_count, endpoint_levels) };
    DecodeIse(&colour, value_count, endpoint_levels, digits, low_bits);
    uint32_t colour_values[18];
    for (uint32_t i = 0; i < value_count; ++i) {
        colour_values[i] = UnquantizeColour(digits[i], low_bits[i], endpoint_levels);
    }
    uint32_t next = 0;
    for (uint32_t p = 0; p < partitions; ++p) {
        uint32_t values[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        const uint32_t n = ((modes[p] >> 2) + 1u) * 2u;
        for (uint32_t i = 0; i < n; ++i) {
            values[i] = colour_values[next++];
        }
        if (!DecodeEndpoints(modes[p], values, d->e0[p], d->e1[p])) {
            return info->kind = ASTC_BLOCK_ERROR;
        }
    }

    // Weights, read downwards from the top of the block, then infilled onto the texels.
    Stream stream = { block, 0, true, weight_bits };
    DecodeIse(&stream, weight_count, bm.levels, digits, low_bits);
    uint32_t unquantized[2][64];
    Ise wise = { 0, 0, 0 };
    IseOf(bm.levels, &wise);
    for (uint32_t i = 0; i < weight_count; ++i) {
        const uint32_t value = (digits[i] << wise.bits) | low_bits[i];
        unquantized[i % planes][i / planes] = AstcUnquantizeWeight(value, bm.levels);
    }
    const uint32_t ds = (1024u + width / 2u) / (width - 1u);
    const uint32_t dt = (1024u + height / 2u) / (height - 1u);
    for (uint32_t plane = 0; plane < planes; ++plane) {
        for (uint32_t t = 0; t < height; ++t) {
            for (uint32_t s = 0; s < width; ++s) {
                const uint32_t gs = (ds * s * (bm.grid_width - 1u) + 32u) >> 6;
                const uint32_t gt = (dt * t * (bm.grid_height - 1u) + 32u) >> 6;
                const uint32_t js = gs >> 4;
                const uint32_t fs = gs & 0xFu;
                const uint32_t jt = gt >> 4;
                const uint32_t ft = gt & 0xFu;
                const uint32_t v0 = js + jt * bm.grid_width;
                const uint32_t w11 = (fs * ft + 8u) >> 4;
                const uint32_t w10 = ft - w11;
                const uint32_t w01 = fs - w11;
                const uint32_t w00 = 16u - fs - ft + w11;
                const uint32_t *const w = unquantized[plane];
                const uint32_t p00 = w[v0];
                const uint32_t p01 = v0 + 1u < grid ? w[v0 + 1u] : 0u;
                const uint32_t p10 = v0 + bm.grid_width < grid ? w[v0 + bm.grid_width] : 0u;
                const uint32_t p11 =
                    v0 + bm.grid_width + 1u < grid ? w[v0 + bm.grid_width + 1u] : 0u;
                d->weights[plane][t * width + s] =
                    (p00 * w00 + p01 * w01 + p10 * w10 + p11 * w11 + 8u) >> 4;
            }
        }
    }
    return info->kind = ASTC_BLOCK_NORMAL;
}

AstcBlockKind AstcDecodeBlock(const uint8_t block[16], uint32_t width, uint32_t height,
                              uint16_t texels[][4]) {
    static Decoded d;
    const AstcBlockKind kind = Decode(block, width, height, &d);
    const bool small_block = width * height < 31u;
    for (uint32_t t = 0; t < height; ++t) {
        for (uint32_t s = 0; s < width; ++s) {
            uint16_t *const out = texels[t * width + s];
            if (kind == ASTC_BLOCK_ERROR) {
                out[0] = 0xFFFF;
                out[1] = 0;
                out[2] = 0xFFFF;
                out[3] = 0xFFFF;
                continue;
            }
            if (kind == ASTC_BLOCK_CONSTANT) {
                memcpy(out, d.constant, sizeof(d.constant));
                continue;
            }
            const uint32_t p = d.info.partitions == 1u
                                   ? 0u
                                   : SelectPartition(d.partition_index, s, t, 0,
                                                     d.info.partitions, small_block);
            for (uint32_t c = 0; c < 4u; ++c) {
                const uint32_t plane = d.info.dual_plane && c == d.ccs ? 1u : 0u;
                const uint32_t w = d.weights[plane][t * width + s];
                const uint32_t c0 = (uint32_t)d.e0[p][c] * 257u;
                const uint32_t c1 = (uint32_t)d.e1[p][c] * 257u;
                out[c] = (uint16_t)((c0 * (64u - w) + c1 * w + 32u) >> 6);
            }
        }
    }
    return kind;
}

void AstcDescribeBlock(const uint8_t block[16], uint32_t width, uint32_t height,
                       AstcBlockInfo *info) {
    static Decoded d;
    Decode(block, width, height, &d);
    *info = d.info;
}

void AstcUnorm8Answers(uint16_t c, uint8_t *rounded, uint8_t *truncated) {
    *rounded = (uint8_t)(((uint32_t)c * 255u + 32767u) / 65535u);
    *truncated = (uint8_t)(c >> 8);
}
