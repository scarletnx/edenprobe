// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <math.h>

#include "blendref.h"

float BlendUnormToFloat(uint8_t value) {
    return (float)value / 255.0f;
}

uint8_t BlendFloatToUnorm(float value) {
    // Clamped first, because a fixed point target holds nothing outside its range and the
    // specification says so: two of the cases below are entirely about that clamp.
    float clamped = value;
    if (!(clamped > 0.0f)) {
        clamped = 0.0f;
    }
    if (clamped > 1.0f) {
        clamped = 1.0f;
    }
    // Round to nearest. Where the halfway point falls is left to the implementation by the
    // specification, so every case here is built to land nowhere near one, and the reference
    // suite measures how near.
    return (uint8_t)floorf(clamped * 255.0f + 0.5f);
}

static float FactorValue(BlendFactor factor, const float source[4], const float constant[4],
                         int channel) {
    switch (factor) {
    case BLEND_FACTOR_ZERO:
        return 0.0f;
    case BLEND_FACTOR_ONE:
        return 1.0f;
    case BLEND_FACTOR_SOURCE_ALPHA:
        return source[3];
    case BLEND_FACTOR_INVERSE_SOURCE_ALPHA:
        return 1.0f - source[3];
    case BLEND_FACTOR_CONSTANT:
        return constant[channel];
    }
    return 0.0f;
}

void BlendReferenceScaled(const BlendSpec *spec, const uint8_t source[4],
                          const uint8_t destination[4], float out[4]) {
    float s[4];
    float d[4];
    for (int i = 0; i < 4; ++i) {
        s[i] = BlendUnormToFloat(source[i]);
        d[i] = BlendUnormToFloat(destination[i]);
    }

    for (int channel = 0; channel < 4; ++channel) {
        if (!spec->enabled) {
            out[channel] = s[channel] * 255.0f;
            continue;
        }

        const BlendOp op = channel < 3 ? spec->color_op : spec->alpha_op;
        const BlendFactor source_factor =
            channel < 3 ? spec->color_source : spec->alpha_source;
        const BlendFactor destination_factor =
            channel < 3 ? spec->color_destination : spec->alpha_destination;

        float result;
        if (op == BLEND_OP_MIN) {
            result = s[channel] < d[channel] ? s[channel] : d[channel];
        } else if (op == BLEND_OP_MAX) {
            result = s[channel] > d[channel] ? s[channel] : d[channel];
        } else {
            const float weighted_source =
                s[channel] * FactorValue(source_factor, s, spec->constant, channel);
            const float weighted_destination =
                d[channel] * FactorValue(destination_factor, s, spec->constant, channel);
            if (op == BLEND_OP_ADD) {
                result = weighted_source + weighted_destination;
            } else if (op == BLEND_OP_SUBTRACT) {
                result = weighted_source - weighted_destination;
            } else {
                result = weighted_destination - weighted_source;
            }
        }

        float clamped = result;
        if (!(clamped > 0.0f)) {
            clamped = 0.0f;
        }
        if (clamped > 1.0f) {
            clamped = 1.0f;
        }
        out[channel] = clamped * 255.0f;
    }
}

void BlendReference(const BlendSpec *spec, const uint8_t source[4], const uint8_t destination[4],
                    uint8_t out[4]) {
    float scaled[4];
    BlendReferenceScaled(spec, source, destination, scaled);
    for (int i = 0; i < 4; ++i) {
        out[i] = (uint8_t)floorf(scaled[i] + 0.5f);
    }
}

// The cases, here rather than in the suite that draws them, so the reference suite can walk the
// same table with no device and check three things: that the arithmetic in the comment is the
// arithmetic the reference does, that no result lands near the halfway point between two bytes,
// and that no case asks for an alpha destination factor of its own.
//
// That last one is a limit of the graphics library and not of the hardware, which sends the
// colour destination factor in the alpha destination register too, so a case built on the
// difference would fail a machine that had done nothing wrong. The alpha channel below differs
// from the colour one by its equation or its source factor instead, and the blend suite measures
// whether either reaches the unit rather than assuming it.
static const BlendCase CASES[] = {
    {
        "blending off replaces the destination",
        { false, BLEND_OP_ADD, BLEND_FACTOR_ONE, BLEND_FACTOR_ZERO, BLEND_OP_ADD,
          BLEND_FACTOR_ONE, BLEND_FACTOR_ZERO, { 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0x11, 0x22, 0x33, 0x44 },
        { 0x99, 0xAA, 0xBB, 0xCC },
        "the source, unaltered: 11223344",
    },
    {
        "adding with both factors one",
        // The alpha channel carries the colour equation and its own source factor, nought where
        // the colour's is one, so a machine that took that factor from the colour state hands
        // back 96 in the alpha rather than 32.
        { true, BLEND_OP_ADD, BLEND_FACTOR_ONE, BLEND_FACTOR_ONE, BLEND_OP_ADD,
          BLEND_FACTOR_ZERO, BLEND_FACTOR_ONE, { 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0x20, 0x20, 0x20, 0x40 },
        { 0x40, 0x60, 0x80, 0x20 },
        "32+64, 32+96, 32+128, and an alpha channel weighting its own source to nothing, which "
        "leaves the destination alpha where the colour channels added to theirs: 6080a020",
    },
    {
        "adding clamps at the top of the range",
        { true, BLEND_OP_ADD, BLEND_FACTOR_ONE, BLEND_FACTOR_ONE, BLEND_OP_ADD,
          BLEND_FACTOR_ONE, BLEND_FACTOR_ONE, { 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0xC0, 0x40, 0xFF, 0x80 },
        { 0x80, 0x20, 0x02, 0x30 },
        "192+128 and 255+2 pass the top and stop there, 64+32 and 128+48 do not: ff60ffb0",
    },
    {
        "subtracting the destination from the source",
        { true, BLEND_OP_SUBTRACT, BLEND_FACTOR_ONE, BLEND_FACTOR_ONE, BLEND_OP_SUBTRACT,
          BLEND_FACTOR_ONE, BLEND_FACTOR_ONE, { 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0x60, 0xFF, 0x90, 0xC0 },
        { 0x20, 0x30, 0x10, 0x40 },
        "96-32, 255-48, 144-16, 192-64: 40cf8080",
    },
    {
        "subtracting clamps at the bottom of the range",
        { true, BLEND_OP_SUBTRACT, BLEND_FACTOR_ONE, BLEND_FACTOR_ONE, BLEND_OP_SUBTRACT,
          BLEND_FACTOR_ONE, BLEND_FACTOR_ONE, { 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0x20, 0x90, 0x00, 0x70 },
        { 0x60, 0x30, 0x40, 0x20 },
        "32-96 and 0-64 stop at nought, 144-48 and 112-32 do not: 00600050",
    },
    {
        "subtracting the source from the destination",
        { true, BLEND_OP_REVERSE_SUBTRACT, BLEND_FACTOR_ONE, BLEND_FACTOR_ONE,
          BLEND_OP_REVERSE_SUBTRACT, BLEND_FACTOR_ONE, BLEND_FACTOR_ONE,
          { 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0x20, 0x30, 0x10, 0x40 },
        { 0x60, 0xFF, 0x90, 0xC0 },
        "the same four differences with the operands the other way round: 40cf8080",
    },
    {
        "the minimum ignores the factors",
        { true, BLEND_OP_MIN, BLEND_FACTOR_ZERO, BLEND_FACTOR_ZERO, BLEND_OP_MIN,
          BLEND_FACTOR_ZERO, BLEND_FACTOR_ZERO, { 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0x30, 0xF0, 0x11, 0x80 },
        { 0x70, 0x10, 0x22, 0x40 },
        "both factors are zero and the answer is still the smaller of each pair: 30101140",
    },
    {
        "the maximum ignores the factors",
        { true, BLEND_OP_MAX, BLEND_FACTOR_ZERO, BLEND_FACTOR_ZERO, BLEND_OP_MAX,
          BLEND_FACTOR_ZERO, BLEND_FACTOR_ZERO, { 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0x30, 0xF0, 0x11, 0x80 },
        { 0x70, 0x10, 0x22, 0x40 },
        "the same, the larger of each pair: 70f02280",
    },
    {
        "the source alpha weights both sides",
        // Here the two differ by the equation instead: the colour adds its weighted sides, the
        // alpha keeps the larger of its unweighted ones. They carry the same pair of factors on
        // purpose, so the case rests on no factor the graphics library cannot send. A machine
        // running the alpha through the colour equation gives 72 rather than 128.
        { true, BLEND_OP_ADD, BLEND_FACTOR_SOURCE_ALPHA, BLEND_FACTOR_INVERSE_SOURCE_ALPHA,
          BLEND_OP_MAX, BLEND_FACTOR_SOURCE_ALPHA, BLEND_FACTOR_INVERSE_SOURCE_ALPHA,
          { 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0xFF, 0x00, 0xFF, 0x80 },
        { 0x00, 0xFF, 0x00, 0x10 },
        "128/255 of white over nothing is 128, and 127/255 of white under nothing is 127: the "
        "two are not complements of each other and the case says so. The alpha keeps the larger "
        "of 128 and 16 under an equation of its own: 807f8080",
    },
    {
        "the blend constant is a factor of its own",
        { true, BLEND_OP_ADD, BLEND_FACTOR_CONSTANT, BLEND_FACTOR_ZERO, BLEND_OP_ADD,
          BLEND_FACTOR_CONSTANT, BLEND_FACTOR_ZERO, { 0.5f, 0.25f, 0.75f, 0.125f } },
        { 0x80, 0xC0, 0x40, 0x80 },
        { 0x11, 0x22, 0x33, 0x44 },
        // Four different components on purpose. There is a second factor that broadcasts the
        // constant's alpha to all four channels, and with one constant repeated the two are the
        // same number everywhere and an implementation that wired one to the other would pass.
        // With these, that mistake gives 10 18 08 10 instead.
        "a half of 128, a quarter of 192, three quarters of 64 and an eighth of 128, with the "
        "destination weighted to nothing: 40303010",
    },
    {
        "a source factor of zero leaves the destination",
        { true, BLEND_OP_ADD, BLEND_FACTOR_ZERO, BLEND_FACTOR_ONE, BLEND_OP_ADD,
          BLEND_FACTOR_ZERO, BLEND_FACTOR_ONE, { 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0xAB, 0xCD, 0xEF, 0x12 },
        { 0x10, 0x20, 0x30, 0x40 },
        "the destination, unaltered, with a source that looks nothing like it: 10203040",
    },
};

size_t BlendCaseCount(void) {
    return sizeof(CASES) / sizeof(CASES[0]);
}

const BlendCase *BlendCaseAt(size_t index) {
    return index < BlendCaseCount() ? &CASES[index] : NULL;
}
