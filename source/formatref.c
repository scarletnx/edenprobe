// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <string.h>

#include "formatref.h"

#define CH(shift, bits, kind) { (shift), (bits), (kind) }
#define NONE { 0, 0, FORMAT_UNUSED }

// Four channels of one kind and one width, byte aligned, red first.
#define RGBA(name, width, kind, output)                                                          \
    { name, (uint8_t)((width) / 2u), output,                                                     \
      { CH(0, width, kind), CH(width, width, kind), CH(2 * (width), width, kind),                \
        CH(3 * (width), width, kind) }, NONE }
// The same three, and the fourth stored but not defined.
#define RGBX(name, width, kind, output)                                                          \
    { name, (uint8_t)((width) / 2u), output,                                                     \
      { CH(0, width, kind), CH(width, width, kind), CH(2 * (width), width, kind), NONE },        \
      CH(3 * (width), width, FORMAT_UNDEFINED) }
#define RG(name, width, kind, output)                                                            \
    { name, (uint8_t)((width) / 4u), output,                                                     \
      { CH(0, width, kind), CH(width, width, kind), NONE, NONE }, NONE }
#define R(name, width, kind, output)                                                             \
    { name, (uint8_t)((width) / 8u), output, { CH(0, width, kind), NONE, NONE, NONE }, NONE }

// The widths above are in bits and the byte counts come from them: four channels of width w are
// w / 2 bytes, two are w / 4, one is w / 8.
const FormatInfo FORMAT_TABLE[] = {
    R("R8_Unorm", 8, FORMAT_UNORM, FORMAT_OUTPUT_FLOAT),
    R("R8_Snorm", 8, FORMAT_SNORM, FORMAT_OUTPUT_FLOAT),
    R("R8_Uint", 8, FORMAT_UINT, FORMAT_OUTPUT_UINT),
    R("R8_Sint", 8, FORMAT_SINT, FORMAT_OUTPUT_SINT),
    R("R16_Float", 16, FORMAT_FLOAT, FORMAT_OUTPUT_FLOAT),
    R("R16_Unorm", 16, FORMAT_UNORM, FORMAT_OUTPUT_FLOAT),
    R("R16_Snorm", 16, FORMAT_SNORM, FORMAT_OUTPUT_FLOAT),
    R("R16_Uint", 16, FORMAT_UINT, FORMAT_OUTPUT_UINT),
    R("R16_Sint", 16, FORMAT_SINT, FORMAT_OUTPUT_SINT),
    R("R32_Float", 32, FORMAT_FLOAT, FORMAT_OUTPUT_FLOAT),
    R("R32_Uint", 32, FORMAT_UINT, FORMAT_OUTPUT_UINT),
    R("R32_Sint", 32, FORMAT_SINT, FORMAT_OUTPUT_SINT),
    RG("RG8_Unorm", 8, FORMAT_UNORM, FORMAT_OUTPUT_FLOAT),
    RG("RG8_Snorm", 8, FORMAT_SNORM, FORMAT_OUTPUT_FLOAT),
    RG("RG8_Uint", 8, FORMAT_UINT, FORMAT_OUTPUT_UINT),
    RG("RG8_Sint", 8, FORMAT_SINT, FORMAT_OUTPUT_SINT),
    RG("RG16_Float", 16, FORMAT_FLOAT, FORMAT_OUTPUT_FLOAT),
    RG("RG16_Unorm", 16, FORMAT_UNORM, FORMAT_OUTPUT_FLOAT),
    RG("RG16_Snorm", 16, FORMAT_SNORM, FORMAT_OUTPUT_FLOAT),
    RG("RG16_Uint", 16, FORMAT_UINT, FORMAT_OUTPUT_UINT),
    RG("RG16_Sint", 16, FORMAT_SINT, FORMAT_OUTPUT_SINT),
    RG("RG32_Float", 32, FORMAT_FLOAT, FORMAT_OUTPUT_FLOAT),
    RG("RG32_Uint", 32, FORMAT_UINT, FORMAT_OUTPUT_UINT),
    RG("RG32_Sint", 32, FORMAT_SINT, FORMAT_OUTPUT_SINT),
    RGBA("RGBA8_Unorm", 8, FORMAT_UNORM, FORMAT_OUTPUT_FLOAT),
    RGBA("RGBA8_Snorm", 8, FORMAT_SNORM, FORMAT_OUTPUT_FLOAT),
    RGBA("RGBA8_Uint", 8, FORMAT_UINT, FORMAT_OUTPUT_UINT),
    RGBA("RGBA8_Sint", 8, FORMAT_SINT, FORMAT_OUTPUT_SINT),
    RGBA("RGBA16_Float", 16, FORMAT_FLOAT, FORMAT_OUTPUT_FLOAT),
    RGBA("RGBA16_Unorm", 16, FORMAT_UNORM, FORMAT_OUTPUT_FLOAT),
    RGBA("RGBA16_Snorm", 16, FORMAT_SNORM, FORMAT_OUTPUT_FLOAT),
    RGBA("RGBA16_Uint", 16, FORMAT_UINT, FORMAT_OUTPUT_UINT),
    RGBA("RGBA16_Sint", 16, FORMAT_SINT, FORMAT_OUTPUT_SINT),
    RGBA("RGBA32_Float", 32, FORMAT_FLOAT, FORMAT_OUTPUT_FLOAT),
    RGBA("RGBA32_Uint", 32, FORMAT_UINT, FORMAT_OUTPUT_UINT),
    RGBA("RGBA32_Sint", 32, FORMAT_SINT, FORMAT_OUTPUT_SINT),
    { "RGBX8_Unorm_sRGB", 4, FORMAT_OUTPUT_FLOAT,
      { CH(0, 8, FORMAT_SRGB), CH(8, 8, FORMAT_SRGB), CH(16, 8, FORMAT_SRGB), NONE },
      CH(24, 8, FORMAT_UNDEFINED) },
    { "RGBA8_Unorm_sRGB", 4, FORMAT_OUTPUT_FLOAT,
      { CH(0, 8, FORMAT_SRGB), CH(8, 8, FORMAT_SRGB), CH(16, 8, FORMAT_SRGB),
        CH(24, 8, FORMAT_UNORM) },
      NONE },
    { "RGB10A2_Unorm", 4, FORMAT_OUTPUT_FLOAT,
      { CH(0, 10, FORMAT_UNORM), CH(10, 10, FORMAT_UNORM), CH(20, 10, FORMAT_UNORM),
        CH(30, 2, FORMAT_UNORM) },
      NONE },
    { "RGB10A2_Uint", 4, FORMAT_OUTPUT_UINT,
      { CH(0, 10, FORMAT_UINT), CH(10, 10, FORMAT_UINT), CH(20, 10, FORMAT_UINT),
        CH(30, 2, FORMAT_UINT) },
      NONE },
    { "RG11B10_Float", 4, FORMAT_OUTPUT_FLOAT,
      { CH(0, 11, FORMAT_UFLOAT), CH(11, 11, FORMAT_UFLOAT), CH(22, 10, FORMAT_UFLOAT), NONE },
      NONE },
    RGBX("RGBX8_Unorm", 8, FORMAT_UNORM, FORMAT_OUTPUT_FLOAT),
    RGBX("RGBX8_Snorm", 8, FORMAT_SNORM, FORMAT_OUTPUT_FLOAT),
    RGBX("RGBX8_Uint", 8, FORMAT_UINT, FORMAT_OUTPUT_UINT),
    RGBX("RGBX8_Sint", 8, FORMAT_SINT, FORMAT_OUTPUT_SINT),
    RGBX("RGBX16_Float", 16, FORMAT_FLOAT, FORMAT_OUTPUT_FLOAT),
    RGBX("RGBX16_Unorm", 16, FORMAT_UNORM, FORMAT_OUTPUT_FLOAT),
    RGBX("RGBX16_Snorm", 16, FORMAT_SNORM, FORMAT_OUTPUT_FLOAT),
    RGBX("RGBX16_Uint", 16, FORMAT_UINT, FORMAT_OUTPUT_UINT),
    RGBX("RGBX16_Sint", 16, FORMAT_SINT, FORMAT_OUTPUT_SINT),
    RGBX("RGBX32_Float", 32, FORMAT_FLOAT, FORMAT_OUTPUT_FLOAT),
    RGBX("RGBX32_Uint", 32, FORMAT_UINT, FORMAT_OUTPUT_UINT),
    RGBX("RGBX32_Sint", 32, FORMAT_SINT, FORMAT_OUTPUT_SINT),
    // The blue first formats: the channels in the table are still red, green, blue, alpha, and
    // the shifts are where the name puts them.
    { "BGR565_Unorm", 2, FORMAT_OUTPUT_FLOAT,
      { CH(11, 5, FORMAT_UNORM), CH(5, 6, FORMAT_UNORM), CH(0, 5, FORMAT_UNORM), NONE },
      NONE },
    { "BGR5_Unorm", 2, FORMAT_OUTPUT_FLOAT,
      { CH(10, 5, FORMAT_UNORM), CH(5, 5, FORMAT_UNORM), CH(0, 5, FORMAT_UNORM), NONE },
      CH(15, 1, FORMAT_UNDEFINED) },
    { "BGR5A1_Unorm", 2, FORMAT_OUTPUT_FLOAT,
      { CH(10, 5, FORMAT_UNORM), CH(5, 5, FORMAT_UNORM), CH(0, 5, FORMAT_UNORM),
        CH(15, 1, FORMAT_UNORM) },
      NONE },
    { "BGRX8_Unorm", 4, FORMAT_OUTPUT_FLOAT,
      { CH(16, 8, FORMAT_UNORM), CH(8, 8, FORMAT_UNORM), CH(0, 8, FORMAT_UNORM), NONE },
      CH(24, 8, FORMAT_UNDEFINED) },
    { "BGRA8_Unorm", 4, FORMAT_OUTPUT_FLOAT,
      { CH(16, 8, FORMAT_UNORM), CH(8, 8, FORMAT_UNORM), CH(0, 8, FORMAT_UNORM),
        CH(24, 8, FORMAT_UNORM) },
      NONE },
    { "BGRX8_Unorm_sRGB", 4, FORMAT_OUTPUT_FLOAT,
      { CH(16, 8, FORMAT_SRGB), CH(8, 8, FORMAT_SRGB), CH(0, 8, FORMAT_SRGB), NONE },
      CH(24, 8, FORMAT_UNDEFINED) },
    { "BGRA8_Unorm_sRGB", 4, FORMAT_OUTPUT_FLOAT,
      { CH(16, 8, FORMAT_SRGB), CH(8, 8, FORMAT_SRGB), CH(0, 8, FORMAT_SRGB),
        CH(24, 8, FORMAT_UNORM) },
      NONE },
};

const uint32_t FORMAT_COUNT = sizeof(FORMAT_TABLE) / sizeof(FORMAT_TABLE[0]);

// What the library's format table says about the rest of the colour formats: no colour surface
// format, so a render target of one is refused by its debug build and garbage in its release
// build. Named here so the report says what was left out and why, rather than going quiet.
const FormatRefused FORMAT_REFUSED[] = {
    { "RGB32_Float, RGB32_Uint, RGB32_Sint",
      "twelve byte pixels, which the render target unit has no surface format for" },
    { "RGBA4_Unorm, RGB5_Unorm, RGB5A1_Unorm, RGB565_Unorm, A5BGR5_Unorm",
      "sampling only: the library gives them no colour surface format; the blue first "
      "BGR565, BGR5 and BGR5A1 are the sixteen bit formats a target can have" },
    { "E5BGR9_Float", "sampling only: a shared exponent has no colour surface format" },
    { "the BC, ETC2 and ASTC formats", "compressed, sampling only" },
    { "S8, Z16, Z24X8, ZF32, Z24S8, ZF32_X24S8", "depth and stencil, the depth suite's" },
};

const uint32_t FORMAT_REFUSED_COUNT = sizeof(FORMAT_REFUSED) / sizeof(FORMAT_REFUSED[0]);

static uint32_t FloatWord(float value) {
    uint32_t word;
    memcpy(&word, &value, sizeof(word));
    return word;
}

static float WordFloat(uint32_t word) {
    float value;
    memcpy(&value, &word, sizeof(value));
    return value;
}

static uint32_t FieldMask(uint32_t bits) {
    return bits >= 32u ? 0xFFFFFFFFu : ((1u << bits) - 1u);
}

uint32_t FormatUnorm(double value, uint32_t bits) {
    const uint32_t largest = FieldMask(bits);
    // Written as "not above nought" so a NaN lands on nought rather than on whatever a cast of
    // it gives, though no case here writes one.
    if (!(value > 0.0)) {
        return 0u;
    }
    if (value >= 1.0) {
        return largest;
    }
    return (uint32_t)floor(value * (double)largest + 0.5);
}

uint32_t FormatSnorm(double value, uint32_t bits) {
    const int32_t largest = (int32_t)(FieldMask(bits - 1u));
    double clamped = value;
    if (!(clamped > -1.0)) {
        clamped = -1.0;
    }
    if (clamped > 1.0) {
        clamped = 1.0;
    }
    const double scaled = clamped * (double)largest;
    // To the nearest code, halves away from nought; the values drawn are never halves.
    const int32_t code = scaled < 0.0 ? -(int32_t)floor(-scaled + 0.5)
                                      : (int32_t)floor(scaled + 0.5);
    return (uint32_t)code & FieldMask(bits);
}

double FormatSrgbEncode(double linear) {
    if (!(linear > 0.0)) {
        return 0.0;
    }
    if (linear >= 1.0) {
        return 1.0;
    }
    // The curve the Vulkan and sRGB specifications give, in double precision.
    return linear <= 0.0031308 ? linear * 12.92 : 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
}

double FormatSrgbDecode(double encoded) {
    return encoded <= 0.04045 ? encoded / 12.92 : pow((encoded + 0.055) / 1.055, 2.4);
}

/// A non-negative value rounded to the nearest whole number, ties to even, written out rather than
/// left to rint: rint is one frintx, and under Eden that rounded 1024.5 up to 1025 with the
/// rounding mode at nearest, where the architecture and the host give 1024.
static double NearestEven(double value) {
    const double down = floor(value);
    const double fraction = value - down;
    if (fraction > 0.5) {
        return down + 1.0;
    }
    if (fraction < 0.5) {
        return down;
    }
    return fmod(down, 2.0) == 0.0 ? down : down + 1.0;
}

/// An unsigned float with a five bit exponent biased by fifteen and @p mantissa bits, rounded to
/// nearest with ties to even: binary16's magnitude, and the two small floats of RG11B10.
static uint32_t SmallFloat(double magnitude, uint32_t mantissa) {
    const uint32_t infinity = 0x1Fu << mantissa;
    if (isnan(magnitude)) {
        return infinity | (1u << (mantissa - 1u));
    }
    if (magnitude == 0.0) {
        return 0u;
    }
    // Below the smallest normal the step is fixed at two to the minus fourteen over the
    // mantissa's range.
    const double smallest_normal = ldexp(1.0, -14);
    if (magnitude < smallest_normal) {
        return (uint32_t)NearestEven(magnitude / ldexp(1.0, -14 - (int)mantissa));
    }
    int exponent = 0;
    const double fraction = frexp(magnitude, &exponent); // magnitude = fraction * 2^exponent
    // fraction is in [0.5, 1), so the significand with its leading one is fraction * 2.
    double significand = NearestEven(fraction * ldexp(1.0, (int)mantissa + 1));
    int biased = exponent - 1 + 15;
    if (significand >= ldexp(1.0, (int)mantissa + 1)) {
        significand = ldexp(1.0, (int)mantissa);
        ++biased;
    }
    if (biased >= 31) {
        return infinity;
    }
    return ((uint32_t)biased << mantissa) | ((uint32_t)significand - (1u << mantissa));
}

uint32_t FormatHalfFromFloat(float value) {
    const uint32_t sign = (FloatWord(value) >> 16) & 0x8000u;
    const double magnitude = fabs((double)value);
    if (isinf(value)) {
        return sign | 0x7C00u;
    }
    return sign | SmallFloat(magnitude, 10u);
}

float FormatFloatFromHalf(uint32_t half) {
    const uint32_t exponent = (half >> 10) & 0x1Fu;
    const uint32_t mantissa = half & 0x3FFu;
    double magnitude;
    if (exponent == 0u) {
        magnitude = ldexp((double)mantissa, -24);
    } else if (exponent == 31u) {
        magnitude = mantissa == 0u ? INFINITY : NAN;
    } else {
        magnitude = ldexp((double)(mantissa | 0x400u), (int)exponent - 25);
    }
    return (float)((half & 0x8000u) != 0u ? -magnitude : magnitude);
}

uint32_t FormatUfloatFromFloat(float value, uint32_t bits) {
    const uint32_t mantissa = bits - 5u;
    if (isnan(value)) {
        return SmallFloat(NAN, mantissa);
    }
    // No sign bit: a negative value, minus nought included, is nought.
    if (!(value > 0.0f)) {
        return 0u;
    }
    if (isinf(value)) {
        return 0x1Fu << mantissa;
    }
    return SmallFloat((double)value, mantissa);
}

float FormatFloatFromUfloat(uint32_t code, uint32_t bits) {
    const uint32_t mantissa_bits = bits - 5u;
    const uint32_t exponent = code >> mantissa_bits;
    const uint32_t mantissa = code & FieldMask(mantissa_bits);
    if (exponent == 0u) {
        return (float)ldexp((double)mantissa, -14 - (int)mantissa_bits);
    }
    if (exponent == 31u) {
        return mantissa == 0u ? INFINITY : NAN;
    }
    return (float)ldexp((double)(mantissa | (1u << mantissa_bits)),
                        (int)exponent - 15 - (int)mantissa_bits);
}

uint32_t FormatEncodeField(FormatKind kind, uint32_t bits, uint32_t word) {
    const float value = WordFloat(word);
    switch (kind) {
    case FORMAT_UNORM:
        return FormatUnorm((double)value, bits);
    case FORMAT_SNORM:
        return FormatSnorm((double)value, bits);
    case FORMAT_SRGB:
        return FormatUnorm(FormatSrgbEncode((double)value), bits);
    case FORMAT_UINT:
    case FORMAT_SINT:
        // Both keep the low bits of the word: every value drawn fits, and a signed one in two's
        // complement is its own low bits.
        return word & FieldMask(bits);
    case FORMAT_FLOAT:
        return bits == 32u ? word : FormatHalfFromFloat(value);
    case FORMAT_UFLOAT:
        return FormatUfloatFromFloat(value, bits);
    case FORMAT_UNUSED:
    case FORMAT_UNDEFINED:
        break;
    }
    return 0u;
}

double FormatCodeMargin(FormatKind kind, uint32_t bits, uint32_t word) {
    const double value = (double)WordFloat(word);
    double scaled;
    switch (kind) {
    case FORMAT_UNORM:
        if (!(value > 0.0) || value >= 1.0) {
            return 0.0;
        }
        scaled = value * (double)FieldMask(bits);
        break;
    case FORMAT_SNORM:
        if (!(value > -1.0) || value >= 1.0) {
            return 0.0;
        }
        scaled = value * (double)FieldMask(bits - 1u);
        break;
    case FORMAT_SRGB: {
        const double encoded = FormatSrgbEncode(value);
        if (encoded <= 0.0 || encoded >= 1.0) {
            return 0.0;
        }
        scaled = encoded * (double)FieldMask(bits);
        break;
    }
    default:
        return 0.0;
    }
    return fabs(scaled - floor(scaled + 0.5));
}

bool FormatCodeExact(FormatKind kind, uint32_t bits, uint32_t word) {
    const float value = WordFloat(word);
    float largest;
    if (kind == FORMAT_UNORM) {
        if (!(value > 0.0f) || value >= 1.0f) {
            return true;
        }
        largest = (float)FieldMask(bits);
    } else if (kind == FORMAT_SNORM) {
        if (!(value > -1.0f) || value >= 1.0f) {
            return true;
        }
        largest = (float)FieldMask(bits - 1u);
    } else {
        return true;
    }
    // volatile, so the product is rounded to single precision here and not kept wider or fused.
    volatile float product = value * largest;
    return product == floorf(product);
}

/// The nearest float to @p code over the largest code, and for a signed one the same with the
/// sign kept.
static uint32_t CodeWord(int32_t code, uint32_t largest) {
    return FloatWord((float)((double)code / (double)largest));
}

static int32_t SignExtend(uint32_t value, uint32_t bits) {
    if (bits >= 32u) {
        return (int32_t)value;
    }
    const uint32_t sign = 1u << (bits - 1u);
    return (int32_t)((value ^ sign) - sign);
}

// The integer patterns: the golden ratio's word times one to four, which sets high and low bits
// in every width here. The drawn value is the complement, so every stored bit changes.
static uint32_t IntegerPattern(uint32_t channel) {
    return 0x9E3779B9u * (channel + 1u);
}

static const float FLOAT_CLEAR[4] = { 0.5f, -1.25f, 1024.0f, -3.75f };
static const float UFLOAT_CLEAR[3] = { 0.5f, 1.25f, 3.0f };
static const float UFLOAT_DRAW[3] = { 1.5f, 96.0f, 0.25f };

// The sRGB codes the drawn red and green land on. Mid range and far from each other, and neither
// in the linear segment at the bottom of the curve.
#define SRGB_DRAWN_RED 188
#define SRGB_DRAWN_GREEN 60

static uint32_t ChannelValue(const FormatField *field, uint32_t channel, bool drawn) {
    const uint32_t bits = field->bits;
    switch ((FormatKind)field->kind) {
    case FORMAT_UNORM: {
        const int32_t largest = (int32_t)FieldMask(bits);
        if (!drawn) {
            // A fifth, two fifths, three fifths of the range, and alpha full.
            static const int32_t fifths[3] = { 1, 2, 3 };
            return channel == 3u ? CodeWord(largest, (uint32_t)largest)
                                 : CodeWord(largest * fifths[channel] / 5, (uint32_t)largest);
        }
        switch (channel) {
        case 0u:
            return CodeWord(largest - largest / 5, (uint32_t)largest);
        case 1u:
            return CodeWord(largest / 9, (uint32_t)largest);
        case 2u:
            return FloatWord(1.5f); // above the range, so the largest code
        default:
            return FloatWord(-0.5f); // below it, so nought
        }
    }
    case FORMAT_SNORM: {
        const int32_t largest = (int32_t)FieldMask(bits - 1u);
        if (!drawn) {
            switch (channel) {
            case 0u:
                return CodeWord(largest / 3, (uint32_t)largest);
            case 1u:
                return CodeWord(-(largest / 2), (uint32_t)largest);
            case 2u:
                return FloatWord(-1.0f);
            default:
                return FloatWord(1.0f);
            }
        }
        switch (channel) {
        case 0u:
            return CodeWord(-(largest * 4 / 5), (uint32_t)largest);
        case 1u:
            return CodeWord(largest / 7, (uint32_t)largest);
        case 2u:
            return FloatWord(1.5f);
        default:
            return FloatWord(-1.5f); // minus the largest positive code, not the code below it
        }
    }
    case FORMAT_SRGB:
        if (!drawn) {
            // Nought and one, which encode to themselves: whether a clear goes through the
            // curve or not, these land on the same code.
            return FloatWord(channel == 1u ? 1.0f : 0.0f);
        }
        switch (channel) {
        case 0u:
            return FloatWord((float)FormatSrgbDecode((double)SRGB_DRAWN_RED / 255.0));
        case 1u:
            return FloatWord((float)FormatSrgbDecode((double)SRGB_DRAWN_GREEN / 255.0));
        default:
            return FloatWord(1.5f);
        }
    case FORMAT_UINT:
        return (drawn ? ~IntegerPattern(channel) : IntegerPattern(channel)) & FieldMask(bits);
    case FORMAT_SINT: {
        const uint32_t pattern =
            (drawn ? ~IntegerPattern(channel) : IntegerPattern(channel)) & FieldMask(bits);
        return (uint32_t)SignExtend(pattern, bits);
    }
    case FORMAT_FLOAT:
        if (!drawn) {
            return FloatWord(FLOAT_CLEAR[channel]);
        }
        if (bits == 32u) {
            // Pi, a large negative, the smallest normal and an infinity: exponent fields at
            // both ends and in the middle, and no denormal, which a target may flush.
            static const float drawn32[4] = { 3.14159274f, -1.0e30f, 1.17549435e-38f,
                                              INFINITY };
            return FloatWord(drawn32[channel]);
        } else {
            // Exactly representable in binary16: the largest finite and the smallest normal.
            static const float drawn16[4] = { 1.5f, -2.25f, 65504.0f, 6.103515625e-05f };
            return FloatWord(drawn16[channel]);
        }
    case FORMAT_UFLOAT:
        return FloatWord(drawn ? UFLOAT_DRAW[channel] : UFLOAT_CLEAR[channel]);
    case FORMAT_UNUSED:
    case FORMAT_UNDEFINED:
        break;
    }
    return 0u;
}

static FormatValue MakeValue(const FormatInfo *format, bool drawn) {
    FormatValue value;
    for (uint32_t channel = 0; channel < 4u; ++channel) {
        const FormatField *field = &format->channel[channel];
        value.word[channel] =
            field->kind == FORMAT_UNUSED ? 0u : ChannelValue(field, channel, drawn);
    }
    return value;
}

FormatValue FormatClearValue(const FormatInfo *format) {
    return MakeValue(format, false);
}

FormatValue FormatDrawValue(const FormatInfo *format) {
    return MakeValue(format, true);
}

static void PlaceBits(uint8_t bytes[FORMAT_MAX_BYTES], uint8_t mask[FORMAT_MAX_BYTES],
                      uint32_t shift, uint32_t bits, uint32_t code) {
    for (uint32_t bit = 0; bit < bits; ++bit) {
        const uint32_t position = shift + bit;
        if (position >= FORMAT_MAX_BYTES * 8u) {
            return;
        }
        const uint8_t one = (uint8_t)(1u << (position % 8u));
        mask[position / 8u] |= one;
        if (((code >> bit) & 1u) != 0u) {
            bytes[position / 8u] |= one;
        }
    }
}

void FormatPack(const FormatInfo *format, const FormatValue *value,
                uint8_t bytes[FORMAT_MAX_BYTES], uint8_t mask[FORMAT_MAX_BYTES]) {
    memset(bytes, 0, FORMAT_MAX_BYTES);
    memset(mask, 0, FORMAT_MAX_BYTES);
    for (uint32_t channel = 0; channel < 4u; ++channel) {
        const FormatField *field = &format->channel[channel];
        if (field->kind == FORMAT_UNUSED || field->kind == FORMAT_UNDEFINED) {
            continue;
        }
        const uint32_t code =
            FormatEncodeField((FormatKind)field->kind, field->bits, value->word[channel]);
        PlaceBits(bytes, mask, field->shift, field->bits, code);
    }
}

void FormatQuadClip(float out[12]) {
    const float half = (float)FORMAT_TARGET_SIZE * 0.5f;
    const float left = (float)FORMAT_QUAD_X0 / half - 1.0f;
    const float right = (float)FORMAT_QUAD_X1 / half - 1.0f;
    // Clip space plus one is the top row, so the top of the quad is the larger y.
    const float top = 1.0f - (float)FORMAT_QUAD_Y0 / half;
    const float bottom = 1.0f - (float)FORMAT_QUAD_Y1 / half;
    const float corners[12] = { left, top, right, top, right, bottom,
                                left, top, right, bottom, left, bottom };
    memcpy(out, corners, sizeof(corners));
}

bool FormatInsideQuad(uint32_t x, uint32_t y) {
    return x >= FORMAT_QUAD_X0 && x < FORMAT_QUAD_X1 && y >= FORMAT_QUAD_Y0 &&
           y < FORMAT_QUAD_Y1;
}
